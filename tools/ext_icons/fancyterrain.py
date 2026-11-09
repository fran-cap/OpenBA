"""tools/ext_icons/fancyterrain.py -- the white pictogram for the fancy-terrain entry (programmer art, our own).

Same conventions as water.py: draw(px) paints the 12 x 12 interior (x, y in [2, 13]) of a 16 x 16 tile.
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


def draw_fancy_terrain(px):
    """A sun over a ridge whose far slope and the valley floor beyond it are in shade."""
    _paint(px, [
        "#...........",
        "##..........",
        "#...........",
        "............",
        "....#.......",
        "...###......",
        "..#####k....",
        ".######kk...",
        "#######kkk..",
        "#######kkkk#",
        "########kk##",
        "############",
    ])


ICONS = {
    "fancy_terrain": ("A", draw_fancy_terrain),
}
