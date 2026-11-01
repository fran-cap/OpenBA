# Device simulator (`tools/device_sim.sh`)

Runs the Windows debug exe with an Android device's screen, so the phone / tablet layout of
`ext.phone_menu` ("Phone screen fit"), `ext.touch_mode` ("Touch controls") and `ext.touch_pointer`
("Touch pointer") can be looked at and screenshotted on the desktop.

```bash
tools/device_sim.sh tablet --screenshot shot.png          # build, run, shot, quit (~30 s after the build)
tools/device_sim.sh phone --no-build --ui 42 --view game  # the owner's UI 42, no harness dialogs
tools/device_sim.sh tablet --panels eventlog,town --tap 0.5,0.5
tools/device_sim.sh tablet --set ext.touch_mode_pad_scale=0 --set ext.phone_menu_safe_margin=0
tools/device_sim.sh phone --view options                  # Options open on its Enhanced tab
tools/device_sim.sh phone --view options --set GameLastOption=0   # ... on its Game tab (0..4 = Game..Enhanced)
```

`--set KEY=VALUE` (repeatable) writes one more settings.cfg key after the profile's; the saved settings.cfg is
put back after the run as usual. `--view options` opens the pause menu at frame 600, clicks its Options button
(MainMenuDialog id 4) 60 frames later with `GameLastOption=4` so the dialog opens on the Enhanced tab, takes the
shot 120 frames after that, and ends the run by its own process id once the shot is written (nothing in the
harness clicks the dialog shut).

Options are in the script's header. It uses the functional game dir only
(`tools/game_dirs.sh env device-sim functional`). It saves that dir's `settings.cfg`, writes the profile into
it and puts it back after the run. The perf dir and the user's own settings are never touched.

## Profiles

| Profile | Window (pixels) | Source |
|---|---|---|
| `phone` | 2340 x 1080 | Galaxy S25 (`docs/android/TOUCH_MODE.md`, the reference device) |
| `tablet` | 3392 x 2400 | OnePlus Pad (issue #96) |

Both profiles write the keys of the Android first-run seed (`kPhoneSettings`, `src/platform/android/app_android.cpp`):
- `ext.phone_menu=1`, `ext.touch_mode=1` (`ext.touch_mode_toolbar_scale=200`), `ext.touch_pointer=1`;
- `ext.fps_cap=1` at 60;
- the graphics extensions off;
- `GameEdgeScroll=false`, `VideoMultisample=0`, `VideoShadowScalar=2`, `VideoRenderScale=50`.

They also write the windowed size (`VideoWindowWidth/Height`, `VideoFullscreen=false`) and `GameUIScale`.

## What the layout reads, and why the simulation is faithful there

- **Size, not density.** The three extensions lay out from the back buffer's size and `UserInterface::_uiScale`
  (`touch_math.h`: fractions of the short edge, in layout units; `UiScaleFloorNow`: the screen extent).
  Nothing in them reads the pixel density. `Platform::Window::GamePixelDensity` is read only by
  `display_density.cpp` (first-run UI scale and slider range) and `ext.sharp_ui`'s font pick.
- **Android's density is 1.** SDL's Android windows are pixel-sized, without `SDL_WINDOW_HIGH_PIXEL_DENSITY`:
  `SDL_GetWindowPixelDensity` is 1 and the screen density is only the display's content scale. So the engine's
  density on a phone is the render scale alone (`PixelDensity = 1 x RenderScale`). Windows answers 1 as well.
- **UI scale 50 = a fresh phone.** The setting is in screen units (`docs/android/README.md` "UI scale in screen
  units"). A first run at the seeded render scale Half gets `FirstRunPercentAt(100, 50) = 50`. The layout space
  is then `screen x UIScale / 100` at any render scale. A desktop at render scale 100 with `GameUIScale=50`
  therefore lays out the same units as the device at Half with 50. The default is `--ui 50`. A player who moved
  the slider has another value: `--ui N`. The issue #96 reporter's value is not known.

## What is not the device

- **Render scale.** Windows renders at full size (`RenderScale()` is 1.0 there; the `VideoRenderScale` key is
  written but ignored). The layout is the same (above). The frame is sharper than the phone's upscaled
  half-resolution frame, and rounding of a fraction can differ by a pixel.
- **The safe area is the extension's margin, not the device's cut-outs.** `ext.phone_menu_safe_margin`, 3.375 % of
  the height by default (`--set` another). No Android insets exist on Windows; `ext.phone_menu` does not read
  them on Android either.
- **Fingers are one synthetic tap.** `BANISHED_FINGER_TAP` pushes an SDL finger down / move / up into the
  event queue, at a window fraction. `ext.touch_pointer` takes it as a touch screen's. With a building armed,
  the tap aims it (the ghost sits `touch_pointer_lift` device pixels above the finger) and the action bar shows.
  A real mouse event in the 3 s that follow hides the bar again (`kMouseHideSeconds`).
- **Panels open where the game puts them.** `--panels` opens the toolbar's utility panels through the
  harness (`BANISHED_TOOL_HARNESS=utility`). They all open at the same top-left anchor, so the last one opened
  covers the others. `town` (`Template\UtilityStatusBar.rsc`) resolves and opens, but nothing showed in the
  shots: unexplained.
- **Mods.** The functional dir's mods are parked. A mod-specific symptom (issue #96: Options under the North 7 mod)
  needs the mod installed and enabled there: copy its `.pkm` into the functional dir's `WinData` for the run only,
  add `--set ModEnabled_<stem>=true --set ModIndex_<stem>=0` (the keys the Mods dialog writes; settings.cfg is put
  back after the run) and remove the file afterwards.
- **DX11.** The exe is the Windows DX11 backend, not Vulkan.

## Capture

The shot is the game's own with-interface screenshot: `BANISHED_SCREENSHOT_AT=<frame>,ui` raises retail's
quickload bit, and `GameUpdateLoop::Draw` step 13 calls `TakeScreenshot` after the interface. That writes the
back buffer to `<user dir>\Screenshots\ScreenshotN.bmp`, which the script converts to PNG and removes. It is
the swap chain's own pixels, so a window larger than the monitor, off screen or covered is captured whole.
`tools/capture_window.ps1` (screen copy) cannot do that.

**Window size on a smaller desktop.** The window is created at the device size, hidden. Two things can shrink
it:
- **A window manager.** PowerToys FancyZones snapped the shown window into a 2528x1345 zone.
- **Windows itself.** `DefWindowProc`'s `WM_GETMINMAXINFO` caps every later resize at the virtual screen's
  height plus the frame (2400 came back 1421 on 1440-high monitors).

The script handles both:
- `BANISHED_WINDOW_ANY_SIZE=1` (harness, `src/app/options.h`) makes the window's subclass
  (`src/platform/win/window_hook_win.cpp`) raise the max track size.
- `tools/device_sim_window.ps1` puts the client size back with `SetWindowPos(... SWP_NOSENDCHANGING)` while
  the menu settles (`BANISHED_AUTOSTART=300`).

The script prints `client A -> B` and warns if the size was not granted.

## Frames

The frames are `GameUpdateLoop` updates at the 60 fps cap:
- 60: the harness opens the panels and arms the building;
- 90: the finger;
- 420: the shot;
- 600: the pause menu opens;
- 720: Quit Game.
