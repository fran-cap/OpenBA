#pragma once
// ============================================================================
// src/game/Component/StateMachineComponent.h — ENDGAME B.3.8 block VIS
//
// `StateMachineComponent` as a real C++ class with a compiler-generated vtable
// (B38_RULES.md D1-D4, D6).
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §6):
//   reflected "StateMachineComponent", parent "Component", ti 0x1401c7d98
//   create 0x1400382d0: MemAlloc(0x58, 8) + ctor 0x14012ba30
//   ctor   0x14012ba30: Component ctor, stamp 0x140164520; the MEMBER at +0x28:
//                       stamp 0x14015f070 (the interface), +0x30 = +0x38 = 0,
//                       stamp 0x1401644d0 (its own table), +0x40 = this;
//                       +0x48 = this+0x28; +0x50 = 0xff00ffff; flags |= 0xcd
//   dtor   0x14012b9e0: restamp 0x140164520, member restamp 0x14015f070,
//                       restamp 0x140162920, ~Internal (no own work)
//   table  0x140164520, 16 slots:
//     [0]  0x1400384a0 scalar deleting dtor  [1]/[2] TypeName/Type
//     [3]  0x14012b940 Persist (the four state bytes)
//     [4]  0x14012b6a0 Startup (the controller linker)
//     [6]  0x14012b660 OnAdded (Tick(sm, Time(0), 0, 0) — the register-time tick)
//     [8]  0x14012b640 Tick    (Tick(sm, time, 0, 1) — the tree-walk tick)
//     [9]  0x14012b620 Update  (Tick(sm, time, 1, 1) — the bucket tick)
//     [11] 0x14012b7d0 Slot11  (post-load relink + walk)
//     every other slot = Component's
//   member table 0x1401644d0, 9 slots (the controller interface 0x14015f070,
//   ControllerComponent::Controller): the ROOT node of the state-node chain.
//
// NOT MULTIPLE INHERITANCE (B38_RULES §0.1, D6): StateMachineComponent derives
// Component only; the root node is a MEMBER at +0x28, and +0x48.. is the
// StateMachine the tick runs (+0x48 current node, +0x50 input / +0x51 active /
// +0x52 pending / +0x53 transition state bytes).
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Component/Component.h"
#include "Component/ControllerComponent.h"
#include "Object/exe_type.h"

// The StateMachine StateMachine::Tick (0x1400a9660) runs, embedded in
// StateMachineComponent at +0x48 (H6). Its address is what every Controller slot
// receives as `stateMachine`. The four state bytes are signed (Tick's MOVSX / JS
// tests; Persist 0x14012b940 serializes each through Serialize(signed char&),
// import 0x140152418); the component's retail ctor writes them as one dword
// 0xff00ffff (input, active, transition = -1, pending = 0).
class StateMachine {
public:
    // 0x1400a9660: runs the pending transition (Update / OnTransition, walk to the
    // chain's tail, FindState, Enter) while one is pending, then, when `run`, the
    // per-frame column (Tick when `useAlt` is 0, TickAlt when 1) with `time`.
    // Retail passes `const System::Time&` (RDX = &time, 0x14012b67b); the node slots
    // forward the pointer.
    void Tick(const System::Time* time, char useAlt, char run);

    ControllerComponent::Controller* current;   // +0x00 current node (component +0x48; retail ctor this+0x28)
    int8_t   currentInput;        // +0x08 the state the machine was asked for last (component +0x50)
    int8_t   activeState;         // +0x09 the running state's table index (-1: none)
    int8_t   pendingState;        // +0x0A the requested state (-1: nothing pending)
    int8_t   transitionState;     // +0x0B the transition being run (-1: none)
    // +0x0C (component +0x54): retail's tail padding, never written by retail's ctor
    // 0x14012ba30 and never serialized (Persist writes the four bytes above). NOT RETAIL:
    // the port keeps SMMEMO's slot-8 memo here (Component/sm_memo.h): SmMemo::kNone (0,
    // the ctor's value), or the state word above, inverted, as it stood after a full
    // slot-8 tick that called no trans30 handler.
    uint32_t tickMemo;
};

static_assert(sizeof(StateMachine) == 0x10, "StateMachine spans component +0x48..+0x57");
static_assert(offsetof(StateMachine, current) == 0x00, "current node (component +0x48, ctor 0x14012ba86 MOV [RBX+0x48],RAX)");
static_assert(offsetof(StateMachine, tickMemo) == 0x0C, "tail padding (component +0x54..+0x57: retail's ctor 0x14012ba30 never writes it; SMMEMO's memo)");
static_assert(offsetof(StateMachine, currentInput) == 0x08, "input byte (component +0x50, 0x14012ba8a)");
static_assert(offsetof(StateMachine, activeState) == 0x09, "active state (component +0x51, Persist 0x14012b983)");
static_assert(offsetof(StateMachine, pendingState) == 0x0A, "pending state (component +0x52, Persist 0x14012b995)");
static_assert(offsetof(StateMachine, transitionState) == 0x0B, "transition state (component +0x53, Persist 0x14012b9a7)");

class StateMachineComponent final : public Component {
    DECLARE_EXE_TYPE(StateMachineComponent)

public:
    // The root node of the state-node chain (member table 0x1401644d0). The
    // controllers' nodes hang off it (+0x08 parent, +0x10 child).
    // Retail 0x140038630-0x140038800: the state-table template over the class's
    // own static table (0x1401dc8b0), which holds ONE empty state, id 0 (static
    // init 0x14014fa70). So FindState answers 0 for id 0 and -1 for any other id,
    // FindTransition -1 (and *desired = -1), HasStates 1, every handler slot 0
    // (statemachinecomponent.cpp). A state request no linked controller owns
    // climbs past the root into a null parent, in retail as here.
    class RootController final : public ControllerComponent::Controller {
    public:
        explicit RootController(Component* owner) { _owner = owner; }
        ~RootController() = default;

        int  FindState(int stateId) override;                                    // [0] 0x140038630
        int  FindTransition(int stateIndex, int* desiredStateId) override;       // [1] 0x140038660
        char PostEvent(StateMachine* stateMachine, int stateIndex, int eventId) override; // [2] 0x1400386c0
        char Enter(StateMachine* stateMachine, int stateIndex) override;                 // [3] 0x140038710
        char Update(StateMachine* stateMachine, int stateIndex) override;                // [4] 0x140038740
        void OnTransition(StateMachine* stateMachine, int stateIndex,
                          int transitionIndex) override;                         // [5] 0x140038770
        char Tick(StateMachine* stateMachine, const System::Time* time, int stateIndex) override;      // [6] 0x1400387a0
        char TickAlt(StateMachine* stateMachine, const System::Time* time, int stateIndex) override;   // [7] 0x1400387d0
        char HasStates() override;                                               // [8] 0x140038800
    };

    StateMachineComponent();                                      // 0x14012ba30

    // 0x14012b9e0
    ~StateMachineComponent() override = default;                  // [0]  0x1400384a0
    void Persist(IO::Stream& stream) override;                          // [3]  0x14012b940
    void Startup() override;                                      // [4]  0x14012b6a0
    void OnAdded() override;                                      // [6]  0x14012b660
    void Tick(const System::Time* time) override;                               // [8]  0x14012b640
    void Update(const System::Time* time) override;                             // [9]  0x14012b620
    void Slot11() override;                                       // [11] 0x14012b7d0 RelinkAndResume

    // PUBLIC while the ported bodies read them (B38 decision 1).
    RootController _controller;   // +0x28 root node (vptr, +0x30 parent, +0x38 first child, +0x40 owner)
    StateMachine   stateMachine;  // +0x48 the StateMachine Tick runs (H6)

private:
    // The body of slot 11 (0x14012b7d0): relink, then walk the chain to its tail and
    // back up to the node owning the input state (the ctor seats `current` = the root).
    void RelinkAndResume();                                       // 0x14012b7d0
};

static_assert(sizeof(StateMachineComponent) == 0x58, "StateMachineComponent is 0x58 (create 0x1400382d0)");
static_assert(alignof(StateMachineComponent) == 8, "StateMachineComponent alignment");
static_assert(sizeof(StateMachineComponent::RootController) == 0x20, "the root node is 0x20 bytes");
static_assert(offsetof(StateMachineComponent, _controller) == 0x28, "member root node at +0x28 (0x14012ba5b)");
static_assert(offsetof(StateMachineComponent, stateMachine) == 0x48, "StateMachine at +0x48 (0x14012ba86; slot 9 0x14012b623 ADD RCX,0x48)");
