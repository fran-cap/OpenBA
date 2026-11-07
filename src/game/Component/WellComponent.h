#pragma once
// ============================================================================
// src/game/Component/WellComponent.h — ENDGAME B.3.8 pilot (B38_RULES.md D1-D6)
//
// The first exe ControllerComponent leaf with a COMPILER-GENERATED vtable and
// an EMBEDDED controller member (D6).
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §2 and §6, bytes read 2026-09-26):
//   reflected "WellComponent", parent "ControllerComponent", ti 0x1401c4c28
//   create 0x14000fc80: MemAlloc(0x48, 8); if non-null CALL ctor 0x14005d060
//   createInPlace = the IMPORTED `Object::Internal::CreateInPlaceObject`, whose
//     body is `XOR EAX,EAX; RET` — retail cannot build a component in place.
//     The tree's in-place entry DID build one; this class keeps the tree's
//     behaviour (CreateInPlaceObject below) and B38_RULES §6 records it.
//   ctor 0x14005d060: CALL Component ctor 0x1401188b0; stamp 0x140162b80;
//     stamp 0x14015ae90; member at +0x28: stamp 0x14015f070, +0x30 = +0x38 = 0,
//     stamp 0x14015ae40, +0x40 = this; then `_flags |= 2` with the dead notify
//     arm (Component.h OrFlagsInCtor).
//   dtor  0x14005d000 (Ghidra mislabels it `WellComponent_Ctor`): stamp
//     0x14015ae90, member vptr = 0x14015f070, stamp 0x140162b80, 0x140162920,
//     tail-JMP `~Internal`. No other work: the member dtor and every base dtor
//     are empty.
//   table 0x14015ae90, 18 slots:
//     [0]  0x14000fe50 scalar deleting dtor (the dtor above + MemFree on &1)
//     [1]  0x14000fc70 TypeName   [2] 0x14000fc60 Type
//     [3]  0x14005cfa0 Persist: CALL 0x1401189b0 (Component::Persist), then TWO
//          empty Serializer(stream,0) scopes = ControllerComponent::Persist's
//          one (inlined) + its own
//     [7]  0x14005cf60 OnRemove (unregister the water source)
//     [16] 0x1400026c0 `MOV EAX,2`   [17] 0x140004080 `LEA RAX,[RCX+0x28]`
//     every other slot = ControllerComponent's / Component's
//   member table 0x14015ae40: 9 slots, the table-driven state controller over
//     the class's state table 0x1401d1d70 (populated by the static initializer
//     0x14014bc20: state 2 -> 0x14005cf20, state 3 -> 0x14005cee0).
//   sizeof 0x48.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Component/ControllerComponent.h"
#include "Component/controller_state_table.h"
#include "Object/exe_type.h"

struct WellCtrlTag { using Owner = class WellComponent; };   // the handlers' receiver (controller_state_table.h)

class WellComponent final : public ControllerComponent {
    DECLARE_EXE_TYPE(WellComponent)

public:
    using StateController = ::ControllerLive::TableController<WellCtrlTag>;

    WellComponent();                                              // 0x14005d060

    // 0x14005d000
    ~WellComponent() override = default;                          // [0]  0x14000fe50
    void          Persist(IO::Stream& stream) override;                 // [3]  0x14005cfa0
    void          OnRemove() override;                            // [7]  0x14005cf60
    int           GetDefaultState() const override { return SmId(BuildingState::Built); }  // [16] 0x1400026c0
    Controller*   GetController() override { return &_controller; }  // [17] 0x140004080

    // The controller's state handlers (table 0x14014bc20; RCX = this).
    char EnterBuilt();                                            // 0x14005cf20 state 2: register
    char EnterDemolish();                                         // 0x14005cee0 state 3: unregister

    // +0x28 — the embedded controller (D6). Public: StateMachineComponent and
    // the controller dispatch read it through slot 17 and at +0x28 raw.
    StateController _controller;
};

static_assert(sizeof(WellComponent) == 0x48, "WellComponent is 0x48 (create 0x14000fc80 MemAlloc(0x48))");
static_assert(offsetof(WellComponent, _controller) == 0x28,
              "WellComponent's controller member at +0x28 (slot 17 0x140004080: LEA RAX,[RCX+0x28])");
static_assert(offsetof(WellComponent, _controller) + offsetof(ControllerComponent::Controller, _owner) == 0x40,
              "controller back-pointer at +0x40 (ctor 0x14005d060)");
