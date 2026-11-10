---
name: hand-test
description: Hand a running OpenBA build to the user for a manual look - pick the flavour (debug for the Alt+F10 tweak overlay), pick a free game dir, turn the extension on in settings.cfg, deploy, launch so it stays up, confirm it booted, report exactly what is running, then record the verdict and close it by PID. Use for "launch it so I can check", "ok launch it ill do a visual check", "launch the game so I can spot check", "relaunch please", "closed, launch it", "another session killed the app", "is it deployed to the folder?", "launch the release stress map".
---

# Hand a build to the user

The user plays what you launch. Their game may also be running already: every step below assumes it is.
Android hand-off is the skill `android-device`. Framing a spot: `camera-pin`. Other menu routes: `menu-driver`.
A run that dies or will not boot: `triage-run`.

## 1. Pick the flavour (say which in the hand-over)
| Need | Flavour | Build dir |
|---|---|---|
| Alt+F10 tweak overlay (`docs/reference/tweak_overlay.md`; debug, Windows only) | `debug` | `build/cmake-debug` |
| A look or a feel at real speed, fps on screen | `perf` | `build/cmake-perf` |
| "do a release launch", what ships (PGO, LTO) | `release` | `build/cmake-release` |
| Vulkan only | `BANISHED_RENDER_BACKEND=vulkan tools/build.sh deploy` | `build/cmake-<flavour>-vulkan` |
The default Windows exe is the dual DX11 + Vulkan one; its renderer is picked in game (`ext.render_backend`).
The in-game debug panel is the debug **mod** (a `.pkm` in `WinData/`), not the flavour: if a gate parked it in
`WinData/_mods_off/`, move it back when the user wants it ("leave the debug mod on").

## 2. Pick a game dir that is not in use
```bash
tools/game_dirs.sh status                       # perf = C:/GOG Games/Banished, functional = .../Banished-B
tools/game_dir_lock.sh orphans                  # names an UNLOCKED OpenBA run in the default dir
powershell -NoProfile -Command "Get-Process OpenBA -EA SilentlyContinue | Select Id,StartTime,Path"
eval "$(tools/game_dirs.sh env handtest functional 600)"   # its own statement, never `eval ... &&`
echo "BANISHED_DIR=$BANISHED_DIR"               # empty = lock not granted; do not proceed
```
- An OpenBA.exe already running from a dir = the user is playing there: use the other dir, touch nothing of it.
- Hold the lock for the whole hand-off: it is what stops another session's gate from deploying over (or
  killing) the user's game. Release it only after the verdict (step 6).
- functional uses `BANISHED_USER_DIR=<dir>/_userdata` (settings + saves of its own; the eval exports it). A
  launch from Banished-B without it shares the real `Documents/Banished` settings and `Auto.sav`.
- A save the user names lives in the real user dir; for the functional dir copy it in:
  `cp -n "$USERPROFILE/Documents/Banished/Save/<name>.sav" "$BANISHED_USER_DIR/Save/"`.

## 3. Turn the extension on (so the user does not hunt for it)
The user dir's `settings.cfg` (`$BANISHED_USER_DIR/settings.cfg`, or `$USERPROFILE/Documents/Banished/settings.cfg`
for the perf dir) holds `ext.<id>=0|1` and `fix.<id>=0|1`. Edit it only while no game uses that user dir
(the game rewrites the file at quit and would undo the edit). Back it up first; CRLF and LF lines both parse.
```bash
S="$BANISHED_USER_DIR/settings.cfg"; cp "$S" "$S.pre_handtest"
grep -q "^ext.<id>=" "$S" && sed -i "s/^ext.<id>=.*/ext.<id>=1/" "$S" || printf 'ext.<id>=1\n' >> "$S"
grep -a "^ext\.\|^fix\." "$S"
```
Class B / C rows lock per game: they take effect at New Game; a loaded save keeps the rows it was saved with.

## 4. Deploy and launch so it stays up
```bash
tools/build.sh deploy <flavour>                 # builds + copies; `unit` alone never relinks the exe
cmp "$(tools/build.sh exe <flavour>)" "$BANISHED_DIR/OpenBA.exe" && echo "deployed exe is current"
cd "$BANISHED_DIR" && rm -f standalone_boot_state.txt standalone_crash.log standalone_hang.log
BANISHED_AUTOSTART=1 start OpenBA.exe           # new game; drop the variable to stop at the menu
cd - >/dev/null && tools/boot_state.sh --wait 300   # boot_state reads $BANISHED_DIR
```
- Menu: plain `start OpenBA.exe`. A named save: `BANISHED_AUTOLOAD=<save name> start OpenBA.exe` (`1` = Auto).
  Fixed map: add `BANISHED_SEED=12345`; a framed spot: `BANISHED_CAMERA_PIN=town,90` (skill `camera-pin`).
- Release stress map: `STRESS_ARGS="--label handtest --frames 0" tools/build.sh stress release`, in the
  background (it waits for the exe to exit). `--frames 0` = no timeout; `STRESS_PROFILE=all` for everything on,
  any other value (`per-entry`) for the settings.cfg rows; the default is faithful.

Traps that bit:
- `tools/build.sh run` passes `/profile:faithful`: every fix and ext is off and the overlay reads "nothing to
  tweak". Use it for the gate only, never for a hand-off of an extension.
- `tools/build.sh run` / `run-menu` / `deploy` go to `C:/GOG Games/Banished` unless `BANISHED_DIR` is exported.
- `./OpenBA.exe` in the foreground blocks the tool call; `timeout N ./OpenBA.exe` kills the user's game at N.
- Never launch anything else visible (notepad, explorer, `start <file>`): the user sees it pop up.
- deploy says "exe busy": a game is running from that dir. Do not kill it to make room; use the other dir.
- A stale `standalone_boot_state.txt` describes the previous process: delete it before the launch, and check the
  `pid=` it reports has a StartTime after your launch.
- An AUTOSTART / AUTOLOAD launch counts as unattended: a main loop stalled past the budget (a debugger
  break) makes the watchdog end it with exit 4 and `standalone_hang.log`.
- From a worktree: run `tools/build.sh` inside it (its own `build/`); a fresh one needs its submodules
  (`git -c protocol.file.allow=always submodule update --init`).

## 5. Say "it's up" only after RUNNING, then hand over
```
Up: <exe path in the game dir>, <flavour> [<backend>] build of <branch> @ <short commit> (dirty: yes/no)
Game dir: <dir> (user dir <path>), pid <pid>, started <time>
State: <main menu | new game seed N | save "<name>">; profile per-entry
On: ext.<id>=1, ... (fixes: default)   Off: anything else touched
Look at: <where, what changed, what "right" looks like>. Alt+F10 = tweak overlay (debug only).
Say when done; it is closed by pid <pid>.
```
"Is it deployed to the folder?": `Get-Process OpenBA | Select Id,Path,StartTime` shows which exe the user runs;
`cmp` that dir's exe with the build; the main menu's build line shows the commit.
"Another session killed it" / "closed, launch it": rerun step 2 (another session may have deployed its own exe:
`cmp` again, redeploy), relaunch, and keep the lock held this time.

## 6. After the verdict
- Tweaks the user settled on: the overlay's "write to settings.cfg" and the panels' copy lines; read them back
  from `settings.cfg` and the `[TweakOverlay]` lines in `standalone_error.log`, and put them into the
  extension's params header (e.g. `src/ext/depth_of_field/dof_params.h`). Write down the values the user said.
- Close by PID only, and only the run you launched (StartTime after your launch):
  `taskkill //PID <pid> //F`. Never `//IM OpenBA.exe`: it closes the user's own game in the other dir.
  A crashed process that `taskkill` cannot find yet still holds the exe: `wmic process where "ProcessId=<pid>" delete`.
- Restore `settings.cfg` from the backup if the user did not ask to keep the rows; `tools/game_dirs.sh release handtest`.
