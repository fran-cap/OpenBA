// ============================================================================
// src/engine/ui/dialog.cpp — Wave 7 lane B
//   UI::DialogInstance + UI::Dialog + UI::DialogController + UI::ElementDescription
// ============================================================================
//
// Ports 14 of the 28 lane-B symbols (PLAN.md §4.2 wave 7, §6). The recovered
// layouts and the evidence for every offset are in src/engine/ui/
// DialogInstance.h, Dialog.h and ElementDescription.h; the per-function
// write-up, the oracle argument for each, everything held back and why, and
// the resolution of this lane's two phantom export names are in
// docs/history/runtime_port/WAVE7_LANE_B.md.
//
//   | Export (short form)                   | Retail      | Ord  | Ported | Oracle |
//   |---------------------------------------|-------------|------|--------|--------|
//   | ??0DialogInstance (ctor)              | 0x180114870 |  147 | NO     | —      |
//   | ??1DialogInstance (dtor)              | 0x180114d40 |  574 | W9-UID | shape  |
//   | ??_7DialogInstance (vtable)           | 0x180156d70 | 1420 | n/a    | —      |
//   | ?AddController@ (I,H,H,External*,Str) |  NOT AN EXPORT     | n/a    | —      |
//   | ?AddController@ (…ExtPtr,ExtPtr,Str)  | 0x180113470 | 1541 | NO     | —      |
//   | ?AddController@ (…ExtPtr,IntPtr)      | 0x1801133b0 | 1542 | NO     | —      |
//   | ?FindElement@                         | 0x180114c70 | 2346 | yes    | replay |
//   | ?FindElementType@                     | 0x1801140c0 | 2348 | yes    | replay |
//   | ?GetController@                       | 0x180113320 | 2513 | yes    | replay |
//   | ?GetRoot@                             | 0x180045580 | 2875 | yes    | replay |
//   | ?OnElementEvent@                      | 0x180114ed0 | 3274 | yes    | shape  |
//   | ?OnUpdate@                            | 0x180115400 | 3286 | yes    | shape  |
//   | ?OnUpdateLayout@                      | 0x1801153a0 | 3289 | yes    | shape  |
//   | ?SetFlagsOnOff@                       | 0x180114690 | 3882 | yes    | replay |
//   | ?SetUpperLeft@                        | 0x180114a60 | 3997 | NO     | —      |
//   | ?Persist@DialogInstance@              | 0x180114fa0 | 3322 | W9-Z   | none   |
//   | ?OnSerializeSettings@DialogInstance@  | 0x180115470 | 3281 | W9-Z   | none   |
//   | ?TypeName@DialogInstance@             | 0x180045400 | 4379 | W9-Z   | none   |
//   | ?OnClosed@DialogInstance@             | 0x180115330 | 3269 | NO(*)  | —      |
//     (*) DECLINED AGAIN by WAVE 9 LANE UID, on a blocker RE-MEASURED rather
//         than inherited — see the block below RtUiDialogInstance_Dtor.
//   | ?StartTimer@                          | 0x1801137c0 | 4081 | NO     | —      |
//   | ?StopTimer@                           | 0x180113730 | 4111 | yes    | replay |
//   | ?CreateElement@Dialog@                | 0x180112ed0 | 1888 | NO     | —      |
//   | ?GetReference@Dialog@ (as spelled)    |  NOT AN EXPORT     | n/a    | —      |
//   | ?GetTemplate@Dialog@                  | 0x1800388b0 | 2956 | yes    | replay |
//   | ?_typeInfo@Dialog@ (data)             | 0x1801faa88 | 4847 | n/a    | —      |
//   | ??1DialogController (dtor)            | 0x1800cd2f0 |  572 | yes    | shape  |
//   | ?GetContainer@DialogController@       | 0x18003b700 | 2508 | yes    | replay |
//   | ??0ElementDescription (ctor)          | 0x180115a40 |  173 | yes    | replay |
//   | ??1ElementDescription (dtor)          | 0x180116100 |  585 | yes(*) | none   |
//     (*) WAVE 9 LANE UIF. `none` is the literal oracle disposition, not a gap
//         nobody noticed: a replay would double-release a refcounted String and
//         run ~External twice. See the body's header near the bottom of this file.
//   | ?CreateInstance@ElementDescription@   | 0x180115ad0 | 2004 | W10-UIF| replay+twin |
//   | ?GetFileVersion@ElementDescription@   | 0x180116af0 | 2624 | yes    | replay |
//   | ?Serialize@ElementDescription@        | 0x180116b00 | 3687 | NO     | —      |
//
// All 28 mangled names were checked against the shipped export directory of
// C:\GOG Games\Banished\Runtime-x64.dll with pefile BEFORE any body was
// written (reference_resolveexport_phantom_names.md: a name the DLL does not
// contain resolves to NULL in silence, the port still registers, and the
// census still counts it). TWO ARE NOT THERE — see the next block. The
// addresses and ordinals in the table are that query's output, not Ghidra's.
//
// ---------------------------------------------------------------------------
// THE TWO PHANTOM NAMES — BOTH ARE LIVE DEFECTS, NEITHER IS PORT WORK
// ---------------------------------------------------------------------------
// ?AddController@DialogInstance@UI@@QEAAXIHHPEBVExternal@Object@@AEBVString@System@@@Z
//     Not in the export table. It is a mis-mangling of the SIX-argument
//     overload that IS there, 0x180113470 / ordinal 1541, whose real signature
//     is (uint, Base*, ExternalPtr<DialogControllerConfig>,
//     ExternalPtr<ElementDescription>, String const&). The phantom spells
//     arguments 2 and 3 as plain `int`s, which is the
//     reference_hidden_retval_and_int_pointer_args.md failure exactly: the two
//     pointer arguments land in R8/R9 as 32-bit halves.
//     `engine_funcs.cpp` ALREADY resolves that same overload correctly as
//     `fn_DialogInstance_AddController6`, so the phantom is a duplicate — and
//     its ResolveExport returns null, at which point its fallback reads exe
//     dispatch slot 0x140151a60, which `tools/dispatch_slot_map.py` says is
//     `?AddChild@Element@UI@@…`. So `fn_DialogInstance_AddController` has been
//     silently bound to Element::AddChild. It is harmless ONLY because the
//     pointer has zero callers tree-wide; it is removed at its source rather
//     than repaired (PLAN §6 step 8), and WAVE7_LANE_B.md §5 records it.
//
// ?GetReference@Dialog@UI@@QEBA?AV?$ExternalPtr@VDialogReference@UI@@@Object@@XZ
//     Not in the export table, and neither is any symbol containing
//     "DialogReference" — the class name was invented. The real function is
//     ?GetReference@Dialog@UI@@QEBA?AV?$ExternalPtr@VExternal@Object@@@Object@@H@Z
//     at 0x1800388d0 / ordinal 2861: it returns ExternalPtr<Object::External>
//     and it TAKES AN INT INDEX, which the phantom typedef omits. It sits four
//     bytes after ?GetTemplate@ and is the identical one-liner against a
//     different array (Dialog+0x78 vs +0x68). `fn_Dialog_GetReference` has no
//     fallback, so it has been NULL since it was written, and no caller has
//     ever noticed because it has no callers. Removed at its source too.
//
// ---------------------------------------------------------------------------
// WHY REGISTERING THIS PORT IS SAFE
// ---------------------------------------------------------------------------
// Registering redirects OUR callers only. The DLL keeps calling its own
// bodies through direct near-CALLs the registry cannot see, and — new in this
// lane — three of the fourteen ports are VIRTUALS (`?OnElementEvent@`,
// `?OnUpdate@`, `?OnUpdateLayout@` are all `UEAA`), so every dispatch through
// the DLL's own `??_7DialogInstance@UI@@6B@` still lands in the DLL no matter
// what we register. That is not a problem to fix, it is the shape of the
// wave: what we redirect is the `fn_DialogInstance_*` pointers our exe-side
// dialog vtables call directly.
//
// It is safe here because of four facts, each checked rather than assumed:
//
//   1. ⚠ NO LONGER TRUE AS WRITTEN — corrected by WAVE 9 LANE UID rather than
//      left standing. Wave 7's claim was: nothing ported here allocates,
//      frees or reshapes a tree; every body reads a field, writes fields of
//      an object somebody else owns at static_assert'ed offsets, or walks an
//      existing array. That still describes every Wave 7 body, and it is why
//      the replay oracle is sound for them.
//      IT DOES NOT DESCRIBE `??1DialogInstance@UI@@` (lane UID). That body
//      FREES two arrays, destroys a whole child tree through slot 0, clears
//      a linked list and removes the object from the handle table. It is
//      therefore NOT covered by argument 3 below either: it has no replay
//      oracle and cannot have one. What carries it instead is stated at the
//      body — a shape check of the six values that could silently be wrong,
//      compared against the shipped bytes while the DLL is still present.
//      Everything else that allocates (both AddControllers, StartTimer,
//      CreateInstance, the two ctors) is still held back — WAVE7_LANE_B.md §2.
//   2. NO PORTED BODY STAMPS A VTABLE IT MINTED. The one ported function that
//      writes vtable pointers, `??0ElementDescription`, resolves all three of
//      them (`??_7Base@Object@@6B@`, `??_7External@Object@@6B@`,
//      `??_7ElementDescription@UI@@6B@`) out of the DLL's own export table and
//      stores those. This is the `??_7Geometry@Graphics@@6B@` lever from Wave
//      6 and it is what keeps a half-ported class from putting two vtables in
//      play (PLAN §4.3 risk 3).
//   3. THE REPLAY ORACLE PROVES, ON EVERY DIFFED CALL, that our body leaves
//      the object byte-identical to what the DLL's body would have left. That
//      is what makes "two implementations, one live object" a non-event.
//   4. NO DETOUR IS INSTALLED, deliberately, for the same reason Wave 6 lane A
//      installed none: it would route the DLL's own UI loop into this port for
//      no gain the argument above does not already give.
//
// ---------------------------------------------------------------------------
// ONE REGISTRATION HAS A WIDER BLAST RADIUS THAN THE OTHERS — AND IT IS HELD BACK
// ---------------------------------------------------------------------------
// `standalone/address_registry.cpp` binds five exe PE-import slots to lane-B
// exports by NAME, through `RegisterDispatchSlot(slot, ResolveExport(name))`:
//
//     0x1401518b0  ??0ElementDescription@UI@@QEAA@XZ
//     0x1401518b8  ??1ElementDescription@UI@@UEAA@XZ
//     0x1401518c0  ?GetFileVersion@ElementDescription@UI@@UEBAHXZ
//     0x1401518c8  ?Serialize@ElementDescription@UI@@UEAA_NAEAVStream@IO@@@Z
//     0x1401518d0  ?CreateInstance@ElementDescription@UI@@QEBAXV?$InternalPtr@…@Z
//     0x1401528c0  ?CreateElement@Dialog@UI@@QEBA?AV?$InternalPtr@…@Z
//
// `RuntimePortInit()` runs BEFORE `AddressRegistryInit()` (src/app/main.cpp
// :1614 / :1626, and the comment there says so on purpose), and `ResolveExport`
// consults the runtime registry first — so registering one of those names does
// not merely redirect our own `fn_*` pointers, it re-points the exe's own
// retail-shaped `.rdata` slot at our body too. For the two we do register
// (`??0ElementDescription`, `?GetFileVersion@`) that is exactly what we want
// and both are fully oracled. For `?Serialize@` it would put our body on the
// UIDescription parse path of every `.rsc` in the game, which is the single
// worst place in this codebase to land an unverifiable port
// (reference_createplaced_placebitarray_zeroed_by_reparse.md, and commit
// a12037d). It is held back; see WAVE7_LANE_B.md §2.
//
// ---------------------------------------------------------------------------
// THREE ORACLES
// ---------------------------------------------------------------------------
// (a) REPLAY, for the accessors and the two self-contained mutators. Snapshot
//     the live object into a scratch copy BEFORE our body runs, run our body
//     on the real object, then run the DLL's body on the scratch copy and
//     compare. Because the scratch is a memcpy of the live object, the four
//     padding holes the DialogInstance ctor never initialises (DialogInstance.h
//     lists them) hold identical bytes on both sides — this is NOT the
//     "comparing bytes neither side wrote" trap
//     (reference_oracle_must_not_compare_unwritten_bytes.md), which bites only
//     when one side starts from fill.
//     For the const accessors the comparison is the returned value, which is
//     the entire output.
//
// (b) REPLAY WITH A REDIRECTED ARRAY, for `?StopTimer@` alone. It mutates a
//     heap buffer the object merely points at, so a naive replay would shift
//     the SAME buffer twice. The scratch object's `_timers._data` is repointed
//     at a private copy of the timer entries first, and the comparison is over
//     a fingerprint of the object prefix (minus that one pointer) plus the live
//     timer entries.
//
// (c) SHAPE, decoded from the shipped bytes at registration time, for the four
//     functions a replay would double-execute: the three controller-dispatch
//     loops (running them twice would tick every controller twice) and
//     `??1DialogController` (running it twice would swap-remove the object from
//     the handle table twice). A dispatch loop's ENTIRE failure surface is
//     three numbers — which field holds the array, which field holds the count,
//     and which vtable slot it calls — and all three are literal displacements
//     in the shipped instruction stream. `VerifyDispatchLoop` reads them out
//     and reports through an ordinary diff site. This is the Wave 6 lane A
//     `VerifyThunk` idea applied to a loop instead of a tail-jump.
//     ~DialogController's whole body is two instructions (stamp a vtable, tail-
//     jump to `Object::Internal::~Internal`), and `VerifyDtorThunk` checks both
//     of the addresses they name.
//     invocation — say so when reading the summary rather than concluding the
//     function was never exercised.
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>

#include "system/Filename.h"
#include "ui/Dialog.h"
#include "ui/DialogInstance.h"
#include "ui/ElementDescription.h"
#include "ui/Element.h"       // B3X W0: the root's slots by name
#include "ui/UserInterface.h" // B4: UserInterface::_uiScale / _events
#include "object/VSlot.h"     // B3X W0 audit: the left Contains call's slot
#include "ui/StringTable.h"   // WAVE 10 LANE UIF — the twin's table
#include "system/Timer.h"     // ENDGAME B.3.9 SS1: Timer::_secondsPerTick
#include "video/Device.h"     // ENDGAME B.3.9 SS1: Video::Device::_singleton
#include "system/MemoryAllocator.h"
#include "object/External.h"
#include "../collection/array_internal.h" // ENDGAME B.3.2 step 2 — kSpec_Event30
// ⚠ WAVE 9 LANE Z — `src/engine/object/TypeInfo.h` IS NOT INCLUDABLE HERE,
// and the reason is a real defect in another lane's header rather than anything
// about this file. `src/engine/object/External.h` opens `namespace RtObject
// {` at its line 37 and then `#include "TypeInfo.h"` at its line 276, INSIDE
// that namespace — so every declaration in TypeInfo.h lands in
// `RtObject::RtObject`, and `#pragma once` means whichever translation unit
// pulls External.h first (here, via ElementDescription.h) fixes that nesting for
// the whole TU. `RtObject::BaseTypeInfo` then does not name a type.
//
// Filed, NOT fixed: moving that one `#include` out of External.h's namespace is
// a two-line change in a header 40+ files depend on, it is not this lane's file,
// and it cannot be validated without a full build — which lane Z does not hold.
// `?TypeName@DialogInstance@` below therefore spells the one offset it needs as
// a raw offset with its evidence at the use site, which is the convention
// src/engine/ui/UserInterface.h already states for foreign offsets.

using RtUI::DialogTimer;
using RtUI::kDialogInstanceCtorExtent;
using RtUI::DialogNamedTemplate;   // WAVE 9 LANE UIC (?CreateElement@Dialog@)

// ----------------------------------------------------------------------------
namespace {

// ---- Raw retail ABI typedefs ----------------------------------------------
// Every member below takes `this` in RCX. Every one whose C++ return type is a
// class (InternalPtr<T>, ExternalPtr<T>) has a HIDDEN RETURN SLOT in RDX and
// returns that same pointer in RAX — confirmed instruction by instruction in
// the disassembly, e.g. ?GetRoot@ is literally
//   MOV RAX,[RCX+0x50] / MOV [RDX],RAX / MOV RAX,RDX / RET.
// Getting this wrong is the wild-write family
// (reference_hidden_retslot_wild_write.md), so it is spelled out per typedef.

// ---- WAVE 9 LANE UIC helpers ----------------------------------------------
// The `System::String` release retail open-codes at every temporary, with its
// asymmetry intact: the FREEING branch leaves the local holding `data - 8`
// (dangling, and then freed), the SHARING branch nulls it. Identical to the one
// `??1ElementDescription@` at the bottom of this file spells out; factored here
// because `?CreateElement@` performs it three times.
//
// Takes the local BY REFERENCE so the two different post-states are visible to
// the caller exactly as they are in retail — a by-value helper would look
// correct and silently drop the asymmetry.
void ReleaseStringLocal(char*& p) {
    if (!p) return;
    if (--RtSystem::StringRefCount(p) == 0) {
        p -= 8;
        System::MemoryAllocator::MemFree(p);        // 0x1800c7ad0
    } else {
        p = nullptr;
    }
}

// `?_singleton@Device@Video@@0PEAV12@EA`: read as the class static
// `Video::Device::_singleton` at each use (B4: its accessor is gone).

// ---- slots 1/2 of DialogInstance and ElementDescription --------------------
// Retail reads ONE `Object::BaseTypeInfo` per class, +0x00 the id and +0x08 the
// name: `MOV EAX,[0x1801faac0]` / `MOV RAX,[0x1801faac8]` (DialogInstance,
// 0x1800453e0 / 0x180045400) and `MOV EAX,[0x1801faaf8]` / `MOV RAX,[0x1801fab00]`
// (ElementDescription, 0x1800350d0 / 0x1800350f0). ENDGAME B.3.9 TI-UI: that
// record is each class's own `_typeInfo` (DECLARE_TYPE / DEFINE_TYPE at the end
// of this file); the RT_DATA_SLOT accessors of the minted rows are gone.


// 0x1800c7ad0 (`?MemFree@MemoryAllocator@System@@`), the out-of-line spelling
// of the lock+Free retail inlines at both of ~DialogInstance's array frees.
// Same argument src/engine/collection/linkedlist.cpp's header block makes for
// 0x180009c30. Called directly (B3R RT-UI).
inline void MemFreeStatic(void* p) {
    System::MemoryAllocator::MemFree(p);
}

// ENDGAME B.3.2 step 2 — the 0x1800325f0 forwarder that used to sit here is
// GONE. `self->_children` (DialogInstance.h) is a real
// `Collection::LinkedList<InternalBasePtr>` member now, so its one caller
// below calls `.Clear()` on the container directly.

}  // namespace

// ============================================================================
// WAVE 9 LANE Z — three DialogInstance vtable slots the CUTOVER needs
// ============================================================================
// The DLL-absent boot asks for four `DialogInstance` virtuals, all four from
// ONE exe-side site (0x7ff71e05a0dc, four times each): the derived-dialog vtable
// builders in src/game/Dialog/mod_dialog_vtables.cpp,
// src/game/Dialog/toolbar_vtable_standalone.cpp and
// src/game/Player/tool_base_toolui.cpp, which copy the base class's slots into
// each derived table. With no DLL those copies are nulls STAMPED INTO A LIVE
// VTABLE — an EXEC-of-0 on the first virtual call, several frames from anything
// naming `DialogInstance`.
//
// Three of the four land here. `?OnClosed@` is DECLINED and why is recorded at
// the end of this block.
// ============================================================================

// ---- ?Persist@DialogInstance@UI@@UEAAXAEAVStream@IO@@@Z  0x180114fa0 ord 3322
// ---- ?OnSerializeSettings@DialogInstance@UI@@UEAAXAEAVStream@IO@@@Z
//      0x180115470 ord 3281 -----------------------------------------------
//
// Both are ONE INSTRUCTION in the shipped DLL:
//
//     180114fa0  RET
//     180115470  RET
//
// (three bytes each per Ghidra's function ranges, because MSVC pads them; the
// body is the single `RET`). They are the base class's do-nothing hooks —
// `Object::Internal`'s `Persist` slot and the dialog's settings hook — and every
// derived dialog that cares overrides them.
//
// AN EMPTY BODY IS STILL A PORT, and it is worth stating why rather than
// treating it as trivia. What the cutover needs is not behaviour, it is a
// NON-NULL ADDRESS to stamp into slot 3 and slot 9 of every derived vtable. A
// null there is indistinguishable at the call site from a correct empty
// override right up until it is called. There is deliberately no oracle: a
// function with no reads, no writes and no return value has nothing to compare,
// and no oracle site is manufactured for the sake of a green count over work
// that never happened
// (reference_probe_needs_an_effectiveness_control.md).
// ENDGAME B.3.9 UI-D: the two RET slot doors are retired with the minted
// table; the members are `DialogInstance::Persist` / `OnSerializeSettings`.

// ---- ?TypeName@ / ?Type@DialogInstance@UI@@ (0x180045400 / 0x1800453e0) ----
// `MOV RAX,[0x1801faac8]` / `MOV EAX,[0x1801faac0]`: +0x08 and +0x00 of the
// class's record -- DECLARE_TYPE's inline bodies (ENDGAME B.3.9 TI-UI).

// ---- ?OnClosed@DialogInstance@UI@@UEAAXXZ  0x180115330  ord 3269
//
// PORTED — Wave 9 lane ENC-2, in src/engine/ui/dialog_settings.cpp alongside
// ?SaveSettings@DialogInstance@. The DECLINED note that stood here was right to
// decline and WRONG about the size of the job, in a way worth recording:
//
//   it said "it becomes a one-function job the moment ?SaveSettings@ lands".
//
// It does not. SaveSettings builds a BinaryWriter and then dispatches into it,
// so it needs FOUR more bodies that were themselves unported minted traps --
// the two scalar Serialize slots (+0x98, +0xa0), the back-patching destructor
// ??1BinaryWriter, and BinaryWriter's unexported deleting destructor at slot 0.
// Those landed first, in src/engine/io/binary_writer.cpp. The row was six
// functions, not one, and counting it as one is the same underestimate that
// made lane ENC four functions instead of the two it was scoped for.
//
// The controller loop's slot +0x30 that this note flagged as unnamed is now
// named by the EXPORT TABLE rather than by a caller: ??_7DialogController@UI@@6B@
// (0x1801565f0) was read byte-wise and slot +0x30 is
// ?OnClosed@DialogController@UI@@UEAAXXZ (0x1800cd290), with +0x50
// ?OnSerializeSettings@DialogController@UI@@ (0x1800cd250). Both are in
// dialog_settings.cpp's header with the evidence.

// ============================================================================
// UI::DialogInstance — const accessors
// ============================================================================

// ============================================================================
// UI::DialogInstance — self-contained mutators
// ============================================================================

// ============================================================================
// UI::DialogInstance — the three controller-dispatch virtuals
// ============================================================================
// All three are `UEAA`, i.e. VIRTUAL, and the DLL's exported
// ??_7DialogInstance@UI@@6B@ is what the DLL itself dispatches through — so
// registering these redirects our direct `fn_DialogInstance_*` callers and
// nothing else. That is intentional (see the safety note at the top).
//
// The loops carry NO null check on the controller pointer: retail's
// `MOV RCX,[RDI+RAX] / MOV RAX,[RCX]` dereferences it unguarded. Kept.

// ?OnUpdateLayout@ (0x1801153a0), ?OnUpdate@ (0x180115400) and
// ?OnElementEvent@ (0x180114ed0): ENDGAME B.3.9 UI-D retired the three slot
// doors with the minted table; the members are in the UI section below.
// OnElementEvent's InternalPtr<Element> arrives BY ADDRESS and the member
// hands each controller a pointer to its OWN fresh copy, never the caller's.

// ============================================================================
// UI::DialogInstance — frame leftovers (docs/history/runtime_port/closure/WORKLIST.md)
// ============================================================================
// Four bodies read from the disassembly (not the decompiled C, which drops the
// implicit RDX argument on the `OnUpdate` vtable call and mis-shapes
// `ComputeLayout`'s tail-call jump table — reference_ghidra_c_output_vs_
// disassembly.md). Raw Element offsets are spelled out at the use site, same
// convention src/engine/ui/UserInterface.h states for a foreign class this
// file does not own.
//
// Element vtable slots used below (src/engine/ui/Element.h): 0x20
// ComputeSize(), 0x28 ComputeLayout(x,y,w,h), 0x38 Update(...), 0x48
// Contains(x,y,UserInterface*). DialogInstance's OWN vtable slots (this file's
// tables above / tooltipdialog.cpp): 0x30 OnUpdateLayout(), 0x38 OnUpdate(time).

// ============================================================================
// UI::Dialog
// ============================================================================

// ============================================================================
// UI::DialogController
// ============================================================================

// ??1DialogController@UI@@UEAA@XZ (0x1800cd2f0): the door is RETIRED (ENDGAME
// B.3.9 UI-D) — its only reader was the RT_DELDTOR row of the retired minted
// `??_7DialogController@UI@@6B@`; the empty `UI::DialogController::~DialogController`
// is retail's body.


// ============================================================================
// WAVE 9 LANE UID — ??1DialogInstance@UI@@UEAA@XZ  0x180114d40  ord 574
// ============================================================================
//
// The dialog lifecycle's teardown half, declined by lane UIF (row 6, "the one
// genuinely close call") and again by lane UIC (§7). BOTH DECLINES RESTED ON
// THREE STATED BLOCKERS AND ALL THREE DISSOLVED ON RE-CHECKING
// (reference_holdback_blockers_are_stale_check_them_first.md). Each is answered
// from the bytes below rather than argued:
//
//  1. "0x18002fca0 (int)" — NOT unported. `ArrayPtrSetSize` in
//     src/engine/object/internal.cpp has been the port of FUN_18002fca0 since
//     Wave 3, with the address in the comment directly above it. What was
//     missing was the REGISTRATION at that address, which this lane added
//     (internal.cpp, ungated). A half-registered port reads exactly like an
//     absent one from a callee census, which is how it survived two lanes.
//  2. "0x1800325f0 (int, list free-all)" — now ported, as
//     `RtColl_LinkedListClear_1800325f0` in src/engine/collection/linkedlist.cpp,
//     the fourth member of the `Collection::LinkedList<T=8>` family lane UIC
//     opened. ~20 lines; its whole failure surface is four displacements and
//     they are shape-checked below.
//  3. "the body OPEN-CODES the InternalFactory swap-remove inline at
//     0x180114e71 … in a function with no oracle, and it is the one row where
//     being wrong CORRUPTS THE HANDLE TABLE."
//
//     This one is the substance of both declines, and it is answered by reading
//     `??1Internal@Object@@UEAA@XZ` (0x1800bfdb0) beside it. THE TWO ARE THE
//     SAME INSTRUCTION STREAM, register allocation aside:
//
//       0x1800bfdb0  ~Internal              0x180114e64  ~DialogInstance tail
//       ---------------------------------  ---------------------------------
//       LEA RAX,[0x180154720]              LEA RAX,[0x180154720]
//       MOV [RCX],RAX                      MOV [RDI],RAX
//       MOV dword [RCX+0xc],0x0            MOV dword [RDI+0xc],EBP   (EBP == 0)
//       MOV RCX,[0x1801fc6d8]              MOV RCX,[0x1801fc6d8]
//       MOV EDX,[RCX+0x8] / DEC EDX        MOV EDX,[RCX+0x8] / DEC EDX
//       MOVSXD R11,[RBX+0x8]               MOVSXD R11,[RDI+0x8]
//       CMP EDX,R11D / JZ skip             CMP EDX,R11D / JZ skip
//       MOV R9,[RCX] / LEA R10,[R11*8]     MOV R9,[RCX] / LEA R10,[R11*8]
//       MOVSXD RAX,EDX                     MOVSXD RAX,EDX
//       MOV R8,[R9+RAX*8]                  MOV R8,[R9+RAX*8]
//       MOV [R9+R10],R8                    MOV [R9+R10],R8
//       MOV RAX,[RCX] / MOV R8,[RAX+R10]   MOV RAX,[RCX] / MOV R8,[RAX+R10]
//       MOV [R8+0x8],R11D                  MOV [R8+0x8],R11D
//       skip: CALL 0x18002fca0             skip: CALL 0x18002fca0
//       LEA RAX,[0x180154788]              LEA RAX,[0x180154788]
//       MOV [RBX],RAX                      MOV [RDI],RAX
//
//     So retail's "open-coded swap-remove" IS `Object::Internal::~Internal`,
//     INLINED VERBATIM — not a second, private handle-table mutator. This port
//     therefore does what `??1DialogController@UI@@` (60 lines above, Wave 7,
//     shape-verified) already does for the identical tail: it calls the ONE
//     body the tree owns, through the seam, so exactly one handle-table mutator
//     is ever in play and it is whichever one the object-table port
//     tier has armed. Nothing here reimplements the swap-remove, and nothing
//     here arms a tier that is off.
//
//     ⚠ THE COUPLING IS REAL AND IS NOT HIDDEN: with the DLL present and that
//     tier off (the default), this dtor's tail is the DLL's `~Internal`, which
//     is correct. AT CUTOVER the tier must be armed — WAVE9.md already records
//     it as "Opt-in, and MANDATORY at cutover", so this adds no new obligation,
//     but it does add a caller that will fault loudly if the promise is broken.
//
// WHAT THE BODY IS, in retail's own order (0x180114d40-0x180114ec9):
//
//     this->vptr = ??_7DialogInstance@UI@@6B@          (0x180156d70)
//     if (_root) { _root->vtbl[0](_root, 1); _root = 0; }        // scalar-del
//     for (i = 0; i < _controllers._size; ++i) {
//         c = _controllers._data[i];                  // RE-READ each iteration
//         if (c) { c->vtbl[0](c, 1); _controllers._data[i] = 0; }
//     }
//     if (_controllers._data) MemFree(_controllers._data);
//     _controllers._data = 0;  _controllers._size = 0;   // NOT _capacity
//     if (_timers._data)      MemFree(_timers._data);
//     _timers._data = 0;       _timers._size = 0;        // NOT _capacity
//     LinkedList::Clear(&_children);                   // 0x1800325f0
//     Object::Internal::~Internal(this);               // inlined, see above
//
// FOUR DETAILS THAT A TIDIER REWRITE WOULD GET WRONG:
//   * `_controllers._data` is RE-LOADED INSIDE the loop (0x180114da0), not
//     hoisted. A controller's own destructor can reallocate the array it is
//     being destroyed out of, and retail re-reads for that reason or by
//     accident — either way, hoisting changes what the next iteration sees.
//   * `_capacity` IS LEFT ALONE for both arrays. Only data and size are zeroed.
//   * THE `if (ptr)` BEFORE EACH MemFree IS RETAIL'S OWN redundancy — MemFree
//     (0x1800c7ad0) opens with `TEST RCX,RCX; JZ ret`. It is reproduced rather
//     than dropped; this is not a guard added to paper over a crash.
//   * SLOT 0 IS CALLED WITH FLAG 1 — the SCALAR DELETING destructor, which
//     frees the child. Passing 0 would destruct without freeing and leak every
//     element and controller of every dialog.
//
// ORACLE: NONE, and none is possible — it is a destructor, so a replay is not a
// comparison but a second destruction: it would run every child's deleting
// destructor again, free both arrays twice, and swap-remove a handle that no
// longer exists. It ships in the disposition lanes M, Z and UIF established: a
// no-oracle port with NO call into the shipped body at all.
//
// What stands in for it is a SHAPE CHECK, `VerifyDialogInstanceDtorShape`,
// which reads the shipped instruction stream and compares the three vtable
// addresses this body stamps, the two functions it calls, and the `_children`
// displacement it hands to the first of them. That is not "some" of the failure
// surface — for a destructor whose every other operation is a store of zero,
// those six values ARE what could silently be wrong, and all six are compared
// against the DLL while the DLL is still there (WAVE9.md §2: the verification
// budget has to be spent before the removal).
// ENDGAME B.3.9 UI-D: the door `RtUiDialogInstance_Dtor` that stood here is
// retired with the minted table and its RT_DELDTOR row; the body above is
// `UI::DialogInstance::~DialogInstance` (ReleaseFields, the UI section), and
// ~Internal is the compiler's chain.

// ============================================================================
// UI::ElementDescription
// ============================================================================

// ??0ElementDescription@UI@@QEAA@XZ — retail 0x180115a40, ordinal 173.
//
// Three vtable stores in base-class order, then the field initialisers. The
// two non-zero seeds are the interesting part and both are reproduced exactly:
//   +0x3c = -1        (the element id; -1 means "unset", 0 is a REAL id —
//                      reference_unresolved_lookup_leaves_a_valid_looking_zero.md)
//   +0x40 = 3         (as a QWORD, so +0x44, the flags, is zeroed with it)
//
// Every byte of 0x00..0x67 is written, which is what makes the replay below a
// full-object oracle rather than a spot check.
// ENDGAME B.3.9 UI-D: the door `RtUiElementDescription_Ctor` is retired
// with the minted table; the real constructor (ElementDescription.h) runs
// External() and the three stamps through the compiler, and its body is
// `InitFields` (the UI section of this file).

// ?GetFileVersion@ElementDescription@UI@@UEBAHXZ — retail 0x180116af0, ord 2624.
//   MOV EAX,0x4 / RET
// The constant is the .rsc element-description file version and it is what
// ?Serialize@ (held back) is written against.
// ENDGAME B.3.6 fragment pass F2: the body is UI::ElementDescription::
// GetFileVersion (member section below); the door delegates QUALIFIED.
// (ENDGAME B.3.9 UI-D: the slot door is retired; the member is below.)

// ?TypeName@ / ?Type@ElementDescription@UI@@ (retail 0x1800350f0 / 0x1800350d0):
// DECLARE_TYPE's inline bodies over the class's own record (ENDGAME B.3.9
// TI-UI).

// ============================================================================
// ??1ElementDescription@UI@@UEAA@XZ  retail 0x180116100  ord 585
//                                                          (WAVE 9 LANE UIF)
// ============================================================================
//
// WHY THIS ROW EXISTS AT ALL. Like `??1Element`, this destructor is not reached
// by any direct C++ caller in our tree — it is reached through a LIVE MINTED
// VTABLE. `??_7ElementDescription@UI@@6B@` slot 0 is the vector deleting
// destructor `??_EElementDescription@UI@@` at 0x180035350, and lane K's family
// reaches `~Class` by ADDRESS through ResolveAddr.
// With this row unported that edge went to the DLL, and after cutover to NULL.
//
// ⚠ CORRECTION, WAVE 27 lane IOTEXT. This paragraph used to call 0x180035350
// "one of the 37 Wave 9 lane K ported". IT WAS NOT PORTED until Wave 27: at
// wave27/base, grepping 0x180035350 across runtime/, standalone/ and code/
// found it ONLY in this comment and in standalone/rt_data_mint_tables.inc:1016
// (the mint row, i.e. the TRAP), never in a RegisterRuntimeAddr. The sentence
// read as a completion and cost the table three waves of being 3/6
// (reference_a_prescription_read_as_a_completion.md). The row now exists:
// src/engine/object/deleting_dtor.cpp, spec index 44, "UI::ElementDescription".
//
// 0x180035350 WAS DISASSEMBLED FOR THIS LANE, and it is one of lane K's "22 of
// 37 that INLINE the scalar branch": only its ARRAY branch names the destructor
// (`LEA R9,[0x180116100]` at 0x180035377), while its scalar branch open-codes
// the same instructions. That is exactly why lane K's port calls `~Class` on
// BOTH branches rather than reproducing the inlining — so this registration is
// what both branches now land on.
//
// THE BODY, from the disassembly at 0x180116100 (13 instructions, one tail
// jump), and it is small enough to quote in full:
//
//     this->_vtable = &??_7ElementDescription@UI@@6B@;   // 0x1801561c0
//     <release this->_textName>                          // +0x60, the ONE String
//     JMP ~External(this);                               // 0x1800bc950, TAIL
//
// src/engine/ui/ElementDescription.h already says this in its SIZE argument
// ("`??1ElementDescription` (0x180116100) releases exactly one String, the one
// at +0x60, before chaining"), written by Wave 7 lane B from the same bytes.
// The port agreeing with a header derived independently is a weak check, but it
// is a real one and it is free.
//
// EVERY CALLEE IS SATISFIED — the admission test, and this is the cheapest row
// in the cluster to pass it:
//   * 0x1800c7ad0 MemFree      — PORTED, lane Q
//   * 0x1800bc950 ~External    — PORTED, Wave 9 lane B2 (?? 1External@Object@@)
// Two callees, both ours. The row CLOSES; it does not move one frame deeper.
//
// NO ORACLE, for the same reason as `??1Element` and stated for the same
// reason: a replay would decrement the `_textName` refcount a second time and,
// where our body already took it to zero, free a block twice and then run
// `~External` twice over a handle that no longer exists. The did-work split
// how many calls actually released a buffer; `calls=` stays 0 because there is
// no comparison to report. Liveness, not correctness.
// ENDGAME B.3.9 UI-D: the door `RtUiElementDescription_Dtor` that stood
// here is retired with the minted table and its RT_DELDTOR row. The body
// above is `UI::ElementDescription::~ElementDescription` (ReleaseFields);
// the restamp and the ~External tail are the compiler's.

// ============================================================================
// WAVE 9 LANE UIC — the two rows WAVE9_LANE_UIF.md declined at 8 and 11
// ============================================================================

// ---- ?SetUpperLeft@DialogInstance@UI@@QEAAXHH_N@Z  0x180114a60  ord 3997 ----

// ============================================================================
// WAVE 10 LANE UIF
// ?CreateInstance@ElementDescription@UI@@QEBAXV?$InternalPtr@VElement@UI@@@Object@@@Z
//                                                 0x180115ad0  ord 2004
// ============================================================================
//
// "Stamp this serialized description onto that live element." It is the second
// half of `?CreateElement@Dialog@` (ported above by Wave 9 lane UIC): that one
// picks WHICH description wins, this one applies it.
//
// WHY IT WAS BLOCKED AND WHY IT IS NOT NOW. Wave 7 lane B held it in the
// "everything else that allocates" group at the top of this file. It does not,
// in fact, allocate anything of its own — but it FREES, twice, through the
// inlined `System::String` release, and every free in this project sat behind
// the Wave 2 allocator NO-GO. Wave 10 lane ALLOC retired that gate
// (docs/history/runtime_port/WAVE10_LANE_ALLOC.md): the allocator port runs with the
// DLL present, so "our body frees what the DLL's unported code allocated" is
// one heap either way. This body does not need the flag; it frees through
// whichever `MemFree` is live.
//
// ---------------------------------------------------------------------------
// ABI. `QEBAX` returning void: RCX = `this` (const), RDX = the `InternalPtr<
// UI::Element>` argument. `InternalPtr<T>` is an 8-byte class with a
// non-trivial copy constructor, so MSVC x64 passes it BY ADDRESS even though
// the mangled name spells it by value — and the body proves it, dereferencing
// RDX eleven times (`MOV RBX,[RDI]` at 0x180115b15 and ten re-reads). So the
// second parameter is `void** elemPtr`, NOT `void* elem`.
// reference_hidden_retval_and_int_pointer_args.md is the family this belongs to.
//
// THE BODY, from the disassembly at 0x180115ad0 (0x31a bytes):
//
//   1. `elem->_name = this->GetPath().GetPathIdentifer();` through retail's
//      inlined String assign-with-refcount, then release both temporaries in
//      the order ident-then-path.
//   2. Eleven scalar copies from the description onto the element. Note that
//      `_flags` is OR-ed (0x180115bec `OR [RCX+0x34],EAX`), never assigned —
//      a description with no flags cannot CLEAR a flag the element already has
//      — and that `_id` (+0x3c) and `_tabOrder` (+0x38) CROSS OVER into
//      element +0x38 and +0x3c respectively. Both are retail behaviour, not
//      transcription slips, and both are the kind of thing a byte oracle
//      catches and a reading does not.
//   3. `_stringTable` -> `elem->_toolTip` (a `UIString` at +0x48: the table at
//      +0x48 and its index at +0x50), with the index taken from
//      `StringTable::GetIndexFromName` or -1.
//   4. `elem->_description = this` (+0xb8).
//   5. A `UI::Dialog` TEMPORARY on the stack, constructed and destroyed purely
//      to be the context argument of one virtual call: if `_childDesc` (+0x48)
//      is non-null, `elem->_border = *childDesc->vtbl[+0x40](childDesc, &slot,
//      &tempDialog)`, else `elem->_border = nullptr`.
//   6. A flags cascade guarded on four conditions — see the DEAD CODE note.
//
// ---------------------------------------------------------------------------
// THREE FACTS THAT ONLY THE DISASSEMBLY SHOWS
// (reference_ghidra_c_output_vs_disassembly.md)
// ---------------------------------------------------------------------------
//
// (a) THE INLINED `UI::Dialog` TEMPORARY IS THE EXPORTED CTOR/DTOR, INSTRUCTION
//     FOR INSTRUCTION. Retail inlined `??0Dialog@UI@@QEAA@XZ` (0x180112db0) and
//     `??1Dialog@UI@@UEAA@XZ` (0x180112e40) here. Both were disassembled and
//     compared against the inline before this port was written:
//       * the ctor is 3 vtable stores (Base 0x180154788, External 0x1801547a0,
//         Dialog 0x1801562a0) and 17 zero/constant stores out to +0x80, with
//         +0x30 = -1. The inline differs in ONE respect and it is not a
//         behavioural one: the inline writes +0x20/+0x28 as a single 16-byte
//         `XORPS/MOVDQA`, the out-of-line as two QWORDs. Same 16 bytes.
//       * the dtor is the four array destructors at +0x78/+0x68/+0x58/+0x48
//         (0x180009460, 0x180039440, 0x180039360, 0x180039280), the String
//         release at +0x28, and a tail-JMP to `~External` (0x1800bc950) — which
//         is exactly the tail of the inline, `~External` included.
//     So the port TRANSCRIBES the ctor (it is only constant stores, and doing
//     so adds NO callee at all) and CALLS the exported dtor. That is the same
//     out-of-line substitution `??1DialogInstance@` already makes for `MemFree`
//     in this file. It grows the cutover surface by exactly ONE named row,
//     `??1Dialog@UI@@UEAA@XZ` — reference_porting_can_grow_the_cutover_surface
//     .md says to say so rather than let a later census discover it.
//
// (b) `elem->_toolTip._index` IS WRITTEN AS A QWORD OVER AN UNINITIALISED
//     UPPER HALF. Retail stores the int32 index into a stack slot
//     (`MOV [RBP+0x1f],EAX`) and then reads EIGHT bytes back out of it
//     (`MOV RAX,[RBP+0x1f]; MOV [RCX+0x50],RAX`), so element +0x54..+0x57 —
//     the `UIString` pad — receives whatever was on retail's stack. That is
//     INDETERMINATE and cannot be reproduced. The port writes the DWORD only,
//     and the oracle EXCLUDES +0x54..+0x57 from its compare. This is a stated
//     deviation, not an accident, and it is the one place this body is not
//     byte-faithful.
//
// (c) THE FLAGS CASCADE IS DEAD CODE IN THE SHIPPED BINARY, and the port keeps
//     it anyway. The guard is
//         MOV EDX,[RCX+0x34] ; MOV EAX,EDX ; OR EAX,0x90000
//         MOV [RCX+0x34],EAX ; XOR EDX,EAX ; TEST DL,0x1 ; JZ over
//     `0x90000` touches bits 16 and 19 only, so `(old ^ (old|0x90000))` is
//     always ZERO in its low byte and the `JZ` is always taken. The parent walk
//     that follows (`OR [RCX+0x34],0x8000000` up the +0x98 chain) can never
//     execute. It is transcribed rather than deleted, because "provably
//     unreachable in this build" is a claim the port should not bake in.
//
// ---------------------------------------------------------------------------
// THE WIDER BLAST RADIUS — read this before registering
// ---------------------------------------------------------------------------
// This is one of the six names in this file whose registration also re-points
// an EXE PE-import slot: `standalone/address_registry.cpp:1115` binds
// 0x1401518d0 with `RegisterDispatchSlot(slot, ResolveExport(name))`, and
// `RuntimePortInit()` runs BEFORE `AddressRegistryInit()`. So registering this
// puts our body on the element-stamping path of every dialog the game builds,
// not merely on our own `fn_*` pointers. That is precisely the row the census
// asked for — it is *sourced* from address_registry.cpp — but it is why this
// one gets a REPLAY oracle rather than a shape check.
//
// EVERY CALLEE IS SATISFIED:
//   * 0x180006250 `?GetPath@External@Object@@`        — ported (external.cpp)
//   * 0x1800c6610 `?GetPathIdentifer@Filename@System@@` — ported (filename.cpp)
//   * 0x1800c7ad0 `MemFree`                            — ported, lane Q
//   * 0x180002470 `?GetIndexFromName@StringTable@UI@@`  — see sprite.cpp:571
//   * 0x180112e40 `??1Dialog@UI@@UEAA@XZ`              — NOT ported; new row
//   * `_childDesc`'s vtable slot 8                     — PURE on the base
//     (standalone/rt_data_mint_tables.inc, `RT_MINT_PURECALL`), so it is only
//     ever reached on a concrete derived description and porting this body
//     introduces no new cutover symbol for it — the same argument
//     `?CreateElement@Dialog@` already makes for the identical slot.
// ---------------------------------------------------------------------------

namespace {

// The retail-inlined `??0Dialog@UI@@QEAA@XZ` / `??1Dialog@UI@@UEAA@XZ` pair is
// `::new (p) UI::Dialog()` / `~T()` (slot 0, flags 0) since ENDGAME B.3.9 UI-D (the
// temporary is a real UI::Dialog wearing the class's table).
constexpr size_t kDialogTempSize = 0x88;

// ENDGAME B.3.6 fragment pass F2: the body that stood here (the file-local
// `EdCreateInstanceBody`) MOVED into UI::ElementDescription::CreateInstance(
// InternalPtr<Element>*) in the member section below; the door delegates
// QUALIFIED.

// (The replay oracle's storage that sat here — g_edCi* — had no reader since
// the oracle was retired; removed with the body move.)

} // namespace

// ############################################################################
// ENDGAME B.3.6 block B — THE MEMBER BODIES.
//
// Every body that stood in a door above MOVED here onto UI::DialogInstance /
// UI::Dialog / UI::DialogController (src/engine/ui/{DialogInstance,Dialog,
// DialogController}.h), unchanged except `self->x` became `x` and the
// hand-inlined vtable calls onto THIS block's classes became member calls:
//   * `(*(void***)self)[6/7]`       -> OnUpdateLayout() / OnUpdate(time) (virtual)
//   * controller slot +0x10/+0x38/+0x40/+0x48 -> c->Type() / OnUpdateLayout() /
//     OnUpdate(time) / OnElementEvent(...)  (virtual, on UI::DialogController)
//   * RtUiDialogInstance_Contains/_Update/_GetMouseCursorIndex -> member calls.
// The doors above keep their registrations and delegate QUALIFIED. Calls onto
// OTHER blocks' classes (Element's slots 4/5/7/9, Element::FindElement /
// FindElementType / AdjustUpperLeft, UserInterface::_events) are left as they
// were: those classes are not converted yet.
//
// Per-body evidence and the retail disassembly are in the comment blocks at
// each door above; they are not repeated.
// ############################################################################

namespace {

// B3R RT-UI: Object::External::GetPath (0x180006250), StringTable::
// GetIndexFromName (0x180002470) and Element::AdjustUpperLeft (0x180115840) are
// called by name in the bodies below.


}  // namespace

namespace UI {

// ---- Object::Internal slots ------------------------------------------------
void DialogInstance::Persist(IO::Stream&/*stream*/) {}              // [3] 0x180114fa0  RET
void DialogInstance::OnSerializeSettings(IO::Stream&/*stream*/) {}  // [9] 0x180115470  RET




// ?CreateNewObject@DialogInstance@UI@@SAPEAVBase@Object@@XZ  0x1800454f0
// The retired FACTUI1 row: the locked MemAlloc of 0xb8 bytes, align 8;
// retail's null branch; then ??0DialogInstance (0x180114ca0) inlined. The
// class exports no CreateInPlaceObject.
Object::Base* DialogInstance::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(DialogInstance), 8);
    if (p == nullptr) return nullptr;
    return ::new (p) DialogInstance();
}

// ============================================================================
// UI::DialogInstance::ReleaseFields — ??1DialogInstance 0x180114d40 between
// the vptr store and the inlined ~Internal (ENDGAME B.3.8 block RB, B8-1).
// The real destructor's body (DialogInstance.h); the door that shared it,
// `RtUiDialogInstance_Dtor`, is retired (ENDGAME B.3.9 UI-D). Moved verbatim
// (`self->x` -> `x`); the four details the old door's banner lists hold.
// ============================================================================
void DialogInstance::ReleaseFields() {
    // --- the root Element, through its scalar deleting destructor ------------
    // Retail calls slot 0 with EDX = 1: destruct AND free.
    if (_root) {
        delete _root.Get();
        _root = nullptr;
    }

    // --- every controller, same way. _data is re-read each iteration ---------
    for (int i = 0; i < _controllers._size; ++i) {
        DialogController** data = _controllers._data;   // re-read: see above
        DialogController*  c    = data[i];
        if (c) {
            delete c;
            data[i] = nullptr;
        }
    }

    if (_controllers._data) MemFreeStatic(_controllers._data);
    _controllers._data = nullptr;
    _controllers._size = 0;      // _capacity deliberately untouched

    if (_timers._data) MemFreeStatic(_timers._data);
    _timers._data = nullptr;
    _timers._size = 0;           // _capacity deliberately untouched

    _children.Clear();           // retail 0x1800325f0
}


// ---- accessors -------------------------------------------------------------
Object::InternalPtr<Element> DialogInstance::GetRoot() {       // 0x180045580
    return _root;
}

// THE PARAMETER IS A TYPE ID, NOT AN INDEX (see the door). No null check on
// the controller: retail loads [RCX] off it unguarded.
Object::InternalPtr<DialogController>
DialogInstance::GetController(uint32_t typeId) const {         // 0x180113320
    DialogController* found = nullptr;
    for (int32_t i = 0; i < _controllers._size; ++i) {
        DialogController* c = _controllers._data[i];
        if (c->Type() == typeId) {                             // controller slot 2
            found = _controllers._data[i];
            break;
        }
    }
    return Object::InternalPtr<DialogController>(found);
}

// Forwarders into Element (0x180115650 / 0x180115760). NO NULL CHECK on
// _root — retail has none.
Object::InternalPtr<Element>
DialogInstance::FindElement(const System::String& name) {      // 0x180114c70
    return _root->FindElement(name);
}

Object::InternalPtr<Element>
DialogInstance::FindElementType(uint32_t typeId) {             // 0x1801140c0
    return _root->FindElementType(typeId);
}

// ---- self-contained mutators ------------------------------------------------
void DialogInstance::SetFlagsOnOff(uint32_t on, uint32_t off) { // 0x180114690
    _flags = (on | _flags) & ~off;
}

// Transcribed from the DISASSEMBLY (see the door): 64-bit search bound,
// `data + index*0x18` null test on the SUM, one decrement at the end.
void DialogInstance::StopTimer(int32_t id) {                   // 0x180113730
    const int64_t count64 = static_cast<int64_t>(_timers._size);
    if (count64 > 0) {
        DialogTimer* const base = _timers._data;
        int32_t  i = 0;
        int64_t  walked = 0;
        for (;;) {
            if (base[walked]._id == id) break;
            ++walked;
            ++i;
            if (walked >= _timers._size) return;   // retail: plain RET
        }
        if (i < _timers._size) {
            int64_t k = i;
            do {
                ++i;
                DialogTimer* p = base + k;
                if (i < _timers._size && p != nullptr)
                    *p = p[1];                      // retail: three qword moves
                ++k;
            } while (i < _timers._size);
        }
        _timers._size -= 1;
    }
}

// ---- the three controller-dispatch virtuals ---------------------------------
// No null check on the controller pointer: retail dereferences it unguarded.
void DialogInstance::OnUpdateLayout() {                        // [6] 0x1801153a0
    for (int32_t i = 0; i < _controllers._size; ++i)
        _controllers._data[i]->OnUpdateLayout();               // controller slot 7
}

void DialogInstance::OnUpdate(const System::Time& time) {      // [7] 0x180115400
    for (int32_t i = 0; i < _controllers._size; ++i)
        _controllers._data[i]->OnUpdate(time);                 // controller slot 8
}

// Retail re-reads `*element` into a fresh stack slot on EVERY iteration and
// hands the ADDRESS of that copy on, so a controller that overwrites its
// argument cannot corrupt the caller's InternalPtr. Kept.
void DialogInstance::OnElementEvent(Object::InternalPtr<Element>* element,
                                    int32_t a, int32_t b, int32_t c4,
                                    int32_t d, int32_t e, void* userData) {  // [8] 0x180114ed0
    for (int32_t i = 0; i < _controllers._size; ++i) {
        DialogController* c = _controllers._data[i];
        Object::InternalPtr<Element> local = *element;
        c->OnElementEvent(&local, a, b, c4, d, e, userData);   // controller slot 9
    }
}

// ---- frame leftovers --------------------------------------------------------
// Element offsets are block A's; spelled raw at the use site (Element.h pins
// them). Element vtable slots: 4 ComputeSize, 5 ComputeLayout, 7 Update,
// 9 Contains.
bool DialogInstance::Contains(int32_t x, int32_t y) const {    // 0x180114820
    const Element* const root = _root.Get();
    if ((_flags >> 2) & 1) return false;
    if ((_flags >> 3) & 1)
        return root->Contains(x, y, _ui);                      // Element slot 9
    return root->_layoutLeft <= x && x <= root->_layoutRight &&
           root->_layoutTop <= y && y <= root->_layoutBottom;
}

int32_t DialogInstance::GetMouseCursorIndex(Object::InternalPtr<Element> element,
                                            int32_t x, int32_t y) const {  // 0x180114010
    const Element* const elem = element.Get();
    int32_t result = elem->_mouseCursorIndex;
    const int32_t count = elem->_children._size;
    Element* const* const children = elem->_children._data;
    for (int32_t i = 0; i < count; ++i) {
        Element* child = children ? children[i] : nullptr;
        if (!child) continue;
        if ((child->_flags & 3) != 0) continue;                // the low byte of +0x34
        if (child->_layoutLeft <= x && x < child->_layoutRight &&
            child->_layoutTop <= y && y < child->_layoutBottom)
            result = GetMouseCursorIndex(Object::InternalPtr<Element>(child), x, y);
    }
    return result;
}

void DialogInstance::ComputeLayout(int32_t x, int32_t y, int32_t w, int32_t h) {  // 0x180113d90
    OnUpdateLayout();                                          // own slot 6 — VIRTUAL

    Element* const root = _root.Get();
    auto ComputeSize = [&]() {
        root->ComputeSize();                                   // Element slot 4
    };
    ComputeSize();

    int32_t layoutW = root->_layoutWidth;
    int32_t layoutH = root->_layoutHeight;
    const int32_t alignment = root->_alignment;
    const uint8_t elemFlags = static_cast<uint8_t>(root->_flags);   // the low byte of +0x34
    const int32_t leftPad   = root->_leftPad;
    const int32_t rightPad  = root->_rightPad;
    const int32_t topPad    = root->_topPad;
    const int32_t bottomPad = root->_bottomPad;

    int32_t posX = _upperLeftX + x;
    int32_t posY = _upperLeftY + y;
    if (w < posX + layoutW) posX = w - layoutW;
    if (h < posY + layoutH) posY = h - layoutH;

    const int32_t availRight  = w - rightPad;
    const int32_t availBottom = h - bottomPad;
    posX += leftPad;
    posY += topPad;
    int32_t availW = availRight - posX;
    int32_t availH = availBottom - posY;
    if ((elemFlags >> 2) & 1) layoutW = availW;
    if ((elemFlags >> 3) & 1) layoutH = availH;

    switch (alignment) {
        case 1: case 4: case 7: posX = posX + (availW >> 1) - (layoutW >> 1); break;
        case 2: case 5: case 8: posX = availRight - layoutW; break;
        default: break;   // left-aligned: posX unchanged
    }
    if (alignment > 2) {
        if (alignment < 6)      posY = posY + (availH >> 1) - (layoutH >> 1);
        else if (alignment < 9) posY = availBottom - layoutH;
    }

    ComputeSize();   // retail calls it a second time; reproduced as-is.
    int32_t totalW = root->_layoutWidth + rightPad + leftPad;
    int32_t totalH = root->_layoutHeight + bottomPad + topPad;
    const int32_t minW = root->_minWidth;
    const int32_t minH = root->_minHeight;
    if (totalW < minW) totalW = minW;
    if (totalH < minH) totalH = minH;

    const uint32_t rootFlags = root->_flags;
    if (rootFlags & 0x30) {
        Element* ancestor = root;
        while (ancestor->_parent != nullptr)
            ancestor = ancestor->_parent;
        // Element::_dialogParent -> DialogInstance::_ui -> UserInterface::_uiScale.
        const DialogInstance* dlgParent = ancestor->_dialogParent;
        const float uiScale = dlgParent ? dlgParent->_ui->_uiScale : 1.0f;
        const ::Video::Device* device = ::Video::Device::_singleton;
        if (device) {
            if (rootFlags & 0x20)
                totalW = static_cast<int32_t>(static_cast<float>(device->backBufferWidth) * uiScale);
            if (rootFlags & 0x10)
                totalH = static_cast<int32_t>(static_cast<float>(device->backBufferHeight) * uiScale);
        }
    }

    root->ComputeLayout(                                       // Element slot 5
        posX, posY, posX + totalW, posY + totalH);
}

void DialogInstance::Update(const System::Time& time, UserInterface* ui,
                            Element::UpdateState& state) {     // 0x180113980
    // The 0x30-byte UI event record (see the door's banner). +0x18 is the
    // mouse pair, written as one qword by retail (the UpdateState's +0x18).
    struct EventRecord {
        void*    dialog;
        uint64_t _unused08;
        int32_t  type;
        int32_t  data;
        int32_t  mouseX;
        int32_t  mouseY;
        uint64_t _unused20_28[2];
    };
    static_assert(sizeof(EventRecord) == 0x30, "UI::UserInterface::Event stride");
    static_assert(offsetof(EventRecord, mouseX) == 0x18, "the event's mouse pair");
    // UserInterface::_events (+0x48) through the hand-derived kSpec_Event30
    // row, which leaves bytes [0x24,0x28) of every element UNWRITTEN — see
    // the door's banner for why the trait-derived member is not used.
    auto PushEvent = [&](int32_t type, int32_t data, int32_t mouseX, int32_t mouseY) {
        EventRecord rec{};
        rec.dialog = this;
        rec.type = type;
        rec.data = data;
        rec.mouseX = mouseX;
        rec.mouseY = mouseY;
        auto* events = rtcoll::AsArray<&rtcoll::kSpec_Event30>(&ui->_events);
        events->SetSizeFill(events->GetSize() + 1, &rec);          // retail 0x180033690
    };

    bool anyChildBit1 = false;
    for (auto it = _children.GetFirst(); it; ++it) {
        DialogInstance* child = it->Get();
        anyChildBit1 = anyChildBit1 || ((child->_flags >> 1) & 1) != 0;
    }

    if (!anyChildBit1) {
        OnUpdate(time);                                        // own slot 7 — VIRTUAL

        const int64_t timeTicks = time._ticks;
        // ENDGAME B.3.9 SS1 (B39_RULES D7): the class static now, read fresh.
        const double secondsPerTick = ::System::Timer::_secondsPerTick;
        // DAT_1801616b8: a double, read directly out of the shipped DLL
        // (0.25 seconds) — no export names it, so it is a literal here.
        constexpr double kTooltipDelaySeconds = 0.25;

        if (state.b08 == 0 && state.b09 == 0 && state.leftDown == 0) {
            const int32_t mx = state.mouseX;
            const int32_t my = state.mouseY;
            if (state.mouseCaptured == 0 && Contains(mx, my)) {
                // +0x90/+0x94, NOT the upper-left pair at +0x88 (see the door).
                if (_hoverMouseX == mx && _hoverMouseY == my) {
                    _hoverTime += timeTicks;
                    if (_hoverTime > static_cast<int64_t>(kTooltipDelaySeconds / secondsPerTick))
                        _hovering = 1;
                }
                _hoverMouseX = mx;
                _hoverMouseY = my;
            }
        } else if (_hovering != 0) {
            _hovering = 0;
            PushEvent(0x11, 0, 0, 0);
            _hoverTime = 0;
        }

        // ---- timers: +0x08 is the LIVE countdown (see the door) -----------
        if (_timers._size > 0) {
            RtUI::DialogTimer* data = _timers._data;
            int32_t i = 0;
            while (i < _timers._size) {
                RtUI::DialogTimer& t = data[i];
                t._remaining -= timeTicks;
                if (t._remaining <= 0) {
                    PushEvent(0x13, t._id, 0, 0);
                    for (int32_t k = i; k + 1 < _timers._size; ++k)
                        data[k] = data[k + 1];                 // the 0x18-byte record
                    --_timers._size;
                    continue;   // retail does NOT re-visit the slot just filled
                }
                ++i;
            }
        }

        // ---- root Element::Update (slot 7, five arguments) -----------------
        Element* const root = _root.Get();
        // Slot 7 takes the InternalPtr<DialogInstance> by the ADDRESS of the
        // caller's 8-byte handle and the UpdateState by reference (Element.h's
        // banner).
        Object::InternalPtr<DialogInstance> topDialog(this);
        root->Update(time, ui, &topDialog, state);

        const int32_t mx2 = state.mouseX;
        const int32_t my2 = state.mouseY;
        // bit2 is mouse-passthrough: it SKIPS the hit block (see the door).
        bool hit;
        if ((_flags >> 2) & 1) {
            hit = false;
        } else if ((_flags >> 3) & 1) {
            // Retail 0x180113baf..bba: RCX = [RBX+0x50] (_root), EDX/R8D = mouse
            // x/y, R9 = [RBX+0x10] (_ui), CALL [RAX+0x48] = Element slot 9,
            // Contains(x, y, UserInterface*). B3R owed 1 (behaviour): this used
            // to pass `_dialog` (+0x18) as the fourth argument.
            hit = root->Contains(mx2, my2, _ui);
        } else {
            hit = root->_layoutLeft <= mx2 && mx2 <= root->_layoutRight &&
                  root->_layoutTop <= my2 && my2 <= root->_layoutBottom;
        }
        if (hit) {
            const int32_t px = state.mouseX;
            const int32_t py = state.mouseY;
            if (state.buttonPressed != 0) PushEvent(8,  0, px, py);
            if (state.b03 != 0)           PushEvent(9,  0, px, py);
            if (state.b02 != 0)           PushEvent(0xa, 0, px, py);
            state.leftDown = 0;          state.b08 = 0;        // retail: one WORD store at +0x07
            state.buttonPressed = 0;     state.b02 = 0;        // one WORD store at +0x01
            state.b04 = 0;               state.b05 = 0;        // one WORD store at +0x04
            state.b09 = 0;
            state.b03 = 0;
            state.b06 = 0;
            state.mouseCaptured = 1;
        }
    }

    for (auto it = _children.GetFirst(); it; ++it)
        it->Get()->Update(time, ui, state);
}

void DialogInstance::SetUpperLeft(int32_t x, int32_t y, bool scaled) {  // 0x180114a60
    const float scale = _ui->_uiScale;

    const ::Video::Device* const device = ::Video::Device::_singleton;
    const int32_t screenW = static_cast<int32_t>(
        static_cast<float>(device->backBufferWidth) * scale);
    const int32_t screenH = static_cast<int32_t>(
        static_cast<float>(device->backBufferHeight) * scale);

    if (scaled) {
        x = static_cast<int32_t>(static_cast<float>(x) * scale);
        y = static_cast<int32_t>(static_cast<float>(y) * scale);
    }

    Element* const root = _root.Get();

    // EXT SEAM (NOT RETAIL, docs/android/P11B_DIALOG_SNAP.md; UserInterface::kExtPlaceUpperLeft):
    // the rectangle the upper-left is clamped into. Retail's is the whole layout-space screen; an
    // ext that lays dialogs out inside a safe area answers that area, so the position stored here is
    // the one its next layout keeps (a dialog moved here does not snap at that layout). Called before
    // the size is taken, so what the ext sizes at a dialog's first placement is in it. Null: retail.
    int32_t bounds[4] = {0, 0, screenW, screenH};
    if (UserInterface::_extDialogPlace != nullptr)
        UserInterface::_extDialogPlace(UserInterface::kExtPlaceUpperLeft, this, screenW, screenH, bounds);

    // Element vtable slot 4 — ComputeSize. Pure virtual on the base.
    root->ComputeSize();

    int32_t w = root->_layoutWidth + root->_rightPad + root->_leftPad;
    int32_t h = root->_layoutHeight + root->_bottomPad + root->_topPad;
    const uint32_t flags = root->_flags;
    if (w < root->_minWidth)
        w = root->_minWidth;
    if (h < root->_minHeight)
        h = root->_minHeight;

    if ((flags & 0x30u) != 0) {
        const Element* e = root;
        while (e->_parent != nullptr)
            e = e->_parent;
        const DialogInstance* dlg = e->_dialogParent;
        if ((flags & 0x20u) != 0) {
            const float s = dlg->_ui->_uiScale;
            w = static_cast<int32_t>(static_cast<float>(device->backBufferWidth) * s);
        }
        if ((flags & 0x10u) != 0) {
            const float s = dlg->_ui->_uiScale;
            h = static_cast<int32_t>(static_cast<float>(device->backBufferHeight) * s);
        }
    }

    // THE CLAMP ORDER IS LOAD-BEARING (see the door).
    // `bounds` is {0, 0, screenW, screenH} (retail's four compares) unless the seam above answered.
    if (x < bounds[0]) x = bounds[0];
    if (w + x > bounds[2]) x = bounds[2] - w;
    if (y < bounds[1]) y = bounds[1];
    if (h + y > bounds[3]) y = bounds[3] - h;

    const int32_t dx = x - _upperLeftX;
    const int32_t dy = y - _upperLeftY;

    if (dx != 0 || dy != 0) {
        Element* const r = _root.Get();
        r->_layoutLeft   += dx;
        r->_layoutRight  += dx;
        r->_layoutTop    += dy;
        r->_layoutBottom += dy;

        const int32_t n = r->_children._size;
        for (int32_t i = 0; i < n; ++i) {
            Element* child = r->_children._data[i];
            if (child) child->AdjustUpperLeft(dx, dy);          // 0x180115840
        }
        if (Element* border = r->_border)
            border->AdjustUpperLeft(dx, dy);

        _upperLeftX = x;
        _upperLeftY = y;
    }
}

// ============================================================================
// UI::Dialog
// ============================================================================

// Six instructions, no bounds check; MOVSXD, so a negative index reads BEFORE
// the array, faithfully.
Object::ExternalPtr<ElementDescription>
Dialog::GetTemplate(int32_t index) const {                     // 0x1800388b0
    return Object::ExternalPtr<ElementDescription>(_templates[static_cast<int64_t>(index)]);
}

// The same six instructions over the +0x78 array (Dialog.h's table: `*out =
// ((void**)this[0x78])[index]`): no bounds check, MOVSXD index. The exe reaches
// it for a dialog's referenced resources (EventNotifyDialog's popup template,
// 0x140067d88 with R8D = 0).
Object::ExternalPtr<Object::External>
Dialog::GetReference(int32_t index) const {                    // 0x1800388d0
    return Object::ExternalPtr<Object::External>(_references[static_cast<int64_t>(index)]);
}

// "Make an element from this description, unless a NAMED TEMPLATE overrides
// it." Full evidence (the inline strcmp, the slot-8 dispatch, the release
// order) at the door above.
Object::InternalPtr<Element>
Dialog::CreateElement(Object::ExternalPtr<ElementDescription> description) const {  // 0x180112ed0
    ElementDescription* const desc = description.Get();
    Object::InternalPtr<Element> result;
    if (!desc) return result;

    char* name = nullptr;
    {
        // identTmp's destructor runs before pathTmp's — retail's order.
        System::Filename pathTmp = desc->GetPath();                 // 0x180006250
        const System::Filename identTmp = pathTmp.GetPathIdentifer();
        name = identTmp._data;
        if (name) { ++RtSystem::StringRefCount(name); }   // 0x180112f3e INC [RCX-4]
    }

    const ElementDescription* target = desc;   // the no-match answer: the argument itself
    bool  matched = false;
    const int32_t count = _namedTemplates._size;
    for (int32_t i = 0; i < count; ++i) {
        const RtUI::DialogNamedTemplate& e = _namedTemplates[i];
        const int32_t lenOurs   = name    ? RtSystem::StringLength(name)    : 0;
        const int32_t lenTheirs = e._name._data ? RtSystem::StringLength(e._name._data) : 0;
        if (lenOurs != lenTheirs) continue;

        if (!name)             { matched = true; }            // 0x180112fd5
        else if (lenOurs == 0) { matched = true; }            // 0x180112fdb
        else if (!e._name._data) { matched = false; }           // LEA ECX,[R8+1] -> 1
        else {
            // The inlined strcmp at 0x180112fe8..0x180113021, transcribed.
            const char* a = name;
            const char* b = e._name._data;
            size_t k = 0;
            unsigned char c = static_cast<unsigned char>(a[0]);
            if (c == static_cast<unsigned char>(b[0])) {
                while (c != 0 && b[k] != 0) {
                    ++k;
                    c = static_cast<unsigned char>(a[k]);
                    if (c != static_cast<unsigned char>(b[k])) break;
                }
            }
            const int diff = static_cast<int>(static_cast<signed char>(a[k])) -
                             static_cast<int>(static_cast<signed char>(b[k]));
            matched = (diff == 0);
        }
        if (matched) { target = e._description; break; }
    }

    if (matched && !target) {
        // 0x180113045: matched but a null description — 0, no dispatch.
        result = Object::InternalPtr<Element>();
    } else {
        // The description's slot 8 (CreateInstance, RETSLOT, B6-2) — a
        // virtual on UI::ElementDescription, dispatched through the leaf's
        // own table (fragment pass F2: was a hand-inlined vtbl[8]).
        target->CreateInstance(&result, this);
    }

    if (name) ReleaseStringLocal(name);   // 0x180113061..0x180113087
    return result;
}

// ============================================================================
// UI::DialogController
// ============================================================================
Object::InternalPtr<Element> DialogController::GetContainer() {  // 0x18003b700
    return Object::InternalPtr<Element>(_container);
}

// ============================================================================
// UI::ElementDescription — ENDGAME B.3.6 fragment pass F2
// ============================================================================
// Block A declared these on the class and left them undefined because their
// bodies were registered doors in THIS file. They moved here unchanged except
// `self->x` / `d + 0x..` became member access; the doors above delegate
// QUALIFIED. Per-body evidence (the retail disassembly) stays at each door.


// ?TypeId@ElementDescription@UI@@SAIXZ 0x1800350e0 — the record's first
// dword (ENDGAME B.3.9 UI-D).


// ?CreateNewObject@ElementDescription@UI@@SAPEAVBase@Object@@XZ       0x180035100
// ?CreateInPlaceObject@ElementDescription@UI@@SAPEAVBase@Object@@PEAX@Z 0x180035110
// Both `xor eax,eax; ret`: the class is abstract (the retired FACTUI1 row).
Object::Base* ElementDescription::CreateNewObject() { return nullptr; }
Object::Base* ElementDescription::CreateInPlaceObject(void*) { return nullptr; }

// `MOV EAX,0x4 / RET` — the .rsc element-description file version.
int32_t ElementDescription::GetFileVersion() const {               // [3] 0x180116af0
    return 4;
}

// ??0ElementDescription@UI@@QEAA@XZ 0x180115a40 MINUS the inlined External
// ctor and the three vptr stores: the body of `ElementDescription()`
// (ElementDescription.h). Retail writes these as QWORD pairs; the bytes are
// identical.
void ElementDescription::InitFields() {
    _x = 0;              _y = 0;              // +0x20
    _width = 0;          _height = 0;         // +0x28
    _minWidth = 0;       _minHeight = 0;      // +0x30
    _tabOrder = 0;                            // +0x38
    _id = 0xFFFFFFFFu;                        // +0x3c
    _elementType = 3;    _flags = 0;          // +0x40 (QWORD 3)
    _childDesc = nullptr;                     // +0x48
    _toolTipDialog = nullptr;                 // +0x50
    _stringTable = nullptr;                   // +0x58
    // +0x60: the member `_textName`'s String() (B4 Y RT-UI).
}

// ??1ElementDescription@UI@@UEAA@XZ 0x180116100: the one String release (+0x60)
// is the member `_textName`'s ~String (B4 Y RT-UI: the hand-written
// ReleaseFields, with the same two arms, is gone).


// ?CreateInstance@ElementDescription@UI@@QEBAXV?$InternalPtr@VElement@UI@@@Object@@@Z
// 0x180115ad0. RULE B6-11: the handle arrives by HIDDEN POINTER (retail
// dereferences RDX eleven times), so the member takes `InternalPtr<Element>*`.
// The full evidence block (facts (a)..(c)) is at the door above.
void ElementDescription::CreateInstance(Object::InternalPtr<Element>* element) const {
    // ---- 1. elem->_name = this->GetPath().GetPathIdentifer() --------------
    // Object::External::GetPath (0x180006250) and System::Filename::
    // GetPathIdentifer, both members.
    Element* elem = element->Get();

    // The inner scope ends the two temporaries' lives where retail's two
    // inlined releases are, in retail's order (identTmp, then pathTmp).
    {
        System::Filename pathTmp = Object::External::GetPath();
        System::Filename identTmp;
        identTmp = pathTmp.GetPathIdentifer();

        char*    want = identTmp._data;
        // Element::_name at +0x10: retail's inlined assign stays.
        char*    old  = elem->_name;
        if (old != want) {
            if (old) {
                if (--RtSystem::StringRefCount(old) == 0) {
                    // Retail's asymmetry: the FREEING branch leaves the FIELD
                    // holding `data - 8` and frees that; sharing nulls it.
                    elem->_name = old - 8;
                    MemFreeStatic(elem->_name);
                } else {
                    elem->_name = nullptr;
                }
            }
            elem->_name = want;
            if (want) ++RtSystem::StringRefCount(want);
        }
    }

    // ---- 2. the eleven scalar copies --------------------------------------
    // The handle is RE-READ before every store, exactly as retail does. Nothing
    // in between can change it, but the re-read is what the shipped bytes do
    // and transcribing it costs nothing. The description's +0x20..+0x2c land
    // on the element's four PADS, its +0x3c on the COLOUR and its +0x38 on the
    // mouse-cursor index (retail's crossing; the description's names are the
    // tree's guesses).
    #define ED_ELEM  (element->Get())
    ED_ELEM->_alignment        = static_cast<int32_t>(_elementType);   // +0x30
    ED_ELEM->_leftPad          = _x;                                   // +0x18
    ED_ELEM->_rightPad         = _y;                                   // +0x1c
    ED_ELEM->_topPad           = _width;                               // +0x20
    ED_ELEM->_bottomPad        = _height;                              // +0x24
    ED_ELEM->_minWidth         = _minWidth;                            // +0x28
    ED_ELEM->_minHeight        = _minHeight;                           // +0x2c
    ED_ELEM->_flags           |= _flags;                               // +0x34  OR, never assign
    ED_ELEM->_color            = _id;                                  // +0x38  _id -> +0x38
    ED_ELEM->_mouseCursorIndex = _tabOrder;                            // +0x3c  _tabOrder -> +0x3c
    ED_ELEM->_toolTipDialog    = _toolTipDialog;                       // +0x40

    // ---- 3. the tool-tip UIString -----------------------------------------
    StringTable* const table = _stringTable;
    int32_t index = -1;
    if (table) {
        index = table->GetIndexFromName(_textName);                    // 0x180002470
    }
    elem = ED_ELEM;
    elem->_toolTip.table = table;                                      // +0x48
    // Fact (b): retail writes a QWORD here whose upper half is uninitialised
    // stack. The port writes the DWORD only.
    elem->_toolTip.index = index;                                      // +0x50

    // ---- 4. elem->_description = this -------------------------------------
    ED_ELEM->_description = this;                                      // +0xb8

    // ---- 5. the UI::Dialog temporary and the one virtual call -------------
    alignas(16) uint8_t tempDialog[kDialogTempSize];
    UI::Dialog* const tempDlg = ::new (static_cast<void*>(tempDialog)) UI::Dialog();

    Object::InternalPtr<Element>  slot;
    Object::InternalPtr<Element>* ret = &slot;
    if (_childDesc != nullptr) {
        // The child description's slot 8 (CreateInstance, RETSLOT, B6-2) — a
        // virtual on this class, dispatched through the child's own table.
        ret = _childDesc->CreateInstance(&slot, tempDlg);
    } else {
        slot = Object::InternalPtr<Element>();
        ret  = &slot;
    }
    ED_ELEM->_border = ret->Get();                                     // +0xb0

    // ---- 6. the flags cascade (fact (c): the inner walk is unreachable) ---
    elem = ED_ELEM;
    if (elem->_toolTipDialog != nullptr &&
        elem->_toolTip.table != nullptr &&
        elem->_toolTip.index >= 0 &&
        (elem->_flags & 0x800000u) == 0) {
        const uint32_t before = elem->_flags;
        const uint32_t after  = before | 0x90000u;
        elem->_flags = after;
        if ((static_cast<uint8_t>(before ^ after) & 1u) != 0) {
            Element* w = elem;
            do {
                w->_flags |= 0x8000000u;
                w = w->_parent;
            } while (w != nullptr);
        }
    }

    #undef ED_ELEM

    // ---- the temporary's destructor ---------------------------------------
    tempDlg->UI::Dialog::~Dialog();   // ~Dialog: see fact (a)
}

}  // namespace UI

// ============================================================================
// ENDGAME B.3.9 TI-UI (B39 D6): UI::ElementDescription's own type record, built at CRT init
// (head-insert into Object::BaseTypeInfo::_firstTypeInfo). Literals copied from
// the retired minted row ?_typeInfo@ElementDescription@UI@@ (0x1801faaf8): name "ElementDescription", parent
// "Object::External" (retail ?_ParentTypeName@ElementDescription@UI@@), no file extension; factories:
// the class's own.
// ============================================================================
DEFINE_TYPE(UI::ElementDescription, "Object::External", nullptr)

// ============================================================================
// ENDGAME B.3.9 TI-UI (B39 D6): UI::DialogInstance's own type record, built at CRT init
// (head-insert into Object::BaseTypeInfo::_firstTypeInfo). Literals copied from
// the retired minted row ?_typeInfo@DialogInstance@UI@@ (0x1801faac0): name "DialogInstance", parent
// "Object::Internal" (retail ?_ParentTypeName@DialogInstance@UI@@), no file extension; factories:
// CreateNewObject the class's own, CreateInPlaceObject Object::Internal's (inherited).
// ============================================================================
DEFINE_TYPE(UI::DialogInstance, "Object::Internal", nullptr)
