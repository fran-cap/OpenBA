# TOOLNAMES hand-in

`ext.tool_names`: the citizen panel shows the name of the tool the citizen carries. One class C toggle, **off by default**, fixed for a game once it starts (the game's lock), no effect on the simulation. Which tool each citizen carries is recorded in the save's tail.

## 1. What it does

A citizen holds no item. When a citizen with no tool fetches one (`CitizenAIComponent::State_SeekHappiness`, state id 8, which the ported source names for its limit scan; the item-priority extension calls the same finder category "Tool"), `CitizenAI_PerformWorkCtor5_bd070` posts the tool's use count to the citizen (`CitizenComponent` +0xA8 = the count of the last tool taken, +0xAC = uses left, message 1) and consumes the item from the storage. Nothing remembers which item it was.

`CitizenUI::OnUpdateLayout` then reduces that to three words of the text element's string table: index 0 = no tool (+0xAC < 1), 1 = the use count (+0xA8) equals the citizen description's default (`_i110`, 100), 2 = anything else. A stone tool, or any modded tool, reads as one of the two canned words (iron / steel in the shipped table), whatever its name is.

With the row on, the pick-up seam notes which item the citizen is on its way to take (the item's `RawMaterialDescription`, the one whose `_i54` it posts), and the panel names that item from the item's own string table (`_stringTable`, `_name`, the pair the storage and production dialogs use for item labels).

## 2. The seams

Three, plus a header:

* `CitizenAI_PerformWorkCtor5_bd070` (`src/game/Component/citizenai_actionctors.cpp`): after the limit component is found, `Ext::ToolNames::RememberPickup(citizen, limit, limit->_i54)`.
* `CitizenUI::OnUpdateLayout` (`src/game/Dialog/citizenui.cpp`): when the citizen has a tool, `Ext::ToolNames::ToolFor(citizen, +0xA8)`; if it returns an item with a string table and a valid name, that `UI::String` replaces retail's before `SetText`.
* `GameUpdateLoop::Update` (`src/game/Game/gameupdateloop.cpp`): `Ext::ToolNames::BeginFrame()`, once per frame, which every 600 frames drops the notes of citizens who are gone, and on the first frame after a load drops the waiting records of citizens who are gone.

A live note is checked against the citizen's use count before it is trusted, so a note that belongs to another tool, to a citizen that no longer exists (a new citizen at a reused address) or to a tool the citizen no longer has is never shown; the panel then falls back to retail's word. With the row off the seams are one load and a branch, and the panel is retail's own.

## 3. Class C: the save record

The notes are the town's own state: a reload that forgot them would show retail's word for a tool the player has already seen named. They travel in the row's record in the save tail (`SaveTail::StateCodec`, `Ext::ToolNames::Codec`, format version 1), the way `ext.item_priority`'s lists do (DESIGN.md "Class C state: the contract").

A note is written as WHO, WHAT and how many uses:

| field | what it holds |
|---|---|
| entity | the citizen's entity id (`ComponentEntity::_entityId`, stable across a save and a load; `ext.family_tree` finds a living citizen again by the same id) |
| gender, name | the citizen's, checked at the claim so a reused id is never mistaken for the citizen |
| path | the item's template path (`Object::External::GetPath`), never an index: mods change the load order between sessions |
| uses | the use count the pick-up posted |

Wire format v1 (little-endian): `u32 count` (0..4096), then per note `i32 entity`, `i32 uses` (at least 1), `u8 gender` (0 or 1), `u32 length` + name bytes (up to 255, no NUL), `u32 length` + path bytes (1..1024, no control characters). The decoder accepts exactly what the encoder writes and refuses a count the payload cannot hold, a bad gender or use count, an over-long or NUL-holding name, an empty, over-long or control-character path, an entity twice, bytes left over, and a cut-short payload; a refused payload changes nothing.

**After a load.** `Decode` runs once the world is deserialized and leaves the records waiting in a list (it also empties the live notes of the game that was there). The panel does the matching when it is opened: for the citizen it shows, with no live note, it claims the record with the same entity id, name, gender and use count, resolves the record's path to the loaded item (the factory walk the game uses to enumerate what its packages loaded), checks the item's own use count (`_i54`) still equals the citizen's, and makes it a live note. A record whose path the game no longer has, whose citizen is gone, or whose tool has changed names nothing, and the panel keeps retail's word. The first frame after a load, `BeginFrame` drops every waiting record that does not match a living citizen (same entity id, name and gender), so a citizen who died before their panel was opened leaves nothing in later saves; the same check runs with the 600-frame prune. It is skipped while the town has no citizens at all (a game that has not finished loading). A second save made before the panel was opened keeps the records of the citizens who are still there. A citizen who takes a new tool before the panel is opened replaces the record for their entity.

**Threads.** `encode` runs on the save worker (`ext.background_save`) while the panel on the main thread may claim a record, so the two tables are behind one mutex. The table is capped at 4096 notes (far above any town's population); on reaching the cap the live table is emptied and the citizens show retail's word until their next pick-up.

**The lock.** A class C row is held by the game's lock once the game starts: the toggle is the Extensions dialog's before a game, not live in one. A save made with the row on carries the record; a build without the row refuses to load it as it would any unknown extension, and the `TurnedOff` notice (`src/ext/load_notice.h`) covers a save made with the row on loaded with it off.

## 4. Where

| file | change |
|---|---|
| `src/ext/tool_names/tool_names.h`, `tool_names.cpp` (new) | the note table, the claim, the prune and the codec: standard C++, unit-tested |
| `src/ext/tool_names/tool_names_town.cpp` (new) | the game side: a citizen's entity id, name and gender; an item's template path; the factory walk from a path to the item; `BeginFrame` |
| `src/game/Component/citizenai_actionctors.cpp`, `src/game/Dialog/citizenui.cpp`, `src/game/Game/gameupdateloop.cpp` | the three seams and their includes |
| `src/ext/registry.{h,cpp}`, `src/ext/state_codecs.cpp` | the row (class C, version 1, OFF) and its codec |
| `src/ext/ui/Dialog/ExtensionsSprites.rsc`, `Sprite/Extensions/Entry_tool_names.png`, `src/ext/resources/ext_sprites_data.h`, `tools/ext_icons/toolnames.py` | the row icon (programmer art, ours), baked with `tools/ext_sprites_bake.py` |
| `tests/unit/ext_tool_names_test.cpp` (new), `tests/unit/sources.cmake` | the tests |
| `docs/extensions/LIST.MD`, `docs/runtime_port/STATUS.md` | the list row, the status row |

No game content ships (DESIGN.md hook rule 7): the names come from the running game's own item descriptions.

## 5. Verified and not verified

* **Seen in a running game (Windows, GOG install):** a modded tool is named, not "iron" or "steel"; a new pick-up is named; a citizen with no tool shows the "no tool" text; turning the row off before a game gives retail's words; the names survive a save and a load, also after a second save made before the panel was opened.
* **Unit:** the row, the lock, the note table, the prune, the prune of waiting records, the cap, the codec's round trip and claim, and every refusal (`tests/unit/ext_tool_names_test.cpp`).
* **Performance (hook rule 6):** not measured. The pick-up seam adds a path string and one table store per tool fetched (a few per citizen per tool lifetime); the panel seam adds one lookup per panel update while the panel is open (a second lookup only while records wait from a load); `BeginFrame` is a counter except every 600 frames.
* **Not run:** Linux, macOS, the resave gate under this row.
