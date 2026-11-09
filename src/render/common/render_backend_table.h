#pragma once
// ============================================================================
// src/render/common/render_backend_table.h -- one backend's entry points, in the dual exe
// ============================================================================
// NOT RETAIL. Only a build with several backends (BANISHED_RENDER_BACKEND=dx11+vulkan) uses this.
// Each backend fills one BackendTable in its own render_backend_<name>.cpp with the addresses of the
// functions it defines behind the backend-neutral headers (its inline namespace, render_impl.h), and
// returns it from RenderBackendTable_<name>() (the names come from the CMake-generated
// render_backends.inc). src/render/common/multi/render_backend_dispatch.cpp defines the public
// names of those functions as forwards to the table of the backend the player picked.
//
// Nothing outside a backend names the backend's namespace: the table holds plain function
// addresses, filled where the backend's own names are in scope.
// ============================================================================

#include <atomic>
#include <cstdint>

namespace Video {
class Context;
class Device;
class DeviceData;
class ImageBuffer;
class VertexProgram;
}  // namespace Video
namespace Math {
class Matrix;
}
namespace Ext {
namespace DerivedCache {
class Store;
}
}  // namespace Ext
namespace Graphics {
struct ExtAoFrame;
struct RendererSceneEntry;
}  // namespace Graphics
namespace BbsRewrite {
struct Params;
}
namespace FaaBackend {
struct TweakStats;
}
namespace TerrainLightBackend {
struct Frame;
struct Textures;
}
namespace DofBackend {
struct Frame;
}
namespace CloudShadowBackend {
struct Frame;
}

namespace Render {

struct BackendTable {
    // "dx11", "vulkan": the backend's directory name, as BANISHED_RENDER_BACKEND lists it.
    const char* id;

    // Whether the backend can start on this machine, asked before its device is made: a cheap probe
    // (Vulkan: the loader loads and lists an adapter). A backend that cannot tell answers true and is
    // judged by its CreateDevice.
    bool (*available)();

    // ---- render_backend.h ----------------------------------------------------------------------
    const char* (*deviceName)();
    ::Video::DeviceData* (*createDeviceData)(::Video::Device* device);
    uint64_t (*graphicsWindowFlags)();
    void (*dumpSwapChainState)(const char* tag);
    bool (*bootedExclusiveFullscreen)();
    bool (*supportsExclusiveFullscreen)();
    const char* (*placeholderDeviceName)();
    ::Video::DeviceData* (*placeholderCreateDeviceData)(::Video::Device* device);

    // ---- ao/ao_backend.h -----------------------------------------------------------------------
    bool (*aoSupported)();
    void (*aoRequestReadableDepth)(bool on);
    bool (*aoDepthReadable)();

    // ---- async_gpu_create/async_gpu_create_backend.h -------------------------------------------
    void (*asyncGpuCreateRequest)(bool on);
    bool (*asyncGpuCreateSupported)();

    // ---- better_trees/billboard_shadow_backend.h -----------------------------------------------
    void (*bbsRequest)(bool on, bool naturalGrowth);
#if BANISHED_TWEAK_OVERLAY
    void (*bbsRequestParams)(const ::BbsRewrite::Params& p);   // the debug tweak overlay (debug flavour)
    ::BbsRewrite::Params (*bbsAppliedParams)();
#endif

    // ---- boot_device_lost/boot_device_lost_backend.h -------------------------------------------
    void (*bootDeviceLostSetFixFlag)(const std::atomic<bool>* live);

    // ---- better_clouds/cloud_shadow_backend.h --------------------------------------------------
    bool (*cloudShadowSupported)();
    void (*cloudShadowRequest)(bool on);
    void (*cloudShadowDraw)(::Video::Context& context, const ::Video::ImageBuffer* noise,
                            const ::CloudShadowBackend::Frame& frame);

    // ---- fancy_terrain/terrain_light_backend.h -------------------------------------------------
    bool (*terrainLightSupported)();
    void (*terrainLightRequest)(bool on);
    void (*terrainLightDraw)(::Video::Context& context, const ::TerrainLightBackend::Textures& textures,
                             const ::TerrainLightBackend::Frame& frame);

    // ---- dof/dof_backend.h ---------------------------------------------------------------------
    bool (*dofSupported)();
    void (*dofRequest)(bool on);
    void (*dofDraw)(::Video::Context& context, const ::DofBackend::Frame& frame);

    // ---- compress_ao/compress_ao_backend.h -----------------------------------------------------
    void (*compressAoRequest)(bool on, ::Ext::DerivedCache::Store* cache);
    bool (*compressAoSupported)();

    // ---- create_on_demand/create_on_demand_backend.h -------------------------------------------
    void (*createOnDemandRequest)(bool on, int idleMinutes);
    bool (*createOnDemandSupported)();
    void (*createOnDemandPrefetch)(const void* companion);
    void (*createOnDemandKeep)(const void* companion);
    bool (*createOnDemandVertexBufferMade)(const void* companion);
    bool (*createOnDemandImageMade)(const void* companion);

    // ---- decal_fog/decal_fog_backend.h ---------------------------------------------------------
    bool (*decalFogSupported)();

    // ---- better_trees/foliage_aa_backend.h -----------------------------------------------------
    void (*faaRequest)(int mode, float fillK, float fillFloor, const ::BbsRewrite::Params* dither,
                       bool naturalGrowth);
    void (*faaSetDrawBloom)(bool bloom);
#if BANISHED_TWEAK_OVERLAY
    ::FaaBackend::TweakStats (*faaStats)();   // the debug tweak overlay's stats line (debug flavour)
#endif

    // ---- draw_batch/instance_bank.h ------------------------------------------------------------
    int32_t (*instanceBankCap)();
    void (*instanceBankSetWide)(::Video::Context& context, int32_t slot, const ::Math::Matrix& m);

    // ---- better_water/scene_depth_backend.h ----------------------------------------------------
    bool (*sceneDepthSupported)();
    void (*sceneDepthRequest)(bool on);
    void (*sceneDepthCopy)(void* context);
    const ::Video::ImageBuffer* (*sceneDepthImage)();

    // ---- share_gpu_data/share_gpu_data_backend.h -----------------------------------------------
    void (*shareGpuDataRequest)(bool on);
    bool (*shareGpuDataSupported)();

    // ---- draw_batch/smoke_vs_backend.h ---------------------------------------------------------
    void (*smokeVsRequest)(bool on);
    bool (*smokeVsRewritten)(const ::Video::VertexProgram* vp);

    // ---- texture_quality/texture_quality_backend.h ---------------------------------------------
    bool (*textureQualitySupported)();

    // ---- src/ext/render_pipe/finish_split.h ----------------------------------------------------
    int32_t (*renderPipeFinishPresent)(::Video::Context& ctx);
    void (*renderPipeFinishRecover)(::Video::Device& device, int32_t hr);

    // ---- src/engine/graphics/renderer_ext_ao.h -------------------------------------------------
    void (*extAoAfterOpaque)(::Video::Context& context, const ::Graphics::ExtAoFrame& frame,
                             const ::Graphics::RendererSceneEntry& entry);
};

}  // namespace Render
