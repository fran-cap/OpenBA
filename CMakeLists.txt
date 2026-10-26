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
#   perf    [standalone-perf]   -O2 -g -DNDEBUG, logs stripped, PerfX knobs default-on
#   release [standalone]        -O3 -DNDEBUG, logs stripped, PerfX knobs default-off
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
project(banished_reimpl VERSION 1.0.1 LANGUAGES CXX)
# OpenBA version shown beside the retail version in the main / pause menu's BuildId
# overlay (src/game/Dialog/mainmenudialog.cpp). Bump here to cut a release (README "Versioning").
add_compile_definitions(OPENBA_VERSION="${PROJECT_VERSION}")
if(WIN32)
  enable_language(RC)   # src/app/icon.rc
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

set(BANISHED_FLAVOUR "debug" CACHE STRING "debug | og | perf | release")
set_property(CACHE BANISHED_FLAVOUR PROPERTY STRINGS debug og perf release)
option(BANISHED_USE_CCACHE "Compile through ccache when it is on PATH" ON)
set(BANISHED_GAME_DIR "C:/GOG Games/Banished" CACHE PATH "where sa-deploy copies the exe")
# The debug tweak overlay (Dear ImGui, Alt+F10; src/diag/tweak_overlay.h + src/diag/win/tweak_overlay.cpp, third_party/imgui,
# docs/reference/tweak_overlay.md): ON for the debug flavour on Windows only. OFF, not one source
# of it is compiled and every `#if BANISHED_TWEAK_OVERLAY` site is out, so og / perf / release build
# exactly as without it.
# The renderer backend (src/render/common/README.md): dx11 on Windows, metal on APPLE, vulkan on
# every other host (Linux, since Vulkan P5) and by choice on Windows (docs/vulkan/PLAN.md). Chosen
# here because the tweak overlay below is DX11-only; the fragment itself is included further down,
# with the sources. dx11 is Windows-only: its backend.cmake answers not-ready elsewhere.
if(APPLE)
  set(BANISHED_RENDER_BACKEND_DEFAULT metal)
elseif(WIN32)
  set(BANISHED_RENDER_BACKEND_DEFAULT dx11)
else()
  set(BANISHED_RENDER_BACKEND_DEFAULT vulkan)
endif()
set(BANISHED_RENDER_BACKEND ${BANISHED_RENDER_BACKEND_DEFAULT} CACHE STRING "Renderer backend: src/render/<name>/backend.cmake")
if(BANISHED_FLAVOUR STREQUAL "debug" AND WIN32 AND BANISHED_RENDER_BACKEND STREQUAL "dx11")
  set(_banished_tweak_default ON)
else()
  set(_banished_tweak_default OFF)
endif()
option(BANISHED_TWEAK_OVERLAY "Debug tweak overlay (Dear ImGui): debug flavour on Windows, dx11 backend only" ${_banished_tweak_default})

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

# ---------------------------------------------------------------------------------------
# Profile-guided optimisation (perf/pgo/README.md). BANISHED_PGO:
#   auto      (default) release flavour on Windows: `use` when BANISHED_PGO_DIR holds a profile set,
#             otherwise plain; every other flavour / platform: off
#   generate  instrument OpenBA; the training runs (perf/pgo/train.sh) write the profile set
#   use       compile OpenBA against the profile set in BANISHED_PGO_DIR
#   off       never
# The profile set is keyed on each object's path RELATIVE to the build dir (-fprofile-prefix-path),
# so it is written by one build dir and read by another (CI's), on the one pinned compiler
# (tools/toolchain/install_winlibs.sh): GCC refuses a profile written by a different version.
# A profile older than a function it describes is a warning (coverage-mismatch), not an error:
# releases are infrequent and retrain (train.sh) every time, drift between them is accepted.
# ---------------------------------------------------------------------------------------
set(BANISHED_PGO "auto" CACHE STRING "auto | generate | use | off (perf/pgo/README.md)")
set_property(CACHE BANISHED_PGO PROPERTY STRINGS auto generate use off)
set(BANISHED_PGO_DIR "${CMAKE_SOURCE_DIR}/perf/pgo/profile" CACHE PATH "the .gcda profile set")
set(PGO_MODE "${BANISHED_PGO}")
if(PGO_MODE STREQUAL "auto")
  file(GLOB _pgo_set "${BANISHED_PGO_DIR}/*.gcda")
  if(WIN32 AND BANISHED_FLAVOUR STREQUAL "release" AND _pgo_set)
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
if(PGO_MODE STREQUAL "generate")
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
option(BANISHED_LTO "Link-time optimisation on the Windows release exe" ON)
set(LTO_FLAGS)
if(WIN32 AND BANISHED_LTO AND BANISHED_FLAVOUR STREQUAL "release" AND NOT PGO_MODE STREQUAL "generate")
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
  ${CMAKE_SOURCE_DIR}/src/ext/ui/Dialog/TerrainEditPage.rsc
  ${CMAKE_SOURCE_DIR}/src/ext/ui/Dialog/TouchControls.rsc)
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
if(NOT EXISTS ${CMAKE_SOURCE_DIR}/src/render/${BANISHED_RENDER_BACKEND}/backend.cmake)
  message(FATAL_ERROR "BANISHED_RENDER_BACKEND=${BANISHED_RENDER_BACKEND}: no src/render/${BANISHED_RENDER_BACKEND}/backend.cmake")
endif()
include(${CMAKE_SOURCE_DIR}/src/render/${BANISHED_RENDER_BACKEND}/backend.cmake)
message(STATUS "OpenBA render backend: ${BANISHED_RENDER_BACKEND} (ready: ${RENDER_BACKEND_READY})")
file(GLOB_RECURSE RENDER_COMMON_SRCS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/render/common/*.cpp)
list(APPEND RENDER_SRCS ${RENDER_COMMON_SRCS})
# ext.compress_ao (docs/extensions/handins/COMPRESSAO.md): the BC4 encoder and the derived cache's
# content hash run over ~2 GB of AO maps on a large-mod boot, on the loader's critical path. They are
# built -O2 in every flavour (debug and og included; the unit build too, as these properties are
# per source file): at -O0 the encoder does ~7 MB/s instead of ~57 MB/s per core.
set_source_files_properties(${CMAKE_SOURCE_DIR}/src/render/common/bc4/bc4.cpp
                            ${CMAKE_SOURCE_DIR}/src/ext/derived_cache/hash64.cpp
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
  ${RENDER_BACKEND_INCLUDES}  # src/render/<backend>/backend.cmake (dx11: src/render/dx11)
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
  # DX11 backends. Upstream's warnings are not ours to fix: silenced per file, as for ASSAO.
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
  target_compile_options(OpenBA PRIVATE ${BANISHED_CXX_FLAGS} ${FLAVOUR_FLAGS})
  # -rdynamic: the exe's own functions in the dynamic symbol table, so the crash and hang
  # dumps (backtrace + dladdr, src/diag/posix) name them.
  if(APPLE)
    target_link_options(OpenBA PRIVATE ${FLAVOUR_FLAGS} -Wl,-export_dynamic)
  elseif(ANDROID)
    # A shared library exports its functions already (the crash dump's dladdr names them), and
    # no undefined symbol may survive the link: the loader would only say so on the phone.
    target_link_options(OpenBA PRIVATE ${FLAVOUR_FLAGS} -Wl,--no-undefined)
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
if(NOT BANISHED_RENDER_BACKEND STREQUAL "vulkan")
  list(FILTER UNIT_SRCS EXCLUDE REGEX "/vulkan_[a-z_]+_test\\.cpp$")
endif()
add_executable(unit EXCLUDE_FROM_ALL ${UNIT_SRCS} ${UNIT_TREE_SRCS} ${RENDER_BACKEND_UNIT_SRCS})
set_target_properties(unit PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
if(TARGET OpenBA)   # absent when the backend is not ready on this host (dx11 off Windows)
  add_dependencies(OpenBA ext-rsc-text openba-build-info)
endif()
add_dependencies(unit ext-rsc-text)
target_include_directories(unit PRIVATE ${BANISHED_INCLUDES} ${CMAKE_SOURCE_DIR}/tests/unit)
target_compile_options(unit PRIVATE
  -fexceptions -fno-strict-aliasing ${BANISHED_LIFETIME_DSE_FLAG} -ffp-contract=off -Wall -Wextra -Wno-invalid-offsetof
  -g -O0 -D_DEBUG -DBANISHED_UNIT_TEST ${BANISHED_PORTABLE_FLAGS} ${BANISHED_ONE_WRITE_STDIO})
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
