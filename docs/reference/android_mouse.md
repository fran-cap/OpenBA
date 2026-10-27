# Android: the mouse, pointer capture and SDL's Java glue

Facts for the Android build (S25, Android 15 / API 35, SDL 3.4.18 pinned in `third_party/SDL`).

## How a mouse reaches the game

- Uncaptured, a mouse's events arrive at `SDLSurface.onTouch` (button down / up and dragged motion,
  `TOOL_TYPE_MOUSE`, `getButtonState()`) and at the generic-motion listener (`ACTION_HOVER_MOVE`,
  `ACTION_SCROLL`). Both call `SDLActivity.onNativeMouse(buttonState, action, x, y, relative)`;
  `SDL_androidmouse.c` `Android_OnMouse` turns `ACTION_DOWN` / `ACTION_UP` into the button whose bit
  changed against the state it last saw, `ACTION_MOVE` / `ACTION_HOVER_MOVE` into motion.
- The game reads buttons through `SDL_GetGlobalMouseState` (`src/platform/sdl/input_sdl.cpp`,
  `ReadMouse`); Android has no global-state backend, so that is SDL's event-tracked state.
- While the middle button is held, `SetRotateDrag` turns on relative mouse mode; SDL's
  `SDLGenericMotionListener_API26.setRelativeMouseEnabled` calls `View.requestPointerCapture()`.

## The defect (SDL 3.4.18), and the workaround

With the pointer captured, Android delivers the mouse to `View.onCapturedPointerEvent` with source
`SOURCE_MOUSE_RELATIVE`: `ACTION_MOVE` (motion with a button held; `getX/getY` are deltas),
`ACTION_BUTTON_PRESS` / `ACTION_BUTTON_RELEASE`, `ACTION_DOWN` / `ACTION_UP`, `ACTION_HOVER_MOVE`,
`ACTION_SCROLL`. SDL's `SDLSurface.onCapturedPointerEvent` hands the event to the generic-motion
listener, whose mouse arm keeps only `ACTION_SCROLL` and `ACTION_HOVER_MOVE`. Every button change and
every held-button motion is dropped, so the middle button's release never reaches SDL: the game keeps
rotating and never releases the capture.

`android/app/src/main/java/org/openba/game/OpenBASurface.java` (made by `OpenBAActivity.createSDLSurface`)
overrides `onCapturedPointerEvent`: `ACTION_MOVE` -> `onNativeMouse(0, ACTION_MOVE, dx, dy, true)`;
`ACTION_BUTTON_PRESS` / `RELEASE` -> `onNativeMouse(getButtonState(), ACTION_DOWN / ACTION_UP, 0, 0, true)`;
the plain `ACTION_DOWN` / `ACTION_UP` beside them are consumed (a second call would change no bit and
send SDL button 0); everything else goes to SDL's own handler. A fix in SDL itself would do the same in
`SDLSurface.onCapturedPointerEvent`; until an SDL bump carries one, the override stays.

## Units and batching of a captured mouse

- A captured `ACTION_MOVE`'s x / y are the device's own counts (10 counts injected -> 10.0), unscaled and
  unaccelerated: the units SDL's relative motion has on Windows (raw input, `SDL_HINT_MOUSE_RELATIVE_SYSTEM_SCALE`
  off), so the same physical motion turns the camera the same amount once all of it arrives.
- Android batches one frame's samples into one `ACTION_MOVE`: 20 events of 10 counts 1 ms apart arrived as
  `ACTION_MOVE`s with 1, 8, 7 and 0 historical samples. `getX(0)` is the last sample only; the
  deltas are `getHistoricalX(0, h)` for every h plus `getX(0)`. Reading the last alone lost most of the
  motion (the slow rotate the owner saw); `OpenBASurface` sums them (P8 round 2).

## Where the pointer is when a capture ends

Android shows the pointer where the capture began; SDL's position (`SDL_GetMouseState`) has moved by the
drag's relative motion meanwhile, and `SDL_SetRelativeMouseMode(false)` re-sends that moved position (no
warp exists on Android). The first hover motion after the release then snapped SDL's cursor back
(measured: drag began at x 802.5, SDL at 1002.5 at the release, next motion 833.1). Since P8 round 2
`SetRotateDrag` (src/platform/sdl/input_sdl.cpp) warps SDL's position back to where the drag began when
`Platform::Input::RotateDragReturnsCursor()` (true on Android) and drains the warp's motion: after the
release the next motion continues from the start point (802.5 -> 833.1, +30.6 for the two moves). A new
input device's pointer starts at the screen's centre (each uinput test mouse does); a KVM that
re-announces its mouse would also put the pointer there: not reproduced with the owner's hardware.

## Testing without a mouse: a uinput device

`adb shell uinput <file>` (Android 14+, runs as shell) makes a virtual evdev device from JSON lines;
the device exists while the command runs. A three-button mouse, a middle press, a drag, a release:
```
{"id":1,"command":"register","name":"test mouse","vid":4660,"pid":22136,"bus":"usb","configuration":[{"type":"UI_SET_EVBIT","data":[1,2]},{"type":"UI_SET_KEYBIT","data":[272,273,274]},{"type":"UI_SET_RELBIT","data":[0,1,8]}]}
{"id":1,"command":"delay","duration":3000}
{"id":1,"command":"inject","events":[1,274,1,0,0,0]}
{"id":1,"command":"delay","duration":1000}
{"id":1,"command":"inject","events":[2,0,60,0,0,0]}
{"id":1,"command":"delay","duration":1000}
{"id":1,"command":"inject","events":[1,274,0,0,0,0]}
{"id":1,"command":"delay","duration":2000}
```
(`events` are `type, code, value` triples: EV_KEY 1 / BTN_MIDDLE 274, EV_REL 2 / REL_X 0, EV_SYN 0.)
`adb shell dumpsys input | grep -i "pointer capture"` shows the capture: `Current Window with Pointer
Capture: ...OpenBAActivity` while the button is held, `None` after the release.

`adb shell input tap` is a touch, which the game sees as a left click only when held (a 1-frame tap is
missed by the polled mouse): `adb shell input swipe X Y X Y 200` clicks.

## Multi-touch without fingers: a uinput touchscreen

`adb shell uinput` also makes a direct multi-touch device (`INPUT_PROP_DIRECT`, `ABS_MT_SLOT`, `ABS_MT_TRACKING_ID`,
`ABS_MT_POSITION_X / Y`, `BTN_TOUCH`); Android maps it to the built-in display in the panel's natural (portrait)
axes, so a landscape point (x, y) on the S25 is sent as (1079 - y, x). `tools/android_multitouch.py` writes the
script: taps, one-finger drags (with a hold before the lift) and two-finger pinch / spin / pan, each frame 16 ms
apart. SDL delivers it as `SDL_EVENT_FINGER_*` with distinct finger ids, the same path as real fingers
(`docs/android/P11_TOUCH_POINTER.md` used it for every gesture). Push the file to `/data/local/tmp/` with
`MSYS_NO_PATHCONV=1`; the device exists only while the command runs.
