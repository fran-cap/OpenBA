#pragma once
// ============================================================================
// src/game/Component/StorageLocationComponent.h — ENDGAME B.3.8 block MI-B
//
// `StorageLocationComponent` (registers a stockpile/barn as a town storage
// location) as a real C++ class with a compiler-generated vtable and its state
// controller as an EMBEDDED MEMBER (B38_RULES.md D1-D6). No fields of its own.
// The engine/types POD is a forwarder to this file.
//
// RETAIL (B38_SLOTS.md §2 / §6, bytes read 2026-09-26):
//   reflected "StorageLocationComponent", parent "ControllerComponent", ti 0x1401c7ee8
//   create 0x140038df0: MemAlloc(0x48, 8) + ctor 0x14012d360
//   ctor 0x14012d360: Component ctor; member at +0x28 (0x14015f070, 0x140164830),
//     back-pointer +0x40; `_flags |= 2`.
//   [0] 0x140038fc0: restamps + ~Internal + MemFree on &1 (no body of its own).
//   table 0x140164880, 18 slots: [3] 0x14012d2a0 Persist, [7] 0x14012d270 OnRemove,
//     [16] 0x1400026c0 `MOV EAX,2`, [17] 0x140004080 `LEA RAX,[RCX+0x28]`;
//     every other slot = ControllerComponent's / Component's.
//   member table 0x140164830: the table-driven controller over the class's state
//     table (ControllerLive::Dispatcher<StorageCtrlTag>).
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Component/ControllerComponent.h"
#include "Component/controller_state_table.h"
#include "Object/exe_type.h"

struct StorageCtrlTag { using Owner = class StorageLocationComponent; };   // the handlers' receiver (controller_state_table.h)

class StorageLocationComponent final : public ControllerComponent {
    DECLARE_EXE_TYPE(StorageLocationComponent)

public:
    using StateController = ::ControllerLive::TableController<StorageCtrlTag>;

    StorageLocationComponent();                                          // 0x14012d360

    // 0x140038fc0: no body
    ~StorageLocationComponent() override = default;                     // [0]  0x140038fc0
    void          Persist(IO::Stream& stream) override;                       // [3]  0x14012d2a0
    // The removal hook: the owner leaves the town's storage-location list
    // (CitizenBehavior +0x00), the list the state-2 enter appended it to.
    void          OnRemove() override;                                  // [7]  0x14012d270
    int         GetDefaultState() const override { return SmId(BuildingState::Built); }  // [16] 0x1400026c0
    Controller* GetController() override { return &_controller; }       // [17] 0x140004080

    // B4 WORK-b; body in Work/pickup_gate.cpp: the description's accept mask, else the
    // entity's StorageComponent's.
    int AcceptMask();                                                   // 0x14012d560

    StateController _controller;   // +0x28 the embedded controller (D6)
};

static_assert(sizeof(StorageLocationComponent) == 0x48, "StorageLocationComponent is 0x48 (create 0x140038df0)");
static_assert(alignof(StorageLocationComponent) == 8, "StorageLocationComponent alignment");
static_assert(offsetof(StorageLocationComponent, _controller) == 0x28,
              "StorageLocationComponent's controller member at +0x28 (slot 17 0x140004080)");
static_assert(offsetof(StorageLocationComponent, _controller) + offsetof(ControllerComponent::Controller, _owner) == 0x40,
              "controller back-pointer at +0x40 (ctor 0x14012d3bc)");
