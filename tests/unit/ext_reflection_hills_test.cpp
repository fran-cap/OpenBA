// ============================================================================
// tests/unit/ext_reflection_hills_test.cpp -- fix.reflection_hills' rule
// ============================================================================
// src/ext/reflection_hills/reflection_hills.h without the world: only the reflection's group gets the
// tall chunk box, and only with the row on; everything else keeps retail's value to the last bit.
// ============================================================================
#include "unit.h"
#include "ext/reflection_hills/reflection_hills.h"

#include <cstring>
#include <initializer_list>

using Ext::ReflectionHills::ChunkZMax;
using Ext::ReflectionHills::kReflectionShader;
using Ext::ReflectionHills::kTallZMax;

UNIT_TEST(ext_reflection_hills_only_the_reflection_group_with_the_row_on) {
    const float retail = -16.0f + 32.0f;   // geometry_draw.cpp kChunkZMin + kChunkZRange
    // row off: retail's bits for every group
    for (int shader = 0; shader <= 3; ++shader) {
        const float z = ChunkZMax(false, shader, retail);
        CHECK(std::memcmp(&z, &retail, sizeof z) == 0);
    }
    // row on: the main view (0), the shadow map (1) and a spot light (3) keep retail's
    for (int shader : {0, 1, 3}) {
        const float z = ChunkZMax(true, shader, retail);
        CHECK(std::memcmp(&z, &retail, sizeof z) == 0);
    }
    // the reflection's group: the tall box
    CHECK(kReflectionShader == 2);
    CHECK(ChunkZMax(true, kReflectionShader, retail) == kTallZMax);
}

UNIT_TEST(ext_reflection_hills_tall_box_covers_every_height) {
    // The heightmap is int16 words of 1 / 256 world unit: nothing is higher than 32767 / 256.
    CHECK(kTallZMax >= 32767.0f / 256.0f);
    // A box already taller is never lowered.
    CHECK(ChunkZMax(true, kReflectionShader, 500.0f) == 500.0f);
}
