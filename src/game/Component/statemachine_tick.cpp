// src/game/Component/statemachine_tick.cpp
// StateMachine::Tick (0x1400a9660), on its own so a host unit test can link the
// real body against scripted Controller nodes (tests/unit/sm_memo_test.cpp) without
// the rest of StateMachineComponent (the factory, Persist, the root node table).
// Moved verbatim from statemachinecomponent.cpp (2026-10-17); nothing else changed.
//
// Decompiled from Application-x64.exe using Ghidra.

#include "Component/ControllerComponent.h"
#include "Component/StateMachineComponent.h"
#include "Component/BuildingState.h"
#include "system/Value.h"
#include <cstdint>

using Controller = ControllerComponent::Controller;

// The chain links of a node: +0x08 parent, +0x10 child.
static inline Controller* NodeParent(const Controller* node) {
    return node->_parent;
}
static inline Controller* NodeChild(const Controller* node) {
    return node->_child;
}

// ============================================================
// StateMachine::Tick (0x1400a9660): advance the active state-node chain, firing
// Enter / Update / OnTransition / Tick virtuals
// ============================================================
// Faithful port of the Ghidra decompilation, the node slots called by name.
// Node-interface ABI (verified from the 0x1400a9660 disasm): the state index is
// carried in R8D for Enter/Update and R9D for Tick/TickAlt (time in R8). Ghidra
// DROPS these in the decompile; the real dispatchers read them.
// Node slots used: [0] FindState(int), [3] Enter, [4] Update, [5] OnTransition,
// [6] Tick / [7] TickAlt; node+0x08 parent, node+0x10 first child.
void StateMachine::Tick(const System::Time* time, char useAlt, char run) {
    if (pendingState < 0) goto LAB_transition;
    do {
        do {
            if (currentInput >= 0) {
                int iVar5 = activeState;
                Controller* node = current;
                while (((iVar5 < 0 || node->Update(this, iVar5) == '\0') &&
                        ((node = NodeParent(node)) != nullptr))) {
                    iVar5 = node->FindState(currentInput);
                }
                if (transitionState >= 0) {
                    current->OnTransition(this, activeState, transitionState);
                }
            }
            currentInput = pendingState;
            // Walk to the chain tail (node+0x10 = child pointer)
            Controller* tail = current;
            for (Controller* p = NodeChild(current); p != nullptr; p = NodeChild(p)) {
                tail = p;
            }
            current = tail;
            char c = tail->FindState(pendingState) & 0xff;
            activeState = c;
            while (c < 0) {
                Controller* up = NodeParent(current);
                current = up;
                c = up->FindState(currentInput) & 0xff;
                activeState = c;
            }
            int iVar5 = activeState;
            Controller* node = current;
            // retail stores 0xffff as one word over +0x0A / +0x0B (H7: two stores)
            pendingState = StateByte(BuildingState::None);
            transitionState = -1;
            while (((iVar5 < 0 || node->Enter(this, iVar5) == '\0') &&
                    ((node = NodeParent(node)) != nullptr))) {
                iVar5 = node->FindState(currentInput);
            }
        } while (pendingState >= 0);
    LAB_transition:
        if (run != '\0' && currentInput >= 0) {
            int iVar5 = activeState;
            Controller* node = current;
            if (useAlt == '\0') {
                while (((iVar5 < 0 || node->Tick(this, time, iVar5) == '\0') &&
                        ((node = NodeParent(node)) != nullptr))) {
                    iVar5 = node->FindState(currentInput);
                }
            } else {
                while (((iVar5 < 0 || node->TickAlt(this, time, iVar5) == '\0') &&
                        ((node = NodeParent(node)) != nullptr))) {
                    iVar5 = node->FindState(currentInput);
                }
            }
            run = '\0';
        }
    } while (pendingState >= 0);
}

