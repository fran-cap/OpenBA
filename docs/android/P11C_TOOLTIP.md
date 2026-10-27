# P11c: the press-and-hold tooltip floats above the finger (ext.phone_menu)

Ruling (owner, 2026-10-26, real-finger test of the P11b apk, NEXT_SESSION.md §2.11): the press-and-hold tooltip
works but sits under the finger -> while the pointer is a touch, a tooltip goes about the lift distance (160 px)
ABOVE the finger, inside the safe area, flipped below only when there is no room above. A mouse keeps retail's
placement.

## Where retail puts a tooltip

`UserInterface::BeginToolTip` (0x180120a27) and the tooltip MOVE arm of `Element::Update` (every frame while the
pointer rests) both call `SetUpperLeft(x, y - h)`: the tooltip's bottom-left on the pointer, clamped to the screen.
The frame's layout then lays the dialog out at that upper-left (`DialogInstance::ComputeLayout`, plus the ext
rectangle of `UserInterface::_extDialogPlace`). The ext point exists before and after the clamp, so no engine seam
was added.

## What was built

- `src/ext/phone_menu/hold_math.{h,cpp}` (pure): `HoldTipLiftDevicePx` (ext.touch_pointer_lift with that row on,
  else `kHoldTipLiftDevicePx` 160), `DeviceToLayout` (device px x `Platform::Touch::EnginePixelsPerDevicePixel`
  x `_uiScale`: the render scale / UI scale coupling of `ui_render_scale.h`; 64 units on the S25 at Half / UI 40),
  `PlaceHoldTip` (bottom `lift` above the finger; else top `lift` below it; else the safe top; x = retail's, the
  left on the finger, pulled inside the safe edges; an axis the tooltip does not fit between the margins uses the
  whole screen, SafeArea's rule).
- `src/ext/phone_menu/touch_hold.cpp` `PlaceHeldToolTip`: with the row on, the pointer a touch and `dialog` the
  tooltip up:
  - `kExtPlaceLayout`: the finger is the cursor (ext.touch_pointer holds it on the press point), read as
    `UserInterface::Update` reads it; the tooltip's padded size times its scale k (a tooltip follows its owner's
    fitted scale); the rectangle is the offset from retail's stored upper-left to `PlaceHoldTip`'s, so it works
    whichever units the owner hit-tested in (the scaled toolbar's or a fitted window's).
  - `kExtPlaceUpperLeft`: answers retail's bounds. Without it, phone_menu's P11b arm stores the LAID-OUT upper-left
    (`LaidOutUpperLeft`), and the next MOVE arm's `SetUpperLeft(x, y - h)` moved the laid-out tooltip by the
    difference: back under the finger (seen on the first phone run for the Fixes and Extensions dialog; the
    toolbar's tooltips never reached that arm because ext.touch_mode owns them).
- `phone_menu.cpp` `Place`: one call after ext.touch_mode's arm (so a toolbar tooltip is placed too) and one
  `if (heldTip) return;` after `TouchFirstPlace` (its own commit).
- Tests: `tests/unit/ext_phone_hold_test.cpp` (5: lift source, units, above, flip / no room, safe clamps).

## Measured on the S25 (render Half, UI 40, ext.touch_pointer Default: lift 160)

uinput holds (`tools/android_multitouch.py drag:X,Y,X,Y,3500`), screenshot 2 s into the hold. Gap = finger y minus
the tooltip's bottom, device px:

| Hold | Finger | Tooltip bottom | Gap | Shot |
|---|---|---|---|---|
| Housing sub-toolbar, Wooden House (costs tooltip) | (936, 714) | 550 | 164 | `a2_house_hold.png` |
| Toolbar Town Services, Housing sub-toolbar open | (1563, 924) | 763 | 161 | `b2_toolbar_hold.png` |
| Toolbar Removal tools (right end: pulled in to the safe right edge) | (2025, 924) | 763 | 161 | `b_toolbar_hold.png` |
| Fixes and Extensions, Eye-level camera check box | (1619, 398) | 236 | 162 | `d2_ext_checkbox_top.png` |

Before the `kExtPlaceUpperLeft` arm the check-box tooltip had a gap of 3 px (`d_ext_checkbox_hold.png`).
The Options dialog's Game tab controls have no tooltip in retail (`c_checkbox_hold.png`, `c_label_hold.png`), so
the "Options check box" case was taken on the Fixes and Extensions dialog's check box. The flip below was not met
on the phone (no tooltip-bearing control is close enough to the top); the unit test covers it.

Windows: `run-menu` with ext.phone_menu and ext.touch_pointer on boots to the menu (`[PhoneMenu] ... (row on)`).
A mouse hover was not driven unattended; the arm is gated on `Platform::Input::PointerIsTouch()`, false for a mouse.

## Rulings owed

- ext.touch_pointer on with its lift at 0 (a registry value, no preset): the tooltip then sits right above the
  finger (gap 0). Taken literally from the ruling ("the lift value when that row is on"); a floor of 160 is a
  one-line change.
- Flip rule when neither side has room: the tooltip goes to the safe top (above the finger, as far as possible).
- x stays retail's (left edge on the finger), not centred on it.
