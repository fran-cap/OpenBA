// ============================================================================
// src/render/dx11/ao/ao_pass.cpp -- the backend end of ext.ambient_occlusion
// ============================================================================
// NOT RETAIL. docs/extensions/AO.md §3; docs/extensions/handins/AOCORE.md.
//
// Three things live here because only the dx11 TUs see the D3D headers:
//   1. the readable main depth (ao_depth_views.h, ao_backend.h): the request the
//      extension makes, what CreateDepthBuffer built, and the views
//      AttachDepthBuffer makes on it;
//   2. Graphics::ExtAoAfterOpaque (renderer_ext_ao.h): the pass Renderer::Draw
//      runs between the Opaque and Blended lists of the game's main entry:
//        save every D3D binding the pass touches
//        resolve the main depth (MSAA or 1x) -> R32F raw depth   ao_depth_resolve
//        rebind the main colour + depth, run the AO stage        ao_stage_assao
//          (ASSAO multiplies into the bound colour: blend ZERO / SRC_COLOR,
//           its own depth state = test and write off)
//        restore every binding
//      The engine's ContextData caches what it last bound; restoring the D3D
//      state exactly keeps that cache true (the spike's S4: the Blended list
//      renders unchanged after it), so no engine dirty bit is raised;
//   3. the stage's lifetime: built on the first AO frame of a device, rebuilt
//      when the device changes (device lost: DeviceData::CreateDevice makes a new
//      ID3D11Device; the old objects keep their device alive until released
//      here), resized when the depth's size changes (window resize / Options:
//      the depth, and its SRV, are rebuilt by the tracked-image registry,
//      dynamic_resources.cpp RecreateDynamic).
// ============================================================================
#include "ao/ao_backend.h"
#include "ao/ao_depth_resolve.h"
#include "ao/ao_depth_views.h"
#include "ao/ao_stage.h"
#include "ao/ao_state_backup.h"

#include "dx11_backend_access.h"
#include "ext/ambient_occlusion/ao_math.h"
#include "graphics/Camera.h"
#include "graphics/renderer_ext_ao.h"
#include "video/Context.h"
#include "video/Device.h"
#include "video/ImageBuffer.h"

#include <atomic>
#include <cstdio>

namespace AoBackend {
namespace {

std::atomic<bool> g_readableRequested{false};   // the extension's (main thread)
std::atomic<bool> g_depthReadable{false};       // what CreateDepthBuffer built (render thread too)

ID3D11Device* LiveDevice() {
    const ::Video::Device* const dev = ::Video::Device::_singleton;
    if (dev == nullptr || dev->deviceData == nullptr) return nullptr;
    return VideoDX11::Dx11DeviceDataOf(dev)->d3dDevice;
}

}  // namespace

bool Supported() {
    ID3D11Device* const d = LiveDevice();
    return d != nullptr && d->GetFeatureLevel() >= D3D_FEATURE_LEVEL_11_0;
}

void RequestReadableDepth(bool on) {
    g_readableRequested.store(on);
    ::Video::Device* const dev = ::Video::Device::_singleton;
    if (dev == nullptr || LiveDevice() == nullptr) return;   // the first CreateDepthBuffer reads it
    if (g_depthReadable.load() != on) {
        std::fprintf(stderr, "[AmbientOcclusion] main depth -> %s: rebuild requested (Device::settingsChanged)\n",
                     on ? "readable (R24G8_TYPELESS + SRV)" : "retail (D24_UNORM_S8_UINT)");
        dev->settingsChanged = 1;
    }
}

bool DepthReadable() { return g_depthReadable.load(); }

bool BuildReadableDepth() {
    const bool on = g_readableRequested.load();
    g_depthReadable.store(on);
    return on;
}

void CreateMainDepthViews(ID3D11Device* device, ID3D11Texture2D* depth,
                          ID3D11DepthStencilView** dsv, ID3D11ShaderResourceView** srv) {
    D3D11_TEXTURE2D_DESC td;
    depth->GetDesc(&td);
    const bool ms = td.SampleDesc.Count > 1;

    D3D11_DEPTH_STENCIL_VIEW_DESC dd = {};
    dd.Format        = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dd.ViewDimension = ms ? D3D11_DSV_DIMENSION_TEXTURE2DMS : D3D11_DSV_DIMENSION_TEXTURE2D;
    const HRESULT h1 = device->CreateDepthStencilView(depth, &dd, dsv);

    D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
    sd.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    if (ms) {
        sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DMS;
    } else {
        sd.ViewDimension       = D3D11_SRV_DIMENSION_TEXTURE2D;
        sd.Texture2D.MipLevels = 1;
    }
    const HRESULT h2 = device->CreateShaderResourceView(depth, &sd, srv);
    std::fprintf(stderr, "[AmbientOcclusion] readable main depth %ux%u x%u: DSV 0x%08lx, SRV 0x%08lx\n", td.Width,
                 td.Height, td.SampleDesc.Count, static_cast<unsigned long>(h1),
                 static_cast<unsigned long>(h2));
}

}  // namespace AoBackend

namespace {

// ---------------------------------------------------------------------------
// The pass's objects, per device.
// ---------------------------------------------------------------------------
struct PassObjects {
    ID3D11Device*         device = nullptr;   // the device they belong to (not AddRef'd)
    AoStage::DepthResolve resolve;
    AoStage::Stage*       stage  = nullptr;
    bool                  failed = false;     // creation failed on this device: skip until it changes

    void Drop() {
        delete stage;
        stage = nullptr;
        resolve.Release();
        failed = false;
        device = nullptr;
    }
    bool EnsureFor(ID3D11Device* dev) {
        if (dev == device) return !failed;
        if (device != nullptr) std::fprintf(stderr, "[AmbientOcclusion] device changed: the AO stage is rebuilt\n");
        Drop();
        device = dev;
        failed = !resolve.Create(dev);
        if (!failed) {
            stage  = AoStage::CreateAssao(dev);
            failed = stage == nullptr;
        }
        return !failed;
    }
};
PassObjects g_pass;

using AoStage::StateBackup;   // ao_state_backup.h

}  // namespace

namespace Graphics {

void ExtAoAfterOpaque(Video::Context& context, const ExtAoFrame& frame,
                      const RendererSceneEntry& entry) {
    // The readable depth's SRV, in the depth alias's ImageBufferData. Null while
    // the rebuild that makes the depth readable has not run yet (the frames
    // between the toggle and the next BeginCommands): no AO for those frames.
    const ::Video::RenderBuffer* const rb = context._defaultRenderBuffer;
    const ::Video::ImageBuffer* const depthImage = rb->depthBuffer;
    if (depthImage == nullptr || depthImage->_data == nullptr) return;
    VideoDX11::ImageBufferData* const depth = VideoDX11::Dx11ImageBackend(depthImage);
    ID3D11ShaderResourceView* const depthSrv = depth->shaderResourceView;
    if (depthSrv == nullptr || depth->texture == nullptr) return;

    ID3D11Device* const dev = VideoDX11::Dx11Device();
    ID3D11DeviceContext* const dc = VideoDX11::Dx11Context();
    if (!g_pass.EnsureFor(dev)) return;

    D3D11_TEXTURE2D_DESC td;
    depth->texture->GetDesc(&td);
    if (!g_pass.resolve.Resize(dev, td.Width, td.Height)) return;
    g_pass.stage->Resize(dev, td.Width, td.Height);

    float lh[16];
    Ext::AmbientOcclusion::AoMath::AssaoProjection(entry._camera._projection.m, lh);

    StateBackup saved(dc);
    if (saved.Colour() == nullptr) return;   // nothing bound to darken (never seen; restored by `saved`)
    ID3D11ShaderResourceView* const raw = g_pass.resolve.Run(dc, depthSrv, td.SampleDesc.Count);
    saved.RebindTargets();
    // The fade follows this frame's fog (Weather::DrawUpdate -> Context::SetFog ran
    // before Renderer::Draw): the materials fogged their pixels already, so AO must be
    // gone where the fog is (ao_math.h FadeFromFog; the user's look 2026-10-14).
    Graphics::ExtAoFrame fogged = frame;
    Ext::AmbientOcclusion::AoMath::FadeFromFog(context._fogParams.x, context._fogParams.y, fogged);
    g_pass.stage->Draw(dc, raw, lh, fogged);
}

}  // namespace Graphics
