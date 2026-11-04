// tests/unit/depth_bias_test.cpp -- src/render/common/depth_bias.h: the RenderState depth bias in D24
// rasterizer units, as retail's MULSS + CVTTSS2SI (VideoDX11 0x180003827-0x18000384b) gives it.
#include "unit.h"

#include <cstdint>
#include <limits>

#include "../../src/render/common/depth_bias.h"

namespace {
// volatile: the conversion runs at run time, as it does on a RenderState read from a resource.
int32_t Units(float bias) {
    volatile float v = bias;
    return Render::DepthBiasUnits(v);
}
}  // namespace

UNIT_TEST(depth_bias_in_range_truncates_toward_zero) {
    CHECK_EQ(Units(0.0f), 0);
    CHECK_EQ(Units(1.0f / 16777215.0f), 1);
    CHECK_EQ(Units(-1.0f / 16777215.0f), -1);
    CHECK_EQ(Units(0.0001f), int32_t(0.0001f * 16777215.0f));
    CHECK_EQ(Units(-0.0001f), int32_t(-0.0001f * 16777215.0f));
    CHECK_EQ(Units(100.0f), int32_t(100.0f * 16777215.0f));
    CHECK_EQ(Units(128.0f), 2147483520);   // 2^31 - 128: the largest product below 2^31
    CHECK_EQ(Units(-128.0f), -2147483520);
}

// A bias whose product leaves the int range (|bias| >= about 128), NaN or an infinity: CVTTSS2SI's
// integer-indefinite value, INT_MIN, as on retail. A C++ cast is undefined there ([conv.fpint]).
UNIT_TEST(depth_bias_out_of_range_is_cvttss2si_indefinite) {
    const int32_t indefinite = std::numeric_limits<int32_t>::min();
    CHECK_EQ(Units(1000.0f), indefinite);
    CHECK_EQ(Units(-1000.0f), indefinite);
    CHECK_EQ(Units(129.0f), indefinite);
    CHECK_EQ(Units(std::numeric_limits<float>::quiet_NaN()), indefinite);
    CHECK_EQ(Units(std::numeric_limits<float>::infinity()), indefinite);
    CHECK_EQ(Units(-std::numeric_limits<float>::infinity()), indefinite);
}
