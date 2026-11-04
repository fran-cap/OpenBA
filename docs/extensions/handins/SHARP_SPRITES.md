# SHARP_SPRITES: crisp menu sprites for ext.sharp_ui

`ext.sharp_ui` (class A, default on) draws the game's dialog sprites, and our own Extensions sheet, from a
copy of their sheet 2 or 4 times the size while the UI is magnified, with our own art for the menu chrome
and icons. No retail pixel is shipped: the copy is built at load from the player's own sheet. Facts here; the session log of the icon hunt and
the maintainer's rulings stayed out of the tree.

## 1. Why the copy is built at load

The game draws its dialog sprites from ONE sheet, `Dialog/SpriteSheet.rsc`: 549 sprites in a 1024 x 1024
BGRA8 atlas, including the toolbar's building icons and the painted resource art. The existing 2x twin
path (`<name>2x.rsc`, SHARPUI.md §12) needs a COMPLETE twin, because the swap changes the texture of a
whole draw batch and a missing sprite would draw nothing. A shipped twin would therefore have to carry
enlarged copies of every retail sprite, which AGENTS.md §1 forbids.

So `src/ext/sharp_ui/sharp_scaled.cpp`, at each selection (the first magnified frame, a slider or
back-buffer change, a load's factory Clean; never per frame), for each sheet we have art for
(`Dialog/SpriteSheet.rsc` and our `Dialog/ExtensionsSprites.rsc`, `KindOfSheet`):
1. reads the sheet's diffuse texture back from the GPU (`ImageBuffer::CopyFromTexture`; a texture that is
   not plain BGRA8, e.g. a mod's compressed one, keeps retail's sheet, one `[SharpUI]` line) and hashes
   its pixels and sprite rectangles: a build of the same content and factor made earlier in the session
   is reused. Keyed by CONTENT, not address: a Clean frees a sheet and the next load makes a new one, often
   at the same address, and the Extensions sheet is rebuilt at every Clean;
2. builds an atlas `scale` times larger (`sharp_sheet.h`, pure, unit-tested): every sprite rectangle
   with its filtering pad ring is upscaled bilinearly, sampling only inside itself; then each sprite we
   have art for (matched by NAME, and only when the art is exactly `scale` x the record's size, so a
   modded sheet keeps its own) is written over its rectangle and its pad ring refilled from the art's
   edge, `scale` texels wide;
3. creates the texture (below) and a MaterialInstance on the sheet's own base material
   (`ext/resources/sheet_builder.h`'s Fill*) and installs a swap with `material` set
   and no twin table: the batch keeps the BASE UVs (every rectangle is the base one times the factor) and
   draws from our material (`ui/SharpDraw.h`, `spritecontext_flush.cpp`).

A record's `x` / `y` are NOT the sprite's atlas position (the kit writes its pad offset, 1 / 1); the UVs
are (`RectFromUvs`). The first in-game run built the copy from x / y and drew no UI at all.

The texture:
- **2x**: one BGRA8 level, as the sheet itself, no mips. The UI sampler takes the NEAREST mip
  (`MIN_MAG_LINEAR_MIP_POINT`, retail's filter 1), so at slider 75 a mip would be a 1x-sized level
  magnified, softer than the 2x level minified.
- **4x**: BC3 with the full mip chain (`sharp_encode.h`, pure, unit-tested: alpha-weighted 2 x 2 box mips;
  BC1 colour by principal axis + least-squares refinement, BC4 alpha), ~21 MB instead of 64 MB. Encoded on
  up to 8 threads (4096 x 4096 + chain ~150 ms at -O2) and stored in the derived-data cache
  (`ext/derived_cache`, producer `SHSC`, keyed by the built atlas's bytes; bump `kCacheVersion` in
  `sharp_scaled.cpp` when the encode changes); later starts read it back. A GPU without BC (Android) gets
  it as ETC2 through the Vulkan backend's existing BC3 -> ETC2 transcode. With the nearest-mip sampler the
  4x copy draws its 2x-sized level from 2 to 2.83 px per unit and its top level above.

THE BUILD IS AT LOAD, OFF THE FRAME. The selection reads the sheet back and hashes it on the main thread
(a few ms), then a worker thread does the rest from copies (the upscale, our art, the BC3 chain or its
cache read); the main thread makes the texture and the material when a selection next asks. Two load-time
seams start the workers before the menu draws (`Prewarm`, for the UI scale in the settings):
- the loading splash's `GameLoadDialogLoopT::Update`, every frame: the dialog sheet exists from boot
  (`GameContainer_Init`), so its copy is built while the splash shows;
- `MenuDLT::Startup` after its dialogs load (`PrewarmAndWait`): our Extensions sheet is provided then, so
  that copy is started there and the startup waits for the workers (at most 2 s; ~15 ms at 2x).
The menu's first frame finds both built. A later change (the slider, the Sharp UI level) builds on a worker
while retail's sheet is drawn; the frame poll (`ScaledSheetJobFinished`) selects again when it is done. A
worker of a factor no longer wanted is dropped when it finishes. Worker time (debug exe; `sharp_sheet.cpp`
and `sharp_encode.cpp` are -O2 in every flavour): 2x 13 / 30 ms, 4x encoded ~100 / ~265 ms (Extensions /
dialog). The objects are kept for the process (a
frame in flight may still sample them). A deployed `<name>2x.rsc` twin still wins over the built copy.

Trace (stderr, once per build and on the existing engage line):
`[SharpUI] 4x dialog sheet built: 4096 x 4096 BC3 + mips, encoded, 220 of 549 sprites from our art, 264.1 ms
on a worker, drawn 280.5 ms after it was asked for`, and the engage line `[SharpUI] on scale=0.3000 ...
2x=0 sheet=0` (the copy not in yet) / `sheet=4`.

The level is the Enhanced tab's "Sharp UI" combo (`src/ext/sharp_ui/enhanced_entry.cpp`): Off (the row
`ext.sharp_ui` off), Text (the default: scale-matched fonts and whole-pixel quads, `ext.sharp_ui_icons` 0)
and Text + icons (`ext.sharp_ui_icons` 1: also the sheet copies and deployed twins). Live: the selection
is redone when the key changes.

## 2. The art

| | Tool | Output |
|---|---|---|
| Icons (69 pictures) | `tools/sharp_sprites/bake.py` | `src/ext/sharp_ui/art/<N>x/{ButtonIcons,Misc,ImageStatus,StatusBar}/` |
| Chrome (24 pictures: scroll bars, slider, progress bar, frames) | `tools/sharp_sprites/chrome.py` | `src/ext/sharp_ui/art/<N>x/{Scroll,Slider,Progress,Borders}/` |
| Extensions-sheet icons (3 pictures) | `tools/ext_sprites.py` | `src/ext/sharp_ui/art/<N>x/Extensions/` |
| Exe data | `tools/sharp_sprites/pack.py` | `src/ext/sharp_ui/sharp_sprites_data_<N>x.h`, namespace `Ext::SharpUI::SpritesData<N>x` |

Art is baked at N = 2 and 4. Each header holds `kScale`, `kSprites` / `kSpriteCount` (220 sprites of the
game's `Dialog/SpriteSheet.rsc`) and `kExtSprites` / `kExtSpriteCount` (3 sprites of our own
`Dialog/ExtensionsSprites.rsc`), same `ArtSprite` shape: 495 KB of pixels at 2x, 1982 KB at 4x.

- The icons are third-party SVGs (Phosphor, Tabler, Lucide, Bootstrap Icons, game-icons.net:
  `tools/sharp_sprites/svg/LICENSES.md`, `THIRD_PARTY_NOTICES.md`) plus five shapes of our own
  (`svg/own/`, written by `bake.py`), recoloured, combined, cut and mirrored by the rules in `bake.py`'s
  `build_specs()`, rendered with resvg.
- `ButtonIcons/OptionsGraphics` is our own upright flat brush (`gen_brush`, parameters in `BRUSH`):
  bristle block, gap, ferrule, tapered neck, round-ended handle, as tall as the glyph box (Phosphor's broad
  brush, which it replaced, is squat and drew at half the height). `ButtonIcons/Scenario` (the Mods
  button) is Lucide circle-play with its triangle filled.
- `ButtonIcons/Options` is our own two-gear composite (`gen_gears`, parameters in `GEARS`): an 8-tooth and
  a 6-tooth spur gear of the same module (2.25), meshed on the down-right diagonal (big upper left, small
  lower right, pitch circles 0.8 apart), a big tooth pointing into a small-gear gap; trapezoid teeth 3.4
  deep (base 0.60, flat top 0.36 of the tooth period), hub holes r 2.8 / 1.6; fitted into the retail glyph
  box like every icon. It replaced game-icons' "gears", whose source cuts off at the edge.
- The chrome is our own parametric drawing (bevelled rectangles, gradients, rings, shadow, glow, grip
  ticks, the slider thumb), parameters measured once from the retail sprites. Nothing is traced. Geometry
  is in retail pixels, supersampled 4 x 4 per output texel at any factor: a 1 px bevel is N texels wide,
  every slice margin N times retail's.
- The Extensions-sheet icons are the parametric drawings of `tools/ext_sprites.py` drawn natively at the
  factor (not upscaled): `ExtensionsIcon` (the main-menu gear, 16 px at 1x), `ToolbarExtensions` (the same
  gear at the toolbar's 32 px; the PNG is `Toolbar_extensions.png`) and `GameFilesIcon` (ext.phone_menu's
  floppy, 16 px). The table carries the sprite names of `ExtensionsSprites.rsc`; the 1x PNGs under
  `src/ext/ui/Dialog/Sprite/Extensions/` are unchanged by the 2x / 4x draw.
- Colours sampled from retail sprites are moved 1-2 values per channel before use; the sampled values are
  never written. Pure white stays white.
- What the bake needs from the game is frozen in `tools/sharp_sprites/measurements.json` (sizes, glyph
  boxes, bar weights, the nudged colours): a bake needs neither the kit nor the game. `bake.py --measure
  <ToolKit>/resource/Dialog/Sprite` rewrites it. `tools/sharp_sprites/sheet_map.json` is the kit's sprite
  table for our pictures (name, picture, sub-rectangle: the nine pieces of a frame are sub-rectangles of one
  picture); `pack.py --map <ToolKit>/resource/Dialog/SpriteSheet.rsc` rewrites it.
- `HScrollHighlight` / `VScrollHighlight` are drawn but no sheet sprite uses them; they are not packed.

Regenerate after a change (Python with `tools/sharp_sprites/requirements.txt`, from the repo root;
`ext_sprites.py` writes both factors and the 1x sheet):
```
python -I tools/ext_sprites.py
python -I tools/sharp_sprites/bake.py --scale 2 && python -I tools/sharp_sprites/chrome.py --scale 2
python -I tools/sharp_sprites/bake.py --scale 4 && python -I tools/sharp_sprites/chrome.py --scale 4
python -I tools/sharp_sprites/pack.py --scale 2 && python -I tools/sharp_sprites/pack.py --scale 4
```

## 3. The factor

Everything takes the factor as a parameter (`bake.py --scale N`, `chrome.py --scale N`, `pack.py
--scale N`, `BuildScaledSheet(scale)`; the unit tests run 2 and 4). 2x and 4x ship.

The pick (`PickSheetFactor`, `sharp_math.h`, unit-tested): none unless the UI is magnified on both axes
(more than 1 device pixel per layout unit: slider below 100); then the smallest factor at or above the
larger axis's px per unit, so a copy is never magnified while a bigger one exists; 4x beyond 4. Ties go
to the smaller (2.0 exactly is the 2x copy, texel for pixel). The ratio is back-buffer pixels per layout
unit, which already holds everything that matters:
- the slider (retail 75-150 %: 1.33 at most, so Windows draws 2x; ext.phone_menu's lower floor reaches 4x);
- the window density (Retina 2: up to 2.67, 4x);
- the render scale (Android): the UI is drawn into the back buffer, `_uiScale` = uiPercent / renderPercent
  (`platform/common/ui_render_scale.h`), so a Half back buffer halves the ratio, as it should;
- not Windows display scaling: the exe is DPI-unaware, as retail (`src/app/main.cpp`), so the back buffer
  never sees it.

Slider values above 100 shrink the UI (`_uiScale` is layout units per device pixel, SHARPUI.md §1) and
keep retail's sheet.

## 4. Verified

- `tools/build.sh unit`: `ext_sharp_sheet_*` (rect from UVs, rect scaling at 2 and 4, the name + size art
  match, the per-sprite upscale with no neighbour bleed, bilinear values, art placement with its ring and
  the RGBA -> BGRA swap at 2 and 4, both generated tables at both factors), `ext_sharp_encode_*` (mip
  sizes, alpha-weighted mips, flat / two-colour / transparent blocks, sub-4 levels, gradient PSNR 43 dB,
  thread-count independence), `ext_sharp_ui_pick_sheet_factor`.
- The gate (`tools/build.sh run`, faithful profile: the extension off).
- Autostarted town, extension on (debug exe, functional dir, 2528 x 1345, `BANISHED_SCREENSHOT_AT=600,ui`):
  slider 75 builds both 2x copies (Extensions 3 of 114 sprites from our art, dialog 220 of 549), the load
  rebuilds neither; slider 30 (3.34 px per unit) builds both 4x copies as BC3, encoded on the first run
  and read from the cache on the second. Frames and our icons crisp, no seams, no visible block
  artefacts; the toolbar's building icons are retail's, upscaled.
- By hand earlier: the main menu at 50 (2.0, point-sampled).

## 5. Open

- Device-lost: the built texture is made by us, not loaded through the factory, so a device restart does
  not recreate it. Not exercised.
- The main-menu gear and the phone menu's floppy are placed (the build line counts them) but were not
  looked at in this pass: the harness screenshot is in-game only.
- Every selection reads each sheet back (4 MB for the dialog sheet, a GPU sync) to hash it: on a slider
  change or a load, never per frame.
- Retail's Windows slider stops at 50 (2.0 px per unit), so Windows never draws the 4x copy; Retina and
  ext.phone_menu do. A `GameUIScale` below 50 in settings.cfg reaches it for a test.
- Next art passes (low-resolution sprites first): professions, the dialog work icons, the toolbar, the
  create icons, the small event icons; achievements last; painted resource art skipped.
