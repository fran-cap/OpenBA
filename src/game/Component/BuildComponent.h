#pragma once
// ============================================================================
// src/game/Component/BuildComponent.h — ENDGAME B.3.8 block MI-A
//
// `BuildComponent` as a real C++ class with a compiler-generated vtable and its
// state controller as an EMBEDDED MEMBER (B38_RULES.md D1-D6; there is no
// multiple inheritance). The engine/types POD is a forwarder to this file.
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §2 and §6, bytes read 2026-09-26):
//   reflected "BuildComponent", parent "ControllerComponent", ti 0x1401c7008
//   create 0x14002f2b0: MemAlloc(0x78, 8) + ctor 0x140108fc0
//   createInPlace = the imported `Internal::CreateInPlaceObject` (returns null);
//     the tree's in-place entry builds the object and is kept (B38 B8-13).
//   ctor 0x140108fc0: Component ctor; member at +0x28; +0x48 = 0, +0x4c = 1,
//     +0x50 = 0 (byte), +0x58..+0x70 = 0; `_flags |= 0x83`.
//   dtor 0x140108f30: MemFree(+0x68), +0x68 = +0x70 = 0; MemFree(+0x58),
//     +0x58 = +0x60 = 0; member restamp; ~Internal. [0] 0x14002f4d0 = dtor + MemFree.
//   table 0x140162260, 18 slots:
//     [0] 0x14002f4d0 dtor  [1]/[2] TypeName/Type  [3] 0x140108d80 Persist
//     [4] 0x140108d60 `RET 0`  [6] 0x140108d40 OnAdded  [7] 0x140108cb0 OnRemove
//     [11] 0x140108d70 `RET 0`  [15] 0x140108710 OnMessage (labour credit)
//     [16] 0x14002f2f0 `MOV EAX,1`  [17] 0x140004080 `LEA RAX,[RCX+0x28]`
//     every other slot = ControllerComponent's / Component's.
//   member table 0x140162210 (9 slots): the table-driven state controller over the
//     class's state table (ControllerLive::Dispatcher<BuildCtrlTag>), stamped at
//     this+0x28 after the interface 0x14015f070; back-pointer at this+0x40.
//
// B.4 W0-H2 fields (retail ctor 0x140108fc0, dtor 0x140108f30, Persist
// 0x140108d80, OnMessage 0x140108710; Persist passes no key names):
//   +0x48 int32 laborProgress   Serialize(int&); OnMessage(0) is `+0x48 += 1`
//                               (one unit of labour delivered); ctor 0
//   +0x4C int32 costMultiplier  Serialize(int&); ctor 1; the state-1 body sets
//                               it from the site's multiplier; completion is
//                               laborProgress reaching (materials x this)
//   +0x50 bool  buildFlag       Serialize(bool&); ctor 0 (meaning not settled)
//   +0x58 Collection::Array<InternalPtr<ModelComponent>>: data +0x58, count
//         +0x60, capacity +0x64 (Persist: count via Serialize(int&), SetSize
//         0x1400032e0, each element InternalBasePtr::Serialize + IsDerivedFrom
//         ModelComponent's type id 0x1401c7a50)
//   +0x68 Collection::Array<InternalPtr<PickingComponent>>: data +0x68, count
//         +0x70, capacity +0x74 (the same, PickingComponent's type id)
//   Both are Collection::Array members over kSpec_PtrZero (the 0x1400032e0 /
//   0x140003110 row: stride 8, align 8, new tail zeroed, no element destroy); the dtor
//   body is empty and reverse declaration order frees +0x68, then +0x58, then
//   restamps the +0x28 controller, retail's order (0x140108f55..0x140108f8b).
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Component/ControllerComponent.h"
#include "Component/controller_state_table.h"
#include "Object/exe_type.h"
#include "collection/Collection.h"   // Collection::Array (the two component lists)

struct BuildCtrlTag { using Owner = class BuildComponent; };   // the handlers' receiver (controller_state_table.h)
class ModelComponent;     // Component/ModelComponent.h
class PickingComponent;   // Component/PickingComponent.h
namespace rtcoll { extern const ArraySpec kSpec_PtrZero; }   // 0x180008d60 (Entity/EntityList.h)

class BuildComponent final : public ControllerComponent {
    DECLARE_EXE_TYPE(BuildComponent)

public:
    using StateController = ::ControllerLive::TableController<BuildCtrlTag>;

    BuildComponent();                                     // 0x140108fc0

    // 0x140108f30
    ~BuildComponent() override;                                   // [0]  0x14002f4d0
    void          Persist(IO::Stream& stream) override;                 // [3]  0x140108d80
    void          Startup() override {}                           // [4]  0x140108d60 `RET 0` (own)
    void          OnAdded() override;                             // [6]  0x140108d40
    void          OnRemove() override;                            // [7]  0x140108cb0
    void          Slot11() override {}                            // [11] 0x140108d70 `RET 0` (own)
    void          OnMessage(int message, void* data,
                            uint32_t arg0, uint32_t arg1) override;  // [15] 0x140108710
    int         GetDefaultState() const override { return 1; }  // [16] 0x14002f2f0
    Controller* GetController() override { return &_controller; }  // [17] 0x140004080
    // 0x140109390 (B4 DLG-a; defined in Dialog/entityui.cpp): the description's
    // +0x30 cost times costMultiplier <= laborProgress (`CMP [RDI+0x48],EDX; SETGE`).
    bool IsBuildComplete() const;

    // ---- non-virtual members (retail addresses) ----
    // 0x140108720: the OnAdded body (slot 6 is a thunk that skips it while the
    // instant-build bit is set); State8Enter's instant branch calls it directly.
    void FillModelLists();
    bool HaveAllMaterials();                                      // 0x140108630
    // The state handlers of the controller's table. The node dispatchers call
    // them as `MOV RCX,[node+0x18]; JMP h` with RDX = the state machine: RCX is
    // the controller's back-pointer (+0x40), i.e. this component.
    int  State1Enter(StateMachine* sm);                                   // 0x140108290
    char State1Update(StateMachine* sm);                                  // 0x1401081e0 (per frame)
    char State8Enter(StateMachine* sm);                                   // 0x140107ae0
    char State8Update(StateMachine* sm);                                  // 0x1401076e0 (per frame)
    char State8OneShot(StateMachine* sm);                                 // 0x140107600 (state 8 exit)
    char State8To2Action();                                       // 0x1401075e0 (the 8->2 action)
    void State8Finalize();                                        // 0x140107ff0

    // PUBLIC while the ported bodies read them at raw offsets (B38 decision 1).
    StateController _controller;   // +0x28 the embedded controller (D6)
    int32_t         laborProgress;   // +0x48 labour units delivered (OnMessage 0)
    int32_t         costMultiplier;  // +0x4c cost multiplier (retail ctor: 1)
    bool            buildFlag;       // +0x50 (serialized bool)
    uint8_t         _pad51[7];       // +0x51
    using ModelList   = Collection::Array<Object::InternalPtr<ModelComponent>, Collection::DefaultAllocator,
                                          Collection::ExplicitSpec<&rtcoll::kSpec_PtrZero>>;
    using PickingList = Collection::Array<Object::InternalPtr<PickingComponent>, Collection::DefaultAllocator,
                                          Collection::ExplicitSpec<&rtcoll::kSpec_PtrZero>>;
    ModelList   modelComponents;     // +0x58 (count +0x60, capacity +0x64)
    PickingList pickingComponents;   // +0x68 (count +0x70, capacity +0x74)
};

static_assert(sizeof(BuildComponent) == 0x78, "BuildComponent is 0x78 (create 0x14002f2b0)");
static_assert(alignof(BuildComponent) == 8, "BuildComponent alignment");
static_assert(offsetof(BuildComponent, _controller) == 0x28,
              "BuildComponent's controller member at +0x28 (slot 17 0x140004080: LEA RAX,[RCX+0x28])");
static_assert(offsetof(BuildComponent, _controller) + offsetof(ControllerComponent::Controller, _owner) == 0x40,
              "controller back-pointer at +0x40 (ctor 0x140108fc0)");
static_assert(offsetof(BuildComponent, laborProgress) == 0x48, "BuildComponent::laborProgress @ +0x48 (Serialize int&)");
static_assert(offsetof(BuildComponent, costMultiplier) == 0x4c, "BuildComponent::costMultiplier @ +0x4c (Serialize int&)");
static_assert(offsetof(BuildComponent, buildFlag) == 0x50, "BuildComponent::buildFlag @ +0x50 (Serialize bool&)");
static_assert(offsetof(BuildComponent, _pad51) == 0x51, "BuildComponent +0x51 padding");
static_assert(offsetof(BuildComponent, modelComponents) == 0x58, "BuildComponent model array data @ +0x58");
static_assert(offsetof(BuildComponent, modelComponents) + offsetof(BuildComponent::ModelList, _size) == 0x60, "BuildComponent model array count @ +0x60");
static_assert(offsetof(BuildComponent, modelComponents) + offsetof(BuildComponent::ModelList, _capacity) == 0x64, "BuildComponent model array capacity @ +0x64");
static_assert(offsetof(BuildComponent, pickingComponents) == 0x68, "BuildComponent picking array data @ +0x68");
static_assert(offsetof(BuildComponent, pickingComponents) + offsetof(BuildComponent::PickingList, _size) == 0x70, "BuildComponent picking array count @ +0x70");
static_assert(offsetof(BuildComponent, pickingComponents) + offsetof(BuildComponent::PickingList, _capacity) == 0x74, "BuildComponent picking array capacity @ +0x74");
