#pragma once
// ============================================================================
// src/game/Component/CemeteryComponent.h — ENDGAME B.3.8 block MI-A
//
// `CemeteryComponent` as a real C++ class with a compiler-generated vtable and its
// state controller as an EMBEDDED MEMBER (B38_RULES.md D1-D6; there is no
// multiple inheritance). The engine/types POD is a forwarder to this file.
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §2 and §6, bytes read 2026-09-26):
//   reflected "CemeteryComponent", parent "ControllerComponent", ti 0x1401c4090
//   create 0x140004040: MemAlloc(0x58, 8) + ctor 0x1400476f0
//   createInPlace = the imported `Internal::CreateInPlaceObject` (returns null);
//     the tree's in-place entry builds the object and is kept (B38 B8-13).
//   ctor 0x1400476f0: Component ctor; member at +0x28; +0x48 = +0x50 = 0;
//     `_flags |= 0x83`.
//   dtor 0x140047680: MemFree(+0x48), +0x48 = +0x50 = 0 (~Array of the plot list),
//     member restamp, ~Internal: reverse declaration order. [0] 0x140004220 = dtor + MemFree.
//   table 0x1401590a0, 18 slots:
//     [0] 0x140004220 dtor  [1]/[2] TypeName/Type  [3] 0x1400475c0 Persist
//     [4] 0x1400475b0 `RET 0`  [6] 0x140047200 OnAdded (build the plot grid)
//     [7] 0x1400471c0 OnRemove
//     [16] 0x1400026c0 `MOV EAX,2`  [17] 0x140004080 `LEA RAX,[RCX+0x28]`
//     every other slot = ControllerComponent's / Component's.
//   member table 0x140159050 (9 slots): the table-driven state controller over the
//     class's state table (ControllerLive::Dispatcher<CemeteryCtrlTag>), stamped at
//     this+0x28 after the interface 0x14015f070; back-pointer at this+0x40.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Component/ControllerComponent.h"
#include "Component/controller_state_table.h"
#include "Object/exe_type.h"
#include "collection/Collection.h"   // Collection::Array (plots)

struct CemeteryCtrlTag { using Owner = class CemeteryComponent; };   // the handlers' receiver (controller_state_table.h)

class CemeteryComponent final : public ControllerComponent {
    DECLARE_EXE_TYPE(CemeteryComponent)

public:
    using StateController = ::ControllerLive::TableController<CemeteryCtrlTag>;

    CemeteryComponent();                                  // 0x1400476f0

    // 0x140047680
    ~CemeteryComponent() override;                                // [0]  0x140004220
    void          Persist(IO::Stream& stream) override;                 // [3]  0x1400475c0
    void          Startup() override {}                           // [4]  0x1400475b0 `RET 0` (own)
    void          OnAdded() override;                             // [6]  0x140047200
    void          OnRemove() override;                            // [7]  0x1400471c0
    int         GetDefaultState() const override { return 2; }  // [16] 0x1400026c0
    Controller* GetController() override { return &_controller; }  // [17] 0x140004080

    // ---- non-virtual members (retail addresses) ----
    // The state handlers of the controller's table (RCX = [node+0x18] = this).
    char EnterBuilt();                                            // 0x140046fc0 state 2
    char EnterDemolish();                                         // 0x140046f80 state 3

    // PUBLIC while the ported bodies read them at raw offsets (B38 decision 1).
    StateController _controller;   // +0x28 the embedded controller (D6)
    // Grave-plot cell list, an Array<int32>: the plot-grid builder (slot 6)
    // fills it; the state-2 enter and Cemetery_PickFreeSlot_6c60 walk it. int32_t's
    // trait-derived row is exe 0x1400021d0's (stride 4, tail as-found, no destroy).
    Collection::Array<int32_t> plots;   // +0x48 cell indices (size +0x50, capacity +0x54)
};

static_assert(sizeof(CemeteryComponent) == 0x58, "CemeteryComponent is 0x58 (create 0x140004040)");
static_assert(alignof(CemeteryComponent) == 8, "CemeteryComponent alignment");
static_assert(offsetof(CemeteryComponent, _controller) == 0x28,
              "CemeteryComponent's controller member at +0x28 (slot 17 0x140004080: LEA RAX,[RCX+0x28])");
static_assert(offsetof(CemeteryComponent, _controller) + offsetof(ControllerComponent::Controller, _owner) == 0x40,
              "controller back-pointer at +0x40 (ctor 0x1400476f0)");
static_assert(offsetof(CemeteryComponent, plots) == 0x48, "CemeteryComponent +0x48 plots");
static_assert(offsetof(CemeteryComponent, plots) + offsetof(Collection::Array<int32_t>, _size) == 0x50, "CemeteryComponent +0x50");
static_assert(offsetof(CemeteryComponent, plots) + offsetof(Collection::Array<int32_t>, _capacity) == 0x54, "CemeteryComponent +0x54");

// The state-2 enter's zone test on a free plot's cell (0x140047100 MOVZX ECX,[RDI+0x29];
// 0x140047104 SHL EAX,CL; 0x140047106 TEST EAX,0x1e0): zones 5..8 are walkable.
constexpr bool CemeteryWalkableZone(uint8_t zone) {
    // SHL masks its count to 5 bits; the mask keeps a saved zone byte >= 32 defined (review F-161).
    return ((1u << (zone & 0x1f)) & 0x1e0u) != 0;
}
