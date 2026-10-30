# STILLPATH -- the exact "standing still" fast path of the character move tick (E1)

Block STILLPATH, 2026-10-12. OFFSCREEN design E1 (§4.3), built as faithful perf work in the
style of WALKCACHE: no registry entry, no settings key, no `BANISHED_*` flag, no `Opt()` field,
no `getenv`. A `static constexpr bool StillPath::kEnabled` is the kill switch. **The game was
not run by this block**; the coordinator runs the COMPPROF=2 row and the COMPPROF=4 shadow.

**Short version.** The fast path does not skip the executor, and it does not compare against a
signature recorded by the previous tick. It is an **early exit from the move tick's tail**, taken
at the point where the tail would start writing, when every value the tail is about to write
already has the same bits. The decision reads the live state at that moment, after the executor
has run in full, so it needs no writer enumeration and no invalidation. The only remembered
state is a **memo of the tail's own trig** (yaw bits -> sin / cos bits). It is a pure-function
memo: every entry is true whoever wrote it, so it can only cause a decline, never a wrong pass.
This choice and its cost are argued in §2. The predicted gain is ~1-2% of EntityList (§6),
well below the 9.2% ceiling.

## 1. The fixed-point proof

`CharacterComponent::Tick` (slot 8) = `CharacterComponent_MoveTick_1094c0`
(`src/game/Component/citizenai_executor.cpp`), then the entity's ModelComponent tick, which is
untouched. Below, *intermediate iteration* means `MeshInstance::_rootOnlyUpdates` is set
(`gameupdateloop.cpp:778`: `rootOnly = iteration < speedMultiplier - 1`), and *last* means it
is clear.

### 1.1 What runs before the decision (unchanged, always)

| step | code | why it stays |
|---|---|---|
| executor | `if (current >= 0) actionStack.Execute(time)` | Runs in full on every tick. Precheck, main, a timed node's flip (`PerformWork` `elapsed >= required`, `PlayAnimation` `f38 < f30`, `AnimateNaturalResource`'s `IsAnimating`), the pop, the next entry run in the same tick, `Clear()` on a 2, the flush on an emptied stack: all retail. The fast path never pops and never "hands over" mid-executor, because it starts after the executor returns. A flip that pushes a MoveTo, changes the animation or sets `+0x38` / `+0x3C` is seen by the decision, which reads those values afterwards. The idle case (empty stack) does not run the executor, in retail or here. |
| gate | `if (characterIndex < 0) return` | unchanged |
| tail head | `rootOnly` byte, `model = modelComponent.Get()`, translation x y z, `fVar14` heading recurrence from `+0x3C`, `model->yaw`, desc `+0x24`, desc `+0x28`, then `RootAnimationOffset()` -> `speed = sqrt(dot) * +0x38`, then the step if `0 < speed` | unchanged. The decision uses **these very values** (`fVar14`, `speed`, `fz`), so no arithmetic is duplicated: it is the tail's own result, not a copy of it. |

### 1.2 The decision (inserted right after the step, before `Map* gameWorld`)

Taken only when `kEnabled && rootOnly != 0 && !(0.0f < speed)`. On the last iteration it is one
test of a register that is already loaded. For a character that stepped, it is one more test.

Every load and store the REST of the tail would make on that tick, and the fast path's
treatment of each:

| rest of the tail (rootOnly set, no step) | value | fast path |
|---|---|---|
| `gameWorld = _singleton->m_game` | a pointer load, not dereferenced on an intermediate iteration | skipped (a load; `_singleton` itself is dereferenced by the fast path's `Place` call, so no faulting deref is skipped) |
| the 0x0b blocked-cell test, `GetTerrainPos`, `Scalar::Max` | `rootOnly == 0` only | not run in retail either |
| `if (!b3c) {..rootOnly==0 only..} else pos.z = desc+0x40` | pos = (fx, fy, b3c ? f40 : fz), where fx / fy are the bits read (no step) | `zOut = ZOut(b3c, f40, fz)`; **condition: bits(zOut) == bits(z)**. x / y are written back unchanged by construction. |
| `fVar14 += desc+0x24` -> written to `model->yaw` | yawOut | **condition: bits(yawOut) == bits(live yaw)** |
| `ang = yawOut * pi/180 * 0.5`; `sinf(ang)`, `cosf(ang)`; rotation = (+0, +0, -sin, cos) | R(yawOut), a pure function of the yawOut bits (`MsvcLibm::Sinf/Cosf`, the ported CRT bodies, are stateless; no code in `src/` touches MXCSR) | **condition: memo[idx].valid, memo[idx].yaw == bits(yawOut), live rotation bits == (0, 0, memo.negSin, memo.cos)** |
| `model->transform = xform` (`Transform::operator=`, seven dwords), `model->yaw = yawOut` | the seven dwords and the yaw | by the three conditions above, every stored bit equals the live bit, so the stores are no-ops and are skipped |
| push: `if (rootOnly && GeomXfLastOnly()) no push; else meshInstance.SetTransform(model->transform)` | `Geometry::SetTransform` compares seven floats and, if any differs (a NaN always does), rolls previous <- current, takes the new one, sets dirty bits, `UpdateAttached` | **performed by the fast path with the same condition and the same argument** (the model transform it would have pushed has the same bits). Exact by construction, whatever state the Geometry is in (a culled last-iteration push, an attached tool). |
| `MoveTickTraceOn()` diagnostic (`BANISHED_MOVETICK_TRACE`) | prints / counts | **condition: trace off.** The fast path declines when the diagnostic is on, so its log is unchanged. |
| `pickingComponent != nullptr && rootOnly == 0` | the pick push is last-iteration only | not run in retail either (one field load skipped) |
| `m_characterGrid->Place(characterIndex)` | the grid placement and its rect memo (`charmanager.cpp:164`) | **performed by the fast path** with the same index and the same model state. Exact by construction: the memo, `Link` / `Unlink` and `Release` interplay do not need to be reasoned about. |

If all conditions hold: `++StillPath::g_fastTicks`, then the push (when it would run), `Place`,
`return`. The state after is the state the full tail leaves, bit for bit. Every call it makes,
retail makes with the same arguments in the same order (push before `Place`).

**Claim.** For every tick where the decision passes, the full tail would store exactly the
bits already stored (translation, rotation, yaw) and would make exactly the two calls the fast
path makes. *Proof:* the store values are functions of (fx, fy, fz, fVar14, desc +0x24,
desc +0x3c / +0x40) and of R(yawOut). The first group is compared directly against the live
bits. R(yawOut) is a pure function whose value for yawOut's bits is the memo entry (§1.3). The
two calls are made with identical arguments because the model transform they read is unchanged.
Nothing else in the rest of the tail writes anything on an intermediate iteration. ∎

### 1.3 The trig memo (the only remembered state)

`StillPath::MemoTable s_stillMemo` (citizenai_executor.cpp) has one 16-byte entry
{yaw bits, -sin bits, cos bits, valid} per CharacterGrid index (`self+0x40`): dense, ~32 KB for
2,000 characters. It is written in exactly one place. That place is the tail's trig line, on a
tick that reached the decision and declined: the first still tick after a stop, a turn that is
still converging, an index never memoised. It stores the yawOut that tick fed to `SinCosf` and
the two results it got.

- **No invalidation is needed.** An entry is (y, R(y)) for its own y, and the check requires
  the live yawOut to have y's bits. So a stale entry, an entry left by another character on a
  reused grid index, or a model teleported by any writer can only make the check fail.
- **Zero entries are invalid** (`valid = 0`). A zero entry is not R(+0), because cos(0) = 1.
- The memo is written only on a decline, so the decisions are the same with the shadow on or
  off: the shadow arm does not write it either.

### 1.4 The OFFSCREEN checklist, answered

- **Executor precheck / pop.** The executor runs in full before the decision (§1.1). The
  flipping tick pops and runs the next entry in retail's own code, and the tail decision sees
  the result. A timed node's `elapsed += dt` is the node's own `Execute`, unchanged.
- **ModelComponent tick.** Untouched: it runs after, as before, so the work animation plays.
- **`MoveTo::Enter` at bucket rate** (the follow flag `+0x474`, the speed `+0x38`). It runs in
  another phase. Whatever it changed is read live by the tail head, so the fast path sees it.
- **CharacterGrid memo.** `Place` is called by the fast path (§1.2), so nothing to prove.
- **`_rootOnlyUpdates`.** The decision is gated on it. On the last iteration nothing new runs:
  the terrain z, the 0x0b test and the pick push stay retail's, every frame.
- **The animation clock / root motion.** `RootAnimationOffset()` is read by the unchanged tail
  head, and the decision uses the resulting `speed`. A clip with root motion and a non-zero
  `+0x38` gives `0 < speed` and is declined. With `+0x38 == 0`, or a NaN product, the full tail
  does not step either, so the tick is still a fixed point and the fast path is exact. The rule
  does not depend on what the clip is; it uses the value the tail uses.
- **Idle (empty stack).** The executor is not run, in retail or here. The tail is the same
  function, so idle characters take the fast path under the same rule.
- **Pathwait (MoveTo with pathIndex -1) and any other node** whose main returns 1 with the
  character not stepping also qualify. The rule is about the tail's outputs, not the node class.
- **Turning.** While |heading diff| > 0.1 deg the yaw changes every tick, so the tick declines.
  On the converging tick (fVar14 = `+0x3C`) the memo is written, and the next tick passes.

## 2. Why not the signature design of OFFSCREEN §4.3

§4.3 sketched a signature recorded at the end of a full tick (stack index, top node, `_status`,
transform bits, ...), followed by "do the timed node's add and return". I did not build it, for
four reasons:

1. **A signature checked against a past tick needs every writer of every field enumerated.**
   About 15 files write a model transform or yaw (spawn, birth, immigration, herd, boat,
   tornado, destroy, tool placement, ModelComponent::Persist on load). Many more push, pop or
   clear the stack (every Work builder, `Work::Cleanup`, `AbortForEntity`, `FlagForRemoval`,
   `Deposit`). Validating instead of invalidating means reading those fields, and that is the
   same set of cache lines the tick already touches. Reading the live state at the point of use
   gives the same exactness with nothing to enumerate.
2. **Inlining the timed node's add saves almost nothing.** EXECSELF: 90% of `Execute`'s self
   time is the action-slot and node-line misses. A fast path still pays both, because it must
   read `actions[current]` to validate and write `elapsed` on the node line. The part an inline
   add could remove (prologue, epilogue, loop control, the virtual call) is ~1.1% of the tree
   phase across all characters, and less for the still ones. Inlining would also need a vtable
   identity test against `PerformWork` / `PlayAnimation`, which has no clean spelling here.
3. **Running the executor for real keeps its semantics trivially exact**: flips, pops, the
   same-tick next entry, `Clear()`, the flush.
4. **It covers more ticks.** Pathwait, idle, `AnimateNaturalResource` and "other" nodes all
   qualify when the character does not step.

## 3. Decline rules (the full tail runs)

- the last iteration of the frame (`rootOnly` clear)
- `0 < speed` (the character steps)
- yawOut bits != live yaw bits (turning, or an external yaw write the tail would snap)
- zOut bits != live z bits (`desc+0x3c` set and z != `+0x40`, e.g. after the last iteration's
  0x0b blocked-cell z = 0 on a b3c character)
- no valid memo entry for the index, memo yaw != yawOut, or live rotation != (+0, +0, memo)
  (includes a -0.0 in rotation x / y, which the tail rewrites to +0)
- `BANISHED_MOVETICK_TRACE` on

No guard was added anywhere: the fast path dereferences only what the full tail dereferences
(`model`, `characterDescription`, `_singleton`, the grid).

## 4. The shadow `StillFastPath` (BANISHED_STRESS_COMPPROF=4)

When the decision passes and `g_verifyOn` is set, the fast path does not return. It snapshots
the eight values it claims are already final (rotation x y z w, translation x y z, yaw). Then
the **full tail runs on the live object**: the trig, the stores, the push and `Place`, exactly
as in a build with `kEnabled = false`. At the tail's end it compares, and reports
`StressHarness_Verify("StillFastPath", diff == 0, diff)` with detail bit 0 translation,
1 rotation, 2 yaw.

I chose "fast decision, then full tail on the live object" over "full tail on a copy". It is
exact, because the sim under the shadow is the faithful sim and is not modelled. It covers the
push and `Place` without copying the Geometry or the grid. And it needs no copy of the fields
the tail writes. Off (`g_verifyOn == 0`), the shadow costs one load and branch of
`g_verifyOn`, only on a tick that already took the fast path (the precedent is
`TerrainSidecar::Get` in the same function). Nothing else runs, and the sim is unchanged.

Read: `[VERIFY] StillFastPath checks=N mismatches=M` in the [COMPPROF] report /
standalone_error.log. **Expected M = 0.** N is the number of fast-path ticks the run would
have taken. Run: `BANISHED_STRESS_COMPPROF=4 tools/build.sh stress` (heavy town, as ELPROF /
WALKCACHE).

## 5. The counter: the `[COMPPROF] /char stillpath` rows (BANISHED_STRESS_COMPPROF=2)

`StillPath::g_fastTicks` is incremented once per fast-path tick: one global RMW, no branch.
`CharProf_Begin` reads it before each counted CharacterComponent tick, and `CharProf_End`
charges the tick to the new `fast` / `fastTicks` of its class x visibility cell when it
changed. The two new rows are printed after `/char bound still (E1)`:

```
[COMPPROF]   /char stillpath (E1) fast ticks/frame=  N = a% of intermediate char ticks, b% of still ticks; X ms/frame Y ns/call; full-tail still intermediate M/frame Z ns/call; est. saved S ms/frame = P% of EntityList
[COMPPROF]   /char stillpath by class (fast ticks/frame, % of the class's intermediate ticks):  idle n (x%)  walk n (x%)  pathwait n (x%)  timed n (x%)  other n (x%)
```

How to read them:
- `b% of still ticks` is close to 100% x (intermediate share of still ticks) when the memo
  works. Expect ~85-90% at speed 10: the last iteration is never fast, and a stop's first
  tick fills the memo.
- `est. saved` = fast ticks x (mean of the still intermediate ticks that ran the full tail -
  mean of the fast ticks), from the same window and with the same profiler overhead. The
  full-tail population is small (stops, memo fills) and may carry more transitions, so treat
  the estimate as indicative only.
- **The authoritative number is the A/B against the OFFSCREEN baseline run**, same town and
  same settings. Compare its `/char bound still (E1)` ms/frame with this run's (the still ticks'
  total cost), and compare EntityList / treeWalk unprofiled (COMPPROF=0, CLEAN machine).
  Without the profiler, run `kEnabled = false` vs `true` (a rebuild) in interleaved pairs, as
  PREFETCH did.

## 6. Prediction

On a fast tick the work removed is: the `m_game` load, the b3c / pick / trace tests, one add,
**`SinCosf`** (one argument reduction + two double polynomials, "trig 6 ns" in OFFSCREEN's
tail breakdown), the transform build and its 8 stores. The work added is one memo line (dense,
L1/L2) and 8 integer compares. Net ~7-10 ns of the ~50-57 ns a still character tick costs.
The executor, the root-offset chase, the heading recurrence, `Place` (~5 ns) and the push
compare all stay, as do the model tick and the walk visit.

**Predicted gain: ~15% of the E1 ceiling, i.e. ~1-2% of EntityList** in the heavy spread town at
speed 10 (~0.03-0.06 ms/frame), more at speed 50 (a larger intermediate share). The 9.2%
ceiling assumed the whole still tick is removed. The exact version cannot remove the executor's
misses (§2) or the calls whose effects it must reproduce. If the measured gain is below 1%, the
kill switch goes to `false`. The code stays as the exact, shadow-checked base for a bigger
variant, if one is ever wanted (§8).

## 7. Files

| file | change |
|---|---|
| `src/game/Component/still_path.h` (new) | `kEnabled`, `g_fastTicks` (extern), `Bits`, `Memo` / `MemoTable`, `ZOut`, `IsFixedPoint`, `ShadowDiff`. Header-only, no game types. |
| `src/game/Component/citizenai_executor.cpp` | the decision after the step, the memo write after the trig (declines only), the shadow compare at the tail's end; `g_fastTicks` and `s_stillMemo` definitions. Nothing else in the tail moved. |
| `src/diag/char_prof.h` | `Cell::lastStillTicks / fast / fastTicks`, `AddChar(..., bool fast = false)`, `Window::Sum`, `EstimateStillPath`. |
| `src/diag/stress_harness.cpp` | `CharPending::fast0`, the fast flag in `CharProf_End`, the two `/char stillpath` rows. |
| `tests/unit/still_path_test.cpp` (new) | 6 tests (below). |

Unit (`tools/build.sh unit perf`): **1100 tests, 0 failures** (6 new):
`still_path_memo_table`, `still_path_decision` (bits not values: -0.0, NaN, z rule, memo
mismatch), `still_path_shadow_diff`, `still_path_converges_then_fast`,
`still_path_char_prof_estimate`, and **`still_path_two_worlds_bit_identical`**. The last one
runs a literal copy of the tail's intermediate-iteration arithmetic (the exe constants, the
heading recurrence, the speed, the b3c z, a pure trig) for 24 characters x 4,000 iterations in
two worlds, full-always vs fast path. A script applies new facings, steps starting and
stopping, `+0x38 = 0` with root motion, external yaw / position writes, yaw nudges inside the
0.1 deg epsilon, z-only writes, -0.0 and tampered rotations, and grid indices shared by two
characters. The models must be bit-identical after every tick, with the same push / Place
counts. The fast path fires on 44,640 of 96,000 ticks. Mutation checks, run once and reverted:
dropping the yaw compare or the z compare fails the two-worlds test, and dropping the ry
compare fails the decision test.

Gates: `tools/build.sh build perf` OK, `python tools/abi_audit.py` OK,
`tools/acceptance/acceptance.sh` 0 lines off target, `python tools/castmetric.py src`
**total 2** (no casts added: bit reads use `memcpy`).

## 8. Doubts

- **The gain is small** (§6). OFFSCREEN's "honest catch" applies, and harder than §4.3 hoped,
  because the exact version keeps the executor, the root-offset read and `Place`. Two exact
  extensions were not built:
  - (a) Skip `Place` by proving its memo would hit. That means exposing the rect memo and
    reading the slot line, for ~2-3 ns.
  - (b) Hoist the decision above `RootAnimationOffset()` by memoising "this instance's clip
    root offset is 0". That needs the layer handle read and the pool slot, which the model
    tick touches anyway.

  Neither looks worth its proof until the row says how much (a) the still ticks and (b) the
  trig really cost.
- **MXCSR.** The memo assumes the SSE rounding mode is fixed for the process. No code in `src/`
  changes it. A third-party DLL that did would make the memo's result differ from a recomputed
  one. The shadow's rotation bit would catch it.
- **Diagnostics.** The fast path skips the move tick's `BANISHED_MOVETICK_TRACE` arm only when
  the trace is off, so it declines when the trace is on. The forester force harness and any other
  diagnostic that reads the model state between ticks see identical state.
- **Profiled cost.** Under COMPPROF=2 every tick is bracketed by `CharProf_Begin` / `End`, so the
  rows' ns/call include the profiler. That is why §5 asks for an unprofiled A/B.
- Not run in the game by this block. First verification: the coordinator's COMPPROF=4 heavy run
  (`StillFastPath` mismatches = 0), then the resave gate / `tools/build.sh run` (the sim is
  claimed unchanged, so the resave golden must hold), then the COMPPROF=2 row.

## 9. Results (coordinator, 2026-10-12 night) -- SHIPPED ON (`kEnabled = true`)

Idle machine (RTSS / Afterburner stopped, no compiler, every run `machine load: CLEAN`), the perf exe
copied in by hand (`--no-build`), heavy = `--map large --citizens 1600 --sets 3 --spread --speed 10`,
compact = the default town; `perf/results/<stamp>_sp_*`.

**Shadow** (`BANISHED_STRESS_COMPPROF=4`, heavy, fast path on): `[VERIFY] StillFastPath checks=56,845,480
mismatches=0` at the end of the run (51.6M / 0 at the previous window). Exact.

**A/B** (`kEnabled` false / true, interleaved twice):

| town | treeWalk ms/frame off | on | sim ms/iter off | on |
|---|---|---|---|---|
| heavy | 2.227 / 2.273 | 2.247 / 2.190 | 0.31 / 0.31 | 0.31 / 0.30 |
| compact | 1.258 / 1.244 | 1.212 / 1.238 | 0.18 / 0.18 | 0.17 / 0.18 |

Mean treeWalk -1.4% heavy (one pair +0.02, one -0.08), -2.0% compact (both pairs the same sign). At the
edge of the +-0.03 ms/frame noise, never worse by more than it; the prediction of section 5 (1-2%) holds.

**The row** (`BANISHED_STRESS_COMPPROF=2`, heavy, frame 6000 window): 9,111 fast ticks per frame = 44.4% of
the intermediate character ticks and 90.0% of the still ones (idle 99.7%, timed 93.2%, pathwait 92.3%,
walk 0), 20.7 ns per fast tick; `/char bound still (E1)` 0.262 ms/frame = 6.8% of EntityList (this run;
9.2% in OFFSCREEN's). The indicative estimate (0.188 ms/frame) overstates the measured ~0.03-0.05: the fast
tick still pays the executor's slot and node-line misses, the root-offset read, `Place` and the model tick
(section 5), so the saving per tick is the ~7-10 ns of trig + transform build, not the ~29 ns the bound
assumes.

**Verdict:** kept ON as faithful perf work, like the terrain sidecar: proven exact (shadow 0, bit-identical
unit worlds), neutral-or-better in every pair, one register test on the faithful path. Do not expect it to
move the headline number; the walk money is D2 (`ext.walk_batch`, class B), whose ceiling the OFFSCREEN
rows put at 12% of EntityList.
