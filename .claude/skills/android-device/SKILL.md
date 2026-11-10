---
name: android-device
description: The Android phone loop over adb (tools/android.sh) - build, sign and install a debug or release apk, push-data, run, pull logs and crash dumps, screenshot, zero the phone's settings, perf runs on the device, the Android PGO retrain, device_sim and the emulator, a signed tester apk for an issue reporter. Use for "phone is attached", "build an apk", "is it deployed to the phone", "test it on the phone / S25 / S10", "can I have the release apk", "zero out the settings on the phone", "re-run pgo for the apk", or when the phone looks frozen or locked.
---

# The Android device loop

Everything goes through `tools/android.sh` from **Git Bash** at the repo root (its header is the
reference; `docs/android/README.md` has the layout, data folder, logs and release). Package
`org.openba.game`; data folder `D=/sdcard/Android/data/org.openba.game/files` (logs, `WinData/`,
`Documents/Banished/settings.cfg`, `Save/`, `Cache/`). Several devices attached: `--serial S` (or
`ANDROID_SERIAL`) before the command; `adb devices -l` names the model (S25 = `SM_S931U`, S10 = `SM-G973U`).
Raw adb from Git Bash needs `export MSYS_NO_PATHCONV=1` or `/sdcard/...` paths get rewritten.

## The loop
```bash
tools/android.sh build debug --jobs 8        # android/app/build/outputs/apk/debug/app-debug.apk (debug key)
tools/android.sh install debug               # adb install -r -d
tools/android.sh push-data                   # once per fresh install: BANISHED_DIR's WinData/*.pkg, *.pkm
tools/android.sh run                         # to the main menu (always force-stops first)
tools/android.sh run BANISHED_AUTOSTART=1 BANISHED_PAUSEMENU=3000 BANISHED_PAUSEMENU_QUIT=120 \
                     BANISHED_PAUSEMENU_BUTTON=6          # new game, then a clean quit
tools/android.sh boot-state --wait 300       # = tools/boot_state.sh --android; same states / exit codes
tools/android.sh pull <scratchpad-dir>       # standalone_{debug,error,crash,hang}.log + boot state
tools/android.sh screenshot <file>.png       # adb exec-out screencap; Read the PNG to look at it
tools/android.sh logcat                      # follows OpenBA/SDL/vulkan/crash tags (blocks: run with a timeout)
tools/android.sh stop
```
- Agents block in the foreground on builds and runs, as on Windows. Jobs: 8 for a lone builder, fewer
  when a desktop build runs at the same time (CLAUDE.md rule).
- "Is it deployed?": `adb shell dumpsys package org.openba.game | grep -E "versionName|lastUpdateTime|pkgFlags"`
  (`DEBUGGABLE` in pkgFlags = the debug apk) against the apk's mtime.
- `boot-state --wait` returns at RUNNING. A harness run that ends itself (stress, pause-menu quit) is
  done only at `EXITED clean-shutdown`: poll `tools/android.sh boot-state` until then.
- Read `standalone_crash.log` first when it dies; symbolize `libmain.so + 0x...` with the unstripped
  library and `llvm-addr2line` (`docs/android/README.md` "Crashes"). Hangs: `tools/android.sh debug -o "thread backtrace all"`
  (debug apk only, `docs/android/LLDB.md`).
- Validation layer (debug apk): `tools/android.sh validation on <libVkLayer_khronos_validation.so>`, run,
  pull, grep `[Vulkan] validation`, then `validation off`.

## Release apk on the phone, and signing
- `tools/android.sh build release` = the release flavour (-O3, ThinLTO, the committed PGO profile),
  unsigned. `--flavour perf` is the -O2 baseline build; never compare a perf-flavour number with release.
- Local test signing uses the debug key (Windows-form path; a `/c/...` path is "file not found" to Java):
  ```bash
  OPENBA_KEYSTORE="$(cygpath -m ~/.android/debug.keystore)" OPENBA_KEY_ALIAS=androiddebugkey \
  OPENBA_KEYSTORE_PASSWORD=android OPENBA_KEY_PASSWORD=android tools/android.sh sign
  tools/android.sh install release             # apk/release/app-release.apk
  ```
- A debug-key install and a release-key install cannot replace each other, and `adb uninstall`
  deletes the data folder (packages, saves, settings). Before that, back up the owner's data:
  `adb pull $D/Documents <dir>` (and `WinData` if it will be re-pushed), restore with `adb push` after.
- **An apk for an issue reporter** is the release-key apk, which only CI has. Dispatch the Release
  workflow on the branch, then download the artifact:
  ```bash
  gh workflow run release.yml --ref <branch>; sleep 10; gh run list --workflow release.yml --limit 1
  gh run watch <id> --exit-status --interval 60     # foreground, repeat past 10 min
  gh run download <id> -n package-android -D <scratchpad-dir>
  ```
  No tag = no GitHub release is published. The file keeps the CMake version's name
  (`OpenBA-v<x.y.z>-android-arm64.apk`) until a version bump. Do not try to sign with a local copy of
  the release key. Install it on the attached phone for a smoke run before handing it over;
  posting it on the issue is outward-facing: ask first.

## Before a test: zero the settings and extensions
The first start with no `settings.cfg` writes the phone (or tablet) seed defaults, ext rows included.
```bash
adb shell am force-stop org.openba.game
adb pull $D/Documents/Banished/settings.cfg <scratchpad-dir>/settings_before_zero.cfg   # keep the user's
adb shell rm -f $D/Documents/Banished/settings.cfg
tools/android.sh run                         # reseeds; check the file with adb shell cat afterwards
```
Saves and `Cache/` stay. Zero again after an agent team's runs, before the user picks the phone back up.

## Phone state: locked, popups, frozen (ask the user)
- **A run stuck at a system dialog looks alive**: the pid lives, the game paused on lost focus.
  Take `tools/android.sh screenshot` and look before concluding anything.
- `adb devices` says `unauthorized`: the user must accept the USB-debugging prompt on the phone. Ask.
- Locked phone (`adb shell dumpsys window | grep -o "showing=[a-z]*"` = `showing=true`): adb cannot
  unlock a secure lock screen. Ask the user to unlock, then re-run. Keep it awake during a session with
  `adb shell svc power stayon usb` (restore `false` at the end).
- Play Protect's "send this app for a security scan?" blocks every adb install of a debug-key apk and
  sits over the game. With the user's OK: `adb shell settings put global verifier_verify_adb_installs 0`
  (undo: `1`); repeat any run that overlapped the popup.
- Frozen phone: `adb reboot` only with the user's OK; then the user must unlock it again.
- Wireless adb (the phone on the user's Tailscale): `adb connect <ip>:<port>` with the port the phone's
  Wireless debugging screen shows; wrap it in `timeout 20` (an offline phone hangs it).

## One driver per phone
Only one party installs or runs on a phone at a time; an install mid-run kills the other's run. An
agent team takes a `mkdir` lock directory in the coordinator's scratchpad (`phone.lock`, holder file
inside) around install + run. Stop a background harness by killing its bash process tree, not TaskStop
alone: orphans kept driving the phone.

## Perf on the device
- Measure with the stress harness: `tools/android.sh run BANISHED_STRESS=1 BANISHED_STRESS_FRAMES=<n> [BANISHED_FPS_CAP=60]`,
  read `^\[Stress\] t=` lines (`fps=`, `frame=`, p95) in the pulled `standalone_error.log`.
  `BANISHED_STRESS_SPEED=1` for render cost (speed 10 is sim-bound; fps noise hides render changes).
- The phone seed caps at 60 (`ext.fps_cap`); a result at exactly 60/120 is the cap, not the code.
  State the cap and render scale (seed Half = 50) with every number.
- Thermal: start each run cool and stopped: S25 idles ~37 C while charging, cool to <= 38 C; S10 to
  battery <= 32.0 C. `adb shell dumpsys battery | grep temperature` (tenths of a degree) and
  `adb shell dumpsys thermalservice` (status 0). Never let the screen sleep between runs (a secure lock
  holds the next run): dim it (`settings put system screen_brightness 1`) and restore the saved value.
- A/B as ABBA pairs; a single pair is noise. Which limit: the S25 is CPU-limited in ordinary play and
  GPU-pixel-bound at far zoom at render scale 100; the S10 is CPU / memory-bound (`docs/android/P13_RENDER.md`
  §3.4 for the GPU-busy check). A pure GPU win may not move S25 fps.
- S10 = the weak-device check: Vulkan 1.1 fallback (Adreno 640, no BC: ETC2 transcode at load,
  AO "not supported"; `docs/known_port_issues.md`), no ADPF, the gold-cluster pin.

## Android PGO retrain (before a release)
No script: follow `perf/pgo/android/README.md` (`build release --pgo generate`, sign, install, clear
`$D/pgo`, the 10x and 50x stress towns each to `EXITED clean-shutdown`, `tools/android.sh pgo-merge`,
remove the stale RelWithDebInfo `.cxx` dir, `build release`). A run killed from recents writes no
profile. Commit `perf/pgo/android/openba.profdata`; on a cool phone with nothing else running.

## Desktop first: device_sim and the emulator
UI / touch / scaling changes are checked on the desktop before the phone:
`tools/device_sim.sh phone|tablet [--view game|panels|options] [--ui N] [--set KEY=VALUE] --screenshot <png>`
(S25 2340x1080, tablet 3392x2400; `docs/reference/device_sim.md` lists what it is not). For an Android
build without the phone: `tools/android.sh build debug --emulator`, `avd create|start tablet|phone`,
then `--avd <profile>` on every command (`docs/android/EMULATOR.md`). Neither measures phone perf.

## Traps that bit
- adb-made `$D/WinData` belongs to the shell user and the app cannot read it: never `adb push` or
  `mkdir` into a missing data folder; `push-data` starts the app once to make it.
- A harness launch without game data exits 6 (`NO_GAME_DATA`) silently: run `push-data` after a fresh install.
- `unit` and `tools/build.sh` do not touch the apk: rebuild with `tools/android.sh build` before install.
- `install` refuses a lower versionCode over a release-key install (`-d` works only over debuggable).
- After the first `pgo-merge` a configured release dir keeps building plain (BANISHED_PGO=auto is
  decided at configure).
