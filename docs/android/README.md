# OpenBA on Android

NOT RETAIL. The Android build of OpenBA: the same game, engine and Vulkan backend as Linux, packaged as
an app whose native library is the repository's root `CMakeLists.txt`. Plan and rulings:
`docs/vulkan/PLAN.md` (§1, §3.6, §4.5, §4.6, phases P6 and P7). Reference device: Galaxy S25 (Adreno 830).
Only new Adreno parts are in scope; a mouse and a keyboard are how the game is played (touches arrive as
mouse clicks).

No game data is in the tree, the build directory or the apk. The player copies the `WinData` packages of
their own Banished install into the app's data folder (below). The apk does carry one third-party file
set, the FrancophilSans typeface with its notice (below, `THIRD_PARTY_NOTICES.md`).

## Layout

| Path | What |
|---|---|
| `android/` | The Gradle project: `settings.gradle`, `build.gradle` (AGP pin), `gradle.properties` (the package id and app name, each set once), the Gradle wrapper. |
| `android/app/build.gradle` | The app module: SDK levels, NDK and CMake pins, arm64-v8a only, the native build over `../../CMakeLists.txt` (targets `OpenBA` = `libmain.so` and `SDL3-shared` = `libSDL3.so`), SDL's Java glue compiled in place from `third_party/SDL/android-project`. |
| `android/app/src/main/AndroidManifest.xml` | One activity, landscape only, Vulkan 1.3 required. |
| `android/app/src/main/java/org/openba/game/OpenBAActivity.java` | `SDLActivity` subclass: runs the game's own `main`, turns the launch intent's `BANISHED_*` extras into the environment, makes the surface an `OpenBASurface`. |
| `android/app/src/main/java/org/openba/game/OpenBASurface.java` | `SDLSurface` subclass: the captured mouse's buttons and motion (P8). |
| `src/platform/android/` | The platform twins (`README.md` there has the rule). |
| `src/diag/android/` | The crash handler and boot-state watchdog twins. |
| `tools/android.sh` | Build, install, push the data, run, pull the logs. |
| `tools/boot_state.sh --android` | The boot state of the app on the attached device. |

## Versions (pinned)

| Item | Version |
|---|---|
| Android Gradle Plugin | 8.8.1 (`android/build.gradle`) |
| Gradle | 8.12.1 (wrapper, distribution SHA-256 pinned) |
| JDK running Gradle | Android Studio's bundled JBR 21 (AGP 8.8 needs 17 or newer) |
| NDK | r30, `30.0.16248370` (clang 21) |
| CMake (SDK) | 3.31.6, with its ninja |
| compileSdk / targetSdk / minSdk | 35 / 35 / 29 |
| ABI | arm64-v8a only |
| C++ runtime | `c++_shared` (`libc++_shared.so` in the apk) |

## Build

From Git Bash at the repository root, no Android Studio needed:
```bash
tools/android.sh build debug --jobs 8      # android/app/build/outputs/apk/debug/app-debug.apk
```
The script sets `ANDROID_HOME` (default `%LOCALAPPDATA%/Android/Sdk`) and `JAVA_HOME` (default Android
Studio's `jbr`) when they are unset, and passes the job count to the native build. A clean debug build
takes about 4.5 minutes with 2 jobs; a one-file change rebuilds in well under a minute. Release builds
(`build release`, the release flavour since P12: -O3, ThinLTO, the clang profile when one is committed,
`perf/pgo/android/README.md`; `--flavour perf` for the perf baselines' -O2 build) come out unsigned
(`apk/release/app-release-unsigned.apk`); `tools/android.sh sign` aligns and signs them (see Release below).

The NDK configures the root `CMakeLists.txt` with `ANDROID` set. That branch takes `src/platform/posix`
plus `src/platform/sdl` and `common`, with every `src/platform/android/<name>_android.cpp` replacing its
`posix/<name>_posix.cpp` twin (the darwin rule), the same for `src/diag/android`, builds `OpenBA` as the
shared library `libmain.so`, keeps SDL shared (`libSDL3.so`), and turns on SDL's joystick and HIDAPI
modules because SDL's Java glue calls into them at startup. The renderer is the Vulkan backend.

## Install, data, run

```bash
tools/android.sh install debug            # adb install -r -d (a debug apk replaces any debuggable build)
tools/android.sh push-data                # BANISHED_DIR's WinData/*.pkg -> the phone (once; the font comes with the apk)
tools/android.sh run                      # launch to the main menu
tools/android.sh run BANISHED_AUTOSTART=1 BANISHED_PAUSEMENU=3000 BANISHED_PAUSEMENU_QUIT=120 \
                     BANISHED_PAUSEMENU_BUTTON=6   # new game, then Quit Game from the pause menu
tools/android.sh boot-state --wait 300    # = tools/boot_state.sh --android --wait 300
tools/android.sh pull <dir>               # the standalone_* logs and the boot state
tools/android.sh stop
```

**The data folder** is the app-specific external folder, `/sdcard/Android/data/<package>/files`
(`SDL_GetAndroidExternalStoragePath()`): no permission, readable over adb and MTP, removed with the app.

| On the phone | What |
|---|---|
| `files/WinData/*.pkg` | The retail packages, pushed by the player. Only the top-level `*.pkg` are pushed; parked mods and other subfolders stay home. |
| `files/FrancophilSans.ttf`, `FrancophilSans-Bold.ttf`, `FrancophilSans_README.txt` | The typeface `ext.sharp_ui` rasterises its fonts from at a UI scale other than 100 % (below). Carried by the apk as assets and copied here by the app at start when absent (P9); a file already there is left alone. |
| `files/OpenBA` | Does not exist: it is the "exe path" the engine is told (`file_android.cpp`), so the game directory is `files/`. |
| `files/standalone_debug.log`, `standalone_error.log`, `standalone_crash.log`, `standalone_hang.log`, `standalone_boot_state.txt` | The same files as beside `OpenBA.exe` on Windows. |
| `files/Documents/Banished/` | The per-user folder: `settings.cfg`, `Save/`, `Cache/` (the derived-data cache: SPIR-V, `vulkan_pipelines.bin` / `.txt`). |
| `files/tmp/` | Temporary files (a save is written there, then moved). |

`WinData/` must be made by the app: a folder adb makes belongs to the shell user, and the app process can
neither list nor open anything inside it (EACCES). The app makes `WinData/` and `Documents/` at every
start, and `push-data` starts the app once when the folder is missing (or removes an adb-made one first).

**The font rasteriser** (P8). `ext.sharp_ui` draws the UI's text crisp at any UI scale by rasterising the
game's typeface, FrancophilSans, at the pixel height the screen needs (FreeType,
`src/platform/common/font_raster_freetype.cpp`). Windows, Linux and macOS find `FrancophilSans.ttf`
installed or beside the exe, where the release packages put it. On Android the one place is the data folder
(`src/platform/android/font_raster_android.cpp`): the app looks for `files/FrancophilSans.ttf` and nowhere
else. The typeface is Manfred Klein's, kept unchanged with his notice in `tools/release/fonts/` and shipped
beside the exe in the desktop packages (`THIRD_PARTY_NOTICES.md`). The apk carries the same three files
(P9, ruled 2026-10-26): `android/app/build.gradle` adds `tools/release/fonts` as an assets folder in place
(nothing is copied into `android/`), and `OpenBAActivity.onCreate` copies each file absent from the data
folder before the native side starts (written to `<name>.part`, then renamed; logcat `font: copied ...`).
The copy is on the Java side because assets are a Java API; the native side would need the activity's
`AssetManager` through JNI for the same three files. `push-data` no longer pushes them. Without the file
the text keeps the game's own glyph sheets (`[ExtRes] font variants off: typeface FrancophilSans absent`
in `standalone_error.log`); with it, a UI scale other than 100 shows `[ExtRes] font <Kit> at <px> px: ...
glyphs rasterised`.

**Harness flags.** The game reads its `BANISHED_*` flags once at boot from the process environment
(`src/app/options.cpp`; `src/app/options.h` is the inventory). On Android the launch intent is the
source: every string extra whose key starts with `BANISHED_` becomes that environment variable before the
native library loads (`OpenBAActivity.java`); leftovers from an earlier launch in the same process are
cleared first, so a launch from the icon runs with none. By hand:
```bash
adb shell am start -n org.openba.game/org.openba.game.OpenBAActivity -e BANISHED_AUTOSTART 1
```
SDL ends the game's process state once `main` returns and refuses a second start in the same process, so
`tools/android.sh run` force-stops the app first.

**Phone defaults** (PLAN.md §1). The first start (no `settings.cfg` yet) writes a `settings.cfg` holding
only `GameEdgeScroll=false` (P7: a touch held at the edge is a parked cursor), `VideoRenderScale=50`,
`VideoMultisample=0`, `VideoShadowScalar=2` (retail's Low Shadow Resolution; shadows stay on: retail's own
off is Options > Shadow Quality = None, `VideoShaderDetail=0`, `P8_SHADOWS_RELEASE.md`), and
`ext.ambient_occlusion`, `ext.better_trees`, `ext.foliage_aa`, `ext.billboard_shadow`,
`ext.max_shadow_res` off, `ext.fps_cap` on at `ext.fps_cap_rate=60` (P8, ruled: the menu and the game
paced to 60 on the 120 Hz panel), `ext.touch_mode` on at `ext.touch_mode_toolbar_scale=200` (P10, ruled: a
camera pad, rotate and zoom buttons bottom-left, the toolbar at its own scale beside them,
`docs/android/TOUCH_MODE.md`), `ext.touch_pointer` on (P11, ruled: fingers aim the tools, an action bar places /
rotates / cancels, two fingers pan / pinch / spin, a tap picks the nearest; `docs/android/P11_TOUCH_POINTER.md`; its
keys `ext.touch_pointer_lift`, `_pick_radius`, `_tap_twice` keep their defaults), and `ext.phone_menu` on (`app_android.cpp`): on the phone's 19.5:9 screen the
main menu's logo sits whole and centred at the top and the menu's buttons form one row of square buttons
across the middle (owner's ruling, P8 round 2), inside a 3.4 % safe area (P11c; 4.5 % before) clear of the
rounded corners and the camera cut-out, the in-game interface (the toolbar and event notices bottom-right,
the panels at their default top-left, tooltips, drop-downs) is kept inside the same safe area, and Options >
User interface scale goes down to 25 % (the UI four times larger than 100 %;
`docs/extensions/handins/PHONEMENU.md`). The loading screen looks like the pause menu: its picture top-aligned at
the menus' scale plus its torn bottom border drawn again at the screen's bottom, the spinner and
"Loading..." in that border at the Options UI scale (P9). The first-run UI scale is the game's own
100 in back-buffer pixels, stored in screen units: 50 at the seeded Half (P9, below). Options changes are
kept as anywhere else.

**Dialogs for a finger** (P9, same row). Scroll bars are four times thicker, dialog close buttons twice
the size, a dialog larger than the safe area (Options and Fixes and Extensions below about UI 45 at render
scale Full) is shown whole at its own fitted scale while it is open (the rest of the interface keeps the
player's, P11), and the main and pause menus show the
backdrop's bottom border with the version line inside it (`docs/extensions/handins/PHONEMENU.md` §2,
`docs/android/P9_DIALOGS.md`). P11: every other control a finger presses is brought to about the close
button's size (36 units): title-bar and icon buttons (the citizens' `>`), Ok / Cancel / Apply, check boxes,
drop-downs and their rows, tabs, spinners, and sliders at twice their art (`docs/android/P11_TOUCH_SIZE.md`).
P11b: a dialog taller than the safe area caps its scrolling lists so it fits at the player's scale (the
fitted scale-down only past three rows), and the Options Video / Audio pages and the Professions list are
moved into a scrolling frame, so every Options page scrolls like the Enhanced tab
(`docs/android/P11B_SCROLL_SIZE.md`).

**Render scale** (P7). `VideoRenderScale=<percent>` (25..100) sizes the engine's back buffer against the
window: the phone's 50 renders 1170x540 and the present pass scales it up (linear) while turning it into the
1080x2340 swap-chain images. `Platform::Window::RenderScale` reads the key at window creation; since P8
Options > Enhanced "Render scale" (Full / Half / Quarter = 100 / 50 / 25) changes it live
(`SetRenderScale`, then `Video::Device::settingsChanged`: the Vulkan backend rebuilds the back buffer, the
targets and the swap chain at the next frame) and records the key. Desktops answer 1.0 and show the row
disabled. The SDL layer hands the engine the scaled size and pixel density, so taps land where they are
drawn. The MSAA default stays off: the numbers are in `P7_FIRST_PASS.md`.

**UI scale in screen units** (P9, `docs/android/P9_LOADING_SCALE.md` §2). User interface scale
(`GameUIScale`) is the UI scale at Full render scale: the interface is applied at `_uiScale = UIScale /
render percent` (`src/platform/common/ui_render_scale.h`), so it keeps its size on the screen at Full,
Half and Quarter; changing the render scale re-derives `_uiScale` and never rewrites the setting. The
slider's range and `ext.phone_menu`'s floor (35 on the S25) are in the same units and do not move with the
render scale. At render scale 100 (every desktop) this is retail's `UIScale / 100`.

**Frame rate cap** (P8). Options > Enhanced "Frame rate cap" (Off / 30 / 60 / 90 / 120) is the class A row
`ext.fps_cap` with `ext.fps_cap_rate`: `src/app/frame_limiter.h` paces the menu and the game to it (the
game's fixed simulation step is untouched; only fewer frames draw it). The phone's first run turns it on
at 60; a desktop leaves it off, the game uncapped as retail. `BANISHED_FPS_CAP` still overrides it.
P12: the rate is also told to the display. Each ceiling the limiter paces to goes to
`Platform::Window::SetPreferredFrameRate` (`src/platform/android/window_android.cpp`:
`ANativeWindow_setFrameRate`, API 30, resolved at run time, so a minSdk 29 phone ignores it), and
SurfaceFlinger switches a variable-rate panel to it: the S25's 120 Hz panel runs at 60 under the default
cap, which halves the display's power and lets FIFO itself pace the frames instead of the limiter's sleep
landing on every other vsync (`docs/android/PERF_P7.md` §4 rank 2). A new native window after a trip to
the background is told again where the Vulkan backend makes its swap chain. The log line
`[Window] preferred frame rate 60 -> ANativeWindow_setFrameRate = 0` is the check (0 = accepted).

**Audio** (P12). SDL's AAudio stream is opened in the default performance mode, not LOW_LATENCY
(`SDL_HINT_ANDROID_LOW_LATENCY_AUDIO` = 0 in `app_android.cpp`): the game needs no 5 ms output latency,
and the default mode spares the fast-mixer wakeups.

**Threads on a heterogeneous SoC** (P12). The roster (`src/platform/common/cpu/roster.h`, THE BIG
CORES) sizes the path workers, cull helpers and the fork-join pool on the big cores only: a CPU whose
kernel `cpu_capacity` is half the largest or less (an efficiency cluster) no longer counts, so no worker
is created for a core that would run two to three times slower than the rest and hold up every
fork-join. The boot line `[Cpu] ... big=0x..` shows the mask on such a phone; a uniform SoC (the S25's
2 + 6, all within half) and every desktop are unchanged. Nothing is pinned.

**Mouse** (P8). A mouse works as on a desktop. While the middle button is held (camera rotate) the window
is in relative mode, which on Android is pointer capture; `OpenBASurface.java` passes the captured
pointer's buttons and motion (the frame's batched samples summed) to SDL, which the pinned SDL drops, and
at the release the cursor is put back where the drag began, where Android shows the pointer
(`docs/reference/android_mouse.md`).

**Retail's Video rows** (P8). With `ext.phone_menu` on, Options > Video's Resolution, Refresh Rate,
Fullscreen and VSync are disabled on the phone (they change nothing there; VSync is kept on, so the
Frame rate cap is the only frame-rate control): `docs/android/P8_INPUT_OPTIONS.md` round 2.
**Phone defaults on an existing file** (P8): each start appends the defaults whose key the file lacks,
never changing a key it has. A
uinput mouse for tests: `adb shell uinput <file.json>` (the script in that reference).

## Logs

- `adb logcat -s OpenBA` follows `standalone_error.log` live (a thread in `app_android.cpp` copies every
  line it gains to logcat), plus the crash dump lines. `tools/android.sh logcat` adds SDL's, the Vulkan
  loader's and the system crash reporter's tags.
- `standalone_debug.log` (stdout) is only in the file.
- `tools/boot_state.sh --android` reads the state file over adb with the Windows script's verdicts and
  exit codes: the pid is checked with `pidof <package>`, the freshness with the file's mtime against the
  phone's clock.

## Crashes: tombstone and ndk-stack

The crash handler (`src/diag/android/crash_handler_android.cpp`) writes `standalone_crash.log` in the
usual shape (arm64 registers, `Fault:` from the ESR, the stack walked from the signal frame's pc / lr /
frame-pointer chain) and copies it to logcat, then hands the signal to Android's crash reporter, which
writes a tombstone. To symbolise either:
```bash
# the unstripped library Gradle built (the apk carries a stripped copy); RelWithDebInfo for the release apk
SO=$(ls android/app/build/intermediates/cxx/Debug/*/obj/arm64-v8a/libmain.so)
# frames #1 and up are return addresses: pass offset - 1 to land on the call's own line
NDK="$ANDROID_HOME/ndk/30.0.16248370"
# one frame of standalone_crash.log ("libmain.so + 0xA74D14"):
"$NDK/toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-addr2line.exe" -f -C -i -e "$SO" 0xA74D14
# the whole tombstone from logcat:
adb logcat -d | "$NDK/ndk-stack.cmd" -sym "$(dirname "$SO")"
```
`adb bugreport` collects the tombstones (`/data/tombstones` is not readable without root).

## Lifecycle

Going to the background tears the window down without waiting for a Vulkan app. The Vulkan backend
installs an SDL event watch (`src/render/vulkan/device.cpp`, THE MOBILE LIFECYCLE): on
`SDL_EVENT_WILL_ENTER_BACKGROUND` it waits for a frame still being presented, idles the device and
destroys the swap chain and the surface; SDL then blocks the main thread until the app returns. On
`SDL_EVENT_DID_ENTER_FOREGROUND` the next frame makes the surface and the swap chain again. Nothing calls
`exit()`. Since P7 the same watch writes the pipeline cache and the warm list when the app goes to the
background (Android may kill it there without a quit).

A second watch, the platform's (`src/platform/android/app_android.cpp`, THE LIFECYCLE WATCH), pauses
the boot-state watchdog's hang budget while the app is in the background (the state file says
`detail=background`; `src/diag/android/boot_state_background.h`) and turns `SDL_EVENT_LOW_MEMORY` (in the
foreground) and `SDL_EVENT_TERMINATING` into a save request: the next game update starts the game's own
autosave (`Platform::App::TakeSaveRequest`, `GameUpdateLoop::Update`). Every `onTrimMemory` reaches SDL as
LOW_MEMORY, including the one each trip home brings; in the background the main thread is blocked and
such a request is dropped. To check by hand: `adb shell input keyevent KEYCODE_HOME`, then
`adb shell am start -n org.openba.game/org.openba.game.OpenBAActivity` brings the running game back;
`adb shell am send-trim-memory org.openba.game RUNNING_CRITICAL` in a running town writes `Save/Auto.sav`.

## Release

`.github/workflows/release.yml`, job `android`, builds and signs the apk next to the three desktop packages:
`OpenBA-v<version>-android-arm64.apk` (+ `.sha256`) is a release asset on a `v*` tag and the run artifact
`package-android` on a manual dispatch. The version is the root `CMakeLists.txt`'s `project(... VERSION x.y.z)`:
`android/app/build.gradle` reads it (versionName `x.y.z`, versionCode `x*10000 + y*100 + z`), so the release bump
stays the one CMake edit. The job installs the pinned SDK packages (`ndk;30.0.16248370`, `cmake;3.31.6`,
`platforms;android-35`, `build-tools;36.0.0`) into the runner's SDK, runs Gradle on JDK 21 (Temurin), builds with
`tools/android.sh build release --ccache` and signs with `tools/android.sh sign`:

1. `zipalign -f -P 16 4` (16 KB pages for the uncompressed native libraries), then
2. `apksigner sign` (v2 / v3 by minSdk 29; no v1), the passwords read from the environment (`--ks-pass
   env:...`), then `zipalign -c -P 16 4` and `apksigner verify --verbose --print-certs`.

**What the release library is built with** (P12). The release flavour (-O3, as the desktop packages; the
apk was the perf flavour's -O2 before), ThinLTO, and the clang profile `perf/pgo/android/openba.profdata`
when it exists (`perf/pgo/android/README.md` has the training recipe; without it the build is plain).
`libmain.so` is linked with hidden visibility and exports `main` alone: every cross-file call inside it is
direct instead of through the PLT, and the loader has no symbol table of tens of thousands of entries to
relocate. The crash and hang dumps therefore print `libmain.so + <offset>` without a function name; the
unstripped library symbolises them as before (Crashes below). Gradle keeps `-g` in the unstripped copy.

The key: the repository's Actions secrets `ANDROID_KEYSTORE_BASE64` (the PKCS12 keystore, base64),
`ANDROID_KEYSTORE_PASSWORD`, `ANDROID_KEY_ALIAS`, `ANDROID_KEY_PASSWORD`. The keystore is decoded into the
runner's temp folder (outside the workspace, mode 600, removed when the step ends) and reaches only the signing
step; Gradle, the native build and the caches never see it. Signing in a step of its own rather than a Gradle
`signingConfig`: the build is the same command as on a developer's machine, and the secrets stay out of the
Gradle daemon and every build script. The release keystore itself (alias `openba`, RSA 4096) is kept off-tree by
the owner; nothing of it is in the repository.

The manifest files the app as a game (`android:appCategory="game"`, and the older `android:isGame`), which is
what the system's game list, Game Mode and the vendors' game launchers read.

A local check of the same commands with the debug key (`adb install` replaces a debug install: same key):
```bash
tools/android.sh build release --jobs 8
OPENBA_KEYSTORE=~/.android/debug.keystore OPENBA_KEY_ALIAS=androiddebugkey \
OPENBA_KEYSTORE_PASSWORD=android OPENBA_KEY_PASSWORD=android tools/android.sh sign
tools/android.sh install release          # apk/release/app-release.apk
```
`ci.yml` builds the debug apk on every PR (job `Android (arm64, debug apk)`); it is not one of the four
required checks.

## Pre-rotation

The S25's panel is portrait, so a landscape app sees `currentTransform = ROTATE_90`. The swap chain is
made with that transform and portrait images; the game renders its landscape frame into an image of the
backend's own and the frame's end draws it, turned, into the swap-chain image (`src/render/vulkan/present.h`;
shaders `present/present.vert`, `.frag`, compiled by `tools/vulkan/gen_present_spirv.sh`). Without that the
compositor rotates every frame and every present answers `VK_SUBOPTIMAL_KHR`.

## Validation layers

The Khronos validation layer runs in the **debug** apk only (it is debuggable; `run-as` and the GPU
debug-layer settings need that). The layer is not in the tree or the apk: take the arm64-v8a
`libVkLayer_khronos_validation.so` from the Vulkan-ValidationLayers release asset
`android-binaries-<ver>.zip` (the Windows Vulkan SDK has no Android binary; P7 used 1.4.363.0), then:
```bash
tools/android.sh install debug
tools/android.sh validation on <dir>/android-binaries-<ver>/arm64-v8a/libVkLayer_khronos_validation.so
tools/android.sh run BANISHED_AUTOSTART=1 BANISHED_PAUSEMENU=3000 BANISHED_PAUSEMENU_QUIT=120 \
                     BANISHED_PAUSEMENU_BUTTON=6
tools/android.sh pull <dir>                # grep '\[Vulkan\] validation' <dir>/standalone_error.log
tools/android.sh validation off            # the settings deleted, the layer removed from the app
```
`validation on` copies the layer into the app's private folder (`run-as <pkg> cp`) and sets
`enable_gpu_debug_layers`, `gpu_debug_app`, `gpu_debug_layers`, `gpu_debug_layer_app`; the Android loader
then lists the layer to that one app, and the debug flavour enables it (`[Vulkan] instance: ...,
validation on`, `validation=1` on the device line). Messages land in `standalone_error.log` as
`[Vulkan] validation ERROR|warning: ...` through the backend's debug messenger (logcat tag OpenBA; the
layer's own VALIDATION tag stays empty while a messenger is installed). The layer stops repeating one
message after 10 (`duplicate_message_limit`), so a count is per distinct message, capped. Layer settings
are system properties: `adb shell setprop debug.vulkan.khronos_validation.<setting> <value>`.

P7 results (2026-10-25, menu + new game + pause-menu quit, 7,220 frames): before, 4 errors (the present
source's first-frame layout; the byte-4 vertex attribute against an `int4` input, three pipelines) and the
depth-only pass's unused `SV_Target` warning; after, 0 errors and the warning
(`docs/android/P7_FIRST_PASS.md` §A).
