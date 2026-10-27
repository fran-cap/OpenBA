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
//     plain ribbon) does not scroll, so no list can give the height back: the UI scale is raised
//     while such a dialog is open instead (the choice is in docs/android/P9_DIALOGS.md §3).
// ============================================================================

#include "ext/phone_menu/touch_fit.h"
#include "ext/phone_menu/touch_math.h"
#include "ext/phone_menu/touch_size.h"
#include "ext/registry.h"

#include <cstdio>
#include <cstring>

#include "object/TypeInfo.h"
#include "ui/ContainerElement.h"
#include "ui/DialogInstance.h"
#include "ui/ElementDescription.h"
#include "ui/ImageDescription.h"
#include "ui/ImageElement.h"
#include "ui/ScrollElement.h"
#include "ui/UserInterface.h"
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

// One UserInterface's fitted scale. A loop's UserInterface lives as long as the loop (the front
// end's, the game's, a modal's nested one), so a handful of entries is enough; a new one at an old
// address starts inactive unless its scale happens to equal the old fit.
struct ScaleFit {
    const UI::UserInterface* ui = nullptr;
    float user = 1.0f;      // the player's scale (what the Options slider / settings.cfg wrote)
    float applied = 1.0f;   // the scale in force while `active`
    bool active = false;
};
constexpr int kMaxUis = 4;
ScaleFit g_fits[kMaxUis];
int g_nextFit = 0;

ScaleFit* FitOf(const UI::UserInterface* ui, bool create) {
    for (ScaleFit& f : g_fits) {
        if (f.ui == ui) return &f;
    }
    if (!create) return nullptr;
    ScaleFit& f = g_fits[g_nextFit];
    g_nextFit = (g_nextFit + 1) % kMaxUis;
    f = ScaleFit();
    f.ui = ui;
    return &f;
}

// A window the fit is for: a dialog with a description, shown, not a whole-screen picture (a
// backdrop: the menus, the loading screen) and not sized to the screen (ComputeLayout's 0x10 / 0x20).
// Combo drop-downs (no description) and tooltips keep retail's placement.
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

void DecideScale(UI::UserInterface* ui, bool on) {
    ScaleFit* fit = FitOf(ui, on);
    if (fit == nullptr) return;
    if (!fit->active || ui->_uiScale != fit->applied) fit->user = ui->_uiScale;   // the player's own
    float want = fit->user;
    const ::Video::Device* device = ::Video::Device::_singleton;
    if (on && device != nullptr) {
        for (auto* node = ui->_dialogs._head; node != nullptr; node = node->next) {
            UI::Element* root = nullptr;
            if (node->element == nullptr || !IsWindow(node->element, root)) continue;
            int32_t w = 0;
            int32_t h = 0;
            PaddedSize(root, w, h);
            const float s = FitScale(fit->user, device->backBufferWidth, device->backBufferHeight, w, h);
            if (s > want) want = s;
        }
    }
    const float now = fit->active ? fit->applied : fit->user;
    // Keep a fit that is a hair above what is wanted (text measured in another font size can move a
    // dialog by a unit), so the scale does not hunt between two percents.
    if (want < now && want > fit->user && now - want < 0.015f) want = now;
    if (want != fit->user) {
        if (!fit->active || fit->applied != want) {
            std::fprintf(stderr, "[PhoneMenu] UI %p: a dialog larger than the safe area, laid out at UI scale "
                         "%.2f (the player's %.2f) while it is open\n", (const void*)ui,
                         static_cast<double>(want), static_cast<double>(fit->user));
        }
        fit->applied = want;
        fit->active = true;
        ui->_uiScale = want;
        return;
    }
    if (fit->active) {
        std::fprintf(stderr, "[PhoneMenu] UI %p: back to the player's UI scale %.2f\n", (const void*)ui,
                     static_cast<double>(fit->user));
        if (ui->_uiScale == fit->applied) ui->_uiScale = fit->user;
        fit->active = false;
    }
}

}  // namespace

void TouchFrame(int32_t point, const UI::DialogInstance* dialog, int32_t& width, int32_t& height, int32_t* rect) {
    if (point != UI::UserInterface::kExtPlaceLayout && point != UI::UserInterface::kExtPlaceAfterLayout) return;
    if (dialog == nullptr) return;
    UI::UserInterface* ui = dialog->_ui;
    if (ui == nullptr) return;
    const bool on = Ext().phone_menu;
    const bool head = ui->_dialogs._head != nullptr && ui->_dialogs._head->element == dialog;
    if (point == UI::UserInterface::kExtPlaceLayout && head && (on || g_touched)) {
        // Once a frame per UserInterface, before any of its dialogs is laid out.
        g_changes = 0;
        for (auto* node = ui->_dialogs._head; node != nullptr; node = node->next) {
            if (node->element == nullptr) continue;
            Walk(node->element->_root.Get(), on);
            g_changes += TouchSizeWalk(node->element, on);   // P11 (touch_size.h)
        }
        DecideScale(ui, on);
        if (on) {
            g_touched = true;
            g_quietFrames = 0;
        } else if (g_changes == 0 && ++g_quietFrames > 8) {
            g_touched = false;
        }
    }
    const ScaleFit* fit = FitOf(ui, false);
    if (fit == nullptr || !fit->active) return;
    const ::Video::Device* device = ::Video::Device::_singleton;
    if (device == nullptr) return;
    width = static_cast<int32_t>(static_cast<float>(device->backBufferWidth) * fit->applied);
    height = static_cast<int32_t>(static_cast<float>(device->backBufferHeight) * fit->applied);
    if (point == UI::UserInterface::kExtPlaceLayout) {
        rect[0] = 0;
        rect[1] = 0;
        rect[2] = width;
        rect[3] = height;
    }
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
