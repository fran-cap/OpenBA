#pragma once
// src/engine/graphics/material_binding_bounds.h
// The test MaterialInstance::BuildParameters (0x1801033e0) applies to a binding before it
// appends it. MaterialInstance::Setup (0x180102c90) then indexes the instance's texture /
// constant arrays with `sourceIndex` and the Context's slot arrays with `shaderSlot`, on
// every draw, with no test. Pure, so the unit tests reach it
// (tests/unit/review_material_binding_test.cpp).

#include <cstdint>

namespace MaterialBindingBounds {

// Video::Context::_imageBuffers[16] (ctx +0x30): the texture arm's slot array.
constexpr int32_t kTextureSlots = 16;
// Video::Context::PixelConstantStaging::material[4] (ctx +0x208): the constant arm's rows.
constexpr int32_t kConstantSlots = 4;

// NOT RETAIL (review S-003): retail appends every binding. FindTexture (0x180103020)
// answers -1 for a name the material lacks, and `dest` (the shader resource's slot) is
// never range-checked, so a mod material missing a texture the shader declares, or a
// shader resource with an out-of-range slot, makes Setup read before _textures and write
// past _imageBuffers on every draw.
constexpr bool IsTextureBinding(int32_t sourceIndex, int32_t shaderSlot) {
    return sourceIndex >= 0 && shaderSlot >= 0 && shaderSlot < kTextureSlots;
}

// NOT RETAIL (review S-003): as above for the constants arm (FindConstant 0x180102f40,
// Setup's flat `material[0][0] + shaderSlot * 4` store).
constexpr bool IsConstantBinding(int32_t sourceIndex, int32_t shaderSlot) {
    return sourceIndex >= 0 && shaderSlot >= 0 && shaderSlot < kConstantSlots;
}

}  // namespace MaterialBindingBounds
