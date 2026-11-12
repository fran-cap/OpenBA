# ROADUPGRADE hand-in (2026-11-11)

Branch `ext/road-upgrade`, on `main` a159fde9. The task (the user's): upgrade dirt roads to stone without
tracing them again, as a new placement tool with its own button in the roads menu. **Result:
`ext.road_upgrade`, class A, on by default (the user's ruling).**

## 1. What retail does
A road is laid by `RoadTool::CalcPath` (0x140104d30): the road A* gives the run's cells and each goes through
`Map::PlaceRoadCell` (0x14009c960), which posts a `BuildRoad` work, records the cell in the pending-road set
and stamps the cell's road type with the queued mark. PlaceRoadCell's first test, `roadType < cell->roadType`,
is the upgrade rule: stone (0) goes over dirt (1) and over bare ground (0xFF), dirt never over stone. So a
dirt road is already upgraded by drawing a stone road over it, one traced run at a time.

## 2. What the extension does
A drag tool, `RoadUpgradeTool` (a `DragTool`, `src/ext/road_upgrade/road_upgrade.cpp`): the rectangle between
the press and the release, and in it every cell whose road is of a lower tier than the roads group's best
road gets that road's order through `Map::PlaceRoadCell`, with the decal material recorded and the road mesh
rebuilt as CalcPath does. Dirt roads still queued are replaced by the stone order (PlaceRoadCell's own
handling of a pending cell). Cells the road tool would refuse (a blocker, a type outside its RoadRoute set)
are left. The builders, the stone and a cancel with the Remove tools are the game's.

* The tool's description is the stone road's template, so the cost bar (DragTool::OnKey) shows that road's
  icon and per-cell text, with the number of roads the rectangle upgrades as its figure.
* Preview: the roads it would upgrade as green road tiles, the rest of the rectangle as faint tiles.
* Nothing to upgrade in the rectangle: the error sound, nothing placed. A tutorial step that restricts the
  player refuses the tool.
* Which road is "stone" is read, not named: of the group's items whose template carries a
  `CreateRoadDescription`, the first with the lowest `_index`; a group with fewer than two road types gets no
  button (`PickTarget`, `road_upgrade_math.h`).

Class A: the orders are ones a player places with the stone road tool, nothing is kept or saved, and the
toggle is live (the button appears the next time the roads group opens).

## 3. The button (a ruling)
DESIGN.md's Surfaces rule gives extensions one toolbar button, the extension window's. The user asked for
this tool's button in the roads menu (2026-11-11): a placement tool sits beside the tools it works with. The
toolbar host (`src/ext/toolbar`) therefore adds at most one button per group: the window's in Reports, this
tool's after the stone road. The arms in `ToolbarDialog` are the same null-item arms the window's button
uses, now asking the host: `InsertAt`, `ButtonTemplate` (a tool button is a clone of template 2, the group's
own tool buttons), `Style`, `Click`, `SyncButton` (the button is released when the tool is no longer the one
in use, as the toolbar releases its own).

The button carries the stone road's template as its user data, as the group's own tool buttons carry
theirs, so its tooltip (`BuildRequirementUI::OnCreated`) shows that road's cost lines; one more arm there,
`Ext::Toolbar::ToolTipOpened`, puts "Upgrade Roads" in the title and hides the hotkey text (the button has no
hotkey: taking one from the group's pool would move the keys of the buttons after it). Without the template
the tooltip read "???" and placeholder cost figures.

The icon (`Toolbar_road_upgrade.png`, `tools/ext_sprites.py`) is our own drawing: a dirt tile, an arrow, a
stone tile. The game's road icons are not copied or composed into it (AGENTS.md section 1).

| file | role |
|---|---|
| `src/ext/road_upgrade/road_upgrade_math.h` | the rectangle, the per-cell test, the target pick (pure, unit tested) |
| `src/ext/road_upgrade/road_upgrade.{h,cpp}` | the tool, its start, the button's click and release |
| `src/ext/toolbar/toolbar_window.cpp` | the host's per-group button |
| `src/game/Dialog/toolbardialog.cpp` | the EXT SEAM arms |
| `tests/unit/ext_road_upgrade_test.cpp` | the row and the rules |

## 4. Verified
`tools/build.sh unit`: 2352 tests, 0 failures. `tools/build.sh run` (faithful profile, fixes off): a new game
runs, no crash log, no button (the off path). The tool in a game with the row on: the user's hand test.
