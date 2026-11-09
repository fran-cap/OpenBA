// src/render/vulkan/tweak_overlay/overlay.vert -- the debug tweak overlay's vertex shader (NOT RETAIL).
// OpenBA's own shader, not game content: Dear ImGui's vertex (position in pixels, uv, packed RGBA colour)
// to clip space. Regenerate overlay_spirv.h with tools/vulkan/gen_tweak_overlay_spirv.sh after an edit.
#version 450

layout(push_constant) uniform Frame {
    vec2 scale;       // 2 / display size
    vec2 translate;   // -1 - display pos * scale
} pc;

layout(location = 0) in vec2 inPos;
layout(location = 1) in vec2 inUv;
layout(location = 2) in vec4 inColor;   // R8G8B8A8_UNORM

layout(location = 0) out vec2 uv;
layout(location = 1) out vec4 color;

void main() {
    uv = inUv;
    color = inColor;
    gl_Position = vec4(inPos * pc.scale + pc.translate, 0.0, 1.0);
}
