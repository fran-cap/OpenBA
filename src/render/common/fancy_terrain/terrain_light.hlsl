// ============================================================================
// src/render/common/fancy_terrain/terrain_light.hlsl -- ext.fancy_terrain: the terrain's own light
// ============================================================================
// NOT RETAIL. Our own shader (src/ext/fancy_terrain/horizon.h has the method). A full-screen pass between
// the opaque and the blended lists of the main scene (the ground-draw seam): every opaque pixel's world
// position from the scene depth, the horizon map and the ground height at its (x, y), and the light found
// there multiplied into the colour (blend ZERO / SRC_COLOR, RGB only). The light is split the way retail
// lights the ground: an ambient part (`map.w` of it) and the sun's, N.L of the ground's normal (from the
// height texture). The factor is
//     (ambient * sky + sun * N.L * lit) / (ambient + sun * N.L)
// so each loss only takes its own part:
//   lit     the sun below the horizon in its own direction (the two nearest of the eight directions
//           interpolated, the horizon's absolute angle), softly around the angle. Near the camera the retail
//           shadow map already shadows the terrain, so only `camera.w` of it applies there, rising to the
//           full strength by `tint.w`. A face turned from the sun (N.L small) loses little: retail already
//           darkened it.
//   sky     each horizon taken against the ground's own slope in that direction (a plain incline sees the
//           whole sky; only a hollow, a valley floor or the foot of a cliff loses some), the mean of cos^2.
//           A face in full sun loses little of it: most of its light is the sun's.
//   bounce  part of the darkening given back in the ground's colour (`tint`), so a shaded slope is warm,
//           not black.
// A point well above the ground (a roof, a tree top) gets less of it (`look.w`); everything fades into the
// fog as the AO pass does. The sky (depth 1) is left alone.
//
// The full-screen vertex program is AO's (src/render/common/ao/ao_depth_resolve.hlsl VSFullscreen). Compiled
// offline by tools/water_shaders.py (fxc, ps_4_0) into terrain_light_ps.h.
// ============================================================================

Texture2D<float> g_depth    : register(t0);   // the scene's hardware depth, 1x (0 near .. 1 far)
Texture2D        g_horizon0 : register(t1);   // directions 0..3 (RGBA8: the signed angle, (a / (pi / 2) + 1) / 2)
Texture2D        g_horizon1 : register(t2);   // directions 4..7
Texture2D<float> g_ground   : register(t3);   // the ground height (world units)
SamplerState     s_map      : register(s1);   // linear, clamp

cbuffer LightConstants : register(b0) {
    row_major float4x4 invViewProj;   // clip -> world (the engine's row-vector convention: v * M)
    float4 sun;      // xyz: toward the sun (world, z up, unit), w: the sun shadow's strength far away
    float4 look;     // x: sky strength, y: bounce, z: sun edge softness (radians), w: height fade (world)
    float4 screen;   // xy: 1 / the target size, z: fade from, w: fade to (world distance from the camera)
    float4 camera;   // xyz: the camera's world position, w: the sun shadow's share near the camera
    float4 map;      // x: half the grid, y: 1 / width, z: 1 / height (vertices), w: the ambient share
    float4 tint;     // rgb: the bounce colour, w: the distance where the sun shadow is at full strength
};

struct PSIn {
    float4 position : SV_Position;
};

static const float kHalfPi = 1.5707963;
static const float kStep = 0.7853982;   // 2 pi / 8

float4 PSTerrainLight(PSIn input) : SV_Target {
    const float depth = g_depth.Load(int3(input.position.xy, 0));
    if (depth >= 0.99999) return float4(1.0, 1.0, 1.0, 1.0);   // the sky

    const float2 uv = input.position.xy * screen.xy;
    const float4 clip = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, depth, 1.0);
    const float4 hw = mul(clip, invViewProj);
    const float3 world = hw.xyz / hw.w;

    // Vertex c sits at world c - half: its texel centre is (c + 0.5) / width.
    const float2 tc = (world.xy + map.x + 0.5) * map.yz;
    if (any(tc < 0.0) || any(tc > 1.0)) return float4(1.0, 1.0, 1.0, 1.0);   // off the map
    const float4 h0 = (g_horizon0.SampleLevel(s_map, tc, 0) * 2.0 - 1.0) * kHalfPi;
    const float4 h1 = (g_horizon1.SampleLevel(s_map, tc, 0) * 2.0 - 1.0) * kHalfPi;
    const float ground = g_ground.SampleLevel(s_map, tc, 0);
    float hz[8] = { h0.x, h0.y, h0.z, h0.w, h1.x, h1.y, h1.z, h1.w };

    // The ground's slope: one vertex each way (one world unit between vertices).
    const float gx = (g_ground.SampleLevel(s_map, tc + float2(map.y, 0.0), 0) -
                      g_ground.SampleLevel(s_map, tc - float2(map.y, 0.0), 0)) * 0.5;
    const float gy = (g_ground.SampleLevel(s_map, tc + float2(0.0, map.z), 0) -
                      g_ground.SampleLevel(s_map, tc - float2(0.0, map.z), 0)) * 0.5;
    const float3 normal = normalize(float3(-gx, -gy, 1.0));

    // Sky visibility against the slope (horizon.h SkyVisibility is the same on the CPU).
    float vis = 0.0;
    [unroll] for (int k = 0; k < 8; ++k) {
        float sk, ck;
        sincos((float)k * kStep, sk, ck);
        const float rel = max(hz[k] - atan(gx * ck + gy * sk), 0.0);
        const float c = cos(rel);
        vis += c * c;
    }
    vis *= 0.125;

    // The sun against the horizon in its direction.
    float az = atan2(sun.y, sun.x);
    if (az < 0.0) az += 6.2831853;
    // The two nearest directions, weighted by closeness (a tent over the eight; no dynamic index).
    const float t = az / kStep;
    float horizon = 0.0;
    [unroll] for (int d = 0; d < 8; ++d) {
        float gap = abs(t - (float)d);
        gap = min(gap, 8.0 - gap);
        horizon += hz[d] * saturate(1.0 - gap);
    }
    const float elevation = asin(clamp(sun.z, -1.0, 1.0));
    const float soft = max(look.z, 1e-3);
    const float lit = smoothstep(horizon - soft, horizon + soft, elevation);

    const float dist = distance(world, camera.xyz);
    const float sunShare = sun.w * lerp(camera.w, 1.0, smoothstep(tint.w * 0.5, tint.w, dist));
    const float litF = 1.0 - sunShare * (1.0 - lit);
    const float skyF = 1.0 - look.x * (1.0 - vis);
    const float ambient = map.w;
    const float direct = (1.0 - ambient) * saturate(dot(normal, sun.xyz));
    float f = (ambient * skyF + direct * litF) / max(ambient + direct, 1e-3);

    // Less above the ground, gone into the fog.
    const float above = max(world.z - ground, 0.0);
    const float keep = (1.0 - saturate(above / max(look.w, 1e-3))) * (1.0 - smoothstep(screen.z, screen.w, dist));
    f = lerp(1.0, f, keep);

    // The bounce gives part of the darkening back in the ground's colour.
    const float3 rgb = f + (1.0 - f) * look.y * tint.rgb;
    return float4(saturate(rgb), 1.0);
}
