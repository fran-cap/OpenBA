# Banished x64 — Progressive Reimplementation

@AGENTS.md

The file above is the contributor-facing rule sheet (read by every coding agent); this file is the
maintainer workflow. A rule that applies to outside PRs belongs in AGENTS.md, not here.

Keep this file under ~120 lines: rules and pointers only. No status, counts, dates or
history here — those go in `docs/runtime_port/STATUS.md`. Procedures go in
`.claude/skills/`, area-specific rules in `.claude/rules/`, facts in `docs/reference/`.

## Goal
Reverse engineer Banished (Shining Rock Software) and replace it with clean, readable C++
that mirrors the game's logic and compiles to a replacement exe — like OpenRCT2, re3 and
devilution. `OpenBA.exe` is fully standalone: it loads no retail binary.
Runtime-x64.dll is ported (`src/engine/`, its Win32 seams in `src/platform/win/`), VideoDX11-x64.dll
is replaced (`src/render/dx11/`), game logic lives in `src/game/`; the launcher is `src/app/`, the
diagnostics and harnesses `src/diag/`. There is no address registry: a call is a C++ call, and a
function with no body yet is a `// PORT GAP 0x<addr> <name>` comment at its call site.

## Current work — read these, don't restate them here
- **Where things stand:** `docs/runtime_port/STATUS.md`; **what is next:** `docs/runtime_port/NEXT_SESSION.md`
- **Phase plan:** `docs/runtime_port/ENDGAME.md` (closed B-phase sheets and kits: `docs/history/endgame_b/`)
- **How work is chosen and measured:** `docs/runtime_port/DIRECTION.md`;
  facts and the per-function recipe: `docs/runtime_port/PLAN.md` §2, §3, §6
- No waves, lanes, new instruments or new `BANISHED_RT_*` flags (DIRECTION.md §2).

## Build, run, gates
The build is CMake + Ninja (`CMakeLists.txt`; the pinned WinLibs GCC 16.2 from
`tools/toolchain/install_winlibs.sh`, cmake / ninja from pip on PATH). `tools/build.sh` is the one entry point;
every kit script calls it. The release exe is PGO-built: `perf/pgo/train.sh --build` retrains the profile set
before a release (`perf/pgo/README.md`).
```bash
tools/build.sh build [debug|og|perf|release]   # exe -> build/cmake-<flavour>/OpenBA.exe (debug default)
tools/build.sh deploy          # debug build + copy to C:/GOG Games/Banished
tools/build.sh run             # THE GATE: deploy + launch, auto-starts a new game
tools/build.sh run-menu        # same, stops at the main menu
tools/build.sh unit [flavour]  # build + run the unit tests
tools/build.sh stress          # perf build + the stress town (skill `stress-test`)
tools/build.sh clean [flavour] # remove the build directory (all four without a flavour)
tools/resave_gate/resave_gate.sh Gate   # serializer regression check (README alongside)
BANISHED_RENDER_BACKEND=vulkan tools/build.sh run-menu   # the Vulkan backend (docs/vulkan/PLAN.md), own build dir
```
`--jobs N` / `BUILD_JOBS` (default 8). Agents: jobs = 8 / agents building at once, minimum 2 (a lone agent builds at 8). On Linux the same CMakeLists builds `OpenBA` (Vulkan,
the default there) and `unit` (`tools/linux_gamedir.sh`, `tools/linux_run.sh`). `run` is the default check of any gameplay /
rendering / world-build change.
**Work lands by PR.** `main` is protected: the four CI checks (Windows, Linux, macOS, Acceptance) must
pass; admin bypass is for emergencies only. The gate and the resave check still run locally before
the merge. Renaming a CI job means updating the required checks in branch protection in the same change.

## Reimplementation workflow (per function)
1. **Decompile** in Ghidra: rename, annotate, document the struct layout (`ghidra` skill)
2. **Type header**: binary-compatible class, `static_assert` on every offset
   (`src/engine/<ns>/` for engine classes, the module's header in `src/game/`)
3. **Write clean C++** in `src/game/<Module>/` — the tree mirrors the original source layout;
   declare it in the class's header (or the module's `exe_bodies.h`, with its retail address)
4. **Call it by name** from its callers: include the declaring header, never a local `extern`.
   Replace the caller's `// PORT GAP` arm in a commit titled `behaviour:`
5. **Test**: `tools/build.sh run`, then check boot state and logs (below)

## Hard rules
- **Don't "fix" a faithful crash with a null check.** When a byte-faithful function
  crashes on an unguarded deref, retail is not wrong — something upstream handed us a
  null. Find that. Retail's unguarded derefs are load-bearing; a guard doesn't restore
  the behaviour, it skips it. Guard only where our standalone genuinely differs from
  retail (e.g. an entity legitimately lacking a component because a panel template is
  shared), and say so in the comment.
- **Never `getenv`.** Flags are read once at boot into `Options`; read `Opt().field`.
  Every `BANISHED_*` flag and its reader is listed in `src/app/options.h`. There are
  no live-feature env gates.
- **Poll a run with `tools/boot_state.sh --wait 300`, never a grep-until-timeout loop.**
  A quiet log does not mean it is loading: a modal dialog blocks boot while the process
  looks alive.
- **`[AutoStart]` and env-gated diagnostics go to `standalone_error.log`**, not
  `standalone_debug.log`. A grep of the wrong log returns 0 on a healthy run.
- **When the exe dies, read `standalone_crash.log` first** (exit 3). Exit 4 +
  `standalone_hang.log` means it froze.
- **A pixel diff between two runs measures nothing** (frames are not reproducible, even
  with seed + camera pin). Compare with `tools/trace_oracle/`.
- **No doors.** Nothing registers or resolves a body by retail name or address
  (`tools/acceptance/acceptance.sh` must stay at target). Wiring a body into a `PORT GAP` arm
  changes behaviour: read the consumer first, title it `behaviour:`.
- **Never run `find /`** or any unscoped filesystem search; it has contaminated perf A/Bs.
  Use Grep/Glob scoped to the worktree. Memory: `~/.claude/projects/C--code-GIT-untitled-rev-eng-project/memory/`
  (index `MEMORY.md`; `archive/` only when a task needs the history). No memory entries for PRs or sessions.
- **No game content in the tree or the exe.** No retail shader source / DXBC, textures, models, sounds,
  `.rsc` copies, or ports / derivatives of them; retail data is read from the user's install at run
  time (transform it at load if needed, never ship it). Rule 7 in `docs/extensions/DESIGN.md` §3.
- MSYS2 packages are stale: don't `pacman -S`. The compiler is not MSYS2's anyway (above).

## Subagents
Skill `agent-team` holds the brief template, worktrees, build jobs, game-dir lanes, hand-in and merge
rules. Models, passed on every call: `opus` for porting, debugging, design and research; `sonnet` for
mechanical edits on an established pattern and Ghidra lookups; `haiku` for file / log searches.
Subagents do not see this file: put the relevant hard rules in the prompt.

## Logs (all in the game dir, `$BANISHED_DIR`, `C:/GOG Games/Banished/` by default)
- `standalone_debug.log` — stdout (TRACE/printf) · `standalone_error.log` — stderr (`[AutoStart]`, `[BootState]`)
- `standalone_crash.log` — VEH crash dump · `standalone_hang.log` — watchdog thread stacks
- `standalone_boot_state.txt` — one-line machine-readable boot state

## Where everything else lives
| Need | Read |
|---|---|
| Crash / hang / stuck boot, exit codes, symbolizing | skill `triage-run` |
| Clicking menus unattended, autostart internals, modal answers | skill `menu-driver` |
| Perf runs, stress test, profilers | skill `stress-test`, `perf/README.md` |
| Framing or taking a screenshot, rendering verification | skill `camera-pin`, `tools/trace_oracle/README.md` |
| Launching a build for the user's own check | skill `hand-test` |
| Reviewing a contributor PR, landing a PR, stale branches | skills `review-pr`, `land-pr`, `cleanup-branches` |
| A new `fix.*` / `ext.*` row, Enhanced toggle, tweak tunables | skill `new-extension`, `docs/extensions/DESIGN.md` |
| The Android phone loop, apks | skill `android-device`, `docs/android/README.md` |
| Cutting a release, a tester apk | skill `release` |
| Ghidra setup and naming conventions | skill `ghidra`, `docs/reference/HOW_TO_START_GHIDRA.md` |
| Type header ABI rules | `.claude/rules/type-headers.md` (auto-loads on `src/engine/**`) |
| Retail binary facts, import table, modding toolkit | `docs/reference/binaries.md` |
| Main loop, launcher dialog, threading, address resolution | `docs/reference/engine_architecture.md` |
| Toolchain versions, VS Code / MCP debugging | `docs/reference/environment.md` |
| Per-binary function inventories | `decomp/README.md` |
| A player's bug report; known retail bugs; bugs in OUR port | skill `bug-report`, `docs/retail_bugs.md`, `docs/known_port_issues.md` |
| Wave / step history, old notes, lane logs (archive, open only when a task needs it) | `docs/history/` |
