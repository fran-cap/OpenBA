"""tools/ext_icons/itemlimits.py -- the white pictogram for the item_limits row (programmer art, our own).

Same conventions as highlimits.py: draw(px) paints the 12 x 12 interior (x, y in [2, 13]) of a 16 x 16 tile.
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


def draw_item_limits(px):
    """Three stacks of crates; the middle one stops at a dark limit line above it: one item held to its own limit."""
    _paint(px, [
        "............",
        "............",
        "kkkkkkkkkkkk",
        "............",
        "..........##",
        "##........##",
        "##..##....##",
        "##..##....##",
        "##..##..####",
        "##..##..####",
        "##..##..####",
        "############",
    ])


ICONS = {
    "item_limits": ("C", draw_item_limits),
}
