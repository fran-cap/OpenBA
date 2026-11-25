// ============================================================================
// tests/unit/ext_fullscreen_mode_test.cpp -- ext.fullscreen_mode's registry row and its borderless key
// ============================================================================
// The row (class A, ON by default, off under the faithful profile) and ext.fullscreen_borderless (a
// 0..1 range key, 0 = exclusive, so the row alone changes nothing). The window and the swap chain need
// the running game (tools/build.sh run with the Video tab's combo).
// ============================================================================
#include "unit.h"
#include "ext/registry.h"
#include "ext/fullscreen_mode/toggle_size.h"
#include "io/settings_file.h"

#include <string>

namespace {

using IO::SettingsFile::Entries;

bool EntriesLookup(void* ctx, const char* key, std::string& value) {
    const Entries* entries = static_cast<const Entries*>(ctx);
    for (const IO::SettingsFile::Entry& e : *entries) {
        if (e.key == key) {
            value = e.value;
            return true;
        }
    }
    return false;
}

Ext::State ResolveText(const char* text) {
    Entries entries = IO::SettingsFile::Parse(std::string(text));
    return Ext::Resolve(&EntriesLookup, &entries, nullptr, nullptr);
}

}  // namespace

UNIT_TEST(ext_fullscreen_mode_registry_row) {
    const Ext::Entry* e = Ext::FindEntry("fullscreen_mode");
    CHECK(e != nullptr);
    if (e == nullptr) return;
    CHECK(e->tier == Ext::Tier::Extension);
    CHECK(e->cls == Ext::Class::A);
    CHECK(e->defaultOn);
    CHECK(e->formatVersion == 0);
    CHECK(Ext::SettingsKey(*e) == "ext.fullscreen_mode");
    CHECK(ResolveText("").ext.fullscreen_mode);
    CHECK(!ResolveText("ext.fullscreen_mode=0\n").ext.fullscreen_mode);
    CHECK(!ResolveText("profile=faithful\n").ext.fullscreen_mode);
}

// Alt+Enter out of fullscreen opens a window that fits the display (toggle_size.h).
UNIT_TEST(ext_fullscreen_mode_toggle_window_fits_the_display) {
    using Ext::FullscreenMode::WindowSizeForToggle;
    int32_t w = 0;
    int32_t h = 0;
    // A saved window that fits is kept.
    WindowSizeForToggle(1600, 900, 2560, 1440, 2560, 1440, &w, &h);
    CHECK_EQ(w, 1600);
    CHECK_EQ(h, 900);
    // So is one as wide as the display (a window that was maximised).
    WindowSizeForToggle(2560, 1377, 2560, 1440, 2560, 1440, &w, &h);
    CHECK_EQ(w, 2560);
    CHECK_EQ(h, 1377);
    // Never saved (-1), and the mode is the display's own size: no room for a frame, three quarters.
    WindowSizeForToggle(-1, -1, 2560, 1440, 2560, 1440, &w, &h);
    CHECK_EQ(w, 1920);
    CHECK_EQ(h, 1080);
    // Never saved, a smaller mode: the mode's size, as the Options dialog opens it.
    WindowSizeForToggle(-1, -1, 1280, 720, 2560, 1440, &w, &h);
    CHECK_EQ(w, 1280);
    CHECK_EQ(h, 720);
    // A scaled 4K display (2560 x 1440 to the process) with the mode and a saved window in physical
    // pixels: neither fits.
    WindowSizeForToggle(3840, 2160, 3840, 2160, 2560, 1440, &w, &h);
    CHECK_EQ(w, 1920);
    CHECK_EQ(h, 1080);
    // No display known: nothing is filtered; nothing at all known: 1280 x 720.
    WindowSizeForToggle(-1, -1, 3840, 2160, 0, 0, &w, &h);
    CHECK_EQ(w, 3840);
    CHECK_EQ(h, 2160);
    WindowSizeForToggle(-1, -1, 0, 0, 0, 0, &w, &h);
    CHECK_EQ(w, 1280);
    CHECK_EQ(h, 720);
}

UNIT_TEST(ext_fullscreen_borderless_key_defaults_to_exclusive) {
    const Ext::ValueKey* k = Ext::FindValueKey("fullscreen_borderless");
    CHECK(k != nullptr);
    if (k == nullptr) return;
    CHECK(std::string(k->owner) == "fullscreen_mode");
    CHECK(k->choices == nullptr);
    CHECK(k->defaultValue == 0);
    CHECK(ResolveText("").val.fullscreen_borderless == 0);
    CHECK(ResolveText("ext.fullscreen_borderless=1\n").val.fullscreen_borderless == 1);
    CHECK(ResolveText("ext.fullscreen_borderless=7\n").val.fullscreen_borderless == 1);   // clamped
    CHECK(ResolveText("ext.fullscreen_borderless=x\n").val.fullscreen_borderless == 0);   // malformed
}
