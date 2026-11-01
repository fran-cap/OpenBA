#!/usr/bin/env python3
"""tools/ext_tree_tiles.py -- draw the line tiles of the Family page's drawn tree (NOT RETAIL art).

Writes src/ext/ui/Dialog/Sprite/Extensions/TreeLink<1..15>.png (26 x 14: the arms of a junction, bit 1 up, 2 right,
4 down, 8 left, matching kArm* in src/ext/family_tree/tree_layout.h), TreeGap.png and TreeGapStem.png (26 x 40: the
short line between a couple's two boxes, without and with a stem down). The line is 2 px wide, light grey. Sizes are
the page's constants (kTileW, kLinkH, kBoxH in src/ext/family_tree/toolbar_page.cpp).

usage: python tools/ext_tree_tiles.py        (from the repo root; needs Pillow). Then tools/ext_sprites_bake.py.
"""
import os
from PIL import Image

OUT = os.path.join("src", "ext", "ui", "Dialog", "Sprite", "Extensions")
TILE_W, LINK_H, BOX_H = 26, 14, 40
LINE = (196, 196, 196, 255)
CLEAR = (0, 0, 0, 0)


def blank(w, h):
    return Image.new("RGBA", (w, h), CLEAR)


def rect(img, x0, y0, x1, y1):
    """Fill x0..x1, y0..y1 inclusive."""
    px = img.load()
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            px[x, y] = LINE


def main():
    os.makedirs(OUT, exist_ok=True)
    cx = TILE_W // 2 - 1          # the line is columns cx, cx + 1
    for mask in range(1, 16):
        img = blank(TILE_W, LINK_H)
        cy = LINK_H // 2 - 1      # rows cy, cy + 1
        if mask & 1:
            rect(img, cx, 0, cx + 1, cy + 1)
        if mask & 4:
            rect(img, cx, cy, cx + 1, LINK_H - 1)
        if mask & 2:
            rect(img, cx, cy, TILE_W - 1, cy + 1)
        if mask & 8:
            rect(img, 0, cy, cx + 1, cy + 1)
        img.save(os.path.join(OUT, "TreeLink%d.png" % mask))
    mid = BOX_H // 2 - 1
    gap = blank(TILE_W, BOX_H)
    rect(gap, 0, mid, TILE_W - 1, mid + 1)
    gap.save(os.path.join(OUT, "TreeGap.png"))
    stem = blank(TILE_W, BOX_H)
    rect(stem, 0, mid, TILE_W - 1, mid + 1)
    rect(stem, cx, mid, cx + 1, BOX_H - 1)
    stem.save(os.path.join(OUT, "TreeGapStem.png"))
    print("wrote 15 TreeLink tiles, TreeGap, TreeGapStem")


main()
