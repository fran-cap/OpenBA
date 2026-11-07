#pragma once
// ============================================================================
// src/game/Component/ResourceProduceComponent.h — ENDGAME B.3.8 block MI-B
//
// `ResourceProduceComponent`, the abstract base of the six production buildings
// (ConsumeProduce, Fishing, Forester, Herbalist, Hunt, Market), as a real C++
// class with a compiler-generated vtable (B38_RULES.md D1-D6). Its state
// controller is an EMBEDDED MEMBER at +0x28 that ALL SIX leaves share: retail
// has ONE member table (0x1401643a0) over ONE state table (DAT_1401db920), and
// the tree has one `RPCtrlTag` Dispatcher (resourceproducecomponent_states.cpp).
// There is no multiple inheritance (B38_RULES §0.1).
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §1 / §2, bytes read 2026-09-26):
//   reflected "ResourceProduceComponent", parent "ControllerComponent",
//   ti 0x1401c7d60, abstract (create = 0x140003730); DEFINE_EXE_TYPE with the abstract
//   factory (ENDGAME B.3.9 RF).
//   ctor 0x14012b3d0: Component ctor 0x1401188b0; stamps 0x140162b80, 0x1401643f0;
//     member at +0x28: 0x14015f070, +0x30 = +0x38 = 0, 0x1401643a0, +0x40 = this;
//     `MOV byte [RBX+0x48],0`. No flag OR.
//   dtor [0] 0x140037d40: restamps (0x1401643f0, member 0x14015f070, 0x140162b80,
//     0x140162920), ~Internal, MemFree on &1 — NO body of its own.
//   table 0x1401643f0, 22 slots:
//     [0] 0x140037d40  [1]/[2] TypeName/Type  [3] 0x14012b4a0 Persist
//     [4]-[12] = ControllerComponent's (Component's RET bodies)
//     [13] 0x14012b520 CommandMask  [14] 0x14012b5a0 Command  [15] = parent
//     [16] 0x1400026c0 `MOV EAX,2`  [17] 0x140004080 `LEA RAX,[RCX+0x28]`
//     [18] _purecall (CreateWork)  [19]/[20]/[21] 0x140006b10 `RET 0`
//   member table 0x1401643a0 (9 slots): the table-driven controller over the
//     shared RP state table (ControllerLive::Dispatcher<RPCtrlTag>).
//
// SLOT NAMES 18-21 are the tree's (the exe has no symbols): the shared state
// machine calls 18 from state-13 enter (0x14012ae60), 19 there too, 20 from
// state-14 enter (0x14012ad20) and the 13->3 transition (0x14012af70), 21 from
// the state-13 per-frame column (0x14012b190).
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Component/ControllerComponent.h"
#include "Component/controller_state_table.h"
#include "Object/exe_type.h"   // DECLARE_EXE_TYPE (ENDGAME B.3.9 RF)
#include "object/Internal.h"   // Object::InternalPtr (slot 18's out slot)

class Work;

// The ONE tag of the shared ResourceProduce state table (was in an anonymous
// namespace in resourceproducecomponent_states.cpp; the member type needs it).
struct RPCtrlTag { using Owner = class ResourceProduceComponent; };   // the handlers' receiver (controller_state_table.h)

class ResourceProduceComponent : public ControllerComponent {
    DECLARE_EXE_TYPE(ResourceProduceComponent)

public:
    using StateController = ::ControllerLive::TableController<RPCtrlTag>;

    // 0x140037d40 has no body beyond the restamps: the compiler chain is it.
    ~ResourceProduceComponent() override = default;                 // [0]  0x140037d40
    // [1]/[2]: the abstract base answers its own record (DEFINE_EXE_TYPE,
    // ENDGAME B.3.9 RF); every leaf overrides both.
    // [1] TypeName 0x140037d30 and [2] Type 0x140037d20: DECLARE_EXE_TYPE (retail `MOV RAX,[ti+8]` / `MOV EAX,[ti]`).
    void        Persist(IO::Stream& stream) override;                     // [3]  0x14012b4a0
    uint32_t    CommandMask(int command) override;                  // [13] 0x14012b520
    void        Command(int command) override;                      // [14] 0x14012b5a0
    int         GetDefaultState() const override { return SmId(BuildingState::Built); }      // [16] 0x1400026c0
    Controller* GetController() override { return &_controller; }   // [17] 0x140004080
    // [18] pure: mint the building's Work, hand it back through `out` (an
    // InternalBasePtr slot) and return `out`. Retail RCX = this, RDX = out.
    virtual ::Object::InternalPtr<Work>* CreateWork(::Object::InternalPtr<Work>* out) = 0;              // [18] _purecall
    virtual char  OnEnterProducing() { return 0; }                  // [19] 0x140006b10
    virtual char  OnExitProducing() { return 0; }                   // [20] 0x140006b10
    virtual char  ProduceTick(const System::Time* time) { (void)time; return 0; } // [21] 0x140006b10

    void        RequestProduce();                                   // 0x14012b310 set produceFlag, ask the controller for the producing transition

    // PUBLIC while the ported bodies read them at raw offsets (B38 decision 1).
    StateController _controller;   // +0x28 the shared RP controller (D6)
    // +0x48 the panel's pause latch: Built enter picks Paused (14) when it is
    // set, Producing (13) when clear; serialized as a bool by Persist.
    bool            produceFlag;   // +0x48
    uint8_t         _pad49[7];     // +0x49 never written by the ctor

protected:
    // 0x14012b3d0 minus its vptr stores; retail's leaf ctors all CALL it.
    ResourceProduceComponent();
};

static_assert(sizeof(ResourceProduceComponent) == 0x50, "ResourceProduceComponent is 0x50 (Market/Herbalist add nothing)");
static_assert(offsetof(ResourceProduceComponent, _pad49) == 0x49, "ResourceProduceComponent::_pad49");
static_assert(alignof(ResourceProduceComponent) == 8, "ResourceProduceComponent alignment");
static_assert(offsetof(ResourceProduceComponent, _controller) == 0x28,
              "RP controller member at +0x28 (slot 17 0x140004080: LEA RAX,[RCX+0x28])");
static_assert(offsetof(ResourceProduceComponent, _controller) + offsetof(ControllerComponent::Controller, _owner) == 0x40,
              "RP controller back-pointer at +0x40 (ctor 0x14012b427)");
static_assert(offsetof(ResourceProduceComponent, produceFlag) == 0x48, "RP +0x48 (ctor 0x14012b42b)");

