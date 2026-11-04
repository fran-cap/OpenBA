# SHARP_SPRITES: crisp menu sprites for ext.sharp_ui

`ext.sharp_ui` (class A, default on) draws the game's dialog sprites from a copy of their sheet twice the
size while the UI is magnified, with our own art for the menu chrome and icons. No retail pixel is shipped:
the copy is built at load from the player's own sheet. Facts here; the session log of the icon hunt and
the maintainer's rulings stayed out of the tree.

## 1. Why the copy is built at load

The game draws its dialog sprites from ONE sheet, `Dialog/SpriteSheet.rsc`: 549 sprites in a 1024 x 1024
BGRA8 atlas, including the toolbar's building icons and the painted resource art. The existing 2x twin
path (`<name>2x.rsc`, SHARPUI.md §12) needs a COMPLETE twin, because the swap changes the texture of a
whole draw batch and a missing sprite would draw nothing. A shipped twin would therefore have to carry
enlarged copies of every retail sprite, which AGENTS.md §1 forbids.

So `src/ext/sharp_ui/sharp_scaled.cpp`, on the first magnified frame:
1. reads the sheet's diffuse texture back from the GPU (`ImageBuffer::CopyFromTexture`; a texture that is
   not plain BGRA8, e.g. a mod's compressed one, keeps retail's sheet, one `[SharpUI]` line);
2. builds an atlas `scale` times larger (`sharp_sheet.h`, pure, unit-tested): every sprite rectangle
   with its filtering pad ring is upscaled bilinearly, sampling only inside itself; then each sprite we
   have art for (matched by NAME, and only when the art is exactly `scale` x the record's size, so a
   modded sheet keeps its own) is written over its rectangle and its pad ring refilled from the art's
   edge, `scale` texels wide;
3. creates the texture and a MaterialInstance on the sheet's own base material
   (`ext/resources/sheet_builder.h`'s Fill* / CreateTexture) and installs a swap with `material` set
   and no twin table: the batch keeps the BASE UVs (every rectangle is the base one times the factor) and
   draws from our material (`ui/SharpDraw.h`, `spritecontext_flush.cpp`).

A record's `x` / `y` are NOT the sprite's atlas position (the kit writes its pad offset, 1 / 1); the UVs
are (`RectFromUvs`). The first in-game run built the copy from x / y and drew no UI at all.

The build takes 65-90 ms (debug exe) once; the objects are kept for the process (a frame in flight may
still sample them). A deployed `<name>2x.rsc` twin still wins over the built copy.

Trace (stderr, once per build and on the existing engage line):
`[SharpUI] 2x sheet built: 2048 x 2048, 220 of 549 sprites from our art, 68.0 ms`,
`[SharpUI] on scale=0.7500 ... 2x=0 sheet=2`.

## 2. The art

| | Tool | Output |
|---|---|---|
| Icons (69 pictures) | `tools/sharp_sprites/bake.py` | `src/ext/sharp_ui/art/<N>x/{ButtonIcons,Misc,ImageStatus,StatusBar}/` |
| Chrome (24 pictures: scroll bars, slider, progress bar, frames) | `tools/sharp_sprites/chrome.py` | `src/ext/sharp_ui/art/<N>x/{Scroll,Slider,Progress,Borders}/` |
| Exe data | `tools/sharp_sprites/pack.py` | `src/ext/sharp_ui/sharp_sprites_data.h` (220 sheet sprites, 471 KB of pixels at 2x) |

- The icons are third-party SVGs (Phosphor, Tabler, Lucide, Bootstrap Icons, game-icons.net:
  `tools/sharp_sprites/svg/LICENSES.md`, `THIRD_PARTY_NOTICES.md`) plus three shapes of our own, recoloured,
  combined, cut and mirrored by the rules in `bake.py`'s `build_specs()`, rendered with resvg.
- The chrome is our own parametric drawing (bevelled rectangles, gradients, rings, shadow, glow, grip
  ticks, the slider thumb), parameters measured once from the retail sprites. Nothing is traced.
- Colours sampled from retail sprites are moved 1-2 values per channel before use; the sampled values are
  never written. Pure white stays white.
- What the bake needs from the game is frozen in `tools/sharp_sprites/measurements.json` (sizes, glyph
  boxes, bar weights, the nudged colours): a bake needs neither the kit nor the game. `bake.py --measure
  <ToolKit>/resource/Dialog/Sprite` rewrites it. `tools/sharp_sprites/sheet_map.json` is the kit's sprite
  table for our pictures (name, picture, sub-rectangle: the nine pieces of a frame are sub-rectangles of one
  picture); `pack.py --map <ToolKit>/resource/Dialog/SpriteSheet.rsc` rewrites it.
- `HScrollHighlight` / `VScrollHighlight` are drawn but no sheet sprite uses them; they are not packed.

Regenerate after a change (Python with `tools/sharp_sprites/requirements.txt`):
```
python -I tools/sharp_sprites/bake.py && python -I tools/sharp_sprites/chrome.py && python -I tools/sharp_sprites/pack.py
```

## 3. The factor

Everything takes the factor as a parameter (`bake.py --scale N`, `chrome.py --scale N`, `pack.py
--scale N`, `BuildScaledSheet(scale)`; the unit tests run 2 and 4). Only 2x ships: retail's 75-150 %
slider reaches at most 1.33 device pixels per layout unit. A 4x set (Retina at density 2: up to ~2.67;
ext.phone_menu: up to 4) is a second generated header and one more arm in `PickSheetScale`
(`sharp_scaled.cpp`); nothing else changes.

The 2x copy engages whenever the UI is magnified (`_uiScale < 1`, slider below 100). Slider values above
100 shrink the UI (`_uiScale` is layout units per device pixel, SHARPUI.md §1) and keep retail's sheet.

## 4. Verified

- `tools/build.sh unit`: `ext_sharp_sheet_*` (rect from UVs, rect scaling at 2 and 4, the name + size art
  match, the per-sprite upscale with no neighbour bleed, bilinear values, art placement with its ring and
  the RGBA -> BGRA swap at 2 and 4, the generated table).
- The gate (`tools/build.sh run`, faithful profile: the extension off).
- By hand (debug exe, functional dir, 2528 x 1345 window): main menu and town HUD at slider 75 (1.33 px
  per unit) and the main menu at 50 (2.0, ext.phone_menu's range, point-sampled): every frame and icon
  of ours draws, crisp, no seams; the toolbar's building icons are retail's, upscaled.

## 5. Open

- Device-lost: the built texture is made by us, not loaded through the factory, so a device restart does
  not recreate it. Not exercised.
- The cache is keyed by the base texture's address: a sheet freed and reloaded at the same address would
  reuse the old copy (same file, so the same pixels, unless a mod swapped it mid-session).
- Next art passes (low-resolution sprites first): professions, the dialog work icons, the toolbar, the
  create icons, the small event icons; achievements last; painted resource art skipped.
