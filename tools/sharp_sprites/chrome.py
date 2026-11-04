"""tools/sharp_sprites/chrome.py -- the dialog chrome (scroll bars, slider, progress bar, frames) as
programmatic vector art for ext.sharp_ui's magnified sheet (NOT RETAIL; docs/extensions/handins/SHARP_SPRITES.md).

usage: python -I tools/sharp_sprites/chrome.py [--scale N] [--out DIR] [--stretch DIR]
       (default: --scale 2, out = src/ext/sharp_ui/art/<N>x/<Category>/<Name>.png)

The frames are our own drawing, built from generic primitives (a bevelled rectangle with rounded or cut
corners and a rim coloured by its normal, a convex polygon, gradients, rings, a Gaussian shadow, an
exponential glow, arrowhead grip ticks, the slider thumb) whose parameters -- sizes, slice margins, bevel
widths, falloff constants -- were measured once from the retail sprites. No pixel is copied and no outline
is traced. Every colour in the table below is already MOVED 1-2 values per channel from the value it was
sampled from (the maintainer's ruling: no colour is a clone of a retail one); the sampled values are not
kept. Geometry is in retail pixels (1x); a Canvas renders it at any integer scale, supersampled SS x SS and
box-filtered down (the method of tools/ext_icons/terrain_tools.py). --stretch writes each frame stretched
to 3x its size through its own margins (a seam check, not shipped).

Needs numpy and Pillow (tools/sharp_sprites/requirements.txt).
"""
import argparse
import math
import os

import numpy as np
from PIL import Image

SS = 4
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))


def save_png(img, path):
    """A PNG with no ancillary chunks, so a re-run writes the same bytes."""
    img.save(path, optimize=False, compress_level=9)


# ---------------------------------------------------------------------------------------------------------
# colour
# ---------------------------------------------------------------------------------------------------------
def C(hexstr):
    """Colour constant for the parameter table, as floats 0..1 (the values are already nudged)."""
    r, g, b = (int(hexstr[i:i + 2], 16) for i in (0, 2, 4))
    return np.array((r, g, b), dtype=np.float64) / 255.0


# ---------------------------------------------------------------------------------------------------------
# canvas: premultiplied float RGBA at scale * SS, composited with "over", box-filtered on output
# ---------------------------------------------------------------------------------------------------------
class Canvas:
    def __init__(self, w, h, scale):
        self.w, self.h, self.scale = w, h, scale
        self.k = scale * SS
        ys, xs = np.mgrid[0:h * self.k, 0:w * self.k]
        self.x = (xs + 0.5) / self.k          # sample position in retail pixels
        self.y = (ys + 0.5) / self.k
        self.rgb = np.zeros(self.x.shape + (3,))
        self.a = np.zeros(self.x.shape)

    def over(self, cov, colour):
        cov = np.clip(cov, 0.0, 1.0)
        col = colour if np.ndim(colour) == 3 else np.broadcast_to(colour, self.rgb.shape)
        self.rgb = col * cov[..., None] + self.rgb * (1.0 - cov[..., None])
        self.a = cov + self.a * (1.0 - cov)

    def image(self):
        k, H, W = SS, self.h * self.scale, self.w * self.scale
        rgb = self.rgb.reshape(H, k, W, k, 3).mean(axis=(1, 3))
        a = self.a.reshape(H, k, W, k).mean(axis=(1, 3))
        safe = np.where(a > 0, a, 1.0)
        out = np.dstack([rgb / safe[..., None], a])
        return Image.fromarray(np.round(np.clip(out, 0, 1) * 255).astype(np.uint8), "RGBA")


# ---------------------------------------------------------------------------------------------------------
# shapes: callables (x, y) -> (signed distance in retail px, outward normal nx, ny)
# ---------------------------------------------------------------------------------------------------------
def rrect(x0, y0, x1, y1, r=0.0, cut=0.0):
    """Rectangle [x0,x1) x [y0,y1) with rounded corners (radius r) or 45-degree cut corners (leg `cut`)."""
    Hx, Hy = (x1 - x0) / 2, (y1 - y0) / 2
    cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
    hx, hy = (x1 - x0) / 2 - r, (y1 - y0) / 2 - r

    def sdf(x, y):
        px, py = x - cx, y - cy
        qx, qy = np.abs(px) - hx, np.abs(py) - hy
        ox, oy = np.maximum(qx, 0), np.maximum(qy, 0)
        L = np.hypot(ox, oy)
        d = L + np.minimum(np.maximum(qx, qy), 0) - r
        safe = np.where(L > 0, L, 1.0)
        nx = np.where(L > 0, ox / safe, (qx >= qy) * 1.0) * np.sign(px)
        ny = np.where(L > 0, oy / safe, (qy > qx) * 1.0) * np.sign(py)
        if cut > 0:
            dc = (np.abs(px) - Hx + np.abs(py) - Hy + cut) / math.sqrt(2)
            m = dc > d
            d = np.where(m, dc, d)
            nx = np.where(m, np.sign(px) / math.sqrt(2), nx)
            ny = np.where(m, np.sign(py) / math.sqrt(2), ny)
        return d, nx, ny
    sdf.box = (x0, y0, x1, y1)
    return sdf


def poly(points):
    """Convex polygon, points clockwise in screen space (y down)."""
    edges = []
    n = len(points)
    for i in range(n):
        (ax, ay), (bx, by) = points[i], points[(i + 1) % n]
        ex, ey = bx - ax, by - ay
        L = math.hypot(ex, ey)
        edges.append((ax, ay, ey / L, -ex / L))     # outward normal of a clockwise edge

    def sdf(x, y):
        best = None
        for ax, ay, nx, ny in edges:
            d = (x - ax) * nx + (y - ay) * ny
            if best is None:
                best, bnx, bny = d, np.full_like(d, nx), np.full_like(d, ny)
            else:
                m = d > best
                best = np.where(m, d, best)
                bnx = np.where(m, nx, bnx)
                bny = np.where(m, ny, bny)
        return best, bnx, bny
    xs, ys = [p[0] for p in points], [p[1] for p in points]
    sdf.box = (min(xs), min(ys), max(xs), max(ys))
    return sdf


# ---------------------------------------------------------------------------------------------------------
# paint primitives
# ---------------------------------------------------------------------------------------------------------
def vgrad(stops):
    """Vertical gradient: stops [(y, colour)], clamped outside. Returns a paint callable."""
    def paint(cv):
        ys = [s[0] for s in stops]
        out = np.empty(cv.rgb.shape)
        for ch in range(3):
            out[..., ch] = np.interp(cv.y, ys, [s[1][ch] for s in stops])
        return out
    return paint


def _paint(cv, colour):
    return colour(cv) if callable(colour) else colour


def fill(cv, shape, colour):
    d, _, _ = shape(cv.x, cv.y)
    cv.over((d < 0) * 1.0, _paint(cv, colour))


def ring(cv, outer, inner, colour):
    """Everything inside `outer` and outside `inner`."""
    d0, _, _ = outer(cv.x, cv.y)
    d1, _, _ = inner(cv.x, cv.y)
    cv.over(((d0 < 0) & (d1 >= 0)) * 1.0, _paint(cv, colour))


def angle_colour(cv, nx, ny, stops, yrange):
    """Colour by the direction of the outward normal: 0 deg = up, 90 = side, 180 = down (mirrored L/R).
    A stop is (deg, colour) or (deg, colour_at_ytop, colour_at_ybottom), lerped over yrange."""
    theta = np.degrees(np.arctan2(np.abs(nx), -ny))
    t = np.clip((cv.y - yrange[0]) / (yrange[1] - yrange[0]), 0, 1)[..., None]
    cols = []
    for s in stops:
        cols.append(s[1] if len(s) == 2 else s[1] * (1 - t) + s[2] * t)
    out = np.zeros(cv.rgb.shape) + cols[0]
    for i in range(len(stops) - 1):
        a0, a1 = stops[i][0], stops[i + 1][0]
        f = np.clip((theta - a0) / (a1 - a0), 0, 1)[..., None]
        seg = cols[i] * (1 - f) + cols[i + 1] * f
        out = np.where((theta >= a0)[..., None], seg, out)
    return out


def bevel(cv, shape, width, stops, yrange=None):
    """The rim band 0 < -d < width of a shape, coloured by normal direction (a bevel: light up, dark down)."""
    d, nx, ny = shape(cv.x, cv.y)
    yr = yrange or (shape.box[1], shape.box[3])
    cv.over(((d < 0) & (d > -width)) * 1.0, angle_colour(cv, nx, ny, stops, yr))


CUT = ("cut", 1.5)    # the 1 px pixel-art corner of the raised / sunken pieces


def bevel_rect(cv, box, corner, body, rim, rim_w=1.0, yrange=None):
    """The workhorse: filled rect (corner = radius, or CUT) + a directional rim rim_w px wide."""
    s = rrect(*box, cut=corner[1]) if isinstance(corner, tuple) else rrect(*box, corner)
    fill(cv, s, body)
    bevel(cv, s, rim_w, rim, yrange)
    return s


def halo(cv, shape, colour, alpha_of_d, hole=None):
    """Shadow or glow: alpha = alpha_of_d(distance outside the shape), painted outside the shape only
    (and outside `hole`, when the cut-out differs from the shape the light falls off from)."""
    d, _, _ = shape(cv.x, cv.y)
    a = np.where(d > 0, alpha_of_d(np.maximum(d, 0)), 0.0)
    if hole is not None:
        a = np.where(hole(cv.x, cv.y)[0] < 0, 0.0, a)
    cv.over(a, colour)


def gaussian(peak, sigma):
    return lambda d: peak * np.exp(-(d / sigma) ** 2)


def exp_glow(peak, tau, fade0, fade1):
    def f(d):
        t = np.clip((d - fade0) / (fade1 - fade0), 0, 1)
        return peak * np.exp(-d / tau) * (1 - t * t * (3 - 2 * t))
    return f


def arrowhead(tip, base_x, half, horizontal=True):
    """Grip tick: a small filled triangle pointing at `tip`; base `half` either side of the axis."""
    tx, ty = tip
    if horizontal:
        pts = [(tx, ty), (base_x, ty - half), (base_x, ty + half)]
        if base_x < tx:
            pts = [(tx, ty), (base_x, ty + half), (base_x, ty - half)]
    else:
        pts = [(tx, ty), (tx + half, base_x), (tx - half, base_x)]
        if base_x < ty:
            pts = [(tx, ty), (tx - half, base_x), (tx + half, base_x)]
    return poly(pts)


# ---------------------------------------------------------------------------------------------------------
# parameter table (colours already nudged 1-2 per channel from the sampled values; geometry in retail px)
# ---------------------------------------------------------------------------------------------------------
P = dict(
    light=C("716962"), light_corner=C("5d5650"), side_hi=C("514a44"), side=C("49433f"),
    side_lo=C("322f2d"), dark_corner=C("2d2b28"), dark=C("272420"),
    body=C("403d39"), body_hi=C("534f4a"), grip=C("5d5650"), grip_hi=C("716862"),
    well=C("2d2a26"), well_lip=C("57514c"),
    btn_body=C("46413d"), btn_top=C("5e5852"), btn_hi_top=C("645d57"),
    dis_top=C("5c5650"), dis_side_hi=C("423d38"), dis_side=C("3c3734"), dis_side_lo=C("272422"),
    dis_dark=C("231f1d"),
    dark_body=C("4d4844"), dark_rim=C("312e2b"), dark_rim_lo=C("21201d"), dark_bottom=C("1b1a17"),
    slider_top=C("5e5852"), slider_bot=C("46413c"), slider_hi_top=C("676158"), slider_hi_bot=C("46413d"),
    wing=C("34322e"), wing_rim=C("45423d"), wing_top=C("58504b"), wing_lo=C("2d2b28"),
    gold=C("f5cc50"), glow=C("f2cb4e"),
    key_face=C("544f4a"), key_top=C("6e6660"), key_sheen=C("81776b"), key_side_top=C("696056"),
    key_side_bot=C("34312d"), key_corner_lo=C("2d2925"), key_bottom=C("302b28"),
    tip_rim=C("716860"), tip_body=C("272420"),
    tab_rim=C("49433f"), tab_top=C("46413d"), tab_bot=C("403d39"),
    black=C("010201"), white=C("fefdfe"),
    shadow_dialog=dict(peak=0.69, sigma=1.47), shadow_slider=dict(peak=0.29, sigma=1.15),
    glow_falloff=dict(peak=0.99, tau=5.35, fade0=8.5, fade1=12.5),
)

# rim colour by normal direction, shared by every raised piece
RAISED = [(0, P["light"]), (40, P["light_corner"]), (65, P["side_hi"]), (90, P["side"]),
          (115, P["side_lo"]), (140, P["dark_corner"]), (180, P["dark"])]
SUNKEN = [(0, P["dark"]), (60, P["dark"]), (90, P["well"]), (120, P["well"]), (140, P["well_lip"]),
          (180, P["light"])]
DISABLED = [(0, P["dis_top"]), (50, P["dis_side_hi"]), (90, P["dis_side"]), (130, P["dis_side_lo"]),
            (180, P["dis_dark"])]
DARK = [(0, P["dark_rim"]), (95, P["dark_rim"]), (125, P["dark_rim_lo"]), (180, P["dark_bottom"])]
FLAT = [(0, P["side"]), (180, P["side"])]


# ---------------------------------------------------------------------------------------------------------
# sprites
# ---------------------------------------------------------------------------------------------------------
def sunken_well(cv, w, h):
    bevel_rect(cv, (0, 0, w, h), CUT, P["well"], SUNKEN)


def scroll_thumb(cv, w, h, vertical, body, grip):
    if vertical:
        box, c = (2, 0, 10, 16), w / 2
        bevel_rect(cv, box, CUT, body, RAISED)
        fill(cv, arrowhead((c, 2.2), 6.0, 2.0, horizontal=False), grip)
        fill(cv, arrowhead((c, h - 2.2), h - 6.0, 2.0, horizontal=False), grip)
    else:
        box, c = (0, 2, 16, 10), h / 2
        bevel_rect(cv, box, CUT, body, RAISED)
        fill(cv, arrowhead((2.2, c), 6.0, 2.0), grip)
        fill(cv, arrowhead((w - 2.2, c), w - 6.0, 2.0), grip)


def slider_thumb(cv, top, bot):
    outline = poly([(2, 2), (3, 1), (12, 1), (13, 2), (13, 21.5), (7.5, 27), (2, 21.5)])
    halo(cv, outline, P["black"], gaussian(**P["shadow_slider"]))
    wing = rrect(0, 6.2, 15, 17.8, 1.5)
    halo(cv, wing, P["black"], gaussian(**P["shadow_slider"]))
    fill(cv, wing, P["wing"])
    bevel(cv, wing, 1.0, [(0, P["wing_top"]), (60, P["wing_rim"]), (120, P["wing_rim"]),
                          (180, P["wing_lo"])])
    fill(cv, outline, vgrad([(2.0, top), (15.0, bot)]))
    bevel(cv, outline, 1.0, RAISED[:4] + [(125, P["dark"]), (180, P["dark"])], (1, 21))


def button_state(cv, ox, state):
    box = (ox, 0, ox + 16, 24)
    if state == "En":
        bevel_rect(cv, box, CUT, vgrad([(1, P["btn_top"]), (12.5, P["btn_body"])]), RAISED)
    elif state == "Di":
        bevel_rect(cv, box, CUT, P["btn_body"], DISABLED)
    elif state == "Hi":
        bevel_rect(cv, box, CUT, vgrad([(1, P["btn_hi_top"]), (13.5, P["btn_body"])]), RAISED)
    else:
        bevel_rect(cv, box, CUT, vgrad([(1.5, P["dark"]), (13.5, P["btn_body"])]), FLAT)


def key_cap(cv):
    outer, face = rrect(0, 0, 48, 48, 4.5), rrect(6, 6.5, 42, 41, 3.0)
    d, _, _ = outer(cv.x, cv.y)
    _, fnx, fny = face(cv.x, cv.y)
    band = angle_colour(cv, fnx, fny, [(0, P["key_top"]), (35, P["key_sheen"]), (55, P["key_sheen"]),
                                       (75, P["key_side_top"], P["key_side_bot"]),
                                       (105, P["key_side_top"], P["key_side_bot"]),
                                       (140, P["key_corner_lo"]), (180, P["key_bottom"])], (6, 41))
    cv.over((d < 0) * 1.0, band)
    fill(cv, face, P["key_face"])


def save_mask(cv, highlight):
    outer, hole = rrect(0, 0, 180, 136, 6.5), rrect(4, 4, 176, 132, 5.0)
    ring(cv, outer, hole, P["black"])
    if highlight:
        ring(cv, rrect(1, 1, 179, 135, 5.5), rrect(3, 3, 177, 133, 4.5), P["white"])


SPRITES = {
    # name: (w, h, draw(cv), slice)   slice: ("h3"|"v3", start, end) | ("9", left, top, right, bottom)
    "Scroll/HScrollBack": (16, 12, lambda cv: sunken_well(cv, 16, 12), ("h3", 7, 7)),
    "Scroll/HScrollBar": (16, 12, lambda cv: scroll_thumb(cv, 16, 12, False, P["body"], P["grip"]), ("h3", 7, 7)),
    "Scroll/HScrollHigh": (16, 12, lambda cv: scroll_thumb(cv, 16, 12, False, P["body_hi"], P["grip_hi"]),
                           ("h3", 7, 7)),
    "Scroll/HScrollHighlight": (16, 12, lambda cv: scroll_thumb(cv, 16, 12, False, P["body_hi"], P["grip_hi"]),
                                ("h3", 7, 7)),
    "Scroll/VScrollBack": (12, 16, lambda cv: sunken_well(cv, 12, 16), ("v3", 7, 7)),
    "Scroll/VScrollBar": (12, 16, lambda cv: scroll_thumb(cv, 12, 16, True, P["body"], P["grip"]), ("v3", 7, 7)),
    "Scroll/VScrollHigh": (12, 16, lambda cv: scroll_thumb(cv, 12, 16, True, P["body_hi"], P["grip_hi"]),
                           ("v3", 7, 7)),
    "Scroll/VScrollHighlight": (12, 16, lambda cv: scroll_thumb(cv, 12, 16, True, P["body_hi"], P["grip_hi"]),
                                ("v3", 7, 7)),
    "Slider/Slider": (15, 28, lambda cv: slider_thumb(cv, P["slider_top"], P["slider_bot"]), None),
    "Slider/SliderHighlight": (15, 28, lambda cv: slider_thumb(cv, P["slider_hi_top"], P["slider_hi_bot"]), None),
    "Slider/SliderBackground": (16, 16, lambda cv: sunken_well(cv, 16, 16), ("h3", 6, 6)),
    "Progress/Scroll": (16, 16, lambda cv: fill(cv, rrect(2.5, 3, 13.5, 13, 3.5), P["gold"]), ("h3", 6, 6)),
    "Progress/ScrollBackground": (16, 16, lambda cv: sunken_well(cv, 16, 16), ("h3", 4, 4)),
    "Borders/Button": (80, 64, lambda cv: [button_state(cv, 20 * i, s) for i, s in enumerate(("En", "Di", "Hi", "Pr"))],
                       ("9", 4, 4, 4, 4)),
    "Borders/Dark": (16, 16, lambda cv: bevel_rect(cv, (0, 0, 16, 16), CUT, P["dark_body"], DARK), ("9", 4, 4, 4, 4)),
    "Borders/Raised": (16, 16, lambda cv: bevel_rect(cv, (0, 0, 16, 16), CUT, P["body"], RAISED), ("9", 4, 4, 4, 4)),
    "Borders/Sunken": (16, 16, lambda cv: sunken_well(cv, 16, 16), ("9", 4, 4, 4, 4)),
    "Borders/Dialog": (20, 21, lambda cv: (halo(cv, rrect(3, 3, 17, 18, 2.5), P["black"], gaussian(**P["shadow_dialog"])),
                                           bevel_rect(cv, (3, 3, 17, 18), 2.5, P["body"],
                                                      [(0, P["light"]), (30, P["side"]), (70, P["body"]),
                                                       (110, P["body"]), (140, P["dark_corner"]), (180, P["dark"])])),
                       ("9", 8, 8, 8, 8)),
    "Borders/Glow": (64, 64, lambda cv: halo(cv, rrect(13, 13, 51, 51, 7.0), P["glow"], exp_glow(**P["glow_falloff"]),
                                              hole=rrect(13, 13, 51, 51)),
                     ("9", 14, 14, 14, 14)),
    "Borders/Key": (48, 48, key_cap, ("9", 12, 12, 12, 12)),
    "Borders/Save": (180, 136, lambda cv: save_mask(cv, False), ("9", 9, 9, 9, 9)),
    "Borders/SaveHighlight": (180, 136, lambda cv: save_mask(cv, True), ("9", 9, 9, 9, 9)),
    "Borders/Tab": (4, 10, lambda cv: (fill(cv, rrect(0, 0, 4, 10), vgrad([(2.5, P["tab_top"]), (9.5, P["tab_bot"])])),
                                       fill(cv, rrect(0, 0, 1, 10), P["tab_rim"]),
                                       fill(cv, rrect(3, 0, 4, 7), P["tab_rim"])), ("h3", 1, 1)),
    "Borders/ToolTip": (8, 8, lambda cv: bevel_rect(cv, (0, 0, 8, 8), 3.0, P["tip_body"],
                                                    [(0, P["tip_rim"]), (180, P["tip_rim"])], rim_w=1.0), ("9", 3, 3, 3, 3)),
}


def render(name, scale):
    w, h, draw, _ = SPRITES[name]
    cv = Canvas(w, h, scale)
    draw(cv)
    return cv.image()


# ---------------------------------------------------------------------------------------------------------
# stretch test: each frame stretched to 3x its size through its own slice margins
# ---------------------------------------------------------------------------------------------------------
def _stretch_axis(img, axis, a, b, target):
    """Keep [0,a) and [len-b,len), repeat the centre pixel of the middle to reach `target`."""
    n = img.size[axis]
    mid = (a + n - b) // 2
    def crop(lo, hi):
        return img.crop((lo, 0, hi, img.size[1]) if axis == 0 else (0, lo, img.size[0], hi))
    head, tail, strip = crop(0, a), crop(n - b, n), crop(mid, mid + 1)
    size = list(img.size)
    size[axis] = target - a - b
    body = strip.resize(tuple(size), Image.NEAREST)
    out_size = list(img.size)
    out_size[axis] = target
    out = Image.new("RGBA", tuple(out_size))
    for piece, pos in ((head, 0), (body, a), (tail, target - b)):
        out.paste(piece, (pos, 0) if axis == 0 else (0, pos))
    return out


def stretch(img, sl, k):
    w, h = img.size
    if sl[0] == "h3":
        return _stretch_axis(img, 0, sl[1] * k, sl[2] * k, 3 * w)
    if sl[0] == "v3":
        return _stretch_axis(img, 1, sl[1] * k, sl[2] * k, 3 * h)
    _, l, t, r, b = sl
    return _stretch_axis(_stretch_axis(img, 0, l * k, r * k, 3 * w), 1, t * k, b * k, 3 * h)


def write_stretch_tests(images, scale, folder):
    """Every frame stretched to 3x its size through its own margins, one PNG each plus ALL.png."""
    k = scale
    sheet = []
    os.makedirs(folder, exist_ok=True)
    for name, img in images.items():
        sl = SPRITES[name][3]
        if not sl:
            continue
        if name == "Borders/Button":   # four 16 x 24 states, one every 20 px
            cells = [img.crop((20 * k * i, 0, 20 * k * i + 16 * k, 24 * k)) for i in range(4)]
        else:
            cells = [img]
        for i, cell in enumerate(cells):
            st = stretch(cell, sl, k)
            suffix = "_" + "En Di Hi Pr".split()[i] if len(cells) > 1 else ""
            st.save(os.path.join(folder, name.replace("/", "_") + suffix + ".png"))
            sheet.append(st)
    width = max(s.size[0] for s in sheet) + 8
    height = sum(s.size[1] + 8 for s in sheet) + 8
    out = Image.new("RGBA", (width, height), (128, 140, 128, 255))
    y = 8
    for s in sheet:
        out.alpha_composite(s, (4, y))
        y += s.size[1] + 8
    out.save(os.path.join(folder, "ALL.png"))
    return len(sheet)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--scale", type=int, default=2, help="integer scale over the retail size (default 2)")
    ap.add_argument("--out", default=None, help="output root (default src/ext/sharp_ui/art/<scale>x)")
    ap.add_argument("--stretch", default=None, help="also write the 3x stretch tests into this folder")
    args = ap.parse_args()
    out_root = args.out or os.path.join(ROOT, "src", "ext", "sharp_ui", "art", "%dx" % args.scale)
    images = {}
    for name in SPRITES:
        path = os.path.join(out_root, name + ".png")
        os.makedirs(os.path.dirname(path), exist_ok=True)
        img = render(name, args.scale)
        save_png(img, path)
        images[name] = img
    print("chrome: wrote", len(SPRITES), "sprites at %dx to" % args.scale, out_root)
    if args.stretch:
        n = write_stretch_tests(images, args.scale, args.stretch)
        print("chrome: wrote", n, "stretch tests to", args.stretch)


if __name__ == "__main__":
    main()
