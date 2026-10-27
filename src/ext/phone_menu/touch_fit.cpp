// ============================================================================
// src/ext/phone_menu/touch_fit.cpp -- ext.phone_menu: the game's dialogs fitted for a finger (P9)
// ============================================================================
// The rules are in touch_fit.h; the arithmetic is touch_math.cpp.
//
// HOW RETAIL SIZES WHAT IS CHANGED HERE (read off the engine's bodies):
//   * A scroll bar (UI::ScrollElement, ComputeSize 0x18011ad80) is as thick as its track sprite
//     (12 units for the game's SharedElements.rsc bar). The scrolling list around it
//     (UI::ContainerElement, ComputeSize 0x1800cc850) gives the bar its padded width, raised to the
//     bar's _minWidth, and the clip region what is left of the container's requested width. The bar
//     fills that gutter only when its stretch flag is set (ComputeLayout 0x18011af20); its thumb
//     (GetBarLayout 0x18011a2d0) and hit test follow its laid-out rectangle.
//     So: the bar's _minWidth (a horizontal bar: _minHeight) becomes the thicker side, its stretch
//     flag is set, and the container's requested width grows by what the bar gained, so the list's
//     rows keep their width (Mod.rsc's rows are `_minWidth = 580`, the clip's width exactly).
//   * A close button (StandardDialog.rsc:buttonClose, a 20-unit ButtonElement around a 16-unit
//     ImageElement padded 2) is sized by its icon; an ImageElement draws its sprite over its own
//     `_width` x `_height` when they are set (ComputeSize 0x180118650, ImageDescription +0x78 / +0x7c,
//     -1 = the sprite's size). So the icon's _width / _height become twice the sprite's; the button,
//     its frame and the title bar grow around it.
//   * A dialog's layout space is the back buffer times UserInterface::_uiScale (Update), the
//     scale the Options slider writes and every draw reads (Draw, ext.sharp_ui's fonts). A dialog
//     larger than that space is laid out with its bottom-right on the screen's (DialogInstance::
//     ComputeLayout 0x180113d90) and its top cut off. The Options dialog's tallest page (Video, a
//     plain ribbon) does not scroll, so no list can give the height back: such a dialog is shown
//     at a higher UI scale while it is open instead (the choice is in docs/android/P9_DIALOGS.md §3),
//     the dialog alone, through UI::UserInterface::_extDialogScale (P11_PHONE_BUGS.md §1).
// ============================================================================

#include "ext/phone_menu/touch_fit.h"
#include "ext/phone_menu/touch_math.h"
#include "ext/phone_menu/touch_size.h"
#include "ext/registry.h"
#include "ext/touch_mode/touch_mode.h"   // TouchMode::Scale: the toolbar keeps its own scale

#include <atomic>
#include <cstdio>
#include <cstring>

#include "object/TypeInfo.h"
#include "ui/ComboDialog.h"
#include "ui/ContainerElement.h"
#include "ui/DialogInstance.h"
#include "ui/ElementDescription.h"
#include "ui/ImageDescription.h"
#include "ui/ImageElement.h"
#include "ui/ScrollElement.h"
#include "ui/ToolTipDialog.h"
#include "ui/UserInterface.h"
#include "input/Device.h"   // Input::Device::_singleton: the left button (a finger down)
#include "platform/common/platform_input.h"   // Platform::Input::PointerIsTouch (P11)
#include "video/Device.h"   // Video::Device::_singleton: the back buffer the fitted scale is for

namespace Ext {
namespace PhoneMenu {

namespace {

// Element::_flags bits 2 / 3 (UI::kElementFlagAlignArg1 / 2): the sticky stretch overrides a scroll
// bar's ComputeLayout reads (widget_common.h kFlagStretchX / kFlagStretchY).
constexpr uint32_t kStretchX = UI::kElementFlagAlignArg1;
constexpr uint32_t kStretchY = UI::kElementFlagAlignArg2;

// Set when anything was changed with the row on; while it is set, a frame with the row off walks the
// open dialogs to put retail's values back. Cleared once a few frames have restored nothing.
bool g_touched = false;
int32_t g_quietFrames = 0;
int32_t g_changes = 0;   // this frame's changes (applied or restored)

// ---- (a) scroll bars --------------------------------------------------------------

void FitScroll(UI::ScrollElement* bar, bool on) {
    const UI::ElementDescription* desc = bar->_description;
    if (desc == nullptr) return;
    const bool vertical = bar->_vertical != 0;
    const int32_t sprite = vertical ? bar->_trackStart.Width() : bar->_trackStart.Height();
    if (sprite <= 0) return;
    const int32_t pads = vertical ? bar->_leftPad + bar->_rightPad : bar->_topPad + bar->_bottomPad;
    const int32_t retailMin = vertical ? desc->_minWidth : desc->_minHeight;
    const int32_t retailExtent = sprite + pads > retailMin ? sprite + pads : retailMin;
    const int32_t target = ScrollBarThickness(sprite, retailExtent);
    if (target == retailExtent) return;
    const uint32_t stretch = vertical ? kStretchX : kStretchY;
    int32_t& minSide = vertical ? bar->_minWidth : bar->_minHeight;
    const bool applied = minSide == target;
    if (on == applied) return;

    // The scrolling list the bar belongs to: its requested width (height, for the bottom bar) grows
    // by what the bar gains, so the clip region keeps its width.
    int32_t delta = target - retailExtent;
    if (!on) delta = -delta;
    UI::Element* parent = bar->_parent;
    if (::Object::IsDerivedFromNullSafe(parent, UI::ContainerElement::TypeId())) {
        UI::ContainerElement* list = static_cast<UI::ContainerElement*>(parent);
        if (list->_children._size > 1 && list->_children._data[1] == bar && list->_requestedWidth > 0)
            list->_requestedWidth += delta;
        if (list->_children._size > 2 && list->_children._data[2] == bar && list->_requestedHeight > 0)
            list->_requestedHeight += delta;
    }
    if (on) {
        minSide = target;
        bar->_flags |= stretch;
    } else {
        minSide = retailMin;
        if ((desc->_flags & stretch) == 0) bar->_flags &= ~stretch;
    }
    bar->SetNeedsSizeCalc();
    ++g_changes;
}

// ---- (b) close buttons -------------------------------------------------------------

void FitClose(UI::Element* button, bool on) {
    for (int32_t i = 0; i < button->_children._size; ++i) {
        UI::Element* child = button->_children._data[i];
        if (!::Object::IsDerivedFromNullSafe(child, UI::ImageElement::TypeId())) continue;
        UI::ImageElement* icon = static_cast<UI::ImageElement*>(child);
        const UI::ElementDescription* base = icon->_description;
        if (!::Object::IsDerivedFromNullSafe(base, UI::ImageDescription::TypeId())) return;
        const UI::ImageDescription* desc = static_cast<const UI::ImageDescription*>(base);
        const int32_t w = CloseIconSide(icon->_sprite.Width());
        const int32_t h = CloseIconSide(icon->_sprite.Height());
        if (w <= 0 || h <= 0) return;
        const bool applied = icon->_width == w && icon->_height == h;
        if (on == applied) return;
        icon->_width = on ? w : desc->_width;
        icon->_height = on ? h : desc->_height;
        icon->SetNeedsSizeCalc();
        ++g_changes;
        return;
    }
}

// Every frame over every open dialog's tree (the Options dialog is about 2000 elements), so the
// tests are the cheap ones: the exact type (no class derives from ScrollElement) and the name's
// first letter before the compare.
void Walk(UI::Element* e, bool on) {
    if (e == nullptr) return;
    if (e->IsA(UI::ScrollElement::TypeId())) {
        FitScroll(static_cast<UI::ScrollElement*>(e), on);
        return;
    }
    const char* name = e->_name;
    if (name != nullptr && name[0] == 'b' && std::strcmp(name, "buttonClose") == 0) {
        FitClose(e, on);
        return;
    }
    for (int32_t i = 0; i < e->_children._size; ++i) {
        Walk(e->_children._data[i], on);
    }
}

// ---- (c) a dialog larger than the safe area -------------------------------------

// The windows shown smaller than the UI scale this frame: the dialog, its UserInterface and its
// own scale k (UserInterface::_extDialogScale: own units = layout units / k, k < 1). Written by the
// main thread at a UI's first layout call, read by the seam's three call points (Update's layout
// and pump on the main thread, Draw on whichever thread draws the UI): one relaxed atomic per field,
// so a reader sees a stale entry for one frame at worst.
struct DialogFit {
    std::atomic<const UI::DialogInstance*> dialog{nullptr};
    std::atomic<const UI::UserInterface*> ui{nullptr};
    std::atomic<float> k{1.0f};
};
constexpr int kMaxFits = 8;
DialogFit g_dialogFits[kMaxFits];

DialogFit* FitEntry(const UI::DialogInstance* d) {
    for (DialogFit& f : g_dialogFits) {
        if (f.dialog.load(std::memory_order_relaxed) == d) return &f;
    }
    return nullptr;
}

// A window the fit is for: a dialog with a description, shown, not a whole-screen picture (a
// backdrop: the menus, the loading screen) and not sized to the screen (ComputeLayout's 0x10 / 0x20).
// Combo drop-downs (no description) and tooltips follow the window they belong to (FitOwner).
bool IsWindow(const UI::DialogInstance* d, UI::Element*& root) {
    root = d->_root.Get();
    if (d->_dialog == nullptr || root == nullptr) return false;
    if (d->_spriteSheet != nullptr && d->_spriteIndex >= 0) return false;
    if ((root->_flags & (UI::kElementFlagHidden | 0x30u)) != 0) return false;
    return true;
}

// DialogInstance::ComputeLayout's padded size, sized now (the same ComputeSize it runs).
void PaddedSize(UI::Element* root, int32_t& w, int32_t& h) {
    root->ComputeSize();
    w = root->_layoutWidth + root->_leftPad + root->_rightPad;
    h = root->_layoutHeight + root->_topPad + root->_bottomPad;
    if (w < root->_minWidth) w = root->_minWidth;
    if (h < root->_minHeight) h = root->_minHeight;
}

DialogFit* NewFit() {
    for (DialogFit& e : g_dialogFits) {
        if (e.dialog.load(std::memory_order_relaxed) == nullptr) return &e;
    }
    return nullptr;
}

// Once a frame per UserInterface, before its dialogs are laid out: which of its top-level windows
// are larger than the safe area at the player's UI scale, and the scale each is shown at. Only the
// window itself (and what it opens: its child dialogs, its tooltip, its combo drop-downs) takes
// that scale; the rest of the interface keeps the player's (docs/android/P11_PHONE_BUGS.md §1).
// Not judged: a child dialog (its parent places it; a sub-toolbar's padding reaches across the
// screen) and a dialog ext.touch_mode scales itself (the toolbar).
void DecideScale(UI::UserInterface* ui, bool on) {
    const float user = ui->_uiScale;
    const ::Video::Device* device = ::Video::Device::_singleton;
    bool keep[kMaxFits] = {};
    if (on && device != nullptr && user > 0.0f) {
        for (auto* node = ui->_dialogs._head; node != nullptr; node = node->next) {
            const UI::DialogInstance* d = node->element;
            UI::Element* root = nullptr;
            if (d == nullptr || d->_parent.Get() != nullptr || !IsWindow(d, root)) continue;
            if (TouchMode::Scale(d) != 1.0f) continue;
            int32_t w = 0;
            int32_t h = 0;
            PaddedSize(root, w, h);
            float s = FitScale(user, device->backBufferWidth, device->backBufferHeight, w, h);
            DialogFit* f = FitEntry(d);
            const float before = f != nullptr ? user / f->k.load(std::memory_order_relaxed) : user;
            // Keep a fit that is a hair above what is wanted (text measured in another font size can
            // move a dialog by a unit), so the scale does not hunt between two percents.
            if (s < before && s > user && before - s < 0.015f) s = before;
            if (s <= user) continue;
            const float k = user / s;
            if (f == nullptr) {
                f = NewFit();
                if (f == nullptr) continue;   // more than kMaxFits tall windows open: retail's layout
                f->ui.store(ui, std::memory_order_relaxed);
                f->k.store(k, std::memory_order_relaxed);
                f->dialog.store(d, std::memory_order_relaxed);
                std::fprintf(stderr, "[PhoneMenu] dialog %p larger than the safe area: shown at UI scale %.2f "
                             "(k %.3f), the interface keeps the player's %.2f\n", (const void*)d,
                             static_cast<double>(s), static_cast<double>(k), static_cast<double>(user));
            } else if (f->k.load(std::memory_order_relaxed) != k) {
                f->k.store(k, std::memory_order_relaxed);
                std::fprintf(stderr,
                             "[PhoneMenu] dialog %p now shown at UI scale %.2f (k %.3f), the player's %.2f\n",
                             (const void*)d, static_cast<double>(s), static_cast<double>(k),
                             static_cast<double>(user));
            }
            keep[f - g_dialogFits] = true;
        }
    }
    for (int i = 0; i < kMaxFits; ++i) {
        DialogFit& e = g_dialogFits[i];
        if (keep[i] || e.ui.load(std::memory_order_relaxed) != ui) continue;
        const UI::DialogInstance* gone = e.dialog.load(std::memory_order_relaxed);
        if (gone == nullptr) continue;
        e.dialog.store(nullptr, std::memory_order_relaxed);
        e.ui.store(nullptr, std::memory_order_relaxed);
        e.k.store(1.0f, std::memory_order_relaxed);
        std::fprintf(stderr, "[PhoneMenu] dialog %p back at the interface's scale\n", (const void*)gone);
    }
}

// The top-level dialog whose scale `d` follows: the root of its parent chain; a tooltip follows its
// owner, a combo drop-down (not linked as a child: ComboDialog::_parentDialog) its box's dialog.
const UI::DialogInstance* FitOwner(const UI::DialogInstance* d) {
    for (int guard = 0; guard < 16; ++guard) {
        const UI::DialogInstance* parent = d->_parent.Get();
        if (parent == nullptr && d->IsA(UI::ComboDialog::TypeId()))
            parent = static_cast<const UI::ComboDialog*>(d)->_parentDialog.Get();
        const UI::UserInterface* ui = d->_ui;
        const UI::DialogInstance* tip = ui != nullptr ? ui->_toolTipDialog : nullptr;
        if (parent == nullptr && tip != nullptr && d == tip)
            parent = ui->_toolTipOwner;
        if (parent == nullptr) return d;
        d = parent;
    }
    return d;
}

// ---- (e) a lifted finger does not hover ---------------------------------------------

// Retail shows a tooltip once the pointer has rested on a dialog for 0.25 s with no button down
// (DialogInstance::Update: `_hoverTime` grows while the pointer stands still, `_hovering` is set past
// the delay, an element under it then opens its tooltip; a button down ends the hover). A finger
// leaves the pointer where it lifted, so after every tap the tapped control's tooltip came up and
// stayed, over whatever is above it (the owner's ruling, P11: HIDE it when the finger lifts). With
// the pointer last moved by a touch and no finger down, no dialog of this UserInterface counts
// hover time, and a tooltip still up is closed (the arm BeginToolTip runs before opening a new
// one). The element keeps its "tooltip begun" flag, so it does not reopen until the pointer leaves
// it. A real mouse (on the phone too) is untouched. Main thread, before the dialogs are pumped.
void FingerUpHidesToolTip(UI::UserInterface* ui) {
    if (!::Platform::Input::PointerIsTouch()) return;
    const ::Input::Device* in = ::Input::Device::_singleton;
    if (in == nullptr || in->_mouse[4].value > 0) return;   // a finger down: retail's hover rule
    for (auto* node = ui->_dialogs._head; node != nullptr; node = node->next) {
        if (node->element != nullptr) node->element->_hoverTime = 0;
    }
    if (ui->_toolTipDialog == nullptr) return;
    Object::InternalPtr<UI::DialogInstance> tip(ui->_toolTipDialog);
    ui->CloseDialog(&tip);
    ui->_toolTipDialog = nullptr;
    ui->_toolTipOwner = nullptr;
}

}  // namespace

void TouchFrame(int32_t point, const UI::DialogInstance* dialog) {
    if (point != UI::UserInterface::kExtPlaceLayout) return;
    if (dialog == nullptr) return;
    UI::UserInterface* ui = dialog->_ui;
    if (ui == nullptr) return;
    const bool on = Ext().phone_menu;
    const bool head = ui->_dialogs._head != nullptr && ui->_dialogs._head->element == dialog;
    if (!head || (!on && !g_touched)) return;
    // Once a frame per UserInterface, before any of its dialogs is laid out.
    g_changes = 0;
    for (auto* node = ui->_dialogs._head; node != nullptr; node = node->next) {
        if (node->element == nullptr) continue;
        Walk(node->element->_root.Get(), on);
        g_changes += TouchSizeWalk(node->element, on);   // P11 (touch_size.h)
    }
    DecideScale(ui, on);
    if (on) FingerUpHidesToolTip(ui);
    if (on) {
        g_touched = true;
        g_quietFrames = 0;
    } else if (g_changes == 0 && ++g_quietFrames > 8) {
        g_touched = false;
    }
}

float FitDialogScale(const UI::DialogInstance* dialog) {
    if (dialog == nullptr) return 1.0f;
    const DialogFit* f = FitEntry(FitOwner(dialog));
    return f != nullptr ? f->k.load(std::memory_order_relaxed) : 1.0f;
}

void BackdropLowerDraw(int32_t width, int32_t height, int32_t* rect) {
    const Rect lower = LowerBackdrop(Rect{rect[0], rect[1], rect[2], rect[3]}, width, height);
    if (lower.x2 <= lower.x1) return;
    rect[0] = lower.x1;
    rect[1] = lower.y1;
    rect[2] = lower.x2;
    rect[3] = lower.y2;
}

}  // namespace PhoneMenu
}  // namespace Ext
