---
name: menu-driver
description: Drive OpenBA.exe menus unattended - how BANISHED_AUTOSTART works and how to click front-end, Mods, ModDetails and pause-menu buttons with BANISHED_AUTOMENU_BUTTON / BANISHED_AUTOMENU_CLICKS / BANISHED_PAUSEMENU_* / BANISHED_MODAL_ANSWER. Use when a test needs a menu route other than plain "new game", or when autostart itself misbehaves.
---

# Unattended menu driving

Every flag here is declared, with its reader, in `src/app/options.h`. Whether a given
route currently works is recorded in `docs/runtime_port/STATUS.md`.

## Autostart (what `tools/build.sh run` does)
`BANISHED_AUTOSTART` (any value except `0`/`false`) auto-drives menu → New Game → Start.
A numeric value > 1 overrides the menu settle-frame count (default 60). A direct launch of
`OpenBA.exe` leaves it unset and stops at the menu, as does `tools/build.sh run-menu`.

- **Mechanism**: `src/diag/autostart.cpp` ticks a state machine from
  `MenuDLT::Stub_Update`. It injects the two reimplemented button-click events through the
  dialog vtable slot-8 handlers (`MainMenuDialog::OnCommand` id=0 "buttonNew", then
  `NewGameDialog::OnEvent` data=2 "buttonOk") — no input simulation. The Stage-C
  transition in `MenuDLT::Update` then advances to the queued GameUpdateLoop chain exactly
  like a manual click.
- **Trace**: grep `standalone_error.log` for `[AutoStart]` (each step logs). Not
  `standalone_debug.log`: a grep there returns 0 on a healthy run, the same signature as a
  run that never built a world.
- A 600-frame (~10 s) timeout aborts the Start injection if `NewGameDialog::buttonOk`
  (+0xF0) never populates, rather than crashing on half-built combos.

## Load a save, or quit
Same state machine, other routes (`src/diag/autostart.cpp`), both unattended:
- `BANISHED_AUTOLOAD=<save name>` drives menu -> Load -> select that save -> Load, the full retail
  load path. `1` means the autosave `Auto`. The save is read from the run's user dir
  (`Documents/Banished/Save`, or `$BANISHED_USER_DIR/Save` in the functional game dir), so copy a
  reporter's or fixture save there first. A save that carries mods or `ext:` rows raises a modal:
  add `BANISHED_MODAL_ANSWER=2` to accept it (the most common pairing in practice). The trace is
  `[AutoStart] AUTOLOAD ... sequence fired` in `standalone_error.log`.
- `BANISHED_AUTOQUIT[=<settle frames>]` clicks the menu's Quit: the only retail exit path, so it
  is the one that writes settings.cfg back. Use it to check a setting round-trips.

## Hold a building panel open
`BANISHED_PANEL_HOLD=<ComponentTypeName>` (e.g. `StorageComponent`, `ConsumeProduceComponent`,
`ResidenceComponent`) selects one such building in game and leaves its info panel up for the rest
of the run, for a window capture (`tools/capture_window.ps1`, skill `camera-pin`).
`BANISHED_PANEL_HOLD_FRAME=<n>` is when (default 3000; give a workplace time to staff and stock),
`BANISHED_PANEL_HOLD_CLICK=<Controller>:<elementId>` presses one of that panel's buttons, every
`BANISHED_PANEL_HOLD_CLICK_EVERY=<frames>` (default 300) from the open frame on
(`src/diag/panel_hold_harness.cpp`). `BANISHED_PANEL_HOLD_TAB=<index>` shows that tab of the held panel
30 frames after it opened (0-based, `-1` = the last: an extension's added tab). The harness presses the
panel's "go to" after the select, so the panel is pinned and the camera is on the building: retail closes
an unpinned panel whose building is off screen, which is what a loaded save's camera usually gave.

## A mod's template paths
`BANISHED_TEMPLATE_DUMP=<DescriptionTypeName>` (`*` = every building) logs, 120 frames into a game, the
template path of each placeable building whose template carries that description
(`StorageLocationDescription`, `TradeDescription`, ...) as `[TemplateDump]` lines in
`standalone_error.log` (`src/diag/template_dump.cpp`). The paths feed `BANISHED_SHOWROOM`'s list
(finished buildings; a `bench <dist>` line keeps the game running with no shots) and
`BANISHED_PLACEMENT_TEMPLATE`.

## Front-end buttons
`BANISHED_AUTOMENU_BUTTON=<id>` clicks one main-menu button and stops there:
New=0, Load=2, Options=4, Quit=6, Mods=7, Achievements=8. Id 10 (Goals) is NOT a front-end
button: `buttonGoal` exists only in the pause-menu `.rsc` (and hides itself when the game has no
scenario). Injecting id 10 at the front end dispatches `MainMenuDialog::OnCommand` -> `OpenGoal`
with no game world and `GoalDialog` faults on the null world read (`goaldialog.cpp`, a faithful
read; seen 2026-09-26 at the B.3.6 close). Drive Goals from the pause menu of a scenario game.

`BANISHED_AUTOMENU_CLICKS=<tok>,<tok>,...` then delivers follow-up clicks, one settle
interval apart:

| Token | Target |
|---|---|
| `<id>` | the last-opened ModDialog |
| `d<id>` | ModDetailsDialog |
| `m<id>` | the MainMenuDialog again |

## Pause menu (in game)
`BANISHED_PAUSEMENU=600 BANISHED_PAUSEMENU_QUIT=120 BANISHED_PAUSEMENU_BUTTON=7
BANISHED_PAUSEMENU_CLICKS=...` — same tokens plus `w` (wait) and `p` (dump the
mod/package state). The feed survives the world reload that a mods Ok triggers.

## Modal dialogs
A modal `UI::ShowDialog` pumps its own loop that no driver can reach (and reads as HUNG to
the watchdog on an unattended run). `BANISHED_MODAL_ANSWER=<n>` answers it with `n`.
With `BANISHED_AUTOLOAD`, an answer other than 2 on the load path (confirmLoad, a mods or
`ext:` notice) declines the load; the driver then clicks the menu's Quit and the run exits 0.

## Example: the Mods menu
```
BANISHED_AUTOMENU_BUTTON=7 BANISHED_AUTOMENU_CLICKS=1001,5001,d0,4001,101,m6 BANISHED_MODAL_ANSWER=1
```
