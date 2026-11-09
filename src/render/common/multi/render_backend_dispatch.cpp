// ============================================================================
// src/render/common/multi/render_backend_dispatch.cpp -- the dual exe's backend-neutral entry points
// ============================================================================
// NOT RETAIL. Compiled only into a build with several backends (BANISHED_RENDER_BACKEND=dx11+vulkan,
// CMakeLists.txt). Each backend defines the functions of the backend-neutral headers in its own inline
// namespace (render_impl.h) and hands their addresses over in a BackendTable
// (render_backend_table.h). This file defines the public names: each forwards to the table of the
// active backend, the one the player picked (ext.render_backend, src/ext/render_backend/) or the one
// the fallback reached.
//
// THE REQUESTS. A Request / SetFixFlag / SetDrawBloom call is state the extensions hand a backend once
// (at boot, or when the player changes a row), and a backend reads it when it makes its device and
// its programs. A backend picked later has never seen them, so each such call is recorded here and
// made again of the new backend by SelectBackend, while no device exists. They are not made of the
// inactive backend as they arrive: a backend's request body may read Video::Device::deviceData as its
// own type, which it is not while the other backend is active.
//
// Everything else (the Supported() answers, the draws, the finish halves) is a question about, or work
// on, the device that exists, and goes to the active backend only.
// ============================================================================
#include "render_backend.h"
#include "render_backend_table.h"

#include "ao/ao_backend.h"
#include "async_gpu_create/async_gpu_create_backend.h"
#include "better_clouds/cloud_shadow_backend.h"
#include "fancy_terrain/terrain_light_backend.h"
#include "dof/dof_backend.h"
#include "better_trees/billboard_shadow_backend.h"
#include "better_trees/foliage_aa_backend.h"
#include "better_trees/receiver_rewrite.h"
#include "better_water/scene_depth_backend.h"
#include "boot_device_lost/boot_device_lost_backend.h"
#include "compress_ao/compress_ao_backend.h"
#include "create_on_demand/create_on_demand_backend.h"
#include "decal_fog/decal_fog_backend.h"
#include "draw_batch/instance_bank.h"
#include "draw_batch/smoke_vs_backend.h"
#include "share_gpu_data/share_gpu_data_backend.h"
#include "texture_quality/texture_quality_backend.h"
#include "ext/render_pipe/finish_split.h"
#include "graphics/renderer_ext_ao.h"
#include "video/Device.h"
#include "render_switch_drive.h"   // BANISHED_RENDER_SWITCH failcreate (src/diag)

#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>

// The backends this exe carries, in CMake's order (the fallback order): one line
// BANISHED_RENDER_BACKEND(<name>) each, generated into the build directory.
#define BANISHED_RENDER_BACKEND(name) const Render::BackendTable& RenderBackendTable_##name();
#include "render_backends.inc"
#undef BANISHED_RENDER_BACKEND

namespace Render {
namespace {

const BackendTable& TableAt(int index) {
    using Getter = const BackendTable& (*)();
    static const Getter kGetters[] = {
#define BANISHED_RENDER_BACKEND(name) &RenderBackendTable_##name,
#include "render_backends.inc"
#undef BANISHED_RENDER_BACKEND
    };
    return kGetters[index]();
}

int Count() {
    static const int kCount = [] {
        int n = 0;
#define BANISHED_RENDER_BACKEND(name) ++n;
#include "render_backends.inc"
#undef BANISHED_RENDER_BACKEND
        return n;
    }();
    return kCount;
}

// The active backend's index. Written only by SelectBackend (no device exists then); read by every
// forward below, the loader and render threads' included.
std::atomic<int> g_active{0};
// The backends that made a device on the game window this run, a bit per index (CreateDeviceData with the
// window known): WindowPresentedByOtherBackend.
std::atomic<uint32_t> g_onWindow{0};
// The order of those devices, for WindowTakenOverByOtherBackend: a counter bumped by each one, and per
// backend the count of its first and of its latest (0 = none yet).
constexpr int kMaxBackends = 32;   // g_onWindow's bits
std::atomic<uint32_t> g_onWindowCount{0};
std::atomic<uint32_t> g_firstOnWindow[kMaxBackends]{};
std::atomic<uint32_t> g_lastOnWindow[kMaxBackends]{};

const BackendTable& Active() {
    return TableAt(g_active.load(std::memory_order_acquire));
}

// ---- the recorded requests (THE REQUESTS above) -----------------------------------------------
template <typename T>
struct Recorded {
    bool made = false;
    T value{};
};

struct FaaRequestArgs {
    int mode = 0;
    float fillK = 0.0f;
    float fillFloor = 0.0f;
    bool hasDither = false;
    BbsRewrite::Params dither{};
    bool naturalGrowth = false;
};

struct Requests {
    Recorded<bool> aoReadableDepth;
    Recorded<bool> asyncGpuCreate;
    Recorded<bool> bbs;
    bool bbsNaturalGrowth = false;
    Recorded<const std::atomic<bool>*> bootDeviceLostFlag;
    Recorded<bool> cloudShadow;
    Recorded<bool> terrainLight;
    Recorded<bool> dof;
    Recorded<bool> compressAo;
    Ext::DerivedCache::Store* compressAoCache = nullptr;
    Recorded<bool> createOnDemand;
    int createOnDemandIdleMinutes = 0;
    Recorded<FaaRequestArgs> faa;
    Recorded<bool> faaDrawBloom;
    Recorded<bool> sceneDepth;
    Recorded<bool> shareGpuData;
    Recorded<bool> smokeVs;
};

std::mutex g_requestsLock;
Requests g_requests;

// Makes every recorded request of `t`, in the order the extensions first make them at boot.
void Replay(const BackendTable& t) {
    Requests r;
    {
        std::lock_guard<std::mutex> guard(g_requestsLock);
        r = g_requests;
    }
    int made = 0;
    if (r.bootDeviceLostFlag.made) {
        t.bootDeviceLostSetFixFlag(r.bootDeviceLostFlag.value);
        ++made;
    }
    if (r.compressAo.made) {
        t.compressAoRequest(r.compressAo.value, r.compressAoCache);
        ++made;
    }
    if (r.createOnDemand.made) {
        t.createOnDemandRequest(r.createOnDemand.value, r.createOnDemandIdleMinutes);
        ++made;
    }
    if (r.asyncGpuCreate.made) {
        t.asyncGpuCreateRequest(r.asyncGpuCreate.value);
        ++made;
    }
    if (r.shareGpuData.made) {
        t.shareGpuDataRequest(r.shareGpuData.value);
        ++made;
    }
    if (r.faa.made) {
        const FaaRequestArgs& a = r.faa.value;
        t.faaRequest(a.mode, a.fillK, a.fillFloor, a.hasDither ? &a.dither : nullptr, a.naturalGrowth);
        ++made;
    }
    if (r.faaDrawBloom.made) {
        t.faaSetDrawBloom(r.faaDrawBloom.value);
        ++made;
    }
    if (r.bbs.made) {
        t.bbsRequest(r.bbs.value, r.bbsNaturalGrowth);
        ++made;
    }
    if (r.smokeVs.made) {
        t.smokeVsRequest(r.smokeVs.value);
        ++made;
    }
    if (r.aoReadableDepth.made) {
        t.aoRequestReadableDepth(r.aoReadableDepth.value);
        ++made;
    }
    if (r.sceneDepth.made) {
        t.sceneDepthRequest(r.sceneDepth.value);
        ++made;
    }
    if (r.cloudShadow.made) {
        t.cloudShadowRequest(r.cloudShadow.value);
        ++made;
    }
    if (r.terrainLight.made) {
        t.terrainLightRequest(r.terrainLight.value);
        ++made;
    }
    if (r.dof.made) {
        t.dofRequest(r.dof.value);
        ++made;
    }
    std::fprintf(stderr, "[RenderBackend] %s: %d recorded request(s) made again\n", t.id, made);
}

}  // namespace

// ---- the backend set (render_backend.h) ---------------------------------------------------------

int BackendCount() {
    return Count();
}

const char* BackendId(int index) {
    if (index < 0 || index >= Count()) return nullptr;
    return TableAt(index).id;
}

const char* BackendName(int index) {
    if (index < 0 || index >= Count()) return nullptr;
    return TableAt(index).deviceName();
}

int FindBackend(const char* id) {
    if (id == nullptr) return -1;
    for (int i = 0; i < Count(); ++i) {
        if (std::strcmp(TableAt(i).id, id) == 0) return i;
    }
    return -1;
}

int ActiveBackend() {
    return g_active.load(std::memory_order_acquire);
}

bool BackendAvailable(int index) {
    if (index < 0 || index >= Count()) return false;
    return TableAt(index).available();
}

bool SelectBackend(int index) {
    if (index < 0 || index >= Count()) return false;
    const Video::Device* const device = Video::Device::_singleton;
    if (device != nullptr && device->deviceData != nullptr) {
        std::fprintf(stderr, "[RenderBackend] select %s refused: a device exists\n", TableAt(index).id);
        return false;
    }
    const int previous = g_active.exchange(index, std::memory_order_acq_rel);
    if (previous != index) {
        std::fprintf(stderr, "[RenderBackend] active backend %s -> %s\n", TableAt(previous).id, TableAt(index).id);
        Replay(TableAt(index));
    }
    return true;
}

// ---- render_backend.h -------------------------------------------------------------------------

const char* DeviceName() {
    return Active().deviceName();
}

::Video::DeviceData* CreateDeviceData(::Video::Device* device) {
    // BANISHED_RENDER_SWITCH=failcreate=<backend> (src/diag/render_switch_drive.h): the backend's own
    // placeholder, whose CreateDevice fails, so PlatformStartup's fallback runs on a real failure path.
    if (RenderSwitchDrive::TakeFailCreate(Active().id, device != nullptr && device->windowHandle != nullptr))
        return Active().placeholderCreateDeviceData(device);
    if (device != nullptr && device->windowHandle != nullptr) {
        const int active = g_active.load(std::memory_order_acquire);
        g_onWindow.fetch_or(1u << active, std::memory_order_acq_rel);
        const uint32_t order = g_onWindowCount.fetch_add(1, std::memory_order_acq_rel) + 1;
        uint32_t none = 0;
        g_firstOnWindow[active].compare_exchange_strong(none, order, std::memory_order_acq_rel);
        g_lastOnWindow[active].store(order, std::memory_order_release);
    }
    return Active().createDeviceData(device);
}

bool WindowPresentedByOtherBackend() {
    const uint32_t others = g_onWindow.load(std::memory_order_acquire) &
                            ~(1u << g_active.load(std::memory_order_acquire));
    return others != 0;
}

bool WindowTakenOverByOtherBackend() {
    const int active = g_active.load(std::memory_order_acquire);
    const uint32_t first = g_firstOnWindow[active].load(std::memory_order_acquire);
    if (first == 0) return false;
    for (int b = 0; b < Count(); ++b) {
        if (b != active && g_lastOnWindow[b].load(std::memory_order_acquire) > first) return true;
    }
    return false;
}

// The game window is made before any backend is picked for certain, and with no graphics-API flag:
// a backend that needs one gets it when it makes its surface (SDL3 turns the window into a Vulkan
// window in SDL_Vulkan_CreateSurface), so a missing Vulkan loader cannot fail the window itself.
uint64_t GraphicsWindowFlags() {
    return 0;
}

void DumpSwapChainState(const char* tag) {
    Active().dumpSwapChainState(tag);
}

bool BootedExclusiveFullscreen() {
    return Active().bootedExclusiveFullscreen();
}

bool SupportsExclusiveFullscreen() {
    return Active().supportsExclusiveFullscreen();
}

const char* PlaceholderDeviceName() {
    return Active().placeholderDeviceName();
}

::Video::DeviceData* PlaceholderCreateDeviceData(::Video::Device* device) {
    return Active().placeholderCreateDeviceData(device);
}

}  // namespace Render

// ---- the extensions' backend headers ------------------------------------------------------------

namespace AoBackend {
bool Supported() {
    return Render::Active().aoSupported();
}
void RequestReadableDepth(bool on) {
    {
        std::lock_guard<std::mutex> guard(Render::g_requestsLock);
        Render::g_requests.aoReadableDepth = {true, on};
    }
    Render::Active().aoRequestReadableDepth(on);
}
bool DepthReadable() {
    return Render::Active().aoDepthReadable();
}
}  // namespace AoBackend

namespace AsyncGpuCreateBackend {
void Request(bool on) {
    {
        std::lock_guard<std::mutex> guard(Render::g_requestsLock);
        Render::g_requests.asyncGpuCreate = {true, on};
    }
    Render::Active().asyncGpuCreateRequest(on);
}
bool Supported() {
    return Render::Active().asyncGpuCreateSupported();
}
}  // namespace AsyncGpuCreateBackend

namespace BbsBackend {
void Request(bool on, bool naturalGrowth) {
    {
        std::lock_guard<std::mutex> guard(Render::g_requestsLock);
        Render::g_requests.bbs = {true, on};
        Render::g_requests.bbsNaturalGrowth = naturalGrowth;
    }
    Render::Active().bbsRequest(on, naturalGrowth);
}
#if BANISHED_TWEAK_OVERLAY
// The debug tweak overlay (debug flavour): not replayed on a backend switch; the overlay's next change
// sends the set again.
void RequestParams(const BbsRewrite::Params& p) {
    Render::Active().bbsRequestParams(p);
}
BbsRewrite::Params AppliedParams() {
    return Render::Active().bbsAppliedParams();
}
#endif
}  // namespace BbsBackend

namespace BootDeviceLostBackend {
void SetFixFlag(const std::atomic<bool>* live) {
    {
        std::lock_guard<std::mutex> guard(Render::g_requestsLock);
        Render::g_requests.bootDeviceLostFlag = {true, live};
    }
    Render::Active().bootDeviceLostSetFixFlag(live);
}
}  // namespace BootDeviceLostBackend

namespace CloudShadowBackend {
bool Supported() {
    return Render::Active().cloudShadowSupported();
}
void Request(bool on) {
    {
        std::lock_guard<std::mutex> guard(Render::g_requestsLock);
        Render::g_requests.cloudShadow = {true, on};
    }
    Render::Active().cloudShadowRequest(on);
}
void Draw(Video::Context& context, const Video::ImageBuffer* noise, const Frame& frame) {
    Render::Active().cloudShadowDraw(context, noise, frame);
}
}  // namespace CloudShadowBackend

namespace TerrainLightBackend {
bool Supported() {
    return Render::Active().terrainLightSupported();
}
void Request(bool on) {
    {
        std::lock_guard<std::mutex> guard(Render::g_requestsLock);
        Render::g_requests.terrainLight = {true, on};
    }
    Render::Active().terrainLightRequest(on);
}
void Draw(Video::Context& context, const Textures& textures, const Frame& frame) {
    Render::Active().terrainLightDraw(context, textures, frame);
}
}  // namespace TerrainLightBackend

namespace DofBackend {
bool Supported() {
    return Render::Active().dofSupported();
}
void Request(bool on) {
    {
        std::lock_guard<std::mutex> guard(Render::g_requestsLock);
        Render::g_requests.dof = {true, on};
    }
    Render::Active().dofRequest(on);
}
void Draw(Video::Context& context, const Frame& frame) {
    Render::Active().dofDraw(context, frame);
}
}  // namespace DofBackend

namespace CompressAoBackend {
void Request(bool on, Ext::DerivedCache::Store* cache) {
    {
        std::lock_guard<std::mutex> guard(Render::g_requestsLock);
        Render::g_requests.compressAo = {true, on};
        Render::g_requests.compressAoCache = cache;
    }
    Render::Active().compressAoRequest(on, cache);
}
bool Supported() {
    return Render::Active().compressAoSupported();
}
}  // namespace CompressAoBackend

namespace CreateOnDemandBackend {
void Request(bool on, int idleMinutes) {
    {
        std::lock_guard<std::mutex> guard(Render::g_requestsLock);
        Render::g_requests.createOnDemand = {true, on};
        Render::g_requests.createOnDemandIdleMinutes = idleMinutes;
    }
    Render::Active().createOnDemandRequest(on, idleMinutes);
}
bool Supported() {
    return Render::Active().createOnDemandSupported();
}
void Prefetch(const void* companion) {
    Render::Active().createOnDemandPrefetch(companion);
}
void Keep(const void* companion) {
    Render::Active().createOnDemandKeep(companion);
}
bool VertexBufferMade(const void* companion) {
    return Render::Active().createOnDemandVertexBufferMade(companion);
}
bool ImageMade(const void* companion) {
    return Render::Active().createOnDemandImageMade(companion);
}
}  // namespace CreateOnDemandBackend

namespace DecalFogBackend {
bool Supported() {
    return Render::Active().decalFogSupported();
}
}  // namespace DecalFogBackend

namespace FaaBackend {
void Request(int mode, float fillK, float fillFloor, const BbsRewrite::Params* dither, bool naturalGrowth) {
    {
        std::lock_guard<std::mutex> guard(Render::g_requestsLock);
        Render::FaaRequestArgs a;
        a.mode = mode;
        a.fillK = fillK;
        a.fillFloor = fillFloor;
        a.hasDither = dither != nullptr;
        if (dither != nullptr) a.dither = *dither;
        a.naturalGrowth = naturalGrowth;
        Render::g_requests.faa = {true, a};
    }
    Render::Active().faaRequest(mode, fillK, fillFloor, dither, naturalGrowth);
}
void SetDrawBloom(bool bloom) {
    {
        std::lock_guard<std::mutex> guard(Render::g_requestsLock);
        Render::g_requests.faaDrawBloom = {true, bloom};
    }
    Render::Active().faaSetDrawBloom(bloom);
}
#if BANISHED_TWEAK_OVERLAY
TweakStats Stats() {
    return Render::Active().faaStats();
}
#endif
}  // namespace FaaBackend

namespace InstanceBank {
int32_t Cap() {
    return Render::Active().instanceBankCap();
}
void SetWide(Video::Context& context, int32_t slot, const Math::Matrix& m) {
    Render::Active().instanceBankSetWide(context, slot, m);
}
}  // namespace InstanceBank

namespace SceneDepthBackend {
bool Supported() {
    return Render::Active().sceneDepthSupported();
}
void Request(bool on) {
    {
        std::lock_guard<std::mutex> guard(Render::g_requestsLock);
        Render::g_requests.sceneDepth = {true, on};
    }
    Render::Active().sceneDepthRequest(on);
}
void Copy(void* context) {
    Render::Active().sceneDepthCopy(context);
}
const Video::ImageBuffer* Image() {
    return Render::Active().sceneDepthImage();
}
}  // namespace SceneDepthBackend

namespace ShareGpuDataBackend {
void Request(bool on) {
    {
        std::lock_guard<std::mutex> guard(Render::g_requestsLock);
        Render::g_requests.shareGpuData = {true, on};
    }
    Render::Active().shareGpuDataRequest(on);
}
bool Supported() {
    return Render::Active().shareGpuDataSupported();
}
}  // namespace ShareGpuDataBackend

namespace SmokeVsBackend {
void Request(bool on) {
    {
        std::lock_guard<std::mutex> guard(Render::g_requestsLock);
        Render::g_requests.smokeVs = {true, on};
    }
    Render::Active().smokeVsRequest(on);
}
bool Rewritten(const Video::VertexProgram* vp) {
    return Render::Active().smokeVsRewritten(vp);
}
}  // namespace SmokeVsBackend

namespace TextureQualityBackend {
bool Supported() {
    return Render::Active().textureQualitySupported();
}
}  // namespace TextureQualityBackend

namespace Ext {
namespace RenderPipe {
int32_t FinishPresent(Video::Context& ctx) {
    return Render::Active().renderPipeFinishPresent(ctx);
}
void FinishRecover(Video::Device& device, int32_t hr) {
    Render::Active().renderPipeFinishRecover(device, hr);
}
}  // namespace RenderPipe
}  // namespace Ext

namespace Graphics {
void ExtAoAfterOpaque(Video::Context& context, const ExtAoFrame& frame, const RendererSceneEntry& entry) {
    Render::Active().extAoAfterOpaque(context, frame, entry);
}
}  // namespace Graphics
