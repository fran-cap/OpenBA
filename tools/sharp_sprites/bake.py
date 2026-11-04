#!/usr/bin/env python3
"""tools/sharp_sprites/bake.py -- the menu icons of ext.sharp_ui's magnified sheet (NOT RETAIL;
docs/extensions/handins/SHARP_SPRITES.md).

usage: python -I tools/sharp_sprites/bake.py [--scale N] [--out DIR]
           bake: every icon at N x its retail size (default 2) into src/ext/sharp_ui/art/<N>x/<Cat>/<Name>.png
       python -I tools/sharp_sprites/bake.py --measure <ToolKit>/resource/Dialog/Sprite
           re-measure: rewrite measurements.json from the mod kit's sprite PNGs (maintainer only)

The icons are third-party SVGs (Phosphor, Tabler, Lucide, Bootstrap Icons -- MIT / ISC; game-icons.net --
CC BY 3.0; each file under svg/ keeps its set's licence, svg/LICENSES.md) plus four shapes of our own
(svg/own/, written by this script), combined and recoloured by the rules in build_specs() and rendered with
resvg. Nothing is traced or copied from the game.

What the bake needs from the game is a handful of MEASUREMENTS, frozen in measurements.json: each sprite's
pixel size and alpha bounding box (where the glyph is placed), the bar weight of the plus / minus / X glyphs
(how much to thicken them), and the colours of the coloured sprites. Every colour there is already MOVED
1-2 values per channel from the value it was sampled from (the maintainer's ruling: no colour is a clone of
a retail one); the sampled values themselves are never written. So a bake needs neither the mod kit nor the
game; --measure (which does) only runs when a measurement must change.

Needs resvg-py, svgelements and Pillow (tools/sharp_sprites/requirements.txt).
"""
import argparse
import hashlib
import io
import json
import math
import pathlib
import re
import statistics

import resvg_py
from PIL import Image
from svgelements import Path as SvgPath

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent.parent
SVG = HERE / "svg"
GEN = SVG / "own"
MEASUREMENTS = HERE / "measurements.json"

def norm(p):
    return str(p).replace("\\", "/")


WHITE = "#ffffff"
SECOND_TONE = "#bdb3a8"  # our own choice (not sampled): the tinted hand of TradeTab


# ---------------------------------------------------------------------------------------------- measurements
# Each measuring function is called by name and arguments; the result is looked up in measurements.json
# (a bake) or computed from the kit's PNGs and recorded (--measure). Results only: sizes, boxes, weights,
# and colours AFTER nudge().
class Measure:
    def __init__(self):
        self.kit = None
        self.table = {}
        self.used = set()
        if MEASUREMENTS.exists():
            self.table = json.loads(MEASUREMENTS.read_text(encoding="utf-8"))
        self._images = {}

    def image(self, sprite):
        if sprite not in self._images:
            self._images[sprite] = Image.open(self.kit / (sprite + ".png")).convert("RGBA")
        return self._images[sprite]

    def get(self, fn, *args):
        key = fn.__name__ + json.dumps(list(args))
        self.used.add(key)
        if self.kit is not None:
            value = fn(self, *args)
            self.table[key] = list(value) if isinstance(value, tuple) else value
        if key not in self.table:
            raise SystemExit("sharp_sprites: no measurement %s; run --measure <kit sprite dir>" % key)
        value = self.table[key]
        return tuple(value) if isinstance(value, list) else value

    def save(self):
        table = {k: self.table[k] for k in sorted(self.used)}
        MEASUREMENTS.write_text(json.dumps(table, indent=1) + "\n", encoding="utf-8", newline="\n")
        return len(table)


M = Measure()


def nudge(rgb, key):
    """Move every channel by 1 or 2 (deterministic per key), never leaving 0..255, never 0 change."""
    out = []
    for i, v in enumerate(rgb):
        h = hashlib.sha1(("%s/%d" % (key, i)).encode()).digest()[0]
        d = (1 + (h & 1)) * (1 if h & 2 else -1)
        if not 0 <= v + d <= 255:
            d = -d
        out.append(v + d)
    return "#%02x%02x%02x" % tuple(out)


def _size(m, sprite):
    return m.image(sprite).size


def _bbox(m, sprite):
    im = m.image(sprite)
    return im.getchannel("A").getbbox() or (0, 0, im.size[0], im.size[1])


def _sample(m, sprite, rect, key):
    """Median colour of the most opaque pixels in rect (retail px), nudged."""
    im = m.image(sprite)
    x0, y0, x1, y1 = rect
    px = [im.getpixel((x, y)) for y in range(y0, y1) for x in range(x0, x1)]
    amax = max(p[3] for p in px)
    assert amax > 0, (sprite, rect)
    sel = [p for p in px if p[3] >= 0.75 * amax]
    rgb = tuple(int(statistics.median(p[c] for p in sel)) for c in range(3))
    return nudge(rgb, key)


def _sample_rgba(m, sprite, rect, key):
    """Colour and alpha of a translucent region (median), both nudged 1-2 values."""
    im = m.image(sprite)
    x0, y0, x1, y1 = rect
    px = [im.getpixel((x, y)) for y in range(y0, y1) for x in range(x0, x1)]
    px = [q for q in px if q[3] > 0]
    rgb = tuple(int(statistics.median(q[c] for q in px)) for c in range(3))
    a = int(statistics.median(q[3] for q in px))
    an = int(nudge((a, a, a), key + "/alpha")[1:3], 16)
    return (nudge(rgb, key), an / 255.0)


def _sample_lum(m, sprite, lo, hi, key):
    """Median colour of the opaque pixels whose luminance is in [lo, hi), nudged (outline / shade tones)."""
    im = m.image(sprite)
    sel = [p for p in im.getdata()
           if p[3] >= 200 and lo <= 0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2] < hi]
    assert sel, (sprite, lo, hi)
    rgb = tuple(int(statistics.median(p[c] for p in sel)) for c in range(3))
    return nudge(rgb, key)


def _weight(m, sprite, kind):
    """Bar weight in retail px from the alpha sum: minus = A/L, plus/x = L - sqrt(L^2 - A)."""
    im = m.image(sprite)
    a = sum(p[3] for p in im.getdata()) / 255.0
    x0, y0, x1, y1 = _bbox(m, sprite)
    w = x1 - x0
    if kind == "minus":
        return a / w
    length = w * math.sqrt(2) if kind == "x" else w
    return length - math.sqrt(max(length * length - a, 0))


def _ink(m, sprite):
    """The alpha sum of a sprite in retail px (the ink area an X is matched to)."""
    return sum(q[3] for q in m.image(sprite).getdata()) / 255.0


def retail_size(sprite):
    return M.get(_size, sprite)


def retail_bbox(sprite):
    return M.get(_bbox, sprite)


def sample(sprite, rect, key):
    return M.get(_sample, sprite, list(rect), key)


def sample_rgba(sprite, rect, key):
    return M.get(_sample_rgba, sprite, list(rect), key)


def sample_lum(sprite, lo, hi, key):
    return M.get(_sample_lum, sprite, lo, hi, key)


def measure_weight(sprite, kind):
    return M.get(_weight, sprite, kind)


def ink_area(sprite):
    return M.get(_ink, sprite)


LID = (-14, 118, 128, 34, 270, 118)  # quadratic lid curve over phosphor eye-fill (256 box): P0, control, P2
LID_W = 20
LID_GAP = 8


def lid_y(x):
    """y of the lid curve at x (the curve is symmetric, so solve the quadratic in t)."""
    x0, y0, cx, cy, x2, y2 = LID
    t = (x - x0) / (x2 - x0)
    return (1 - t) ** 2 * y0 + 2 * (1 - t) * t * cy + t * t * y2


def eye_clip():
    """The eye is kept only below the lid's lower edge + a gap, so no white shows above the lid."""
    off = LID_W / 2 + LID_GAP
    pts = [(x, lid_y(x) + off) for x in range(-20, 281, 10)]
    return pts + [(280, 400), (-20, 400)]


def gen_eyelid():
    """Our own eyelid for phosphor eye-fill (256 box): a heavy arc wider than the eye, plus a pupil glint
    (white disc at the top-left of the pupil hole, centre 128,128 r40)."""
    svg = ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 256 256" fill="none" stroke="currentColor" '
           'stroke-width="%g" stroke-linecap="round"><path d="M%g,%g Q%g,%g %g,%g"/>'
           '<circle cx="113" cy="113" r="10" fill="currentColor" stroke="none"/></svg>' % ((LID_W,) + LID))
    GEN.mkdir(exist_ok=True)
    out = GEN / "eyelid.svg"
    out.write_text(svg, encoding="utf-8")
    return out


def pin_grooves(centres=(2.6, 4.6), width=0.5):
    """Two bands across bootstrap pin-angle-fill's barrel (16 box). The pin axis is the line x + y = 15.75,
    the barrel spans x - y = 0.55 .. 6.8; a band of constant x - y is perpendicular to the axis."""
    bands = []
    for c in centres:
        a0, a1 = c - width / 2, c + width / 2
        quad = []
        for a, b in ((a0, 4), (a0, 28), (a1, 28), (a1, 4)):
            quad.append(((a + b) / 2, (b - a) / 2))
        bands.append(quad)
    return bands


def gen_spiral(turns=1.2, r0=10.0, r1=3.4, width=2.6, start=-0.35 * math.pi, head_len=3.6, head_w=3.0):
    """Our own ProductionTab art (ruling 7): an Archimedean spiral stroke, counter-clockwise inward,
    ending in an arrowhead at the centre. Pure parameters, nothing traced."""
    cx = cy = 12.0
    n = 200
    sweep = turns * 2 * math.pi
    pts = []
    for i in range(n + 1):
        f = i / n
        a = start - f * sweep  # screen y is down: decreasing angle = counter-clockwise on screen
        r = r0 + (r1 - r0) * f
        pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    (x1, y1), (x2, y2) = pts[-2], pts[-1]
    tl = math.hypot(x2 - x1, y2 - y1)
    tx, ty = (x2 - x1) / tl, (y2 - y1) / tl
    nx, ny = -ty, tx
    px, py = x2, y2
    tip = (px + tx * head_len, py + ty * head_len)
    b1 = (px + nx * head_w, py + ny * head_w)
    b2 = (px - nx * head_w, py - ny * head_w)
    d = "M" + " L".join("%.3f,%.3f" % q for q in pts[:-3])
    svg = ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" '
           'stroke-width="%g" stroke-linecap="round" stroke-linejoin="round"><path d="%s"/>'
           '<path fill="currentColor" stroke-width="0.6" d="M%.3f,%.3f L%.3f,%.3f L%.3f,%.3f Z"/></svg>'
           % (width, d, tip[0], tip[1], b1[0], b1[1], b2[0], b2[1]))
    GEN.mkdir(exist_ok=True)
    out = GEN / "spiral_arrow.svg"
    out.write_text(svg, encoding="utf-8")
    return out


def gen_figure():
    """Our own standing figure (ruling 9d): head circle, torso, two legs, in a 12x32 unit box.
    Subpaths: 0 head, 1 torso, 2 left leg, 3 right leg."""
    d = ("M3.2,3.6 a2.8,2.8 0 1,0 5.6,0 a2.8,2.8 0 1,0 -5.6,0 Z "
         "M2.6,7.6 L9.4,7.6 Q10.2,7.6 10.1,8.6 L9.6,18.4 L2.4,18.4 L1.9,8.6 Q1.8,7.6 2.6,7.6 Z "
         "M2.7,18 L5.6,18 L5.4,31.6 L2.9,31.6 Z "
         "M6.4,18 L9.3,18 L9.1,31.6 L6.6,31.6 Z")
    svg = '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 12 32" fill="currentColor"><path d="%s"/></svg>' % d
    GEN.mkdir(exist_ok=True)
    out = GEN / "figure.svg"
    out.write_text(svg, encoding="utf-8")
    return out


def gear_path(cx, cy, teeth, root, tip, phase, root_frac=0.56, tip_frac=0.30, hole=0.0):
    """One spur gear as a closed polygon (+ a hub hole subpath, evenodd): `teeth` trapezoid teeth from
    radius `root` to `tip`, the first centred on angle `phase` (degrees, screen y down). A tooth's base
    spans root_frac of the tooth period, its flat top tip_frac; the root between teeth is an arc."""
    period = 2 * math.pi / teeth
    pts = []
    for k in range(teeth):
        a = math.radians(phase) + k * period
        hb, ht = root_frac * period / 2, tip_frac * period / 2
        pts += [(root, a - hb), (tip, a - ht), (tip, a + ht), (root, a + hb)]
        gap = period - 2 * hb
        for j in range(1, 6):  # the root arc up to the next tooth
            pts.append((root, a + hb + gap * j / 6))
    d = "M" + " L".join("%.3f,%.3f" % (cx + r * math.cos(a), cy + r * math.sin(a)) for r, a in pts) + " Z"
    if hole > 0:
        d += " M%.3f,%.3f a%g,%g 0 1,0 %g,0 a%g,%g 0 1,0 %g,0 Z" % (
            cx - hole, cy, hole, hole, 2 * hole, hole, hole, -2 * hole)
    return d


GEARS = dict(big_teeth=8, small_teeth=6, module=2.25, depth=3.4, clearance=0.8, big_hole=2.8, small_hole=1.6,
             root_frac=0.60, tip_frac=0.36)


def gen_brush(width, bristle_h, gap, ferrule_w, ferrule_h, neck_w, handle_w, handle_h, round_r):
    """Our own OptionsGraphics art: an upright flat paint brush (the retail sprite's subject, standing, as
    tall as its glyph box; Phosphor's broad brush is squat and drew at half the height). Top to bottom:
    the bristle block `width` wide with rounded top corners, a transparent `gap`, the ferrule, a neck
    tapering to the handle, the handle with a round end. Units are free: bake.py fits the bbox."""
    cx = width / 2
    r = round_r
    y = 0.0
    d = ("M0,%.3f Q0,0 %.3f,0 L%.3f,0 Q%.3f,0 %.3f,%.3f L%.3f,%.3f L0,%.3f Z "
         % (r, r, width - r, width, width, r, width, bristle_h, bristle_h))
    y = bristle_h + gap
    fx0, fx1 = cx - ferrule_w / 2, cx + ferrule_w / 2
    nx0, nx1 = cx - neck_w / 2, cx + neck_w / 2
    hx0, hx1 = cx - handle_w / 2, cx + handle_w / 2
    y1 = y + ferrule_h
    y2 = y1 + (ferrule_w - neck_w)  # the neck's taper, 45 degrees
    y3 = y2 + handle_h
    d += ("M%.3f,%.3f L%.3f,%.3f L%.3f,%.3f L%.3f,%.3f L%.3f,%.3f L%.3f,%.3f "
          "A%.3f,%.3f 0 0,1 %.3f,%.3f L%.3f,%.3f L%.3f,%.3f L%.3f,%.3f Z"
          % (fx0, y, fx1, y, fx1, y1, nx1, y2, hx1, y2, hx1, y3,
             handle_w / 2, handle_w / 2, hx0, y3, hx0, y2, nx0, y2, fx0, y1))
    h = y3 + handle_w / 2
    svg = ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 %.3f %.3f" fill="currentColor">'
           '<path d="%s"/></svg>' % (width, h, d))
    GEN.mkdir(exist_ok=True)
    out = GEN / "brush.svg"
    out.write_text(svg, encoding="utf-8")
    return out


BRUSH = dict(width=8.0, bristle_h=8.4, gap=0.7, ferrule_w=7.0, ferrule_h=2.2, neck_w=2.6, handle_w=2.6,
             handle_h=3.4, round_r=1.0)

# Lucide circle-play's triangle (its first path), drawn filled as well as stroked.
PLAY_TRIANGLE = ("M9 9.003a1 1 0 0 1 1.517-.859l4.997 2.997a1 1 0 0 1 0 1.718l-4.997 2.997A1 1 0 0 1 9 14.996z")


def gen_gears(big_teeth, small_teeth, module, depth, clearance, big_hole, small_hole, root_frac, tip_frac):
    """Our own Options art: a big and a small spur gear meshed on the down-right diagonal (big upper left,
    small lower right, as the retail sprite places them). Pitch radius = module * teeth / 2 for both, so
    the tooth pitch matches; the centres are the two pitch radii + `clearance` apart, a big tooth points
    at the small gear and a small-gear gap faces it. Teeth `depth` deep (half outside the pitch circle),
    a hub hole in each. Units are free: bake.py fits the drawn bbox into the retail glyph box."""
    rb, rs = module * big_teeth / 2, module * small_teeth / 2
    dist = rb + rs + clearance
    cb = rb + depth / 2 + 0.5
    cs = cb + dist / math.sqrt(2)
    small_period = 360.0 / small_teeth
    d = (gear_path(cb, cb, big_teeth, rb - depth / 2, rb + depth / 2, 45.0, root_frac, tip_frac, big_hole) + " " +
         gear_path(cs, cs, small_teeth, rs - depth / 2, rs + depth / 2, 225.0 + small_period / 2,
                   root_frac, tip_frac, small_hole))
    size = cs + rs + depth / 2 + 0.5
    svg = ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 %.3f %.3f" fill="currentColor">'
           '<path fill-rule="evenodd" d="%s"/></svg>' % (size, size, d))
    GEN.mkdir(exist_ok=True)
    out = GEN / "gears.svg"
    out.write_text(svg, encoding="utf-8")
    return out


# ---------------------------------------------------------------------------------------------- svg parsing
def load_svg(path):
    txt = pathlib.Path(path).read_text(encoding="utf-8")
    txt = re.sub(r"<!--.*?-->", "", txt, flags=re.S)
    m = re.search(r"<svg\b([^>]*)>(.*)</svg>", txt, flags=re.S)
    attrs = dict(re.findall(r'([\w:-]+)="([^"]*)"', m.group(1)))
    inner = m.group(2).strip()
    vb = [float(v) for v in re.split(r"[ ,]+", attrs["viewBox"].strip())]
    return attrs, inner, vb


def root_style(attrs):
    keep = ["fill", "stroke", "stroke-width", "stroke-linecap", "stroke-linejoin"]
    out = " ".join('%s="%s"' % (k, attrs[k]) for k in keep if k in attrs)
    if "fill" not in attrs:  # SVG default paint is black (e.g. Fluent): use the layer colour instead
        out += ' fill="currentColor"'
    return out


class Part:
    """One source SVG and the layers drawn from it."""

    def __init__(self, svg, ops=(), layers=None, half=False, thicken=None):
        self.svg = pathlib.Path(svg)
        self.attrs, inner, self.vb = load_svg(self.svg)
        self.ops = list(ops)
        self.half = half
        self.thicken = thicken  # extra stroke width in source units
        if "drop_bg" in self.ops:
            inner2 = re.sub(r'<path d="M0 0h512v512H0z"\s*/>', "", inner, count=1)
            assert inner2 != inner, "no game-icons background square in %s" % svg
            inner = inner2.replace('fill="#fff"', 'fill="currentColor"')
        # hard-coded paint (e.g. Fluent's #212121) becomes the layer colour
        inner = re.sub(r'(fill|stroke)="#[0-9a-fA-F]{3,6}"', r'\1="currentColor"', inner)
        self.inner = inner
        self.subpaths = None
        if any(isinstance(L.get("select"), list) for L in (layers or [])) or any(
                o.startswith("drop_subpaths") for o in self.ops):
            ps = re.findall(r'<path([^>]*?)\sd="([^"]+)"([^>]*)/>', inner)
            assert len(ps) == 1, "subpath ops need a single-path glyph: %s" % svg
            self.path_attrs = (ps[0][0] + ps[0][2]).strip()
            self.subpaths = [SvgPath(s).d(relative=False) for s in SvgPath(ps[0][1]).as_subpaths()]
            for o in self.ops:
                if o.startswith("drop_subpaths:"):
                    drop = {int(i) for i in o.split(":")[1].split(",")}
                    self.subpaths = [d for i, d in enumerate(self.subpaths) if i not in drop]
            self.inner = '<path %s d="%s"/>' % (self.path_attrs, " ".join(self.subpaths))
        self.layers = layers or [{"colour": WHITE}]
        self.gb = None
        self.transform = None
        if any(c[0] == "yfrac" for L in self.layers for c in L.get("clip", [])):
            plain = Part.__new__(Part)
            plain.__dict__.update(self.__dict__)
            plain.layers = [{"colour": WHITE}]
            plain.half = False
            self.gb = glyph_bbox([plain], [])

    def content(self, select):
        if isinstance(select, list):
            return '<path %s d="%s"/>' % (self.path_attrs, " ".join(self.subpaths[i] for i in select))
        return self.inner

    def render(self, ids, with_half=True, half_x=None):
        style = root_style(self.attrs)
        if self.thicken:
            style = re.sub(r'stroke="[^"]*"|stroke-width="[^"]*"|stroke-linejoin="[^"]*"', "", style)
            style += (' stroke="currentColor" stroke-width="%.2f" stroke-linejoin="round" stroke-linecap="round"'
                      % self.thicken)
        defs, body = [], []
        for L in self.layers:
            clip = ""
            if L.get("clip"):
                cid = "c%d" % next(ids)
                shapes = []
                for s in L["clip"]:
                    if s[0] == "rect":
                        shapes.append('<rect x="%g" y="%g" width="%g" height="%g"/>' % tuple(s[1:]))
                    elif s[0] == "poly":
                        shapes.append('<polygon points="%s"/>' % " ".join("%g,%g" % pt for pt in s[1]))
                    elif s[0] == "yfrac":  # a band of the glyph's own bbox height (fractions from the top)
                        gx0, gy0, gx1, gy1 = self.gb
                        y0 = gy0 + s[1] * (gy1 - gy0)
                        y1 = gy0 + s[2] * (gy1 - gy0)
                        shapes.append('<rect x="-10000" y="%g" width="20000" height="%g"/>' % (y0, y1 - y0))
                    else:
                        shapes.append('<circle cx="%g" cy="%g" r="%g"/>' % tuple(s[1:]))
                defs.append('<clipPath id="%s">%s</clipPath>' % (cid, "".join(shapes)))
                clip = ' clip-path="url(#%s)"' % cid
            content = self.content(L.get("select"))
            lstyle = style
            if L.get("d"):  # an extra shape from the glyph's own geometry
                content = '<path fill="currentColor" d="%s"/>' % L["d"]
            if L.get("outline"):  # glyph (+ extra shapes) stroked wide in this colour, drawn under the fills
                lstyle = ('fill="currentColor" stroke="currentColor" stroke-width="%g" stroke-linejoin="round"'
                          % L["outline"])
                content = ("" if L.get("only_extra") else self.inner.replace('fill="currentColor"', "")) + "".join(
                    '<path d="%s"/>' % d for d in L.get("extra_d", []))
            if L.get("stroke_only"):  # a rim drawn ON TOP of the fills
                lstyle = ('fill="none" stroke="currentColor" stroke-linejoin="round" stroke-width="%g"'
                          % L["stroke_only"])
                content = ("" if L.get("only_extra") else self.inner.replace('fill="currentColor"', "")) + "".join(
                    '<path d="%s"/>' % d for d in L.get("extra_d", []))
            if L.get("silhouette"):  # glyph + extra shapes as one filled shape (shadow / glow source)
                lstyle = ('fill="currentColor" stroke="currentColor" stroke-linejoin="round" stroke-width="%g"'
                          % L.get("spread", 0))
                content = ("" if L.get("only_extra") else self.inner.replace('fill="currentColor"', "")) + "".join(
                    '<path d="%s"/>' % d for d in L.get("extra_d", []))
            extra = ""
            if L.get("cut"):  # transparent cuts through this layer (mask)
                mid = "m%d" % next(ids)
                defs.append('<mask id="%s" maskUnits="userSpaceOnUse" x="-10000" y="-10000" width="20000" '
                            'height="20000"><rect x="-10000" y="-10000" width="20000" height="20000" fill="#fff"/>'
                            '<g fill="#000">%s</g></mask>' % (mid, "".join(
                                '<path d="%s"/>' % d if isinstance(d, str) else
                                '<polygon points="%s"/>' % " ".join("%g,%g" % pt for pt in d) for d in L["cut"])))
                extra += ' mask="url(#%s)"' % mid
            if L.get("offset"):
                content = '<g transform="translate(%g,%g)">%s</g>' % (L["offset"][0], L["offset"][1], content)
            if L.get("blur"):
                fid = "f%d" % next(ids)
                defs.append('<filter id="%s" x="-50%%" y="-50%%" width="200%%" height="200%%">'
                            '<feGaussianBlur stdDeviation="%g"/></filter>' % (fid, L["blur"]))
                extra += ' filter="url(#%s)"' % fid
            if L.get("opacity") is not None:
                extra += ' opacity="%.4f"' % L["opacity"]
            body.append('<g color="%s"%s%s><g %s>%s</g></g>' % (L["colour"], clip, extra, lstyle, content))
        g = "".join(body)
        if self.half and with_half:
            cid = "h%d" % next(ids)
            defs.append('<clipPath id="%s"><rect x="-10000" y="-10000" width="%g" height="20000"/></clipPath>'
                        % (cid, half_x + 10000))
            g = '<g clip-path="url(#%s)">%s</g>' % (cid, g)
        return "".join(defs), g


def compose(parts, vb, transforms, with_half=True, half_x=None):
    ids = iter(range(1000))
    defs, body = "", ""
    for p in parts:
        d, g = p.render(ids, with_half, half_x)
        defs += d
        if p.transform:
            g = '<g transform="%s">%s</g>' % (p.transform, g)
        body += g
    x, y, w, h = parts[0].vb
    if "mirror" in transforms:
        body = '<g transform="translate(%g,0) scale(-1,1)">%s</g>' % (2 * x + w, body)
    if "flip_v" in transforms:
        body = '<g transform="translate(0,%g) scale(1,-1)">%s</g>' % (2 * y + h, body)
    return ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="%g %g %g %g"><defs>%s</defs>%s</svg>'
            % (tuple(vb) + (defs, body)))


def raster(svg, w, h):
    data = resvg_py.svg_to_bytes(svg_string=svg, width=w, height=h)
    im = Image.open(io.BytesIO(bytes(data))).convert("RGBA")
    if im.size != (w, h):
        # From the viewBox alone resvg derives the height from the width, and a 6 x 16 box whose
        # ratio lands a hair off a whole pixel came out 12 x 31 / 12 x 33: put the size on the root.
        svg = svg.replace("<svg ", '<svg width="%d" height="%d" ' % (w, h), 1)
        data = resvg_py.svg_to_bytes(svg_string=svg, width=w, height=h)
        im = Image.open(io.BytesIO(bytes(data))).convert("RGBA")
    return im


def glyph_bbox(parts, transforms):
    """Bounding box of the drawn glyph in source units (from a 1024 px render)."""
    vb = parts[0].vb
    n = 1024
    m = round(n * vb[3] / vb[2])
    im = raster(compose(parts, vb, transforms, with_half=False), n, m)
    bx = im.getchannel("A").point(lambda a: 255 if a > 8 else 0).getbbox()
    sx, sy = vb[2] / n, vb[3] / m
    return (vb[0] + bx[0] * sx, vb[1] + bx[1] * sy, vb[0] + bx[2] * sx, vb[1] + bx[3] * sy)



def S(rel):
    return SVG / rel


# ---------------------------------------------------------------------------------------------- the picks
def white_part(svg):
    ops = ["drop_bg"] if "/game-icons/" in norm(svg) else []
    return Part(svg, ops)


def thick_variant(sprite, svg, kind, colour=None):
    """Thicken a Phosphor bold line glyph (bars 24/256) so that, once its bbox is fitted into the retail
    alpha bbox, the bar is as thick as retail's. A round-capped stroke e grows the bbox by e, so
    (24 + e) * tw / (gw + e) = t  ->  e = (gw * t - 24 * tw) / (tw - t)."""
    if kind == "x_plus":
        # ruling 8: an X as heavy as retail Plus: same ink area over two diagonals of this sprite's bbox
        ink = ink_area("ButtonIcons/Plus")
        bx = retail_bbox(sprite)
        L = (bx[2] - bx[0]) * math.sqrt(2)
        t = L - math.sqrt(L * L - ink)
    else:
        t = measure_weight(sprite, kind)  # retail px
    col = colour or WHITE
    part = Part(svg, layers=[{"colour": col}])
    gx0, gy0, gx1, gy1 = glyph_bbox([part], [])
    gw = gx1 - gx0
    rx0, ry0, rx1, ry1 = retail_bbox(sprite)
    tw = rx1 - rx0
    e = (gw * t - 24 * tw) / (tw - t)
    part.thicken = max(e, 0)
    note = ("stroke +%.1f/256" % e) if e > 0 else "none needed: the fitted bar is already %.2f px" % (24 * tw / gw)
    return {"parts": [part],
            "ops": ["thicken: target bar %.2f px (%s) -> %s" % (
                t, "ink area of retail Plus over the X diagonals" if kind == "x_plus" else
                "measured from this sprite's alpha, " + kind, note)]}


def build_specs():
    """sprite -> list of variants; a variant = {suffix, parts, transforms, fit, bbox_from, ops}."""
    PH = "phosphor/assets/"
    spec = {}
    for sp in ["ButtonIcons/Plus"]:
        spec[sp] = [thick_variant(sp, S(PH + "bold/plus-bold.svg"), "plus")]
    spec["ButtonIcons/Minus"] = [thick_variant("ButtonIcons/Minus", S(PH + "bold/minus-bold.svg"), "minus")]
    for sp in ["ButtonIcons/Close", "ButtonIcons/SimpleCheck", "ButtonIcons/QuitGame"]:
        spec[sp] = [thick_variant(sp, S(PH + "bold/x-bold.svg"), "x_plus")]
    spec["ButtonIcons/GoalFail"] = [thick_variant(
        "ButtonIcons/GoalFail", S(PH + "bold/x-bold.svg"), "x_plus",
        sample("ButtonIcons/GoalFail", (0, 0, 16, 16), "GoalFail/0"))]
    spec["ButtonIcons/GoalSuccess"] = [{"parts": [Part(S(PH + "bold/check-bold.svg"), layers=[
        {"colour": sample("ButtonIcons/GoalSuccess", (0, 0, 16, 16), "GoalSuccess/0")}])]}]
    spec["ButtonIcons/Goal"] = [{"parts": [white_part(S(PH + "fill/flag-pennant-fill.svg"))],
                                 "transforms": ["mirror"]}]
    sg = S(PH + "bold/magnifying-glass-bold.svg")
    spec["Misc/Search"] = [{"parts": [white_part(sg)], "transforms": ["mirror"]}]
    spec["Misc/Lock"] = [{"parts": [white_part(S("tabler/icons/filled/lock.svg"))]}]
    spec["ButtonIcons/AgriTab"] = [{"parts": [white_part(S("lucide/icons/wheat.svg"))], "transforms": ["mirror"]}]
    pin = S("bootstrap-icons/package/icons/pin-angle-fill.svg")
    spec["ButtonIcons/ButtonPin"] = [{"parts": [Part(pin, layers=[{"colour": WHITE, "cut": pin_grooves()}])],
                                      "ops": ["two transparent grooves 0.5/16 wide across the barrel at x-y = 2.6 "
                                              "and 4.6 (perpendicular to the pin axis x+y=15.75)"]}]
    fo = S(PH + "fill/folder-open-fill.svg")
    flap_hole = Part(S(PH + "bold/folder-open-bold.svg"), ["drop_subpaths:0,1"]).subpaths[0]
    spec["ButtonIcons/LoadGame"] = [{"parts": [Part(fo, layers=[{"colour": WHITE, "select": [0], "cut": [flap_hole]}])],
                                     "ops": ["ruling 27: back panel solid (folder-open-fill outer subpath, hole filled)",
                                             "front flap hollow: folder-open-bold's flap interior (subpath 2) cut out"]}]
    spec["ButtonIcons/Scenario"] = [{"parts": [Part(S("lucide/icons/circle-play.svg"), layers=[
        {"colour": WHITE}, {"colour": WHITE, "d": PLAY_TRIANGLE}])],
        "ops": ["the play triangle filled (the user, 2026-11-04): Lucide's own triangle path as a solid layer"]}]
    spec["ButtonIcons/OptionsGraphics"] = [{"parts": [white_part(gen_brush(**BRUSH))],
                                            "ops": ["our own upright flat brush (gen_brush, BRUSH): Phosphor's "
                                                    "broad brush fitted at half the glyph height"]}]
    for sp, rel in [("ButtonIcons/NomadTab", "delapouite/camping-tent"),
                    ("ButtonIcons/StorageTab", "delapouite/ball-pyramid")]:
        spec[sp] = [{"parts": [white_part(S("game-icons/%s.svg" % rel))]}]
    spec["ButtonIcons/Options"] = [{"parts": [white_part(gen_gears(**GEARS))],
                                    "ops": ["own parametric art: %(big_teeth)d- and %(small_teeth)d-tooth spur gears "
                                            "meshed on the diagonal, module %(module)g, depth %(depth)g, hub holes "
                                            "r %(big_hole)g / %(small_hole)g" % GEARS]}]
    spec["ButtonIcons/ButtonCenter"] = [{"parts": [white_part(S("tabler/icons/outline/target.svg"))]}]
    spec["ButtonIcons/OverviewTab"] = [{"parts": [
        Part(S(PH + "fill/eye-fill.svg"), layers=[{"colour": WHITE, "clip": [("poly", eye_clip())]}]),
        white_part(gen_eyelid())],
        "ops": ["phosphor eye-fill kept only below the lid (lower edge + %g gap): no white above the lid" % LID_GAP,
                "our own lid: quadratic arc %s, stroke %g/256, wider than the eye" % (str(LID), LID_W),
                "pupil glint: white disc r10 at (113,113), top-left of the pupil"]}]
    spiral = dict(turns=1.0, start=-0.5 * math.pi, r0=10.0, r1=4.0, width=2.6, head_len=4.2, head_w=3.4)
    spec["ButtonIcons/ProductionTab"] = [{"parts": [white_part(gen_spiral(**spiral))],
                                          "ops": ["own parametric art: Archimedean spiral, %(turns)g turn CCW inward "
                                                  "from the top, r %(r0)g->%(r1)g /24, stroke %(width)g/24, "
                                                  "arrowhead %(head_len)g long at the centre, pointing left" % spiral]}]
    AR = "Misc/Arrow"
    face = ("M126.47 18.25l273.81 229.344 39.314-39.313 23.47 205.095-205.095-23.438 37.467-37.468"
            "L20.594 58.655z")
    wedge = [(0, 36.6), (20.594, 58.655), (295.44, 352.47), (257.97, 389.94), (463.06, 413.38), (560, 424),
             (560, 560), (0, 560)]
    shade = sample_lum(AR, 50, 150, "Arrow/shade")
    spec[AR] = [{"parts": [Part(S("game-icons/lorc/bottom-right-3d-arrow.svg"), ["drop_bg"], layers=[
        {"colour": sample_lum(AR, 0, 30, "Arrow/shadow"), "silhouette": True, "only_extra": True,
         "extra_d": [face], "spread": 24, "blur": 22, "opacity": 1.0, "offset": (-14, -14)},
        {"colour": shade, "outline": 8, "only_extra": True, "extra_d": [face]},
        {"colour": sample(AR, (20, 20, 30, 30), "Arrow/gold"), "d": face},
        {"colour": shade, "stroke_only": 10, "only_extra": True, "extra_d": [face]}])],
        "ops": ["flat arrow: only the face (the glyph's own first 7 vertices); 3D side and rim strip dropped",
                "soft shadow: face silhouette spread 24/512, Gaussian blur 22/512, offset (-14,-14) up-left",
                "face filled gold", "ruling 28: thin dark rim 10/512 stroked ON TOP all the way round"]}]
    # ruling 32: the body solid, the label and the shutter tab cut out (Phosphor's fill weight draws the
    # reverse: a frame with the label solid). Subpath 0 is the outer outline (filled whole); the label is
    # the notch subpath 1 leaves at the bottom, closed along the frame's inner edge y=208; subpath 2 is the
    # tab. Then ruling 29's flip: the notch faces down.
    fd = S(PH + "fill/floppy-disk-fill.svg")
    label = "M184,208V152a16,16,0,0,0-16-16H88a16,16,0,0,0-16,16v56Z"
    tab = Part(fd, ["drop_subpaths:0,1"]).subpaths[0]
    spec["ButtonIcons/SaveGame"] = [{"parts": [Part(fd, layers=[{"colour": WHITE, "select": [0], "cut": [label, tab]}])],
                                     "transforms": ["flip_v"],
                                     "ops": ["ruling 32: body solid, label + shutter tab cut out",
                                             "ruling 29: notch faces down"]}]
    spec["ButtonIcons/TradeTab"] = [{"parts": [Part(S("game-icons/delapouite/shaking-hands.svg"), ["drop_bg"], layers=[
        {"colour": WHITE, "select": [1, 3, 4, 5, 6, 7, 8]},
        {"colour": SECOND_TONE, "select": [0, 2]}])],
        "ops": ["tint right hand (subpaths 0,2) %s, our own second tone" % SECOND_TONE]}]

    # hearts and stars (retail colours, nudged)
    red = sample("ImageStatus/HeartFilled", (2, 3, 14, 9), "Heart/red")
    dark_h = sample("ImageStatus/HeartBorder", (0, 0, 16, 16), "Heart/border")
    gold = sample("ImageStatus/StarFilled", (3, 6, 13, 11), "Star/gold")
    dark_s = sample("ImageStatus/StarBorder", (0, 0, 16, 16), "Star/border")
    spec["ImageStatus/HeartFilled"] = [{"parts": [Part(S(PH + "fill/heart-fill.svg"), layers=[{"colour": red}])]}]
    hin, ha = sample_rgba("ImageStatus/HeartBorder", (3, 3, 13, 8), "Heart/interior")
    spec["ImageStatus/HeartBorder"] = [{"parts": [
        Part(S(PH + "fill/heart-fill.svg"), layers=[{"colour": hin, "opacity": ha}]),
        Part(S(PH + "bold/heart-bold.svg"), layers=[{"colour": dark_h}])],
        "ops": ["translucent off-red interior: heart-fill at opacity %.3f (sampled, nudged)" % ha]}]
    spec["ImageStatus/HeartHalf"] = [{"parts": [Part(S(PH + "fill/heart-fill.svg"), layers=[{"colour": red}],
                                                     half=True)], "bbox_from": "ImageStatus/HeartFilled",
                                      "ops": ["left half of the filled heart only; nothing on the missing half"]}]
    spec["ImageStatus/StarFilled"] = [{"parts": [Part(S(PH + "fill/star-fill.svg"), layers=[{"colour": gold}])]}]
    sin, sa = sample_rgba("ImageStatus/StarBorder", (6, 7, 11, 10), "Star/interior")
    spec["ImageStatus/StarBorder"] = [{"parts": [
        Part(S(PH + "fill/star-fill.svg"), layers=[{"colour": sin, "opacity": sa}]),
        Part(S(PH + "bold/star-bold.svg"), layers=[{"colour": dark_s}])],
        "ops": ["translucent off-gold interior: star-fill at opacity %.3f (sampled, nudged)" % sa]}]
    spec["ImageStatus/StarHalf"] = [{"parts": [Part(S(PH + "fill/star-fill.svg"), layers=[{"colour": gold}],
                                                    half=True)], "bbox_from": "ImageStatus/StarFilled"}]

    # status bar: per-path / per-region colours
    cloud_c = sample("StatusBar/CloudyIcon", (3, 8, 13, 13), "Cloudy/cloud")
    spec["StatusBar/CloudyIcon"] = [{"parts": [Part(S(PH + "fill/cloud-fill.svg"), layers=[{"colour": cloud_c}])]}]
    rays = sample("StatusBar/SunnyIcon", (0, 6, 3, 9), "Sunny/rays")
    disc = sample("StatusBar/SunnyIcon", (6, 6, 10, 10), "Sunny/disc")
    glow = sample("StatusBar/SunnyIcon", (3, 7, 4, 9), "Sunny/innerglow")  # the disc's orange rim pixels

    def sun_part():
        # disc (sun-fill subpath 1: centre 128,128 r64) in orange, then a blurred smaller yellow disc clipped to it:
        # a soft orange inner glow near the rim
        return Part(S(PH + "fill/sun-fill.svg"), layers=[
            {"colour": rays, "select": [0, 2, 3, 4, 5, 6, 7, 8]},
            {"colour": glow, "select": [1]},
            {"colour": disc, "d": "M128,82 a46,46 0 1,0 0.01,0 Z", "blur": 9, "clip": [("circle", 128, 128, 64)]}])

    sun_ops = ["ruling 31: soft orange inner glow: orange disc + yellow disc r46 blurred 9/256 inside it (colours sampled)"]
    spec["StatusBar/SunnyIcon"] = [{"parts": [sun_part()], "ops": sun_ops}]
    sun2 = sample("StatusBar/PartlySunnyIcon", (8, 2, 13, 6), "PartlySunny/sun")
    cloud2 = sample("StatusBar/PartlySunnyIcon", (2, 9, 9, 13), "PartlySunny/cloud")
    cloud_clip = [("circle", 164, 148, 76), ("rect", 84, 120, 80, 104), ("circle", 84, 172, 52)]
    ps_sun = sun_part()
    ps_sun.transform = "translate(86,2) scale(0.68)"
    ps_cloud = Part(S(PH + "fill/cloud-fill.svg"), layers=[{"colour": cloud_c}])
    ps_cloud.transform = "translate(0,56) scale(0.82)"
    spec["StatusBar/PartlySunnyIcon"] = [{"parts": [ps_sun, ps_cloud],
                                          "ops": ["ruling 30: composite of our SunnyIcon (behind, scale 0.68, top right) "
                                                  "and our CloudyIcon (in front, scale 0.82, lower left)"] + sun_ops}]
    rain = sample("StatusBar/RainIcon", (1, 12, 15, 16), "Rain/rain")
    cloud3 = sample("StatusBar/RainIcon", (3, 4, 13, 9), "Rain/cloud")
    spec["StatusBar/RainIcon"] = [{"parts": [Part(S(PH + "fill/cloud-rain-fill.svg"), layers=[
        {"colour": cloud3, "clip": [("rect", 0, 0, 256, 168)]},
        {"colour": rain, "clip": [("rect", 0, 168, 256, 88)]}])], "ops": ["split at y=168/256: cloud above, rain below"]}]
    snow = sample("StatusBar/SnowIcon", (0, 11, 16, 16), "Snow/snow")
    cloud4 = sample("StatusBar/SnowIcon", (3, 4, 13, 9), "Snow/cloud")
    spec["StatusBar/SnowIcon"] = [{"parts": [Part(S(PH + "fill/cloud-snow-fill.svg"), layers=[
        {"colour": snow, "select": [0, 1, 2, 3, 4]}, {"colour": cloud4, "select": [5]}])]}]
    tube = sample("StatusBar/TemperatureIcon", (1, 1, 4, 8), "Temp/tube")
    bulb = sample("StatusBar/TemperatureIcon", (1, 12, 4, 15), "Temp/bulb")
    spec["StatusBar/TemperatureIcon"] = [{"parts": [Part(S(PH + "fill/thermometer-simple-fill.svg"), ["drop_subpaths:1"],
                                                         layers=[{"colour": tube},
                                                                 {"colour": bulb, "clip": [("rect", 0, 120, 256, 136)]}])],
                                          "fit": "crop",
                                          "ops": ["drop the tube's hollow (subpath 1): solid tube like retail",
                                                  "red from y=120/256 down (column + bulb)"]}]
    fig = gen_figure()
    # our own figure (head, tunic, legs, feet) in the figure's own colours, nudged
    for sp, key in [("StatusBar/AdultIcon", "Adult"), ("StatusBar/ChildIcon", "Child")]:
        if key == "Adult":
            col = {"head": sample(sp, (3, 0, 5, 2), "Adult/head"), "tunic": sample(sp, (2, 3, 5, 7), "Adult/tunic"),
                   "legs": sample(sp, (2, 8, 5, 11), "Adult/legs"), "feet": sample(sp, (2, 12, 5, 16), "Adult/feet")}
        else:
            col = {"head": sample(sp, (2, 4, 4, 6), "Child/head"), "tunic": sample(sp, (2, 6, 4, 9), "Child/tunic"),
                   "legs": sample(sp, (2, 10, 4, 12), "Child/legs"), "feet": sample(sp, (2, 13, 4, 16), "Child/feet")}
        spec[sp] = [
            {"parts": [Part(fig, layers=[
                {"colour": col["tunic"], "select": [1]},
                {"colour": col["legs"], "select": [2, 3]},
                {"colour": col["feet"], "select": [2, 3], "clip": [("rect", 0, 26, 12, 6)]},
                {"colour": col["head"], "select": [0]}])],
             "ops": ["own parametric figure: head circle, torso, two legs (12x32 units)"]}]
    return spec

# ---------------------------------------------------------------------------------------------- the defaults
# Sprites drawn from one source SVG in white, fitted into the retail glyph's box (no further rule).
DEFAULTS = [
    ("ButtonIcons/ArrowDown", "phosphor/assets/fill/caret-down-fill.svg"),
    ("ButtonIcons/ArrowLeft", "phosphor/assets/fill/caret-left-fill.svg"),
    ("ButtonIcons/ArrowRight", "phosphor/assets/fill/caret-right-fill.svg"),
    ("ButtonIcons/ArrowUp", "phosphor/assets/fill/caret-up-fill.svg"),
    ("ButtonIcons/ArrowDownSmall", "phosphor/assets/fill/caret-down-fill.svg"),
    ("ButtonIcons/ArrowLeftSmall", "phosphor/assets/fill/caret-left-fill.svg"),
    ("ButtonIcons/ArrowRightSmall", "phosphor/assets/fill/caret-right-fill.svg"),
    ("ButtonIcons/ArrowUpSmall", "phosphor/assets/fill/caret-up-fill.svg"),
    ("ButtonIcons/ArrowDownLine", "phosphor/assets/fill/caret-line-down-fill.svg"),
    ("ButtonIcons/Pause", "phosphor/assets/fill/pause-fill.svg"),
    ("ButtonIcons/Play", "phosphor/assets/fill/play-fill.svg"),
    ("Misc/Bullet", "phosphor/assets/fill/circle-fill.svg"),
    ("Misc/BulletLarge", "phosphor/assets/fill/circle-fill.svg"),
    ("Misc/BulletMedium", "phosphor/assets/fill/circle-fill.svg"),
    ("Misc/BulletSmall", "phosphor/assets/fill/circle-fill.svg"),
    ("ButtonIcons/Achievement", "phosphor/assets/fill/star-fill.svg"),
    ("ButtonIcons/GenderFemale", "phosphor/assets/bold/gender-female-bold.svg"),
    ("ButtonIcons/GenderMale", "phosphor/assets/bold/gender-male-bold.svg"),
    ("ButtonIcons/GraphsTab", "phosphor/assets/bold/chart-line-bold.svg"),
    ("ButtonIcons/LimitsTab", "phosphor/assets/fill/arrows-horizontal-fill.svg"),
    ("ButtonIcons/Map", "phosphor/assets/fill/compass-rose-fill.svg"),
    ("ButtonIcons/NewGame", "phosphor/assets/fill/file-fill.svg"),
    ("ButtonIcons/OptionsGame", "phosphor/assets/fill/joystick-fill.svg"),
    ("ButtonIcons/OptionsInput", "phosphor/assets/fill/hand-pointing-fill.svg"),
    ("ButtonIcons/OptionsSound", "phosphor/assets/fill/speaker-simple-low-fill.svg"),
    ("ButtonIcons/OrderTab", "phosphor/assets/fill/shopping-cart-fill.svg"),
    ("ButtonIcons/PurchaseTab", "phosphor/assets/fill/arrows-clockwise-fill.svg"),
    ("ButtonIcons/ResidenceTab", "phosphor/assets/fill/user-fill.svg"),
    ("ButtonIcons/ResumeGame", "phosphor/assets/fill/arrow-left-fill.svg"),
    ("ButtonIcons/ReturnMenu", "game-icons/delapouite/entry-door.svg"),
    ("ButtonIcons/Tutorial", "phosphor/assets/bold/question-mark-bold.svg"),
]


def all_specs():
    spec = build_specs()
    for sprite, rel in DEFAULTS:
        assert sprite not in spec, sprite
        spec[sprite] = [{"parts": [white_part(S(rel))]}]
    return spec


def bake_variant(sprite, v, scale):
    parts = v["parts"]
    W, H = retail_size(sprite)
    transforms = v.get("transforms", [])
    gb = glyph_bbox(parts, transforms)
    half_x = (gb[0] + gb[2]) / 2
    fit = v.get("fit", "bbox")
    if fit == "box":
        vb = parts[0].vb
    else:
        tx0, ty0, tx1, ty1 = (0, 0, W, H) if fit == "canvas" else retail_bbox(v.get("bbox_from", sprite))
        tw, th = tx1 - tx0, ty1 - ty0
        gx0, gy0, gx1, gy1 = gb
        gw, gh = gx1 - gx0, gy1 - gy0
        if fit == "crop":
            gw = gh * tw / th
        s = min(tw / gw, th / gh)  # retail px per source unit
        gcx, gcy = (gx0 + gx1) / 2, (gy0 + gy1) / 2
        tcx, tcy = (tx0 + tx1) / 2, (ty0 + ty1) / 2
        vb = [gcx - tcx / s, gcy - tcy / s, W / s, H / s]
    svg = compose(parts, vb, transforms, with_half=True, half_x=half_x)
    return raster(svg, scale * W, scale * H)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--scale", type=int, default=2, help="integer scale over the retail size (default 2)")
    ap.add_argument("--out", default=None, help="output root (default src/ext/sharp_ui/art/<scale>x)")
    ap.add_argument("--measure", default=None, metavar="KIT_SPRITE_DIR",
                    help="re-measure from <ToolKit>/resource/Dialog/Sprite and rewrite measurements.json")
    args = ap.parse_args()
    if args.measure:
        M.kit = pathlib.Path(args.measure)
    out_root = pathlib.Path(args.out) if args.out else ROOT / "src" / "ext" / "sharp_ui" / "art" / ("%dx" % args.scale)
    spec = all_specs()
    for sprite in sorted(spec):
        (v,) = spec[sprite]
        img = raster_checked(bake_variant(sprite, v, args.scale), sprite, args.scale)
        path = out_root / (sprite + ".png")
        path.parent.mkdir(parents=True, exist_ok=True)
        img.save(path, optimize=False, compress_level=9)
    print("bake: wrote %d icons at %dx to %s" % (len(spec), args.scale, out_root))
    if args.measure:
        print("bake: measurements.json rewritten (%d entries)" % M.save())


def raster_checked(img, sprite, scale):
    w, h = retail_size(sprite)
    assert img.size == (scale * w, scale * h), (sprite, img.size)
    return img


if __name__ == "__main__":
    main()
