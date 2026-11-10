"""tools/ext_icons/workcand.py -- the white pictogram for the work_cand row (programmer art, our own).

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


def draw_work_cand(px):
    """Three bars in ascending order: the jobs as one sorted list."""
    _paint(px, [
        "............",
        "............",
        "###.........",
        "###.........",
        "............",
        "#######.....",
        "#######.....",
        "............",
        "###########.",
        "###########.",
        "............",
        "............",
    ])


ICONS = {
    "work_cand": ("B", draw_work_cand),
}
