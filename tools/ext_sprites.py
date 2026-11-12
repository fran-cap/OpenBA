#!/usr/bin/env python3
"""tools/ext_sprites.py -- draw the Extensions programmer-art sprites (NOT RETAIL art).

Writes src/ext/ui/Dialog/Sprite/Extensions/{ExtensionsIcon,OptionsEnhanced,TierFix,TierA,TierB,TierC}.png, 16 x 16
RGBA like the Mods button's "Mod" sprite (the kit's Dialog/Sprite/ButtonIcons/Scenario.png). The
PNGs are committed; rerun this only to regenerate the placeholder art. The user replaces the PNGs
with real art later (tools/ext_resources.sh recompiles them into the sprite sheet). Also writes the 2x / 4x
ExtensionsIcon, Toolbar_extensions, Toolbar_road_upgrade and GameFilesIcon for ext.sharp_ui (SHARP_SPRITES below).

usage: python tools/ext_sprites.py        (from the repo root; needs Pillow)
"""
import os
from PIL import Image

OUT = os.path.join("src", "ext", "ui", "Dialog", "Sprite", "Extensions")
W = H = 16
CLEAR = (0, 0, 0, 0)
RIM = (42, 42, 42, 255)
FILL = (232, 232, 232, 255)


def icon(size=W):
    """A cog (size x size; 16 by default, 32 for the toolbar button) with a plus cut into its hub, white with antialiased alpha and NO outline, like the
    kit's 16 x 16 ButtonIcons (Scenario / Achievement / Options are pure white silhouettes with
    27-79 alpha levels; the dark-rimmed version this replaces was the only outlined icon on the
    main menu, the user 2026-10-17). Drawn at 8x and box-filtered down for the soft edges."""
    S = 8
    W = H = size
    big = Image.new("L", (W * S, H * S), 0)
    px = big.load()
    cx, cy, s = (W * S - 1) * 0.5, (H * S - 1) * 0.5, W * S * 0.5
    for y in range(H * S):
        for x in range(W * S):
            dx, dy = (x - cx) / s, (y - cy) / s
            r2 = dx * dx + dy * dy
            ax, ay = abs(dx), abs(dy)
            diagonal = ax > 0.38 * (ax + ay) and ay > 0.38 * (ax + ay)
            axial = ax < 0.18 or ay < 0.18
            outer = 0.96 if (axial or diagonal) else 0.78
            on = r2 <= outer * outer
            if r2 <= 0.42 ** 2:                      # the hub ring
                on = True
            if r2 <= 0.30 ** 2:                      # the hub's hole, with the plus cut out of the ring
                on = False
            if (ax < 0.07 and ay < 0.40) or (ay < 0.07 and ax < 0.40):
                on = r2 > 0.42 ** 2 and on            # the plus: clear across the hub, kept outside it
            px[x, y] = 255 if on else 0
    alpha = big.resize((W, H), Image.BOX)
    img = Image.new("RGBA", (W, H), CLEAR)
    img.putalpha(alpha)
    ipx, apx = img.load(), alpha.load()
    for y in range(H):
        for x in range(W):
            ipx[x, y] = (255, 255, 255, apx[x, y])
    return img


def road_upgrade_icon(size=32):
    """ext.road_upgrade's toolbar button (src/ext/road_upgrade): a dirt road tile (an outline with loose
    pebbles), an arrow, a stone road tile (laid stones): a dirt road becomes a stone road. White with
    antialiased alpha and no outline like icon(), at the toolbar icons' 32 px; our own drawing, not the
    game's road icons. Drawn at 8x on a 32-unit grid and box-filtered down."""
    from PIL import ImageDraw
    S = 8
    k = size * S / 32.0
    big = Image.new("L", (size * S, size * S), 0)
    d = ImageDraw.Draw(big)

    def box(x0, y0, x1, y1, fill=255):
        d.rectangle([x0 * k, y0 * k, x1 * k - 1, y1 * k - 1], fill=fill)

    # the dirt tile: a frame with pebbles
    box(1, 8, 12, 24)
    box(2.5, 9.5, 10.5, 22.5, fill=0)
    for cx, cy in ((4.75, 12), (8.25, 14), (5, 16.5), (8.25, 19), (4.75, 20.5)):
        d.ellipse([(cx - 1.0) * k, (cy - 1.0) * k, (cx + 1.0) * k - 1, (cy + 1.0) * k - 1], fill=255)
    # the arrow
    box(13, 15, 16.5, 17)
    d.polygon([(16 * k, 12.5 * k), (19.5 * k, 16 * k), (16 * k, 19.5 * k)], fill=255)
    # the stone tile: four courses of laid stones, the joints staggered
    for row, y in enumerate((8, 12.25, 16.5, 20.75)):
        xs = ((20.5, 25.5), (26.5, 31)) if row % 2 == 0 else ((20.5, 22.5), (23.5, 28), (29, 31))
        for x0, x1 in xs:
            box(x0, y, x1, y + 3.25)
    alpha = big.resize((size, size), Image.BOX)
    img = Image.new("RGBA", (size, size), CLEAR)
    img.putalpha(alpha)
    ipx, apx = img.load(), alpha.load()
    for y in range(size):
        for x in range(size):
            ipx[x, y] = (255, 255, 255, apx[x, y])
    return img


def game_files_icon(size=W):
    """ext.phone_menu's "Game files" menu button (issue #96): a floppy disk, white with
    antialiased alpha and no outline like icon(): the body with its top-right corner cut, the shutter
    slot near the top and the label window at the bottom cut out. The geometry is in 16 px units, drawn
    at `size` (16 by default; 32 / 64 for ext.sharp_ui's magnified sheets) at 8x, box-filtered down."""
    S = 8
    W = H = size
    k = S * size / 16.0                                      # samples per 16 px design unit
    big = Image.new("L", (W * S, H * S), 0)
    px = big.load()
    for y in range(H * S):
        for x in range(W * S):
            fx, fy = (x + 0.5) / k, (y + 0.5) / k
            on = 1.0 <= fx <= 15.0 and 1.0 <= fy <= 15.0
            if fx - fy > 11.0:                               # the cut top-right corner
                on = False
            if 4.0 <= fx <= 10.5 and 1.0 <= fy <= 5.5:       # the shutter's window
                on = False
            if 8.0 <= fx <= 9.5 and 1.8 <= fy <= 4.7:        # the shutter's slot stays solid
                on = True
            if 3.5 <= fx <= 12.5 and 8.5 <= fy <= 13.5:      # the label window
                on = False
            px[x, y] = 255 if on else 0
    alpha = big.resize((W, H), Image.BOX)
    img = Image.new("RGBA", (W, H), CLEAR)
    ipx, apx = img.load(), alpha.load()
    for y in range(H):
        for x in range(W):
            ipx[x, y] = (255, 255, 255, apx[x, y])
    return img


def options_enhanced():
    """The Options dialog's "Enhanced" tab icon (block AOMENU): a ball resting in a soft contact
    shadow, white with alpha like retail's 16 x 16 OptionsGraphics / OptionsSound tab icons."""
    img = Image.new("RGBA", (W, H), CLEAR)
    px = img.load()
    for y in range(H):
        for x in range(W):
            a = 0.0
            # the contact shadow: a flat ellipse under the ball, darkest at its centre
            ex, ey = (x - 7.5) / 7.5, (y - 13.0) / 2.2
            e2 = ex * ex + ey * ey
            if e2 <= 1.0:
                a = 0.55 * (1.0 - e2)
            # the ball, lit from the top left: full alpha, dimmer toward the occluded bottom
            bx, by = (x - 7.5) / 5.5, (y - 6.5) / 5.5
            b2 = bx * bx + by * by
            if b2 <= 1.0:
                a = max(a, 1.0 - 0.45 * max(0.0, (bx + by) * 0.5 + 0.2))
            if a > 0.0:
                px[x, y] = (255, 255, 255, int(round(255 * min(1.0, a))))
    return img


# 5 x 7 letters, scaled x2 into a 10 x 14 box.
LETTERS = {
    "F": ["11111", "10000", "10000", "11110", "10000", "10000", "10000"],
    "A": ["01110", "10001", "10001", "11111", "10001", "10001", "10001"],
    "B": ["11110", "10001", "10001", "11110", "10001", "10001", "11110"],
    "C": ["01110", "10001", "10000", "10000", "10000", "10001", "01110"],
}


def glyph(letter, colour):
    """A rounded tile in the tier's colour with the letter in white."""
    img = Image.new("RGBA", (W, H), CLEAR)
    px = img.load()
    for y in range(H):
        for x in range(W):
            corner = (x in (0, W - 1)) and (y in (0, H - 1))
            if corner:
                continue
            edge = x in (0, W - 1) or y in (0, H - 1)
            px[x, y] = RIM if edge else colour
    rows = LETTERS[letter]
    ox, oy = 3, 1
    for j, row in enumerate(rows):
        for i, bit in enumerate(row):
            if bit == "1":
                for dy in range(2):
                    for dx in range(2):
                        px[ox + 2 * i + dx, oy + 2 * j + dy] = (255, 255, 255, 255)
    return img


# Per-entry row icons: the tier's tile with a white pictogram in place of the letter. Each
# tools/ext_icons/<group>.py holds ICONS = {registry id: draw(px)}; draw paints white (or
# RIM-dark) pixels into the 12 x 12 interior x, y in [2, 13] of a 16 x 16 tile.
ICON_SCALE = 3   # saved 48 x 48 (Mod.rsc:imageIcon's size), pixel-tripled so the sheet does not blur it
TIER_COLOUR = {
    "fix": (46, 125, 50, 255),     # green: a fix
    "A": (30, 100, 170, 255),      # blue: live
    "B": (190, 120, 20, 255),      # amber: locked
    "C": (170, 40, 40, 255),       # red: extends the save
}


def tile(colour):
    """The tier's rounded tile: dark rim, colour fill, no letter."""
    img = Image.new("RGBA", (W, H), CLEAR)
    px = img.load()
    for y in range(H):
        for x in range(W):
            if (x in (0, W - 1)) and (y in (0, H - 1)):
                continue
            px[x, y] = RIM if (x in (0, W - 1) or y in (0, H - 1)) else colour
    return img


def entry_icons():
    """{registry id: (tier key, draw)} from every tools/ext_icons/*.py module."""
    import importlib.util
    here = os.path.join(os.path.dirname(os.path.abspath(__file__)), "ext_icons")
    found = {}
    for name in sorted(os.listdir(here)):
        if not name.endswith(".py") or name.startswith("_"):
            continue
        spec = importlib.util.spec_from_file_location(name[:-3], os.path.join(here, name))
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)
        for entry_id, (tier, draw) in mod.ICONS.items():
            assert entry_id not in found, entry_id
            found[entry_id] = (tier, draw)
    return found


def glyph_sprites():
    """{name: draw(size) -> image} from the GLYPHS of every tools/ext_icons/*.py module: white picture-button glyphs."""
    import importlib.util
    here = os.path.join(os.path.dirname(os.path.abspath(__file__)), "ext_icons")
    found = {}
    for name in sorted(os.listdir(here)):
        if not name.endswith(".py") or name.startswith("_"):
            continue
        spec = importlib.util.spec_from_file_location(name[:-3], os.path.join(here, name))
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)
        for glyph_id, draw in getattr(mod, "GLYPHS", {}).items():
            assert glyph_id not in found, glyph_id
            found[glyph_id] = draw
    return found


# ext.sharp_ui's magnified sheets (docs/extensions/handins/SHARP_SPRITES.md): the parametric icons drawn
# natively at 2 and 4 x their size into src/ext/sharp_ui/art/<N>x/Extensions/; tools/sharp_sprites/pack.py
# packs them as kExtSprites under the sheet's sprite names (ExtensionsSprites.rsc).
SHARP_OUT = os.path.join("src", "ext", "sharp_ui", "art")
SHARP_FACTORS = (2, 4)
SHARP_SPRITES = [  # (png name, draw(size), 1x size)
    ("ExtensionsIcon", icon, 16),
    ("Toolbar_extensions", icon, 32),
    ("Toolbar_road_upgrade", road_upgrade_icon, 32),
    ("GameFilesIcon", game_files_icon, 16),
]


def main():
    os.makedirs(OUT, exist_ok=True)
    icon().save(os.path.join(OUT, "ExtensionsIcon.png"))
    options_enhanced().save(os.path.join(OUT, "OptionsEnhanced.png"))
    game_files_icon().save(os.path.join(OUT, "GameFilesIcon.png"))
    icon(32).save(os.path.join(OUT, "Toolbar_extensions.png"))   # the toolbar's one extension button: the cog at the toolbar icons' 32 px
    road_upgrade_icon(32).save(os.path.join(OUT, "Toolbar_road_upgrade.png"))   # ext.road_upgrade's tool button in the roads group
    glyph("F", TIER_COLOUR["fix"]).save(os.path.join(OUT, "TierFix.png"))
    glyph("A", TIER_COLOUR["A"]).save(os.path.join(OUT, "TierA.png"))
    glyph("B", TIER_COLOUR["B"]).save(os.path.join(OUT, "TierB.png"))
    glyph("C", TIER_COLOUR["C"]).save(os.path.join(OUT, "TierC.png"))
    icons = entry_icons()
    for entry_id, (tier, draw) in sorted(icons.items()):
        img = tile(TIER_COLOUR[tier])
        draw(img.load())
        img = img.resize((W * ICON_SCALE, H * ICON_SCALE), Image.NEAREST)
        img.save(os.path.join(OUT, "Entry_%s.png" % entry_id))
    glyphs = glyph_sprites()
    for glyph_id, draw in sorted(glyphs.items()):
        draw().save(os.path.join(OUT, "Glyph_%s.png" % glyph_id))
    print("wrote %d sprites to %s" % (8 + len(icons) + len(glyphs), OUT))
    for factor in SHARP_FACTORS:
        out = os.path.join(SHARP_OUT, "%dx" % factor, "Extensions")
        os.makedirs(out, exist_ok=True)
        for name, draw, size in SHARP_SPRITES:
            draw(size * factor).save(os.path.join(out, name + ".png"))
        print("wrote %d sprites to %s" % (len(SHARP_SPRITES), out))


if __name__ == "__main__":
    main()
