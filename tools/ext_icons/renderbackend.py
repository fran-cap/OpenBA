"""tools/ext_icons/renderbackend.py -- the white pictogram for the render_backend row (class A).
Programmer art, our own.

Same conventions as phone.py: draw(px) paints the 12 x 12 interior (x, y in [2, 13]) of a 16 x 16 tile.
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


def draw_render_backend(px):
    """Two cards, one over the other, with a swap arrow between them: one renderer or the other."""
    _paint(px, [
        "######......",
        "#kkkk#......",
        "#kkkk#..#...",
        "######.###..",
        "........#...",
        "...#....#...",
        "...#....#...",
        "...#........",
        "..###.######",
        "...#..#kkkk#",
        "......#kkkk#",
        "......######",
    ])


ICONS = {
    "render_backend": ("A", draw_render_backend),
}
