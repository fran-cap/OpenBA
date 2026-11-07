#pragma once
// ============================================================================
// src/game/Component/HappinessComponent.h — ENDGAME B.3.8 block MI-A
//
// `HappinessComponent` as a real C++ class with a compiler-generated vtable and its
// state controller as an EMBEDDED MEMBER (B38_RULES.md D1-D6; there is no
// multiple inheritance). The engine/types POD is a forwarder to this file.
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §2 and §6, bytes read 2026-09-26):
//   reflected "HappinessComponent", parent "ControllerComponent", ti 0x1401c42c0
//   create 0x140005ba0: MemAlloc(0x50, 8) + ctor 0x14004ab60
//   createInPlace = the imported `Internal::CreateInPlaceObject` (returns null);
//     the tree's in-place entry builds the object and is kept (B38 B8-13).
//   ctor 0x14004ab60: Component ctor; +0x28 = 0 (byte); member at +0x30;
//     `_flags |= 2`.
//   dtor 0x14004ab00: restamp only, ~Internal (no own work).
//     [0] 0x140005db0 = the dtor inlined + MemFree on &1.
//   table 0x1401595d0, 18 slots:
//     [0] 0x140005db0 dtor  [1]/[2] TypeName/Type  [3] 0x14004aa80 Persist
//     [7] 0x14004aa20 OnRemove  [15] 0x14004a780 OnMessage (437 B)
//     [16] 0x1400026c0 `MOV EAX,2`  [17] 0x140005be0 `LEA RAX,[RCX+0x30]`
//     every other slot = ControllerComponent's / Component's.
//   member table 0x140159580 (9 slots): the table-driven state controller over the
//     class's state table (ControllerLive::Dispatcher<HappinessCtrlTag>), stamped at
//     this+0x30 after the interface 0x14015f070; back-pointer at this+0x48.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Component/ControllerComponent.h"
#include "Component/controller_state_table.h"
#include "Object/exe_type.h"

struct HappinessCtrlTag { using Owner = class HappinessComponent; };   // the handlers' receiver (controller_state_table.h)

class HappinessComponent final : public ControllerComponent {
    DECLARE_EXE_TYPE(HappinessComponent)

public:
    using StateController = ::ControllerLive::TableController<HappinessCtrlTag>;

    HappinessComponent();                                 // 0x14004ab60

    // 0x14004ab00
    ~HappinessComponent() override;                               // [0]  0x140005db0
    void          Persist(IO::Stream& stream) override;                 // [3]  0x14004aa80
    void          OnRemove() override;                            // [7]  0x14004aa20
    // message 0: spend one stored good that meets _goodsFlags (the tavern's ale)
    void          OnMessage(int message, void* data,
                            uint32_t arg0, uint32_t arg1) override;  // [15] 0x14004a780
    int         GetDefaultState() const override { return SmId(BuildingState::Built); }  // [16] 0x1400026c0
    Controller* GetController() override { return &_controller; }  // [17] 0x140005be0

    // ---- non-virtual members (retail addresses; RCX = this) ----
    void  UpdateInfluence(int16_t delta);                         // 0x14004a300
    bool  IsActive();                                             // 0x14004a550
    void  Register();                                             // 0x14004a4f0
    void  Unregister();                                           // 0x14004a4a0
    // state handlers (the controller hands them its owner, node+0x18)
    char  StateBuiltEnter();                                      // 0x14004a9f0 (states Built, Producing)
    char  StateBuiltPerFrame();                                   // 0x14004a9a0 (states Built, Producing)
    char  StateDemolishEnter();                                   // 0x14004a940 (state Demolish)

    // PUBLIC while the ported bodies read them by name (B38 decision 1).
    bool            registered;    // +0x28 on the town's happiness list (Persist 0x14004aa80: Serialize(bool&))
    uint8_t         _pad29[7];     // +0x29
    StateController _controller;   // +0x30 the embedded controller (D6)
};

// PlayAnimation_Action_Ctor4 (0x1400ba856) builds a SendComponentMessage whose target is
// [0x1401c42c0] = HappinessComponent::TypeId(); SendComponentMessage::AbstractImpl delivers
// it to the slot-15 OnMessage 0x14004a780 above.

static_assert(sizeof(HappinessComponent) == 0x50, "HappinessComponent is 0x50 (create 0x140005ba0)");
static_assert(offsetof(HappinessComponent, _pad29) == 0x29, "HappinessComponent::_pad29");
static_assert(alignof(HappinessComponent) == 8, "HappinessComponent alignment");
static_assert(offsetof(HappinessComponent, _controller) == 0x30,
              "HappinessComponent's controller member at +0x30 (slot 17 0x140005be0: LEA RAX,[RCX+0x30])");
static_assert(offsetof(HappinessComponent, _controller) + offsetof(ControllerComponent::Controller, _owner) == 0x48,
              "controller back-pointer at +0x48 (ctor 0x14004ab60)");
static_assert(offsetof(HappinessComponent, registered) == 0x28, "HappinessComponent +0x28");
