# ALLOCTIEBREAK: ties in the job and house allocators no longer follow memory addresses (`fix.alloc_tiebreak`, 2026-10-30)

> **Built: `fix.alloc_tiebreak` (tier 1, default ON, off under `/profile:faithful`), sim-visible.**
> The employment allocator and the housing re-balancer order equal candidates by a stable id (an entity's
> EntityList id, a profession's list index) instead of by heap address, so a save plays out the same way
> each time it is loaded. Nothing new is saved.

Retail entry: `docs/retail_bugs.md` §14. Inventory row F14. Found by the determinism audit (check 7); the
owner ruled a `fix.*` row keyed on a stable id, with the row off exactly faithful.

## 1. What changes, and where

| Site | Retail | With the row on |
|---|---|---|
| `PathGraph::AssignCitizens` (0x1400d43b0, `src/game/Component/citizen_assign_citizens.cpp`): the idle set | `CitizenComponent*` ascending | (entity id, pointer) ascending |
| the same: the profession-count map, Phase 5B's `begin()` | `Profession*` ascending | (profession rank, pointer) ascending; `begin()` kept |
| the same: each need's cost tree | (cost, `CitizenComponent*`) | (cost, entity id, pointer) |
| `PathGraph::ReassignWorkers` (0x1400d62a0, `src/game/Component/citizen_reassign_workers.cpp`): the empty residences | `ResidenceComponent*` ascending | (entity id, pointer) ascending |
| the same: the occupied residences | (cost descending, `ResidenceComponent*`) | (cost descending, entity id, pointer) |

One key type, `Ext::AllocTiebreak::TieKey<T>` (`src/ext/alloc_tiebreak/tie_key.h`): an `int32_t` id in front
of the pointer. Each seam reads `Ext::Fix().alloc_tiebreak` once per call and passes 0 for every id when it is
off, so the comparators reduce to retail's pointer order. The ids come from `alloc_tiebreak.{h,cpp}`
(`EntityKey`, `ProfessionOrder`). The need priority queue is untouched: it already keys on the need's index.

## 2. Why the ids are stable

- **Entity id** (`ComponentEntity::_entityId`, +0x18). `EntityList::RegisterEntity` (0x1400c48a0) sets it to
  the master array's count before the append; `RemoveEntity` (0x1400c38e0) moves the last entity into the
  freed slot and re-stamps it. So `masterArray[e->_entityId] == e` holds at all times: unique at any moment,
  and decided only by the order of registrations and removals, which is the sim's. `EntityList::Serialize`
  (0x1400c44a0) writes the master array in order with each entity's id and reads both back, so a load
  restores the same ids. No address, allocation or clock enters it. An entity not registered yet carries
  -1; two such keys stay distinct (the pointer orders them), and the allocators only see registered
  citizens and residences.
- **Profession rank**: the index in `ProfessionList::professions` (+0x20), built by `ProfessionList::Startup`
  (0x1400a90a0) from the factory's walk of the Profession externals (the resource tree is keyed by path)
  and sorted by +0x48 with retail's introsort: the same order on every load of the same data. A profession
  outside that list (a child's or student's title, a mod's hidden one) ranks after it, by its place in the
  same path-ordered factory walk.

## 3. Save, sim and perf

- Save: none. The containers are scratch, rebuilt every call; no id is written.
- Sim: which equal candidate wins changes, so a town with the row on diverges from one with it off at the
  first tie. Both are valid towns; with the row on, two loads of one save stay together.
- Perf: one int compare per tree compare; one entity read per idle citizen and per residence per call; a
  scan of the profession list (about 15 entries) per idle citizen and per assignment. Off: one bool read per
  call and the zero ids.

## 4. Tests and gates

- `tests/unit/ext_alloc_tiebreak_test.cpp`: ids 0 walk in pointer order; ids walk in id order at two address
  layouts; equal ids stay two keys; the order is strict; a map's `begin()` is the lowest rank.
- `tests/unit/ext_registry_test.cpp` `ext_registry_table_shape`: the row is a fix, no class, default ON.
- Row icon `Entry_alloc_tiebreak.png` (`tools/ext_icons/fixes.py` `draw_tiebreak`, baked by
  `tools/ext_sprites_bake.py`).
- The gates run `/profile:faithful`, which forces the row off: retail's pointer order.
