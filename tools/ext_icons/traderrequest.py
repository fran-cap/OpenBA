"""tools/ext_icons/traderrequest.py -- the white pictogram for the trader_request row (programmer art, our own).

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


def draw_trader_request(px):
    """A trade boat (mast, sail, hull) on a dark waterline: the trader the post asked for."""
    _paint(px, [
        "............",
        ".....#......",
        ".....##.....",
        ".....###....",
        ".....####...",
        ".....#####..",
        ".....#......",
        "############",
        ".##########.",
        "..########..",
        "kkkkkkkkkkkk",
        "............",
    ])


ICONS = {
    "trader_request": ("C", draw_trader_request),
}
