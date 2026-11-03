# Touch placement, line tools and picking: ideas (not ruled)

NOT RETAIL. A research sheet for the phone's touch mode. The camera controls (virtual D-pad, rotate,
zoom, the scaled bottom toolbar) are `android-touch`'s design (`docs/android/TOUCH_MODE.md` when it
lands). This sheet covers the next question: how the game's tools work with a finger. Nothing here is
ruled; section 5 lists the rulings needed.

## 1. How input reaches the game today

| Fact | Where |
|---|---|
| The game never sees an SDL event. It polls: `ReadMouse` takes `SDL_GetGlobalMouseState` once a frame, and every tool reads `Input::KeyMap` action ids (`Pressed`, `Released`, `GetValue`). | `src/platform/sdl/input_sdl.cpp` |
| A touch reaches the game only through SDL3's touch-to-mouse emulation (`SDL_HINT_TOUCH_MOUSE_EVENTS`, default on). The port sets no touch or pen hint and reads no `SDL_EVENT_FINGER_*` or `SDL_EVENT_PEN_*` event. | `third_party/SDL/src/events/SDL_touch.c` |
| SDL emulates the FIRST finger only: a motion to the touch point, then a left-button down with the same timestamp. A second finger produces no mouse event until the first lifts. So pinch, two-finger pan and two-finger twist are invisible to the game. | `SDL_touch.c`, `finger_touching` / `track_fingerid` |
| A pen moves the mouse only while it touches the glass (`SDL_pen.c`, `pen_touching`). A hovering S Pen does not move the cursor, so pen hover is no help today. | `third_party/SDL/src/events/SDL_pen.c` |
| A press and release inside one frame are missed by the polled mouse (`adb shell input tap` is not seen; a 200 ms swipe on one spot is). A real finger tap is usually several frames long, but a slow frame can still eat one. | `docs/reference/android_mouse.md` |
| After a lift the cursor stays where the finger was. Retail then treats it as a parked mouse: the placement ghost stays there, and a UI tooltip opens on the tapped button after 0.25 s of stillness. | `src/engine/ui/dialog.cpp` (hover timer, `kTooltipDelaySeconds`) |
| Android-only input code is small: the rotate drag's cursor return (`input_android.cpp`) and the captured-mouse fix (`OpenBASurface.java`). Both serve the KVM mouse; neither knows about fingers. | `src/platform/android/input_android.cpp`, `android/app/src/main/java/org/openba/game/OpenBASurface.java` |
| Edge scrolling is off on the phone because a finger held at the edge is a parked cursor. | `src/platform/android/app_android.cpp`, `kPhoneSettings` |
| The platform already has the seams a touch layer needs: `PreprocessPumpEvent` / `ConsumePumpEvent` see every SDL event first, and `PolledMouseButtons` rewrites the polled buttons. `src/diag/mouse_inject.cpp` shows the other half: writing pointer and button edges into `Input::Device` after the poll, before the UI and tools read it. | `src/platform/common/platform_input_hooks.h` |

The KeyMap action ids the tools read (names from the callers' comments):

| Id | Action | Read by |
|---|---|---|
| 0, 1 | cursor x, y | every tool's ray (`PlayerRay_GetMouseRay_1005a0`), UI hover |
| 2 | mouse dx (rotate drag) | camera update, `camera_input.cpp` |
| 4 | wheel | camera zoom; UI list scrolling |
| 5 | primary button (left click) | every tool's `OnKey`, `EntityUI::OnUpdate`, `ToolUI` dialog drag |
| 6 | cancel (right click) | `PlayerToolManager::UpdateHotkeys` |
| 7 | middle button (rotate drag) | camera |
| 9, 10 / 0xb-0xe / 0xf, 0x10 / 0x11, 0x12 | rotate / pan / zoom / pitch keys | camera |
| 0x13, 0x14, 0x15 | rotate building CCW, CW, flip | `PlacementTool::OnKey`, `FencedTool::OnKey` |
| 0x16 | road route modifier (held) | `RoadTool::OnKey` (sets `modifierFlag` for `Map::FindRoadPath`) |
| 0x17, 0x2d-0x31 | pause, camera views | `UpdateHotkeys` |

## 2. The tool inventory in the port

The tool registry (`EntityUI` setup, `src/game/Dialog/entityui.cpp`) maps a toolbar entry's
description type to a tool class. `PlayerToolManager::Update` (`src/game/Dialog/entityui.cpp`) runs
each frame: `UpdateHotkeys` (the tool's `OnKey`, then right-click cancel), the camera, then the tool's
`Update`. `inputFlag` is "no dialog holds the mouse". All tools live in `src/game/Player/`.

### 2.1 Place a building with rotation: `PlacementTool`

| | Mouse today | With a finger |
|---|---|---|
| Aim | `Update` calls `GetPlacement` every frame on the cursor ray; the ghost mesh, the footprint tiles (`Draw`) and the cost bar follow the hover. | No hover. The ghost sits where the last touch ended. |
| Commit | `OnKey`: `Pressed(0, 5)` places at once through `GetPlacement` + `CreateAt` (place or error sound). | The press that moves the cursor to the new spot is the press that places. The player never sees the ghost at the spot before it is built: placement is blind. |
| Rotate / flip | Keys 0x13 / 0x14 / 0x15 change `RotationSetting::rotation` or call `FlipVariant`. | No keys. Unreachable without a keyboard. |
| Cost / validity | `PlayerToolManager::UpdateCreateBar` (`tool_base_createbar.cpp`) centres the CreateBar dialog over the projected footprint, smoothed 0.25 / 0.75. | It is centred under the finger, so the finger hides it while aiming. |
| Cancel | Right click: `UpdateHotkeys` calls `OnCancel`; `PlacementTool` answers 0, so the manager drops back to `EditTool`. Not honoured while `inputFlag` is 0 or a tutorial forces the tool (`field_0x80`). | No right button. |

### 2.2 Drag-line tools: `RoadTool`, `BridgeTool`, `FencedTool` (all `DragTool`)

`DragTool::OnKey` (`dragtool.cpp`) is the shared state machine:

1. Idle, `Pressed(0, 5)`: the start cell is the cell under the cursor (`PlayerRay_ScreenToCell_101270`); open the CreateBar.
2. Dragging: each frame the end cell follows the cursor, `CalcCost` previews the route, a tick plays per new cell.
3. Release on a different cell: `CalcPath` commits.
4. **Release on the start cell: `singleClick` mode.** The end cell keeps following the hover and the NEXT press commits. Retail already has a tap-start, tap-end mode.
5. Right click mid-drag: `DragTool::OnCancel` drops the drag and keeps the tool; idle, it answers 0 and the tool ends.

`RoadTool::OnKey` adds the held modifier 0x16. `FencedTool::OnKey` adds rotation 0x13 / 0x14.

With a finger:
- Press-drag-release already works (the first finger is the left button). The finger hides the route's end and the cost bar.
- The camera cannot move during a drag. Edge scroll is off, and SDL hides a second finger.
- Commit on release cannot be taken back: a misplaced road costs a removal pass.
- The tap-tap mode is blind at the end: with no hover, the second tap's press commits at a cell the player never previewed.
- The route modifier and fence rotation need keys.

### 2.3 Drag-rectangle tools: `RemovalTool`, `DragPlacementTool` (both `DragTool`)

- **`RemovalTool`** covers clear, remove roads and the other area marks; its description's flags pick what it takes. Its `CalcCost` takes the bounding rectangle of start and end cells.
- **`DragPlacementTool`** covers crop fields, orchards, pastures and stockpiles: the dragged rectangle becomes the footprint.

Both use the same state machine as 2.2. A finger makes the same complaints, and two are worse:
- The rectangle's far corner is under the finger.
- Large areas need the camera to move mid-drag.

### 2.4 Selection and picking: `EditTool`, `PathTool`

- **`EditTool::OnKey`** is the idle tool (`edittool.cpp`). On `Pressed(0, 5)` it casts one ray from the cursor pixel:
  - the terrain march (`WorldGeometry::TerrainMarch`) gives a cell, and `Map::EntityAtCell` gives the building on it;
  - then `Picking::Scene::Intersect` (triangle-exact, capped at the terrain distance) overrides it with any mesh hit.
  - A new entity goes to `SelectEntity`, which opens its info panel.
- **`PathTool`** is the "Paths" report tool: it shows worker home-work routes. Its `OnKey` makes the same single-ray pick.

With a finger:
- A building is a large target and the cell fallback forgives a near miss.
- A citizen or animal is a few pixels of moving triangles, so a finger's contact centre rarely lands on it. There is no radius, no nearest rule and no cycling.

### 2.5 Panels and dialogs that follow an entity

- **`EntityUI`** is the info panel. `OnUpdate` (slot 8) uses `Pressed(0, 5)` over the panel to re-select its entity, and its pin check box keeps it open.
- **`ToolUI`** (`src/game/Dialog/toolui.cpp`) drags a dialog once the press moves more than 5 px. This works with a finger.
- **Hover tooltips** need the mouse still with the button up for 0.25 s. After a tap the parked cursor satisfies that, so every tapped button opens its tooltip, and it stays until the next touch elsewhere.
- **Lists** scroll with the wheel (id 4) or the scroll bar. A finger has only the bar.

### 2.6 Summary of what a finger lacks

| Mouse feature | Used by | Touch status |
|---|---|---|
| Hover (aim before commit) | PlacementTool ghost, DragTool single-click end, tooltips | absent; the cursor parks where the last touch ended |
| Right click | cancel everywhere | absent |
| Wheel | zoom, list scroll | absent (second finger is invisible) |
| Precise pixel | citizen / animal pick | contact patch is about 7-10 mm, roughly 115-165 px on the S25's 2340x1080 6.2-inch panel (about 416 ppi) |
| Keys 0x13-0x16 | rotate, flip, road modifier | absent without a keyboard |
| Middle drag | camera rotate | `android-touch`'s buttons |

## 3. How other games handled it

| Game | Placement confirm | Rotation | Line / area | Picking / hover | Players said | Sources |
|---|---|---|---|---|---|---|
| Mindustry (touch-native) | Tap queues a ghost plan; a tap on a plan removes it; nothing is built until a check button (`confirmplace`) is pressed, which re-validates each plan. | On-screen rotate button. | A long press starts line mode, a drag extends it and a lift queues it. Area select uses corner drags with a drawn box and auto-pans near the edge. | A drag starting on a plan moves the plans; a drag anywhere else pans. A Cancel button clears the queue. | Not reviewed in depth. | [1] |
| OpenRCT2 (Android) | First port: one touch = left press, a second touch within 300 ms = right click; the finger was a bare cursor. PR #24257 (open, opt-in): first tap selects or previews, second tap confirms; a drag pans. | n/a | Land tools: the first touch selects, a drag from there does not pan. | n/a | Forum: a stylus or mouse was "practically needed". A maintainer called the #24257 demo "much better than what we currently have". | [2]-[6] |
| OpenRCT2 PR #27053 (proposal, unreviewed) | A tap only positions; one action-bar button is the only commit (Build / Destroy); Cancel places nothing. | In the action bar. | Area size by +/- buttons, so a drag always pans. A second finger landing drops the interaction. | First tap previews, second confirms. | n/a | [7] |
| RollerCoaster Tycoon Classic (mobile) | Tap to position, then a Build button. | Buttons. | Footpaths need "a lot of taps, and plenty of mistakes". | n/a | Buttons need "a delicate touch". The Switch release added touch in 2025 after complaints and kept a cursor. | [8]-[10] |
| OpenTTD (Android port) | Tool on one finger. | n/a | "Scroll the map with two fingers when you are building roads or stations." | n/a | Bug: the release sent a second click, so toggles flipped back. | [11], [12] |
| Civilization VI (iPad / iPhone) | Drag a unit to a tile. | n/a | n/a | Press-and-hold for tooltips. | Praised as "brilliantly translated", with Next Turn in a corner. On iPhone, "difficult to select a specific unit". | [13], [14] |
| Civilization VI (Switch) | n/a | n/a | n/a | n/a | Units were sometimes sent to the wrong tile. | [15] |
| Townscaper (mobile) | Tap places, hold removes; undo/redo. | n/a | n/a | One finger rotates the view, two fingers pan. | Haptics praised; "frustrating and limited at times". The gesture details rest on one review seen in search snippets. | [16], [17] |
| Pocket City | Roads and zones are drag-painted. | n/a | Paint by dragging. | n/a | "Never an issue where I build something I'm not supposed to." Complaint: the camera does not follow the finger while laying roads. | [18], [19] |
| SimCity BuildIt | Drag and drop; a long press on a building enters move mode. | Unverified. | n/a | n/a | n/a | [20], [21] |
| Factorio (Switch) | Joy-Con mouse mode for placement. | n/a | n/a | n/a | Touch "fairly imprecise outside of the menu"; hard to pick a single item. | [22]-[24] |
| Terraria (mobile) | Smart Cursor draws a yellow box on the target tile; manual mode places blocks. | n/a | n/a | The highlighted target stands in for hover. | n/a | [25] |
| RimWorld (Steam Deck patch) | n/a | n/a | n/a | Drag scrolls lists; a held button turns taps into right clicks. | n/a | [26] |
| Cities: Skylines, Tropico 6, Anno 1800 (consoles) | Cursor and radial menus; touch absent or token. | n/a | Anno: roads wrap a block automatically. | n/a | Radials "cumbersome"; placement with sticks "fiddly". | [27]-[30] |

Nothing solid was found for:
- TheoTown, Northgard mobile, The Settlers mobile, Surviving Mars, Prison Architect's placement details, Frostpunk mobile and Dwarf Fortress touch.
- Banished on Android: Winlator adds touch profiles, but no Banished report was found [31].

**Patterns.**
- **Ghost plus an explicit commit button** is the touch-native answer: Mindustry, RCT Classic and both OpenRCT2 touch PRs.
- **Direct place-on-release** works only where actions are cheap or undoable: Pocket City painting, Townscaper's undo.
- **Rotation** is a button. No source showed a two-finger twist.
- **Pan versus tool** is split one of three ways: two fingers pan while a tool is active (OpenTTD); a drag always pans and taps only position (OpenRCT2 #27053); or the drag's start point decides (Mindustry).
- **Hover** is replaced by a press-and-hold tooltip (Civ VI), a first-tap preview (OpenRCT2) or a highlighted target (Terraria).

**Complaints.**
- Small-unit picking and misdirected moves (Civ VI, Factorio).
- No auto-pan while drawing (Pocket City).
- Many taps for paths (RCT Classic).
- The finger as a bare cursor, and a double click on release (OpenRCT2, OpenTTD).

## 4. Ideas for OpenBA, ranked

Every idea is a class A extension under `src/ext/<id>/` (`docs/extensions/DESIGN.md` §3):
- It holds no serialized state and has no sim effect.
- Its switch comes from settings, with a Names-table row.
- The Android first run turns it on, as `phone_menu` and `fps_cap` are.
- It changes only what the input layer hands the faithful tools, or draws an overlay. The tools' bodies stay retail's.

**The common base for all of them: a touch pointer layer** (working id `touch_pointer`). On Android with the extension on:
- **Finger events.** `SDL_HINT_TOUCH_MOUSE_EVENTS` is set to 0, and `ConsumePumpEvent` takes the `SDL_EVENT_FINGER_*` events.
- **Pointer state.** The layer keeps its own pointer (position, a latched button edge, an injected cancel / rotate / wheel edge). It writes that state into `Input::Device` after the poll, as `src/diag/mouse_inject.cpp` does, so every tool, `EntityUI` and the UI read it through the unchanged KeyMap.
- **Latched edges.** An edge is held for at least one game frame, which also fixes the missed one-frame tap.
- **The KVM mouse.** Real mouse events carry their own mouse id, never `SDL_TOUCH_MOUSEID`. With emulation off the physical mouse reaches the game exactly as now. The layer writes only while a finger was the last pointer: the last device wins, and the overlay hides after a few seconds of real mouse use.

**Base size:**
- an Android input twin plus an ext module of about 400-600 lines;
- the overlay buttons, which can share `android-touch`'s on-screen control code.

**Base risks:**
- Writing `Input::Device` must use the same offsets the UI decodes (`userinterface_frame.cpp`), and a missed field desynchronises hover.
- Injected rotate and flip edges must go to the action id, not to a scancode, so a rebinding does not break them.

### 4.1 Building placement (`PlacementTool`)

| Rank | Design | Interaction | Hooks | Size | Risks |
|---|---|---|---|---|---|
| 1 | **Aim, then Place** | In a placement tool, finger down or drag moves the pointer with the button UP. `PlacementTool::Update` makes the ghost, tiles and cost bar follow as they follow a hover. Lifting leaves the ghost parked. A floating action bar (Place, Rotate left / right, Flip, Cancel) sits on the side away from the D-pad. Place injects one action-5 edge at the parked pointer. | No tool change. Place = action 5; rotate and flip = 0x13 / 0x14 / 0x15; Cancel = action 6 with the pointer over the world (`UpdateHotkeys` ignores it while `inputFlag` is 0), or `SelectTool(nullptr)`. | Base + about 150 lines. | One finger now aims instead of panning while a tool is active, so the camera needs the D-pad or two fingers. The tutorial's forced placement (`field_0x80`) must keep working: test it. |
| 2 | **Aim with a lift offset** | As 1, but while the finger is down the pointer sits a fixed distance above the contact point (about 1.5 finger widths, roughly 200 px), with a small marker. The ghost and cost bar are not under the finger. | Base only. | Small on top of 1. | A ghost near the bottom edge needs the camera moved. Players must learn the offset. |
| 3 | **Trackpad drag** | Any drag moves the ghost by the finger's delta, scaled down for precision; the ghost never jumps to the finger. Same action bar. | Base only. | Small on top of 1. | Less direct. A far placement needs a long drag or a camera move. |
| 4 | **Tap twice to build** | The first tap moves the ghost; a second tap inside the ghost's footprint within a short time places it. Rotation still needs buttons. | Base; the ext reads the footprint from the CreateBar's projected rect or `GetPlacement`. | Small. | An accidental double tap builds. It suits players who find buttons slow, so it could be a setting under 1. |
| 5 | **Retail as is** | A tap places at once. | none | 0 | Blind placement. Only the KVM mouse makes it usable. |

A magnifier loupe above the finger, as iOS text selection does it, was considered and left out. It costs a second view render each frame on a phone already at half render scale, and ideas 2 and 3 solve the occlusion for free.

### 4.2 Line tools (`RoadTool`, `BridgeTool`, `FencedTool`)

| Rank | Design | Interaction | Hooks | Size | Risks |
|---|---|---|---|---|---|
| 1 | **Retail's single-click mode, driven by touch** | Tap the start: the layer sends a press and a release on the same cell, so `DragTool::OnKey` enters `singleClick` with retail's own code. Then a finger moving the pointer with the button up previews the route to it, the hover retail expects, with the cost bar. Lift parks it. Confirm injects the second press: `CalcPath` commits. Cancel injects action 6: `DragTool::OnCancel` drops the drag and keeps the tool, as in retail. | No tool change. The road modifier 0x16 is a toggle button shown only while a `RoadTool` is current. Fence rotate uses the same buttons as placement. | Base + about 100 lines. | The start cannot be moved without Cancel and a new tap, which is retail's model too. Moving the end off-screen needs the camera. Add edge auto-pan while the finger is held near the edge, in this mode only (Pocket City's complaint). |
| 2 | **Two handles** | Start and end are both draggable markers, adjusted until Confirm. | Re-creates the drag by injecting press, motion and release on Confirm. | Medium. | The preview runs only while retail's drag state is active, so the handles would preview through an injected drag, which is fragile. Prefer 1. |
| 3 | **Retail press-drag-release plus edge auto-pan and lift offset** | As now, with the offset of 4.1 idea 2 and auto-pan. | Base only. | Small. | Commit on release cannot be undone, and Banished has no undo. |
| 4 | **Two-finger pan during a drag** | A second finger pans the camera while the first holds the drag (OpenTTD). | Base: two-finger motion goes to the camera pan axes. | Small. | It is the most natural on a phone but awkward one-handed. It complements 1 or 3. |

### 4.3 Rectangle tools (`RemovalTool`, `DragPlacementTool`)

The same machinery as 4.2:
- Tap the first corner (retail `singleClick`).
- Drag the pointer to the far corner with the button up; the rectangle and the size text in the cost bar follow.
- Confirm or Cancel.

Edge auto-pan is the most important addition here, because fields and clear areas are large.

The +/- size buttons of OpenRCT2 #27053 fit badly: Banished's rectangles are free-form and often 10-20 cells on a side. Paint-by-drag (Pocket City) does not match retail's rectangle model and would need a new tool, not class A input work.

### 4.4 Picking (`EditTool`, `PathTool`)

| Rank | Design | Interaction | Hooks | Size | Risks |
|---|---|---|---|---|---|
| 1 | **Snap the pointer to the nearest target** | On a tap in `EditTool` or `PathTool`, the ext first casts the retail ray. If it hits nothing pickable, the ext samples rays on rings out to a radius r (about 40-60 px at screen scale) through `Picking::Scene::Intersect`, capped by the terrain as retail does. It chooses the hit nearest the tap in screen space, moves the injected pointer onto that hit's pixel and only then injects the press. `EditTool::OnKey` picks it with retail's code. | Read-only use of `Picking::Scene` and `WorldGeometry::TerrainMarch`; no tool change. | About 150 lines. | 20-40 ray casts on a tap is cheap. When a building is under the finger but a citizen is within r, which wins? Recommendation: prefer a moving entity when the exact ray hit nothing but terrain or a building more than a few px from the tap centre. |
| 2 | **Tap again to cycle** | A second tap within r and a short time of the first selects the next candidate in distance order. | As 1, keeping the candidate list for one second. | Small on top of 1. | It conflicts with 4.1 idea 4 (tap twice) only in placement tools, where it is off. |
| 3 | **Candidate chooser** | When two or more candidates are within r, a small list of their names or icons appears; a tap chooses. | Overlay plus 1. | Medium. | Adds a step for every crowded pick. Use only when 1 and 2 test badly. |
| 4 | **Bigger pick meshes** | Inflate citizens' picking meshes. | `Picking::Scene` data. | Small. | It changes what retail's ray hits on desktop too unless gated, and it touches engine data. Not recommended. |

### 4.5 Hover, cancel, wheel and modifiers

- **Tooltips.** Under touch, a tap should not leave the pointer parked over a UI element:
  - after a lift on a dialog the layer moves the pointer back to its last world position, so placement ghosts stay put;
  - a long press (about 0.5 s) on a UI element holds the pointer there with the button up, so retail's 0.25 s hover opens the tooltip (Civ VI's press-and-hold).
- **Cancel.** The action bar's Cancel button and the Android back gesture inject action 6 with the pointer over the world. Outside a tool, back opens the pause menu.
- **Wheel.** A two-finger vertical drag over a dialog injects wheel steps (id 4), so lists scroll. A pinch over the world injects zoom, unless `android-touch` rules zoom buttons only.
- **Modifiers.** The only game modifier is the road route key 0x16: a toggle button in the road tool's action bar. Shift is read only by the launcher.

## 5. Rulings (all nine RULED as recommended by the owner, 2026-10-26)

Additions from the ruling: pinch = zoom and a two-finger spin = rotate (both through the camera hold hook
`docs/android/TOUCH_MODE.md` §4 exposes); last device wins means a pinch / spin ends on release and the mouse or
the D-pad takes over at once. The extension is Android-seed only. Build order: `ext.touch_mode` (P10) first,
`touch_pointer` (P11) after it.

| Question | Recommendation |
|---|---|
| One finger while a tool is active: aim (ideas 4.1-1, 4.2-1) or pan? | Aim. Pan with the D-pad or two fingers. Idle (`EditTool`), one finger stays free for `android-touch` to use as pan if they rule it. |
| Placement commit: action-bar Place button, or tap twice? | The Place button by default, with tap-twice as a setting. |
| Lift offset for the aiming pointer (4.1-2)? | Yes, on by default under touch, with a setting to turn it off. Try it on the phone before ruling the distance. |
| Line and rectangle tools: retail's single-click mode driven by touch (4.2-1)? | Yes. It reuses retail's own state machine. |
| Edge auto-pan while a finger is held near the edge during a drag tool? | Yes, but only in drag tools and only while a finger is down. Edge scroll stays off otherwise. |
| Pick radius and cycling (4.4-1, 4.4-2)? | Yes: snap-to-nearest with r about 50 px, and tap-again to cycle. |
| Where the action bar sits relative to `android-touch`'s controls? | Right-hand side, above the bottom toolbar, away from the D-pad, so the thumb that aims also confirms. Coordinate with `android-touch` before either lands. |
| One extension or several? | One class A extension, `touch_pointer`, with sub-settings (offset, pick radius, tap-twice). It is on in the Android first-run seed and off on desktop. |
| KVM mouse coexistence: last device wins, overlay hides after real mouse use? | Yes. Never block the physical mouse. |

## Sources

1. Mindustry `MobileInput.java`: https://raw.githubusercontent.com/Anuken/Mindustry/master/core/src/mindustry/input/MobileInput.java
2. OpenRCT2 PR #2520, touch as left press, second touch as right click: https://github.com/OpenRCT2/OpenRCT2/pull/2520
3. OpenRCT2 issue #1496, touch scrolling too fast: https://github.com/OpenRCT2/OpenRCT2/issues/1496
4. OpenRCT2 issue #2091, drag to move the map: https://github.com/OpenRCT2/OpenRCT2/issues/2091
5. OpenRCT2 forum, "OpenRCT2 for Android": https://forums.openrct2.org/topic/5514-openrct2-for-android/ ; "Android question": https://forums.openrct2.org/topic/6136-android-question/
6. OpenRCT2 PR #24257, TouchEnhancements (open): https://github.com/OpenRCT2/OpenRCT2/pull/24257
7. OpenRCT2 PR #27053, action-bar commit (open, unreviewed proposal): https://github.com/OpenRCT2/OpenRCT2/pull/27053
8. Coaster101, RCT Classic review (2016): https://www.coaster101.com/2016/12/31/review-roller-coaster-tycoon-classic/
9. iLounge, RCT Classic app diary (seen through search snippets only): https://www.ilounge.com/index.php/apps/entry/app-diary-rollercoaster-tycoon-classic
10. Nintendo Life, RCT Classic touch update (2025): https://www.nintendolife.com/news/2025/01/rollercoaster-tycoon-classic-receives-much-requested-touch-control-update ; Nintendo World Report review: https://nintendoworldreport.com/review/69361
11. OpenTTD on F-Droid, the port's tips: https://f-droid.org/en/packages/org.openttd.fdroid/
12. pelya/openttd-android issue #24, double click on release: https://github.com/pelya/openttd-android/issues/24
13. CivFanatics, Civilization VI iPad FAQ: https://forums.civfanatics.com/threads/civilization-vi-ipad-faq.628678/
14. Macworld, Civilization VI iPhone review: https://www.macworld.com/article/231955/civilization-vi-iphone-review.html
15. Gamereactor, Civilization VI Switch: https://www.gamereactor.eu/civilization-vi-switch/
16. AppUnwrapper, Townscaper iOS review (2021; seen through search snippets only): https://www.appunwrapper.com/2021/10/20/townscaper-ios-review/
17. TapSmart, Townscaper review: https://www.tapsmart.com/news/townscaper-review/
18. Destructoid, Pocket City: https://destructoid.com/?p=227371
19. iMore, Pocket City review: https://www.imore.com/pocket-city-review
20. EA forums, SimCity BuildIt move mode: https://forums.ea.com/discussions/simcity-buildit-en/re-what-if-i-want-to-change-city-layout/10425925
21. LevelWinner, SimCity BuildIt guide: https://www.levelwinner.com/simcity-buildit-beginners-guide-tips-tricks-strategies/
22. Factorio Friday Facts #370: https://cf-www.factorio.com/blog/post/fff-370
23. Nintendo Life, Factorio review: https://www.nintendolife.com/reviews/switch-eshop/factorio
24. Factorio Friday Facts #439, Switch 2: https://factorio.com/blog/post/fff-439
25. Terraria wiki, cursor modes: https://terraria.wiki.gg/wiki/Cursor_modes
26. Ludeon blog, RimWorld 1.3.3326 (Steam Deck): https://ludeon.com/blog/?p=1500
27. God is a Geek, Cities: Skylines Switch report: https://godisageek.com/reviews/switch-report-review-31-cities-skylines-velocity-2x-subaracity/
28. God is a Geek, Tropico 6 Switch: https://godisageek.com/reviews/tropico-6-switch-review/ ; Nintendo World Report: https://www.nintendoworldreport.com/review/55527/tropico-6-switch-review
29. Ubisoft News, Anno 1800 Console Edition controls: https://news.ubisoft.com/en-us/article/6xDLQxuay1FASfTGQyquJl
30. But Why Tho, Anno 1800 Console Edition review (2023): https://butwhytho.net/2023/03/review-anno-1800-console-edition/
31. Notebookcheck, Winlator 10.1: https://www.notebookcheck.net/Winlator-10-1-update-brings-key-improvements-to-Windows-game-emulator-for-Android.1064597.0.html
