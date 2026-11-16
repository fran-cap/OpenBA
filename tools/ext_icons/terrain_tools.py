"""tools/ext_icons/terrain_tools.py -- the white glyphs of the Terrain page's picture buttons (programmer art, our own).

Unlike the other ext_icons modules these are not registry row icons: GLYPHS = {name: draw(size) -> RGBA image} and
tools/ext_sprites.py writes each as Glyph_<name>.png (48 x 48, white with antialiased alpha and no outline, like the
game's own white button icons). Drawn at 4x and box-filtered down.
"""
import math
from PIL import Image, ImageDraw

S = 4
SIZE = 48
ICONS = {}   # no registry row icons here


def _canvas(size):
    big = Image.new("L", (size * S, size * S), 0)
    return big, ImageDraw.Draw(big)


def _finish(big, size):
    alpha = big.resize((size, size), Image.BOX)
    img = Image.new("RGBA", (size, size), (255, 255, 255, 0))
    img.putalpha(alpha)
    px, ap = img.load(), alpha.load()
    for y in range(size):
        for x in range(size):
            px[x, y] = (255, 255, 255, ap[x, y])
    return img


def _p(v):
    return v * S


def _arrow(d, cx, top, bottom, up):
    """A fat arrow along x = cx between the y values `top` and `bottom`; the head at the top when `up`."""
    head = 11
    shaft = 4
    if up:
        d.polygon([(_p(cx), _p(top)), (_p(cx - head), _p(top + head)), (_p(cx + head), _p(top + head))], fill=255)
        d.rectangle([_p(cx - shaft), _p(top + head - 1), _p(cx + shaft), _p(bottom)], fill=255)
    else:
        d.polygon([(_p(cx), _p(bottom)), (_p(cx - head), _p(bottom - head)), (_p(cx + head), _p(bottom - head))],
                  fill=255)
        d.rectangle([_p(cx - shaft), _p(top), _p(cx + shaft), _p(bottom - head + 1)], fill=255)


def raise_glyph(size=SIZE):
    big, d = _canvas(size)
    _arrow(d, 24, 5, 32, True)
    d.rectangle([_p(7), _p(37), _p(41), _p(42)], fill=255)
    return _finish(big, size)


def lower_glyph(size=SIZE):
    big, d = _canvas(size)
    d.rectangle([_p(7), _p(6), _p(41), _p(11)], fill=255)
    _arrow(d, 24, 16, 43, False)
    return _finish(big, size)


def flatten_glyph(size=SIZE):
    big, d = _canvas(size)
    d.rectangle([_p(5), _p(21), _p(43), _p(27)], fill=255)
    # an arrow pressing down from above, one lifting from below, onto the bar
    d.polygon([(_p(24), _p(18)), (_p(15), _p(8)), (_p(33), _p(8))], fill=255)
    d.polygon([(_p(24), _p(30)), (_p(15), _p(40)), (_p(33), _p(40))], fill=255)
    return _finish(big, size)


def mountain_glyph(size=SIZE):
    big, d = _canvas(size)
    d.polygon([(_p(2), _p(41)), (_p(18), _p(10)), (_p(26), _p(25)), (_p(31), _p(17)), (_p(46), _p(41))], fill=255)
    return _finish(big, size)


def smooth_glyph(size=SIZE):
    big, d = _canvas(size)
    pts = []
    for i in range(0, 200):
        x = 4 + 40.0 * i / 199.0
        y = 24 - 10.0 * math.sin((x - 4) / 40.0 * 2.0 * math.pi)
        pts.append((_p(x), _p(y)))
    d.line(pts, fill=255, width=_p(6), joint="curve")
    for x, y in (pts[0], pts[-1]):
        r = _p(3)
        d.ellipse([x - r, y - r, x + r, y + r], fill=255)
    return _finish(big, size)


def undo_glyph(size=SIZE):
    big, d = _canvas(size)
    cx, cy, r = 25, 27, 13
    # an arc from the left just below the centre, over the top and round the right to the lower right
    pts = []
    for i in range(0, 181):
        a = math.radians(200 - i * 1.35)
        pts.append((_p(cx + r * math.cos(a)), _p(cy - r * math.sin(a))))
    d.line(pts, fill=255, width=_p(6), joint="curve")
    # the arrowhead at the arc's start, pointing back (left and down)
    sx, sy = cx + r * math.cos(math.radians(200)), cy - r * math.sin(math.radians(200))
    d.polygon([(_p(sx - 9), _p(sy + 3)), (_p(sx + 5), _p(sy - 11)), (_p(sx + 8), _p(sy + 7))], fill=255)
    return _finish(big, size)


def redo_glyph(size=SIZE):
    """Undo's arrow turned the other way (a mirror image)."""
    return undo_glyph(size).transpose(Image.FLIP_LEFT_RIGHT)


def plant_glyph(size=SIZE):
    """A tree: a round crown on a short trunk, with a ground line."""
    big, d = _canvas(size)
    d.ellipse([_p(9), _p(4), _p(39), _p(30)], fill=255)
    d.rectangle([_p(21), _p(28), _p(27), _p(40)], fill=255)
    d.rectangle([_p(10), _p(39), _p(38), _p(43)], fill=255)
    return _finish(big, size)


GLYPHS = {
    "terrain_raise": raise_glyph,
    "terrain_lower": lower_glyph,
    "terrain_flatten": flatten_glyph,
    "terrain_mountain": mountain_glyph,
    "terrain_smooth": smooth_glyph,
    "terrain_undo": undo_glyph,
    "terrain_redo": redo_glyph,
    "terrain_plant": plant_glyph,
}
