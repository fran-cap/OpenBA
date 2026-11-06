// ============================================================================
// tests/unit/ext_fullscreen_mode_test.cpp -- ext.fullscreen_mode's registry row and its borderless key
// ============================================================================
// The row (class A, ON by default, off under the faithful profile) and ext.fullscreen_borderless (a
// 0..1 range key, 0 = exclusive, so the row alone changes nothing). The window and the swap chain need
// the running game (tools/build.sh run with the Video tab's combo).
// ============================================================================
#include "unit.h"
#include "ext/registry.h"
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
