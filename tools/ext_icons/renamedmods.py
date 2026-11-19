"""tools/ext_icons/renamedmods.py -- the white pictogram for the renamed_mods row (programmer art, our own).

Same conventions as recipeskip.py: draw(px) paints the 12 x 12 interior (x, y in [2, 13]) of a 16 x 16 tile.
'#' = white, '.' = leave the tier colour showing.
"""

WHITE = (255, 255, 255, 255)


def _paint(px, rows):
    assert len(rows) == 12 and all(len(r) == 12 for r in rows), rows
    for j, row in enumerate(rows):
        for i, ch in enumerate(row):
            if ch == "#":
                px[2 + i, 2 + j] = WHITE


def draw_renamed_mods(px):
    """A package under its old name, an arrow, the same package under its new name: it still loads."""
    _paint(px, [
        "............",
        "####....####",
        "#..#....#..#",
        "#..#..#.#..#",
        "#..#...##..#",
        "#..######..#",
        "#..#...##..#",
        "#..#..#.#..#",
        "#..#....#..#",
        "####....####",
        "............",
        "............",
    ])


ICONS = {
    "renamed_mods": ("B", draw_renamed_mods),
}
