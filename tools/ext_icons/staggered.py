"""tools/ext_icons/staggered.py -- the white pictogram for the staggered_passes row (issue #253; class A).
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


def draw_staggered_passes(px):
    """Two frames, one behind the other and offset: a white one in front, its dark twin held behind."""
    _paint(px, [
        "kkkkkkk.....",
        "k.....k.....",
        "k.....k.....",
        "k..#######..",
        "k..#.....#..",
        "k..#.....#..",
        "kkk#.....#..",
        "...#.....#..",
        "...#.....#..",
        "...#######..",
        "............",
        "............",
    ])


ICONS = {
    "staggered_passes": ("A", draw_staggered_passes),
}
