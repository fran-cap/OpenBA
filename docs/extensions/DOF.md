# DOF -- depth of field for bokeh / tilt-shift screenshots, the plan

Research 2026-11-08 (one session: the render tree plus the published techniques). Rulings are in §1;
§2 is the facts the choice rests on, §3 the technique, §4 the shape as built, §5 what stays open. The
live tier is built (2026-11-08): the row `ext.depth_of_field` ("Depth blur", class A, ON by default, the
Extensions dialog's Default tab) and its key `ext.depth_blur` (the Enhanced tab's "Depth blur", OFF by default), DX11
and Vulkan; Metal answers "not supported".

## 1. Rulings (the user, 2026-11-08)

1. **LOCKED: the live tier** -- a half-resolution scatter-as-gather DoF (§3.1): CoC from depth, a
   prefilter with an LDR highlight boost, a golden-angle disk gather with near / far separation, a
   tent filter and a full-resolution composite. Pixel shaders only, one HLSL source for every backend.
2. **OPEN: tilt-shift** (§3.2). The live tier's CoC function is the place it plugs in; which CoC
   (screen band, tilted focal plane, or both) is not ruled.
3. **PARKED: photo mode** (§3.3, accumulated aperture-jittered renders through the town_screenshot
   machinery). Do not build it.
4. **OPEN, a later date: how several blurs combine** (§5.1) -- depth DoF with tilt-shift, and with
   anything else that blurs. Rule this before tilt-shift is built.

## 2. Facts the design rests on (the tree on 2026-11-08)

- **No compute anywhere in `src/render`.** Our shaders are HLSL compiled offline by fxc to DXBC and
  checked in as byte arrays (`src/render/common/ao/shaders/*.h`, `src/render/common/better_clouds/*_ps.h`).
  DX11 uses the DXBC directly; Vulkan converts it to SPIR-V with dxbc-spirv
  (`src/render/vulkan/backend.cmake`, `shader_convert.cpp`); Metal converts it with airconv
  (`src/render/metal/airconv_abi.h`). ASSAO runs on all three this way (`src/render/dx11/ao/`,
  `src/render/vulkan/ao/assao_pass.cpp`, `src/render/metal/passes/assao_pass.mm`). **A DoF written as
  ps_5_0 / vs_5_0 fullscreen passes needs no new toolchain on any backend.** Compute, geometry shaders
  and subgroup ops would each be a conversion risk in both translators.
- **Colour is LDR.** Main colour is B8G8R8A8, MSAA, resolved once per frame in `EndCommands`
  (AO.md §2 "Targets"). There is no HDR range, so bright pixels do not produce bokeh discs on their own;
  the highlight boost (§3.1 step 1) fakes it. The main colour's alpha is NOT free (terrain writes the
  water height, River reads it); our own half-res targets carry CoC in alpha.
- **Depth.** The shared R32F depth copy (`src/render/common/depth_copy/depth_copy_backend.h`) is made
  at the Opaque / Blended boundary and reused while no draw writes depth. Water does not write depth:
  a lake blurs at the lakebed's depth. Acceptable for a screenshot effect; noted, not fixed.
- **Mid-frame colour copy** precedent: `FrameCopyImageBuffer` (`src/engine/graphics/FrameCopyImageBuffer.h`).
- **Camera.** Perspective fovY 40, near 2, far 512; eye_camera near 0.1 (AO.md §2 "Camera"). CoC needs
  linear view depth, so near / far are passed in, as the AO depth resolve does.
- **Android** is the Vulkan backend on new Adreno parts (`docs/vulkan/PLAN.md`): a tiler, bandwidth-bound.
  Few passes, half resolution, small formats.
- **Menu precedent**: the Enhanced graphics tab (`src/ext/enhanced_tab/`), as AO did.

## 3. The technique

### 3.1 Live tier (LOCKED)

The family of Jimenez's Call of Duty: AW DoF, Unity Post Processing v2 and KinoBokeh, cut down to
pixel shaders.

1. **CoC + prefilter** (full res in, half res out). Linearise depth, compute the signed CoC (negative
   near, positive far) from focus distance and aperture, downsample colour 2x2 with a CoC-aware weight,
   CoC to alpha. **Highlight boost** here: weight each sample by about `1 + k * luma^4` (or an inverse
   Karis average) so bright LDR pixels make discs; the composite divides the boost back out.
2. **Tile max-CoC** (optional, off on Low): an 8x8 or 16x16 max of |near CoC|, dilated one tile, so a
   blurred foreground spreads over the sharp background behind it.
3. **Gather** (half res). Golden-angle / ring disk, taps per tier: Low 16, Medium 22, High 43,
   Ultra 71. Near and far accumulated apart (scatter-as-gather: a sample counts when its own CoC reaches
   the centre pixel), then blended by near coverage. The bokeh shape is the tap table: a hexagon or
   rounded polygon is a different table, not a different pass.
4. **Tent filter** (half res, 9 taps) to hide the sparse kernel.
5. **Composite** (full res): lerp sharp colour to blurred by |CoC|, near layer over the top. Writes the
   resolved colour before the UI draws.

Cost: four or five fullscreen passes, three of them at a quarter of the pixels. Expected well under
1 ms at 1080p on a desktop and a few ms on an Adreno at Medium -- an estimate from comparable shipped
implementations, to be measured in the spike (§4).

### 3.2 Tilt-shift (OPEN)

The same passes with a different CoC function in step 1:
- **Miniature** (the look players expect): CoC grows with screen-space distance from a horizontal focus
  band. Parameters: band centre, band width, strength.
- **Physical** (Scheimpflug): CoC from the distance to a tilted focal plane through a picked world
  point. Holds up better on hills; with Banished's near-top-down camera the plane is roughly the ground.
- Combined with depth DoF: see §5.1.

### 3.3 Photo mode (PARKED)

For the record only: render N = 32-128 frames with the eye jittered over a disk / polygon aperture and
the focal plane fixed (off-axis projection), accumulate in float. True occlusion, no halos, a real
aperture shape, seconds per picture and no per-frame cost. It would reuse ext.town_screenshot's pause
and re-render path (`src/ext/town_screenshot/town_screenshot.h`). Needs everything animated (foliage
wind, water, particles, weather) frozen, and a float accumulation target or a CPU sum.

### 3.4 Rejected

| Technique | Why not |
|---|---|
| Circular separable convolution (Garcia, EA Frostbite) | circles only; several complex-valued RGBA16F targets (bandwidth on Android); rings with few components; weak near / far occlusion |
| Sprite scatter (a quad per bokeh pixel) | heavy overdraw on tilers; a geometry-shader version is a translator risk |
| AMD FidelityFX DoF (MIT) | compute + subgroup ops, DX12 / Vulkan only; no fit for DXBC -> SPIR-V / AIR or Metal |
| Gustafsson single pass, full res | lovely and simple (Blender Workbench uses it), but taps grow with radius squared; at most a future "Maximum" tier |

## 4. Shape (built 2026-11-08)

```
src/render/common/dof/dof.hlsl        the four pixel programs (prefilter, gather, tent, composite);
                                      tools/water_shaders.py compiles them (fxc ps_4_0) to dof_*_ps.h
src/render/common/dof/dof_math.h      the CoC and the max CoC's scaling in C++ (tests/unit/ext_depth_of_field_test.cpp)
src/render/common/dof/dof_backend.h   the backend contract: Supported / Request / Draw, the 32-float Frame
src/render/{dx11,vulkan,metal}/dof/   each backend's pass (Metal: the "not supported" stub)
src/ext/scene_draw.{h,cpp}            the scene-draw seam's arm (Graphics::Renderer::_extSceneDraw)
src/ext/depth_of_field/               the row, the focus, the Enhanced tab combo "Depth blur" Off / On
```

What the spike questions came to:
1. **The seam**: a new one, `Renderer::_extSceneDraw` (Camera.h), called after the last of the MAIN
   entry's three lists in `Renderer::Draw` pass 2 and in the render_pipe copy (`passes.cpp`, the packet
   carries it). Before the tool draw (step 11) and the UI (step 12), so the placement ghost and the UI
   stay sharp; the water, particles and status icons are blurred with the scene. Not armed for
   ext.town_screenshot's tiles.
2. **The resolved colour mid-frame**: DX11 `ResolveSubresource` (MSAA) or `CopyResource` (1x) of the
   bound colour into a 1x twin; Vulkan a copy of the main colour source (the swap-chain image holds the
   resolved frame so far; draw.h `DrawAoCopyMainColor`) inside the AO bracket. Adreno cost not measured.
3. **Focus**: the player camera's distance to its target (`PlayerController::distance`): the centre of
   the screen is that target, so what the player looks at is sharp at every zoom, with no read-back and
   no easing needed. Under ext.eye_camera the pass reads the depth at the screen's centre instead.
4. **Measured cost**: not yet (no stress A/B run).
5. **Highlight boost**: a gather weight of `1 + 2 * saturate((luma - 0.8) * 5)`; not yet judged at dusk
   or on snow.

The look's defaults (`dof_params.h` Look, tuned by the user in the overlay 2026-11-08): focus at 0.74 x the
camera target's distance, gain 1.01, max CoC 5.1 half-resolution pixels at 1080 lines (scaled with the
target's height), in-focus band 0.217 of `(d - f) / d`, boost 2, threshold 0.8. The intermediate targets
are 8-bit RGBA on both backends (the CoC rides in alpha as `CoC / (2 max) + 0.5`). No tile max-CoC pass
(§3.1 step 2): the gather's foreground term spreads the near field on its own; add it if near edges halo.

## 5. Open

1. **Several blurs** (the user, later): how depth DoF and tilt-shift combine (max of the two CoCs, a
   sum, one replacing the other, or one mode at a time), and how either sits with any other blur.
2. Tilt-shift's CoC (§3.2), after 5.1.
3. Photo mode (§3.3), parked.
5. Cost on the S25 and the desktop (§4 question 4).

## Sources

- Jimenez, Next Generation Post Processing in Call of Duty: AW (SIGGRAPH 2014): https://www.iryoku.com/next-generation-post-processing-in-call-of-duty-advanced-warfare/
- Gustafsson, Bokeh depth of field in a single pass: https://blog.voxagon.se/2018/05/04/bokeh-depth-of-field-in-single-pass.html
- Garcia, Circular Separable Convolution Depth of Field: https://ea.com/frostbite/news/circular-separable-convolution-depth-of-field
- Wronski, Separable disk-like depth of field: https://bartwronski.com/2017/08/06/separable-bokeh/
- AMD FidelityFX DoF: https://gpuopen.com/fidelityfx-dof/
- A hybrid system for real-time DoF (arXiv 2210.06158): https://arxiv.org/pdf/2210.06158
- GPU Gems 3 ch. 28, Practical Post-Process Depth of Field: https://developer.nvidia.com/gpugems/gpugems3/part-iv-image-effects/chapter-28-practical-post-process-depth-field
