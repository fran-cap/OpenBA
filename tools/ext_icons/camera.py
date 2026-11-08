"""tools/ext_icons/camera.py -- the white pictogram for the better_camera row (programmer art, our own).

Same conventions as faa.py / hitch.py: draw(px) paints the 12 x 12 interior (x, y in [2, 13]) of a 16 x 16 tile.
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


def draw_better_camera(px):
    """A camera body, a dark ring around a white lens, a viewfinder bump on top: the view."""
    _paint(px, [
        "............",
        "...###......",
        ".##########.",
        ".#........#.",
        ".#..kkkk..#.",
        ".#.kk##kk.#.",
        ".#.k####k.#.",
        ".#.kk##kk.#.",
        ".#..kkkk..#.",
        ".#........#.",
        ".##########.",
        "............",
    ])


ICONS = {
    "better_camera": ("B", draw_better_camera),
}


def draw_camera_angle(px):
    """An eye looking down a slope at a dark ground line: the camera's tilt."""
    _paint(px, [
        "............",
        ".##.........",
        "####........",
        ".##.#.......",
        ".....#......",
        "......#.....",
        ".......#....",
        "........#...",
        ".........#..",
        "kkkkkkkkkkkk",
        "kkkkkkkkkkkk",
        "............",
    ])


ICONS["camera_angle"] = ("A", draw_camera_angle)
