# STABLEORDER: entities act in a fixed order, not by where they sit in memory (`fix.stable_order`, 2026-11-18)

> Renamed from `fix.update_order` (hand-in UPDATEORDER.md) before it shipped, by the owner's ruling: the
> row covers more than the update order. It never shipped, so no retired id.

> **Built: `fix.stable_order` (tier 1, default OFF by the ruling of 2026-11-19, section 5), sim-visible.**
> The entity update walk visits the update tree's entities by EntityList id instead of by heap address,
> and the address-decided choices the digests found on the way (who follows whom on a path, which
> nearby building a citizen visits in its free time, the order a torn-down task cancels its workers)
> use the id too. The sound (the audio scene's sample picks, the music track pick) draws from a Random
> of its own instead of the world's, so the wall clock of the audio device no longer moves the sim.
> Nothing new is saved. **Result (section 6): with the row and `fix.alloc_tiebreak` on and inline
> paths, every 9000-frame run of the stress town gives the same digest at every frame.**
>
> **The cost, in one sentence: with the row on, the sim's random sequence differs from retail's even
> on the same machine and the same address layout, because the audio no longer consumes world draws.**

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
| The Work leaves' `Cleanup` (vtable slot 8), twelve classes (table below) | cancel the workers one by one, each time the citizen on the set's LEFTMOST node (the lowest key in pointer order), until the set is empty | the same loop and the same cancel; the node picked is the one whose key has the lowest entity id |
| `GameUpdateLoop::Update` step 12, `Audio::Scene::Update` (`gameupdateloop.cpp`) | the scene gets the WORLD Random (0x1400966aa `MOV R9,[RDI+0xd8]`, the call at 0x1400966bb) and draws 2 per sample it starts (variant, pitch) | the presentation Random (`Ext::StableOrder::PresentationRandom`) |
| `MusicPlayer::Update` (0x1400a40e0, `unreg_lane5_musicplayer.cpp`) | the next track `Rand(n)` from the WORLD Random when the current track stops | the presentation Random |

**The sound's draws (a clock, not an address).** A sample starts when its window passes, but a channel
is recycled, and an ambient effect restarts, only when the device reports the sample ENDED
(`Scene::OnSoundEnded`, the audio thread, real playback time); the music track changes when
`IsEffectPlaying` turns false, also real time. So the number of world draws the sound makes between two
fixed simulation steps depends on the machine's timing, and the next simulation draw goes to another
value. The traced pair that showed it: at the first tick of the split frame one run's world Random was
4 draws ahead (2 samples), with no draw through `System::Random`'s methods in between (the audio inlines
its own `RngDraw` over the same state). The presentation Random: a function-local static
`System::Random` seeded once per process with a constant (`0x5eed0001`), in
`src/ext/stable_order/presentation_random.h`; never serialized, never reset per world or per load, and
read by nothing but those two consumers (variant, pitch, track: what is heard, nothing the sim reads).
Row off: both consumers get `m_random` exactly as before.

Each seam reads `Ext::Fix().stable_order` once per call (the walk: once per `EntityList::Update`), and
the other arm is retail's code as it was.

**The Cleanup picks.** Retail's loop is "while the set is not empty: take its first key, clear that
citizen's action stack" (the clear runs the actions' cancels, and the leaf's `OnCancel` removes the
key). "First" is the minimum of the set's order, and the set's order is the key's address. The row
keeps the loop, the cancel and the set; it only changes which order "first" is the minimum of: the key's
entity id (`ComponentEntity::_entityId`; for a citizen key, its entity's). So it is the same pick under
a different order, not a different rule: retail takes min-by-address, the row takes min-by-id, and both
visit every worker exactly once. Each site computes retail's leftmost node as before and, with the row
on, replaces the pick by `Ext::StableOrder::LowestIdKey` / `MinNodeById` (a walk of the set, a handful of
workers, only when a task is torn down: `WorkScheduler::TaskCleanup` and `GatherRawMaterials::Validate`).

| Class | Retail Cleanup | Set (key) |
|---|---|---|
| `BuildStructure` | 0x1400e57a0 | `_citizens` (`CitizenComponent*`) |
| `ClearArea` | 0x1400e6650 | `_entities` (the `ComponentEntity*` being cleared; value: the citizen) -- its second loop, over `_cells` (int key), is left |
| `ClearStorage` | 0x1400e72a0 | `_citizens` |
| `FireWork` | 0x1400e99a0 | `_citizens` |
| `ForesterWork` | 0x1400eb8a0 | `_citizens` |
| `GatherRawMaterials` | 0x1400ec480 | `_citizens` |
| `HerbalistWork` | 0x1400eeae0 | `_citizens` |
| `HuntWork` | 0x1400ef530 | `_citizens` |
| `MarketWork` | 0x1400f1ca0 | `_claims` (keyed by the citizen) |
| `PastureWork` | 0x1400f2b90 | `_citizens` |
| `TendArea` | 0x1400f5830 | `_citizens` |
| `TradeWork` | 0x1400f6be0 | `assignments` (keyed by the citizen) |

The leaves whose Cleanup walks an int-keyed set (`HarvestArea`, `HarvestOrchard`, `PlantArea`,
`PlantOrchard`: map cells) or an array (`ConsumeProduce`, `FishingWork`) or holds one citizen
(`BuildRoad`, `LocationWork`, `Pickup`) are already address-free. The leaves' `Validate` walks (slot 9)
over the same sets were read and are order-neutral: each tests one citizen with the pure
`Work::ValidateTask` (0x1400f7100) and the invalid ones are removed by key, so the set that remains is
the same whatever the walk order; `BuildStructure::Validate`'s slot list is a membership test. The sums
over `_claims` / `assignments` in `MarketWork::Execute` / `TradeWork` are commutative.

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
| the Work leaves' pointer-keyed sets (`_citizens` x9, `ClearArea::_entities`, `MarketWork::_claims`, `TradeWork::assignments`) | Cleanup cancels in pointer order; Validate and the Execute sums walk them too | **covered**: the Cleanup pick by id (section 2), container and save unchanged; Validate / sums order-neutral. Found by reading, NOT proven by the digests: a 9000-frame pair with the pick forced back to retail's leftmost (experiment build) also repeated, with the same final digest (section 6) |
| `PlayerToolManager` +0x60 entity tree, `TownHallUI` / `TradeUI` / `EntityUI` trees | UI state | presentation only |
| `AmbientEnvironmentComponent` memo (`unordered_map` by pointer) | a lookup cache, never iterated for the sim | order-neutral |
| `Object::InternalFactory::_random` (time-seeded) | handle generations | not the world Random; never reaches the sim |

**What the earlier "extra disease roll" was.** The previous block's open split (an extra
`UpdateDisease` roll at the start of an Update, the RNG identical before it) was not a citizen getting
an extra update: the traced RNG lines only covered `System::Random`'s methods, and the audio scene
draws the same state through its own inlined `RngDraw` between two GameUpdateLoop steps. The disease
roll was the first sim draw after it. With the sound on its own Random, the splits are gone (section 6).

### The world Random on a presentation or wall-clock trigger (sweep of every `m_random` reader)

| Reader | Trigger | Verdict |
|---|---|---|
| `Audio::Scene::Update` (step 12) | samples starting, channels recycled on the device's real playback end | **covered** (presentation Random); digests proved it |
| `MusicPlayer::Update` (0x1400a40e0) | the track stops playing (real time) | **covered** (presentation Random); same shape, not separately proven (one draw per track change) |
| `AnimAudioEvent::Construct` (0x140087e40) | an animation event (`Animation::ApplyEvents`), a sound's play chance | inside the sim step (AnimationManager step 3b and the model ticks), fixed time per step; `applyEvents` is its own argument, not the root-only / culled flag; deterministic, left |
| `ModelComponent::Startup` random mesh group, `PlacementTool` mesh variant, `brush_tool` / `cancel_refund` / `recipe_skip` | entity creation inside a tick, or a player action | input- or sim-driven, not a clock; left (a player opening a placement tool draws once from the world Random: an input, not a clock) |
| weather particles (`weather_particles.cpp`) | per frame | its own Random (retail's local `Random`), not the world's |
| particle systems (`Graphics::Particle::_rng`, wave27/28) | per frame | their own static Random |
| `weather_sim.cpp`, fire, tornado, disease, the Work leaves, citizen AI | inside the sim step | the sim itself |
| `newgamedialog` / `mapparamsdialog` / `debugui` | before the world / debug UI | not in a running town |

## 4. How the walk is built, and the perf machinery

The update tree keeps retail's pointer key and AVL code: changing its comparator would change the
insert / remove / search of `PreUpdate`, `PostUpdateStaging` and `RemoveEntity` (faithful bodies), and
the key would change under a live node whenever `RemoveEntity` re-stamps an id. Instead:

- **TREE_SNAPSHOT** (the flat in-order copy, rebuilt when the tree changes): unchanged, still in pointer
  order, still rebuilt at the same moments.
- **The row's permutation** `s_walkOrder`: the snapshot's indices sorted by id, built by
  `Ext::StableOrder::BuildIdOrder` (a counting placement over the dense ids, O(n + master count), no
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

The seams added in this block cost nothing on a tick: the Cleanup picks run only when a task is torn
down (`WorkScheduler::TaskCleanup`, `GatherRawMaterials::Validate` finding an invalid worker), each pick
a walk of the set (a handful of workers; k picks for k workers, so k * k / 2 node visits, no sort, a
small `std::vector` stack); the sound seam is one row test per GameUpdateLoop step (per frame, not per
tick) and one per track change.

## 5. Class, default, save

- Tier 1 fix, no class (like `fix.alloc_tiebreak`): nothing serialized, so no class B / C record; the
  save format is retail's and a save made either way loads in retail and in either mode.
- Default OFF (ruled 2026-11-19; this block recommended ON). The reasons: with the row on the town's
  random sequence is no longer retail's even on one machine (sound stops drawing from the world Random),
  and a player's town still does not repeat, because path results arrive on the worker's clock (the async
  pairs of section 6 part at frame 10). So a player would pay the difference from retail and see nothing
  for it. The stress harness turns the row on, with inline paths, for the repeatable benchmark. Revisit
  the default when the path worker is made deterministic (the lockstep design at the end of section 6).
- Live toggle: the order switches at the next walk; both orders are valid towns.

## 6. Determinism proof

### 6.1 The proof (2026-11-19, tip `82e377788`, perf exe md5 `d97a5fed`)

`tools/stress_run.sh --speed 50 --frames 9000 --hash 10 --no-build --baseline none`, the default seed,
the functional dir (`C:/GOG Games/Banished-B`), every `fix.*` / `ext.*` line pinned to 0 and then
`STRESS_EXT=none STRESS_FIXES=stable_order,alloc_tiebreak` (per-entry profile, every extension off),
unless stated. Every run's address order differed from its partner's (`frame=0 ... order=`). A digest
every 10 frames: 906 lines per run, through frame 9060.

| Pair | Profile | Paths | Frames identical | First differing digest |
|---|---|---|---|---|
| p1 | stable_order + alloc_tiebreak | inline | all 906 (to 9060) | none; final `39a68e1ca0b215d9` |
| p2 | stable_order + alloc_tiebreak | inline | all 906 (to 9060) | none; final `39a68e1ca0b215d9` |
| p3 | stable_order + alloc_tiebreak | inline | all 906 (to 9060) | none; final `39a68e1ca0b215d9` |
| d1 | registry DEFAULTS (every fix/ext line removed, `STRESS_PROFILE=defaults`: `path_workers` and the other default-on rows on) | inline | all 906 (to 9060) | none; final `b3eaee077f3a3933` |
| a1 | stable_order + alloc_tiebreak | async (3000 frames) | frame 0 only | frame 10: rng, pf and cit |
| a2 | stable_order + alloc_tiebreak | async (3000 frames) | frame 0 only | frame 10: rng, pf and cit |
| x1 (experiment, uncommitted build: the twelve Cleanup picks forced back to retail's leftmost) | stable_order + alloc_tiebreak | inline | all 906 (to 9060) | none; final `39a68e1ca0b215d9` |

**Verdict: yes, a full 9000-frame run repeats** (six runs of three pairs give one digest sequence; the
defaults pair repeats too). The async pairs part at the first digest line: the path worker's race
alone moves the town at once, as before.

### 6.2 The bisect that found the sound

With the Cleanup picks and without the sound seam, a 3000-frame pair split at frame 150 and other pairs
at 184 / 585 (a moving point, the mark of a clock). A per-draw trace (investigation build, never
committed: every `System::Random` method logged its value and caller inside a tick window, the bucket
visits and the tick boundaries too) showed the two runs equal up to the end of a GameUpdateLoop step
and one run's world Random 4 draws ahead at the next step's first draw, with no traced draw between:
the audio scene's inlined draws (section 2). With the sound seam the next 3000-frame pair repeated to
the end, and the 9000-frame pairs above followed.

### 6.3 Before this block (tip `1c4361d08`, 3000 frames, for the record)

| Pair | Paths | Identical through | First differing digest | What split first |
|---|---|---|---|---|
| before the seams (walk only) | inline | frame 0 | frame 10 (frame 1 at `--hash 1`) | citizens (MoveTo follow, leisure pick) |
| p1 | inline | 840 | 850 | the RNG (an extra disease roll) |
| p2 | inline | 840 | 850 (the same two digests as p1) | the RNG |
| p3 | inline | 410 | 420 | the citizens |
| p1a vs p2a / p1b vs p2b | inline | 940 / 880 | 950 / 890 | the RNG |
| async | async | frame 0 | frame 10 | everything: the path worker's race alone moves the town at once |
| registry defaults (every default-on fix and extension, `path_workers` among them) | inline | 880 | 890 | everything by 890 |

Before the row the towns differed from Update 1 (agent repeat's finding). With the walk and the two
first seams the first 400 to 900 frames repeated; the rest was the sound (6.2).

**Async paths (described, not built).** The worker answers on wall-clock time and `MoveTo::Execute`
(0x1400c8570) polls the ready byte every tick, so the tick a path lands on is a race. A lockstep design:
stamp each request with the sim tick it was made (the pathfinder token already orders them), and make a
result visible to the sim only at a fixed tick after the request (request tick + N, N about the
worker's typical latency in ticks); if the result is not ready at that tick, the main thread waits for
the worker (or runs that one search inline) instead of moving on. The worker keeps its parallelism;
only the hand-over tick becomes a function of the sim. Results must also be delivered in token order at
a tick, not in completion order. Under `ext.path_workers` the same applies per worker queue.

## 7. Open choices

- **The id.** Ruled: renamed `update_order` -> `stable_order` before it ships (done).
- **The sound seam inside this row, or its own fix row?** It is a clock, not an address, but it is
  part of the same promise ("one seed, one town") and the proof needs it. Recommendation: keep it in
  `fix.stable_order` (one switch for a repeatable town; a separate row would be a second switch every
  benchmark and mod must remember). Its cost is the sentence at the top: with the row on, the sim's
  random sequence is no longer retail's even on one machine.
- **The twelve Cleanup picks: keep, or drop as unproven?** The digests do not need them in the stress
  town (pair x1). They are address-ordered by construction and act on the world (cancels), and a town
  where work is torn down with several workers on it reaches them (demolitions, a destroyed field, a
  fire put out). Recommendation: keep (a set walk of a few workers at teardown only, nothing on a tick).
- **Default ON**: recommended, on section 6.1 (three faithful pairs and the defaults pair repeat for the
  benchmark's full length).

## 8. Tests and gates

- `tests/unit/ext_stable_order_test.cpp`: the visit is the id order at two address layouts; a snapshot
  with id gaps; ids outside the master range or duplicated still tick (last, in pointer order); scratch
  reuse; the follow test by id; the n-th by id of a pointer-keyed set at two layouts; the ids of one
  add / remove sequence (with RemoveEntity's swap) are the same at two address layouts; the Cleanup pick
  (`LowestIdKey`) cancels three workers in id order at two address layouts.
- `tests/unit/ext_registry_test.cpp` `ext_registry_table_shape`: the row is a fix, no class, default ON.
- Row icon `Entry_stable_order.png` (`tools/ext_icons/fixes.py` `draw_stable_order`, baked).
- The gate and the resave gate run `/profile:faithful`, which forces the row off: retail's order.
