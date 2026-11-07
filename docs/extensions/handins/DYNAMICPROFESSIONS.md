# DYNAMICPROFESSIONS hand-in (2026-11-06)

Branch `ext-dynamic-professions`, on `main` e70c566b. The task (the user's): "a professions utility window that only shows the professions that your town actually has buildings for", the laborer and the builder always on it at the top, and a profession that still has workers kept on the list until the player lowers it to zero. **Result: `ext.dynamic_professions`, class A, off by default: the toolbar's Professions button opens a window that lists only the professions the town can use, in place of retail's list of all of them.**

## 1. Why
Retail's Professions window (`ProfessionUI`) builds one row for every entry of the game's profession list. Vanilla has 20; a big mod pack brings many more (MegaMod's own profession file defines about 84), so the window is a long scroll of professions the town has no workplace for.

## 2. What the window lists
`src/ext/dynamic_professions/professions_math.h` (pure, unit tested). One row per profession of the game's list (`ProfessionList::professions`, already without the professions that are not player-selectable and sorted by `_sortPriority`), in the game's order, with:

* the **laborer** (`ProfessionList::field_0x30`) first and the **builder** (`field_0x38`) second, always shown: neither has a workplace of its own to find, and the builder would otherwise come and go with every construction site;
* any other profession **while the town has a workplace for it** (the entities in `CitizenBehavior::field_0x100`'s per-profession tree: the same tree retail's window sums its worker slots from) **or a citizen holds it** (adult, not at school: retail's count). A profession that has workers but no workplace left therefore stays until the player lowers it to zero;
* a row that stops qualifying stays 120 frames (about two seconds) more before it goes, so the rows below it do not move under the cursor the moment a spinner reaches zero.

## 3. The rows
The rows are retail's own: the table's one template item is `Dialog/Profession.rsc:professionRow` (the name and icon, the spinner, "of", the worker slots, the goto arrow), so they look and behave as the player knows them. The window controller (`professions_window.cpp`) does what `ProfessionUI::OnOpened` does for each row (the label's text, icon and tip from the profession; the spinner's and the arrow's ids, 300 + i and 500 + i by the profession's place in the game's list; no "of" and no slots on the laborer's row), and what `OnUpdate` does every few frames (the spinner reads the holders, its range is 0 to holders + laborers, the laborer's is locked; the slots read the workplaces that are accepting workers).

The spinner and the goto arrow do retail's own work: the spinner moves citizens in and out of the profession with `CitizenBehavior::FindAvailableBuilding`, `FindWorkerToRelocate`, `FindNearestHousedLaborer`, `FindLaborerFromFullestHouse`, `FindIdleLaborer` and `RemoveWorker`; the arrow goes to the next adult citizen of that profession with `PlayerToolManager::CenterOnEntity`. Retail's versions (`ProfessionUI::HandleSpinner` / `HandleButton`) are private to its controller, so they are written out again over the same calls.

An event is only written down while its element dispatches it; `OnUpdate` applies it, because the table it sits in is rebuilt there (the favorites window's rule).

## 4. The window's controls
* **Two columns or one.** A button in the title bar, before the close X, switches the list between retail's two columns and one. The button shown is the layout it switches to (two buttons, one hidden). The scroll box is 440 wide for two columns and 230 for one; every row is at least 210 wide, so the laborer's row (no "of", no slots) does not pull the second column in. The choice is kept while the game runs, not between runs.
* **Height.** With nothing set the list is as tall as its rows, up to 300. Dragging the strip of dots under the list sets the height: a whole number of rows at a time (the shortest is one row), the list scrolls when it is shorter than its rows, and the window keeps that height while professions come and go. A click on the strip, without a drag, returns to fitting the rows. A text element sends no press message of its own, so the window's mouse-down (message 8) is tested against the strip's rectangle. The game has no resize cursor; the strip shows the title bar's move cursor.
* **Which window it replaces.** Only retail's: `Handles` also requires the controller's layout to be `Dialog/Profession.rsc:content`. The twelve `UtilityProfessionsXXX` toolbar entries of MegaMod (`DSProfession1`, `ProfessionVN` and so on) are sized variants the player picked on purpose, and keep their windows.
* The title bar is the game's own with one more slot (`titleBarProfessions`). The two buttons and their images are made from the close X's (`StandardDialog.rsc:buttonClose` / `imageClose`): the compiled game keeps only the blocks something uses, and `titleButton` / `titleImage` are not among them.

* **A mod that rebuilds `Profession.rsc`.** The window's rows are retail's `professionRow`. At every open, one throwaway row is checked for the shape the code relies on (child 0 a label, child 1 holding a number edit, child 3 a number element, five children in all). If a mod changed it, the window closes itself, `Toggle` reports `declined`, and the seam goes on into retail's own window. The check is not cached: applying a mod change reloads the game's resources without leaving the run (`ModDialog::Apply`), so the next open checks the new row.

## 5. The seam
One call in `PlayerToolManager::ToggleUtilityDialog` (`toolbardialog.cpp`), which the toolbar's utility button, the entity panels and the tutorial all go through: after the utility's `UIDescription` is found, `Ext::DynamicProfessions::Handles` is true when the extension is on and one of the description's controllers is `ProfessionUI` with retail's own layout (section 4); the call then returns `Ext::DynamicProfessions::Toggle` (open the window, or close it when it is open) instead of building retail's dialog. With the extension off nothing changes. The window keeps its place during the session; it does not use the tool manager's pinned-position set.

| file | role |
|---|---|
| `src/ext/dynamic_professions/professions_math.h` | which professions are listed and in what order (pure, unit tested) |
| `src/ext/dynamic_professions/dynamic_professions.h` | `Handles`, `Toggle` |
| `src/ext/dynamic_professions/professions_window.cpp` | the window: rows, counts, the spinner and goto |
| `src/ext/ui/Dialog/ProfessionsWindow.rsc` | the window (title bar with the columns button, a scrolling table of retail's row, the resize strip) |
| `src/ext/resources/text_reader.cpp` | names the container's two shrink flags (`_allowShrinkWidth`, `_allowShrinkHeight`) |
| `tools/ext_icons/dynamicprofessions.py` | the row icon and the two column-button glyphs |
| `tests/unit/ext_dynamic_professions_test.cpp` | the list rules |

The registration is the usual one: `registry.cpp/.h` (the row), `resources.cpp` and `CMakeLists.txt` (the embedded `.rsc`), four strings in `ExtensionsStringTable.rsc` (the title, the two button tips, the strip's dots) and three sprites (the row icon and the two column glyphs, programmer art drawn by `tools/ext_icons/`). The rows use the game's own.

Class A: the game keeps nothing, nothing in the simulation reads the flag, and the spinner makes the same changes retail's does. Off by default because it replaces a window the player knows. Rule 7: no game content; the rows are the loaded game's own.

## 6. Not done / not looked at
* **Looked at in a game by the author** (Windows, DX11, release build): the list and its growth as workplaces are built, the spinner and goto arrow, both layouts, the resize strip, and MegaMod's sized variants, which keep their own windows. Not looked at: Vulkan, Metal, Linux, Android, macOS, a touch screen.
* The registry test (`ext_registry_test.cpp`) has a per-name exception for this row, for the shared class A rule "live rows are on by default": this row is class A (nothing is saved) but replaces a window the player knows, so it is off by default (the question for the maintainer: is a per-name exception right, or a registry field).
* The window does not scroll with the touch pointer yet: `ext/phone_menu/touch_scroll.cpp` registers retail's `professionTable` by name; this window's table is `tableProfessions`.
* The window's position is not remembered between sessions, and it is not part of the tool manager's pinned-dialog set.
* A retail Professions window that is open when the extension is switched on is closed by the next button press (the seam finds it first), not replaced.
* The Windows gate and the resave check (the maintainer's).
