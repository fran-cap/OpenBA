# Host unit tests for the real classes (ENDGAME B)

The classes under test stand on their own: no game directory, no window, no registry
(docs/history/endgame_b/B31_RULES.md rule 6). A test file is a list of `UNIT_TEST`
functions using the `CHECK` macros in `unit.h`; `main.cpp` runs them all and exits with
the failure count.

## Build and run

```bash
tools/build.sh unit [debug|og|perf|release]   # build + run; debug is the default
tools/build.sh unit debug --shards 4          # the same suite on 4 parallel processes (CI does this)
cmake --build build/cmake-debug -t unit -j2   # build only -> build/cmake-debug/unit.exe
build/cmake-debug/unit.exe                    # run from the repo root
```

`tools/build.sh unit` configures `build/cmake-<flavour>`, builds the `unit` target and runs
`build/cmake-<flavour>/unit` directly; run it from the REPO ROOT (the tests open `tests/unit/data/`
and `src/` by relative path). The `run-unit` CMake target does the same with that directory set. On Linux the same CMakeLists builds `unit`
over `src/platform/posix` (`cmake --build build/linux -t unit && build/linux/unit`). The
exe links statically on Windows, never links SDL, and is built with `-g -Og -D_DEBUG
-DBANISHED_UNIT_TEST` plus the tree's portable flags, in every flavour (CMakeLists.txt says why). `--jobs N` / `BUILD_JOBS` limit the
parallelism (agents use 2).

## The harness (`unit.h`, `main.cpp`)

- `UNIT_TEST(name) { ... }` registers a test at static-init time; `main.cpp` walks the
  list in link order and prints `ok  `, `FAIL` or `skip` before each name, and the test's
  wall time after it when that is a second or more (`ok   name (12.3 s)`). stdout is
  flushed after every test, so a crash's log ends at the test that was running.
- `unit --shard K/N` runs only the tests at positions K, K+N, K+2N... of the list;
  `unit --filter TEXT` only the tests whose name contains TEXT (`build/cmake-debug/unit --filter wg_parallel`).
  `tools/build.sh unit --shards N` cuts the list into 8N chunks and runs them on N parallel
  processes (`xargs -P`), prints the chunks' output in order, one summary line, the skips
  and the ten slowest tests; it exits non-zero when any chunk failed or died. Tests that
  write files must therefore use a name no other test uses (the temp-directory helpers
  stamp theirs with the clock or the pid); a test may run beside any other test.
- `CHECK(cond)`, `CHECK_EQ(a, b)`, `CHECK_STREQ(a, b)` print `FAIL file:line: expr` and
  count one failure; the test keeps running (return early yourself when a later line
  would dereference what the failed check guarded).
- `UNIT_SKIP(reason)` records a skip (counter + reason), prints `skip <name>: <reason>` and
  returns from the test. Use it when the host cannot run the test at all: no game install
  (`xwma_decode_test.cpp`, `xwma_tos16_test.cpp` read `WinData/data0.pkg` from
  `$BANISHED_DIR`, `C:/GOG Games/Banished` or `/mnt/c/GOG Games/Banished`), a
  compile-time switch off (`draw_sort_test.cpp`). A skipped test is never `ok`: the summary
  line is `N tests, N failures, N skipped` and every skipped name and reason is listed
  after it, so a silently absent fixture cannot pass a run. The exit code counts failures
  only.

## What gets compiled and linked

- **Auto-discovery.** CMakeLists.txt globs `tests/unit/*.cpp` (`CONFIGURE_DEPENDS`): a
  new test file is picked up by the next build with no list to edit. Helper `.inc` files
  under `tests/unit/data/` are included by the tests that use them.
- **Tree sources: `tests/unit/sources.cmake`.** Hand-maintained (it was generated from
  the Makefile's `sources.mk` until the Makefile was deleted on 2026-10-11). It lists
  every `src/` file the tests link, one family per block. A body a test calls whose
  `.cpp` is not on the list fails at LINK time with an undefined reference, never at
  compile time; the fix is to add the file, or, when the real file cannot link on the
  host (it stands on the package system, Win32, the video device...), a stub in
  `link_stubs.cpp` or in the test's own `*_stubs.cpp`. If a stubbed file is later added to
  the list the linker reports the duplicate definition: delete the stub, not the body.
  On Linux each `src/platform/win/<seam>_win.cpp` on the list is swapped for its
  `src/platform/posix/<seam>_posix.cpp` twin.
- **`-DBANISHED_UNIT_TEST`** is defined for the unit build only. It compiles out what the
  host cannot provide and substitutes the smallest possible seam (`grep -rn
  BANISHED_UNIT_TEST src/` lists every site):
  - `src/engine/collection/Collection.h`: the width-specialised `AvlOps` bodies
    (the ported cores in `set_avl.cpp`, `set_avl_e24.cpp`, `avl_trees.cpp` and
    `modification_avl8.cpp`; `set_core.cpp` holds the forwarders and allocator doors) are replaced
    by `AvlGeneric` for every width -- except in `set_ported_test.cpp`, which defines
    `BANISHED_UNIT_PORTED_AVL` first and links the four ported cores for its own element types -- and the inline `rtcoll::MemFree` becomes an
    out-of-line call; `array.cpp` and `linkedlist.cpp` allocate with malloc/free (the
    list's test allocator carries a header that detects double frees and exports
    `g_listNodeAllocs` / `g_listNodeFrees` / `g_listNodeDoubleFrees` for `list_test.cpp`).
  - `src/engine/object/Internal.h`, `internal_class.cpp`, `type_reflection.cpp`,
    `root_operator_delete.cpp`: `InternalFactory::ResetPersistState` and the registered
    factory doors are compiled out; the factory and the type records allocate with
    malloc instead of `System::MemoryAllocator`.
  - `src/engine/system/String.h` / `string.cpp`: the String release path uses the host
    malloc seam instead of `MemoryAllocator::MemFree`.
  - `src/engine/video/companion_operators.cpp`: the Video companion-data roots allocate
    with malloc.
  - `src/ext/registry.h` / `registry.cpp`: three test-only extension rows (`unit_a`,
    `unit_b`, `unit_c`) exist so `ext_registry_test.cpp` can test the class lock rules.
- **`wmadec`.** The unit exe links `third_party/wmadec` (FFmpeg's WMA v2 decoder,
  `EXTRACT.md`), the same static C library the game links, so the `xwma_*` tests decode
  real xWMA.

## Fixtures (`tests/unit/data/`)

`data/README.md` describes each file. No game content is stored in the tree
(CLAUDE.md): `resource_names.inc` is a generated list of the resource names our own
sources mention, and the xWMA decoder is checked structurally (sample count, not silent, not clipped,
chunked == whole, deterministic) in `xwma_decode_test.cpp`; the retail-derived fingerprint
it once carried was removed 2026-10-17 at the user's ruling. The packet bytes it decodes
are read from the user's install at test time (and
the test skips, visibly, when there is none).
