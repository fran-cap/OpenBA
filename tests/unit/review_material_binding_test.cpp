// tests/unit/review_material_binding_test.cpp
// review S-003: the bindings MaterialInstance::BuildParameters appends
// (src/engine/graphics/material_binding_bounds.h). BuildParameters and Setup need the
// video module and a live Context; they call these. All platforms.

#include "unit.h"
#include "graphics/material_binding_bounds.h"
#include <cstdint>

using MaterialBindingBounds::IsConstantBinding;
using MaterialBindingBounds::IsTextureBinding;

UNIT_TEST(review_material_binding_texture) {
    // Every slot of Context::_imageBuffers[16] with a found texture is bound.
    for (int32_t slot = 0; slot < 16; ++slot) {
        CHECK(IsTextureBinding(0, slot));
    }
    CHECK(IsTextureBinding(7, 15));
    // FindTexture's miss (-1): Setup would read _textures._data[-1].
    CHECK(!IsTextureBinding(-1, 0));
    CHECK(!IsTextureBinding(INT32_MIN, 0));
    // A shader resource slot outside _imageBuffers[16].
    CHECK(!IsTextureBinding(0, 16));
    CHECK(!IsTextureBinding(0, -1));
    CHECK(!IsTextureBinding(0, INT32_MAX));
}

UNIT_TEST(review_material_binding_constant) {
    for (int32_t slot = 0; slot < 4; ++slot) {
        CHECK(IsConstantBinding(0, slot));
    }
    CHECK(IsConstantBinding(3, 3));
    CHECK(!IsConstantBinding(-1, 0));
    // material[4][4]: slot 4 would land on pixel[0] (ctx +0x248), past the array.
    CHECK(!IsConstantBinding(0, 4));
    CHECK(!IsConstantBinding(0, -1));
    CHECK(!IsConstantBinding(0, INT32_MIN));
}
