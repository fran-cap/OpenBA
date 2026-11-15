// ============================================================================
// src/engine/achievement/achievement_typeinfo_accessors.cpp — WAVE 27 ACHIEVE
// ============================================================================
//
// THREE VTABLE SLOT BODIES OF THE Achievement CLASSES: Goal::GetFileVersion,
// GoalInstance::Startup and GoalInstance::CheckGoalState. The Type / TypeName
// slots of all four classes are DECLARE_TYPE's (achievement_type_records.cpp);
// `Construct`/`Destruct`/`Reconstruct@Achievement@1@` and
// `Persist@AchievementInstance@Achievement@@` are in achievement_vtable.cpp
// (porting record: docs/history/runtime_port/WAVE27_LANE_ACHIEVE.md §3).
//
// GREPPED BEFORE WRITTEN (kickoff §4 #3). Every one of the 11 mangled names,
// and every one of the 11 retail addresses, was grepped across code/ engine/
// runtime/ standalone/ src/render/dx11/ before a line of this file existed. All 11
// returned nothing outside standalone/rt_data_mint_tables.inc (the generated
// slot table, which is a description of the DLL, not a port).
//
//
// ============================================================================
// THE BODIES, READ FROM THE SHIPPED Runtime-x64.dll BYTES
// ============================================================================
// Disassembled, not decompiled: Ghidra's C output drops dereferences and prints
// MOVSXD as a zero extension (reference_ghidra_c_output_vs_disassembly.md).
//
//   ?Type@Achievement@1@UEBAIXZ                       0x180001020
//       MOV EAX,dword ptr [0x1801f9898] ; RET          = node+0x00
//   ?TypeName@Achievement@1@UEBAPEBDXZ                0x180001040
//       MOV RAX,qword ptr [0x1801f98a0] ; RET          = node+0x08
//
//   ?Type@AchievementInstance@Achievement@@UEBAIXZ     0x1800017a0
//       MOV EAX,dword ptr [0x1801f98d0] ; RET          = node+0x00
//   ?TypeName@AchievementInstance@Achievement@@UEBAPEBDXZ 0x1800017c0
//       MOV RAX,qword ptr [0x1801f98d8] ; RET          = node+0x08
//
//   ?Type@Goal@Achievement@@UEBAIXZ                    0x180001b40
//       MOV EAX,dword ptr [0x1801f9908] ; RET          = node+0x00
//   ?TypeName@Goal@Achievement@@UEBAPEBDXZ             0x180001b60
//       MOV RAX,qword ptr [0x1801f9910] ; RET          = node+0x08
//
//   ?Type@GoalInstance@Achievement@@UEBAIXZ            0x180001e40
//       MOV EAX,dword ptr [0x1801f9940] ; RET          = node+0x00
//   ?TypeName@GoalInstance@Achievement@@UEBAPEBDXZ     0x180001e60
//       MOV RAX,qword ptr [0x1801f9948] ; RET          = node+0x08
//
//   ?GetFileVersion@Goal@Achievement@@UEBAHXZ          0x18009ee10
//       MOV EAX,0x10000 ; RET                          -> literal 65536
//
//   ?Startup@GoalInstance@Achievement@@UEAAXXZ         0x18009f000
//       RET                                            -> EMPTY. One byte.
//
//   ?CheckGoalState@GoalInstance@Achievement@@UEBA?AW4GoalState@Goal@2@XZ
//                                                      0x18009f060
//       MOV RCX,qword ptr [RCX + 0x18]                 ; self->_goal.ptr
//       MOV RAX,qword ptr [RCX]                        ; that Goal's vtable
//       JMP  qword ptr [RAX + 0x40]                    ; tail call, slot 8
//
// `this` is read by only ONE of the eleven, CheckGoalState — the eight node
// readers, GetFileVersion and the empty Startup ignore RCX entirely. That is not
// an inference from a decompile: the nine encodings above have NO RCX operand at
// all, and Startup is a single `RET`. It is what makes the node readers shareable
// across every instance of the class.
//
//
// ============================================================================
// CheckGoalState — THE ONE BODY HERE THAT IS NOT AN ACCESSOR
// ============================================================================
// It is a three-instruction TAIL CALL and it is UNGUARDED. `MOV RCX,[RCX+0x18]`
// then `MOV RAX,[RCX]` faults if `_goal.ptr` is null; retail does not test it.
// That deref stays faithful (CLAUDE.md "Don't 'fix' a faithful crash with a
// null check"): a GoalInstance whose _goal is null is an upstream defect, and a
// guard here would skip the state evaluation rather than restore it.
//
// SLOT 8 (`[RAX+0x40]`) OF `??_7Goal@Achievement@@6B@` IS PURECALL IN RETAIL
// (address 0x18007ec28). That is not a decode error: `Achievement::Goal` declares
// CheckGoalState pure and the EXE's subclasses (StatGoal, TimeGoal,
// PopulationGoal, BuildingGoal, FinishTutorialGoal, … in src/game/Goal/)
// override it. So the object this body dispatches on is never a bare
// `Achievement::Goal` at runtime, and the call lands in the exe.
//
// src/engine/achievement/Goal.h already records +0x18 as
// `ExternalPtr<Achievement::Goal>` and names 0x18009f060 as one of the two
// bodies that pin it, so this port changes no type.
//
// NO `?AW4GoalState@Goal@2@` RETURN-SLOT HAZARD. The mangled return type is a
// `W4` enum, which the MS x64 ABI returns in EAX like an int — there is no
// hidden return slot to omit (reference_hidden_retslot_wild_write.md). The
// `JMP` rather than `CALL/RET` in retail is itself the proof: a body with a
// hidden return-slot argument cannot tail-jump to a function that does not take
// one. `Achievement::Goal::GoalState` is `int32_t`-backed, per Goal.h.
//
// ============================================================================
// Startup — AN EMPTY BODY IS STILL A PORT
// ============================================================================
// `?Startup@GoalInstance@Achievement@@UEAAXXZ` is one `RET`: no return value,
// no argument read, no side effect.
//
// ============================================================================
// HISTORY
// ============================================================================
// The per-slot record of this file and the promotability verdict of the four
// retail `??_7` rows are in docs/history/runtime_port/WAVE27_LANE_ACHIEVE.md §3.1 and §5.
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "achievement/AchievementManager.h"

// ENDGAME B.3.7 block B3: the bodies are MEMBERS of the real classes.

namespace Achievement {

// ---- slots 1 and 2 of all four classes, and the static TypeId -------------
// ENDGAME B.3.9 TI-REST (B39_RULES D6): the eight node readers above
// (Type / TypeName / TypeId of Achievement, AchievementInstance, Goal,
// GoalInstance) are DECLARE_TYPE's bodies over each class's own record
// (achievement_type_records.cpp); the four RT_DATA_SLOT readers of the minted
// records are gone with the rows.

// ---- ??_7Goal@Achievement@@6B@  slot 3 -------------------------------------
// `MOV EAX,0x10000 ; RET`. A literal: the on-disk format version of a Goal,
// 65536 — NOT derived from anything in our tree.
int32_t Goal::GetFileVersion() const { return 0x10000; }                      // 0x18009ee10

// ---- ??_7GoalInstance@Achievement@@6B@  slots 4 and 6 ----------------------
// `RET`. One byte; the exe subclasses override it.
void GoalInstance::Startup() {}                                               // 0x18009f000

// `MOV RCX,[RCX+0x18] ; MOV RAX,[RCX] ; JMP [RAX+0x40]` — Goal slot 8 on
// `_goal`, a VIRTUAL call (the callee is always an exe override: slot 8 is
// `_purecall` in `??_7Goal@Achievement@@6B@`). The deref of `_goal` is
// UNGUARDED in retail and stays so.
Goal::GoalState GoalInstance::CheckGoalState() const {                        // 0x18009f060
    return _goal->CheckGoalState();
}

}  // namespace Achievement

// ============================================================================
// ENDGAME B.3.9 ENG: all eleven slot doors of this file and both register
// functions are gone with the four minted Achievement `??_7` rows -- the
// tables were their only reader. The bodies above are the classes' members,
// reached through the compiler's tables (and TypeId() by the other files).
// ============================================================================
