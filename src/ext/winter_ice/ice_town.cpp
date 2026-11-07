// ============================================================================
// src/ext/winter_ice/ice_town.cpp -- the town's lake ice and ext.walk_on_ice's codec (ice_town.h)
// ============================================================================
// NOT RETAIL. docs/extensions/handins/WINTERICE.md section 5. Standard C++ only.
// ============================================================================
#include "ext/winter_ice/ice_town.h"

#include <cstdio>
#include <cstring>
#include <utility>

#include "ext/savetail.h"

struct MapCell;   // Game/Map.h: walk_type.h's g_base points at the map's cells

namespace Ext {
namespace WalkOnIce {   // walk_type.h (defined below)
extern const uint8_t* g_frozen;
extern const MapCell* g_base;
extern int g_count;
}  // namespace WalkOnIce
}  // namespace Ext

namespace Ext {
namespace WinterIce {

namespace {

constexpr uint32_t kFlagKnown = 1u;
constexpr uint32_t kFlagWalkable = 2u;

uint32_t Bits(float f) {
    uint32_t u = 0;
    std::memcpy(&u, &f, sizeof u);
    return u;
}

float FromBits(uint32_t u) {
    float f = 0.0f;
    std::memcpy(&f, &u, sizeof f);
    return f;
}

bool FrontOk(float f) { return f >= 0.0f && f <= kFreezeCap; }   // false for NaN

void EncodeWalk(std::vector<uint8_t>& payload) {
    Town().Encode(payload);
}

// The seam lets go of the town's walk set before the set is replaced or emptied: a load's own zone flood
// reads through walk_type.h, and the old buffer is about to be freed (winter_ice.cpp puts the new set on its
// map on the first frame).
void DropSeam() {
    WalkOnIce::g_frozen = nullptr;
    WalkOnIce::g_base = nullptr;
    WalkOnIce::g_count = 0;
}

bool DecodeWalk(const uint8_t* bytes, size_t size, int32_t savedVersion) {
    DropSeam();
    // Validate already refused a NEWER version; an older one would migrate here.
    if (savedVersion == kWalkWireVersion && Town().Decode(bytes, size)) {
        return true;
    }
    Town().Clear();
    std::fprintf(stderr, "[WalkOnIce] the saved ice (v%d, %zu bytes) is damaged: the town starts from a guess\n",
                 static_cast<int>(savedVersion), size);
    return true;
}

void ResetWalk() {
    DropSeam();
    Town().Clear();
}

}  // namespace

void IceTown::Encode(std::vector<uint8_t>& payload) const {
    SaveTail::Writer w(payload);
    const size_t cells = static_cast<size_t>(walkEdge > 0 ? walkEdge : 0) * static_cast<size_t>(walkEdge > 0 ? walkEdge : 0);
    const bool withSet = known && walkable && walkEdge > 0 && static_cast<uint32_t>(walkEdge) <= kMaxWalkEdge &&
                         walkSet.size() == cells;
    w.U32((known ? kFlagKnown : 0u) | (withSet ? kFlagWalkable : 0u));
    w.U32(Bits(ice.freeze));
    w.U32(Bits(ice.thaw));
    if (!withSet) return;
    std::vector<uint32_t> runs;   // start, length pairs
    for (size_t i = 0; i < cells;) {
        if (walkSet[i] == 0) {
            ++i;
            continue;
        }
        size_t j = i;
        while (j < cells && walkSet[j] != 0) ++j;
        runs.push_back(static_cast<uint32_t>(i));
        runs.push_back(static_cast<uint32_t>(j - i));
        i = j;
    }
    w.U32(static_cast<uint32_t>(walkEdge));
    w.U32(static_cast<uint32_t>(runs.size() / 2));
    for (const uint32_t v : runs) w.U32(v);
}

bool IceTown::Decode(const uint8_t* bytes, size_t size) {
    SaveTail::Reader r(bytes, size);
    uint32_t flags = 0;
    uint32_t freeze = 0;
    uint32_t thaw = 0;
    if (!r.U32(flags) || !r.U32(freeze) || !r.U32(thaw)) return false;
    if ((flags & ~(kFlagKnown | kFlagWalkable)) != 0) return false;
    IceTown t;
    t.ice.freeze = FromBits(freeze);
    t.ice.thaw = FromBits(thaw);
    if (!FrontOk(t.ice.freeze) || !FrontOk(t.ice.thaw)) return false;
    t.known = (flags & kFlagKnown) != 0;
    t.walkable = (flags & kFlagWalkable) != 0;
    if (t.walkable && !t.known) return false;
    if (t.walkable) {
        uint32_t edge = 0;
        uint32_t runCount = 0;
        if (!r.U32(edge) || edge == 0 || edge > kMaxWalkEdge || !r.U32(runCount)) return false;
        const uint64_t cells = static_cast<uint64_t>(edge) * edge;
        if (runCount > cells / 2 + 1 || static_cast<uint64_t>(runCount) * 8 > r.left) return false;
        t.walkEdge = static_cast<int32_t>(edge);
        t.walkSet.assign(static_cast<size_t>(cells), 0);
        uint64_t next = 0;   // the first index a run may start at (runs never touch)
        for (uint32_t k = 0; k < runCount; ++k) {
            uint32_t start = 0;
            uint32_t length = 0;
            if (!r.U32(start) || !r.U32(length)) return false;
            if (length == 0 || start < next || static_cast<uint64_t>(start) + length > cells) return false;
            for (uint32_t i = 0; i < length; ++i) t.walkSet[start + i] = 1;
            next = static_cast<uint64_t>(start) + length + 1;
        }
    }
    if (!r.AtEnd()) return false;
    t.adopt = t.known;
    *this = std::move(t);
    return true;
}

IceTown& Town() {
    static IceTown town;
    return town;
}

}  // namespace WinterIce

// walk_type.h's seam state, defined here so every build that links the walk readers links it too.
namespace WalkOnIce {
const uint8_t* g_frozen = nullptr;
const MapCell* g_base = nullptr;
int g_count = 0;
const float* g_surface = nullptr;
const MapCell* g_surfaceBase = nullptr;
int g_surfaceCount = 0;
float g_surfaceThaw = 0.0f;
}  // namespace WalkOnIce

namespace WinterIce {

const SaveTail::StateCodec& WalkCodec() {
    static const SaveTail::StateCodec c = { kWalkId, EncodeWalk, DecodeWalk, ResetWalk };
    return c;
}

}  // namespace WinterIce
}  // namespace Ext
