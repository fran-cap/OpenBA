# Review campaign 2026-11-03: sanitizers, fuzzing, static analysis, scoped sweeps

Session document (history, not reference). The map the sweeps work from is `P0_MAP.md` (hotspot
rank, bug-class taxonomy C1-C18, input surface, concurrency, GPU / platform).

## Rulings

1. Tools installed: clang-19 + clang-tidy + compiler-rt (WSL, apt), WinLibs GCC 16.2 + LLVM 23.1.3
   beside the pin (`C:/winlibs-gcc16-llvm`, the pinned `C:/winlibs-gcc16` untouched), Semgrep 1.183
   (pip --user), CodeQL CLI 2.27.4 (`C:/codeql-cli`). All archives checked against their sha256.
2. The campaign is an exception to DIRECTION.md §2: sanitizer builds, fuzz targets and analysis
   baselines are in scope.
3. Sanitizer / fuzz / static-analysis CI jobs are not required checks.
4. Every instance of undefined behaviour is fixed, with a minimal fix and a comment naming the UB,
   whether or not retail shares it.
5. Order: faithful core, fixes, default-on extensions first; default-off extensions and `src/diag`
   after. Android and Metal are in scope.
6. At most 4 agents build at once; read-only sweeps are not capped. No AMD / Intel GPU yet.

## Phases

| # | Phase | State |
|---|---|---|
| P0 | Map (hotspots, bug-class taxonomy, input surface, threads, GPU) | done: `P0_MAP.md` |
| P1 | `check` build: libstdc++ assertions, pattern-initialised locals; unit + gate | |
| P2 | WSL ASan+UBSan and TSan: unit suite, then a Linux game route | |
| P3 | clang-tidy, Clang Static Analyzer / CodeChecker, CodeQL, Semgrep: baselines | |
| P4 | libFuzzer targets for the input surface (P0 ranks 1-8) | |
| P5 | Thread-safety annotations on the eight structures P0 names | |
| P6 | GPU: Vulkan validation (sync, GPU-AV, best practices), DX11 debug device | |
| P7 | Mull on serializer / collections / math / text codec | |
| P8 | Scoped agent sweeps: hotspot module x bug class, variant analysis | |
| P9 | Fix PRs: one finding per PR, with its failing test | |

## Build plumbing

`BANISHED_SANITIZE=none|check|asan|ubsan|tsan|fuzz` (CMakeLists.txt) on top of any flavour;
`tools/build.sh` keys `build/cmake-<flavour>[-<backend>]-<sanitize>` from the env variable.
Under `asan` / `fuzz` the engine heap's instrument seat routes MemAlloc / MemFree to
`posix_memalign` / `free` (`src/engine/system/allocator.cpp`), so ASan sees every engine
allocation. UBSan runs without `vptr` and `alignment` (the type headers reinterpret memory).

## Findings

Every finding carries a proof (failing unit test, fuzz input, sanitizer report, or a replayable
route) and a label: **port** (differs from retail), **retail** (faithful; fixed anyway if UB, else a
`fix.*` row), **ext** (our extension / platform code), **harness** (diag / tests). Unproven leads
are in "Suspected".

| ID | Where | Class | Label | Proof | State | PR |
|---|---|---|---|---|---|---|

## Suspected (no proof yet)

| ID | Where | Lead | Source |
|---|---|---|---|
