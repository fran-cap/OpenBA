#pragma once
// ============================================================================
// src/game/Component/ClearAreaComponent.h — ENDGAME B.3.8 block MI-A
//
// `ClearAreaComponent` as a real C++ class with a compiler-generated vtable and its
// state controller as an EMBEDDED MEMBER (B38_RULES.md D1-D6; there is no
// multiple inheritance). The engine/types POD is a forwarder to this file.
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §2 and §6, bytes read 2026-09-26):
//   reflected "ClearAreaComponent", parent "ControllerComponent", ti 0x1401c71c8
//   create 0x140030ea0: MemAlloc(0x50, 8) + ctor 0x140117fa0
//   createInPlace = the imported `Internal::CreateInPlaceObject` (returns null);
//     the tree's in-place entry builds the object and is kept (B38 B8-13).
//   ctor 0x140117fa0: Component ctor; member at +0x28; +0x48 = 0 (dword).
//     No flag OR (0x80).
//   dtor 0x140117f40: restamp only, ~Internal (no own work).
//     [0] 0x1400310c0 = the dtor inlined + MemFree on &1.
//   table 0x1401626d0, 18 slots:
//     [0] 0x1400310c0 dtor  [1]/[2] TypeName/Type  [3] 0x140117ec0 Persist
//     [16] 0x140003730 `XOR EAX,EAX`  [17] 0x140004080 `LEA RAX,[RCX+0x28]`
//     every other slot = ControllerComponent's / Component's.
//   member table 0x140162680 (9 slots): the table-driven state controller over the
//     class's state table (ControllerLive::Dispatcher<ClearAreaCtrlTag>), stamped at
//     this+0x28 after the interface 0x14015f070; back-pointer at this+0x40.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Component/ControllerComponent.h"
#include "Component/controller_state_table.h"
#include "Object/exe_type.h"

struct ClearAreaCtrlTag { using Owner = class ClearAreaComponent; };   // the handlers' receiver (controller_state_table.h)

class ClearAreaComponent final : public ControllerComponent {
    DECLARE_EXE_TYPE(ClearAreaComponent)

public:
    using StateController = ::ControllerLive::TableController<ClearAreaCtrlTag>;

    ClearAreaComponent();                                 // 0x140117fa0

    // 0x140117f40
    ~ClearAreaComponent() override;                               // [0]  0x1400310c0
    void          Persist(IO::Stream& stream) override;                 // [3]  0x140117ec0
    int         GetDefaultState() const override { return SmId(BuildingState::ClearSite); }  // [16] 0x140003730
    Controller* GetController() override { return &_controller; }  // [17] 0x140004080

    // ---- non-virtual members (retail addresses; RCX = this) ----
    int   FootprintSweep();                                       // 0x140117d60
    bool  IsDone();                                               // 0x140118440
    int   CountRemaining();                                       // 0x140118330
    void  FinishClear();                                          // 0x140118270
    // state handlers (the controller hands them its owner, node+0x18)
    char  State0Enter(StateMachine* sm);                                  // 0x140117960
    char  State0PerFrame(StateMachine* sm);                               // 0x1401178f0
    char  State0Update();                                         // 0x140117890
    char  State1Enter(StateMachine* sm);                                  // 0x140117820
    char  State2Enter();                                          // 0x1401177f0

    // PUBLIC while the ported bodies read them at raw offsets (B38 decision 1).
    StateController _controller;   // +0x28 the embedded controller (D6)
    int32_t         clearState;    // +0x48 Persist 0x140117ec0: Serialize(int&); footprint occupants left
    uint8_t         _pad4C[4];     // +0x4c
};

static_assert(sizeof(ClearAreaComponent) == 0x50, "ClearAreaComponent is 0x50 (create 0x140030ea0)");
static_assert(offsetof(ClearAreaComponent, _pad4C) == 0x4C, "ClearAreaComponent::_pad4C");
static_assert(alignof(ClearAreaComponent) == 8, "ClearAreaComponent alignment");
static_assert(offsetof(ClearAreaComponent, _controller) == 0x28,
              "ClearAreaComponent's controller member at +0x28 (slot 17 0x140004080: LEA RAX,[RCX+0x28])");
static_assert(offsetof(ClearAreaComponent, _controller) + offsetof(ControllerComponent::Controller, _owner) == 0x40,
              "controller back-pointer at +0x40 (ctor 0x140117fa0)");
static_assert(offsetof(ClearAreaComponent, clearState) == 0x48, "ClearAreaComponent +0x48");
