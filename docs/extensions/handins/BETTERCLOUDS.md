# BETTERCLOUDS -- `ext.better_clouds`: drifting clouds

`ext.better_clouds`, display "Better clouds", class A, **ON by default** (the standing class A rule),
live: the Extensions dialog's toggle and the Enhanced tab's combo take effect on the next frame.
The valued key `ext.better_clouds_mode` (1..2, default 1) picks the level: 1 On, 2 Fancy.

## 1. What the player sees

Retail's sky is a still painted cube map. With the row on, the sky draws with a pixel program of ours:
the painted sky, blurred (its own clouds smoothed away), under procedural clouds that drift down one wind
direction. They move at the game's speed, capped at 3x (the user's ruling: at 10x and 50x uncapped
clouds race), and stand still while the game is paused. The weather sets them: thin fair-weather clouds
when the sky is clear, a thicker, darker cover in winter and in storms (the same blend value the
installed sky program uses between its clear and cloudy halves). The scene's sun lights them: brighter
on the side toward the sun, a soft glow at thin edges when looking toward it. The water's reflection of
the sky draws the same clouds with a cheaper program. Drawing only: nothing is saved, the sim is
untouched. The main menu's backdrop keeps the installed sky (it has no town loop).

Fancy adds the clouds' shadows on the ground (section 2.6).

Two earlier designs were tried and dropped: turning the sky dome about its up axis (the whole painted
sky wheeling round looked wrong), and the same turn in a vertex program.

## 2. How

### 2.1 The programs
`src/render/common/better_clouds/cloud_sky.hlsl`, compiled offline by `tools/water_shaders.py` (fxc,
ps_4_0) into `cloud_sky_ps.h` (`g_bc_PSCloudSky`, the sky) and `cloud_sky_cheap_ps.h`
(`g_bc_PSCloudSkyCheap`, the reflection: no detail octave, no sun probe). The program reads what the
installed sky's vertex program writes (the clip position and the cube-map direction, `TEXCOORD1` in
register 1) and the engine's pixel constants (`b0`: the light and ambient colours, `userPosition` whose
x and w make the weather blend value exactly as the installed program does).

- **The base sky**: the installed cube map, sampled at a coarse mip (`kBlurLod`), clear half and cloudy
  half blended by the weather as the installed program blends them.
- **The cloud layer**: the view direction projected onto a curved plane overhead
  (`q = d.xy / (d.z + kCurve) * scale`), so clouds shrink and crowd toward the horizon.
- **The shape**: three octaves of a baked noise texture (t1), each rotated and scaled (rotations 0, 36
  and 74 degrees, frequencies 1, 2.13, 4.71) so the tiling never lines up, plus a slow warp field.
  Coverage, edge softness, darkness and opacity follow the weather. The horizon fades into the base sky.
- **The light**: the lit side near white with a touch of the sun's colour, the shaded side a light grey
  with a little of the sky in it; one extra noise tap toward the sun (full program only) gives the
  bright and dark sides; a forward-scatter lobe brightens thin edges toward the sun. A triangular
  dither hides banding on the 8-bit backbuffer.

### 2.2 The noise
`src/ext/better_clouds/cloud_noise.{h,cpp}`, pure: a 256x256 RGBA tile baked once per process on the
loader thread (about 0.1 s). R Perlin-Worley (the billowy base), G Perlin fBm, B inverted Worley fBm
(the detail), A low Perlin (the warp). Every channel is periodic over the tile and histogram-equalised,
so coverage thresholds mean the same thing in each. `better_clouds.cpp` converts it to BGRA8 with a full
2x2 box mip chain and makes it a wrap / anisotropic `Video::ImageBuffer` through the engine's own
`Create`, as `ext.winter_ice` makes its textures.

### 2.3 The motion
`src/ext/better_clouds/cloud_params.{h,cpp}`, pure. Every octave reads the noise at
`R_o * q * F_o + off_o`; moving the clouds by `W` down the wind is `off_o = -R_o * W * F_o`, so every
octave travels the one world direction although each is rotated in the texture. The offsets are
accumulated in double a frame at a time and wrapped to 0..1, so the program never sees a large time
value. Each octave runs a little faster than the one under it (wisps run ahead of the bulk); the warp
drifts slower, so the shapes change as they travel. The clock is the frame time times
`min(speed, 3)`, fed from `GameUpdateLoop::Update` where the game time advances (not called while
paused). `gc.time` was not usable: it is real time, not scaled by the game speed.

### 2.4 Getting the programs into the sky draw
- `SkyLoaded` (loader thread, after the sky mesh is built) remembers the sky's material and material
  instances.
- `BeginFrame` (main thread), with the row on: makes our two pixel programs and, for each pass of the
  sky material with a vertex program, a twin `Video::VideoProgram` (the pass's vertex layout, vertex
  program and render state, our pixel program: the full one for pass 0, the cheap one for the others).
  The material's pass slots point at the twins; with the row off they point at the installed programs
  again. A pass whose installed vertex program does not write what ours reads keeps its own
  (`sky_fit.{h,cpp}` compares our input signature with the installed output signature; a modded sky
  that moves the direction falls back to retail, with one log line).
- The constants (the octave offsets, the sun direction, the look) and the noise texture reach the draw
  through `Graphics::MaterialInstance::_extAfterSetup`, called after every material setup on the drawing
  thread. `src/ext/material_setup.{h,cpp}` shares that one seam between `ext.better_trees`' blossom and
  this row. The hook acts on the sky's material instances alone: it writes the 16 floats into the
  context's material constants (b1, dirty bit `0x8000`) and binds the noise at slot 1.

### 2.5 The look
`Look` in `cloud_params.h`: white, shade, sky lift, coverage bias, layer scale, wind speed, morph (how
much the octaves' speeds differ: how fast the shapes change as they travel), and Fancy's shadow strength,
scale, height and softness. The defaults
are the user's pick in the debug tweak overlay (Alt+F10, "Better clouds"), which moves them live in a
build with the overlay.

### 2.6 Fancy: the clouds' shadows on the ground
A full-screen pass at the ground-draw seam (`Graphics::Renderer::_extGroundDraw`, `src/ext/ground_draw.cpp`:
between the opaque and the blended lists of the main scene, before the map overlay and the grid), so it
darkens the ground, the trees and the buildings, and the water and everything blended draw over it.

- **The contract**: `src/render/common/better_clouds/cloud_shadow_backend.h` (`Supported`, `Request`,
  `Draw`, the 40-float `Frame`). DX11 and Vulkan draw it (`src/render/{dx11,vulkan}/better_clouds/
  cloud_shadow.cpp`); Metal answers not supported, so Fancy there draws the sky's clouds alone.
- **Vulkan**: in draw.cpp's AO bracket (`DrawAoBegin` / `DrawAoEnd`), with a reader bit of its own on the
  readable main depth (`kDepthReaderClouds`, asked for through `Request` while Fancy draws). The depth is
  first copied to a 1x R32F image in a pass of its own (AO's resolve programs: sample 0 of an MSAA depth,
  a copy of a 1x or tiler-MSAA one), because the apply's pass, `DrawAoBeginApply`, has the main depth
  attached when it is the main target. The apply multiplies RGB only, as on DX11. Checked on Windows
  (RTX 5080, 1x, validation on: no message from the pass); MSAA and the S25 path are not yet run.
- **The depth (DX11)**: the bound depth copied whole with `CopyResource` (better_water's scene-depth method: no
  readable-depth rebuild), MSAA resolved to 1x R32F by AO's resolve. Every binding is saved and put back
  (`src/render/dx11/ao/ao_state_backup.h`, moved out of `ao_pass.cpp` for this second user).
- **The program** (`cloud_shadow.hlsl`, `g_bc_PSCloudShadow`): each opaque pixel's world position from
  the depth and the inverted view-projection (the engine's row-vector convention, built as
  `Context::SetCamera` builds it), followed up toward the sun to a flat layer `shadowHeight` above it,
  the sky's base and mid octaves and warp with the sky's offsets, multiplied into the colour (blend
  ZERO / SRC_COLOR, RGB only: the alpha holds the shore depth the water reads). The sky (depth 1) is
  left alone.
- **The terms** (`ShadowTermsFor`, pure): the sky's coverage, capped at 0.70 so gaps of sun remain under
  a winter sky; the strength weakened by bad weather (userPosition.w) and a low sun, not by winter alone
  (the snow keeps the trees' sun shadows, so it keeps the clouds' too); faded out with the fog, as AO is.

## 3. Seams (all `EXT SEAM (NOT RETAIL)` comments)
- `GameUpdateLoop::Load` (`gameupdateloop_load.cpp`): `SkyLoaded(m_skyMesh)` after the sky is built.
- `GameUpdateLoop::Update` (`gameupdateloop.cpp`): `Advance(time, speedMultiplier)` where the game time
  advances; `BeginFrame()` in the ext block; `Forget()` before the sky is removed in `Shutdown`.
- `registry_boot.cpp`: `OnVideoLost` / `OnVideoRestored` on the main loop's device restart hooks (the
  installed programs back before the restart walk reloads them; the noise released and made again from
  its kept levels).
- `material_setup.cpp`: the shared `_extAfterSetup` dispatcher.

## 4. Rule 7
Nothing from the game ships: the program and the noise are ours. The installed sky's cube map, vertex
program and render state are used from the player's install at run time.

## 5. Checked in the game
- The log line `[BetterClouds] sky material ...: 2 of 3 passes draw the clouds` (the sky and the
  reflection; the depth pass has no vertex program).
- Level-camera screenshots eleven seconds apart: the cloud banks move across the sky, the reflection
  follows.
- The look tuned live by the user in the overlay.
- Fancy: `[BetterClouds] shadow pass made` and `shadow depth copy ...: made`; the shadows checked by the
  user in game (they stay on the ground as the camera moves; winter keeps them after the snow fix).

## 6. Tests
`tests/unit/ext_better_clouds_test.cpp`: the noise tiles, is equalised and deterministic; the motion's
3x cap, pause, one wind direction for every octave, wrap over an hour at 3x and wind scale; the
constants carry the sun and the look; morph 0 moves every octave the same world distance; the shadow
terms (coverage cap, weather, winter, night, fog); the shadow program is a ps_4_0 container; both generated programs are ps_4_0 containers that fit the
installed sky's vertex outputs, and the fit test refuses a moved, narrowed or missing direction and
garbage; the row is class A ON with its mode key. `ext_registry_test.cpp`'s
`ext_registry_fits_the_extensions_dialog` holds the registry to the Extensions dialog's row count.
