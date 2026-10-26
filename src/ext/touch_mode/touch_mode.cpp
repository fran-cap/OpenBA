// src/ext/touch_mode/touch_mode.cpp -- ext.touch_mode (NOT RETAIL; docs/android/TOUCH_MODE.md)
//
// The control cluster dialog, the toolbar's own scale (Scale, through ext.phone_menu's
// UI::UserInterface::_extDialogScale), the place
// rules for the dialogs that scale touches (called first from ext.phone_menu's place function) and the
// camera hold. touch_math.h holds the arithmetic.

#include "ext/touch_mode/touch_mode.h"
#include "ext/touch_mode/touch_math.h"

#include "ext/registry.h"
#include "ext/phone_menu/phone_math.h"   // SafeMargin: the cluster keeps ext.phone_menu's safe area

#include "ui/ImageElement.h"
#include "ui/ButtonElement.h"
#include "ui/Element.h"
#include "ui/DialogInstance.h"
#include "ui/ToolTipDialog.h"
#include "ui/UserInterface.h"
#include "ui/Sprite.h"
#include "ui/Dialog.h"
#include "input/Device.h"
#include "object/External.h"
#include "object/Internal.h"
#include "object/TypeInfo.h"
#include "system/String.h"
#include "system/Filename.h"
#include "system/MemoryAllocator.h"
#include "Game/GameUpdateLoop.h"
#include "Game/PlayerController.h"

#include <cstdint>
#include <cstdio>
#include <new>

namespace Ext {
namespace TouchMode {

namespace {

constexpr const char* kToolbarRsc = "Dialog/Toolbar.rsc";
constexpr const char* kClusterRsc = "Dialog/TouchControls.rsc";

// The buttons and their holders in the cluster dialog, in touch_math.h Control order.
constexpr const char* kButtons[kControlCount] = {"buttonTouchPad", "buttonTouchRotateLeft", "buttonTouchRotateRight",
                                                 "buttonTouchZoomIn", "buttonTouchZoomOut"};
constexpr const char* kHolders[kControlCount] = {"holderTouchPad", "holderTouchRotateLeft", "holderTouchRotateRight",
                                                 "holderTouchZoomIn", "holderTouchZoomOut"};
constexpr const char* kControlNames[kControlCount] = {"pad", "rotate left", "rotate right", "zoom in", "zoom out"};

// A glyph's side as a fraction of its button's shorter side, and the frame's inset (layout units).
constexpr float kGlyphFraction = 0.5f;
constexpr int32_t kFrameInset = 6;

// ButtonElement (ButtonElement.h): state 3 = pressed (its fourth state child), child 4 = the label.
constexpr int32_t kButtonPressedState = 3;
constexpr int32_t kButtonLabelChild = 4;

const UI::Dialog* g_toolbarDesc = nullptr;   // Dialog/Toolbar.rsc, the toolbar's and every sub-toolbar's
const GameUpdateLoop* g_toolbarFor = nullptr;   // the game it was resolved for (a load reloads the resource)
float g_k = 1.0f;                            // the toolbar's own scale this frame
float g_loggedK = 0.0f;
int32_t g_loggedOwnWidth = -1;
Cluster g_cluster;
int32_t g_layoutW = 0;
int32_t g_layoutH = 0;
int32_t g_bar[4] = {0, 0, 0, 0};   // the toolbar, layout units
int32_t g_sub[4] = {0, 0, 0, 0};   // the open sub-toolbar, layout units

// The camera holds (Hold): the source that started last wins.
CameraHold g_hold[2];
uint64_t g_holdSince[2] = {0, 0};   // when each source last became active
uint64_t g_holdClock = 0;
bool g_activeNow[2] = {false, false};
bool g_activeBefore[2] = {false, false};
float g_fingerZoom = 0.0f;

bool Active(const CameraHold& h) {
    return h.panX != 0 || h.panY != 0 || h.rotate != 0 || h.zoom != 0 || h.zoomSteps != 0.0f;
}

// The cluster's held control and its zoom repeat.
Control g_held = kNone;
float g_heldSeconds = 0.0f;
ZoomRepeat g_zoomRepeat;
Capture g_capture;

class ClusterDialog;
ClusterDialog* g_open = nullptr;

const UI::Dialog* LoadDialog(const char* path) {
    if (::Object::ExternalFactory::_singleton == nullptr) return nullptr;
    const ::Object::External* ext = ::Object::ExternalFactory::_singleton->FindOrLoad(System::Filename(path));
    if (!::Object::IsDerivedFromNullSafe(ext, UI::Dialog::TypeId())) {
        std::fprintf(stderr, "[TouchMode] %s did not load as a UI::Dialog (%p)\n", path, static_cast<const void*>(ext));
        return nullptr;
    }
    return static_cast<const UI::Dialog*>(ext);
}

// Resolved once per game: the game's Startup loads the toolbar's dialog resource (gameupdateloop.cpp), and a
// new game or a load may give it a new address. No game: none (the front end has no toolbar).
const UI::Dialog* ToolbarDescription() {
    const GameUpdateLoop* game = GameUpdateLoop::_singleton;
    if (game == nullptr) {
        g_toolbarFor = nullptr;
        g_toolbarDesc = nullptr;
        return nullptr;
    }
    if (game != g_toolbarFor && ::Object::ExternalFactory::_singleton != nullptr) {
        g_toolbarFor = game;
        g_toolbarDesc = LoadDialog(kToolbarRsc);
    }
    return g_toolbarDesc;
}

bool IsToolbar(const UI::DialogInstance* d) {
    return d != nullptr && g_toolbarDesc != nullptr && d->_dialog == g_toolbarDesc;
}

void PaddedSize(UI::Element* e, int32_t& w, int32_t& h) {
    e->ComputeSize();
    w = e->_layoutWidth + e->_leftPad + e->_rightPad;
    h = e->_layoutHeight + e->_topPad + e->_bottomPad;
    if (w < e->_minWidth) w = e->_minWidth;
    if (h < e->_minHeight) h = e->_minHeight;
}

void StoreRect(int32_t* out, int32_t x1, int32_t y1, int32_t x2, int32_t y2) {
    out[0] = x1;
    out[1] = y1;
    out[2] = x2;
    out[3] = y2;
}

void ElementRectInLayout(const UI::Element* e, float k, int32_t* out) {
    StoreRect(out, ToLayout(e->_layoutLeft, k), ToLayout(e->_layoutTop, k), ToLayout(e->_layoutRight, k),
              ToLayout(e->_layoutBottom, k));
}

// The cluster for this layout screen; true when it changed.
bool UpdateCluster(int32_t width, int32_t height) {
    if (width == g_layoutW && height == g_layoutH && g_cluster.cell > 0) return false;
    g_layoutW = width;
    g_layoutH = height;
    const int32_t phoneMargin = Ext().phone_menu ? PhoneMenu::SafeMargin(height) : 0;
    g_cluster = LayoutCluster(width, height, Margin(width, height, phoneMargin));
    return true;
}

// ---- the cluster dialog -----------------------------------------------------

class ClusterDialog final : public UI::DialogInstance {
public:
    ClusterDialog(UI::UserInterface* ui, const UI::Dialog* dialog)
        : UI::DialogInstance(ui, dialog, Object::InternalPtr<UI::DialogInstance>(), nullptr, nullptr, 0) {}

    ~ClusterDialog() override {
        if (g_open == this) g_open = nullptr;
    }

    void OnOpened() override {
        g_open = this;
        _shapedW = -1;
    }

    void OnClosed() override {
        if (g_open == this) g_open = nullptr;
    }

    // The captured control in its pressed state (ButtonElement state 3, the toolbar's held art) while the press is
    // held, even off the button: retail's ButtonElement::Update shows it only while the pointer is over the button,
    // and runs first each frame (the UI's update precedes the camera's), so this puts state 3 back for the frame's
    // draw. Same steps as retail's own state change: the state child laid out over the button, the label's colour.
    void ShowPressed(const Capture& capture) {
        for (int32_t i = 0; i < kControlCount; ++i) {
            if (!capture.ShowsPressed(static_cast<Control>(i))) continue;
            UI::Element* e = FindElement(System::String(kButtons[i])).Get();
            if (!::Object::IsDerivedFromNullSafe(e, UI::ButtonElement::TypeId())) continue;
            UI::ButtonElement* button = static_cast<UI::ButtonElement*>(e);
            if (button->_state == kButtonPressedState) continue;
            button->_state = kButtonPressedState;
            button->SetNeedsSizeCalc();
            if (UI::Element* art = button->_children._data[kButtonPressedState]) {
                art->ComputeLayout(button->_layoutLeft, button->_layoutTop, button->_layoutRight,
                                   button->_layoutBottom);
            }
            if (UI::Element* label = button->_children._data[kButtonLabelChild]) {
                label->SetColor(button->_stateColor[kButtonPressedState]);
            }
        }
    }

    // The holders' padding and the buttons' sizes from the cluster's rectangles, the glyphs scaled to
    // their buttons. Again whenever the layout screen changes.
    void Shape() {
        if (_shapedW == g_layoutW && _shapedH == g_layoutH) return;
        _shapedW = g_layoutW;
        _shapedH = g_layoutH;
        std::fprintf(stderr, "[TouchMode] %dx%d: cluster cell %d, box %d,%d..%d,%d, bar region %d..%d\n", g_layoutW,
                     g_layoutH, g_cluster.cell, g_cluster.box.x1, g_cluster.box.y1, g_cluster.box.x2,
                     g_cluster.box.y2, g_cluster.barRegion.x1, g_cluster.barRegion.x2);
        for (int32_t i = 0; i < kControlCount; ++i) {
            const Rect& r = g_cluster.control[i];
            UI::Element* holder = FindElement(System::String(kHolders[i])).Get();
            UI::Element* button = FindElement(System::String(kButtons[i])).Get();
            if (holder == nullptr || button == nullptr) {
                std::fprintf(stderr, "[TouchMode] %s / %s missing from %s\n", kHolders[i], kButtons[i], kClusterRsc);
                continue;
            }
            // Relative to the cluster's box: the dialog's root sits at the box's corner (Place), so the
            // dialog hit-tests (and takes the pointer from the world) over the box only.
            holder->_leftPad = r.x1 - g_cluster.box.x1 - g_cluster.frame;
            holder->_topPad = r.y1 - g_cluster.box.y1 - g_cluster.frame;
            holder->SetNeedsSizeCalc();
            button->_minWidth = r.Width();
            button->_minHeight = r.Height();
            button->SetNeedsSizeCalc();
            // The pad's tooltip off: a finger left on it after a pan is a parked cursor.
            if (i == kPad) button->SetFlagsOnOff(0, UI::kElementFlagToolTipValid);
        }
        // The content inside the frame: inset by the cluster's frame on every side, so the frame (the toolbar's
        // borders, which fill the root) wraps the controls with the toolbar's margin.
        if (UI::Element* content = FindElement(System::String("groupTouchContent")).Get()) {
            content->_leftPad = g_cluster.frame;
            content->_topPad = g_cluster.frame;
            content->_rightPad = g_cluster.frame;
            content->_bottomPad = g_cluster.frame;
            content->SetNeedsSizeCalc();
        }
        const Rect& pad = g_cluster.control[kPad];
        if (UI::Element* arrows = FindElement(System::String("groupTouchPadArrows")).Get()) {
            arrows->_minWidth = pad.Width() - 2 * kFrameInset;
            arrows->_minHeight = pad.Height() - 2 * kFrameInset;
            arrows->SetNeedsSizeCalc();
        }
        const int32_t glyph = static_cast<int32_t>(kGlyphFraction * static_cast<float>(g_cluster.cell));
        const char* images[] = {"imageTouchUp", "imageTouchDown", "imageTouchLeft", "imageTouchRight",
                                "imageTouchRotateLeftArrow", "imageTouchCameraLeft", "imageTouchCameraRight",
                                "imageTouchRotateRightArrow", "imageTouchZoomIn", "imageTouchZoomOut"};
        for (const char* name : images) {
            UI::Element* e = FindElement(System::String(name)).Get();
            if (!::Object::IsDerivedFromNullSafe(e, UI::ImageElement::TypeId())) continue;
            UI::ImageElement* image = static_cast<UI::ImageElement*>(e);
            const UI::Sprite sprite = image->GetSprite();
            const int32_t sw = sprite.Width();
            const int32_t sh = sprite.Height();
            if (sw <= 0 || sh <= 0) continue;
            // A whole multiple of the sprite from twice its size on (even pixels), else the glyph's own size.
            // The pad's four arrows sit alone on a large button: larger.
            const bool padArrow = name == images[0] || name == images[1] || name == images[2] || name == images[3];
            int32_t h = padArrow ? glyph * 3 / 2 : glyph;
            if (h >= 2 * sh) h = (h / sh) * sh;
            image->SetWidthHeight(sw * h / sh, h);
            image->SetNeedsSizeCalc();
        }
    }

private:
    int32_t _shapedW = -1;
    int32_t _shapedH = -1;
};

void OpenCluster(UI::UserInterface* ui) {
    const UI::Dialog* rsc = LoadDialog(kClusterRsc);
    if (rsc == nullptr) return;
    void* mem = System::MemoryAllocator::MemAlloc(sizeof(ClusterDialog), 8);
    if (mem == nullptr) return;
    ClusterDialog* dlg = ::new (mem) ClusterDialog(ui, rsc);
    Object::InternalPtr<UI::DialogInstance> ref(dlg);
    ui->AddDialog(&ref);   // dispatches OnOpened
    std::fprintf(stderr, "[TouchMode] controls shown\n");
}

void CloseCluster(UI::UserInterface* ui) {
    Object::InternalPtr<UI::DialogInstance> ref(g_open);
    ui->CloseDialog(&ref);
    g_open = nullptr;
    std::fprintf(stderr, "[TouchMode] controls hidden\n");
}

// The top-level toolbar, open and visible (the cluster follows it).
bool ToolbarShown(const UI::UserInterface* ui) {
    if (ui == nullptr || ToolbarDescription() == nullptr) return false;
    for (auto* node = ui->_dialogs._head; node != nullptr; node = node->next) {
        const UI::DialogInstance* d = node->element;
        if (!IsToolbar(d) || d->_parent.Get() != nullptr) continue;
        const UI::Element* root = d->_root.Get();
        return root != nullptr && root->IsVisible();
    }
    return false;
}

// ---- the place rules ----------------------------------------------------------

void PlaceToolbar(int32_t point, const UI::DialogInstance* d, int32_t width, int32_t* rect) {
    UI::Element* const root = const_cast<UI::Element*>(d->_root.Get());
    if (root == nullptr) return;
    const bool top = d->_parent.Get() == nullptr;
    const Rect& region = g_cluster.barRegion;
    if (point == UI::UserInterface::kExtPlaceLayout) {
        if (top) {
            int32_t ownW = 0;
            int32_t ownH = 0;
            PaddedSize(root, ownW, ownH);
            const float requested = ScaleOfPercent(Val().touch_mode_toolbar_scale);
            g_k = ToolbarScale(requested, region.Width(), ownW);
            if (g_k != g_loggedK || ownW != g_loggedOwnWidth) {
                g_loggedK = g_k;
                g_loggedOwnWidth = ownW;
                std::fprintf(stderr, "[TouchMode] toolbar k=%.3f (requested %.2f, fit %.3f): %dx%d own units, "
                             "%dx%d layout units\n", g_k, requested,
                             ownW > 0 ? static_cast<float>(region.Width()) / static_cast<float>(ownW) : 0.0f, ownW,
                             ownH, ToLayout(ownW, g_k), ToLayout(ownH, g_k));
            }
            StoreRect(rect, 0, 0, region.x2, region.y2);
        } else {
            // Retail's centring (ToolbarDialog::OnElementEvent) uses the global width: laid out against
            // that width in own units, the child centres on its button (touch_math.h SubToolbarRight).
            StoreRect(rect, 0, 0, SubToolbarRight(width, g_k), region.y2);
        }
        return;
    }
    if (point != UI::UserInterface::kExtPlaceAfterLayout) return;
    if (top) {
        ElementRectInLayout(root, g_k, g_bar);
        return;
    }
    // A sub-toolbar stays inside the bar region (never over the cluster, never off the right edge).
    const int32_t left = ToOwn(region.x1, g_k);
    const int32_t right = ToOwn(region.x2, g_k);
    int32_t dx = 0;
    if (root->_layoutRight > right) dx = right - root->_layoutRight;
    if (root->_layoutLeft + dx < left) dx = left - root->_layoutLeft;
    if (dx != 0) root->AdjustUpperLeft(dx, 0);
    ElementRectInLayout(root, g_k, g_sub);
}

}  // namespace

float Scale(const UI::DialogInstance* dialog) {
    if (!Ext().touch_mode) return 1.0f;
    return IsToolbar(dialog) ? g_k : 1.0f;
}

bool Place(int32_t point, const UI::DialogInstance* dialog, int32_t width, int32_t height, int32_t* rect) {
    if (!Ext().touch_mode || dialog == nullptr) return false;
    if (point == UI::UserInterface::kExtPlaceBackdrop) return false;
    // The game's own interface only (the front end and the loading screens have their own).
    const GameUpdateLoop* game = GameUpdateLoop::_singleton;
    if (game == nullptr || dialog->_ui != game->m_userInterface) return false;
    UpdateCluster(width, height);
    ToolbarDescription();

    if (IsToolbar(dialog)) {
        PlaceToolbar(point, dialog, width, rect);
        return true;
    }
    if (dialog == g_open) {
        // The cluster: its root at the box's upper-left; the holders place the buttons inside it (Shape).
        if (point == UI::UserInterface::kExtPlaceLayout) {
            g_open->Shape();
            StoreRect(rect, g_cluster.box.x1, g_cluster.box.y1, width, height);
        }
        return true;
    }
    const UI::UserInterface* ui = dialog->_ui;
    if (ui == nullptr) return false;
    // A tooltip opened from the scaled toolbar: retail put it at the pointer in the toolbar's units.
    const UI::DialogInstance* tip = ui->_toolTipDialog;
    if (dialog == tip && IsToolbar(ui->_toolTipOwner) && g_k != 1.0f) {
        if (point == UI::UserInterface::kExtPlaceLayout) {
            UI::Element* root = const_cast<UI::Element*>(dialog->_root.Get());
            if (root == nullptr) return false;
            int32_t w = 0;
            int32_t h = 0;
            PaddedSize(root, w, h);
            int32_t dx = 0;
            int32_t dy = 0;
            TooltipOffset(dialog->_upperLeftX, dialog->_upperLeftY, h, g_k, dx, dy);
            StoreRect(rect, dx, dy, width, height);
        }
        return true;
    }
    // The event notices, parented to the toolbar (EventNotifyDialog::OnUpdateLayout, 0x1400680c0): retail keeps the
    // ribbon behind the toolbar while there is nothing to show and slides it out to the LEFT of the toolbar by its
    // right padding (the smoothed offset) as notices come: where the cluster is. Here the same slide lifts it out
    // from behind the bar's left end to just above it (the right-hand strip stays free for touch_pointer): fully
    // out once the offset reaches retail's 44-unit base.
    if (IsToolbar(dialog->_parent.Get())) {
        if (point == UI::UserInterface::kExtPlaceAfterLayout && g_bar[2] > g_bar[0]) {
            UI::Element* root = const_cast<UI::Element*>(dialog->_root.Get());
            if (root != nullptr) {
                float t = static_cast<float>(root->_rightPad) / 44.0f;
                if (t < 0.0f) t = 0.0f;
                if (t > 1.0f) t = 1.0f;
                const int32_t hiddenBottom = g_bar[3];
                const int32_t shownBottom = g_bar[1] - g_cluster.gap;
                const int32_t bottom = hiddenBottom + static_cast<int32_t>(t * static_cast<float>(shownBottom - hiddenBottom));
                const int32_t dx = g_bar[0] - root->_layoutLeft;
                const int32_t dy = bottom - root->_layoutBottom;
                root->AdjustUpperLeft(dx, dy);
            }
        }
        return point == UI::UserInterface::kExtPlaceLayout || point == UI::UserInterface::kExtPlaceAfterLayout;
    }
    return false;
}

bool Shown() {
    return g_open != nullptr;
}

void Hold(Source source, const CameraHold& hold) {
    const int32_t i = static_cast<int32_t>(source);
    const bool active = Active(hold);
    if (active && !g_activeBefore[i]) g_holdSince[i] = ++g_holdClock;
    g_hold[i] = hold;
    g_activeNow[i] = g_activeNow[i] || active;
    if (source == Source::Fingers) g_fingerZoom += hold.zoomSteps;
}

void CameraInput(float dt, float& yaw, float& panX, float& panY, int& zoomDir) {
    GameUpdateLoop* loop = GameUpdateLoop::_singleton;
    UI::UserInterface* ui = loop != nullptr ? loop->m_userInterface : nullptr;
    const bool want = Ext().touch_mode && ToolbarShown(ui);
    if (want && g_open == nullptr && ui != nullptr) OpenCluster(ui);
    if (!want && g_open != nullptr && ui != nullptr) CloseCluster(ui);

    // The cluster: the control the press started on, held until the release whatever the pointer crosses
    // (touch_math.h Capture; the owner's rule, 2026-10-26).
    Control held = kNone;
    int32_t mx = 0;
    int32_t my = 0;
    if (g_open != nullptr && ui != nullptr && ::Input::Device::_singleton != nullptr) {
        const ::Input::Device* in = ::Input::Device::_singleton;
        mx = static_cast<int32_t>(static_cast<float>(in->_mouse[0].value) * ui->_uiScale);
        my = static_cast<int32_t>(static_cast<float>(in->_mouse[1].value) * ui->_uiScale);
        const bool down = in->_mouse[4].value > 0;
        held = g_capture.Step(g_cluster, down, mx, my);
    } else {
        g_capture = Capture();
    }
    if (g_open != nullptr) g_open->ShowPressed(g_capture);
    CameraHold pad;
    if (held == kPad) {
        int32_t dx = 0;
        int32_t dy = 0;
        PadDirection(g_cluster.control[kPad], mx, my, dx, dy);
        pad.panX = static_cast<int8_t>(dx);
        pad.panY = static_cast<int8_t>(dy);
    } else if (held == kRotateLeft) {
        pad.rotate = -1;
    } else if (held == kRotateRight) {
        pad.rotate = 1;
    }
    const bool zoomHeld = held == kZoomIn || held == kZoomOut;
    if (g_zoomRepeat.Step(zoomHeld, dt) != 0) pad.zoom = static_cast<int8_t>(held == kZoomIn ? 1 : -1);
    Hold(Source::Pad, pad);
    // A held control keeps the pad source active between a zoom button's repeats.
    g_activeNow[0] = held != kNone;

    // One line per press and release, with the camera after it (TOUCH_MODE.md section 7).
    if (held != g_held) {
        const PlayerController* pc = loop != nullptr ? loop->m_playerController : nullptr;
        if (g_held != kNone && pc != nullptr) {
            std::fprintf(stderr, "[TouchMode] held %s %.2f s: yaw %.1f target %.1f,%.1f distance %.1f\n",
                         kControlNames[g_held], g_heldSeconds, pc->yaw, pc->target.x, pc->target.y, pc->distance);
        }
        if (held != kNone && pc != nullptr) {
            std::fprintf(stderr, "[TouchMode] press %s: yaw %.1f target %.1f,%.1f distance %.1f\n",
                         kControlNames[held], pc->yaw, pc->target.x, pc->target.y, pc->distance);
        }
        g_held = held;
        g_heldSeconds = 0.0f;
    } else if (held != kNone) {
        g_heldSeconds += dt;
    }

    // The active source that started last; keys keep what they set.
    int32_t src = 0;
    if (g_activeNow[1] && (!g_activeNow[0] || g_holdSince[1] > g_holdSince[0])) src = 1;
    const CameraHold& h = g_hold[src];
    if (panX == 0.0f) panX = static_cast<float>(h.panX);
    if (panY == 0.0f) panY = static_cast<float>(h.panY);
    yaw += static_cast<float>(h.rotate) * dt * kRotateDegPerSecond;
    if (zoomDir == 0) {
        if (src == 0 && h.zoom != 0) {
            zoomDir = h.zoom;
        } else if (src == 1 && (g_fingerZoom >= 1.0f || g_fingerZoom <= -1.0f)) {
            zoomDir = g_fingerZoom > 0.0f ? 1 : -1;
            g_fingerZoom -= static_cast<float>(zoomDir);
        }
    }
    // A finger hold lasts one frame: touch_pointer sends it every frame it applies.
    g_hold[1] = CameraHold();
    g_activeBefore[0] = g_activeNow[0];
    g_activeBefore[1] = g_activeNow[1];
    g_activeNow[0] = false;
    g_activeNow[1] = false;
}

void BarRect(int32_t* rect) {
    if (g_open == nullptr) {
        StoreRect(rect, 0, 0, 0, 0);
        return;
    }
    StoreRect(rect, g_bar[0], g_bar[1], g_bar[2], g_bar[3]);
}

void SubToolbarRect(int32_t* rect) {
    if (g_open == nullptr) {
        StoreRect(rect, 0, 0, 0, 0);
        return;
    }
    StoreRect(rect, g_sub[0], g_sub[1], g_sub[2], g_sub[3]);
}

}  // namespace TouchMode
}  // namespace Ext
