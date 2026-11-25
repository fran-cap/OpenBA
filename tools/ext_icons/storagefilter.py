"""tools/ext_icons/storagefilter.py -- the white pictogram for the storage_filter row (programmer art, our own).

Same conventions as traderrequest.py: draw(px) paints the 12 x 12 interior (x, y in [2, 13]) of a 16 x 16 tile.
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


def draw_storage_filter(px):
    """A crate with a dark slash through it: the storage building turns an item away."""
    _paint(px, [
        "############",
        "#.........k#",
        "#........k.#",
        "#.......k..#",
        "#......k...#",
        "#.....k....#",
        "#....k.....#",
        "#...k......#",
        "#..k.......#",
        "#.k........#",
        "#k.........#",
        "############",
    ])


ICONS = {
    "storage_filter": ("C", draw_storage_filter),
}


def storage_filter_tab_glyph():
    """A 16 x 16 white crate with a check mark in it: the Accepts tab's icon."""
    from PIL import Image
    rows = [
        "................",
        "................",
        ".##############.",
        ".##############.",
        "..#..........#..",
        "..#.......#..#..",
        "..#......##..#..",
        "..#.#...##...#..",
        "..#.##.##....#..",
        "..#..###.....#..",
        "..#...#......#..",
        "..#..........#..",
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


# not a registry row icon: the white glyph on the Accepts tab (tools/ext_sprites.py draws it)
GLYPHS = {
    "storage_filter_tab": storage_filter_tab_glyph,
}
