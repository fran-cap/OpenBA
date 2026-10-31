# NATURAL_GROWTH -- "Natural tree growth", a part of `ext.better_trees` (class A): staggered autumn colour, leaf fall and spring regrowth, the plan

## 1. Goal and rulings

Deciduous trees should change through the year the way a forest does: each tree turns, drops its leaves and
leafs out again on its own schedule, in a range of autumn colours, instead of the whole map changing in
lockstep in one gold tint. A second phase adds falling leaves.

Rulings (the user, 2026-10-31):
1. **Part of `ext.better_trees`, not its own row.** The rewrite runs on the same billboard `Normal_ps` family
   the cutout and the edge dither already rewrite; a second row would be a dependency chain between two
   extensions on one blob. The stage chains it with the other pixel rewrites, as BETTER-TREES.md §1 does for
   the dither.
2. **Settings name: "Natural tree growth"** (a checkbox under "Better trees" in the Options dialog).
3. **Stagger both ends of the year:** the autumn turn and leaf fall, and the spring regrowth.
4. **A slight per-tree variation of the greens** on the deciduous trees and the evergreens alike, minor enough to read as retail (3.4).

Class A: visual only. Nothing in the sim reads the per-tree state (it exists only in the shaders), nothing
is saved, the sim RNG is never touched. The faithful profile and the row off are retail byte for byte.

## 2. Facts (what retail does; file:line are the tree's on 2026-10-31)

**The season terms.** `WeatherSimulation` Part 4 (`src/game/Game/weather_sim.cpp:306-337`) computes four
floats each update, `field_0x1C` = (snowCoverage, fadeA, fadeB, activeWeatherIntensity). The render pipe
records them (`src/ext/render_pipe/render_pipe.cpp:245-248`) and they reach every shader as
`userPosition.xyzw`. With `doy = yearTime / 300 s` (one month = 300 s, 12 months, the year starts in Early
Spring):

| Term | Formula (weather_sim.cpp) | Over the year |
|---|---|---|
| `fadeA` = `userPosition.y`, the fall colour | `saturate((doy - 6) / 1.5)` | 0 until Early Fall, ramps to 1 by mid Mid Fall, stays 1 through winter, back to 0 at the year's wrap |
| `fadeB` = `userPosition.z`, the leaf loss | `1 - saturate(doy / 0.5)` before 0.5; 0 to 7.5; `saturate((doy - 7.5) * 2)` after | regrows over the first half of Early Spring; drops over the second half of Mid Fall; bare from Late Fall |

**Who reads them** (the mod kit's `.srsl`, a reference only, FOLIAGE_AA.md §2): `.x` (snow) is read by the
terrain, opaque-snow, sky and UI programs and `.w` by the sky; **`.y` and `.z` only by
`BillboardMaterial`** (the deciduous canopies, `BillboardMaterial.rsc`; `BillboardEvergreenMaterial` uses
the `psevergreen` entries, which ignore both). A mod's own material could read them too, so the CPU side
never changes what `userPosition` carries.

**What the billboard programs do with them.**
- `Normal_ps` (colour): the colour lerps from the texel's own colour toward a fixed gold tint scaled by the
  texel's squared brightness, by `userPosition.y * move`; the alpha cutoff is `(a - move * userPosition.z) -
  0.5` (FOLIAGE_AA §2's `(a - season*move) - 0.5`). `move` (`lightFogMove.w`, from the vertex shader) is 1
  on the swaying leaf-card vertices, 0 on the trunk.
- `Depth_ps` (the shadow caster): the vertex shader passes `userPosition.z * move` in `seasonData.x`; the
  pixel shader subtracts it from the alpha before the 0.5 cut. `seasonData.y` is written with the same value
  and **read by nothing** (a free interpolant component in the depth pass; to be confirmed in the DXBC, S2).
- The canopies are **instanced**: the vertex shader takes the tree's transform from
  `tc.transforms[instance]`; its translation is the tree's world origin, the same for every card of the
  tree. That is the per-tree key (BbsRewrite's card centre C is per VERTEX, TS-HASH, so it is not).

So retail already goes green -> gold -> bare -> green, but every tree on the map at the same moment, in one
colour, the drop in about two weeks of game time, with no falling leaves.

**The existing machinery.** `src/render/common/better_trees/` holds the pure DXBC rewriters
(`cutout_rewrite`, `receiver_rewrite`, `edge_dither`, the container / token code in `dxbc.{h,cpp}`), unit
tested on synthetic blobs (`tests/unit/ext_better_trees_test.cpp`). DX11 (`src/render/dx11/better_trees/`),
Vulkan (`src/render/vulkan/shader_rewrites.{h,cpp}`, through the DXBC -> SPIR-V converter) and Metal
(`src/render/metal/passes/tree_passes.mm`) all run the same transforms, so one rewrite covers the three
backends. Nothing in `dxbc.h` edits an input / output signature yet (`ISGN` / `OSGN`): every rewrite so far
reused an existing register (the dither reads the shadow output's spare `.w`).

## 3. Design

The plan as written before the spike. Phase 1 is built; where the build differs (the day goes in
`gc.time.y`, the hash, the pines' interpolant, the chain order, the default), §6 is the record.

### 3.1 The per-tree terms (vertex shaders)

In both billboard vertex shaders (`Normal_vs`, `Depth_vs`), after the instance transform is read:
- `T` = the tree's world origin (the translation of `tc.transforms[instance]`).
- `h1, h2` = two independent hashes of `T.xy` in [0, 1) (the `frc(dot(.., K))` family BbsRewrite uses, with
  different K), and `g` = a low-frequency term of `T.xy` (a smooth value noise, a few hundred metres per
  cell), so groves on a slope turn a little together rather than as pure salt and pepper.
- The tree's **offset** `o = sigma * (0.6 * (2 h1 - 1) + 0.4 * (2 g - 1))`, in months; its **hue**
  `hue = h2`.
- From `doy` (3.3) and `o`, the tree's own colour amount `c` and leaf loss `l` (3.2); `Normal_vs` passes `c,
  l, hue` to the pixel shader (one new interpolant, 3.4); `Depth_vs` passes `l` in `seasonData.x` (and
  `.y`), so the shadow thins with the visible canopy exactly as in retail.

### 3.2 The curves (starting values; the look pass, S7, moves them)

Retail's windows widened and shifted per tree (`sigma` ~ 0.75 month). `l` uses retail's shape on every
tree; only the timing differs.

| Phase | Retail (whole map) | Per tree (start, length in months, + o) |
|---|---|---|
| colour turn | 6.0, 1.5 | 6.0 + o, 1.25 |
| leaf fall | 7.5, 0.5 | 7.25 + o, 0.75 (the last-turning trees are bare by mid Late Fall) |
| regrowth | 0.0, 0.5 | 0.0 + o_s, 0.75, with `o_s = sigma_s * (2 h1 - 1)` folded to `[0, 1.5]` (Early Spring to mid Mid Spring) |

The colour amount resets to 0 at the year's wrap, as retail's does; a tree that has not regrown yet is
bare, so the reset is never seen. The hash `h1` drives both ends, so an early-turning tree is not always an
early leafer-out (`o_s` folds it); the user's look check decides whether the two should correlate.

### 3.3 Getting `doy` into the shaders

Retail hands the shaders only `fadeA` / `fadeB`, which are flat outside their windows: `doy` cannot be
rebuilt from them, and a per-tree window wider than retail's needs `doy` itself. Candidates, in order (S3
decides):
1. **A spare constant in a cbuffer the billboard programs already bind** (a padding component in
   `VSConstants` / the global cbuffer that the engine writes as 0 and no program reads). The engine side
   writes `doy` there each frame while the part is on. Least plumbing if one exists.
2. **One extra cbuffer at a free slot**, bound by the rewrite stage of each backend when it binds a
   rewritten billboard program (each backend's stage already does per-bind work: the A2C clone on DX11 /
   Metal, the pipeline-key bits on Vulkan).
3. Never: changing what `userPosition.y/z` carry (§2: a mod's material may read them, and a blob the matcher
   refuses would then draw the wrong season).

### 3.4 The colour (pixel shader)

`Normal_ps`'s colour lerp is replaced: the target is a palette colour picked by `hue` (gold, orange, red,
russet; a few deciduous species keep to yellows, if the species can be told apart: S5), keeping retail's
"scaled by the texel's squared brightness" so the leaf texture's detail survives, and a late browning as `l`
rises (the leaves dull before they drop). The lerp amount is `c * move` and the cutoff `(a - move * l) -
0.5`, so with `c = fadeA`, `l = fadeB` and the gold target the program is retail's again (a unit test).

**The green variation (the user, 2026-10-31: "a slight difference in greens on both the leaf and
evergreens... just minor so it looks retail-ish").** All year, every tree's leaf colour gets a small
per-tree tint from a third hash `h3`: a few percent of brightness and a slight shift between a yellower
and a bluer green, scaled so the leaf texture's own variation stays larger than the tree-to-tree one. On
the deciduous canopies it applies under the autumn palette (the autumn lerp starts from the tinted green).
On the evergreens it is the only change: the `psevergreen` billboard entry and whichever conifer programs
S1 finds (expected `OpaqueMaterial` `Foliage` / `Snow_alphatest`, instanced the same way), each with the
per-tree hash in its vertex shader. Where a program has no free interpolant, the tint is applied in the
vertex shader to an existing colour-carrying output if the program has one (S2), else that program is left
retail.

The new interpolant needs **signature editing** in `dxbc.h`: append one element to the vertex shader's
`OSGN` and the pixel shader's `ISGN` (the next free register, `TEXCOORD` semantic with the next free index),
the matching `dcl_output` / `dcl_input_ps linear`, and the chunk / container fix-ups. Both blobs of a pair
must agree; the stage only binds the rewritten pixel shader with the rewritten vertex shader (the Vulkan and
Metal converters link by signature: S4 confirms both accept the appended element). DETAIL0 has no shadow
interpolant, so its next free register differs from DETAIL1..3: the rewriter reads it from the signature.

### 3.5 Chaining in the stage

The pixel side runs on the blob the cutout and the dither rewrite: the order is natural growth (it replaces
the colour lerp and the season term in the cutoff) -> dither -> cutout, so the cutout matcher still sees its
grammar (`sample` -> one season `mad` -> `add` -> `lt` -> `discard_nz`; the natural-growth rewrite keeps a
single `mad` feeding the add, now reading `l` from the interpolant). The vertex side runs after
`receiver_rewrite` on the same blob (`Normal_vs`), and alone on `Depth_vs`. A blob the matcher refuses stays
retail, and so does its partner (never a rewritten VS with a retail PS or the reverse).

### 3.6 Settings

A valued key `ext.natural_tree_growth` (0 / 1, owner `better_trees`, `src/ext/registry.cpp` beside
`foliage_aa_mode`), shown as "Natural tree growth" under the "Better trees" combo
(`src/ext/better_trees/enhanced_entry.cpp`), greyed while the row is off. `BetterTrees::Params` gains
`naturalGrowth`; `Sync()` hands it to the three backends' requests with the rest. The default (on whenever
the row is on, or off) is the user's ruling after the look pass.

### 3.7 Phase 2: falling leaves

During each tree's own leaf-fall window (`l` between ~0.1 and ~0.9), leaf cards tumble from its crown.
- Render side only, built on the weather particle path (`src/game/Game/weather_particles.cpp`,
  `weather_particle_draw.cpp`): no entities, no sim RNG (a hash of the tree id and the frame time), nothing
  saved: it stays class A.
- Emitted only for trees in the view and near the camera (a budget, ~a few thousand leaves on screen), so
  the overview zoom costs nothing.
- The tree list and the same `o` / `hue` computed on the CPU from the tree's position (one shared header of
  the hash and the curves, unit tested to match the shader's constants).
- The sprite is a procedural leaf shape (our own content, rule 7), tinted with the tree's palette colour;
  wind from the weather sim's wind term, if one is exposed.
- Optional: a ground-litter tint on the terrain under bare trees through Late Fall (a separate question; the
  terrain shader is not a better_trees program).
Phase 2 is built: §7.

## 4. The spike (before any rewrite code; nothing retail enters the tree)

The season route is FAA-SPIKE's: a spike-only switch writes `Calendar::yearTime` once on the first in-game
frame (Early Fall 1800 s, mid Mid Fall 2310 s, Late Fall 2550 s, Early Spring 60 s, Summer 1200 s). The
blob dump writes `fxc /dumpbin` input to `C:/GOG Games/Banished/rdoc_natural/blobs/`, outside the tree.
Neither the switch nor the dump is kept.

| # | Question | Decides | Evidence |
|---|---|---|---|
| S1 | Captures in mid Mid Fall and Early Spring, street level and overview: which programs draw the deciduous canopies (expected: the `BillboardMaterial` `Normal` / `Depth` pairs only), and does any other program change with `userPosition.y/z`? | the match list | two `.rdc` in the game dir + the summariser output |
| S2 | In every DETAIL0..3 variant of `Normal_ps`, `Normal_vs`, `Depth_vs`, `Depth_ps`: the exact colour-lerp sequence and its registers; where `tc.transforms[instance]` is read (the translation components); is `seasonData.y` really unread? | the matchers' grammar | `fxc /dumpbin` of the dumped blobs (kept outside the tree), a prose summary here |
| S3 | Is there a spare constant the billboard programs already bind (3.3 option 1)? The cbuffer declarations in the blobs against the engine's layouts (`src/engine/video/constant_buffer.cpp`, `shader_constants.inc`). | where `doy` goes | the declared sizes and the engine's written ranges |
| S4 | Does an appended signature element survive the DX11 runtime, the Vulkan DXBC -> SPIR-V converter and Metal's airconv, with the cutout + dither rewrites chained on the same blob? | 3.4's mechanism | a hand-patched pair drawing a constant hue on each backend (Metal on the owner's Mac) |
| S5 | Can the deciduous species be told apart in the shader (one `BillboardMaterial` per species with a different texture? an instance field?), or is the palette one set for all? | 3.4's palette | the `.rsc` list of deciduous trees and their materials |
| S6 | Cost: one extra interpolant and ~30 ALU on the canopy pixel shader, measured on the stress town | go / no-go | `tools/build.sh stress`, clean machine (perf/README.md) |
| S7 | The look: the user, by hand, all four seasons, street and overview, with the cutout and the dither on | the curves and the palette (3.2, 3.4) and the default (3.6) | screenshots outside the tree |

## 5. Work order

| Block | Who | What | Done when |
|---|---|---|---|
| NG-SPIKE | done | S1-S5 (S6 not run) | §6 |
| NG-CORE | done | signature editing in `dxbc.{h,cpp}`, the `natgrowth_rewrite.{h,cpp}` VS + PS rewriters, the `doy` plumbing (S3's answer), the stage chaining on DX11 / Vulkan / Metal, the settings key and the Options checkbox, unit tests (the identity case of 3.4, the curves at the window edges, the signature round trip) | `tools/build.sh unit`, the gate, the resave check, the `[BetterTrees]` match line counting every variant |
| NG-LOOK | the user | S7 | the ruling on curves, palette and default |
| NG-LEAVES | done | phase 2 (3.7) | §7, then the look check |

## 6. As built (phase 1)

### 6.1 Spike answers

| # | Answer |
|---|---|
| S1 | The deciduous canopies (Birch, Oak, the eight fruit trees) draw with `BillboardMaterial`'s `Normal` / `Depth` programs, and Pine with `FoliageMaterial`. Blueberry uses `BillboardEvergreen` (`psevergreen`), which has no season term. That program is refused and stays retail, so it gets no green variation (`no season mad after the sample` in the log). |
| S2 | All four DETAIL variants match: `Normal_ps` 1d0afbb6 / d5503f8c / 1935b5ef / a437d0d2, `Normal_vs` a12e2a30 (v0) / a88a6da1 (v1-3), `Depth_vs` fa28d234, `Foliage_vs` 0c7bf19e / a62bf119, `Foliage_ps` bc55ff72 / d384ccbf / 97870cc6 / f299fbbb. `FoliageDepth_vs` b64eab92 is refused, so the pines' shadows are retail (they have no season). The translation is `cb3[rI + 3]`. The fall colour is `mul rA.w, move, pc.userPosition.y` feeding a `mad` toward the gold immediate. |
| S3 | `gc.time.y` (cb0[10].y). `Context::SetTime` always writes 0 there, and no retail program reads it. While the part is on, the game writes `1 + months` after every SetTime (`gameupdateloop.cpp` Draw step 2, and the render pipe's replay). A missing stamp (0) gives retail's fadeA / fadeB and gold, which is what the menu scene shows. |
| S4 | An appended TEXCOORD element survives DX11, and the Vulkan DXBC -> SPIR-V path on this machine (all 12 rewrites, nothing FAILED). On Metal, airconv has not been run: CI builds the code, and it still needs a run on the owner's Mac. |
| S5 | No. One palette for all species: the hash picks gold / orange / red per tree. |
| S6 | Not run (no stress A/B yet). The cost is one interpolant on the colour pair, about 40 vertex ALU (the hash, the grove, the curves) and about 15 pixel ALU. |
| S7 | Open: the user's look pass. |

### 6.2 The rewrite (`src/render/common/better_trees/natural_growth.{h,cpp}`)

The header comment is the reference. In short:
- Per tree, in the vertex shader, from the translation `T`:
  - `h = hash33(T.xy)` (Dave Hoskins), and a grove term `g` from two sines.
  - Autumn offset `o = 0.75 * (1.2 h.x - 0.6 + 0.2 g)`.
  - Regrowth start `s = 0.75 + 0.75 * (1.2 frc(h.x + h.z) - 0.6 + 0.2 g)`.
  - Colour `c = sat((d - o - 6) / 1.25)`.
  - Loss `l = max(sat((d - o - 7.25) / 0.75), 1 - sat((d - s) / 0.75))`.
  - Tint `t = 2 h.z - 1`.
- The billboard colour pair carries `(c, l, hue = h.y, t)` in a new interpolant.
- The depth VS uses `l` for the shadow caster's leaf loss.
- The pines carry `t` in the snow interpolant's unused `w`.
- Palette: gold (retail's 233,185,71) -> orange -> red by hue, browned up to 50 % by `l`.
- Green variation: `colour *= 1 + t * move * (0.08, 0.05, -0.05)` after the discard. On the pines it applies without `move`, at about three times the strength: `(0.20, 0.15, -0.02)`, since `(0.08, 0.05, -0.05)` was not visible on the dark needles.
- The CPU mirror (`TermsOf` / `SeasonOf`) is unit tested against an interpreter run of synthetic blobs (`tests/unit/ext_natural_growth_test.cpp`). Phase 2 uses the same terms.

### 6.3 The stages

The chain is natural growth first, then the cutout, then the dither (pixel), and the receiver lookup (vertex).
- DX11: `billboard_shadow_stage` / `foliage_aa_stage`.
- Vulkan: `shader_rewrites.cpp`.
- Metal: `passes/tree_passes.mm`.

A natural-growth pixel copy reads an interpolant that only a natural-growth vertex copy writes. Each backend therefore binds it only beside one:
- DX11: `FaaStage::VertexPairChanged`.
- Vulkan: `ShaderEntry::naturalGrowth` in `draw.cpp`.
- Metal: `BlendGuard` swaps back to retail.

### 6.4 Settings

`ext.natural_tree_growth` (0 / 1, default 1, owner `better_trees`) is shown as "Natural tree growth" in the Enhanced options. It takes effect only while `ext.better_trees` is on. The pixel half needs a foliage mode >= 1. The vertex half follows the row. The default is provisional until S7.

### 6.5 Checks run (2026-10-31)

- `tools/build.sh unit`: the natural-growth tests pass. `ext_sheet_kit_crosscheck` fails locally against a stale kit output (CI skips it without the kit).
- DX11 debug: in-game screenshots (in the scratchpad, not the tree) at Early Fall, mid Mid Fall, Late Fall, Early Spring and Summer. Every rewrite applied.
- Vulkan debug: mid Mid Fall, all 12 rewrites. The `SV_Target`-without-attachment validation warning is the same with the part off.
- Metal: compiled by CI only.
- The gate (`tools/build.sh run`, faithful): RUNNING, no crash.
- `tools/resave_gate/resave_gate.sh Gate`: PASS.

## 7. Phase 2: the falling leaves (as built)

While natural tree growth is on, deciduous trees near the view drop leaves during their own autumn leaf fall.
Nothing new is shipped or rewritten.
- The leaves are our own procedural atlas.
- The program and blend state are the user's installed snow weather material (`Material/Weather/WeatherMaterial.rsc`).
- It is render only (class A): nothing is saved, and the sim never sees it.

**Files.**
- `src/ext/better_trees/leaf_fall.{h,cpp}`: the pure part, unit tested.
- `src/ext/better_trees/falling_leaves.{h,cpp}`: the exe part.
- Two seams, both in `GameUpdateLoop` (with `ext.render_pipe`'s MainHalf twin of the first):
  - `LeavesFrame`: Draw step 2, beside the season stamp.
  - `LeavesShutdown`: Shutdown's removal of the global scene objects.
- `Ext::Resources::BuildSheetObjects` gained an overload that takes a base material the caller already holds.

**The atlas.**
- 128 x 128, with 4 leaf shapes (oval, lance, lobed, round) in 4 colours (gold, orange, red, brown).
- Painted at load with 4 x 4 supersampled edges and a darker midrib.
- The colour is filled across the whole cell, so bilinear filtering leaves no dark fringe.
- It is bound in the snow material's `diffuse` slot.
- The weather program reads its corner texture coordinates as UNORM8, so a card picks its cell in 1/255 steps. The cells keep a clear 2-texel margin.
- The weather program is unlit and outputs the texture's colour as is, which is why the colours sit below the canopy palette.

**The trees.**
- The map's occupants are scanned a sixteenth per frame. Every tree species shares one description, `Template/NaturalResourceTree.rsc`, so a tree counts as deciduous by its mesh: its first material instance is a billboard material that is not the evergreen one.
- Each pass keeps every tree's position and world box. It holds no pointers.
- A tree drops leaves when it is within 45 units of where the camera looks (the view ray at ground height) and the camera is under 110 units above it.

**The timing.**
- A tree's drop rate is `0.1 + 1.6 l (1 - l)` leaves a second, where `l` is its own autumn leaf loss. `l` is the autumn half of `SeasonOf`, the canopy shader's own terms.
- Leaves fall only while that tree's canopy thins.
- There is a cap of 2400 leaves. Spawning starts from a random tree each frame, so a capped frame favours no part of the view.

**The motion.**
- Leaves spawn on the crown's outer edge, in its lower 35-70 %.
- Each leaf falls at 0.7-1.2 units a second, modulated by its flutter.
- It also has a side-to-side flutter, a drift, a spin about the view axis and a tumble (the card squashed by `|cos|`).
- It is gone at the tree's foot.
- The leaves move in real time while the game clock runs. They freeze when the clock has not moved for 0.25 s (paused).

**The draw.**
- `LeavesGeometry` is a `Graphics::Geometry` in the scene's global list. Its draw item is WeatherParticleGeometry's own.
- It draws one instance at the origin, with the cards' world positions in the dynamic vertex stream.
- Visibility runs to 70-90 units of depth, plus the near fade.
- The main thread builds the cards. `Draw` copies the last finished set under a lock, so it is safe on the render pipe's thread, and records the camera.
- Outside months 6-9.5 the geometry leaves the scene, so the rest of the year draws exactly as without the part.
- A frame holding it is one the render pipe's snapshot does not cover, so that frame keeps the fence.

**Checked (2026-10-31, DX11 debug, the spike-only season switch):**
- 9-11 k deciduous trees on a new map, and about 250-450 dropping near a town view.
- About 200 leaves at play distance (2,400 against the cap before the rate and radius were lowered).
- Quit to the main menu mid-fall: clean, with an empty crash log.
- The Vulkan build compiles.
- No leaves run has been done on Vulkan or Metal. The draw uses only `Video::Context` calls the weather already makes.
- The look (size, colours, rate) is the user's ruling.
