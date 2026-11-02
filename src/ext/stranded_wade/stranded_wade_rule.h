// ============================================================================
// src/ext/stranded_wade/stranded_wade_rule.h -- fix.stranded_wade: the pure rule
// ============================================================================
// NOT RETAIL. docs/retail_bugs.md §19. Standard C++ only, so
// tests/unit/ext_stranded_wade_test.cpp links it without the world.
//
// Retail's walk test (Map::RequestPath 0x14009cfb0) lets a walk with a water or mountain endpoint
// cross water and mountains anywhere and end in any land zone, then refuses every land-to-land walk
// between two different zones ({-1, 0}; MoveTo::Execute 0x1400c8570 answers 2). A citizen who ends
// up on land cut off from his home asks for refused walks home until he starves or freezes.
//
// Ruled trigger (the user, 2026-11-01, replacing the "two refusals in a row" counter). A citizen is
// STRANDED when both hold:
//   1. he stands on a land cell (zoned, not types 5-8) whose zone differs from his ANCHOR zone;
//   2. since he entered his current zone, at least one of his walks to a target in the anchor zone was
//      refused on the land-to-land zone mismatch.
// The state clears when he stands in the anchor zone again. A walk that succeeds inside his current
// zone does not reset it, nor does a refusal toward some other zone. So a citizen whose home is in his
// own zone (one living across water by the player's choice, a town citizen whose work target is across
// a river) is never rescued.
//
// The anchor (ruled, ChooseAnchor) is the first of these that exists: (1) his home; (2) the home he
// just lost: retail's CitizenComponent::Update drops an unreachable home at once (SetHome(nullptr)),
// so a stranded citizen is homeless almost at once; the lost home is remembered from that call until
// he stands in its zone again or gets a new home; (3) the nearest town hall; (4) the nearest storage.
// The rescue walk goes to the anchor: the home, the lost home if it still stands (else the nearest
// storage, else any building, in its zone), the hall or the storage. If that walk fails, the next one
// goes to the fallback (the nearest town hall, else storage) when it lies in another zone than his.
//
// Rescue: one walk with the pass flags 0xe1f | 0x1e0, so RequestPath takes retail's own bypass
// (wade / climb).
//
// Storage, never saved (a load starts every citizen unstranded): CharacterComponent padding +0x44,
// byte 0 the flags below, bytes 2..3 the zone he stood in when the refusal toward the anchor was
// recorded ("since he entered": a record from another zone is stale); CitizenComponent padding
// +0x244, the lost home's cell + 1 (0: none).
// ============================================================================
#pragma once

#include <cstdint>

namespace Ext {
namespace StrandedWade {

constexpr uint32_t kLandTypes = 0xe1fu;   // Map::FillZone 0x14009c170: types that get a zone
constexpr uint32_t kZonelessTypes = 0x1e0u;   // types 5-8: the bypass in RequestPath
constexpr uint32_t kRescueFlags = kLandTypes | kZonelessTypes;   // 0xfff

// The flag byte.
constexpr uint8_t kRefused = 0x01;     // a walk toward the anchor zone was refused from `zone`
constexpr uint8_t kPending = 0x04;     // a rescue walk is out; its outcome is the next one
constexpr uint8_t kToFallback = 0x08;    // the rescue walk out goes to the fallback (hall, else storage)
constexpr uint8_t kAnchorFailed = 0x10;  // the last rescue walk to the anchor failed: the next goes to the fallback

// A land cell with a zone (FillZone gives zones to the land types only).
inline bool IsZonedLand(uint8_t type, uint16_t zone) {
    return ((1u << (type & 0x1f)) & kLandTypes) != 0 && zone != 0;
}

// The anchor's kind, the first that exists (ruled order).
enum class AnchorKind : uint8_t {
    None,
    Home,       // his home
    LostHome,   // the home SetHome(nullptr) just took from him (remembered)
    Hall,       // the nearest town hall
    Storage,    // the nearest storage building
};

inline AnchorKind ChooseAnchor(bool hasHome, bool hasLostHome, bool hasHall, bool hasStorage) {
    if (hasHome) return AnchorKind::Home;
    if (hasLostHome) return AnchorKind::LostHome;
    if (hasHall) return AnchorKind::Hall;
    if (hasStorage) return AnchorKind::Storage;
    return AnchorKind::None;
}

// The remembered lost home (its cell + 1, 0: none) after a home change: a new home forgets it, losing
// a home remembers that home's cell.
inline uint32_t OnHomeChange(uint32_t lost, bool hadHome, int oldHomeCell, bool hasNewHome) {
    if (hasNewHome) return 0;
    if (hadHome && oldHomeCell >= 0) return static_cast<uint32_t>(oldHomeCell) + 1;
    return lost;
}

// The remembered lost home is kept until he stands on land of its zone again.
inline uint32_t SettleLostHome(uint32_t lost, uint8_t typeHere, uint16_t zoneHere, uint16_t lostZone) {
    if (lost != 0 && IsZonedLand(typeHere, zoneHere) && zoneHere == lostZone) return 0;
    return lost;
}

struct State {
    uint8_t flags = 0;
    uint16_t zone = 0;   // where he stood when kRefused was recorded
};

// Whether RequestPath refused a walk on the land-to-land zone mismatch: `flags` its pass flags,
// (typeA, zoneA) / (typeB, zoneB) the two endpoint cells. Not when an endpoint or a pass flag is
// zone-less (types 5-8: RequestPath bypasses the zone test) or a cell has no zone.
inline bool IsZoneRefusal(uint32_t flags, uint8_t typeA, uint16_t zoneA, uint8_t typeB, uint16_t zoneB) {
    const uint32_t mask = flags | (1u << (typeA & 0x1f)) | (1u << (typeB & 0x1f));
    if ((mask & kZonelessTypes) != 0) return false;
    if (!IsZonedLand(typeA, zoneA) || !IsZonedLand(typeB, zoneB)) return false;
    return zoneA != zoneB;
}

// A refused walk from (typeHere, zoneHere) to (typeDest, zoneDest), `anchorZone` the citizen's anchor
// zone (0: none). Only a zone refusal toward the anchor zone, from outside it, is recorded.
inline State OnRefusal(State s, uint32_t flags, uint8_t typeHere, uint16_t zoneHere, uint8_t typeDest,
                       uint16_t zoneDest, uint16_t anchorZone) {
    if (anchorZone == 0) return s;
    if (!IsZoneRefusal(flags, typeHere, zoneHere, typeDest, zoneDest)) return s;
    if (zoneDest != anchorZone || zoneHere == anchorZone) return s;
    s.flags = static_cast<uint8_t>(s.flags | kRefused);
    s.zone = zoneHere;
    return s;
}

// Condition 1 and 2 at (typeHere, zoneHere).
inline bool IsStranded(State s, uint8_t typeHere, uint16_t zoneHere, uint16_t anchorZone) {
    if (anchorZone == 0 || !IsZonedLand(typeHere, zoneHere)) return false;
    if (zoneHere == anchorZone) return false;
    return (s.flags & kRefused) != 0 && s.zone == zoneHere;
}

// The record clears when he stands in the anchor zone again, and goes stale when he stands on land
// of another zone than the one it was made in (he left that zone).
inline State Settle(State s, uint8_t typeHere, uint16_t zoneHere, uint16_t anchorZone) {
    if ((s.flags & kRefused) == 0 || !IsZonedLand(typeHere, zoneHere)) return s;
    if (zoneHere == anchorZone || zoneHere != s.zone) {
        s.flags = static_cast<uint8_t>(s.flags & ~kRefused);
        s.zone = 0;
    }
    return s;
}

// A walk's end that is not a refusal: `arrived` (on its destination cell) or failed (an empty path).
// `rescueFlags`: the walk carried kRescueFlags. A rescue walk's outcome is the first one after it was
// pushed with those flags; any outcome clears kPending. A failed rescue to the anchor sends the next
// one to the fallback; a failed rescue to the fallback drops the record (the next rescue waits for a
// new refusal and tries the anchor again).
inline State OnWalkEnd(State s, bool arrived, bool rescueFlags) {
    const bool rescue = (s.flags & kPending) != 0 && rescueFlags;
    const bool toFallback = (s.flags & kToFallback) != 0;
    s.flags = static_cast<uint8_t>(s.flags & ~(kPending | kToFallback));
    if (!rescue) return s;
    if (arrived) {
        s.flags = static_cast<uint8_t>(s.flags & ~kAnchorFailed);
    } else if (toFallback) {
        s.flags = static_cast<uint8_t>(s.flags & ~(kAnchorFailed | kRefused));
        s.zone = 0;
    } else {
        s.flags = static_cast<uint8_t>(s.flags | kAnchorFailed);
    }
    return s;
}

enum class Target : uint8_t {
    Anchor,
    Fallback,
};

// Where the rescue walk goes: the anchor's destination, unless the last wading walk there failed.
inline Target RescueTarget(State s) {
    if ((s.flags & kAnchorFailed) == 0) return Target::Anchor;
    return Target::Fallback;
}

// Whether a walk to the goal needs the bypass: false when he and the goal stand on land of one zone
// (an ordinary walk gets there). Used for the fallback after a failed rescue walk.
inline bool NeedsRescue(uint8_t typeHere, uint16_t zoneHere, uint8_t typeGoal, uint16_t zoneGoal) {
    if (!IsZonedLand(typeHere, zoneHere) || !IsZonedLand(typeGoal, zoneGoal)) return true;
    return zoneHere != zoneGoal;
}

// The state once the rescue walk to `target` was pushed.
inline State OnRescuePushed(State s, Target target) {
    s.flags = static_cast<uint8_t>(s.flags | kPending);
    if (target == Target::Fallback) s.flags = static_cast<uint8_t>(s.flags | kToFallback);
    return s;
}

// The state when no rescue can be pushed (no goal, or the goal is in his own zone): the record is
// dropped so the AI is not asked again at every pick.
inline State OnNoTarget(State s) {
    s.flags = static_cast<uint8_t>(s.flags & ~(kRefused | kAnchorFailed));
    s.zone = 0;
    return s;
}

}  // namespace StrandedWade
}  // namespace Ext
