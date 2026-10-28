"""tools/ext_icons/townscreenshot.py -- the white pictogram for the town_screenshot row (programmer art, our own).

Same conventions as camera.py: draw(px) paints the 12 x 12 interior (x, y in [2, 13]) of a 16 x 16 tile.
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


def draw_town_screenshot(px):
    """A picture frame with a hill and a sun in it: a picture of the map."""
    _paint(px, [
        "############",
        "#..........#",
        "#.......##.#",
        "#.......##.#",
        "#....#.....#",
        "#...###....#",
        "#..#####...#",
        "#.#######..#",
        "#########..#",
        "#..........#",
        "#..........#",
        "############",
    ])


ICONS = {
    "town_screenshot": ("B", draw_town_screenshot),
}

# ---- the picture button of the Town Screenshot page: a white glyph, drawn at 4x and box-filtered down ----------
# (the same recipe as terrain_tools.py: 48 x 48, white with antialiased alpha and no outline, like the game's
# own white button icons)
S = 4
SIZE = 48


def _p(v):
    return v * S


def take_glyph(size=SIZE):
    """A camera: a body with a bump on top, a lens and a small flash window."""
    from PIL import Image, ImageDraw
    big = Image.new("L", (size * S, size * S), 0)
    d = ImageDraw.Draw(big)
    d.polygon([(_p(16), _p(10)), (_p(32), _p(10)), (_p(35), _p(16)), (_p(13), _p(16))], fill=255)
    d.rounded_rectangle([_p(5), _p(15), _p(43), _p(40)], radius=_p(4), fill=255)
    d.ellipse([_p(14.5), _p(18.5), _p(33.5), _p(37.5)], fill=0)       # the lens's ring is the body, cut out
    d.ellipse([_p(17), _p(21), _p(31), _p(35)], fill=255)             # the lens
    d.ellipse([_p(21), _p(25), _p(27), _p(31)], fill=0)               # its glass
    d.rectangle([_p(36), _p(19), _p(40), _p(22)], fill=0)             # the flash window
    alpha = big.resize((size, size), Image.BOX)
    img = Image.new("RGBA", (size, size), (255, 255, 255, 0))
    img.putalpha(alpha)
    px, ap = img.load(), alpha.load()
    for y in range(size):
        for x in range(size):
            px[x, y] = (255, 255, 255, ap[x, y])
    return img


def view_glyph(size=SIZE):
    """Viewfinder corners round a small square: what is on the screen."""
    from PIL import Image, ImageDraw
    big = Image.new("L", (size * S, size * S), 0)
    d = ImageDraw.Draw(big)
    lo, hi, arm, w = 8, 40, 11, 4
    for x, sx in ((lo, 1), (hi, -1)):
        for y, sy in ((lo, 1), (hi, -1)):
            # the corner at (x, y): a horizontal arm and a vertical arm, each w thick, sharing the corner square
            xs = sorted([x - sx * w / 2, x + sx * arm])
            ys = sorted([y - sy * w / 2, y + sy * arm])
            d.rectangle([_p(xs[0]), _p(y - w / 2), _p(xs[1]), _p(y + w / 2)], fill=255)
            d.rectangle([_p(x - w / 2), _p(ys[0]), _p(x + w / 2), _p(ys[1])], fill=255)
    d.rectangle([_p(20), _p(20), _p(28), _p(28)], fill=255)
    alpha = big.resize((size, size), Image.BOX)
    img = Image.new("RGBA", (size, size), (255, 255, 255, 0))
    img.putalpha(alpha)
    px, ap = img.load(), alpha.load()
    for y in range(size):
        for x in range(size):
            px[x, y] = (255, 255, 255, ap[x, y])
    return img


GLYPHS = {
    "town_screenshot_take": take_glyph,
    "town_screenshot_view": view_glyph,
}
