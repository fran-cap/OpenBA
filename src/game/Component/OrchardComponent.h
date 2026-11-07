#pragma once
// ============================================================================
// src/game/Component/OrchardComponent.h — ENDGAME B.3.8 block MI-B
//
// `OrchardComponent` (an orchard: its tree slots and the plant/tend/harvest/cut
// cycle) as a real C++ class with a compiler-generated vtable and its state
// controller as an EMBEDDED MEMBER (B38_RULES.md D1-D6). The engine/types POD is
// a forwarder.
//
// RETAIL (B38_SLOTS.md §2 / §6, bytes read 2026-09-26):
//   reflected "OrchardComponent", parent "ControllerComponent", ti 0x1401c46b0
//   create 0x140009870: MemAlloc(0x68, 8) + ctor 0x140051f40
//   ctor 0x140051f40: Component ctor; member at +0x28 (0x14015f070, 0x14015a130),
//     back-pointer +0x40; +0x48 = 0 (byte); `MOV dword [RBX+0x4c],-1`; +0x50 =
//     +0x58 = +0x60 = 0; `_flags |= 0x80`.
//   dtor 0x140051ed0: MemFree(+0x50), +0x50 = +0x58 = 0 (the slot array), restamps,
//     ~Internal. [0] 0x140009a70 = dtor + MemFree on &1.
//   table 0x14015a180, 18 slots: [3] 0x140051de0 Persist, [4] 0x140051dd0 Startup
//     (`MOV qword [RCX+0x60],0`), [13] 0x140051c90 CommandMask, [14] 0x140051ba0
//     Command, [15] 0x140051990 OnMessage, [16] 0x1400026c0 `MOV EAX,2`, [17]
//     0x140004080 `LEA RAX,[RCX+0x28]`; every other slot = ControllerComponent's.
//   The tree's slots 18-21 were OrchardDescription's table 0x14015a210 read past
//     this table's end (B38 D3.5): gone.
//   member table 0x14015a130: the table-driven controller over the class's state
//     table (ControllerLive::Dispatcher<OrchardCtrlTag>).
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Component/ControllerComponent.h"
#include "Component/controller_state_table.h"
#include "Object/exe_type.h"

class ComponentEntity;
class NaturalResourceComponent;
class FruitComponent;
class ComponentDescription;

struct OrchardCtrlTag { using Owner = class OrchardComponent; };   // the handlers' receiver (controller_state_table.h)

// OrchardSlot::state (+0x04). Values are retail's: the grid is laid out Empty, the msg-0 bind sets
// Planted (tree + resource + fruit bound) or Hole (unusable), and ValidateRefs demotes a stale
// Planted slot to Hole. OrchardHoleCount counts the Holes for the replant.
enum class OrchardSlotState : int32_t {
    Empty   = 0,
    Planted = 1,
    Hole    = 2,
};

// One tree position of the orchard, stride 0x28 (Array element of +0x50; resized by
// 0x140009f80, serialized by 0x14000a230 as int, int, three InternalPtrs, bool).
struct OrchardSlot {
    int32_t                   tile;       // +0x00 cell index into the Map's cell array
    OrchardSlotState          state;      // +0x04 empty / planted (bound) / hole
    Object::InternalPtr<ComponentEntity>          tree;       // +0x08 the tree on the cell
    Object::InternalPtr<NaturalResourceComponent> resource;   // +0x10
    Object::InternalPtr<FruitComponent>           fruit;      // +0x18
    bool                      picked;     // +0x20 harvested this season
    uint8_t                   _pad21[7];  // +0x21
};
static_assert(sizeof(OrchardSlot) == 0x28, "OrchardSlot stride 0x28 (0x140009f80)");
static_assert(offsetof(OrchardSlot, tile) == 0x0, "OrchardSlot::tile");
static_assert(offsetof(OrchardSlot, _pad21) == 0x21, "OrchardSlot::_pad21");
static_assert(offsetof(OrchardSlot, state) == 0x04, "OrchardSlot +0x04");
static_assert(sizeof(OrchardSlotState) == 4, "OrchardSlot::state is an int32_t");
static_assert(offsetof(OrchardSlot, tree) == 0x08, "OrchardSlot +0x08");
static_assert(offsetof(OrchardSlot, resource) == 0x10, "OrchardSlot +0x10");
static_assert(offsetof(OrchardSlot, fruit) == 0x18, "OrchardSlot +0x18");
static_assert(offsetof(OrchardSlot, picked) == 0x20, "OrchardSlot +0x20");

class OrchardComponent final : public ControllerComponent {
    DECLARE_EXE_TYPE(OrchardComponent)

public:
    using StateController = ::ControllerLive::TableController<OrchardCtrlTag>;

    OrchardComponent();                                                  // 0x140051f40

    // 0x140051ed0
    ~OrchardComponent() override;                                       // [0]  0x140009a70
    void          Persist(IO::Stream& stream) override;                       // [3]  0x140051de0
    void          Startup() override;                                   // [4]  0x140051dd0
    uint32_t      CommandMask(int command) override;                    // [13] 0x140051c90
    void          Command(int command) override;                        // [14] 0x140051ba0
    void          OnMessage(int message, void* data,
                            uint32_t arg0, uint32_t arg1) override;     // [15] 0x140051990
    int         GetDefaultState() const override { return SmId(BuildingState::Built); }          // [16] 0x1400026c0
    Controller* GetController() override { return &_controller; }       // [17] 0x140004080

    // ---- non-virtual members (retail addresses; orchardcomponent*.cpp) ----
    void  SetSlotCount(int count);                      // 0x140009f80 Array<OrchardSlot>::SetSize on +0x50
    void  ValidateRefs();                               // 0x140051830
    float GetGrowth() const;                            // 0x140051640
    // The state table's handlers (the controller hands them this component).
    char EnterIdle();                                   // 0x140051530 state Built
    char TickIdle(StateMachine* sm);                            // 0x1400514c0
    char OnPlantEvent(StateMachine* sm);                        // 0x140051450 event Plant
    char EnterPlanting();                               // 0x140051070 state Plant
    char TickPlanting(StateMachine* sm);                        // 0x140050f90
    char OnHarvestEvent(StateMachine* sm);                      // 0x140050900 event Harvest
    char OnClearEvent(StateMachine* sm);                        // 0x1400513e0 event Clear
    char TryHarvest(StateMachine* sm);                          // 0x140051580
    char EnterTending(StateMachine* sm);                        // 0x140050e30 state Tend
    char UpdateTending();                               // 0x140050ba0 (Replant's 0x1400509b0 is the same body)
    char TickTending(StateMachine* sm);                         // 0x140050c00
    char EnterReplanting();                             // 0x140050ae0 state Replant
    char TickReplanting(StateMachine* sm);                      // 0x140050a10
    char EnterHarvesting();                             // 0x140050800 state Harvest
    char TickHarvesting(StateMachine* sm);                      // 0x140050650
    char EnterClearing();                               // 0x1400504e0 state OrchardClear
    char UpdateClearing();                              // 0x1400503e0
    char TickClearing(StateMachine* sm);                        // 0x140050430

    // PUBLIC while the ported bodies read them (B38 decision 1). Types from Persist
    // 0x140051de0 (bool +0x48, int +0x4c, the slot array, ExternalPtr +0x60).
    StateController   _controller;      // +0x28 the embedded controller (D6)
    bool              enabled;          // +0x48 work switch (panel command 2)
    uint8_t           _pad49[3];        // +0x49
    int32_t           lastHarvestYear;  // +0x4c -1 = never (retail ctor)
    OrchardSlot*      slots;            // +0x50 Array<OrchardSlot> data (freed by the dtor)
    int32_t           slotCount;        // +0x58
    int32_t           slotCapacity;     // +0x5c
    ComponentDescription* treeType;     // +0x60 ExternalPtr: the tree template chosen in OrchardUI (Startup clears it)
};

static_assert(sizeof(OrchardComponent) == 0x68, "OrchardComponent is 0x68 (create 0x140009870)");
static_assert(offsetof(OrchardComponent, _pad49) == 0x49, "OrchardComponent::_pad49");
static_assert(alignof(OrchardComponent) == 8, "OrchardComponent alignment");
static_assert(offsetof(OrchardComponent, _controller) == 0x28,
              "OrchardComponent's controller member at +0x28 (slot 17 0x140004080)");
static_assert(offsetof(OrchardComponent, _controller) + offsetof(ControllerComponent::Controller, _owner) == 0x40,
              "controller back-pointer at +0x40 (ctor 0x140051f9c)");
static_assert(offsetof(OrchardComponent, enabled) == 0x48, "OrchardComponent +0x48");
static_assert(offsetof(OrchardComponent, lastHarvestYear) == 0x4c, "OrchardComponent +0x4c (ctor 0x140051fa3)");
static_assert(offsetof(OrchardComponent, slots) == 0x50, "OrchardComponent +0x50");
static_assert(offsetof(OrchardComponent, slotCount) == 0x58, "OrchardComponent +0x58");
static_assert(offsetof(OrchardComponent, slotCapacity) == 0x5c, "OrchardComponent +0x5c");
static_assert(offsetof(OrchardComponent, treeType) == 0x60, "OrchardComponent +0x60");
