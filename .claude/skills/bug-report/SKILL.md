---
name: bug-report
description: Handle a player's bug report end to end - restate it as a testable claim, list the missing data, read the attached logs, reproduce it (the reporter's save, a harness route, a parked mod set), decide with evidence whether it is a port divergence, a retail bug (fix.* row), intended retail behaviour or not reproducible, fix it on a branch off main, and draft the reply. Use for "got a bug report", "user reported this issue", "can we look into this issue", "can we look into this reported issue", "can you explain this behavior a user is reporting", a pasted Discord/email message, a GitHub issue link, or attached saves, crash logs or a reporter's exe.
---

# Handle a player's bug report

The report is a claim, not a diagnosis. Reporters guess at causes ("since 1.0.1", "on Android",
"after my extension"); each guess is checked, never adopted.

## 1. Restate and list what is missing
- Write the report as one testable sentence: action, expected result, observed result, frequency.
- List what is missing before any code is read: platform and build (release zip, apk, their own
  build), mod list, settings (fixes / extensions on?), a save, logs, screenshots.
- For a state bug (a tile, a pile, a stuck citizen) ask the reporter, through the user, for a
  **before/after save pair**: one where it still works, one where it does not. Guessing a repro
  instead cost a whole session; the pair settled it.
- Read the issue itself with `gh issue view <N> --comments` (read only).

## 2. Read what came with it
- `standalone_crash.log` first, then `standalone_error.log`, then `standalone_debug.log`
  (skill `triage-run`: exit codes, symbolizing; `[REDZONE-DUMP]` lines are not evidence).
- **Check the build is ours.** Grep the log's distinctive strings in the tree; a line no branch has
  means a custom combination build. Ask for that exe: an unstripped exe names its own addresses
  (`nm <exe>`), and its crash may still be a `main` bug.
- Screenshots: confirm the version string and which extensions are visible.

## 3. Reproduce
Work in a worktree off main from the start (`git worktree add -b fix/<topic> C:/code/GIT/openba-<topic> main`,
then `git -c protocol.file.allow=always submodule update --init --recursive`); the eventual PR comes
from it. Take a game dir with `eval "$(tools/game_dirs.sh env <lane> functional)"` on its own line
(never chained with `&&`), and `tools/game_dirs.sh release <lane>` after.

| Report shape | Route |
|---|---|
| Their save | copy it into the lane's save dir; `BANISHED_AUTOLOAD=<name>` loads `<save dir>/<name>.sav` (functional user dir: `C:/GOG Games/Banished-B/_userdata`) |
| Placement / red cells | `tools/cell_dump/cell_dump.sh <save> <outdir>` per save, then `cell_diff.py` (`tools/cell_dump/README.md`) |
| A game action (trade, birth, immigration...) | `tools/harness_route.sh list`, then `tools/harness_route.sh <route>`; `HARNESS_FIXES=<id>,...` for fixes on |
| A menu / Mods page / pause-menu path | skill `menu-driver` |
| A click on a building or panel | `BANISHED_PANEL_HOLD` / `_CLICK` plus the mouse injector (`src/diag/mouse_inject.cpp`, flags in `src/app/options.h`); skill `camera-pin` to frame it |
| A tool sequence (upgrade, demolish, place) | the scripted scenarios in `src/diag/tool_harness.cpp` |
| Rendering | `tools/trace_oracle/README.md`; never a pixel diff between two runs |
| A mod or mod set | parked sets sit in a `_mods_off/` folder in the game dir; move, rename or symlink them in and back; never copy a multi-GB set per run |
| A package that vanished | read the real file through `PackageFile::ReadPackfile` in a temporary unit test |

- `tools/build.sh run` and the harness routes are `/profile:faithful`; a player runs the settings
  profile with the default fixes on. Reproduce in both (`CELL_DUMP_PROFILE=settings`, `HARNESS_FIXES`).
- A clean result states exactly what ran (frames, speed, counts, profile) so it means something.

## 4. Decide the bucket, with evidence
Read the retail function in Ghidra (skill `ghidra`) before choosing; "it looks wrong" is not a bucket.

- **(a) Port divergence** - the port differs from retail. Find what differs upstream; a crash on an
  unguarded deref is fixed where the null came from, never with a guard. Commit titled `behaviour:`,
  plus a section in `docs/known_port_issues.md` marked FIXED.
- **(b) Retail's own bug** - retail does the same. A `fix.<id>` row under `src/ext/<id>/` with a class
  (A/B/C) and a settings switch (`docs/extensions/DESIGN.md` §3), a `docs/retail_bugs.md` section,
  `docs/extensions/INVENTORY.md` and `CHANGELOG.md`; a new row's icon re-runs
  `tools/ext_icons/fixes.py`, `tools/ext_sprites_bake.py` and `tools/ext_resources.sh` before `unit`.
  Faithful mode keeps retail's behaviour. Never an `#if` in game code. Row mechanics: skill `new-extension`.
- **(c) Intended retail behaviour** - explain the mechanism with file:line and retail address; no change.
- **(d) Not reproducible** - say what ran and what data would settle it; record a SUSPECTED section in
  `docs/known_port_issues.md` only when there is a concrete suspect.

When the evidence is ambiguous (retail bug or intended? fix or extension?), present it and let the
user rule; do not pick for them.

## 5. Fix and test
- Add a unit test for the pure logic (`tests/unit/`, `tools/build.sh unit`).
- Re-run the repro from step 3 with the fix, and once without it to show the difference.
- Offer a by-hand check (skill `hand-test`) when the symptom is visual or interactive.
- Hand over to skill `land-pr` (gate, resave, PR, CI, merge). Release builds are for releases, not PRs;
  build a test exe or apk for the reporter only when the user asks.

## 6. Draft the reply (the user posts it)
Discord or email: one line, plain words, no file names.
> Found it: <symptom> came from <cause in player terms>. Fixed in <PR / next build>; <what to do meanwhile>.

GitHub comment: impersonal (no "I", "we", "our"), shown to the user for approval before any `gh` write.
> Cause: <one sentence>. Retail <does / does not> do the same (`0x<addr>`), so this is <a port bug / a
> retail bug, now `fix.<id>`, on by default / intended behaviour>. Fix: <PR link>. Checked: unit, gate,
> resave<, reporter's save>. <Data still needed, if any.>

Changelog credit uses only the name the user confirms; ask, since report names and GitHub handles differ.

## Traps that bit
- A platform in the report is a guess: an "Android" missing-mod bug hit every platform.
- "Since version X" is a guess: a "1.0.1 regression" had never worked in the port.
- A silent reject hides the cause: a package that fails parsing is dropped with no log line.
- The reporter's own build may carry fixes `main` lacks or lack ones it has: compare before chasing.
- The perf game dir may hold the user's own running game: check `Get-Process` start time before a run or kill there.
- A reporter's "the debug panel broke" was retail behaviour (the barn was full) next to a real port crash: split a report into its separate claims.
- Similar reporter names and GitHub handles belonged to different people: confirm the credit name before it lands in `CHANGELOG.md`.
