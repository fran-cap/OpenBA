"""tools/ext_icons/touchpointer.py -- the white pictogram for the touch_pointer row (programmer art, our own).

Same conventions as touchmode.py: draw(px) paints the 12 x 12 interior (x, y in [2, 13]) of a 16 x 16 tile.
'#' = white, 'k' = the dark accent, '.' = leave the tier colour showing.
"""

WHITE = (255, 255, 255, 255)
DARK = (42, 42, 42, 255)


def _paint(px, rows):
    assert len(rows) == 12 and all(len(r) == 12 for r in rows), rows
    for j, row in enumerate(rows):
        for i, ch in enumerate(row):
            if ch == "#":
                px[2 + i, 2 + j] = WHITE
            elif ch == "k":
                px[2 + i, 2 + j] = DARK


def draw_touch_pointer(px):
    """A pointing finger under a small dark aim mark: the cursor a finger drives."""
    _paint(px, [
        "....k.......",
        "...kkk......",
        "....k.......",
        "....##......",
        "....##......",
        "....######..",
        "....#######.",
        "..#########.",
        "..#########.",
        "...########.",
        "....######..",
        "....######..",
    ])


ICONS = {
    "touch_pointer": ("A", draw_touch_pointer),
}


def shift_glyph(size=48):
    """The action bar's Shift toggle: the keyboard's shift arrow as an outline (a hollow up arrow on a short stem),
    48 x 48 white with antialiased alpha like terrain_tools.py's glyphs. Drawn at 4x and box-filtered down."""
    from PIL import Image, ImageDraw
    S = 4
    big = Image.new("L", (size * S, size * S), 0)
    d = ImageDraw.Draw(big)
    pts = [(24, 4), (43, 24), (33, 24), (33, 43), (15, 43), (15, 24), (5, 24)]
    scaled = [(x * S, y * S) for x, y in pts]
    d.line(scaled + [scaled[0], scaled[1]], fill=255, width=5 * S, joint="curve")
    alpha = big.resize((size, size), Image.BOX)
    img = Image.new("RGBA", (size, size), (255, 255, 255, 0))
    img.putalpha(alpha)
    px = img.load()
    ap = alpha.load()
    for y in range(size):
        for x in range(size):
            px[x, y] = (255, 255, 255, ap[x, y])
    return img


GLYPHS = {
    "pointer_shift": shift_glyph,
}
