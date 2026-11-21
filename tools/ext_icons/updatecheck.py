"""tools/ext_icons/updatecheck.py -- the white pictogram for the update_check row (class A).
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


def draw_update_check(px):
    """An arrow coming down into a tray: a newer release to fetch."""
    _paint(px, [
        ".....##.....",
        ".....##.....",
        ".....##.....",
        ".....##.....",
        ".##..##..##.",
        "..###..###..",
        "...######...",
        "....####....",
        ".....##.....",
        "#..........#",
        "#..........#",
        "############",
    ])


ICONS = {
    "update_check": ("A", draw_update_check),
}
