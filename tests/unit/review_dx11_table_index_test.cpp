// ============================================================================
// tests/unit/review_dx11_table_index_test.cpp -- the serialized RenderState and VertexLayout
// enum fields the DX11 backend indexes its tables with (review 2026-11-03, S-042 / S-043).
//
// `Video::RenderState::Serialize` and the VertexLayout element reader read the blend, compare,
// stencil, cull and fill fields and each element's format with no range check, so a .rsc in a
// mod .pkm hands any uint32 to:
//   * RenderStateData::Create's kBlendFactor[10] / kBlendOp[3] / kCompareFunc[8] /
//     kStencilOp[8] / kCullMode[3] / kFillMode[2] (S-042, retail 0x1800037c0..);
//   * VertexLayoutData::PlatformCreate's kElementFormat[9] (S-043, retail 0x180003ff0).
// The DX11 bodies link only against D3D11, so the index and predicate they now go through
// (RenderState.h, VertexLayout.h) are what is tested here.
// ============================================================================
#include "unit.h"

#include "video/RenderState.h"
#include "video/VertexLayout.h"

#include <cstdint>

namespace {

using Video::RenderState;
using Video::VertexLayout;

// The out-of-range values a hostile file carries: one past each table, the top bit, all ones.
const uint32_t kHostile[] = { 2u, 3u, 8u, 9u, 10u, 0x80000000u, 0xffffffffu };

}  // namespace

UNIT_TEST(review_renderstate_table_index_stays_in_its_table) {
    const uint32_t counts[] = {
        RenderState::kBlendFactorCount, RenderState::kBlendOpCount, RenderState::kCompareFuncCount,
        RenderState::kStencilOpCount,   RenderState::kCullModeCount, RenderState::kFillModeCount,
    };
    for (const uint32_t count : counts) {
        // Every retail value indexes itself: the faithful descriptors are unchanged.
        for (uint32_t v = 0; v < count; ++v)
            CHECK_EQ(RenderState::TableIndex(v, count, 0), v);
        for (const uint32_t v : kHostile) {
            const uint32_t index = RenderState::TableIndex(v, count, 0);
            CHECK(index < count);
            if (v >= count)
                CHECK_EQ(index, 0u);
        }
    }
    // Out of range, a field takes its constructor default (RenderState.h): the cull mode's 1.
    CHECK_EQ(RenderState::TableIndex(3, RenderState::kCullModeCount, 1), 1u);
    CHECK_EQ(RenderState::TableIndex(2, RenderState::kFillModeCount, 0), 0u);
    CHECK_EQ(RenderState::TableIndex(0xffffffffu, RenderState::kBlendFactorCount, 1), 1u);
}

UNIT_TEST(review_vertexlayout_format_in_range) {
    for (int32_t f = 0; f < 9; ++f)
        CHECK(VertexLayout::FormatInRange(f));
    CHECK(!VertexLayout::FormatInRange(9));
    CHECK(!VertexLayout::FormatInRange(-1));
    CHECK(!VertexLayout::FormatInRange(INT32_MIN));
    CHECK(!VertexLayout::FormatInRange(INT32_MAX));
}
