#!/usr/bin/env bash
# tools/build.sh -- the one build / deploy / run entry point over the CMake build (2026-10-11).
#
# usage: tools/build.sh <command> [flavour] [--jobs N]
#   build   [flavour]   configure the platform build dir if absent, build OpenBA
#   unit    [flavour]   build the unit tests and run them (exit = the suite's exit); --shards N runs
#                       them on N parallel processes (run_unit below)
#   backend [flavour]   compile the renderer backend's own sources as OpenBA would, link nothing
#                       (target render-backend; CI's second Windows configure)
#   deploy  [flavour]   build + copy the exe to the game directory (BANISHED_DIR), then
#                       tools/ext_resources.sh --deploy (the extension .crs files, loose in WinData/Dialog/)
#   run     [flavour]   deploy + launch with BANISHED_AUTOSTART=1 and /profile:faithful (THE GATE's by-hand run)
#   run-menu [flavour]  deploy + launch, stops at the main menu
#   stress  [flavour]   build perf (default), deploy, tools/stress_run.sh --no-build $STRESS_ARGS
#   clean   [flavour]   remove the platform build dir (all flavours when none is given)
#   exe     [flavour]   print the exe path (for scripts)
#   build dirs: build/cmake-<flavour> (Windows, Linux); on macOS build/mac-<flavour>, with debug = build/mac-arm64.
#   env BANISHED_RENDER_BACKEND=vulkan picks the Vulkan backend into build/cmake-<flavour>-vulkan.
#   env BANISHED_RENDER_BACKEND=dx11+vulkan builds the dual Windows exe (both backends, the renderer picked
#               in game: src/render/common/README.md) into build/cmake-<flavour>-dx11+vulkan.
#               The default backend: dx11+vulkan on Windows (since 1.0.3), vulkan on Linux (plain build/cmake-<flavour>),
#               Metal on Darwin.
#   flavours: debug (default) | og | perf | release   -- CMakeLists.txt's BANISHED_FLAVOUR
#
# Jobs: --jobs N or BUILD_JOBS (default 8 since 2026-10-17, the user: no wide multi-agent builds any more; agents still use 2 -- memory
# feedback_build_parallelism_low_memory). Extra cmake args: CMAKE_EXTRA (e.g. "-DBANISHED_USE_CCACHE=OFF").
# Toolchain: cmake + ninja from pip on PATH; on Windows the compiler is the PINNED WinLibs GCC
# (tools/toolchain/install_winlibs.sh; docs/reference/environment.md), found through BANISHED_GCC_DIR
# (default C:/winlibs-gcc16/mingw64/bin), put first on PATH here and handed to CMake explicitly; a build dir
# configured with another compiler is wiped and reconfigured. On Linux the same script works with the
# POSIX targets (OpenBA, unit) -- no deploy / run there (tools/linux_gamedir.sh + tools/linux_run.sh do that).
set -u
CMD="${1:-build}"; shift || true
FLAV="debug"; [ "$CMD" = stress ] && FLAV=perf
FLAV_GIVEN=0
case "${1:-}" in debug|og|perf|release) FLAV="$1"; FLAV_GIVEN=1; shift;; esac
JOBS="${BUILD_JOBS:-8}"
SHARDS=1
while [ $# -gt 0 ]; do
  case "$1" in
    --jobs) JOBS="$2"; shift 2;;
    --shards) SHARDS="$2"; shift 2;;
    *) echo "unknown argument $1"; exit 2;;
  esac
done
case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) EXT=.exe;; *) EXT="";; esac
# THE game-directory variable: env BANISHED_DIR (every script under tools/ reads this one name);
# the CMake cache var BANISHED_GAME_DIR is set from it at configure time.
BANISHED_DIR="${BANISHED_DIR:-C:/GOG Games/Banished}"
B="build/cmake-$FLAV"
if [ "$(uname -s)" = Darwin ]; then
  B="build/mac-$FLAV"
  [ "$FLAV" = debug ] && B="build/mac-arm64"
fi
# The renderer backend (src/render/common/README.md): env BANISHED_RENDER_BACKEND picks a non-default
# one (vulkan; docs/vulkan/PLAN.md) and keys its own build directory, build/cmake-<flavour>-<backend>,
# so the default build is never reconfigured under it. Unset = the platform default (the dual dx11+vulkan
# exe on Windows since 1.0.3, vulkan on Linux, metal on macOS). On Windows the default is also passed
# explicitly and checked in configure(): a build/cmake-<flavour> configured before the change (cached dx11,
# with the dx11-only tweak overlay) is wiped and configured again rather than silently staying dx11.
BACKEND="${BANISHED_RENDER_BACKEND:-}"
BACKEND_CMAKE=""
WANT_BACKEND=""
if [ -n "$BACKEND" ]; then
  B="$B-$BACKEND"
  BACKEND_CMAKE="-DBANISHED_RENDER_BACKEND=$BACKEND"
  WANT_BACKEND="$BACKEND"
elif [ "$EXT" = .exe ]; then
  BACKEND_CMAKE="-DBANISHED_RENDER_BACKEND=dx11+vulkan"
  WANT_BACKEND="dx11+vulkan"
fi
# The checking builds (CMakeLists.txt BANISHED_SANITIZE, docs/reference/checking_builds.md): env
# BANISHED_SANITIZE=check|asan|ubsan|tsan|fuzz keys its own build directory, build/cmake-<flavour>[-<backend>]-<sanitize>.
SANITIZE="${BANISHED_SANITIZE:-none}"
if [ "$SANITIZE" != none ]; then
  B="$B-$SANITIZE"
  BACKEND_CMAKE="$BACKEND_CMAKE -DBANISHED_SANITIZE=$SANITIZE"
fi
EXE="$B/OpenBA$EXT"
# THE GATE RUNS FAITHFUL (docs/extensions/DESIGN.md §3 "Gates"): every fix and extension off for
# this one run, through the exe's own command line (retail's `/key:value` parser, System::Configuration),
# not settings.cfg and not an environment flag; src/ext/registry.h. run-menu (the by-hand look) does
# not pass it. MSYS2_ARG_CONV_EXCL keeps the shell from rewriting the leading `/` as a path.
FAITHFUL="/profile:faithful"

# The pinned Windows compiler (header comment). COMPILER_CMAKE carries it into every configure;
# the wipe below keeps a build dir from silently staying on the compiler it was first configured with.
COMPILER_CMAKE=""
if [ "$EXT" = .exe ]; then
  GCC_DIR="${BANISHED_GCC_DIR:-C:/winlibs-gcc16/mingw64/bin}"
  if [ ! -x "$GCC_DIR/g++.exe" ]; then
    echo "!! no g++.exe in $GCC_DIR -- run tools/toolchain/install_winlibs.sh (or set BANISHED_GCC_DIR)" >&2
    exit 1
  fi
  # On PATH in its POSIX spelling: bash splits a `C:/...` entry at the drive colon, and then the
  # LTO link's `make` (CMakeLists.txt, BANISHED_LTO) is not found and the partitions run serially.
  export PATH="$(cygpath -u "$GCC_DIR"):$PATH"
  # windres too: CMake otherwise takes whichever `windres` PATH holds at configure time (a bare name
  # that CI's ninja could not spawn; locally MSYS2's masked that). gcc-ar / gcc-ranlib: the release
  # exe is LTO (CMakeLists.txt BANISHED_LTO) and plain ar cannot index slim LTO objects in the static
  # third-party libraries; they are plain ar for everything else, so every flavour gets them.
  COMPILER_CMAKE="-DCMAKE_C_COMPILER=$GCC_DIR/gcc.exe -DCMAKE_CXX_COMPILER=$GCC_DIR/g++.exe -DCMAKE_RC_COMPILER=$GCC_DIR/windres.exe -DCMAKE_AR=$GCC_DIR/gcc-ar.exe -DCMAKE_RANLIB=$GCC_DIR/gcc-ranlib.exe"
fi

configure() {
  if [ -f "$B/build.ninja" ]; then
    if [ -n "$COMPILER_CMAKE" ] && ! { grep -q "^CMAKE_CXX_COMPILER:[A-Z]*=$GCC_DIR/g++.exe$" "$B/CMakeCache.txt" &&
                                       grep -q "^CMAKE_AR:[A-Z]*=$GCC_DIR/gcc-ar.exe$" "$B/CMakeCache.txt"; } 2>/dev/null; then
      echo "== $B was configured with another compiler / archiver; wiping it for $GCC_DIR"
      rm -rf "$B"
    elif [ -n "$WANT_BACKEND" ] && ! grep -qF "BANISHED_RENDER_BACKEND:STRING=$WANT_BACKEND" "$B/CMakeCache.txt" 2>/dev/null; then
      echo "== $B was configured with another renderer backend; wiping it for $WANT_BACKEND"
      rm -rf "$B"
    else
      return 0
    fi
  fi
  echo "== configure $B (BANISHED_FLAVOUR=$FLAV${BACKEND:+, BANISHED_RENDER_BACKEND=$BACKEND})"
  cmake -S . -B "$B" -G Ninja -DBANISHED_FLAVOUR="$FLAV" -DBANISHED_GAME_DIR="$BANISHED_DIR" $COMPILER_CMAKE $BACKEND_CMAKE ${CMAKE_EXTRA:-} || exit 1
}
build() { # targets...
  configure
  echo "== build $B: $* (-j$JOBS)"
  cmake --build "$B" -j "$JOBS" -t "$@" || exit 1
}
# The unit suite, whole or on SHARDS parallel workers. The list is cut into 8 chunks per worker
# (tests/unit/main.cpp `--shard K/N`) that the workers take in turn (xargs -P), so a chunk holding one
# of the multi-second tests does not hold up the rest. Each chunk writes its own log; they are printed
# in order, then one summary and the skips. Exit 0 only when every chunk exits 0: a chunk that dies
# prints no summary line and is reported as such.
run_unit() {
  local exe="$B/unit$EXT"
  case "$SHARDS" in ''|*[!0-9]*) echo "build.sh unit: --shards wants a number (got '$SHARDS')" >&2; return 2;; esac
  if [ "$SHARDS" -le 1 ]; then
    "$exe"
    return
  fi
  local dir="$B/unit-shards"
  local chunks=$((SHARDS * 8))
  rm -rf "$dir"
  mkdir -p "$dir"
  seq 0 $((chunks - 1)) | xargs -P "$SHARDS" -I{} sh -c \
    '"$1" --shard "$2/$3" > "$4/$2.log" 2>&1; echo $? > "$4/$2.rc"' sh "$exe" {} "$chunks" "$dir"
  local k rc=0 code line tests=0 fails=0 skips=0
  for ((k = 0; k < chunks; k++)); do
    code=$(cat "$dir/$k.rc" 2>/dev/null || echo 127)
    grep -vE '^[0-9]+ tests, [0-9]+ failures, [0-9]+ skipped$|^  skipped ' "$dir/$k.log" 2>/dev/null
    line=$(grep -E '^[0-9]+ tests, [0-9]+ failures, [0-9]+ skipped$' "$dir/$k.log" 2>/dev/null | tail -1)
    if [ -n "$line" ]; then
      set -- $line
      tests=$((tests + $1))
      fails=$((fails + $3))
      skips=$((skips + $5))
    else
      echo "!! unit chunk $k/$chunks exited $code with no summary: it died in the test after its last line above"
      [ "$code" -eq 0 ] && code=1
    fi
    [ "$code" -ne 0 ] && [ "$rc" -eq 0 ] && rc=$code
  done
  echo "$tests tests, $fails failures, $skips skipped ($SHARDS workers, $chunks chunks)"
  cat "$dir"/*.log | grep -E '^  skipped '
  echo "slowest tests:"
  cat "$dir"/*.log | grep -E '^(ok  |FAIL|skip) [A-Za-z0-9_]+ \([0-9.]+ s\)$' | sort -t'(' -k2 -rn | head -10
  return $rc
}
need_game_dir() {
  [ -d "$BANISHED_DIR" ] || { echo "build.sh $CMD: game directory not found: $BANISHED_DIR (set BANISHED_DIR to the Banished install)"; exit 1; }
}
deploy() {
  if [ "$(uname -s)" = Darwin ]; then
    echo "build.sh $CMD is not supported on macOS; use tools/macos/make_app.sh and tools/macos/run_openba.sh" >&2
    exit 2
  fi
  need_game_dir
  build OpenBA
  cp -f "$EXE" "$BANISHED_DIR/OpenBA.exe" || { echo "deploy: exe busy, is a game running?"; exit 1; }
  rm -f "$BANISHED_DIR/standalone_debug.log"
  echo "Deployed $EXE to $BANISHED_DIR/OpenBA.exe"
  # The extension resources (the Extensions dialog, the Enhanced tab, the sprites, the sharp_ui
  # fonts) are built inside the exe (docs/extensions/KITFREE.md); nothing loose is deployed.
  # tools/ext_resources.sh remains only as the cross-check oracle (the kit's compiled bytes).
}

case "$CMD" in
  exe)     echo "$EXE";;
  build)   build OpenBA;;
  unit)    build unit; run_unit;;
  backend) build render-backend;;
  deploy)  deploy;;
  run)     deploy; ( cd "$BANISHED_DIR" && BANISHED_AUTOSTART=1 MSYS2_ARG_CONV_EXCL="$FAITHFUL" start OpenBA.exe $FAITHFUL )
           echo "Launched (autostart, $FAITHFUL)";;
  run-menu) deploy; ( cd "$BANISHED_DIR" && start OpenBA.exe ); echo "Launched (menu only)";;
  stress)  if [ "$(uname -s)" = Darwin ]; then
             echo "macOS stress harness is not part of this repository; run the stress town directly with BANISHED_STRESS=1 (see perf/README.md)" >&2
             exit 2
           else deploy; fi
           tools/stress_run.sh --no-build ${STRESS_ARGS:-};;
  clean)   if [ "$FLAV_GIVEN" = 1 ]; then rm -rf "$B"; echo "removed $B"
           elif [ "$(uname -s)" = Darwin ]; then
             rm -rf build/mac-*
             echo "removed build/mac-*"
           else rm -rf build/cmake-debug build/cmake-og build/cmake-perf build/cmake-release build/cmake-*-vulkan; echo "removed build/cmake-{debug,og,perf,release}[-vulkan]"; fi;;
  *)
    echo "unknown command $CMD"
    sed -n '2,19p' "$0"
    exit 2
    ;;
esac
