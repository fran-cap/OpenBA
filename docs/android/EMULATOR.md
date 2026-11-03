# OpenBA on the Android emulator

The Android emulator is a test bed for layout and touch changes on a tablet and a phone screen without
a physical device. It runs the x86_64 system image, so the app is built for x86_64 for it; the release
apk and the default debug apk stay arm64-v8a only. The reference device is still the Galaxy S25
(`README.md`): the emulator checks layout, input routes and boot, not performance or the GPU driver.

## Requirements

| Item | What |
|---|---|
| SDK packages | `emulator` (35.3.12 used), `system-images;android-35;google_apis_playstore;x86_64`, `cmdline-tools;latest` (avdmanager), `platform-tools` |
| Hypervisor | WHPX (Windows Hypervisor Platform) or AEHD. `"$ANDROID_HOME/emulator/emulator.exe" -accel-check` must report one as usable; the script never installs one. |
| Host GPU | Vulkan on the host. The AVDs run `-gpu host`: gfxstream forwards the guest's Vulkan to the host driver. The software renderers (`swiftshader`, `guest`) were not tried. |

## Profiles

`tools/android.sh avd create <profile>` makes the AVD with avdmanager from the x86_64 API 35 image and
writes the profile's keys into its `config.ini` (no skin, so the screen is the lcd size).

| Profile | AVD | Serial | Screen | Density | Matches |
|---|---|---|---|---|---|
| `tablet` | `OpenBA_Tablet` | `emulator-5580` | 3392x2400, landscape natural | 320 | OnePlus Pad 3 class (13.2", ~315 ppi; issue #96) |
| `phone` | `OpenBA_Phone` | `emulator-5582` | 1080x2340, portrait natural | 420 | Galaxy S25 class (6.2", ~416 ppi) |

Both: 6 cores (the emulator's maximum; it lowers a larger `hw.cpu.ncore` to 6), 6 GB RAM, 512 MB VM heap,
an 8 GB data partition, a hardware keyboard, `hw.gpu.mode=host`. The density is the nearest standard
bucket to the device's physical ppi, not the vendor's logical density. The phone is portrait-natural like
the S25, so the landscape app runs through the backend's pre-rotation path (`README.md`, Pre-rotation);
the tablet is landscape-natural and runs unrotated (`transform 0x1`).

Each profile has a fixed console port, so its adb serial is known: `--avd <profile>` before any
command sets `ANDROID_SERIAL` to it (`tools/boot_state.sh --android` uses it too). `--serial S` picks any
other device.

## Recipe

From Git Bash at the repository root:
```bash
tools/android.sh build debug --emulator --jobs 4       # x86_64 -> android/app/build-emulator/outputs/apk/debug/app-debug.apk
tools/android.sh avd create tablet                     # once (--force makes it again, wiping its data)
tools/android.sh avd start tablet                      # headless; returns when Android has booted
tools/android.sh --avd tablet install debug            # an x86_64 device takes the build-emulator apk
tools/android.sh --avd tablet push-data                # WinData/*.pkg from BANISHED_DIR (read only)
tools/android.sh --avd tablet run BANISHED_AUTOSTART=1
tools/android.sh --avd tablet boot-state --wait 300
tools/android.sh --avd tablet screenshot tablet.png    # adb exec-out screencap -p
tools/android.sh --avd tablet pull <dir>               # the standalone_* logs
tools/android.sh avd stop tablet                       # or `avd stop` for every profile
tools/android.sh avd list                              # profiles, AVDs made, running or not
```

`build --emulator` passes `-Popenba.emulator=true` to Gradle (`android/app/build.gradle`): `abiFilters`
becomes `x86_64` and the build directory `app/build-emulator/`, so the arm64 apk under `app/build/` is
left alone. The native objects go to their own `app/.cxx/.../x86_64` folder.

`avd start` runs the emulator with `-no-window -gpu host -no-audio -no-boot-anim -no-snapshot` (a cold
boot each time, about 30 s here), writes its output to `$TMPDIR/openba-emulator-<profile>.log`, waits for
`sys.boot_completed`, then keeps the screen on (`svc power stayon true`), dismisses the keyguard and
marks the system's one-time "Viewing full screen" notice as seen
(`settings put secure immersive_mode_confirmations confirmed`): on a fresh install that notice takes the
focus at the app's first start, and the game pauses on focus loss (GameAutoPause), so an autostarted town
comes up behind the pause menu. `avd start <profile> --window` shows the emulator's window instead.

Input without a window: `adb shell input tap X Y`, `input swipe`, `input keyevent` (screen pixels,
as `screencap` writes them).

## What works (emulator 35.3.12, RTX 5080 host, WHPX)

- The guest reports `android.hardware.vulkan.version` 0x403000 (Vulkan 1.3.0) and level 1, so the
  manifest's Vulkan 1.3 requirement is met and installs unchanged. The backend's device line:
  `api 1.3.0 ... path 1.3; push_descriptor=0 ... vertex_input_dynamic=0 ... eds2=0 load_store_none=1
  d24s8=1 msrtss=0 ... bc=1 etc2=1`.
- Tablet: boot, the main menu, AutoStart to a new town, at the 60 fps cap (`[FrameLimiter] fps=60.0
  (cap=60.0) in game`), empty `standalone_crash.log`. The first run reports `[Display] 3392x2400 px,
  xdpi 320.0 ydpi 320.0 (density 320 dpi): 10.60x7.50 in` and writes the tablet defaults (`README.md`,
  Tablet defaults: render scale 100, `GameUIScale=50`, toolbar 150 %; `ext.phone_menu`,
  `ext.touch_mode`, `ext.touch_pointer` on). Before issue #96 it got the phone's render scale 50.
- Phone: `[Display] 2340x1080 px, xdpi 420.0 ydpi 420.0 (density 420 dpi): 5.57x2.57 in`, the phone
  defaults; the same route to a town at the 60 fps cap, empty `standalone_crash.log`, through pre-rotation
  (`swap chain 1080x2340 ... transform 0x2 (current 0x2, currentExtent 2340x1080)`, `present pass: frame
  1170x540 drawn into 1080x2340`).

## What does not, or differs from a device

- Performance numbers mean nothing: the CPU is the host's under WHPX and the GPU is the host's through
  gfxstream. Use the S25 for perf (`docs/android/PERF_P7.md`).
- No Adreno driver: driver bugs and Adreno-only behaviour do not show here.
- No ADPF or thermal behaviour of a phone, no big.LITTLE roster (`[Cpu] mask=0x3f logical=6`).
- `push_descriptor` and `vertex_input_dynamic` are off on gfxstream; the backend's fallbacks run instead of
  the paths a phone may take.
- The Khronos validation layer recipe (`README.md`) needs the x86_64 layer build, not the arm64 one.
- The crash handler prints x86_64 registers (`RAX`..`R15`, `RIP`, `RSP`) and READ / WRITE / EXEC from the
  page-fault error code; the frame-pointer walk is the same as on arm64 (`src/diag/android/signal_context_android.h`).
  To symbolise, use the x86_64 `libmain.so` under `app/.cxx` or `app/build-emulator/intermediates`.
- avdmanager prints `Could not load devices from ...\x86_64\devices.xml` at `avd create`; the AVD is made
  anyway (the image ships no devices.xml).
