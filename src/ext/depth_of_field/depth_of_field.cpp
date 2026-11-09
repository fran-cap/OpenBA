// ============================================================================
// src/ext/depth_of_field/depth_of_field.cpp -- ext.depth_of_field (see depth_of_field.h)
// ============================================================================
// NOT RETAIL. docs/extensions/DOF.md.
// ============================================================================
#include "ext/depth_of_field/depth_of_field.h"

#include "ext/depth_of_field/dof_params.h"
#include "ext/eye_camera/eye_camera.h"
#include "ext/registry.h"
#include "ext/tweak/ext_tweaks.h"   // the debug overlay's live Look
#include "ext/town_screenshot/town_screenshot.h"
#include "dof/dof_backend.h"   // src/render/common: the pass (DX11, Vulkan; Metal unsupported)
#include "Game/GameUpdateLoop.h"
#include "Game/PlayerController.h"
#include "graphics/Camera.h"
#include "math/Math.h"

#include <atomic>
#include <cstring>

namespace Ext {
namespace DepthOfField {

namespace {

// The focus distance armed for this frame (main thread, SceneDraw; read by the drawing thread's Draw of the
// frame; under ext.render_pipe the render thread may still draw the previous frame: either value is a whole
// frame's). 0: the pass takes the depth at the centre of the screen.
std::atomic<float> g_focus{0.0f};

Look CurrentLook() {
#if BANISHED_TWEAK_OVERLAY
    return Ext::Tweak::DofValues();
#else
    return Look{};
#endif
}

void Draw(Video::Context& context, const Graphics::Camera& camera) {
    const Look look = CurrentLook();
    DofBackend::Frame f = {};
    const Math::Matrix cameraToWorld = camera._cameraToWorld.AsMatrix();
    const Math::Matrix viewProjection = cameraToWorld.Inverse() * camera._projection;
    const Math::Matrix inverse = viewProjection.Inverse();
    std::memcpy(f.invViewProj, inverse.m, sizeof(f.invViewProj));
    f.lens[0] = g_focus.load(std::memory_order_acquire);
    f.lens[1] = look.gain;
    f.lens[2] = look.maxCoc;
    f.lens[3] = look.band;
    f.camera[0] = cameraToWorld.m[12];
    f.camera[1] = cameraToWorld.m[13];
    f.camera[2] = cameraToWorld.m[14];
    f.camera[3] = look.boost;
    f.look[0] = look.threshold;
    f.look[1] = look.showCoc ? 1.0f : 0.0f;
    DofBackend::Draw(context, f);
}

}  // namespace

// The row (ON by default) and the Enhanced tab's "Depth blur" (ext.depth_blur, OFF by default).
bool Enabled() { return Ext::Ext().depth_of_field && Ext::Val().depth_blur != 0; }

SceneFn SceneDraw() {
    const GameUpdateLoop* loop = GameUpdateLoop::_singleton;
    const PlayerController* pc = loop != nullptr ? loop->m_playerController : nullptr;
    const bool screenshot = Ext::Ext().town_screenshot && TownScreenshot::Active();
    const bool wanted = Enabled() && pc != nullptr && !screenshot;
    DofBackend::Request(wanted);
    if (!wanted || !DofBackend::Supported()) return nullptr;
    const Look look = CurrentLook();
    const bool eye = Ext::Ext().eye_camera && EyeCamera::Active();
    float focus = pc->distance * look.focusScale;   // FocusMode::CameraTarget
    if (look.focusMode == static_cast<int>(FocusMode::Manual))
        focus = look.focusDistance;
    else if (eye || look.focusMode == static_cast<int>(FocusMode::ScreenCentre))
        focus = 0.0f;   // the pass reads the depth at the screen's centre
    g_focus.store(focus, std::memory_order_release);
    return &Draw;
}

}  // namespace DepthOfField
}  // namespace Ext
