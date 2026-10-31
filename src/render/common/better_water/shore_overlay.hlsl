// ============================================================================
// src/render/common/better_water/shore_overlay.hlsl -- ext.better_water's shore pixel program
// ============================================================================
// NOT RETAIL. Our own shader (docs/extensions/handins/BETTERWATER.md). It is drawn as a second pass
// over each water range, with the water material's own vertex layout, vertex program and render state
// (the user's installed ones, bound by retail's draw) and this pixel program in place of the water's.
// It adds, on top of retail's water (left exactly as it drew):
//   1. a wet bed: the ground under the shallows darkened, as wet ground is (retail shows it dry);
//   2. a thin line at the waterline, white only on the outside of bends;
// both starting from a waterline that wanders, so the shore's contour breaks up.
//
// WHAT IT READS (both bound by retail's water draw, so this pass binds nothing):
//   t0  the water material's normal texture; .wy are the two normal components (two scrolling
//       samples, as the water itself takes them): the refraction offset and the foam's ripple shading.
//   t1  the frame copy taken before the water. Its alpha is how far below the waterline the surface
//       behind the pixel lies: 0 at the bank, rising linearly to 1 about 1.5 units down. The terrain
//       and the in-water models (piles, piers) write it; the water uses it to fade in from the bank.
//       That makes it a vertical, world-space shore depth that does not move with the camera.
//
// THE INTERPOLANTS are the water vertex program's outputs, in its register order; this program reads
// a prefix of them (the shadow coordinate after TEXCOORD3 is not declared):
//   TEXCOORD0  the two scrolling uv sets (xy, zw)      TEXCOORD2  the projective screen coordinate
//   TEXCOORD1  the view direction (world - camera)    TEXCOORD3  x/y lighting, z fog, w the mesh alpha
//
// FIXED TO THE WORLD. Every pattern here is keyed to the mesh's own still coordinate (MeshCoord: a
// blend of the two scrolling uv sets that cancels the scroll, per material) or to the world through
// the view direction's screen derivatives, never to the screen or to the scrolling uvs: a pattern
// keyed to those swims and sparkles as the camera turns.
//
// Colours come from the frame copy itself (the lit, fogged scene behind the water), so the foam and
// the wet bed follow fog, seasons and snow without any constant of their own.
//
// Compiled offline by tools/water_shaders.py (fxc, ps_4_0) into shore_overlay_ps.h, twice: the river
// program and, with WATER_LAKE, the lake program (their uv sets scroll differently). Every backend
// takes the DXBC (DX11 directly, Vulkan through dxbc-spirv, Metal through airconv).
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

// The mesh's still coordinate. Both water vertex programs copy the vertex uv into xy and zw and then
// scroll some components with time; a fixed blend of a scrolled pair cancels the scroll.
#ifndef WATER_LAKE
// River: y += 0.02 t, w += 0.01 t; x, z still.  2w - y = v.
float2 MeshCoord(float4 t) { return float2(t.x, 2.0 * t.w - t.y); }
#else
// Lake: y -= 0.0057 t, w += 0.01 t, z += 0.005 t; x still.  (0.01 y + 0.0057 w) / 0.0157 = v.
float2 MeshCoord(float4 t) { return float2(t.x, (0.01 * t.y + 0.0057 * t.w) / 0.0157); }
#endif

// ---- the look ----------------------------------------------------------------------------------
// Depths are in frame-alpha units: 0 = the bank, 1 = about 1.5 world units down. Every layer starts
// and ends on a wide smoothstep: a hard step at the waterline or at the end of a band reads as a cut.
//
// THE LOOK comes in the MaterialConstants (b1), which the pass fills for its own draw from
// Ext::BetterWater::ShoreParams (src/ext/better_water/shore_params.h: the defaults and what each
// value means; the debug overlay moves them live). Same order as that struct.
cbuffer MaterialConstants : register(b1) {
    float4 mc0;   // lineIn, lineOut0, lineOut1, linePlain
    float4 mc1;   // lineBend, bendRadius, bendFrom, bendTo
    float4 mc2;   // wetRise, wetFall, wetEnd, wetStrength
    float4 mc3;   // wetShade, edgeAmp, edgeScale, opacity
};
#define kLineIn        mc0.x
#define kLineOut0      mc0.y
#define kLineOut1      mc0.z
#define kLineStrength  mc0.w
#define kBendStrength  mc1.x
#define kBendRadius    mc1.y
#define kBendFrom      mc1.z
#define kBendTo        mc1.w
#define kWetRise       mc2.x
#define kWetFall       mc2.y
#define kWetEnd        mc2.z
#define kWetStrength   mc2.w
#define kWetShade      mc3.x
#define kEdgeAmp       mc3.y
#define kEdgeScale     mc3.z
#define kOpacity       mc3.w

// Fixed: not part of the look.
static const float kMeshScale     = 18.0;   // mesh units -> world units, about (measured 2026-10-31)
static const float kGate          = 0.03;   // everything fades in over the first stretch below the waterline
static const float kMaxRingPixels = 96.0;   // the bend ring never reaches further than this on screen
static const float kLand          = 0.02;   // a ring tap below this depth is land
static const float kRefract       = 0.05;   // the water's own refraction offset (the wet bed's colour)

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

// The fraction of an 8-tap ring that is land: about 0.5 on a straight bank, more where the bank
// curves around the water. `ringX` / `ringY` are the screen-uv offsets of one world unit along x and y.
float LandFraction(float2 uv, float2 ringX, float2 ringY) {
    static const float2 kRing[8] = {
        float2(1.0, 0.0), float2(0.7071, 0.7071), float2(0.0, 1.0), float2(-0.7071, 0.7071),
        float2(-1.0, 0.0), float2(-0.7071, -0.7071), float2(0.0, -1.0), float2(0.7071, -0.7071),
    };
    float land = 0.0;
    [unroll] for (int k = 0; k < 8; ++k) {
        float2 offset = (kRing[k].x * ringX + kRing[k].y * ringY) * kBendRadius;
        float d = g_frame.SampleLevel(s_frame, uv + offset, 0).a;
        land += d < kLand ? 1.0 : 0.0;
    }
    return land * 0.125;
}

float4 PSShoreOverlay(PSIn i) : SV_Target {
    // The screen offsets of one world unit along x and y on the water plane, from the view direction's
    // derivatives (world - camera: the camera cancels). Taken first, in uniform flow.
    float2 wx = ddx(i.viewDir.xy);
    float2 wy = ddy(i.viewDir.xy);
    float det = wx.x * wy.y - wy.x * wx.y;
    float fw;
    float fh;
    g_frame.GetDimensions(fw, fh);
    float2 pxPerWorldX = float2(wy.y, -wx.y) / det;    // pixels moved by one world unit along x
    float2 pxPerWorldY = float2(-wy.x, wx.x) / det;    // ...and along y
    float reach = max(length(pxPerWorldX), length(pxPerWorldY)) * kBendRadius;
    float shrink = reach > kMaxRingPixels ? kMaxRingPixels / reach : 1.0;
    float2 ringX = pxPerWorldX * shrink / float2(fw, fh);
    float2 ringY = pxPerWorldY * shrink / float2(fw, fh);
    bool ringOk = abs(det) > 1e-8;

    float2 n = g_water.Sample(s_water, i.texcoord.xy).wy - 0.5;
    n += g_water.Sample(s_water, i.texcoord.zw).wy - 0.5;

    float2 screenCoord = i.screen.xy / i.screen.w;
    float4 here = g_frame.Sample(s_frame, screenCoord);                     // the surface (the line)
    float4 behind = g_frame.Sample(s_frame, screenCoord + n * kRefract);    // the bed, as the water refracts it
    float depth = here.a;
    // Open water, past the wet bed and any wander: nothing to draw (most of a river's pixels).
    if (depth > kWetEnd + kEdgeAmp && behind.a > kWetEnd) return float4(0.0, 0.0, 0.0, 0.0);

    // Where the frame alpha is 0 retail shows no water at all (an ordinary model, or the bank above
    // the line): nothing is drawn there, and everything fades in from it.
    float gate = smoothstep(0.0, kGate, depth);

    // The waterline wanders: the depth the wet bed and the line start from is shifted by a still
    // noise, so the shore's contour breaks up without a line of its own.
    float2 p = MeshCoord(i.texcoord) * kMeshScale;
    float wander = (ValueNoise(p * kEdgeScale) - 0.5) * 2.0 * kEdgeAmp;
    float d = max(depth + wander, 0.0);

    // The wet bed: under shallow water the ground is darker and richer than on the bank. Retail lets
    // the dry bed show through its shallows; this pulls it toward its own dark shade, rising just
    // under the waterline and handing over as retail's water turns opaque. Drawn as a pull toward a
    // dark colour (alpha blend), so the water's ripples on top still show.
    float wet = gate * smoothstep(0.0, kWetRise, d) * (1.0 - smoothstep(kWetFall, kWetEnd, d)) * kWetStrength;
    float bedLuma = dot(behind.rgb, float3(0.299, 0.587, 0.114));
    float3 wetColor = saturate(lerp(bedLuma.xxx, behind.rgb, 1.3)) * kWetShade;

    // The line at the waterline, white on the outside of bends.
    float band = gate * smoothstep(0.0, kLineIn, d) * (1.0 - smoothstep(kLineOut0, kLineOut1, d));
    float bend = 0.0;
    if (ringOk && band > 0.0) {
        bend = smoothstep(kBendFrom, kBendTo, LandFraction(screenCoord, ringX, ringY));
    }
    float breakup = ValueNoise(p * 0.8) * 0.65 + ValueNoise(p * 2.1 + 3.1) * 0.35;
    float plain = smoothstep(0.45, 0.75, breakup) * kLineStrength;
    float white = smoothstep(0.25, 0.55, breakup) * kBendStrength * bend;
    float foam = band * max(plain, white);

    float luma = dot(here.rgb, float3(0.299, 0.587, 0.114));
    // Foam: near white, lit by the scene at the bank, shaded by the water's own ripples so its glints
    // are the water's scale.
    float ripple = 0.9 + 0.2 * saturate(0.5 + n.y);
    float3 foamColor = saturate((lerp(here.rgb, luma.xxx, 0.8) * 0.9 + 0.5) * ripple);

    // Back to front: the wet bed, then the foam.
    float3 rgb = wetColor;
    float a = wet;
    rgb = lerp(rgb, foamColor, foam / max(foam + a * (1.0 - foam), 1e-4));
    a = foam + a * (1.0 - foam);

    // Far off the fog takes the water; the overlay goes with it. lfa.w is the mesh's own edge alpha.
    a *= (1.0 - saturate(i.lfa.z)) * i.lfa.w * kOpacity;
    return float4(rgb, a);
}
