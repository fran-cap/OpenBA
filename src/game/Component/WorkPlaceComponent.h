#pragma once
// ============================================================================
// src/game/Component/WorkPlaceComponent.h — ENDGAME B.3.8 block MI-B
//
// `WorkPlaceComponent` (a building's job roster and its profession-registry key)
// as a real C++ class with a compiler-generated vtable and its state controller
// as an EMBEDDED MEMBER (B38_RULES.md D1-D6). The engine/types POD is a forwarder.
//
// RETAIL (B38_SLOTS.md §2 / §6, bytes read 2026-09-26):
//   reflected "WorkPlaceComponent", parent "ControllerComponent", ti 0x1401c80e0
//   create 0x14003a030: MemAlloc(0x68, 8) + ctor 0x14012f2e0
//   ctor 0x14012f2e0: Component ctor; member at +0x28 (0x14015f070, 0x140164ce0),
//     back-pointer +0x40; +0x48 = 0; `MOV byte [RBX+0x50],1`; +0x58 = +0x60 = 0;
//     `_flags |= 2`.
//   dtor 0x14012f270: MemFree(+0x58), +0x58 = +0x60 = 0 (the roster: ~workers, before
//     ~_controller in reverse declaration order), restamps,
//     ~Internal. [0] 0x14003a220 = dtor + MemFree on &1.
//   table 0x140164d30, 18 slots: [3] 0x14012f120 Persist, [7] 0x14012f090 OnRemove,
//     [16] 0x140003730 `XOR EAX,EAX` (default state 0), [17] 0x140004080
//     `LEA RAX,[RCX+0x28]`; every other slot = ControllerComponent's / Component's.
//   member table 0x140164ce0: the table-driven controller over the class's state
//     table (ControllerLive::Dispatcher<WPCCtrlTag>).
//
// B.4 W0-H2 fields (retail ctor 0x14012f2e0, dtor 0x14012f270, Persist
// 0x14012f120; Persist passes no key names):
//   +0x48 ExternalPtr<Profession> profession: Persist version >= 1 serializes
//         it with ExternalPtr<Profession>::Serialize (0x140018020, type gate
//         TypeInfo_Profession); version 0 copies the description's +0x20.
//         Declared as a plain Profession* (layout-identical; the tree's ctor
//         zeroes it, as retail's does).
//   +0x50 bool  isAccepting: Serialize(bool&); ctor `MOV byte [+0x50],1`.
//   +0x58 Collection::Array<InternalPtr<CitizenComponent>> the worker roster:
//         data +0x58, count +0x60, capacity +0x64 (Persist: count via
//         Serialize(int&), SetSize 0x1400032e0, each element
//         InternalBasePtr::Serialize + IsDerivedFrom CitizenComponent). A real
//         Array member over kSpec_PtrZero (B.4 H5): ~workers is the dtor's free.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Component/ControllerComponent.h"
#include "Component/controller_state_table.h"
#include "Object/exe_type.h"
#include "collection/Collection.h"   // Collection::Array (workers)
#include "object/Internal.h"         // Object::InternalPtr<T>

// The roster grows through retail's stride-8 rows (0x1400032e0 / 0x140003110: stride
// 8, align 8, the new tail ZEROED, no element destroy) and is freed by the inlined
// ~Array (dtor 0x14012f290..0x14012f2a0): kSpec_PtrZero (Entity/EntityList.h).
namespace rtcoll { extern const ArraySpec kSpec_PtrZero; }   // 0x180008d60

struct WPCCtrlTag { using Owner = class WorkPlaceComponent; };   // the handlers' receiver (controller_state_table.h)
class Profession;         // Game/Profession.h
class CitizenComponent;   // Component/CitizenComponent.h

class WorkPlaceComponent final : public ControllerComponent {
    DECLARE_EXE_TYPE(WorkPlaceComponent)

public:
    using StateController = ::ControllerLive::TableController<WPCCtrlTag>;

    WorkPlaceComponent();                                                // 0x14012f2e0

    // 0x14012f270
    ~WorkPlaceComponent() override;                                     // [0]  0x14003a220
    void          Persist(IO::Stream& stream) override;                       // [3]  0x14012f120
    void          OnRemove() override;                                  // [7]  0x14012f090
    int         GetDefaultState() const override { return SmId(BuildingState::ClearSite); }  // [16] 0x140003730
    Controller* GetController() override { return &_controller; }       // [17] 0x140004080
    // Defined in destroycomponent_workerrelease.cpp (B4 COMP-c).
    void SetMaxWorkers(int newMax);                                     // 0x14012fb00
    void EnterBuilderProfession();                                      // 0x14012f520

    // ---- non-virtual members (retail addresses; B.4 COMP-f) ----
    void ReleaseWorkers();                                              // 0x14012ee60
    void LeaveBuilderProfession();                                      // 0x14012f670
    void Detach(Object::InternalPtr<CitizenComponent>* worker);         // 0x14012f7e0
    void Attach(Object::InternalPtr<CitizenComponent>* worker);         // 0x14012fa20
    void SetAccepting(bool accepting);                                  // 0x14012fc00
    // The controller's state handlers (table 0x14012f3a0; RCX = this).
    char EnterConstruction();                                           // 0x14012ee40 state 0
    char EnterBuilt();                                                  // 0x14012edf0 state 2
    char EnterDemolish();                                               // 0x14012ed60 state 3
    // The tree's own (no retail body): StartTown's occupied-house visual.
    void ApplyOccupiedVisual();
    // B4 TUT; body in Tutorial/tutorial_base.cpp.
    int GetMaxWorkers() const;                                          // 0x14012fbb0

private:
    void EvictRoster();                                                 // 0x14012fc00's pause tail (B5)

public:

    // PUBLIC while the ported bodies read them at raw offsets (B38 decision 1).
    StateController _controller;   // +0x28 the embedded controller (D6)
    Profession* profession;        // +0x48 ExternalPtr<Profession>
    bool     isAccepting;          // +0x50 1 from the ctor (Serialize bool&)
    uint8_t  _pad51[7];            // +0x51
    using WorkerList = Collection::Array<Object::InternalPtr<CitizenComponent>, Collection::DefaultAllocator,
                                         Collection::ExplicitSpec<&rtcoll::kSpec_PtrZero>>;
    WorkerList workers;            // +0x58 the roster (count +0x60, capacity +0x64)
};

static_assert(sizeof(WorkPlaceComponent) == 0x68, "WorkPlaceComponent is 0x68 (create 0x14003a030)");
static_assert(alignof(WorkPlaceComponent) == 8, "WorkPlaceComponent alignment");
static_assert(offsetof(WorkPlaceComponent, _controller) == 0x28,
              "WorkPlaceComponent's controller member at +0x28 (slot 17 0x140004080)");
static_assert(offsetof(WorkPlaceComponent, _controller) + offsetof(ControllerComponent::Controller, _owner) == 0x40,
              "controller back-pointer at +0x40 (ctor 0x14012f33c)");
static_assert(offsetof(WorkPlaceComponent, profession) == 0x48, "WorkPlaceComponent::profession @ +0x48 (0x140018020)");
static_assert(offsetof(WorkPlaceComponent, isAccepting) == 0x50, "WorkPlaceComponent +0x50 (ctor 0x14012f344)");
static_assert(offsetof(WorkPlaceComponent, _pad51) == 0x51, "WorkPlaceComponent +0x51 padding");
static_assert(offsetof(WorkPlaceComponent, workers) == 0x58, "WorkPlaceComponent roster data @ +0x58");
static_assert(offsetof(WorkPlaceComponent, workers) + offsetof(WorkPlaceComponent::WorkerList, _size) == 0x60, "WorkPlaceComponent roster count @ +0x60");
static_assert(offsetof(WorkPlaceComponent, workers) + offsetof(WorkPlaceComponent::WorkerList, _capacity) == 0x64, "WorkPlaceComponent roster capacity @ +0x64");
