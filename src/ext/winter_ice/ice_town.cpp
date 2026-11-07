// ============================================================================
// src/ext/winter_ice/ice_town.cpp -- the town's lake ice and ext.walk_on_ice's codec (ice_town.h)
// ============================================================================
// NOT RETAIL. docs/extensions/handins/WINTERICE.md section 5. Standard C++ only.
// ============================================================================
#include "ext/winter_ice/ice_town.h"

#include <cstdio>
#include <cstring>

#include "ext/savetail.h"

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

bool DecodeWalk(const uint8_t* bytes, size_t size, int32_t savedVersion) {
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
    Town().Clear();
}

}  // namespace

void IceTown::Encode(std::vector<uint8_t>& payload) const {
    SaveTail::Writer w(payload);
    w.U32((known ? kFlagKnown : 0u) | (walkable ? kFlagWalkable : 0u));
    w.U32(Bits(ice.freeze));
    w.U32(Bits(ice.thaw));
}

bool IceTown::Decode(const uint8_t* bytes, size_t size) {
    SaveTail::Reader r(bytes, size);
    uint32_t flags = 0;
    uint32_t freeze = 0;
    uint32_t thaw = 0;
    if (!r.U32(flags) || !r.U32(freeze) || !r.U32(thaw) || !r.AtEnd()) return false;
    if ((flags & ~(kFlagKnown | kFlagWalkable)) != 0) return false;
    IceTown t;
    t.ice.freeze = FromBits(freeze);
    t.ice.thaw = FromBits(thaw);
    if (!FrontOk(t.ice.freeze) || !FrontOk(t.ice.thaw)) return false;
    t.known = (flags & kFlagKnown) != 0;
    t.walkable = t.known && (flags & kFlagWalkable) != 0;
    t.adopt = t.known;
    *this = t;
    return true;
}

IceTown& Town() {
    static IceTown town;
    return town;
}

const SaveTail::StateCodec& WalkCodec() {
    static const SaveTail::StateCodec c = { kWalkId, EncodeWalk, DecodeWalk, ResetWalk };
    return c;
}

}  // namespace WinterIce
}  // namespace Ext
