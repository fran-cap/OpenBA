// ============================================================================
// src/ext/item_priority/save_state.cpp -- ext.item_priority's per-town state in the save tail (class C)
// ============================================================================
// NOT RETAIL. docs/extensions/handins/ITEMPRIO.md, DESIGN.md "Class C state: the contract".
//
// The state is the town's four ranked lists (item_priority.h). Items are stored by the TEMPLATE PATH of their
// RawMaterialDescription, never by index: mods change the load order between sessions, and a path the game no
// longer has simply ranks nothing.
//
// Wire format v2 (little-endian; v1 is the same without kind 2 and still loads):
//   u32  categories       always 4: Fuel, Tool, Health, Clothing, in the finder's numbering 1..4
//   per category {
//     u32  count          0..kMaxItems
//     count x {
//       u8   kind         1 = a ranked item, best first (the only kind in v1); 2 = a Never item (v2)
//       u32  length       1..kMaxPathBytes
//       u8   path[length] the template path, bytes 0x20..0xFF, no NUL
//     }
//   }
// Decode accepts exactly what Encode writes and refuses the payload (the save is damaged) on a different
// category count, a count the payload cannot hold, an unknown kind (kind 2 in a v1 payload is unknown), an
// empty, over-long or control-character path, a path twice in one category (ranked and Never together;
// compared without case), bytes left over, or a cut-short payload. A refused payload changes nothing: the
// old lists stay.
//
// Standard C++ only: tests/unit/ext_item_priority_test.cpp links this file with item_priority.cpp and
// savetail.cpp.
// ============================================================================

#include "ext/item_priority/item_priority.h"

#include "ext/savetail.h"

#include <cctype>
#include <string>
#include <vector>

namespace Ext {
namespace ItemPriority {
namespace {

constexpr uint8_t kKindRankedItem = 1;
constexpr uint8_t kKindNeverItem = 2;
constexpr uint32_t kMaxItems = 4096;       // a list longer than any town's materials is damage, not a list
constexpr uint32_t kMaxPathBytes = 1024;
constexpr size_t kMinEntryBytes = 1 + 4 + 1;   // kind + length + a one-byte path

bool PathIsValid(const std::string& path) {
    if (path.empty() || path.size() > kMaxPathBytes) {
        return false;
    }
    for (char c : path) {
        if (static_cast<unsigned char>(c) < 0x20) {
            return false;
        }
    }
    return true;
}

std::string Fold(const std::string& s) {
    std::string out = s;
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

void Encode(std::vector<uint8_t>& payload) {
    const Lists& lists = Current();
    SaveTail::Writer w(payload);
    w.U32(static_cast<uint32_t>(kCategories));
    for (int category = 1; category <= kCategories; ++category) {
        const std::vector<std::string>& list = lists.Of(category);
        const std::vector<std::string>& never = lists.NeverOf(category);
        w.U32(static_cast<uint32_t>(list.size() + never.size()));
        for (const std::string& path : list) {
            w.Bytes(&kKindRankedItem, 1);
            w.String(path);
        }
        for (const std::string& path : never) {
            w.Bytes(&kKindNeverItem, 1);
            w.String(path);
        }
    }
}

bool Decode(const uint8_t* bytes, size_t size, int32_t savedVersion) {
    // Validate already refused a NEWER version; v1 (no Never kind) migrates unchanged.
    if (savedVersion != 1 && savedVersion != 2) {
        return false;
    }
    SaveTail::Reader r(bytes, size);
    uint32_t categories = 0;
    if (!r.U32(categories) || categories != static_cast<uint32_t>(kCategories)) {
        return false;
    }
    Lists lists;
    for (int category = 1; category <= kCategories; ++category) {
        uint32_t count = 0;
        if (!r.U32(count) || count > kMaxItems || count > r.left / kMinEntryBytes) {
            return false;
        }
        std::vector<std::string> folded;
        for (uint32_t i = 0; i < count; ++i) {
            const uint8_t* kind = nullptr;
            if (!r.Bytes(1, kind)) {
                return false;
            }
            const bool known = *kind == kKindRankedItem || (*kind == kKindNeverItem && savedVersion >= 2);
            if (!known) {
                return false;
            }
            std::string path;
            if (!r.String(path) || !PathIsValid(path)) {
                return false;
            }
            const std::string key = Fold(path);
            for (const std::string& seen : folded) {
                if (seen == key) {
                    return false;
                }
            }
            folded.push_back(key);
            if (*kind == kKindNeverItem) {
                lists.NeverOf(category).push_back(path);
            } else {
                lists.Of(category).push_back(path);
            }
        }
    }
    if (!r.AtEnd()) {
        return false;
    }
    Apply(lists);
    return true;
}

void Reset() {
    ResetState();
}

}  // namespace

const SaveTail::StateCodec& Codec() {
    static const SaveTail::StateCodec c = { "item_priority", Encode, Decode, Reset };
    return c;
}

}  // namespace ItemPriority
}  // namespace Ext
