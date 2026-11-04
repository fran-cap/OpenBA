// ============================================================================
// tests/unit/harness_parse_test.cpp -- the checked readers in src/app/clamped_atoi.h that
// replace atoi / atof / sscanf on BANISHED_* text in the harnesses (review F-087). Each case is
// text a user can type: garbage, empty, a sign, overflow, the exact bounds, NaN and infinity.
// ClampedAtoi's own int-range cases are in review_engine_ub_test.cpp.
// ============================================================================
#include "unit.h"

#include <cfloat>
#include <climits>

#include "app/clamped_atoi.h"

UNIT_TEST(harness_parse_clamped_atoi_null) {
    CHECK_EQ(ClampedAtoi(nullptr), 0);
    CHECK_EQ(HarnessAtoi(nullptr), 0);
    CHECK_EQ(ClampedAtof(nullptr), 0.0f);
}

UNIT_TEST(harness_parse_range) {
    CHECK_EQ(ClampedAtoiRange("5", 1, 10), 5);
    CHECK_EQ(ClampedAtoiRange("1", 1, 10), 1);
    CHECK_EQ(ClampedAtoiRange("10", 1, 10), 10);
    CHECK_EQ(ClampedAtoiRange("0", 1, 10), 1);
    CHECK_EQ(ClampedAtoiRange("11", 1, 10), 10);
    CHECK_EQ(ClampedAtoiRange("-3", 1, 10), 1);
    CHECK_EQ(ClampedAtoiRange("junk", 1, 10), 1);   // garbage reads 0, then clamps
    CHECK_EQ(ClampedAtoiRange("", -5, 5), 0);
    CHECK_EQ(ClampedAtoiRange("99999999999999999999", -5, 5), 5);
    CHECK_EQ(ClampedAtoiRange("-99999999999999999999", -5, 5), -5);
}

// The harness range: in-range text reads exactly as atoi did; beyond +-1e9 clamps, so that
// frameNum + delay cannot overflow.
UNIT_TEST(harness_parse_harness_atoi) {
    CHECK_EQ(HarnessAtoi("600"), 600);
    CHECK_EQ(HarnessAtoi("  42,7"), 42);   // atoi stops at the comma
    CHECK_EQ(HarnessAtoi("-1"), -1);
    CHECK_EQ(HarnessAtoi("+3"), 3);
    CHECK_EQ(HarnessAtoi("x5"), 0);
    CHECK_EQ(HarnessAtoi(""), 0);
    CHECK_EQ(HarnessAtoi("1000000000"), kHarnessIntLimit);
    CHECK_EQ(HarnessAtoi("-1000000000"), -kHarnessIntLimit);
    CHECK_EQ(HarnessAtoi("1000000001"), kHarnessIntLimit);
    CHECK_EQ(HarnessAtoi("-1000000001"), -kHarnessIntLimit);
    CHECK_EQ(HarnessAtoi("2147483648"), kHarnessIntLimit);
    CHECK_EQ(HarnessAtoi("99999999999999999999999"), kHarnessIntLimit);
    CHECK_EQ(HarnessAtoi("-99999999999999999999999"), -kHarnessIntLimit);
    CHECK(HarnessAtoi("99999999999999999999999") + kHarnessIntLimit > 0);   // no overflow
}

UNIT_TEST(harness_parse_clamped_atof) {
    CHECK_EQ(ClampedAtof("2.5"), 2.5f);
    CHECK_EQ(ClampedAtof("  -0.25x"), -0.25f);
    CHECK_EQ(ClampedAtof("abc"), 0.0f);
    CHECK_EQ(ClampedAtof(""), 0.0f);
    CHECK_EQ(ClampedAtof("nan"), 0.0f);   // atof gave NaN
    CHECK_EQ(ClampedAtof("-nan"), 0.0f);
    CHECK_EQ(ClampedAtof("inf"), FLT_MAX);
    CHECK_EQ(ClampedAtof("-inf"), -FLT_MAX);
    CHECK_EQ(ClampedAtof("1e39"), FLT_MAX);   // (float)atof was undefined here
    CHECK_EQ(ClampedAtof("-1e39"), -FLT_MAX);
    CHECK_EQ(ClampedAtof("1e999"), FLT_MAX);   // beyond double: strtod's HUGE_VAL
    CHECK_EQ(ClampedAtof("3.4028234663852886e38"), FLT_MAX);   // the exact bound
    CHECK_EQ(ClampedFloat(1.0e300), FLT_MAX);
    CHECK_EQ(ClampedFloat(-1.0e300), -FLT_MAX);
}

UNIT_TEST(harness_parse_scan_int) {
    const char* p = " 12,x";
    int v = -7;
    CHECK(ScanClampedInt(p, &v));
    CHECK_EQ(v, 12);
    CHECK_EQ(*p, ',');
    CHECK(ScanLiteral(p, ','));
    CHECK(!ScanLiteral(p, ','));
    v = -7;
    CHECK(!ScanClampedInt(p, &v));   // "x": no conversion, nothing stored or moved
    CHECK_EQ(v, -7);
    CHECK_EQ(*p, 'x');

    const char* big = "99999999999999999999";
    CHECK(ScanClampedInt(big, &v));
    CHECK_EQ(v, INT_MAX);
    CHECK_EQ(*big, '\0');
    const char* neg = "-99999999999999999999";
    CHECK(ScanClampedInt(neg, &v, -10, 10));
    CHECK_EQ(v, -10);

    const char* empty = "";
    CHECK(!ScanClampedInt(empty, &v));
    const char* null = nullptr;
    CHECK(!ScanClampedInt(null, &v));
    CHECK(!ScanLiteral(null, ','));
}

// sscanf("%d,%d,%d,%d,%d,%d") as the clear-overlap harness used it: the count converted, and the
// entries after it left as they were.
UNIT_TEST(harness_parse_scan_ints) {
    int v[6] = {0, 0, 0, 0, 0, 1};
    CHECK_EQ(ScanClampedInts("3000,60,120", v, 6), 3);
    CHECK_EQ(v[0], 3000);
    CHECK_EQ(v[1], 60);
    CHECK_EQ(v[2], 120);
    CHECK_EQ(v[3], 0);
    CHECK_EQ(v[5], 1);

    int w[2] = {5, 6};
    CHECK_EQ(ScanClampedInts("", w, 2), 0);
    CHECK_EQ(ScanClampedInts("garbage", w, 2), 0);
    CHECK_EQ(ScanClampedInts("7;8", w, 2), 1);   // the separator must be a comma
    CHECK_EQ(w[0], 7);
    CHECK_EQ(w[1], 6);
    CHECK_EQ(ScanClampedInts("-4, 9", w, 2), 2);   // %d skips the space after the comma
    CHECK_EQ(w[0], -4);
    CHECK_EQ(w[1], 9);
    CHECK_EQ(ScanClampedInts("1,99999999999999999999", w, 2, -kHarnessIntLimit, kHarnessIntLimit), 2);
    CHECK_EQ(w[1], kHarnessIntLimit);
    CHECK_EQ(ScanClampedInts("1,2,3", w, 2), 2);   // stops at count
}

// sscanf("%f,%f,%f,%f") as BANISHED_CAMERA_PIN used it.
UNIT_TEST(harness_parse_scan_floats) {
    float f[4] = {0.0f, 0.0f, 9.0f, 9.0f};
    CHECK_EQ(ScanClampedFloats("1.5,-2,sweep", f, 4), 2);
    CHECK_EQ(f[0], 1.5f);
    CHECK_EQ(f[1], -2.0f);
    CHECK_EQ(f[2], 9.0f);
    CHECK_EQ(ScanClampedFloats("town,40", f, 4), 0);
    CHECK_EQ(ScanClampedFloats("1e39,nan,-inf,4", f, 4), 4);
    CHECK_EQ(f[0], FLT_MAX);
    CHECK_EQ(f[1], 0.0f);
    CHECK_EQ(f[2], -FLT_MAX);
    CHECK_EQ(f[3], 4.0f);
    const char* p = "0.75,x";
    float g = 3.0f;
    CHECK(ScanClampedFloat(p, &g));
    CHECK_EQ(g, 0.75f);
    CHECK(ScanLiteral(p, ','));
    CHECK(!ScanClampedFloat(p, &g));
    CHECK_EQ(g, 0.75f);
}
