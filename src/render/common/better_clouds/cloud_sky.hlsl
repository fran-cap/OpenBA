// ============================================================================
// src/render/common/better_clouds/cloud_sky.hlsl -- ext.better_clouds' sky pixel program
// ============================================================================
// NOT RETAIL. Our own shader (docs/extensions/handins/BETTERCLOUDS.md). It stands in for the sky
// material's pixel program in its "normal" pass (the full program) and its "reflect" pass (CLOUDS_CHEAP:
// no detail octave, no sun probe); the sky's vertex layout, vertex program and render state stay the
// user's installed ones.
//
// THE SKY UNDER IT. The installed sky draws a painted cube map along the dome's direction (TEXCOORD1:
// the dome position over 512, the dome's own z up) and along its z-mirror for the cloudy sky, blended by
// season and weather (t below, the same blend). We draw the same blend from a blurred mip level, which
// keeps the painting's colours and smears its painted (still) clouds into soft colour; our clouds then
// carry all the visible structure.
//
// THE CLOUDS (docs/extensions/handins/BETTERCLOUDS.md section 2): the direction projected onto a curved
// cloud layer, q = d.xy / (d.z + c) * scale (bounded at the horizon); three octaves of our own tileable
// noise (t1, made at load: R base shape, G mid, B billows, A with G the warp field), each rotated and at
// a non-integer frequency so their periods never line up, all moving down the one wind direction. The
// motion lives in the offsets (MaterialConstants), which the CPU accumulates at the game's speed and
// wraps to 0..1: the shader never sees a time value. Coverage, softness and darkness follow the same
// season / weather blend as the painted sky; light from the scene's sun colour, ambient colour and the
// sun's direction (one probe toward the sun on the layer).
//
// Compiled offline by tools/water_shaders.py (fxc, ps_4_0) into cloud_sky_ps.h and, with CLOUDS_CHEAP,
// cloud_sky_cheap_ps.h. Every backend takes the DXBC (DX11 directly, Vulkan through dxbc-spirv, Metal
// through airconv).
// ============================================================================

TextureCube  g_sky   : register(t0);   // the installed sky cube (the material binds it)
SamplerState s_sky   : register(s0);
Texture2D    g_noise : register(t1);   // ours: 256 x 256 RGBA8, tileable, full mip chain, wrap
SamplerState s_noise : register(s1);

cbuffer PixelConstants : register(b0) {
    float4 texelSize;
    float4 fogColor;
    float4 lightColor;      // the sun
    float4 ambientColor;
    float4 userPosition;    // x: winter, w: weather (the installed sky's blend)
};

// Filled for the sky's draw alone (src/ext/better_clouds/better_clouds.cpp, cloud_params.h).
cbuffer MaterialConstants : register(b1) {
    float4 mc0;   // xy: base octave offset, zw: mid octave offset (texture space, 0..1)
    float4 mc1;   // xy: detail octave offset, zw: warp field offset
    float4 mc2;   // xyz: toward the sun (dome space, z up, unit), w: the layer scale (noise tiles per unit)
    float4 mc3;   // x: the lit side's white, y: the shaded side (times x), z: sky lift, w: coverage bias
                  // (src/ext/better_clouds/cloud_params.h Look: the defaults, the debug overlay moves them)
};

struct PSIn {
    float4 position : SV_Position;
    float3 texcoord : TEXCOORD1;
};

// ---- the look ----------------------------------------------------------------------------------
static const float kCurve       = 0.20;   // c: how flat the layer reads (the horizon is 1/c the zenith scale)
static const float kWarp        = 0.12;   // the slow warp's reach, layer units
static const float kSunStep     = 0.02;  // the sun probe's step on the layer
static const float kBlurLod     = 5.0;    // the painted sky's mip level under the clouds
static const float kHorizonFade = 0.10;   // the clouds dissolve below this height (d.z)
static const float kHorizonFog  = 0.30;   // and take the fog colour below this one

// Per-octave rotation and frequency: src/ext/better_clouds/cloud_params.cpp builds the offsets with the
// same values.
static const float2x2 R0 = float2x2(1.000, 0.000, 0.000, 1.000);    //  0 deg
static const float2x2 R1 = float2x2(0.809, -0.588, 0.588, 0.809);   // 36 deg
static const float2x2 R2 = float2x2(0.276, -0.961, 0.961, 0.276);   // 74 deg
static const float F0 = 1.00;
static const float F1 = 2.13;
static const float F2 = 4.71;
static const float FW = 0.37;

float Hash12(float2 p) {
    float3 p3 = frac(float3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return frac((p3.x + p3.y) * p3.z);
}

float4 PSCloudSky(PSIn input) : SV_Target {
    const float3 dir = input.texcoord;
    const float3 d = normalize(dir);
    const float dz = max(d.z, 0.0);

    // The installed sky's season / weather blend: 0 a clear summer, 0.75 winter, 1 weather.
    const float t = lerp(userPosition.x * 0.75, 1.0, userPosition.w);

    // ---- the painted sky, blurred ----
    const float3 clearSky = g_sky.SampleLevel(s_sky, dir, kBlurLod).rgb;
    const float3 cloudySky = g_sky.SampleLevel(s_sky, float3(dir.xy, -dir.z), kBlurLod).rgb;
    const float3 sky = lerp(lerp(clearSky, cloudySky, t), 1.0, mc3.z);

    // ---- the layer ----
    const float2 q = d.xy / (dz + kCurve) * mc2.w;
    const float2 warp = (g_noise.Sample(s_noise, q * FW + mc1.zw).ga - 0.5) * kWarp;
    const float2 qw = q + warp;

    const float base = g_noise.Sample(s_noise, mul(R0, qw) * F0 + mc0.xy).r;
    const float mid = g_noise.Sample(s_noise, mul(R1, qw) * F1 + mc0.zw).g;
#ifndef CLOUDS_CHEAP
    float det = g_noise.Sample(s_noise, mul(R2, q) * F2 + mc1.xy).b;
    det = lerp(0.5, det, smoothstep(0.03, 0.25, dz));   // to its mean at grazing angles
#else
    const float det = 0.5;
#endif
    const float n = base * 0.68 + mid * 0.22 + det * 0.10;

    // ---- weather: coverage, edges, darkness ----
    const float coverage = saturate(lerp(0.50, 0.95, smoothstep(0.0, 1.0, t)) + mc3.w);
    const float softness = lerp(0.50, 0.35, t);
    const float darkness = lerp(0.05, 0.60, t);
    const float opacity = lerp(0.95, 1.0, t);
    float dens = saturate((n - (1.0 - coverage)) / max(coverage, 1e-3));
    dens = smoothstep(0.0, softness, dens);

    // ---- light ----
    // The sun lights the clouds' edges while it is up; it fades in over its first few degrees.
    const float sunGain = saturate(mc2.z * 6.0) * lerp(1.0, 0.35, t);
#ifndef CLOUDS_CHEAP
    const float sunH = length(mc2.xy);
    const float2 sunXY = sunH > 1e-4 ? mc2.xy / sunH : float2(0.0, 0.0);
    const float2 qs = qw + sunXY * kSunStep;
    const float baseS = g_noise.Sample(s_noise, mul(R0, qs) * F0 + mc0.xy).r;
    const float bright = saturate(0.70 + (base - baseS) * 6.0 * sunGain);
#else
    const float bright = 0.75;
#endif
    // White as the painted clouds are: the lit side near white with a touch of the sun's colour, the shaded
    // side a light grey with a little of the sky and the ambient in it; the weather greys both.
    const float3 white = mc3.xxx;
    const float3 litCol = lerp(white, lightColor.rgb, 0.15);
    const float3 shadowCol = lerp(white * mc3.y, lerp(sky, ambientColor.rgb, 0.5), 0.25) * (1.0 - 0.45 * darkness);
    float3 col = lerp(shadowCol, litCol, bright);
    col *= 1.0 - darkness * pow(dens, 1.5);

    // Thin edges glow when looking toward the sun (a soft forward-scatter lobe).
    const float mu = saturate(dot(d, mc2.xyz));
    col += lightColor.rgb * (pow(mu, 8.0) * 0.6 * sunGain) * dens * (1.0 - dens) * 2.0;

    // ---- the horizon ----
    col = lerp(fogColor.rgb, col, saturate(dz / kHorizonFog));
    const float alpha = dens * opacity * smoothstep(0.0, kHorizonFade, dz);
    float3 outCol = lerp(sky, col, alpha);

    // A triangular dither of one 8-bit step against banding in the soft gradients.
    const float2 sp = input.position.xy;
    outCol += (Hash12(sp) + Hash12(sp + 17.13) - 1.0) * (1.0 / 255.0);
    return float4(outCol, 1.0);
}
