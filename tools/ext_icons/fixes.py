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


def draw_queue_first(px):
    # an arrow passing a row of waiting dots to the front (the urgent path query goes first)
    _paint(px, [
        "............",
        "..#.........",
        "..##........",
        "#######.....",
        "..##........",
        "..#.........",
        "............",
        "............",
        "##.++.++.++.",
        "##.++.++.++.",
        "............",
        "............",
    ])


def draw_field_rows(px):
    # crop rows in a field, the last row broken off (a field that finishes planting past an unreachable row)
    _paint(px, [
        "############",
        "#..........#",
        "#.++.++.++.#",
        "#..........#",
        "#.++.++.++.#",
        "#..........#",
        "#.++.++.++.#",
        "#..........#",
        "#.++.......#",
        "#..........#",
        "############",
        "............",
    ])


def draw_well(px):
    # a well: roof, posts and a round stone wall (an achievement counted against the wells it names)
    _paint(px, [
        "............",
        "..########..",
        ".##########.",
        "..#......#..",
        "..#......#..",
        "..#..##..#..",
        "..#..##..#..",
        ".++++++++++.",
        ".+########+.",
        ".+########+.",
        ".++++++++++.",
        "............",
    ])


def draw_seed_rows(px):
    # two sprouts on furrows with a dark gap between them (a crop the town names but cannot find)
    _paint(px, [
        "............",
        "............",
        ".#.......#..",
        "###.....###.",
        ".#.......#..",
        ".#...+...#..",
        ".#.......#..",
        "............",
        "####.###.###",
        "............",
        "####.###.###",
        "............",
    ])


def draw_commute(px):
    # a house and a workplace joined by a dotted walk with a tick mark (the commute is measured)
    _paint(px, [
        "............",
        "..#......##.",
        ".###....####",
        "#####...#++#",
        ".#+#....#++#",
        ".#+#....####",
        "............",
        "..#.#.#.#...",
        "............",
        ".....#......",
        "....###.....",
        "............",
    ])


def draw_arrival(px):
    # a walker beside a house with a river line between them and a turn-back arrow (arrival means at the hall)
    _paint(px, [
        "............",
        ".#.......#..",
        "###.....###.",
        ".#.....#####",
        "###....#+.+#",
        ".#.....#+.+#",
        "#.#....#####",
        "............",
        "++++++++++++",
        "............",
        "..#.........",
        ".######.....",
    ])


def draw_mods_lock(px):
    # a package with a padlock on it (the mod list is saved at once and changed only from the main menu)
    _paint(px, [
        "............",
        ".########...",
        ".#......#...",
        ".#.++++.#...",
        ".#......#...",
        ".#.++++.#...",
        ".#......###.",
        ".######.#.#.",
        "......#####.",
        "......##+##.",
        "......#####.",
        "............",
    ])


def draw_save_failed(px):
    # a floppy disk with a "!" on its label (a save that did not reach the disk says so)
    _paint(px, [
        "##########..",
        "#+++++++#.#.",
        "#+++++++#..#",
        "#+++++++####",
        "#..........#",
        "#.########.#",
        "#.###++###.#",
        "#.###++###.#",
        "#.########.#",
        "#.###++###.#",
        "#.########.#",
        "############",
    ])


def draw_boat_ridge(px):
    # a boat on water, a ridge between it and the dock post (a dock the boats cannot reach)
    _paint(px, [
        "............",
        "...#........",
        "...##.......",
        "...###....#.",
        "...#......#.",
        "+++++++...#.",
        ".+++++..+.#.",
        "........+.#.",
        "#######+++##",
        "............",
        "#.#.#.#.#.#.",
        "............",
    ])


def draw_cancel_refund(px):
    # a dashed site outline with piles (logs) laid outside it
    _paint(px, [
        "............",
        ".##......##.",
        ".##.#.#..##.",
        "...#.....#..",
        "..#.......#.",
        "...#.....#..",
        "..#.......#.",
        "...#.#.#.#..",
        "............",
        ".++..++..++.",
        ".##..##..##.",
        "............",
    ])


def draw_coin_crossed(px):
    # a crate kept back beside a coin (goods worth nothing to the merchant are not handed over)
    _paint(px, [
        "............",
        "......####..",
        ".....#....#.",
        ".....#.##.#.",
        ".....#.##.#.",
        ".....#....#.",
        "......####..",
        "############",
        "#++#++#++#.#",
        "#++#++#++#.#",
        "############",
        "............",
    ])


def draw_load_fits(px):
    # a short stack going into a nearly full bin (the fetched load fits the building's room)
    _paint(px, [
        ".....#......",
        ".....#......",
        "...#####....",
        "....###.....",
        ".....#......",
        "#..........#",
        "#..........#",
        "#..++++++..#",
        "#++++++++++#",
        "#++++++++++#",
        "############",
        "............",
    ])



def draw_wade(px):
    # a figure in the water (wave lines) walking back to a house on the bank
    _paint(px, [
        "............",
        "........#...",
        ".......###..",
        "..#...#####.",
        ".###..#+.+#.",
        "..#...#+.+#.",
        ".###..#####.",
        "..#.........",
        "+#+#+.......",
        "............",
        "++.++.++.++.",
        "............",
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
    "path_query_first": ("fix", draw_queue_first),   # arrow to the front of a waiting row
    "crop_planting_exit": ("fix", draw_field_rows),  # crop rows in a field outline
    "firefighter_wells": ("fix", draw_well),         # a well under its roof
    "unresolved_seeds": ("fix", draw_seed_rows),     # two sprouts on furrows, a gap between them
    "commute_cost": ("fix", draw_commute),           # house and workplace joined by a measured path
    "nomad_arrival": ("fix", draw_arrival),          # walker, river line, hall, turn-back arrow
    "mods_persist": ("fix", draw_mods_lock),         # a package with a padlock
    "save_failure_notice": ("fix", draw_save_failed),   # a floppy disk with a "!" on its label
    "boat_reach_notice": ("fix", draw_boat_ridge),   # a boat kept from its dock by a ridge
    "cancel_refund": ("fix", draw_cancel_refund),    # a site outline, piles laid outside it
    "autotrade_free_goods": ("fix", draw_coin_crossed),   # a crate kept back beside a coin
    "input_fetch_room": ("fix", draw_load_fits),     # a short load into a nearly full bin
    "stranded_wade": ("fix", draw_wade),             # a figure in the water, a house on the bank
}
