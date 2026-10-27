# Fixes and extensions -- the design (decided 2026-10-11 with the user)

The port is faithful: `OpenBA.exe` plays retail's game byte for byte. This sheet says how we
change that game on purpose -- fixing retail's bugs, adding features retail never had -- without
losing the faithful build, its gates, or anyone's saves. Rules first, then the shape, then what
every agent must know. Status and the inventory live beside this file (`INVENTORY.md`, `STATUS.md`),
never here.

## 1. Facts the design rests on

- **The save format is retail's and stays retail's.** Every class serializes under its own small
  version (`IO::Serializer(stream, version)`, branches on `GetVersion()` on read). A save carries a
  mod list: one `Save::SaveLoad::ModInfo` per mod (name, path, versionMajor, versionMinor,
  gameVersion). On load `Save::SaveLoad::CheckMod` (retail 0x1800c1090) turns a mismatch into the
  player's known modals: 1 modsMissing, 4 modsChanged, 3 modsVersion, 2 modsOutOfDate.
- **A `.pkm` mod is data only** (compiled `.rsc`, a priority, version pair, overridable flag). No
  code, no dependency list. Load order: `.pkg` / `.pkm` pack stack first, then loose files for
  names NO package holds (`IO::FileSystem::OpenFile`, `filesystem_openfile.cpp:337`): a loose file
  never overrides a packaged object, so every object we ship gets a new name (AOSPIKE S5).
- **`settings.cfg`** (`src/engine/io/settings_file.h`) is a flat key=value map read once at boot by
  `IO::Persistence::ReadSettings` and flushed by `WriteSettings`. `BANISHED_*` env flags are
  harness-only (`src/app/options.h`) and frozen: no new ones (DIRECTION.md §2).
- **The recorded candidates**: `docs/retail_bugs.md` (3 bugs, each with its "faithful lever"),
  the ~59 `DEVIATION` comments in `src/`, and the user's rulings in `docs/known_port_issues.md`
  (rows 452, 1162, 1295 among them).

## 2. The three tiers

| Tier | What | Save format | Default | Where the gate runs |
|---|---|---|---|---|
| 0 Faithful | retail's behaviour | retail's | -- | here, and only here |
| 1 Fix | a retail bug corrected at its faithful lever | retail's, unchanged | **ON** | boot/stress/quit only |
| 2 Extension | a feature retail never had | by class (below) | **class A ON, B / C OFF** (ruled 2026-10-18; `eye_camera` is class A but OFF: it changes how the game is played; so is `no_fog`: it changes how the map looks; so is `phone_menu`: it moves the desktop menu, and the Android first run turns it on; so is `fps_cap`: it caps a desktop's game, which retail leaves uncapped, and the Android first run turns it on at 60) | boot/stress/quit only |

A **fix** is one branch at the one site `retail_bugs.md` names as the lever, retail's path kept
in the other arm. It never changes what is serialized. It may change sim outcomes (that is the
bug), so a save written under fixes loads in retail and in faithful mode with no warning.

An **extension** carries a **class**, which is part of its type and shown in the menu, because
the class says what a toggle costs the player:

| Class | Save impact | Toggle | Examples |
|---|---|---|---|
| **A** | none: no serialized state, no sim effect | live, any time | eye-level citizen camera, renderer work, sharp UI at high scale |
| **B** | sim-divergent, format-neutral | locked for the game's lifetime; recorded in the save | pathfinding changes |
| **C** | format-extending | locked for the game's lifetime; recorded in the save | anything with new per-entity or per-town state |

**What the lock covers (ruled 2026-10-20).** The B / C lock is on the extension's *activation*: whether
it is on for this town. It says nothing about the extension's own state, which may change live for
the whole game (a class B pathfinder's data changes every frame; a class C extension's per-town
settings change whenever the player edits them). A window that edits class C state is fully live;
what the lock fixes is whether that window exists for the town. "A live control cannot be class C"
is a misreading: the control is live, the feature is locked.

**Which class, decided by one test.** Must a reload restore the extension's state for the town to play
on as it was saved? Then the state is per-town and the extension is class C, whatever the state's
size. Such state never lives in `settings.cfg`: a save opened beside a different `settings.cfg`
would play differently while its mod list claims nothing, and class A's "no sim effect" fails too.

Class C state is written in a chunk at the **tail of block 1 (the compressed game-state block)**,
after the last faithful Serialize and before the stream closes (`gameupdateloop_serialize.cpp`,
the save arm of `GameUpdateLoop::SaveRestore`), never inside a retail class's serialization. Retail's
loader stops where it always did and ignores the tail; the block size and CRC cover it automatically.
NOT after the file end: retail checks the exact file size and marks such a save bad (SAVETAIL §1).
Every enabled B/C extension (B included) writes a small manifest record (id, format version) into
that tail; our loader locks the game's extension set from the manifest, because the scenario,
tutorial and autoload routes never read the save's mod list (SAVETAIL §4).

**The save-compat mechanism for B and C.** An enabled B/C extension writes itself into the save's
existing mod list as a pseudo-mod: path `ext:<id>` (a reserved prefix no real `.pkm` path can
carry; an id never contains `.`, so it cannot match a file on Linux either), name = its display
name, ModInfo+0x18 = the extension's format version (the field retail's modal displays), +0x14 =
0x010007ff, +0x10 = 0 (it is the package CRC, never compared for a missing mod). Retail then loads
such a save through the standard modsMissing path -- a Yes/No modal per entry, Yes continues (retail
0x1400b61e0, SAVETAIL §3) -- the same warning a missing `.pkm` gives, which is the paradigm the player
already knows; the tail chunk is never read. Our own loader splits the `ext:` entries out before
CheckMod and shows its own notice for a missing or disabled extension. No header field changes.
Toggling a B/C extension mid-game reuses `Dialog/Reload.rsc:game`, as the Mods dialog does; the
reloaded game keeps the new request although its save-restore wrote the old set into the manifest
(`Ext::KeepRequestOnNextLock`, ruled 2026-10-21): a record of an extension turned off is dropped, one
turned on starts with empty state.
Both write seams (pseudo-mods in `Save::SaveLoad::SaveMetaData`, the tail chunk) write ZERO bytes
when no B/C extension is enabled: the resave gate byte-compares the whole decompressed block 1.

## 3. Shape in the tree

```
src/ext/                      the only home of non-faithful code
  registry.h / registry.cpp   the one table: id, tier, class, default, display name, description,
                              format version (C only); Ext() -- the read-once struct of toggles,
                              filled from settings.cfg at boot (keys `ext.<id>=0|1`, `fix.<id>=0|1`)
  fixes/<id>.cpp              a fix's helper, if its lever needs more than a line
  <id>/                       one directory per extension (its bodies, its .rsc sources)
src/game/Dialog/ExtensionsDialog.*   the menu, cloned from the Mods dialog shape
```

**Hook rules (performance and readability, decided 2026-10-11):**

1. A **fix** is `if (Fix().<id>) { corrected } else { retail }` at the lever, nothing else moved.
   The branch is tiny by construction (a load moved, a different component looked up).
2. A **class A extension** replaces a whole function, selected at the caller or at a seam that
   already exists (the camera controller, the UI layout pass). Live toggles are fine: not hot,
   not sim-affecting.
3. **Class B / C** replace whole bodies, selected at the highest level that works, **once per
   game** (at new game / load, when the class lock is taken). The faithful bodies are never edited.
4. **No toggle is read inside a hot inner loop.** Hoist the decision above the loop or split the
   function. No function-pointer tables, no hook vtables, no indirect-call-everywhere: an indirect
   call blocks inlining of the faithful body at every site and makes the faithful code unreadable.
5. **An engine-side seam is a null-by-default static data pointer** that the game side sets (the
   pseudo-mod provider, `Graphics::SunLight::_extShadowFitView`): `src/engine` never includes
   `src/ext`, the faithful path pays one null test, and the retail body it steers is unchanged. Never
   a virtual, never a function-pointer table.
6. **Measured.** Every extension's hand-in reports the stress run faithful vs. everything-on. Perf is
   recorded, not chased (the user's rule) -- EXCEPT a regression on the faithful profile, which is
   a defect: the faithful path must stay exactly as fast as today.
7. **No game content is distributed** (the user, 2026-10-14). The tree, the exe and anything built
   for handing out hold only our own code and our own resources: no retail shader source (the kit's
   `.srsl`) or DXBC, no hand port or translation of either, no retail texture, model, sound, string
   table or `.rsc` copy, and nothing derived from them. The kit and the `.pkg` are references to read
   (as Ghidra is) and inputs at build or run time on the user's machine. An extension that needs a
   changed version of retail data transforms the user's installed copy at load (the FOLIAGE_AA
   bytecode rewriter is the pattern). It never ships the changed copy. Hand-ins state it.

**Class C state: the contract (ruled 2026-10-20).** A class C extension's per-town state travels in
the payload of its own record in the tail chunk (`src/ext/savetail.h`, `Record::payload`), never in
a retail class's Serialize. The extension gives the registry a pair of callbacks beside its Entry:
*encode* (its state into the payload, at the save arm) and *decode* (from the payload, when the
loader locks the game's set from the manifest). The serializer never learns what the payload means.
Rules: the record exists whenever the extension is active, even with empty state (the lock set must
not depend on content; the faithful profile still writes zero bytes); state refers to game objects by
name (template path, string id), never by index, because mods change load order between sessions;
the extension's `formatVersion` is bumped when the payload's layout changes; a shipped class C
extension gets its own resave golden under its profile (Gates, below). A save that carries an
extension the player has turned off shows our `TurnedOff` notice (`src/ext/load_notice.h`); on Yes
the manifest locks the extension ON for that town and its state is restored (ruled 2026-10-21: the
save keeps the extensions it was made with). Turning it off for a town is the in-game toggle above,
which drops the payload at the reload. Per-entity state (a value per citizen or per building) is a
payload too, keyed by a stable name or id; `decode` runs after the whole world is deserialized, so the
objects exist. A codec's `encode` runs on the save worker (ext.background_save): it reads only plain
state its extension owns, never a UI object, and that state is not written during a frozen update.

**Host records (ruled 2026-10-21, handins/EXTHOSTS.md).** The tail (version 2) carries two kinds of
record, told apart by a kind byte, with a must-understand flag beside it. *Extension records* are the
above: one per active B / C extension, always must-understand, and they ARE the lock set (an unknown id
or a newer `formatVersion` is fatal). *Host records* are the per-town state of a shared surface (the
toolbar window: open, where, which page), PRESENTATION ONLY: anything a reload must restore for the
town to play on as saved is an extension record. A host record is never in the lock set; an unknown
host, a newer host version or a payload its codec refuses is dropped with a log line and the host keeps
its defaults. Host records are written only beside at least one extension record, so a town with only
class A (or no) extensions still writes zero bytes and its window does not reopen. Retail's equivalent
is PlayerToolManager's pinned-window set, which cannot hold a non-retail window: its key is a retail
template path in retail's part of the save. A later record kind needs no new chunk version: an
unknown kind without the must-understand flag is dropped, one with it is fatal. Not built (no
extension needs it yet): state that survives an extension being turned off and on again, which would
be a third kind carried through untouched.

**Surfaces: the UI axis is not the class axis (ruled 2026-10-20).** The class letter answers one
question only, what a toggle costs the player's save. Where an extension shows up in the UI is a
separate axis, a *surface*, and every surface is built once as a host that extensions contribute to:

| Surface | Host | What an extension contributes |
|---|---|---|
| Extensions dialog (front end) | `src/game/Dialog/ExtensionsDialog.*` | its registry row (mandatory), in the Optional tab when `defaultOn` is false, in the Default tab when it is true |
| Enhanced tab (Options dialog) | `src/ext/enhanced_tab/` | rows for its class A values (`EnhancedTab::Contribution`: open / apply / close) |
| In-game toolbar window | one extension button on the toolbar opening a window of pages, built with the first extension that needs it | a page, offered only while the extension is active for the town |

A new window is never a new class and never a button per feature: the toolbar gets ONE extension
button, the window shows one page per active contributing extension, and with no active contributor
the button is not added and the toolbar is retail's. Dialog resources are our own text `.rsc`
served by the resource providers (KITFREE.md §2), as the Extensions dialog's are.

*Example, a generic class C extension with its own window.* An extension `ext.<id>` that keeps a
per-town list the player edits in game ships as:
```
src/ext/<id>/
  <id>.h / <id>.cpp        the bodies behind the one sim seam (the decision hoisted out of the
                           per-entity loop into a flag on the object, hook rule 4)
  save_state.cpp           encode / decode of the list for the tail record (names, not indices)
  toolbar_page.cpp         the page contribution: find its elements by name, fill, apply
  src/ext/ui/Dialog/<Id>Page.rsc   the page's objects (ours; derived from the installed base
                           objects at load, rule 7)
src/ext/registry.{h,cpp}   the row: class C, formatVersion 1, default OFF, + the state callbacks
tests/unit/ext_<id>_test.cpp   the codec round-trips, the off path is retail's bit for bit
docs/extensions/handins/<ID>.md
```
plus `// EXT SEAM` lines at the sim site and the toolbar host, each citing the hand-in. The
Extensions dialog shows the row; the toolbar page appears when the row is locked active. A second
extension of the same kind adds its own row, its own record and its own page; it shares the button,
the window shell and the state contract.

**The format to follow (code).** Every name below that does not exist yet (`SaveTail::StateCodec`,
`Toolbar::Contribution`) is built by the first extension that needs it, in the shape and under
the name shown (the Names table below), and the next extension reuses it. Existing names are the real ones.

*1. The registry row.* `src/ext/registry.h` gets the bool, `registry.cpp` the row, exactly like
every row today (class C, `formatVersion` 1, default OFF):
```cpp
// registry.h, struct Extensions
bool my_list = false;   // class C: src/ext/my_list/ (docs/extensions/handins/MYLIST.md)

// registry.cpp, kEntries (after the last extension row)
// MYLIST (docs/extensions/handins/MYLIST.md): a per-town list the player edits in game.
{ "my_list", Tier::Extension, Class::C, false,
  "My list",
  "Lets the player keep a per-town list of things. Play diverges; recorded in the save.",
  1, [](State& s) -> bool& { return s.ext.my_list; } },
```

*2. The state contract.* A codec per class C extension, registered beside the Entry by id. The
serializer calls it through the registry and never includes the extension's header:
```cpp
// savetail.h (new, shared): what a class C extension gives the save tail
namespace Ext { namespace SaveTail {
struct StateCodec {
    const char* id;                                       // the Entry id
    void (*encode)(std::vector<uint8_t>& payload);        // the save arm: append this town's state
    bool (*decode)(const uint8_t* bytes, size_t size, int32_t savedFormatVersion);
                                                          // the load arm: false = malformed (fatal)
    void (*reset)();                                      // a new game, or the game ends: empty state
};
const StateCodec* FindCodec(const char* id);              // null: a class B row, no state (state_codecs.cpp)
}}

// src/ext/my_list/save_state.cpp: the codec and its registration
namespace Ext { namespace MyList {
namespace {
// Wire format v1 (little-endian): u32 count, count x { u32 len, u8 name[len] }.
// Names are template paths, never indices (mods change load order between sessions).
void Encode(std::vector<uint8_t>& out) {
    SaveTail::Writer w(out);
    w.U32(static_cast<uint32_t>(g_names.size()));
    for (const std::string& n : g_names) w.String(n);   // u32 len + bytes
}
bool Decode(const uint8_t* p, size_t size, int32_t savedVersion) {
    if (savedVersion != 1) return false;   // Validate already refused a NEWER version; older = migrate here
    SaveTail::Reader r(p, size); uint32_t count = 0;
    if (!r.U32(count)) return false;
    g_names.clear();
    for (uint32_t i = 0; i < count; ++i) { std::string n; if (!r.String(n)) return false; g_names.push_back(n); }
    return r.AtEnd();
}
void Reset() { g_names.clear(); }
}  // namespace
const SaveTail::StateCodec& Codec() { static const SaveTail::StateCodec c = { "my_list", Encode, Decode, Reset }; return c; }
}}  // namespace Ext::MyList

// src/ext/state_codecs.cpp (linked into the unit build too; a codec is standard C++), kCodecs: one line per class C extension
const SaveTail::StateCodec* const kCodecs[] = { &MyList::Codec(), };
```
The two serializer arms (`gameupdateloop_serialize.cpp`, `ExtSave_WriteTail` / `ExtSave_ReadTail`)
grow by one loop each and stay the only place the tail is touched:
```cpp
// SAVE: savetail.cpp ActiveRecords(), after r.formatVersion = e.formatVersion
if (const StateCodec* c = FindCodec(e.id)) c->encode(r.payload);   // class B: no codec, empty payload

// LOAD: ExtSave_ReadTail, after LockForGameWithSet succeeded
for (const Ext::SaveTail::Record& r : records)
    if (const Ext::SaveTail::StateCodec* c = Ext::SaveTail::FindCodec(r.id.c_str()))
        if (!c->decode(r.payload.data(), r.payload.size(), r.formatVersion))
            ExtSave_Fatal("This save's ext:" + r.id + " state is damaged.");
```
`reset` runs for every codec at new game and in `Ext::UnlockForGame`, so a town never inherits the
previous town's list. With the row inactive for the town the codec is never called: the state is
empty and the next save carries no record.

*3. The toolbar page.* The same shape as `EnhancedTab::Contribution`, one host, one page per active
extension. The host is `src/ext/toolbar/toolbar.{h,cpp}` and the page's objects are the extension's
own text `.rsc`:
```cpp
// src/ext/toolbar/toolbar.h (new, shared)
namespace Ext { namespace Toolbar {
struct Contribution {
    const char* id;        // the Entry id; the page is offered only while Active(entry)
    const char* caption;   // the tab's text: a key in Dialog/ExtensionsStringTable.rsc
    const char* page;      // "Dialog/MyListPage.rsc:pageMyList", our text .rsc
    void (*open)(UI::Element* page, int32_t idBase);     // find the elements by name, give the controls
                                                         // ids from idBase, fill them from the state
    bool (*changed)(UI::Element* page, int32_t id);      // a control (local id) was used: write the state
                                                         // (live, no Apply); false = not mine
    void (*close)();                                     // forget the element pointers
};
constexpr int32_t kPageIdSpan = 10000;           // the element ids one page owns: [idBase, idBase + span)
bool Offered();                                  // any active contributor, and not the forced faithful profile
// the button, built by ToolbarDialog while the Reports group is made (one EXT SEAM per site)
int  InsertAt(const ::Toolbar* group, const ::Toolbar* const* items, int n);   // slot, or -1: no button
void Style(UI::Element* button, int32_t index);  // name, id, tooltip, icon of the cloned button
void Toggle(UI::UserInterface* ui);              // the click: open the window, or close it
}}

// src/ext/my_list/toolbar_page.cpp
namespace Ext { namespace MyList {
namespace {
UI::TableElement* g_table = nullptr;
void Open(UI::Element* page, int32_t idBase) { g_table = FindElement<UI::TableElement>(page, "tableMyList"); /* ids from idBase, fill rows */ }
bool Changed(UI::Element* page, int32_t id) { /* id is local: 0 .. kPageIdSpan - 1; read the check, mark the object flags (section 4); false if not mine */ return false; }
void Close() { g_table = nullptr; }
}  // namespace
}}
const Ext::Toolbar::Contribution& Ext::Toolbar::MyListPage() {
    static const Contribution c = { "my_list", "MyListTab", "Dialog/MyListPage.rsc:pageMyList", MyList::Open, MyList::Changed, MyList::Close };
    return c;
}
```
The host's `// EXT SEAM` comments are the few sites in `ToolbarDialog` (`src/game/Dialog/toolbardialog.cpp`)
that call `InsertAt` / `Style` / `Toggle`; the toolbar keeps a **null** entry in its item array for the
button, so every retail reader of that array skips null. The page table in `toolbar.cpp` lists
`MyListPage()` the way `enhanced_tab.cpp` lists its entries. **Element ids belong to the host:** each open
page owns the span `[idBase, idBase + kPageIdSpan)` (`IdBaseFor(pageIndex)`), `open` receives `idBase`, a
control gets `idBase + local` as its element id, and `changed` is called only for ids in the page's own
span, with the LOCAL id, so two pages can never answer each other's clicks. Shared helpers (`OurString`,
`SetTableText`, `Lower`) are exported by the host; a page never carries its own copy. The window shows
`kMaxPages` (8) pages and logs any beyond that. The tab's label is the text `caption` (a key in the string
table), not a sprite.

*4. The sim seam.* The decision is made once when the list changes and stored on the object; the
per-entity loop tests a flag and never a container or the toggle (hook rule 4):
```cpp
// src/ext/my_list/my_list.h
namespace Ext { namespace MyList {
bool Listed(const Game::Thing* t);     // the flag the page's Changed() set on the object
}}

// the faithful site, src/game/<Module>/<file>.cpp -- the ONE seam, cites the hand-in
// EXT SEAM (NOT RETAIL, docs/extensions/handins/MYLIST.md): with ext.my_list active for this town,
// a listed thing is skipped here; retail's loop is otherwise untouched.
if (Ext::MyList::Listed(thing)) continue;
```
`Listed` is a flag read; with the row inactive every flag is clear, so the faithful path pays one
predictable branch and the resave gate stays byte-identical. Where the extension has no object to mark (a
test on a material in a per-row picker, `ext.restricted_foods`), the decision is hoisted above the loops
(`const bool on = g_on && ...`) and the loop scans a short flat array of object pointers rebuilt only when
the list or the lock epoch changes: no map and no string work per row.

*5. The tests.* `tests/unit/ext_my_list_test.cpp`: the codec round-trips (encode then decode gives
the same list), decode refuses a truncated payload, `reset` empties the state, and the sim seam's
helper returns false for every object while the row is off.

**Names (the standard, ruled 2026-10-20).** Fixed here so every extension spells things the same
way. The existing extensions were brought into line with it on 2026-10-20
(block ext-standard). `<id>` is the settings id
(`[a-z0-9_]+`, e.g. `my_list`), `<Id>` its PascalCase (`MyList`), `<ID>` its upper case with the
underscores dropped (`MYLIST`).

| Thing | Name |
|---|---|
| settings key / registry id | `ext.<id>` / `fix.<id>`; the bool `Extensions::<id>`; a valued key `ext.<owner>_<what>` |
| directory | `src/ext/<id>/` (one per extension; a row with no code has none) |
| namespace | `Ext::<Id>` (`Ext::EyeCamera`, `Ext::SpreadWarmth`); shared hosts `Ext::EnhancedTab`, `Ext::Toolbar`, `Ext::SaveTail`, `Ext::Resources` |
| bodies + the seam helpers | `src/ext/<id>/<id>.h` / `<id>.cpp` (the header every faithful seam includes) |
| Enhanced tab rows | `src/ext/<id>/enhanced_entry.cpp`, accessor `Ext::EnhancedTab::<Id>Entry()` declared in `enhanced_tab.h` |
| toolbar page | `src/ext/<id>/toolbar_page.cpp`, accessor `Ext::Toolbar::<Id>Page()` declared in `src/ext/toolbar/toolbar.h` |
| class C codec | `src/ext/<id>/save_state.cpp`, accessor `Ext::<Id>::Codec()` declared in `<id>.h`; the struct `Ext::SaveTail::StateCodec` in `savetail.h`; the table `kCodecs` + `Ext::SaveTail::FindCodec(id)` in `src/ext/state_codecs.cpp` (linked into the unit build too, so a codec stays standard C++ with no engine headers) |
| host codec | a shared host's `Ext::SaveTail::HostCodec` (`id`, its own `formatVersion`, `encode` / `decode` / `reset`) in the host's own directory (`src/ext/toolbar/window_state.cpp`: `Toolbar::WindowCodec()`, host id `toolbar`); the table `Ext::SaveTail::FindHostCodec(id)` in `src/ext/host_codecs.cpp` (linked into the unit build too), only for hosts of the Surfaces table |
| byte helpers | `Ext::SaveTail::Writer` (`U32`, `I32`, `String`, `Bytes`) and `Ext::SaveTail::Reader` (`U32`, `I32`, `String`, `Bytes`, `AtEnd`), promoted from `savetail.cpp` to `savetail.h` by the first codec; no per-extension copies |
| codec callbacks | `encode` / `decode` / `reset`; the wire format is documented at the top of `save_state.cpp` ("Wire format v<n>") |
| load and reset arms | `Ext::SaveTail::ApplyState(records, &offending)` hands each record's payload to its codec, once, after the manifest's lock on both load routes; `Ext::SaveTail::ResetAllState()` runs every codec's `reset` (new game, and inside `Ext::UnlockForGame` for the lock's owner only) |
| toolbar host | `src/ext/toolbar/toolbar.{h,cpp}`, `toolbar_window.cpp`: `Contribution { id, caption, page, open, changed, close }`, `Offered()`, `InsertAt` / `Style` / `Toggle` for the button, `kPageIdSpan` / `IdBaseFor` / `OwnsId` for element ids, the shared helpers `OurString` / `SetTableText` / `Lower` |
| element lookup | `Ext::FindElement<T>(page, name)` in `src/ext/ui/find_element.h` (the Enhanced tab's contributions use it) |
| list rows that fill the list | `Ext::ListWidth(content)` in `src/ext/ui/list_width.h` (the scrolling list's viewport width, from its container); the controller gives the cells that width as `_minWidth` and the cell's `Flags _flags = ExpandWidth;` lays its last element across it (the Foods page); when the element that takes the space is not the last, its `_minWidth` is raised by the space instead (the Priority page's `FitRow`); never a pixel width for the row in the page's `.rsc` |
| derived-data cache | `src/ext/derived_cache/` (`Ext::DerivedCache::Store`, the shared one `OpenShared()` / `Shared()`), files in `<Documents>\Banished\Cache\`; a user names itself by a producer FourCC and an encoder version in its `Key` (`AOB4`, `CompressAo::kEncoderVersion`; `PKIX`, ext.package_cache's package directory, handins/PKGCACHE.md); the one cap `ext.derived_cache_mb` (owned by its first user, compress_ao); never opened under the faithful profile (handins/COMPRESSAO.md) |
| shared GPU objects | `src/render/common/share_gpu_data/share_table.h` (`GpuShare::Table`: key = device + descriptor blob + content hash, users counted, the table's own reference; backend-neutral), the DX11 create-or-share `src/render/dx11/share_gpu_data/share_create.h`; a backend shares only immutable objects (SHAREGPUDATA.md) |
| resource records (the stream cache) | `src/ext/stream_cache/`: `stream_record.h` (`Ext::StreamCache::Recorder` / `Bundle` / `RecordView`: the ranges of a decompressed resource stream a load consumed, with their bytes, per packed entry and per package, kept as chunks in the derived cache), `segment_stream.h` (`SegmentStream`: the IO::Stream a load reads from a record), the seams `Object::ExternalFactory::_extLoadBegin` / `_extLoadEnd`, `IO::CompressedStream::_extReadTap`, `Engine::LoaderLoop::_extLoadDone`, `CreateOnDemand::g_altLocate` / `g_altSkip` (STREAMCACHE.md) |
| resource-table shortcuts | `src/engine/object/resource_ext.h` (`Object::ExtVisitReplay` / `Object::ExtFindFast`, the seams `Object::ExternalFactory::_extVisitReplay` / `_extFindFast`, the change feed `Object::NotifyResourceEvent` with `Object::ResourceEvent`); `src/ext/visit_replay/` (`edge_store.h`: `Ext::VisitReplay::EdgeStore`, a row's recorded hit-arm calls) and `src/ext/find_fastpath/` (`request_table.h`: `Ext::FindFastpath::RequestTable`, request characters to a row); the hit arm's halves `RtObject_ExternalFactory_ArmMark` / `VisitRow` / `AddDirect` (VISITREPLAY.md) |
| first-use GPU creates | `src/render/common/create_on_demand/`: `payload.h` (`CreateOnDemand::Locator` = package mapping + entry offset / size + decompressed pos / len, the extension's own refcounted read-only `PackageMaps` detached by `IO::FileSystem::_extHandleClosing`, `RedecodeRange`), `stream_locate.h` (`Locate` / `Skip` over a CompressedStream on a packed entry), `lazy_table.h` (`LazyTable`: one materialization per object, by a use or a prefetch worker; `Pause` / `Resume` around the device), `pending_levels.h`; the DX11 records `src/render/dx11/create_on_demand/lazy_create.h` (CREATEONDEMAND.md) |
| our `.rsc` text | `src/ext/ui/Dialog/<Id>Page.rsc`, root object `page<Id>`; elements `<kind><Id><What>` (`tableMyList`, `checkMyListRow`, `comboAmbientOcclusion`); strings in `Dialog/ExtensionsStringTable.rsc`, `_name = "<Id><What>"` |
| sprites | `src/ext/ui/Dialog/Sprite/Extensions/Entry_<id>.png` (the dialog row's icon), `Glyph_<id>.png` (a white picture-button glyph, drawn by a `GLYPHS` table of a `tools/ext_icons/*.py` module), `Toolbar_extensions.png` (the one shared button); baked by `tools/ext_sprites_bake.py` |
| per-object flag | never a new field in a retail class (every `sizeof` is asserted): a side table in the extension keyed by the object's id, or a documented spare bit; engine-side seams are `_ext<What>` static data pointers (`Graphics::SunLight::_extShadowFitView`, `Graphics::Renderer::_extDrawItemList`); a seam with more than one call point is one function pointer taking a point constant (`Engine::MainLoop::_extFrame`, `Engine::LoaderLoop::_extFrame` with `kExtUpdate` / `kExtDraw`), and counters a loader-thread body publishes are a struct of relaxed atomics behind a null pointer (`Engine::UpdateLoop::_extLoadProgress`); a backend-side seam is a `g_ext<What>` atomic in the backend (`VideoDX11::g_extDrawBatchVs`) |
| draw-item tag | an extension's own draw items carry a key value the faithful code never emits, named in the extension's rules header (`Ext::TerrainBatch::kRunTag`, WorldGeometry key bits `0xC000`) and tested at the faithful draw's branch by an `Is<What>Key` helper; the unit tests prove no retail key matches |
| finger input | the platform's finger events go to ONE sink, `Platform::Touch::SetSink` (`src/platform/common/platform_touch.h`, body `src/platform/sdl/touch_sdl.cpp`; a set sink turns SDL's touch-to-mouse off); what the game sees is written after the poll through `Input::Device::_extAfterUpdate` (one latched pointer state per poll, one-step edges on the KeyMap-bound input of an action, never a scancode); camera motion from fingers goes through ext.touch_mode's hold, `Ext::TouchMode::Hold(Source::Fingers, ...)` (ext.touch_pointer, `docs/android/P11_TOUCH_POINTER.md`); a finger pressing a dialog keeps an aiming tool's ghost through `Ext::TouchPointer::HoldToolAim` / `ToolAimHeld` / `ToolCursor` (PlayerToolManager::Update, the tool draw flag, the tools' cursor ray), and a held press counts tooltip hover time through `UI::UserInterface::_extHoverWhileDown` (ext.phone_menu, `docs/android/P11B_POINTER.md`) |
| a fix on a seam another row owns | the owner's hook calls the fix's own function (`src/ext/<id>/<id>.h`) before its own row's test, so the fix runs with that row off; the fix tests its own bool (`Ext::DialogPositionDrift::OnSetUpperLeft`, called from ext.phone_menu's `Place` at `UserInterface::kExtPlaceUpperLeft`; handins/DIALOGPOSITIONDRIFT.md) |
| seam comment | `// EXT SEAM (NOT RETAIL, docs/extensions/handins/<ID>.md): <one line of what and when>` at every faithful site, including dialog arms |
| log prefix | `[<Id>]` on stderr (`[ExtSave]`, `[AmbientOcclusion]`); never stdout |
| hand-in / block | `docs/extensions/handins/<ID>.md`; a follow-up block `<ID>2`, `<ID>-<PART>` (`FAA-SHADOW2`) |
| unit tests | `tests/unit/ext_<id>_test.cpp`, cases `ext_<id>_<what_it_proves>` |
| list / status rows | `docs/extensions/LIST.MD` (`Ext C | <display name> | ...`), `docs/runtime_port/STATUS.md` |

**Rules that carry over unchanged.** No doors (`tools/acceptance/acceptance.sh` at target: nothing
registers or resolves a body by retail name or address). No null guard on a faithful crash. Every
fix cites the retail address and quotes the lever from `retail_bugs.md`. No `getenv`. No new
`BANISHED_*` flags: a toggle is a `settings.cfg` key and a menu entry.

**Gates.** The existing gate (`tools/build.sh run`, resave x5 against the golden, unit, castmetric,
acceptance, worldcmp) runs with the **faithful profile** forced (`fix.*=0`, `ext.*=0` for the run).
A second, cheaper gate runs everything on and only has to boot, run the stress town and quit clean
(exit 0, no crash / hang log). A class C extension that ships gets its own resave golden under its
profile.

**Menu.** The front end gets an Extensions button beside Mods with its own icon (a new `.rsc` and
sprite is fine). The dialog lists fixes and extensions with the class letter, the lock state
("live" / "locked for this game"), the toggle, and the description, in two tabs in registry order:
Optional (shown when the dialog opens: the rows with `defaultOn` false, where the player adds
extras) and Default (the rows with `defaultOn` true: every fix and the default-on extensions, where
one is switched off). The split is presentation only: keys, lock and save manifest are unchanged. A B/C toggle in-game shows the
same restart/reload notice the Mods dialog shows.

## 4. Order of work

1. **Scaffold** (block EXTSCAFFOLD): `src/ext/registry`, the settings keys, `Ext()` / `Fix()`, the
   Extensions dialog + button + icon, the faithful-profile force for the gate, unit tests for the
   registry and the settings keys.
2. **Fix pilots** (block FIXPILOT): the three recorded retail bugs as `fix.icon_slide`,
   `fix.residence_goto`, `fix.tavern_counter`, each at its lever.
3. **Verification** (block SAVETAIL): DONE, see §5.
4. **Inventory** (block EXTINVENTORY): every `DEVIATION` comment, every RULED row and every
   retail bug classified restore / fix / extension(A|B|C) with the lever, into `INVENTORY.md`.
5. Then the first extensions, in the user's order: **A** eye-level citizen camera; **B**
   pathfinding; **A** sharp UI at high scale (font / UI scaling or integer upscaling).

## 5. Open until verified

- SAVETAIL: DONE 2026-10-11 (`handins/SAVETAIL.md`, all five VERIFIED): the chunk moved from the file
  tail to block 1's tail (above); modsMissing continues on Yes; `ext:` never matches a `.pkm`. Not
  clicked through in retail, proved from the bytes and the ToolKit's dialog resource.
- Whether a loose text `.rsc` (the Extensions dialog, its icon) is loaded by the shipped loader
  or must be compiled (`Tools-x64.exe`) into a `.pkg`; the scaffold block finds out and records it.
