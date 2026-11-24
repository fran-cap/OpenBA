// ============================================================================
// src/ext/storage_filter/save_state.cpp -- ext.storage_filter's per-town state in the save tail (class C)
// ============================================================================
// NOT RETAIL. docs/extensions/handins/STORAGEFILTER.md, DESIGN.md "Class C state: the contract".
//
// The state is the town's list of refused items and caps per storage building (storage_filter.h). A building
// is stored by its footprint's corner tile (an entity id is not stable: removing any entity renumbers the
// last one), an item by the template path of its RawMaterialDescription, never by index: mods change the
// load order between sessions, and a path the game no longer has simply refuses nothing.
//
// Wire format v1 (little-endian):
//   u32  count            0..kMaxBuildings
//   count x {
//     i32  tileX, tileY   the storage building's footprint corner tile, both >= 0
//     u32  items          0..kMaxItemsPerBuilding
//     items x {
//       u32  length       1..kMaxPathBytes
//       u8   path[length] the template path, bytes 0x20..0xFF, no NUL
//     }
//     u32  caps           0..kMaxItemsPerBuilding; items + caps >= 1
//     caps x {
//       u32  length, u8 path[length]   as above
//       i32  limit        1..kMaxCap
//     }
//   }
// Decode accepts exactly what Encode writes and refuses the payload (the save is damaged) on a count the
// payload cannot hold, a negative or repeated building tile, an entry with nothing in it, too many items, a
// repeated or out-of-range cap, an empty, over-long or control-character path, a path twice in one building
// (compared without case), bytes left over, or a cut-short payload. A refused payload changes nothing: the
// old list stays.
//
// Standard C++ only: tests/unit/ext_storage_filter_test.cpp links this file with storage_filter.cpp and
// savetail.cpp.
// ============================================================================

#include "ext/storage_filter/storage_filter.h"

#include "ext/savetail.h"

#include <cctype>
#include <string>
#include <unordered_set>
#include <vector>

namespace Ext {
namespace StorageFilter {
namespace {

// tile x, y + item count + cap count + one length + a one-byte path
constexpr size_t kMinEntryBytes = 4 + 4 + 4 + 4 + 4 + 1;
constexpr size_t kMinItemBytes = 4 + 1;            // a length and a one-byte path

std::string Fold(const std::string& s) {
    std::string out = s;
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

void Encode(std::vector<uint8_t>& payload) {
    const std::vector<Entry>& entries = Entries();
    SaveTail::Writer w(payload);
    w.U32(static_cast<uint32_t>(entries.size()));
    for (const Entry& e : entries) {
        w.I32(e.tileX);
        w.I32(e.tileY);
        w.U32(static_cast<uint32_t>(e.refused.size()));
        for (const std::string& path : e.refused) {
            w.String(path);
        }
        w.U32(static_cast<uint32_t>(e.caps.size()));
        for (const Entry::Cap& c : e.caps) {
            w.String(c.path);
            w.I32(c.limit);
        }
    }
}

bool Decode(const uint8_t* bytes, size_t size, int32_t savedVersion) {
    if (savedVersion != 1) {   // Validate already refused a NEWER version
        return false;
    }
    SaveTail::Reader r(bytes, size);
    uint32_t count = 0;
    if (!r.U32(count) || count > kMaxBuildings || count > r.left / kMinEntryBytes) {
        return false;
    }
    std::vector<Entry> entries;
    std::unordered_set<uint64_t> buildings;
    for (uint32_t i = 0; i < count; ++i) {
        Entry e;
        uint32_t items = 0;
        if (!r.I32(e.tileX) || !r.I32(e.tileY) || e.tileX < 0 || e.tileY < 0) {
            return false;
        }
        if (!r.U32(items) || items > kMaxItemsPerBuilding || items > r.left / kMinItemBytes) {
            return false;
        }
        const uint64_t key = (static_cast<uint64_t>(static_cast<uint32_t>(e.tileX)) << 32) |
                             static_cast<uint32_t>(e.tileY);
        if (!buildings.insert(key).second) {   // a building twice
            return false;
        }
        std::unordered_set<std::string> folded;
        for (uint32_t k = 0; k < items; ++k) {
            std::string path;
            if (!r.String(path) || !PathIsValid(path)) {
                return false;
            }
            if (!folded.insert(Fold(path)).second) {   // a path twice
                return false;
            }
            e.refused.push_back(path);
        }
        uint32_t capCount = 0;
        if (!r.U32(capCount) || capCount > kMaxItemsPerBuilding || capCount > r.left / (kMinItemBytes + 4) ||
            (items == 0 && capCount == 0)) {
            return false;
        }
        std::unordered_set<std::string> foldedCaps;
        for (uint32_t k = 0; k < capCount; ++k) {
            Entry::Cap c;
            if (!r.String(c.path) || !PathIsValid(c.path) || !r.I32(c.limit) || c.limit < 1 ||
                c.limit > kMaxCap) {
                return false;
            }
            if (!foldedCaps.insert(Fold(c.path)).second) {
                return false;
            }
            e.caps.push_back(c);
        }
        entries.push_back(e);
    }
    if (!r.AtEnd()) {
        return false;
    }
    ReplaceEntries(entries);
    return true;
}

void Reset() {
    ResetState();
}

}  // namespace

const SaveTail::StateCodec& Codec() {
    static const SaveTail::StateCodec c = { kId, Encode, Decode, Reset };
    return c;
}

}  // namespace StorageFilter
}  // namespace Ext
