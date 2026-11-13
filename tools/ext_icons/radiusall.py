"""tools/ext_icons/radiusall.py -- the white pictogram for the radius_all row (programmer art, our own).

Same conventions as nofog.py: draw(px) paints the 12 x 12 interior (x, y in [2, 13]) of a 16 x 16 tile.
'#' = white, '.' = leave the tier colour showing.
"""

WHITE = (255, 255, 255, 255)


def _paint(px, rows):
    assert len(rows) == 12 and all(len(r) == 12 for r in rows), rows
    for j, row in enumerate(rows):
        for i, ch in enumerate(row):
            if ch == "#":
                px[2 + i, 2 + j] = WHITE


def draw_radius_all(px):
    """Two work circles that overlap, each with its building at the centre."""
    _paint(px, [
        "..###.......",
        ".#...#......",
        "#.....#.....",
        "#..#..###...",
        "#....##..#..",
        ".#..##....#.",
        "..###..#..#.",
        "....#.....#.",
        ".....#...#..",
        "......###...",
        "............",
        "............",
    ])


ICONS = {
    "radius_all": ("A", draw_radius_all),
}
