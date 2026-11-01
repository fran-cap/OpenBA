// ============================================================================
// tests/unit/display_size_test.cpp -- src/platform/common/platform_display_size.h (issue #96)
// ============================================================================
// The tablet line (the shorter physical edge at 3.3 inches) on the sizes phones and tablets of each
// class report, the dpi plausibility check, and the tablet seed built from the phone seed
// (phone_seed.h WithSeedOverrides). Header-only parts, so they run on every host.
// ============================================================================
#include "unit.h"
#include "platform/android/phone_seed.h"
#include "platform_display_size.h"

#include <string>

UNIT_TEST(display_size_tablet_line) {
    using Platform::Display::IsTabletScreen;
    CHECK(!IsTabletScreen(0.0f, 0.0f));     // unknown
    CHECK(!IsTabletScreen(5.62f, 0.0f));    // half unknown
    CHECK(!IsTabletScreen(2.60f, 5.62f));   // Galaxy S25 class (6.2", 2340x1080 at ~416 ppi)
    CHECK(!IsTabletScreen(6.30f, 2.89f));   // 6.9" 19.5:9, landscape
    CHECK(!IsTabletScreen(3.08f, 6.40f));   // 7.2" 18.7:9 phablet
    CHECK(IsTabletScreen(6.10f, 3.43f));    // 7" 16:9 tablet
    CHECK(IsTabletScreen(3.71f, 5.94f));    // 7" 16:10 tablet
    CHECK(IsTabletScreen(10.60f, 7.50f));   // OnePlus Pad class (3392x2400 at 320 dpi)
    CHECK(IsTabletScreen(3.30f, 5.00f));    // the line itself is a tablet
}

UNIT_TEST(display_size_plausible_dpi) {
    using Platform::Display::PlausibleDpi;
    CHECK(PlausibleDpi(416.0f, 420.0f) == 416.0f);   // the panel's own figure
    CHECK(PlausibleDpi(160.0f, 420.0f) == 420.0f);   // a placeholder xdpi: the bucket
    CHECK(PlausibleDpi(0.0f, 320.0f) == 320.0f);
    CHECK(PlausibleDpi(300.0f, 0.0f) == 300.0f);     // no bucket: the reported figure
    CHECK(PlausibleDpi(0.0f, 0.0f) == 0.0f);         // unknown
}

UNIT_TEST(display_size_tablet_seed_overrides) {
    using Platform::Android::WithSeedOverrides;
    const char seed[] = "GameEdgeScroll=false\nVideoRenderScale=50\next.touch_mode_toolbar_scale=200\n";
    const char overrides[] = "VideoRenderScale=100\nGameUIScale=50\next.touch_mode_toolbar_scale=150\n";
    // Replaced in place, the new key appended, the other lines kept.
    CHECK(WithSeedOverrides(seed, overrides) ==
          "GameEdgeScroll=false\nVideoRenderScale=100\next.touch_mode_toolbar_scale=150\nGameUIScale=50\n");
    // No overrides: the seed unchanged.
    CHECK(WithSeedOverrides(seed, "") == seed);
    // A longer key does not replace a shorter one.
    CHECK(WithSeedOverrides("ext.fps_cap=1\n", "ext.fps_cap_rate=30\n") ==
          "ext.fps_cap=1\next.fps_cap_rate=30\n");
}
