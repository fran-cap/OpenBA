// src/game/Component/multiobjectcomponent.cpp
// Clean reimplementation of MultiObjectComponent lifecycle methods from Application-x64.exe.
//
// MultiObjectComponent (0x60 bytes) extends ControllerComponent.
// Manages multi-part objects (multi-tile buildings spanning several entities).
//
// Vtable at 0x140163cf0. Two non-trivial overrides:
//   Persist   (0x140125880) — Serialize entity ref array + multiCount
//   OnRemove  (0x1401257c0) — Mark child entities for removal
//

#include "Component/MultiObjectComponent.h"
#include <cstdint>
#include "object/TypeInfo.h"
#include "Component/exe_bodies.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Action/exe_bodies.h"   // ActHunt_MarkEntityRemoval_c2ea0 (0x1400c2ea0)
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch A
#include "io/Stream.h"      // ENDGAME B.3.4 callers batch H: IO::Stream::GetDirection
#include <cstdio>
#include <new>
#include "object/Internal.h"   // ENDGAME B.3.4 caller batch G
#include "Entity/ComponentEntity.h"   // ENDGAME B.3.9 CB: ComponentEntity::TypeId()
#include "Entity/EntityList.h"

// ============================================================
// MultiObjectComponent::OnRemove — slot 7 (0x1401257c0)
// ============================================================
// Iterates the child entity array (count and data re-read per pass, as retail). For
// each child not yet marked (entity flag bit 10), MarkForRemoval (0x1400c2ea0) and
// append it to the entity list's removal queue (0x140003110).
// The singleton / entity-list null tests are the tree's (retail has neither).
void MultiObjectComponent::OnRemove() {
    if (GameUpdateLoop::_singleton == nullptr) return;

    for (int i = 0; i < children._size; i++) {
        ComponentEntity* entityPtr = children[i].Get();
        EntityList* entityList = GameUpdateLoop::_singleton->m_entityList;   // +0xE8

        if (entityPtr != nullptr && (entityPtr->_entityFlags & Bits(ComponentFlags::MarkedForRemoval)) == 0) {
            entityPtr->MarkForRemoval();   // 0x1400c2ea0

            if (entityList) {
                int removalCount = entityList->removalList._size;
                entityList->removalList
                    .SetSize(removalCount + 1, entityPtr);
            }
        }
    }
}

// ===== ENDGAME B.3.8 MI-B — MultiObjectComponent's own slot bodies =====
#include "Object/factory_registration.h"
#include "Component/controller_state_table.h"
#include "Component/MapComponent.h"
#include "Component/FloorComponent.h"

// ============================================================================
// ENDGAME B.3.8 MI-B — MultiObjectComponent as a REAL class with its retail
// controller member (MultiObjectComponent.h; B38_RULES D1, D2, D6, D9 #8).
//
// Before: a hand-built 48-slot table (slots 3/12 patched per create,
// Inert_ScalarDtor in slot 0) and NO controller: slots 16/17 were inert stubs, so
// the entity's StateMachineComponent never linked this node. After: the compiler's
// 18-slot vtable, a real ctor/dtor chain, and retail's member at +0x28 over retail's
// state table.
// ============================================================================

using MultiObjectCtrlDispatch = ::ControllerLive::Dispatcher<MultiObjectCtrlTag>;

// state-2 ENTER — retail 0x140125560. For every child entity in the list at +0x48
// (count +0x50, re-read each pass):
//   map   = FindComponent<MapComponent>(child)      (type 0x1401c79e0)
//   map->AddToMap()                                 (0x1401241b0 — stamp the child's
//                                                    footprint; guarded by its own
//                                                    +0x20 bit 20, so idempotent)
//   floor = FindComponent<FloorComponent>(child)    (type 0x1401c76d0)
//   if (floor) FloorComponent_sub_f390(floor)       (0x14011f390)
// then `XOR AL,AL`. Retail derefs the MapComponent unguarded: every child of a
// multi-object structure carries one (a faithful crash if not, not a guard here).
char MultiObjectComponent::EnterState2() {
    for (int i = 0; i < children._size; ++i) {
        ComponentEntity* child = children[i].Get();
        MapComponent* map = child->FindComponent<MapComponent>();
        map->AddToMap();   // 0x1401241b0
        FloorComponent* floor = child->FindComponent<FloorComponent>();
        if (floor) floor->PlaceFloor();   // 0x14011f390
    }
    return 0;
}

// The table (controller_state_table.h) holds plain handler addresses called as
// handler([node+0x18], sm): [node+0x18] is the controller's back-pointer, this
// component, so the entry is a one-line adapter that calls the member.
static char MultiObjectEnterState2(MultiObjectComponent* c, StateMachine* /*sm*/) { return c->EnterState2(); }   // 0x140125560

// Retail's table, transcribed from MultiObjectDescription_Setup (0x14014f530): its
// RegisterState (0x1400360f0) registers state 2 with enter 0x140125560 and every other
// column zeroed; its AddTransition (0x140036210) appends 3 with a null handler. State 3
// is a transition target only. Built ONCE at static-init time (B38 D6/D7).
static void MultiObjectComponent_PopulateStateTable() {
    using namespace ::ControllerLive;
    auto& t = MultiObjectCtrlDispatch::table;
    if (t.count != 0) return;                      // idempotent
    TableRegisterState(t, BuildingState::Built, &MultiObjectEnterState2,
                       nullptr, nullptr, nullptr);
    TableAddTransition(t, BuildingState::Demolish, nullptr);
}
static const bool s_multiObjectStateTableBuilt = (MultiObjectComponent_PopulateStateTable(), true);

// 0x140125a70. Component() is the CALL 0x1401188b0; the stamps are the compiler's;
// `_controller(this)` is the member's back-pointer (+0x40); +0x48 = +0x50 = 0;
// +0x58 = 1; then `_flags |= 2` (0x82).
// (+0x58 is the cell count the bridge/fence tools stamp; the tree's factory zeroed it.)
MultiObjectComponent::MultiObjectComponent()
    : _controller(this), multiCount(1), _pad5C{} {
    OrFlagsInCtor(0x2);
}

// 0x140125a00: MemFree(+0x48), +0x48 = +0x50 = 0 (~children: kSpec_PtrZero has no element
// destroy), then the controller's restamp (~_controller), the restamps and ~Internal (the
// compiler chain, reverse declaration order). The body is empty.
MultiObjectComponent::~MultiObjectComponent() {
}

// create 0x140035ef0: MemAlloc(0x60, 8), then the ctor.
::Object::Base* MultiObjectComponent::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(MultiObjectComponent), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) MultiObjectComponent());
}
// Retail's createInPlace is `Internal::CreateInPlaceObject` (returns null); the
// tree's in-place entry built the object, and that is kept (B38 B8-13).
::Object::Base* MultiObjectComponent::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) MultiObjectComponent());
}

// ============================================================
// MultiObjectComponent::Persist (0x140125880)
// ============================================================
// Component_Serialize + empty serializer block + second serializer:
//   serialize count from +0x50, resize array if loading, serialize each IBP
//   with IsDerivedFrom validation, serialize multiCount at +0x58.
//   After loading, remove null entries from array (compact).

void MultiObjectComponent::Persist(IO::Stream& stream) {
    Component::Persist(stream);   // retail: CALL 0x1401189b0 (qualified)

    IO::Stream* st = &stream;

    // First empty serializer block (controller section)
    { IO::Serializer scope(*st, 0); }

    // Second serializer block with entity array + multiCount
    IO::Serializer ser(*st, 0);

    int localCount = children._size;
    ser.Serialize(localCount, nullptr);

    // If loading (stream+8 == 0), resize array
    if (static_cast<int>(st->GetDirection()) == 0) {   // IO::Stream::GetDirection, retail 0x180006200
        children.SetSize(localCount);
    }

    // Serialize each InternalBasePtr in the array
    Object::InternalPtr<ComponentEntity>* arrayBase = children._data;
    if (localCount > 0) {
        for (int i = 0; i < localCount; i++) {
            Object::InternalPtr<ComponentEntity>& ibp = arrayBase[i];
            ibp.InternalBasePtr::Serialize(ser);

            // Validate: if not ComponentEntity, null it out
            // ENDGAME B.3.9 CB: retail validates this IBP with IsDerivedFrom(ptr, [0x1401c5d70]) =
            // ComponentEntity::TypeId() (Persist 0x140125880, at 0x140125936). The tree read 0x1401c72e8 instead -- the NAME
            // field (+8) of Description's record, whose only retail reader is Description's
            // TypeName slot 0x140031c60 -- which is unregistered, so the check never ran.
            // NOT RETAIL (F-143): null and type tests on the held Internal*, before any
            // downcast to ComponentEntity*; the answer is the same.
            if (!ibp.IsNull()) {
                // slot 0x140152a98 = ?IsDerivedFrom@Base@Object@@QEBA_NI@Z
                if (!::Object::IsDerivedFromNullSafe(ibp, ComponentEntity::TypeId())) {
                    ibp = nullptr;
                }
            }
        }
    }

    // Serialize multiCount at +0x58 (retail Serialize(int&)).
    ser.Serialize(multiCount, nullptr);

    // On load: compact array — remove null entries
    if (static_cast<int>(st->GetDirection()) == 0) {   // IO::Stream::GetDirection, retail 0x180006200
        int count = children._size;
        for (int i = 0; i < count; /* no increment */) {
            Object::InternalPtr<ComponentEntity>* arr = children._data;
            if (arr[i].Get() == nullptr) {
                // Shift remaining entries down
                for (int j = i; j < count - 1; j++) {
                    // Clear then copy
                    arr[j] = nullptr;
                    arr[j] = arr[j + 1];
                }
                count--;
                children._size = count;
                // Don't increment i, re-check this slot
            } else {
                i++;
            }
        }
    }
}
// -- slot 12 (+0x60) -- retail 0x140125490 -- the child-entity MESSAGE FORWARDER -----
// Pure relay, no message filter: for every child entity in the array at +0x48 (count at
// +0x50), dispatch slot 12 on EVERY component that child carries, with the same msgId.
//
// This is how a multi-part structure's parts learn that the structure finished. It was
// inert, and that is why a completed Tunnel had no terrain deformation: Tunnel.rsc's own
// `TerraformDescription terraform {}` block is EMPTY -- the floor/ceiling picking meshes
// live on Template/TunnelStart.rsc, i.e. on the two MOUTH SEGMENTS, not on the master.
// BuildComponent state 8 broadcasts msg 4 to the master only (0x1400c2b70 walks ONE
// entity), so without this forwarder the segments that actually own the meshes were never
// told, and their TerraformComponent slot 12 never ran.
//
// Measured 2026-08-22: with terraform live but this still inert, the trace showed exactly
// one "msg 4 -> deform" (the master), both mesh passes correctly skipped on a null mesh,
// and FootprintPass converting 18 cells -- master-level work only, no carve.
void MultiObjectComponent::OnEvent(int eventId) {
    const int count = children._size;
    Object::InternalPtr<ComponentEntity>* list = children._data;
    if (count <= 0 || !list) return;

    for (int i = 0; i < count; ++i) {
        ComponentEntity* child = list[i].Get();
        if (!child) continue;

        const int compCount = child->_components._size;
        Object::InternalPtr<Component>* comps = child->_components._data;
        for (int k = 0; k < compCount && comps; ++k) {
            Component* comp = comps[k].Get();
            if (!comp) continue;
            comp->OnEvent(eventId);   // slot 12 (+0x60)
        }
    }
}

DEFINE_EXE_TYPE(MultiObjectComponent, "ControllerComponent")
