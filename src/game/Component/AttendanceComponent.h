#pragma once
// ============================================================================
// src/game/Component/AttendanceComponent.h — ENDGAME B.3.8 block MI-A
//
// `AttendanceComponent` as a real C++ class with a compiler-generated vtable and its
// state controller as an EMBEDDED MEMBER (B38_RULES.md D1-D6; there is no
// multiple inheritance). The engine/types POD is a forwarder to this file.
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §2 and §6, bytes read 2026-09-26):
//   reflected "AttendanceComponent", parent "ControllerComponent", ti 0x1401c3f40
//   create 0x140002670: MemAlloc(0x58, 8) + ctor 0x140044580
//   createInPlace = the imported `Internal::CreateInPlaceObject` (returns null);
//     the tree's in-place entry builds the object and is kept (B38 B8-13).
//   ctor 0x140044580: Component ctor; +0x28 = +0x30 = 0; member at +0x38;
//     `_flags |= 0xa` (0x8a with Component's 0x80).
//   dtor 0x140044510: MemFree(+0x28) and +0x28 = +0x30 = 0 (the waiting
//     queue), member restamp, ~Internal. [0] 0x140002860 = dtor + MemFree on &1.
//   table 0x140158ce0, 18 slots:
//     [0] 0x140002860 dtor  [1]/[2] TypeName/Type  [3] 0x140044410 Persist
//     [7] 0x1400443d0 OnRemove (leave the town attendance list)
//     [9] 0x140044370 Update  [13] 0x140043f10 CommandMask  [14] 0x140043e80 Command
//     [16] 0x1400026c0 `MOV EAX,2`  [17] 0x1400026b0 `LEA RAX,[RCX+0x38]`
//     The tree's slots 18-21 held AttendanceDescription's table (past retail's
//     end, B38 D3.5): gone.
//     every other slot = ControllerComponent's / Component's.
//   member table 0x140158c90 (9 slots): the table-driven state controller over the
//     class's state table (ControllerLive::Dispatcher<AttendanceCtrlTag>), stamped at
//     this+0x38 after the interface 0x14015f070; back-pointer at this+0x50.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Component/ControllerComponent.h"
#include "Component/controller_state_table.h"
#include "Object/exe_type.h"
#include "collection/Collection.h"   // Collection::Array (queue)

struct AttendanceCtrlTag { using Owner = class AttendanceComponent; };   // the handlers' receiver (controller_state_table.h)
class CitizenComponent;   // Component/CitizenComponent.h

// The queue grows through retail's stride-8 rows (0x1400032e0 SetSize(n) / 0x140003110
// SetSize(n, fill): stride 8, align 8, the new tail ZEROED, no element destroy) and is freed
// by the inlined ~Array (dtor 0x140044543: MemFree, then +0x28 = +0x30 = 0):
// kSpec_PtrZero (Entity/EntityList.h).
namespace rtcoll { extern const ArraySpec kSpec_PtrZero; }   // 0x180008d60

class AttendanceComponent final : public ControllerComponent {
    DECLARE_EXE_TYPE(AttendanceComponent)

public:
    using StateController = ::ControllerLive::TableController<AttendanceCtrlTag>;

    AttendanceComponent();                                // 0x140044580

    // 0x140044510
    ~AttendanceComponent() override;                              // [0]  0x140002860
    void          Persist(IO::Stream& stream) override;                 // [3]  0x140044410
    void          OnRemove() override;                            // [7]  0x1400443d0
    void          Update(const System::Time* time) override;                    // [9]  0x140044370
    uint32_t      CommandMask(int command) override;              // [13] 0x140043f10
    void          Command(int command) override;                  // [14] 0x140043e80
    int         GetDefaultState() const override { return SmId(BuildingState::Built); }  // [16] 0x1400026c0
    Controller* GetController() override { return &_controller; }  // [17] 0x1400026b0

    // ---- non-virtual members (retail addresses) ----
    void ProcessQueue();                                          // 0x140043f60 send every queued citizen home
    char AllFull();                                               // 0x140043db0 every building of this description full?
    // The state handlers of the controller's table (RCX = [node+0x18] = this).
    char State2Enter();                                           // 0x140043c90 open
    char State2Frame();                                           // 0x140043c30 per frame (trans38)
    char State3Enter();                                           // 0x140043bd0 closed

    // PUBLIC while the ported bodies read them (B38 decision 1). Persist
    // 0x140044410: the count, then per entry an InternalBasePtr nulled unless
    // IsDerivedFrom CitizenComponent (0x1401c7158).
    using QueueList = Collection::Array<Object::InternalPtr<CitizenComponent>, Collection::DefaultAllocator,
                                        Collection::ExplicitSpec<&rtcoll::kSpec_PtrZero>>;
    QueueList          queue;          // +0x28 the waiting citizens (count +0x30, capacity +0x34)
    StateController _controller;   // +0x38 the embedded controller (D6)
};

static_assert(sizeof(AttendanceComponent) == 0x58, "AttendanceComponent is 0x58 (create 0x140002670)");
static_assert(alignof(AttendanceComponent) == 8, "AttendanceComponent alignment");
static_assert(offsetof(AttendanceComponent, _controller) == 0x38,
              "AttendanceComponent's controller member at +0x38 (slot 17 0x1400026b0: LEA RAX,[RCX+0x38])");
static_assert(offsetof(AttendanceComponent, _controller) + offsetof(ControllerComponent::Controller, _owner) == 0x50,
              "controller back-pointer at +0x50 (ctor 0x140044580)");
static_assert(offsetof(AttendanceComponent, queue) == 0x28, "AttendanceComponent +0x28 queue");
static_assert(offsetof(AttendanceComponent, queue) + offsetof(AttendanceComponent::QueueList, _size) == 0x30, "AttendanceComponent +0x30 queue count");
static_assert(offsetof(AttendanceComponent, queue) + offsetof(AttendanceComponent::QueueList, _capacity) == 0x34, "AttendanceComponent +0x34 queue capacity");
