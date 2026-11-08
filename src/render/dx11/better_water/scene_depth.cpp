// ============================================================================
// src/render/dx11/better_water/scene_depth.cpp -- ext.better_water's scene depth copy on this backend
// ============================================================================
// NOT RETAIL. The contract is src/render/common/better_water/scene_depth_backend.h
// (docs/extensions/handins/BETTERWATER.md section 8).
//
// THE COPY. Copy() runs at the frame-copy seam, while the main colour and depth are still bound as the
// output the water then draws into. It is THE SHARED DEPTH COPY (depth_copy/depth_copy.h; the rule:
// src/render/common/depth_copy/depth_copy_backend.h) of the depth bound there: the one the AO pass or
// the cloud shadow made earlier this frame while no draw has written the depth since (the Blended
// draws before the water test the depth and never write it), else made here (the bound depth copied
// whole, or the readable main depth resolved). Load returns the 0..1 depth as a float in .r either way:
// a 1x R32_FLOAT holding sample 0 of a multisampled depth, or the single-sampled depth itself.
//
// THE IMAGE. An engine Video::ImageBuffer made once through the class factory (CreateNewObject; its
// PlatformConstruct asks DeviceData for this backend's ImageBufferData companion). It is never Create()d:
// kind 2 and flags 0 keep it off the tracked-image registry and out of ReleaseDynamic / RecreateDynamic,
// so nothing but this file writes its companion. The companion holds a reference to the SRV the water
// reads (the shared copy's view, and the texture behind it), so the engine's own bind path
// (ImageBufferData::Bind: PSSetSamplers + PSSetShaderResources) binds it at t4; its sampler stays null
// (the shader Loads).
//
// LIFETIME. The copy's objects are depth_copy.cpp's. Image() hands the image out for the D3D device of
// the last Copy() that found a copy (a frame with no depth bound keeps the last one, as before).
// Request(false) clears the image's view, under the device lock (the drawing thread holds it while it
// records a frame, so no draw of the frame can be binding the copy).
// ============================================================================
#include "better_water/scene_depth_backend.h"

#include "depth_copy/depth_copy.h"
#include "dx11_backend_access.h"
#include "video/Context.h"
#include "video/Device.h"
#include "video/ImageBuffer.h"

#include <atomic>
#include <cstdint>
#include <cstdio>

namespace {

template <class T> void Rel(T*& p) {
    if (p != nullptr) {
        p->Release();
        p = nullptr;
    }
}

ID3D11Device* LiveDevice() {
    const ::Video::Device* const dev = ::Video::Device::_singleton;
    if (dev == nullptr || dev->deviceData == nullptr) return nullptr;
    return VideoDX11::Dx11DeviceDataOf(dev)->d3dDevice;
}

struct CopyState {
    ID3D11Device*         device = nullptr;   // the device of the view the image holds (compared only)
    bool                  ready  = false;     // a Copy() found a copy: Image() may hand it out
    ::Video::ImageBuffer* image  = nullptr;   // the engine image; made once, never deleted
};
CopyState g_copy;

std::atomic<bool> g_requested{false};

// The SRV the image hands out (and the texture behind it), held by its companion with a reference of
// its own. Null clears it.
void Expose(ID3D11ShaderResourceView* srv) {
    VideoDX11::ImageBufferData* const data = VideoDX11::Dx11ImageBackend(g_copy.image);
    if (data->shaderResourceView == srv) return;
    Rel(data->shaderResourceView);
    Rel(data->texture);
    if (srv == nullptr) return;
    srv->AddRef();
    data->shaderResourceView = srv;
    ID3D11Resource* resource = nullptr;
    srv->GetResource(&resource);
    if (resource != nullptr) {
        resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&data->texture));
        resource->Release();
    }
}

void ReleaseAll() {
    if (g_copy.image != nullptr) Expose(nullptr);
    g_copy.device = nullptr;
    g_copy.ready  = false;
}

// The copy itself; Copy() holds the device lock around it.
void CopyLocked(const ::Video::Context& context) {
    ID3D11ShaderResourceView* const view =
        VideoDX11::DepthCopy::Acquire(context, VideoDX11::DepthCopy::Source::Bound);
    if (view == nullptr) return;   // no depth bound (not the scene's target) or none made: as before
    if (g_copy.image == nullptr)
        g_copy.image = static_cast<::Video::ImageBuffer*>(::Video::ImageBuffer::CreateNewObject());
    if (g_copy.image == nullptr) return;
    Expose(view);
    ID3D11Resource* resource = nullptr;
    view->GetResource(&resource);
    if (resource == nullptr) return;
    ID3D11Texture2D* texture = nullptr;
    resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&texture));
    resource->Release();
    if (texture == nullptr) return;
    D3D11_TEXTURE2D_DESC desc;
    texture->GetDesc(&desc);
    texture->Release();
    g_copy.image->_width  = static_cast<int32_t>(desc.Width);
    g_copy.image->_height = static_cast<int32_t>(desc.Height);
    g_copy.device = VideoDX11::Dx11Device();
    g_copy.ready  = true;
}

}  // namespace

namespace SceneDepthBackend {
BANISHED_RENDER_IMPL_BEGIN

bool Supported() {
    ID3D11Device* const d = LiveDevice();
    // The MSAA resolve's shaders are vs_5_0 / ps_5_0 (tools/ao_shaders.py), as AO's.
    return d != nullptr && d->GetFeatureLevel() >= D3D_FEATURE_LEVEL_11_0;
}

void Request(bool on) {
    const bool was = g_requested.exchange(on);
    VideoDX11::DepthCopy::Request(VideoDX11::DepthCopy::kReaderSceneDepth, on);
    if (on || !was) return;
    ::Video::Device* const dev = ::Video::Device::_singleton;
    if (dev == nullptr) return;
    dev->Lock();
    ReleaseAll();
    dev->Unlock();
    std::fprintf(stderr, "[SceneDepth] copy released (not requested)\n");
}

void Copy(void* context) {
    if (!g_requested.load() || !Supported() || context == nullptr) return;
    ::Video::Device* const dev = ::Video::Device::_singleton;
    dev->Lock();
    // FrameCopyImageBuffer::Generate's context: the engine's Video::Context (RenderBufferData::Resolve's
    // first argument); the depth copied is the one bound on the immediate context.
    CopyLocked(*static_cast<const ::Video::Context*>(context));
    dev->Unlock();
}

const Video::ImageBuffer* Image() {
    if (!g_requested.load() || !g_copy.ready || g_copy.image == nullptr) return nullptr;
    // A device restart since the last Copy(): the copy belongs to the old device until the next one.
    if (g_copy.device != LiveDevice()) return nullptr;
    return g_copy.image;
}

BANISHED_RENDER_IMPL_END
}  // namespace SceneDepthBackend
