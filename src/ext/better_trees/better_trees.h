// ============================================================================
// src/ext/better_trees/better_trees.h -- ext.better_trees (class A, default OFF)
// ============================================================================
// NOT RETAIL. docs/extensions/handins/BETTER-TREES.md (this row); the history of its two
// halves: docs/extensions/FOLIAGE_AA.md (smooth edges, §6 the spike's rulings),
// docs/extensions/TREE_SHADOW.md (the shading diagnosis and design).
//
// One row for the tree look; it replaces ext.foliage_aa and ext.billboard_shadow.
//
// Smooth alpha-tested edges (leaves, needles, twigs): the six retail cutout
// programs (x 4 DETAIL variants) are rewritten at load, in memory, from the
// user's own installed bytecode (src/render/dx11/better_trees/): the discard
// becomes a sharpened alpha written to o0.w and drawn with alpha to coverage
// (MSAA on); mode 2 also scales the alpha up with the mip level (the distance
// fill). With MSAA off the discard stays and mode 2 keeps only the fill.
//
// Stable tree shading: the deciduous canopies and the blueberry bushes are camera-facing
// leaf cards; retail casts their shadow from sun-facing cards but looks it up from the
// camera-facing ones, so the canopy shows a line that turns with the view. With the row on,
// the billboard colour vertex shader (src/render/dx11/better_trees/) looks the shadow up
// at the card centre's depth moved toward the sun, and the cut is dappled per card and per
// texel (BbsRewrite::Params: basis / margin / jitter / noise / noiseScale).
//
// Settings: the bool row `ext.better_trees` (the Extensions dialog's checkbox) and the
// valued key `ext.foliage_aa_mode` (1..2, default 2; registry.cpp, owner better_trees). The
// Options dialog's combo is Off / On / On + distance fill = Current().mode 0 / 1 / 2,
// written through Set(). Mode 0 = the row off, which also turns the shading half off.
//
// Natural tree growth (docs/extensions/NATURAL_GROWTH.md): the valued key `ext.natural_tree_growth`
// (0..1, default 1, owner better_trees; the Options dialog's "Natural tree growth" combo Off / On).
// While the row is on and the key is 1, the billboard and foliage programs also carry the per-tree
// autumn colour, leaf fall, spring regrowth and green variation (natural_growth.h), and the game
// writes SeasonStamp into the shaders' spare time.y after every SetTime. In spring the canopies also
// flower (docs/extensions/BLOSSOM.md): each draw's species code goes in time.zw (SyncBlossom).
//
//   Sync()   boot (Ext::InitFromSettings), every Extensions-dialog toggle
//            (Ext::SetAndRecord) and every Set(): hands the mode, k and the dapple
//            parameters to the render side (FaaBackend::Request, BbsBackend::Request).
//            A mode / k / parameter change re-creates the transformed shaders (they are
//            immediates in the emitted code).
// ============================================================================
#pragma once

namespace Ext {
namespace BetterTrees {

enum Mode : int {
    kOff          = 0,   // the row is off (retail)
    kSmoothEdges  = 1,   // part 1: alpha to coverage + the sharpened alpha
    kDistanceFill = 2,   // parts 1 + 2: + alpha *= 1 + max(0, lod) * k
};

// Part 2's k (FOLIAGE_AA §3 "from ~0.25"; FAA-TUNE row 3 moved it to 0.15: at
// the overview zoom the canopy's covered samples are +6.3% over the mip-0
// reference at 0.15, +8.0% at 0.25, +9.6% at 0.40; smooth edges alone already
// +1.2%. The user's look ruling may move it). Baked into the emitted code as an
// immediate.
constexpr float kDistanceFillK = 0.15f;

// The fill floor (FAA-TUNE, FOLIAGE_AA §6.5; handins/FAA-TUNE.md row 1): where
// the sampled alpha is under this, mode 2 neither fills nor covers, so a low-alpha
// texel (whose filtered RGB is not leaf colour: the blue-grey speckles) is never
// pulled into coverage. Measured: under 0.15 the covered samples are 41-83%
// blue-over-green against 3% for the leaf; the floor costs 0.4% of the canopy's
// covered samples at the overview. Baked into the emitted code beside k.
constexpr float kDistanceFillFloor = 0.15f;

struct Params {
    int  mode          = kOff;    // 0 = the row is off, else ext.foliage_aa_mode (1..2)
    bool naturalGrowth = false;   // the row is on and ext.natural_tree_growth is 1
};

// The live value: mode = row on ? ext.foliage_aa_mode : 0. Cheap; never read
// per draw (the render side holds what Sync last handed it).
const Params& Current();

// The menu's write : mode <= 0 turns the row off (the key keeps its
// stored mode); mode >= 1 stores clamp(mode, 1, 2) and turns the row on. Records
// the settings keys (the caller flushes, Ext::PersistSettings or retail's
// WriteSettings), then Sync()s. `naturalGrowth` is stored with the mode, only when
// mode >= 1 (the key keeps its value while the row is off).
void Set(const Params& p);

// GameUpdateLoop::Draw / the render pipe, after SetTime: the value for Context::_time.y,
// kStampBase + the months into the year (natural_growth.h "THE DAY"), or 0 while natural
// growth is off (retail's 0: nothing is written).
float SeasonStamp(float yearSeconds);
// Current().naturalGrowth, for the two seams (they read the year clock only while it is on).
bool NaturalGrowthOn();

void Sync();

// blossom_draw.cpp (docs/extensions/BLOSSOM.md): arms Graphics::MaterialInstance::_extAfterSetup, the
// per-draw blossom code in gc.time.zw, while natural tree growth is on; clears it otherwise. Sync calls it.
void SyncBlossom(bool on);

}  // namespace BetterTrees
}  // namespace Ext
