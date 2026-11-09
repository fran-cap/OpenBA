# Banished x64 reimplementation -- CMake build (Phase D, 2026-10-10).
#
# THE build since 2026-10-11: the gate kit (docs/runtime_port/phase_d/*.sh), the stress runner and
# the resave gate call tools/build.sh, which configures and builds here. The Makefile that mirrored
# these flavours was deleted on 2026-10-11. Configure one build directory per flavour:
#
#   cmake -S . -B build/cmake-debug -G Ninja -DBANISHED_FLAVOUR=debug
#   cmake -S . -B build/cmake-perf  -G Ninja -DBANISHED_FLAVOUR=perf
#   cmake --build build/cmake-debug            # -> build/cmake-debug/OpenBA.exe
#   cmake --build build/cmake-debug -t unit    # host unit tests (build/cmake-debug/unit.exe)
#
# Flavours (the old Makefile target in brackets, for the history):
#   debug   [standalone-debug]  -O0 -g -D_DEBUG, logs kept
#   og      [standalone-og]     -Og -g -D_DEBUG, logs kept
#   perf    [standalone-perf]   -O2 -g -DNDEBUG, logs stripped (the perf baselines' build)
#   release [standalone]        -O3 -DNDEBUG, logs stripped; on Windows and Android also PGO + LTO (below)
#
# Off Windows (Phase D.4 POSIX + LINUX-1; handins/LINUX-1.md has the recipe) the targets are
# `OpenBA` (the Linux game over src/platform/posix + sdl and the Vulkan backend, the default
# there since Vulkan P5) and `unit` (the host unit tests over the src/platform/posix twins).
# src/platform/win and src/diag/win are never built there. (`linux-check`, the compile-only
# dry run of POSIX, went when the exe linked: the exe builds every TU it did.)
#   cmake -S . -B build/linux -G Ninja -DBANISHED_FLAVOUR=debug
#   cmake --build build/linux -t unit && build/linux/unit
#   cmake --build build/linux -t OpenBA
cmake_minimum_required(VERSION 3.20)
project(banished_reimpl VERSION 1.0.2 LANGUAGES CXX)
# OpenBA version shown beside the retail version in the main / pause menu's BuildId
# overlay (src/game/Dialog/mainmenudialog.cpp). Bump here to cut a release (README "Versioning").
add_compile_definitions(OPENBA_VERSION="${PROJECT_VERSION}")
if(WIN32)
  enable_language(RC)   # src/app/icon.rc
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

# compile_commands.json in every build dir: clang-tidy, the Clang Static Analyzer and CodeQL read it
# (docs/history/review_20261103/README.md).
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(BANISHED_FLAVOUR "debug" CACHE STRING "debug | og | perf | release")
set_property(CACHE BANISHED_FLAVOUR PROPERTY STRINGS debug og perf release)
option(BANISHED_USE_CCACHE "Compile through ccache when it is on PATH" ON)
set(BANISHED_GAME_DIR "C:/GOG Games/Banished" CACHE PATH "where sa-deploy copies the exe")
# The debug tweak overlay (Dear ImGui, Alt+F10; src/diag/tweak_overlay.h + src/diag/win/tweak_overlay.cpp, third_party/imgui,
# docs/reference/tweak_overlay.md): ON for the debug flavour on Windows, whatever the backend list (each
# backend renders it: src/render/dx11/tweak_overlay_dx11.cpp, src/render/vulkan/tweak_overlay_vulkan.cpp). OFF, not one source
# of it is compiled and every `#if BANISHED_TWEAK_OVERLAY` site is out, so og / perf / release build
# exactly as without it.
# The renderer backend (src/render/common/README.md): the dual dx11+vulkan exe on Windows (both backends,
# the renderer picked in game by ext.render_backend; the default since 1.0.3, maintainer ruling 2026-11-08,
# so the shipped exe carries Vulkan), metal on APPLE, vulkan on every other host (Linux, since Vulkan P5).
# A dx11-only Windows build is BANISHED_RENDER_BACKEND=dx11. The fragment itself is included further
# down, with the sources. dx11 is Windows-only: its backend.cmake answers not-ready elsewhere.
if(APPLE)
  set(BANISHED_RENDER_BACKEND_DEFAULT metal)
elseif(WIN32)
  set(BANISHED_RENDER_BACKEND_DEFAULT dx11+vulkan)
else()
  set(BANISHED_RENDER_BACKEND_DEFAULT vulkan)
endif()
set(BANISHED_RENDER_BACKEND ${BANISHED_RENDER_BACKEND_DEFAULT} CACHE STRING "Renderer backend: src/render/<name>/backend.cmake")
if(BANISHED_FLAVOUR STREQUAL "debug" AND WIN32)
  set(_banished_tweak_default ON)
else()
  set(_banished_tweak_default OFF)
endif()
option(BANISHED_TWEAK_OVERLAY "Debug tweak overlay (Dear ImGui): debug flavour on Windows" ${_banished_tweak_default})

if(BANISHED_USE_CCACHE)
  find_program(CCACHE_PROGRAM ccache)
  if(CCACHE_PROGRAM)
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
  endif()
endif()

# The three flags that are NOT tuning knobs (explained here):
#   -fno-strict-aliasing : ported bodies pun raw offsets through two lvalue types
#   -fno-lifetime-dse    : GCC treats an object as dead once its destructor starts and deletes
#                          "dead" stores to it; retail dtors null the fields they free and callers
#                          rely on it (a double free that showed only at -O2). Clang has no such
#                          dead-store rule and rejects the flag, so it is added only where the
#                          compiler knows it (check_cxx_compiler_flag), never blindly.
#   -fexceptions         : spelled out (ENDGAME B.1 decision 7); no object may be built without it
# And one for portability: -ffp-contract=off. Retail is MSVC x64 without FMA contraction; GCC in
# gnu++17 mode defaults to -ffp-contract=fast and emits fmadd on aarch64 (and on x86 with
# -march=native / FMA), which would change worldgen rounding ([WorldHash] byte-identical worlds).
include(CheckCXXCompilerFlag)
check_cxx_compiler_flag(-fno-lifetime-dse BANISHED_HAVE_NO_LIFETIME_DSE)
set(BANISHED_LIFETIME_DSE_FLAG)
if(BANISHED_HAVE_NO_LIFETIME_DSE)
  set(BANISHED_LIFETIME_DSE_FLAG -fno-lifetime-dse)
endif()
set(BANISHED_CXX_FLAGS
  -fexceptions -fno-strict-aliasing ${BANISHED_LIFETIME_DSE_FLAG} -ffp-contract=off
  -Wall -Wextra -Wno-cast-function-type -Wno-invalid-offsetof)
# Off Windows, two flags pin what MinGW gives for free (plans/ABI.md C.7): `char` is signed
# (abi.h asserts it; ARM64 Linux defaults to unsigned) and bit-fields lay out MSVC-style
# (the tree has none today; abi_audit keeps it that way).
if(NOT WIN32)
  set(BANISHED_PORTABLE_FLAGS -fsigned-char -mms-bitfields)
  list(APPEND BANISHED_CXX_FLAGS ${BANISHED_PORTABLE_FLAGS})
endif()
# Android (P12): libmain.so exports nothing but `main` (src/app/main.cpp marks it). With the default
# visibility a shared library's every cross-file call goes through the PLT and every global through
# the GOT, and its dynamic symbol table runs to tens of thousands of entries the loader relocates at
# start. Hidden is the Android norm; the crash and hang dumps still print `libmain.so + offset`,
# which the unstripped library symbolises (docs/android/README.md). The vendored C (SDL, wmadec,
# freetype) follows the same preset; SDL exports its API through its own macro.
if(ANDROID)
  set(CMAKE_C_VISIBILITY_PRESET hidden)
  set(CMAKE_CXX_VISIBILITY_PRESET hidden)
  set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
endif()
# Clang (Android, macOS): -fno-delete-null-pointer-checks. Retail calls members through a null
# `this` its callee tests first (UI::String {null, -1} -> StringTable::GetText checks the index
# before touching `this`, src/engine/ui/sprite.cpp); clang otherwise marks `this` dereferenceable and
# may hoist the callee's field load above that test. The instrumented Android build did exactly
# that and crashed at the main menu (perf/pgo/android/README.md). GCC has not been seen to do it;
# its flags are left as measured.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  list(APPEND BANISHED_CXX_FLAGS -fno-delete-null-pointer-checks)
endif()
# BANISHED_THREAD_SAFETY=ON (clang only): clang's thread-safety analysis over the annotations of
# src/engine/system/thread_annotations.h, a broken lock contract failing the build. OpenBA and unit.
# GCC and MSVC ignore the annotations (the macros are empty there), so the option has nothing to
# check on them. The Linux check: CMAKE_EXTRA="-DBANISHED_THREAD_SAFETY=ON" with clang.
option(BANISHED_THREAD_SAFETY "clang: -Wthread-safety, a lock contract broken is an error" OFF)
set(THREAD_SAFETY_FLAGS)
if(BANISHED_THREAD_SAFETY)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "BANISHED_THREAD_SAFETY needs clang (got ${CMAKE_CXX_COMPILER_ID})")
  endif()
  set(THREAD_SAFETY_FLAGS -Wthread-safety -Werror=thread-safety-analysis)
  list(APPEND BANISHED_CXX_FLAGS ${THREAD_SAFETY_FLAGS})
endif()
# (SHELL: keeps each `-include <file>` pair whole: CMake de-duplicates repeated option tokens,
# which with two force-includes drops the second `-include` and leaves its path a bare input.)
set(BANISHED_STRIP_LOGS -DBANISHED_STRIP_LOGS "SHELL:-include ${CMAKE_SOURCE_DIR}/src/app/log_strip.h")
# One write per diagnostic line on the unbuffered stdout / stderr (docs/extensions/handins/HITCH_STDERR.md):
# fprintf / vfprintf / printf to those two streams format into a stack buffer and fwrite once
# (MinGW's printf family writes an unbuffered stream one WriteFile per character). Every flavour,
# OpenBA and unit; MinGW only (the header is empty elsewhere). Vendored third-party sources opt
# out with BANISHED_THIRD_PARTY_SOURCE (ASSAO and Dear ImGui, below).
set(BANISHED_ONE_WRITE_STDIO "SHELL:-include ${CMAKE_SOURCE_DIR}/src/app/one_write_stdio.h")

if(BANISHED_FLAVOUR STREQUAL "debug")
  set(FLAVOUR_FLAGS -g -O0 -D_DEBUG)
elseif(BANISHED_FLAVOUR STREQUAL "og")
  set(FLAVOUR_FLAGS -Og -g -D_DEBUG)
elseif(BANISHED_FLAVOUR STREQUAL "perf")
  # (No PerfX master any more: PerfX is ext.render_culling / ext.anim_batch in every
  # flavour, docs/extensions/handins/PERFXEXT.md.)
  set(FLAVOUR_FLAGS -O2 -g -DNDEBUG ${BANISHED_STRIP_LOGS})
elseif(BANISHED_FLAVOUR STREQUAL "release")
  set(FLAVOUR_FLAGS -O3 -DNDEBUG ${BANISHED_STRIP_LOGS})
else()
  message(FATAL_ERROR "BANISHED_FLAVOUR must be debug, og, perf or release (got '${BANISHED_FLAVOUR}')")
endif()

# Checking builds (docs/reference/checking_builds.md), on top of any flavour; tools/build.sh keys
# their own build directory from env BANISHED_SANITIZE (build/cmake-<flavour>-<sanitize>):
#   none   (default) nothing added
#   check  libstdc++ / libc++ assertions (out-of-range operator[], bad iterators) and every
#          uninitialised local filled with a pattern, so an uninitialised read is deterministic
#   asan   AddressSanitizer + UBSan (the engine heap reports to ASan: src/engine/system/allocator.cpp)
#   ubsan  UBSan alone
#   tsan   ThreadSanitizer (not with asan; Linux only)
#   fuzz   asan + ubsan + libFuzzer instrumentation (clang; the tests/fuzz targets)
# Every sanitizer aborts on its first report (-fno-sanitize-recover): a report is a failed run.
# UBSan's vptr and alignment checks are off: the binary-compatible type headers reinterpret raw
# memory on purpose (.claude/rules/type-headers.md); they are enabled by hand for a sweep.
set(BANISHED_SANITIZE "none" CACHE STRING "none | check | asan | ubsan | tsan | fuzz")
set_property(CACHE BANISHED_SANITIZE PROPERTY STRINGS none check asan ubsan tsan fuzz)
set(BANISHED_UBSAN_CHECKS
  "bounds,null,shift,return,unreachable,vla-bound,float-cast-overflow,integer-divide-by-zero,signed-integer-overflow,bool,enum,builtin,pointer-overflow,nonnull-attribute,returns-nonnull-attribute")
# BANISHED_SANITIZE_RECOVER=ON: a survey run that reports every finding instead of stopping at the
# first (UBSan continues; ASan continues with ASAN_OPTIONS=halt_on_error=0).
option(BANISHED_SANITIZE_RECOVER "checking builds: report and continue (survey) instead of aborting" OFF)
set(BANISHED_SANITIZE_STOP -fno-sanitize-recover=all)
if(BANISHED_SANITIZE_RECOVER)
  set(BANISHED_SANITIZE_STOP -fsanitize-recover=all)
endif()
set(SANITIZE_FLAGS)
set(SANITIZE_LINK)
if(BANISHED_SANITIZE STREQUAL "check")
  set(SANITIZE_FLAGS -D_GLIBCXX_ASSERTIONS -D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_EXTENSIVE
    -ftrivial-auto-var-init=pattern)
elseif(BANISHED_SANITIZE STREQUAL "asan")
  set(SANITIZE_FLAGS -fsanitize=address,${BANISHED_UBSAN_CHECKS} ${BANISHED_SANITIZE_STOP}
    -fno-omit-frame-pointer -D_GLIBCXX_ASSERTIONS)
  set(SANITIZE_LINK -fsanitize=address,${BANISHED_UBSAN_CHECKS})
elseif(BANISHED_SANITIZE STREQUAL "ubsan")
  set(SANITIZE_FLAGS -fsanitize=${BANISHED_UBSAN_CHECKS} ${BANISHED_SANITIZE_STOP} -fno-omit-frame-pointer)
  set(SANITIZE_LINK -fsanitize=${BANISHED_UBSAN_CHECKS})
elseif(BANISHED_SANITIZE STREQUAL "tsan")
  set(SANITIZE_FLAGS -fsanitize=thread -fno-omit-frame-pointer)
  set(SANITIZE_LINK -fsanitize=thread)
elseif(BANISHED_SANITIZE STREQUAL "fuzz")
  set(SANITIZE_FLAGS -fsanitize=fuzzer-no-link,address,${BANISHED_UBSAN_CHECKS} ${BANISHED_SANITIZE_STOP}
    -fno-omit-frame-pointer -D_GLIBCXX_ASSERTIONS)
  set(SANITIZE_LINK -fsanitize=address,${BANISHED_UBSAN_CHECKS})
elseif(NOT BANISHED_SANITIZE STREQUAL "none")
  message(FATAL_ERROR "BANISHED_SANITIZE must be none, check, asan, ubsan, tsan or fuzz (got '${BANISHED_SANITIZE}')")
endif()
list(APPEND FLAVOUR_FLAGS ${SANITIZE_FLAGS})

# ---------------------------------------------------------------------------------------
# Profile-guided optimisation (perf/pgo/README.md). BANISHED_PGO:
#   auto      (default) release flavour on Windows: `use` when BANISHED_PGO_DIR holds a profile set;
#             release flavour on Android: `use` when BANISHED_PGO_PROFDATA exists (perf/pgo/android/
#             README.md); otherwise, and in every other flavour / platform: off
#   generate  instrument OpenBA; the training runs (perf/pgo/train.sh; tools/android.sh on the
#             phone) write the profile
#   use       compile OpenBA against the profile set in BANISHED_PGO_DIR (GCC) or the merged
#             BANISHED_PGO_PROFDATA (clang)
#   off       never
# GCC (Windows): the profile set is keyed on each object's path RELATIVE to the build dir
# (-fprofile-prefix-path), so it is written by one build dir and read by another (CI's), on the one
# pinned compiler (tools/toolchain/install_winlibs.sh): GCC refuses a profile written by a different
# version. A profile older than a function it describes is a warning (coverage-mismatch), not an
# error: releases are infrequent and retrain (train.sh) every time, drift between them is accepted.
# Clang (Android, the NDK): IR instrumentation (-fprofile-generate); the device writes one .profraw
# per run into the app's data folder (src/platform/android/app_android.cpp), tools/android.sh
# pgo-merge pulls and merges them with the NDK's llvm-profdata into the one .profdata the `use` build
# reads. A stale or missing record is a warning there too (profile-instr-unprofiled / -out-of-date).
# ---------------------------------------------------------------------------------------
set(BANISHED_PGO "auto" CACHE STRING "auto | generate | use | off (perf/pgo/README.md)")
set_property(CACHE BANISHED_PGO PROPERTY STRINGS auto generate use off)
set(BANISHED_PGO_DIR "${CMAKE_SOURCE_DIR}/perf/pgo/profile" CACHE PATH "the .gcda profile set (GCC)")
set(BANISHED_PGO_PROFDATA "${CMAKE_SOURCE_DIR}/perf/pgo/android/openba.profdata" CACHE FILEPATH
    "the merged clang profile (Android)")
set(PGO_MODE "${BANISHED_PGO}")
if(PGO_MODE STREQUAL "auto")
  file(GLOB _pgo_set "${BANISHED_PGO_DIR}/*.gcda")
  if(WIN32 AND BANISHED_FLAVOUR STREQUAL "release" AND _pgo_set)
    set(PGO_MODE "use")
  elseif(ANDROID AND BANISHED_FLAVOUR STREQUAL "release" AND EXISTS "${BANISHED_PGO_PROFDATA}")
    set(PGO_MODE "use")
  else()
    set(PGO_MODE "off")
  endif()
endif()
set(PGO_FLAGS)
set(PGO_LINK_FLAGS)
# GCC compares the prefix against the object path as the compiler sees it: the build dir from
# getcwd() in the platform's native spelling (backslashes on Windows), a plain string match.
file(TO_NATIVE_PATH "${CMAKE_BINARY_DIR}" PGO_PREFIX)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  if(PGO_MODE STREQUAL "generate")
    set(PGO_FLAGS -fprofile-generate)
    set(PGO_LINK_FLAGS -fprofile-generate)
    message(STATUS "PGO: generate (clang IR instrumentation; the device writes the .profraw files)")
  elseif(PGO_MODE STREQUAL "use")
    if(NOT EXISTS "${BANISHED_PGO_PROFDATA}")
      message(FATAL_ERROR "BANISHED_PGO=use: no profile at ${BANISHED_PGO_PROFDATA} (tools/android.sh pgo-merge)")
    endif()
    set(PGO_FLAGS "-fprofile-use=${BANISHED_PGO_PROFDATA}" -Wno-profile-instr-unprofiled
        -Wno-profile-instr-out-of-date -Wno-backend-plugin)
    message(STATUS "PGO: use <- ${BANISHED_PGO_PROFDATA}")
  elseif(NOT PGO_MODE STREQUAL "off")
    message(FATAL_ERROR "BANISHED_PGO must be auto, generate, use or off (got '${BANISHED_PGO}')")
  endif()
elseif(PGO_MODE STREQUAL "generate")
  set(PGO_FLAGS -fprofile-generate -fprofile-update=atomic
      "-fprofile-dir=${BANISHED_PGO_DIR}" "-fprofile-prefix-path=${PGO_PREFIX}")
  set(PGO_LINK_FLAGS -fprofile-generate)
  message(STATUS "PGO: generate -> ${BANISHED_PGO_DIR}")
elseif(PGO_MODE STREQUAL "use")
  set(PGO_FLAGS -fprofile-use -fprofile-correction -Wno-missing-profile -Wno-error=coverage-mismatch
      "-fprofile-dir=${BANISHED_PGO_DIR}" "-fprofile-prefix-path=${PGO_PREFIX}")
  message(STATUS "PGO: use <- ${BANISHED_PGO_DIR}")
elseif(NOT PGO_MODE STREQUAL "off")
  message(FATAL_ERROR "BANISHED_PGO must be auto, generate, use or off (got '${BANISHED_PGO}')")
endif()
# Link-time optimisation, the Windows release exe only (BANISHED_LTO, default ON there). Measured
# 2026-10-26 (perf/pgo/README.md): on top of the profile set about -7% sim / -5% draw and a 14% smaller
# exe; without the profile set LTO alone is no faster than plain -O3. Never with `generate`: the
# profile set is recorded by the non-LTO instrumented build and read by this one (the per-function
# records match; 0 coverage-mismatch). The static libraries must then be archived by gcc-ar / gcc-ranlib
# (tools/build.sh passes them: plain ar does not index slim LTO objects). Off on the other
# platforms until their release builds are measured.
# Android (P12): the same rule for libmain.so, as ThinLTO (lld, the NDK's clang); the static
# third-party libraries stay plain objects, which lld links beside the bitcode as usual.
option(BANISHED_LTO "Link-time optimisation on the Windows release exe and the Android release library" ON)
set(LTO_FLAGS)
if(ANDROID AND BANISHED_LTO AND BANISHED_FLAVOUR STREQUAL "release" AND NOT PGO_MODE STREQUAL "generate")
  set(LTO_FLAGS -flto=thin)
  message(STATUS "LTO: on (thin, release libmain.so)")
elseif(WIN32 AND BANISHED_LTO AND BANISHED_FLAVOUR STREQUAL "release" AND NOT PGO_MODE STREQUAL "generate")
  set(LTO_FLAGS -flto=8 -fno-fat-lto-objects)
  message(STATUS "LTO: on (release exe)")
  # The parallel LTRANS stage needs a `make` on PATH (tools/toolchain/install_winlibs.sh
  # provides it); without one GCC runs the partitions serially and the link takes ~1.5 min
  # instead of ~10 s.
endif()

# ---------------------------------------------------------------------------------------
# Sources: the tree mirrors the original source layout; every directory is globbed.
# CONFIGURE_DEPENDS re-globs on build so a new file needs no edit here.
# ---------------------------------------------------------------------------------------
file(GLOB_RECURSE GAME_SRCS     CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/game/*.cpp)
file(GLOB_RECURSE ENGINE_SRCS   CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/engine/*.cpp)
# src/ext: the fixes and extensions (docs/extensions/DESIGN.md §3), the only home of non-faithful
# code; built into OpenBA with the game sources (both platforms).
file(GLOB_RECURSE EXT_SRCS      CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/ext/*.cpp)
list(APPEND GAME_SRCS ${EXT_SRCS})
# The extension UI resources are built from OUR .rsc text inside the exe (docs/extensions/
# KITFREE.md, block RSCTEXT): tools/ext_rsc_embed.py turns the sources under src/ext/ui/ into
# one generated header of string literals, <build>/gen/ext_rsc_text.h, read by
# src/ext/resources/resources.cpp. Target ext-rsc-text; both OpenBA and unit depend on it.
set(EXT_RSC_SOURCES
  ${CMAKE_SOURCE_DIR}/src/ext/ui/Dialog/Extensions.rsc
  ${CMAKE_SOURCE_DIR}/src/ext/ui/Dialog/EnhancedOptions.rsc
  ${CMAKE_SOURCE_DIR}/src/ext/ui/Dialog/ExtensionsStringTable.rsc
  ${CMAKE_SOURCE_DIR}/src/ext/ui/Dialog/ExtensionsWindow.rsc
  ${CMAKE_SOURCE_DIR}/src/ext/ui/Dialog/RestrictedFoodsPage.rsc
  ${CMAKE_SOURCE_DIR}/src/ext/ui/Dialog/ItemPriorityPage.rsc
  ${CMAKE_SOURCE_DIR}/src/ext/ui/Dialog/ItemLimitsPanel.rsc
  ${CMAKE_SOURCE_DIR}/src/ext/ui/Dialog/TerrainEditPage.rsc
  ${CMAKE_SOURCE_DIR}/src/ext/ui/Dialog/TouchControls.rsc
  ${CMAKE_SOURCE_DIR}/src/ext/ui/Dialog/TouchPointer.rsc
  ${CMAKE_SOURCE_DIR}/src/ext/ui/Dialog/PhoneScroll.rsc
  ${CMAKE_SOURCE_DIR}/src/ext/ui/Dialog/TownScreenshotPage.rsc
  ${CMAKE_SOURCE_DIR}/src/ext/ui/Dialog/FamilyTreePage.rsc
  ${CMAKE_SOURCE_DIR}/src/ext/ui/Dialog/FamilyTreeWindow.rsc
  ${CMAKE_SOURCE_DIR}/src/ext/ui/Dialog/FamilyTreeSummary.rsc
  ${CMAKE_SOURCE_DIR}/src/ext/ui/Dialog/ShowGridPage.rsc
  ${CMAKE_SOURCE_DIR}/src/ext/ui/Dialog/FpsCounterPage.rsc
  ${CMAKE_SOURCE_DIR}/src/ext/ui/Dialog/BuildingFinderPage.rsc
  ${CMAKE_SOURCE_DIR}/src/ext/ui/Dialog/FavoritesWindow.rsc
  ${CMAKE_SOURCE_DIR}/src/ext/ui/Dialog/FullscreenMode.rsc
  ${CMAKE_SOURCE_DIR}/src/ext/ui/Dialog/MapOverlays.rsc
  ${CMAKE_SOURCE_DIR}/src/ext/ui/Dialog/ProfessionsWindow.rsc)
set(EXT_RSC_GEN_DIR ${CMAKE_BINARY_DIR}/gen)
set(EXT_RSC_GEN ${EXT_RSC_GEN_DIR}/ext_rsc_text.h)
find_package(Python3 COMPONENTS Interpreter QUIET)
if(NOT Python3_Interpreter_FOUND)
  message(FATAL_ERROR "python3 is needed to embed src/ext/ui/*.rsc (tools/ext_rsc_embed.py)")
endif()
add_custom_command(OUTPUT ${EXT_RSC_GEN}
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/ext_rsc_embed.py ${EXT_RSC_GEN} ${EXT_RSC_SOURCES}
  DEPENDS ${CMAKE_SOURCE_DIR}/tools/ext_rsc_embed.py ${EXT_RSC_SOURCES}
  COMMENT "ext_rsc_embed: src/ext/ui/*.rsc -> gen/ext_rsc_text.h")
add_custom_target(ext-rsc-text DEPENDS ${EXT_RSC_GEN})
# gen/openba_build_info.h: the git commit + build date shown beside OPENBA_VERSION in the
# main menu's build line (README "Versioning"). Regenerated on every build, relinks only
# when the commit changes (cmake/build_info.cmake).
add_custom_target(openba-build-info
  COMMAND ${CMAKE_COMMAND} -DOUT=${EXT_RSC_GEN_DIR}/openba_build_info.h -DSRC=${CMAKE_SOURCE_DIR}
          -P ${CMAKE_SOURCE_DIR}/cmake/build_info.cmake
  BYPRODUCTS ${EXT_RSC_GEN_DIR}/openba_build_info.h
  COMMENT "build_info: git commit -> gen/openba_build_info.h")
if(WIN32)
  file(GLOB_RECURSE PLATFORM_SRCS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/platform/win/*.cpp
                                                    ${CMAKE_SOURCE_DIR}/src/platform/common/*.cpp
                                                    ${CMAKE_SOURCE_DIR}/src/platform/sdl/*.cpp)
else()
  # D4_RULES C1: posix/ is globbed only off Windows. (src/platform/stub, the link stubs for
  # seam functions with no POSIX body, is gone: its one PORT GAP, the CRT's _purecall, is the
  # POSIX crash handler's since Phase D.4 LINUX-1, and the unit's own in tests/unit.)
  file(GLOB_RECURSE PLATFORM_SRCS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/platform/posix/*.cpp
                                                    ${CMAKE_SOURCE_DIR}/src/platform/common/*.cpp
                                                    ${CMAKE_SOURCE_DIR}/src/platform/sdl/*.cpp)
  if(APPLE)
    # src/platform/darwin/README.md: a darwin/<name>_darwin.cpp replaces posix/<name>_posix.cpp
    # whole-file; a darwin file with no posix twin is simply added.
    file(GLOB PLATFORM_DARWIN_SRCS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/platform/darwin/*.cpp)
    foreach(darwin_src ${PLATFORM_DARWIN_SRCS})
      get_filename_component(darwin_name ${darwin_src} NAME)
      string(REGEX REPLACE "_darwin\\.cpp$" "_posix.cpp" posix_name ${darwin_name})
      list(REMOVE_ITEM PLATFORM_SRCS ${CMAKE_SOURCE_DIR}/src/platform/posix/${posix_name})
    endforeach()
    list(APPEND PLATFORM_SRCS ${PLATFORM_DARWIN_SRCS})
  elseif(ANDROID)
    # src/platform/android/README.md: the darwin rule with the suffix _android. An
    # android/<name>_android.cpp replaces posix/<name>_posix.cpp whole-file; an android file with
    # no posix twin of the same <name> is simply added.
    file(GLOB PLATFORM_ANDROID_SRCS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/platform/android/*.cpp)
    foreach(android_src ${PLATFORM_ANDROID_SRCS})
      get_filename_component(android_name ${android_src} NAME)
      string(REGEX REPLACE "_android\\.cpp$" "" android_stem ${android_name})
      list(REMOVE_ITEM PLATFORM_SRCS ${CMAKE_SOURCE_DIR}/src/platform/posix/${android_stem}_posix.cpp)
    endforeach()
    list(APPEND PLATFORM_SRCS ${PLATFORM_ANDROID_SRCS})
  endif()
endif()
# The renderer backend: src/render/${BANISHED_RENDER_BACKEND}/backend.cmake sets RENDER_SRCS (its own
# sources), RENDER_BACKEND_INCLUDES and RENDER_BACKEND_READY, and its per-source properties
# (src/render/common/README.md). The choice (BANISHED_RENDER_BACKEND) is made near the top.
# src/render/common is globbed here, not in the fragment: every backend links it, and the unit
# tests and the extensions include it whichever backend is picked.
#
# SEVERAL BACKENDS IN ONE EXE (src/render/common/README.md): BANISHED_RENDER_BACKEND may name a
# '+'-separated list, `dx11+vulkan` (the dual Windows exe; tools/build.sh keys its own build directory).
# Each backend's fragment is included in turn; each backend's sources get its include directories and
# BANISHED_RENDER_IMPL_NS=impl_<name> (render_impl.h: its definitions of the backend-neutral entry points
# go into an inline namespace of that name, so the two sets do not collide), and
# src/render/common/multi/ defines the public names as forwards to the picked backend. A one-backend build
# compiles src/render/common/single/ instead and leaves every backend source as it was.
string(REPLACE "+" ";" BANISHED_RENDER_BACKENDS "${BANISHED_RENDER_BACKEND}")
list(LENGTH BANISHED_RENDER_BACKENDS BANISHED_RENDER_BACKEND_COUNT)
set(_render_all_srcs)
set(_render_all_link)
set(_render_all_unit_srcs)
set(_render_all_unit_libs)
set(_render_all_ready ON)
foreach(_render_be ${BANISHED_RENDER_BACKENDS})
  if(NOT EXISTS ${CMAKE_SOURCE_DIR}/src/render/${_render_be}/backend.cmake)
    message(FATAL_ERROR "BANISHED_RENDER_BACKEND=${BANISHED_RENDER_BACKEND}: no src/render/${_render_be}/backend.cmake")
  endif()
  set(RENDER_SRCS)
  set(RENDER_BACKEND_INCLUDES)
  set(RENDER_BACKEND_READY OFF)
  set(RENDER_BACKEND_LINK_LIBRARIES)
  set(RENDER_BACKEND_UNIT_SRCS)
  set(RENDER_BACKEND_UNIT_LIBS)
  include(${CMAKE_SOURCE_DIR}/src/render/${_render_be}/backend.cmake)
  message(STATUS "OpenBA render backend: ${_render_be} (ready: ${RENDER_BACKEND_READY})")
  if(NOT RENDER_BACKEND_READY)
    set(_render_all_ready OFF)
  endif()
  # The backend's include directories reach its own sources only (a source property), so every other
  # compile line is the same whichever backend is configured, and ccache serves the second configure's
  # objects from the first's (.github/workflows/ci.yml). Nothing outside src/render/<backend> includes a
  # backend header (src/render/common/README.md).
  if(RENDER_BACKEND_INCLUDES)
    set_property(SOURCE ${RENDER_SRCS} APPEND PROPERTY INCLUDE_DIRECTORIES ${RENDER_BACKEND_INCLUDES})
  endif()
  if(BANISHED_RENDER_BACKEND_COUNT GREATER 1)
    set_property(SOURCE ${RENDER_SRCS} APPEND PROPERTY COMPILE_DEFINITIONS BANISHED_RENDER_IMPL_NS=impl_${_render_be})
  endif()
  list(APPEND _render_all_srcs ${RENDER_SRCS})
  list(APPEND _render_all_link ${RENDER_BACKEND_LINK_LIBRARIES})
  list(APPEND _render_all_unit_srcs ${RENDER_BACKEND_UNIT_SRCS})
  list(APPEND _render_all_unit_libs ${RENDER_BACKEND_UNIT_LIBS})
endforeach()
set(RENDER_SRCS ${_render_all_srcs})
set(RENDER_BACKEND_READY ${_render_all_ready})
set(RENDER_BACKEND_LINK_LIBRARIES ${_render_all_link})
set(RENDER_BACKEND_UNIT_SRCS ${_render_all_unit_srcs})
set(RENDER_BACKEND_UNIT_LIBS ${_render_all_unit_libs})
# RENDER_BACKEND_OWN_SRCS (the target render-backend below) is the list before src/render/common joins it.
set(RENDER_BACKEND_OWN_SRCS ${RENDER_SRCS})
file(GLOB_RECURSE RENDER_COMMON_SRCS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/render/common/*.cpp)
if(BANISHED_RENDER_BACKEND_COUNT GREATER 1)
  list(FILTER RENDER_COMMON_SRCS EXCLUDE REGEX "/src/render/common/single/")
  # render_backends.inc: one BANISHED_RENDER_BACKEND(<name>) line per backend, in the list's order (the
  # fallback order), for the dispatcher.
  set(_render_backends_inc "// Generated by CMakeLists.txt from BANISHED_RENDER_BACKEND=${BANISHED_RENDER_BACKEND}.\n")
  foreach(_render_be ${BANISHED_RENDER_BACKENDS})
    string(APPEND _render_backends_inc "BANISHED_RENDER_BACKEND(${_render_be})\n")
  endforeach()
  file(WRITE ${CMAKE_BINARY_DIR}/render_gen/render_backends.inc.tmp "${_render_backends_inc}")
  configure_file(${CMAKE_BINARY_DIR}/render_gen/render_backends.inc.tmp
                 ${CMAKE_BINARY_DIR}/render_gen/render_backends.inc COPYONLY)
  set_property(SOURCE ${CMAKE_SOURCE_DIR}/src/render/common/multi/render_backend_dispatch.cpp APPEND PROPERTY
               INCLUDE_DIRECTORIES ${CMAKE_BINARY_DIR}/render_gen)
else()
  list(FILTER RENDER_COMMON_SRCS EXCLUDE REGEX "/src/render/common/multi/")
  set_property(SOURCE ${CMAKE_SOURCE_DIR}/src/render/common/single/render_backend_single.cpp APPEND PROPERTY
               COMPILE_DEFINITIONS BANISHED_RENDER_BACKEND_ID="${BANISHED_RENDER_BACKEND}")
endif()
list(APPEND RENDER_SRCS ${RENDER_COMMON_SRCS})
# ext.compress_ao (docs/extensions/handins/COMPRESSAO.md): the BC4 encoder and the derived cache's
# content hash run over ~2 GB of AO maps on a large-mod boot, on the loader's critical path. They are
# built -O2 in every flavour (debug and og included; the unit build too, as these properties are
# per source file): at -O0 the encoder does ~7 MB/s instead of ~57 MB/s per core. The BC1 / BC3
# decoder (a GPU without BC, bc_decode.h) runs over every retail texture at load, the same way, and
# so does the ETC2 transcode that replaces it where the GPU samples ETC2 (docs/android/P13_MEM.md).
# ext.sharp_ui's magnified sprite sheets (the upscale, sharp_sheet.h, and the BC3 mip chain, sharp_encode.h)
# are built at load the same way.
set_source_files_properties(${CMAKE_SOURCE_DIR}/src/render/common/bc4/bc4.cpp
                            ${CMAKE_SOURCE_DIR}/src/render/common/bc_decode/bc_decode.cpp
                            ${CMAKE_SOURCE_DIR}/src/render/common/etc2/etc2.cpp
                            ${CMAKE_SOURCE_DIR}/src/render/common/etc2/bc_to_etc2.cpp
                            ${CMAKE_SOURCE_DIR}/src/ext/derived_cache/hash64.cpp
                            ${CMAKE_SOURCE_DIR}/src/ext/sharp_ui/sharp_encode.cpp
                            ${CMAKE_SOURCE_DIR}/src/ext/sharp_ui/sharp_sheet.cpp
                            PROPERTIES COMPILE_OPTIONS "-O2")
file(GLOB         APP_SRCS      CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/app/*.cpp)
file(GLOB         DIAG_SRCS     CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/diag/*.cpp)
# src/diag/win: the Windows-only diagnostics (crash / hang handlers, watchdog, sampler,
# hardware watchpoints, redzone allocator; D4_RULES C10). Globbed only on Windows.
if(WIN32)
  file(GLOB       DIAG_WIN_SRCS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/diag/win/*.cpp)
  # The tweak overlay lives here too (Win32 + D3D11 headers: the ABI audit's rule for src/diag/win)
  # but is added only with BANISHED_TWEAK_OVERLAY, below.
  list(REMOVE_ITEM DIAG_WIN_SRCS ${CMAKE_SOURCE_DIR}/src/diag/win/tweak_overlay.cpp)
  list(APPEND DIAG_SRCS ${DIAG_WIN_SRCS})
endif()
# The tweak overlay's own sources: never globbed in, added below only with BANISHED_TWEAK_OVERLAY.
set(TWEAK_SRCS ${CMAKE_SOURCE_DIR}/src/diag/win/tweak_overlay.cpp ${CMAKE_SOURCE_DIR}/src/ext/tweak/ext_tweaks.cpp)
list(REMOVE_ITEM DIAG_SRCS ${TWEAK_SRCS})
list(REMOVE_ITEM GAME_SRCS ${TWEAK_SRCS})

set(BANISHED_INCLUDES
  ${CMAKE_SOURCE_DIR}/src/game
  ${CMAKE_SOURCE_DIR}/src/engine
  ${CMAKE_SOURCE_DIR}/src/render/common
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_SOURCE_DIR}/src/diag
  ${CMAKE_SOURCE_DIR}/src/platform/common
  ${EXT_RSC_GEN_DIR}         # the generated ext_rsc_text.h (target ext-rsc-text above)
  ${CMAKE_SOURCE_DIR}/src)   # last: only `ext/...` resolves here (src/ext)

# ---------------------------------------------------------------------------------------
# SDL3: the pinned submodule third_party/SDL (tag in docs/reference/environment.md), built static
# from source with the same options the old Makefile's third_party rule used. EXCLUDE_FROM_ALL: only
# what OpenBA links is built; `unit` never links SDL (D4_RULES C7). The game itself compiles no
# C, so the C flags below reach only the vendored C (SDL and wmadec): optimised whatever the flavour
# (as the old Makefile did: one Release SDL into every flavour), without touching the flavour's C++ flags.
# ---------------------------------------------------------------------------------------
if(NOT EXISTS ${CMAKE_SOURCE_DIR}/third_party/SDL/CMakeLists.txt)
  message(FATAL_ERROR "third_party/SDL is empty: run `git submodule update --init`")
endif()
foreach(_sdl_opt SDL_SHARED SDL_TEST_LIBRARY SDL_TESTS SDL_EXAMPLES SDL_GPU SDL_RENDER SDL_CAMERA
                 SDL_JOYSTICK SDL_HAPTIC SDL_HIDAPI SDL_SENSOR SDL_POWER SDL_DIALOG SDL_TRAY)
  set(${_sdl_opt} OFF CACHE BOOL "Banished: SDL3 vendored build option" FORCE)
endforeach()
set(SDL_STATIC ON CACHE BOOL "Banished: SDL3 vendored build option" FORCE)
if(ANDROID)
  # SDL's Android Java glue (SDLActivity -> HIDDeviceManager) calls into the joystick / HIDAPI
  # natives unconditionally at startup; without them the activity dies with UnsatisfiedLinkError.
  set(SDL_JOYSTICK ON CACHE BOOL "Banished: SDL3 vendored build option" FORCE)
  set(SDL_HIDAPI ON CACHE BOOL "Banished: SDL3 vendored build option" FORCE)
endif()
if(NOT WIN32)
  # The Linux game links SDL3 shared (libSDL3.so.0 beside the exe; see OpenBA below).
  set(SDL_SHARED ON CACHE BOOL "Banished: SDL3 vendored build option" FORCE)
endif()
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O2 -DNDEBUG")
endif()
add_subdirectory(${CMAKE_SOURCE_DIR}/third_party/SDL ${CMAKE_BINARY_DIR}/third_party/sdl3 EXCLUDE_FROM_ALL)
if(NOT TARGET SDL3::SDL3-static)
  message(FATAL_ERROR "third_party/SDL did not define SDL3::SDL3-static")
endif()

# The WMA v2 decoder extracted from FFmpeg (third_party/wmadec/EXTRACT.md; D4_RULES §3 Q5): every
# shipped sound is xWMA. A static C library with its own fixed flags; OpenBA and unit link it
# (src/engine/audio/xwma_decode.cpp and its test).
add_subdirectory(${CMAKE_SOURCE_DIR}/third_party/wmadec ${CMAKE_BINARY_DIR}/third_party/wmadec EXCLUDE_FROM_ALL)

# FreeType (the pinned submodule third_party/freetype, VER-2-13-3; third_party/freetype_build has
# the minimal module set and the licence note): the run-time font rasteriser's engine off Windows
# (src/platform/posix/font_raster_posix.cpp) and the GDI comparison on it
# (src/platform/common/font_raster_freetype.cpp, docs/extensions/handins/FREETYPE.md). OpenBA and
# unit link it on both platforms.
add_subdirectory(${CMAKE_SOURCE_DIR}/third_party/freetype_build ${CMAKE_BINARY_DIR}/third_party/freetype EXCLUDE_FROM_ALL)

# The log-strip guard that runs before every log-stripped build (as the old Makefile did) (tools/check_log_strip.py)
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
  add_custom_target(check-log-strip
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/check_log_strip.py
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "check_log_strip: no qualified std::printf")
endif()

if(WIN32 AND NOT RENDER_BACKEND_READY)
  message(STATUS "OpenBA target skipped: the ${BANISHED_RENDER_BACKEND} backend is not ready on this host (see above)")
elseif(WIN32)
add_executable(OpenBA WIN32
  ${GAME_SRCS} ${ENGINE_SRCS} ${PLATFORM_SRCS} ${RENDER_SRCS} ${APP_SRCS} ${DIAG_SRCS}
  ${CMAKE_SOURCE_DIR}/src/app/icon.rc)
set_target_properties(OpenBA PROPERTIES OUTPUT_NAME OpenBA RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
target_include_directories(OpenBA PRIVATE ${BANISHED_INCLUDES})
target_compile_options(OpenBA PRIVATE ${BANISHED_CXX_FLAGS} ${BANISHED_ONE_WRITE_STDIO} ${FLAVOUR_FLAGS} ${PGO_FLAGS} ${LTO_FLAGS})
# The backend's per-source properties (dx11: the verbatim ASSAO copy's silenced warnings) are set
# by src/render/${BANISHED_RENDER_BACKEND}/backend.cmake.
# the flavour flags are on the link line too (as the old Makefile did: -flto reaches the linker)
target_link_options(OpenBA PRIVATE ${FLAVOUR_FLAGS} ${PGO_LINK_FLAGS} ${LTO_FLAGS} -static -static-libgcc -static-libstdc++ -mwindows)
# shell32: SHGetFolderPathW (Platform::DocumentsDir). No ole32 of ours since Phase D.4 SDL-5 D2;
# SDL3's own system libraries still bring it.
# gdi32: the run-time font rasteriser (src/platform/win/font_raster_win.cpp, KITFREE RSCFONT).
target_link_libraries(OpenBA PRIVATE user32 dbghelp shell32 gdi32)
# SDL3 static: the target carries its include directory and its system libraries
target_link_libraries(OpenBA PRIVATE SDL3::SDL3-static)
# The backend's own link libraries (dx11: none; vulkan: dxbc_spv, the loader is opened by SDL).
target_link_libraries(OpenBA PRIVATE ${RENDER_BACKEND_LINK_LIBRARIES})
# xWMA -> PCM (third_party/wmadec): the target carries its include directory
target_link_libraries(OpenBA PRIVATE wmadec)
# FreeType (third_party/freetype_build): src/platform/common/font_raster_freetype.cpp
target_link_libraries(OpenBA PRIVATE freetype_min)
if(BANISHED_TWEAK_OVERLAY)
  # Dear ImGui v1.92.9b, unmodified (third_party/imgui/README.banished.md): the core + the SDL3 and
  # DX11 backends (the Vulkan renderer is our own, in src/render/vulkan). Upstream's warnings are not ours to fix: silenced per file, as for ASSAO.
  set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/third_party/imgui)
  set(IMGUI_SRCS ${IMGUI_DIR}/imgui.cpp ${IMGUI_DIR}/imgui_draw.cpp ${IMGUI_DIR}/imgui_tables.cpp
                 ${IMGUI_DIR}/imgui_widgets.cpp ${IMGUI_DIR}/backends/imgui_impl_sdl3.cpp
                 ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp)
  set_source_files_properties(${IMGUI_SRCS} PROPERTIES COMPILE_OPTIONS "-w"
                              COMPILE_DEFINITIONS BANISHED_THIRD_PARTY_SOURCE)
  target_sources(OpenBA PRIVATE ${TWEAK_SRCS} ${IMGUI_SRCS})
  target_include_directories(OpenBA PRIVATE ${IMGUI_DIR})
  target_compile_definitions(OpenBA PRIVATE BANISHED_TWEAK_OVERLAY=1)
  # imgui_impl_dx11 compiles its two shaders with D3DCompile (d3dcompiler_47.dll, part of Windows).
  target_link_libraries(OpenBA PRIVATE d3dcompiler)
  message(STATUS "BANISHED_TWEAK_OVERLAY: ON (Dear ImGui tweak overlay, Alt+F10)")
endif()
if(TARGET check-log-strip)
  add_dependencies(OpenBA check-log-strip)
endif()

add_custom_target(sa-deploy
  COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:OpenBA> "${BANISHED_GAME_DIR}/OpenBA.exe"
  COMMAND ${CMAKE_COMMAND} -E rm -f "${BANISHED_GAME_DIR}/standalone_debug.log"
  DEPENDS OpenBA
  COMMENT "Deploy to ${BANISHED_GAME_DIR}/OpenBA.exe")
else()
# ---------------------------------------------------------------------------------------
# The Linux game (Phase D.4 LINUX-1): the Windows exe's tree -- game, engine, app, diag
# (+ src/diag/posix, the twins of src/diag/win) and the renderer backend -- over
# src/platform/posix + sdl + common. `cmake --build <dir> -t OpenBA` -> <dir>/OpenBA.
# The renderer is src/render/vulkan (the default off Windows and Apple since Vulkan P5;
# docs/vulkan/PLAN.md); the Vulkan loader is the user's own, opened by SDL at run time. macOS
# takes this branch too, with src/render/metal.
# ---------------------------------------------------------------------------------------
if(RENDER_BACKEND_READY)
  file(GLOB DIAG_POSIX_SRCS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/diag/posix/*.cpp)
  if(APPLE)
    # Diagnostics follow the same whole-file twin rule as src/platform.
    file(GLOB DIAG_DARWIN_SRCS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/diag/darwin/*.cpp)
    foreach(darwin_src ${DIAG_DARWIN_SRCS})
      get_filename_component(darwin_name ${darwin_src} NAME)
      string(REGEX REPLACE "_darwin\\.cpp$" "_posix.cpp" posix_name ${darwin_name})
      list(REMOVE_ITEM DIAG_POSIX_SRCS ${CMAKE_SOURCE_DIR}/src/diag/posix/${posix_name})
    endforeach()
    list(APPEND DIAG_POSIX_SRCS ${DIAG_DARWIN_SRCS})
  elseif(ANDROID)
    # The same rule with src/diag/android (docs/android/README.md).
    file(GLOB DIAG_ANDROID_SRCS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/diag/android/*.cpp)
    foreach(android_src ${DIAG_ANDROID_SRCS})
      get_filename_component(android_name ${android_src} NAME)
      string(REGEX REPLACE "_android\\.cpp$" "_posix.cpp" posix_name ${android_name})
      list(REMOVE_ITEM DIAG_POSIX_SRCS ${CMAKE_SOURCE_DIR}/src/diag/posix/${posix_name})
    endforeach()
    list(APPEND DIAG_POSIX_SRCS ${DIAG_ANDROID_SRCS})
  endif()
  if(ANDROID)
    # Android (docs/android/README.md): the game is libmain.so, loaded by the app's SDLActivity
    # subclass (android/), which calls its `main` on SDL's thread. Gradle's externalNativeBuild
    # configures this file for arm64-v8a and packages the library from where CMake puts it.
    add_library(OpenBA SHARED
      ${GAME_SRCS} ${ENGINE_SRCS} ${PLATFORM_SRCS} ${RENDER_SRCS} ${APP_SRCS} ${DIAG_SRCS}
      ${DIAG_POSIX_SRCS})
    set_target_properties(OpenBA PROPERTIES OUTPUT_NAME main)
  else()
    add_executable(OpenBA
      ${GAME_SRCS} ${ENGINE_SRCS} ${PLATFORM_SRCS} ${RENDER_SRCS} ${APP_SRCS} ${DIAG_SRCS}
      ${DIAG_POSIX_SRCS})
    # RPATH $ORIGIN: the game directory carries libSDL3.so.0 beside the exe (below).
    set_target_properties(OpenBA PROPERTIES OUTPUT_NAME OpenBA RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
                                                BUILD_RPATH "$ORIGIN")
  endif()
  target_include_directories(OpenBA PRIVATE ${BANISHED_INCLUDES})
  # PGO_FLAGS / LTO_FLAGS are empty off Android (the blocks above): the Linux and macOS lines are
  # as before.
  target_compile_options(OpenBA PRIVATE ${BANISHED_CXX_FLAGS} ${FLAVOUR_FLAGS} ${PGO_FLAGS} ${LTO_FLAGS})
  # -rdynamic: the exe's own functions in the dynamic symbol table, so the crash and hang
  # dumps (backtrace + dladdr, src/diag/posix) name them.
  if(APPLE)
    target_link_options(OpenBA PRIVATE ${FLAVOUR_FLAGS} -Wl,-export_dynamic)
  elseif(ANDROID)
    # P12: hidden visibility (above) leaves `main` as the library's one export; --exclude-libs keeps
    # the static third-party archives' symbols out of the dynamic table too. No undefined symbol may
    # survive the link: the loader would only say so on the phone. The crash dump's dladdr no
    # longer names a function (it prints the library offset, symbolised offline as before).
    # --wrap=pthread_create: a new thread starts on the process's mask, not on the fastest cores its
    # creator may be pinned to (src/platform/android/thread_start_android.h).
    target_link_options(OpenBA PRIVATE ${FLAVOUR_FLAGS} ${PGO_LINK_FLAGS} ${LTO_FLAGS}
                        -Wl,--no-undefined -Wl,--exclude-libs,ALL -Wl,--wrap=pthread_create)
  else()
    target_link_options(OpenBA PRIVATE ${FLAVOUR_FLAGS} -rdynamic)
  endif()
  find_package(Threads REQUIRED)
  # SDL3 SHARED on Linux, built from the same pinned submodule and shipped beside the exe
  # (libSDL3.so.0, RPATH $ORIGIN). The reason it became shared, the Phase D.4 renderer route that
  # dlopened SDL itself, went in Vulkan P5; the game directory and the release package still
  # carry the shared library (known_port_issues row 1284).
  if(APPLE)
    # Metal needs no dlopened SDL: SDL3 is static, and libairconv.dylib is found beside the exe.
    target_link_libraries(OpenBA PRIVATE SDL3::SDL3-static wmadec freetype_min Threads::Threads ${CMAKE_DL_LIBS})
  elseif(ANDROID)
    # libSDL3.so is the apk's second library (SDLActivity loads it before libmain.so); log is
    # __android_log_write (the logcat copy of the log lines, src/platform/android).
    target_link_libraries(OpenBA PRIVATE SDL3::SDL3-shared wmadec freetype_min Threads::Threads ${CMAKE_DL_LIBS}
                          log)
  else()
    target_link_libraries(OpenBA PRIVATE SDL3::SDL3-shared wmadec freetype_min Threads::Threads ${CMAKE_DL_LIBS})
  endif()
  # The backend's own link libraries (metal: the Metal and QuartzCore frameworks; vulkan: dxbc_spv).
  target_link_libraries(OpenBA PRIVATE ${RENDER_BACKEND_LINK_LIBRARIES})
else()
  message(STATUS "OpenBA target skipped: the ${BANISHED_RENDER_BACKEND} backend is not ready on this host (see above)")
endif()
endif()

# ---------------------------------------------------------------------------------------
# Host unit tests (ENDGAME B): the real classes standing on their own. tests/unit/README.md.
# tests/unit/sources.cmake lists the tree sources each test family links against (hand-
# maintained since tests/unit/sources.mk was deleted with the Makefile on 2026-10-11).
# ---------------------------------------------------------------------------------------
include(${CMAKE_SOURCE_DIR}/tests/unit/sources.cmake)
if(NOT WIN32)
  # Each src/platform/win/<seam>_win.cpp the tests link has its src/platform/posix twin.
  list(TRANSFORM UNIT_TREE_SRCS REPLACE "/src/platform/win/([a-z_]+)_win\\.cpp$" "/src/platform/posix/\\1_posix.cpp")
  # error.cpp's fallback exit is kernel32!ExitProcess on Windows and Platform::ExitProcess
  # (os_posix.cpp) here; the POSIX test file exercises the rest of os_posix.cpp.
  list(APPEND UNIT_TREE_SRCS ${CMAKE_SOURCE_DIR}/src/platform/posix/os_posix.cpp)
  # os_posix.cpp's ReleaseModule / ExecutableImageBase (dlclose, dl_iterate_phdr).
  set(UNIT_POSIX_LIBS ${CMAKE_DL_LIBS})
  if(APPLE)
    # On Apple a src/platform/darwin twin, when one exists, replaces its posix twin (the
    # same whole-file rule as PLATFORM_SRCS above; src/platform/darwin/README.md).
    set(UNIT_TREE_SRCS_DARWIN)
    foreach(unit_src ${UNIT_TREE_SRCS})
      if(unit_src MATCHES "/src/(platform|diag)/posix/([a-z_]+)_posix\\.cpp$")
        set(darwin_twin ${CMAKE_SOURCE_DIR}/src/${CMAKE_MATCH_1}/darwin/${CMAKE_MATCH_2}_darwin.cpp)
        if(EXISTS ${darwin_twin})
          set(unit_src ${darwin_twin})
        endif()
      endif()
      list(APPEND UNIT_TREE_SRCS_DARWIN ${unit_src})
    endforeach()
    set(UNIT_TREE_SRCS ${UNIT_TREE_SRCS_DARWIN})
  endif()
endif()
file(GLOB UNIT_SRCS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/tests/unit/*.cpp)
if(NOT APPLE)
  list(FILTER UNIT_SRCS EXCLUDE REGEX "/platform_mac_data_test\\.cpp$")
endif()
# tests/unit/vulkan_*_test.cpp test the Vulkan backend's pure parts and build only when that backend
# is configured (BANISHED_RENDER_BACKEND=vulkan): they need its RENDER_BACKEND_UNIT_SRCS / _LIBS.
if(NOT "vulkan" IN_LIST BANISHED_RENDER_BACKENDS)
  list(FILTER UNIT_SRCS EXCLUDE REGEX "/vulkan_[a-z_]+_test\\.cpp$")
endif()
add_executable(unit EXCLUDE_FROM_ALL ${UNIT_SRCS} ${UNIT_TREE_SRCS} ${RENDER_BACKEND_UNIT_SRCS})
set_target_properties(unit PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
if(TARGET OpenBA)   # absent when the backend is not ready on this host (dx11 off Windows)
  add_dependencies(OpenBA ext-rsc-text openba-build-info)
endif()
add_dependencies(unit ext-rsc-text)
# tests/unit/screen_keyboard_race_test.cpp compiles src/platform/android/input_android.cpp with the
# SDL calls it makes stubbed: SDL's headers only, on that one source (`unit` links no SDL).
set_source_files_properties(${CMAKE_SOURCE_DIR}/tests/unit/screen_keyboard_race_test.cpp
  PROPERTIES INCLUDE_DIRECTORIES ${CMAKE_SOURCE_DIR}/third_party/SDL/include)
target_include_directories(unit PRIVATE ${BANISHED_INCLUDES} ${CMAKE_SOURCE_DIR}/tests/unit)
# -Og in every flavour (it was -O0): the suite runs ~2x faster (77 s -> 37 s as one process, Linux) for
# ~10% more compile time, every test passing; the tests run optimised code, closer to the shipped -O2 /
# -O3 builds, and stay debuggable. The flags that keep results exact (-fno-strict-aliasing, the
# lifetime-DSE flag, -ffp-contract=off) are the same at any level.
target_compile_options(unit PRIVATE
  -fexceptions -fno-strict-aliasing ${BANISHED_LIFETIME_DSE_FLAG} -ffp-contract=off -Wall -Wextra -Wno-invalid-offsetof
  -g -Og -D_DEBUG -DBANISHED_UNIT_TEST ${BANISHED_PORTABLE_FLAGS} ${BANISHED_ONE_WRITE_STDIO})
# The checking builds reach the suite too (BANISHED_SANITIZE, above).
target_compile_options(unit PRIVATE ${SANITIZE_FLAGS} ${THREAD_SAFETY_FLAGS})
target_link_options(unit PRIVATE ${SANITIZE_LINK})
# Except these, -O0: under clang they call a base class's D2 destructor by its mangled name
# (RT_ASM_LABEL, "Clang with the Itanium ABI traps in abstract-base D1"). The base destructors are
# inline; optimised, clang inlines them into the probes' destructors and emits no D2, and on Mach-O
# (label prefix "_") the label is a separate symbol, left undefined at link (the macOS job). At -O0
# the probes call D2 out of line, which emits it. A source's options follow the target's: -O0 wins.
set_source_files_properties(
  ${CMAKE_SOURCE_DIR}/tests/unit/rb_achievement_abi_test.cpp ${CMAKE_SOURCE_DIR}/tests/unit/rb_engine_abi_test.cpp
  ${CMAKE_SOURCE_DIR}/tests/unit/rb_graphics_abi_test.cpp ${CMAKE_SOURCE_DIR}/tests/unit/rb_ui_abi_test.cpp
  ${CMAKE_SOURCE_DIR}/tests/unit/rb_video_abi_test.cpp
  PROPERTIES COMPILE_OPTIONS -O0)
if(WIN32)
  target_link_options(unit PRIVATE -static -static-libgcc -static-libstdc++)
  target_link_libraries(unit PRIVATE gdi32)   # font_raster_win.cpp (ext_font_raster_test.cpp)
else()
  # glibc stays dynamic (getpwuid_r needs NSS at run time); pthreads for the POSIX twins.
  find_package(Threads REQUIRED)
  target_link_libraries(unit PRIVATE Threads::Threads ${UNIT_POSIX_LIBS})
endif()
# tests/unit/xwma_decode_test.cpp decodes through the extracted WMA v2 decoder
target_link_libraries(unit PRIVATE wmadec)
# tests/unit/ext_font_raster_test.cpp: the FreeType rasteriser (and, on Windows, its GDI comparison)
target_link_libraries(unit PRIVATE freetype_min)
# The backend's unit-only library (vulkan: dxbc_spv; the other backends set none)
if(RENDER_BACKEND_UNIT_LIBS)
  target_link_libraries(unit PRIVATE ${RENDER_BACKEND_UNIT_LIBS})
endif()
add_custom_target(run-unit COMMAND unit DEPENDS unit WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# render-backend: the backend's own sources compiled exactly as OpenBA compiles them, nothing linked
# (`tools/build.sh backend`). CI's second Windows configure (vulkan) builds this and `unit` instead of a
# second whole OpenBA: everything else in that exe is the dx11 configure's, and the Linux job builds
# and links the whole vulkan OpenBA.
if(TARGET OpenBA)
  add_library(render-backend OBJECT EXCLUDE_FROM_ALL ${RENDER_BACKEND_OWN_SRCS})
  target_compile_options(render-backend PRIVATE $<TARGET_PROPERTY:OpenBA,COMPILE_OPTIONS>)
  target_compile_definitions(render-backend PRIVATE $<TARGET_PROPERTY:OpenBA,COMPILE_DEFINITIONS>)
  target_include_directories(render-backend PRIVATE $<TARGET_PROPERTY:OpenBA,INCLUDE_DIRECTORIES>)
  target_link_libraries(render-backend PRIVATE SDL3::SDL3-static ${RENDER_BACKEND_LINK_LIBRARIES})
  add_dependencies(render-backend ext-rsc-text)
endif()

# ---------------------------------------------------------------------------------------
# font_dump (KITFREE RSCFONT): the run-time font rasteriser's by-eye harness, a standalone
# debug-only Windows executable over src/platform/win/font_raster_win.cpp and the kit .crs
# reader (src/diag/font_dump/font_dump.cpp has the usage). Never part of OpenBA.
#   cmake --build build/cmake-debug -t font_dump   ->  build/cmake-debug/font_dump.exe
# ---------------------------------------------------------------------------------------
if(WIN32 AND BANISHED_FLAVOUR STREQUAL "debug")
  add_executable(font_dump EXCLUDE_FROM_ALL
    ${CMAKE_SOURCE_DIR}/src/diag/font_dump/font_dump.cpp
    ${CMAKE_SOURCE_DIR}/src/ext/resources/font_tables.cpp
    ${CMAKE_SOURCE_DIR}/src/platform/win/font_raster_win.cpp)
  set_target_properties(font_dump PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
  target_include_directories(font_dump PRIVATE ${BANISHED_INCLUDES})
  target_compile_options(font_dump PRIVATE -g -O0 -Wall -Wextra ${BANISHED_PORTABLE_FLAGS})
  target_link_options(font_dump PRIVATE -static -static-libgcc -static-libstdc++)
  target_link_libraries(font_dump PRIVATE gdi32)
endif()

# ---------------------------------------------------------------------------------------
# Fuzz targets (BANISHED_SANITIZE=fuzz, clang on Linux; tests/fuzz/README.md). Each
# tests/fuzz/<name>_fuzz.cpp defines LLVMFuzzerTestOneInput and becomes `fuzz_<name>`, linked
# against the unit suite's tree sources and stubs (everything a unit test can call, a fuzz target
# can). `cmake --build <dir> -t fuzz` builds them all. A crash input a target finds becomes a
# unit test (tests/unit/) with the bytes inline: the failing test the fix PR carries.
# ---------------------------------------------------------------------------------------
if(BANISHED_SANITIZE STREQUAL "fuzz")
  file(GLOB FUZZ_SRCS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/tests/fuzz/*_fuzz.cpp)
  file(GLOB FUZZ_UNIT_STUBS ${CMAKE_SOURCE_DIR}/tests/unit/*_stubs.cpp)
  add_library(fuzz_tree STATIC EXCLUDE_FROM_ALL ${UNIT_TREE_SRCS} ${FUZZ_UNIT_STUBS})
  target_include_directories(fuzz_tree PUBLIC ${BANISHED_INCLUDES} ${CMAKE_SOURCE_DIR}/tests/unit
    ${CMAKE_SOURCE_DIR}/tests/fuzz)
  target_compile_options(fuzz_tree PUBLIC
    -fexceptions -fno-strict-aliasing ${BANISHED_LIFETIME_DSE_FLAG} -ffp-contract=off -g -O1 -D_DEBUG
    -DBANISHED_UNIT_TEST -DBANISHED_FUZZ ${BANISHED_PORTABLE_FLAGS} ${SANITIZE_FLAGS})
  find_package(Threads REQUIRED)
  target_link_libraries(fuzz_tree PUBLIC Threads::Threads ${UNIT_POSIX_LIBS} wmadec freetype_min)
  add_dependencies(fuzz_tree ext-rsc-text)
  add_custom_target(fuzz)
  foreach(fuzz_src ${FUZZ_SRCS})
    get_filename_component(fuzz_name ${fuzz_src} NAME_WE)
    string(REGEX REPLACE "_fuzz$" "" fuzz_name ${fuzz_name})
    add_executable(fuzz_${fuzz_name} EXCLUDE_FROM_ALL ${fuzz_src})
    set_target_properties(fuzz_${fuzz_name} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/fuzz)
    target_link_libraries(fuzz_${fuzz_name} PRIVATE fuzz_tree)
    target_link_options(fuzz_${fuzz_name} PRIVATE -fsanitize=fuzzer ${SANITIZE_LINK})
    add_dependencies(fuzz fuzz_${fuzz_name})
  endforeach()
endif()
