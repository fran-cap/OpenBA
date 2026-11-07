"""tools/ext_icons/dynamicprofessions.py -- the white pictogram for the dynamic_professions row (programmer art, our own).

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


def draw_dynamic_professions(px):
    """A short list of rows, each with a dot for the worker: the professions the town has."""
    _paint(px, [
        "##.########.",
        "##.########.",
        "............",
        "##.#######..",
        "##.#######..",
        "............",
        "##.#####....",
        "##.#####....",
        "............",
        "##.####.....",
        "##.####.....",
        "............",
    ])


ICONS = {
    "dynamic_professions": ("A", draw_dynamic_professions),
}


def _glyph(rows):
    """A 16 x 16 white glyph from `rows` (16 strings of 16): '#' white, anything else clear."""
    from PIL import Image
    assert len(rows) == 16 and all(len(r) == 16 for r in rows), rows
    img = Image.new("RGBA", (16, 16), (255, 255, 255, 0))
    px = img.load()
    for y, row in enumerate(rows):
        for x, ch in enumerate(row):
            if ch == "#":
                px[x, y] = WHITE
    return img


def one_column_glyph():
    """Three rows, one column: the title bar button that makes the list one column."""
    bar = "..############.."
    gap = "................"
    return _glyph([gap, gap, bar, bar, gap, gap, bar, bar, gap, gap, bar, bar, gap, gap, gap, gap])


def two_columns_glyph():
    """Three rows of two short bars: the title bar button that makes the list two columns."""
    bar = "..#####..#####.."
    gap = "................"
    return _glyph([gap, gap, bar, bar, gap, gap, bar, bar, gap, gap, bar, bar, gap, gap, gap, gap])


# not registry row icons: white picture-button glyphs for the window's title bar (tools/ext_sprites.py draws them)
GLYPHS = {
    "professions_one_column": one_column_glyph,
    "professions_two_columns": two_columns_glyph,
}
