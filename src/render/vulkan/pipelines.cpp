// ============================================================================
// src/render/vulkan/pipelines.cpp -- shader registry, vertex layouts, the pipeline cache and warm list
// ============================================================================
// NOT RETAIL. pipelines.h has the contract (binding model, registry, key, persistence).
// ============================================================================

#include "pipelines.h"
#include "device_objects.h"
#include "spirv_legalize.h"
#include "vulkan11.h"

#include "ext/derived_cache/derived_cache.h"   // CacheDir, OpenShared: the user's derived-data folder
#include "ext/derived_cache/hash64.h"
#include "ext/registry.h"                      // CurrentProfile: the faithful profile keeps no files
#include "platform_file.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace VideoVulkan {

namespace {

constexpr uint32_t kSetBindings = Shaders::kBindingsPerSet;
constexpr size_t kWarmRecordCap = 4096;
constexpr char kCacheMagic[8] = { 'O', 'B', 'A', 'V', 'K', 'P', 'C', '2' };   // 2 (P8): combined image samplers
constexpr const char* kWarmHeader = "OpenBA vulkan pipelines 1";

// ---- hashing helpers ----
uint64_t HashBytes(const void* data, size_t size, uint64_t seed = 0) {
    return Ext::DerivedCache::Hash64(data, size, seed);
}

struct KeyHash {
    size_t operator()(const PipelineKey& k) const {
        return static_cast<size_t>(HashBytes(&k, sizeof(PipelineKey)));
    }
};
struct KeyEqual {
    bool operator()(const PipelineKey& a, const PipelineKey& b) const {
        return std::memcmp(&a, &b, sizeof(PipelineKey)) == 0;
    }
};
struct PairHash {
    size_t operator()(const std::pair<uint64_t, uint64_t>& p) const {
        return static_cast<size_t>(p.first * 0x9E3779B97F4A7C15ull ^ p.second);
    }
};

// ---- the binding model ----
VkDescriptorSetLayout g_setLayouts[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };
VkPipelineLayout g_pipelineLayout = VK_NULL_HANDLE;
uint32_t g_dynamicCbs = 0;

// ---- P7: the push-descriptor path (pipelines.h) ----
struct StageMasks {
    uint32_t set = 0;
    uint32_t cb = 0;
    uint32_t tex = 0;
    uint32_t smp = 0;   // P8: the further pairs' bits k (BindingOfSampler(k)); no separate sampler is left
};
struct PairMasks {
    StageMasks vs;
    StageMasks ps;
};
struct MasksHash {
    size_t operator()(const PairMasks& m) const {
        return static_cast<size_t>(HashBytes(&m, sizeof(PairMasks)));
    }
};
struct MasksEqual {
    bool operator()(const PairMasks& a, const PairMasks& b) const {
        return std::memcmp(&a, &b, sizeof(PairMasks)) == 0;
    }
};
bool g_push = false;
std::mutex g_pushLock;
std::vector<std::pair<StageMasks, VkDescriptorSetLayout>> g_pushSetLayouts;
std::unordered_map<PairMasks, PairLayout, MasksHash, MasksEqual> g_pushLayouts;   // nodes: stable pointers

// ---- the registries ----
std::mutex g_shaderLock;
std::unordered_map<uint64_t, ShaderEntry*> g_shaders;
std::mutex g_layoutLock;
std::unordered_map<uint64_t, std::vector<LayoutElement>> g_layouts;
std::mutex g_linkLock;
std::unordered_map<std::pair<uint64_t, uint64_t>, VkShaderModule, PairHash> g_linked;

// ---- pipelines ----
VkPipelineCache g_cache = VK_NULL_HANDLE;
std::mutex g_pipelineLock;
std::unordered_map<PipelineKey, VkPipeline, KeyHash, KeyEqual> g_pipelines;   // VK_NULL_HANDLE = failed
std::atomic<uint32_t> g_built{0};
std::atomic<uint32_t> g_warmed{0};
std::atomic<uint32_t> g_failed{0};
std::atomic<uint32_t> g_buildLogLines{0};

// ---- the warm list ----
struct WarmRecord {
    PipelineKey key;
    std::vector<LayoutElement> elements;
    bool fromFile = false;
    bool queued = false;
};
std::mutex g_warmLock;
std::condition_variable g_warmCv;
std::vector<WarmRecord> g_records;                                        // file records + this run's
std::unordered_set<PipelineKey, KeyHash, KeyEqual> g_recordKeys;
std::unordered_multimap<uint64_t, size_t> g_pendingByShader;              // shader hash -> record index
std::deque<size_t> g_warmQueue;
std::thread g_warmThread;
bool g_warmStop = false;
bool g_recordsDirty = false;
bool g_persist = false;          // a cache folder exists and the profile is not faithful
std::string g_cacheDir;

// ---- files (the Platform seam: UTF-16 paths) ----
std::u16string ToPath(const std::string& utf8) {
    std::u16string out;
    size_t i = 0;
    while (i < utf8.size()) {
        const unsigned char c = static_cast<unsigned char>(utf8[i]);
        uint32_t cp = 0xFFFD;
        size_t extra = 0;
        if (c < 0x80) {
            cp = c;
        } else if ((c >> 5) == 0x6) {
            cp = c & 0x1F;
            extra = 1;
        } else if ((c >> 4) == 0xE) {
            cp = c & 0x0F;
            extra = 2;
        } else if ((c >> 3) == 0x1E) {
            cp = c & 0x07;
            extra = 3;
        }
        if (extra != 0 && i + extra >= utf8.size()) break;   // a truncated sequence ends the string
        for (size_t k = 1; k <= extra; ++k)
            cp = (cp << 6) | (static_cast<unsigned char>(utf8[i + k]) & 0x3F);
        i += extra + 1;
        if (cp >= 0x10000) {
            cp -= 0x10000;
            out.push_back(static_cast<char16_t>(0xD800 + (cp >> 10)));
            out.push_back(static_cast<char16_t>(0xDC00 + (cp & 0x3FF)));
        } else {
            out.push_back(static_cast<char16_t>(cp));
        }
    }
    return out;
}

bool ReadWhole(const std::string& path, std::vector<uint8_t>* out) {
    const std::u16string p = ToPath(path);
    Platform::FileHandle h = Platform::FileOpenReadShared(p.c_str());
    if (h == Platform::kInvalidFileHandle || h == nullptr) return false;
    const int64_t size = Platform::FileSize64(h);
    bool ok = size >= 0 && size < (int64_t{1} << 30);
    if (ok) {
        out->resize(static_cast<size_t>(size));
        uint32_t got = 0;
        ok = size == 0 || (Platform::FileRead(h, out->data(), static_cast<uint32_t>(size), &got) != 0 &&
                           got == static_cast<uint32_t>(size));
    }
    Platform::FileClose(h);
    return ok;
}

// Writes `<path>.tmp`, then replaces `path` with it: a crash leaves the old file or the new one.
bool WriteWhole(const std::string& path, const void* data, size_t size) {
    const std::u16string p = ToPath(path);
    const std::u16string tmp = p + u".tmp";
    Platform::FileHandle h = Platform::FileCreateWrite(tmp.c_str());
    if (h == Platform::kInvalidFileHandle || h == nullptr) return false;
    uint32_t wrote = 0;
    bool ok = size == 0 || (Platform::FileWrite(h, data, static_cast<uint32_t>(size), &wrote) != 0 &&
                            wrote == static_cast<uint32_t>(size));
    ok = ok && Platform::FileFlush(h) != 0;
    Platform::FileClose(h);
    ok = ok && Platform::FileReplace(tmp.c_str(), p.c_str()) != 0;
    if (!ok) Platform::FileDelete(tmp.c_str());
    return ok;
}

// ---- the persistent VkPipelineCache ----
// File: CacheHeader, then the driver's blob. Every field is checked before the blob reaches the
// driver (which checks its own header too): another GPU or driver, a torn write, a changed blob.
struct CacheHeader {
    char magic[8];
    uint32_t vendorID;
    uint32_t deviceID;
    uint32_t driverVersion;
    uint32_t reserved;
    uint8_t uuid[VK_UUID_SIZE];
    uint64_t dataSize;
    uint64_t dataHash;
};
static_assert(sizeof(CacheHeader) == 56, "the pipeline-cache file header is 56 bytes");

std::string CachePath() {
    return g_cacheDir + "\\vulkan_pipelines.bin";
}

std::string WarmPath() {
    return g_cacheDir + "\\vulkan_pipelines.txt";
}

void FillHeader(CacheHeader* h) {
    std::memset(h, 0, sizeof(*h));
    std::memcpy(h->magic, kCacheMagic, sizeof(kCacheMagic));
    const VkPhysicalDeviceProperties& p = LiveProperties();
    h->vendorID = p.vendorID;
    h->deviceID = p.deviceID;
    h->driverVersion = p.driverVersion;
    std::memcpy(h->uuid, p.pipelineCacheUUID, VK_UUID_SIZE);
}

std::vector<uint8_t> LoadCacheBlob() {
    std::vector<uint8_t> file;
    if (!g_persist || !ReadWhole(CachePath(), &file) || file.size() < sizeof(CacheHeader)) return {};
    CacheHeader want;
    FillHeader(&want);
    CacheHeader got;
    std::memcpy(&got, file.data(), sizeof(got));
    const bool same = std::memcmp(got.magic, want.magic, sizeof(want.magic)) == 0 && got.vendorID == want.vendorID &&
                      got.deviceID == want.deviceID && got.driverVersion == want.driverVersion &&
                      std::memcmp(got.uuid, want.uuid, VK_UUID_SIZE) == 0 &&
                      got.dataSize == file.size() - sizeof(CacheHeader);
    if (!same) {
        std::fprintf(stderr, "[Vulkan] pipeline cache: the stored cache is another device's or driver's; starting empty\n");
        return {};
    }
    const uint8_t* blob = file.data() + sizeof(CacheHeader);
    if (HashBytes(blob, static_cast<size_t>(got.dataSize)) != got.dataHash) {
        std::fprintf(stderr, "[Vulkan] pipeline cache: the stored cache fails its hash; starting empty\n");
        return {};
    }
    return std::vector<uint8_t>(blob, blob + got.dataSize);
}

void SaveCacheBlob() {
    if (!g_persist || !g_cache) return;
    // P7: the warm-up worker may add a pipeline between the size query and the copy (the background
    // write runs early in a session, while it still works); the copy then answers VK_INCOMPLETE. The
    // pair is retried with a fresh size; a write that still fails says so.
    std::vector<uint8_t> file;
    size_t size = 0;
    VkResult r = VK_INCOMPLETE;
    for (int attempt = 0; attempt < 4 && r == VK_INCOMPLETE; ++attempt) {
        size = 0;
        r = vkGetPipelineCacheData(LiveDevice(), g_cache, &size, nullptr);
        if (r != VK_SUCCESS) break;
        if (size == 0) return;
        size += size / 8 + 4096;   // room for what the worker adds meanwhile
        file.assign(sizeof(CacheHeader) + size, 0);
        r = vkGetPipelineCacheData(LiveDevice(), g_cache, &size, file.data() + sizeof(CacheHeader));
    }
    if (r != VK_SUCCESS) {
        std::fprintf(stderr, "[Vulkan] pipeline cache: not written (vkGetPipelineCacheData: %s)\n", ResultName(r));
        return;
    }
    file.resize(sizeof(CacheHeader) + size);
    CacheHeader h;
    FillHeader(&h);
    h.dataSize = size;
    h.dataHash = HashBytes(file.data() + sizeof(CacheHeader), size);
    std::memcpy(file.data(), &h, sizeof(h));
    if (!WriteWhole(CachePath(), file.data(), file.size())) {
        std::fprintf(stderr, "[Vulkan] pipeline cache: could not write %s\n", CachePath().c_str());
        return;
    }
    std::fprintf(stderr, "[Vulkan] pipeline cache: %zu bytes written\n", size);
}

// ---- the tables (dx11 renderstatedata.cpp / vertexlayoutdata.cpp, as Vulkan values) ----
const VkFormat kVertexFormat[9] = {
    VK_FORMAT_R32G32B32A32_SFLOAT,   // 0  DXGI_FORMAT_R32G32B32A32_FLOAT
    VK_FORMAT_R32G32B32_SFLOAT,      // 1  DXGI_FORMAT_R32G32B32_FLOAT
    VK_FORMAT_R32G32_SFLOAT,         // 2  DXGI_FORMAT_R32G32_FLOAT
    VK_FORMAT_R32_SFLOAT,            // 3  DXGI_FORMAT_R32_FLOAT
    VK_FORMAT_R16G16B16A16_SINT,     // 4  DXGI_FORMAT_R16G16B16A16_SINT
    VK_FORMAT_R16G16_SINT,           // 5  DXGI_FORMAT_R16G16_SINT
    VK_FORMAT_B8G8R8A8_UNORM,        // 6  DXGI_FORMAT_B8G8R8A8_UNORM
    VK_FORMAT_R8G8B8A8_UINT,         // 7  DXGI_FORMAT_R8G8B8A8_UINT
    VK_FORMAT_R8G8B8A8_UNORM,        // 8  DXGI_FORMAT_R8G8B8A8_UNORM
};

// P7: the attribute format of engine element format `format` for a shader input of numeric type
// `numeric` (Shaders::IoElement). Vulkan requires the two to agree (VUID-VkGraphicsPipelineCreateInfo-
// Input-08733); D3D11 does not check and reinterprets the bits. The one mismatch the retail shaders
// have is format 7 (the byte-4 element, R8G8B8A8_UINT) feeding an int4 input: it is read as
// R8G8B8A8_SINT, the same value for every byte below 128. A byte of 128 or more would read negative
// here and positive on D3D11 (docs/known_port_issues.md, "Vulkan backend": not seen, not ruled out).
VkFormat AttributeFormat(uint8_t format, uint8_t numeric) {
    if (format == 7 && numeric == Shaders::IoElement::kSint) return VK_FORMAT_R8G8B8A8_SINT;
    return kVertexFormat[format];
}

bool SameSemantic(const std::string& a, const char* b) {
    if (a.size() != std::strlen(b)) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        const char x = (a[i] >= 'a' && a[i] <= 'z') ? static_cast<char>(a[i] - 32) : a[i];
        if (x != b[i]) return false;
    }
    return true;
}

// The vertex input of (layout, vertex shader), exactly as D3D11 links an input layout whose element i
// is (TEXCOORD, i): every input the shader reads is fed by the element of its semantic index. A shader
// input no element feeds fails the pipeline, as CreateInputLayout fails on DX11 (nothing is drawn).
// `strides`: the key's (PipelineKey::strides), used on the 1.1 path only.
bool BuildVertexInput(const std::vector<LayoutElement>& elements, const Shaders::ShaderReflection& vs,
                      const uint8_t* strides,
                      std::vector<VkVertexInputBindingDescription>* bindings,
                      std::vector<VkVertexInputAttributeDescription>* attributes, std::string* why) {
    uint32_t streamMask = 0;
    for (const Shaders::IoElement& in : vs.inputs) {
        if (in.builtIn != Shaders::IoElement::kNoBuiltIn) continue;   // SV_VertexID / SV_InstanceID
        if (!SameSemantic(in.semantic, "TEXCOORD") || in.semanticIndex >= elements.size()) {
            *why = "the vertex shader reads " + in.semantic + std::to_string(in.semanticIndex) +
                   ", which the layout of " + std::to_string(elements.size()) + " element(s) does not feed";
            return false;
        }
        const LayoutElement& e = elements[in.semanticIndex];
        if (e.format >= 9) {
            *why = "a layout element has format " + std::to_string(e.format);
            return false;
        }
        VkVertexInputAttributeDescription a{};
        a.location = in.location;
        a.binding = e.stream;
        a.format = AttributeFormat(e.format, in.numeric);
        a.offset = e.offset;
        attributes->push_back(a);
        streamMask |= 1u << (e.stream & 31);
    }
    for (uint32_t s = 0; s < 32; ++s) {
        if (!(streamMask & (1u << s))) continue;
        VkVertexInputBindingDescription b{};
        b.binding = s;
        b.stride = 0;   // dynamic (VERTEX_INPUT_BINDING_STRIDE): the stride the engine binds
        if (Vulkan11Path() && s < sizeof(PipelineKey::strides)) b.stride = strides[s];
        b.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        bindings->push_back(b);
    }
    return true;
}

bool HasStencil(VkFormat f) {
    return f == VK_FORMAT_D24_UNORM_S8_UINT || f == VK_FORMAT_D32_SFLOAT_S8_UINT;
}

// The 1.1 path (spirv_legalize.h WidenInputs): the width of each plain vertex output at component 0,
// by Location; 0 where there is none.
void OutputWidths(const Shaders::ShaderReflection& vs, uint32_t widths[32]) {
    for (uint32_t i = 0; i < 32; ++i)
        widths[i] = 0;
    for (const Shaders::IoElement& out : vs.outputs) {
        const bool plain = out.builtIn == Shaders::IoElement::kNoBuiltIn && out.location < 32;
        if (!plain || !(out.componentMask & 1u)) continue;
        widths[out.location] = 32u - static_cast<uint32_t>(__builtin_clz(out.componentMask));
    }
}

// The 1.1 path: true when a vertex output is wider than the pixel input at its Location, which only
// maintenance4 allows: the pair then gets its own pixel module, with those inputs widened.
bool OutputsWider(const Shaders::ShaderReflection& vs, const Shaders::ShaderReflection& ps) {
    uint32_t widths[32];
    OutputWidths(vs, widths);
    for (const Shaders::IoElement& in : ps.inputs) {
        const bool plain = in.builtIn == Shaders::IoElement::kNoBuiltIn && in.location < 32;
        if (!plain || !(in.componentMask & 1u)) continue;
        if (32u - static_cast<uint32_t>(__builtin_clz(in.componentMask)) < widths[in.location]) return true;
    }
    return false;
}

// PipelineKey::linked for the pair: its interpolants do not link by register, or (the 1.1 path) a vertex
// output is wider than its pixel input.
uint8_t LinkedFlag(const ShaderEntry* vs, const ShaderEntry* ps) {
    if (!ps) return 0;
    if (!Shaders::StagesCompatible(vs->reflection, ps->reflection)) return 1;
    return (Vulkan11Path() && OutputsWider(vs->reflection, ps->reflection)) ? 1 : 0;
}

// The pair's linked pixel module (pipelines.h): converted once per (vertex, pixel) pair.
VkShaderModule LinkedPixelModule(const ShaderEntry* vs, const ShaderEntry* ps, std::string* why) {
    const auto pair = std::make_pair(vs->hash, ps->hash);
    {
        std::lock_guard<std::mutex> guard(g_linkLock);
        auto it = g_linked.find(pair);
        if (it != g_linked.end()) return it->second;
    }
    Shaders::ConvertOptions options;
    options.previousStage = &vs->reflection;
    options.combineSamplers = true;   // P8 (shader_convert.h)
    std::vector<uint32_t> spirv;
    Shaders::ShaderReflection refl;
    bool hit = false;
    if (!Shaders::LoadOrConvert(Ext::DerivedCache::OpenShared(), ps->dxbc.data(), ps->dxbc.size(),
                                Shaders::Stage::Pixel, &spirv, &refl, &hit, why, options)) {
        return VK_NULL_HANDLE;
    }
    uint32_t widened = 0;
    if (Vulkan11Path()) {
        uint32_t widths[32];
        OutputWidths(vs->reflection, widths);
        if (!Shaders::WidenInputs(&spirv, widths, &widened, why)) return VK_NULL_HANDLE;
    }
    VkShaderModule module = CreateShaderModule(spirv, why);
    if (!module) return VK_NULL_HANDLE;
    std::lock_guard<std::mutex> guard(g_linkLock);
    auto inserted = g_linked.emplace(pair, module);
    if (!inserted.second) {
        DestroyShaderModule(module);
        return inserted.first->second;
    }
    std::fprintf(stderr, "[Vulkan] linked pixel shader %016llx for vertex shader %016llx (%s)\n",
                 static_cast<unsigned long long>(ps->hash), static_cast<unsigned long long>(vs->hash),
                 hit ? "cache" : "converted");
    if (widened) {
        std::fprintf(stderr, "[Vulkan] 1.1 path: %u pixel input(s) widened to the vertex outputs\n", widened);
    }
    return module;
}

StageMasks MasksOf(uint32_t set, const Shaders::ShaderReflection* refl) {
    StageMasks m;
    m.set = set;
    if (refl) {
        m.cb = refl->constantBufferMask & ((1u << Shaders::kMaxConstantBuffers) - 1u);
        m.tex = refl->textureMask & ((1u << Shaders::kMaxTextures) - 1u);
        // P8: every game program is combined (shader_convert.h COMBINED IMAGE SAMPLERS): its texture
        // bindings are COMBINED_IMAGE_SAMPLER, and its further pairs sit in the sampler range.
        for (const Shaders::CombinedPair& p : refl->extraPairs)
            m.smp |= 1u << (p.binding - Shaders::BindingOfSampler(0));
    }
    return m;
}

uint32_t BindingCount(const StageMasks& m) {
    return static_cast<uint32_t>(__builtin_popcount(m.cb) + __builtin_popcount(m.tex) + __builtin_popcount(m.smp));
}

// g_pushLock held. The set layout of one stage's masks (made once): the pixel set is a push set with
// plain uniform buffers, the vertex set a pool set with dynamic ones (pipelines.h, THE PAIR LAYOUTS).
VkDescriptorSetLayout PairSetLayoutLocked(const StageMasks& m) {
    for (const auto& entry : g_pushSetLayouts) {
        if (std::memcmp(&entry.first, &m, sizeof(StageMasks)) == 0) return entry.second;
    }
    const bool vertex = m.set == Shaders::kSetVertex;
    const VkShaderStageFlags stage = vertex ? VK_SHADER_STAGE_VERTEX_BIT : VK_SHADER_STAGE_FRAGMENT_BIT;
    std::vector<VkDescriptorSetLayoutBinding> bindings;
    for (uint32_t r = 0; r < Shaders::kMaxConstantBuffers; ++r) {
        if (!((m.cb >> r) & 1)) continue;
        VkDescriptorSetLayoutBinding b{};
        b.binding = Shaders::BindingOfConstantBuffer(r);
        b.descriptorType = vertex ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        b.descriptorCount = 1;
        b.stageFlags = stage;
        bindings.push_back(b);
    }
    for (uint32_t r = 0; r < Shaders::kMaxTextures; ++r) {
        if ((m.tex >> r) & 1) {
            VkDescriptorSetLayoutBinding b{};
            b.binding = Shaders::BindingOfTexture(r);
            b.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            b.descriptorCount = 1;
            b.stageFlags = stage;
            bindings.push_back(b);
        }
        if ((m.smp >> r) & 1) {
            VkDescriptorSetLayoutBinding b{};
            b.binding = Shaders::BindingOfSampler(r);
            b.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            b.descriptorCount = 1;
            b.stageFlags = stage;
            bindings.push_back(b);
        }
    }
    auto li = Make<VkDescriptorSetLayoutCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
    li.flags = vertex ? 0 : VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
    li.bindingCount = static_cast<uint32_t>(bindings.size());
    li.pBindings = bindings.data();
    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    const VkResult r = vkCreateDescriptorSetLayout(LiveDevice(), &li, nullptr, &layout);
    if (r != VK_SUCCESS) {
        std::fprintf(stderr, "[Vulkan] pair set layout: %s\n", ResultName(r));
        return VK_NULL_HANDLE;
    }
    g_pushSetLayouts.emplace_back(m, layout);
    return layout;
}

// Builds the pipeline of `key` from its shaders. VK_NULL_HANDLE with *why on failure.
VkPipeline BuildPipeline(const PipelineKey& key, const ShaderEntry* vs, const ShaderEntry* ps, std::string* why) {
    std::vector<LayoutElement> elements;
    {
        std::lock_guard<std::mutex> guard(g_layoutLock);
        auto it = g_layouts.find(key.layout);
        if (key.layout != 0 && it == g_layouts.end()) {
            *why = "unknown vertex layout";
            return VK_NULL_HANDLE;
        }
        if (it != g_layouts.end()) elements = it->second;
    }
    std::vector<VkVertexInputBindingDescription> bindings;
    std::vector<VkVertexInputAttributeDescription> attributes;
    if (!BuildVertexInput(elements, vs->reflection, key.strides, &bindings, &attributes, why)) {
        return VK_NULL_HANDLE;
    }

    VkShaderModule psModule = VK_NULL_HANDLE;
    if (ps) {
        psModule = key.linked ? LinkedPixelModule(vs, ps, why) : ps->module;
        if (!psModule) return VK_NULL_HANDLE;
    }
    VkPipelineShaderStageCreateInfo stages[2];
    stages[0] = Make<VkPipelineShaderStageCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vs->module;
    stages[0].pName = "main";
    stages[1] = Make<VkPipelineShaderStageCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = psModule;
    stages[1].pName = "main";

    auto vi = Make<VkPipelineVertexInputStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO);
    vi.vertexBindingDescriptionCount = static_cast<uint32_t>(bindings.size());
    vi.pVertexBindingDescriptions = bindings.data();
    vi.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
    vi.pVertexAttributeDescriptions = attributes.data();
    auto ia = Make<VkPipelineInputAssemblyStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO);
    ia.topology = static_cast<VkPrimitiveTopology>(key.topology);
    auto vp = Make<VkPipelineViewportStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO);
    vp.viewportCount = 1;
    vp.scissorCount = 1;

    const StateBlock& s = key.state;
    auto rs = Make<VkPipelineRasterizationStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO);
    rs.depthClampEnable = VK_FALSE;   // DepthClipEnable TRUE (retail's hard 1)
    rs.rasterizerDiscardEnable = VK_FALSE;
    rs.polygonMode = (s.polygonMode == VK_POLYGON_MODE_LINE && !LiveCaps().fillModeNonSolid)
                         ? VK_POLYGON_MODE_FILL
                         : static_cast<VkPolygonMode>(s.polygonMode);
    rs.cullMode = s.cullMode;
    // D3D11 FrontCounterClockwise FALSE. The negative-height viewport keeps D3D's screen orientation,
    // so D3D's clockwise is Vulkan's clockwise in framebuffer space.
    rs.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rs.depthBiasEnable = (s.depthBias != 0 || s.slopeBias != 0.0f) ? VK_TRUE : VK_FALSE;
    rs.depthBiasConstantFactor = static_cast<float>(s.depthBias);
    rs.depthBiasClamp = 0.0f;
    rs.depthBiasSlopeFactor = s.slopeBias;
    rs.lineWidth = 1.0f;
    auto ms = Make<VkPipelineMultisampleStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO);
    ms.rasterizationSamples = static_cast<VkSampleCountFlagBits>(key.samples);
    // P4: ext.foliage_aa's A2C clone (pipelines.h StateBlock::extFlags); never set for a retail state.
    ms.alphaToCoverageEnable = (s.extFlags & kExtAlphaToCoverage) ? VK_TRUE : VK_FALSE;
    auto ds = Make<VkPipelineDepthStencilStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO);
    ds.depthTestEnable = s.depthTest;
    ds.depthWriteEnable = (s.depthTest && s.depthWrite) ? VK_TRUE : VK_FALSE;   // D3D: no test, no write
    ds.depthCompareOp = static_cast<VkCompareOp>(s.depthFunc);
    ds.stencilTestEnable = s.stencilEnable;
    ds.front.failOp = static_cast<VkStencilOp>(s.frontFail);
    ds.front.passOp = static_cast<VkStencilOp>(s.frontPass);
    ds.front.depthFailOp = static_cast<VkStencilOp>(s.frontDepthFail);
    ds.front.compareOp = static_cast<VkCompareOp>(s.frontFunc);
    ds.front.compareMask = 0xFF;
    ds.front.writeMask = 0xFF;
    ds.back.failOp = static_cast<VkStencilOp>(s.backFail);
    ds.back.passOp = static_cast<VkStencilOp>(s.backPass);
    ds.back.depthFailOp = static_cast<VkStencilOp>(s.backDepthFail);
    ds.back.compareOp = static_cast<VkCompareOp>(s.backFunc);
    ds.back.compareMask = 0xFF;
    ds.back.writeMask = 0xFF;
    ds.minDepthBounds = 0.0f;
    ds.maxDepthBounds = 1.0f;
    VkPipelineColorBlendAttachmentState blend{};
    blend.blendEnable = s.blendEnable;
    blend.srcColorBlendFactor = static_cast<VkBlendFactor>(s.srcBlend);
    blend.dstColorBlendFactor = static_cast<VkBlendFactor>(s.dstBlend);
    blend.colorBlendOp = static_cast<VkBlendOp>(s.blendOp);
    const bool alphaZero = (s.extFlags & kExtAlphaZero) != 0;   // P4: ext.foliage_aa's A2C clone
    blend.srcAlphaBlendFactor = alphaZero ? VK_BLEND_FACTOR_ZERO : static_cast<VkBlendFactor>(s.srcBlend);
    blend.dstAlphaBlendFactor = alphaZero ? VK_BLEND_FACTOR_ZERO : static_cast<VkBlendFactor>(s.dstBlend);
    blend.alphaBlendOp = static_cast<VkBlendOp>(s.blendOp);
    blend.colorWriteMask = ps ? s.writeMask : 0;   // no pixel shader: nothing reaches the colour target
    auto cb = Make<VkPipelineColorBlendStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO);
    // No colour attachment (the shadow map's depth-only pass): no blend state either.
    cb.attachmentCount = key.colorFormat != VK_FORMAT_UNDEFINED ? 1 : 0;
    cb.pAttachments = &blend;
    const VkDynamicState dynamics[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR,
                                        VK_DYNAMIC_STATE_STENCIL_REFERENCE,
                                        VK_DYNAMIC_STATE_VERTEX_INPUT_BINDING_STRIDE };
    auto dyn = Make<VkPipelineDynamicStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO);
    dyn.dynamicStateCount = static_cast<uint32_t>(sizeof(dynamics) / sizeof(dynamics[0]));
    if (Vulkan11Path()) dyn.dynamicStateCount -= 1;   // the strides are the binding descriptions'
    dyn.pDynamicStates = dynamics;
    const VkFormat color = static_cast<VkFormat>(key.colorFormat);
    const VkFormat depth = static_cast<VkFormat>(key.depthFormat);
    auto ri = Make<VkPipelineRenderingCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO);
    ri.colorAttachmentCount = color != VK_FORMAT_UNDEFINED ? 1 : 0;
    ri.pColorAttachmentFormats = &color;
    ri.depthAttachmentFormat = depth;
    ri.stencilAttachmentFormat = HasStencil(depth) ? depth : VK_FORMAT_UNDEFINED;
    // P3: the tiler's MSAA on single-sampled attachments (VK_EXT_multisampled_render_to_single_sampled)
    // is the pass's (draw.cpp BeginPass); the pipeline only rasterizes at key.samples. P7: the
    // VkMultisampledRenderToSingleSampledInfoEXT chained here before is not a pipeline structure (the
    // validation layer's "unexpected VkStructureType"); key.msrtss keeps those pipelines apart.

    auto pi = Make<VkGraphicsPipelineCreateInfo>(VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO);
    pi.stageCount = ps ? 2 : 1;
    pi.pStages = stages;
    pi.pVertexInputState = &vi;
    pi.pInputAssemblyState = &ia;
    pi.pViewportState = &vp;
    pi.pRasterizationState = &rs;
    pi.pMultisampleState = &ms;
    pi.pDepthStencilState = &ds;
    pi.pColorBlendState = &cb;
    pi.pDynamicState = &dyn;
    SetPipelineTargets(&pi, &ri);
    // P7: the pair's own layout on the push path; the game layout (sets from the frame's pools) when
    // push descriptors are off, or for a pair whose pixel stage declares more bindings than
    // maxPushDescriptors (PairLayoutFor answers null; the draw then takes the pool path).
    pi.layout = g_pipelineLayout;
    if (g_push) {
        const PairLayout* pair = PairLayoutFor(vs->reflection, ps ? &ps->reflection : nullptr);
        if (pair) pi.layout = pair->layout;
    }
    VkPipeline pipeline = VK_NULL_HANDLE;
    const VkResult r = vkCreateGraphicsPipelines(LiveDevice(), g_cache, 1, &pi, nullptr, &pipeline);
    if (r != VK_SUCCESS) {
        *why = std::string("vkCreateGraphicsPipelines: ") + ResultName(r);
        return VK_NULL_HANDLE;
    }
    return pipeline;
}

// Stores `pipeline` under `key` unless another thread stored one first; returns the stored one.
VkPipeline Publish(const PipelineKey& key, VkPipeline pipeline) {
    std::lock_guard<std::mutex> guard(g_pipelineLock);
    auto inserted = g_pipelines.emplace(key, pipeline);
    if (inserted.second) return pipeline;
    if (!inserted.first->second && pipeline) {
        inserted.first->second = pipeline;   // a key that failed once and built now
        return pipeline;
    }
    if (pipeline) vkDestroyPipeline(LiveDevice(), pipeline, nullptr);
    return inserted.first->second;
}

// ---- the warm list ----

std::string Hex(const void* data, size_t size) {
    static const char digits[] = "0123456789abcdef";
    std::string out;
    out.reserve(size * 2);
    const auto* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < size; ++i) {
        out.push_back(digits[p[i] >> 4]);
        out.push_back(digits[p[i] & 15]);
    }
    return out;
}

bool Unhex(const std::string& text, void* data, size_t size) {
    if (text.size() != size * 2) return false;
    auto* p = static_cast<uint8_t*>(data);
    for (size_t i = 0; i < size; ++i) {
        int v = 0;
        for (int k = 0; k < 2; ++k) {
            const char c = text[2 * i + k];
            int d = -1;
            if (c >= '0' && c <= '9') d = c - '0';
            if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
            if (d < 0) return false;
            v = v * 16 + d;
        }
        p[i] = static_cast<uint8_t>(v);
    }
    return true;
}

// g_warmLock held. Indexes a record not yet queued under its shaders, or queues it when both are here.
void IndexRecordLocked(size_t index) {
    WarmRecord& r = g_records[index];
    if (r.queued) return;
    bool vsHere = false;
    bool psHere = r.key.ps == 0;
    {
        std::lock_guard<std::mutex> guard(g_shaderLock);
        vsHere = g_shaders.count(r.key.vs) != 0;
        if (!psHere) psHere = g_shaders.count(r.key.ps) != 0;
    }
    if (vsHere && psHere) {
        r.queued = true;
        g_warmQueue.push_back(index);
        g_warmCv.notify_one();
        return;
    }
    g_pendingByShader.emplace(vsHere ? r.key.ps : r.key.vs, index);
}

void LoadWarmList() {
    std::vector<uint8_t> bytes;
    if (!g_persist || !ReadWhole(WarmPath(), &bytes)) return;
    std::istringstream in(std::string(bytes.begin(), bytes.end()));
    std::string line;
    if (!std::getline(in, line) || line.rfind(kWarmHeader, 0) != 0) return;
    size_t loaded = 0;
    while (std::getline(in, line) && g_records.size() < kWarmRecordCap) {
        std::istringstream f(line);
        std::string tag;
        std::string vs;
        std::string ps;
        std::string state;
        unsigned topology = 0;
        unsigned color = 0;
        unsigned depth = 0;
        unsigned samples = 0;
        unsigned linked = 0;
        size_t count = 0;
        f >> tag >> vs >> ps >> topology >> color >> depth >> samples >> linked >> state >> count;
        if (!f || tag != "P" || count > 64) continue;
        WarmRecord r;
        r.fromFile = true;
        bool ok = Unhex(vs, &r.key.vs, 8) && Unhex(ps, &r.key.ps, 8) && Unhex(state, &r.key.state, sizeof(StateBlock));
        r.key.topology = static_cast<uint8_t>(topology);
        r.key.colorFormat = color;
        r.key.depthFormat = depth;
        r.key.samples = static_cast<uint8_t>(samples);
        r.key.linked = static_cast<uint8_t>(linked);
        for (size_t i = 0; ok && i < count; ++i) {
            std::string e;
            f >> e;
            LayoutElement element;
            ok = static_cast<bool>(f) && Unhex(e, &element, sizeof(element));
            r.elements.push_back(element);
        }
        // The 1.1 path's strides (PipelineKey::strides), written only when one is set: "S" + 16 hex digits.
        std::string strides;
        if (ok && f >> strides) {
            ok = strides.size() == 17 && strides[0] == 'S' &&
                 Unhex(strides.substr(1), r.key.strides, sizeof(r.key.strides));
        }
        if (!ok) continue;
        r.key.layout = InternLayout(r.elements.data(), r.elements.size());
        if (!g_recordKeys.insert(r.key).second) continue;
        g_records.push_back(std::move(r));
        ++loaded;
    }
    std::fprintf(stderr, "[Vulkan] pipeline warm list: %zu record(s)\n", loaded);
}

void SaveWarmList() {
    std::lock_guard<std::mutex> guard(g_warmLock);
    if (!g_persist || !g_recordsDirty) return;
    std::string out = std::string(kWarmHeader) + "\n";
    for (const WarmRecord& r : g_records) {
        out += "P " + Hex(&r.key.vs, 8) + " " + Hex(&r.key.ps, 8) + " " + std::to_string(r.key.topology) + " " +
               std::to_string(r.key.colorFormat) + " " + std::to_string(r.key.depthFormat) + " " +
               std::to_string(r.key.samples) + " " + std::to_string(r.key.linked) + " " +
               Hex(&r.key.state, sizeof(StateBlock)) + " " + std::to_string(r.elements.size());
        for (const LayoutElement& e : r.elements)
            out += " " + Hex(&e, sizeof(e));
        const uint8_t noStrides[sizeof(r.key.strides)] = {};
        if (std::memcmp(r.key.strides, noStrides, sizeof(noStrides)) != 0) {
            out += " S" + Hex(r.key.strides, sizeof(r.key.strides));
        }
        out += "\n";
    }
    if (!WriteWhole(WarmPath(), out.data(), out.size())) {
        std::fprintf(stderr, "[Vulkan] pipeline warm list: could not write %s\n", WarmPath().c_str());
    }
}

// A key the render thread built: kept for the next start's warm-up (first come, up to the cap).
void RecordKey(const PipelineKey& key) {
    std::vector<LayoutElement> elements;
    {
        std::lock_guard<std::mutex> guard(g_layoutLock);
        auto it = g_layouts.find(key.layout);
        if (it != g_layouts.end()) elements = it->second;
    }
    std::lock_guard<std::mutex> guard(g_warmLock);
    if (!g_persist || g_records.size() >= kWarmRecordCap || !g_recordKeys.insert(key).second) return;
    WarmRecord r;
    r.key = key;
    r.elements = std::move(elements);
    r.queued = true;   // built already: never warmed this run
    g_records.push_back(std::move(r));
    g_recordsDirty = true;
}

void WarmMain() {
    for (;;) {
        PipelineKey key;
        {
            std::unique_lock<std::mutex> lock(g_warmLock);
            g_warmCv.wait(lock, [] {
                return g_warmStop || !g_warmQueue.empty();
            });
            if (g_warmStop) return;
            key = g_records[g_warmQueue.front()].key;
            g_warmQueue.pop_front();
        }
        {
            std::lock_guard<std::mutex> guard(g_pipelineLock);
            if (g_pipelines.count(key)) continue;
        }
        ShaderEntry* vs = AcquireShader(key.vs);
        ShaderEntry* ps = key.ps ? AcquireShader(key.ps) : nullptr;
        if (vs && (ps || key.ps == 0)) {
            // A record written on the 1.3 path does not know the 1.1 path's linked pairs.
            if (Vulkan11Path()) key.linked = LinkedFlag(vs, ps);
            std::string why;
            VkPipeline p = BuildPipeline(key, vs, ps, &why);
            if (p) {
                Publish(key, p);
                g_warmed.fetch_add(1, std::memory_order_relaxed);
            }
        }
        if (vs) ReleaseShader(vs);
        if (ps) ReleaseShader(ps);
    }
}

}  // namespace

// ---- lifecycle ------------------------------------------------------------------------------------

bool PipelinesStartup(std::string* why) {
    const uint32_t dynLimit = LiveProperties().limits.maxDescriptorSetUniformBuffersDynamic;
    g_dynamicCbs = std::min<uint32_t>(Shaders::kMaxConstantBuffers, dynLimit / 2);
    for (uint32_t set = 0; set < 2; ++set) {
        std::vector<VkDescriptorSetLayoutBinding> b(kSetBindings);
        const VkShaderStageFlags stage = set == Shaders::kSetVertex ? VK_SHADER_STAGE_VERTEX_BIT
                                                                    : VK_SHADER_STAGE_FRAGMENT_BIT;
        for (uint32_t i = 0; i < kSetBindings; ++i) {
            b[i] = VkDescriptorSetLayoutBinding{};
            b[i].binding = i;
            b[i].descriptorCount = 1;
            b[i].stageFlags = stage;
            if (i < Shaders::kMaxConstantBuffers) {
                b[i].descriptorType = i < g_dynamicCbs ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC
                                                       : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            } else {
                // P8: textures and the further pairs in the sampler range are combined image samplers.
                b[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            }
        }
        auto li = Make<VkDescriptorSetLayoutCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
        li.bindingCount = kSetBindings;
        li.pBindings = b.data();
        const VkResult r = vkCreateDescriptorSetLayout(LiveDevice(), &li, nullptr, &g_setLayouts[set]);
        if (r != VK_SUCCESS) {
            *why = std::string("vkCreateDescriptorSetLayout: ") + ResultName(r);
            return false;
        }
    }
    auto pl = Make<VkPipelineLayoutCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
    pl.setLayoutCount = 2;
    pl.pSetLayouts = g_setLayouts;
    VkResult r = vkCreatePipelineLayout(LiveDevice(), &pl, nullptr, &g_pipelineLayout);
    if (r != VK_SUCCESS) {
        *why = std::string("vkCreatePipelineLayout: ") + ResultName(r);
        return false;
    }

    // P7: the push-descriptor path (pipelines.h) wherever the extension is (its spec floor is 32).
    g_push = LiveCaps().pushDescriptor && vkCmdPushDescriptorSetKHR != nullptr &&
             LiveCaps().maxPushDescriptors >= 32;

    g_persist = Ext::CurrentProfile() != Ext::Profile::Faithful && Ext::DerivedCache::CacheDir(g_cacheDir) &&
                Platform::EnsureDirChain(ToPath(g_cacheDir).c_str());
    const std::vector<uint8_t> blob = LoadCacheBlob();
    auto ci = Make<VkPipelineCacheCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO);
    ci.initialDataSize = blob.size();
    ci.pInitialData = blob.empty() ? nullptr : blob.data();
    r = vkCreatePipelineCache(LiveDevice(), &ci, nullptr, &g_cache);
    if (r != VK_SUCCESS && !blob.empty()) {
        ci.initialDataSize = 0;
        ci.pInitialData = nullptr;
        r = vkCreatePipelineCache(LiveDevice(), &ci, nullptr, &g_cache);
    }
    if (r != VK_SUCCESS) {
        g_cache = VK_NULL_HANDLE;
        std::fprintf(stderr, "[Vulkan] vkCreatePipelineCache: %s (pipelines are built without a cache)\n", ResultName(r));
    }
    {
        std::lock_guard<std::mutex> guard(g_warmLock);
        g_records.clear();
        g_recordKeys.clear();
        g_pendingByShader.clear();
        g_warmQueue.clear();
        g_recordsDirty = false;
        g_warmStop = false;
        LoadWarmList();
        for (size_t i = 0; i < g_records.size(); ++i)
            IndexRecordLocked(i);
    }
    g_warmThread = std::thread(WarmMain);
    std::fprintf(stderr, "[Vulkan] pipelines: layout set0/set1 x %u bindings, %u dynamic constant buffer(s) per set, "
                         "cache %zu bytes loaded%s; descriptors %s\n",
                 kSetBindings, g_dynamicCbs, blob.size(), g_persist ? "" : " (no cache folder: nothing persists)",
                 g_push ? "a layout per shader pair (pixel set pushed, vertex set pooled)" : "sets from per-frame pools");
    return true;
}

void PipelinesPersist() {
    SaveWarmList();
    SaveCacheBlob();
}

void PipelinesShutdown() {
    {
        std::lock_guard<std::mutex> guard(g_warmLock);
        g_warmStop = true;
    }
    g_warmCv.notify_all();
    if (g_warmThread.joinable()) g_warmThread.join();
    SaveWarmList();
    SaveCacheBlob();
    {
        std::lock_guard<std::mutex> guard(g_pipelineLock);
        for (auto& kv : g_pipelines) {
            if (kv.second) vkDestroyPipeline(LiveDevice(), kv.second, nullptr);
        }
        g_pipelines.clear();
    }
    {
        std::lock_guard<std::mutex> guard(g_linkLock);
        for (auto& kv : g_linked)
            DestroyShaderModule(kv.second);
        g_linked.clear();
    }
    if (g_cache) vkDestroyPipelineCache(LiveDevice(), g_cache, nullptr);
    g_cache = VK_NULL_HANDLE;
    if (g_pipelineLayout) vkDestroyPipelineLayout(LiveDevice(), g_pipelineLayout, nullptr);
    g_pipelineLayout = VK_NULL_HANDLE;
    {
        std::lock_guard<std::mutex> guard(g_pushLock);
        for (auto& kv : g_pushLayouts) {
            if (kv.second.layout) vkDestroyPipelineLayout(LiveDevice(), kv.second.layout, nullptr);
        }
        g_pushLayouts.clear();
        for (auto& entry : g_pushSetLayouts)
            vkDestroyDescriptorSetLayout(LiveDevice(), entry.second, nullptr);
        g_pushSetLayouts.clear();
    }
    g_push = false;
    for (VkDescriptorSetLayout& l : g_setLayouts) {
        if (l) vkDestroyDescriptorSetLayout(LiveDevice(), l, nullptr);
        l = VK_NULL_HANDLE;
    }
    std::lock_guard<std::mutex> guard(g_warmLock);
    g_records.clear();
    g_recordKeys.clear();
    g_pendingByShader.clear();
    g_warmQueue.clear();
}

// ---- the binding model ------------------------------------------------------------------------------

VkPipelineCache GamePipelineCache() {
    return g_cache;
}

VkPipelineLayout GamePipelineLayout() {
    return g_pipelineLayout;
}

VkDescriptorSetLayout StageSetLayout(Shaders::Stage stage) {
    return g_setLayouts[Shaders::SetOfStage(stage)];
}

uint32_t DynamicConstantBuffers() {
    return g_dynamicCbs;
}

bool PushDescriptors() {
    return g_push;
}

const PairLayout* PairLayoutFor(const Shaders::ShaderReflection& vs, const Shaders::ShaderReflection* ps) {
    PairMasks key;
    key.vs = MasksOf(Shaders::kSetVertex, &vs);
    key.ps = MasksOf(Shaders::kSetPixel, ps);
    std::lock_guard<std::mutex> guard(g_pushLock);
    auto it = g_pushLayouts.find(key);
    if (it != g_pushLayouts.end()) return it->second.layout ? &it->second : nullptr;
    PairLayout pair;
    if (BindingCount(key.ps) <= LiveCaps().maxPushDescriptors) {
        VkDescriptorSetLayout sets[2] = { PairSetLayoutLocked(key.vs), PairSetLayoutLocked(key.ps) };
        if (sets[0] && sets[1]) {
            auto pl = Make<VkPipelineLayoutCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
            pl.setLayoutCount = 2;
            pl.pSetLayouts = sets;
            const VkResult r = vkCreatePipelineLayout(LiveDevice(), &pl, nullptr, &pair.layout);
            if (r != VK_SUCCESS) {
                std::fprintf(stderr, "[Vulkan] pair pipeline layout: %s\n", ResultName(r));
                pair.layout = VK_NULL_HANDLE;
            }
        }
        pair.vertexSet = sets[0];
        pair.vertexCbs = static_cast<uint32_t>(__builtin_popcount(key.vs.cb));
        pair.vertexBindings = BindingCount(key.vs);
    }
    if (!pair.layout) {
        std::fprintf(stderr,
                     "[Vulkan] pair layout: pixel stage with %u bindings (maxPushDescriptors %u): this pair "
                     "takes the pool path (sets from the frame's pools, the game layout)\n",
                     BindingCount(key.ps), LiveCaps().maxPushDescriptors);
    }
    auto inserted = g_pushLayouts.emplace(key, pair);
    return inserted.first->second.layout ? &inserted.first->second : nullptr;
}

// ---- the shader registry -----------------------------------------------------------------------------

uint64_t ShaderHash(const uint8_t* dxbc, size_t size, Shaders::Stage stage) {
    return HashBytes(dxbc, size, 0x5653 + static_cast<uint64_t>(stage));
}

ShaderEntry* AcquireShader(uint64_t hash) {
    std::lock_guard<std::mutex> guard(g_shaderLock);
    auto it = g_shaders.find(hash);
    if (it == g_shaders.end()) return nullptr;
    ++it->second->refs;
    return it->second;
}

ShaderEntry* AddShader(uint64_t hash, Shaders::Stage stage, VkShaderModule module,
                       const Shaders::ShaderReflection& reflection, const uint8_t* dxbc, size_t size) {
    ShaderEntry* entry = nullptr;
    {
        std::lock_guard<std::mutex> guard(g_shaderLock);
        auto it = g_shaders.find(hash);
        if (it != g_shaders.end()) {
            ++it->second->refs;
            entry = it->second;
        } else {
            entry = new ShaderEntry();
            entry->hash = hash;
            entry->stage = stage;
            entry->module = module;
            entry->reflection = reflection;
            if (stage == Shaders::Stage::Pixel) entry->dxbc.assign(dxbc, dxbc + size);
            entry->refs = 1;
            g_shaders.emplace(hash, entry);
            module = VK_NULL_HANDLE;
        }
    }
    if (module) {
        DestroyShaderModule(module);   // the other thread's module serves
        return entry;
    }
    // Warm-list records waiting for this shader: queue the ones that are now complete.
    std::lock_guard<std::mutex> guard(g_warmLock);
    auto range = g_pendingByShader.equal_range(hash);
    std::vector<size_t> waiting;
    for (auto it = range.first; it != range.second; ++it)
        waiting.push_back(it->second);
    g_pendingByShader.erase(hash);
    for (size_t index : waiting)
        IndexRecordLocked(index);
    return entry;
}

void ReleaseShader(ShaderEntry* entry) {
    if (!entry) return;
    {
        std::lock_guard<std::mutex> guard(g_shaderLock);
        if (--entry->refs > 0) return;
        g_shaders.erase(entry->hash);
    }
    DestroyShaderModule(entry->module);
    delete entry;
}

// ---- vertex layouts ------------------------------------------------------------------------------------

uint64_t InternLayout(const LayoutElement* elements, size_t count) {
    if (count == 0) return 0;
    const uint64_t hash = HashBytes(elements, count * sizeof(LayoutElement), count) | 1;
    std::lock_guard<std::mutex> guard(g_layoutLock);
    g_layouts.emplace(hash, std::vector<LayoutElement>(elements, elements + count));
    return hash;
}

// ---- pipelines ---------------------------------------------------------------------------------------

VkPipeline ResolvePipeline(PipelineKey& key, const ShaderEntry* vs, const ShaderEntry* ps, const PairLayout** pair) {
    key.linked = LinkedFlag(vs, ps);
    *pair = g_push ? PairLayoutFor(vs->reflection, ps ? &ps->reflection : nullptr) : nullptr;
    {
        std::lock_guard<std::mutex> guard(g_pipelineLock);
        auto it = g_pipelines.find(key);
        if (it != g_pipelines.end()) return it->second;
    }
    std::string why;
    VkPipeline pipeline = BuildPipeline(key, vs, ps, &why);
    if (!pipeline) {
        g_failed.fetch_add(1, std::memory_order_relaxed);
        std::fprintf(stderr, "[Vulkan] pipeline vs %016llx ps %016llx: %s (its draws are skipped)\n",
                     static_cast<unsigned long long>(key.vs), static_cast<unsigned long long>(key.ps), why.c_str());
        Publish(key, VK_NULL_HANDLE);
        return VK_NULL_HANDLE;
    }
    g_built.fetch_add(1, std::memory_order_relaxed);
    if (g_buildLogLines.fetch_add(1, std::memory_order_relaxed) < 3) {
        std::fprintf(stderr, "[Vulkan] pipeline built: vs %016llx ps %016llx%s topology %u\n",
                     static_cast<unsigned long long>(key.vs), static_cast<unsigned long long>(key.ps),
                     key.linked ? " (linked)" : "", key.topology);
    }
    RecordKey(key);
    return Publish(key, pipeline);
}

void TakePipelineStats(uint32_t* built, uint32_t* warmed, uint32_t* failed) {
    *built = g_built.exchange(0, std::memory_order_relaxed);
    *warmed = g_warmed.exchange(0, std::memory_order_relaxed);
    *failed = g_failed.exchange(0, std::memory_order_relaxed);
}

}  // namespace VideoVulkan
