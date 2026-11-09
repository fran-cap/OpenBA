// ============================================================================
// src/ext/item_limits/save_state.cpp -- ext.item_limits' per-town state in the save tail (class C)
// ============================================================================
// NOT RETAIL. docs/extensions/handins/ITEMLIMITS.md, DESIGN.md "Class C state: the contract".
//
// The state is the town's list of item limits (item_limits.h). Items are stored by the TEMPLATE PATH of
// their RawMaterialDescription, never by index: mods change the load order between sessions, and a path the
// game no longer has simply limits nothing.
//
// Wire format v1 (little-endian):
//   u32  count            0..kMaxItems
//   count x {
//     u32  length         1..kMaxPathBytes
//     u8   path[length]   the template path, bytes 0x20..0xFF, no NUL
//     i32  limit          0..kMaxLimit
//   }
// Decode accepts exactly what Encode writes and refuses the payload (the save is damaged) on a count the
// payload cannot hold, an empty, over-long or control-character path, a path twice (compared without case),
// a limit out of range, bytes left over, or a cut-short payload. A refused payload changes nothing: the old
// list stays.
//
// Standard C++ only: tests/unit/ext_item_limits_test.cpp links this file with item_limits.cpp and
// savetail.cpp.
// ============================================================================

#include "ext/item_limits/item_limits.h"

#include "ext/savetail.h"

#include <cctype>
#include <string>
#include <vector>

namespace Ext {
namespace ItemLimits {
namespace {

constexpr uint32_t kMaxItems = 4096;   // a list longer than any town's materials is damage, not a list
constexpr uint32_t kMaxPathBytes = 1024;
constexpr size_t kMinEntryBytes = 4 + 1 + 4;   // length + a one-byte path + limit

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
    const std::vector<Limit>& limits = Limits();
    SaveTail::Writer w(payload);
    w.U32(static_cast<uint32_t>(limits.size()));
    for (const Limit& l : limits) {
        w.String(l.path);
        w.I32(l.limit);
    }
}

bool Decode(const uint8_t* bytes, size_t size, int32_t savedVersion) {
    if (savedVersion != 1) {   // Validate already refused a NEWER version
        return false;
    }
    SaveTail::Reader r(bytes, size);
    uint32_t count = 0;
    if (!r.U32(count) || count > kMaxItems || count > r.left / kMinEntryBytes) {
        return false;
    }
    std::vector<Limit> limits;
    std::vector<std::string> folded;
    for (uint32_t i = 0; i < count; ++i) {
        Limit l;
        if (!r.String(l.path) || !PathIsValid(l.path)) {
            return false;
        }
        if (!r.I32(l.limit) || l.limit < 0 || l.limit > kMaxLimit) {
            return false;
        }
        const std::string key = Fold(l.path);
        for (const std::string& seen : folded) {
            if (seen == key) {
                return false;
            }
        }
        folded.push_back(key);
        limits.push_back(l);
    }
    if (!r.AtEnd()) {
        return false;
    }
    ReplaceLimits(limits);
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

}  // namespace ItemLimits
}  // namespace Ext
