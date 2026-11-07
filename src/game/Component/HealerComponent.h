#pragma once
// ============================================================================
// src/game/Component/HealerComponent.h — ENDGAME B.3.8 block MI-A
//
// `HealerComponent` as a real C++ class with a compiler-generated vtable and its
// state controller as an EMBEDDED MEMBER (B38_RULES.md D1-D6; there is no
// multiple inheritance). The engine/types POD is a forwarder to this file.
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §2 and §6, bytes read 2026-09-26):
//   reflected "HealerComponent", parent "ControllerComponent", ti 0x1401c4330
//   create 0x140006490: MemAlloc(0x48, 8) + ctor 0x14004af70
//   createInPlace = the imported `Internal::CreateInPlaceObject` (returns null);
//     the tree's in-place entry builds the object and is kept (B38 B8-13).
//   ctor 0x14004af70: Component ctor; member at +0x28; `_flags |= 2`.
//   dtor 0x14004af10: restamp only, ~Internal (no own work).
//     [0] 0x140006660 = the dtor inlined + MemFree on &1.
//   table 0x140159730, 18 slots:
//     [0] 0x140006660 dtor  [1]/[2] TypeName/Type  [3] 0x14004aeb0 Persist
//     [7] 0x14004ae70 OnRemove
//     [16] 0x1400026c0 `MOV EAX,2`  [17] 0x140004080 `LEA RAX,[RCX+0x28]`
//     every other slot = ControllerComponent's / Component's.
//   member table 0x1401596e0 (9 slots): the table-driven state controller over the
//     class's state table (ControllerLive::Dispatcher<HealerCtrlTag>), stamped at
//     this+0x28 after the interface 0x14015f070; back-pointer at this+0x40.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Component/ControllerComponent.h"
#include "Component/controller_state_table.h"
#include "Object/exe_type.h"

struct HealerCtrlTag { using Owner = class HealerComponent; };   // the handlers' receiver (controller_state_table.h)

class HealerComponent final : public ControllerComponent {
    DECLARE_EXE_TYPE(HealerComponent)

public:
    using StateController = ::ControllerLive::TableController<HealerCtrlTag>;

    HealerComponent();                                    // 0x14004af70

    // 0x14004af10
    ~HealerComponent() override;                                  // [0]  0x140006660
    void          Persist(IO::Stream& stream) override;                 // [3]  0x14004aeb0
    void          OnRemove() override;                            // [7]  0x14004ae70
    int         GetDefaultState() const override { return SmId(BuildingState::Built); }  // [16] 0x1400026c0
    Controller* GetController() override { return &_controller; }  // [17] 0x140004080

    // state handlers (the controller hands them its owner, node+0x18)
    char  StateBuiltEnter();                                      // 0x14004ae30 (state Built)
    char  StateDemolishEnter();                                   // 0x14004adf0 (state Demolish)

    // PUBLIC while the ported bodies read them at raw offsets (B38 decision 1).
    StateController _controller;   // +0x28 the embedded controller (D6)
};

static_assert(sizeof(HealerComponent) == 0x48, "HealerComponent is 0x48 (create 0x140006490)");
static_assert(alignof(HealerComponent) == 8, "HealerComponent alignment");
static_assert(offsetof(HealerComponent, _controller) == 0x28,
              "HealerComponent's controller member at +0x28 (slot 17 0x140004080: LEA RAX,[RCX+0x28])");
static_assert(offsetof(HealerComponent, _controller) + offsetof(ControllerComponent::Controller, _owner) == 0x40,
              "controller back-pointer at +0x40 (ctor 0x14004af70)");
