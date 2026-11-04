// ============================================================================
// src/ext/stream_cache/stream_record.cpp -- ext.stream_cache: records and bundles
// ============================================================================
// NOT RETAIL (issue #13, docs/extensions/handins/STREAMCACHE.md). The contract is in the header.
// ============================================================================
#include "ext/stream_cache/stream_record.h"

#include <algorithm>
#include <cstring>

#include "ext/derived_cache/hash64.h"

namespace Ext {
namespace StreamCache {

namespace {

void PutU32(std::vector<uint8_t>& v, uint32_t x) {
    uint8_t b[4];
    std::memcpy(b, &x, 4);
    v.insert(v.end(), b, b + 4);
}

void PutU64(std::vector<uint8_t>& v, uint64_t x) {
    uint8_t b[8];
    std::memcpy(b, &x, 8);
    v.insert(v.end(), b, b + 8);
}

uint32_t GetU32(const uint8_t* p) {
    uint32_t x;
    std::memcpy(&x, p, 4);
    return x;
}

uint64_t GetU64(const uint8_t* p) {
    uint64_t x;
    std::memcpy(&x, p, 8);
    return x;
}

// A record's bytes in its chunk: the header, the directory, the segment table, the bytes padded to a
// multiple of 8.
size_t RecordBytes(const RecordView& v) {
    const size_t body = (static_cast<size_t>(v.byteCount) + 7) & ~static_cast<size_t>(7);
    return kRecordHeader + static_cast<size_t>(v.blockCount) * 8 + static_cast<size_t>(v.segCount) * 8 + body;
}

}  // namespace

bool SameEntry(const EntryId& a, const EntryId& b) {
    return a.offset == b.offset && a.size == b.size;
}

uint32_t DirectoryTotal(const uint8_t* head) {
    const uint32_t n = GetU32(head);
    if (n < 1 || n > 0x10000u) return 0;   // 2 GB of 32 KB blocks: no entry is larger (int32 sizes)
    uint16_t last = 0;
    std::memcpy(&last, head + 4 + static_cast<size_t>(n - 1) * 8 + 6, 2);
    return (n - 1) * kDirBlockBytes + last;
}

uint64_t PackageIdentity(const PackageStamp& s) {
    struct Head {
        uint32_t crc, version, userVersion, fileCount;
        int64_t  fileSize;
        uint64_t writeTime;
    } head{s.crc, s.version, s.userVersion, s.fileCount, s.fileSize, s.writeTime};
    uint64_t h = DerivedCache::Hash64(&head, sizeof(head));
    if (s.name != nullptr) h = DerivedCache::Hash64(s.name, std::strlen(s.name), h);
    if (!s.table.empty()) h = DerivedCache::Hash64(s.table.data(), s.table.size() * sizeof(uint32_t), h);
    return h;
}

// ---- RecordView ------------------------------------------------------------------------------

bool RecordView::Read(uint32_t pos, uint32_t n, uint8_t* dst) const {
    uint32_t at = 0;   // the segment's first byte in `bytes`
    for (uint32_t i = 0; i < segCount; ++i) {
        const Segment s = segs[i];
        if (pos >= s.pos && pos - s.pos <= s.len && n <= s.len - (pos - s.pos)) {
            std::memcpy(dst, bytes + at + (pos - s.pos), n);
            return true;
        }
        at += s.len;
    }
    return false;
}

// ---- Recorder --------------------------------------------------------------------------------

void Recorder::Begin(const EntryId& id, uint32_t total, const uint8_t* head) {
    _id       = id;
    _total    = total;
    _consumed = 0;
    _active   = true;
    _bad      = false;
    _overflow = false;
    _segs.clear();
    const uint32_t n = GetU32(head);
    _head.assign(head, head + 4 + static_cast<size_t>(n) * 8);
}

void Recorder::Seed(const RecordView& view) {
    Begin(view.id, view.total, view.head);
    uint32_t at = 0;
    for (uint32_t i = 0; i < view.segCount; ++i) {
        const Segment s = view.segs[i];
        Seg seg;
        seg.pos = s.pos;
        seg.bytes.assign(view.bytes + at, view.bytes + at + s.len);
        _segs.push_back(std::move(seg));
        _consumed += s.len;
        at += s.len;
    }
}

void Recorder::Note(uint32_t pos, uint32_t n, const uint8_t* bytes) {
    if (!_active || _bad || _overflow || n == 0) return;
    if (pos + n < pos) {
        _bad = true;
        return;
    }
    const uint32_t end = pos + n;
    // The first segment that ends at or after pos (adjacent counts: it merges).
    size_t i = 0;
    while (i < _segs.size() && _segs[i].pos + _segs[i].bytes.size() < pos) ++i;
    if (i == _segs.size() || _segs[i].pos > end) {
        // Between segments (or past the last): a new one.
        if (_consumed + n > kRecordMax) {
            _overflow = true;
            _segs.clear();
            return;
        }
        Seg seg;
        seg.pos = pos;
        seg.bytes.assign(bytes, bytes + n);
        _segs.insert(_segs.begin() + static_cast<std::ptrdiff_t>(i), std::move(seg));
        _consumed += n;
        return;
    }
    // Overlapping or touching segment i: the overlap must agree, the rest extends it.
    Seg& s = _segs[i];
    const uint32_t sEnd = s.pos + static_cast<uint32_t>(s.bytes.size());
    const uint32_t ovBegin = std::max(pos, s.pos);
    const uint32_t ovEnd   = std::min(end, sEnd);
    if (ovBegin < ovEnd &&
        std::memcmp(s.bytes.data() + (ovBegin - s.pos), bytes + (ovBegin - pos), ovEnd - ovBegin) != 0) {
        _bad = true;
        return;
    }
    uint32_t added = 0;
    if (pos < s.pos) added += s.pos - pos;
    if (end > sEnd) added += end - sEnd;
    if (_consumed + added > kRecordMax) {
        _overflow = true;
        _segs.clear();
        return;
    }
    if (pos < s.pos) {
        s.bytes.insert(s.bytes.begin(), bytes, bytes + (s.pos - pos));
        s.pos = pos;
    }
    if (end > sEnd) {
        s.bytes.insert(s.bytes.end(), bytes + (sEnd - pos), bytes + n);
    }
    _consumed += added;
    // The widened segment may now reach the following ones: fold them in (their bytes agree by
    // the same rule, checked over the overlap).
    while (i + 1 < _segs.size() && _segs[i + 1].pos <= s.pos + s.bytes.size()) {
        Seg& next = _segs[i + 1];
        const uint32_t curEnd = s.pos + static_cast<uint32_t>(s.bytes.size());
        const uint32_t nextEnd = next.pos + static_cast<uint32_t>(next.bytes.size());
        const uint32_t ov = std::min(curEnd, nextEnd) - next.pos;
        if (ov > 0 && std::memcmp(s.bytes.data() + (next.pos - s.pos), next.bytes.data(), ov) != 0) {
            _bad = true;
            return;
        }
        if (nextEnd > curEnd) {
            s.bytes.insert(s.bytes.end(), next.bytes.begin() + ov, next.bytes.end());
        }
        _consumed -= ov;   // the overlap was counted twice
        _segs.erase(_segs.begin() + static_cast<std::ptrdiff_t>(i) + 1);
    }
}

void Recorder::Fail() {
    _bad = true;
}

bool Recorder::Finish(std::vector<uint8_t>& chunk) {
    const bool ok = Storable() && !_segs.empty();
    if (ok) {
        PutU64(chunk, _id.offset);
        PutU32(chunk, _id.size);
        PutU32(chunk, _total);
        PutU32(chunk, static_cast<uint32_t>(_segs.size()));
        chunk.insert(chunk.end(), _head.begin(), _head.end());   // block count + directory
        for (const Seg& s : _segs) {
            PutU32(chunk, s.pos);
            PutU32(chunk, static_cast<uint32_t>(s.bytes.size()));
        }
        for (const Seg& s : _segs) chunk.insert(chunk.end(), s.bytes.begin(), s.bytes.end());
        while (chunk.size() % 8 != 0) chunk.push_back(0);
    }
    _active = false;
    _segs.clear();
    return ok;
}

// ---- chunks ----------------------------------------------------------------------------------

void BeginChunk(std::vector<uint8_t>& chunk) {
    chunk.clear();
    PutU32(chunk, kChunkMagic);
    PutU32(chunk, kChunkVersion);
    PutU32(chunk, 0);
    PutU32(chunk, 0);
}

void EndChunk(std::vector<uint8_t>& chunk, uint32_t count) {
    if (chunk.size() < kChunkHeader) return;
    std::memcpy(chunk.data() + 8, &count, 4);
}

uint32_t ChunkRecordCount(const std::vector<uint8_t>& chunk) {
    if (chunk.size() < kChunkHeader) return 0;
    if (GetU32(chunk.data()) != kChunkMagic || GetU32(chunk.data() + 4) != kChunkVersion) return 0;
    return GetU32(chunk.data() + 8);
}

// ---- Bundle ----------------------------------------------------------------------------------

bool Bundle::AddChunk(std::vector<uint8_t> chunk) {
    const uint32_t count = ChunkRecordCount(chunk);
    if (count == 0) return false;
    if (chunk.size() % 8 != 0) return false;
    // No record is smaller than its header: a count the chunk cannot hold is refused before the
    // reserve, which would otherwise ask for 72 bytes per claimed record (309 GB for 0xFFFFFFFF).
    if (count > (chunk.size() - kChunkHeader) / kRecordHeader) return false;
    std::vector<std::pair<uint64_t, RecordView>> parsed;
    parsed.reserve(count);
    const uint8_t* const base = chunk.data();
    const size_t size = chunk.size();
    size_t at = kChunkHeader;
    for (uint32_t i = 0; i < count; ++i) {
        if (at + kRecordHeader > size) return false;
        RecordView v;
        v.id.offset  = GetU64(base + at);
        v.id.size    = GetU32(base + at + 8);
        v.total      = GetU32(base + at + 12);
        v.segCount   = GetU32(base + at + 16);
        v.blockCount = GetU32(base + at + 20);
        v.head       = base + at + 20;
        at += kRecordHeader;
        // The directory: at least one block, inside the entry, and describing the recorded total.
        if (v.blockCount == 0 || 4 + static_cast<uint64_t>(v.blockCount) * 8 > v.id.size) return false;
        if (at + static_cast<size_t>(v.blockCount) * 8 > size) return false;
        if (DirectoryTotal(v.head) != v.total) return false;
        // Hostile-cache hardening (review F-234). A full 0x10000th block makes the total 0x80000000 or
        // more; SegmentStream's int32 position cannot reach the end of such a stream without signed
        // overflow, and no package stream is that long (CompressedStream sizes are int32).
        if (v.total > 0x7FFFFFFFu) return false;
        at += static_cast<size_t>(v.blockCount) * 8;
        if (v.segCount == 0 || v.segCount > kRecordMax) return false;
        if (at + static_cast<size_t>(v.segCount) * 8 > size) return false;
        v.segs = reinterpret_cast<const Segment*>(base + at);   // CAST-OK: byte_io (8-byte aligned)
        at += static_cast<size_t>(v.segCount) * 8;
        uint64_t total = 0;
        uint64_t last  = 0;
        for (uint32_t s = 0; s < v.segCount; ++s) {
            const Segment seg = v.segs[s];
            if (seg.len == 0 || (s > 0 && seg.pos < last) || static_cast<uint64_t>(seg.pos) + seg.len > v.total) return false;
            last = static_cast<uint64_t>(seg.pos) + seg.len;
            total += seg.len;
        }
        if (total > kRecordMax) return false;
        v.byteCount = static_cast<uint32_t>(total);
        v.bytes = base + at;
        at += total;
        at = (at + 7) & ~static_cast<size_t>(7);
        if (at > size) return false;
        parsed.emplace_back(v.id.offset, v);
    }
    if (at != size) return false;
    _bytes += size;
    _chunks.push_back(std::move(chunk));
    for (auto& p : parsed) {
        _index[p.first] = p.second;
    }
    return true;
}

const RecordView* Bundle::Find(const EntryId& id) const {
    const auto it = _index.find(id.offset);
    if (it == _index.end()) return nullptr;
    if (!SameEntry(it->second.id, id)) return nullptr;
    return &it->second;
}

void Bundle::Clear() {
    _index.clear();
    _chunks.clear();
    _bytes = 0;
}

size_t Bundle::LiveBytes() const {
    size_t n = 0;
    for (const auto& kv : _index) n += RecordBytes(kv.second);
    return n;
}

std::vector<uint8_t> Bundle::Consolidate() const {
    std::vector<uint8_t> chunk;
    if (_index.empty()) return chunk;
    std::vector<const RecordView*> live;
    live.reserve(_index.size());
    for (const auto& kv : _index) live.push_back(&kv.second);
    std::sort(live.begin(), live.end(),
              [](const RecordView* a, const RecordView* b) { return a->id.offset < b->id.offset; });
    chunk.reserve(kChunkHeader + LiveBytes());
    BeginChunk(chunk);
    for (const RecordView* v : live) {
        // A record is contiguous in its chunk, from its header on.
        const uint8_t* const begin = v->Begin();
        chunk.insert(chunk.end(), begin, begin + RecordBytes(*v));
    }
    EndChunk(chunk, static_cast<uint32_t>(live.size()));
    return chunk;
}

// ---- generations -----------------------------------------------------------------------------

bool ShouldConsolidate(const Bundle& b) {
    if (b.Chunks() < 2 || b.Records() == 0) return false;
    if (b.Chunks() > kConsolidateChunks) return true;
    const size_t live = kChunkHeader + b.LiveBytes();
    const size_t dead = b.Bytes() > live ? b.Bytes() - live : 0;
    return dead * 4 >= b.Bytes();
}

std::vector<uint8_t> EncodeMarker(uint32_t start) {
    std::vector<uint8_t> m;
    PutU32(m, kMarkerMagic);
    PutU32(m, kMarkerVersion);
    PutU32(m, start);
    PutU32(m, ~start);
    return m;
}

bool DecodeMarker(const std::vector<uint8_t>& marker, uint32_t& start) {
    if (marker.size() != kMarkerBytes) return false;
    const uint8_t* const p = marker.data();
    if (GetU32(p) != kMarkerMagic || GetU32(p + 4) != kMarkerVersion) return false;
    if (GetU32(p + 8) != ~GetU32(p + 12)) return false;
    start = GetU32(p + 8);
    return true;
}

ReadBackResult ReadBack(Bundle& bundle, const FindFn& findChunk, const FindFn& findMarker) {
    ReadBackResult r;
    std::vector<uint8_t> buf;
    std::vector<uint32_t> starts;
    while (findMarker(r.markers, buf)) {
        uint32_t s = 0;
        if (DecodeMarker(buf, s)) starts.push_back(s);
        buf.clear();
        ++r.markers;
    }
    // The newest marker whose start chunk is stored; ordinal 0 when none is.
    std::vector<uint8_t> chunk;
    uint32_t ordinal = 0;
    bool found = false;
    for (size_t i = starts.size(); i > 0 && !found; --i) {
        ordinal = starts[i - 1];
        found = findChunk(ordinal, chunk);
        if (!found && i == starts.size()) r.startMissing = true;
    }
    if (!found) {
        ordinal = 0;
        found = findChunk(ordinal, chunk);
    }
    r.start = found ? ordinal : 0;
    while (found) {
        if (!bundle.AddChunk(std::move(chunk))) r.malformed.push_back(ordinal);
        chunk.clear();
        ++r.read;
        ++ordinal;
        found = findChunk(ordinal, chunk);
    }
    r.next = ordinal;
    return r;
}

}  // namespace StreamCache
}  // namespace Ext
