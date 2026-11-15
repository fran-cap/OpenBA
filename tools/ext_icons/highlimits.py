"""tools/ext_icons/highlimits.py -- the white pictogram for the high_limits row (programmer art, our own).

Same conventions as itemlimits.py: draw(px) paints the 12 x 12 interior (x, y in [2, 13]) of a 16 x 16 tile.
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


def draw_high_limits(px):
    """A tall bar rising past a dark limit line, with an arrow head: a limit pushed higher."""
    _paint(px, [
        ".....##.....",
        "....####....",
        "...######...",
        "..########..",
        "kkkk####kkkk",
        ".....##.....",
        ".....##.....",
        ".....##.....",
        ".....##.....",
        "############",
        "............",
        "............",
    ])


ICONS = {
    "high_limits": ("B", draw_high_limits),
}
