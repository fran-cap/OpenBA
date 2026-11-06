// src/ext/fullscreen_mode/fullscreen_mode.cpp -- NOT RETAIL. ext.fullscreen_mode (fullscreen_mode.h has the
// contract)

#include "ext/fullscreen_mode/fullscreen_mode.h"

#include "ext/registry.h"

#include "object/External.h"
#include "object/TypeInfo.h"
#include "platform_window.h"   // Platform::Window::GetMonitorRect
#include "render_backend.h"    // Render::SupportsExclusiveFullscreen
#include "system/Filename.h"
#include "ui/CheckElement.h"
#include "ui/ComboElement.h"
#include "ui/DialogInstance.h"
#include "ui/ElementDescription.h"
#include "ui/GroupElement.h"
#include "video/Device.h"

#include <cstdio>

namespace Ext {
namespace FullscreenMode {

namespace {

constexpr const char* kCellRsc = "Dialog/FullscreenMode.rsc:fullscreenModeCell";

// The combo's items, in order (FullscreenMode.rsc fullscreenModeItems).
enum Item : int32_t {
    kItemOff        = 0,
    kItemExclusive  = 1,
    kItemBorderless = 2,
};

const UI::DialogInstance* g_dialog = nullptr;   // the one open Options dialog (it is modal)
UI::ComboElement* g_combo = nullptr;
UI::CheckElement* g_check = nullptr;
int32_t g_committedFrom = -1;   // ext.fullscreen_borderless before CommitVideoRow, -1 = no commit

const ValueKey& BorderlessKey() {
    static const ValueKey* k = FindValueKey("fullscreen_borderless");
    return *k;
}

UI::Element* Child(const UI::Element* e, int32_t index) {
    if (index < 0 || index >= e->_children._size) return nullptr;
    return e->_children._data[index];
}

int32_t IndexIn(const UI::Element* parent, const UI::Element* child) {
    for (int32_t i = 0; i < parent->_children._size; ++i) {
        if (parent->_children._data[i] == child) return i;
    }
    return -1;
}

void Dirty(UI::Element* e) {
    for (UI::Element* p = e; p != nullptr; p = p->_parent) p->SetNeedsSizeCalc();
}

// The checkbox's cell given to a new fullscreenModeCell group: its combo shown, the checkbox its hidden
// second child (the slot the description leaves for it, an empty group, deleted). Null when the
// resource did not load or its shape is not the one FullscreenMode.rsc writes.
UI::ComboElement* TakeCell(const UI::DialogInstance* dialog, UI::CheckElement* check) {
    UI::Element* parent = check->_parent;
    const int32_t index = parent != nullptr ? IndexIn(parent, check) : -1;
    if (index < 0) {
        std::fprintf(stderr, "[FullscreenMode] the Fullscreen checkbox has no cell: retail's checkbox kept\n");
        return nullptr;
    }
    UI::ElementDescription* desc =
        ::Object::ExternalFactory::_singleton->FindOrLoadAs<UI::ElementDescription>(System::Filename(kCellRsc));
    if (!::Object::IsDerivedFromNullSafe(desc, UI::ElementDescription::TypeId())) {
        std::fprintf(stderr, "[FullscreenMode] %s did not load (%p): retail's checkbox kept\n", kCellRsc,
                     static_cast<const void*>(desc));
        return nullptr;
    }
    Object::InternalPtr<UI::Element> made;
    desc->CreateInstance(&made, dialog->_dialog);
    UI::Element* group = made.Get();
    UI::Element* combo = group != nullptr ? Child(group, 0) : nullptr;
    UI::Element* slot = group != nullptr ? Child(group, 1) : nullptr;
    if (group == nullptr || !group->IsA(UI::GroupElement::TypeId()) || group->_children._size != 2 ||
        !::Object::IsDerivedFromNullSafe(combo, UI::ComboElement::TypeId()) || slot == nullptr) {
        std::fprintf(stderr, "[FullscreenMode] %s is not a group of combo + slot: retail's checkbox kept\n",
                     kCellRsc);
        delete group;
        return nullptr;
    }
    delete slot;
    group->_children._data[1] = check;
    check->_parent = group;
    check->Hide(true);
    group->_parent = parent;
    parent->_children._data[index] = group;
    Dirty(group);
    return static_cast<UI::ComboElement*>(combo);
}

}  // namespace

bool Offered() {
    return Ext().fullscreen_mode && Render::SupportsExclusiveFullscreen();
}

bool Borderless() {
    const Video::Configuration* cfg = Video::Configuration::_singleton;
    if (cfg == nullptr || cfg->fullscreen == 0 || !Render::SupportsExclusiveFullscreen()) return false;
    if (Installed()) return Ext().fullscreen_mode && Val().fullscreen_borderless != 0;
    // Inside Engine::Startup (the DX11 device is created before Ext::InitFromSettings installs the
    // registry): the same answer, resolved from settings.cfg and the command line once.
    static const State boot = ResolveFromSettings();
    return boot.ext.fullscreen_mode && boot.val.fullscreen_borderless != 0;
}

bool DesktopSize(void* monitor, int32_t* width, int32_t* height) {
    if (monitor == nullptr) return false;
    Platform::Rect r = {0, 0, 0, 0};
    Platform::Window::GetMonitorRect(monitor, &r);
    if (r.right <= r.left || r.bottom <= r.top) return false;
    *width = r.right - r.left;
    *height = r.bottom - r.top;
    return true;
}

void OpenVideoRow(UI::DialogInstance* dialog, UI::CheckElement* checkFullscreen) {
    g_dialog = nullptr;
    g_combo = nullptr;
    g_check = nullptr;
    g_committedFrom = -1;
    if (!Offered() || dialog == nullptr || checkFullscreen == nullptr) return;
    UI::ComboElement* combo = TakeCell(dialog, checkFullscreen);
    if (combo == nullptr) return;
    int32_t item = kItemOff;
    if (checkFullscreen->_checked != 0) item = Val().fullscreen_borderless != 0 ? kItemBorderless : kItemExclusive;
    combo->SetCurrentItem(item);
    g_dialog = dialog;
    g_combo = combo;
    g_check = checkFullscreen;
    std::fprintf(stderr, "[FullscreenMode] Video tab: combo items=%d item=%d\n", combo->GetItemCount(), item);
}

bool ReadVideoRow(const UI::DialogInstance* dialog) {
    if (dialog == nullptr || dialog != g_dialog) return false;
    const int32_t item = g_combo->_currentItem;
    if (item < kItemOff || item > kItemBorderless) return false;
    g_check->_checked = item != kItemOff ? 1 : 0;
    // Off keeps the stored choice (the next Fullscreen comes back as it was): only a fullscreen item
    // changes it.
    if (item == kItemOff) return false;
    const int32_t borderless = item == kItemBorderless ? 1 : 0;
    return borderless != Val().fullscreen_borderless;
}

void CommitVideoRow(const UI::DialogInstance* dialog) {
    if (dialog == nullptr || dialog != g_dialog) return;
    g_committedFrom = -1;   // a second Apply in the same dialog rolls back only its own change
    const int32_t item = g_combo->_currentItem;
    if (item != kItemExclusive && item != kItemBorderless) return;
    const int32_t borderless = item == kItemBorderless ? 1 : 0;
    if (borderless == Val().fullscreen_borderless) return;
    g_committedFrom = Val().fullscreen_borderless;
    const int32_t stored = SetValueAndRecord(BorderlessKey(), borderless);
    std::fprintf(stderr, "[FullscreenMode] Apply: ext.fullscreen_borderless=%d\n", stored);
}

void RollbackVideoRow(const UI::DialogInstance* dialog) {
    if (dialog == nullptr || dialog != g_dialog || g_committedFrom < 0) return;
    SetValueAndRecord(BorderlessKey(), g_committedFrom);
    std::fprintf(stderr, "[FullscreenMode] the video reset failed: ext.fullscreen_borderless=%d again\n",
                 g_committedFrom);
    g_committedFrom = -1;
}

void CloseVideoRow(const UI::DialogInstance* dialog) {
    if (dialog == nullptr || dialog != g_dialog) return;
    g_dialog = nullptr;
    g_combo = nullptr;
    g_check = nullptr;
    g_committedFrom = -1;
}

}  // namespace FullscreenMode
}  // namespace Ext
