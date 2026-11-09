// src/render/vulkan/tweak_overlay/overlay.frag -- the debug tweak overlay's fragment shader (NOT RETAIL).
// OpenBA's own shader, not game content: the vertex colour times the texture (the font atlas).
// Regenerate overlay_spirv.h with tools/vulkan/gen_tweak_overlay_spirv.sh after an edit.
#version 450

layout(set = 0, binding = 0) uniform sampler2D tex;

layout(location = 0) in vec2 uv;
layout(location = 1) in vec4 color;
layout(location = 0) out vec4 outColor;

void main() {
    outColor = color * texture(tex, uv);
}
