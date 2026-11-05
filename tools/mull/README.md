# Mutation testing with Mull (by hand, Linux / WSL)

Mull compiles small changes ("mutants") into the code under test, such as `a + b` to `a - b`,
`<` to `<=` or `++x` to `--x`. It then runs the unit tests once per mutant. A mutant the tests
catch is **killed**. A mutant they miss **survived**: either no test checks that behaviour, or
the change is equivalent (it cannot change any result). This measures how much of the code the
suite actually checks, which line coverage cannot show.

This is not a CI job. Run it by hand after a change to one of the measured files, or when
writing tests for them.

## What is measured

`mull.yml` lists the files that get mutants (everything else compiles normally):

| Module | Files | Tests (`run.sh tests_for`) |
|---|---|---|
| serializer | `src/engine/io/serialize_family.cpp`, `compressed_stream_read.cpp`, `compressed_stream_open.cpp`, `memory_stream.cpp`, `text_codec.cpp` | decode_exact, compressed_stream_bounds, package_file_read, save_metadata_hardening, string_codec_hardening, io_streams_abi, b39_io_classes, review_system_misc, mull_gaps_serializer |
| collections | `src/engine/collection/array.cpp`, `array_destroy.cpp`, `linkedlist.cpp`, `introsort.cpp`, `set_core.cpp`, `set_avl.cpp`, `set_avl_e24.cpp`, `avl_trees.cpp` | array, list, set, set_ported, setsize_fast, ptr_index, msvc_sort_skeleton, review_engine_ub |
| math | `src/engine/math/*.cpp` | math, frustum_cull, terrain_ray, mull_gaps_math |
| string | `src/engine/system/string.cpp` | string, filename, review_engine_ub, string_codec_hardening, mull_gaps_string |

Every `UNIT_TEST` in a module's test files is passed to `unit` as a `--filter`. `unit` ORs
repeated filters (`tests/unit/main.cpp`). Only a `UNIT_TEST(name)` at column 0 is picked up: an
indented one, or a test declared through another macro, is left out of the selection. `--filter`
is a substring match, so a name that is a prefix of other test names selects those tests too.
An unknown module name stops `run.sh` with exit 2 before anything runs.

## Setup (Ubuntu 24.04 / WSL)

clang-19 and Mull 0.37 for LLVM 19. The cloudsmith apt repository Mull's documentation names
answers `402 Payment Required`, so the packages come from the GitHub release instead:

```bash
sudo apt-get install -y clang-19
cd "$(mktemp -d)"
curl -sLO https://github.com/mull-project/mull/releases/download/0.37.0/Mull-0.37.0-ubuntu-amd64-24.04.deb
curl -sLO https://github.com/mull-project/mull/releases/download/0.37.0/Mull-19-0.37.0-LLVM-19.1.1-ubuntu-amd64-24.04.deb
sudo apt-get install -y ./Mull-0.37.0-ubuntu-amd64-24.04.deb ./Mull-19-0.37.0-LLVM-19.1.1-ubuntu-amd64-24.04.deb
mull-runner-19 --version     # lists the plugin /usr/lib/mull-ir-frontend-19
```

The checkout needs its submodules (`third_party/`), as for any Linux `unit` build.

## Run

From the repository root:

```bash
tools/mull/run.sh build                  # build/mull/unit, compiled with the Mull plugin + coverage
tools/mull/run.sh run                    # all four modules, then the census and the table
tools/mull/run.sh run string math        # some of them
tools/mull/run.sh summary                # the table again
tools/mull/run.sh survivors serializer   # the surviving mutants, path:line:col: status: mutator
```

`MULL_WORKERS` (default `nproc`), `MULL_TIMEOUT` (ms per test run, default 60000), `BUILD_JOBS`
and `MULL_BUILD_DIR` (default `build/mull`) adjust the run. `MULL_EXCLUDE` leaves test files out of
`tests_for` (space-separated names). The baseline without the `mull_gaps_*` cases is:

```bash
MULL_EXCLUDE="mull_gaps_string_test mull_gaps_math_test mull_gaps_serializer_test" tools/mull/run.sh run
```

The plugin reads `mull.yml` when each file is compiled. After `mull.yml` changes, delete `build/mull` and build again. Reports go to
`build/mull/mull-report/`: `<module>.sqlite`, the IDE-style `<module>.txt` of survivors, and the
runner's log.

Times on a 16-thread machine: build about 3 minutes, string under 5 s, collections about 5 s,
math about 1 minute. Serializer takes about 15 minutes, because its tests take about 11 s per run
(`decode_exact_readarray_matches_per_element_reads`).

## Reading the table

```
module        mutants   killed  survived    score  not covered
```

- **mutants**: the module's mutants on lines its tests reach, as measured by the coverage of
  one plain run. Mull does not run the others.
- **score**: killed / mutants.
- **not covered**: mutants on lines none of the module's tests reach (from the `--dry-run
  --include-not-covered` census). Most are in retail bodies these tests never call, such as
  per-type Array specialisations and write-side stream paths.

A survivor needs one of two answers. Either a test that kills it (a new case in
`tests/unit/`; check it with `run.sh run <module>`), or a reason it is equivalent. Common
equivalent shapes in these files:

- A boundary flip where both sides take the same action at equality. Examples: `x > m ? x : m`
  as `>=`, or a `< 0` clamp to 0.
- A `+`/`-` flip on a term that is always zero, such as `x * 0.0f`, or a coordinate the branch
  pins to 0.
- A loop bound read one past the end, when that slot holds the terminator.
- A change that only moves an instrumentation counter (`g_sc`, `g_pc`, `g_swBranch`, the
  SetSize profile).
- A change whose only effect is memory safety: an allocation that is too small, or a read or
  write one past the end. No assertion can see it. It is killed under the sanitizer builds
  (`BANISHED_SANITIZE=asan`), not by this run.
