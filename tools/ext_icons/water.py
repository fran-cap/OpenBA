"""tools/ext_icons/water.py -- the white pictogram for the better-water entry (programmer art).

Each draw(px) paints the 12 x 12 interior (x, y in [2, 13]) of a 16 x 16 tile. Rows are 12 chars:
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


def draw_better_water(px):
    """A bank sloping into water, a broken line of foam where they meet."""
    _paint(px, [
        "kk..........",
        "kkk.........",
        "kkkk........",
        "kkkkk.......",
        "kkkkkk......",
        "kkkkkk#.#.#.",
        "kkkkkkk.....",
        "kkkkkkk#..##",
        "kkkkkkkk....",
        "kkkkkkkk.##.",
        "kkkkkkkkk...",
        "kkkkkkkkk#..",
    ])


ICONS = {
    "better_water": ("A", draw_better_water),
}
