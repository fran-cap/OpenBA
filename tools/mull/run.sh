#!/usr/bin/env bash
# tools/mull/run.sh -- mutation testing of the engine cores with Mull (LLVM 19), Linux / WSL only.
# Not a CI job: a by-hand measure of how much of the serializer / collection / math / string code the
# unit suite actually checks. README.md beside this file has the setup and how to read the output.
#
#   tools/mull/run.sh build                 # configure + build build/mull/unit with the Mull plugin
#   tools/mull/run.sh run [MODULE...]       # mull-runner over the module's tests (default: all four)
#   tools/mull/run.sh summary               # per-module mutants / killed / survived / score table
#   tools/mull/run.sh survivors [MODULE...] # the surviving mutants, one per line (path:line:col)
#
# MODULE is serializer, collections, math or string. Run from the repository root. Environment:
# MULL_WORKERS (default: nproc), MULL_TIMEOUT (ms per test run, default 60000), BUILD_JOBS (default 8),
# MULL_BUILD_DIR (default build/mull), MULL_EXCLUDE (test files left out of tests_for, space-separated,
# e.g. "mull_gaps_string_test mull_gaps_math_test mull_gaps_serializer_test" for the before-gaps baseline).
set -u
cd "$(dirname "$0")/../.." || exit 1

MULL_VERSION=19
PLUGIN=/usr/lib/mull-ir-frontend-${MULL_VERSION}
RUNNER=mull-runner-${MULL_VERSION}
B=${MULL_BUILD_DIR:-build/mull}
JOBS=${BUILD_JOBS:-8}
WORKERS=${MULL_WORKERS:-$(nproc)}
TIMEOUT=${MULL_TIMEOUT:-60000}
EXCLUDE=" ${MULL_EXCLUDE:-} "
REPORTS=$B/mull-report

# The tests each module is measured with: the test files whose subject is that module's code. Every
# UNIT_TEST in these files is passed to unit as a --filter (main.cpp ORs repeated filters).
tests_for() {
  case "$1" in
    serializer)  echo decode_exact_test compressed_stream_bounds_test package_file_read_test \
                      save_metadata_hardening_test string_codec_hardening_test io_streams_abi_test \
                      b39_io_classes_test review_system_misc_test mull_gaps_serializer_test ;;
    collections) echo array_test list_test set_test set_ported_test setsize_fast_test ptr_index_test \
                      msvc_sort_skeleton_test review_engine_ub_test ;;
    math)        echo math_test frustum_cull_test terrain_ray_test mull_gaps_math_test ;;
    string)      echo string_test filename_test review_engine_ub_test string_codec_hardening_test \
                      mull_gaps_string_test ;;
    *) echo "unknown module $1 (serializer|collections|math|string)" >&2; return 1 ;;
  esac
}

# The source files a module's mutants live in (a regex over the reported path).
files_for() {
  case "$1" in
    serializer)  echo 'src/engine/io/(serialize_family|compressed_stream_read|compressed_stream_open|memory_stream|text_codec)\.cpp' ;;
    collections) echo 'src/engine/collection/' ;;
    math)        echo 'src/engine/math/' ;;
    string)      echo 'src/engine/system/string\.cpp' ;;
  esac
}

filters_for() {
  local f
  for f in $(tests_for "$1"); do
    [ -f "tests/unit/$f.cpp" ] || continue
    case "$EXCLUDE" in *" $f "*) continue ;; esac
    sed -n 's/^UNIT_TEST(\([A-Za-z0-9_]*\)).*/--filter \1/p' "tests/unit/$f.cpp"
  done
}

build() {
  command -v clang++-${MULL_VERSION} >/dev/null || { echo "clang++-${MULL_VERSION} not found" >&2; exit 1; }
  [ -f "$PLUGIN" ] || { echo "$PLUGIN not found: install mull-${MULL_VERSION} (README.md)" >&2; exit 1; }
  local flags="-fpass-plugin=$PLUGIN -g -grecord-command-line -fprofile-instr-generate -fcoverage-mapping"
  # The plugin reads the config when each file is compiled; a changed mull.yml needs a clean build.
  export MULL_CONFIG=$PWD/tools/mull/mull.yml
  cmake -S . -B "$B" -G Ninja -DBANISHED_FLAVOUR=debug \
    -DCMAKE_C_COMPILER=clang-${MULL_VERSION} -DCMAKE_CXX_COMPILER=clang++-${MULL_VERSION} \
    -DBANISHED_USE_CCACHE=OFF -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DCMAKE_CXX_FLAGS="$flags" -DCMAKE_EXE_LINKER_FLAGS="-fprofile-instr-generate" || exit 1
  cmake --build "$B" -j "$JOBS" -t unit || exit 1
}

run_module() {
  local m=$1
  local filters
  filters=$(filters_for "$m") || exit 1
  mkdir -p "$REPORTS"
  # The SQLite reporter appends to an existing database: start each run from an empty one.
  rm -f "$REPORTS/$m.sqlite"
  echo "== $m: $(echo "$filters" | grep -c -- --filter) tests"
  # shellcheck disable=SC2086
  # --timeout: the warm-up run's limit too; the serializer selection takes ~12 s (decode_exact).
  "$RUNNER" --workers "$WORKERS" --timeout "$TIMEOUT" --allow-surviving --no-mutant-output \
    --reporters IDE --reporters SQLite --report-dir "$REPORTS" --report-name "$m" \
    "$B/unit" $filters > "$REPORTS/$m.log" 2>&1
  grep -E '^\[(info|warning|error)\]' "$REPORTS/$m.log"
}

# Every mutant the plugin compiled in, reached by a test or not (a dry run: no mutant is executed).
# The warm-up run still runs the program once, so it gets a filter that matches no test.
census() {
  mkdir -p "$REPORTS"
  rm -f "$REPORTS/census.sqlite"
  "$RUNNER" --dry-run --include-not-covered --allow-surviving --no-output \
    --reporters SQLite --report-dir "$REPORTS" --report-name census "$B/unit" --filter mull_census_no_test \
    > "$REPORTS/census.log" 2>&1
}

summary() {
  local m
  printf '%-12s %8s %8s %9s %8s %12s\n' module mutants killed survived score 'not covered'
  for m in serializer collections math string; do
    [ -f "$REPORTS/$m.sqlite" ] || continue
    python3 tools/mull/summary.py "$REPORTS/$m.sqlite" "$(files_for "$m")" "$m" "$REPORTS/census.sqlite"
  done
}

survivors() {
  local m
  for m in "$@"; do
    echo "== $m"
    python3 tools/mull/summary.py --survivors "$REPORTS/$m.sqlite" "$(files_for "$m")" "$m"
  done
}

# An unknown name would select no --filter (mull-runner then runs the whole suite) and an empty
# files_for regex (summary.py then counts every path): reject it before anything runs.
check_modules() {
  local m
  for m in "$@"; do
    tests_for "$m" >/dev/null || exit 2
  done
}

cmd=${1:-}
shift || true
case "$cmd" in
  build) build ;;
  run)
    [ $# -eq 0 ] && set -- serializer collections math string
    check_modules "$@"
    for m in "$@"; do run_module "$m"; done
    census
    summary ;;
  summary) summary ;;
  survivors)
    [ $# -eq 0 ] && set -- serializer collections math string
    check_modules "$@"
    survivors "$@" ;;
  *) sed -n '2,14p' "$0"; exit 2 ;;
esac
