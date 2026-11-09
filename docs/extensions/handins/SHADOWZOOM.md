# SHADOWZOOM: sun map size by zoom (GPUFRAME rank 1), derivation only, NOT BUILT

> **Update (2026-11-07): BUILT as `ext.max_shadow_zoom`, section 6.** Sections 1-3 assumed the manual
> pitch at 0; retail's default is 0.75, and there the ratio does change with the zoom.
>
> **Outcome (2026-10-20): not built.** Under the user's ruling ("on-screen shadow sharpness stays what
> the 8192 map gives at every zoom level") the zoom-adaptive map is a no-op: the shadow texels per
> screen pixel barely change with zoom, so no zoom ever has the 2x surplus needed to halve the map.
> The coordinator stopped the build (no off-switched perf bodies in main). This file keeps the
> derivation and the mechanism design, in case the user rules a looser reference.

Spec: `gpuframe/ideas_passes.md` rank 3, `GPUFRAME.md` idea 1. Base `gpuframe` 208c2a787.
No code change, nothing run.

## 1. The geometry

**The view camera** (`PlayerController::UpdateCamera`, `gameupdateloop_load.cpp`; `CalcTransform`):
fovY 40 degrees, near 2, far 512. fovX follows the aspect (above 16:9 it is widened from the 16:9 fovY:
59.3 degrees at 2560x1377). The eye sits `distance` back from the focus along `FromYawPitch(yaw, pitch)`,
and the focus z is 0 (the pin and the follow both set it to 0). The pitch target comes from
`Ext::BetterCamera::PitchTarget` with manual 0: retail goes from -60 at zoom 20 to -85 at zoom 130
(flattening to top-down). `ext.better_camera` keeps -60 at every zoom (its "steady tilt") and widens
the range to 6..200.

**The sun box** (`SunLight::GetSunCamera`, `sun_camera.cpp`): each of the four near-plane corner rays
is cut at the ground plane z = -2 (or extruded 1000 units if it misses within 1000, with the preserved
z clamp). The box half-size is the bounding sphere radius of those 8 points times
`sqrt(|forward.z|)`, so its side is `2 * radius * skew`. One shadow texel is `side / N` world units
for an N map, and the texel snap uses the same `Width()`.

**Why the box follows the pixel footprint.** At every retail and better_camera zoom all four corner
rays hit the ground well inside 1000 units: the top ray is at most 40 degrees below the horizon at
zoom 20 (pitch -60, half fovY 20) and 65 at zoom 130. So the box is the bounding sphere of the
screen's ground footprint. That footprint scales linearly with the eye distance, and so does the world
size of one screen pixel. Their ratio, texels per pixel, depends only on the pitch, the FOV and the
fixed -2 plane offset, never on the zoom. With a fixed pitch (better_camera) the configuration is
exactly self-similar and the ratio is constant. In retail the pitch steepens with zoom, which makes the
footprint more square and the bounding sphere less wasteful, so the ratio rises slightly.

**Texels per pixel.** Measured at the near (bottom) edge of the screen, where the pixels are smallest
and the shadow detail is finest: TPP = (world width of the bottom edge / 2560) / (side / N). A pixel
higher on screen is larger, so the near edge is the binding case. The vertical footprint (one pixel
row at the bottom centre) is also listed. Model: a Python replica of `GetSunCamera` (the corner ray
cut, the clamp quirk, the bounding sphere, the skew) with the camera above, at 2560x1377, focus z 0.

### Retail camera (pitch -60 -> -85)

| zoom | pitch | sun box side | bottom edge width | px (h / v) world | texel @8192 | TPP 8192 (h / v) | TPP 4096 | TPP 2048 |
|---|---|---|---|---|---|---|---|---|
| 20 | -60.0 | 43.3 | 27.6 | 0.0108 / 0.0093 | 0.0053 | 2.04 / 1.76 | 1.02 | 0.51 |
| 50 | -66.8 | 97.5 | 67.5 | 0.0264 / 0.0225 | 0.0119 | 2.22 / 1.89 | 1.11 | 0.55 |
| 90 | -75.9 | 164.2 | 126.1 | 0.0493 / 0.0421 | 0.0200 | 2.46 / 2.10 | 1.23 | 0.61 |
| 130 | -85.0 | 225.1 | 191.3 | 0.0747 / 0.0658 | 0.0275 | 2.72 / 2.39 | 1.36 | 0.68 |

### ext.better_camera (pitch -60 at every zoom)

| zoom | sun box side | bottom edge width | texel @8192 | TPP 8192 (h / v) | TPP 4096 |
|---|---|---|---|---|---|
| 6 | 16.1 | 10.3 | 0.0020 | 2.04 / 1.76 | 1.02 |
| 20 | 43.3 | 27.6 | 0.0053 | 2.04 / 1.76 | 1.02 |
| 50 | 101.6 | 64.6 | 0.0124 | 2.04 / 1.76 | 1.02 |
| 90 | 179.3 | 114.0 | 0.0219 | 2.04 / 1.76 | 1.02 |
| 130 | 257.0 | 163.4 | 0.0314 | 2.04 / 1.76 | 1.02 |
| 200 | 392.9 | 249.9 | 0.0480 | 2.04 / 1.76 | 1.02 |

(The ratio does not depend on the resolution's aspect, but it scales with the pixel count: at
1920x1080 every TPP above is about 1.33x larger, at every zoom alike.)

## 2. What the ruling allows

The reference is the 8192 map at the closest zoom: TPP 2.04 at zoom 20 (or zoom 6 with better_camera).
A sub-resolution s keeps the sharpness only while TPP_8192(zoom) x s / 8192 >= 2.04, so 4096 needs a
2x surplus. The largest surplus in range is 2.72 / 2.04 = **1.33x** (retail, zoom 130). With
better_camera it is **1.0x** at every zoom. **No zoom qualifies for 4096. The selector would answer
8192 every frame, in both camera modes.**

The same tables say that 8192 gives about 2 texels per near-edge pixel at EVERY zoom, zoom 20
included. The 0.52 ms measured for 8192 -> 4096 at zoom 130 is not a zoom-out-only saving. It is
the price of the 2x oversampling the map has everywhere.

**Correction to the ideas estimate.** `ideas_passes.md` §1 put the zoom-130 sun box at 450-500 units
and the near-edge pixel at ~0.18 units, which led to "~3 texels per pixel at 8192, earns its keep only
zoomed in". The replica gives a 225-257 unit box and 0.064-0.075 units per near-edge pixel. That is a
similar TPP at zoom 130 (2.0-2.7), but the same ratio holds at zoom 20 too. Before any rerun of this
idea, confirm the box side with a one-off dump of `GetSunCamera`'s `radius * skew` at zoom 20 and 130
(stderr, once, not per frame).

## 3. What the looser choices would look like (for the user to rule)

- **4096 at every zoom** (the combo set to 4096 today, no code): near-edge TPP 1.02 at zoom 20 rising
  to 1.36 at zoom 130 (retail), 1.02 everywhere with better_camera. One shadow texel then covers about
  one screen pixel at the bottom of the screen, and more higher up. The 3x3 PCF kernel (one texel
  spacing) blurs an edge over ~3 texels, so ~3 px at the near edge instead of ~1.5 px today. This is
  softer at every zoom, not only zoomed out. The texel snap grid doubles too, so shadow edges crawl in
  coarser steps when the camera moves. Gain: the measured -0.52 ms at zoom 130, about the same
  elsewhere (the fill per tree is what drops). Retail's own High is 2048 (TPP ~0.5).
- **"At least 1 texel per near-edge pixel"** as the reference: that picks 4096 at every retail and
  better_camera zoom (TPP 1.02-1.36 >= 1, 2048 never qualifies), the same picture as 4096 everywhere.
  So it needs no per-frame mechanism, only the existing combo. A zoom-adaptive map would pay only if
  the reference sat between the zoom-20 and zoom-130 ratios (e.g. TPP >= 1.3 picks 4096 only near zoom
  130 in retail and never with better_camera). That gain is too narrow to justify the code.

**Recommendation:** let the user compare 8192 and 4096 by eye at zoom 20 and zoom 130 with the existing
Enhanced-tab combo (screenshots at BANISHED_CAMERA_PIN=town,20 / town,130, both sizes). If 4096 reads
the same at zoom 20, set the default to 4096 and take the 0.5 ms everywhere. If it does not, 8192
stays and this idea is closed.

## 4. The mechanism, if a looser reference is ever ruled (not built)

- **One 8192 map, rendered into a top-left s x s sub-rectangle** (s = 8192 >> k). This is simpler than
  a mip chain: no extra DSV/SRV and no +33% memory. `SunLight::DrawShadowMap` takes s instead of
  `Width()` for its viewport, its scissor and the 1-texel guard viewport (1, 1, s-2, s-2). The full
  clear stays: `ClearDepthStencilView` clears the whole surface, so every texel outside the
  sub-rectangle reads 1.0 (lit), as retail's clamped guard texels do.
- **Sampling.** In `SunLight::DrawLight`, scale the bias matrix's u and v columns (m[0], m[12], m[5],
  m[13]) by s / N, a power of two, so it is exact. The shadow UV then lands in [0, s/N]. The depth
  column is untouched, so better_trees' `shadowProjection` row-2 extent is unchanged. **Keep
  `texelSize` = 1 / N**: in the full texture's UV one texel is still 1/N, so the 3x3 PCF taps stay one
  texel apart. They then sample exactly the lattice retail would at size s. The sampler (clamp,
  comparison linear) needs no change.
- **Texel snap.** `GetSunCamera` snaps with s instead of `Width()`, which is retail's snap at size s.
- **Per frame and per Renderer.** Decide in `SunLight::AddDrawItems` (pass 1, before `GetSunCamera`).
  Record s in a small atomic table keyed by `Renderer*`, which `GetSunCamera`, `DrawShadowMap` and
  `DrawLight` read. Under `ext.render_pipe` the two Renderer slots alternate, so the render thread's
  pass 2 reads its own frame's s while the main thread decides the next.
- **Selector** (pure, unit-testable). R = Q / Q_ref with Q = bottom-edge world width /
  (2 tan(fovX/2) x sun box side), which does not depend on resolution or aspect. Q_ref is the
  constant at the reference zoom. Step down to level k+1 only when R >= 2^(k+1) x 1.15, and step back
  as soon as R < 2^k, so the reference is never undercut. If either bottom ray misses the ground
  (eye_camera level views), use the full map.
- **Class A**, a row `ext.shadow_by_zoom` (or a third combo choice of ext.max_shadow_res). The seam is
  a null-by-default `SunLight::_ext...` data pointer as `_extMaxShadowRes` is. The `[ShadowRes]` line
  gains the active size once per change.

## 5. A/B, should it ever be built

STRESS_EXT stress legs at `BANISHED_CAMERA_PIN=town,130` and `town,50`, reading frame_ms / fps
(machine load CLEAN). `capture_arm.sh` + `gpu_timing.py`: the shadow pass's ms and the shadow draws'
ns/tri. Screenshots at zoom 20 / 50 / 90 / 130 for the user's eye. A pixel diff between runs measures
nothing.

## 6. BUILT (2026-11-07): `ext.max_shadow_zoom`, and why sections 1-3 were wrong

**The error.** Sections 1-3 modelled the camera with the manual pitch at 0 (the steepest tilt, -60 at
zoom 20). Retail starts at `PlayerToolManager::manualPitch = 0.75` (`gameupdateloop_load.cpp`), so the
default camera is -34 degrees at zoom 20 and -70 at zoom 130. RenderDoc captures of the stress town at
`BANISHED_CAMERA_PIN=town,20` / `town,130` confirm it (view forward z -0.563 / -0.940) and the sun box
side (63.0 / 234.0 world units, the replica above gives 64.2 / 234.0 with fovY 40). At the default tilt
the bottom-centre pixel, in its smaller direction, gets **1.09 texels at zoom 20 and 1.94 at zoom 130**
(8192 map, 2560x1440): the zoomed-out view draws 1.8x the texels the screen shows, which is the
headroom the zoom-adaptive map needs. (The "18 texels per pixel" estimate of the GPU profile was an
area ratio over the whole map, not a per-pixel one.)

**The mechanism** (`src/engine/graphics/shadow_zoom.h`, seam `SunLight::_extShadowZoom`; a valued key of
`ext.max_shadow_res`, 0..1, default 1; Options > Enhanced "Adaptive shadow map" Off / On, under "Max shadow
resolution", `src/ext/max_shadow_res/enhanced_entry.cpp`). The map is not rebuilt.
`AddDrawItems` scales the sun camera's projection in clip space so the box lands in the map's first
1 / 2^shift along x and y; the casters (`DrawItemList`) and `DrawLight`'s sampling matrix both read that
projection, so the map and its matrix match on every backend with no backend code. `GetSunCamera`
snaps with `Width() >> shift` (retail's snap at that size). The PCF texel size stays 1 / Width(), one
texel. The cull frustum is unchanged. The group that draws the map picks the next frame's shift from
the engine's own reading of the bottom-centre pixel's ground size against the sun box texel: halve when
the halved map still gives >= 0.9 texels per pixel, go back below 0.8, never below 2048. One stderr
line per switch, the first 16 only. Row off or faithful profile: shift 0, retail.

At 2560x1440, default tilt: 4096 drawn from about zoom 115 out (the engine reads 1.90 at zoom 130 on
DX11, 1.81 on Vulkan at the switch frame), 8192 back below about zoom 90. 1920x1080: from about zoom 45.
3840x2160: never.

**A/B** (stress town, 2560x1440 4x MSAA, every visual ext on, ext.no_fog; three other agents compiling,
GPU-bound scene, PresentMon GPUBusy medians; RenderDoc replay of the shadow pass):

| | zoom 130 off | zoom 130 on | zoom 20 off | zoom 20 on |
|---|---|---|---|---|
| DX11 GPUBusy ms | 2.155 / 2.156 | 1.799 / 1.743 | 1.118 | 1.163 (no switch: noise) |
| Vulkan GPUBusy ms | 1.746 / 1.806 | 1.366 / 1.379 | 0.842 | 0.838 (no switch) |
| DX11 replay shadow pass ms | 0.816 | 0.527 | | |
| Vulkan replay shadow pass ms | 0.448 | 0.151 | | |

Visual: the captured sun map holds the casters in one quadrant on both backends; the final frames at
zoom 130 read the same side by side at 3x (same sun, same shadows, no offset).

**Rulings (2026-11-08).** The sharpness trade is accepted: the 0.9 / 0.8 thresholds stay. The user
watched the 8192 / 4096 crossover live and could not see it. The key gets the Enhanced row above; the
default stays on.
