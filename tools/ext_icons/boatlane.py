"""tools/ext_icons/boatlane.py -- the white pictogram for the boat-lane entry (programmer art, our own).

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


def draw_boat_lane(px):
    """A sailing boat over a curving lane."""
    _paint(px, [
        ".....#......",
        ".....##.....",
        ".....###....",
        ".....####...",
        ".....#......",
        ".#########..",
        "..#######...",
        "............",
        "##.......###",
        "..###..##...",
        ".....##.....",
        "............",
    ])


ICONS = {
    "boat_lane": ("B", draw_boat_lane),
}
