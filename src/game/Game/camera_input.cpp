// src/game/Game/camera_input.cpp
// In-game camera controller — keyboard/mouse driven view movement.
//
// Clean reimplementation of two helpers that live inside the tool manager's
// Update (0x1401026a0; the object at GameUpdateLoop+0x120, PlayerToolManager):
//   FUN_1400feec0  PlayerToolManager::UpdateCamera  — WASD pan, Q/E rotate, wheel zoom, pitch
//   FUN_1400ff6e0  PlayerToolManager::UpdateHotkeys — TogglePause, the tool's OnKey, camera presets, right-click cancel
//   FUN_1400fe8d0  PlayerToolManager::ApplyCameraView — one preset
//
// Their caller is PlayerToolManager::Update (0x1401026a0, Dialog/entityui.cpp).
//
// ── Camera state lives on the PlayerController at GameWorld+0xE0 ──
//   +0x1c,+0x20,+0x24  focus/target position (x,y,z) — what panning moves
//   +0x28              yaw    (degrees)               — Q/E rotate
//   +0x2c              pitch  (degrees, auto/smoothed)
//   +0x30              distance (smoothed)            — scroll-wheel zoom
//   +0x98..+0xa0       camera basis (right vector)    — NOT populated in standalone,
//                                                        so we derive it from yaw.
// ── Controller (EntityUI, GameUpdateLoop+0x120) smoothing state ──
//   +0x30 player(spacebar) pause flag   +0x38 wheel-allow   +0x3c zoom target
//   +0x40 pitch target   +0x44 manual-pitch 0..1   +0x48 timer
//   +0x70 camera-preset table (5 × 0x1c)   +0x80 tool-active
//   +0xa4 follow flag   +0xa8/+0xac follow target cells
//
// KeyMap input IDs (see inputbindings.cpp): 0/1 mouseXY, 2 mouseDeltaX, 4 wheel,
//   7 mid-mouse, 9/10 rotate L/R, 0xb/0xc forward/back, 0xd/0xe left/right,
//   0xf/0x10 zoom in/out, 0x11/0x12 pitch up/down, 0x17 toggle-pause, 0x2d..0x31
//   camera presets 0..4.
//
// All magic constants are taken verbatim from the exe .rdata (addresses noted).

#include "Game/exe_bodies.h"   // B3R STORAGE: g_gameSettings
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Game/GameUpdateLoop.h"   // caller batch CB6: the world's named fields
#include "Game/Map.h"   // m_game's class (B.4 GAME-a)
#include "Game/PlayerToolManager.h"   // the camera bodies' class (B.4 GAME-a)
#include "Game/PlayerController.h"    // m_playerController's class
#include "options.h"
#include "showroom.h"          // BANISHED_SHOWROOM camera focus (src/diag/)
#include "Game/exe_bodies.h"   // Reimpl_MapGetTerrainPos
#include <cstdint>
#include <cstring>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include "system/Value.h"      // ENDGAME B.3.4b System caller batch (Random/Time/Color)
#include "input/Input.h"       // ENDGAME B.3.7 callers (K3)
#include "video/Device.h"   // ENDGAME B.3.9 SS2: Video::Device::_singleton
#include "Entity/ComponentEntity.h"   // B4 S4 RT2: InternalPtr<ComponentEntity>::Get() needs the class complete
#include "Player/Tool.h"            // UpdateHotkeys: the current tool's OnKey / OnCancel
#include "ext/registry.h"             // EXT SEAM (NOT RETAIL, docs/extensions/handins/EYECAM.md, BETTERCAM.md): Ext::Ext()
#include "ext/eye_camera/eye_camera.h"   // EXT SEAM (NOT RETAIL, docs/extensions/handins/EYECAM.md): ext.eye_camera's zoom
#include "ext/better_camera/better_camera.h"   // EXT SEAM (NOT RETAIL, docs/extensions/handins/BETTERCAM.md): ext.better_camera's zoom range + steady tilt
#include "ext/touch_mode/touch_mode.h"   // EXT SEAM (NOT RETAIL, docs/android/TOUCH_MODE.md): ext.touch_mode's camera controls
#include "ext/touch_pointer/touch_pointer.h"   // EXT SEAM (NOT RETAIL, docs/android/P11_TOUCH_POINTER.md): ext.touch_pointer's bar

extern "C" bool StartTown_GetSpawnTile(int* x, int* y);   // starttown.cpp (BANISHED_CAMERA_PIN=town)

namespace Game {

namespace {

// ── .rdata constants (Application-x64.exe), decoded from raw bytes ──
constexpr float kDeg2Rad      = 0.01745329f; // 0x1401720cc
constexpr float kRotSpeed     = 90.0f;       // 0x140172204  deg/sec keyboard rotate
constexpr float kMidDragScale = 0.2f;        // 0x1401720fc  mouse-middle drag rotate
constexpr float kPitchStep    = 0.125f;      // 0x1401720ec  manual pitch increment
constexpr float kZoomStep     = 5.0f;        // 0x140172198  distance per zoom action
constexpr float kZoomMin      = 20.0f;       // 0x1401721c4
constexpr float kZoomMax      = 130.0f;      // 0x140172218
constexpr float kZoomSpan     = 110.0f;      // 0x14017220c  (130-20)
constexpr float kPanSpeedFar  = 70.0f;       // 0x1401721f8  pan speed when zoomed out
constexpr float kPanSpeedNear = 12.0f;       // 0x1401721b8  pan speed when zoomed in
constexpr float kPitchFarMan  = -65.0f;      // 0x140172298
constexpr float kPitchFarFlat = 85.0f;       // 0x140172200
constexpr float kPitchNearMan = -25.0f;      // 0x140172290
constexpr float kPitchNearFlt = 60.0f;       // 0x1401721f0
constexpr float kSmoothNew    = 0.1f;        // 0x1401720e8  exp-smoothing alpha
constexpr float kSmoothOld    = 0.9f;        // 0x140172134
constexpr float kClampBase    = 100.0f;      // 0x140172208
constexpr float kClampSpan    = 150.0f;      // 0x14017221c
constexpr float kHalf         = 0.5f;        // 0x140172114
constexpr float kFollowSnap   = 0.1f;        // 0x1401720e4
constexpr float kFollowLerp   = 0.2f;        // 0x1401720f8  (1 - kSmoothOld_follow)
constexpr float kFollowKeep   = 0.8f;        // 0x140172128

inline float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// KeyMap accessor (the triple-deref exref pattern, same as inputbindings.cpp):
// Input::Configuration's first field.
inline const Input::KeyMap* CamKeyMap() {
    if (Input::Configuration::_singleton == nullptr) return nullptr;
    return Input::Configuration::_singleton->_keyMap;
}

// f3e0 = 0x14009f3e0 grid->world: Reimpl_MapGetTerrainPos (Game/exe_bodies.h).

// Video::Device (screen dims): retail reads **exref 0x140151a80; the class static
// is that cell (ENDGAME B.3.9 SS2). +4=W, +8=H.
inline bool ScreenSize(int& w, int& h) {
    w = h = 0;
    const ::Video::Device* dev = ::Video::Device::_singleton;
    if (!dev) return false;
    w = dev->backBufferWidth;    // +0x04
    h = dev->backBufferHeight;   // +0x08
    return true;
}

} // namespace

} // namespace Game

// ============================================================
// UpdateCamera — port of FUN_1400feec0
// ============================================================
//   this      = GameUpdateLoop::m_toolManager (+0x120 controller)
//   inputFlag = "no UI input focus" (1 = gameplay has input)
//   timeTicks = frame delta as System::Time ticks (int64)
void PlayerToolManager::UpdateCamera(int inputFlag, int64_t timeTicks) {
    using namespace Game;
    if (GameUpdateLoop::_singleton == nullptr) return;

    const Input::KeyMap* keyMap = CamKeyMap();
    if (!keyMap) return;

    PlayerController* pc = GameUpdateLoop::_singleton->m_playerController;
    if (!pc) return;

    System::Time timeObj;
    timeObj._ticks = timeTicks;
    const float dt = timeObj.AsSeconds();

    // ── yaw: keyboard rotate (ids 9/10) + middle-mouse drag (id 7 + mouseDeltaX id 2) ──
    float yaw = pc->yaw;
    if (keyMap->GetValue(0, 9))  yaw -= dt * kRotSpeed;   // RotateLeft
    if (keyMap->GetValue(0, 10)) yaw += dt * kRotSpeed;   // RotateRight
    if (keyMap->GetValue(0, 7)) {                          // MouseMiddle drag
        short ddx = keyMap->GetValue(0, 2);
        yaw -= (float)ddx * kMidDragScale;
    }

    // ── pan input axes: edge-scroll (optional) then keyboard overrides ──
    float panX = 0.0f; // left/right axis
    float panY = 0.0f; // forward/back axis
    // Game-settings byte +0x14 (DAT_1401c3de0 -> settings base): edge-scroll enabled.
    if (g_gameSettings != nullptr && g_gameSettings->edgeScroll != 0) {
        int sw, sh; ScreenSize(sw, sh);
        unsigned mx = (unsigned)(uint16_t)keyMap->GetValue(0, 0);
        unsigned my = (unsigned)(uint16_t)keyMap->GetValue(0, 1);
        if (mx < 4)                               panX = -1.0f;
        if ((int)mx > sw - 5 && (int)mx < sw)     panX =  1.0f;
        if (my < 4)                               panY = -1.0f;
        if ((int)my > sh - 5 && (int)my < sh)     panY =  1.0f;
    }
    if (keyMap->GetValue(0, 0xd)) panX = -1.0f; // Left
    if (keyMap->GetValue(0, 0xe)) panX =  1.0f; // Right
    if (keyMap->GetValue(0, 0xb)) panY = -1.0f; // Forward
    if (keyMap->GetValue(0, 0xc)) panY =  1.0f; // Back

    // ── zoom target (+0x3c) init + wheel/key input ──
    if (field_0x3C == 0.0f)
        field_0x3C = pc->distance; // seed from current distance

    short wheel = keyMap->GetValue(0, 4);
    if (inputFlag == 0 && !field_0x38) wheel = 0; // no wheel zoom if UI focused (+0x38)
    int zoomDir;
    if (keyMap->Pressed(0, 0xf)) {          // ZoomIn
        zoomDir = 1;
    } else if (keyMap->Pressed(0, 0x10)) {  // ZoomOut
        zoomDir = -1;
    } else {
        zoomDir = wheel;
    }
    // EXT SEAM (NOT RETAIL, docs/android/TOUCH_MODE.md): ext.touch_mode's held control (pad, rotate,
    // zoom) drives the same locals the keys above drive; a key that set one keeps it.
    // EXT SEAM (NOT RETAIL, docs/android/P11_TOUCH_POINTER.md): ext.touch_pointer's two-finger camera
    // reaches the same locals through touch_mode's hold, and its action bar follows the current tool.
    if (Ext::Ext().touch_mode || Ext::Ext().touch_pointer || Ext::TouchMode::Shown())
        Ext::TouchMode::CameraInput(dt, yaw, panX, panY, zoomDir);
    if (Ext::Ext().touch_pointer || Ext::TouchPointer::Shown())
        Ext::TouchPointer::GameFrame();

    // EXT SEAM (NOT RETAIL, docs/extensions/handins/EYECAM.md): while
    // ext.eye_camera follows a citizen the zoom moves the eye's own distance
    // and retail's zoom target (+0x3C) is left alone.
    if (Ext::Ext().eye_camera && Ext::EyeCamera::Active())
        zoomDir = Ext::EyeCamera::Zoom(zoomDir);

    // ── manual pitch accumulator +0x44 (0..1) from PitchUp/PitchDown (ids 0x11/0x12) ──
    if (keyMap->Pressed(0, 0x11))
        field_0x44 += kPitchStep;
    else if (keyMap->Pressed(0, 0x12))
        field_0x44 -= kPitchStep;
    field_0x44 = clampf(field_0x44, 0.0f, 1.0f);

    // EXT SEAM (NOT RETAIL, docs/extensions/handins/BETTERCAM.md): ext.better_camera's zoom range and steady
    // tilt, the toggle read once per frame. Off = retail's own constants (the values below are the same floats).
    const bool betterCam = Ext::Ext().better_camera;
    const float zMin  = betterCam ? Ext::BetterCamera::kZoomMin  : kZoomMin;
    const float zMax  = betterCam ? Ext::BetterCamera::kZoomMax  : kZoomMax;
    const float zSpan = betterCam ? Ext::BetterCamera::kZoomSpan : kZoomSpan;
    // Row off: a zoom target outside retail's range (a save, or a camera preset, written with the row on) goes back into
    // 20 .. 130 before anything reads it. A no-op for every value retail itself can hold; retail's own seeding of a zero
    // target from the distance (above) has already run.
    if (!betterCam)
        field_0x3C = Ext::BetterCamera::ClampToRetailRange(field_0x3C);

    // ── apply zoom step to the zoom target, clamp 20..130 ──
    if (zoomDir != 0) {
        float z = field_0x3C + (zoomDir < 1 ? kZoomStep : -kZoomStep);
        field_0x3C = clampf(z, zMin, zMax);
    }

    // ── pitch/distance coupling ──
    const float manual  = field_0x44;                        // 0..1
    const float normZ   = (field_0x3C - zMin) / zSpan;       // 0..1
    const float pitchTarget = Ext::BetterCamera::PitchTarget(manual, normZ, kPitchFarMan, kPitchFarFlat, kPitchNearMan, kPitchNearFlt, betterCam);
    field_0x40 = pitchTarget;                                // +0x40

    // pan speed scales with zoom (near=12, far=70)
    const float panSpeed = normZ * kPanSpeedFar + (1.0f - normZ) * kPanSpeedNear;

    // commit yaw; smooth pitch and distance toward their targets
    pc->yaw = yaw;
    pc->pitch = pitchTarget * kSmoothNew + pc->pitch * kSmoothOld;
    pc->distance = pc->distance * kSmoothOld + field_0x3C * kSmoothNew;

    // ── pan: move focus along the camera's ground-plane basis ──
    // Original reads the camera right-vector from PlayerController+0x98; that field is
    // not populated in standalone, so derive it from yaw (matching Vector3::FromYawPitch:
    // view dir = (-cos·sin yaw, cos·cos yaw, sin), giving right = (cos yaw, sin yaw, 0)).
    float nlen = std::sqrt(panX * panX + panY * panY);
    if (nlen > 0.0f) {
        const float nx = panX / nlen;
        const float ny = panY / nlen;
        const float yawR = yaw * kDeg2Rad;
        const float cy = std::cos(yawR), sy = std::sin(yawR);
        const float rightX =  cy, rightY =  sy;   // camera right basis (PlayerController+0x98)
        const float fwdX   =  sy, fwdY   = -cy;   // cross(right, up=(0,0,1))
        const float aR = panSpeed * nx; // along right
        const float aF = panSpeed * ny; // along forward
        pc->target.x += (rightX * aR + fwdX * aF) * dt;
        pc->target.y += (rightY * aR + fwdY * aF) * dt;
        // z basis is 0 for both vectors, so focus.z is unaffected by panning.
    }

    // ── clamp focus to the (zoom-scaled) map bounds ──
    {
        Map* grid = GameUpdateLoop::_singleton->m_game;
        if (grid) {
            float factor = clampf(1.0f - (pc->distance - kClampBase) / kClampSpan, 0.0f, 1.0f);
            float halfExtent = (float)grid->size * kHalf * factor;
            pc->target.x = clampf(pc->target.x, -halfExtent, halfExtent);
            pc->target.y = clampf(pc->target.y, -halfExtent, halfExtent);
        }
    }

    // ── follow/center-on-entity smooth move (only when the +0xa4 flag is set) ──
    if (field_0xA4) {
        Map* grid = GameUpdateLoop::_singleton->m_game;
        if (grid) {
            const int gw = grid->size;
            const float halfCell = (float)(gw >> 1) - kHalf;
            float worldA[3] = {0,0,0}, worldB[3] = {0,0,0};
            int cellA = field_0xAC;
            int cellB = field_0xA8;
            float inA[3] = { (float)(cellA % gw) - halfCell, (float)(cellA / gw) - halfCell, 0.0f };
            float inB[3] = { (float)(cellB % gw) - halfCell, (float)(cellB / gw) - halfCell, 0.0f };
            grid->GetTerrainPos(worldA, inA, '\0', nullptr);   // 0x14009f3e0
            grid->GetTerrainPos(worldB, inB, '\0', nullptr);
            float midX = (worldA[0] + worldB[0]) * kHalf;
            float midY = (worldA[1] + worldB[1]) * kHalf;
            float* fx = &pc->target.x;
            float* fy = &pc->target.y;
            float* fz = &pc->target.z;
            *fx = midX * kFollowLerp + *fx * kFollowKeep;
            *fy = midY * kFollowLerp + *fy * kFollowKeep;
            *fz = 0.0f * kFollowLerp + *fz * kFollowKeep;
            float dx = *fx - midX, dy = *fy - midY, dz = *fz;
            if (std::sqrt(dx*dx + dy*dy + dz*dz) < kFollowSnap)
                field_0xA4 = false;
        }
    }

    // Harness (env BANISHED_CAMERA_PIN=x,y[,dist[,yaw]]): pin the camera focus to a world
    // position every frame, overriding pan/follow, so a verification run can look at a
    // known spot (e.g. the demo quarry) without keyboard input. World coords are
    // cell - (mapDim/2 - 0.5); dist defaults to 40. The optional yaw (degrees) pins the
    // camera's heading too (an azimuth pair for view-dependence checks, TS-CORE).
    // A trailing `,sweep` (HITCH_BGSAVE scenario 3) keeps the focus pinned but moves the rest of
    // the camera every call, through the same fields the keys write: the yaw turns at 40 deg/s,
    // the zoom target (+0x3C) swings 25 around the pinned distance and the manual pitch (+0x44)
    // sweeps 0..1, so the pitch target (+0x40) and the smoothed distance and pitch follow. A
    // camera that never stops, for the save-while-moving checks.
    static int s_pin = -1;
    static float s_pinX = 0, s_pinY = 0, s_pinDist = 40.0f;
    static float s_pinZ = 0.0f;   // focus height; only the showroom sets it
    static bool s_pinHasYaw = false;
    static float s_pinYaw = 0.0f;
    static bool s_sweep = false;
    static float s_sweepT = 0.0f;
    if (s_pin < 0) {
        const char* e = Opt().cameraPin;
        s_pin = 0;
        if (e && e[0]) {
            s_sweep = std::strstr(e, ",sweep") != nullptr;
            float px, py, pd, pyaw;
            int got = std::sscanf(e, "%f,%f,%f,%f", &px, &py, &pd, &pyaw);
            if (got >= 2) {
                s_pinX = px; s_pinY = py; if (got >= 3) s_pinDist = pd; s_pin = 1;
                if (got >= 4) { s_pinYaw = pyaw; s_pinHasYaw = true; }
            }
            // "town[,dist]": pin on the start-town spawn tile, whatever seed/map size
            // produced it (the stress harness uses this — the spawn tile differs per map).
            // Resolved lazily: the tile only exists once StartTown_SpawnEntities has run,
            // which is before the first camera tick of the world.
            else if (std::strncmp(e, "town", 4) == 0) {
                int tx = 0, ty = 0;
                if (StartTown_GetSpawnTile(&tx, &ty) && GameUpdateLoop::_singleton != nullptr) {
                    const Map* game = GameUpdateLoop::_singleton->m_game;
                    const int dim = game ? game->size : 0;
                    if (dim > 0) {
                        s_pinX = (float)tx - ((float)(dim >> 1) - 0.5f);
                        s_pinY = (float)ty - ((float)(dim >> 1) - 0.5f);
                        const int gotTown = std::sscanf(e + 4, ",%f,%f", &pd, &pyaw);
                        if (gotTown >= 1) s_pinDist = pd;
                        if (gotTown >= 2) { s_pinYaw = pyaw; s_pinHasYaw = true; }
                        s_pin = 1;
                        std::fprintf(stderr, "[CameraPin] town tile=(%d,%d) dim=%d -> world (%.1f,%.1f) dist=%.0f\n",
                                     tx, ty, dim, s_pinX, s_pinY, s_pinDist);
                    }
                }
                if (!s_pin) s_pin = -1;   // not ready yet — retry next tick
            }
        }
    }
    // BANISHED_SHOWROOM (src/diag/showroom.h): the pin follows the showroom's shot sequence,
    // one placed building at a time.
    {
        float focusX = 0.0f;
        float focusY = 0.0f;
        float focusZ = 0.0f;
        float focusDist = 0.0f;
        float focusYaw = 0.0f;
        if (Showroom_CameraFocus(&focusX, &focusY, &focusZ, &focusDist, &focusYaw)) {
            s_pin = 1;
            s_pinX = focusX;
            s_pinY = focusY;
            s_pinZ = focusZ;
            s_pinDist = focusDist;
            s_pinYaw = focusYaw;
            s_pinHasYaw = true;
        }
    }
    if (s_pin) {
        pc->target.x = s_pinX;
        pc->target.y = s_pinY;
        pc->target.z = s_pinZ;
        if (s_sweep) {
            s_sweepT += dt;
            pc->yaw = s_pinYaw + 40.0f * s_sweepT;
            field_0x3C = clampf(s_pinDist + 25.0f * std::sin(s_sweepT * 2.0f), kZoomMin, kZoomMax);
            field_0x44 = 0.5f + 0.5f * std::sin(s_sweepT * 1.3f);
        } else {
            pc->distance = s_pinDist;
            field_0x3C = s_pinDist;   // zoom target, so smoothing doesn't fight it
            if (s_pinHasYaw) pc->yaw = s_pinYaw;
        }
    }


    // ── decrement the controller's input timer (+0x48) ──
    int64_t* tmr = &field_0x48;
    *tmr -= timeTicks;
    if (*tmr < 0) *tmr = 0;

}

// ============================================================
// Apply one camera preset (port of FUN_1400fe8d0)
// ============================================================
void PlayerToolManager::ApplyCameraView(int index) {
    PlayerCameraView* table = cameraViews._data;
    if (!table) return; // no preset table in standalone yet
    const PlayerCameraView* p = &table[index];
    if (!p->used) return; // preset slot not set

    PlayerController* pc = GameUpdateLoop::_singleton->m_playerController;
    if (!pc) return;
    pc->distance = p->field_0x14; // distance
    pc->yaw = p->field_0x0C;      // yaw
    pc->target.x = p->position.x;  // focus x
    pc->target.y = p->position.y;  // focus y
    pc->target.z = p->position.z;  // focus z
    pc->pitch = p->field_0x10;    // pitch
    field_0x3C = p->field_0x14;   // zoom target
    field_0x40 = p->field_0x10;   // pitch target
    // Re-run movement with dt=0 to settle smoothing onto the preset immediately.
    UpdateCamera(0, 0);
}

// ============================================================
// UpdateHotkeys — 0x1400ff6e0 (EntityUI::Update's input half)
// ============================================================
// TogglePause (0x17) unless the pause is locked (GameWorld +0xd0 bit 10); then
// the current tool's OnKey (slot 6, +0x30) with the input flag, and only when it
// returns non-zero: the five camera views (0x2d..0x31) and, with the input
// flag, the right-click cancel (action 6) while no tool is forced (+0x80 == 0):
// the tool's OnCancel (slot 9, +0x48) answering 0 drops back to the EditTool.
// No null test on the key map, the world or the tool: retail has none.
void PlayerToolManager::UpdateHotkeys(bool inputFlag) {
    const Input::KeyMap* keyMap = Input::Configuration::_singleton->_keyMap;
    if (keyMap->Pressed(0, 0x17) && ((GameUpdateLoop::_singleton->m_gameFlags >> 10) & 1) == 0)
        paused = !paused;
    if (currentTool.Get()->OnKey(inputFlag)) {
        for (int i = 0; i < 5; ++i) {
            if (keyMap->Pressed(0, i + 0x2d))
                ApplyCameraView(i);
        }
        if (inputFlag && keyMap->Pressed(0, 6) && !field_0x80) {
            if (!currentTool.Get()->OnCancel())
                SelectTool(nullptr);
        }
    }
}
