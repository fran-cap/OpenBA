// ============================================================================
// src/ext/cancel_refund/cancel_refund.cpp -- fix.cancel_refund
// ============================================================================
// NOT RETAIL. docs/retail_bugs.md §26. The seam is DestroyComponent::ScatterContents
// (src/game/Component/destroycomponent_states.cpp); the ring walk and the pile size are
// cancel_refund_rule.h.
// ============================================================================
#include "ext/cancel_refund/cancel_refund.h"
#include "ext/cancel_refund/cancel_refund_rule.h"

#include "Game/GameUpdateLoop.h"
#include "Game/Map.h"
#include "Game/exe_constants.h"
#include "Entity/ComponentEntity.h"
#include "Entity/EntityList.h"
#include "Component/ComponentDescription.h"
#include "Component/RawMaterialComponent.h"
#include "object/Internal.h"
#include "system/MemoryAllocator.h"
#include "system/Value.h"   // System::Random

#include <cstdio>
#include <new>

namespace Ext {
namespace CancelRefund {

namespace {

// One stderr line per scatter that needed the row, for the first few only (AGENTS.md §3).
int s_logged = 0;
constexpr int kLogCap = 16;

// The cell FindPlacementCell picks for a drop started from the ring just outside the
// footprint, or -1. The site's own cells never qualify: its stamp holds them (a blocker
// on the non-'.' cells, a footprint count on the '.' cells).
int FindDropCell(Map* map, int x0, int y0, int w, int h) {
    int found = -1;
    ComponentEntity* allow = nullptr;   // DropOnGround's null allow slot
    ForEachRimCell(x0, y0, w, h, map->size, [&](int start) -> bool {
        found = map->FindPlacementCell(start, '\0', &allow, 0);   // 0x14009bd40
        return found >= 0;
    });
    return found;
}

}  // namespace

int PlaceOverflow(ComponentDescription* material, int weight, int remaining, int x0, int y0, int w, int h) {
    GameUpdateLoop* gw = GameUpdateLoop::_singleton;
    if (gw == nullptr || material == nullptr || remaining <= 0) return remaining;
    Map* map = gw->m_game;
    if (map == nullptr) return remaining;

    const int before = remaining;
    int piles = 0;
    while (remaining > 0) {
        const int cell = FindDropCell(map, x0, y0, w, h);
        if (cell < 0) break;
        const int count = PileSize(weight, remaining);

        // The pile as ScatterContents and DropOnGround build one: a copy of the material
        // template (0x1400c3720), positioned with a random quarter-turn yaw (0x140129760),
        // its amount set and its profession cleared, then registered (0x1400c48a0); its
        // MapComponent takes the cell, so the next search passes it by.
        void* mem = System::MemoryAllocator::MemAlloc(0x48, 8);
        if (mem == nullptr) break;
        ComponentEntity* pile = ::new (mem) ComponentEntity(material);
        Object::InternalPtr<ComponentEntity> hold(pile);
        RawMaterialComponent* raw = pile->FindComponent<RawMaterialComponent>();
        if (raw == nullptr) break;
        const int yaw = gw->m_random->RandTol(2);
        raw->PositionRawMaterial(cell, static_cast<float>(yaw) * kRdata_140172204);
        raw->quantity = count;
        raw->profession = nullptr;
        gw->m_entityList->RegisterEntity(&hold, static_cast<int>(material->_category));

        remaining -= count;
        ++piles;
    }

    if (s_logged < kLogCap) {
        ++s_logged;
        std::fprintf(stderr, "[CancelRefund] site %d,%d %dx%d: %d of %d units laid in %d piles outside the footprint\n",
                     x0, y0, w, h, before - remaining, before, piles);
    }
    return remaining;
}

}  // namespace CancelRefund
}  // namespace Ext
