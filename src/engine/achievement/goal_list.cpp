// ============================================================================
// src/engine/achievement/goal_list.cpp — WAVE 27 lane ACHIEVE
// Achievement::GoalList::Initialize
// ============================================================================
//
//   ?Initialize@GoalList@Achievement@@QEAAXV?$Array@V?$ExternalPtr@
//       VGoal@Achievement@@@Object@@@Collection@@@Z          retail 0x18009f0e0
//
// One of the two BODY rows of the Wave 27 Achievement/Audio brief, and the one
// the kickoff singled out for a specific trap. GREPPED BEFORE WRITTEN: the
// mangled name and the address appear in this tree only as PROSE — Goal.h:161,
// achievement_manager.cpp:211 and 657, src/game/Game/gameupdateloop_load.cpp
// :1660 — every one of them a comment saying "called, not reimplemented". No
// body existed.
//
// ============================================================================
// READ FROM THE SHIPPED BYTES, NOT FROM GHIDRA'S DISASSEMBLER
// ============================================================================
// `mcp__ghidra-mcp__disassemble_function(address="0x18009f0e0")` returns a
// DIFFERENT FUNCTION — entry 0x18009f090, a body that allocates 0x100 bytes and
// stamps vtable 0x1801a9310, nothing to do with GoalList — while
// `get_function_by_address` on the SAME address correctly answers
// `Initialize at 18009f0e0, body 18009f0e0-18009f264`. Same for 0x180009d70
// (returns entry 0x180009c20). Recorded, NOT FIXED (kickoff §4 #1); see
// WAVE27_LANE_ACHIEVE.md.
//
// So this body was decoded from `pefile` + `objdump -D -b binary -m
// i386:x86-64 --adjust-vma=0x18009f0e0` over the shipped Runtime-x64.dll. The
// 0x185 bytes that produced it:
//
//   0x18009f10a  8b 52 08              MOV EDX,[RDX+8]      ; src->_size
//   0x18009f10d  e8 5e ac f6 ff        CALL 0x180009d70     ; SetSize(this, n)
//   0x18009f112  45 33 e4              XOR R12D,R12D        ; i = 0
//   0x18009f115  44 39 65 08           CMP [RBP+8],R12D     ; this->_size
//   0x18009f119  0f 8e e2 00 00 00     JLE  tail
//   loop (r15 = i*8):
//   0x18009f130  49 8b 06              MOV RAX,[R14]        ; src->_data  RELOADED
//   0x18009f133  49 8b 0c 07           MOV RCX,[R15+RAX]    ; Goal* goal = src[i]
//   0x18009f137  48 8b 01              MOV RAX,[RCX]        ; goal's vtable
//   0x18009f13a  48 8d 54 24 60        LEA RDX,[RSP+0x60]   ; hidden return slot
//   0x18009f13f  ff 50 48              CALL [RAX+0x48]      ; GetInstanceType
//   0x18009f142  8b 10                 MOV EDX,[RAX]        ; *slot = the typeId
//   0x18009f144..0x18009f172           Factory::CreateObject, INLINED
//   0x18009f175  48 8b f0              MOV RSI,RAX          ; the new instance
//   0x18009f178  8b 3d c2 a7 15 00     MOV EDI,[0x1801f9940] ; GoalInstance typeId
//   0x18009f17e..0x18009f1cd           Base::IsDerivedFrom, INLINED; on failure
//                                      `XOR ESI,ESI` — the instance becomes NULL
//   0x18009f1cf  48 8b 45 00           MOV RAX,[RBP+0]      ; this->_data RELOADED
//   0x18009f1d3  49 89 34 07           MOV [R15+RAX],RSI    ; this->_data[i] = inst
//   0x18009f1d7..0x18009f1e2           reload this->_data[i] and src->_data[i]
//   0x18009f1e6  48 89 41 18           MOV [RCX+0x18],RAX   ; inst->_goal = goal
//   0x18009f1ea  48 8b 01              MOV RAX,[RCX]
//   0x18009f1ed  ff 50 20              CALL [RAX+0x20]      ; inst->Startup()
//   0x18009f1f0  41 ff c4              INC R12D
//   0x18009f1f3  49 83 c7 08           ADD R15,8
//   0x18009f1f7  44 3b 65 08           CMP R12D,[RBP+8]     ; RE-READ every pass
//   tail:
//   0x18009f201  49 8b 3e              MOV RDI,[R14]        ; src->_data
//   0x18009f204  48 85 ff / 74 36      TEST/JZ              ; skip if null
//   0x18009f209..0x18009f239           Enter(alloc+0x640); Free(alloc, p); Leave
//   0x18009f23f  49 c7 06 ...          MOV [R14],0
//   0x18009f246  49 c7 46 08 ...       MOV [R14+8],0        ; _size AND _capacity
//
// ============================================================================
// THE PARAMETER IS BY VALUE, AND THAT IS WHY THE BODY DESTROYS IT
// ============================================================================
// The mangling says `V?$Array@…@Collection@@` — an `Array<ExternalPtr<Goal>>`
// BY VALUE. Under the MS x64 ABI an aggregate that is not 1/2/4/8 bytes is
// passed by hidden reference to a CALLER-allocated temporary, and the CALLEE
// owns it. So the teardown at 0x18009f201 is not a bug and not a courtesy: it
// is the by-value parameter's destructor, inlined, and the port must run it.
//
// ============================================================================
// THE REFCOUNT TRAP THE KICKOFF WARNED ABOUT — AND WHAT THE BYTES SAY
// ============================================================================
// The brief warned that `Array<ExternalPtr<Goal>>` is a REFCOUNTED-ELEMENT
// array, that the POD-element `Array<T>` recipes in achievement_manager.cpp are
// the wrong template for it, and that a port following the sibling recipe
// "compiles, runs, and leaks or double-releases a refcount with no byte oracle
// catching it".
//
// MEASURED ANSWER: **retail's teardown here IS the POD one, and copying the
// refcounted recipe would have been the defect.** 0x18009f201-0x18009f24e is a
// bare `if (p) { Enter; Free; Leave }` plus two zeroing stores. There is NO
// per-element loop and NO release of any kind — the same teardown as
// `GoalArray`'s own destructor (0x180004890, Goal.h), which has no element
// destroy because an ExternalPtr owns nothing.
//
// WHY retail can do that here: 0x18009f1e6 MOVES each `Goal*` out of the source
// array and into the GoalInstance's `_goal` field, with no addref and no
// release. Ownership is TRANSFERRED element by element, and what is left at the
// tail is a buffer of pointers nobody owns any more — a POD buffer. Releasing
// them, which is what the refcounted recipe would do, would over-release every
// Goal in the list.
//
// So the trap was real and it points the OPPOSITE way from the warning: the
// hazard was not "reused the POD recipe", it was "reached for the refcounted
// one because the element type looks refcounted". Both directions are settled
// here by the same two instructions, and neither by the element type's name.
//
// ============================================================================
// TWO INLINED Object HELPERS, CALLED THROUGH THEIR PORTED EXPORTS
// ============================================================================
// Retail inlines `Factory::CreateObject` and `Base::IsDerivedFrom` here. Both
// are ported in src/engine/object/type_reflection.cpp. They are called rather
// than transcribed a third time:
//
//   * `FindNode` + `rec->info->factoryFn()` is `Object::Factory::CreateObject`
//     instruction for instruction, INCLUDING the unguarded `MOV RCX,[RAX+0x10]`
//     on a null node that faults on an unregistered type.
//   * the `GetTypeId()` compare then the `parent`-chain walk is
//     `Object::Base::IsDerivedFrom`, including its `ADD RAX,8`-on-null.
//
// A third transcription would be a third copy of two tree walks that must never
// disagree — exactly what reference_grep_for_the_port_before_writing_it.md is
// about, and what type_system.cpp's own banner says about FindNode being shared
// by four callers.
//
// ============================================================================
// THREE UNGUARDED DEREFS, ALL KEPT
// ============================================================================
//  1. `src->_data[i]` is dereferenced as a Goal with no null test.
//  2. `this->_data[i]->_goal = goal` runs on the instance the dynamic cast may
//     have just set to NULL (0x18009f1cd `XOR ESI,ESI`). Retail stores the null
//     into the array and then writes through it. A cast failure is therefore a
//     CRASH in retail, by construction: it means a Goal asked for an instance
//     type that is not a GoalInstance, i.e. a broken resource.
//  3. `Factory::CreateObject` faults on an unregistered type id.
// None of the three is guarded here (CLAUDE.md "Don't 'fix' a faithful crash
// with a null check"). If our standalone reaches any of them, the fault is
// upstream — a Goal resource whose GetInstanceType returns a type the Factory
// does not carry — and a guard would skip building the goal list rather than
// restore it.
//
//
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "achievement/Goal.h"
#include "object/Internal.h"
#include "object/TypeInfo.h"     // ENDGAME B.3.9 SS1: Object::Factory::_singleton

// `rtcoll::MemFree` (Collection.h, inline: System::MemoryAllocator::MemFree):
// `if (p) { Enter(alloc+0x640); Free(alloc, p); Leave; }`,
// instruction for instruction the teardown inlined at 0x18009f209..0x18009f239.

namespace {

// GoalInstance's type record @ retail 0x1801f9940. Retail reads its +0x00 (the
// typeId) INSIDE the loop, at 0x18009f178. ENDGAME B.3.9 TI-REST: the class's
// own `DEFINE_TYPE` record, read fresh on each call.
uint32_t GoalInstanceTypeId() { return Achievement::GoalInstance::TypeId(); }

// `?_singleton@Factory@Object@@0PEAV12@EA` @ 0x1801fc6e8. ENDGAME B.3.9 SS1
// (B39_RULES D7): the class static now -- read fresh, no lazy cache (this body
// runs on the LOADER thread, B9-10).
const ::Object::Factory* FactorySingleton() {
    return ::Object::Factory::_singleton;
}

}  // namespace

namespace Achievement {

// ============================================================================
// void GoalList::Initialize(Collection::Array<ExternalPtr<Goal>>)
// ?Initialize@GoalList@Achievement@@QEAAX…@Z               retail 0x18009f0e0
//
// NON-virtual. The by-value Array arrives by HIDDEN POINTER (B7-5) and the
// CALLEE tears it down. See the banner for the byte listing, the ownership
// transfer, and the three unguarded derefs.
// ============================================================================
void GoalList::Initialize(GoalArray* src) {
    // retail 0x180009d70 — `_goals` names kSpec_PtrZero in its type.
    _goals.SetSize(src->GetSize());

    // `CMP R12D,[RBP+8]` is re-read at the bottom of every pass, and both
    // `this->_data` and `src->_data` are RELOADED inside the loop — Startup()
    // may reallocate either. Kept.
    for (int i = 0; i < _goals.GetSize(); ++i) {
        const Goal* goal = src->GetData()[i].Get();

        // SLOT 9, `GetInstanceType` — a VIRTUAL call, on purpose: retail
        // `CALL [RAX+0x48]`s it, and the object is an exe subclass whose table
        // holds its own override (TimeGoal) or Goal's (Building/Population/
        // Stat). The retslot is spelled explicitly, in retail order (B7-4).
        Object::Type typeSlot;
        const uint32_t instanceTypeId = *goal->GetInstanceType(&typeSlot);

        ::Object::Base* inst = FactorySingleton()->CreateObject(instanceTypeId);   // 0x1800c0ae0

        // The inlined dynamic cast at 0x18009f17e..0x18009f1cd. On failure
        // retail nulls the instance (`XOR ESI,ESI`) and then writes through it
        // two instructions later. Faithful.
        if (!inst->IsDerivedFrom(GoalInstanceTypeId()))                  // 0x1800c0390
            inst = nullptr;

        _goals[i] = Object::InternalPtr<GoalInstance>(static_cast<GoalInstance*>(inst));

        // Retail RELOADS both arrays here (0x18009f1d7..0x18009f1e2).
        GoalInstance* stored = _goals[i].Get();
        // GoalInstance::_goal @ +0x18 — UNGUARDED (the banner's deref 2). The
        // Goal pointer MOVES out of the source array: no addref, no release.
        stored->_goal = src->GetData()[i];

        // SLOT 4, `Startup` — virtual (`CALL [RAX+0x20]`).
        stored->Startup();
    }

    // The by-value parameter's destructor, inlined by retail. POD teardown: the
    // buffer only, NO per-element release — see the refcount section above.
    rtcoll::MemFree(src->_data);
    src->_data     = nullptr;
    src->_size     = 0;    // retail zeroes _size and _capacity as ONE qword
    src->_capacity = 0;
}

}  // namespace Achievement
