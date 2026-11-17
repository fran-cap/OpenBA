// ============================================================================
// src/render/common/map_edge/map_edge.hlsl -- ext.map_edge_fade: the map's rim fades to black
// ============================================================================
// NOT RETAIL. Our own shader (docs/extensions/handins/MAPEDGE.md). A full-screen pass over the finished main
// scene (the scene-draw seam, before the tool and the UI): every pixel's world position from the scene depth,
// its distance past a square a little inside the map's edge, and the colour multiplied toward black over that
// distance (blend ZERO / SRC_COLOR). A pixel with nothing drawn (depth 1) takes the point where its view ray
// meets the ground plane, so what lies past the map is black; a ray that goes up keeps the sky.
//
// The full-screen vertex program is AO's (src/render/dx11/ao/ao_depth_resolve.hlsl VSFullscreen: the clip
// position alone). Compiled offline by tools/water_shaders.py (fxc, ps_4_0) into map_edge_ps.h. The same
// arithmetic on the CPU, unit-tested: src/ext/map_edge_fade/edge_math.h.
// ============================================================================

Texture2D<float> g_depth : register(t0);   // the scene's hardware depth, 1x (0 near .. 1 far)

cbuffer EdgeConstants : register(b0) {
    row_major float4x4 invViewProj;   // clip -> world (the engine's row-vector convention: v * M)
    float4 map;      // x: half the map's side less the inset (world: where the fade starts),
                     // y: 1 / the fade's width (world), z: the horizon band (see below), w: unused
    float4 screen;   // xy: 1 / the target size
    float4 camera;   // xyz: the camera's world position
};

struct PSIn {
    float4 position : SV_Position;
};

float4 PSMapEdge(PSIn input) : SV_Target {
    const float depth = g_depth.Load(int3(input.position.xy, 0));
    const float2 uv = input.position.xy * screen.xy;
    const float4 clip = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, depth, 1.0);
    const float4 h = mul(clip, invViewProj);
    const float3 world = h.xyz / h.w;

    float2 p = world.xy;
    float keep = 0.0;   // 1: the sky above the horizon, left as it is
    if (depth >= 0.99999) {
        // Nothing drawn here: the view ray down to the ground plane (z = 0).
        const float3 dir = normalize(world - camera.xyz);
        if (dir.z >= -1e-4) return float4(1.0, 1.0, 1.0, 1.0);
        p = camera.xy + dir.xy * (camera.z / -dir.z);
        // Just under the horizon the black gives way to the sky over a narrow band.
        keep = saturate(1.0 + dir.z * map.z);
    }

    // The distance past the inner square, round at the corners.
    const float2 q = max(abs(p) - map.xx, 0.0);
    float t = saturate(length(q) * map.y);
    t = t * t * (3.0 - 2.0 * t);
    const float f = lerp(1.0 - t, 1.0, keep);
    return float4(f, f, f, 1.0);
}
