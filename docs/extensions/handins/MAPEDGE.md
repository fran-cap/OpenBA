# MAPEDGE: ext.map_edge_fade, the map's rim fades to black

The request (the maintainer, 2026-11-16, from a hand test of the wider zoom near the rim): "at the edge of the
map I think we need to add a nice black blur/fade out to hide the end of the map and the overdraw", as an
Off / On on the Enhanced tab.

## 1. What retail draws

The terrain is wider than the playable map: `WorldGeometry::_heightRowStride` is 769 samples on a 384-tile
(Medium) map, one sample a world unit, centred on the origin, so the ground runs -384 .. +384 while the
playable tiles (`Map::size`) run -192 .. +192. The border between the two is bare ground. At the terrain's
last row the mesh stops in a vertical cut, and nothing is drawn behind it. Retail's zoom (20 .. 130) and pan
clamp rarely bring that cut into view; `ext.better_camera`'s zoom (to 200) and `ext.camera_angle`'s shallow
tilt do.

## 2. The row

`ext.map_edge_fade`, class A (drawing only, nothing saved, live), **ON by default** (maintainer ruling
2026-11-16; the faithful profile turns it off with every other row). The Enhanced tab's "Map edge fade" Off / On is the row itself (`src/ext/map_edge_fade/enhanced_entry.cpp`).

| File | What |
|---|---|
| `src/ext/map_edge_fade/edge_math.h` | the look (`Look`: inset 32, outset 2 world units, the horizon band), `TerrainHalf`, `BandFor`, `Darkness` (the shader's expression on the CPU) |
| `src/ext/map_edge_fade/map_edge_fade.{h,cpp}` | `SceneDraw()`: the request, the terrain's stride, the pass's constants |
| `src/ext/scene_draw.cpp` | the scene-draw seam now arms the rim fade, the depth blur, or both (the fade first) |
| `src/render/common/map_edge/` | `map_edge_backend.h` (the contract), `map_edge.hlsl` (our own program), `map_edge_ps.h` (fxc ps_4_0, `tools/water_shaders.py`) |
| `src/render/{dx11,vulkan}/map_edge/map_edge_pass.cpp` | the pass; `src/render/metal/map_edge/` answers "not supported" |
| `src/render/common/render_backend_table*.h`, `multi/render_backend_dispatch.cpp` | the dual exe's forwards |
| `src/render/dx11/depth_copy/depth_copy.h`, `src/render/vulkan/draw.h` | a sixth reader bit of the shared depth copy |

## 3. The pass

At the scene-draw seam (after the main entry's last list: the water, the particles and the status icons are
in the picture; the tool and the UI are not), a full-screen triangle multiplies the main colour (RGB only):

* every pixel's world position from the shared depth copy;
* a pixel with nothing drawn (depth 1) takes the point where its view ray meets the ground plane, so what
  lies past the terrain is black; a ray that goes up keeps the sky, and just under the horizon the black
  gives way to the sky over a narrow band (the eye-level camera);
* the distance past a square `inset` inside the terrain's edge, round at the corners, over `inset + outset`
  world units, through a smoothstep: 0 inside, black from `outset` past the edge.

The fade lies wholly in the border (192 tiles wide on a Medium map, the inset is 32): no playable tile is
darkened. With `ext.depth_of_field`'s blur on, the blur runs after it and blurs the faded rim with the rest.
`ext.town_screenshot`'s tiles are drawn without it.

## 4. Verified, and not

* VERIFIED: unit (`ext_map_edge_fade_*`: the row, the band, the darkness; the registry and Enhanced-table
  pins). The debug exe builds.
* VERIFIED in game, by engine screenshot (seed 12345, a Medium map, camera pinned at the east terrain edge,
  360,0 at distance 150): DX11 with the row on and off; Vulkan with the row on and the depth blur on. The cut
  is hidden on both; the off shot is retail's. Empty crash logs.
* VERIFIED: the gate (`tools/build.sh run`, the faithful profile: the row off) and the resave check PASS.
* LOOKED AT by the maintainer (2026-11-16, a DX11 debug run, seed 511106701): "fade looks good".
* NOT RUN: the eye-level camera's horizon band; Small and Large maps; the Vulkan 1.1 path (the pass answers
  "not supported" there, as the other full-screen passes do). No tweak-overlay panel yet.

## 5. Open rulings

1. Default: ruled ON (2026-11-16). The Android first run does not seed it off: the phone pays a depth copy
   and a full-screen pass for it; not measured there.
2. The inset (32 world units) and the outset (2): tune by eye; a tweak-overlay panel can be added.
