# BETTER-TREES -- one row for the tree rewrites, and the per-texel shadow dapple (2026-10-17)

> (moved to the DESIGN.md §3 names 2026-10-20, block ext-standard: `src/render/dx11/billboard_shadow/` and `src/render/dx11/foliage_aa/` moved to `src/render/dx11/better_trees/` (file names kept); `tests/unit/ext_billboard_shadow_test.cpp` and `ext_foliage_aa_test.cpp` merged into `ext_better_trees_test.cpp`, cases `ext_bbs_*` / `ext_faa_*` renamed `ext_better_trees_*`.)

The user, after TS-HASH (TS-CORE.md: the "per-vertex uv hash" follow-up found moot): "combine both into a
'better trees' extension". Ruled in the same breath: one row, build the noise too.

## 1. What `ext.better_trees` is

One class A row (default OFF, Enhanced tab combo "Better trees": Off / On / On + distance fill) that owns
the three tree-only shader rewrites, all made at load from the user's own installed bytecode:

| Part | Was | Where | What it does |
|---|---|---|---|
| the cutout | `ext.foliage_aa` (+ `ext.foliage_aa_mode`) | `src/render/dx11/foliage_aa/cutout_rewrite` | alpha to coverage + sharpened alpha on the six leaf / needle cutout pixel programs; mode 2 adds the distance fill (FOLIAGE_AA.md, FAA-CORE.md) |
| the receiver lookup | `ext.billboard_shadow` | `src/render/dx11/billboard_shadow/receiver_rewrite` | the billboard colour VERTEX shader's shadow lookup moved per vertex (margin -0.05, jitter 0.4, the user's ruling; TREE_SHADOW.md, TS-CORE.md) |
| the edge dither | new | `src/render/dx11/billboard_shadow/edge_dither` | per-texel noise on the billboard PIXEL shader's shadow compare depth, amplitude = the card's half extent (below) |
| natural tree growth | new (2026-10-31) | `src/render/common/better_trees/natural_growth` | per-tree autumn colour, leaf fall, spring regrowth and green variation on the billboard and pine programs; valued key `ext.natural_tree_growth` (0..1, default 1) (NATURAL_GROWTH.md) |

The valued key `ext.foliage_aa_mode` keeps its name and range (1..2, default 2); its owner is now `better_trees`.
`ext.max_shadow_res` stays its own row (not tree-specific).

**Settings migration** (`Ext::InitFromSettings`, registry_boot.cpp): with no `ext.better_trees` key in
settings.cfg, `ext.foliage_aa=1` or `ext.billboard_shadow=1` turns the new row on and records the new key at
the next write; the old keys are left in the file (the registry never asks for them again). Under a profile
nothing is migrated (a profile decides every row).

**Why one row.** The dither has to run on the same pixel shaders the cutout rewrites (the billboard
`Normal_ps` family), so the two pixel rewrites are chained on one blob inside one stage. TREE_SHADOW.md §3's
"foliage_aa and billboard_shadow share no blob" was true of the vertex-only row and is retired with it: the
one row owns both seams (`VideoDX11::g_extFoliageAA` for the pixel stage, `VideoDX11::g_extBillboardShadow`
for the vertex stage), both null while the row is off.

## 2. The edge dither (why the pixel shader, and how)

TS-HASH showed the vertex stage cannot bend a cut inside a card: four corner offsets interpolate to a line
(or a shallow hyperbola) across a planar card, and on a large card the depth slope across it dominates any
corner spread, so a long straight self-shadow edge always remains. A per-TEXEL term lives in the pixel shader.

**The vertex side** (`receiver_rewrite.{h,cpp}` "THE EXTENT"): after the shadow output's last write, four
instructions set `o3.w = -(r * |lc.shadowProjection[0] row 2 .xyz|)` -- the card's half extent r (already
computed for the lookup) in depth units (the z row's length is the depth per world unit, 0.00392 at the
retail fit), NEGATIVE so a retail vertex shader (whose w is +1, orthographic) gives the pixel side an
amplitude of 0. Retail's pixel shaders never read that w (`dcl_input_ps linear v3.xyz`, every variant).

**The pixel side** (`edge_dither.{h,cpp}`, namespace `BbsDither`): a ps_4_x program whose `sample_c` /
`sample_c_lz` instructions take their compare value straight from one input register S (retail: `v3.z`, nine
PCF taps, some through temp offsets of `v3`) and whose first plain `sample` reads its coordinates from an
input U (the diffuse texcoord, `v1.xy`). Emitted before the first instruction, in two new temps T and N:

    mov T, vS
    mul N.xy, vU.xy, noiseScale ; round_ni N.xy ; dp2 N.x, N.xy, (12.9898, 78.233)
    sincos N.x, null, N.x ; mul N.x, N.x, 43758.5453 ; frc N.x ; add N.x, N.x, -0.5      h - 0.5
    max N.y, -vS.w, 0 ; mul N.x, N.x, N.y ; mad T.z, N.x, noise, T.z

then every read of `vS` in the program is pointed at T (so every tap, direct or offset, compares against
the dithered depth). `dcl_input_ps vS.xyz` becomes `.xyzw`, and the ISGN element of S gets its read mask
widened to xyzw (the signature chunk is patched in place: 24-byte elements, byte 21 is the read mask).
11 instructions, `dcl_temps` + 2. The noise is a hash of the texcoord quantised to cells of
1 / noiseScale uv: attached to the card, so the pattern is world-stable (no swimming with the camera);
noiseScale 64 is 8 texels of a 512 atlas. `noise` is in half extents like margin and jitter.

**Chaining** (`foliage_aa_stage.cpp` Build): per blob, the cutout where it matches (its output is the next
input), then the dither where THAT matches and the noise is not 0 -- on the cutout's output or on the retail
blob when the cutout did not apply (mode 1 without MSAA, or a non-cutout program). One
`CreatePixelShader` when either ran. The dither parameters are part of the stage's request
(`FaaBackend::Request(mode, k, floor, &params)`): a change bumps the generation and the copies are re-made
through the existing reset path. Log lines: `[FoliageAA] dithered <hash> v<n> gen <g>: <B> -> <B> (shadow v3.z,
uv v1, noise <n> scale <s>, after the cutout)`, `not dithered ... : <why>` once per blob for a program that
has a `sample_c` but did not match, `dither of ... FAILED its self-check`.

**Shipped: noise 0.05, scale 32** (`BbsRewrite::kDefaultParams{0, -0.05, 0.4, 0.05, 32}`, the user's ruling by eye
with the sliders, 2026-10-17: "0.05 noise and 32 scale looks best"; the block shipped at noise 0 until then). The overlay's "Billboard shadow" panel (debug, Alt+F10) has `noise` 0..1.5 and `noise
scale` 8..256, applied on release; "retail (0/0/0/0)" = `kRetailParams`.

## 3. Refusals

Vertex (unchanged from TS-DAPPLE, plus): the extent goes after the shadow output's last write, whatever it is.
Pixel: not a pixel shader / not SM4; flow control (a discard is NOT flow control: the cutout programs
discard); no `sample_c` (the depth variants: not a candidate, no log line); no `sample_c` whose compare value
is a plain input select, or two different ones; no `sample` reading its coordinates from an input; the
texcoord and the shadow input being one register; `ret` not the one last instruction; no plain
`dcl_input_ps` for S. The transform refuses (nothing used) when the ISGN chunk is absent or malformed or lists
no element for S, when nothing was retargeted, or when the copy fails its re-parse / checksum / shape check.

## 4. Evidence

Unit (`tools/build.sh unit`): **1241 tests, 0 failures**. `ext_bbs_match_and_meaning` now also checks
`o3.w = -r` (identity shadow matrix). New: `ext_bbs_dither_match_and_meaning` (a synthetic colour program
with a direct tap and a tap through a temp copy: both see `z + (h - 0.5) noise max(-w, 0)` in the
interpreter's float order over 6 seeds, w = +1 leaves z alone; the copy declares and signs v3 xyzw, temps + 2,
is not rewritten twice) and `ext_bbs_dither_refusals` (a depth variant, no direct compare, no input uv, flow
control, a bad ISGN, a vertex program).

**Look arms** (debug exe, the user's own settings.cfg with everything on -- the migration ran live:
`[Ext] migrated ext.foliage_aa / ext.billboard_shadow -> ext.better_trees=1` -- `GameShowWeather=false` for the
arms, seed 12345, `town,25,<yaw>`, speed 10, screenshot at frame 12000, the window focused so the pin runs;
`rdoc_tree/bt_n0_y0.png`, `bt_n0_y90.png` (noise 0 = the ruled look, the shipped default) and `bt_n05_y0.png`,
`bt_n05_y90.png` (a look-only exe with noise 0.5 / scale 64 as the default; not committed); 1:1 crops of the
same crowns in **`rdoc_tree/BT_noise_sheet.png`**). At noise 0.5 the straight cuts inside the large birch
cards (yaw 90, left column) are gone: each cut is a jagged, leaf-cluster-scale edge, and the crowns read as a
fine mottle. The 8-texel cells are visible as a square grain at this zoom; a finer `noise scale` (128..256) is
the obvious next thing to try with the slider. **The user's ruling (2026-10-17, in game with the sliders): noise
0.05, scale 32** -- a faint dapple at a finer cell; locked in as `kDefaultParams`. SESSION CLOSED.

**The first look arm broke the frame** (`bt_n05_y0` of the un-guarded build: terrain and houses black, smoke
white): the dither then matched EVERY lit pixel shader (66 blobs: terrain, opaque, water...), and two things
followed -- their shadow interpolant's w is not an extent (a packed term, so the amplitude was garbage), and
every dithered program got the A2C blend clone (`Apply`: a transformed copy under coverage), so the terrain
drew with coverage 0. Hence the three guards of edge_dither.h: the cutout programs only (m.colour; 18 blobs
dithered, every one "after the cutout"), the ISGN element of S must say the vertex stage writes w, and -w is
taken only in (0, 0.1). A second broken run after the guards was a deploy mistake: `tools/build.sh unit` does
not relink the game exe, so the guarded code had not reached the deployed exe (`build` first, then deploy).

**Not dithered: the DETAIL v2 variants** (`not dithered <hash> v2: no sample_c compares an input directly`,
six programs): that variant computes its compare depth in a temp first. The user's detail setting binds v1 /
v3 (the `[BillboardShadow] rewrote ... v1` line), so the dither applies; a user on the v2 detail level gets the
vertex-side look only. Extending the match to a `mov / add rX, vS` chain is a follow-up if anyone runs that
level.

## 5. Gate (debug exe `b5c550f9...`, the merged branch `better-trees`)

| Item | Result |
|---|---|
| Unit | `tools/build.sh unit`: **1241 tests, 0 failures** (the two dither tests new; the row tests moved to `better_trees`). |
| Acceptance | `tools/acceptance/acceptance.sh`: 0 lines off target. |
| `tools/build.sh run` (faithful autostart) | `[Ext] profile=faithful ... GATE: faithful forced`, `ext.better_trees=0`, RUNNING to frame 43276, crash log 0 bytes, 0 `[FoliageAA]` / `[BillboardShadow]` lines. |
| Resave | `tools/resave_gate/resave_gate.sh Gate`: **PASS (resave == golden, 23453453 bytes)**, crash log 0. |
| The user's settings.cfg | backed up at the start (md5 `8ec17148...`), the look arms ran on it (the migration seen live, `GameShowWeather=false` for the arms), restored byte-identical at the end. The migration therefore runs again at the user's next boot and writes `ext.better_trees=1` with the next settings flush. |
| perf flavour | compiles (the plumbing agent's build); not run. |
