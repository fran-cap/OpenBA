// ============================================================================
// src/ext/foundation_piles/foundation_piles.cpp -- fix.foundation_piles
// ============================================================================
// NOT RETAIL. docs/extensions/handins/FOUNDATIONPILES.md. The seam is ClearArea::Execute's
// phase 3 (src/game/Work/cleararea.cpp); the cell choice is foundation_piles_rule.h.
// ============================================================================
#include "ext/foundation_piles/foundation_piles.h"
#include "ext/foundation_piles/foundation_piles_rule.h"

#include "Game/GameUpdateLoop.h"
#include "Game/Map.h"
#include "Game/WorkScheduler.h"
#include "Game/WorkSchedulerNodes.h"
#include "Game/exe_constants.h"
#include "Entity/ComponentEntity.h"
#include "Entity/CitizenBehavior.h"
#include "Component/CitizenComponent.h"
#include "Component/CharacterComponent.h"
#include "Component/ClearAreaComponent.h"
#include "Component/ComponentDescription.h"
#include "Component/MapComponent.h"
#include "Component/ModelComponent.h"
#include "Component/NaturalResourceComponent.h"
#include "Component/RawMaterialComponent.h"
#include "Component/RawMaterialDescription.h"
#include "Component/StorageComponent.h"
#include "Component/WorkComponent.h"
#include "Action/Action.h"
#include "Action/DropOffRawMaterial.h"
#include "Action/MoveTo.h"
#include "Work/GridTile.h"
#include "Work/Pickup.h"
#include "Work/exe_bodies.h"   // WorkAct_PerformWorkCtor9_c0ad0 0x1400c0ad0
#include "object/TypeInfo.h"
#include "system/MemoryAllocator.h"

#include <cstdint>
#include <cstdio>
#include <new>

namespace Ext {
namespace FoundationPiles {

namespace {

// The scheduler's Pickup work for a pile's work id: the trees[0] descent Pickup::Execute
// inlines (Pickup_FindTypeWork, 0x1400f33b0).
Pickup* FindPickupWork(WorkScheduler* scheduler, int workId) {
    const WorkSchedulerIdNode* node = scheduler->trees[0].root;
    while (node != nullptr) {
        if (workId < node->key) {
            node = node->left;
        } else if (workId > node->key) {
            node = node->right;
        } else {
            Work* work = node->work.Get();
            return Object::IsDerivedFromNullSafe(work, Pickup::TypeId()) ? static_cast<Pickup*>(work) : nullptr;
        }
    }
    return nullptr;
}

// A cell's centre on the terrain, as Pickup::Execute turns its access cell into the
// haul chain's destination (off = (size >> 1) - 0.5, then Map::GetTerrainPos).
void CellWorldPos(Map* map, int cell, float out[4]) {
    const int size = map->size;
    const float off = static_cast<float>(size >> 1) - kRdata_140172114;
    float xy[3] = {static_cast<float>(cell % size) - off, static_cast<float>(cell / size) - off, 0.0f};
    out[0] = 0.0f;
    out[1] = 0.0f;
    out[2] = 0.0f;
    out[3] = 0.0f;
    map->GetTerrainPos(out, xy, 0, nullptr);
}

int CitizenCell(Map* map, CitizenComponent* citizen) {
    const Math::Vector3& pos = citizen->character->modelComponent->transform.translation;
    const int size = map->size;
    int x = static_cast<int>(pos.x + static_cast<float>(size >> 1));
    int y = static_cast<int>(pos.y + static_cast<float>(size >> 1));
    if (x < 0) x = 0;
    if (x >= size) x = size - 1;
    if (y < 0) y = 0;
    if (y >= size) y = size - 1;
    return y * size + x;
}

}  // namespace

bool WaitsOnPilesOnly(ComponentEntity* site, const GridTile* tiles, int count) {
    if (site == nullptr || site->FindComponent<ClearAreaComponent>() == nullptr) return false;
    for (int i = 0; i < count; ++i) {
        ComponentEntity* e = tiles[i].entity;
        if (e == nullptr) continue;
        if (e->FindComponent<RawMaterialComponent>() == nullptr) return false;
    }
    return true;
}

int Relocate(ComponentEntity* site, ComponentEntity* pile, CitizenComponent** citizenRef) {
    GameUpdateLoop* gw = GameUpdateLoop::_singleton;
    Map* map = gw->m_game;
    RawMaterialComponent* raw = pile->FindComponent<RawMaterialComponent>();
    WorkComponent* workComp = pile->FindComponent<WorkComponent>();
    if (raw == nullptr || workComp == nullptr) return 0;
    Description* descRaw = raw->_description;
    RawMaterialDescription* desc = Object::IsDerivedFromNullSafe(descRaw, RawMaterialDescription::TypeId())
                                       ? static_cast<RawMaterialDescription*>(descRaw)
                                       : nullptr;
    if (desc == nullptr) return 0;

    // Storage that takes it: retail's own haul handles the pile (or will, once a hauler
    // is free); nothing to do here.
    if (gw->m_citizenList->HasStorageFor(pile->_description, raw->quantity)) return 0;

    Pickup* pickup = FindPickupWork(gw->m_tutorial, workComp->workId);
    if (pickup == nullptr || pickup->status != 0) return 0;   // another citizen has it

    // An empty-handed citizen with room for the pile: the drop spills everything the
    // citizen carries, so it must carry only this pile.
    CitizenComponent* citizen = *citizenRef;
    StorageComponent* carry = citizen->_entity->FindComponent<StorageComponent>();
    if (carry->QueryCount() != 0) return 0;
    if (carry->capacity < desc->_weight * raw->quantity) return 0;

    const int size = map->size;
    const int citizenCell = CitizenCell(map, citizen);
    const int pileCell = pickup->field_28;
    const int pileAccess = map->FindCell(pileCell, citizenCell, 0xe07);
    if (pileAccess < 0) return 0;
    if (!map->ValidateCell(0xe1f, static_cast<uint32_t>(pileAccess), citizenCell)) return 0;

    MapComponent* mc = site->FindComponent<MapComponent>();
    Rect r;
    r.x0 = mc->tileX0;
    r.y0 = mc->tileY0;
    r.w = mc->tileX1;
    r.h = mc->tileY1;

    // The drop cell must pass DropOnGround's own search at ring 0 (Map::FindPlacementCell
    // 0x14009bd40: no occupant, no blocker -- so no other site, zone or clear marker --,
    // no footprint count, no road, buildable ground, the height test) and be reachable.
    ComponentEntity* allow = nullptr;
    auto accept = [&](int col, int row) -> bool {
        const int cell = row * size + col;
        if (map->FindPlacementCell(cell, '\0', &allow, 0) != cell) return false;
        return map->ValidateCell(0xe1f, static_cast<uint32_t>(cell), citizenCell) != 0;
    };
    const int target = ChooseDropCell(r, pileCell % size, pileCell / size, size, RingsFor(r, 6), accept);
    if (target < 0) return 0;

    // The stack runs last-pushed first: the drop and the walk to the drop cell go in
    // before the haul chain, so they run after it.
    CharacterComponent* character = citizen->character.Get();
    ActionStack* stack = &character->actionStack;
    stack->Clear();   // what the chain's cancelFirst does (0x1400c5400)

    {
        void* mem = System::MemoryAllocator::MemAlloc(0x40, 8);
        Action* drop = nullptr;
        if (mem) {
            ComponentEntity* noDestination = nullptr;   // -> SpillAllStorage -> DropOnGround
            CitizenComponent* who = citizen;
            drop = new (mem) DropOffRawMaterial(&who, &noDestination, nullptr, 0xffffffff, false);
        }
        Object::InternalPtr<Action> ibp = drop;
        ActionStackPush(stack, &ibp);
    }
    float dropPos[4];
    CellWorldPos(map, target, dropPos);
    {
        void* mem = System::MemoryAllocator::MemAlloc(0x480, 8);
        Action* walk = nullptr;
        if (mem) {
            CharacterComponent* family = citizen->_entity->FindComponent<CharacterComponent>();
            const uint8_t type = map->cells.GetData()[target].terrainType;
            const uint32_t mask = (1u << (type & 0x1f)) | 0xe1fu;
            // pathType 4: the haul chain's "carrying" walk (0x1400c0f61 CMOVNZ).
            walk = new (mem) MoveTo(&family, 4, 0, dropPos, mask, 0, kRdata_140172280);
        }
        Object::InternalPtr<Action> ibp = walk;
        ActionStackPush(stack, &ibp);
    }

    float pilePos[4];
    CellWorldPos(map, pileAccess, pilePos);
    ComponentEntity* pileRef = pile;
    CitizenComponent* who = citizen;
    WorkAct_PerformWorkCtor9_c0ad0(&who, static_cast<uint32_t>(pickup->field_20), &pileRef, pilePos, pileCell, 0);

    // Claim and stamp as Pickup::Execute does after its dispatch (0x1400f2f00).
    pickup->status = 1;
    pickup->citizenIBP = citizen;
    Profession* target10 = nullptr;
    pickup->GetTarget(&target10);
    citizen->field_0x30 = pickup->field_20;
    citizen->statusFlags |= 4;
    citizen->field_0xB0 = 0;
    if (target10 == citizen->profession && pickup->field_20 >= 0) citizen->happinessFlags |= 0x20000;

    std::fprintf(stderr, "[FoundationPiles] pile %p (%d) at %d,%d -> %d,%d (site %d,%d %dx%d)\n",
                 static_cast<void*>(pile), raw->quantity, pileCell % size, pileCell / size, target % size,
                 target / size, r.x0, r.y0, r.w, r.h);
    return 2;
}

}  // namespace FoundationPiles
}  // namespace Ext
