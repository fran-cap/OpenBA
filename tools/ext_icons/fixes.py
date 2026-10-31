"""Row icons for the "fix" tier of the Extensions dialog (programmer art, NOT RETAIL art).

Each draw(px) paints a 12 x 12 pictogram into the interior x, y in [2, 13] of a 16 x 16 tile:
'#' = white, '+' = dark accent, '.' = leave the tier colour. Read by tools/ext_sprites.py.
"""

WHITE = (255, 255, 255, 255)
DARK = (42, 42, 42, 255)


def _paint(px, rows):
    for j, row in enumerate(rows):
        for i, c in enumerate(row):
            if c == "#":
                px[2 + i, 2 + j] = WHITE
            elif c == "+":
                px[2 + i, 2 + j] = DARK


def draw_slide(px):
    # "!" marker popping in place above a ground line (status icons no longer slide in)
    _paint(px, [
        "............",
        ".....##.....",
        ".....##.....",
        ".....##.....",
        ".....##.....",
        "............",
        ".....##.....",
        ".....##.....",
        "............",
        "............",
        "###########.",
        "............",
    ])


def draw_goto(px):
    # house with a target box on its wall (the "go to resident" button centres on them)
    _paint(px, [
        ".....##.....",
        "....####....",
        "...######...",
        "..########..",
        ".##########.",
        "..########..",
        "..##++++##..",
        "..##+##+##..",
        "..##+##+##..",
        "..##++++##..",
        "..########..",
        "............",
    ])


def draw_mug(px):
    # tankard with foam top and handle (tavern statistics count the good actually consumed)
    _paint(px, [
        "..#######...",
        "..#######...",
        "..#+++++#.##",
        "..#+++++#..#",
        "..#+++++#..#",
        "..#+++++#.##",
        "..#+++++#...",
        "..#+++++#...",
        "..#+++++#...",
        "..#######...",
        "............",
        "............",
    ])


def draw_barn(px):
    # barn with pitched roof and door, resting on a ground line (starting buildings sit on the ground)
    _paint(px, [
        ".....##.....",
        "....####....",
        "...######...",
        "..########..",
        ".##########.",
        "..########..",
        "..##++++##..",
        "..##++++##..",
        "..##++++##..",
        "..##++++##..",
        "############",
        "............",
    ])


def draw_window(px):
    # centred dialog frame with a title bar and resize corner brackets (UI scale keeps dialogs in place)
    _paint(px, [
        "##........##",
        "#..........#",
        "............",
        "..########..",
        "..########..",
        "..#......#..",
        "..#......#..",
        "..#......#..",
        "..########..",
        "............",
        "#..........#",
        "##........##",
    ])


def draw_slider(px):
    # slider track with the thumb held at its right end stop (a dragged slider clamps, never jumps back)
    _paint(px, [
        "............",
        "............",
        "..........##",
        "........####",
        "#.......####",
        "#+++++++####",
        "#+++++++####",
        "#.......####",
        "........####",
        "..........##",
        "............",
        "............",
    ])


def draw_drift(px):
    # a panel resting on the screen's bottom edge, a bar over it (a panel at the edge stays put, no jump)
    _paint(px, [
        "############",
        "............",
        "..########..",
        "..#++++++#..",
        "..########..",
        "..#......#..",
        "..#......#..",
        "..#......#..",
        "..#......#..",
        "..########..",
        "............",
        "############",
    ])


def draw_tiebreak(px):
    # two equal figures, numbered 1 and 2 under them (equal candidates always taken in one order)
    _paint(px, [
        "............",
        "..##....##..",
        "..##....##..",
        "............",
        ".####..####.",
        ".####..####.",
        ".####..####.",
        "..##....##..",
        "..##....##..",
        "............",
        "..+.....++..",
        "..+.....++..",
    ])


def draw_outline_panel(px):
    # a panel over the outlined plot it belongs to (the outline arrives with its panel, not before it)
    _paint(px, [
        "..########..",
        "..#++++++#..",
        "..########..",
        "..#......#..",
        "..########..",
        "............",
        "############",
        "#..........#",
        "#..++++++..#",
        "#..........#",
        "############",
        "............",
    ])


def draw_foundation(px):
    # a dashed foundation outline, a log lying just outside it (piles carried off a site's footprint)
    _paint(px, [
        "############",
        "#..........#",
        "#..........#",
        "#..........#",
        "#..........#",
        "#..........#",
        "#..........#",
        "############",
        "............",
        "..++++++++..",
        ".+########+.",
        "..++++++++..",
    ])


ICONS = {
    "icon_slide": ("fix", draw_slide),               # "!" marker over a ground line
    "residence_goto": ("fix", draw_goto),            # house with a target box on the wall
    "tavern_counter": ("fix", draw_mug),             # tankard with foam and handle
    "start_barn_height": ("fix", draw_barn),         # barn with door on a ground line
    "ui_scale_layout": ("fix", draw_window),         # centred dialog frame, resize corner marks
    "slider_drag_clamp": ("fix", draw_slider),       # slider track, thumb at its end stop
    "dialog_position_drift": ("fix", draw_drift),    # panel held between two edge bars
    "alloc_tiebreak": ("fix", draw_tiebreak),        # two equal figures, numbered
    "outline_with_panel": ("fix", draw_outline_panel),   # a panel over its outlined plot
    "foundation_piles": ("fix", draw_foundation),    # foundation outline, a log outside it
}
