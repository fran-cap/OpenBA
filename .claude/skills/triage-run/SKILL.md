---
name: triage-run
description: Diagnose an OpenBA.exe run that crashed, hung, exited non-zero, went quiet, or seems stuck loading. Covers tools/boot_state.sh states and exit codes, the HUNG watchdog and standalone_hang.log, standalone_crash.log and symbolizing with nm. Use before concluding "it's still loading" and whenever the exe dies.
---

# Triage a standalone run

All logs are in `C:/GOG Games/Banished/` (the launch working directory):
`standalone_debug.log` (stdout), `standalone_error.log` (stderr, survives abnormal exit),
`standalone_crash.log`, `standalone_hang.log`, `standalone_boot_state.txt`.

## 1. Is it actually running? — boot state

**A quiet log does not mean the game is working.** Retail's launcher dialog
(`Engine::PlatformConfigure`) appears whenever the previous run did not finish
`Engine::Startup` — in development, after every killed process. The process then stays
alive and pumping messages while `standalone_error.log` stops mid-boot, which looks
exactly like a slow load. Any other modal Win32 dialog (assert, driver prompt) blocks
boot the same way.

```bash
tools/boot_state.sh              # one status line
tools/boot_state.sh --wait 300   # block until RUNNING; bails INSTANTLY if blocked
```

Exit codes: `0` RUNNING · `1` NOT_RUNNING · `2` BLOCKED_DIALOG · `3` BOOTING ·
`4` STALE (state file left by a pid that is gone) · `5` HUNG · `6` NO_GAME_DATA.

Always poll with this, never a `grep -q`-until-timeout loop on the log: against a blocked
boot that burns the whole timeout for nothing. The state is published every second by
`BootState::Start` (`src/diag/win/boot_state_watchdog.cpp`) and logged as `[BootState]`
lines, repeated every 15 s while blocked.

`HarnessGuardUnattendedBoot` (`src/app/main.cpp`) clears the dialog's arming bit on
unattended runs, so those normally boot clean; the watchdog covers attended runs and the
cases where the guard does not hold. Background: `docs/reference/engine_architecture.md`.

## 2. How did it end?

| Evidence | Meaning | Read first |
|---|---|---|
| exit 3 + `standalone_crash.log` | crashed (VEH handler) | the crash log |
| exit 4 + `standalone_hang.log` | froze; watchdog killed it | the hang log |
| exit 6 + `STATE=NO_GAME_DATA` | the exe's `WinData/` is missing, empty or has no `data0.pkg`; stopped before `Init()` (`src/app/game_data_check.h`) | the state line's `detail=` (verdict + folder) |
| BLOCKED_DIALOG in the boot state, then exit 255 after the box is clicked | `Engine::Startup` failed: retail's fatal box (`System::Error::Display`), GameMain returns -1 (row 1273) | `standalone_debug.log`'s error string; `BANISHED_MODAL_ANSWER` does NOT answer this box |

### HUNG
The state line carries the main loop frame counter (`frame=` / `stale=`), bumped once per
`MainLoop::Run` iteration. If it stops for 20 s (`RUNNING`) or 45 s (`QUITTING`: `Run`
has returned and teardown renders no frames) the watchdog publishes `STATE=HUNG`, logs one
`[BootState] HUNG` line to stderr and writes `standalone_hang.log` with every thread's
stack, main thread marked. Unattended runs then self-terminate with exit 4; attended runs
are only reported, never killed. The thresholds live next to the watchdog's `Sleep`;
`tools/boot_state.sh` reads them from the source.

A modal in-game `UI::ShowDialog` pumps its own loop and so reads as HUNG on an unattended
run. That is intended: its pump is on the main thread's stack in the hang log, and
`BANISHED_MODAL_ANSWER=<n>` answers it (see the `menu-driver` skill).

### Crash
- The VEH handler catches ACCESS_VIOLATION / ILLEGAL_INSTRUCTION and fails fast to
  `standalone_crash.log`: fault address, registers, stack trace.
- Read the crash log FIRST. `[REDZONE-DUMP]` lines in the debug log can mislead.
- `EXEC-of-0` = a null function pointer was called (often an unregistered address).
- Symbolize with `nm` against `build/cmake-debug/OpenBA.exe` (image base `0x140000000`).
- If the fault is an unguarded deref in a byte-faithful function, do not add a null
  check: find the upstream null (CLAUDE.md, hard rules).

## 3. Heap corruption
`BANISHED_REDZONE=watch` (allocator watchdog, no shim) plus `src/diag/win/hw_watch.cpp`
hardware write watchpoints.
