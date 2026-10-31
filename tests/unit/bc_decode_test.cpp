// ============================================================================
// tests/unit/bc_decode_test.cpp -- src/render/common/bc_decode: BC1 / BC3 decoded to RGBA8
// ============================================================================
//   * BC1 four-colour mode: endpoints expand by bit replication, interpolants at 1/3 and 2/3;
//   * BC1 three-colour mode (c0 <= c1): the midpoint and transparent black;
//   * BC3: the colour block is read four-colour even when c0 <= c1, the alpha is the BC4 block;
//   * a 5 x 3 level: the sizes are the D3D ones, the edge block writes nothing past the level.
// ============================================================================
#include "unit.h"

#include <cstdint>
#include <vector>

#include "bc_decode/bc_decode.h"

namespace {

// RGB565 of 8-bit channels (the low bits dropped).
uint16_t Pack565(int r, int g, int b) {
    return static_cast<uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

void PutColour(uint8_t* block, uint16_t c0, uint16_t c1, uint32_t indices) {
    block[0] = static_cast<uint8_t>(c0);
    block[1] = static_cast<uint8_t>(c0 >> 8);
    block[2] = static_cast<uint8_t>(c1);
    block[3] = static_cast<uint8_t>(c1 >> 8);
    for (int b = 0; b < 4; ++b)
        block[4 + b] = static_cast<uint8_t>(indices >> (8 * b));
}

}  // namespace

UNIT_TEST(bc_decode_bc1_four_colour) {
    uint8_t block[8];
    // c0 white, c1 black: c0 > c1. Pixel i takes index i & 3.
    PutColour(block, Pack565(255, 255, 255), Pack565(0, 0, 0), 0xE4E4E4E4u);
    uint8_t out[64];
    BcDecode::DecodeColourBlock(block, false, out);
    const int expect[4] = { 255, 0, 170, 85 };
    for (int i = 0; i < 16; ++i) {
        CHECK_EQ(static_cast<int>(out[i * 4 + 0]), expect[i & 3]);
        CHECK_EQ(static_cast<int>(out[i * 4 + 1]), expect[i & 3]);
        CHECK_EQ(static_cast<int>(out[i * 4 + 2]), expect[i & 3]);
        CHECK_EQ(static_cast<int>(out[i * 4 + 3]), 255);
    }
}

UNIT_TEST(bc_decode_bc1_three_colour_and_transparent) {
    uint8_t block[8];
    // c0 black, c1 pure red (0xF800): c0 <= c1.
    PutColour(block, 0x0000, 0xF800, 0xE4E4E4E4u);
    uint8_t out[64];
    BcDecode::DecodeColourBlock(block, false, out);
    CHECK_EQ(static_cast<int>(out[1 * 4 + 0]), 255);   // index 1: c1
    CHECK_EQ(static_cast<int>(out[2 * 4 + 0]), 128);   // index 2: the midpoint
    CHECK_EQ(static_cast<int>(out[2 * 4 + 3]), 255);
    CHECK_EQ(static_cast<int>(out[3 * 4 + 0]), 0);     // index 3: transparent black
    CHECK_EQ(static_cast<int>(out[3 * 4 + 3]), 0);
}

UNIT_TEST(bc_decode_bc3_colour_is_four_colour) {
    uint8_t block[16] = {};
    block[0] = 200;   // alpha e0
    block[1] = 200;   // alpha e1: every alpha index gives 200
    PutColour(block + 8, 0x0000, 0xF800, 0xFFFFFFFFu);   // c0 <= c1, every pixel index 3
    uint8_t out[64];
    BcDecode::DecodeBC3Block(block, out);
    for (int i = 0; i < 16; ++i) {
        CHECK_EQ(static_cast<int>(out[i * 4 + 0]), 170);   // (c0 + 2 c1) / 3, not transparent black
        CHECK_EQ(static_cast<int>(out[i * 4 + 3]), 200);
    }
}

UNIT_TEST(bc_decode_edge_level) {
    CHECK_EQ(BcDecode::Bc1Size(5, 3), static_cast<size_t>(16));
    CHECK_EQ(BcDecode::Bc3Size(5, 3), static_cast<size_t>(32));
    CHECK_EQ(BcDecode::Bc1Size(1, 1), static_cast<size_t>(8));
    std::vector<uint8_t> src(BcDecode::Bc1Size(5, 3));
    PutColour(src.data(), Pack565(255, 255, 255), Pack565(255, 255, 255), 0);
    PutColour(src.data() + 8, Pack565(255, 255, 255), Pack565(255, 255, 255), 0);
    const int pitch = 5 * 4 + 4;   // one guard pixel per row
    std::vector<uint8_t> dst(static_cast<size_t>(pitch) * 3 + 16, 7);
    BcDecode::DecodeBC1(src.data(), 5, 3, dst.data(), pitch);
    for (int y = 0; y < 3; ++y) {
        for (int x = 0; x < 5; ++x)
            CHECK_EQ(static_cast<int>(dst[static_cast<size_t>(y) * pitch + x * 4]), 255);
        CHECK_EQ(static_cast<int>(dst[static_cast<size_t>(y) * pitch + 20]), 7);   // the guard
    }
    CHECK_EQ(static_cast<int>(dst[static_cast<size_t>(pitch) * 3]), 7);   // nothing past the last row
}
