// src/ext/enhanced_tab/render_scale_entry.cpp -- the Enhanced tab's "Render scale" row (P8)
//
// NOT RETAIL, and not an extension row: the render scale is a platform setting, the key
// `VideoRenderScale` (a percent, 25..100) beside retail's Video keys in settings.cfg, read by
// Platform::Window::RenderScale (src/platform/common/platform_window_hooks.h; the phone's first run
// seeds 50). The combo offers Full / Half / Quarter = 100 / 50 / 25. Where the platform can change it
// (RenderScaleAdjustable: Android) Apply sets it live: SetRenderScale, then Video::Device's
// settingsChanged, which makes the next frame's settings change rebuild the back buffer, every
// dynamic target and the swap chain at the new size (the Vulkan backend's ApplyEngineSettingsChange),
// and records the key for the next start. Elsewhere the combo shows Full and is disabled: a
// desktop's present path does not scale the frame (RenderScale is 1.0 there). A hand-edited value
// that is none of the three (75) shows as the nearest item and is left alone unless the player picks
// another item.
//
// P9 (docs/android/P9_LOADING_SCALE.md §2): the UI scale is in screen units
// (platform/common/ui_render_scale.h), so a render scale change re-derives the open UserInterface's
// _uiScale from the setting it stands for: the interface keeps its size on the screen. Nothing is
// stored for it (GameUIScale does not change); every later ApplyKeyMapToUI derives the same value.

#include "ext/enhanced_tab/enhanced_tab.h"

#include "platform_window_hooks.h"   // Platform::Window::RenderScale / RenderScaleAdjustable / SetRenderScale
#include "platform_window.h"         // Platform::Window::GetClientRect: the back buffer at the new render scale
#include "platform/common/ui_render_scale.h"   // UiPercentFromRatio / UiScaleRatioAt (P9)
#include "io/Persistence.h"
#include "ui/ComboElement.h"
#include "ui/DialogInstance.h"
#include "ui/UserInterface.h"
#include "video/Device.h"

#include <cstdio>

namespace Ext {
namespace EnhancedTab {

namespace {

UI::ComboElement* g_combo = nullptr;
UI::UserInterface* g_ui = nullptr;   // the Options dialog's interface (P9: its _uiScale is re-derived)
int g_openItem = 0;   // the item Open showed: Apply changes nothing while it is still picked

constexpr int kPercents[] = { 100, 50, 25 };   // Full, Half, Quarter
constexpr int kItemCount = static_cast<int>(sizeof(kPercents) / sizeof(kPercents[0]));

int CurrentPercent() {
    return static_cast<int>(Platform::Window::RenderScale() * 100.0f + 0.5f);
}

int NearestItem(int percent) {
    if (percent >= 75) return 0;
    if (percent >= 37) return 1;
    return 2;
}

// The UserInterface the page's dialog is in: only a dialog's root element carries its
// DialogInstance (dialog_create.cpp), so the page's parents are walked up to it.
UI::UserInterface* DialogUi(UI::Element* e) {
    for (int depth = 0; e != nullptr && depth < 16; ++depth) {
        if (e->_dialogParent != nullptr) return e->_dialogParent->_ui;
        e = e->_parent;
    }
    return nullptr;
}

// The back buffer's new size, published at once as retail's Device::SizeWindow (0x1800f7d20) does
// for a resized window: the size now, the rebuild (settingsChanged) at the next frame. Without it the
// UI scale below changed while the back buffer kept the old size until that rebuild, and the layout
// passes of this update ran in a space the size of neither (Quarter -> Full: 585 x 0.42 = 245 units
// wide where both sides are 982), clamping the open dialog into the top-left corner for the first
// frame drawn at the new size. SizeWindow itself returns early in fullscreen (the phone), so the
// size is written here. The backend's rebuild then finds the size it wants already in place: the
// window is not resized (it does not change), the swap chain and the targets are.
void PublishBackBufferSize() {
    Video::Device* const dev = Video::Device::_singleton;
    if (dev == nullptr) return;
    Platform::Rect rc;
    Platform::Window::GetClientRect(nullptr, &rc);
    const int32_t w = rc.right - rc.left;
    const int32_t h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) return;   // minimised: the rebuild reads the size itself
    dev->backBufferWidth  = w;
    dev->backBufferHeight = h;
}

void Open(UI::Element* page) {
    g_combo = FindElement<UI::ComboElement>(page, "comboRenderScale");
    g_ui = DialogUi(page);
    const bool adjustable = Platform::Window::RenderScaleAdjustable();
    g_openItem = NearestItem(CurrentPercent());
    if (g_combo != nullptr) {
        g_combo->SetCurrentItem(g_openItem);
        if (!adjustable) g_combo->Enable(false);
    }
    std::fprintf(stderr, "[RenderScale] combo=%p ui=%p item=%d (%d %%)%s\n", static_cast<void*>(g_combo),
                 static_cast<void*>(g_ui), g_openItem, CurrentPercent(),
                 adjustable ? "" : ", fixed on this platform");
}

void Apply() {
    if (g_combo == nullptr || !Platform::Window::RenderScaleAdjustable()) return;
    const int item = g_combo->_currentItem;
    if (item < 0 || item >= kItemCount || item == g_openItem) return;
    const int percent = kPercents[item];
    const int before = CurrentPercent();
    const bool changed = percent != before;
    Platform::Window::SetRenderScale(percent);
    if (changed) PublishBackBufferSize();
    if (changed && g_ui != nullptr) {
        // P9: the same UI scale setting at the new render scale (ui_render_scale.h).
        const float was = g_ui->_uiScale;
        const int32_t setting = Platform::Display::UiPercentFromRatio(was, before);
        g_ui->_uiScale = Platform::Display::UiScaleRatioAt(setting, percent);
        std::fprintf(stderr, "[RenderScale] UI scale %d %% kept on the screen: _uiScale %.3f -> %.3f\n",
                     setting, static_cast<double>(was), static_cast<double>(g_ui->_uiScale));
    }
    if (changed && Video::Device::_singleton != nullptr) Video::Device::_singleton->settingsChanged = 1;
    if (IO::Persistence* p = IO::Persistence::_singleton) p->Set("Video", "RenderScale", percent);
    g_openItem = item;
    std::fprintf(stderr, "[RenderScale] Apply: item=%d VideoRenderScale=%d%s\n", item, percent,
                 changed ? " (the targets are rebuilt at the next frame)" : "");
}

void Close() {
    g_combo = nullptr;
    g_ui = nullptr;
}

}  // namespace

const Contribution& RenderScaleEntry() {
    static const Contribution c = { "render_scale", Open, Apply, Close, nullptr };
    return c;
}

}  // namespace EnhancedTab
}  // namespace Ext
