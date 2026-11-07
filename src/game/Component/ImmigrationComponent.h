#pragma once
// ============================================================================
// src/game/Component/ImmigrationComponent.h — ENDGAME B.3.8 block MI-A
//
// `ImmigrationComponent` as a real C++ class with a compiler-generated vtable and its
// state controller as an EMBEDDED MEMBER (B38_RULES.md D1-D6; there is no
// multiple inheritance). The engine/types POD is a forwarder to this file.
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §2 and §6, bytes read 2026-09-26):
//   reflected "ImmigrationComponent", parent "ControllerComponent", ti 0x1401c4480
//   create 0x140007a20: MemAlloc(0x68, 8) + ctor 0x14004e640
//   createInPlace = the imported `Internal::CreateInPlaceObject` (returns null);
//     the tree's in-place entry builds the object and is kept (B38 B8-13).
//   ctor 0x14004e640: Component ctor; member at +0x28; +0x48 = 0, +0x50 = 0
//     (dword), +0x58 = +0x60 = 0. No flag OR (0x80).
//   dtor 0x14004e5d0: MemFree(+0x58), +0x58 = +0x60 = 0 (~group, before ~_controller
//     in reverse declaration order), member restamp,
//     ~Internal. [0] 0x140007b70 = dtor + MemFree on &1.
//   table 0x140159b30, 18 slots:
//     [0] 0x140007b70 dtor  [1]/[2] TypeName/Type  [3] 0x14004e4b0 Persist
//     [16] 0x1400026c0 `MOV EAX,2`  [17] 0x140004080 `LEA RAX,[RCX+0x28]`
//     every other slot = ControllerComponent's / Component's.
//   member table 0x140159ae0 (9 slots): the table-driven state controller over the
//     class's state table (ControllerLive::Dispatcher<ImmigrationCtrlTag>), stamped at
//     this+0x28 after the interface 0x14015f070; back-pointer at this+0x40.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Component/ControllerComponent.h"
#include "Component/controller_state_table.h"
#include "Object/exe_type.h"
#include "system/Value.h"   // System::Time
#include "collection/Collection.h"   // Collection::Array (group)
#include "object/Internal.h"         // Object::InternalPtr<T>
namespace rtcoll { extern const ArraySpec kSpec_PtrZero; }   // 0x180008d60 (Entity/EntityList.h)

class ComponentEntity;

struct ImmigrationCtrlTag { using Owner = class ImmigrationComponent; };   // the handlers' receiver (controller_state_table.h)

class ImmigrationComponent final : public ControllerComponent {
    DECLARE_EXE_TYPE(ImmigrationComponent)

public:
    using StateController = ::ControllerLive::TableController<ImmigrationCtrlTag>;

    ImmigrationComponent();                               // 0x14004e640

    // 0x14004e5d0
    ~ImmigrationComponent() override;                             // [0]  0x140007b70
    void          Persist(IO::Stream& stream) override;                 // [3]  0x14004e4b0
    int         GetDefaultState() const override { return SmId(BuildingState::Built); }  // [16] 0x1400026c0
    Controller* GetController() override { return &_controller; }  // [17] 0x140004080

    // ---- non-virtual members (retail addresses; immigrationcomponent_states.cpp) ----
    bool CanAccept();                                     // 0x14004e3f0
    int  FindSpawnCell(int x, int y, int hallCell);       // 0x14004e270
    void Spawn();                                         // 0x14004dbe0
    void Dismiss();                                       // 0x14004d300
    void Accept();                                        // 0x14004d450
    // The state table's handlers (the controller hands them this component).
    char EnterIdle(StateMachine* sm);                             // 0x14004d290 state Built
    char TickIdle(StateMachine* sm);                              // 0x14004d220
    char EnterWaiting();                                  // 0x14004d1a0 state ImmigrationWaiting (0xf)
    char TickWaiting(StateMachine* sm, const System::Time* time);               // 0x14004d0d0
    char EnterArriving();                                 // 0x14004d090 state ImmigrationArriving (0x10)
    char TickArriving();                                  // 0x14004d070
    char OnArrived(StateMachine* sm);                             // 0x14004cff0 event ImmigrantsArrived
    void OnLeaveGroup();                                  // 0x14004cd00 transition to Demolish
    char EnterAnswer();                                   // 0x14004ceb0 state ImmigrationAnswer (0x11)
    char TickAnswer(StateMachine* sm);                            // 0x14004ce00
    char OnAllow(StateMachine* sm);                               // 0x14004cd90 event AllowImmigrants
    char OnDeny(StateMachine* sm);                                // 0x14004cd20 event DenyImmigrants

    // PUBLIC while the ported bodies read them (B38 decision 1). Types from Persist
    // 0x14004e4b0 (int +0x50; int count +0x60 with the +0x58 IBP array, each
    // validated against ComponentEntity; Time +0x48) and OnArrived 0x14004cff0.
    StateController   _controller;    // +0x28 the embedded controller (D6)
    System::Time      waitTimer;      // +0x48 counts down to the arrival season
    int32_t           arrivedCount;   // +0x50 nomads of the group at the hall
    uint8_t           _pad54[4];      // +0x54
    // Grows through retail's stride-8 rows (0x1400032e0 / 0x140003110: the new tail
    // ZEROED, no element destroy); freed by the inlined ~Array (dtor 0x14004e5f0..
    // 0x14004e600): kSpec_PtrZero.
    using GroupList = Collection::Array<Object::InternalPtr<ComponentEntity>, Collection::DefaultAllocator,
                                        Collection::ExplicitSpec<&rtcoll::kSpec_PtrZero>>;
    GroupList         group;          // +0x58 the nomad group (count +0x60, capacity +0x64)
};

static_assert(sizeof(ImmigrationComponent) == 0x68, "ImmigrationComponent is 0x68 (create 0x140007a20)");
static_assert(alignof(ImmigrationComponent) == 8, "ImmigrationComponent alignment");
static_assert(offsetof(ImmigrationComponent, _controller) == 0x28,
              "ImmigrationComponent's controller member at +0x28 (slot 17 0x140004080: LEA RAX,[RCX+0x28])");
static_assert(offsetof(ImmigrationComponent, _controller) + offsetof(ControllerComponent::Controller, _owner) == 0x40,
              "controller back-pointer at +0x40 (ctor 0x14004e640)");
static_assert(offsetof(ImmigrationComponent, waitTimer) == 0x48, "ImmigrationComponent +0x48");
static_assert(offsetof(ImmigrationComponent, arrivedCount) == 0x50, "ImmigrationComponent +0x50");
static_assert(offsetof(ImmigrationComponent, _pad54) == 0x54, "ImmigrationComponent +0x54");
static_assert(offsetof(ImmigrationComponent, group) == 0x58, "ImmigrationComponent +0x58");
static_assert(offsetof(ImmigrationComponent, group) + offsetof(ImmigrationComponent::GroupList, _size) == 0x60, "ImmigrationComponent +0x60");
static_assert(offsetof(ImmigrationComponent, group) + offsetof(ImmigrationComponent::GroupList, _capacity) == 0x64, "ImmigrationComponent +0x64");
