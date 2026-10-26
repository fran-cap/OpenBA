# Dev environment (reference)

(The executable was formerly named `reimpl-x64.exe`; the Linux target `reimpl-x64`.)

## Fresh clone checklist
1. `git submodule update --init` (SDL3 at `third_party/SDL`; CMake refuses to configure without it).
2. The pinned WinLibs GCC: `tools/toolchain/install_winlibs.sh` (unpacks to `C:\winlibs-gcc16`; `tools/build.sh`
   finds it there or through `BANISHED_GCC_DIR`), and the pip `cmake` + `ninja` on PATH (Toolchain below).
3. `BANISHED_DIR` (default `C:/GOG Games/Banished`): the game directory `tools/build.sh deploy` / `run`
   copy the exe into; the unit tests also read `WinData/data0.pkg` from it and skip the tests that
   need it when it is absent (also tried: `/mnt/c/GOG Games/Banished`).
4. The Banished Modding ToolKit 1.0.7 unpacked at `<repo>/ToolKit/` (gitignored; `ToolKit/bin/x64/Tools-x64.exe`
   and `ToolKit/resource/`): `tools/ext_resources.sh`, run by deploy, fails without it.
5. The MCP venvs `.mcp.json` points at are gitignored: `tools/ghidra_mcp/.venv` (+ the GhidraMCP bridge,
   skill `ghidra`) and `tools/renderdoc_mcp/.venv`; recreate them before using those servers. The renderdoc
   entry needs an ABSOLUTE `RENDERDOC_MODULE_PATH` (Win32 AddDllDirectory rejects a relative one): export
   it, or edit the `${RENDERDOC_MODULE_PATH:-...}` default in `.mcp.json`.
Then `tools/build.sh unit` (no game needed beyond item 3's skips) and `tools/build.sh run` (the gate).

## Toolchain
- **GCC 16.2.0, WinLibs build r3** (mingw-w64 14.0.0, UCRT, posix threads, SEH) at `C:\winlibs-gcc16\mingw64\bin\`,
  installed and pinned by `tools/toolchain/install_winlibs.sh` (sha256 of the zip in the script; CI installs the same);
  C++17, static-linked runtime. `tools/build.sh` puts it first on PATH and passes it to CMake, and wipes a build
  directory configured with another compiler. Switched from MSYS2 GCC 13.1 on 2026-10-26 (3% faster sim, and the
  release exe's PGO profile set `perf/pgo/` is tied to the exact compiler version; `perf/pgo/README.md`). The MSYS2
  install stays for its shell and gdb (`.vscode/launch.json`); its GCC is no longer used. The install script also
  copies `mingw32-make.exe` to `make.exe` beside the compiler: GCC's LTO link runs its partitions through `make`.
- The build is CMake + Ninja through `tools/build.sh` (2026-10-11; the gate kit, the stress runner and the
  resave gate all call it; build directories `build/cmake-<flavour>`). The Makefile and its `mingw32-make`
  route were deleted the same day (history: `docs/history/`, the STATUS log).
- MSYS2 packages are outdated (stale signatures): avoid `pacman -S` for new installs
- Linux (WSL2 Ubuntu 24.04, g++ 13.3, cmake 3.28, ninja): the same CMakeLists builds `OpenBA` and
  `unit` in every flavour, with the Vulkan backend as the default renderer there (no flag; Vulkan P5);
  `tools/linux_gamedir.sh` + `tools/linux_run.sh` run it on lavapipe (Mesa's CPU Vulkan, apt
  `mesa-vulkan-drivers` + `libvulkan1`; `vulkan-tools` for `vulkaninfo`); the source copy lives in the
  WSL home (`~/banished-main`, rsync of the working tree with its submodules) because a build over
  `/mnt/c` is slow; ccache makes a re-copy's build near-free. WSL2 needs the hypervisor: with
  `hypervisorlaunchtype off` (the uProf IBS setup) `wsl` refuses to start
- Python 3.9.10 system-wide; 3.10.11 in the project venv `tools/ghidra_mcp/.venv/`
- `uv` v0.9.24; Node.js + npx (for the vscode MCP server)
- JAVA_HOME `C:\Program Files\Java\jdk-24` (for Ghidra)
- CMake 4.4 + ninja from pip (`C:\Users\USPC\AppData\Roaming\Python\Python312\Scripts`, on PATH)

## Android toolchain (Vulkan P6; `docs/android/README.md`)
- Android SDK at `%LOCALAPPDATA%\Android\Sdk` (no system-wide `ANDROID_HOME`; `tools/android.sh` and
  `tools/boot_state.sh --android` default to it): cmdline-tools 22.0 (`cmdline-tools/latest`), NDK r30
  `ndk/30.0.16248370` (clang 21, `ndk-stack.cmd`, `shader-tools/.../glslc`), SDK CMake 3.31.6 with its ninja,
  platforms 35 and 36, build-tools 36.0.0, platform-tools 37.0.1 (adb also at `C:\code\platform-tools`).
- `sdkmanager` and Gradle run on Android Studio's JBR 21: `JAVA_HOME="C:/Program Files/Android/Android Studio/jbr"`
  (the Ghidra JDK 24 above is not used for Android). Gradle 8.12.1 through the wrapper in `android/`, AGP 8.8.1;
  both are downloaded into `~/.gradle` on the first build (network needed once).
- Git Bash rewrites `/sdcard/...` arguments into Windows paths before they reach `adb.exe`: the scripts set
  `MSYS_NO_PATHCONV=1` and pass local paths through `cygpath -m`. Do the same for a hand-typed `adb push`.
- Reference device: Galaxy S25 over USB (`adb devices`); Vulkan facts in `docs/vulkan/PLAN.md` §2.1.
- The Vulkan SDK (`C:\VulkanSDK\1.4.363.0`) provides `glslc` / `spirv-val` for
  `tools/vulkan/gen_present_spirv.sh` (the NDK's `glslc` works too).

## Third-party (pinned submodules; run `git submodule update --init` after a clone)
- **SDL3** at `third_party/SDL`: tag **`release-3.4.18`** (the current stable, 2026-10-02),
  commit `829a65d769d935c4852f8159e964312c0957260a` (D4_RULES §3 Q4: SDL3 only, static; the
  revised SDL plan §B). First pinned at `release-3.2.30`, moved the same day. `.gitmodules` has `shallow = true`. CMake builds it from source inside each build directory
  (`build/cmake-<flavour>/third_party/sdl3`, static, with the options below; the Makefile used to build it
  once into `build/third_party/install`). GCC 13.1 ucrt64 built it as is, GCC 16.2 (WinLibs) too, no fallback:
  ```
  cmake -S third_party/SDL -B build/third_party/sdl3 -G Ninja -DCMAKE_BUILD_TYPE=Release
        -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TEST_LIBRARY=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF
        -DSDL_GPU=OFF -DSDL_RENDER=OFF -DSDL_CAMERA=OFF -DSDL_JOYSTICK=OFF -DSDL_HAPTIC=OFF
        -DSDL_HIDAPI=OFF -DSDL_SENSOR=OFF -DSDL_POWER=OFF -DSDL_DIALOG=OFF -DSDL_TRAY=OFF
        -DCMAKE_INSTALL_PREFIX=<repo>/build/third_party/install
  cmake --build build/third_party/sdl3 -j2 --target install
  ```
  SDL_HAPTIC must stay OFF while SDL_JOYSTICK is OFF (configure fails otherwise).
  `SDL_NOTIFICATION` does not exist in this tag. The exe links `libSDL3.a` plus the system
  libraries the installed `sdl3.pc` lists, read at link time (with joystick / HIDAPI off that
  list has no `hid` / `mincore`). The unit tests and the resave tool do not link SDL (D4_RULES C7). The CMake twin
  builds the same tree with `add_subdirectory(third_party/SDL EXCLUDE_FROM_ALL)`.
  To move the pin: `git -C third_party/SDL fetch --depth 1 origin tag <tag>`, check the tag
  out, commit the submodule, update this entry, delete `build/third_party`.
- **Dear ImGui** at `third_party/imgui` (vendored files, not a submodule): tag **`v1.92.9b`**, commit
  `f1cc2ae15e53a861a874c3034aae6798fde194ab`, MIT; the core + `imgui_impl_sdl3` + `imgui_impl_dx11`, unmodified
  (`third_party/imgui/README.banished.md` has the file hashes). Compiled into the **debug flavour only** (CMake
  option `BANISHED_TWEAK_OVERLAY`) for the Alt+F10 tweak overlay, `docs/reference/tweak_overlay.md`; the debug exe
  then imports `d3dcompiler_47.dll`. Also vendored, all flavours: `third_party/wmadec` (FFmpeg's WMA v2 decoder,
  `EXTRACT.md`) and the ASSAO copy under `src/render/dx11/ao/assao/` (its `README.md`).

## VS Code debugging
- Configurations live in `.vscode/launch.json`: "Launch Standalone (debug, autostart new
  game)" sets `BANISHED_AUTOSTART`; "Launch Standalone (debug)" stops at the menu
- Launch AND inspect with the `vscode-debugger` MCP (`start_config`, `mcpManaged: true`):
  a session started through the `vscode` MCP's `start_debug_session` is invisible to it
- `vscode` MCP is for general editor operations (terminal, tasks, open files)
- Run `tools/build.sh deploy` first: the preLaunchTask may not run through MCP
- `workspaceFolder` must use a lowercase drive letter (`c:\...`)

## Ghidra
See the `ghidra` skill (`.claude/skills/ghidra/SKILL.md`) and `docs/reference/HOW_TO_START_GHIDRA.md`.
