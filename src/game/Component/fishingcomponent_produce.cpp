// src/game/Component/fishingcomponent_produce.cpp
// FishingComponent vtable slots 19/20/21 — the production hooks of the shared
// ResourceProduceComponent state machine. Fishing is the only subclass in the food
// wave that overrides all three; Hunt and Herbalist leave the base no-ops in place.
//
//   slot 19 (+0x98) OnEnterProducing  retail 0x14011ee80
//   slot 20 (+0xa0) OnExitProducing   retail 0x14011ee50
//   slot 21 (+0xa8) ProduceTick       retail 0x14011ec10
//
// +0x50 (`fishingFlag`) is the registered latch: enter stamps the dock's radius into
// the map coverage grid with +1 and sets it, exit stamps -1 and clears it. Both are
// guarded on the latch so a double enter/exit cannot drift the shared grid counter —
// that guard is retail's, not ours.

#include "object/TypeInfo.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "Component/FishingComponent.h"
#include "Component/RadiusDescription.h"    // ENDGAME B.3.9 C1: 0x1401c4870 cell class
#include "Component/FishingDescription.h"   // ENDGAME B.3.9 C1: 0x1401c7698 cell class
#include "Component/exe_bodies.h"
#include "Component/ComponentDescription.h"
#include "Component/MapComponent.h"
#include "Entity/ComponentEntity.h"
#include "Game/Map.h"

namespace {

// 0x14009f990 Map::StampInfluence(state, cx, cy, r, delta) — ported
// (happinesscomponent.cpp), called directly. Adds `delta` to the byte at
// cell+0x1c+state over a disk of radius r, then to the per-state total
// Map::influenceTotals[state]. The fishing dock uses state 8.

constexpr int kFishingCoverageState = 8;

// Walk the owning entity's description (its sub-descriptions) for the
// RadiusDescription and return its radius. Retail inlines this scan in both eed0 and
// the tick; it breaks at the FIRST slot whose GetTypeId matches, then re-validates with
// IsDerivedFrom — so a host without a RadiusDescription yields a null and, in retail,
// an unguarded deref. We return -1 instead ONLY because the standalone can legitimately
// build a dock whose description set is still partly inert; see the callers.
int RadiusOf(const FishingComponent* comp) {
    const ComponentEntity* entity = comp->_entity.Get();
    if (!entity) return -1;
    const ComponentDescription* list = entity->_description;
    if (!list) return -1;
    Description* const* items = list->_subDescs;
    if (!items) return -1;
    for (int i = 0; i < list->_subDescCount; ++i) {
        Description* obj = items[i];
        if (!obj) continue;
        if (::Object::IsDerivedFromNullSafe(obj, RadiusDescription::TypeId()))
            return static_cast<RadiusDescription*>(obj)->_radius;
    }
    return -1;
}

// Entity tile centre, as both retail bodies compute it from the MapComponent:
//   cx = (w >> 1) + x ,  cy = (h >> 1) + y
// (tileX0/tileY0 are the footprint origin, tileX1/tileY1 its extent, all read as int16.)
bool TileCentre(const FishingComponent* comp, int* cx, int* cy) {
    ComponentEntity* entity = comp->_entity.Get();
    if (!entity) return false;
    MapComponent* mc = entity->FindComponent<MapComponent>();   // 0x140004680
    if (!mc) return false;
    *cx = (static_cast<int16_t>(mc->tileX1) >> 1) + static_cast<int16_t>(mc->tileX0);
    *cy = (static_cast<int16_t>(mc->tileY1) >> 1) + static_cast<int16_t>(mc->tileY0);
    return true;
}

// GameUpdateLoop::m_game (+0xf8).
Map* TheMap() {
    return GameUpdateLoop::_singleton->m_game;
}

// retail 0x14011eed0 — FishingComponent_helper_eed0(self, delta)
void StampCoverage(const FishingComponent* comp, short delta) {
    const int r = RadiusOf(comp);
    int cx = 0, cy = 0;
    if (r < 0 || !TileCentre(comp, &cx, &cy) ||
        GameUpdateLoop::_singleton == nullptr) {
        return;
    }
    Map* map = TheMap();
    if (!map) return;
    map->StampInfluence(kFishingCoverageState, cx, cy, r, delta);   // 0x14009f990
}

}  // namespace

// [19] 0x14011ee80
// ── slot 19 (+0x98) OnEnterProducing — retail 0x14011ee80 ─────────────────────────
char FishingComponent::OnEnterProducing() {
    if (fishingFlag) return 0;   // already registered
    StampCoverage(this, +1);
    FishingComponent::ProduceTick(nullptr);                        // retail: Time(0.0); qualified, as the free call was
    fishingFlag = true;
    return 0;
}
// [20] 0x14011ee50
// ── slot 20 (+0xa0) OnExitProducing — retail 0x14011ee50 ──────────────────────────
char FishingComponent::OnExitProducing() {
    if (!fishingFlag) return 0;   // not registered
    StampCoverage(this, -1);
    fishingFlag = false;
    return 0;
}
// [21] 0x14011ec10
// ── slot 21 (+0xa8) ProduceTick — retail 0x14011ec10 ──────────────────────────────
// Recomputes the dock's fishing rate: over the disk of the RadiusDescription radius,
// count in-bounds tiles (total) and those whose terrain byte (cell+0x29) is set in the
// FishingDescription's accepted-terrain bitmask (_terrainMask), then
//     fishingRate = matching / (total * 0.5)          [DAT_140172114 = 0.5f]
// A dock with no water in range therefore rates 0 and produces nothing — which is the
// retail behaviour players see when a dock is placed too far up the bank.
char FishingComponent::ProduceTick(const System::Time* /*time*/) {
    const int r = RadiusOf(this);
    int cx = 0, cy = 0;
    if (r < 0 || !TileCentre(this, &cx, &cy) ||
        GameUpdateLoop::_singleton == nullptr)
        return 0;

    const Map* map = TheMap();
    if (!map) return 0;
    const int width = map->size;
    const MapCell* grid = map->cells.GetData();
    if (!grid || width <= 0) return 0;

    // The accepted-terrain mask lives on the component's description, but only if it
    // really is a FishingDescription — retail runs the same IsDerivedFrom filter and
    // would read a null otherwise.
    Description* descObj = _description;
    uint32_t mask = 0;
    if (descObj && ::Object::IsDerivedFromNullSafe(descObj, FishingDescription::TypeId()))
        mask = static_cast<FishingDescription*>(descObj)->_terrainMask;

    int total = 0, matching = 0;
    for (int dy = -r; dy < r; ++dy) {
        for (int dx = -r; dx < r; ++dx) {
            if (dx * dx + dy * dy > r * r) continue;
            const int x = cx + dx, y = cy + dy;
            if (x < 0 || y < 0 || x >= width || y >= width) continue;
            ++total;
            const uint8_t terrain = static_cast<uint8_t>(grid[y * width + x].terrainType);   // +0x29
            if ((mask >> (terrain & 0x1f)) & 1) ++matching;
        }
    }

    const float rate = (total > 0) ? (float)matching / ((float)total * 0.5f) : 0.0f;
    fishingRate = rate;

    return 0;
}
