#pragma once
// ============================================================================
// src/platform/common/platform_display_size.h -- the display's physical size, and the tablet class
// ============================================================================
// NOT RETAIL (issue #96). The Android first run (src/platform/android/app_android.cpp) seeds its
// settings.cfg by the class of the screen: a phone's defaults, or a tablet's when the screen's
// shorter physical edge is at least kTabletShortEdgeInches. The class is keyed on inches, not pixels
// or the density bucket: a 2340x1080 phone and a 2560x1600 tablet are both "xhdpi-ish" and both
// about 2.5k pixels wide, but one is 6 cm tall in landscape and the other 13 cm.
//   Android (src/platform/android/display_size_android.cpp): DisplayMetrics of the whole display
//            (getRealMetrics: widthPixels / xdpi, heightPixels / ydpi) asked of the activity through
//            JNI (OpenBAActivity.physicalDisplay).
//   Everywhere else (src/platform/posix/display_size_posix.cpp, also macOS's; src/platform/win/
//            display_size_win.cpp): unknown.
// D4_RULES C1: namespace `Platform`, free functions, NO OS header here. The rules below are pure
// (tests/unit/display_size_test.cpp).
// ============================================================================

namespace Platform {
namespace Display {

// The tablet line: the shorter physical edge (the height in landscape, the room the touch controls,
// the toolbar and the panels share) at or above 3.3 inches. The largest phones stay below it: a 6.9"
// 19.5:9 phone is 2.89" short, a 6.8" 20:9 one 2.79", the widest phablets about 3.1" (a 7.2" 18.7:9).
// The smallest tablets are above it: a 7" 16:9 tablet is 3.43" short, a 7" 16:10 one 3.71", an 8"
// 16:10 one 4.24"; a book-style foldable opened (about 7.6" near-square) is about 5". It is SDL's own
// 7-inch-diagonal tablet line (SDLActivity.isTablet) measured on the short edge instead, so a long
// 21:9 phone cannot cross it on its diagonal.
constexpr float kTabletShortEdgeInches = 3.3f;

// Whether a screen of widthInches x heightInches (either orientation) is a tablet by the line above.
// A size <= 0 (unknown) is not.
constexpr bool IsTabletScreen(float widthInches, float heightInches) {
    return widthInches > 0.0f && heightInches > 0.0f &&
           (widthInches < heightInches ? widthInches : heightInches) >= kTabletShortEdgeInches;
}

// The dots per inch to divide by: the panel's `reported` dpi (DisplayMetrics xdpi / ydpi) when it is
// within 0.6x..1.6x of the density bucket `densityDpi` (DisplayMetrics densityDpi), else the bucket.
// Some devices report a placeholder xdpi (160, or 0) that would make a phone a 15-inch screen; the
// bucket is the nearest standard step to the real ppi, so it is never that far off. Both <= 0: 0
// (unknown).
constexpr float PlausibleDpi(float reported, float densityDpi) {
    if (densityDpi <= 0.0f) return reported > 0.0f ? reported : 0.0f;
    if (reported >= 0.6f * densityDpi && reported <= 1.6f * densityDpi) return reported;
    return densityDpi;
}

// The display's physical size in inches (the whole panel, not the app's window), width first in
// the display's current orientation. False when the platform does not know it (*widthInches and
// *heightInches are then 0).
bool PhysicalScreenInches(float* widthInches, float* heightInches);

}  // namespace Display
}  // namespace Platform
