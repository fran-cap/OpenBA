"""tools/ext_icons/storagemetrics.py -- the white pictogram for the storage_metrics row (programmer art, our own).

Same conventions as showgrid.py: draw(px) paints the 12 x 12 interior (x, y in [2, 13]) of a 16 x 16 tile.
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


def draw_storage_metrics(px):
    """A crate filled to about four fifths: the open box, its contents, the empty band on top."""
    _paint(px, [
        "............",
        "#..........#",
        "#..........#",
        "#..........#",
        "#.########.#",
        "#.########.#",
        "#.##k##k##.#",
        "#.########.#",
        "#.##k##k##.#",
        "#.########.#",
        "#..........#",
        "############",
    ])


ICONS = {
    "storage_metrics": ("A", draw_storage_metrics),
}


def storage_tab_glyph():
    """A 16 x 16 white crate seen from the front: the lid, the box and its two cross battens. The Storage tab's icon."""
    from PIL import Image
    rows = [
        "................",
        "................",
        ".##############.",
        ".##############.",
        "..#..........#..",
        "..############..",
        "..##........##..",
        "..#.##....##.#..",
        "..#...####...#..",
        "..#...####...#..",
        "..#.##....##.#..",
        "..##........##..",
        "..############..",
        "..############..",
        "................",
        "................",
    ]
    assert len(rows) == 16 and all(len(r) == 16 for r in rows), rows
    img = Image.new("RGBA", (16, 16), (255, 255, 255, 0))
    px = img.load()
    for y, row in enumerate(rows):
        for x, ch in enumerate(row):
            if ch == "#":
                px[x, y] = WHITE
    return img


# not a registry row icon: the white glyph on the Town Hall's Storage tab (tools/ext_sprites.py draws it)
GLYPHS = {
    "storage_metrics_tab": storage_tab_glyph,
}
