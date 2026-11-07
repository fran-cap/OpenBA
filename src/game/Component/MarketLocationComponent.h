#pragma once
// ============================================================================
// src/game/Component/MarketLocationComponent.h — ENDGAME B.3.8 block MI-B
//
// `MarketLocationComponent` (registers a market as a town market location) as a
// real C++ class with a compiler-generated vtable and its state controller as an
// EMBEDDED MEMBER (B38_RULES.md D1-D6, D9 #8: the tree had none). No fields of its
// own. The engine/types POD is a forwarder.
//
// RETAIL (B38_SLOTS.md §2 / §6, bytes read 2026-09-26):
//   reflected "MarketLocationComponent", parent "ControllerComponent", ti 0x1401c4560
//   create 0x1400086c0: MemAlloc(0x48, 8) + ctor 0x14004f060
//   ctor 0x14004f060: Component ctor; member at +0x28 (0x14015f070, 0x140159d80),
//     back-pointer +0x40; `_flags |= 2`.
//   [0] 0x140008900: restamps + ~Internal + MemFree on &1 (no body of its own).
//   table 0x140159dd0, 18 slots: [3] 0x14004efa0 Persist, [7] 0x14004ef60 OnRemove
//     (Ghidra "OnCreate": erase the owner from the town's market list), [16]
//     0x1400026c0 `MOV EAX,2`, [17] 0x140004080 `LEA RAX,[RCX+0x28]`; every other
//     slot = ControllerComponent's / Component's.
//   member table 0x140159d80 (9 slots: FindState 0x140008b40 over the table at
//     0x1401cdf30, count 0x1401ceeb0; HasStates 0x140008d10 reads the "built" flag
//     0x1401ceeb8): the table-driven controller over the class's state table
//     (ControllerLive::Dispatcher<MarketLocationCtrlTag>), built by
//     MarketLocationDescription_SetupCrossRefs 0x14014b220.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Component/ControllerComponent.h"
#include "Component/controller_state_table.h"
#include "Object/exe_type.h"

struct MarketLocationCtrlTag { using Owner = class MarketLocationComponent; };   // the handlers' receiver (controller_state_table.h)

class MarketLocationComponent final : public ControllerComponent {
    DECLARE_EXE_TYPE(MarketLocationComponent)

public:
    using StateController = ::ControllerLive::TableController<MarketLocationCtrlTag>;

    MarketLocationComponent();                                           // 0x14004f060

    // 0x140008900: no body
    ~MarketLocationComponent() override = default;                      // [0]  0x140008900
    void          Persist(IO::Stream& stream) override;                       // [3]  0x14004efa0
    void          OnRemove() override;                                  // [7]  0x14004ef60
    int         GetDefaultState() const override { return SmId(BuildingState::Built); }          // [16] 0x1400026c0
    Controller* GetController() override { return &_controller; }       // [17] 0x140004080

    StateController _controller;   // +0x28 the embedded controller (D6)
};

static_assert(sizeof(MarketLocationComponent) == 0x48, "MarketLocationComponent is 0x48 (create 0x1400086c0)");
static_assert(alignof(MarketLocationComponent) == 8, "MarketLocationComponent alignment");
static_assert(offsetof(MarketLocationComponent, _controller) == 0x28,
              "MarketLocationComponent's controller member at +0x28 (slot 17 0x140004080)");
static_assert(offsetof(MarketLocationComponent, _controller) + offsetof(ControllerComponent::Controller, _owner) == 0x40,
              "controller back-pointer at +0x40 (ctor 0x14004f0bc)");
