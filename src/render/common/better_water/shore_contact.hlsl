// ============================================================================
// src/render/common/better_water/shore_contact.hlsl -- ext.better_water Fancy: contact foam
// ============================================================================
// NOT RETAIL. Our own shader (docs/extensions/handins/BETTERWATER.md section 8). A third pass over each
// water range, drawn like the shore pass (the water material's vertex layout, vertex program and
// render state, this pixel program), at Fancy on a backend that copies the scene depth
// (scene_depth_backend.h). It adds foam where the water meets a MODEL: a pier, a bridge foot, a
// trading post's stone base, a boat. Those write no shore depth into the frame copy's alpha, so the
// shore pass and the strip see nothing there; the scene depth has them.
//
// THE DEPTH. t4 holds the opaque scene's hardware depth (0 near .. 1 far, D3D convention, not reversed),
// read with Load at the pixel. Both depths are linearised exactly, view depth = depthMul / (depthAdd - z)
// (the pass fills the pair per draw from the context's view-projection: better_water.cpp
// DepthLinearize; retail's far plane is 512). Along one ray view depth and distance scale together, so
// the model behind a water pixel lies |viewDir| * (vModel / vWater - 1) further along the ray, and
// times the ray's steepness that is its depth below the surface. (A ratio of the raw 1 - z values is
// NOT enough: it over-reads the depth by far / (far - vModel), 1.24x at 100 units.)
//
// THE RING. Foam also spreads a little OUTWARD from the model onto the open water: eight taps on a
// ring of `ringRadius` world units round the pixel (the screen offsets of a world unit from the view
// direction's derivatives, as the shore pass finds bends) look for scene depth within `width` of the
// water surface -- the part of a model at the waterline, whether just above it (in front of the
// water) or just below. Anything further off the surface (a tree on the bank, a roof, the lake bed)
// is ignored.
//
// THE BANKS are the shore pass's, and the depth alone cannot tell a bank from a pier (the ground above
// the waterline writes frame alpha 0 as a model does). The frame alpha at this water pixel can: the
// contact line is drawn only where it is 0 (a model right behind the water: the terrain under water
// writes its depth there), and the ring only over open water, where the bed behind the pixel is at
// least `kOpen` down. A pier in water shallower than that gets its contact line but no ring.
//
// Compiled offline by tools/water_shaders.py (fxc, ps_4_0), twice: river and, with WATER_LAKE, lake.
// ============================================================================

Texture2D        g_water : register(t0);
SamplerState     s_water : register(s0);
Texture2D        g_frame : register(t1);
SamplerState     s_frame : register(s1);
Texture2D<float> g_depth : register(t4);

struct PSIn {
    float4 position : SV_Position;
    float4 texcoord : TEXCOORD0;
    float3 viewDir  : TEXCOORD1;
    float4 screen   : TEXCOORD2;
    float4 lfa      : TEXCOORD3;
};

#ifndef WATER_LAKE
float2 MeshCoord(float4 t) { return float2(t.x, 2.0 * t.w - t.y); }
#else
float2 MeshCoord(float4 t) { return float2(t.x, (0.01 * t.y + 0.0057 * t.w) / 0.0157); }
#endif

// THE LOOK: Ext::BetterWater::ContactParams (src/ext/better_water/shore_params.h), in the
// MaterialConstants (b1) for this draw only. Distances in world units.
cbuffer MaterialConstants : register(b1) {
    float4 mc0;   // width, strength, ringRadius, ringStrength
    float4 mc1;   // breakupScale, breakupAmount, opacity, unused
    float4 mc2;   // depthMul, depthAdd (filled per draw, not look values), unused
    float4 mc3;
};
#define kWidth         mc0.x
#define kStrength      mc0.y
#define kRingRadius    mc0.z
#define kRingStrength  mc0.w
#define kBreakupScale  mc1.x
#define kBreakup       mc1.y
#define kOpacity       mc1.z
#define kDepthMul      mc2.x
#define kDepthAdd      mc2.y

static const float kMeshScale     = 18.0;   // as the shore pass
static const float kMaxRingPixels = 64.0;   // the ring never reaches further than this on screen
static const float kLand          = 0.02;   // frame alpha above this: the terrain, the shore pass's
static const float kOpen          = 0.15;   // frame alpha from which the bed is deep: open water

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

// How far the scene at pixel `px` lies from the water surface, in world units along the vertical:
// positive below the surface, negative above it (a model in front of the water). `zWater` / `dist` /
// `steep` are this water pixel's: the surface is locally a plane, so the neighbour's water depth is
// taken as this one's (the ring is a few world units).
float SceneOffset(int2 px, float zWater, float dist, float steep) {
    float zModel = g_depth.Load(int3(px, 0));
    if (zModel >= 1.0) return 1e4;   // the sky / nothing drawn
    float vWater = kDepthMul / max(kDepthAdd - zWater, 1e-7);
    float vModel = kDepthMul / max(kDepthAdd - zModel, 1e-7);
    return dist * (vModel / max(vWater, 1e-4) - 1.0) * steep;
}

float4 PSShoreContact(PSIn i) : SV_Target {
    float2 wx = ddx(i.viewDir.xy);
    float2 wy = ddy(i.viewDir.xy);
    float det = wx.x * wy.y - wy.x * wx.y;
    float2 pxPerWorldX = float2(wy.y, -wx.y) / det;
    float2 pxPerWorldY = float2(-wy.x, wx.x) / det;
    float reach = max(length(pxPerWorldX), length(pxPerWorldY)) * kRingRadius;
    float shrink = reach > kMaxRingPixels ? kMaxRingPixels / reach : 1.0;
    float2 ringX = pxPerWorldX * shrink * kRingRadius;
    float2 ringY = pxPerWorldY * shrink * kRingRadius;
    bool ringOk = abs(det) > 1e-8;

    float2 screenCoord = i.screen.xy / i.screen.w;
    float4 here = g_frame.Sample(s_frame, screenCoord);
    float modelBehind = here.a <= kLand ? 1.0 : 0.0;         // the contact line's pixels
    float openWater = smoothstep(kOpen, kOpen * 2.0, here.a);   // the ring's
    if (modelBehind == 0.0 && openWater == 0.0) return float4(0.0, 0.0, 0.0, 0.0);

    float dist = length(i.viewDir);
    float steep = saturate(-i.viewDir.z / max(dist, 1e-4));
    float zWater = i.position.z;
    int2 px = int2(i.position.xy);

    // The model right behind this water pixel, just under the surface: the contact line itself.
    float below = SceneOffset(px, zWater, dist, steep);
    float contact = below >= 0.0 ? (1.0 - smoothstep(0.0, kWidth, below)) * modelBehind : 0.0;

    // The ring: the waterline part of a model within ringRadius, nearer taps counting more.
    float ring = 0.0;
    if (ringOk && kRingStrength > 0.0 && openWater > 0.0) {
        static const float2 kRing[8] = {
            float2(1.0, 0.0), float2(0.7071, 0.7071), float2(0.0, 1.0), float2(-0.7071, 0.7071),
            float2(-1.0, 0.0), float2(-0.7071, -0.7071), float2(0.0, -1.0), float2(0.7071, -0.7071),
        };
        [unroll] for (int k = 0; k < 8; ++k) {
            [unroll] for (int s = 1; s <= 2; ++s) {
                float t = s * 0.5;
                int2 tap = px + int2(round((kRing[k].x * ringX + kRing[k].y * ringY) * t));
                float off = SceneOffset(tap, zWater, dist, steep);
                float near = 1.0 - smoothstep(0.0, kWidth, abs(off));
                ring = max(ring, near * (1.25 - 0.5 * t));
            }
        }
        ring = saturate(ring) * kRingStrength * openWater;
    }

    float2 p = MeshCoord(i.texcoord) * kMeshScale;
    float breakup = ValueNoise(p * kBreakupScale) * 0.65 + ValueNoise(p * kBreakupScale * 2.3 + 5.7) * 0.35;
    float broken = lerp(1.0, smoothstep(0.3, 0.7, breakup), kBreakup);
    float foam = saturate(max(contact * kStrength, ring)) * broken;

    float2 n = g_water.Sample(s_water, i.texcoord.xy).wy - 0.5;
    n += g_water.Sample(s_water, i.texcoord.zw).wy - 0.5;
    float luma = dot(here.rgb, float3(0.299, 0.587, 0.114));
    float ripple = 0.9 + 0.2 * saturate(0.5 + n.y);
    float3 foamColor = saturate((lerp(here.rgb, luma.xxx, 0.8) * 0.9 + 0.5) * ripple);

    float a = foam * (1.0 - saturate(i.lfa.z)) * kOpacity;
    return float4(foamColor, a);
}
