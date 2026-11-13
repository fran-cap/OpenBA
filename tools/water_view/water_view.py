#!/usr/bin/env python3
"""tools/water_view/water_view.py -- a top-down picture of a BANISHED_WATER_DUMP (README.md alongside).

  water_view.py <water_dump.bin> --out map.png                    the whole map
  water_view.py <water_dump.bin> --out a.png --at=31,-58 --radius 12   a close-up (height-cell coordinates;
                                                                  write --at=X,Y: a leading minus needs the =)
  water_view.py <water_dump.bin> --out b.png --at=31,-58 --carve 1.5   the same with a least channel previewed
  water_view.py <water_dump.bin> --thin                           list the thin channels
  water_view.py <water_dump.bin> --thin --crops 6 --out dir       ... and draw the six largest

What the picture shows, per pixel, from the terrain's own triangles (the diagonal alternates per cell):
  land (tan to green by height), water (blue by depth: under the waterline AND under the water mesh),
  bare bed (magenta: under the waterline, no water mesh over it). At --scale 10 and up: the cell grid,
  each height vertex as a dot (white = under the waterline, brown = above) and the water mesh's edges
  (yellow river, cyan lake). `--mesh retail` draws retail's mesh instead of the extension's copy;
  `--mesh both` draws retail's edges in red under the copy's.
"""
import argparse
import os
import struct
import sys

import numpy as np
from PIL import Image, ImageDraw

WATERLINE = -0.75      # GetUnderWaterAmount's 0 (functions.srsl); the water surface
POS_SCALE = 48.0       # water_contour_mesh.cpp: position = tile * 48
HEIGHT_STEP = 1.0 / 256.0
VERTEX = np.dtype([("pos", "<i2", 4), ("uv", "<i2", 2), ("n", "u1", 4)])


class Dump:
    def __init__(self, path):
        raw = open(path, "rb").read()
        if raw[:4] != b"BWD1":
            sys.exit(f"{path}: not a water dump (no BWD1 header)")
        nv, ni, nr, nl, stride, half, _, _ = struct.unpack_from("<8i", raw, 4)
        at = 36
        self.retail = np.frombuffer(raw, VERTEX, nv, at)
        at += nv * 16
        self.drawn = np.frombuffer(raw, VERTEX, nv, at)
        at += nv * 16
        self.indices = np.frombuffer(raw, "<u2", ni, at)
        at += ni * 2
        self.rivers = np.frombuffer(raw, "<i4", nr * 4, at).reshape(nr, 4)
        at += nr * 16
        self.lakes = np.frombuffer(raw, "<i4", nl * 4, at).reshape(nl, 4)
        at += nl * 16
        self.stride = stride
        self.half = half
        self.h = np.frombuffer(raw, "<i2", stride * stride, at).reshape(stride, stride) * HEIGHT_STEP

    def triangles(self, verts, ranges):
        """(T, 3, 2) tile coordinates of the ranges' triangles."""
        out = []
        for first, base, _, n in ranges:   # {first index, base vertex, vertex count, index count}
            idx = self.indices[first:first + n].astype(np.int64) + base
            xy = verts["pos"][idx][:, :2] / POS_SCALE
            out.append(xy.reshape(-1, 3, 2))
        return np.concatenate(out) if out else np.zeros((0, 3, 2))


def height_at(d, x, y):
    """The terrain height at tile coordinates (arrays), on the terrain mesh's own triangles."""
    cx = np.clip(x + d.half, 0, d.stride - 1.001)
    cy = np.clip(y + d.half, 0, d.stride - 1.001)
    col = np.floor(cx).astype(np.int64)
    row = np.floor(cy).astype(np.int64)
    fx = cx - col
    fy = cy - row
    h00 = d.h[row, col]
    h10 = d.h[row, col + 1]
    h01 = d.h[row + 1, col]
    h11 = d.h[row + 1, col + 1]
    even = ((col + row) & 1) == 0
    # even: the diagonal runs (col+1,row) - (col,row+1); odd: (col,row) - (col+1,row+1)
    e_a = h00 + fx * (h10 - h00) + fy * (h01 - h00)
    e_b = h11 + (1 - fx) * (h01 - h11) + (1 - fy) * (h10 - h11)
    o_a = h00 + fx * (h10 - h00) + fy * (h11 - h10)
    o_b = h00 + fx * (h11 - h01) + fy * (h01 - h00)
    return np.where(even, np.where(fx + fy <= 1, e_a, e_b), np.where(fx >= fy, o_a, o_b))


def render(d, x0, y0, x1, y1, scale, mesh, path):
    w = int(round((x1 - x0) * scale))
    h = int(round((y1 - y0) * scale))
    xs = x0 + (np.arange(w) + 0.5) / scale
    ys = y0 + (np.arange(h) + 0.5) / scale
    gx, gy = np.meshgrid(xs, ys)
    above = height_at(d, gx, gy) - WATERLINE

    def to_px(p):
        return [((x - x0) * scale, (y - y0) * scale) for x, y in p]

    verts = d.retail if mesh == "retail" else d.drawn
    tri_r = d.triangles(verts, d.rivers)
    tri_l = d.triangles(verts, d.lakes)
    cover = Image.new("L", (w, h), 0)
    cd = ImageDraw.Draw(cover)
    for t in np.concatenate([tri_r, tri_l]):
        cd.polygon(to_px(t), fill=255)
    covered = np.asarray(cover) > 0

    img = np.zeros((h, w, 3), np.float32)
    t = np.clip(above / 6.0, 0, 1)[..., None]
    land = (1 - t) * np.array([196, 178, 128], np.float32) + t * np.array([70, 120, 60], np.float32)
    depth = np.clip(-above / 1.5, 0, 1)[..., None]
    water = (1 - depth) * np.array([120, 190, 235], np.float32) + depth * np.array([20, 60, 150], np.float32)
    wet = above < 0
    img[:] = land
    img[wet & covered] = water[wet & covered]
    img[wet & ~covered] = np.array([225, 60, 200], np.float32)
    out = Image.fromarray(img.astype(np.uint8))
    dr = ImageDraw.Draw(out)

    if scale >= 10:
        for gxi in range(int(np.ceil(x0)), int(np.floor(x1)) + 1):
            px = (gxi - x0) * scale
            dr.line([(px, 0), (px, h)], fill=(90, 90, 90), width=1)
        for gyi in range(int(np.ceil(y0)), int(np.floor(y1)) + 1):
            py = (gyi - y0) * scale
            dr.line([(0, py), (w, py)], fill=(90, 90, 90), width=1)
    if scale >= 6:
        if mesh == "both":
            for t in d.triangles(d.retail, d.rivers):
                dr.polygon(to_px(t), outline=(230, 40, 40))
        for t in tri_l:
            dr.polygon(to_px(t), outline=(0, 220, 220))
        for t in tri_r:
            dr.polygon(to_px(t), outline=(255, 230, 0))
    if scale >= 10:
        r = max(2, scale // 8)
        for gyi in range(int(np.ceil(y0)), int(np.floor(y1)) + 1):
            for gxi in range(int(np.ceil(x0)), int(np.floor(x1)) + 1):
                row = gyi + d.half
                col = gxi + d.half
                if not (0 <= row < d.stride and 0 <= col < d.stride):
                    continue
                a = d.h[row, col] - WATERLINE
                px = (gxi - x0) * scale
                py = (gyi - y0) * scale
                fill = (255, 255, 255) if a < 0 else (110, 70, 30)
                dr.ellipse([px - r, py - r, px + r, py + r], fill=fill, outline=(0, 0, 0))
                if scale >= 36:
                    dr.text((px + r + 1, py + r + 1), f"{a:+.2f}", fill=(0, 0, 0))
    if scale >= 10:
        step = 5 if scale < 24 else 1
        for gxi in range(int(np.ceil(x0)), int(np.floor(x1)) + 1):
            if gxi % step == 0:
                dr.text(((gxi - x0) * scale + 2, 1), str(gxi), fill=(0, 0, 0))
        for gyi in range(int(np.ceil(y0)), int(np.floor(y1)) + 1):
            if gyi % step == 0:
                dr.text((1, (gyi - y0) * scale + 2), str(gyi), fill=(0, 0, 0))
    out.save(path)
    print(f"[water_view] {path}: tiles x {x0:g}..{x1:g}, y {y0:g}..{y1:g}, {scale} px per height cell, mesh={mesh}")


def carve(d, half_width, depth):
    """A PREVIEW, not what the game does: lowers the heights along every river's centreline (the middle
    vertex of each mesh row) to a parabolic channel `half_width` tiles to each side, `depth` deep in the
    middle. A height is only ever lowered. Shows what a least channel width would look like."""
    h = d.h.copy()
    reach = int(np.ceil(half_width)) + 1
    for _, base, count, _ in d.rivers:
        if count % 5 != 0:
            continue
        mid = d.retail["pos"][base:base + count][2::5, :2] / POS_SCALE
        for a, b in zip(mid[:-1], mid[1:]):
            steps = max(1, int(np.ceil(np.hypot(*(b - a)) / 0.25)))
            for k in range(steps + 1):
                p = a + (b - a) * (k / steps)
                col0 = int(np.floor(p[0])) + d.half
                row0 = int(np.floor(p[1])) + d.half
                for row in range(max(0, row0 - reach), min(d.stride, row0 + reach + 2)):
                    for col in range(max(0, col0 - reach), min(d.stride, col0 + reach + 2)):
                        dist = np.hypot(col - d.half - p[0], row - d.half - p[1])
                        if dist >= half_width:
                            continue
                        bed = WATERLINE - depth * (1.0 - (dist / half_width) ** 2)
                        if bed < h[row, col]:
                            h[row, col] = bed
    lowered = int((h < d.h).sum())
    d.h = h
    print(f"[water_view] carve preview: half width {half_width:g}, depth {depth:g}: {lowered} heights lowered")


def thin_channels(d, near_tiles=4):
    """Clusters of height vertices under the waterline that are part of no fully wet cell, near a river."""
    wet = (d.h - WATERLINE) < 0
    full = wet[:-1, :-1] & wet[1:, :-1] & wet[:-1, 1:] & wet[1:, 1:]   # cells with four wet corners
    in_full = np.zeros_like(wet)
    in_full[:-1, :-1] |= full
    in_full[1:, :-1] |= full
    in_full[:-1, 1:] |= full
    in_full[1:, 1:] |= full
    near = np.zeros_like(wet)
    for _, base, count, _ in d.rivers:
        xy = d.retail["pos"][base:base + count][:, :2] / POS_SCALE
        col = np.clip(np.round(xy[:, 0]).astype(int) + d.half, 0, d.stride - 1)
        row = np.clip(np.round(xy[:, 1]).astype(int) + d.half, 0, d.stride - 1)
        near[row, col] = True
    for _ in range(near_tiles):
        grown = near.copy()
        grown[1:, :] |= near[:-1, :]
        grown[:-1, :] |= near[1:, :]
        grown[:, 1:] |= near[:, :-1]
        grown[:, :-1] |= near[:, 1:]
        near = grown
    thin = wet & ~in_full & near
    seen = np.zeros_like(thin)
    clusters = []
    for row, col in zip(*np.nonzero(thin)):
        if seen[row, col]:
            continue
        stack = [(row, col)]
        seen[row, col] = True
        cells = []
        while stack:
            r, c = stack.pop()
            cells.append((r, c))
            for dr_ in (-1, 0, 1):
                for dc in (-1, 0, 1):
                    rr = r + dr_
                    cc = c + dc
                    if 0 <= rr < d.stride and 0 <= cc < d.stride and thin[rr, cc] and not seen[rr, cc]:
                        seen[rr, cc] = True
                        stack.append((rr, cc))
        cells = np.array(cells)
        # diagonal steps: vertices whose only wet neighbours in the cluster are diagonal
        diag = 0
        cell_set = {(int(r), int(c)) for r, c in cells}
        for r, c in cell_set:
            ortho = any((r + a, c + b) in cell_set for a, b in ((1, 0), (-1, 0), (0, 1), (0, -1)))
            if not ortho:
                diag += 1
        clusters.append({
            "count": len(cells),
            "diagonal": diag,
            "x": float(cells[:, 1].mean() - d.half),
            "y": float(cells[:, 0].mean() - d.half),
            "x0": int(cells[:, 1].min() - d.half), "x1": int(cells[:, 1].max() - d.half),
            "y0": int(cells[:, 0].min() - d.half), "y1": int(cells[:, 0].max() - d.half),
        })
    clusters.sort(key=lambda c: (-c["diagonal"], -c["count"]))
    return clusters


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("dump")
    ap.add_argument("--out", help="the png (or, with --crops, the directory)")
    ap.add_argument("--at", help="x,y height-grid coordinates of the close-up's centre (a game tile is two cells)")
    ap.add_argument("--radius", type=float, default=12.0)
    ap.add_argument("--scale", type=int, help="pixels per height cell (default 3 whole map, 40 close-up)")
    ap.add_argument("--mesh", choices=("drawn", "retail", "both"), default="drawn")
    ap.add_argument("--thin", action="store_true", help="list the thin channels along the rivers")
    ap.add_argument("--crops", type=int, default=0, help="with --thin: draw this many of them into --out")
    ap.add_argument("--carve", type=float, default=0.0, metavar="HALF_WIDTH",
                    help="preview: a least channel this many height cells to each side of every river's centreline")
    ap.add_argument("--carve-depth", type=float, default=0.35, help="the preview channel's depth (default 0.35)")
    args = ap.parse_args()

    d = Dump(args.dump)
    print(f"[water_view] {len(d.retail)} water vertices, {len(d.rivers)} river + {len(d.lakes)} lake ranges, "
          f"heights {d.stride} x {d.stride} (cells {-d.half}..{d.stride - 1 - d.half})")
    if args.carve > 0:
        carve(d, args.carve, args.carve_depth)
    if args.thin:
        clusters = thin_channels(d)
        print(f"[water_view] {len(clusters)} thin channel stretch(es); 'diagonal' = wet vertices with no wet "
              f"vertex beside them, only cornerwise")
        print("   #  vertices  diagonal  centre (x, y)      box")
        for i, c in enumerate(clusters[:40]):
            print(f"  {i:2d}  {c['count']:8d}  {c['diagonal']:8d}  ({c['x']:7.1f}, {c['y']:7.1f})  "
                  f"x {c['x0']}..{c['x1']}  y {c['y0']}..{c['y1']}")
        if args.crops:
            if not args.out:
                sys.exit("--crops needs --out <directory>")
            os.makedirs(args.out, exist_ok=True)
            for i, c in enumerate(clusters[:args.crops]):
                rad = max(8.0, 0.5 * max(c["x1"] - c["x0"], c["y1"] - c["y0"]) + 4.0)
                scale = args.scale or int(max(12, min(40, 1000 // (2 * rad))))
                render(d, c["x"] - rad, c["y"] - rad, c["x"] + rad, c["y"] + rad, scale, args.mesh,
                       os.path.join(args.out, f"thin_{i:02d}.png"))
        return
    if not args.out:
        sys.exit("--out <png> is required")
    if args.at:
        x, y = (float(v) for v in args.at.split(","))
        render(d, x - args.radius, y - args.radius, x + args.radius, y + args.radius, args.scale or 40, args.mesh,
               args.out)
    else:
        lo = -d.half
        hi = d.stride - 1 - d.half
        render(d, lo, lo, hi, hi, args.scale or 3, args.mesh, args.out)


if __name__ == "__main__":
    main()
