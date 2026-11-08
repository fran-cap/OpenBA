// src/game/Component/marketlocationcomponent.cpp
// Clean reimplementation of MarketLocationComponent lifecycle methods from Application-x64.exe.
//
// MarketLocationComponent is a ControllerComponent subclass (0x48 bytes) with no
// additional fields. Two non-trivial vtable overrides:
//   Shutdown (0x14004efa0) — Component_Serialize + two empty Serializer blocks
//   OnCreate (0x14004ef60) — slot 7, the REMOVAL hook: erase-by-value of the owner
//                             entity from the town's market-location list
//
// Decompiled from Application-x64.exe using Ghidra.

#include "Component/MarketLocationComponent.h"
#include "object/Internal.h"
#include "object/TypeInfo.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Component/exe_bodies.h"   // Array<T>::Remove (0x140002cc0)
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch A
#include <cstdint>
#include <cstdio>
#include <new>

// FUN_140002cc0 = DynArray8_EraseByValue (Array<T>::Remove, called by name) — linear
// scan of an 8-byte-stride DynArray (+0x00 data, +0x08 count, +0x0C capacity), shift-down
// removing EVERY slot equal to *value. It is the UNREGISTER primitive, not the append one
// (append counterpart: DynArray8_ResizeFill 0x140003110). Naming it "RegisterEntity" is the
// exact trap that cost a day on the residence registry — see
// reference_ghidra_oncreate_onremove_mislabel.


// ============================================================================
// ENDGAME B.3.8 MI-B — MarketLocationComponent as a REAL class with its retail
// controller member (MarketLocationComponent.h; B38_RULES D1, D2, D6, D9 #8).
//
// Before: COMPONENT_INERT_FACTORY expanded by hand (48-slot table, slots 3/7
// patched per create, Inert_ScalarDtor in slot 0) and NO controller: slots 16/17
// were inert stubs, so the entity's StateMachineComponent never linked this node
// and the market never entered state 2. After: the compiler's 18-slot vtable, a
// real ctor/dtor chain, and retail's member at +0x28 over retail's state table.
// ============================================================================
#include "Object/factory_registration.h"
#include "Entity/CitizenBehavior.h"   // the market-location list (GameUpdateLoop::m_citizenList)
#include "io/Serializer.h"

using MarketLocationCtrlDispatch = ::ControllerLive::Dispatcher<MarketLocationCtrlTag>;

// The town manager at GameUpdateLoop+0xf0; its +0x10 array is the market-location list.
static CitizenBehavior* MarketTown() {
    return GameUpdateLoop::_singleton->m_citizenList;   // +0xF0
}

// state-2 ENTER — retail 0x14004ef20. REGISTER the market: append the owner entity to
// the town's market-location list (CitizenBehavior +0x10) —
//   local = _entity; list = town +0x10;
//   Array<void*>::SetSize(list, list.count + 1, &local)       (0x140003110)
// — then `XOR AL,AL` (0: the Tick's enter walk continues past this node). The slot-7
// removal hook (0x14004ef60) and the state-3 enter below are the matching erase.
static char MarketLoc_State2Enter(MarketLocationComponent* self, StateMachine* /*sm*/) {
    if (GameUpdateLoop::_singleton == nullptr) return 0;
    ComponentEntity* entityPtr = self->_entity.Get();
    CitizenBehavior* town = MarketTown();
    const int count = town->marketLocations._size;
    town->marketLocations
        .SetSizeFill(count + 1, &entityPtr);
    return 0;
}

// state-3 ENTER — retail 0x14004eee0: `local = _entity;
// DynArray8_EraseByValue(town +0x10, &local)` (0x140002cc0), then `XOR AL,AL`.
// Byte-for-byte the slot-7 removal hook's body (retail: an identical body, not a dispatch).
static char MarketLoc_State3Enter(MarketLocationComponent* self, StateMachine* /*sm*/) {
    self->MarketLocationComponent::OnRemove();
    return 0;
}

// Retail's table, transcribed from MarketLocationDescription_SetupCrossRefs
// (0x14014b220) call for call. Its RegisterState (0x140008980) stores only the id and
// the enter handler (the other columns are zeroed); its AddTransition (0x140008a90)
// has no handler. Built ONCE at static-init time (B38 D6/D7).
static void MarketLocationComponent_PopulateStateTable() {
    using namespace ::ControllerLive;
    auto& t = MarketLocationCtrlDispatch::table;
    if (t.count != 0) return;                      // idempotent
    TableRegisterState(t, BuildingState::Built, &MarketLoc_State2Enter,
                       nullptr, nullptr, nullptr);
    TableAddTransition(t, BuildingState::Demolish, nullptr);
    TableRegisterState(t, BuildingState::Demolish, &MarketLoc_State3Enter,
                       nullptr, nullptr, nullptr);
    TableAddTransition(t, BuildingState::Collapse, nullptr);
    TableAddTransition(t, BuildingState::HaulAway, nullptr);
    TableAddTransition(t, BuildingState::Deconstruct, nullptr);
}
static const bool s_marketLocationStateTableBuilt = (MarketLocationComponent_PopulateStateTable(), true);

// 0x14004f060. Component() is the CALL 0x1401188b0; the stamps are the compiler's;
// `_controller(this)` is the member's back-pointer (+0x40); then `_flags |= 2` (0x82).
MarketLocationComponent::MarketLocationComponent() : _controller(this) {
    OrFlagsInCtor(0x2);
}

// create 0x1400086c0: MemAlloc(0x48, 8), then the ctor.
::Object::Base* MarketLocationComponent::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(MarketLocationComponent), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) MarketLocationComponent());
}
// Retail's createInPlace is `Internal::CreateInPlaceObject` (returns null); the
// tree's in-place entry built the object, and that is kept (B38 B8-13).
::Object::Base* MarketLocationComponent::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) MarketLocationComponent());
}

// [3] 0x14004efa0 (the body resolves at its top). [7] 0x14004ef60 — the removal hook.
// ============================================================
// MarketLocationComponent::Persist (vtable slot [3], 0x14004efa0)
// ============================================================
// Calls Component_Serialize (base), then creates/destroys two empty
// Serializer blocks (version 0 each). These exist for binary format
// compatibility (versioned sub-sections with no fields).
//
// Original disassembly:
//   CALL Component_Serialize (0x1401189b0)
//   CALL Serializer_Ctor(local1, stream, 0)
//   CALL Serializer_Dtor(local1)
//   CALL Serializer_Ctor(local2, stream, 0)
//   CALL Serializer_Dtor(local2)

void MarketLocationComponent::Persist(IO::Stream& stream) {
    // Call base Component_Serialize
    Component::Persist(stream);   // retail: CALL 0x1401189b0 (qualified)

    IO::Stream* st = &stream;

    // Two empty serializer blocks (version 0)
    { IO::Serializer block1(*st, 0); }
    { IO::Serializer block2(*st, 0); }
}
// ============================================================
// MarketLocationComponent::OnRemove (vtable slot [7], 0x14004ef60)
// ============================================================
// ⚠ The Ghidra name lies. Slot 7 on this component family is the REMOVAL hook, not
// OnAdded: entity mark-removal (FUN_1400c2ea0) broadcasts vtable+0x38 to every
// component whose capability flags (+0x20) carry bit 1 — MarketLocationComponent's
// 0x82 qualifies. The body is a single DynArray8_EraseByValue, i.e. it UNREGISTERS
// the owning entity from the town's market-location list at (gameState+0xf0)+0x10.
// The function name is kept because it is the name in marketlocationcomponent_funcs.h
// (out of this wave's file scope); the behaviour is the erase.
//
// Original disassembly:
//   MOV RAX,[RCX+0x10]          ; entity ptr from owner
//   LEA RDX,[RSP+0x30]          ; stack InternalBasePtr
//   MOV [RSP+0x30],RAX          ; store entity ptr (the needle)
//   MOV RAX,[DAT_1401c3dd8]     ; game state singleton
//   MOV RCX,[RAX+0xf0]          ; town / world manager
//   ADD RCX,0x10                ; market location entity list
//   CALL FUN_140002cc0           ; erase-by-value

void MarketLocationComponent::OnRemove() {
    if (GameUpdateLoop::_singleton == nullptr)
        return;

    // The needle: the owner entity
    ComponentEntity* entityPtr = _entity.Get();

    // Erase the owner entity from the town's market-location list
    MarketTown()->marketLocations.Remove(entityPtr);   // 0x140002cc0
}

DEFINE_EXE_TYPE(MarketLocationComponent, "ControllerComponent")
