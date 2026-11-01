// ============================================================================
// src/render/common/better_water/shore_strip.hlsl -- ext.better_water Fancy: the shore strip's pixels
// ============================================================================
// NOT RETAIL. Our own shader (docs/extensions/handins/BETTERWATER.md section 7). The shore strip is a
// mesh of ours: the terrain cells that cross the waterline, laid on the terrain (the terrain's own
// heights and triangle diagonals) and lifted a hair above it, in the water's vertex format. It is drawn
// with the river water's vertex layout, vertex program and render state (the user's installed ones) and
// this pixel program, so it is lit, fogged and placed exactly as the water is.
//
// What it adds is what the Plain pass cannot reach, the bank side of the waterline (Plain draws only
// on the water, where the frame alpha is above 0):
//   1. the wet bank: the ground just above the waterline darkened, as a bank the water laps is;
//   2. a foam fringe where the water visibly starts, so the edge where water meets ground softens;
// both from a waterline that wanders (a still noise added to the height), so the edge breaks up.
//
// THE HEIGHT. The vertex's edge-alpha byte (the water program passes it through as TEXCOORD3.w) holds
// the corner's height above the waterline, (h + 0.5) / 2 in world units: -0.5 .. 1.5. Interpolated over
// a triangle it is exactly the terrain's own height (the same grid, the same diagonal), so the line
// the strip draws at 0 is where retail's water meets the ground.
//
// THE INTERPOLANTS are the river water vertex program's (shore_overlay.hlsl has the list). The uv is
// planar (the corner's world position * 48 / 1024, as retail's lake shore ring packs it), so
// MeshCoord * kMeshScale is a world coordinate, and the patterns are fixed to the world.
//
// Compiled offline by tools/water_shaders.py (fxc, ps_4_0) into shore_strip_ps.h.
// ============================================================================

Texture2D    g_water : register(t0);
SamplerState s_water : register(s0);
Texture2D    g_frame : register(t1);
SamplerState s_frame : register(s1);

struct PSIn {
    float4 position : SV_Position;
    float4 texcoord : TEXCOORD0;
    float3 viewDir  : TEXCOORD1;
    float4 screen   : TEXCOORD2;
    float4 lfa      : TEXCOORD3;
};

// River: y += 0.02 t, w += 0.01 t; x, z still.  2w - y = v (shore_overlay.hlsl).
float2 MeshCoord(float4 t) { return float2(t.x, 2.0 * t.w - t.y); }

// THE LOOK: Ext::BetterWater::StripParams (src/ext/better_water/shore_params.h), in the
// MaterialConstants (b1) for this draw only. Heights in world units above the waterline.
cbuffer MaterialConstants : register(b1) {
    float4 mc0;   // wetTop, wetStrength, wetShade, wetBreakup
    float4 mc1;   // foamUp, foamDown, foamStrength, foamScale
    float4 mc2;   // wanderAmp, wanderScale, waterFade, opacity
    float4 mc3;   // lineDrop, unused
};
#define kWetTop        mc0.x
#define kWetStrength   mc0.y
#define kWetShade      mc0.z
#define kWetBreakup    mc0.w
#define kFoamUp        mc1.x
#define kFoamDown      mc1.y
#define kFoamStrength  mc1.z
#define kFoamScale     mc1.w
#define kWanderAmp     mc2.x
#define kWanderScale   mc2.y
#define kWaterFade     mc2.z
#define kOpacity       mc2.w
#define kLineDrop      mc3.x

static const float kMeshScale = 1024.0 / 48.0;   // planar uv -> world units, exactly (the packing)

float Hash(float2 p) {
    p = frac(p * float2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return frac(p.x * p.y);
}

float ValueNoise(float2 p) {
    float2 i = floor(p);
    float2 f = frac(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = Hash(i);
    float b = Hash(i + float2(1.0, 0.0));
    float c = Hash(i + float2(0.0, 1.0));
    float d = Hash(i + float2(1.0, 1.0));
    return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
}

float4 PSShoreStrip(PSIn i) : SV_Target {
    float h = i.lfa.w * 2.0 - 0.5;               // the height above the waterline, world units
    float2 p = MeshCoord(i.texcoord) * kMeshScale;  // the world position

    // The waterline wanders, as Plain's does: the height every layer starts from is shifted by a
    // still noise, so the edge breaks up along the bank.
    float wander = (ValueNoise(p * kWanderScale) - 0.5) * 2.0 * kWanderAmp;
    float hw = h + wander;

    // Under the water Plain has the shallows: the wet bank fades out below the line over waterFade,
    // as Plain's wet bed rises over the same stretch, so the dark runs on across the line.
    float under = smoothstep(-kWaterFade, 0.0, hw);

    float2 screenCoord = i.screen.xy / i.screen.w;
    float4 ground = g_frame.Sample(s_frame, screenCoord);   // the bank as the scene drew it

    // The wet bank: darkest at the line, gone by wetTop, its top edge broken by a coarser noise.
    float top = kWetTop * (1.0 - kWetBreakup * ValueNoise(p * 0.45 + 7.3));
    float wet = (1.0 - smoothstep(0.0, max(top, 1e-3), max(hw, 0.0))) * kWetStrength * under;
    float groundLuma = dot(ground.rgb, float3(0.299, 0.587, 0.114));
    float3 wetColor = saturate(lerp(groundLuma.xxx, ground.rgb, 1.3)) * kWetShade;

    // The foam fringe: a broken line where the water visibly starts. Retail's water is clear at the true
    // waterline and turns opaque only further down (frame alpha 0 at h = 0 to 1 at 1.5 below), so a line
    // at h = 0 reads as floating up the bank; the fringe sits lineDrop below it, on Plain's line, from
    // foamDown below that to foamUp above.
    float hl = hw + kLineDrop;
    float fringe = smoothstep(-kFoamDown, 0.0, hl) * (1.0 - smoothstep(0.0, kFoamUp, hl));
    float breakup = ValueNoise(p * kFoamScale) * 0.65 + ValueNoise(p * kFoamScale * 2.6 + 3.1) * 0.35;
    float foam = fringe * smoothstep(0.4, 0.75, breakup) * kFoamStrength;
    float3 foamColor = saturate(lerp(ground.rgb, groundLuma.xxx, 0.8) * 0.9 + 0.5);

    float3 rgb = wetColor;
    float a = wet;
    rgb = lerp(rgb, foamColor, foam / max(foam + a * (1.0 - foam), 1e-4));
    a = foam + a * (1.0 - foam);

    // The fog takes the strip with the water.
    a *= (1.0 - saturate(i.lfa.z)) * kOpacity;
    return float4(rgb, a);
}
