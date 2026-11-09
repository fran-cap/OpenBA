// ============================================================================
// src/render/common/dof/dof.hlsl -- ext.depth_of_field: the depth blur's four pixel programs
// ============================================================================
// NOT RETAIL. Our own shader (docs/extensions/DOF.md section 3.1, the live tier). A half-resolution
// scatter-as-gather depth of field in the family of Unity's Post Processing v2 / KinoBokeh, pixel
// programs only, so the one DXBC runs on DX11 and, through dxbc-spirv, on Vulkan:
//
//   PSDofPrefilter  full -> half   each half pixel: its 2x2 full pixels' colour averaged, their signed
//                                  circle of confusion (CoC) the most extreme of the four, in alpha
//   PSDofGather     half -> half   a 43-tap ring disk (rings of 1, 7, 14, 21) of the prefiltered image,
//                                  the background and the foreground gathered apart: a tap counts for the
//                                  background when the smaller of its and the centre's CoC reaches the
//                                  centre, for the foreground when its own CoC does (the blurred near
//                                  field spreads over the sharp pixels behind it). Bright taps weigh more
//                                  (the LDR highlight boost: bokeh from a lit window at dusk). Alpha: the
//                                  foreground's coverage.
//   PSDofTent       half -> half   four bilinear taps, a 3x3 tent over the sparse kernel
//   PSDofComposite  -> main        the blurred image blended over the bound main colour, alpha the larger
//                                  of the foreground's coverage and the pixel's own far CoC (blend
//                                  SRC_ALPHA / INV_SRC_ALPHA, RGB only: the main alpha holds the shore
//                                  depth the water reads)
//
// THE CoC (dof_backend.h FocusMath has the C++ twin, unit-tested): each pixel's distance from the camera
// d (the world position from the scene depth and the inverse view-projection, the sky at a large
// distance), the focus distance f (the camera's distance to its target, or the depth at the centre of the
// screen when the frame passes 0),
//     x = (d - f) / d,  then the in-focus band taken off: sign(x) * max(|x| - band, 0),
//     CoC = clamp(x * gain, -1, 1) * maxCoC          (half-resolution pixels; negative = near)
// (d - f) / d is the thin lens's blur at a fixed focal length, up to the aperture: the far field tends to
// the full blur, the near field grows without bound and is clamped. Stored in an 8-bit alpha as
// CoC / (2 maxCoC) + 0.5 (the intermediate targets are BGRA8 on every backend).
//
// The full-screen vertex program is AO's (src/render/common/ao/shaders/VSFullscreen.h, SV_Position only).
// Compiled offline by tools/water_shaders.py (fxc, ps_4_0) into dof_*_ps.h.
// ============================================================================

Texture2D        g_source : register(t0);   // prefilter: the main colour (1x); others: the previous pass
Texture2D<float> g_depth  : register(t1);   // prefilter, composite: the scene's hardware depth (1x, 0 near .. 1 far)
SamplerState     s_linear : register(s0);   // linear, clamp

cbuffer DofConstants : register(b0) {
    row_major float4x4 invViewProj;   // clip -> world (the engine's row-vector convention: v * M)
    float4 screen;    // xy: 1 / the full size, zw: the full size (pixels)
    float4 halfRes;   // xy: 1 / the half size, zw: the half size
    float4 lens;      // x: focus distance (world; <= 0: the centre's depth), y: gain, z: max CoC (half px), w: band
    float4 camera;    // xyz: the camera's world position, w: the highlight boost
    float4 look;      // x: the highlight threshold (luma), y > 0.5: the debug view of the blur map
};

struct PSIn {
    float4 position : SV_Position;
};

static const float kSkyDistance = 100000.0;
static const float kMargin = 1.5;   // half pixels: the soft edge of a tap's reach

// The pixel's distance from the camera, from its hardware depth at full-resolution pixel `p`.
float DistanceAt(int2 p) {
    const float depth = g_depth.Load(int3(p, 0));
    if (depth >= 0.99999) return kSkyDistance;
    const float2 uv = (float2(p) + 0.5) * screen.xy;
    const float4 clip = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, depth, 1.0);
    const float4 hw = mul(clip, invViewProj);
    return length(hw.xyz / hw.w - camera.xyz);
}

float FocusDistance() {
    if (lens.x > 0.0) return lens.x;
    return DistanceAt(int2(screen.zw * 0.5));
}

// The signed CoC in half-resolution pixels (negative: nearer than the focus).
float CocOf(float distance, float focus) {
    float x = (distance - focus) / max(distance, 1e-3);
    x = sign(x) * max(abs(x) - lens.w, 0.0);
    return clamp(x * lens.y, -1.0, 1.0) * lens.z;
}

float EncodeCoc(float coc) { return saturate(coc / (2.0 * lens.z) + 0.5); }
float DecodeCoc(float a) { return (a - 0.5) * 2.0 * lens.z; }

float Luma(float3 c) { return dot(c, float3(0.299, 0.587, 0.114)); }

// ---------------------------------------------------------------------------------------------------------
float4 PSDofPrefilter(PSIn input) : SV_Target {
    const float focus = FocusDistance();
    const int2 base = int2(input.position.xy) * 2;
    const int2 last = int2(screen.zw) - 1;
    float3 colour = 0.0;
    float cocMin = 1e6;
    float cocMax = -1e6;
    [unroll] for (int i = 0; i < 4; ++i) {
        const int2 p = min(base + int2(i & 1, i >> 1), last);
        colour += g_source.Load(int3(p, 0)).rgb;
        const float coc = CocOf(DistanceAt(p), focus);
        cocMin = min(cocMin, coc);
        cocMax = max(cocMax, coc);
    }
    const float coc = -cocMin > cocMax ? cocMin : cocMax;
    return float4(colour * 0.25, EncodeCoc(coc));
}

// ---------------------------------------------------------------------------------------------------------
static const int kTaps = 43;
static const float2 kDisk[kTaps] = {
    float2(0.000000, 0.000000), float2(0.300323, 0.144628), float2(0.074174, 0.324976),
    float2(-0.207830, 0.260610), float2(-0.333333, 0.000000), float2(-0.207830, -0.260610),
    float2(0.074174, -0.324976), float2(0.300323, -0.144628), float2(0.666667, 0.000000),
    float2(0.600646, 0.289256), float2(0.415660, 0.521221), float2(0.148347, 0.649952),
    float2(-0.148347, 0.649952), float2(-0.415660, 0.521221), float2(-0.600646, 0.289256),
    float2(-0.666667, 0.000000), float2(-0.600646, -0.289256), float2(-0.415660, -0.521221),
    float2(-0.148347, -0.649952), float2(0.148347, -0.649952), float2(0.415660, -0.521221),
    float2(0.600646, -0.289256), float2(0.988831, 0.149042), float2(0.900969, 0.433884),
    float2(0.733052, 0.680173), float2(0.500000, 0.866025), float2(0.222521, 0.974928),
    float2(-0.074730, 0.997204), float2(-0.365341, 0.930874), float2(-0.623490, 0.781831),
    float2(-0.826239, 0.563320), float2(-0.955573, 0.294755), float2(-1.000000, 0.000000),
    float2(-0.955573, -0.294755), float2(-0.826239, -0.563320), float2(-0.623490, -0.781831),
    float2(-0.365341, -0.930874), float2(-0.074730, -0.997204), float2(0.222521, -0.974928),
    float2(0.500000, -0.866025), float2(0.733052, -0.680173), float2(0.900969, -0.433884),
    float2(0.988831, -0.149042),
};

float4 PSDofGather(PSIn input) : SV_Target {
    const float2 uv = input.position.xy * halfRes.xy;
    const float4 centre = g_source.SampleLevel(s_linear, uv, 0);
    const float centreCoc = DecodeCoc(centre.a);
    float4 background = 0.0;   // rgb: weighted colour, a: the weights
    float4 foreground = 0.0;
    float coverage = 0.0;      // the foreground taps, each counted once (the boost weighs colour only)
    [loop] for (int i = 0; i < kTaps; ++i) {
        const float2 offset = kDisk[i] * lens.z;
        const float reach = length(offset);
        const float4 tap = g_source.SampleLevel(s_linear, uv + offset * halfRes.xy, 0);
        const float tapCoc = DecodeCoc(tap.a);
        const float bright = 1.0 + camera.w * saturate((Luma(tap.rgb) - look.x) / max(1.0 - look.x, 1e-3));
        const float bgCoc = max(min(centreCoc, tapCoc), 0.0);
        const float bgWeight = saturate((bgCoc - reach + kMargin) / kMargin) * bright;
        float fgWeight = saturate((-tapCoc - reach + kMargin) / kMargin);
        fgWeight *= step(1.0, -tapCoc);   // a near tap with a CoC under a pixel is in focus
        background += float4(tap.rgb * bgWeight, bgWeight);
        foreground += float4(tap.rgb * (fgWeight * bright), fgWeight * bright);
        coverage += fgWeight;
    }
    // The centre tap always reaches itself (reach 0), so the background has weight unless the centre is
    // in the near field; then the foreground carries the pixel.
    const float3 bg = background.a > 0.0 ? background.rgb / background.a : centre.rgb;
    const float3 fg = foreground.a > 0.0 ? foreground.rgb / foreground.a : bg;
    // The foreground's coverage: the share of the disk its taps fill (pi r^2 against the taps' count).
    const float alpha = saturate(coverage * 3.14159265 / kTaps);
    return float4(lerp(bg, fg, alpha), alpha);
}

// ---------------------------------------------------------------------------------------------------------
float4 PSDofTent(PSIn input) : SV_Target {
    const float2 uv = input.position.xy * halfRes.xy;
    const float4 d = halfRes.xyxy * float4(0.5, 0.5, -0.5, 0.0);
    float4 acc = g_source.SampleLevel(s_linear, uv - d.xy, 0);
    acc += g_source.SampleLevel(s_linear, uv - d.zy, 0);
    acc += g_source.SampleLevel(s_linear, uv + d.zy, 0);
    acc += g_source.SampleLevel(s_linear, uv + d.xy, 0);
    return acc * 0.25;
}

// ---------------------------------------------------------------------------------------------------------
float4 PSDofComposite(PSIn input) : SV_Target {
    const float2 uv = input.position.xy * screen.xy;
    const float4 blurred = g_source.SampleLevel(s_linear, uv, 0);
    const float coc = CocOf(DistanceAt(int2(input.position.xy)), FocusDistance());
    if (look.y > 0.5) {
        // The debug view: the blur map over the picture, red behind the focus, blue in front, clear in it.
        const float amount = saturate(abs(coc) / lens.z);
        return float4(coc > 0.0 ? float3(1.0, 0.1, 0.1) : float3(0.1, 0.3, 1.0), amount * 0.75);
    }
    const float far = smoothstep(kMargin, 2.0 * kMargin, coc);
    return float4(blurred.rgb, max(blurred.a, far));
}
