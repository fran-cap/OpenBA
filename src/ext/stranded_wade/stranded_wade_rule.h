// ============================================================================
// src/ext/stranded_wade/stranded_wade_rule.h -- fix.stranded_wade: the pure rule
// ============================================================================
// NOT RETAIL. docs/retail_bugs.md §19. Standard C++ only, so
// tests/unit/ext_stranded_wade_test.cpp links it without the world.
//
// Retail's walk test (Map::RequestPath 0x14009cfb0) lets a walk with a water or mountain endpoint
// cross water and mountains anywhere and end in any land zone, then refuses every land-to-land walk
// between two different zones ({-1, 0}; MoveTo::Execute 0x1400c8570 answers 2). A citizen who ends
// up on land cut off from his home asks for the same refused walk until he starves or freezes.
//
// Ruled definition (the user, 2026-11-01): a citizen is stranded when his last TWO walk requests
// were refused on a land-to-land zone mismatch. Any other failure, and a walk with a zone-less
// endpoint (types 5-8), does not count; one normal success resets the count. Where he lives (across
// water by the player's choice, say) is never looked at.
//
// Rescue: one walk with the pass flags 0xe1f | 0x1e0, so RequestPath takes retail's own bypass
// (wade / climb), to his home; if he has none, or the wading walk home failed, to the town hall.
// No rescue when that goal is land of his own zone (NeedsRescue): the refused walks went to targets
// cut off from him and his home alike, so he is not the one who is stranded.
//
// The state is one byte per character (CharacterComponent padding +0x44, never saved: a load
// restarts the count at 0).
// ============================================================================
#pragma once

#include <cstdint>

namespace Ext {
namespace StrandedWade {

constexpr uint32_t kLandTypes = 0xe1fu;   // Map::FillZone 0x14009c170: types that get a zone
constexpr uint32_t kZonelessTypes = 0x1e0u;   // types 5-8: the bypass in RequestPath
constexpr uint32_t kRescueFlags = kLandTypes | kZonelessTypes;   // 0xfff

// The state byte.
constexpr uint8_t kCountMask = 0x03;   // consecutive zone refusals, 0..2
constexpr uint8_t kPending = 0x04;     // a rescue walk is out; its outcome is the next one
constexpr uint8_t kToHall = 0x08;      // the rescue walk out goes to the town hall
constexpr uint8_t kHomeFailed = 0x10;  // the last rescue walk home failed: the next goes to the hall

constexpr uint8_t kStrandedAt = 2;

enum class Outcome : uint8_t {
    Arrived,       // the walk ended on its destination cell
    ZoneRefused,   // RequestPath refused it on a land-to-land zone mismatch
    OtherFailed,   // any other failure (an empty path, a refusal for another reason)
};

// Why RequestPath refused a walk: `flags` its pass flags, (typeA, zoneA) / (typeB, zoneB) the two
// endpoint cells' terrain type and zone. A zone mismatch only when no endpoint (and no pass flag)
// is zone-less and both cells carry a zone.
inline Outcome ClassifyRefusal(uint32_t flags, uint8_t typeA, uint16_t zoneA, uint8_t typeB, uint16_t zoneB) {
    const uint32_t bitA = 1u << (typeA & 0x1f);
    const uint32_t bitB = 1u << (typeB & 0x1f);
    const uint32_t mask = flags | bitA | bitB;
    if ((mask & kZonelessTypes) != 0) return Outcome::OtherFailed;
    if ((bitA & kLandTypes) == 0 || (bitB & kLandTypes) == 0) return Outcome::OtherFailed;
    if (zoneA == 0 || zoneB == 0) return Outcome::OtherFailed;
    if (zoneA == zoneB) return Outcome::OtherFailed;
    return Outcome::ZoneRefused;
}

inline uint8_t Count(uint8_t state) {
    return state & kCountMask;
}

inline bool IsStranded(uint8_t state) {
    return Count(state) >= kStrandedAt;
}

// The state after a walk's outcome. `rescueFlags`: the walk carried kRescueFlags. A rescue walk's
// outcome is the first outcome after it was pushed with those flags; any outcome clears kPending.
inline uint8_t OnOutcome(uint8_t state, Outcome outcome, bool rescueFlags) {
    const bool rescue = (state & kPending) != 0 && rescueFlags;
    const bool toHall = (state & kToHall) != 0;
    uint8_t next = static_cast<uint8_t>(state & ~(kPending | kToHall | kCountMask));
    if (rescue) {
        if (outcome == Outcome::Arrived || toHall) return static_cast<uint8_t>(next & ~kHomeFailed);
        return static_cast<uint8_t>(next | kHomeFailed);
    }
    if (outcome == Outcome::ZoneRefused) {
        uint8_t count = static_cast<uint8_t>(Count(state) + 1);
        if (count > kStrandedAt) count = kStrandedAt;
        return static_cast<uint8_t>(next | count);
    }
    return next;
}

enum class Target : uint8_t {
    Home,
    Hall,
};

// Where the rescue walk goes: home when he has one and the last wading walk home did not fail.
inline Target RescueTarget(uint8_t state, bool hasHome) {
    if (hasHome && (state & kHomeFailed) == 0) return Target::Home;
    return Target::Hall;
}

// Whether the rescue walk to the goal is needed at all: false when the citizen's cell and the goal's
// cell are land of the same zone, so an ordinary walk reaches the goal. Then the refusals that made
// him "stranded" were walks to targets cut off from HIM and his home alike (retail pushes such walks:
// a gatherer's target across a river, a walk toward a family member who is cut off), and a wading
// walk home would only interrupt him.
inline bool NeedsRescue(uint8_t typeHere, uint16_t zoneHere, uint8_t typeGoal, uint16_t zoneGoal) {
    const uint32_t bitHere = 1u << (typeHere & 0x1f);
    const uint32_t bitGoal = 1u << (typeGoal & 0x1f);
    if ((bitHere & kLandTypes) == 0 || (bitGoal & kLandTypes) == 0) return true;
    if (zoneHere == 0 || zoneGoal == 0) return true;
    return zoneHere != zoneGoal;
}

// The state once the rescue walk to `target` was pushed: the count restarts.
inline uint8_t OnRescuePushed(uint8_t state, Target target) {
    uint8_t next = static_cast<uint8_t>((state & kHomeFailed) | kPending);
    if (target == Target::Hall) next = static_cast<uint8_t>(next | kToHall);
    return next;
}

// The state when no rescue is pushed (no home that a wading walk reached and no town hall, or the
// goal is in his own zone): the count restarts so the AI is not asked again at every pick.
inline uint8_t OnNoTarget(uint8_t state) {
    return static_cast<uint8_t>(state & ~(kCountMask | kHomeFailed));
}

}  // namespace StrandedWade
}  // namespace Ext
