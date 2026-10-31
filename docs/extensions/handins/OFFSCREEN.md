# OFFSCREEN -- can off-screen citizens skip their walk and their work wait?

> (moved to the DESIGN.md §3 names 2026-10-20, block ext-standard: `src/ext/perfx/` (perfx.{h,cpp}, perfx_rules.h, perfx_table.h) moved to `src/ext/render_culling/` (render_culling.{h,cpp}, render_culling_rules.h, side_table.h; namespaces `PerfX` / `PerfXTbl` -> `Ext::RenderCulling` / `Ext::RenderCulling::SideTable`), ext.anim_batch's seam helpers to `src/ext/anim_batch/anim_batch.h`.)

Block OFFSCREEN, 2026-10-12. The user asked whether, like the culling, off-screen agents could
skip animation and pathfinding "and skip to their destinations or next node", e.g. a woodcutter's
take wood -> cutting animation -> finish -> place firewood, "but this should be measured".
This block analyses it, builds the measurement (a COMPPROF=2 row set, no new flag) and writes
the design. **It did not run the game**; the coordinator runs the stress measurements (§3.3).

**Short answer.** Animation is already taken (`ext.render_culling` / `ext.anim_batch`), and
pathfinding is not on the main thread (the request decides where the citizen goes; it cannot be
skipped). What is left is the per-tick **walk** (executor + `MoveTo::Execute` + the move tick's
step / heading / grid placement) and the per-tick **work wait**. Both are real money: a
character costs ~80 ns per sim iteration (char tick 57 + model 12 + walk ~11, ELPROF), ~2,000
of them x 10 iterations = ~1.6 ms of a ~2.8 ms EntityList frame. But "teleport off-screen
citizens" is the worst way to take it: class B **and camera-coupled** (the town would depend on
where the player looked), with ~20 position readers that a sleeping walker would mislead and
six materialisation hooks. Two better-shaped variants come out of the decomposition:
**E1** (an exact, faithful fast path for characters that stand still: work waits and idle)
and **D2** (a walk batch at speed N, the sibling of `ext.anim_batch`, class B but
camera-independent and invisible, since only the last iteration is ever drawn). The new
`/char` rows give each one's ceiling from one stress run.

## 1. Decomposition: what a citizen does per sim iteration

### 1.1 The per-tick machinery (every character, every iteration)

`CharacterComponent::Tick` (slot 8, `charactercomponent.cpp:262`) = `CharacterComponent_MoveTick_1094c0`
(`citizenai_executor.cpp:288`), then the entity's `ModelComponent::Tick`:

| part | what it does | read by the sim? |
|---|---|---|
| **H executor** `ActionStack::Execute` (`citizenai_executor.cpp:146`) | top entry only; every entry is ONE node (the only `_next` writer is `work_act_hunt.cpp:176`, EXECSELF §3b). Precheck (slot 4) once per entry; main (slot 5) every iteration; a 0 return pops and the loop runs the next entry **in the same tick**, so instantaneous nodes never cost a tick of their own | yes, it is the sim |
| **T1 heading** `fVar14` smoothing toward `+0x3C` -> `model->yaw` | exponential turn | `yaw` is serialized (`modelcomponent.cpp:156`); no gameplay read found |
| **T2 step** `RootAnimationOffset()` x `+0x38` along `+0x2C` | the walk is ROOT-MOTION driven: speed = the walk clip's per-tick root offset x the terrain speed MoveTo stored | yes: the position |
| **T3 terrain** blocked cell (type 0x0b) + `GetTerrainPos` z, last iteration only | | z: serialized; gameplay reads are 2-D (cells, `Vector2` distances) |
| **T4 trig** `sinf` / `cosf` (`MsvcLibm::Sinf/Cosf`) -> rotation | | serialized (`transform.Serialize`); presentation otherwise |
| **T5 pushes** `Geometry::SetTransform`, `Picking::Scene::SetTransform` | | no (render_culling already skips them off-screen) |
| **T6 grid** `CharacterGrid::Place` (`charmanager.cpp:158`, memoised for an unchanged rect) | | **yes**: `QueryNearestCharacter` (MoveTo::Enter's follow flag, hunts), `FindBestTarget` |
| **M model** `MeshInstance::Update` (root-only on intermediates; full pose on the last unless render_culling) | clock + pose | the citizen's own clock is read only through T2; the three `IsAnimating` sim reads are on resource / tornado models (PERFXEXT §1) |

Bucket rate (slot 9, throttled): `CharacterComponent::Update` -> `UpdateAnimation` -> the node's
`Enter` (slot 6). For MoveTo that is `MoveTo::Enter` (`moveto.cpp:483`): a 0.5-radius
`QueryNearestCharacter`; a same-direction character ahead sets `+0x474`, and MoveTo then walks at
the description's follow speed. **Walkers interact.**

### 1.2 The node classes

| node | class | per-tick work in retail (`Execute`) | observable |
|---|---|---|---|
| `MoveTo` (`moveto.cpp:279`) | **walker** | 2 x `WorldToCell`, terrain byte -> `+0x38` speed (road / normal / default, or the follow speed), waypoint-cell test -> `UpdateFacing` (`+0x2C`, `+0x3C`); arrival = 2-D distance < 0.05 inside the destination cell. Path index -1: poll the pathfinder result slot | position every tick (grid, ~20 reader families, §4.1), arrival tick, follow flag |
| `PerformWork` (`performwork.cpp:152`) | **timed** | `if (elapsed >= required) return BreakCheck() ? 2 : 0; elapsed += dt; return 1` | `elapsed` (serialized), the end tick |
| `PlayAnimation` (`playanimation.cpp:75`) | **timed** | `f38 += dt; return f38 < f30` | `f38` (serialized), the end tick |
| `AnimateNaturalResource` wait=1 (`animatenaturalresource.cpp:74`) | **timed** (clock wait) | the TREE model's `IsAnimating()` | the tree's clock (its own ModelComponent tick) |
| ShowTool, SendComponentMessage, CompleteTask, Consume/Create/Pickup/PickupStored/DropOff RawMaterial, RemoveNaturalResource | **instantaneous** | precheck does the work and returns 0 / 1 then base Execute 0: popped in the tick it reaches the top | storage, reservations, the work scheduler, entity creation, AI re-decision (the flush SCM) |

The sim tick is fixed: `kFixedStepSeconds = 1/60` (`engine.cpp:247`), so a timed wait's end
tick is a pure function of its fields.

### 1.3 The three jobs (execution order; one node per entry)

- **Woodcutter** (`ConsumeProduce::Execute`, `consumeproduce.cpp:250`). Fetch logs
  (`WorkAct_GatherRawMatCtor_bf790`, `work_act_path.cpp:99`): **MoveTo** to the storage, PerformWork
  0.75 s, PickupStored, ShowTool, PerformWork, **MoveTo** back, PerformWork, DropOff (+CompleteTask),
  ShowTool, PerformWork, flush SCM. Produce (`WorkAct_ConsumeProduceCtor_bef70`,
  `work_act_consume.cpp`): ShowTool, **MoveTo** to the workshop spot, ConsumeRawMaterial,
  **PerformWork** (the cutting animation, `param_12` s, uses the tool), ShowTool(-1), SCM,
  CreateRawMaterial (the firewood pile spawns at the workshop's produce cell -- it is not carried),
  CompleteTask, flush SCM. Haulers move the pile later.
- **Forester cutting a tree** (`WorkAct_PerformWorkCtor_c1eb0`, `work_base_actionctor.cpp:128-300`):
  ShowTool, **MoveTo**, **PerformWork** (chop), **AnimateNaturalResource**(anim 0, wait=1: the tree's
  fall clip), AnimateNaturalResource(anim 1, wait=0), **PerformWork** (fall duration),
  RemoveNaturalResource (the log comes from `RemoveResource`), ShowTool(-1), CompleteTask, flush SCM.
- **Hauler** (`Pickup::Execute` -> `WorkAct_PerformWorkCtor9_c0ad0`, `work_base_actionctor.cpp:349-487`):
  [ShowTool], **MoveTo** to the pile, PerformWork 0.75 s, CompleteTask, PickupRawMaterial, ShowTool,
  PerformWork, ShowTool, flush SCM; storage-to-storage is the woodcutter's fetch shape
  (`clearstorage.cpp:229`, `gatherrawmaterials.cpp:367`).

So the user's woodcutter is exactly: walks (per-tick), work waits (per-tick, trivially
predictable), and instantaneous hand-offs (no per-tick cost, all shared-state writes).
**Nothing can be skipped in the instantaneous nodes**: they cost nothing per tick and every
one of them must happen at its tick, in walk order relative to the other citizens (storage
reservations, the scheduler, pile creation).

### 1.4 How a walk ends early (what a fast-forward must react to)

MoveTo is never re-aimed in place; the stack is torn down and rebuilt from the citizen's
CURRENT position: `Clear()` from a 2 return (no path, `BreakCheck`, consume / pickup short), from
every work builder (new work), `Work::Cleanup` (work cancelled: ~12 call sites), `AbortForEntity`
(the target entity is destroyed, `destroycomponent_state6.cpp:138/196`), `FlagForRemoval` (death),
and `DropOffRawMaterial::Deposit` pushing a NEW MoveTo when the target storage filled up
(`action_pf_roots_drop.cpp:167-217`). Retail never re-checks the stored path cells: a road built
or removed mid-walk changes only the per-cell speed (the terrain byte is read every tick).

## 2. Cost model (ELPROF numbers; heavy town, 1,600 citizens, speed 10)

ELPROF ran with PerfX on (the old perf build; == render_culling + anim_batch). Per Update frame
(10 iterations): EntityList 3.02 ms profiled (2.62-2.84 unprofiled), tree walk 2.35; ~2,000
characters = 19,996 CharacterComponent ticks at 57 ns, 20,037 ModelComponent ticks at 12 ns; the
walk ~11 ns per visit.

**One character, one iteration (~80 ns):** executor ~27 ns (EXECSELF: 90% of its self time is
two cache misses, the action slot and the node line; `MoveTo::Execute` is 94% of the main calls'
inclusive samples, PlayAnimation 5%, PerformWork 1% -- **in this town the walk dominates the
executor**), tail ~30 ns (trig 6, terrain 8 averaged = ~77 on the last iteration, Place 5, self
11), model 12, walk visit 11.

| design | per affected tick removed | per affected tick kept / added | once per event | ceiling (share of EntityList) |
|---|---|---|---|---|
| **D1** teleport off-screen walkers (the question) | all ~80 ns (char + model + visit, if the entity leaves the tick tree) | a per-frame frustum test per sleeper (~2 us/frame total), the timer wheel | the arrival schedule per path, ~1-3 us (a scalar replay over <= 256 cells); materialisation on interrupt / save / view entry | f_off x s_walk x ~55% |
| **D2** walk batch at speed N (§4.2) | executor + tail on the N-1 intermediate iterations (~57 ns) | a scalar step loop, ~5-8 ns per skipped tick; the visit and the model tick stay (anim_batch has the model) | none | (N-1)/N x s_walk x ~38% |
| **E1** still fast path (§4.3) | tail + executor call (~45 ns of 57) for a character that provably recomputes the same transform | a signature compare + the timed node's `elapsed += dt` (~5-20 ns: the node line may still miss) | none | still-share x ~30% |

(f_off = off-screen share of walking ticks, s_walk = walking share of character ticks; the
"~55%" is 80 ns x 20,000 / 2.9 ms.) With a plausible f_off = 0.8 and s_walk = 0.45 in the spread
heavy town: D1 <= ~20% of EntityList, D2 <= ~15%, E1 a guess of 5-10%. **These are the numbers
the instrumentation replaces.** Under the TRUE faithful profile (extensions off) the model tick
is dearer (full pose on the last iteration, the pushes): PERFXEXT measured +0.23 ms/frame of
EntityList for all characters, so every ceiling is a little higher there; the relevant baseline
for a new extension is the profile with render_culling + anim_batch on.

## 3. The instrumentation (`[COMPPROF] /char` rows, `BANISHED_STRESS_COMPPROF=2`)

### 3.1 What was added (no new flag, no new Options field)

- `src/diag/char_prof.h` (header-only, pure; `tests/unit/char_prof_test.cpp`, 4 tests): the class
  rule, the window cells, the per-node table and the bound arithmetic.
- `src/diag/stress_harness.cpp`: `CharProf_Begin` / `CharProf_End` and `CharProfReport` (printed
  after the component rows, to stderr and `standalone_stress_compprof.txt`, reset per window).
- `src/game/Entity/entitylist.cpp` `TickOneComponent`: the existing `g_compProfOn >= 2` arm calls
  `CharProf_Begin` before its timed bracket and `CharProf_End` with the same `dt` after. **The
  faithful / unprofiled path is untouched** (the `else` arm is the same `c->Tick(time)`).
- `src/ext/perfx/perfx.{h,cpp}`: `PerfX::DiagPointVisible` -- the render_culling frustum test
  (same planes, same 6-unit margin) against a diagnostic copy of the planes that
  `SetWorldFrustum` stores only when `g_compProfOn >= 2` (one load + not-taken branch per Draw
  otherwise). It works with every extension off. Nothing in the sim reads it.

Per counted tick: the class is read once per entity visit (keyed on the entity and
`g_elProfTreeNodes`) BEFORE the first of its ticks, so the character's ModelComponent tick is
charged to the same cell whichever ticks first. After the CharacterComponent tick it compares
the model translation (moved), translation + rotation + yaw bit-for-bit (still), and the stack's
current index / top node (transition). Classes: `idle` (empty stack), `walk` (a MoveTo with
pathIndex >= 0), `pathwait` (MoveTo, pathIndex -1), `timed` (PerformWork / PlayAnimation /
AnimateNaturalResource), `other`. Animals (deer, livestock) are characters too and are counted.

### 3.2 The rows

```
[COMPPROF]   /char  CharacterComponent slot-8 ticks by visibility x class (start of tick); EntityList E ms/frame, treeWalk T, walk est. W ns/visit (incl. profiler)
[COMPPROF]   /char off walk     chars/frame=  N  char X ms/frame  Y ns/call (last-iter Z)  model M ms/frame  moved a%  still b% (last-iter c%)  trans/frame t
   ... one row per non-empty (on|off) x (idle|walk|pathwait|timed|other)
[COMPPROF]   /char bound walk     off-screen X ms/frame = P% of EntityList; if every character were off-screen X2 = P2%
   ... pathwait, timed, idle, walk+timed+idle, every class      (char + model + W per visit)
[COMPPROF]   /char bound walk intermediate (D2) every visibility X ms/frame = P% of EntityList (n ticks/frame)
[COMPPROF]   /char bound still (E1)          every visibility X ms/frame = P% of EntityList (timed+idle X2 = P2%)
[COMPPROF]   /charnode MoveTo   chars/frame= N  X ms/frame  Y ns/call  off-screen o% of its chars
   ... top 14 deciding node types by time ("(empty stack)" = idle)
```

`chars/frame` / 10 = characters per iteration at speed 10. Percentages are of the window's
EntityList (drain + preUpdate + treeWalk + buckets, profiled). The walk estimate W is the tree
phase minus every `/tree` tick over the visits; it includes the profiler's own Begin/End (ELPROF:
~0.3 ms/frame), so the D1 bound is a little generous.

### 3.3 Runs for the coordinator (idle machine)

1. Heavy spread town, faithful: `BANISHED_STRESS_COMPPROF=2 tools/build.sh stress` (the heavy
   settings as in ELPROF / PERFXEXT). Read the steady-state windows (frame >= 3600).
2. The same with `STRESS_EXT=render_culling,anim_batch` (the baseline any new extension stacks on).
3. Optional: the compact town (fewer and shorter walks: the low end of s_walk).

How to read them, in order:
- `/char bound walk ... off-screen` = **D1's ceiling** as asked (off-screen only, the camera of the
  stress harness). `if every character were off-screen` = its ceiling with a camera that sees
  nobody (the most a camera-coupled design could ever get).
- `/char bound walk intermediate (D2)` = **D2's ceiling** (every visibility, char ticks only).
- `/char bound still (E1)` = **E1's ceiling**; `still %` per class says WHICH ticks are fixed
  points (expect timed and idle near 100% on intermediate iterations; `still (last-iter)` below that
  means the terrain z or the 0x0b test moves stationary characters on the last iteration).
- `pathwait` rows: the per-tick cost of waiting for the worker (not skippable by any design here:
  the answer's arrival tick is not known in advance).
- `trans/frame`: stack changes per frame (each one is a precheck + instantaneous nodes + often a
  CitizenAI re-decision; the sim's irreducible part).
- `/charnode MoveTo off-screen %` = f_off for walkers directly.

## 4. Designs

### 4.1 D1 -- off-screen walkers fast-forward to their destination (the question): NOT recommended

What it would be: `ext.offscreen_walk`, class B (locked per game, pseudo-mod `ext:offscreen_walk`),
`src/ext/offscreen_walk/`. When a MoveTo takes its path (`TakePath`, `moveto.cpp` case 1) and the
citizen is off-screen, a scalar replay of T2 + MoveTo's speed / waypoint rule over the path's
cells (no trig, no grid, no model; follow flag assumed clear) gives the arrival tick and the
per-waypoint entry ticks. The character leaves the tick tree (its tick flag cleared through
`RefreshEntityFlags`, as `StopEmitter` does for particles) into a timer wheel; at the arrival tick
it is re-armed with position = destination, pathIndex = count, heading / clock advanced, and the
executor runs as usual (arrival, then the instantaneous nodes).

What has to be replayed or materialised (position = the schedule's interpolation at the current
tick): (1) any `Clear()` / `AbortForEntity` / push-over on a sleeper (§1.4: a new path must start
from where retail's citizen would be); (2) the save (`Persist` of ModelComponent, MoveTo,
CharacterComponent); (3) the sleeper entering the frustum margin (one test per sleeper per frame);
(4) the toggle can never go off mid-game (class B lock) but the load of a game saved with it
on must find no sleepers (materialise all before save); (5) the animation clock on wake (the
sim does not read it, root motion aside); (6) `MoveTo::Enter` at bucket rate (skip it for sleepers:
their follow flag is frozen).

What it can NOT do, plainly:
- **Keep the position right for anyone else.** ~20 reader families read a citizen's
  `transform.translation` during the sim: the CharacterGrid (follow flags of other walkers, hunts,
  `FindBestTarget`), `SpreadDisease` proximity (`citizen_updatehealth.cpp:413,639`), every Work's
  start-cell search when the AI re-decides, the drop-target search origin, the State_* handlers'
  destination cells, the herd wander box, fire ignition, the child spawn position. Without
  materialise-on-read at every one of them (unreadable, and a door in all but name), a sleeper is
  at its departure point (or its destination) for all of them for the whole walk.
- **React mid-path to what retail reacts to per tick**: the follow flag (another citizen ahead),
  a road built or removed under the remaining path (speed per cell), the blend-in of the walk clip
  after `SetAnimation` (T2 ramps), a tornado / fire / death in the middle of the walk (handled
  only by the materialisation in (1), with an interpolated, not retail, position).
- **Be camera-independent.** Which citizens sleep depends on where the player looks, so two players
  with one seed get two towns, and a stress A/B is reproducible only because the harness pins the
  camera. PERFXEXT dropped `ANIM_BATCH_CULLED` for exactly this reason.
- **Be checked by a shadow that means anything**: the only check is a ghost replay of retail's walk
  per sleeper under COMPPROF=4 (ghost arrival tick vs schedule; a histogram of the difference),
  which costs what D1 saves and still ignores the other citizens' view of the sleeper.

Ceiling: `/char bound walk off-screen`. Realistic: ~85% of it (the bucket-phase Update, the
materialisations and the frustum test stay). It is the biggest number and the worst shape:
**recommended only if the walk bound is large AND D2 (same money, every visibility, no
materialisation) fails for a reason not foreseen here.**

### 4.2 D2 -- walk batch at speed N (`ext.walk_batch`, class B): the shape to take the walk money

The observation that makes visibility irrelevant: at speed N retail only ever DRAWS the last
iteration's state (`render_culling`'s GEOMXF_LASTONLY rests on it). So batching a walker's N-1
intermediate iterations is invisible for on-screen citizens too, and the design needs no frustum.

- **Selection** (DESIGN hook rule 3): once per game, under the class B lock, the walk body is
  chosen per Update at the top (`WalkUpdateTreeSnapshot<WalkBatch>` vs the faithful
  instantiation); the faithful walk is the existing body, compiled without the test.
- **What is skipped**: for a character whose top node is a MoveTo with pathIndex >= 0, on
  iterations 0..N-2 the executor + tail are replaced by a tight step: T2's step (root offset per
  tick x `+0x38` x `+0x2C`, the same float ops), MoveTo's terrain speed and waypoint rule
  (including `UpdateFacing`'s math at a waypoint cell). No trig, no pushes, no `Place`. On the last
  iteration the real tick runs from the batched state (heading smoothing advanced N steps -- the
  recurrence is cheap; one trig; one `Place`).
- **Declined** (run per tick, exact) when the citizen is inside its destination cell or one step
  from it (arrival and the instantaneous nodes must happen at their own tick), during the
  walk-clip blend-in (`NeedsPerTickRootUpdate`, as anim_batch does), and when the follow flag
  `+0x474` is set.
- **Divergence** (why class B): other characters' grid queries and position reads during
  intermediate iterations see the walker's position as of the frame start (a lag of up to N-1
  ticks); the per-tick root offset of an instanced clip comes from the pool pose of the iteration,
  which the batched step reconstructs from the citizen's own clock (rounding differs).
  Deterministic and camera-independent: a save made with it loads with the pseudo-mod warning in
  retail, as anim_batch's does.
- **Shadow** (COMPPROF=4, `WalkBatchSim`): run retail's per-tick step on a copy for every batched
  frame and count (a) final positions that differ by more than 1e-4, (b) a waypoint or the
  destination cell reached inside a batched span (must be 0: that is what the decline rule is for),
  (c) the grid cell at an intermediate iteration differing from retail's (the class B evidence,
  like `AnimBatchSim`).
- Ceiling: `/char bound walk intermediate (D2)`, minus the step loop (~10% of it).

### 4.3 E1 -- the still fast path: faithful, and the one to build first if the rows confirm

For a character whose last full tick left translation, rotation and yaw bit-identical (the
`still` column), the move tick is a fixed point as long as its inputs do not change: the heading
recurrence is converged (|diff| <= 0.1 degrees gives `fVar14 = +0x3C` and the same yaw), the
root offset is 0 (a clip with no root motion: anim_batch already caches that per `Animation`),
the speed is 0, and on an intermediate iteration T3 is not run at all. The executor's main call
for a single timed node is `elapsed += dt` until the compare flips (`PerformWork`, `PlayAnimation`;
fixed dt), or the tree's `IsAnimating` for `AnimateNaturalResource`. So for such a character a
fast path -- compare a signature recorded by the last full tick (stack current, top node,
`_status`, transform / yaw / `+0x2C` / `+0x3C` / `+0x38` bits, the model's layer-0 clip and layer
count), then do the timed node's own add and return -- produces **the same sim state bit for
bit**: no class, no save change, no camera dependence. The last iteration of each frame (T3) and
any signature change run the full tick. Presentation is identical (the pushes would push the same
transform; render_culling skips them off-screen anyway; the model tick still runs, so the work
animation plays).

- Where: the head of the CharacterComponent tick. Being exact, it is not an extension: like the
  CharMgr cell memo (`charmanager.cpp:164`) and WALKCACHE it becomes unconditional faithful perf
  work once its shadow is 0, with a `constexpr` kill switch; the per-character signature test is
  the price (as the memo's rect compare is). Shadow (COMPPROF=4, `StillFastPath`): run the full
  tick on a copy of the fields it writes and compare bit for bit; expected 0.
- At speed 1 every iteration is the last, so E1 (like D2) buys nothing there; both pay at
  speed 5-50, where the frame budget is spent.
- Honest catch: EXECSELF found the executor's self time is the node-line miss, and the fast path
  still touches that line for `elapsed += dt` (a side counter with lazy write-back would avoid it
  but needs save / Clear materialisation -- then it is no longer trivially exact). So the real
  gain is ~half to two thirds of `/char bound still (E1)` until a prototype measures it.
- Also covers idle characters with an empty stack (the executor does not run; only the tail is
  skipped).

### 4.4 Decision rule from the rows (heavy spread town, render_culling + anim_batch on)

| row | if | then |
|---|---|---|
| `bound still (E1)` | >= 5% of EntityList | build E1 (faithful perf work, shadow `StillFastPath`); else drop it |
| `bound walk intermediate (D2)` | >= 10% | prototype D2 with the `WalkBatchSim` shadow; else the walk is not worth a class B |
| `bound walk off-screen` | only if D2 failed | D1, with the caveats of §4.1 written into its registry description |

## 5. The honest ceiling and the prediction

- The sim's irreducible part is not in these classes: the transitions (precheck + instantaneous
  nodes + `ValidateAndEnterState` / `PathDistance`, ELPROF ~8% of the tree phase plus most of the
  bucket phase), the pathfinding requests, and the other entity kinds (particles, state machines:
  ~25% of the tree phase). No design here touches them.
- Every ceiling above assumes the skipped work is replaced by nothing; the char tick is ~50%
  cache misses (EXECSELF), and every design still touches the CharacterComponent line.
- **Prediction (to be checked by the coordinator's run):** in the heavy spread town at speed 10
  walking is the largest class (~40-50% of character ticks, >90% of executor time is MoveTo);
  D1's off-screen bound lands near 15-20% of EntityList, D2's near 10-15% with no visibility
  dependence, E1's near 5-8%. So: E1 first (exact, small), D2 next (class B, the real walk money),
  D1 never.

## 6. Files

- `src/diag/char_prof.h` (new), `tests/unit/char_prof_test.cpp` (new).
- `src/diag/stress_harness.{h,cpp}`: `CharProf_Begin` / `CharProf_End`, `CharProfReport`.
- `src/game/Entity/entitylist.cpp`: the two calls inside the existing COMPPROF >= 2 arm.
- `src/ext/perfx/perfx.{h,cpp}`: `DiagPointVisible` and the diagnostic plane copy.
- No registry row, no settings key, no `BANISHED_*` flag, no `Opt()` field. The game was not run.
