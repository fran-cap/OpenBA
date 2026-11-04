# src/platform/android — Android twins

Android implementations of the platform seams (docs/android/README.md has the app around them). The
replacement rule is the darwin one (`src/platform/darwin/README.md`) with the suffix `_android`:

- On Android (`ANDROID` set by the NDK toolchain), CMake globs `src/platform/posix/*.cpp`, `common/`,
  `sdl/` (as on Linux) **and** `src/platform/android/*.cpp` (`CMakeLists.txt`, the `PLATFORM_SRCS` block).
- A file `android/<name>_android.cpp` **replaces** `posix/<name>_posix.cpp` whole-file: the POSIX twin is
  removed from the build and the Android twin compiled instead. No per-function mixing: the Android twin
  defines every function its POSIX twin did (copy the POSIX file and change what Android needs; the
  banner says which bodies differ).
- An Android file with no POSIX twin of the same `<name>` is simply added.
- Diagnostics follow the same rule independently: `src/diag/android/<name>_android.cpp` replaces
  `src/diag/posix/<name>_posix.cpp` in `DIAG_POSIX_SRCS`.
- Seams first, then twins: platform behaviour goes behind a `Platform::` hook declared in
  `src/platform/common/platform_*.h`, never behind `#if defined(__ANDROID__)` in `src/app`, `src/game` or
  `src/engine`. ARM code is guarded by `__aarch64__`, not by vendor.
- The unit target is not built for Android.

| Twin | Replaces | What differs |
|---|---|---|
| `app_android.cpp` | `posix/app_posix.cpp` | `EnterRunDirectory`: the data folder becomes the working directory, `WinData/` and `Documents/` are made, a first run gets the phone-default `settings.cfg`, or the tablet one on a screen at least 3.3" on its short edge (a later start appends the defaults whose key the file lacks, `phone_seed.h`); `PreSdlInitHints`: landscape only, the logcat copy of `standalone_error.log` starts; `Shutdown`: the boot state's EXITED, the logcat copy drained; `HasGameFilesScreen` / `OpenGameFilesScreen`: true, and `OpenBAActivity.openGameFilesScreen` through JNI (issue #96; the twins answer false). Also `Platform::Android::DataDir`. |
| `file_android.cpp` | `posix/file_posix.cpp` | `ExecutablePath` is `<data>/OpenBA` (the game directory is the data folder); `FileCreateTemp` uses `<data>/tmp`. |
| `settings_android.cpp` | `posix/settings_posix.cpp` | `DocumentsDir` is `<data>/Documents`. |
| `window_android.cpp` | `posix/window_posix.cpp` | `PrepareCreate` adds `SDL_WINDOW_FULLSCREEN` (immersive mode); `RenderScale` / `SetRenderScale` (the live render scale), `RenderScaleAdjustable` and `FixedDisplaySurface` are true. |
| `input_android.cpp` | `posix/input_posix.cpp` | `RotateDragReturnsCursor` is true: after the camera-rotate drag SDL's cursor goes back where the drag began, where Android shows the pointer when its capture ends (P8). Issue #100: the screen keyboard, shown while a text box has the keyboard (`TextEntryChanged`), its keys fed to the poller one per poll (`AddScreenKeys`); hiding it ends the edit as Return. |
| `font_raster_android.cpp` | `posix/font_raster_posix.cpp` | `FindFaceFile` looks for `<face>.ttf` in the data folder only (the game directory; no exe directory, user or system font folder holds it on Android). |
| `display_size_android.cpp` | `posix/display_size_posix.cpp` | `PhysicalScreenInches` asks the activity (`OpenBAActivity.physicalDisplay`, JNI) for the display's pixels and xdpi / ydpi; the first run's seed picks the tablet defaults by it (issue #96). |
| `android_paths.h` | — | `Platform::Android::DataDir()` for the twins above. |
| `src/diag/android/crash_handler_android.cpp` | `src/diag/posix/crash_handler_posix.cpp` | arm64 registers, a frame-pointer walk from the signal frame (no `backtrace()` below API 33), logcat copy, then debuggerd's tombstone. |
| `src/diag/android/boot_state_watchdog_android.cpp` | `src/diag/posix/boot_state_watchdog_posix.cpp` | the hang dump walks each thread from the context its dump signal interrupted. |

Everything else (threads, sync, time, the allocator, CPU topology, the file map, the SDL window, input,
audio and display code) is the POSIX / SDL code unchanged: bionic has the calls it makes.
