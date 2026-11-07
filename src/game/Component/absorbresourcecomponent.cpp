// src/game/Component/absorbresourcecomponent.cpp
// Clean reimplementation of AbsorbResourceComponent lifecycle methods.
//
// AbsorbResourceComponent (0x48 bytes) is a ControllerComponent subclass with no
// additional fields. Only one non-trivial vtable override:
//   Shutdown (0x140105c00) -- Component_Serialize + two empty Serializer blocks
//
// Decompiled from Application-x64.exe using Ghidra.

#include "object/TypeInfo.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch A
#include <cstdint>
#include <cstdio>
#include <new>
#include "object/Internal.h"   // ENDGAME B.3.4 caller batch G

// ============================================================================
// ENDGAME B.3.8 MI-A — AbsorbResourceComponent as a REAL class with its
// controller member (AbsorbResourceComponent.h, B38_RULES D9 #8).
//
// Before: COMPONENT_INERT_FACTORY_SER — a 48-slot inert table re-filled at every
// create, `Inert_ScalarDtor` in slot 0, and NO controller: slots 16/17 were inert
// stubs, so the entity's StateMachineComponent never linked this component. After:
// the compiler's 18-slot vtable, a real constructor and destructor chain, and the
// retail controller member at +0x28 over its one-state table.
// ============================================================================
#include "Object/factory_registration.h"
#include "Component/AbsorbResourceComponent.h"

#include "Component/RawMaterialComponent.h"   // ENDGAME B.3.9 C1: 0x1401c7c80 cell class
#include "Component/MapComponent.h"
#include "Component/StorageComponent.h"
#include "Entity/ComponentEntity.h"   // ComponentEntity::FindComponent<T>
#include "Entity/EntityList.h"
#include "Game/Map.h"
#include "Component/exe_bodies.h"
#include "Work/exe_bodies.h"
#include "Action/exe_bodies.h"

using AbsorbResourceCtrlDispatch = ::ControllerLive::Dispatcher<AbsorbResourceCtrlTag>;

// ============================================================
// State 0 per-frame handler — retail 0x140105950 (member column +0x18, TickAlt).
// ============================================================
// Every frame while the entity is in state 0, sweep the tiles of its footprint
// and pull every loose raw material lying there into the entity's own storage:
//
//   map  = FindComponent<MapComponent>(entity)            0x140004680 (tile rect)
//   stor = FindComponent<StorageComponent>(entity)        0x140006270
//   for y in [map.y, map.y + map.h), x in [map.x, map.x + map.w):
//       e = world grid cell (x, y)'s entity               (*(gw+0xf8): width @+0,
//                                                          cells @+0x80, stride 0x30,
//                                                          entity @ cell+8)
//       rm = first component of e whose Type() == tid(0x1401c7c80), then
//            IsDerivedFrom-confirmed, as every retail finder does
//       if (rm && StorageCanAdd(stor, e+0x28, rm+0x28))    0x14012cd80
//           StorageAdd(stor, e+0x28, rm+0x28)             0x14012d000
//           if (!(e+0x40 bit 10)) { MarkForRemoval(e)     0x1400c2ea0
//               append e to (gw+0xe8)+0xd0 }              0x140003110 SetSize(n+1, &e)
//   return 1
//
// Read from the disassembly (the decompile names this AbsorbResourceDescription_
// helper_ec70_sub_5950). The map/storage finds and the grid, storage and removal
// derefs are UNGUARDED in retail and stay so: retail relies on every entity
// carrying this component also carrying a MapComponent and a StorageComponent.
// The helpers are called directly: FindComponent<MapComponent> 0x140004680,
// FindComponent<StorageComponent> 0x140006270, Pf_StorageCanAccept_cd80 0x14012cd80, ActRm_StorageAdd_d000 0x14012d000,
// ActHunt_MarkEntityRemoval_c2ea0 0x1400c2ea0.
char AbsorbResourceComponent::State0Tick() {
    // ENDGAME B.3.9 (B39_RULES.md D5): 0x1401c7c80 is RawMaterialComponent's registered
    // type-id cell (INVENTORY_C Table 1), read here BY CELL, not by the old local's name.
    const uint32_t tidRawMaterial = RawMaterialComponent::TypeId();

    MapComponent*     map     = _entity->FindComponent<MapComponent>();       // 0x140004680
    StorageComponent* storage = _entity->FindComponent<StorageComponent>();   // 0x140006270

    const int64_t y0 = static_cast<int16_t>(map->tileY0);
    const int64_t x0 = static_cast<int16_t>(map->tileX0);
    const int64_t yEnd = static_cast<int>(y0) + static_cast<int16_t>(map->tileY1);
    const int64_t xEnd = static_cast<int>(x0) + static_cast<int16_t>(map->tileX1);
    const Map* grid = GameUpdateLoop::_singleton->m_game;

    for (int64_t y = y0; y < yEnd; ++y) {
        for (int64_t x = x0; x < xEnd; ++x) {
            const int64_t cellIndex = static_cast<int64_t>(grid->size) * y + x;
            const MapCell* cell = grid->cells.GetData() + cellIndex;
            ComponentEntity* cellEntity = cell->occupant.Get();
            if (cellEntity == nullptr) continue;

            RawMaterialComponent* rawMaterial = nullptr;
            for (int i = 0; i < cellEntity->_components._size; ++i) {
                ::Component* c = cellEntity->_components[i].Get();
                if (c->Type() != tidRawMaterial) continue;
                if (::Object::IsDerivedFromNullSafe(c, tidRawMaterial))
                    rawMaterial = static_cast<RawMaterialComponent*>(c);
                break;
            }
            if (rawMaterial == nullptr) continue;

            ComponentDescription* item = cellEntity->_description;
            // Retail reads the int32 quantity (0x140105ac9 MOV EDI,dword ptr [RAX+0x28]).
            const int quantity = rawMaterial->quantity;
            // Pf_StorageCanAccept_cd80 / ActRm_StorageAdd_d000 are forwarders: their bodies
            // StorageCanAdd (0x14012cd80) and StorageAdd (0x14012d000) are called.
            if (!(storage->CanAdd(item, quantity) & 0xff)) continue;
            storage->AddItem(item,
                       rawMaterial->quantity);

            EntityList* const entityList = GameUpdateLoop::_singleton->m_entityList;
            if ((cellEntity->_entityFlags & Bits(ComponentFlags::MarkedForRemoval)) != 0) continue;
            cellEntity->MarkForRemoval();
            // exe 0x140003110 IS Collection::Array<void*>::SetSize(n, fill).
            entityList->removalList
                .SetSize(entityList->removalList._size + 1, cellEntity);
        }
    }
    return 1;
}

// The table (controller_state_table.h) holds plain handler addresses called as
// handler([node+0x18], sm, time): [node+0x18] is the controller's back-pointer, this
// component, so the entry is a one-line adapter that calls the member.
static char AbsorbResourceState0Tick(AbsorbResourceComponent* a, StateMachine* /*sm*/, const System::Time* /*time*/) { return a->State0Tick(); }   // 0x140105950

// The retail table, transcribed from the static initializer 0x14014e4f0 ->
// 0x14002ec70: ONE state, id 0, with only the per-frame TickAlt column (+0x18)
// set (0x140105950 above); no enter, no update, no transition, no event. Built
// ONCE at static-init time, as retail's initializer does (B38 D6/D7).
static void AbsorbResourceComponent_PopulateStateTable() {
    using namespace ::ControllerLive;
    auto& t = AbsorbResourceCtrlDispatch::table;
    if (t.count != 0) return;                      // idempotent
    TableRegisterState(t, BuildingState::ClearSite, nullptr, nullptr,
                       &AbsorbResourceState0Tick, nullptr);
}
static const bool s_absorbResourceStateTableBuilt = (AbsorbResourceComponent_PopulateStateTable(), true);

// 0x140105cc0. Component() is the CALL 0x1401188b0; the 0x140162b80 and
// 0x140162000 stamps and the member's 0x14015f070 / 0x140161fb0 stamps are the
// compiler's; `_controller(this)` is the +0x40 back-pointer store and the +0x30 /
// +0x38 zeroes. No flag OR (0x80).
AbsorbResourceComponent::AbsorbResourceComponent() : _controller(this) {}

// 0x140105c60: restamp + ~Internal, no own work.
AbsorbResourceComponent::~AbsorbResourceComponent() = default;

// [3] 0x140105c00: Component::Persist plus TWO empty Serializer scopes — an
// empty scope still occupies 16 bytes in the save encoding. Retail calls
// 0x1401189b0 (Component::Persist) directly, not ControllerComponent's.
void AbsorbResourceComponent::Persist(IO::Stream& stream) {
    Component::Persist(stream);
    { IO::Serializer scope(stream, 0); }
    { IO::Serializer scope(stream, 0); }
}

// create 0x14002ea20: MemAlloc(0x48, 8), then the ctor.
::Object::Base* AbsorbResourceComponent::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(AbsorbResourceComponent), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) AbsorbResourceComponent());
}
// Retail's createInPlace is `Internal::CreateInPlaceObject` (returns null); the
// tree's in-place entry built the object, and that is kept (B38 B8-13).
::Object::Base* AbsorbResourceComponent::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) AbsorbResourceComponent());
}

DEFINE_EXE_TYPE(AbsorbResourceComponent, "ControllerComponent")
