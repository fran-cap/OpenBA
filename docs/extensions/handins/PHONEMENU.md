# PHONEMENU hand-in

`ext.phone_menu`: the front end fitted to a phone held in landscape (the Galaxy S25: 2340x1080,
19.5:9, rounded corners, a camera cut-out on one short edge). One class A row, **off by default**,
switched on by the Android first-run `settings.cfg` (`src/platform/android/app_android.cpp`), live,
no serialized state. Android fit P7.

## 1. What retail does on a 19.5:9 screen

The main menu is a 3D scene (`Game/MenuScene/Scene%i.rsc`, a season picked per start) under the menu
dialog. The dialog's backdrop (`Dialog/MainMenu.rsc` `_backgroundSpriteSheet`) is a 16:9 overlay: the
Banished logo near the top and the dark torn borders, transparent between them. `UserInterface::Draw`
(retail 0x180121dbe) cover-fits a backdrop and centres it: on a screen wider than 16:9 it scales the
overlay to the screen width and cuts the same amount off its top and bottom. At 2340x1080 that is 118
pixels each side, and the top of the logo is cut off. The button column (`buttonList`, MidCenter) and
the version line (`Dialog/BuildId.rsc`, bottom centre) are centred on the screen.

## 2. What the row does

Measured on the installed overlay: the logo spans 0.330 .. 0.675 of the drawn width (centred on the
overlay's own centre line, within 0.3 %) and 0.039 .. 0.210 of its height.

* **Logo at the top, the buttons as rows of squares** (a screen wider than the overlay; owner's rulings
  2026-10-25: P8 round 2 replaced the first "logo left, buttons right" layout, round 3 made the row wrap
  to two rows). The overlay keeps retail's width fit but its top goes on the screen's top (`LogoTop`),
  so the logo is whole and centred at the top and the cut moves to the bottom border. The pause menu has
  the same backdrop and gets the same treatment. A menu's button list (MainMenu.rsc `buttonList`;
  Pause.rsc's root; `RibbonElement`s laid out top-to-bottom, `_vertical` 1) is reshaped on the menu
  INSTANCE (the description is not touched): the ribbon's `_vertical` cleared, every button's
  `_minWidth` / `_minHeight` set to one side, each label centred (alignment 4) with its icon above its
  text (`LabelElement::_mode` 3, `_align` 4) and no padding of its own. `PlanRows` picks, in order:
  1. one row in the labels' own font;
  2. two rows in the labels' own font: the first ceil(n / 2) squares, the rest centred under them (an
     even count splits in halves), so a button added to a menu wraps instead of breaking the layout
     (unit-tested for n = 8, 9, 10, 11);
  3. one row, then two rows, in `Font/FontSmall.rsc` (the game's own small font);
  4. else retail's column (retail's values restored).
  The small-font step stays (round 3 asked to pick): on the phone the logo takes the top quarter of the
  screen, so two own-font rows need more height than the S25 has at its smaller UI scales (at UI 35,
  two rows need 87 units down, 99 per square); without the step the pause menu would fall to the
  column there.
  The square side fills the safe width for the longer row, is capped at 0.22 of the safe height and by
  the band between the logo and the version line (`RowBand` / `BlockHeight`; the rows are centred in
  that band), and is never smaller than the labels need: the widest label ("Achievements") plus
  `kSquareInset` (5 units) on each side, so the text keeps 2 units of air inside the button frame (the
  owner saw it cross the frame in round 2). The buttons keep retail's art (the bordered button frames
  are nine-sliced, so they draw at any size).
  Two rows are made without new elements: the ribbon still lays its squares out left to right, but a
  first-row square's bottom padding reserves the second row's height and the ribbon's right padding
  takes the second row's width back, so the dialog (and the panel behind the squares) is sized for two
  rows; after retail's `ComputeLayout` the hook moves every square into its row
  (`UI::Element::AdjustUpperLeft`) and puts each button's frame back around its square (the seam's third
  call point, `kExtPlaceAfterLayout`, right after `DialogInstance::ComputeLayout` in
  `UserInterface::Update`).
  `standalone_error.log` gets one `[PhoneMenu] <w>x<h>: <n> row(s) of square buttons (...)` or
  `... column (...)` line per decision.
  Touch targets on the S25: at the owner's UI 35 (render scale 100, an 819 x 378 layout, 2.86 device
  pixels per unit) both menus use the small font, 78-unit squares = 223 device pixels (about 14 mm): the
  main menu one row of 8, the pause menu two rows of 5. At the seeded UI 100 / render scale 50 (1170 x
  540, 2 px per unit) both are one own-font row: main menu 108 units = 216 px (the 0.22 cap), pause menu
  104 units = 208 px. Below about UI 33 % (the pause menu) and UI 31 % (the main menu) on the S25 at
  render scale 100 even two rows of small-font squares (2 x 82 + 18 units) are taller than the band
  between the logo and the version line (126 units at UI 25), and the column would run off the
  screen: since P8 round 4 the UI scale cannot go there (the slider's floor, below).
  The icon above each label is drawn larger (round 4, the owner: "the icons could be a bit larger in
  mobile mode"): about 0.45 of the square's side (`IconSide`), the text and the 5-unit inset still
  inside the square, rounded down to a whole multiple of the sprite's size. The icons are 16-pixel
  sprites: in a 78-unit square (the S25 at UI 35) they are 32 units (twice the sprite), 91 device
  pixels, against 46 at their own size; a whole multiple keeps every sprite pixel the same size, so
  the edges stay even, but the icons read as enlarged pixel art next to the rasterised text (no
  sharper source exists in the game's data).
* **Centred layout** (any other screen and any screen where the rows do not fit). Retail's cover fit;
  the menu and the version line are laid out inside the safe area; the column is centred between the
  logo's bottom (0.22 of the overlay) and the version line when it fits there, else above the version
  line, and the version line moves to the bottom-left corner when the column cannot sit above it.
* **Safe area**: 4.5 % of the screen height in from every edge. The S25's corner radius is about 3.5 %
  of the 1080-pixel height and the cut-out about 3 % deep; 4.5 % clears both with a little air and
  moves nothing centred on a desktop.
* **Every other dialog** (P8, 2026-10-25: the owner saw the in-game toolbar under the S25's corner). What
  retail anchors to an edge sits inside the same safe area. `DialogInstance::ComputeLayout` (0x180113d90)
  puts a dialog at `_upperLeftX + x` (left / top alignment), against `right` less its width (right /
  bottom), or centred between them, and pulls one that would cross `right` back. The row hands it
  `{max(0, m - upperLeftX), max(0, m - upperLeftY), w - m, h - m}` (`phone_math.h` `SafeArea`): the far
  edges are the safe area's (the toolbar and the event notices, bottom-right, move in by the margin;
  centred dialogs do not move), the near edges are a floor (a panel at its default (0, 0) opens at the
  safe corner; a tooltip at the cursor, a combo box's drop-down or a dragged panel stays where retail puts
  it unless that point is inside the margin). An axis on which the dialog (last frame's size) is larger
  than the safe area keeps retail's extent, and a root sized to the back buffer (`ComputeLayout`'s 0x10 /
  0x20 flags) keeps retail's rectangle. In-game dialogs seen on a new game: `Toolbar.rsc` (bottom-right),
  `EventNotify.rsc` (bottom-right), `AutoSave.rsc` (top-left); front end: `ShiningRock.rsc`,
  `Loading.rsc`, `NewGame.rsc`. Known effect: a panel dragged out of the corner moves once the cursor
  has passed the margin (`docs/known_port_issues.md` row 1336).
* **Loading screen** (P9, owner's rulings 2026-10-26; `src/ext/phone_menu/loading_screen.cpp`,
  `loading_math.h`; `docs/android/P9_LOADING_SCALE.md` §1): `Dialog/Loading.rsc` is an OPAQUE 16:9
  picture with torn black borders top and bottom and one bottom-centred group (`main`: the spinner,
  "Loading...", the tip line). On a screen wider than the picture retail's centred cover fit cuts both
  borders. With the row on it looks like the pause menu: the picture top-aligned at the menus'
  width-fitted scale (`LogoTop`, its top border whole), then the menus' see-through overlay
  (`Dialog/MainMenu.rsc`'s backdrop sprite, 1600x900), bottom-aligned at the picture's width
  (`OverlayLower`), drawn OVER it and clipped by the sprite scissor to its lower part
  (`OverlayStripTop`, `kOverlayStripFraction` 0.30 of its height: inside its transparent middle, so the
  line never shows), so its torn bottom border lies over a continuous picture (P11; seam
  `_extBackdropOver`, §3). The bottom-aligned draw and its clip end on the screen's real bottom edge,
  `ScreenBottomUnits` (the back buffer times the UI scale rounded UP; the UI's layout height is truncated
  and left a one- or two-pixel sliver of picture along the bottom). The overlay is kept loaded from the
  first `MenuOpened` with the row on (`KeepMenuOverlay`): a resource clean bit of its own, reserved once
  from `Object::ExternalFactory::_cleanBitMask` and never handed back, ORed into the overlay sheet's row
  through `ExternalFactory::Add` with `_loadEnabled` set, so the reference visit marks its material and
  textures too (without the load flag the visit marks nothing and the material is freed under the sheet).
  Before any menu opened (no overlay yet), the picture itself is drawn again bottom-aligned (`LowerCopy`)
  and clipped to its own border (`BorderStripTop`, `kLoadingBorderFraction` 0.125). The group keeps
  retail's bottom-centred rectangle (no safe-area inset) and, ruled, the Options UI scale instead of
  retail's splash scale 900 / height (`LoadingUiScale`, in `GameLoadDialogLoopT::Startup` and
  ext.load_progress's boot screen). A loading screen is recognised at its first
  layout by its `loadingImage` element (the Shining Rock splash has none and keeps retail's fit);
  ext.load_progress's boot screen is the same dialog and gets the same fit.
* **UI scale slider**: Options > User interface scale goes below retail's 75 %. On a screen wider than
  the menus' backdrop the minimum is the lowest scale at which both menus still fit as rows of
  squares (P8 round 4, owner's ruling): `UiScaleFloor` runs `PlanRows` for the pause menu (the menu
  with the most buttons; `PauseMenuMetrics`, its small-font measurements) on the current back buffer
  at every percent and rounds the first that fits UP to a multiple of 5. On the S25 that is 35 (the
  rows fit from 33). Since P9 the UI scale is in screen units (`src/platform/common/ui_render_scale.h`)
  and the floor is taken on the screen the back buffer stands for (`ScreenExtent`), so it is 35 at every
  render scale (before P9: 70 at Half, 100 capped at Quarter, on the back buffer).
  It is computed, not a constant: it follows the screen; it does not follow a
  translation's label widths (the measurements are retail's English labels). A `settings.cfg` holding
  less (written before the floor, or by hand) is raised at every loop's start
  (`GameContainer::ApplyKeyMapToUI`, `Ext::PhoneMenu::ClampUiScale`, one `[PhoneMenu] UI scale ...
  raised to ...` line), so the Options slider opens at the floor. On any other screen the minimum
  stays 25 %. `_uiScale = percent / 100` is layout units per device pixel, so 25 % draws the UI four
  times larger than 100 %. The platform hook `Platform::Display::UiScaleSliderRange` stays what it is (a density
  compensation that keeps retail's physical range on a high-density window; the identity on Android,
  whose window is not created high-density): a wider range is a feature retail lacks, so it is this
  row (DESIGN.md §2), on for the phone by its first-run settings and available to a desktop player
  with a small high-resolution screen.

* **P9: the game's dialogs for a finger** (owner's rulings 2026-10-26, `src/ext/phone_menu/touch_fit.cpp`,
  arithmetic `touch_math.cpp`, session log `docs/android/P9_DIALOGS.md`). Done once a frame per
  UserInterface, on its first dialog's `kExtPlaceLayout` call, over every open dialog's element tree:
  * **Scroll bars 4x thicker** (`kScrollBarFactor`). Retail's bar (`UI::ScrollElement`) is as thick as its
    track sprite (12 units for `SharedElements.rsc:verticalScrollbar`); the scrolling list
    (`UI::ContainerElement`) gives it its padded width raised to its `_minWidth`, and the clip region the
    rest of the requested width. The row sets the bar's `_minWidth` (`_minHeight` for a bottom bar) to 4x
    the sprite and its stretch flag (`Element::_flags` bit 2 / 3), and grows the container's requested
    width (height) by what the bar gained, so the rows keep their width (`Mod.rsc`'s rows are
    `_minWidth = 580`, the clip's width exactly). 48 units = 120 device pixels on the S25 at UI 40.
  * **Close buttons twice the size** (`kCloseButtonFactor`): an element named `buttonClose`
    (`StandardDialog.rsc`'s title bar: a 20-unit button around a 16-unit `ImageElement`) gets its icon's
    `_width` / `_height` set to twice the sprite; the button, its frame and the title bar grow around it.
  * **A dialog larger than the safe area is shown whole.** Retail lays a too-tall dialog out with its
    bottom on the screen's (`DialogInstance::ComputeLayout`), so the Options dialog (437 x 427 units)
    lost its tab row below UI 45 at render scale Full (a 432-unit-high layout at UI 40). Its tallest page,
    Video, is a plain ribbon with no scrolling list (`graphicsOptions`, 327 units), so capping the lists
    cannot give the height back. Instead such a dialog alone is shown at the lowest UI scale (in whole
    percent) at which it fits the safe area (`FitScale`), through the per-dialog scale seam
    `UI::UserInterface::_extDialogScale` with k = player / fitted (P11; P9 raised the whole interface's
    `_uiScale`, which shrank and moved everything behind the dialog). Options at UI 40 on the S25 shows at
    44 %, the Fixes and Extensions dialog at 52 %. Judged: top-level "windows" only, a dialog with a
    description, shown, with no full-screen backdrop and a root not sized to the screen, not a child
    dialog (a sub-toolbar's left padding reaches across the screen: it set the old fit off) and not one
    ext.touch_mode scales. What a fitted window opens takes its k (`FitOwner`): its child dialogs, its
    tooltip (`_toolTipOwner`), its combo drop-downs (`ComboDialog::_parentDialog`). The safe-area arm
    takes a fitted dialog's upper-left and size in layout units (`ToLayoutUnits`). One `[PhoneMenu] dialog
    ... larger than the safe area: shown at UI scale ...` line when a fit starts or changes, `back at the
    interface's scale` when it ends.
    A value the Options slider applies is the player's new scale; the dialog's fit is decided again from
    it on the next frame, so the dialog shows the new size at once (P11).
  * **No tooltip after a finger lifts** (P11, owner's ruling; `touch_fit.cpp` `FingerUpHidesToolTip`). A
    finger leaves the pointer where it lifted, so retail's hover rule (`DialogInstance::Update`: 0.25 s at
    rest with no button down) opened the tapped control's tooltip after every tap and kept it up. With the
    pointer last moved by a touch (`Platform::Input::PointerIsTouch`, SDL's `SDL_TOUCH_MOUSEID`, read in
    `pump_sdl.cpp`) and no finger down, no dialog counts hover time and an open tooltip is closed. A real
    mouse keeps retail's tooltips.
  * **Undone from the description.** Each change is on the element instance; with the row switched off the
    next frames walk the open dialogs and put back the values of the element's own description
    (`Element::_description`, the values `ElementDescription::CreateInstance` copied: the scroll bar's
    `_minWidth` / `_minHeight` and stretch bits, the icon's `ImageDescription::_width` / `_height`), the
    container's requested size by the same amount it grew, and the UI scale. An element built without a
    description is never changed.
* **Every control the size of the close button** (P11, `touch_size.{h,cpp}`, `size_math.{h,cpp}`; session
  log `docs/android/P11_TOUCH_SIZE.md`; the owner: "everything clickable gets about the height of the
  enlarged [x]"). The same walk, the same undo from the description. The side is `kTouchSide` = 36 units
  (the P9 close button: a 16-unit cross at twice its size, padded 2). A button (`ButtonElement`, and
  `CheckElement` / `ComboElement`, which derive from it) is sized by its content's padded size, and its
  content is laid out over it by the content's own alignment (TopLeft for most of the game's labels), so
  the CONTENT's padding grows (centred: half above, half below), not the button's `_minHeight`:
  * title-bar buttons (inside `StandardDialog.rsc:titleBar`: pin, centre, the event log's notify
    checks), icon buttons (the `>` on each citizen row) and check boxes: the icon at twice its sprite
    when that still fits (`TouchIconSide`), then padded to 36 x 36; a check box's tick (`_children[5]`)
    gets the same icon size and padding as its unticked content;
  * text buttons (Ok, Cancel, Apply, Yes, No, Follow, the key bindings), tab buttons and drop-downs: the
    content padded to 36 high, retail's width;
  * a drop-down's rows (the children of `ListElement::_ribbon`): padded to 36 high;
  * a spinner's up / down buttons: arrows at twice their sprite, each button 36 wide and 18 high (the
    pair is 36);
  * sliders: every sprite (track, thumb, the track offset and thumb margin) at `kSliderSpriteFactor` (2)
    times its size through the engine seam `UI::SliderElement::_extSpriteScale` (§3): the slider has no
    size field to change.
  Not changed: a button already at least 36 in its description's minimum (the toolbar's 40-unit buttons,
  the menus' squares), a button whose minimum is not its description's (sized at run time by its owner),
  every dialog ext.touch_mode places (its toolbar, sub-toolbars, event notices and camera cluster: they
  are sized for a finger by that row; `TouchSeen` records them from the place hook, and a dialog is
  walked only from its second frame, once the hook has seen it), and the controls on a tab's page that
  does not scroll (a tab element is as tall as its tallest page: a taller non-scrolling page makes the
  dialog taller, the fitted UI scale above then shrinks the whole dialog, and every control in it ends up
  smaller on screen than before; measured on the S25 at UI 42 with the Options Video page enlarged: the
  fit went from 44 % to 57 %). Controls in a scrolling list on any page grow.
* **A tall window scrolls instead of shrinking** (P11b, `touch_scroll.{h,cpp}`, `size_math.{h,cpp}`
  `ShrinkCap` / `GrowCap`; session log `docs/android/P11B_SCROLL_SIZE.md`; the owner: "the Enhanced tab's
  fit is the model", "the Professions list must scroll"). Two steps inside the same walk:
  * pages retail lays out without a scroll bar are moved into a scrolling frame of ours: the Options
    dialog's Video and Audio pages (`Options.rsc:graphicsOptions`, `soundOptions`) and the Professions
    dialog's table (`Profession.rsc:professionTable`), matched by element name and the tail of the
    description's path. The frame is `Dialog/PhoneScroll.rsc:phoneScroll` (the Game page's container,
    `_width` / `_height` 0: sized by its content), built as `TabElement::AddTab` builds a page (slot-8
    `CreateInstance` against a scratch `UI::Dialog`); its empty content group is deleted and the page's
    ELEMENT takes its place in the clip region (elements own their children by raw pointer; only a
    dialog's root knows its `DialogInstance`), so the dialog's controller keeps the pointers it found at
    open. The frame takes the page's slot and its hidden bit (the tab shows a page by that bit) and has
    `_clampHeight` set (a page shorter than the cap does not leave a gap). Row off: the page goes back
    into its slot with the frame's hidden bit, the frame is deleted;
  * a top-level window taller than the safe area at the player's UI scale caps every vertical list it
    holds (`ContainerElement::_requestedHeight` on the instance; the container clamps the clip region and
    the bar follows) at one height, the tallest list's height less the overflow, at least `kListFloor`
    (3 x 36 units); with room to spare the cap grows back and is dropped once the lists reach their own
    height. A cap that takes nothing off (the height is held by something that does not scroll) is
    dropped. The window then fits at the player's scale and the tall-dialog fit (above) does not shrink
    it, which is what had kept the Fixes and Extensions dialog's buttons at 66 device pixels (drawn at
    0.78) and Options' at 0.93. Decided once per change of the window's height or the safe area, not
    every frame. Row off: each list's description height.
  With every Options page a scrolling list, the non-scrolling-page rule above holds back only other
  dialogs' tab pages.
* **The menus' backdrop's bottom border** (P9, the owner: "the black fringe image is missing"). `LogoTop`
  keeps the 16:9 picture's top on the screen's top, so on a 19.5:9 screen its bottom border (the dark torn
  edge, the last tenth or so of the picture) fell 94 units below a 432-unit layout. The picture is now
  drawn twice for the main and pause menus: an extra copy with its BOTTOM on the screen's bottom
  (`LowerBackdrop`), drawn BEFORE retail's (moved) draw, whose opaque top border then covers what the
  extra copy has above (the lower edge of a second logo) while its transparent middle lets the extra
  copy's bottom border through. Both borders are at the menu picture's own scale.
* **The version line in that border** (the owner, P9: "the version text could be farther down to be all
  in the black area"): with the bottom border drawn (a screen wider than the backdrop) the version line's
  rectangle is the safe area with its bottom on the screen's bottom edge (`LowVersionLayout`; a landscape
  phone's rounded corners and camera cut-out are not under the middle of the bottom edge, where the
  centred line is), and the rows' band ends above it (`LowVersionTop`). The UI scale floor still uses the
  higher placement (conservative).

## 3. The seams

* `UI::UserInterface::_extDialogPlace` (engine, null by default and under the faithful profile, set by
  `Ext::PhoneMenu::Sync` at boot): one function pointer, five call points.
  * `kExtPlaceLayout` in `UserInterface::Update` (`userinterface_frame.cpp`): the rectangle each dialog
    is laid out in (`ComputeLayout(x, y, right, bottom)`), retail's `{0, 0, w, h}` unless the row moves
    a recorded menu dialog or puts another dialog in its safe area. With the pointer null the loop is retail's line.
  * `kExtPlaceAfterLayout` in the same loop, right after that `ComputeLayout` (P8 round 3): the menus'
    second row of squares is moved into place.
  * `kExtPlaceUpperLeft` (P11b) in `DialogInstance::SetUpperLeft` (`dialog.cpp`), before the root is
    sized: `rect` holds `{0, 0, w, h}`, the rectangle retail clamps the new upper-left into (its four
    compares, in its order), and may come back inset. `phone_menu.cpp` `Place` answers the safe area the
    layout uses (`phone_math.h` `SafeBounds`) for every dialog it lays out in that area (not the menus,
    the version line, a screen-sized root, a dialog at its own fitted scale, nor ext.touch_mode's), and
    first sets the dialog's `_upperLeft` to where its root was actually laid out (`LaidOutUpperLeft`, on
    the axes its alignment anchors top / left), because SetUpperLeft moves the laid-out tree by the
    delta from `_upperLeft`. Without the two, a dialog a game tool places after the layout (an entity's
    info panel, every frame, `EditTool::Update`) was drawn for one frame where retail's clamp puts it and
    then moved by the next layout (`docs/android/P11B_DIALOG_SNAP.md`). The same call is a dialog's first
    placement when it comes before its first layout: the touch-size walk and the fit run there
    (`touch_fit.h` `TouchFirstPlace`).
  * `kExtPlaceBackdrop` in `UserInterface::Draw` (`userinterface_draw.cpp`): the backdrop rectangle after
    retail's cover fit.
  * `kExtPlaceBackdropLower` (P9) in `UserInterface::Draw`, just BEFORE a dialog's backdrop is drawn,
    after `kExtPlaceBackdrop`: `rect` holds the backdrop's rectangle as it will be drawn (four ints). If
    the hook answers a different, non-empty rectangle, the same sprite (same sheet, index and tint) is
    drawn there first, under retail's draw; an unchanged or empty answer draws nothing extra. No scissor
    (an earlier build scissored the extra copy to the screen's lower half: it showed in the front end but
    not over the game, so the draw order does the masking instead). `phone_menu.cpp` `Place` answers it
    only for the recorded main and pause menus (`BackdropLowerDraw`, `touch_math.h` `LowerBackdrop`:
    the picture's height kept, its bottom on the screen's bottom; nothing when it does not overhang the
    bottom). It does not serve the loading screen: its picture is opaque, so a copy drawn UNDER it never
    shows (that one uses `_extBackdropOver` below). Row off: `Place` returns before this point (its row check);
    faithful profile: the pointer is null and `Draw` never asks.
  * `TouchFrame` (`touch_fit.cpp`) sees every call first (row on or off): the P9 walk and, once a frame per
    UserInterface, the tall-dialog fit decision (P11: it no longer changes `width` / `height` or `rect`).
* `UI::UserInterface::_extDialogScale` (P10 seam, `docs/android/TOUCH_MODE.md` §2): installed by this
  row's `Sync` (null under the faithful profile) with `DialogScale`: ext.touch_mode's toolbar scale first
  (`TouchMode::Scale`), then the tall-dialog fit (`FitDialogScale`). In the pump arm the engine also gives a
  dialog with k != 1 the screen clip region (`_clipRegion`, every element's hover test) in its own units for
  the length of its Update; with k < 1 the global one cut off the hit test of the dialog's lower part.
* `UI::UserInterface::_extBackdropOver` (P9, engine, null by default and under the faithful profile, set
  by `Sync`): in `UserInterface::Draw`, just AFTER a dialog's backdrop is added. The hook gets the
  backdrop's rectangle as drawn and its sprite (sheet and index, in / out) and may answer another
  rectangle, another sprite (P11) and a clip `{x, y, w, h}` (layout units); that sprite (the backdrop's
  unless the hook names another) is then drawn over the backdrop, before the dialog's elements, inside
  `SpriteContext::SetScissor(clip)` and with the previous scissor restored after it (w <= 0: no clip). A
  scissor change flushes the batch and forgets its sheet, so the draw binds the extra sprite's sheet before
  it and the backdrop's sheet again after it (without that the clipped draw is silently dropped, which is
  likely what the earlier scissored `kExtPlaceBackdropLower` build ran into). `loading_screen.cpp`
  `BackdropOver` answers it for loading screens only (the torn bottom border, above).
* `UI::SliderElement::_extSpriteScale` (P11, engine, null by default and under the faithful profile, set by
  `Sync` to `touch_size.cpp`'s factor: 2 with the row on, 1 off). `SliderElement::ComputeSize`,
  `ComputeLayout`, `Draw` and `Update` multiply every sprite extent, `_trackOffsetY` and `_thumbMargin` by
  it; at 1 every body is retail's arithmetic, and `Draw` keeps retail's natural-size sprite calls (at k > 1
  it draws the same sprites over k-times rectangles). The 32-unit drag band and fix.slider_drag_clamp's
  seam are untouched (verified on the S25: a drag off the band keeps its value with the fix on).
* `MainMenuDialog::OnOpened` (`mainmenudialog.cpp`): `Ext::PhoneMenu::MenuOpened` records the menu's and
  the version line's dialog descriptions, and whether the menu is the pause menu (`game` set).
* `OptionsDialog::OnOpened` (`optionsdialog.cpp`): `Ext::PhoneMenu::UiScaleSliderMinimum` after the
  platform hook.
* `GameLoadDialogLoopT::Startup` (`splash_loop_stubs.cpp`, step 5) and ext.load_progress's `BootBegin`:
  `Ext::PhoneMenu::LoadingUiScale` picks the splash UI's scale (P9: the Options UI scale with the row on,
  retail's 900 / height otherwise).
* `GameContainer::ApplyKeyMapToUI` (`optionsdialog.cpp`, every loop's start and Options' Apply):
  `Ext::PhoneMenu::ClampUiScale` on the settings' UI scale before it becomes `_uiScale` (round 4).

The hooks read the row on every call (live). The menus' layout is decided from THIS frame's sizes: the
hook sizes the menu's tree and the version line's (`ComputeSize`, which retail's `ComputeLayout` runs
anyway) before retail lays the menu out, and the backdrop's place comes from the dialog's own picture.
P8 round 2 fixed the jump this replaced: the first logo-left code learned the logo's place from the
backdrop's DRAW, so a menu's first frame was laid out without it (the pause menu showed centred for one
frame, then moved right; the main menu likewise). Every other dialog's `SafeArea` reads its last laid-out
size only to decide whether it is larger than the safe area.

## 4. ext.sharp_ui below 75 %

At density 1, `Ext::SharpUI::PickFontHeight` drew the base glyphs one texel per device pixel whenever
`PickVariantHeight` answered the base font. Below 75 % (a ratio above 4/3, reachable only with this row)
that answer means "outside the compiled variants", and the text came out at its 1x size, spaced by the
magnified advances (seen at 25 %). It now keeps retail's scaled quads there, as the Retina path already
did (`kRetailRatioMax`); inside 75 .. 150 % the selection is unchanged (the existing unit test sweeps it).

## 5. Verified (Windows, debug, functional game dir)

P8 round 3 (2026-10-26; screenshots in the session scratchpad `android-ui-fit/round3/`):
* S25, the owner's UI 35 at render scale 100: the main menu one row of 8 small-font squares with
  "Achievements" inside its frame (`r3_p_menu35.png`); the pause menu two rows of 5
  (`r3_p_pause35.png`); a tap on a second-row square (Extensions) opens its dialog
  (`r3_p_pause35_ext.png`). At UI 25 both menus are the column (`r3_p_pause25.png`, the log's
  `585x270: column` lines).
* Windows, 2528 x 1345 window at UI 32 (an 808 x 430 layout): the main menu two rows of 4 and the pause
  menu two rows of 5, own font, 99-unit squares (`r3_win_menu32.png`, `r3_win_pause32.png`; the
  hovered square shows its highlight and tooltip).

P8 round 2 (2026-10-25; screenshots in the session scratchpad `android-ui-fit/round2/`):
* S25, owner's settings (UI 35, render scale 100): the main menu as a row of 8 squares under the whole
  logo, the version line at the bottom (`p2_menu3.png`); the pause menu as a row of 10 small-font
  squares (`p2_pause3.png`); Options opened from both rows (`p2_options.png`, `p2_pause_options.png`).
  The first round-2 build (a row only when the labels fit in their own font, the pause menu left a
  column) fell back to the column at UI 35, which ran over the logo and under the version line
  (`p2_menu.png`, `p2_pause.png`): what the owner's sketch was about.
* Windows, 2528 x 1345 window (1.88:1), row on: UI 83 a row of 8 (`win_row.png`), UI 35 a row of 8 in
  the small font (`win_row35.png`), the pause menu a row of 10; row off: retail's centred column and
  cover fit (`win_off.png`). First-frame check: the menu's and the pause menu's laid-out rectangle is
  the same from their first frame on (a temporary per-frame print, removed).
* The mods' restart notice at UI 50 follows the UI scale under `fix.ui_scale_layout`
  (`win_modal_fix.png`; `docs/retail_bugs.md` section 5).

Earlier (the first logo-left pass):

A 3080x1421 window (2.167:1, the S25's 2340x1080 aspect; this desktop's window manager keeps the
window at the work area's height, so the aspect was reproduced at that height):
* row off: the logo's top cut off, retail's layout;
* row on, UI 75 %: logo whole on the left, column right of it, version line under the logo;
* row on, UI 25 %: the same, nothing overlapping; Options opens with the slider at 25 and the text
  drawn by retail's quads (sharp_ui on).

Not verified: the phone itself (reserved by another agent during this work), and the Options dialog at
25 % is taller than a 1080-pixel screen (its tabs go off the top; Cancel / Apply / Ok stay on screen,
retail's ComputeLayout keeps a too-tall dialog's bottom on screen).

Seen on the phone itself in P8 (2026-10-25, Galaxy S25, debug apk, render scale 50, UI 100 and 83;
`docs/android/P8_UI.md`): the logo-left menu, the toolbar clear of the bottom-right corner, a sub-toolbar
right-aligned on the safe edge, the Town panel opened at the safe top-left corner, tooltips at the cursor.

## 6. Where

| file | change |
|---|---|
| `src/ext/phone_menu/phone_math.{h,cpp}` | the pure decisions (unit-tested) |
| `src/ext/phone_menu/phone_menu.{h,cpp}` | the hook, the recorded dialogs, the slider minimum |
| `src/ext/phone_menu/touch_scroll.{h,cpp}`, `src/ext/ui/Dialog/PhoneScroll.rsc` | P11b: pages framed in a scrolling list, a tall window's lists capped |
| `src/ext/phone_menu/touch_math.{h,cpp}`, `touch_fit.{h,cpp}` | P9: scroll bars, close buttons, the tall-dialog fit (P11: per dialog, `FitDialogScale`), the backdrop's extra draw, the low version line |
| `src/engine/ui/UserInterface.h`, `userinterface_frame.cpp`, `userinterface_draw.cpp` | the seam |
| `src/game/Dialog/mainmenudialog.cpp`, `optionsdialog.cpp` | the two game seams |
| `src/ext/sharp_ui/sharp_math.{h,cpp}` | §4 |
| `src/ext/registry.{h,cpp}`, `registry_boot.cpp` | the row (class A, default off), `Sync` |
| `src/platform/android/app_android.cpp` | `ext.phone_menu=1` in the first-run settings |
| `tests/unit/ext_phone_menu_test.cpp`, `ext_phone_touch_test.cpp`, `ext_registry_test.cpp`, `ext_sharp_ui_test.cpp`, `sources.cmake` | tests |
