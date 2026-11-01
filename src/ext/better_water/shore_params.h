// ============================================================================
// src/ext/better_water/shore_params.h -- the shore program's look, as the pass hands it to the shader
// ============================================================================
// NOT RETAIL. docs/extensions/handins/BETTERWATER.md. Plain data, no engine include: the debug tweak
// overlay's struct (src/ext/tweak/ext_tweaks.h) holds one.
//
// The sixteen floats are the shore program's MaterialConstants (cbuffer b1, four float4s, in this
// order: shore_overlay.hlsl "THE LOOK"). DrawShore writes them into the context's material staging
// for its own draw and puts retail's back after it. Depths are in frame-alpha units: 0 = the bank,
// 1 = about 1.5 world units down. The defaults are the user's set from the debug overlay (2026-10-31);
// the overlay (Alt+F10, "Better water") moves them live and prints the set to copy back here. The
// line's fade-out starting before its fade-in ends (lineOut0 < lineIn) is deliberate: a narrow peak
// rather than a plateau. bendFrom below 0.5 lets straight banks keep a little of the white.
// ============================================================================
#pragma once

namespace Ext {
namespace BetterWater {

struct ShoreParams {
    // param0: the line at the waterline
    float lineIn      = 0.033f;     // fades in over this much depth below the (wandering) waterline
    float lineOut0    = 0.012f;     // starts fading out here...
    float lineOut1    = 0.092f;     // ...and is gone here (the band's size)
    float linePlain   = 0.17f;      // its opacity along a plain bank
    // param1: white on the outside of bends
    float lineBend    = 0.49f;      // its opacity on the outside of a bend
    float bendRadius  = 2.24f;      // the ring that finds bends, world units
    float bendFrom    = 0.414f;     // the ring's land share where a bend starts (0.5 = a straight bank)
    float bendTo      = 0.614f;     // ...and is full
    // param2: the wet bed (the darker band under the shallows)
    float wetRise     = 0.121f;     // darkens over this much depth...
    float wetFall     = 0.276f;     // ...holds to here (the band's width)...
    float wetEnd      = 0.709f;     // ...and has handed over to retail's water by here
    float wetStrength = 0.28f;      // how far the bed is pulled toward its wet shade
    // param3: the wet shade, the edge, the whole
    float wetShade    = 0.25f;      // the wet shade: the bed's colour at this brightness
    float edgeAmp     = 0.037f;     // how far the waterline wanders (depth)
    float edgeScale   = 0.33f;      // the wander's cells per world unit
    float opacity     = 0.85f;      // the whole overlay's opacity (0 = retail's look)
};
static_assert(sizeof(ShoreParams) == 64, "ShoreParams is the shader's four float4s");

// Fancy's shore strip (shore_strip.hlsl): the bank side of the waterline. Heights are in WORLD units
// above the waterline (the strip carries the terrain's own height, not the frame alpha).
struct StripParams {
    // mc0: the wet bank
    float wetTop       = 0.134f;    // the darkening is gone by this height above the line
    float wetStrength  = 0.26f;     // how far the bank is pulled toward its wet shade
    float wetShade     = 0.25f;     // the wet shade: the bank's colour at this brightness
    float wetBreakup   = 0.06f;     // how much a coarse noise lowers the wet line's top (0 = even)
    // mc1: the foam fringe where the water visibly starts (lineDrop below the true waterline)
    float foamUp       = 0.024f;    // it reaches this far above its line...
    float foamDown     = 0.053f;    // ...and this far below it
    float foamStrength = 0.13f;     // its opacity
    float foamScale    = 1.13f;     // its breakup's cells per world unit
    // mc2: the waterline, the whole
    float wanderAmp    = 0.058f;    // how far the waterline wanders (height)
    float wanderScale  = 0.323f;    // the wander's cells per world unit
    float waterFade    = 0.148f;    // below the line the wet bank fades out over this depth (as Plain's wet bed rises)
    float opacity      = 1.0f;      // the whole strip (0 = Plain's look)
    // mc3
    float lineDrop     = 0.081f;    // the foam's line below the true waterline: where the water visibly starts
    float unused[3]    = {0.0f, 0.0f, 0.0f};
};
static_assert(sizeof(StripParams) == 64, "StripParams is the shader's four float4s");

// Fancy's contact foam (shore_contact.hlsl): foam where the water meets a model. Distances in world
// units.
struct ContactParams {
    // mc0
    float width        = 0.25f;     // a model within this of the water surface is at the waterline
    float strength     = 0.55f;     // the contact line's opacity (over the model just under the surface)
    float ringRadius   = 0.9f;      // how far the foam spreads out onto the open water
    float ringStrength = 0.45f;     // the spread foam's opacity
    // mc1
    float breakupScale = 1.6f;      // the breakup's cells per world unit
    float breakup      = 0.6f;      // how much of the foam the breakup removes (0 = solid)
    float opacity      = 1.0f;      // the whole contact pass (0 = none)
    float unused1      = 0.0f;
    // mc2: not a look value -- DrawShore fills them per draw from the context's view-projection
    // (DepthLinearize): view depth = depthMul / (depthAdd - z) for a 0..1 hardware depth z.
    float depthMul     = 0.0f;
    float depthAdd     = 0.0f;
    float unused2[2]   = {0.0f, 0.0f};
    // mc3: unused
    float unused[4]    = {0.0f, 0.0f, 0.0f, 0.0f};
};
static_assert(sizeof(ContactParams) == 64, "ContactParams is the shader's four float4s");

// The set the pass draws with: the defaults above, or (debug flavour with the tweak overlay) the
// overlay's live set.
ShoreParams CurrentParams();
StripParams CurrentStripParams();
ContactParams CurrentContactParams();

}  // namespace BetterWater
}  // namespace Ext
