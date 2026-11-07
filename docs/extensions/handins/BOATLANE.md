# BOATLANE -- `ext.boat_lane`: the trade boats sail a smooth lane

`ext.boat_lane`, display "Boat lane", class B, version 1, **OFF by default** (the B / C rule; the user's
ruling 2026-11-06, extension defaults to be revisited next release). Locked for the game, recorded in
the save as the pseudo-mod `ext:boat_lane`, no per-town state.

## 1. What the player sees

A trade boat sails to the trading post down the middle of the river or lake and back out, along a
smooth curve, instead of retail's route, which hugs whichever bank is cheapest and turns in grid steps.
With `ext.winter_ice` on, the ice's open channel is exactly this lane.

## 2. Retail's route and the seam

`BoatComponent::SetDestination` (0x1400456e0, `boatcomponent_route.cpp`) builds the route:

1. the world's polyline 0 (`WorldGeometry::_contourOutlines` record 0) up to the map;
2. two A* legs through the water (`BoatRoute_FindWaterPath` 0x1400a6a30: passable types 7 / 8 / 9,
   mask 0x380, step costs 5 / 1 / 5, diagonal on, blocked cells not respected): the first polyline
   point inside the map -> the trading post's `boat` attach point -> the last polyline point inside;
   both legs or neither (retail's gate at 0x140045e60); each cell's centre snapped to the terrain;
3. the polyline out from the point after the first one back outside.

**THE SEAM.** In step 2, after retail's gate: when both legs exist and `Ext::Ext().boat_lane`,
`Ext::BoatLane::RoutePoints` gives the lane's points (world x, y) in place of the legs' cells, snapped
to the terrain the same way. Retail's gate, steps 1 and 3, the dock heading and
`fix.boat_reach_notice` are untouched. When the lane cannot be built, retail's cells are used.

## 3. The lane (`src/ext/boat_lane/boat_lane.h`, pure)

- The legs exist under the game's own rules: `BoatWaterPath` follows `GridPathSearch::FindPath`
  (0x14001d040): eight neighbours, 10 straight / 14 diagonal times the type's cost, a neighbour (the
  goal included) taken only when its type is passable (`GridPathSearch::Neighbour` 0x1400a5ec0).
- The lane's legs add a pull towards the middle of the water: `BankDistance` (a 10 / 14 chamfer to the
  nearest impassable cell), and a step into a cell `d` cells from land costs `kLanePull * 25 / d^2`
  more. The square keeps the lane off the inside of a wide bend (the user's report: with `4 / d` the
  shorter way round won and the lane hugged the inner bank).
- The way out keeps to the way in's channel: a step onto a cell of the in leg (or a neighbour of one)
  costs `kShareCost` (3) tenths. Where the boats come and go by the same edge the legs are one channel;
  they fork only where their map edges part (the user's report: two lanes side by side across a lake).
- Each leg is a Catmull-Rom spline through every `kLaneSmoothing` (4) cells of its path, six segments
  per span; the ends stay on the entry, dock and exit cells' centres.
- Deterministic: integer costs, a fixed neighbour order, ties broken by cell index. The shape is fixed
  in code (the user's tuned set); the debug overlay's pull and smoothing sliders move the ice's look
  only while this row is off.

A boat follows dense points safely: `BoatCtrl_NextRoutePoint` (0x1400a70c0) skips to the next point at
least the steering spacing away. The route is saved as plain points (`BoatComponent::Persist`), so the
format does not change; a boat already under way keeps the route it was given.

## 4. Why class B

The lane is longer or shorter than retail's legs, so a boat's travel time, and with it when trades
happen, differ from retail's: a sim effect (DESIGN.md: "pathfinding changes" is the class B example).
The game's data never changes shape, so it is not class C.

## 5. Files

| file | what |
|---|---|
| `src/ext/boat_lane/boat_lane.h` | the pure lane: `BoatWaterPath`, `BankDistance`, `SmoothLine`, `Legs` (moved from ext.winter_ice) |
| `src/ext/boat_lane/boat_lane_route.{h,cpp}` | `RoutePoints`: the map's types in, world points out |
| `src/game/Component/boatcomponent_route.cpp` | the seam in `SetDestination` |
| `src/ext/winter_ice/winter_ice.cpp` | the ice channel uses `Legs`; with this row on, the lane exactly |
| `src/ext/registry.{h,cpp}`, `tools/ext_icons/boatlane.py`, `Entry_boat_lane.png`, `ExtensionsSprites.rsc`, `ext_sprites_data.h` | the row and its icon |
| `tests/unit/ext_boat_lane_test.cpp` | the lane's rules, ends, determinism, bank distance, the row |
| `docs/extensions/LIST.MD` | the list rows (Boat lane, and Winter ice, missed in PR #266) |

## 6. Verified

- Unit: every platform's tests, plus four lane tests.
- In game (Windows, DX11 debug), seed 12345, the trade harness (`BANISHED_BUILD_DEMO_TRADINGPOST=1`,
  `BANISHED_TRADE_WATCH=300`, speed 10) with the row on: the merchant boat's route was the lane (674 lane
  points in place of the game's 202 + 230 cells; 1064 route points in all), and the boat docked
  (`[TradeWatch] PASS`, frame 3300). The ice's mask reported its channel by the water legs.
- Not run: Linux / macOS / Android in game; a save written with the row on loaded in retail (the
  pseudo-mod path is the shared, tested mechanism).
