// Included inside VideoMetal (backend.mm): descriptor metadata only; never stores shader/game bytes.
//
// pipelines.txt (under the cache folder) records the pipeline descriptors a session needed, so the next launch can
// compile them on background threads before the first frame asks. File shape: a property list
// {version: kPipelineFileVersion, records: {descriptor-json: descriptor}}. It is read once on first use and written
// once, at shutdown (FlushPipelineRecords), never on the render thread when a pipeline is created. A file of another
// version, a file bigger than kPipelineFileMaxBytes or one holding more than kPipelineRecordCap records is not
// trusted: the extra records are dropped (stale ones first), the rest of the cache simply rebuilds.
constexpr int kPipelineFileVersion = 2;
constexpr unsigned long long kPipelineFileMaxBytes = 8ull * 1024 * 1024;
constexpr NSUInteger kPipelineRecordCap = 4096;

struct PipelineEntry {
    std::mutex mutex;
    std::condition_variable finished;
    id<MTLRenderPipelineState> state = nil;
    std::atomic<bool> started{false};
    bool done = false;   // the async compile's completion handler has run
};
static std::unordered_map<std::string, std::shared_ptr<PipelineEntry>> warmPipelines;
static std::recursive_mutex pipelineMetadataMutex;
static NSMutableDictionary* pipelineRecords;
static NSMutableDictionary<NSString*, id<MTLFunction>>* pipelineFunctions;
static bool pipelineRecordsDirty = false;
// Session counter persisted in the file: each record carries the number of the last session that used it ("seen"), so
// at the cap the record unseen for the longest goes first (a missing "seen" counts as 0, the oldest).
static unsigned long long pipelineSession = 1;

static unsigned long long PipelineRecordSeen(NSDictionary* record) {
    id seen = record[@"seen"];
    return [seen isKindOfClass:[NSNumber class]] ? [seen unsignedLongLongValue] : 0;
}

static NSString* PipelinePath() {
    NSString* home =
        @(ShaderCache::ResolveHome(Opt().userProfile ? Opt().userProfile : "", NSHomeDirectory().UTF8String).c_str());
    NSString* dir = [home stringByAppendingPathComponent:@"Library/Caches/OpenBA"];
    [[NSFileManager defaultManager] createDirectoryAtPath:dir withIntermediateDirectories:YES attributes:nil error:nil];
    return [dir stringByAppendingPathComponent:@"pipelines.txt"];
}

static bool PipelineRecordValid(NSString* key, id r) {
    if (![key isKindOfClass:[NSString class]] || ![r isKindOfClass:[NSDictionary class]]) {
        return false;
    }
    if (![r[@"vs"] isKindOfClass:[NSString class]] || ![r[@"ps"] isKindOfClass:[NSString class]]) {
        return false;
    }
    if (![r[@"attrs"] isKindOfClass:[NSArray class]] || ![r[@"layouts"] isKindOfClass:[NSArray class]] ||
        ![r[@"blend"] isKindOfClass:[NSArray class]]) {
        return false;
    }
    if ([r[@"attrs"] count] != 31 || [r[@"layouts"] count] != 31 || [r[@"blend"] count] != 8) {
        return false;
    }
    bool valid = true;
    for (NSString* field in @[ @"attrs", @"layouts" ]) {
        for (id row in r[field]) {
            if (![row isKindOfClass:[NSArray class]] || [row count] != 3) {
                valid = false;
                break;
            }
            for (id n in row) {
                valid &= [n isKindOfClass:[NSNumber class]];
            }
        }
    }
    for (id n in r[@"blend"]) {
        valid &= [n isKindOfClass:[NSNumber class]];
    }
    for (NSString* field in @[ @"color", @"depth", @"stencil", @"samples", @"coverage" ]) {
        valid &= [r[field] isKindOfClass:[NSNumber class]];
    }
    return valid;
}

static void LoadPipelineRecords() {
    if (pipelineRecords) {
        return;
    }
    pipelineRecords = [NSMutableDictionary new];
    pipelineFunctions = [NSMutableDictionary new];
    NSString* path = PipelinePath();
    NSDictionary* attributes = [[NSFileManager defaultManager] attributesOfItemAtPath:path error:nil];
    if (!attributes || [attributes fileSize] > kPipelineFileMaxBytes) {
        return;
    }
    NSDictionary* file = [NSDictionary dictionaryWithContentsOfFile:path];
    if (![file isKindOfClass:[NSDictionary class]] || ![file[@"version"] isEqual:@(kPipelineFileVersion)]) {
        return;
    }
    NSDictionary* records = file[@"records"];
    if (![records isKindOfClass:[NSDictionary class]]) {
        return;
    }
    if ([file[@"session"] isKindOfClass:[NSNumber class]]) {
        pipelineSession = [file[@"session"] unsignedLongLongValue] + 1;
    }
    for (id key in records) {
        if (pipelineRecords.count >= kPipelineRecordCap) {
            break;
        }
        if (PipelineRecordValid(key, records[key])) {
            pipelineRecords[key] = records[key];
        }
    }
}

// Writes the records once (shutdown). Over the cap, the records unseen for the most sessions go first.
static void FlushPipelineRecords() {
    std::lock_guard<std::recursive_mutex> guard(pipelineMetadataMutex);
    if (!pipelineRecords || !pipelineRecordsDirty) {
        return;
    }
    if (pipelineRecords.count > kPipelineRecordCap) {
        NSArray* oldestFirst = [[pipelineRecords allKeys] sortedArrayUsingComparator:^NSComparisonResult(id a, id b) {
          unsigned long long seenA = PipelineRecordSeen(pipelineRecords[a]);
          unsigned long long seenB = PipelineRecordSeen(pipelineRecords[b]);
          if (seenA == seenB) {
              return NSOrderedSame;
          }
          return seenA < seenB ? NSOrderedAscending : NSOrderedDescending;
        }];
        for (NSString* key in oldestFirst) {
            if (pipelineRecords.count <= kPipelineRecordCap) {
                break;
            }
            [pipelineRecords removeObjectForKey:key];
        }
    }
    NSDictionary* file = @{@"version" : @(kPipelineFileVersion),
                           @"session" : @(pipelineSession),
                           @"records" : pipelineRecords};
    [file writeToFile:PipelinePath() atomically:YES];
    pipelineRecordsDirty = false;
}

static NSDictionary* DescribePipeline(MTLRenderPipelineDescriptor* d, NSString* vs, NSString* ps) {
    NSMutableArray* attrs = [NSMutableArray new];
    NSMutableArray* layouts = [NSMutableArray new];
    for (unsigned i = 0; i < 31; ++i) {
        auto a = d.vertexDescriptor.attributes[i];
        [attrs addObject:@[ @(a.format), @(a.offset), @(a.bufferIndex) ]];
        auto l = d.vertexDescriptor.layouts[i];
        [layouts addObject:@[ @(l.stride), @(l.stepFunction), @(l.stepRate) ]];
    }
    auto c = d.colorAttachments[0];
    return @{
        @"vs" : vs,
        @"ps" : ps,
        @"attrs" : attrs,
        @"layouts" : layouts,
        @"color" : @(c.pixelFormat),
        @"depth" : @(d.depthAttachmentPixelFormat),
        @"stencil" : @(d.stencilAttachmentPixelFormat),
        @"samples" : @(d.rasterSampleCount),
        @"coverage" : @(d.alphaToCoverageEnabled),
        @"blend" : @[
            @(c.blendingEnabled), @(c.sourceRGBBlendFactor), @(c.destinationRGBBlendFactor), @(c.rgbBlendOperation),
            @(c.sourceAlphaBlendFactor), @(c.destinationAlphaBlendFactor), @(c.alphaBlendOperation), @(c.writeMask)
        ]
    };
}

static MTLRenderPipelineDescriptor* RestorePipeline(NSDictionary* r) {
    auto vs = pipelineFunctions[r[@"vs"]];
    auto ps = pipelineFunctions[r[@"ps"]];
    if (!vs || ([r[@"ps"] length] && !ps)) {
        return nil;
    }
    auto d = [MTLRenderPipelineDescriptor new];
    d.vertexFunction = vs;
    d.fragmentFunction = ps;
    d.vertexDescriptor = [MTLVertexDescriptor vertexDescriptor];
    for (unsigned i = 0; i < 31; ++i) {
        NSArray* a = r[@"attrs"][i];
        auto v = d.vertexDescriptor.attributes[i];
        v.format = (MTLVertexFormat)[a[0] unsignedIntegerValue];
        v.offset = [a[1] unsignedIntegerValue];
        v.bufferIndex = [a[2] unsignedIntegerValue];
        NSArray* l = r[@"layouts"][i];
        auto b = d.vertexDescriptor.layouts[i];
        b.stride = [l[0] unsignedIntegerValue];
        b.stepFunction = (MTLVertexStepFunction)[l[1] unsignedIntegerValue];
        b.stepRate = [l[2] unsignedIntegerValue];
    }
    auto c = d.colorAttachments[0];
    c.pixelFormat = (MTLPixelFormat)[r[@"color"] unsignedIntegerValue];
    d.depthAttachmentPixelFormat = (MTLPixelFormat)[r[@"depth"] unsignedIntegerValue];
    d.stencilAttachmentPixelFormat = (MTLPixelFormat)[r[@"stencil"] unsignedIntegerValue];
    d.rasterSampleCount = [r[@"samples"] unsignedIntegerValue];
    d.alphaToCoverageEnabled = [r[@"coverage"] boolValue];
    NSArray* b = r[@"blend"];
    c.blendingEnabled = [b[0] boolValue];
    c.sourceRGBBlendFactor = (MTLBlendFactor)[b[1] unsignedIntegerValue];
    c.destinationRGBBlendFactor = (MTLBlendFactor)[b[2] unsignedIntegerValue];
    c.rgbBlendOperation = (MTLBlendOperation)[b[3] unsignedIntegerValue];
    c.sourceAlphaBlendFactor = (MTLBlendFactor)[b[4] unsignedIntegerValue];
    c.destinationAlphaBlendFactor = (MTLBlendFactor)[b[5] unsignedIntegerValue];
    c.alphaBlendOperation = (MTLBlendOperation)[b[6] unsignedIntegerValue];
    c.writeMask = (MTLColorWriteMask)[b[7] unsignedIntegerValue];
    return d;
}

// Compiles `d` on a Metal worker thread. Only for replayed (warm-up) pipelines: a pipeline the frame needs NOW
// (`required`) is compiled once, synchronously, in CachedPipeline and is never also started here.
static void StartPipeline(const std::string& key, MTLRenderPipelineDescriptor* d) {
    auto& entry = warmPipelines[key];
    if (!entry) {
        entry = std::make_shared<PipelineEntry>();
    }
    if (entry->started.exchange(true)) {
        return;
    }
    auto retained = entry;
    [d.vertexFunction.device
        newRenderPipelineStateWithDescriptor:d
                           completionHandler:^(id<MTLRenderPipelineState> state, NSError* error) {
                             std::lock_guard<std::mutex> guard(retained->mutex);
                             retained->state = state;
                             retained->done = true;
                             retained->finished.notify_all();
                             if (error) {
                                 fprintf(stderr, "[Metal] async pipeline: %s\n", error.localizedDescription.UTF8String);
                             }
                           }];
}

void RegisterPipelineShader(const Shader& shader) {
    std::lock_guard<std::recursive_mutex> guard(pipelineMetadataMutex);
    if (!gpu || !shader.function || shader.identity.empty()) {
        return;
    }
    LoadPipelineRecords();
    pipelineFunctions[@(shader.identity.c_str())] = shader.function;
    // Replayed only when both functions exist. Completion handlers never touch the maps.
    for (NSString* key in pipelineRecords) {
        if (warmPipelines.count(key.UTF8String)) {
            continue;
        }
        auto d = RestorePipeline(pipelineRecords[key]);
        if (d) {
            StartPipeline(key.UTF8String, d);
        }
    }
}

// Adapter so the pure cap rule (retire_pool.h) can work on the NSDictionary of records.
struct PipelineRecordTable {
    NSMutableDictionary* records;
    size_t size() const {
        return records.count;
    }
    size_t erase(const std::string& key) {
        NSString* k = @(key.c_str());
        if (!records[k]) {
            return 0;
        }
        [records removeObjectForKey:k];
        return 1;
    }
};

// pipelineMetadataMutex held. Stamps the record with this session ("seen") the first time it is used in a session.
// At the cap the one record unseen for the longest is evicted, but only if it was not used this session; when every
// record was used this session the new record is not kept (it is simply recompiled next launch).
static void AddPipelineRecord(NSString* key, NSDictionary* record) {
    NSDictionary* existing = pipelineRecords[key];
    if (existing) {
        if (PipelineRecordSeen(existing) != pipelineSession) {
            NSMutableDictionary* stamped = [existing mutableCopy];
            stamped[@"seen"] = @(pipelineSession);
            pipelineRecords[key] = stamped;
            pipelineRecordsDirty = true;
        }
        return;
    }
    NSMutableDictionary* stamped = [record mutableCopy];
    stamped[@"seen"] = @(pipelineSession);
    record = stamped;
    if (pipelineRecords.count >= kPipelineRecordCap) {
        std::vector<std::string> candidates;
        for (NSString* existing in pipelineRecords) {
            candidates.push_back(existing.UTF8String);
        }
        PipelineRecordTable table{ pipelineRecords };
        unsigned long long oldest = pipelineSession;
        for (const auto& candidate : candidates) {
            unsigned long long seen = PipelineRecordSeen(pipelineRecords[@(candidate.c_str())]);
            oldest = std::min(oldest, seen);
        }
        auto stale = [&](const std::string& existing) {
            return PipelineRecordSeen(pipelineRecords[@(existing.c_str())]) == oldest && oldest < pipelineSession;
        };
        if (!MakeRoomForRecord(table, candidates, kPipelineRecordCap, stale)) {
            return;
        }
    }
    pipelineRecords[key] = record;
    pipelineRecordsDirty = true;   // written once, by FlushPipelineRecords at shutdown
}

static id<MTLRenderPipelineState> CachedPipeline(MTLRenderPipelineDescriptor* d, const Shader* vs, const Shader* ps,
                                                 bool required) {
    std::string nativeKey;
    std::shared_ptr<PipelineEntry> entry;
    {
        // The metadata lock covers the maps only; it is dropped before any wait or compile below, so loader threads
        // in RegisterPipelineShader / Translate never queue behind a compile.
        std::lock_guard<std::recursive_mutex> guard(pipelineMetadataMutex);
        LoadPipelineRecords();
        auto record = DescribePipeline(d, @(vs->identity.c_str()), ps ? @(ps->identity.c_str()) : @"");
        // JSON's sorted keys make the descriptor key stable across processes.
        NSData* json = [NSJSONSerialization dataWithJSONObject:record options:NSJSONWritingSortedKeys error:nil];
        NSString* key = [[NSString alloc] initWithData:json encoding:NSUTF8StringEncoding];
        AddPipelineRecord(key, record);
        nativeKey = key.UTF8String;
        auto& slot = warmPipelines[nativeKey];
        if (!slot) {
            slot = std::make_shared<PipelineEntry>();
        }
        entry = slot;
    }
    {
        std::unique_lock<std::mutex> entryGuard(entry->mutex);
        if (required && entry->started.load() && !entry->done) {
            // A warm-up compile for this very key is in flight: wait for it (bounded) instead of compiling twice.
            // The completion handler takes only this entry's mutex, so waiting here cannot deadlock.
            entry->finished.wait_for(entryGuard, std::chrono::seconds(10), [&] {
                return entry->done;
            });
        }
        if (entry->state) {
            return entry->state;
        }
    }
    if (!required) {
        std::lock_guard<std::recursive_mutex> guard(pipelineMetadataMutex);
        StartPipeline(nativeKey, d);
        return nil;
    }
    // The frame needs it now: compile it once, here. (Starting the async compile as well compiled a miss twice.)
    NSError* error = nil;
    double t = StatNow();
    auto state = [gpu newRenderPipelineStateWithDescriptor:d error:&error];
    statPipeline += StatNow() - t;
    ++statPipelinesNew;
    if (error) {
        fprintf(stderr, "[Metal] pipeline: %s\n", error.localizedDescription.UTF8String);
    }
    if (!state) {
        // A descriptor Metal rejects is not kept in pipelines.txt: the next launch would replay it and fail again.
        std::lock_guard<std::recursive_mutex> guard(pipelineMetadataMutex);
        [pipelineRecords removeObjectForKey:@(nativeKey.c_str())];
        pipelineRecordsDirty = true;
    }
    std::lock_guard<std::mutex> entryGuard(entry->mutex);
    entry->state = state;
    return state;
}
