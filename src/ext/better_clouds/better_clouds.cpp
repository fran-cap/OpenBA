// ============================================================================
// src/ext/better_clouds/better_clouds.cpp -- ext.better_clouds (docs/extensions/handins/BETTERCLOUDS.md)
// ============================================================================
// NOT RETAIL. better_clouds.h has the design; cloud_params.h the motion and the constants; cloud_noise.h
// the texture; sky_fit.h the program test.
//
// LIFETIME. Our pixel programs and the twins are made on the first frame the row is on after a world
// load or a device restart, and live for the process (never deleted: a backend may still hold one in
// cached state, and they are small). The noise is baked once per process (loader thread) and its levels
// kept, so a device restart makes the texture again without baking.
// ============================================================================

#include "ext/better_clouds/better_clouds.h"
#include "ext/better_clouds/cloud_noise.h"
#include "ext/better_clouds/cloud_params.h"
#include "ext/better_clouds/sky_fit.h"

#include "ext/material_setup.h"
#include "ext/registry.h"
#if BANISHED_TWEAK_OVERLAY
#include "ext/tweak/ext_tweaks.h"   // the debug overlay's live Look
#endif

#include "better_clouds/cloud_shadow_backend.h"   // src/render/common: Fancy's pass (DX11, Vulkan; Metal unsupported)
#include "better_clouds/cloud_sky_cheap_ps.h"   // src/render/common: g_bc_PSCloudSkyCheap (tools/water_shaders.py)
#include "better_clouds/cloud_sky_ps.h"         // src/render/common: g_bc_PSCloudSky

#include "Game/GameUpdateLoop.h"
#include "graphics/Camera.h"
#include "graphics/Material.h"
#include "graphics/Mesh.h"
#include "graphics/SunLight.h"
#include "system/Value.h"   // System::Time
#include "video/Context.h"
#include "video/Device.h"
#include "video/ImageBuffer.h"
#include "video/ShaderProgram.h"
#include "video/VideoProgram.h"
#include "math/Math.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace Ext {
namespace BetterClouds {

namespace {

// ---- the sky (main thread, but filled by SkyLoaded on the loader thread: g_skyMutex) ------------

constexpr int kMaxMaterials = 4;   // the sky mesh has one material instance in the installed data
constexpr int kMaxPasses = 8;      // SkyMaterial.rsc lists three passes (normal, depth, reflect)

struct Sky {
    Graphics::Material* material = nullptr;
    bool checked = false;                              // the twins were made (or refused) for it
    int32_t passCount = 0;
    Video::VideoProgram* installed[kMaxPasses] = {};   // the material's own programs, per pass
    Video::VideoProgram* twin[kMaxPasses] = {};        // ours, or null where the pass keeps its own
};

std::mutex g_skyMutex;
Sky g_skies[kMaxMaterials];
int g_skyCount = 0;
bool g_swapped = false;
// A sky to look at: until then BeginFrame and Advance return at once.
std::atomic<bool> g_active{false};

// The sky's material instances, as the setup hook compares them (drawing thread).
std::atomic<const Graphics::MaterialInstance*> g_instances[kMaxMaterials];

// ---- our programs (main thread) -----------------------------------------------------------------

Video::PixelProgram* g_pixel[2] = {};   // full, cheap
bool g_pixelTried[2] = {};

Video::PixelProgram* CloudPixel(int which) {
    if (g_pixelTried[which]) return g_pixel[which];
    g_pixelTried[which] = true;
    const uint8_t* bytes = which == 0 ? g_bc_PSCloudSky : g_bc_PSCloudSkyCheap;
    const int size = static_cast<int>(which == 0 ? sizeof(g_bc_PSCloudSky) : sizeof(g_bc_PSCloudSkyCheap));
    // The factory's own create (what a package load runs): the class operator new is not ported.
    Video::PixelProgram* p = static_cast<Video::PixelProgram*>(Video::PixelProgram::CreateNewObject());
    if (p == nullptr || p->_data == nullptr) {
        std::fprintf(stderr, "[BetterClouds] no device: the cloud program was not made\n");
        return nullptr;
    }
    p->_byteCode.SetSize(1);   // one permutation: a backend binds variant 0 for every detail
    Collection::Array<uint8_t>& blob = p->_byteCode[0];
    blob.SetSize(size);
    std::memcpy(blob._data, bytes, static_cast<size_t>(size));
    if (!p->PlatformCreate()) {
        std::fprintf(stderr, "[BetterClouds] the device refused the cloud program: the sky stays as installed\n");
        return nullptr;
    }
    std::fprintf(stderr, "[BetterClouds] %s cloud program made (%d bytes)\n", which == 0 ? "sky" : "reflection", size);
    g_pixel[which] = p;
    return p;
}

const Collection::Array<uint8_t>* FirstVariant(const Video::ShaderProgram* p) {
    if (p == nullptr || p->_byteCode._size < 1) return nullptr;
    const Collection::Array<uint8_t>& b = p->_byteCode._data[0];
    return b._size > 0 ? &b : nullptr;
}

// The pass's twin, or null where it keeps its own program (no vertex program -- the depth pass -- or an
// installed vertex program ours does not fit).
Video::VideoProgram* MakeTwin(const Video::VideoProgram* pass, int32_t index) {
    if (pass == nullptr || pass->_vertexProgram.Get() == nullptr || pass->_pixelProgram.Get() == nullptr) {
        return nullptr;
    }
    const int which = index == 0 ? 0 : 1;   // the sky's own pass, else its reflection
    Video::PixelProgram* ours = CloudPixel(which);
    if (ours == nullptr) return nullptr;
    const Collection::Array<uint8_t>* vs = FirstVariant(pass->_vertexProgram.Get());
    if (vs == nullptr) return nullptr;
    const uint8_t* ps = which == 0 ? g_bc_PSCloudSky : g_bc_PSCloudSkyCheap;
    const size_t psSize = which == 0 ? sizeof(g_bc_PSCloudSky) : sizeof(g_bc_PSCloudSkyCheap);
    std::string why;
    if (!PixelFitsVertex(ps, psSize, vs->_data, static_cast<size_t>(vs->_size), &why)) {
        std::fprintf(stderr, "[BetterClouds] sky pass %d keeps its own program: %s\n", index, why.c_str());
        return nullptr;
    }
    Video::VideoProgram* t = static_cast<Video::VideoProgram*>(Video::VideoProgram::CreateNewObject());
    if (t == nullptr || t->_data == nullptr) return nullptr;
    t->_vertexLayout  = pass->_vertexLayout;
    t->_vertexProgram = pass->_vertexProgram;
    t->_pixelProgram  = ours;
    t->_renderState   = pass->_renderState;
    t->PlatformCreate();
    return t;
}

void Check(Sky& s) {
    s.checked = true;
    const Graphics::Material* m = s.material;
    s.passCount = m->_passes._size < kMaxPasses ? m->_passes._size : kMaxPasses;
    int made = 0;
    for (int32_t i = 0; i < s.passCount; ++i) {
        s.installed[i] = m->_passes._data[i].Get();
        s.twin[i] = MakeTwin(s.installed[i], i);
        if (s.twin[i] != nullptr) ++made;
    }
    std::fprintf(stderr, "[BetterClouds] sky material %p: %d of %d passes draw the clouds\n",
                 static_cast<const void*>(m), made, static_cast<int>(s.passCount));
}

// Every pass slot to the twin (on) or the installed program (off). A slot that holds neither (the
// material changed under us) is left alone. Caller holds g_skyMutex.
void Point(bool on) {
    for (int k = 0; k < g_skyCount; ++k) {
        Sky& s = g_skies[k];
        if (s.material == nullptr) continue;
        if (on && !s.checked) Check(s);
        for (int32_t i = 0; i < s.passCount; ++i) {
            if (s.twin[i] == nullptr) continue;
            Object::ExternalPtr<Video::VideoProgram>& slot = s.material->_passes._data[i];
            Video::VideoProgram* from = on ? s.installed[i] : s.twin[i];
            if (slot.Get() == from) slot = on ? s.twin[i] : s.installed[i];
        }
    }
    g_swapped = on;
}

// ---- the noise texture --------------------------------------------------------------------------

constexpr int32_t kFormatBGRA8 = 5;   // ImageBuffer format 5 = DXGI B8G8R8A8_UNORM
constexpr int32_t kUsageDefault = 2;
constexpr int32_t kWrap = 0;
constexpr int32_t kAnisotropic = 2;
constexpr uint32_t kMipmapped = 1u;   // ImageBuffer::_flags bit 0: the levels are a full chain

std::once_flag g_bakeOnce;
std::vector<std::vector<uint8_t>> g_noiseLevels;   // BGRA8, level 0 first; written once (g_bakeOnce)
Video::ImageBuffer* g_noise = nullptr;
bool g_noiseTried = false;
// The texture the setup hook binds (drawing thread): null until made, and while the device is away.
std::atomic<const Video::ImageBuffer*> g_noiseBound{nullptr};

// Straight RGBA8 -> BGRA8 level 0, then each next level the 2x2 box mean of the one above (a box of a
// power-of-two tile still tiles).
void Bake() {
    const std::vector<uint8_t> rgba = BakeNoise(kNoiseSize);
    std::vector<uint8_t> top(rgba.size());
    for (size_t i = 0; i < top.size(); i += 4) {
        top[i + 0] = rgba[i + 2];
        top[i + 1] = rgba[i + 1];
        top[i + 2] = rgba[i + 0];
        top[i + 3] = rgba[i + 3];
    }
    g_noiseLevels.push_back(std::move(top));
    int32_t s = kNoiseSize;
    while (s > 1) {
        const int32_t n = s / 2;
        const std::vector<uint8_t>& up = g_noiseLevels.back();
        std::vector<uint8_t> down(static_cast<size_t>(n) * static_cast<size_t>(n) * 4u);
        for (int32_t y = 0; y < n; ++y) {
            for (int32_t x = 0; x < n; ++x) {
                for (int32_t c = 0; c < 4; ++c) {
                    const size_t r0 = (static_cast<size_t>(2 * y) * s + 2 * x) * 4 + c;
                    const size_t r1 = (static_cast<size_t>(2 * y + 1) * s + 2 * x) * 4 + c;
                    const int sum = up[r0] + up[r0 + 4] + up[r1] + up[r1 + 4];
                    down[(static_cast<size_t>(y) * n + x) * 4 + c] = static_cast<uint8_t>((sum + 2) / 4);
                }
            }
        }
        g_noiseLevels.push_back(std::move(down));
        s = n;
    }
}

// The GPU texture from the kept levels, through the engine's own Create (what a package load runs).
bool CreateOnDevice(Video::ImageBuffer* image) {
    Video::ImageBuffer::LevelArray levels;
    levels.SetSize(static_cast<int>(g_noiseLevels.size()));
    for (size_t i = 0; i < g_noiseLevels.size(); ++i) {
        Collection::Array<uint8_t>& level = levels[static_cast<int>(i)];
        level.SetSize(static_cast<int>(g_noiseLevels[i].size()));
        std::memcpy(level.GetData(), g_noiseLevels[i].data(), g_noiseLevels[i].size());
    }
    return image->Create(0, kUsageDefault, kFormatBGRA8, kMipmapped, kNoiseSize, kNoiseSize, kAnisotropic, kWrap,
                         kWrap, kWrap, &levels);
}

bool EnsureNoise() {
    if (g_noise != nullptr) return true;
    if (g_noiseTried || g_noiseLevels.empty()) return false;
    g_noiseTried = true;
    Video::ImageBuffer* image = new Video::ImageBuffer();   // the class's operator new (MemAlloc)
    if (image == nullptr || !CreateOnDevice(image)) {
        std::fprintf(stderr, "[BetterClouds] the device refused the noise texture: the sky stays as installed\n");
        delete image;
        return false;
    }
    g_noise = image;
    g_noiseBound.store(image, std::memory_order_release);
    return true;
}

// ---- the draw's constants -----------------------------------------------------------------------

// The look: the shipped defaults, or the debug overlay's live values in a build that has it.
Look CurrentLook() {
#if BANISHED_TWEAK_OVERLAY
    return Ext::Tweak::CloudsValues();
#else
    return Look{};
#endif
}

Motion g_motion;   // main thread
std::mutex g_constMutex;
float g_const[16] = {};   // published by BeginFrame, read by the setup hook

void AfterSetup(const Graphics::MaterialInstance& instance, Video::Context& context) {
    bool sky = false;
    for (const auto& slot : g_instances) sky = sky || slot.load(std::memory_order_relaxed) == &instance;
    if (!sky) return;
    const Video::ImageBuffer* noise = g_noiseBound.load(std::memory_order_acquire);
    if (noise == nullptr) return;
    {
        const std::lock_guard<std::mutex> lock(g_constMutex);
        static_assert(sizeof(g_const) == sizeof(context._pixelConstantStaging.material), "four float4s");
        std::memcpy(context._pixelConstantStaging.material, g_const, sizeof(g_const));
    }
    context._dirty |= 0x8000u;   // the material constants (Context.h: the real bit, unnamed)
    context.SetImageBuffer(noise, 1);
}

// Fancy's shadow pass (the ground-draw seam, drawing thread): the frame's constants from what BeginFrame
// published, the camera the seam hands over and what the frame's draws already put in the context (the
// sky's weather blend, the fog).
void DrawShadows(Video::Context& context, const Graphics::Camera& camera) {
    const Video::ImageBuffer* noise = g_noiseBound.load(std::memory_order_acquire);
    if (noise == nullptr) return;
    float c[16];
    {
        const std::lock_guard<std::mutex> lock(g_constMutex);
        std::memcpy(c, g_const, sizeof(c));
    }
    const Look look = CurrentLook();
    const Math::Vector4& user = context._pixelConstantStaging.pixel[4];   // userPosition
    const ShadowTerms terms = ShadowTermsFor(look, user.x, user.w, c[10], context._fogParams.x, context._fogParams.y);
    if (!(terms.strength > 0.0f)) return;

    CloudShadowBackend::Frame f = {};
    // Context::SetCamera's view-projection (view = the camera-to-world inverted), inverted.
    const Math::Matrix cameraToWorld = camera._cameraToWorld.AsMatrix();
    const Math::Matrix viewProjection = cameraToWorld.Inverse() * camera._projection;
    const Math::Matrix inverse = viewProjection.Inverse();
    std::memcpy(f.invViewProj, inverse.m, sizeof(f.invViewProj));
    for (int i = 0; i < 4; ++i) f.off01[i] = c[i];
    f.offW[2] = c[6];
    f.offW[3] = c[7];
    f.sun[0] = c[8];
    f.sun[1] = c[9];
    f.sun[2] = c[10];
    f.sun[3] = look.shadowHeight;
    f.look[0] = look.shadowScale;
    f.look[1] = terms.coverage;
    f.look[2] = terms.strength;
    f.look[3] = look.shadowSoftness;
    f.screen[2] = terms.fadeFrom;
    f.screen[3] = terms.fadeTo;
    f.camera[0] = cameraToWorld.m[12];
    f.camera[1] = cameraToWorld.m[13];
    f.camera[2] = cameraToWorld.m[14];
    CloudShadowBackend::Draw(context, noise, f);
}

}  // namespace

bool Enabled() { return Ext::Ext().better_clouds; }

GroundFn ShadowDraw() {
    const bool wanted = g_active.load(std::memory_order_acquire) && g_swapped && Ext::Val().better_clouds_mode >= 2;
    CloudShadowBackend::Request(wanted);
    if (!wanted || !CloudShadowBackend::Supported()) return nullptr;
    return &DrawShadows;
}

void SkyLoaded(const Graphics::MeshInstance* sky) {
    std::call_once(g_bakeOnce, Bake);
    const std::lock_guard<std::mutex> lock(g_skyMutex);
    g_skyCount = 0;
    g_swapped = false;
    for (auto& slot : g_instances) slot.store(nullptr, std::memory_order_relaxed);
    if (sky == nullptr) return;
    for (int32_t i = 0; i < sky->_materials._size && g_skyCount < kMaxMaterials; ++i) {
        const Graphics::MaterialInstance* mi = sky->_materials._data[i].Get();
        Graphics::Material* m = mi != nullptr ? mi->_material.Get() : nullptr;
        if (m == nullptr) continue;
        g_instances[g_skyCount].store(mi, std::memory_order_relaxed);
        bool seen = false;
        for (int k = 0; k < g_skyCount; ++k) seen = seen || g_skies[k].material == m;
        g_skies[g_skyCount] = Sky{};
        if (!seen) g_skies[g_skyCount].material = m;
        ++g_skyCount;
    }
    g_motion = Motion{};
    g_active.store(g_skyCount > 0, std::memory_order_release);
}

void Advance(const System::Time& frameTime, int speed) {
    if (!g_active.load(std::memory_order_relaxed)) return;
    const Look look = CurrentLook();
    g_motion.Advance(static_cast<double>(frameTime.AsSeconds()), speed, static_cast<double>(look.speed),
                     static_cast<double>(look.morph));
}

void BeginFrame() {
    if (!g_active.load(std::memory_order_acquire)) return;
    const bool on = Enabled() && EnsureNoise();
    if (on) {
        float sx = 0.0f;
        float sy = 0.0f;
        float sz = 1.0f;
        const GameUpdateLoop* loop = GameUpdateLoop::_singleton;
        if (loop != nullptr && loop->m_sunLight != nullptr) {
            // The direction the light travels; the clouds want the way to the sun.
            sx = -loop->m_sunLight->_direction.x;
            sy = -loop->m_sunLight->_direction.y;
            sz = -loop->m_sunLight->_direction.z;
        }
        float c[16];
        Constants(g_motion, CurrentLook(), sx, sy, sz, c);
        const std::lock_guard<std::mutex> lock(g_constMutex);
        std::memcpy(g_const, c, sizeof(c));
    }
    const std::lock_guard<std::mutex> lock(g_skyMutex);
    if (on != g_swapped) {
        Point(on);
        MaterialSetup::SetClouds(on ? &AfterSetup : nullptr);
    }
}

void Forget() {
    CloudShadowBackend::Request(false);   // the ground draw is not armed again until the next world
    if (!g_active.load(std::memory_order_acquire)) return;
    const std::lock_guard<std::mutex> lock(g_skyMutex);
    if (g_swapped) {
        Point(false);
        MaterialSetup::SetClouds(nullptr);
    }
    for (Sky& s : g_skies) s = Sky{};
    for (auto& slot : g_instances) slot.store(nullptr, std::memory_order_relaxed);
    g_skyCount = 0;
    g_active.store(false, std::memory_order_release);
}

void OnVideoLost() {
    {
        const std::lock_guard<std::mutex> lock(g_skyMutex);
        if (g_swapped) {
            Point(false);
            MaterialSetup::SetClouds(nullptr);
        }
        for (int k = 0; k < g_skyCount; ++k) {
            Graphics::Material* m = g_skies[k].material;
            g_skies[k] = Sky{};
            g_skies[k].material = m;
        }
    }
    for (int w = 0; w < 2; ++w) {
        g_pixel[w] = nullptr;
        g_pixelTried[w] = false;
    }
    g_noiseBound.store(nullptr, std::memory_order_release);
    if (g_noise != nullptr) g_noise->Release();
}

void OnVideoRestored() {
    // A texture OnVideoLost released (ImageBuffer::Release: the companion deleted, _data null): a new
    // companion on the new device, as ImageBuffer::Recreate makes one, then Create with the kept levels.
    if (g_noise == nullptr || g_noise->_data != nullptr) return;
    Video::Device* dev = Video::Device::_singleton;
    if (dev == nullptr || dev->deviceData == nullptr) return;
    g_noise->_data = dev->deviceData->CreateImageBufferData(g_noise);
    if (g_noise->_data == nullptr || !CreateOnDevice(g_noise)) {
        std::fprintf(stderr, "[BetterClouds] the noise texture was not made again after the device restart\n");
        return;
    }
    g_noiseBound.store(g_noise, std::memory_order_release);
}

}  // namespace BetterClouds
}  // namespace Ext
