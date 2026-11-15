// ============================================================================
// src/engine/achievement/batchc_goallist_dtor.cpp — BATCH C
//   ??1GoalList@Achievement@@QEAA@XZ             retail 0x18009f420, ordinal 602
// ============================================================================
//
// The last unported member of Achievement::GoalList. Its behaviour was already
// fully written down — src/engine/achievement/Goal.h's GoalList banner names
// every step of it as one of the four functions that PIN the layout — and this
// file is that description turned into the body, checked line by line against
// the disassembly at 0x18009f420..0x18009f4cc.
//
//   for (i = 0; i < this->_goals._size; ++i) {     // bound RE-READ each pass
//       slot = &this->_goals._data[i];             // base RE-READ each pass
//       if (*slot) { (*slot)->vtable[0](*slot, 1); *slot = nullptr; }
//   }
//   if (this->_goals._data) MemFree(this->_goals._data);
//   this->_goals._data = nullptr;
//   *(uint64*)&this->_goals._size = 0;             // _size AND _capacity
//
// `vtable[0]` WITH FLAG 1 IS THE SCALAR DELETING DESTRUCTOR: the list OWNS its
// GoalInstances and frees each one — `Object::Destroy(p)`, the virtual
// `delete` (slot 0, flags 1) (Goal.h's slot 0).
//
// THE FREE IS INLINED IN RETAIL as lock / Free / unlock around the allocator's
// +0x640 critical section. goal_list.cpp records that this triple IS
// `?MemFree@MemoryAllocator@System@@SAXPEAX@Z` (0x1800c7ad0) instruction for
// instruction; here it is the compiler-emitted `~Array` of `_goals` (below).
//
// ============================================================================

#include <cstddef>
#include <cstdint>

#include "achievement/Goal.h"
#include "object/Internal.h"

namespace Achievement {

// ============================================================================
// GoalList::~GoalList()   ??1GoalList@Achievement@@QEAA@XZ   retail 0x18009f420
//
// ENDGAME B.3.7: a REAL destructor of the value type (B7-8). The body is the
// element loop ONLY; the tail — free the buffer, zero `_data` and the
// `_size`/`_capacity` qword — is the compiler-emitted `~Array` of `_goals`,
// which for kSpec_PtrZero (no element destroy) is exactly `ArrayDtorImpl`:
// `MemFree(_data)` (the null test lives inside 0x1800c7ad0) then the zeroes.
// Retail's order is the same: loop, free, zero.
//
// `slot 0 WITH FLAG 1` is the scalar DELETING destructor — `Object::Destroy`.
// It is VIRTUAL: the instance is an exe subclass (TimeGoalInstance,
// FinishTutorialInstance) and ITS slot 0 must run.
// ============================================================================
GoalList::~GoalList() {
    for (int32_t i = 0; i < _goals._size; ++i) {
        // `MOV R14,[RDI]` sits INSIDE the loop at 0x18009f450 — the base is
        // re-read every pass. Not hoisted.
        Object::InternalPtr<GoalInstance>& slot = _goals._data[i];
        GoalInstance* const inst = slot.Get();
        if (inst == nullptr) continue;
        delete inst;
        slot = Object::InternalPtr<GoalInstance>();
    }
}

}  // namespace Achievement
