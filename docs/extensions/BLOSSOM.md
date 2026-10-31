# BLOSSOM -- spring blossom and catkins, a part of natural tree growth (`ext.better_trees`, class A)

## 1. Goal and rulings

In spring each deciduous canopy flowers for a week or two on its own schedule: the fruit trees in white and
pink blossom, oak, birch, pecan and chestnut in catkins. It extends natural tree growth
(`NATURAL_GROWTH.md`): the same rewritten `BillboardMaterial` programs, the same per-tree hash and grove
terms, the same day stamp. No setting of its own: it is on whenever natural tree growth is.

Rulings (the user, 2026-10-31):
1. **Procedural, not a texture.** At play distance a flower is under a pixel; a cell-noise mask in the
   shader reads the same, needs no new texture slot on any backend and batches as before.
2. **Colours from reference photographs** of each species (section 2), not guesses.
3. **Modded trees** get a colour from a hash of their names, kept to a pale blossom palette.

Class A: visual only. Nothing in the sim reads it, nothing is saved. The faithful profile, the row off and
natural tree growth off are retail (the per-draw hook is null and the programs are not rewritten).

## 2. Reference (web research, 2026-10-31)

| Tree | What shows | Colour (sRGB, estimated from descriptions) | Peak (months from Early Spring) |
|---|---|---|---|
| Plum | white blossom, compact, all along bare branches, the first to flower | `#F4F2EC` | 0.40 |
| Cherry | white to light pink, whole branches covered; pink buds | `#F6E1E6` | 0.60 |
| Peach | pink to magenta, rarely white, on bare wood | `#E58AA8` | 0.60 |
| Pear | white, before or with the leaves | `#F2F1EA` | 0.70 |
| Apple | pink buds open white with a blush, clusters of 4-6, among fresh leaves | `#F5E4E6` | 1.10 |
| Birch | long hanging catkins, easy to see | `#A08A55` (density 0.25) | 0.90 |
| Oak | thin yellow-green catkins, sparse, barely visible at range | `#9A9A5A` (density 0.125) | 1.10 |
| Pecan | greenish-yellow catkins, the latest of the nuts | `#989458` (density 0.125) | 1.40 |
| Walnut | green catkins, close to invisible | `#7A864A` (density 1/16) | 1.00 |
| Chestnut | cream catkins, mid June to early July | `#C8BF8C` (density 0.28) | 3.20 |

The catkin rows were muted to about 70 % brightness, made less saturated and thinned by about 30 % after
the first in-game look (the user, 2026-10-31: the first values read as bright yellow sparks on the bare
trees). Each catkin's length and each cluster's brightness also vary per cell.

Sources: National Trust (blossom identification), specialtyproduce.com (apple blossom), Michigan State
University thesis on apple phenology (king bloom, clusters), saps.org.uk and treeguideuk (cherry plum first),
Suffolk BIS newsletter (Prunus sequence), bhg.com.au (pear with leaves), floweradvisor (peach colour),
Woodland Trust and Gardeners' World (catkins), UC ANR (walnut and pecan), Wikipedia (chestnut). The hex
values are estimates; the look pass may move them. The game's leaf regrowth starts at about 0.75 months,
so plum, cherry, peach and pear flower on bare or half-leafed trees, apple among leaves.

## 3. Design (as built)

`src/render/common/better_trees/blossom.h` is the reference; in short:

- **The species, per draw.** Every deciduous canopy shares one program, so the CPU tells the shader which
  species it is drawing. `Graphics::MaterialInstance::_extAfterSetup` (a null-by-default engine seam, set
  by `Ext::BetterTrees::Sync` while natural tree growth is on) runs after every material `Setup` and writes
  `gc.time.zw` (cb0[10].zw): `Context::SetTime` always writes 0 there and no retail program reads them.
  The code is the species' peak, density, flags (catkin, pink buds) and packed colour; every other draw
  gets 0 / 0 again. The global constants are re-uploaded only when the pair changes. Hooking `Setup`
  rather than `Renderer::DrawItemList` covers `ext.draw_batch`'s list walk too.
- **Which species.** `src/ext/better_trees/blossom_draw.cpp` joins the draw's names (the material instance's
  path and its textures' paths) and `Blossom::SpeciesFor` matches the table by substring; a canopy no row
  names gets a hashed pale colour (FNV-1a, the same on every platform). The answer is cached per material
  instance per thread; each new name set is logged once to stderr as `[NaturalGrowth] blossom: ...`.
- **The vertex shader** (`BillboardColour`, after the natural-growth terms) decodes the code and computes the
  tree's bloom from its hash and grove terms: about 0.6 month of flowering, full over the middle, offset
  per tree by up to +-0.25 month, wrapped across the year's end. Pink buds lower green and blue before the
  flower opens. A second new interpolant carries (r, g, b, coverage); coverage is negative for catkins.
- **The pixel shader**, before the season mad: a 32-cell grid over the card's UV; each cell holds a soft
  cluster with probability |coverage| at a hashed centre and radius (catkins: the cell narrowed and
  stretched into a streak). Where a cell is under ~2.5 pixels the mask blends to its average, so the far
  canopy is a haze rather than shimmer. A flower texel is exempt from the spring leaf-loss cut (a bare plum
  keeps its blossom on the leaf silhouette); after the discard the texel is lerped to the flower colour.
  The trunk (`move` 0) is untouched.
- **Backends.** Pure DXBC token edits in `natural_growth.cpp`, so DX11, Vulkan and Metal all get them; the
  pair rule (a rewritten pixel program only beside a rewritten vertex program) is natural growth's.

## 4. Checks

- `tools/build.sh unit`: the four `ext_blossom_*` tests run the rewritten synthetic programs in the test
  interpreter against the CPU mirror (`Blossom::VertexOut`, `Blossom::Pixel`): species and packing, the
  vertex terms, the curves (staggered, none in summer or winter, the year's wrap, pink buds), the pixel
  terms (alpha before the cut, colour, the trunk untouched, a bare tree keeping its flowers).
- DX11 debug, in game (2026-10-31, a local-only calendar spike, not in the tree): every model name matches
  a table row (`models\naturalresource\tree\materialinstance\{oak,birch}tree.rsc`,
  `models\naturalresource\fruittrees\materialinstance\<name>tree.rsc`); all four `Normal_ps` variants
  are rewritten and the cutout chains on each (`season mad yes`). Catkins hang vertically on the cards.
  Blossom was checked by drawing the oaks as plum (a new map has no fruit trees; orchards do): white
  clouds on the bare crowns, staggered per tree. The faithful gate run reached the world with no crash
  and no blossom line (the hook stays null).
- Open: an orchard in bloom, Vulkan (Metal on the owner's Mac), the resave check, a stress A/B.

## 5. Known limits

- The shadow pass is unchanged: a bare tree in flower casts no blossom shadow.
- The species table matches names by substring. If several fruit trees share one texture and material
  instance name, they share one species (the first row that matches).
