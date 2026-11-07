#pragma once
// ============================================================================
// src/game/Component/PastureComponent.h — ENDGAME B.3.8 block MI-B
//
// `PastureComponent` (a pasture: its livestock, capacity and herder commands) as a
// real C++ class with a compiler-generated vtable and its state controller as an
// EMBEDDED MEMBER at +0x48, after the class's own 0x20 bytes (B38_RULES.md D1-D6;
// there is no multiple inheritance). The engine/types POD is a forwarder.
//
// RETAIL (B38_SLOTS.md §2 / §6, bytes read 2026-09-26):
//   reflected "PastureComponent", parent "ControllerComponent", ti 0x1401c7ba0
//   create 0x140036730: MemAlloc(0x68, 8) + ctor 0x1401280e0
//   ctor 0x1401280e0: Component ctor; +0x28 = +0x30 = +0x38 = 0 (qwords); +0x44 = 0
//     (dword; +0x40 is not written); member at +0x48 (0x14015f070, 0x140163f10),
//     back-pointer +0x60; `_flags |= 0x8b`.
//   dtor 0x140128070: member restamp; MemFree(+0x30), +0x30 = +0x38 = 0 (the animal
//     list: ~Array of `animals`, after ~_controller in reverse declaration order); restamps; ~Internal. [0] 0x140036910 = dtor + MemFree on &1.
//   table 0x140163f60, 18 slots: [3] 0x140127f40 Persist, [4] 0x140127a50 Startup
//     (`MOV qword [RCX+0x28],0`), [6] 0x140127a30 OnAdded, [7] 0x1401277e0 OnRemove
//     (release every animal), [12] 0x140127540 OnEvent (gate-cell claim), [13]
//     0x140127dd0 CommandMask, [14] 0x140127a60 Command, [15] 0x140127720 OnMessage,
//     [16] 0x1400026c0 `MOV EAX,2`, [17] 0x140036770 `LEA RAX,[RCX+0x48]`.
//   The tree's slots 18-21 were PastureDescription's table 0x140163ff0 read past
//     this table's end (B38 D3.5): gone.
//   member table 0x140163f10: the table-driven controller over the class's state
//     table (ControllerLive::Dispatcher<PastureCtrlTag>).
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Component/ControllerComponent.h"
#include "Component/controller_state_table.h"
#include "Object/exe_type.h"
#include "collection/Collection.h"   // Collection::Array (animals)
#include "object/Internal.h"         // Object::InternalPtr<T>

class ComponentDescription;
class LivestockComponent;
class AnimalComponent;

// The animal list grows through retail's stride-8 row (0x1400032e0 / 0x140003110: stride
// 8, align 8, the new tail ZEROED, no element destroy) and is freed by the inlined ~Array
// (dtor 0x1401280a3: MemFree, then +0x30 = +0x38 = 0): kSpec_PtrZero (Entity/EntityList.h).
namespace rtcoll { extern const ArraySpec kSpec_PtrZero; }   // 0x180008d60

struct PastureCtrlTag { using Owner = class PastureComponent; };   // the handlers' receiver (controller_state_table.h)

class PastureComponent final : public ControllerComponent {
    DECLARE_EXE_TYPE(PastureComponent)

public:
    using StateController = ::ControllerLive::TableController<PastureCtrlTag>;

    PastureComponent();                                                  // 0x1401280e0

    // 0x140128070
    ~PastureComponent() override;                                       // [0]  0x140036910
    void          Persist(IO::Stream& stream) override;                       // [3]  0x140127f40
    void          Startup() override;                                   // [4]  0x140127a50
    void          OnAdded() override;                                   // [6]  0x140127a30
    void          OnRemove() override;                                  // [7]  0x1401277e0
    void          OnEvent(int eventId) override;                        // [12] 0x140127540
    uint32_t      CommandMask(int command) override;                    // [13] 0x140127dd0
    void          Command(int command) override;                        // [14] 0x140127a60
    void          OnMessage(int message, void* data,
                            uint32_t arg0, uint32_t arg1) override;     // [15] 0x140127720
    int         GetDefaultState() const override { return SmId(BuildingState::Built); }          // [16] 0x1400026c0
    Controller* GetController() override { return &_controller; }       // [17] 0x140036770

    // ---- non-virtual members (retail addresses; pasturecomponent*.cpp) ----
    int  ComputeCapacity() const;                                  // 0x140128570
    void AdoptWanderers();                                         // 0x140127140
    void Reproduce();                                              // 0x140127370
    void ReleaseAllAnimals();                                      // 0x140126e40 (= slot 7's body)
    void SpawnLivestock(AnimalComponent* const* parent);           // 0x140128670
    int  FindOldestLivestock() const;                              // 0x140128350 the oldest cullable animal's index, -1 if none
    // The state table's handlers (the controller hands them this component).
    char EnterOperational();                                       // 0x1401270c0 state Built
    char TickOperational();                                        // 0x1401267e0
    char EnterTeardown();                                          // 0x140126e40 state Demolish

    // PUBLIC while the ported bodies read them (B38 decision 1). Types from Persist
    // 0x140127f40 (ExternalPtr +0x28; the +0x30 InternalPtr array validated against
    // LivestockComponent; int +0x40, int +0x44).
    ComponentDescription* livestockType;    // +0x28 ExternalPtr: the template the pasture keeps (Startup clears it)
    using AnimalList = Collection::Array<Object::InternalPtr<LivestockComponent>, Collection::DefaultAllocator,
                                         Collection::ExplicitSpec<&rtcoll::kSpec_PtrZero>>;
    AnimalList            animals;          // +0x30 Array<InternalPtr<LivestockComponent>> (count +0x38, capacity +0x3c)
    int32_t               capacity;         // +0x40 ComputeCapacity (OnAdded); not written by the ctor
    int32_t               production;       // +0x44 credited by LivestockComponent::Update, spent by TickOperational
    StateController _controller;   // +0x48 the embedded controller (D6)
};

static_assert(sizeof(PastureComponent) == 0x68, "PastureComponent is 0x68 (create 0x140036730)");
static_assert(alignof(PastureComponent) == 8, "PastureComponent alignment");
static_assert(offsetof(PastureComponent, livestockType) == 0x28, "PastureComponent +0x28");
static_assert(offsetof(PastureComponent, animals) == 0x30, "PastureComponent +0x30");
static_assert(offsetof(PastureComponent, animals) + offsetof(PastureComponent::AnimalList, _size) == 0x38, "PastureComponent +0x38");
static_assert(offsetof(PastureComponent, animals) + offsetof(PastureComponent::AnimalList, _capacity) == 0x3c, "PastureComponent +0x3c");
static_assert(offsetof(PastureComponent, capacity) == 0x40, "PastureComponent +0x40");
static_assert(offsetof(PastureComponent, production) == 0x44, "PastureComponent +0x44 (ctor 0x140128123)");
static_assert(offsetof(PastureComponent, _controller) == 0x48,
              "PastureComponent's controller member at +0x48 (slot 17 0x140036770: LEA RAX,[RCX+0x48])");
static_assert(offsetof(PastureComponent, _controller) + offsetof(ControllerComponent::Controller, _owner) == 0x60,
              "controller back-pointer at +0x60 (ctor 0x14012814b)");
