"""tools/ext_icons/roadupgrade.py -- the white pictogram for the road_upgrade row (programmer art, our own).

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


def draw_road_upgrade(px):
    """Loose dirt on the left, an arrow, laid stones on the right: a dirt road becomes a stone road."""
    _paint(px, [
        "............",
        "............",
        "#.#......##.",
        ".....#......",
        ".#....#...##",
        "....####....",
        "#.#...#..##.",
        ".....#......",
        ".#........##",
        "............",
        "............",
        "............",
    ])


ICONS = {
    "road_upgrade": ("A", draw_road_upgrade),
}
