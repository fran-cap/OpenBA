# SPLASH -- `ext.quick_screens`: the presentation floors end when the work is done (hand-in)

Block SPLASH of the loading-speed session (`LOADPLAN.md` §3; the user's rulings §1 rows 1 and 3).
Branch `worktree-agent-a385019f35a164529`, base main `c41ce8449`. Static gate only: the game was NOT
run (the coordinator runs it, with the row on and off).

## 1. The row

| field | value |
|---|---|
| id / settings key | `quick_screens` / `ext.quick_screens=0\|1` |
| tier / class | Extension, **class A** (no serialized state, no sim effect; live, never in the save) |
| default | **OFF**; OFF under `/profile:faithful` (the gate) and `profile=faithful`; ON under `profile=all` |
| display name | "Quick loading screens" |
| description | "Splash and loading screens close as soon as loading is done instead of staying up for a minimum time, and loading or saving a game no longer waits out a minimum time once the file work is done. The fades still play. The game itself plays exactly the same." |
| code | `src/ext/quick_screens/quick_screens.h` (pure decision), seams in `src/game/Game/splash_loop_stubs.cpp` and `src/game/Dialog/saveloaddialog.cpp` |

The toggle is read **once per screen** (`GameLoadDialogLoopT::Startup` latches it into a file static
beside the existing fade latch) and **once per save-dialog operation** (where the dialog sets the
minimum time). Nothing reads it per frame.

## 2. The floors

### 2.1 Every splash / loading screen: `GameLoadDialogLoopT`'s hold

- **Where.** `GameLoadDialogLoopT::Update` (`splash_loop_stubs.cpp`, the `splash.fadeout` stamp): the
  fade-out starts when `!EngineLoader::IsBusy() && _elapsedTicks >= _delayTicks && worldReady` --
  retail's `LoaderLoop::IsFinished` (loader thread not running AND the hold elapsed) plus our
  world-built term. `_delayTicks` is the ctor's `Time` (`LoaderLoop +0x38`); `_elapsedTicks` advances by
  the frame delta every Update.
- **The holds** (all seven construction sites use this one class, so one seam covers them):

  | screen | site | hold |
  |---|---|---|
  | boot, `Dialog\ShiningRock.rsc` (next = the Loading splash) | `game_main.cpp` buildSplashDLT | **2.0 s** |
  | boot, `Dialog\Loading.rsc` (next = MenuDialogLoopT) | `game_main.cpp` buildSplashDLT | **0.5 s** |
  | new game | `newgamedialog.cpp` | 1.0 s |
  | save load | `saveloaddialog.cpp` (the loop chain) | 1.0 s |
  | quit to menu | `mainmenudialog.cpp` | 1.0 s |
  | reload (mods / B-C toggle) | `worldsetup_reload.cpp` | 1.0 s |
  | tutorial to menu / next scenario | `tutorialui.cpp`, `tutorial_next_scenario.cpp` | 1.0 s |

- **What ON does.** The hold term becomes
  `_elapsedTicks >= _delayTicks || (g_quickScreen && QuickScreenWorkDone(this, &_ui))`; the
  loader-idle and world-built terms stay beside it, unchanged. `QuickScreens::SplashWorkDone` is true when
  - the fade-out was already kicked for this screen (the hold ended on an earlier frame; without this the
    rising fade would re-close the hold mid-fade-out), or
  - **the menu scene's chunked build is finished** (`ChunkedSceneLoader` neither armed nor busy) **AND the
    screen's reveal fade has finished** (UI fade amount `<= 0`).
- **What work it waits for.** The loader thread (retail's term: the next loop's resources and its slot-6
  `Load`, i.e. the whole `GameUpdateLoop::Load` -- world build or `SaveRestore` -- for a game), the world
  built (`GameUpdateLoop_IsWorldBuilt`), the menu-scene build (ours, not retail's: `ChunkedSceneLoader`
  runs LoadSceneA1 + PhaseB behind the first boot splash; faithful waits for it AFTER the fade-out, at
  black, so ON moves that wait in front of the fade-out instead of skipping it), and the reveal fade (so
  both fades play: in, then out -- ruling 3). The floor never ends before any of them.
- **Fades.** Unchanged: the reveal (`SetFullscreenFade` x2 in Startup) and the 0.25 s fade-out
  (`TriggerFadeIn`, then the `alpha >= 0.99` wait) run as before.

### 2.2 The save dialog's minimum operation time

- **Where.** `SaveLoadDialog::OnOpened` (0.75 s, the save list), `DoSave` (1.5 s), `DoLoad` (1.5 s)
  call `Save::SaveLoad::SetMinOperationTime` (`_minOperationTime`, `saveload.cpp:598`); `SetOperation`
  zeroes `_elapsed` (`saveload.cpp:587`); `SaveLoad::Update` adds the frame delta (`saveload.cpp:796`) and
  dispatches the operation's step **only while the persistence worker is idle**; the last step of
  UpdateList / UpdateSave / UpdateLoad waits while `_elapsed <= _minOperationTime`
  (`saveload_update.cpp:815` load, `:1017` save, `:1657` list). See `SAVELOAD.md` §5.
- **What ON does.** The dialog passes `QuickScreens::kNoMinOperationTime` (0 ticks) instead of retail's
  seconds. The engine body is untouched.
- **What work it waits for.** The worker: the final step is only dispatched with the worker idle, i.e.
  after the file read (load, list) or write (save) finished; `_elapsed` is already `> 0` by then (Update
  adds a positive frame delta before it dispatches).
- **Not touched:** `AutoSaveDialog` (its own 1.0 s; not "the save dialog" of ruling 3), and the
  menu's own stage-C fade (`menu.stageC`, presentation, stays retail).

### 2.3 The save list's one step per frame (added after the hand-in)

- **Where.** `Save::SaveLoad::UpdateList` (`saveload_update.cpp`) takes one step per `Update`, and
  three steps per save: ask the worker to verify the file (the whole-file CRC of
  `PersistenceWorker::GetSaveInfoJob`), ask it for the header block, build the row. `Update` runs once
  per frame, so the list costs three frames per save even when the worker answers in under a
  millisecond, and scales with the frame time (worse from the pause menu of a big town).
- **What ON does.** `SaveLoadDialog::OnUpdate` calls `QuickScreens::PumpSaveList` while the list
  operation runs (`src/ext/quick_screens/list_pump.cpp`): it waits on the worker's finished event and
  calls `Save::SaveLoad::Update` with a zero delta again, until `kListPumpBudgetMs` (8 ms) of the frame
  is spent, the list is done, or a step changes nothing. The toggle is latched in `OnOpened`. The engine
  body is untouched: the same steps in the same order, so the rows, their order and the Corrupt marks
  are retail's.
- **Measured** (perf exe, main menu, 40 saves / 359 MB, file cache warm, `dialog opened` to the rows
  built): OFF 2.17 s to 2.20 s (123 list ticks), ON 0.54 s to 0.56 s. What is left is the worker's CRC
  of every save (0.6 s for these files in a stand-alone loop).
- **Cost.** While the "please wait" page is up, a frame may spend up to the budget blocked on the
  worker; a wait that times out can overrun it by the OS timer's granularity.

### 2.4 Nothing else reads the floors' clocks

- `SaveLoad::_elapsed` / `_minOperationTime`: read only by the three gates above (grep of `src/`).
- `GameLoadDialogLoopT::_elapsedTicks`: the hold, the ChunkedSceneLoader reveal cap (0.25 s, unchanged),
  and `DialogLoaderLoopT::Draw`'s shader timer (animation only). The tip timer (`_tipTimer`) is separate.
- Menu music starts in the menu loop, after the splash chain; the splash's own `.rsc` is loaded in
  Startup before the hold starts. No sim state is involved anywhere (class A).

## 3. Expected saving (perf exe, census routes of LOADPLAN §2)

| route | floor today | ON |
|---|---|---|
| boot -> menu (4.74 s) | ShiningRock: fade-out at ~3.7 s though `menu.scene.done` ~2.3 s; Loading: 0.5 s | fade-out right after `menu.scene.done` (~-1.4 s); the Loading splash ends when MenuDLT's load is done (up to -0.5 s, less its load). **~4.7 -> ~3.0 s** (BOOTDEV / the global-resources block are the rest) |
| load Gate.sav (4.07 s) | dialog 1,465 ms after `save.read.io-done`; loading screen `splash.fadeout` 424 ms after `loader.done` | `save.read.done` one frame after `io-done` (**-1.45 s**); fade-out right after the loader / world (**up to -0.4 s**; the hold only bites when the load is shorter than ~1 s from the screen's start, which the other blocks make the rule) |
| new game (9.86 s) | 1.0 s hold, hidden behind the world build | nothing today; after WGPAR / EROSION / AUDIODEC it may bite on small maps |
| list in the load dialog | 0.75 s | one frame after the scan |
| in-game save | 1.5 s spinner | ends when the file is written |

A screen whose work finishes inside its own 0.25 s reveal now shows reveal + fade-out (~0.5 s) instead
of its hold.

## 4. Object proof (faithful path)

Method (WALKGRID §3): perf build at the base `c41ce8449`, the touched faithful TUs' objects kept, the same
objects after the change; `objdump -d -r --no-show-raw-insn -C`, per function, intra-function branch
offsets and section displacements normalised.

| TU | verdict |
|---|---|
| `splash_loop_stubs.cpp` | 12 functions **identical**. `Startup`: + `call Ext::Ext(); movzbl; mov %al,g_quickScreen` (once per screen). `Update`: + `cmpb $0,g_quickScreen; jne` on the not-yet-elapsed path, + the ON arm's out-of-line call block; the fade-kick block moved by the block layout (same instructions); prologue, epilogue and registers **unchanged** (`QuickScreenWorkDone` is `[[gnu::noinline]]` -- inlined, it cost an xmm6 save / restore in every Update). New function `QuickScreenWorkDone`. |
| `saveloaddialog.cpp` | 26 functions **identical**. `OnOpened` / `DoSave` / `DoLoad`: + `call Ext::Ext(); cmpb; je; movq $0` each (once per operation); `OnOpened` also reorders its `mov %rbx,%rcx` argument loads (scheduling). `std::vector<int>::_M_realloc_insert` differs only in a disassembler label name. |

Residue on the faithful path: the single latched-bool test per loading-screen frame while the hold has not
elapsed, and one `Ext::Ext()` read per screen / save-dialog operation.

## 5. Unit tests (`tests/unit/ext_quick_screens_test.cpp`)

`ext_quick_screens_row` (class A, default OFF, `ext.quick_screens=1` ON, `profile=all` ON, `profile=faithful`
and `/profile:faithful` OFF, never a pseudo-mod, live under the game lock);
`ext_quick_screens_off_is_retail_hold` (OFF = `elapsed >= delay` for every work / fade state);
`ext_quick_screens_on_waits_for_the_work` (scene build pending, revealing, not revealed -> held; done and
revealed -> over; over once the fade-out was kicked; never longer than retail);
`ext_quick_screens_min_operation_time` (replaced 2026-10-17 by a `static_assert` on `kNoMinOperationTime == 0`: it was a tautology).

## 6. Static gate (this worktree)

- `tools/build.sh build debug|perf --jobs 2`: exit 0; **0 new warnings** vs a clean build of the base
  (no `warn_baseline_*.txt` in this tree; compared per unique warning with line numbers stripped: perf
  135, debug 61 at the base; the touched TUs' warnings are the pre-existing printf-strip ones).
- `tools/build.sh unit debug --jobs 2`: **1195 tests, 0 failures**.
- `python tools/castmetric.py src`: **2**. `python tools/abi_audit.py`: OK (no type header touched).
- `tools/acceptance/acceptance.sh`: 0 lines off target.

## 7. For the coordinator

**Turn it on for a run.** Either
- `ext.quick_screens=1` in `C:/GOG Games/Banished/settings.cfg` AND launch **without** `/profile:faithful`
  (per-entry profile; the other `ext.*` / `fix.*` keys in that file then apply too -- check them), or
- `/profile:all` (every fix and extension on, class B rows included -- not a clean A/B), or
- the Extensions dialog toggle (live; takes effect from the next screen / save-dialog operation).

`tools/build.sh run` and `docs/extensions/handins/loadkit/census.sh` pass `/profile:faithful`, which forces
the row OFF: for the ON census, run census.sh with its `/profile:faithful` replaced (or a copy without it)
and the settings key set.

**Check, ON:**
1. Boot: `splash.fadeout GameLoadDialogLoopT` (the first) comes right after `menu.scene.done`, the menu
   opens, no black pause before it (debug log: no `waiting for ChunkedSceneLoader` lines after the
   fade-out); the second splash's fade-out right after its `loader.done`.
2. Load Gate.sav: `save.read.done` within a frame of `save.read.io-done`; `splash.fadeout` right after
   `gul.load.done` / `loader.done`; the game runs (resave of the loaded game unchanged vs OFF).
3. The load dialog's list appears at once; an in-game save (Save dialog) writes its file and returns.
4. New game, quit to menu, a tutorial transition: each loading screen fades in and out, no flash, no hang.

**Check, OFF (faithful):** the census numbers unchanged (`io-done` -> `done` ~1.46 s, boot fade-out ~3.7 s);
the gate as usual (the row is OFF under `/profile:faithful`).

## 8. Files

- `src/ext/quick_screens/quick_screens.h` (new)
- `src/ext/registry.h`, `src/ext/registry.cpp` (the field and the row)
- `src/game/Game/splash_loop_stubs.cpp` (the latch, `QuickScreenWorkDone`, the hold term)
- `src/game/Dialog/saveloaddialog.cpp` (three minimum-time sites)
- `tests/unit/ext_quick_screens_test.cpp` (new)
