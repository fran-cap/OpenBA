// ============================================================================
// src/ext/restricted_foods/save_state.cpp -- ext.restricted_foods's per-town state in the save tail (class C)
// ============================================================================
// NOT RETAIL. docs/extensions/handins/RESTRICTEDFOODS.md, DESIGN.md "Class C state: the contract".
//
// The state is the list of blocked foods (restricted_foods.h), each by the template path of its
// RawMaterialDescription, never by index: mods change the load order between sessions, and a path the game no
// longer has simply matches nothing.
//
// Wire format v1 (little-endian):
//   u32  count       0..kMaxItems
//   count x {
//     u8   kind        1 = a blocked food (the only kind in v1)
//     u32  length      1..kMaxPathBytes
//     u8   path[length]  the template path, bytes 0x20..0xFF, no NUL
//   }
// Decode accepts exactly what Encode writes and refuses the payload (the save is damaged) on a cut-short
// or over-long payload, a list longer than kMaxItems, an unknown kind, an empty, over-long or
// control-character path, or a path listed twice (compared without case). A refused payload changes
// nothing: the old list stays.
//
// Standard C++ only: tests/unit/ext_restricted_foods_test.cpp links this file
// with restricted_foods.cpp and savetail.cpp.
// ============================================================================

#include "ext/restricted_foods/restricted_foods.h"

#include "ext/savetail.h"

#include <cctype>
#include <string>
#include <vector>

namespace Ext {
namespace RestrictedFoods {
namespace {

constexpr uint8_t  kKindBlockedFood = 1;
constexpr uint32_t kMaxPathBytes    = 1024;
constexpr size_t   kMinEntryBytes   = 1 + 4 + 1;   // kind + length + a one-byte path
// A longer list than any town's materials is damage (as item_priority's). The cap also bounds the duplicate
// check below, which compares each path with every earlier one (review F-243, F-248).
constexpr uint32_t kMaxItems        = 4096;

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
    const std::vector<std::string>& paths = BlockedPaths();
    SaveTail::Writer w(payload);
    w.U32(static_cast<uint32_t>(paths.size()));
    for (const std::string& path : paths) {
        w.Bytes(&kKindBlockedFood, 1);
        w.String(path);
    }
}

bool Decode(const uint8_t* bytes, size_t size, int32_t savedVersion) {
    // Validate already refused a NEWER version; an older one would migrate here.
    if (savedVersion != 1) {
        return false;
    }
    SaveTail::Reader r(bytes, size);
    uint32_t count = 0;
    if (!r.U32(count)) {
        return false;
    }
    if (count > kMaxItems || count > r.left / kMinEntryBytes) {
        return false;
    }
    std::vector<std::string> paths;
    std::vector<std::string> folded;
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t* kind = nullptr;
        if (!r.Bytes(1, kind) || *kind != kKindBlockedFood) {
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
        paths.push_back(path);
    }
    if (!r.AtEnd()) {
        return false;
    }
    ReplaceBlockedPaths(paths);
    return true;
}

void Reset() {
    ResetState();
}

}  // namespace

const SaveTail::StateCodec& Codec() {
    static const SaveTail::StateCodec c = { "restricted_foods", Encode, Decode, Reset };
    return c;
}

}  // namespace RestrictedFoods
}  // namespace Ext
