#pragma once
// ============================================================================
// src/game/Component/ControllerComponent.h — ENDGAME B.3.8 (B38_RULES.md §2.2, D6)
//
// `ControllerComponent` and the state-controller interface its leaves EMBED.
//
// THERE IS NO MULTIPLE INHERITANCE (B38_SLOTS.md §2, B38_RULES D6). The
// "secondary vptr" of this family is a MEMBER object with its own vtable,
// constructed after the leaf's primary vptr store, at a PER-CLASS offset
// (+0x28 Well/Build/… , +0x30 Happiness, +0x38 Attendance, +0x48 Pasture,
// +0x58 Nomad, +0x78 Boat, +0x88 Trade). Slot 17 returns its address
// (`LEA RAX,[RCX+off]; RET`). No this-adjusting thunk exists in retail.
// ENDGAME.md B.1 decision 2's "class X : public Component, public
// ControllerComponent" is WRONG for this exe (B38_RULES §0 correction).
//
// RETAIL:
//   table 0x140162b80, 18 slots, parent literal "Component", abstract
//   [0]  0x140032800 scalar deleting dtor: stamp 0x140162b80, stamp 0x140162920,
//        `~Internal`, `if (flags & 1) MemFree` — no body of its own
//   [1] TypeName [2] Type
//   [3]  0x1401195a0 Persist: CALL Component::Persist (0x1401189b0), then an
//        EMPTY Serializer(stream, 0) scope
//   [4]-[15] Component's
//   [16] PURE — every leaf: `MOV EAX,N; RET`, N in {0,1,2,4}  (GetDefaultState)
//   [17] PURE — every leaf: `LEA RAX,[RCX+off]; RET`           (GetController)
//   ControllerComponent adds NO bytes: sizeof == sizeof(Component) == 0x28.
//
// THE INTERFACE, table 0x14015f070: 9 slots, ALL `_purecall` (0x14003addc),
// stamped at the member offset by every family ctor/dtor. It has NO destructor
// slot. The names are NOT attested (the exe has no symbols); they are the
// tree's (controller_state_table.h's Dispatcher, read from the retail
// dispatchers and StateMachine::Tick 0x1400a9660). The member's +0x18 is the
// owning component; every retail slot body tail-calls its handler with
// `MOV RCX,[RCX+0x18]`. The member is 0x20 bytes.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Component/Component.h"
#include "Object/exe_type.h"   // DECLARE_EXE_TYPE (ENDGAME B.3.9 RF)

// The machine every slot receives (StateMachineComponent.h): StateMachine::Tick
// (0x1400a9660) passes RDX = its `this`, StateMachineComponent +0x48.
class StateMachine;

class ControllerComponent : public Component {
    DECLARE_EXE_TYPE(ControllerComponent)

public:
    // The 9-slot state-controller interface (0x14015f070). A leaf embeds a
    // concrete subclass by value at its retail offset (D6).
    class Controller {
    public:
        // [0] find the table index of a state id, -1 if none (the tree's Step)
        virtual int  FindState(int stateId) = 0;
        // [1] the index of the current state's transition to *desiredStateId, -1 if none
        virtual int  FindTransition(int stateIndex, int* desiredStateId) = 0;
        // [2] run the current state's handler for eventId (0 on a miss)
        virtual char PostEvent(StateMachine* stateMachine, int stateIndex, int eventId) = 0;
        // [3] state enter handler
        virtual char Enter(StateMachine* stateMachine, int stateIndex) = 0;
        // [4] state update handler (runs when a transition is pending)
        virtual char Update(StateMachine* stateMachine, int stateIndex) = 0;
        // [5] transition action
        virtual void OnTransition(StateMachine* stateMachine, int stateIndex, int transitionIndex) = 0;
        // [6] per-frame handler, StateMachine::Tick useAlt == 0 (column +0x10)
        virtual char Tick(StateMachine* stateMachine, const System::Time* time, int stateIndex) = 0;
        // [7] per-frame handler, StateMachine::Tick useAlt == 1 (column +0x18)
        virtual char TickAlt(StateMachine* stateMachine, const System::Time* time, int stateIndex) = 0;
        // [8] retail (Well 0x140010260): `XOR EAX,EAX; CMP [count],EAX; SETNZ AL`
        virtual char HasStates() = 0;

        // +0x08 / +0x10 zeroed by every family ctor; +0x18 the owner.
        Controller* _parent = nullptr;   // +0x08
        Controller* _child = nullptr;   // +0x10
        Component* _owner  = nullptr;   // +0x18 — read by every slot body

    protected:
        Controller() = default;
        ~Controller() = default;   // no dtor slot in 0x14015f070
    };

    ~ControllerComponent() override = default;                   // [0]  0x140032800
    // [1] TypeName 0x1400327f0 and [2] Type 0x1400327e0: DECLARE_EXE_TYPE (retail `MOV RAX,[ti+8]` / `MOV EAX,[ti]`).
    void          Persist(IO::Stream& stream) override;                // [3]  0x1401195a0
    virtual int         GetDefaultState() const = 0;             // [16] pure
    virtual Controller* GetController() = 0;                     // [17] pure

protected:
    // Retail inlines this into every leaf ctor: Component ctor, then the
    // 0x140162b80 stamp — which is the compiler's.
    ControllerComponent() = default;
};

static_assert(sizeof(ControllerComponent) == 0x28,
              "ControllerComponent adds no bytes; the controller member lives in each leaf");
static_assert(sizeof(ControllerComponent::Controller) == 0x20,
              "the embedded controller is 0x20 bytes (vptr, +8, +0x10, owner +0x18)");
static_assert(offsetof(ControllerComponent::Controller, _parent) == 0x8, "Controller::_parent");
static_assert(offsetof(ControllerComponent::Controller, _child) == 0x10, "Controller::_child");
static_assert(offsetof(ControllerComponent::Controller, _owner) == 0x18,
              "controller +0x18 = owning component (every slot body: MOV RCX,[RCX+0x18])");
