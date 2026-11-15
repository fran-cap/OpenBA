"""tools/ext_icons/smoothreplay.py -- the white pictogram for the render_pipe_replay row (programmer art, our own).

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


def draw_render_pipe_replay(px):
    """A fast-forward mark with the frames in between drawn behind it: the pictures shown during a long step."""
    _paint(px, [
        "............",
        "............",
        "k..k.#...#..",
        "k..k.##..##.",
        "k..k.###.###",
        "k..k.#######",
        "k..k.#######",
        "k..k.###.###",
        "k..k.##..##.",
        "k..k.#...#..",
        "............",
        "............",
    ])


ICONS = {
    "render_pipe_replay": ("A", draw_render_pipe_replay),
}
