// ============================================================================
// src/render/common/better_clouds/cloud_shadow.hlsl -- ext.better_clouds Fancy: the clouds' shadows
// ============================================================================
// NOT RETAIL. Our own shader (docs/extensions/handins/BETTERCLOUDS.md section 2.6). A full-screen pass
// between the opaque and the blended lists of the main scene (the ground-draw seam): every opaque pixel's
// world position from the scene depth, followed up toward the sun to a flat cloud layer, the same noise
// octaves the sky draws (cloud_sky.hlsl) moving with the same offsets, and the result multiplied into the
// colour (blend ZERO / SRC_COLOR). The sky (depth 1) is left alone; the water and everything blended draw
// after it, over it.
//
// The full-screen vertex program is AO's (src/render/common/ao/ao_depth_resolve.hlsl VSFullscreen: the
// clip position alone). Compiled offline by tools/water_shaders.py (fxc, ps_4_0) into cloud_shadow_ps.h.
// ============================================================================

Texture2D<float> g_depth : register(t0);   // the scene's hardware depth, 1x (0 near .. 1 far)
Texture2D    g_noise     : register(t1);   // the sky's noise (better_clouds.cpp): wrap, mipmapped
SamplerState s_noise     : register(s1);

cbuffer ShadowConstants : register(b0) {
    row_major float4x4 invViewProj;   // clip -> world (the engine's row-vector convention: v * M)
    float4 off01;    // xy: base octave offset, zw: mid octave offset (the sky's mc0)
    float4 offW;     // zw: warp offset (the sky's mc1)
    float4 sun;      // xyz: toward the sun (world, z up, unit), w: the cloud layer's height (world units)
    float4 look;     // x: noise tiles per world unit, y: coverage, z: strength (0..1), w: edge softness
    float4 screen;   // xy: 1 / the target size, z: fade from, w: fade to (world distance from the camera)
    float4 camera;   // xyz: the camera's world position
};

struct PSIn {
    float4 position : SV_Position;
};

static const float kWarp = 0.12;
static const float2x2 R0 = float2x2(1.000, 0.000, 0.000, 1.000);
static const float2x2 R1 = float2x2(0.809, -0.588, 0.588, 0.809);
static const float F0 = 1.00;
static const float F1 = 2.13;
static const float FW = 0.37;

float4 PSCloudShadow(PSIn input) : SV_Target {
    const float depth = g_depth.Load(int3(input.position.xy, 0));
    if (depth >= 0.99999) return float4(1.0, 1.0, 1.0, 1.0);   // the sky

    const float2 uv = input.position.xy * screen.xy;
    const float4 clip = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, depth, 1.0);
    const float4 h = mul(clip, invViewProj);
    const float3 world = h.xyz / h.w;

    // Up toward the sun to the layer: a low sun stretches the shadows away from it.
    const float rise = max(sun.w - world.z, 0.0);
    const float2 onLayer = world.xy + sun.xy / max(sun.z, 0.25) * rise;

    const float2 q = onLayer * look.x;
    const float2 warp = (g_noise.Sample(s_noise, q * FW + offW.zw).ga - 0.5) * kWarp;
    const float2 qw = q + warp;
    const float base = g_noise.Sample(s_noise, mul(R0, qw) * F0 + off01.xy).r;
    const float mid = g_noise.Sample(s_noise, mul(R1, qw) * F1 + off01.zw).g;
    const float n = base * 0.75 + mid * 0.25;

    float dens = saturate((n - (1.0 - look.y)) / max(look.y, 1e-3));
    dens = smoothstep(0.0, look.w, dens);

    // Gone into the fog, as the AO pass is (the materials fogged the colour already).
    const float dist = distance(world, camera.xyz);
    const float fade = 1.0 - smoothstep(screen.z, screen.w, dist);

    const float f = 1.0 - look.z * dens * fade;
    return float4(f, f, f, 1.0);
}
