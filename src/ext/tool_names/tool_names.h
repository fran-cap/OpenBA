#pragma once
// ============================================================================
// src/ext/tool_names/tool_names.h -- ext.tool_names (class C)
// ============================================================================
// NOT RETAIL. docs/extensions/handins/TOOLNAMES.md. The citizen panel names the tool the citizen carries.
//
// RETAIL. A citizen holds no item. Picking up a tool (CitizenAI_PerformWorkCtor5, state SeekTool) posts
// the tool's use count to the citizen (CitizenComponent +0xA8 = the count of the last tool taken, +0xAC =
// uses left) and consumes the item from the storage; nothing remembers which item it was. The panel
// (CitizenUI::OnUpdateLayout) reduces that to three words of its string table: 0 = no tool, 1 = the use
// count equals the citizen description's default (+0x110, 100), 2 = anything else. A stone tool, or any
// modded tool, therefore reads as "iron" or "steel" whatever its name is.
//
// THE EXTENSION. With the row on, the pick-up seam notes which item the citizen is on its way to take
// (the item's RawMaterialDescription, the one whose use count it posts), and the panel shows that item's
// own name, from the item's own string table, instead of the canned word. The note is checked against the
// citizen's use count (+0xA8) before it is trusted, so a note that belongs to another tool, to a citizen
// that no longer exists or to a tool the citizen no longer has is never shown; the panel then falls back
// to retail's word.
//
// CLASS C. The notes are the town's own state: they travel in the extension's record in the save tail
// (this file's Codec; format version 1), so a tool carried across a save and a load keeps its name. A note
// is written as WHO (the citizen's entity id, name and gender, the way ext.family_tree finds a citizen
// again), WHAT (the item's template path, never an index: mods change the load order between sessions) and
// the use count it posted. After a load the records wait in a list; the panel claims the one that matches
// the citizen it shows (same entity id, name, gender and use count) and resolves its path to the item. A
// record whose path the game no longer has, or whose citizen is gone, names nothing.
//
// Standard C++ only (plus savetail.h): tests/unit/ext_tool_names_test.cpp links this header's .cpp with
// savetail.cpp. The key of a live note is the citizen component's address and the item is an opaque pointer
// here; the game side (tool_names_town.cpp) casts.
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_set>

class CitizenComponent;
class RawMaterialDescription;

namespace Ext {
namespace SaveTail { struct StateCodec; }

namespace ToolNames {

// Who a note belongs to, the way the save can name a citizen.
struct Citizen {
    int32_t entity = -1;     // the citizen's entity id (stable across a save and a load)
    std::string name;
    uint8_t gender = 0;      // 0 female, 1 male (the family tree's numbering)
};

// A citizen's tool: the item, the use count it posted and who took it.
struct Note {
    const void* item = nullptr;   // null for a record that came from a save and is not claimed yet
    int32_t uses = 0;
    Citizen who;
    std::string path;             // the item's template path
};

// Citizens are few hundred at most; the cap keeps the table small however long a game runs. A save never holds
// more than this either (it is the decoder's limit). When it is reached the live table is emptied.
constexpr std::size_t kMaxNotes = 4096;
constexpr std::size_t kMaxNameBytes = 255;
constexpr std::size_t kMaxPathBytes = 1024;

// The citizen `citizen` (an opaque key: the component's address) is on its way to take `item`, which posts
// `uses` uses. `who` and `path` are what a save needs to find the citizen and the item again.
void Remember(const void* citizen, const void* item, int32_t uses, const Citizen& who, const std::string& path);

// The item to name for `citizen`, who now carries a tool whose use count is `uses` (+0xA8), or null when no
// trustworthy live note exists.
const void* Lookup(const void* citizen, int32_t uses);

// A record from a save for the citizen `who` carrying a tool of `uses` uses: removed from the list of saved
// records, its item path returned. False when none matches.
bool Claim(const Citizen& who, int32_t uses, std::string& path);

// Drops every live note whose citizen is not in `alive` (citizens who died or left).
void Prune(const std::unordered_set<const void*>& alive);

// Forget one citizen / everyone (a new game, a load, the tests). Clear also empties the saved records.
void Forget(const void* citizen);
void Clear();

// How many live notes / unclaimed saved records there are (the tests).
std::size_t LiveCount();
std::size_t SavedCount();

// The row's save-tail codec (state_codecs.cpp). Encode writes every live note and every record still
// waiting from a load; Decode replaces the waiting records (and empties the live notes) or, on a payload
// that is not exactly what Encode writes, changes nothing and returns false; Reset empties everything.
const SaveTail::StateCodec& Codec();

// ---- the game side (tool_names_town.cpp; exe only) ----
// The pick-up seam: the citizen is on its way to take `item`, which posts `uses` uses.
void RememberPickup(const CitizenComponent* citizen, const RawMaterialDescription* item, int32_t uses);
// The panel seam: the item to name for `citizen`, whose tool has `uses` uses; a live note first, then a record
// that came from a save. Null when neither is trustworthy (the panel keeps retail's word).
const RawMaterialDescription* ToolFor(const CitizenComponent* citizen, int32_t uses);
// GameUpdateLoop::Update, once per frame: drops the notes of citizens who are gone.
void BeginFrame();

}  // namespace ToolNames
}  // namespace Ext
