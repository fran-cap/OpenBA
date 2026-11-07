#pragma once
// ============================================================================
// src/game/Component/ClearResourceComponent.h — ENDGAME B.3.8 block MI-A
//
// `ClearResourceComponent` as a real C++ class with a compiler-generated vtable and its
// state controller as an EMBEDDED MEMBER (B38_RULES.md D1-D6; there is no
// multiple inheritance). The engine/types POD is a forwarder to this file.
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §2 and §6, bytes read 2026-09-26):
//   reflected "ClearResourceComponent", parent "ControllerComponent", ti 0x1401c7238
//   create 0x1400315a0: MemAlloc(0x48, 8) + ctor 0x1401186a0
//   createInPlace = the imported `Internal::CreateInPlaceObject` (returns null);
//     the tree's in-place entry builds the object and is kept (B38 B8-13).
//   ctor 0x1401186a0: Component ctor; member at +0x28. No own field, no
//     flag OR (0x80).
//   dtor 0x140118640: restamp only, ~Internal (no own work).
//     [0] 0x140031770 = the dtor inlined + MemFree on &1.
//   table 0x140162840, 18 slots:
//     [0] 0x140031770 dtor  [1]/[2] TypeName/Type  [3] 0x1401185e0 Persist
//     [16] 0x1400026c0 `MOV EAX,2`  [17] 0x140004080 `LEA RAX,[RCX+0x28]`
//     every other slot = ControllerComponent's / Component's.
//   member table 0x1401627f0 (9 slots): the table-driven state controller over the
//     class's state table (ControllerLive::Dispatcher<ClearResourceCtrlTag>), stamped at
//     this+0x28 after the interface 0x14015f070; back-pointer at this+0x40.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Component/ControllerComponent.h"
#include "Component/controller_state_table.h"
#include "Object/exe_type.h"

struct ClearResourceCtrlTag { using Owner = class ClearResourceComponent; };   // the handlers' receiver (controller_state_table.h)

class ClearResourceComponent final : public ControllerComponent {
    DECLARE_EXE_TYPE(ClearResourceComponent)

public:
    using StateController = ::ControllerLive::TableController<ClearResourceCtrlTag>;

    ClearResourceComponent();                             // 0x1401186a0

    // 0x140118640
    ~ClearResourceComponent() override;                           // [0]  0x140031770
    void          Persist(IO::Stream& stream) override;                 // [3]  0x1401185e0
    int         GetDefaultState() const override { return SmId(BuildingState::Built); }  // [16] 0x1400026c0
    Controller* GetController() override { return &_controller; }  // [17] 0x140004080

    // state handler (the controller hands it its owner, node+0x18)
    char  State2Enter();                                          // 0x140118560

    // PUBLIC while the ported bodies read them at raw offsets (B38 decision 1).
    StateController _controller;   // +0x28 the embedded controller (D6)
};

static_assert(sizeof(ClearResourceComponent) == 0x48, "ClearResourceComponent is 0x48 (create 0x1400315a0)");
static_assert(alignof(ClearResourceComponent) == 8, "ClearResourceComponent alignment");
static_assert(offsetof(ClearResourceComponent, _controller) == 0x28,
              "ClearResourceComponent's controller member at +0x28 (slot 17 0x140004080: LEA RAX,[RCX+0x28])");
static_assert(offsetof(ClearResourceComponent, _controller) + offsetof(ControllerComponent::Controller, _owner) == 0x40,
              "controller back-pointer at +0x40 (ctor 0x1401186a0)");
