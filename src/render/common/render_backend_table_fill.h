#pragma once
// ============================================================================
// src/render/common/render_backend_table_fill.h -- a backend's BackendTable, filled in its own sources
// ============================================================================
// NOT RETAIL. Included by exactly one source of each backend in the dual exe (its
// render_backend_<name>.cpp, compiled with BANISHED_RENDER_IMPL_NS: render_impl.h). Every name below
// is written as the backend-neutral headers spell it; inside that source each one resolves to the
// backend's own definition (its inline namespace), so the one list serves every backend and nothing
// here names a backend. The function has internal linkage (the anonymous namespace): each backend's
// copy is its own, never merged with another's.
// ============================================================================

#if !defined(BANISHED_RENDER_IMPL_NS)
#error "render_backend_table_fill.h belongs to a backend source of the dual exe (render_impl.h)"
#endif

#include "render_backend.h"
#include "render_backend_table.h"

#include "ao/ao_backend.h"
#include "async_gpu_create/async_gpu_create_backend.h"
#include "better_clouds/cloud_shadow_backend.h"
#include "fancy_terrain/terrain_light_backend.h"
#include "dof/dof_backend.h"
#include "better_trees/billboard_shadow_backend.h"
#include "better_trees/foliage_aa_backend.h"
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

namespace {

// The table of the backend this source belongs to: `id` and `available` are the backend's own,
// every other entry the address of its definition behind the shared headers.
Render::BackendTable MakeBackendTable(const char* id, bool (*available)()) {
    Render::BackendTable t{};
    t.id = id;
    t.available = available;

    t.deviceName = &Render::DeviceName;
    t.createDeviceData = &Render::CreateDeviceData;
    t.graphicsWindowFlags = &Render::GraphicsWindowFlags;
    t.dumpSwapChainState = &Render::DumpSwapChainState;
    t.bootedExclusiveFullscreen = &Render::BootedExclusiveFullscreen;
    t.supportsExclusiveFullscreen = &Render::SupportsExclusiveFullscreen;
    t.placeholderDeviceName = &Render::PlaceholderDeviceName;
    t.placeholderCreateDeviceData = &Render::PlaceholderCreateDeviceData;

    t.aoSupported = &AoBackend::Supported;
    t.aoRequestReadableDepth = &AoBackend::RequestReadableDepth;
    t.aoDepthReadable = &AoBackend::DepthReadable;

    t.asyncGpuCreateRequest = &AsyncGpuCreateBackend::Request;
    t.asyncGpuCreateSupported = &AsyncGpuCreateBackend::Supported;

    t.bbsRequest = &BbsBackend::Request;
#if BANISHED_TWEAK_OVERLAY
    t.bbsRequestParams = &BbsBackend::RequestParams;
    t.bbsAppliedParams = &BbsBackend::AppliedParams;
#endif

    t.bootDeviceLostSetFixFlag = &BootDeviceLostBackend::SetFixFlag;

    t.cloudShadowSupported = &CloudShadowBackend::Supported;
    t.cloudShadowRequest = &CloudShadowBackend::Request;
    t.cloudShadowDraw = &CloudShadowBackend::Draw;

    t.terrainLightSupported = &TerrainLightBackend::Supported;
    t.terrainLightRequest = &TerrainLightBackend::Request;
    t.terrainLightDraw = &TerrainLightBackend::Draw;

    t.dofSupported = &DofBackend::Supported;
    t.dofRequest = &DofBackend::Request;
    t.dofDraw = &DofBackend::Draw;

    t.compressAoRequest = &CompressAoBackend::Request;
    t.compressAoSupported = &CompressAoBackend::Supported;

    t.createOnDemandRequest = &CreateOnDemandBackend::Request;
    t.createOnDemandSupported = &CreateOnDemandBackend::Supported;
    t.createOnDemandPrefetch = &CreateOnDemandBackend::Prefetch;
    t.createOnDemandKeep = &CreateOnDemandBackend::Keep;
    t.createOnDemandVertexBufferMade = &CreateOnDemandBackend::VertexBufferMade;
    t.createOnDemandImageMade = &CreateOnDemandBackend::ImageMade;

    t.decalFogSupported = &DecalFogBackend::Supported;

    t.faaRequest = &FaaBackend::Request;
    t.faaSetDrawBloom = &FaaBackend::SetDrawBloom;
#if BANISHED_TWEAK_OVERLAY
    t.faaStats = &FaaBackend::Stats;
#endif

    t.instanceBankCap = &InstanceBank::Cap;
    t.instanceBankSetWide = &InstanceBank::SetWide;

    t.sceneDepthSupported = &SceneDepthBackend::Supported;
    t.sceneDepthRequest = &SceneDepthBackend::Request;
    t.sceneDepthCopy = &SceneDepthBackend::Copy;
    t.sceneDepthImage = &SceneDepthBackend::Image;

    t.shareGpuDataRequest = &ShareGpuDataBackend::Request;
    t.shareGpuDataSupported = &ShareGpuDataBackend::Supported;

    t.smokeVsRequest = &SmokeVsBackend::Request;
    t.smokeVsRewritten = &SmokeVsBackend::Rewritten;

    t.textureQualitySupported = &TextureQualityBackend::Supported;

    t.renderPipeFinishPresent = &Ext::RenderPipe::FinishPresent;
    t.renderPipeFinishRecover = &Ext::RenderPipe::FinishRecover;

    t.extAoAfterOpaque = &Graphics::ExtAoAfterOpaque;
    return t;
}

}  // namespace
