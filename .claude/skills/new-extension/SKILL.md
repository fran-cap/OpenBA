---
name: new-extension
description: Add a new `ext.*` or `fix.*` row under src/ext/<id>/ - choosing fix vs extension and class A/B/C, the registry row and valued keys, the Extensions-dialog tab (Default / Optional / Experimental), an Enhanced-tab toggle (Off / On / Fancy), the seam into game or render code, shaders, the row icon, debug tweak-overlay tunables, unit tests, docs and changelog. Use when the user says "can we add an extension that ...", "make it a fix row", "make it a toggle in the enhanced menu", "add an Off/On/Fancy setting", "add tunables to the imgui window", "bake these values as the defaults", "move it to the default extension list", or any new feature retail does not have.
---

# Adding a fix or extension row

The authority is `docs/extensions/DESIGN.md` (§2 tiers and classes, §3 hook rules, the Names table, the
class C contract). This is the file-by-file order; read the section it points to instead of guessing.
Worked examples on main: `git show --stat d7de40d8b` (Depth blur: class A, renderer, Enhanced combo, tweak
panel, shaders), `git show --stat c60d9b59e` (recipe_skip: class B, one sim seam), `git show --stat
92f3c60fe` (walk_on_ice: class C, codec + resave golden), `git show --stat 255136eae` (an Enhanced row alone).

## 1. Decide the row (ask the user when it is not obvious)
- **Fix** = a retail bug corrected at the lever `docs/retail_bugs.md` names: `if (Fix().<id>) {..} else
  {retail}`, never changes what is serialized, default ON, global and live, never in the save.
- **Extension class** (DESIGN.md §2, "Which class, decided by one test"):
  A = no sim effect, nothing saved, live toggle. B = sim-divergent, format-neutral. C = per-town state a
  reload must restore (tail-chunk record + codec). B / C are locked for the game's lifetime and written into
  the save as an `ext:<id>` pseudo-mod; toggling one in game goes through the reload notice.
- **Default**: class A ON, B / C OFF unless the user rules otherwise. A look the user is still judging ships OFF.
  Ask which Extensions-dialog tab: Default (`defaultOn` true), Optional (false), or Experimental
  (the `kExperimental` list in `Experimental()`, `src/ext/registry.cpp`; its ids and count are pinned by
  `ext_registry_experimental_rows` in `tests/unit/ext_camera_angle_test.cpp`). A common shape: row ON in Default, its Enhanced value OFF by default.
- **Per-platform default**: a phone/tablet default is a line in the seed string of
  `src/platform/android/app_android.cpp` (`"ext.<id>=0\n"`); it is not an `#if`.
- **No dependency chains**: a feature that needs another extension's code goes INTO that extension as a mode
  or value key, not a second row that requires the first (the user's ruling).

## 2. Registry (`src/ext/registry.h`, `src/ext/registry.cpp`)
1. The bool: `struct Extensions` (or `struct Fixes`) with a trailing comment `class X, live: src/ext/<id>/ (doc)`.
2. The row in `kEntries` (registry order = menu order): `{ "<id>", Tier::Extension, Class::A, <defaultOn>,
   "<Display name>", "<description>", <formatVersion: 0 for A, >=1 for B/C>, [](State& s) -> bool& { return s.ext.<id>; } }`,
   with a comment line naming the doc. Keep the display name short: row labels wrap at 192 px
   (`src/ext/ui/Dialog/Extensions.rsc`).
3. A tunable the player sets (a level, a mode, a slider) is a **valued key**: an `int32_t` in `struct Values`
   and a row in `kValueKeys` `{ "<owner>_<what>", "<owner id>", min, max, default, slot, choices, count }`.
   Never a separate row, never an env flag (`src/app/options.h` is for harness flags only).
4. A seam that must follow a live toggle gets a `Sync()` call in `src/ext/registry_boot.cpp`, both in
   `InitFromSettings` (boot) and in `SetAndRecord` (the dialog / Enhanced / overlay toggle).
5. Class C: codec `src/ext/<id>/save_state.cpp` + one line in `kCodecs` (`src/ext/state_codecs.cpp`)
   per DESIGN.md "The format to follow (code)" step 2.

## 3. The code (`src/ext/<id>/`, namespace `Ext::<Id>`)
- Bodies and seam helpers in `<id>.h` / `<id>.cpp`. CMake globs `src/ext/*.cpp`; nothing to register.
- Game-side seam: one `// EXT SEAM (NOT RETAIL, docs/extensions/...): <what>` call at the faithful site, the
  decision hoisted out of any hot loop (hook rule 4). Never an `#if` in game code, never a virtual or a
  function-pointer table. Engine-side: a null-by-default `_ext<What>` static pointer the game sets
  (`Graphics::Renderer::_extSceneDraw`, armed/cleared around `Renderer::Draw` in
  `src/game/Game/gameupdateloop.cpp`); `src/engine` never includes `src/ext`.
- If `ext.render_pipe` copies the state you read, carry it in the packet (`src/ext/render_pipe/packet.h`,
  `passes.cpp`) as Depth blur and map_overlays did.
- Renderer work: a backend-neutral header under `src/render/common/<feature>/` (`dof/dof_backend.h` with
  `Supported()`), implemented in `src/render/dx11/<feature>/` and `src/render/vulkan/<feature>/`, a "not
  supported" stub in `src/render/metal/<feature>/`, the dual-exe forwards in
  `src/render/common/render_backend_table.h`, `render_backend_table_fill.h` and
  `src/render/common/multi/render_backend_dispatch.cpp`. Nothing outside a backend names its namespace.
- Shaders: our own HLSL under `src/render/common/<feature>/`, compiled offline to DXBC headers by
  `python -I tools/water_shaders.py` (add rows to `SHADERS` and `source_path`) or `tools/ao_shaders.py`;
  Vulkan consumes the same DXBC. No retail `.srsl` / DXBC or ports of them (AGENTS.md §1); retail data is
  transformed at load.
- Log lines: stderr with the `[<Id>]` prefix, never per frame or per entity in a shipped build (AGENTS.md §3).

## 4. Enhanced tab toggle (when the user asks for one; class A values)
1. `src/ext/<id>/enhanced_entry.cpp`: `Contribution { "<id>", Open, Apply, Close, nullptr }` returned by
   `Ext::EnhancedTab::<Id>Entry()`; copy `src/ext/depth_of_field/enhanced_entry.cpp` (Open reads
   `Requested(row)` and `Val()`, Apply uses `SetAndRecord` / `SetValueAndRecord`; On also turns the row on).
2. Declare it in `src/ext/enhanced_tab/enhanced_tab.h` (the header list and the accessor) and add it to
   `Contributions()` in `enhanced_tab.cpp`.
3. `src/ext/ui/Dialog/EnhancedOptions.rsc`: the label string, the `text<Id>` / `combo<Id>` elements, and the
   two table columns; every row below the insert point moves down one `_y`.
4. Bump the `_items` count in `ext_rsc_text_parse_enhanced_and_strings` (`tests/unit/ext_rsc_text_test.cpp`).

## 5. Row icon (every new row)
1. `tools/ext_icons/<name>.py` with `ICONS = { "<id>": ("A"|"B"|"C"|"fix", draw) }`; copy `depthblur.py`
   (12 x 12 interior, `#` white, `k` dark).
2. `python tools/ext_sprites.py` (needs Pillow) writes `Entry_<id>.png` under
   `src/ext/ui/Dialog/Sprite/Extensions/`.
3. Add `ExtensionsEntry_<id>` to `src/ext/ui/Dialog/ExtensionsSprites.rsc`.
4. `python tools/ext_sprites_bake.py` regenerates `src/ext/resources/ext_sprites_data.h`; commit the header.
   `tools/ext_resources.sh` does NOT regenerate it, and a merge conflict in it is resolved by re-baking.
5. Locally, `ext_sheet_kit_crosscheck` fails after a new sprite until `tools/ext_resources.sh` re-cuts
   `build/ext_resources` (CI has no kit and skips it).

## 6. Tunables in the debug tweak overlay (Alt+F10, debug build only)
1. The look as a struct of defaults in `src/ext/<id>/<what>_params.h` (`dof_params.h`), read by the
   extension every frame through `Ext::Tweak::<What>Values()`.
2. `src/ext/tweak/ext_tweaks.{h,cpp}`: the include, a mutex-guarded `g_<what>`, `<What>Values()` / `Set<What>()`.
3. `src/diag/win/tweak_overlay.cpp`: the id in `kRows`, a `g_ui<What>` copy, a `CollapsingHeader` panel of
   sliders, a "defaults" button and a line printing the set in `params.h` syntax.
4. A row in the panel table of `docs/reference/tweak_overlay.md`.
5. The loop: the user tunes in game and reports the values (often a screenshot of the panel); bake them as
   the defaults in `params.h`, then the overlay's defaults button and the shipped look agree.

## 7. Tests, docs, changelog
- `tests/unit/ext_<id>_test.cpp`, cases `ext_<id>_<what_it_proves>`: pure math, the off path is retail's,
  codec round-trip for C. A `.cpp` the test links goes into `tests/unit/sources.cmake` (hand-maintained).
- Pinned counts move: `Ext::ValueKeyCount()` in `tests/unit/ext_registry_values_test.cpp` (plus checks of
  the new key's min / max / default), the Enhanced `_items` count, the Experimental list.
- Plan or hand-in: `docs/extensions/<NAME>.md` or `docs/extensions/handins/<ID>.md`; the row in
  `docs/extensions/LIST.MD`; a bullet under `## Unreleased` / **Extensions** in `CHANGELOG.md`; a known
  divergence in `docs/known_port_issues.md`.
- Class C that ships: its own resave golden (`docs/runtime_port/resave_gate/golden/Gate<Id>.resaved.md5`,
  how-to in `tools/resave_gate/README.md`).

## 8. Verify and report
- `tools/build.sh unit`, then `tools/build.sh run` (the gate forces the faithful profile, so it proves the
  off path), then a run with the row ON (`ext.<id>=1` in the game dir's `settings.cfg`); `tools/build.sh
  run` under `BANISHED_RENDER_BACKEND=vulkan` too for renderer work. Resave gate before the PR
  (`tools/resave_gate/resave_gate.sh Gate`). The user's own look at it is the skill `hand-test` (settings
  backup, free game dir, hand-over message); landing is the skill `land-pr`.
- Say whether fixes were on. "Faithful" means every `ext.*` off; the `/profile:faithful` run also turns fixes off.
- Perf: the faithful profile must not get slower (a regression there is a defect). A perf extension whose
  A/B is neutral or worse is removed, not left switched off; when it cuts CPU work but fps is GPU-bound,
  present the split and ask (weaker CPUs).

## Traps that bit
- `ext_sprites_data.h` stale after a PNG change: the exe bakes sprites in; only `ext_sprites_bake.py` updates it.
- A valued key used as a runtime "shown" state was overwritten every game start: runtime-only state is a
  plain (atomic) bool in the extension, not a `settings.cfg` key (show_grid review).
- Per-town state in `settings.cfg` makes a save play differently beside another config: that is class C.
- The Extensions dialog drops rows past `kMaxEntries`; `ext_registry_test` checks the table fits.
- Toggling from the dialog without a `Sync()` in `SetAndRecord` leaves the seam stale until restart.
