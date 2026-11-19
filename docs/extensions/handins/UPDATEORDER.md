# UPDATEORDER: entities act in a fixed order, not by where they sit in memory (`fix.update_order`, 2026-11-18)

> **Built: `fix.update_order` (tier 1, default ON, off under `/profile:faithful`), sim-visible.**
> The entity update walk visits the update tree's entities by EntityList id instead of by heap address,
> and the address-decided choices the digests found on the way (who follows whom on a path, which
> nearby building a citizen visits in its free time) use the id too. Nothing new is saved.

Retail entry: `docs/retail_bugs.md` §36 (the sibling of §14, `fix.alloc_tiebreak`). Found by agent
"repeat" (`perf/README.md` "Repeatable town"); the owner ruled a `fix.*` row keyed on a stable id, for a
repeatable benchmark and as groundwork for a deterministic (multiplayer) mod.

## 1. The id

`ComponentEntity::_entityId` (+0x18), retail's own field:

- **Assigned** by `EntityList::RegisterEntity` (0x1400c48a0): the master array's count before the append.
- **Moved** by `EntityList::RemoveEntity` (0x1400c38e0): the last master entry is swapped into the hole
  and re-stamped. So the ids are dense in `[0, master count)`, `masterArray[e->_entityId] == e` always
  holds, no two live entities share one, and an id is REUSED: the entity that was last takes the dead
  one's id. An entity's id therefore changes during its life.
- **Saved**: `EntityList::Serialize` (0x1400c44a0) writes the master array in order and each entity's id,
  and a load restores both. No new save field.

Why that is good enough for an order (and why reuse does not matter here): the id at any moment is a pure
function of the sequence of registrations and removals since the world was built. Two runs that made the
same adds and removes in the same order hold the same ids; the order is recomputed whenever an id moves
(below), so the walk is always "ascending id now". A key that had to name one entity across its death
(a side table outliving it) would need a monotonic serial, which is a new per-entity field in the save:
not done, not needed for an order.

**The add / remove sequence cannot depend on addresses with the row on.** Registrations happen inside
ticks (a building placed, a citizen born, a pile dropped) and in the start-town build, which is
byte-identical run to run before tick 1. Removals are pushed onto `removalList` (an array, in push
order) by the component or action that dies, again inside a tick (`action_base_cnr.cpp`,
`pickuprawmaterial.cpp`, `removeanimal.cpp`, `animalcomponent.cpp`, `boatcomponent_states.cpp`,
`absorbresourcecomponent.cpp`, `action_pf_roots_misc.cpp`), and drained in that order
(`DrainRemovals`, the save's `FlushForSerialize`). The ticks run in id order (the update tree) and in
the buckets' array order (registration order), so the push order is the sim's, given that the sim's own
choices inside a tick are not address-decided (section 3 lists those and what is left).

**Save and load.** A save carries every id; a load restores them and the walk is by id from the first
tick after it, so a loaded town ticks in the order the saving town did. One caveat, the port's and not
the row's: `FlushForSerialize` drains this frame's removals at save time (the port defers the drain one
frame, `entitylist.cpp` step 0), so the saving game's ids after a save equal what the next frame's drain
would give: the same ids, one frame early, and nothing ticks in between. Row off then on, or the
reverse, across a save: nothing changes on disk; the order switches at the next walk.

## 2. Where it changes the sim

| Site | Retail | Row on |
|---|---|---|
| `EntityList::Update` (0x1400c4090) step 3, the update tree walk (`entitylist.cpp`) | in-order walk of a `Set<ComponentEntity*>` keyed by pointer | the same entities, by ascending id |
| `MoveTo::Enter` (0x1400c8970, `moveto.cpp`) | follow speed only behind a character at a HIGHER ADDRESS (`CMP [found], mover; JBE` at 0x1400c8a17) | behind a character with a higher id |
| `CitizenAIComponent::State_Leisure` (0x14010a560, `citizenai_leisure.cpp`) | `Rand(count)`-th key of a scratch `Set<ComponentEntity*>` of nearby buildings, in pointer order | the `Rand(count)`-th by id (the same draw) |

Each seam reads `Ext::Fix().update_order` once per call (the walk: once per `EntityList::Update`), and
the other arm is retail's code as it was.

**Post-update tree (+0x78): left in pointer order, on purpose.** Its only visitor is
`GrowthComponent::PostUpdate` (0x14011fea0): each entity eases its own model's displayed scale toward its
own growth. No RNG, no read of another entity, and the flag it clears goes through the staging array
into a set (membership only). The order cannot reach the sim or the picture.

## 3. Every address-ordered decision found, and its verdict

Swept: pointer `<` / `>` on entities, components and characters in `src/game` and `src/engine`, every
`Collection::Set<T*>` / `std::set` / `std::map` keyed by a pointer, every pointer-keyed hash container,
and every scratch tree whose n-th element is picked.

| Site | What | Verdict |
|---|---|---|
| `EntityList::updateTree` walk (0x1400c4090) | tick order | **covered** (this row) |
| `MoveTo::Enter` (0x1400c8970) | follow speed behind a higher address | **covered**, digests proved it (25 citizens at frame 1) |
| leisure home box (0x14010a560) | `Rand(count)`-th of a pointer-keyed set | **covered**, digests proved it (11 destinations at frame 1) |
| `PathGraph::AssignCitizens` / `ReassignWorkers` scratch trees | ties by pointer | covered by `fix.alloc_tiebreak` |
| `EntityList::postUpdateTree` walk (0x1400c43d0) | GrowthComponent's own scale ease | order-neutral (section 2), left |
| `CitizenBehavior` +0x100 / +0x160 bucket trees (`Profession*`, `ComponentDescription*` keys) | lookups by key; the walks go over the profession list's array | order-neutral, left |
| `WorkScheduler` profession bucket tree (`Profession*` key), queue trees (int key) | lookups by key; ids are ints | order-neutral, left |
| the Work leaves' `_citizens` sets (`Set<CitizenComponent*>`: BuildStructure, ClearStorage, FireWork, Forester, GatherRawMaterials, Herbalist, Hunt, Pasture, TendArea, Market) | walked in pointer order, e.g. `GatherRawMaterials::Validate` (0x1400ebb50) collects and drops the invalid ones in that order | **still open**: a live, serialized set; ordering it by id is a container change (the key moves with the id), past this row's one-comparator rule |
| `PlayerToolManager` +0x60 entity tree, `TownHallUI` / `TradeUI` / `EntityUI` trees | UI state | presentation only |
| `AmbientEnvironmentComponent` memo (`unordered_map` by pointer) | a lookup cache, never iterated for the sim | order-neutral |
| `Object::InternalFactory::_random` (time-seeded) | handle generations | not the world Random; never reaches the sim |

**Still open, measured.** With all three seams on and `--inline-paths`, two runs stay identical for 410 to
940 frames and then part at a two-way event: the frame-850 digest of six runs took one of exactly two
values in four of them (runs p1a = p2a, p1b = p2b), and those two classes split again at 890 / 950; the
third pair split at 420 on the citizens alone. A per-draw log of the world Random (investigation build,
not committed) shows the 850 and 887 splits are an extra `CitizenComponent::UpdateDisease` roll (plus,
once, an `EntityAnchorCell` draw) at the very start of an Update, the RNG identical before it: one run
gives a citizen one more update in the first tick than the other. Repeating, two-valued outcomes point
at more address-decided choices of the kind above (the Work `_citizens` walks are the first suspect),
not at a clock; it was not pinned within this block.

## 4. How the walk is built, and the perf machinery

The update tree keeps retail's pointer key and AVL code: changing its comparator would change the
insert / remove / search of `PreUpdate`, `PostUpdateStaging` and `RemoveEntity` (faithful bodies), and
the key would change under a live node whenever `RemoveEntity` re-stamps an id. Instead:

- **TREE_SNAPSHOT** (the flat in-order copy, rebuilt when the tree changes): unchanged, still in pointer
  order, still rebuilt at the same moments.
- **The row's permutation** `s_walkOrder`: the snapshot's indices sorted by id, built by
  `Ext::UpdateOrder::BuildIdOrder` (a counting placement over the dense ids, O(n + master count), no
  compare sort). It is stale when the snapshot is rebuilt or when `RemoveEntity` moves an id (one store
  in its swap arm), and the row-on walk rebuilds it then. Visit `j` ticks `snapshot[order[j]]`.
- **WALKCACHE** (`s_tickList`, one entry per snapshot index): unchanged; the visit uses the entity's
  snapshot index, so its entry is the same one. `EntityList_TickListInvalidate` still binary-searches the
  pointer-sorted snapshot.
- **PERFXTBL** side table: unchanged; `g_walkIndex` is the snapshot index of the entity being ticked, so
  the slot hint still hits.
- **The staged prefetch** `WalkPrefetchStaged`: takes the visit-to-index map as a functor; its stages run
  ahead in the VISIT order (entity `order[j + k]`), which is what the walk will touch next. Row off the
  functor is the identity and the code is the shipped one.
- **Code layout.** The row-on walk is its own function (`WalkUpdateTreeSnapshotById`, `noinline` +
  `flatten`), so `EntityList::Update`'s faithful walk stays inlined there as before: the disassembly of
  `Update` gains the row test and a call (1619 -> 1633 lines, the same 44 prefetches); the id walk has
  its own 43 prefetches and its per-entity visit inlined.

Row-on cost to look for: one `BuildIdOrder` per snapshot rebuild and per frame with a removal that
moved an id. Counted in the stress town (investigation build, 3000 frames at speed 50 = 150,000
walks): the update tree holds about 1,400 to 1,550 entities (of 27,500 in the master array: trees,
rocks and piles do not tick), the snapshot was rebuilt 1,815 times, the permutation 3,280 times (about
1.1 per frame of 50 walks), and 2,393 of 2,396 removals moved an id. One build is a pass over ~1,500
entity headers and a 27,500-slot array: tens of microseconds, about once a frame. Every other walk
pays one load per visit for `order[j]`; the timing A/B is the judge.

## 5. Class, default, save

- Tier 1 fix, no class (like `fix.alloc_tiebreak`): nothing serialized, so no class B / C record; the
  save format is retail's and a save made either way loads in retail and in either mode.
- Default ON (recommended): it is the precondition of the owner's repeatable benchmark and of any
  deterministic mod, a fix's default is ON (DESIGN.md §2), and the faithful profile still forces it off.
- Live toggle: the order switches at the next walk; both orders are valid towns.

## 6. Determinism proof

`tools/stress_run.sh --speed 50 --frames 3000 --hash 10 --no-build`, the default seed, the functional
dir (`C:/GOG Games/Banished-B`), exe `0e9b74d2` (tip `1c4361d08`), every `fix.*` / `ext.*` line pinned
to 0 and then `STRESS_EXT=none STRESS_FIXES=update_order,alloc_tiebreak` (per-entry profile, every
extension off) unless stated. Every pair's address order differed (`frame=0 ... order=`).

| Pair | Paths | Identical through | First differing digest | What split first |
|---|---|---|---|---|
| before the seams (walk only) | inline | frame 0 | frame 10 (frame 1 at `--hash 1`) | citizens (MoveTo follow, leisure pick) |
| p1 | inline | 840 | 850 | the RNG (an extra disease roll) |
| p2 | inline | 840 | 850 (the same two digests as p1) | the RNG |
| p3 | inline | 410 | 420 | the citizens |
| p1a vs p2a / p1b vs p2b | inline | 940 / 880 | 950 / 890 | the RNG |
| async | async | frame 0 | frame 10 | everything: the path worker's race alone moves the town at once |
| registry defaults (every default-on fix and extension, `path_workers` among them) | inline | 880 | 890 | everything by 890 |

Before the row the towns differed from Update 1 (agent repeat's finding). With it the first 400 to 900
frames (20,000 to 45,000 ticks) repeat, so the walk order and the two seams were the dominant sources;
the remaining splits are listed in section 3. The default-profile pair repeats as long as the faithful
one: under `--inline-paths` the `path_workers` threads do not move the town before frame 880 (whether
every `path_workers` request honours `runInline` was not traced).

**Async paths.** Making the worker deterministic would take the result's arrival tick out of the
wall clock: stamp each request with the tick it was made and have `MoveTo::Execute` accept a result
only at a fixed tick later (request tick + N, waiting for the worker if it is late), i.e. a lockstep
latency instead of a ready byte polled every tick. Out of scope here.

## 7. Open choices

- **The id `update_order`.** The row now also covers two choices that are not the update order (the
  follow test, the leisure pick); its menu text says "act in a fixed order, not by where they sit in
  memory". A rename to `address_order` (or `stable_order`) would say it better; not renamed, it needs a
  ruling, and a fix's id is a settings key a player's file may already hold once it ships.
- **The Work `_citizens` sets** (section 3) need a container change to be ordered by id; a separate
  block if the remaining splits are traced there.

## 8. Tests and gates

- `tests/unit/ext_update_order_test.cpp`: the visit is the id order at two address layouts; a snapshot
  with id gaps; ids outside the master range or duplicated still tick (last, in pointer order); scratch
  reuse; the follow test by id; the n-th by id of a pointer-keyed set at two layouts; the ids of one
  add / remove sequence (with RemoveEntity's swap) are the same at two address layouts.
- `tests/unit/ext_registry_test.cpp` `ext_registry_table_shape`: the row is a fix, no class, default ON.
- Row icon `Entry_update_order.png` (`tools/ext_icons/fixes.py` `draw_update_order`, baked).
- The gate and the resave gate run `/profile:faithful`, which forces the row off: retail's order.
