"""tools/ext_icons/winterice.py -- the white pictograms for the winter-ice and walk-on-ice entries (programmer art, our own).

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


def draw_winter_ice(px):
    """A snowflake over a frozen sheet with a crack running through it."""
    _paint(px, [
        ".....#......",
        "...#.#.#....",
        "....###.....",
        "..#######...",
        "....###.....",
        "...#.#.#....",
        ".....#......",
        "............",
        "############",
        "####k#######",
        "#####kk##k##",
        "#######kk###",
    ])


def draw_walk_on_ice(px):
    """Two footprints crossing a frozen sheet (ext.walk_on_ice, the class C row)."""
    _paint(px, [
        "......##....",
        ".....####...",
        ".....####...",
        "......##....",
        "..##........",
        ".####.......",
        ".####.......",
        "..##........",
        "############",
        "#####k######",
        "##kk##k###k#",
        "####kk#kkk##",
    ])


ICONS = {
    "winter_ice": ("A", draw_winter_ice),
    "walk_on_ice": ("C", draw_walk_on_ice),
}
