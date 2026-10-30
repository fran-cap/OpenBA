"""tools/ext_icons/toolnames.py -- the white pictogram for the tool_names row (programmer art, our own).

Same conventions as camera.py: draw(px) paints the 12 x 12 interior (x, y in [2, 13]) of a 16 x 16 tile.
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


def draw_tool_names(px):
    """A hammer with a name tag line under it: the tool, and its name."""
    _paint(px, [
        "............",
        "..######....",
        "..#######...",
        "..######k#..",
        "....##..k...",
        "....##......",
        "....##......",
        "....##......",
        "............",
        ".########k..",
        "............",
        ".#######....",
    ])


ICONS = {
    "tool_names": ("A", draw_tool_names),
}
