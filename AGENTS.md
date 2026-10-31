# AGENTS.md — rules for contributed code (humans and their coding agents)

OpenBA is a clean-room C++ reimplementation of Banished's engine: the goal is readable code that
mirrors the game's logic exactly, compiled into a standalone exe that loads no retail binary.
A pull request is reviewed against the rules below. They are short; the pointers hold the detail.

## 1. No game content in the tree, the exe or a package
No retail shader source or DXBC, textures, models, sounds, `.rsc` or `.pkg` copies, and no port or
derivative of them. Retail data is read from the user's own install at run time; transform it at
load if you must, never ship it. A launcher or installer never copies game data.
(`docs/extensions/DESIGN.md` §3, rule 7.)

## 2. Faithful first; everything else is a fix or an extension
Game and engine code mirrors retail. A crash in a byte-faithful function is a bug upstream of it,
not a missing null check: find what handed it the null. Guard only where our standalone genuinely
differs from retail, and say so in the comment. Any behaviour retail does not have is either a
`fix.*` or an `ext.*` row under `src/ext/<id>/`, with a class (A/B/C), a settings-driven switch and
a Names-table entry (`docs/extensions/DESIGN.md` §3). It is never an `#if` in game code.

## 3. Flags and log output
`src/app/options.h` is the one inventory of environment flags, read once at boot into `Options`.
Nothing calls `getenv`. Do not add a flag for a working feature: a verified feature is unconditional
code or a settings.cfg key. Harness-only flags go in the harness section, named `BANISHED_*`, with a
comment naming the script that sets them. A field with no reader is removed.

No log output on a hot path (per tick, per frame, per entity or UI update) in a shipped build. Only
unqualified `printf` is stripped from the perf and release builds (`src/app/log_strip.h`);
`fprintf(stderr, ...)` and `ErrLine` ship, and every stderr line is a file write. Such a line goes
under `#ifdef _DEBUG`, behind an `Opt()` field that defaults to off, or under a hard cap on
its total count (the first N lines, then never again). A modulo throttle (`n % 600 == 0`) is not a
gate: it still prints for the whole session. A wall-clock heartbeat of one line every few seconds or
slower is fine. Investigation lines are deleted when the investigation closes.

## 4. Platform and renderer seams
- A platform is a set of whole-file twins picked by CMake: `src/platform/win/`, `src/platform/posix/`,
  `src/platform/darwin/` (`src/platform/darwin/README.md` has the replacement rule). Shared code is
  `src/platform/sdl/` and `src/platform/common/`.
- Platform behaviour goes behind a `Platform::` hook declared in `src/platform/common/platform_*.h`
  (`platform_app.h`: `PreSdlInitHints` / `PostSdlInit` / `Shutdown`; `display_density.h`;
  `Platform::Window::PixelDensity`), never behind `#if defined(__APPLE__)` or `_WIN32` in
  `src/app`, `src/game` or `src/engine`. Guard ARM code by ABI (`__aarch64__`), not by vendor.
- A renderer is `src/render/<name>/` with a `backend.cmake` and a `render_backend_<name>.cpp`
  implementing `src/render/common/render_backend.h`; CMake picks it with `BANISHED_RENDER_BACKEND`.
  Backend-neutral code lives in `src/render/common/`. Nothing outside a backend names that
  backend's namespace, and a backend never impersonates another's. A backend that cannot do a
  feature answers "not supported" through the interface header; it does not stub it silently.
  (`src/render/common/README.md`.)
- Engine type headers are binary-compatible (`.claude/rules/type-headers.md`): `static_assert` on
  every offset, retail vtable slot N is compiler slot N+1, implement every slot.

## 5. Style
One statement per line, no minified functions, `.clang-format` at the root is the reference, format
only the lines you touch (`.claude/rules/style.md`). Comments that carry a retail address or a block
name stay with the code they describe.

## 6. Licensing
Third-party code is listed in `THIRD_PARTY_NOTICES.md` with its licence and how it is linked. A new
dependency adds a row there, pins a version or commit, and ships its licence text with any package.
LGPL code is linked so the user can replace it (a separate dynamic library, or the relink offer).

## 7. What a PR says
- CI (`.github/workflows/ci.yml`) builds and runs `unit` on Windows, Linux and macOS, plus
  `tools/acceptance/acceptance.sh`, on every PR. All four checks must pass before a merge; a break
  on a platform the PR did not touch is fixed in the PR (a seam it missed), not waived.
- Which platforms you built and ran, and which you could not. CI has no game data, so it never
  runs the game: the maintainer runs the Windows gate (`tools/build.sh run`) and the serializer
  check (`tools/resave_gate/resave_gate.sh Gate`).
- Which commit the branch is based on. Rebase onto `main` before asking for review so the diff
  shows only your change.
- Tests for new pure logic (`tests/unit/`, `tools/build.sh unit`). A test that only runs on one
  platform says so.
- Documentation that is a session log (dates, worker names, screenshots not in the tree) goes to
  `docs/history/`; facts go to `docs/reference/`; a known divergence goes to
  `docs/known_port_issues.md`.

Build and run: `CLAUDE.md` (build, gates, logs) and `docs/reference/environment.md` (toolchain).
