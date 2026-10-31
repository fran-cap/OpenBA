# FOUNDATIONPILES: piles no storage takes are carried off a building's footprint (`fix.foundation_piles`, 2026-10-31)

> **Built: `fix.foundation_piles` (tier 1, default ON, off under `/profile:faithful`), sim-visible.**
> A building site whose footprint holds nothing but piles of logs or stone that no store has room for
> has a citizen carry each pile to the nearest free cell just outside the footprint, so the site can
> finish clearing and be built. Retail leaves the piles where they are until they rot (a game year).
> Nothing new is saved: the move is made of retail's own actions.

Retail entry: `docs/retail_bugs.md` §17. Inventory row F16. Reported by a player 2026-10-31 (a stockpile
placed over a clear-tool area, a town hall over trees, the Easy start's stockpile full); the owner asked for
the piles to be moved off the footprint, for any building. The stockpile half of that report was the port's
ClearArea return value (known-issue row 1343): a stockpile site absorbs the piles its own storage takes, as
retail does. The town hall half is retail's behavior.

## 1. Are loose piles "in storage"?

No. Evidence, all in the port of the named address:
- **Totals and capacity.** `CitizenBehavior::CountResources` (0x140089ed0, `src/game/Entity/citizenbehavior.cpp`)
  walks only the entities in the two storage lists (+0x00 / +0x10) and sums their StorageComponents
  (count x `RawMaterialDescription::_weight`); the capacity it reports is those entities' `capacity`. A pile is
  not in either list, so it counts toward neither the town's resource totals nor the storage numbers. A
  citizen's carry storage is not counted either.
- **Moving one keeps the totals.** The pick-up (`PickupRawMaterial`) moves the pile's quantity into the
  citizen's StorageComponent and removes the pile; `DropOffRawMaterial::DropOnGround` (0x1400c78c0) builds a
  new pile entity with that quantity (`PositionRawMaterial` 0x140129760, `quantity = count`) and subtracts the
  same count from the citizen (`StorageComponent::SubtractItem` 0x14012d150). Nothing is in a counted store at
  any step, so nothing is counted twice or lost. (DropOnGround drops the items unplaced if no cell within six
  rings is free; that is retail's, and the drop cell chosen here is free when chosen.)
- **The new pile is ordinary.** `RawMaterialComponent`'s OnAdded (0x1401291d0) posts its own Pickup work, so a
  store with room later collects it as any pile. Its age starts again at 0, so it rots a year after the move.
- **The existing drop path.** `DropOffRawMaterial` with no destination (AbstractImpl 0x1400c7300 ->
  `SpillAllStorage` 0x1400c7570 -> `DropOnGround`) is what a citizen does with a load that has nowhere to go
  (`Deposit` 0x1400c7620's no-storage branch, the stack-clear `CheckEntity`). The fix reuses it unchanged.

## 2. What changes, and where

One seam, `ClearArea::Execute` phase 3 (0x1400e5ee0, `src/game/Work/cleararea.cpp`), where the site offers each
pile's Pickup work to the citizen (`WorkScheduler::TryAssignWorkplace` 0x1400f8620) and retail moves on when
nobody takes it. With the row on, and only when the work's owner carries a `ClearAreaComponent` (a building
site; the clear-tool markers have none) and every occupied tile left is a pile
(`Ext::FoundationPiles::WaitsOnPilesOnly`), `Ext::FoundationPiles::Relocate` (`src/ext/foundation_piles/`):

1. returns without effect unless `HasStorageFor` (0x14008d3c0) refuses the pile (so retail's haul still owns
   every pile a store will take), its Pickup work is unclaimed, the citizen carries nothing and has room, and
   the pile's access cell is reachable;
2. picks the drop cell (`foundation_piles_rule.h`, `ChooseDropCell`): the nearest cell outside the footprint,
   searched in square rings around the pile up to the footprint's longer side + 6, smallest straight-line
   distance in a ring, ties in row-major order, that `Map::FindPlacementCell` (0x14009bd40) accepts as its own
   ring-0 cell (no occupant, no blocker, no footprint count, no road, buildable ground, the height test) and
   that the citizen can reach (`Map::ValidateCell` 0xe1f). A blocker covers every pending building's and
   zone's footprint (the clearoverlap census: the hall's 10x9 holds 80 blocked cells and 10 '.' cells with a
   footprint count, the yard's 8x8 all 64 blocked), so the cell is never inside another site, a stockpile
   zone, a clear marker or a road;
3. clears the citizen's stack and pushes, last-run first, `DropOffRawMaterial(no destination)`, a `MoveTo` to
   the drop cell (path type 4, the haul chain's carrying walk), then retail's pile-haul chain
   (`WorkAct_PerformWorkCtor9_c0ad0` 0x1400c0ad0: walk to the pile, work, `CompleteTask`, pick up); claims the
   Pickup work and stamps the citizen as `Pickup::Execute` (0x1400f2f00) does; returns 2 (dispatched).

Off, the seam is one bool read before the loop; the loop and its result are retail's.

## 3. Save, sim and perf

- Save: none new. A save made mid-move holds retail action classes on the citizen's stack (MoveTo,
  DropOffRawMaterial), which retail persists and loads.
- Sim: which cells the piles lie on, and that a blocked site goes ahead. A cleared stack mid-move spills the
  load where the citizen stands (retail's `CheckEntity`), outside any footprint.
- Class: the registry keeps fixes classless (`ext_registry_table_shape`); a sim-visible fix with retail's save
  format is the `fix.alloc_tiebreak` / `fix.terrain_resort` precedent, so no pseudo-mod or tail record is
  written and a save made with it loads in retail and in faithful mode.
- Perf: off, one bool per phase-3 pass. On, a footprint scan per phase-3 pass of a building site and, only
  when no store takes a pile, the ring search.
- Log: `[FoundationPiles] pile <p> (<qty>) at c,r -> c,r (site x,y wxh)` on stderr per move.

## 4. Tests and gates

- `tests/unit/ext_foundation_piles_test.cpp`: the drop-cell choice (edge pile to the adjacent outside cell,
  never inside the site, nearest side, fall-back past refused cells, row-major ties, map edges, -1 when
  nothing within the rings); `ext_registry_table_shape` lists the row.
- `tools/harness_route.sh clearoverlap` with `HARNESS_FIXES=foundation_piles` (the per-entry profile with only
  this fix on) against `HARNESS_FIXES=none`; the scenario's stall depends on timing (a run where food or
  firewood use frees store room first can clear without a move), see the report of 2026-10-31 in
  `docs/retail_bugs.md` §17.
