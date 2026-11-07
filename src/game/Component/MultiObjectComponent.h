#pragma once
// ============================================================================
// src/game/Component/MultiObjectComponent.h — ENDGAME B.3.8 block MI-B
//
// `MultiObjectComponent` (a structure made of several entities: bridge segments,
// tunnel mouths, a cemetery gate, fence posts) as a real C++ class with a
// compiler-generated vtable and its state controller as an EMBEDDED MEMBER
// (B38_RULES.md D1-D6, D9 #8: the tree had none). The engine/types POD is a
// forwarder.
//
// RETAIL (B38_SLOTS.md §2 / §6, bytes read 2026-09-26):
//   reflected "MultiObjectComponent", parent "ControllerComponent", ti 0x1401c7ac0
//   create 0x140035ef0: MemAlloc(0x60, 8) + ctor 0x140125a70
//   ctor 0x140125a70: Component ctor; member at +0x28 (0x14015f070, 0x140163ca0),
//     back-pointer +0x40; +0x48 = +0x50 = 0; `MOV dword [RBX+0x58],1`;
//     `_flags |= 2`.
//   dtor 0x140125a00: MemFree(+0x48), +0x48 = +0x50 = 0 (the child list: ~children,
//     before ~_controller in reverse declaration order), restamps,
//     ~Internal. [0] 0x1400360c0 = dtor + MemFree on &1.
//   table 0x140163cf0, 18 slots: [3] 0x140125880 Persist, [7] 0x1401257c0 OnRemove
//     (mark every child entity for removal), [12] 0x140125490 OnEvent (relay the
//     event to every child's components), [16] 0x1400026c0 `MOV EAX,2`, [17]
//     0x140004080 `LEA RAX,[RCX+0x28]`; every other slot = ControllerComponent's.
//   member table 0x140163ca0 (FindState 0x1400362c0 over the table at 0x1401d8a70,
//     count 0x1401d99f0; HasStates 0x140036490 reads the "built" flag 0x1401d99f8):
//     the table-driven controller over the class's state table
//     (ControllerLive::Dispatcher<MultiObjectCtrlTag>), built by
//     MultiObjectDescription_Setup 0x14014f530.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Component/ControllerComponent.h"
#include "Component/controller_state_table.h"
#include "Object/exe_type.h"
#include "collection/Collection.h"   // Collection::Array (children)
#include "object/Internal.h"         // Object::InternalPtr<T>

// The child list grows through retail's stride-8 row (0x1400032e0 / 0x140003110: stride
// 8, align 8, the new tail ZEROED, no element destroy) and is freed by the inlined ~Array
// (dtor 0x140125a20: MemFree, then +0x48 = +0x50 = 0): kSpec_PtrZero (Entity/EntityList.h).
namespace rtcoll { extern const ArraySpec kSpec_PtrZero; }   // 0x180008d60

struct MultiObjectCtrlTag { using Owner = class MultiObjectComponent; };   // the handlers' receiver (controller_state_table.h)

class MultiObjectComponent final : public ControllerComponent {
    DECLARE_EXE_TYPE(MultiObjectComponent)

public:
    using StateController = ::ControllerLive::TableController<MultiObjectCtrlTag>;

    MultiObjectComponent();                                              // 0x140125a70

    // 0x140125a00
    ~MultiObjectComponent() override;                                   // [0]  0x1400360c0
    void          Persist(IO::Stream& stream) override;                       // [3]  0x140125880
    void          OnRemove() override;                                  // [7]  0x1401257c0 mark every child for removal
    void          OnEvent(int eventId) override;                        // [12] 0x140125490
    int         GetDefaultState() const override { return SmId(BuildingState::Built); }          // [16] 0x1400026c0
    Controller* GetController() override { return &_controller; }       // [17] 0x140004080

    // ---- non-virtual members (retail addresses) ----
    char EnterState2();                                                 // 0x140125560 state Built enter

    // PUBLIC while the ported bodies read them (B38 decision 1). Types from Persist
    // 0x140125880 (int count +0x50 with the +0x48 IBP array, each validated against
    // ComponentEntity; int +0x58).
    StateController _controller;   // +0x28 the embedded controller (D6)
    using ChildList = Collection::Array<Object::InternalPtr<ComponentEntity>, Collection::DefaultAllocator,
                                        Collection::ExplicitSpec<&rtcoll::kSpec_PtrZero>>;
    ChildList children;            // +0x48 the child-entity list (count +0x50, capacity +0x54)
    int32_t  multiCount;           // +0x58 1 in retail's ctor (Persist: Serialize(int&))
    uint8_t  _pad5C[4];            // +0x5c
};

static_assert(sizeof(MultiObjectComponent) == 0x60, "MultiObjectComponent is 0x60 (create 0x140035ef0)");
static_assert(offsetof(MultiObjectComponent, _pad5C) == 0x5C, "MultiObjectComponent::_pad5C");
static_assert(alignof(MultiObjectComponent) == 8, "MultiObjectComponent alignment");
static_assert(offsetof(MultiObjectComponent, _controller) == 0x28,
              "MultiObjectComponent's controller member at +0x28 (slot 17 0x140004080)");
static_assert(offsetof(MultiObjectComponent, _controller) + offsetof(ControllerComponent::Controller, _owner) == 0x40,
              "controller back-pointer at +0x40 (ctor 0x140125acc)");
static_assert(offsetof(MultiObjectComponent, children) == 0x48, "MultiObjectComponent +0x48");
static_assert(offsetof(MultiObjectComponent, children) + offsetof(MultiObjectComponent::ChildList, _size) == 0x50, "MultiObjectComponent +0x50");
static_assert(offsetof(MultiObjectComponent, children) + offsetof(MultiObjectComponent::ChildList, _capacity) == 0x54, "MultiObjectComponent +0x54");
static_assert(offsetof(MultiObjectComponent, multiCount) == 0x58, "MultiObjectComponent +0x58 (ctor 0x140125ad8)");
