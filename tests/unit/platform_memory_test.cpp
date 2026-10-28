// ============================================================================
// tests/unit/platform_memory_test.cpp -- src/platform/common/platform_memory.h (P13)
// ============================================================================
// The low-memory rule (total RAM below 6.5 GiB, docs/android/P13_MEM.md §6) on the totals phones of
// each class report, and the Half lines it adds to the phone seed: a player's own texture quality
// key is kept. Header-only parts, so they run on every host.
// ============================================================================
#include "unit.h"
#include "platform/android/phone_seed.h"
#include "platform_memory.h"

#include <string>

namespace {

constexpr uint64_t GiB(double g) {
    return static_cast<uint64_t>(g * 1073741824.0);
}

}  // namespace

UNIT_TEST(platform_memory_low_memory_rule) {
    using Platform::Memory::IsLowMemory;
    CHECK(!IsLowMemory(0));            // not known
    CHECK(IsLowMemory(GiB(1.8)));      // a 2 GB phone
    CHECK(IsLowMemory(GiB(3.6)));      // 4 GB
    CHECK(IsLowMemory(GiB(5.5)));      // 6 GB
    CHECK(IsLowMemory(GiB(6.49)));
    CHECK(!IsLowMemory(GiB(6.5)));
    CHECK(!IsLowMemory(GiB(7.2)));     // an 8 GB phone with large carve-outs
    CHECK(!IsLowMemory(GiB(7.6)));     // the Galaxy S10 (8 GB)
    CHECK(!IsLowMemory(GiB(11.2)));    // the Galaxy S25 (12 GB)
    CHECK(!IsLowMemory(GiB(64)));      // a desktop
}

UNIT_TEST(platform_memory_half_seed_keeps_the_players_choice) {
    using Platform::Android::MissingSeedLines;
    const char kSeed[] = "ext.fps_cap=1\next.texture_quality=1\next.texture_quality_drop=1\n";
    // A first run on a low-memory phone: Half.
    CHECK(MissingSeedLines("", kSeed) == kSeed);
    // An older file on a low-memory phone: the Half lines added once.
    CHECK(MissingSeedLines("ext.fps_cap=1\n", kSeed) == "ext.texture_quality=1\next.texture_quality_drop=1\n");
    // The player chose Full (the row off): kept; the drop key alone is harmless with the row off.
    CHECK(MissingSeedLines("ext.fps_cap=1\next.texture_quality=0\n", kSeed) == "ext.texture_quality_drop=1\n");
}
