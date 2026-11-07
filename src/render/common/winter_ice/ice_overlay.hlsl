// ============================================================================
// src/render/common/winter_ice/ice_overlay.hlsl -- ext.winter_ice's ice pixel program
// ============================================================================
// NOT RETAIL. Our own shader (docs/extensions/handins/WINTERICE.md). Drawn as one more pass over each
// water range (WorldGeometry's contour draw: 0x6000 rivers and the coast, 0x8000 lakes), with the water
// material's own vertex layout, vertex program and render state (the user's installed ones, as
// ext.better_water's shore pass does) and this pixel program in place of the water's.
//
// WHERE THE ICE IS. Two fronts in the water's depth units (the frame copy's alpha: 0 at the bank, 1
// about 1.5 world units down; src/ext/winter_ice/ice_model.h): ice from the thaw front down to the
// freeze front. Freezing starts in the shallows at the bank and closes over the middle; thawing opens
// the shallow sides first. Both fronts wander with a still noise so the edge is ragged. Every deep
// pixel reads depth 1, so the deep water's depth gets a broad noise of up to kDeepSpread on top: the
// middle closes (and in spring opens) in drifting patches as the front runs from 1 to ice_model.h's
// kFrozen, never all at once. A mask (one texel per map cell, open_mask.h) keeps open water round the
// buildings in the water and along the trade boats' channel.
//
// WHERE THE PIXEL IS. The river mesh's uv runs across and along the channel, not over the world, so
// the world position is taken from the view direction (TEXCOORD2 = world - camera, both water vertex
// programs) plus the camera, which the pass folds into mc1 with the map's half edge: cells = viewDir.xy
// + mc1.xy. Fixed to the world: nothing swims as the camera turns.
//
// THE TILING. The photograph repeats every kTile world units; two samplings (the second turned and
// scaled) are blended by a broad noise and the whole is brightened and darkened in broad patches, so the
// repeat does not read as a grid.
//
// WHAT IT READS.
//   t1  the frame copy before the water: .a the depth above, .rgb the bed (thin ice shows it)
//   t2  the planar reflection, t3 the highlight cube (bound by retail's water draw, as the water uses them)
//   t4  the ice surface (ours): rgb colour, a roughness          (tiled over the world)
//   t6  the ice detail (ours): rg normal xy, b displacement     (tiled with t4)
//   t7  the open-water mask (ours): r = 1 where the water stays open, 1 texel per map cell
//   b0  the scene: lightColor / ambientColor / fogColor, userPosition.x = the snow cover (saturate(32 - T))
//   b1  the look (Ext::WinterIce::IceParams, src/ext/winter_ice/winter_ice.h), filled by the pass
//
// THE INTERPOLANTS are the water vertex programs' (RiverMaterial.srsl:Normal_vs / Normal_lakevs share
// basevs's outputs), under their own semantic names (the Vulkan backend links by name:
// shore_overlay.hlsl's note).
//
// Compiled offline by tools/water_shaders.py (fxc, ps_4_0) into ice_overlay_ps.h. Every backend takes
// the DXBC (DX11 directly, Vulkan through dxbc-spirv, Metal through airconv).
// ============================================================================

Texture2D    g_frame   : register(t1);
SamplerState s_frame   : register(s1);
Texture2D    g_reflect : register(t2);
SamplerState s_reflect : register(s2);
TextureCube  g_light   : register(t3);
SamplerState s_light   : register(s3);
Texture2D    g_surface : register(t4);
SamplerState s_surface : register(s4);
Texture2D    g_detail  : register(t6);
SamplerState s_detail  : register(s6);
Texture2D    g_open    : register(t7);
SamplerState s_open    : register(s7);

struct PSIn {
    float4 position : SV_Position;
    float4 texcoord : TEXCOORD1;
    float3 viewDir  : TEXCOORD2;
    float4 screen   : TEXCOORD3;
    float4 lfa      : TEXCOORD4;
};

cbuffer PixelConstants : register(b0) {
    float4 pcTexelSize;
    float4 pcFogColor;
    float4 pcLightColor;
    float4 pcAmbientColor;
    float4 pcUserPosition;   // x = the snow cover; yzw = the open-water edge (the pass's own, for its draw):
                             // wobble, 1 / wobble size (cells), grain
};

cbuffer MaterialConstants : register(b1) {
    float4 mc0;   // freeze, thaw, edgeAmp, bankSnow
    float4 mc1;   // camera x + half edge, camera y + half edge, edge softness, tile
    float4 mc2;   // thin rgb (the colour of new, thin ice over dark water), reflect
    float4 mc3;   // frost, normalStrength, innerDepth, dusting
};
#define kFreeze      mc0.x
#define kThaw        mc0.y
#define kEdgeAmp     mc0.z
#define kBankSnow    mc0.w
#define kOffset      mc1.xy
#define kEdgeSoft    mc1.z
#define kWobble      pcUserPosition.y
#define kWobbleFreq  pcUserPosition.z
#define kGrain       pcUserPosition.w
#define kTile        mc1.w
#define kThin        mc2.xyz
#define kReflect     mc2.w
#define kFrost       mc3.x
#define kNormal      mc3.y
#define kInner       mc3.z
#define kDusting     mc3.w

static const float kGate = 0.03;         // fades in over the first stretch below the waterline (the water does too)
static const float kSoft = 0.05;         // a front's soft edge, depth units
static const float kDeepSpread = 0.5;    // the deep water's patch noise (ice_model.h kDeepSpread)
// The second sampling of the photograph: turned 37 degrees and scaled, so its repeat never lines up
// with the first one's.
static const float2x2 kTurn = float2x2(0.7986 * 0.71, -0.6018 * 0.71, 0.6018 * 0.71, 0.7986 * 0.71);

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

float4 PSIceOverlay(PSIn i) : SV_Target {
    float2 cells = i.viewDir.xy + kOffset;   // map cells (0 .. edge), fixed to the world
    float2 screen = i.screen.xy / i.screen.w;
    float3 view = normalize(i.viewDir);

    // Every texture first, in uniform flow (the early outs below would break the derivatives).
    float2 tuv = cells / kTile;
    float2 tuv2 = mul(kTurn, tuv) + float2(0.37, 0.61);
    float4 surfaceA = g_surface.Sample(s_surface, tuv);
    float4 surfaceB = g_surface.Sample(s_surface, tuv2);
    float4 detailA = g_detail.Sample(s_detail, tuv);
    float4 detailB = g_detail.Sample(s_detail, tuv2);
    float mixAB = smoothstep(0.3, 0.7, ValueNoise(cells * 0.045) * 0.8 + ValueNoise(cells * 0.13) * 0.2);
    float4 surface = lerp(surfaceA, surfaceB, mixAB);
    float4 detail = lerp(detailA, detailB, mixAB);
    // The inner layer: the surface seen again, deeper, along the view ray, so the ice reads as a
    // clear slab with its cracks and bubbles below the top (the parallax of a thick sheet).
    float2 slant = view.xy / max(-view.z, 0.25);
    float4 inner = g_surface.Sample(s_surface, tuv * 0.73 + 0.31 + slant * kInner * (1.25 - detail.b) / kTile);
    float3 n = normalize(float3((detail.rg - 0.5) * 2.0 * kNormal, 1.0));
    float4 here = g_frame.Sample(s_frame, screen);
    float4 bed = g_frame.Sample(s_frame, screen + n.xy * 0.01);
    float4 refl = g_reflect.Sample(s_reflect, screen + n.xy * 0.08);
    float3 sky = g_light.Sample(s_light, reflect(view, n)).xyz;
    float maskW;
    float maskH;
    g_open.GetDimensions(maskW, maskH);
    float open = g_open.Sample(s_open, cells / maskW).r;

    float depth = here.a;
    if (depth <= 0.0) return float4(0.0, 0.0, 0.0, 0.0);   // no water behind: a model, or the bank

    // The fronts, wandering: two octaves of still noise move the depth the fronts are tested against,
    // and the deep water gets its broad patches.
    float wander = (ValueNoise(cells * 0.35) - 0.5) * 2.0 * kEdgeAmp +
                   (ValueNoise(cells * 1.7 + 7.3) - 0.5) * kEdgeAmp;
    float patches = ValueNoise(cells * 0.06 + 17.0) * 0.65 + ValueNoise(cells * 0.21 + 3.0) * 0.35;
    float d = saturate(depth) + wander + smoothstep(0.5, 1.0, depth) * patches * kDeepSpread;
    float iceIn = 1.0 - smoothstep(kFreeze - kSoft, kFreeze + kSoft, d);
    float iceOut = kThaw > 0.0 ? smoothstep(kThaw - kSoft, kThaw + kSoft, d) : 1.0;
    // Open water round a building and along the boat channel. The mask's discs and strips are smooth
    // and regular (a bridge's row of discs reads as a ruled strip), so the edge is broken twice: a broad
    // wobble a few cells across that moves it in and out, and a fine grain that roughens it.
    float edgeNoise = (ValueNoise(cells * kWobbleFreq + 31.0) - 0.5) * kWobble + (ValueNoise(cells * 2.3) - 0.5) * kGrain;
    float soft = max(kEdgeSoft, 0.005) * 0.5;
    float hole = smoothstep(0.45 - soft, 0.45 + soft, open + edgeNoise);
    float ice = smoothstep(0.0, kGate, depth) * iceIn * iceOut * (1.0 - hole);
    if (ice <= 0.002) return float4(0.0, 0.0, 0.0, 0.0);

    // Thickness: new ice at either front is thin and dark (the water shows through it); it whitens
    // as it thickens away from the fronts.
    float fromFreeze = kFreeze - d;
    float fromThaw = kThaw > 0.0 ? d - kThaw : 1.0;
    float thick = saturate(min(fromFreeze, fromThaw) / 0.25) * (1.0 - hole);

    // The slab: the photograph's ice over its inner layer, the bed showing through thin ice. The
    // photograph is high in contrast at this tiling: its detail is pulled toward its own mean, and the
    // whole is lightened and darkened in broad patches.
    float3 photo = lerp(dot(surface.rgb, float3(0.333, 0.333, 0.334)).xxx * float3(0.93, 0.97, 1.0), surface.rgb, 0.55);
    float3 slab = lerp(inner.rgb * 0.85, photo, 0.65);
    slab *= 0.9 + 0.2 * ValueNoise(cells * 0.07 + 5.0);
    float3 thin = lerp(bed.rgb * 0.6, kThin, 0.55);
    float3 albedo = lerp(thin, slab, 0.35 + 0.65 * thick);
    // Frost: broad, soft patches (a few cells across) with a little fine grain, so the slab reads as one
    // sheet rather than a camouflage of small blotches.
    float frostNoise = ValueNoise(cells * 0.12 + 3.1) * 0.75 + ValueNoise(cells * 0.9) * 0.25;
    albedo = lerp(albedo, float3(0.86, 0.9, 0.94), kFrost * thick * smoothstep(0.3, 0.9, frostNoise));

    // Snow: banked against the shore while the ground is snowy, and a light dusting in patches.
    float snowCover = saturate(pcUserPosition.x);
    float bank = 1.0 - smoothstep(0.04, 0.3, d + (frostNoise - 0.5) * 0.2);
    float dust = smoothstep(0.5, 0.9, ValueNoise(cells * 0.08 + 11.0) * 0.7 + frostNoise * 0.3) * kDusting;
    float snow = saturate(snowCover * max(bank * kBankSnow, dust) * thick);
    albedo = lerp(albedo, float3(0.92, 0.94, 0.97), snow);

    // Lit as the terrain is (ComputeLighting): the vertex program's direct and ambient terms for an up
    // normal, the normal map nudging the direct term.
    float direct = saturate(i.lfa.x * (0.85 + 0.3 * n.z));
    float3 rgb = albedo * (pcLightColor.xyz * direct + pcAmbientColor.xyz * i.lfa.y);

    // A glossy sheen where the ice is smooth and bare: the scene's reflection and the sky's highlight,
    // stronger at grazing angles (the water's fresnel).
    float facing = saturate(dot(-view, n));
    float fresnel = 1.0 - facing * facing;
    float gloss = (1.0 - surface.a) * (1.0 - snow) * kReflect;
    rgb += (refl.rgb * 0.6 + sky * 0.4 * refl.w) * fresnel * gloss;

    // Far off the fog takes the ice, as it takes the water.
    rgb = lerp(rgb, pcFogColor.xyz, min(saturate(i.lfa.z), pcFogColor.w));

    // Thin ice lets a little of the water through; lfa.w is the water mesh's own edge alpha.
    float a = ice * lerp(0.75, 1.0, max(thick, snow)) * i.lfa.w;
    return float4(rgb, a);
}
