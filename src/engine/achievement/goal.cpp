// ============================================================================
// src/engine/achievement/goal.cpp — Wave 5 lane A — Achievement::Goal /
//                                    GoalInstance / GoalList
// ============================================================================
// ENDGAME B.3.7 block B3: `Achievement::Goal`, `GoalInstance` and `GoalList`
// are real classes (src/engine/achievement/Goal.h). Each ported body below
// is now a MEMBER; each registered door keeps its mangled name and retail
// address and is a one-line QUALIFIED, non-virtual delegation — qualified
// because a live Goal/GoalInstance wears the MINTED table or an EXE subclass's
// hand-built table, whose slot resolves back to the very door, so an
// unqualified virtual call inside a door would re-enter it (B34_BRIEFS §A.1).
//
// ENDGAME B.3.9 ENG (B39_RULES D1/D2): the two CONSTRUCTOR doors (which
// stamped the MINTED tables), the two DESTRUCTOR doors and the slot doors are
// gone with the minted `??_7Goal` / `??_7GoalInstance` rows: the constructors
// are real (Goal.h), every live object wears the COMPILER's table, and the
// factory pairs are the classes' own statics below.
//
// The DESTRUCTORS are real: `~Goal()` / `~GoalInstance()` are empty bodies
// whose compiler-emitted chain (vptr store, `~String` on `_text`, `~External`
// / `~Internal`) is exactly retail's re-stamp / release / tail-jump.
//
// Retail's historical notes that still matter, kept from the Wave 5 banner:
//   * `??1Goal@Achievement@@QEAA@XZ`, `??1GoalInstance@…@QEAA@XZ`,
//     `?Persist@GoalInstance@…@QEAA…` and `?~Goal@…` do NOT exist in the
//     shipped export table — the destructors and Persist are VIRTUAL and
//     exported as UEAA. A QEAA spelling resolves null forever.
//   * Every body is transcribed from the disassembly. No null guards retail
//     lacks.
// ============================================================================

#include <cstdint>
#include <cstdio>
#include <cstring>

#include <atomic>
#include <new>          // placement new into the retslot doors

#include "achievement/Goal.h"
#include "system/Value.h"

namespace Achievement {

// ============================================================================
// ??1Goal@Achievement@@UEAA@XZ   retail 0x18009edb0
//
//     [this] = ??_7Goal@Achievement@@6B@     ; re-stamp
//     _text.~String()                        ; the release idiom, INCLUDING
//                                            ; retail's asymmetry (String.h)
//     JMP ~External                          ; tail call
//
// All three are the compiler's: the vptr store on entry to a destructor, the
// member destructor of `_text` (a `System::String`), then the base destructor.
// The vptr stored is GCC's table for Goal, not the minted one — correct for a
// destructor (nothing dispatches through it before ~External re-stamps), and
// the one thing a hand-written store here could get wrong.
// ============================================================================
Goal::~Goal() {}

// ============================================================================
// Object::Type Goal::GetInstanceType() const — SLOT 9, RULE B7-4
// ?GetInstanceType@Goal@Achievement@@UEBA?AVType@Object@@XZ   retail 0x18009eeb0
//
//     MOV EAX,dword ptr [0x1801f9940]   ; GoalInstance::_typeInfo.typeId
//     MOV dword ptr [RDX],EAX           ; *ret
//     MOV RAX,RDX                       ; return ret
//     RET
//
// `this` is never read: the answer is a class constant. The hidden return slot
// is raw storage, so the value is placement-constructed into it.
// ============================================================================
Object::Type* Goal::GetInstanceType(Object::Type* ret) const {
    return new (ret) Object::Type(GoalInstance::TypeId());
}

// ?CreateNewObject@Goal@Achievement@@SAPEAVBase@Object@@XZ        0x180001b70
// ?CreateInPlaceObject@Goal@Achievement@@SAPEAVBase@Object@@PEAX@Z 0x180001b80
// `XOR EAX,EAX ; RET` both: Goal is abstract (slots 6-8 are `_purecall`), so
// retail's factory never builds one (the FACTREST row had no steps).
Object::Base* Goal::CreateNewObject() { return nullptr; }
Object::Base* Goal::CreateInPlaceObject(void*) { return nullptr; }

// ?CreateNewObject@GoalInstance@Achievement@@SAPEAVBase@Object@@XZ 0x180001f50
// MemAlloc(0x20, 8) (the locked `?MemAlloc@MemoryAllocator@System@@`
// Object::detail::InternalMemAlloc reaches); retail's null branch; then the
// inlined constructor (CALL ??0Internal, the stamp, the dword and qword zero).
Object::Base* GoalInstance::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(GoalInstance), 8);
    if (p == nullptr) return nullptr;
    return ::new (p) GoalInstance();
}

// ============================================================================
// System::String Goal::GetText() const
// ?GetText@Goal@Achievement@@QEBA?AVString@System@@XZ   retail 0x180001ba0
// `*retval = 0; if (src) { *retval = src; ++src[-4]; }` — the String copy
// constructor (0x180006cb0).
// ============================================================================
System::String Goal::GetText() const {
    return _text;
}

// ============================================================================
// ??1GoalInstance@Achievement@@UEAA@XZ   retail 0x18009ef40
// `[this] = ??_7GoalInstance; JMP ~Internal` — both the compiler's. ~Internal
// IS the InternalFactory swap-remove (src/engine/object/internal_class.cpp).
// ============================================================================
GoalInstance::~GoalInstance() {}

// ?IsMet@GoalInstance@Achievement@@QEBA_NXZ      retail 0x18009eed0 — bit 0.
bool GoalInstance::IsMet() const { return (_flags & kFlagMet) != 0; }

// ?HasFailed@GoalInstance@Achievement@@QEBA_NXZ  retail 0x18009eec0 — bit 1.
bool GoalInstance::HasFailed() const { return (_flags & kFlagFailed) != 0; }

// ============================================================================
// void GoalInstance::Update(const System::Time&, const GoalList&) — SLOT 5
// ?Update@GoalInstance@Achievement@@UEAAXAEBVTime@System@@AEBVGoalList@2@@Z
//                                              retail 0x18009f010
//
// Retail reads NEITHER argument (they are for the exe overrides). It polls the
// virtual CheckGoalState (slot 6) — an UNQUALIFIED call on purpose: retail
// dispatches (`CALL [RAX+0x30]`), and on an exe subclass that is the
// subclass's override — and folds the answer into the flags. The FAILED bit
// LATCHES: once set, the entry test makes every later Update a no-op.
// ============================================================================
void GoalInstance::Update(const System::Time& /*time*/, const GoalList& /*list*/) {
    if ((_flags & kFlagFailed) != 0)
        return;

    const Goal::GoalState state = CheckGoalState();

    if (state == Goal::kMet)    { _flags |= kFlagMet;  return; }
    if (state == Goal::kNotMet) { _flags &= ~kFlagMet; return; }
    if (state == Goal::kFailed) { _flags |= kFlagFailed; }
}

// ============================================================================
// GoalList::GoalList()  ??0GoalList@Achievement@@QEAA@XZ  retail 0x18009f0b0
// Two zero qwords — `Collection::Array`'s own default constructor.
// ============================================================================
GoalList::GoalList() {}

// ============================================================================
// GoalList::GoalList(const GoalList&)  ??0GoalList@…@QEAA@AEBV01@@Z  0x180001730
//     XOR EAX,EAX; MOV [RCX],RAX; MOV [RCX+8],RAX    ; the member's zero init
//     CALL 0x180003a30                               ; Array<InternalPtr>::operator=
// 0x180003a30 is the kSpec_PtrZero `operator=` (Assign8Z per element into a
// freshly allocated buffer) — `Collection::Array::Assign` driven by the row
// `_goals` names in its type. See Goal.h: the copy SHARES the instances.
// ============================================================================
GoalList::GoalList(const GoalList& other) : _goals() {
    _goals.Assign(other._goals);
}

// ??4GoalList@Achievement@@QEAAAEAV01@AEBV01@@Z  retail 0x180001760:
// `CALL 0x180003a30; MOV RAX,RBX; RET`.
GoalList& GoalList::operator=(const GoalList& other) {
    _goals.Assign(other._goals);
    return *this;
}

// ============================================================================
// bool GoalList::AreGoalsMetExcept(InternalPtr<GoalInstance>) const
// ?AreGoalsMetExcept@GoalList@Achievement@@QEBA_NV?$InternalPtr@VGoalInstance@Achievement@@@Object@@@Z
//                                              retail 0x18009f070
//
// The InternalPtr arrives by HIDDEN POINTER (`MOV RDX,[RDX]` at 0x18009f07c).
// Two details the decompiler gets wrong, both from the disassembly:
//   * the count is loaded ONCE (`MOVSXD R9,[RCX+8]`) — hoisted here too;
//   * the element read is UNGUARDED (`TEST byte [RCX+0x10],0x1` straight out
//     of the array): a null entry faults, in retail too. Not "fixed".
// ============================================================================
bool GoalList::AreGoalsMetExcept(const Object::InternalPtr<GoalInstance>* except) const {
    const GoalInstance* const skip = except->Get();
    const int64_t count = _goals._size;      // loaded once, as retail does
    for (int64_t i = 0; i < count; ++i) {
        const GoalInstance* g = _goals._data[i].Get();
        if (g != skip && (g->_flags & GoalInstance::kFlagMet) == 0)
            return false;
    }
    return true;
}

}  // namespace Achievement
