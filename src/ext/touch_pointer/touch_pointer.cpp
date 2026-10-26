// src/ext/touch_pointer/touch_pointer.cpp -- ext.touch_pointer (NOT RETAIL; docs/android/P11_TOUCH_POINTER.md)
//
// The finger sink (Platform::Touch), the pointer writer after the input poll (Input::Device::_extAfterUpdate),
// the modes per tool, the two-finger camera, the pick snap, and the action bar / aim marker dialogs.
// touch_pointer_math.h holds the arithmetic.

#include "ext/touch_pointer/touch_pointer.h"
#include "ext/touch_pointer/touch_pointer_math.h"

#include "ext/registry.h"
#include "ext/touch_mode/touch_mode.h"    // the camera hold (Source::Fingers), the bar's and cell's sizes
#include "ext/touch_mode/touch_math.h"    // Margin: the bar keeps touch_mode's safe margin rule
#include "ext/phone_menu/phone_math.h"    // SafeMargin (read only)

#include "platform_touch.h"

#include "input/Device.h"
#include "input/Input.h"
#include "ui/ButtonElement.h"
#include "ui/Dialog.h"
#include "ui/DialogInstance.h"
#include "ui/Element.h"
#include "ui/ImageElement.h"
#include "ui/Sprite.h"
#include "ui/UserInterface.h"
#include "math/Math.h"
#include "object/External.h"
#include "object/Internal.h"
#include "object/TypeInfo.h"
#include "picking/Scene.h"
#include "system/Filename.h"
#include "system/MemoryAllocator.h"
#include "system/String.h"
#include "video/Device.h"
#include "Game/GameUpdateLoop.h"
#include "Game/Map.h"
#include "Game/PlayerController.h"
#include "Game/PlayerToolManager.h"
#include "Game/WorldGeometry.h"
#include "Entity/ComponentEntity.h"
#include "Player/Tool.h"
#include "Player/dragtool.h"
#include "Player/EditTool.h"
#include "Player/FencedTool.h"
#include "Player/PathTool.h"
#include "Player/PlacementTool.h"
#include "Player/exe_bodies.h"   // PlayerRay_GetMouseRay_1005a0: the tools' own cursor ray

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <new>
#include <vector>

namespace Ext {
namespace TouchPointer {

namespace {

constexpr const char* kBarRsc = "Dialog/TouchPointer.rsc";
constexpr const char* kMarkerRsc = "Dialog/TouchPointer.rsc:marker";

constexpr const char* kButtons[kButtonCount] = {"buttonPointerPlace", "buttonPointerRotateLeft",
                                                "buttonPointerRotateRight", "buttonPointerFlip", "buttonPointerCancel"};
constexpr const char* kHolders[kButtonCount] = {"holderPointerPlace", "holderPointerRotateLeft",
                                                "holderPointerRotateRight", "holderPointerFlip", "holderPointerCancel"};
constexpr const char* kButtonNames[kButtonCount] = {"place", "rotate left", "rotate right", "flip", "cancel"};
// The KeyMap actions the buttons send (src/game/Game/inputbindings.cpp): StructureLeft / Right / Type, MouseRight.
constexpr int32_t kButtonAction[kButtonCount] = {-1, 0x13, 0x14, 0x15, 6};

// Retail's input values (input_device_update.cpp kFullScale) and mouse inputs (inputbindings.cpp).
constexpr int16_t kFullScale = 0x7FFF;
constexpr int32_t kMouseX = 0;
constexpr int32_t kMouseY = 1;
constexpr int32_t kMouseDx = 2;
constexpr int32_t kMouseDy = 3;
constexpr int32_t kMouseLeft = 4;
constexpr int32_t kMouseRight = 5;

// After this long of real mouse use the bar hides (the ruling's "a few seconds").
constexpr double kMouseHideSeconds = 3.0;

// Polls the release over a dialog is held before the cursor goes back to the world (the Ui lift).
constexpr int32_t kReleaseHoldPolls = 3;

// ButtonElement (ButtonElement.h): state 3 = pressed (its fourth state child), child 4 = the label.
constexpr int32_t kButtonPressedState = 3;
constexpr int32_t kButtonLabelChild = 4;
constexpr float kGlyphFraction = 0.5f;

double Now() {
    using Clock = std::chrono::steady_clock;
    static const Clock::time_point start = Clock::now();
    return std::chrono::duration<double>(Clock::now() - start).count();
}

// ---- the game's context ------------------------------------------------------------------------------

enum class ToolKind { NoGame, Pick, Place, Drag, Other };

Tool* CurrentTool() {
    const GameUpdateLoop* game = GameUpdateLoop::_singleton;
    if (game == nullptr || game->m_toolManager == nullptr) return nullptr;
    return game->m_toolManager->currentTool.Get();
}

ToolKind KindOf(const Tool* tool) {
    if (GameUpdateLoop::_singleton == nullptr) return ToolKind::NoGame;
    if (tool == nullptr) return ToolKind::Pick;   // the manager installs the EditTool on its next update
    if (tool->IsA(EditTool::TypeId()) || tool->IsA(PathTool::TypeId())) return ToolKind::Pick;
    if (tool->IsA(PlacementTool::TypeId())) return ToolKind::Place;
    if (tool->IsDerivedFrom(DragTool::TypeId())) return ToolKind::Drag;
    return ToolKind::Other;
}

UI::UserInterface* GameUi() {
    const GameUpdateLoop* game = GameUpdateLoop::_singleton;
    return game != nullptr ? game->m_userInterface : nullptr;
}

// The engine's back buffer (the cursor's pixel space) and the device pixels per engine pixel.
void ScreenSize(int32_t& w, int32_t& h) {
    const Video::Device* dev = Video::Device::_singleton;
    w = dev != nullptr ? dev->backBufferWidth : 0;
    h = dev != nullptr ? dev->backBufferHeight : 0;
}

// Device pixels (the settings' unit, measured on the phone's own screen) as engine pixels: the render scale.
float DeviceToEngine(float devicePx) {
    return devicePx * Platform::Touch::EnginePixelsPerDevicePixel();
}

// ---- the fingers ---------------------------------------------------------------------------------------

enum class Role {
    None,
    Bar,       // pressed an action-bar button
    Ui,        // a dialog: a plain mouse, then back to the world position
    Aim,       // a placement / drag tool: the cursor follows (lift offset), button up
    Pick,      // no tool: a tap picks
    Plain,     // any other tool, or no game: a plain mouse
    Gesture,   // one of the two fingers of a gesture
    Ignored,   // a finger left over from a gesture, or a third one
};

constexpr const char* kRoleNames[] = {"none", "bar", "dialog", "aim", "pick", "plain", "gesture", "ignored"};

struct Finger {
    uint64_t id = 0;
    float x = 0.0f, y = 0.0f;
    float downX = 0.0f, downY = 0.0f;
    float travel = 0.0f;
    double downAt = 0.0;
    Role role = Role::None;
};

std::vector<Finger> g_fingers;
TwoFingers g_two;
GestureStep g_pendingGesture;   // summed since the last poll
bool g_gestureActive = false;
int32_t g_barPressed = -1;

LatchQueue g_queue;
std::vector<int32_t> g_actions;   // KeyMap action ids, one per poll
int16_t g_prev[Input::Device::kMouseInputCount] = {};
bool g_owns = false;        // a finger is the last pointer device
bool g_sinkOn = false;
double g_mouseAt = -1.0e9;  // the last real mouse event
PointerState g_lastWorld;   // where the cursor last was over the world
bool g_haveWorld = false;

// Tap twice and tap-to-cycle.
double g_lastTapAt = -1.0e9;
float g_lastTapX = 0.0f, g_lastTapY = 0.0f;
double g_lastPickAt = -1.0e9;
float g_lastPickX = 0.0f, g_lastPickY = 0.0f;
uintptr_t g_lastPicked = 0;

// The bar's layout (layout units) as the dialogs last used it.
Bar g_bar;
uint32_t g_shown = 0;

Finger* FindFinger(uint64_t id) {
    for (Finger& f : g_fingers) {
        if (f.id == id) return &f;
    }
    return nullptr;
}

float ShortEdge() {
    int32_t w = 0;
    int32_t h = 0;
    ScreenSize(w, h);
    const int32_t s = w < h ? w : h;
    return s > 0 ? static_cast<float>(s) : 1.0f;
}

void Move(float x, float y, bool left) {
    PointerState s;
    s.x = x;
    s.y = y;
    s.left = left;
    g_queue.Push(s);
}

// A click at the cursor's parked place: a press and a release on two polls (DragTool / PlacementTool /
// EditTool read Pressed(0, 5) then Released(0, 5)).
void ClickAtParked() {
    const PointerState at = g_queue.Last();
    Move(at.x, at.y, true);
    Move(at.x, at.y, false);
}

void ClickAt(float x, float y) {
    Move(x, y, false);
    Move(x, y, true);
    Move(x, y, false);
}

// ---- the pick snap -----------------------------------------------------------------------------------

// The entity EditTool::OnKey's ray would pick at engine pixel (x, y): the same ray (the tools' own
// PlayerRay_GetMouseRay_1005a0 at that cursor), the terrain march's building at the hit cell, then the picking
// scene capped at the terrain distance. The cursor is set to the sample for the ray and put back.
uintptr_t PickAt(float x, float y) {
    GameUpdateLoop* game = GameUpdateLoop::_singleton;
    Input::Device* dev = Input::Device::_singleton;
    if (game == nullptr || dev == nullptr || Input::Configuration::_singleton == nullptr) return 0;
    const int16_t keepX = dev->_mouse[kMouseX].value;
    const int16_t keepY = dev->_mouse[kMouseY].value;
    dev->_mouse[kMouseX].value = static_cast<int16_t>(x);
    dev->_mouse[kMouseY].value = static_cast<int16_t>(y);
    float origin[3] = {0.0f, 0.0f, 0.0f};
    float dir[3] = {0.0f, 0.0f, 0.0f};
    const bool ray = PlayerRay_GetMouseRay_1005a0(nullptr, origin, dir) != 0;
    dev->_mouse[kMouseX].value = keepX;
    dev->_mouse[kMouseY].value = keepY;
    if (!ray) return 0;

    ComponentEntity* found = nullptr;
    float maxDist = 400.0f;
    WorldGeometry* terrain = game->m_worldGeometry;
    Map* map = game->m_game;
    if (terrain != nullptr && map != nullptr) {
        float maxDistArr[2] = {400.0f, 0.0f};
        float hitPos[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        if (terrain->TerrainMarch(origin, dir, maxDistArr, hitPos, &maxDist) != 0) {
            const int32_t size = map->size;
            const int32_t half = size >> 1;
            int32_t cx = static_cast<int32_t>(hitPos[0] + static_cast<float>(half));
            int32_t cy = static_cast<int32_t>(hitPos[1] + static_cast<float>(half));
            if (cx < 0) cx = 0;
            if (cx >= size) cx = size - 1;
            if (cy < 0) cy = 0;
            if (cy >= size) cy = size - 1;
            const int32_t cell = size * cy + cx;
            if (cell > 0) {
                ComponentEntity* atCell = nullptr;
                ComponentEntity** r = map->EntityAtCell(&atCell, cell);
                if (r != nullptr) found = *r;
            }
        }
    }
    if (Picking::Scene* scene = game->m_pickingScene) {
        Math::Vector3 hitPoint(0.0f, 0.0f, 0.0f);
        Math::Vector3 hitNormal(0.0f, 0.0f, 0.0f);
        const int32_t id = scene->Intersect(Math::Vector3(origin[0], origin[1], origin[2]),
                                            Math::Vector3(dir[0], dir[1], dir[2]), Math::Scalar(maxDist), hitPoint,
                                            hitNormal);
        if (id >= 0) found = static_cast<ComponentEntity*>(scene->GetUserData(static_cast<uint32_t>(id)));
    }
    return reinterpret_cast<uintptr_t>(found);   // CAST-OK: ptr_int (an identity, never dereferenced)
}

// A tap with no tool: the candidates under and around the finger, the next one when the tap continues the
// last (tap-to-cycle), the cursor on its sample, a click. No candidate: retail's ray at the finger.
void PickTap(float x, float y) {
    const float radius = DeviceToEngine(static_cast<float>(Val().touch_pointer_pick_radius));
    const double now = Now();
    std::vector<Hit> hits;
    for (const Sample& s : RingSamples(x, y, radius)) {
        Hit h;
        h.sample = s;
        h.entity = PickAt(s.x, s.y);
        hits.push_back(h);
    }
    const std::vector<Hit> candidates = Candidates(hits);
    const float dx = x - g_lastPickX;
    const float dy = y - g_lastPickY;
    const bool continues = now - g_lastPickAt < kCycleSeconds && std::sqrt(dx * dx + dy * dy) <= radius;
    const int32_t i = ChooseCandidate(candidates, continues ? g_lastPicked : 0);
    float px = x;
    float py = y;
    g_lastPicked = 0;
    if (i >= 0) {
        px = candidates[i].sample.x;
        py = candidates[i].sample.y;
        g_lastPicked = candidates[i].entity;
    }
    g_lastPickAt = now;
    g_lastPickX = x;
    g_lastPickY = y;
    std::fprintf(stderr, "[TouchPointer] pick at %.0f,%.0f: %d candidate(s), chose %d at %.0f,%.0f%s\n", x, y,
                 static_cast<int32_t>(candidates.size()), i, px, py, continues ? " (cycle)" : "");
    ClickAt(px, py);
    g_lastWorld.x = px;
    g_lastWorld.y = py;
    g_haveWorld = true;
}

// ---- classification ------------------------------------------------------------------------------------

class BarDialog;
class MarkerDialog;
BarDialog* g_barDialog = nullptr;
MarkerDialog* g_markerDialog = nullptr;

// True when a dialog the player can touch is under engine pixel (x, y): the tool's own CreateBar, the marker
// and dialogs that pass the mouse through do not count.
bool OverDialog(float x, float y) {
    UI::UserInterface* ui = GameUi();
    if (ui == nullptr) return true;
    const int32_t lx = static_cast<int32_t>(x * ui->_uiScale);
    const int32_t ly = static_cast<int32_t>(y * ui->_uiScale);
    // GetFirstContainedDialog skips the dialogs that pass the mouse through (bit 2: the marker).
    Object::InternalPtr<UI::DialogInstance> hit = ui->GetFirstContainedDialog(lx, ly);
    const UI::DialogInstance* d = hit.Get();
    if (d == nullptr) return false;
    const PlayerToolManager* tm = GameUpdateLoop::_singleton->m_toolManager;
    if (tm != nullptr && d == tm->field_0x00) return false;   // the tool's CreateBar follows the aim
    const UI::Element* root = d->_root.Get();
    return root != nullptr && root->IsVisible();
}

Role Classify(float x, float y, ToolKind kind) {
    if (kind == ToolKind::NoGame) return Role::Plain;
    UI::UserInterface* ui = GameUi();
    if (ui != nullptr && g_barDialog != nullptr) {
        const int32_t b = HitButton(g_bar, static_cast<int32_t>(x * ui->_uiScale), static_cast<int32_t>(y * ui->_uiScale));
        if (b >= 0) {
            g_barPressed = b;
            return Role::Bar;
        }
    }
    if (OverDialog(x, y)) return Role::Ui;
    switch (kind) {
    case ToolKind::Pick:
        return Role::Pick;
    case ToolKind::Place:
    case ToolKind::Drag:
        return Role::Aim;
    default:
        return Role::Plain;
    }
}

void AimAt(const Finger& f) {
    float ax = 0.0f;
    float ay = 0.0f;
    AimPoint(f.x, f.y, DeviceToEngine(static_cast<float>(Val().touch_pointer_lift)), ax, ay);
    Move(ax, ay, false);
    g_lastWorld.x = ax;
    g_lastWorld.y = ay;
    g_haveWorld = true;
}

void PressButton(int32_t b) {
    std::fprintf(stderr, "[TouchPointer] bar %s\n", kButtonNames[b]);
    if (b == kPlace) {
        ClickAtParked();
        return;
    }
    g_actions.push_back(kButtonAction[b]);
}

// The lift of an aiming finger: in a placement tool the cursor parks (Place commits; tap twice is a setting);
// in an idle drag tool a press and a release at the parked cursor start retail's single-click mode; in
// single-click mode it parks (Confirm commits).
void AimLift(const Finger& f, ToolKind kind, double now) {
    const bool tap = IsTap(static_cast<float>(now - f.downAt), f.travel, ShortEdge());
    if (kind == ToolKind::Place) {
        if (tap && Val().touch_pointer_tap_twice != 0) {
            const float dx = f.x - g_lastTapX;
            const float dy = f.y - g_lastTapY;
            const float near = DeviceToEngine(static_cast<float>(Val().touch_pointer_pick_radius > 0
                                                                     ? Val().touch_pointer_pick_radius
                                                                     : 50));
            if (now - g_lastTapAt < kCycleSeconds && std::sqrt(dx * dx + dy * dy) <= near) {
                // The second tap places where the first aimed (the cursor did not move: this tap's aim is the
                // same spot within the radius, so it is put back first).
                Move(g_lastWorld.x, g_lastWorld.y, false);
                ClickAtParked();
                g_lastTapAt = -1.0e9;
                std::fprintf(stderr, "[TouchPointer] tap twice: place\n");
                return;
            }
        }
        if (tap) {
            g_lastTapAt = now;
            g_lastTapX = f.x;
            g_lastTapY = f.y;
        }
        return;
    }
    if (kind == ToolKind::Drag) {
        const DragTool* drag = static_cast<const DragTool*>(CurrentTool());
        if (drag != nullptr && drag->dragState == 0) {
            std::fprintf(stderr, "[TouchPointer] drag tool: start at %.0f,%.0f (single-click mode)\n",
                         g_queue.Last().x, g_queue.Last().y);
            ClickAtParked();
        }
    }
}

// One line per gesture with the camera before / after (the session doc's measurements).
GestureStep g_gestureTotal;

void LogCamera(const char* what) {
    const GameUpdateLoop* game = GameUpdateLoop::_singleton;
    const PlayerController* pc = game != nullptr ? game->m_playerController : nullptr;
    if (pc == nullptr) return;
    std::fprintf(stderr, "[TouchPointer] %s: yaw %.1f target %.1f,%.1f distance %.1f\n", what, pc->yaw, pc->target.x,
                 pc->target.y, pc->distance);
}

void EndGesture() {
    g_gestureActive = false;
    for (Finger& f : g_fingers) {
        if (f.role == Role::Gesture) f.role = Role::Ignored;
    }
    std::fprintf(stderr, "[TouchPointer] gesture end: pan %s %.3f,%.3f pinch %s %.2f steps spin %s %.1f deg\n",
                 g_two.panEngaged() ? "on" : "off", g_gestureTotal.dragX, g_gestureTotal.dragY,
                 g_two.pinchEngaged() ? "on" : "off", g_gestureTotal.zoomSteps, g_two.spinEngaged() ? "on" : "off",
                 g_gestureTotal.yawDegrees);
    LogCamera("camera after the gesture");
}

void OnFinger(const Platform::Touch::Event& e) {
    using Platform::Touch::Phase;
    const double now = Now();
    if (e.phase == Phase::MouseActivity) {
        g_mouseAt = now;
        if (g_owns && g_fingers.empty()) {
            g_owns = false;
            std::fprintf(stderr, "[TouchPointer] mouse: the pointer goes back to the mouse\n");
        }
        return;
    }
    const ToolKind kind = KindOf(CurrentTool());
    if (e.phase == Phase::Down) {
        if (!g_owns) {
            // Take the pointer over where it is, buttons up.
            PointerState s;
            if (Input::Device* dev = Input::Device::_singleton) {
                s.x = static_cast<float>(dev->_mouse[kMouseX].value);
                s.y = static_cast<float>(dev->_mouse[kMouseY].value);
            }
            g_queue.Reset(s);
            g_owns = true;
        }
        Finger f;
        f.id = e.finger;
        f.x = f.downX = e.x;
        f.y = f.downY = e.y;
        f.downAt = now;
        if (g_fingers.empty()) {
            f.role = Classify(e.x, e.y, kind);
            std::fprintf(stderr, "[TouchPointer] down %.0f,%.0f: %s\n", e.x, e.y, kRoleNames[static_cast<int32_t>(f.role)]);
            g_fingers.push_back(f);
            Finger& p = g_fingers.back();
            if (p.role == Role::Ui || p.role == Role::Plain) Move(p.x, p.y, true);
            if (p.role == Role::Aim) AimAt(p);
            return;
        }
        Finger& first = g_fingers.front();
        if (g_fingers.size() == 1 && (first.role == Role::Aim || first.role == Role::Pick)) {
            first.role = Role::Gesture;
            f.role = Role::Gesture;
            g_two.Begin(first.x, first.y, f.x, f.y, ShortEdge());
            g_gestureActive = true;
            g_gestureTotal = GestureStep();
            std::fprintf(stderr, "[TouchPointer] gesture begin\n");
            LogCamera("camera before the gesture");
        } else {
            f.role = Role::Ignored;
        }
        g_fingers.push_back(f);
        return;
    }
    Finger* f = FindFinger(e.finger);
    if (f == nullptr) return;
    const float mdx = e.x - f->x;
    const float mdy = e.y - f->y;
    f->travel += std::sqrt(mdx * mdx + mdy * mdy);
    f->x = e.x;
    f->y = e.y;
    if (e.phase == Phase::Move) {
        switch (f->role) {
        case Role::Ui:
        case Role::Plain:
            Move(f->x, f->y, true);
            break;
        case Role::Aim:
            AimAt(*f);
            break;
        case Role::Gesture: {
            const Finger* a = nullptr;
            const Finger* b = nullptr;
            for (const Finger& g : g_fingers) {
                if (g.role != Role::Gesture) continue;
                if (a == nullptr) {
                    a = &g;
                } else if (b == nullptr) {
                    b = &g;
                }
            }
            if (a != nullptr && b != nullptr) {
                const GestureStep s = g_two.Step(a->x, a->y, b->x, b->y);
                g_pendingGesture.dragX += s.dragX;
                g_pendingGesture.dragY += s.dragY;
                g_pendingGesture.zoomSteps += s.zoomSteps;
                g_pendingGesture.yawDegrees += s.yawDegrees;
                g_gestureTotal.dragX += s.dragX;
                g_gestureTotal.dragY += s.dragY;
                g_gestureTotal.zoomSteps += s.zoomSteps;
                g_gestureTotal.yawDegrees += s.yawDegrees;
            }
            break;
        }
        default:
            break;
        }
        return;
    }
    // Up or Cancel.
    std::fprintf(stderr, "[TouchPointer] %s %.0f,%.0f: %s\n", e.phase == Phase::Up ? "up" : "cancel", e.x, e.y,
                 kRoleNames[static_cast<int32_t>(f->role)]);
    const bool up = e.phase == Phase::Up;
    const Finger lifted = *f;
    for (size_t i = 0; i < g_fingers.size(); ++i) {
        if (g_fingers[i].id == lifted.id) {
            g_fingers.erase(g_fingers.begin() + static_cast<std::ptrdiff_t>(i));
            break;
        }
    }
    switch (lifted.role) {
    case Role::Bar: {
        UI::UserInterface* ui = GameUi();
        const int32_t b = ui != nullptr ? HitButton(g_bar, static_cast<int32_t>(lifted.x * ui->_uiScale),
                                                    static_cast<int32_t>(lifted.y * ui->_uiScale))
                                        : -1;
        if (up && b == g_barPressed && b >= 0) PressButton(b);
        g_barPressed = -1;
        break;
    }
    case Role::Ui:
        Move(lifted.x, lifted.y, false);
        // The cursor back on the world a few polls later, so no tooltip opens on the tapped button and a ghost
        // stays put. The release is held for kReleaseHoldPolls first: a dialog's drag arms from the press's
        // element event, handled after its OnUpdate (ToolUI::OnUpdate tests the 5 px move before the button),
        // so a jump on the poll after the release dragged an info panel across the screen (measured on the S25).
        // With no world position yet: off the screen (no dialog contains it, no tool ray is cast from it).
        {
            PointerState hold;
            hold.x = lifted.x;
            hold.y = lifted.y;
            for (int32_t i = 0; i < kReleaseHoldPolls; ++i) g_queue.Append(hold, true);
            PointerState back;
            back.x = g_haveWorld ? g_lastWorld.x : -1.0f;
            back.y = g_haveWorld ? g_lastWorld.y : -1.0f;
            g_queue.Append(back);
        }
        break;
    case Role::Plain:
        Move(lifted.x, lifted.y, false);
        break;
    case Role::Aim:
        if (up) AimLift(lifted, kind, now);
        break;
    case Role::Pick:
        if (up && IsTap(static_cast<float>(now - lifted.downAt), lifted.travel, ShortEdge())) PickTap(lifted.x, lifted.y);
        break;
    case Role::Gesture:
        EndGesture();
        break;
    default:
        break;
    }
}

// ---- the writer after the poll -------------------------------------------------------------------------

void SetInput(Input::Device::State& st, int16_t value, int16_t& prev) {
    st.pressed = prev == 0 && value != 0;
    st.released = prev != 0 && value == 0;
    st.value = value;
    st.normalized = static_cast<float>(value) / 32767.0f;
    if (value == 0) {
        st.holdTicks = 0;
        st.repeat = false;
    }
    prev = value;
}

// The input the KeyMap binds `action` to (its first bound slot), pressed for this step only.
void PressAction(Input::Device& dev, int32_t action) {
    const Input::KeyMap* km = Input::Configuration::_singleton != nullptr ? Input::Configuration::_singleton->_keyMap
                                                                         : nullptr;
    if (km == nullptr) return;
    for (int32_t slot = 0; slot < 2; ++slot) {
        const int32_t device = static_cast<int8_t>(km->GetDeviceForId(action, slot));
        const int32_t input = km->GetInputForId(action, slot);
        if (device == 1 && input >= 0 && input < Input::Device::kKeyboardInputCount) {
            dev._keyboard[input].pressed = true;
            return;
        }
        if (device == 2 && input >= 0 && input < Input::Device::kMouseInputCount) {
            dev._mouse[input].pressed = true;
            return;
        }
    }
}

void AfterUpdate(Input::Device& dev) {
    const bool on = Ext().touch_pointer;
    if (on != g_sinkOn) {
        g_sinkOn = on;
        Platform::Touch::SetSink(on ? &OnFinger : nullptr);
        g_fingers.clear();
        g_actions.clear();
        g_gestureActive = false;
        g_owns = false;
        std::fprintf(stderr, "[TouchPointer] %s\n", on ? "fingers taken (touch-to-mouse off)" : "fingers released");
    }
    if (!on || !g_owns) return;

    const PointerState s = g_queue.Pop();
    const int16_t x = static_cast<int16_t>(s.x);
    const int16_t y = static_cast<int16_t>(s.y);
    const int16_t dx = static_cast<int16_t>(x - g_prev[kMouseX]);
    const int16_t dy = static_cast<int16_t>(g_prev[kMouseY] - y);
    for (int32_t i = 0; i < Input::Device::kMouseInputCount; ++i) {
        int16_t v = 0;
        if (i == kMouseX) v = x;
        if (i == kMouseY) v = y;
        if (i == kMouseDx) v = dx;
        if (i == kMouseDy) v = dy;
        if (i == kMouseLeft) v = s.left ? kFullScale : 0;
        if (i == kMouseRight) v = s.right ? kFullScale : 0;
        SetInput(dev._mouse[i], v, g_prev[i]);
    }
    if (!g_actions.empty()) {
        PressAction(dev, g_actions.front());
        g_actions.erase(g_actions.begin());
    }

    // The camera: the two fingers' motion since the last poll, or the edge auto-pan of a drag tool's aim.
    TouchMode::CameraHold hold;
    if (g_gestureActive) {
        hold.dragX = g_pendingGesture.dragX;
        hold.dragY = g_pendingGesture.dragY;
        hold.zoomSteps = g_pendingGesture.zoomSteps;
        hold.yawDegrees = g_pendingGesture.yawDegrees;
    } else if (g_fingers.size() == 1 && g_fingers.front().role == Role::Aim &&
               KindOf(CurrentTool()) == ToolKind::Drag) {
        int32_t w = 0;
        int32_t h = 0;
        ScreenSize(w, h);
        int32_t ex = 0;
        int32_t ey = 0;
        EdgePan(g_fingers.front().x, g_fingers.front().y, w, h, ex, ey);
        hold.panX = static_cast<int8_t>(ex);
        hold.panY = static_cast<int8_t>(ey);
    }
    g_pendingGesture = GestureStep();
    TouchMode::Hold(TouchMode::Source::Fingers, hold);
}

// ---- the dialogs ---------------------------------------------------------------------------------------

const UI::Dialog* LoadDialog(const char* path) {
    if (::Object::ExternalFactory::_singleton == nullptr) return nullptr;
    const ::Object::External* ext = ::Object::ExternalFactory::_singleton->FindOrLoad(System::Filename(path));
    if (!::Object::IsDerivedFromNullSafe(ext, UI::Dialog::TypeId())) {
        std::fprintf(stderr, "[TouchPointer] %s did not load as a UI::Dialog (%p)\n", path,
                     static_cast<const void*>(ext));
        return nullptr;
    }
    return static_cast<const UI::Dialog*>(ext);
}

void StoreRect(int32_t* out, int32_t x1, int32_t y1, int32_t x2, int32_t y2) {
    out[0] = x1;
    out[1] = y1;
    out[2] = x2;
    out[3] = y2;
}

class BarDialog final : public UI::DialogInstance {
public:
    BarDialog(UI::UserInterface* ui, const UI::Dialog* dialog)
        : UI::DialogInstance(ui, dialog, Object::InternalPtr<UI::DialogInstance>(), nullptr, nullptr, 0) {}
    ~BarDialog() override {
        if (g_barDialog == this) g_barDialog = nullptr;
    }
    void OnOpened() override { g_barDialog = this; }
    void OnClosed() override {
        if (g_barDialog == this) g_barDialog = nullptr;
    }

    // The holders' padding, the buttons' sizes and visibility from the bar's layout; again on any change.
    void Shape(const Bar& bar, uint32_t shown, int32_t cell, int32_t frame) {
        if (shown == _shownW && bar.box.x1 == _boxX && bar.box.y1 == _boxY && cell == _cell) return;
        _shownW = shown;
        _boxX = bar.box.x1;
        _boxY = bar.box.y1;
        _cell = cell;
        std::fprintf(stderr, "[TouchPointer] bar %d,%d..%d,%d cell %d shown 0x%x\n", bar.box.x1, bar.box.y1, bar.box.x2,
                     bar.box.y2, cell, shown);
        for (int32_t i = 0; i < kButtonCount; ++i) {
            UI::Element* holder = FindElement(System::String(kHolders[i])).Get();
            UI::Element* button = FindElement(System::String(kButtons[i])).Get();
            if (holder == nullptr || button == nullptr) continue;
            const bool visible = (shown & (1u << i)) != 0;
            const uint32_t hidden = UI::kElementFlagHidden;
            holder->SetFlagsOnOff(visible ? 0u : hidden, visible ? hidden : 0u);
            const Rect& r = bar.button[i];
            holder->_leftPad = visible ? r.x1 - bar.box.x1 - frame : 0;
            holder->_topPad = visible ? r.y1 - bar.box.y1 - frame : 0;
            holder->SetNeedsSizeCalc();
            button->_minWidth = cell;
            button->_minHeight = cell;
            button->SetNeedsSizeCalc();
        }
        if (UI::Element* content = FindElement(System::String("groupPointerContent")).Get()) {
            content->_leftPad = frame;
            content->_topPad = frame;
            content->_rightPad = frame;
            content->_bottomPad = frame;
            content->SetNeedsSizeCalc();
        }
        const int32_t glyph = static_cast<int32_t>(kGlyphFraction * static_cast<float>(cell));
        const char* images[] = {"imagePointerPlace",    "imagePointerRotateLeftArrow", "imagePointerHouseLeft",
                                "imagePointerHouseRight", "imagePointerRotateRightArrow", "imagePointerFlipLeft",
                                "imagePointerFlipRight",  "imagePointerCancel"};
        for (const char* name : images) {
            UI::Element* e = FindElement(System::String(name)).Get();
            if (!::Object::IsDerivedFromNullSafe(e, UI::ImageElement::TypeId())) continue;
            UI::ImageElement* image = static_cast<UI::ImageElement*>(e);
            const UI::Sprite sprite = image->GetSprite();
            const int32_t sw = sprite.Width();
            const int32_t sh = sprite.Height();
            if (sw <= 0 || sh <= 0) continue;
            int32_t h = glyph;
            if (h >= 2 * sh) h = (h / sh) * sh;
            image->SetWidthHeight(sw * h / sh, h);
            image->SetNeedsSizeCalc();
        }
    }

    // The pressed button keeps the toolbar's pressed art while the finger is down (touch_mode's rule).
    void ShowPressed(int32_t pressed) {
        if (pressed < 0) return;
        UI::Element* e = FindElement(System::String(kButtons[pressed])).Get();
        if (!::Object::IsDerivedFromNullSafe(e, UI::ButtonElement::TypeId())) return;
        UI::ButtonElement* button = static_cast<UI::ButtonElement*>(e);
        if (button->_state == kButtonPressedState) return;
        button->_state = kButtonPressedState;
        button->SetNeedsSizeCalc();
        if (UI::Element* art = button->_children._data[kButtonPressedState]) {
            art->ComputeLayout(button->_layoutLeft, button->_layoutTop, button->_layoutRight, button->_layoutBottom);
        }
        if (UI::Element* label = button->_children._data[kButtonLabelChild]) {
            label->SetColor(button->_stateColor[kButtonPressedState]);
        }
    }

private:
    uint32_t _shownW = 0xFFFFFFFFu;
    int32_t _boxX = -1;
    int32_t _boxY = -1;
    int32_t _cell = -1;
};

class MarkerDialog final : public UI::DialogInstance {
public:
    MarkerDialog(UI::UserInterface* ui, const UI::Dialog* dialog)
        : UI::DialogInstance(ui, dialog, Object::InternalPtr<UI::DialogInstance>(), nullptr, nullptr, 0) {}
    ~MarkerDialog() override {
        if (g_markerDialog == this) g_markerDialog = nullptr;
    }
    void OnOpened() override {
        g_markerDialog = this;
        SetFlagsOnOff(0x4u, 0);   // the mouse passes through (DialogInstance::Update's bit 2): the cursor stays on the world
    }
    void OnClosed() override {
        if (g_markerDialog == this) g_markerDialog = nullptr;
    }
    void Shape(int32_t cell) {
        if (cell == _cell) return;
        _cell = cell;
        UI::Element* e = FindElement(System::String("imagePointerMarker")).Get();
        if (!::Object::IsDerivedFromNullSafe(e, UI::ImageElement::TypeId())) return;
        UI::ImageElement* image = static_cast<UI::ImageElement*>(e);
        const UI::Sprite sprite = image->GetSprite();
        const int32_t sw = sprite.Width();
        const int32_t sh = sprite.Height();
        if (sw <= 0 || sh <= 0) return;
        // About half a button high, a whole multiple of the sprite (crisp), never smaller than the sprite.
        int32_t h = cell / 2 > sh ? cell / 2 : sh;
        if (h >= 2 * sh) h = (h / sh) * sh;
        image->SetWidthHeight(sw * h / sh, h);
        image->SetNeedsSizeCalc();
    }

private:
    int32_t _cell = -1;
};

template <typename T>
T* OpenDialog(UI::UserInterface* ui, const char* path) {
    const UI::Dialog* rsc = LoadDialog(path);
    if (rsc == nullptr) return nullptr;
    void* mem = System::MemoryAllocator::MemAlloc(sizeof(T), 8);
    if (mem == nullptr) return nullptr;
    T* dlg = ::new (mem) T(ui, rsc);
    Object::InternalPtr<UI::DialogInstance> ref(dlg);
    ui->AddDialog(&ref);   // dispatches OnOpened
    return dlg;
}

void CloseDialog(UI::UserInterface* ui, UI::DialogInstance* d) {
    if (ui == nullptr || d == nullptr) return;
    Object::InternalPtr<UI::DialogInstance> ref(d);
    ui->CloseDialog(&ref);
}

// The buttons the current tool uses.
uint32_t ShownFor(const Tool* tool, ToolKind kind) {
    if (kind == ToolKind::Place) return (1u << kPlace) | (1u << kRotateLeft) | (1u << kRotateRight) | (1u << kFlip) |
                                        (1u << kCancel);
    if (kind == ToolKind::Drag) {
        uint32_t m = (1u << kPlace) | (1u << kCancel);
        if (tool != nullptr && tool->IsA(FencedTool::TypeId())) m |= (1u << kRotateLeft) | (1u << kRotateRight);
        return m;
    }
    return 0;
}

// The bar's layout on the game UI's layout screen.
Bar CurrentBar(const UI::UserInterface* ui, uint32_t shown, int32_t& cell, int32_t& frame) {
    int32_t w = 0;
    int32_t h = 0;
    ScreenSize(w, h);
    const int32_t lw = static_cast<int32_t>(static_cast<float>(w) * ui->_uiScale);
    const int32_t lh = static_cast<int32_t>(static_cast<float>(h) * ui->_uiScale);
    const int32_t shortEdge = lw < lh ? lw : lh;
    cell = TouchMode::Cell();
    if (cell <= 0) cell = static_cast<int32_t>(TouchMode::kCellFraction * static_cast<float>(shortEdge) + 0.5f);
    frame = static_cast<int32_t>(TouchMode::kFrameFraction * static_cast<float>(shortEdge) + 0.5f);
    const int32_t gap = static_cast<int32_t>(TouchMode::kGapFraction * static_cast<float>(shortEdge) + 0.5f);
    const int32_t margin = TouchMode::Margin(lw, lh, Ext().phone_menu ? PhoneMenu::SafeMargin(lh) : 0);
    int32_t bar[4] = {0, 0, 0, 0};
    int32_t sub[4] = {0, 0, 0, 0};
    TouchMode::BarRect(bar);
    TouchMode::SubToolbarRect(sub);
    int32_t above = 0;
    if (bar[3] > bar[1]) above = bar[1];
    if (sub[3] > sub[1] && (above == 0 || sub[1] < above)) above = sub[1];
    if (above == 0) above = lh - margin - cell * 3 / 2;   // retail's toolbar: about a cell and a half high
    return LayoutBar(lw, lh, cell, gap, frame, margin, above, shown);
}

}  // namespace

void Sync() {
    const bool faithful = CurrentProfile() == Profile::Faithful;
    Input::Device::_extAfterUpdate = faithful ? nullptr : &AfterUpdate;
}

bool Shown() {
    return g_barDialog != nullptr || g_markerDialog != nullptr;
}

void GameFrame() {
    UI::UserInterface* ui = GameUi();
    if (ui == nullptr) return;
    const Tool* tool = CurrentTool();
    const ToolKind kind = KindOf(tool);
    const bool mouseRecently = Now() - g_mouseAt < kMouseHideSeconds;
    const uint32_t shown = Ext().touch_pointer && (g_owns || !mouseRecently) ? ShownFor(tool, kind) : 0;
    const bool wantBar = shown != 0 && g_owns;
    if (wantBar && g_barDialog == nullptr) OpenDialog<BarDialog>(ui, kBarRsc);
    if (!wantBar && g_barDialog != nullptr) CloseDialog(ui, g_barDialog);
    int32_t cell = 0;
    int32_t frame = 0;
    g_bar = CurrentBar(ui, shown, cell, frame);
    g_shown = shown;
    if (g_barDialog != nullptr) {
        g_barDialog->Shape(g_bar, shown, cell, frame);
        g_barDialog->ShowPressed(g_barPressed);
    }
    // The marker while a finger aims with a lift offset.
    bool aiming = false;
    for (const Finger& f : g_fingers) {
        if (f.role == Role::Aim) aiming = true;
    }
    const bool wantMarker = Ext().touch_pointer && aiming && Val().touch_pointer_lift > 0;
    if (wantMarker && g_markerDialog == nullptr) OpenDialog<MarkerDialog>(ui, kMarkerRsc);
    if (!wantMarker && g_markerDialog != nullptr) CloseDialog(ui, g_markerDialog);
    if (g_markerDialog != nullptr) g_markerDialog->Shape(cell);
}

bool Place(int32_t point, const UI::DialogInstance* dialog, int32_t width, int32_t height, int32_t* rect) {
    if (dialog == nullptr) return false;
    if (dialog != g_barDialog && dialog != g_markerDialog) return false;
    if (point != UI::UserInterface::kExtPlaceLayout) return true;
    if (dialog == g_barDialog) {
        StoreRect(rect, g_bar.box.x1, g_bar.box.y1, width, height);
        return true;
    }
    // The marker: its arrow's tip on the cursor.
    const UI::UserInterface* ui = dialog->_ui;
    const UI::Element* root = dialog->_root.Get();
    if (ui == nullptr || root == nullptr) return true;
    const PointerState& at = g_queue.Current();
    const int32_t lx = static_cast<int32_t>(at.x * ui->_uiScale);
    const int32_t ly = static_cast<int32_t>(at.y * ui->_uiScale);
    const int32_t w = root->_layoutRight - root->_layoutLeft;
    const int32_t h = root->_layoutBottom - root->_layoutTop;
    StoreRect(rect, lx - w / 2, ly - h, width, height);
    return true;
}

}  // namespace TouchPointer
}  // namespace Ext
