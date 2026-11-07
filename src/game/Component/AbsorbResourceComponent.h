#pragma once
// ============================================================================
// src/game/Component/AbsorbResourceComponent.h — ENDGAME B.3.8 block MI-A
//
// `AbsorbResourceComponent` as a real C++ class with a compiler-generated vtable
// and its state controller as an EMBEDDED MEMBER (B38_RULES.md D1-D6, D9 #8).
// The engine/types POD is a forwarder to this file.
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §2 and §6, bytes read 2026-09-26):
//   reflected "AbsorbResourceComponent", parent "ControllerComponent", ti 0x1401c6f28
//   create 0x14002ea20: MemAlloc(0x48, 8) + ctor 0x140105cc0
//   createInPlace = the imported `Internal::CreateInPlaceObject` (returns null);
//     the tree's in-place entry builds the object and is kept (B38 B8-13).
//   ctor 0x140105cc0: Component ctor; member at +0x28 (0x14015f070, then
//     0x140161fb0; +0x30 = +0x38 = 0; +0x40 = this). No own field, no flag OR.
//   dtor 0x140105c60 (inlined in [0] 0x14002ebf0): restamp only, ~Internal.
//   table 0x140162000, 18 slots:
//     [0] 0x14002ebf0 dtor  [1]/[2] TypeName/Type  [3] 0x140105c00 Persist
//     [16] 0x140003730 `XOR EAX,EAX`  [17] 0x140004080 `LEA RAX,[RCX+0x28]`
//     every other slot = ControllerComponent's / Component's.
//   member table 0x140161fb0 (9 slots): the table-driven state controller over
//     the class's state table 0x1401d2d10 (count 0x1401d3c90, built flag
//     0x1401d3c98), stamped at this+0x28; back-pointer at this+0x40.
//   state table, built by the static initializer 0x14014e4f0 -> 0x14002ec70:
//     ONE state, id 0, no enter/update, per-frame (TickAlt, +0x18 column)
//     0x140105950 — sweep the footprint and absorb loose raw materials into the
//     entity's storage. No transitions.
//
// The tree had NO member here: the inert factory left slots 16/17 as stubs, so
// the StateMachineComponent never linked this controller (B38_RULES D9 #8).
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Component/ControllerComponent.h"
#include "Component/controller_state_table.h"
#include "Object/exe_type.h"

struct AbsorbResourceCtrlTag { using Owner = class AbsorbResourceComponent; };   // the handlers' receiver (controller_state_table.h)

class AbsorbResourceComponent final : public ControllerComponent {
    DECLARE_EXE_TYPE(AbsorbResourceComponent)

public:
    using StateController = ::ControllerLive::TableController<AbsorbResourceCtrlTag>;

    AbsorbResourceComponent();                                    // 0x140105cc0

    // 0x140105c60
    ~AbsorbResourceComponent() override;                          // [0]  0x14002ebf0
    void          Persist(IO::Stream& stream) override;                 // [3]  0x140105c00
    int         GetDefaultState() const override { return SmId(BuildingState::ClearSite); }  // [16] 0x140003730
    Controller* GetController() override { return &_controller; } // [17] 0x140004080

    // The per-frame handler of state 0 (RCX = [node+0x18] = this): sweep the footprint
    // and absorb loose raw materials into the entity's storage.
    char State0Tick();                                            // 0x140105950

    // PUBLIC while the ported bodies read them at raw offsets (B38 decision 1).
    StateController _controller;   // +0x28 the embedded controller (D6)
};

static_assert(sizeof(AbsorbResourceComponent) == 0x48, "AbsorbResourceComponent is 0x48 (create 0x14002ea20)");
static_assert(alignof(AbsorbResourceComponent) == 8, "AbsorbResourceComponent alignment");
static_assert(offsetof(AbsorbResourceComponent, _controller) == 0x28,
              "AbsorbResourceComponent's controller member at +0x28 (slot 17 0x140004080: LEA RAX,[RCX+0x28])");
static_assert(offsetof(AbsorbResourceComponent, _controller) + offsetof(ControllerComponent::Controller, _owner) == 0x40,
              "controller back-pointer at +0x40 (ctor 0x140105cc0)");
