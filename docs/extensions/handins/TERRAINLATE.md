# Terrain drawn last (`ext.terrain_late`) -- hand-in

Class A (drawing only, nothing saved, live). Experimental tab, ON by default (the standing class A rule).
Code: `src/ext/terrain_late/`. Pure rules and their tests: `terrain_late_rules.h`,
`tests/unit/ext_terrain_late_test.cpp`.

## 1. What it does

Retail sorts the main scene entry's Opaque list by detail id first (`DrawSort::DrawItemLess`, the `_flags`
term). The terrain's chunk items carry detail id 1 and every other opaque item 2, so the terrain is drawn
first: only the sky (no depth test, no depth write) comes before it. Every terrain pixel that a building,
tree, rock or citizen covers later is shaded and then overwritten. The terrain's pixel program is costly
per pixel: several texture reads under the player's anisotropic filter, plus a filtered shadow lookup.

With the row on, the main entry's sorted Opaque list has the terrain's items moved behind the others, so
the hardware early depth test rejects the covered terrain pixels before the program runs. The name says
exactly that: the terrain is drawn late. The detail id is not touched, because it is also the reflection
filter (`VideoReflectionDetail`).

## 2. Design as built

| Seam | Where | What |
|---|---|---|
| `Graphics::Renderer::_extOpaqueOrder` | `Renderer::Draw` pass 1, right after the list sort (`wave27_lane_gfxbig.cpp`), and the same point of `Ext::RenderPipe::Pass1` (`passes.cpp`) | Called for list 0 of an entry with `_shader == Normal` only. `OrderOpaque` moves every terrain item (a `WorldGeometry` item whose instance is its `_terrainMaterial`) behind the rest. It is a stable partition: one rotate per run, no allocation. |
| `WorldGeometry::_extTerrainLate` | `WorldGeometry::Draw` (`geometry_draw.cpp`), before the terrain_batch run / chunk branch, shader index 0, not the 0xA000 overlay | `BindLateState` replaces the terrain's own render state (just bound by the material's Setup) with a copy whose depth test is LESS instead of LESS_EQUAL. |

Both seams are null by default and under the faithful profile. They are installed once by
`Ext::TerrainLate::Sync` (registry_boot `InitFromSettings`), and both bodies follow the row's live bool,
so a toggle needs no Sync.

**What moves:** only the terrain items of the main entry's Opaque list.
**What does not move:** the shadow entry (shader 1) and the reflection entry (shader 2); the Blended and
third lists; and the relative order of every other item. Instancing batches, ext.draw_batch runs and the
sky's first place are therefore unchanged.

**Why LESS.** A sample's final colour is the nearest fragment's whatever the draw order, except at an
exact depth tie. There, retail's LESS_EQUAL gives the sample to the later draw, i.e. the object over the
terrain. Drawn last, the terrain therefore tests LESS: a tie fails and the object keeps it, as in retail.
The depth buffer is identical either way.

**Safety rule** (`LateSafe`): the move happens only when the terrain's Normal pass tests and writes depth
with LESS or LESS_EQUAL and does not blend. A mod material with any other state keeps retail's order.

## 3. Measurements (stress town, `BANISHED_CAMERA_PIN=town,200`, 2528x1345, 4x MSAA, 16x aniso, ShaderDetail 3, frame 3000)

Coverage, from the base (row off) Vulkan capture. The MSAA depth and colour were read per sample after the
sky, after the terrain and at the end of the Opaque list:

- Terrain fragments reach 100% of screen pixels.
- 44.3% of terrain samples are covered later by an object.
- **41.7% of terrain pixels have every sample covered later.** These are the pixel-shader invocations
  early-Z can skip.
- 160 of 13.6 M terrain samples (143 pixels) are exact depth ties that a later object wins. These are
  what the LESS state keeps exact.

RenderDoc replay timings (`tools/trace_oracle/gpu_timing.py`). These are relative only, and the off and on
captures come from separate runs:

| Backend | Terrain draws, off | Terrain draws, on | Main pass, off | Main pass, on |
|---|---|---|---|---|
| Vulkan | 0.321 ms (22 draws, 514,048 tris) | 0.184 ms (-43%) | 1.104 ms | 0.971 ms |
| DX11 | 0.433 ms (22 draws) | 0.296 ms (-32%) | 3.406 ms | 3.222 ms |

The other opaque runs did not get slower (trees: 0.306 / 0.392 ms off, 0.302 / 0.379 ms on).

## 4. Image equality

The draw order of depth-writing opaque draws cannot change a sample's final colour when every draw meets
four conditions:
- depth test on;
- depth write on;
- LESS or LESS_EQUAL;
- a blend that does not read the destination.

When those hold, the result is the nearest fragment, with ties going to the object as argued in section 2.
MSAA resolves per sample, and alpha-to-coverage only selects samples, so the resolve is unchanged too.

Every draw of the main Opaque pass was checked against these conditions in all four captures (Vulkan and
DX11, off and on):
- The only draw outside them is the sky. It tests nothing, writes no depth and is drawn first in both orders.
- The A2C tree draws blend colour One/Zero and alpha Zero/Zero, which does not read the destination.
- The terrain draws are LESS_EQUAL with the row off and LESS with it on.

Pixel diffs between runs measure nothing (CLAUDE.md), so the visual pairs below are separate runs at the
same pin: Vulkan at town,40 (foundations, fences, stone pads) and at town,200. No difference was seen.

## 5. Order dependencies checked

| Dependency | Verdict |
|---|---|
| Decals, roads, fields, the water ranges, ext.better_water's shore strip, ext.winter_ice | Unaffected: Blended list, drawn after AO and the ground draw (capture pass 16) |
| Ground-draw hook (fancy_terrain light, show_grid, map_overlays) and AO reading the depth | Unaffected: run after the whole Opaque list. The terrain is still in, and the depth is identical. |
| ext.foliage_aa / A2C trees | Unaffected: per-sample winners are the same, and A2C draws do not read the destination |
| Blended or depth-equal items relying on terrain first, inside the Opaque list | None in the captures (section 4) |
| Exact contact (foundations on the ground) | LESS for the late terrain keeps retail's tie winner |
| Terrain-on-terrain ties | The terrain's own order is unchanged, but its test is LESS. Two terrain fragments at the same quantized depth (a grazing ridge) would keep the first instead of the second. Chunk edges are shared and back faces are culled, so this was not measured and is expected to be negligible. |
| Shadow and reflection passes | Untouched (`_shader != Normal`). The captured frames held no reflection pass. |
| Both backends | Shared sort / submit code. DX11 and Vulkan both verified by capture. |

## 6. Known limits

- With ext.render_pipe, a toggle between a frame's pass 1 and its pass 2 can pair one setting's order with
  the other's state for that one frame. Either pairing differs from retail only at depth ties.
- The LESS copy of the terrain state is created on first use on the drawing thread and kept for the
  process, the same pattern as show_grid's grid state.
