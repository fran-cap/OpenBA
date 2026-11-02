// ============================================================================
// src/engine/save/saveload_update.cpp — Wave 13 — the SaveLoad state machines
// ============================================================================
//
// Wave 5 lane C ported 16 of Save::SaveLoad's exports and left the five
// UpdateXxx bodies plus OnVideoRestart on the DLL, so `RtSave_SaveLoad_Update`
// — which is ours — dispatched into retail five ways. This file closes that.
// Every body below was decompiled AND disassembled at its real address in the
// SHIPPED Runtime-x64.dll (image base 0x180000000) on 2026-09-09.
//
//   0x1800c0f10  ?OnVideoRestart@SaveLoad@Save@@QEAAXXZ
//   0x1800c1ea0  ?UpdateList@SaveLoad@Save@@QEAAXXZ
//   0x1800c2560  ?UpdateLoadFromFile@SaveLoad@Save@@QEAAXXZ
//   0x1800c2690  ?UpdateLoad@SaveLoad@Save@@QEAAXXZ
//   0x1800c2a80  ?UpdateSave@SaveLoad@Save@@QEAAXXZ
//   0x1800c2d70  ?UpdateDelete@SaveLoad@Save@@QEAAXXZ
//
// ----------------------------------------------------------------------------
// THE HAZARD, restated from saveload.cpp because it is sharper here.
//
// A wrong port in this file does not crash. It desyncs a save, or deletes the
// wrong file, and surfaces later on an innocent record. Wave 5 chose its oracle
// shape BEFORE writing each body for exactly that reason, and used three:
// FULL DIFF, SCRATCH-COPY DIFF, and (declared as such) NONE.
//
// **None of those three is available to this file, and that is the whole
// problem it had to solve first.** These six bodies:
//
//   * are not read-only or idempotent, so FULL DIFF would delete a file twice,
//     begin two async loads, and signal two events;
//   * do not confine their writes to `*this`, so there is no scratch extent to
//     copy — UpdateList writes through `_savesData` into a heap array it also
//     reallocates, and OnVideoRestart writes into the Video::Device singleton;
//   * are guard-driven state machines, which is the shape that defeats replay
//     even when replay is otherwise possible: our body advances `_progress`,
//     so retail running second takes a DIFFERENT branch and the compare passes
//     while the branch under test never ran.
//
// Each body below was written against the live sink (retired, B.4 R-IO):
// every store goes through `out` and lands for real.
// ============================================================================

#include <atomic>
#include <cstdint>
#include <new>
#include <cstdio>
#include <cstring>

#include "platform_sync.h"   // ResetEvent / SetEvent -- UpdateList state 0 calls both

#include "saveload_internal.h"
#include "io/Persistence.h"
#include "engine/MainLoop.h"   // ENDGAME B.3.9 ENG: the class static
#include "engine/UpdateLoop.h" // B3X W0: SaveRestore by name

#include "io/Modification.h"   // UpdateLoad reorders the mod list
#include "collection/Collection.h"
#include "video/CompanionData.h"  // ENDGAME B.3.5: Video::ImageBufferData
#include "video/ImageBuffer.h"     // ENDGAME B.3.5: Video::ImageBuffer
#include "video/Device.h"          // ENDGAME B.3.9 SS1: Video::Device::_singleton
#include "../collection/array_internal.h"   // rtcoll::AsArray, ArraySpec -- B.3.2 step 2
#include "system/string_internal.h"              // RtString_ReleaseInline
#include "rt_libm.h"   // RtCrt_Qsort_18007f050
#include "io/PersistenceWorker.h"   // the two worker enqueue bodies
#include "load_time.h"   // [LoadTime] stamps (NOT RETAIL, print-only; block LOADSTAMP)

using RtSave::SaveLoad;
using RtSave::SavedState;
using RtSave::SaveInfo;
using RtSave::Operation;

using RtSaveDetail::MemAlloc;
using RtSaveDetail::MemFree;
using RtSaveDetail::PersistenceSingleton;
using RtSaveDetail::ZeroTime;

// src/engine/video/buffers_lock.cpp. These are the INLINE COPIES of the device
// lock — the ones that re-read `Device::_singleton` themselves and store the
// owner id BEFORE the depth — which is exactly the sequence OnVideoRestart
// carries at 0x1800c0f5a..0x1800c0f77. The exported Video::Device::Lock is a
// different function with the two stores in the other order; using it here
// would be a normalisation, not a transcription.
// (RtVideoDeviceLock / RtVideoDeviceUnlock are declared in video/Device.h.)

// ENDGAME B.3.2 step 2 — two Collection names with external linkage, declared
// at FILE SCOPE (not inside the anonymous namespace below, which would give a
// re-declaration internal linkage and sever it from the real definition):
//   kSpec_MetaData58              src/engine/collection/array.cpp
//   RtColl_ArrayDtorElem_18002ac40  src/engine/collection/array_destroy.cpp
// Both sit in `namespace rtcoll { ... }` there (non-static, non-anonymous),
// and are not declared `extern`/forward-declared in any header this batch may
// edit, so they are declared here instead. See the banners on `kSpec_MetaData58`
// and on `RtColl_ArrayDtorElem_18002ac40`'s call site below for why each is
// used the way it is.
namespace rtcoll { extern const ArraySpec kSpec_MetaData58; }
namespace rtcoll { void* RtColl_ArrayDtorElem_18002ac40(void* self); }

namespace {


// ---- dependencies, all of them REAL exports in the shipped table -----------
// Checked with pefile against C:\GOG Games\Banished\Runtime-x64.dll before
// each name was written down, never copied out of Ghidra's demangler
// (reference_resolveexport_phantom_names.md). Every one is called by name.
//
// B.4 R-IO: the live sink (`src/engine/rt_sink.h`) these bodies were
// written against is gone from this file. It was a pass-through left over from
// the predictive shadow oracle — every `out.U32(&f, v, label)` was `f = v`,
// every `out.ReadPtr(&f)` was `f`, and every `if (out.Call(...))` was an
// unconditional call — so the bodies below are the same stores, loads and
// calls, spelled plainly. The ORACLE paragraphs in the banners describe the
// retired harness; the instruction-level notes in them still stand.

// The Video::Device lock takes the device singleton's cell, typed since B.4
// R-VIDEO (video/Device.h).
// Wave 15 lane ORDER: was a frozen static (see saveload_internal.h's note).
// ENDGAME B.3.9 SS1 (B39_RULES D7): the class static's address IS this cell.
Video::Device** DeviceSingletonSlot() {
    return &::Video::Device::_singleton;
}

// ============================================================================
// Save::SaveLoad::UpdateDelete()        retail 0x1800c2d70   ordinal 4514
// ----------------------------------------------------------------------------
// The simplest of the five, and the only one that calls no unexported internal
// at all: two states, one exported callee.
//
//   state 0  point `_string20` at the selected save's name (the refcount idiom,
//            skipped entirely when the two pointers are already equal), ask
//            IO::Persistence to begin the delete, advance to state 1.
//   state 1  unconditionally finish: clear the operation, reset the timer to
//            System::Time(0.0f), clear the progress, and put `_selectedIndex`
//            back to -1.
//
// NOTE WHAT STATE 1 DOES NOT DO. Its three siblings (UpdateLoad, UpdateSave,
// UpdateList) gate their identical tail on `_minOperationTime < _elapsed`, the
// minimum-spinner-time rule. UpdateDelete does NOT — 0x1800c2e0b is a bare
// `CMP EAX,0x1` with no time compare anywhere in the body. A delete therefore
// completes on the very next tick regardless of SetMinOperationTime. That
// asymmetry looks exactly like the kind of thing a porter "cleans up"; it is
// retail, it is deliberate on retail's part or not, and it is transcribed.
//
// ORACLE: PREDICTIVE, and this body is the one that makes the round mean
// something — its state-1 arm writes four fields whose predicted values differ
// from their pre-values on every real delete, so `changed=` is non-zero rather
// than the vacuous green a body of no-op predictions would produce.
// ============================================================================
}  // namespace

// ENDGAME B.3.7 B2: the body is the member (it was `UpdateDeleteBody(self, out)`).
void Save::SaveLoad::UpdateDelete() {
    const uint32_t progress = _progress;

    if (progress == 0) {
        // `CMP RAX,qword ptr [RSI+RBP*0x1+0x38]` / `JZ` — when the slot already
        // holds this exact pointer, retail skips BOTH halves of the idiom, so
        // the refcount is not bumped and not dropped: String::operator=
        // (0x1800c9810), inlined. The index is sign-extended (MOVSXD).
        _string20 = _saves._data[_selectedIndex].name;

        PersistenceSingleton()->BeginDelete(_string20);

        _progress = 1;
        return;
    }

    if (progress == 1) {
        // Transcribed in retail's store order (0x1800c2e15 .. 0x1800c2e2e):
        // operation, progress, selectedIndex, then the converted time last.
        _operation     = kOpNone;
        _progress      = 0;
        _selectedIndex = -1;
        _elapsed       = ZeroTime();
    }
}

namespace {

// ============================================================================
// Save::SaveLoad::OnVideoRestart()      retail 0x1800c0f10   ordinal 3296
// ----------------------------------------------------------------------------
// Walks every SaveInfo and re-uploads its thumbnail after a device reset:
// `ImageBuffer::Recreate`, then, under the Video::Device lock, one virtual
// upload call with the saved pixel block and its dimensions.
//
// The guard is TWO conditions and both matter: the ImageBuffer pointer at
// SaveInfo+0x18 must be non-null AND the int at SaveInfo+0x40 must be >= 0.
// That second one is the same "has metadata been loaded for this row yet"
// sentinel UpdateList uses at 0x1800c1f8c and 0x1800c2d2b; a row still waiting
// on its metadata has a live ImageBuffer and an unloaded thumbnail, and
// uploading it would push a garbage block at the GPU.
//
// FIELDS THIS BODY PINS. SaveInfo+0x18/+0x20/+0x24/+0x28/+0x40 were all inside
// the `unknown10`/`unknown40` holes that Wave 5's header declared explicitly
// unmodelled ("written by UpdateList/LoadMetaData, neither of which this lane
// ports"). This wave ports both, so the header now names them.
//
// ORACLE: PREDICTIVE, AND DELIBERATELY THIN — said here rather than left for a
// reader to discover from a green board. This body performs almost no writes
// of its own: its entire effect is two calls, and everything either of them
// changes lives inside the ImageBuffer and the GPU resource. So the round
// predicts only the device lock's owner/depth pair, and `Call()` records the
// suppressed targets and arguments. What that DOES verify is worth stating
// precisely, because it is not nothing and it is not everything:
//
//   VERIFIED  which rows are selected (the two-part guard), the iteration
//             order and stride, the three argument values per row, and the
//             identity of the vtable slot — all of these are recorded per call
//             and compared against the targets retail loads.
//   NOT       what Recreate and the upload actually do. Both footprints are
//             declared with Exclude(), and the site prints `excluded=`.
//
// A round on this site will report `changed=0` whenever the lock is balanced,
// which the oracle correctly prints as UNPROVEN rather than as passing. That
// is the honest reading: the argument trace is the evidence here, not the
// post-state.
// ============================================================================
}  // namespace

// ENDGAME B.3.7 B2: the body is the member (it was `OnVideoRestartBody(self, out)`).
void Save::SaveLoad::OnVideoRestart() {
    const int32_t count = _saves._size;
    for (int32_t i = 0; i < count; ++i) {
        const SaveInfo& row = _saves._data[i];

        Video::ImageBuffer* buffer = row.imageBuffer;
        if (buffer == nullptr) continue;
        if (row.saveIndex < 0) continue;

        // ENDGAME B.3.5: `?Recreate@ImageBuffer@Video@@QEAAXXZ` (0x1800f2fd0) is a
        // member of the real `Video::ImageBuffer`, defined in
        // src/engine/video/video_resources.cpp. `buffer` is the save row's
        // +0x18 `Video::ImageBuffer*`. The `out.Call(label, target, …)` wrapper
        // went with the door pointer it labelled: `RtSink::Call` ignores every
        // argument and returns true unconditionally (rt_sink.h).
        buffer->Recreate();

        Video::Device** device = DeviceSingletonSlot();
        RtVideoDeviceLock(device);

        // Retail RE-READS `_savesData` here (0x1800c0f7d) rather than reusing
        // the pointer it loaded before the lock. Transcribed: Recreate can run
        // arbitrary code, and the re-read is what retail relies on.
        const SaveInfo& row2 = _saves._data[i];
        Video::ImageBufferData* obj = row2.imageBuffer->_data;

        // ENDGAME B.3.5: `vtbl+0x28` is slot 5 of the real abstract
        // `Video::ImageBufferData`, `bool Update(void*, int, int)`
        // (CompanionData.h). The receiver is PROVED by the code that produced it:
        // `target` is the row's `Video::ImageBuffer*` and `obj` is its +0x60
        // companion. Retail ignores the result and so does this. The
        // `out.Call(label, target, ...)` wrapper went with the slot pointer it
        // labelled: `RtSink::Call` ignores every argument and returns true
        // unconditionally (rt_sink.h), so it guarded nothing.
        obj->Update(row2.thumbPixels, row2.thumbWidth, row2.thumbHeight);

        RtVideoDeviceUnlock(device);
    }
}

namespace {


// ============================================================================
// THE SAVE/LOAD WORKER ENQUEUE — two unexported internals, called by address.
// ----------------------------------------------------------------------------
// 0x1800b7460 (load) and 0x1800b75c0 (save) are the same shape: copy the
// filename into the Persistence worker's own String slot, reset its counters,
// resize its per-slot pointer array, match each SaveBlock's id against the
// worker's slot table, then ResetEvent/SetEvent to hand the job to the thread.
// Neither is exported under ANY name — checked against the shipped export
// table, not inferred from Ghidra's FUN_ prefix, because a source grep alone
// cannot tell an unexported internal from an export nobody has ported yet
// (reference_grep_cannot_tell_internal_from_unported_export.md).
//
// So they are ADDRESS doors, and they go through the port-aware ResolveAddr,
// not through the raw image: ResolveAddr(Module::Runtime, a) consults the port
// registry FIRST (reference_resolveaddr_consults_port_registry_first.md), so
// the day either one is ported this file picks it up with no edit here.
//
// Their first argument is `persistence + 0x10`, NOT the Persistence itself.
// That sub-object is the worker; passing the container would write the queue
// state 0x10 bytes early, over fields nothing in this project models.
// ----------------------------------------------------------------------------
using WorkerEnqueueFn = void (*)(void* worker, const char** name, int count,
                                 const RtIo::SaveBlock* blocks);

// B3R RT-IO: 0x1800b7460 / 0x1800b75c0 are RtIo_PersistenceWorker_EnqueueLoad /
// EnqueueSave (persistence_worker.cpp), called by name.

// ---- the rest of the exported dependency set --------------------------------
// SetupLoadFn moved to saveload_internal.h in Wave 14 — the port of
// ?SetupLoad@SavedState@Save@@ needs the same shape, and this one was
// unreachable from another file inside this anonymous namespace. One
// definition; see the comment there for why drift in THIS shape is a wild
// write rather than a compile error.
using RtSaveDetail::SetupLoadFn;
using SaveMetaDataFn  = void     (*)(SaveLoad* self, void* pixels, int w, int h,
                                     const char** nameByValue, int mode);
using NameGetterFn    = const char** (*)(const SaveLoad* self, const char** ret);

// EVERY ONE of these returns a class BY VALUE, so the real ABI carries a
// HIDDEN RETURN SLOT in RDX and the caller's arguments shift right by one.
// Ghidra's C output hides it — it prints `GetSaveFilename(_singleton, &tmp)`
// for what the disassembly shows as three registers
// (0x1800c25e6: `LEA R8,[RBX+0x70]` is the NAME; `LEA RDX,[RSP+0x50]` is the
// slot). A typedef that omits the slot is a wild write that still returns the
// right answer, which is why it survives testing
// (reference_hidden_retslot_wild_write.md). Confirmed register by register in
// the disassembly of both call sites before these lines were written.

// ============================================================================
// Save::SaveLoad::UpdateLoadFromFile()  retail 0x1800c2560   ordinal 4521
// ----------------------------------------------------------------------------
// The "load the file the user named" path, as distinct from UpdateLoad's "load
// the row the user picked". Three states:
//
//   state 0  ask Persistence for the save-game info of `_saveName`, go to 1.
//   state 1  allocate a fresh SavedState{_id=1,_size=0,_data=null}, publish it
//            at `_saveState`, have it build the SaveBlock it wants filled, and
//            hand filename + block to the worker. Go to 2.
//   state 2  finish: clear operation, reset the timer, clear progress, put
//            `_selectedIndex` back to -1.
//
// LIKE UpdateDelete AND UNLIKE ITS OTHER THREE SIBLINGS, state 2 has NO
// `_minOperationTime` gate — 0x1800c264c is a bare `CMP EAX,0x2` and there is
// no time compare anywhere in the body. Two of the five skip the minimum
// spinner time and three enforce it; that split is retail's, and it is
// transcribed rather than regularised.
//
// THE ALLOCATION IS STORED EVEN WHEN IT FAILS. `MOV qword ptr [RBX+0x40],RAX`
// at 0x1800c25c8 sits AFTER the `TEST RAX,RAX / JZ` merge point, so a failed
// MemAlloc publishes a NULL `_saveState` and SetupLoad is then called on it.
// That is retail's behaviour on an OOM path and this port reproduces it: the
// project's standing rule is that an unguarded deref is load-bearing and a
// guard skips behaviour rather than restoring it.
//
// ORACLE: PREDICTIVE. One location is deliberately EXCLUDED rather than
// predicted — `_saveState`, because its value is whatever the allocator hands
// back and no predictor can know the address retail's MemAlloc will return.
// Excluding it keeps the coverage control honest: the byte really is not
// covered, and `excluded=` says so rather than a silent pass.
// ============================================================================
}  // namespace

// ENDGAME B.3.7 B2: the body is the member (it was `UpdateLoadFromFileBody(self, out)`).
void Save::SaveLoad::UpdateLoadFromFile() {
    const uint32_t progress = _progress;

    if (progress == 0) {
        // `LEA R8D,[RAX+0x2]` with RAX known zero on this path — the constant
        // is 2, spelled that way by the compiler to reuse a zeroed register.
        PersistenceSingleton()->BeginGetSaveGameInfo(_saveName, 2);
        _progress = 1;
        return;
    }

    if (progress == 1) {
        // `LEA ECX,[RAX+0xf]` with RAX == 1 on this path: the size is 0x10.
        SavedState* fresh = static_cast<SavedState*>(MemAlloc(0x10, 4));
        if (fresh != nullptr) {
            // Retail writes the first TWO fields as one qword store of 1:
            // `MOV qword ptr [RAX],0x1` is _id=1 AND _size=0 together.
            fresh->_id   = 1;
            fresh->_size = 0;
            fresh->_data = nullptr;
        }
        _saveState = fresh;

        RtIo::SaveBlock block{};
        // ENDGAME B.3.7 B2: `SavedState::SetupLoad` is a member of this block's
        // class, called directly (it was an RT_EXPORT_ONCE door onto our own
        // port). Its SaveBlock return is the hidden slot `&block` in retail.
        // `fresh` is NOT null-tested: retail calls SetupLoad on a failed
        // allocation too (see the banner).
        block = fresh->SetupLoad();

        ::IO::Persistence* persistence = PersistenceSingleton();
        if ((persistence->_worker._runFlags & 1) == 0) {
            // GetSaveFilename's by-value Filename is born in retail's hidden
            // slot; the worker enqueue (0x1800b7460) takes the EMBEDDED
            // worker at Persistence+0x10; the Filename is released right
            // after, as its destructor.
            System::Filename filename = persistence->GetSaveFilename(_saveName);
            RtIo_PersistenceWorker_EnqueueLoad(&persistence->_worker,
                                               &filename.AsString(), 1, &block);
        }

        _progress = 2;
        return;
    }

    if (progress == 2) {
        // Retail's store order at 0x1800c2651..0x1800c2672: operation, elapsed,
        // progress, selectedIndex — note this is NOT UpdateDelete's order,
        // which puts elapsed last. Both are transcribed as written.
        _operation     = kOpNone;
        _elapsed       = ZeroTime();
        _progress      = 0;
        _selectedIndex = -1;
    }
}

namespace {

// ============================================================================
// THE MOD-ENTRY LIFECYCLE TRIPLE — three unexported internals, called by
// address, and ALREADY PORTED BY ANOTHER LANE.
// ----------------------------------------------------------------------------
// UpdateLoad's state 1 moves an IO::Mod record from index k to index k-1 with
// the copy-construct / assign / assign / destroy dance MSVC emits for
// `std::swap` on a class with a user-declared copy assignment:
//
//     Mod tmp;  Mod::Mod(&tmp, &mods[k]);      // 0x1800101e0
//     mods[k] = mods[k-1];                     // 0x180026ef0
//     mods[k-1] = tmp;                         // 0x180026ef0
//     tmp.~Mod();                              // 0x180011460
//
// NONE of the three is exported — checked with pefile against the shipped
// table, not inferred from Ghidra's `FUN_` prefix, because a source grep
// cannot tell an unexported internal from an export nobody has ported yet
// (reference_grep_cannot_tell_internal_from_unported_export.md).
//
// ALL THREE ARE ALREADY WRITTEN. `src/engine/io/modification.cpp` (Wave 9
// lane MOD) ports them as RtIo_Mod_CopyCtor_1800101e0 / _Assign_180026ef0 /
// _Dtor_180011460 and registers all three with RegisterRuntimeAddr. So these
// doors go through the port-aware ResolveAddr, which consults the port
// registry FIRST (reference_resolveaddr_consults_port_registry_first.md), and
// this lane gets lane MOD's bodies without writing a line of Mod lifecycle
// code or declaring a cross-lane symbol. Grepping for the port before planning
// to write it is what this project's own
// reference_grep_for_the_port_before_writing_it.md asks for, and it saved the
// whole IO::Mod deep-copy surface here.
//
// WHY NOT CALL ?Swap@Modification@IO@@QEAAXHH@Z, WHICH IS THE SAME FIVE LINES
// AND IS ALSO PORTED. Because retail's UpdateLoad does not call it — it
// INLINES the triple, including the dirty-flag store, and Swap is a separate
// exported body at 0x1800aeb20 that this function never reaches. Routing
// through it would be a normalisation, and it would also nest lane MOD's own
// RT_DIFF shadow site inside this file's predictive round.
// ----------------------------------------------------------------------------
// B.4 acceptance MOD: the three are RtIO::Mod's copy ctor, operator= and
// destructor (Modification.h / modification.cpp).

// ============================================================================
// Save::SaveLoad::UpdateLoad()          retail 0x1800c2690   ordinal 4520
// ----------------------------------------------------------------------------
// The "load the row the user picked" path, as distinct from UpdateLoadFromFile's
// "load the file the user named". Three states, and the middle one is the
// largest single body in this file:
//
//   state 0  ask Persistence for the save-game info of `saves[_selectedIndex]`,
//            go to 1. Byte for byte UpdateLoadFromFile's state 0 with ONE
//            operand changed: the String is `saves[sel].name` (+0x38 off the
//            row) rather than `_saveName`.
//
//   state 1  REBUILD THE GAME'S MOD CONFIGURATION FROM THE SAVE, then start
//            the load. In order:
//              a. disable every installed mod (`mods[i].enabled = 0` for all i)
//                 and mark the list dirty;
//              b. for each ModInfo the save recorded, walking DOWNWARD from
//                 modsSize-1 to 0: build `GetDataDirectory() + modInfo.path`,
//                 AdjustDelimiters it, find the installed mod whose Filename at
//                 Mod+0x58 matches case-insensitively, enable it, and BUBBLE it
//                 down to index 0 one swap at a time;
//              c. allocate a SavedState{_id=1,_size=0,_data=null}, publish it at
//                 `_saveState`, have it build the SaveBlock it wants filled, and
//                 hand `GetSaveFilename(saves[sel].name)` + that block to the
//                 worker thread.
//            Go to 2.
//
//   state 2  gated on `_minOperationTime < _elapsed` — the minimum spinner
//            time. Clears operation, timer and progress and puts
//            `_selectedIndex` back to -1.
//
// WHY (b) WALKS DOWNWARD AND STILL PRODUCES THE SAVE'S ORDER. Each hit is
// bubbled all the way to index 0, so the LAST mod processed ends up first.
// Processing the save's list back to front therefore leaves the installed list
// in exactly the save's recorded order, mod 0 at index 0. Reversing the loop
// "because descending looks like a typo" would reverse the load order of every
// modded save.
//
// ----------------------------------------------------------------------------
// FOUR THINGS THE DISASSEMBLY SAYS AND THE DECOMPILED C DOES NOT.
//
// 1. *** GetSaveFilename TAKES THREE ARGUMENTS HERE AND GHIDRA PRINTS TWO. ***
//    Exactly the hazard that cost the sibling bodies in this file a debugging
//    session, in a different function, on the same callee. Ghidra emits
//
//        IO::Persistence::GetSaveFilename(IO::Persistence::_singleton,
//                                         (String *)&local_res8);
//
//    The bytes say otherwise — R8 is loaded eleven instructions before the CALL
//    and is still live at it:
//
//        1800c29ab  MOVSXD RCX,dword ptr [R13 + 0x48]   ; _selectedIndex
//        1800c29af  IMUL   RCX,RCX,0x58
//        1800c29b3  MOV    R8,qword ptr [R13 + 0x50]    ; _savesData
//        1800c29b7  ADD    R8,0x38                      ; ...+ SaveInfo.name
//        1800c29bb  ADD    R8,RCX                       <-- ARG 3, set here
//        1800c29be  MOV    RBX,qword ptr [0x1801fc6b8]
//        1800c29c5  TEST   byte ptr [RBX + 0x18],0x1
//        1800c29c9  JNZ    0x1800c2a1a
//        1800c29cb  LEA    RDX,[RBP + 0x67]             ; hidden return slot
//        1800c29cf  MOV    RCX,RBX
//        1800c29d2  CALL   0x1800b7c80
//
//    Note also that R8 is computed BEFORE the `TEST`/`JNZ` that can skip the
//    call entirely — which is why a reader scanning only the guarded block
//    finds no third argument at all.
//
// 2. THE HIDDEN RETURN SLOT. `GetSaveFilename`, `GetDataDirectory`,
//    `System::operator+` and `SavedState::SetupLoad` all return a class BY
//    VALUE. For the three MEMBER functions the slot is RDX and the real
//    arguments shift right by one; for the FREE function `operator+` the slot
//    is the FIRST argument in RCX and lhs/rhs follow in RDX/R8. A typedef that
//    omits the slot is a wild write that still returns the right answer, so it
//    survives testing (reference_hidden_retslot_wild_write.md). Every one of
//    the four was read register by register out of the disassembly.
//
// 3. `MOVSXD RCX,dword ptr [R13 + 0x48]` — the selected index is SIGN-extended
//    at all three of its uses in this body. Ghidra's C prints it as a zero
//    extension (reference_ghidra_c_output_vs_disassembly.md), which would turn
//    this class's live -1/-2/-4 sentinels into four-billion-element strides.
//    `_saves._data[index]` with a signed int index does the sign extension.
//
// 4. RETAIL RE-READS `_selectedIndex`, `_savesData` and `Modification::mods`
//    INSIDE the loops rather than hoisting them (0x1800c2771, 0x1800c27d0,
//    0x1800c28db, 0x1800c28ec, 0x1800c2901). Transcribed as re-reads: the mod
//    triple runs arbitrary code, including allocation, and hoisting would be a
//    guess about what it cannot do.
//
// ----------------------------------------------------------------------------
// ONE DELIBERATE DEVIATION FROM THE INSTRUCTION STREAM, stated because it is
// the only one. Retail INLINES the allocator lock here rather than calling the
// composed static:
//
//     1800c2944  MOV RBX,[0x1801fc718] ; ADD RBX,0x640
//     1800c2959  CALL EnterCriticalSection
//     1800c2970  CALL ?Allocate@MemoryAllocator@System@@QEAAPEAX_KH@Z (0x10, 4)
//     1800c297b  CALL LeaveCriticalSection
//
// That is instruction for instruction `?MemAlloc@MemoryAllocator@System@@`
// (0x1800c7a50), which this file already owns as `RtSaveDetail::MemAlloc`, so
// the port calls that. Same lock, same singleton, same order; only the inlining
// differs. `src/engine/io/modification.cpp` made the identical ruling for the
// identical sequence eleven times over, and its reasoning applies unchanged.
// Worth knowing that UpdateLoadFromFile, four hundred bytes away, CALLS MemAlloc
// where UpdateLoad inlines it — the two bodies really do differ here, and the
// difference is the compiler's, not a mis-read.
//
// ----------------------------------------------------------------------------
// ORACLE: PREDICTIVE — and this is the body in this file where the predictor's
// reach has to be stated most carefully, because state 1 has a region it
// genuinely cannot cover and a green board that hid that would be worse than
// no board at all.
//
//   VERIFIED, on every run
//     * `_progress`, in all three states, and state 2's four-field tail
//       (operation / elapsed / progress / selectedIndex) — the same shape as
//       UpdateLoadFromFile's, and the pre-values differ from the predicted ones
//       on every real load, so `changed=` is non-zero rather than vacuous.
//     * `IO::Modification::_dirty` (+0x00), which is OUTSIDE the mods array and
//       therefore outside the exclusion below.
//     * Every `mods[i].enabled` write, in the common case — see the next block
//       for exactly when that stops being true.
//     * The identity, order and argument values of every suppressed call:
//       BeginGetSaveGameInfo, the mod-triple swaps, MemAlloc, SetupLoad,
//       GetSaveFilename and the worker enqueue.
//
//   NOT VERIFIED, and declared with Exclude() so `excluded=`/`unverifiable=`
//   count the hole instead of a silent pass
//     * `_saveState`, for UpdateLoadFromFile's reason: its value is whatever
//       the allocator hands back and no predictor can know that address.
//     * THE WHOLE `Mod` ARRAY, but ONLY ONCE A BUBBLE ACTUALLY HAPPENS.
//       This is the sharp one. A predicting body writes nothing, so it cannot
//       reorder the array — and every iteration after the first reorder then
//       searches an array that retail has already permuted. Our predicted
//       indices would be right about a list that no longer exists, and the
//       compare would report divergences that are artefacts of the harness
//       rather than defects in the port. So the moment the body performs its
//       first swap, it declares the array out of model. Before that point the
//       predictions are sound and ARE compared, which is why the exclusion is
//       lazy rather than unconditional: a save recording no mods, or recording
//       only mods already at index 0, is verified end to end.
//
//   RUN FOR REAL EVEN IN PREDICT MODE — and this needs saying, because "the
//   predicting body writes nothing" is the whole contract:
//     GetDataDirectory, System::operator+, Filename::AdjustDelimiters and
//     _stricmp. All four are balanced allocate-and-release temporaries or pure
//     reads: every String this body creates is released before the iteration
//     ends, so when the DLL runs afterwards there is nothing of ours for it to
//     see. Suppressing them instead would leave `work.data` null and the
//     search would compare nothing, which is the failure mode
//     reference_uniform_population_is_not_a_verification.md is about — a scan
//     that "matches" because every subject is empty.
//
// ----------------------------------------------------------------------------
// RUN EVIDENCE, 2026-09-09, three runs on the BANISHED_AUTOLOAD=Auto route
// (menu -> Load -> select "Auto" -> Load click -> SetOperation(2), i.e. the
// retail load path, not a synthetic driver).
//
//   1. ORACLE ON  (the old DLL-era switch)
//        Save::SaveLoad::UpdateLoad  rounds=90  predictions=7  changed=7
//                                    wrong=0  unpredicted=0
//                                    excluded=8 byte(s), 5 suppressed call(s)
//      Read it field by field, because each column answers a different
//      objection this project has been burned by:
//        changed=7 of 7 — EVERY prediction differed from its pre-value, so not
//          one of them could be satisfied by the DLL doing nothing. There is no
//          vacuous green here (reference_probe_needs_an_effectiveness_control).
//        unpredicted=0 — the coverage control. Across the watched SaveLoad
//          (0x90) and IO::Modification header (0x18), the DLL changed NO byte
//          this body failed to predict. Predictions alone are a lower bound;
//          this is the half that can catch what we do not know about.
//        excluded=8 — exactly `_saveState`, and NOTHING ELSE. That single
//          number is also the proof that the lazy Mod-array exclusion never
//          fired, i.e. this run took the no-mods path.
//        rounds=90 — the body was entered 90 times, 88 of them in state 2
//          under the min-time gate; the 7 predictions are state 0's progress,
//          state 1's dirty + progress, and state 2's four-field tail.
//
//   2. NEGATIVE CONTROL  (+ the old DLL-era switch)
//        wrong=3, and the site reports "self-test caught (the comparison can go
//        red)". The two named catches are `_progress = 1` and
//        `Modification::_dirty = 1`. A clean board from a comparison that
//        CANNOT go red is not a result, and two of the five sites in that run
//        could not demonstrate a failure — this one could.
//
//   3. ORACLE OFF — the port actually driving the load. Predict mode hands the
//      frame to the DLL, so run 1 never executed this body for real. Run 3 did:
//      rc=0, empty crash log, and `[TownCensus] LOAD exit` shows the world
//      restored (19 citizens, 36 herd, 6 residences) from the job THIS body
//      enqueued.
//
// WHAT THOSE THREE RUNS DO NOT COVER, said plainly. The machine had ZERO .pkm
// mounted and "Auto" records ZERO ModInfo entries, so `Modification::count` was
// 0 and `saves[sel].modsSize` was 0. State 1's ENTIRE mod half — the clear
// loop, the path build, the case-insensitive search and the bubble — was
// COMPILED AND REGISTERED BUT NEVER EXECUTED. Exercising it needs two separate
// things and neither is free: mods mounted in WinData (for the clear loop and
// the search), and a save RECORDED while they were mounted (for the reorder;
// the recorded list is what the loop walks). The second one also collides with
// reference_mods_off_breaks_save_loading.md, so it wants its own lane and its
// own copy of the game directory, not a corner of this one.
// ============================================================================
}  // namespace

// NOT RETAIL (block LOADSTAMP): the one-shot flag of the save.read.io-done stamp.
static bool s_ioDoneStamped = false;

// ENDGAME B.3.7 B2: the body is the member (it was `UpdateLoadBody(self, out)`).

void Save::SaveLoad::UpdateLoad() {
    const uint32_t progress = _progress;

    if (progress == 0) {
        ::IO::Persistence* persistence = PersistenceSingleton();
        const SaveInfo& row = _saves._data[_selectedIndex];   // MOVSXD: sign-extended
        // `MOV R8D,0x2` — spelled as a plain immediate here, where
        // UpdateLoadFromFile reuses a zeroed register with `LEA R8D,[RAX+0x2]`.
        // Same 2.
        LoadTime::Stamp("save.info");   // NOT RETAIL: print-only
        persistence->BeginGetSaveGameInfo(row.name, 2);
        _progress = 1;
        return;
    }

    if (progress == 1) {
        ::IO::Modification* mods = RtSaveDetail::ModificationSingleton();

        // (a) Disable everything, then mark the list dirty. The dirty store is
        // AFTER the loop (0x1800c2728), not before it, and it is a BYTE store
        // — `MOV byte ptr [R14],0x1` — matching IO::Modification::Enable's.
        {
            // BOTH the base and the COUNT are re-read every iteration
            // (0x1800c2710 `MOV RAX,[R14+8]`, 0x1800c2722 `CMP EDX,[R14+0x10]`).
            // Nothing in the loop can change either, so hoisting would be
            // equivalent — and would still be a claim about the callee-free
            // loop that the next reader has to re-derive. Transcribed.
            for (int32_t i = 0; i < mods->_mods._size; ++i) {
                mods->_mods._data[i].enabled = 0;
            }
        }
        mods->_dirty = 1;

        // (b) Re-order. `EBX = saves[sel].modsSize - 1` is computed ONCE, here,
        // and the `JS` at 0x1800c2741 skips the whole loop when the save
        // recorded no mods at all — which is every unmodded save, and the case
        // in which this body's predictions are verified end to end.
        const SaveInfo& pickedRow = _saves._data[_selectedIndex];
        for (int32_t m = pickedRow.mods._size - 1; m >= 0; --m) {
            // full = GetDataDirectory() + modInfo.path, then AdjustDelimiters.
            // The same four-step idiom Save::SaveLoad::CheckMod carries
            // (src/engine/save/saveload.cpp), against the same two Strings —
            // ModInfo+0x08 (the package PATH), never ModInfo+0x00 (the display
            // name). Retail re-reads _selectedIndex and _savesData here rather
            // than reusing what it computed above; transcribed.
            // ENDGAME B.3.1 step 2. The three locals are RAII objects now, and
            // the explicit inner scope ends exactly where retail's three
            // releases are (0x1800c281c / 0x1800c2845 / 0x1800c286e) — so the
            // destructors run in retail's order (work, joined, dataDir, the
            // reverse of the declarations) and BEFORE the bubble-down below,
            // not at the end of the iteration. NOTE this is NOT CheckMod's
            // order, which releases work, dataDir, joined; both are transcribed
            // as written rather than regularised.
            int32_t found = -1;
            {
                System::Filename dataDir(                                // 0x180007bd0
                    RtSaveDetail::FileSystemSingleton()->GetDataDirectory());

                const SaveInfo& row = _saves._data[_selectedIndex];
                const RtSave::ModInfo& mi = row.mods._data[m];

                // The `??HSystem@@YA?AVString@0@AEBV10@0@Z` door IS
                // System::operator+ (0x1800c9440).
                System::String joined = dataDir.AsString() + mi.path;

                // Retail adopts the concatenation's buffer into a FRESH slot
                // (with an AddRef) and adjusts THAT, leaving `joined` untouched
                // — which is why three separate Strings are released and not
                // two. Adopt-into-a-nulled-slot followed by AdjustDelimiters IS
                // ??0Filename@System@@QEAA@AEBVString@1@@Z (0x18000c780), so
                // this one line still spends both.
                System::Filename work(joined);

                // Linear scan of IO::Modification: length first (read from the
                // String allocation header at data[-8], with a null String
                // counting as length 0 rather than faulting), then _stricmp.
                // Same re-read pair as the clear loop above (0x1800c27d0 /
                // 0x1800c280f).
                for (int32_t j = 0; j < mods->_mods._size; ++j) {
                    const char* entryName = mods->_mods._data[j].file._data;
                    const int lenEntry = entryName ? RtSystem::StringLength(entryName) : 0;
                    const int lenWork  = work.GetLength();
                    if (lenEntry == lenWork && rtcrt::StrICmp(entryName, work._data) == 0) {
                        found = j;
                        break;
                    }
                }
            }

            if (found < 0) continue;

            mods->_dirty = 1;
            mods->_mods._data[found].enabled = 1;

            // Bubble `found` down to index 0. Retail's inner test is
            // `if (k != k-1)`, which is never false; it is the inlined remains
            // of Modification::Swap's `a == b` early-out and is transcribed as
            // the unconditional body it compiles to.
            for (int32_t k = found; k >= 1; --k) {
                mods->_dirty = 1;

                RtIO::Mod* a = &mods->_mods._data[k];
                RtIO::Mod* b = &mods->_mods._data[k - 1];

                // The four-call dance (copy-construct, assign, assign,
                // destroy) through a stack temporary: 0x1800101e0 /
                // 0x180026ef0 / 0x180011460, in retail's order -- Mod's copy
                // ctor, its operator= twice, and ~tmp at the end of the block.
                RtIO::Mod tmp(*a);
                *a = *b;
                *b = tmp;
            }
        }

        // (c) Allocate the SavedState the load will fill, publish it, and hand
        // the job to the worker. Identical to UpdateLoadFromFile's state 1 from
        // here on except for the String GetSaveFilename is given.
        LoadTime::Stamp("save.read");   // NOT RETAIL: print-only (the file read is queued on the persistence worker)
        s_ioDoneStamped = false;
        SavedState* fresh = static_cast<SavedState*>(MemAlloc(0x10, 4));
        if (fresh != nullptr) {
            // `MOV qword ptr [RDI],0x1` is _id=1 AND _size=0 in one store;
            // `MOV qword ptr [RDI+8],R15` (R15 == 0) is _data=null.
            fresh->_id   = 1;
            fresh->_size = 0;
            fresh->_data = nullptr;
        }
        // 0x1800c299a is AFTER the null merge at 0x1800c2997, so a failed
        // allocation publishes a NULL _saveState and SetupLoad is then
        // called on it. Retail's OOM path, reproduced: a guard here would
        // skip behaviour rather than restore it.
        _saveState = fresh;

        RtIo::SaveBlock block{};
        // ENDGAME B.3.7 B2: `SavedState::SetupLoad` is a member of this block's
        // class, called directly (it was an RT_EXPORT_ONCE door onto our own
        // port). Its SaveBlock return is the hidden slot `&block` in retail.
        // `fresh` is NOT null-tested: retail calls SetupLoad on a failed
        // allocation too (see the banner).
        block = fresh->SetupLoad();

        const SaveInfo& nameRow = _saves._data[_selectedIndex];
        ::IO::Persistence* persistence = PersistenceSingleton();
        if ((persistence->_worker._runFlags & 1) == 0) {
            // THREE arguments (see note 1 in the banner): the hidden slot is
            // `filename`, the name the row's. Released as its destructor.
            System::Filename filename = persistence->GetSaveFilename(nameRow.name);
            RtIo_PersistenceWorker_EnqueueLoad(&persistence->_worker,
                                               &filename.AsString(), 1, &block);
        }

        _progress = 2;
        return;
    }

    if (progress == 2) {
        // The minimum-operation-time gate, which UpdateDelete and
        // UpdateLoadFromFile do NOT have: `MOV RAX,[RCX+0x10]` /
        // `CMP [RCX+8],RAX` / `JLE`, i.e. proceed only while
        // _elapsed > _minOperationTime.
        if (!s_ioDoneStamped) { s_ioDoneStamped = true; LoadTime::Stamp("save.read.io-done"); }   // NOT RETAIL: print-only (the worker is idle)
        if (_elapsed <= _minOperationTime) return;
        LoadTime::Stamp("save.read.done");   // NOT RETAIL: print-only (the minimum operation time passed too)

        // Store order at 0x1800c2a36..0x1800c2a51: operation, elapsed, progress,
        // selectedIndex — UpdateLoadFromFile's order, not UpdateDelete's.
        _operation     = kOpNone;
        _elapsed       = ZeroTime();
        _progress      = 0;
        _selectedIndex = -1;
    }
}

namespace {

// ============================================================================
// Save::SaveLoad::UpdateSave()         retail 0x1800c2a80   ordinal 4526
// ----------------------------------------------------------------------------
//   state 0  arm the screenshot capture: set the capture flag BYTE, and read
//            the save sprite's width and height out of its SpriteSheet. Go to 1.
//   state 1  the real work — write the metadata header, free the captured
//            pixels, allocate the SavedState the game state will serialise
//            into, tell the update loop to serialise (a VIRTUAL call, which is
//            where the whole game's Serialize chain hangs off), settle on the
//            save's display name, build the TWO SaveBlocks, and enqueue.
//   state 2  gated on `_minOperationTime < _elapsed` — the minimum spinner
//            time. Destroys the SavedState, frees the embedded one's buffer,
//            and resets the state machine.
//
// THREE THINGS WORTH THE READER'S ATTENTION.
//
// 1. `_selectedIndex == -4` IS A SENTINEL, not an index. It means "this is an
//    autosave", and it selects GetAutoSaveName over the picked row's name. The
//    comparison is done on the SIGN-EXTENDED index (`MOVSXD RAX,[RBX+0x48]`
//    then `CMP EAX,-0x4`), which is why the header's `_selectedIndex` must
//    stay signed.
//
// 2. THE NAME FALLBACK IS A THREE-WAY, and the middle arm is easy to miss: for
//    a normal index, retail uses the existing row's name ONLY IF that row's
//    metadata is loaded (`SaveInfo+0x40 >= 0`); otherwise it GENERATES a new
//    name. Collapsing that to "use the row's name" would silently write an
//    empty display name over every save made into a row the list had not
//    finished reading.
//
// 3. THE `String` PASSED TO SaveMetaData IS BY VALUE, and this port does NOT
//    release it. The caller retains it (`INC dword ptr [RAX-0x4]`) and then
//    never drops it, which reads exactly like a leak until you notice the
//    parameter is `VString@System@@` — by value — in the mangled name, so MSVC
//    makes the CALLEE destroy it. Releasing it here would double-free the
//    save name on every save.
//
// ORACLE: PREDICTIVE, with `_saveState` excluded for the same allocator reason
// as UpdateLoadFromFile, and the serialise call suppressed — running the game's
// entire Serialize chain twice is exactly the "not idempotent" case that rules
// FULL DIFF out for this file.
// ============================================================================
}  // namespace

// NOT RETAIL (fix.save_failure_notice): whether UpdateSaveTail queued the save job. A file
// static, not a field: SaveLoad's layout is retail's.
static bool g_extSaveQueued = false;

// ENDGAME B.3.7 B2: the body is the member (it was `UpdateSaveBody(self, out)`).
void Save::SaveLoad::UpdateSave() {
    const uint32_t progress = _progress;

    if (progress == 0) {
        // A BYTE store (`MOV byte ptr [RBX+0x7c],1`), matching the ctor's byte
        // store of 0 into the same field — the upper three bytes of
        // _captureFlags are never written by anything in this class.
        _captureFlags = 1;

        // `MOV RCX,[RBX+0x60]` (the sheet) then `MOV RAX,[RCX+0x20]` (its entry
        // array), indexed by the sprite index at +0x68 with stride 0x38, the
        // width at +0x1c and the height at +0x20, and 0 for a negative index:
        // Sprite::Width / Sprite::Height (0x180030bf0 / 0x180030c10), inlined.
        _captureWidth  = _saveSprite.Width();
        _captureHeight = _saveSprite.Height();
        _progress = 1;
        return;
    }

    if (progress == 1) {
        // The capture has not landed yet — retail simply returns and retries
        // next tick. This is the handshake with Draw(), which is the function
        // that fills _capturePixels and which this project holds back.
        void* pixels = _capturePixels;
        if (pixels == nullptr) return;

        // The by-value String argument, in the storage MSVC's convention gives
        // it: copy-constructed here (zero, adopt, AddRef), DESTROYED BY THE
        // CALLEE — so it lives in raw storage that no destructor of ours
        // reaches.
        alignas(System::String) uint8_t nameStorage[sizeof(System::String)];
        System::String* nameCopy = new (nameStorage) System::String(_saveName);

        // ENDGAME B.3.7 B2: a direct member call (was an RT_EXPORT_ONCE door).
        // `&nameCopy` IS the by-value String's hidden pointer — the String's one
        // field, the copy this body just AddRef'd — and the callee destroys it.
        SaveMetaData(pixels, _captureWidth, _captureHeight, nameCopy, _saveNameMode);

        MemFree(pixels);
        _capturePixels = nullptr;

        SavedState* fresh = static_cast<SavedState*>(MemAlloc(0x10, 4));
        if (fresh != nullptr) {
            fresh->_id   = 1;
            fresh->_size = 0;
            fresh->_data = nullptr;
        }
        _saveState = fresh;

        // MainLoop::_singleton->_updateLoop->vtable[7] — the call that serialises
        // the entire game state, and the single most expensive thing this file
        // suppresses in predict mode.
        //
        // *** IT TAKES THREE ARGUMENTS, AND GHIDRA SAYS TWO. *** This cost this
        // lane a debugging session and is the most useful thing in this file.
        // The decompiler prints
        //
        //     (**(code **)(**(longlong **)(MainLoop::_singleton + 0x20) + 0x38))
        //               (*(longlong **)(... + 0x20), 1);
        //
        // because it has typed the vtable slot as `void(void*, bool)`. The
        // disassembly says otherwise — R8 is loaded with the fresh SavedState
        // and is STILL LIVE at the call, twelve instructions later:
        //
        //     1800c2b6f  CALL   MemAlloc(0x10)
        //     1800c2b74  MOV    R8,RAX          <-- arg 3, set here
        //     1800c2b8d  MOV    R8,RDI          <-- ...or 0 on the failure arm
        //     1800c2b90  MOV    qword ptr [RBX+0x40],R8
        //     1800c2b9b  MOV    RCX,qword ptr [RAX+0x20]
        //     1800c2ba2  MOV    DL,0x1
        //     1800c2ba4  CALL   qword ptr [RAX+0x38]
        //
        // The callee is SaveRestore(this, save, savedState): retail's
        // GameUpdateLoop::SaveRestore 0x1400959c0 reads RCX, DL and R8 only
        // (row 1247). UpdateSave's R9 still holds the capture height from the
        // SaveMetaData call four calls earlier, and retail's Load passes a pointer
        // there too, but no body reads it, so the tree's two-argument slot
        // (`Engine::UpdateLoop::SaveRestore(bool, Save::SavedState*)`,
        // src/engine/engine/UpdateLoop.h) is the whole interface.
        //
        // Dropping `savedState` (Ghidra's two-argument reading) is not cosmetic:
        // it then arrives as whatever happened to be in R8, and the callee's
        // `SavedState::Store` does
        // `if (_data) MemFree(_data)` on it (an ACCESS_VIOLATION inside the
        // allocator, UpdateSaveBody -> GUL_SaveRestore -> Store -> MemFree).
        // (The tree's null test on the MainLoop singleton is kept as it was.)
        // EXT SEAM (NOT RETAIL, docs/extensions/handins/HITCH_SAVESNAP.md): ext.background_save -- the serialise runs on a worker and this
        // machine waits in kExtProgressSerializing (below). Null: retail's call.
        if (_extSaveStart != nullptr && _extSaveStart(fresh)) {
            _progress = kExtProgressSerializing;
            return;
        }
        if (::Engine::MainLoop* mainLoop = ::Engine::MainLoop::_singleton) {
            // `fresh`, not a re-read of _saveState: retail passes the REGISTER,
            // which on the allocation-failure arm is 0 and equals what it just
            // stored. The two are the same value; this spells the register.
            mainLoop->_currentLoop->SaveRestore(true, fresh);
        }

        UpdateSaveTail();
        return;
    }

    if (progress == kExtProgressSerializing) {   // NOT RETAIL: ext.background_save
        if (!_extSavePoll()) return;
        UpdateSaveTail();
        return;
    }

    if (progress == 2) {
        // THE MINIMUM-OPERATION-TIME GATE, which UpdateDelete and
        // UpdateLoadFromFile do not have: `CMP [RBX+0x8],[RBX+0x10]` / `JLE`,
        // i.e. proceed only while _elapsed > _minOperationTime. It exists so a
        // fast save still shows its spinner for a readable moment.
        if (_elapsed <= _minOperationTime) return;

        // EXT SEAM (NOT RETAIL, docs/retail_bugs.md §27): fix.save_failure_notice. Retail
        // finishes here without reading the worker's status, so a save that never reached
        // the disk says nothing. With the row on, wait for the job's status (0 while it
        // runs; EnqueueSave cleared it) and report it before the machine resets.
        if (_extFixSaveResult != nullptr && *_extFixSaveResult && _extSaveResult != nullptr) {
            const ::IO::Persistence* persistence = PersistenceSingleton();
            const int32_t status = *static_cast<const volatile int32_t*>(&persistence->_worker._status);
            if (g_extSaveQueued && status == 0) return;
            _extSaveResult(g_extSaveQueued && status == 1);
        }

        // FUN_180032320 — `if (p->_data) MemFree(p->_data); MemFree(p);`, the
        // inlined `delete` of a SavedState. Spelled out rather than called by
        // address: both halves are MemFree, which this file already owns, so
        // there is nothing here an address door would buy.
        SavedState* heap = _saveState;
        if (heap != nullptr) {
            if (heap->_data != nullptr) MemFree(heap->_data);
            MemFree(heap);
        }

        void* embeddedData = _embeddedState._data;
        if (embeddedData != nullptr) MemFree(embeddedData);
        _embeddedState._data = nullptr;
        _embeddedState._size = 0;

        _operation     = kOpNone;
        _elapsed       = ZeroTime();
        _progress      = 0;
        _selectedIndex = -1;
    }
}

// EXT SEAM (NOT RETAIL, docs/extensions/handins/HITCH_SAVESNAP.md): ext.background_save, null by default (SaveLoad.h).
bool (*Save::SaveLoad::_extSaveStart)(SavedState*) = nullptr;
bool (*Save::SaveLoad::_extSavePoll)() = nullptr;
// EXT SEAM (NOT RETAIL, docs/retail_bugs.md §27): fix.save_failure_notice, null by default (SaveLoad.h).
const bool* Save::SaveLoad::_extFixSaveResult = nullptr;
void (*Save::SaveLoad::_extSaveResult)(bool) = nullptr;

// The rest of UpdateSave's state 1, moved out unchanged (NOT RETAIL as a function: retail's
// body is straight-line). Retail's SaveRestore call and ext.background_save's poll both end here.
void Save::SaveLoad::UpdateSaveTail() {
    const int32_t sel = _selectedIndex;
    if (sel == -4) {
        // ENDGAME B.3.7 B2: a direct member call (was an RT_EXPORT_ONCE
        // door). The returned String lands in `generated` exactly as
        // retail's hidden slot does — born at refcount 1, nothing added —
        // is String::operator='d (0x1800c9810) into _string20 and released
        // as it leaves the scope.
        System::String generated = GetAutoSaveName();
        _string20 = generated;
    } else {
        const SaveInfo& row = _saves._data[sel];
        if (row.saveIndex >= 0) {
            _string20 = row.name;
        } else {
            // ENDGAME B.3.7 B2: a direct member call (was an RT_EXPORT_ONCE
            // door); the returned String lands in `generated` as above.
            System::String generated = GenerateSaveName();
            _string20 = generated;
        }
    }

    // TWO blocks. Entry 0 is the SavedState EMBEDDED in this object at
    // +0x28; entry 1 is the heap one at _saveState. Note the field order
    // differs from SavedState's own — see the SaveBlock banner in
    // src/engine/io/Persistence.h.
    RtIo::SaveBlock blocks[2];
    const SavedState* embedded = &_embeddedState;
    blocks[0].data = embedded->_data;
    blocks[0].size = embedded->_size;
    blocks[0].id   = embedded->_id;
    const SavedState* heap = _saveState;
    blocks[1].data = heap->_data;
    blocks[1].size = heap->_size;
    blocks[1].id   = heap->_id;

    ::IO::Persistence* persistence = PersistenceSingleton();
    g_extSaveQueued = false;   // NOT RETAIL: fix.save_failure_notice's state-2 read
    if ((persistence->_worker._runFlags & 1) == 0) {
        System::Filename filename = persistence->GetSaveFilename(_string20);
        RtIo_PersistenceWorker_EnqueueSave(&persistence->_worker,
                                           &filename.AsString(), 2, blocks);
        g_extSaveQueued = true;
    }

    _progress = 2;
}

namespace {

// ============================================================================
// THE UpdateList DEPENDENCY SET — five unexported internals and one CRT entry.
// ----------------------------------------------------------------------------
// Every address below was checked against the SHIPPED export table with pefile
// before it was called by address, because a source grep cannot tell an
// unexported internal from an export nobody has ported yet
// (reference_grep_cannot_tell_internal_from_unported_export.md). Result: five
// of these are genuinely absent from all 4,800+ exports, and everything else
// UpdateList calls turned out to be a plain named export and is resolved by
// name below.
//
// THREE OF THEM ARE ALREADY OURS. `src/engine/save/saveload.cpp` has ported
// and registered 0x1800329c0 (Array<SaveInfo>::~Array), 0x1800c3f40 (~SaveInfo)
// and 0x1800331f0 (Array<ModInfo>::~Array) since Wave 5. Re-porting them here
// would not merely duplicate work: a duplicate RegisterRuntimeAddr is
// last-binding-wins, so it silently turns one of the two bodies into dead code
// (reference_grep_for_the_port_before_writing_it.md). They are called through
// `ResolveAddr(Module::Runtime, addr)`, which consults the port registry FIRST
// (reference_resolveaddr_consults_port_registry_first.md), so these calls
// already land in our code today with no edit here.
// ============================================================================

// 0x1800329c0 — Collection::Array<SaveInfo>::~Array. PORTED (saveload.cpp).

// 0x1800c3f40 — Save::SaveLoad::~SaveInfo, one element. PORTED (saveload.cpp).

// 0x1800332d0 — Collection::Array<SaveInfo>::SetSize(newSize, const T& fill).
// It compares `newSize` against `_size` (+0x08), grows `_capacity`
// (+0x0c) — with the "grew by exactly one from non-empty" case jumping straight
// to 3x, which is why a list of N saves does not realloc N times — reallocates
// at stride 0x58, copy-assigns each element with FUN_180032f70, and destroys
// the tail with the same ~SaveInfo above. UpdateList only ever calls it with
// `_size + 1`, i.e. as push_back.
//
// ENDGAME B.3.2 step 2: this IS `kSpec_MetaData58` (src/engine/collection/
// array.cpp, "Array<Save::MetaData>", stride 0x58, align 8) — the assign that
// calls FUN_180032f70 and the destroy that calls ~SaveInfo above, named by the
// same address, are that row's `assign`/`destroy`. It is a HAND-DERIVED row
// (assign is a call to a real copy ctor, destroy is a call to a real dtor,
// `byteCopy == false`): `SaveInfo` (src/engine/save/SaveLoad.h) stores its
// two Strings and its `ModInfo*` sub-array as raw pointers, so the
// trait-derived `SpecFor<SaveInfo>()` would read `is_trivially_copyable` true
// and silently drop retail's refcounting and nested-array release — exactly
// the trap B32_RULES.md says never to fall into. `kSpec_MetaData58` has
// external linkage (namespace rtcoll, array.cpp) but is not declared `extern`
// in array_internal.h; declared locally rather than editing that header.
//
// `self->_savesData`/`_saves._size`/`_savesCapacity` (SaveLoad.h +0x50/+0x58/
// +0x5c) are still three raw fields, not a `Collection::Array<SaveInfo>`
// member — SaveLoad.h is not one of this batch's files, so the member call
// below reaches them through the transitional
// `reinterpret_cast<Collection::Array<SaveInfo>&>(...)` bridge (ENDGAME
// §B.3.2's rule for a not-yet-retyped POD field), which is safe here because
// the three fields sit at consecutive offsets 0/8/0xc exactly matching
// `Collection::Array<T>`'s own {_data,_size,_capacity} layout (both pinned by
// `static_assert` already). (Declared at file scope above the anonymous
// namespace — see the note there.)

// 0x1800c3de0 — SaveInfo::SetThumbnail(row, pixels, w, h). NOT ported, and NOT
// a small function: it allocates a Video::ImageBuffer, publishes it at row+0x18,
// calls its vtable slot 9 to create the surface, and then uploads `pixels`
// through the ImageBufferData vtable slot 5 under the Video::Device lock before
// storing pixels/w/h at row+0x28/+0x20/+0x24. Those three stores are the
// independent confirmation that OnVideoRestart's upload arguments and this
// header's thumbWidth/thumbHeight/thumbPixels names line up.

// 0x18002ac40 — destroys the Array at Persistence+0x80 and zeroes {data,size}.
// Called only from state 0, immediately before the worker is signalled: it
// drops the cached save list so the worker thread refills it.
//
// ENDGAME B.3.2 step 2: this is `RtColl_ArrayDtorElem_18002ac40`
// (src/engine/collection/array_destroy.cpp), a J2 `~Array` — per-element
// String release (`ReleaseStringsCounted` against `kD_Str_10at08`, "IO:
// Persistence::SaveGameInfo {DateTime, String*@+8}") then the J1 buffer free.
// NOT converted to a `Collection::Array<T>::~Array()` member call: the element
// type has no name or header anywhere in this tree (Persistence+0x80 is bare
// bytes, `IO::Persistence` is not a batch-2b file), and `kD_Str_10at08`
// (array_destroy.cpp) is a `constexpr` at namespace scope, i.e. INTERNAL
// linkage — it cannot be named from this translation unit at all, with or
// without editing a header this batch does not own. Reached by declaring and
// calling the already-ported free function directly, by name, instead of by
// address; still not a member call, and this is why. (Declared at file scope
// above the anonymous namespace — see the note there.)

// 0x18007f050 — the DLL's own statically linked CRT `qsort`. Called by address
// rather than through <cstdlib>'s: for a comparator that is not a total order
// (see below) the ORDER OF EQUAL ELEMENTS is an artefact of the sort algorithm,
// and ucrt's qsort is not the one that shipped. The resulting row order is what
// the save list shows the player, so it is observable.
using QsortFn = void (*)(void* base, size_t count, size_t size,
                         int (*cmp)(const void*, const void*));

// 0x1800c3db0 — the comparator, passed to qsort as DATA. Ghidra never made a
// function of it (it is 22 bytes ending in a bare RET and the decompiler
// reports "no function at that address"), so it is read out of the image:
//
//     83 79 40 00  CMP dword ptr [RCX+0x40],0x0
//     7c 16        JL  .greater                  ; a.saveIndex < 0 -> return 1
//     83 7a 40 00  CMP dword ptr [RDX+0x40],0x0
//     7c 10        JL  .greater                  ; b.saveIndex < 0 -> return 1
//     48 8b 41 30  MOV RAX,[RCX+0x30]            ; a.sortKey
//     48 8b 4a 30  MOV RCX,[RDX+0x30]            ; b.sortKey
//     48 3b c8     CMP RCX,RAX                   ; UNSIGNED
//     77 03        JA  .greater
//     1b c0        SBB EAX,EAX                   ; b<a -> -1, b==a -> 0
//     c3           RET
//   .greater: b8 01 00 00 00 / c3                ; return 1
//
// i.e. DESCENDING by the unsigned 64-bit sortKey. Note it is NOT a total order:
// a row with a negative saveIndex compares GREATER in both positions, so a
// blank row and any other row are "both after each other". Retail gets away
// with that because the only such row is the "new save" slot, and state 1
// deliberately sorts the range that EXCLUDES it — which is what the base+0x58 /
// count-1 shift below is for. Reproducing the shift is not an optimisation; it
// is what keeps an inconsistent comparator out of the sort.
using ComparatorFn = int (*)(const void*, const void*);

// ENDGAME B.3.7 B2: `ArraySaveInfoDtorFn()` / `SaveInfoDtor()` (the 0x1800329c0 /
// 0x1800c3f40 address doors) are gone: both are this class's own bodies,
// called directly (`RtSaveDetail::ArraySaveInfoDtor`, `SaveInfo::~SaveInfo`).
// ENDGAME B.3.2 step 2: `ArraySaveInfoSetSize()`/`PersistenceClearSaveList()`
// (the 0x1800332d0 / 0x18002ac40 address doors) are gone — their two call
// sites below reach `rtcoll::AsArray<&rtcoll::kSpec_MetaData58>(...)` and
// `rtcoll::RtColl_ArrayDtorElem_18002ac40(...)` directly. See the banners on
// `kSpec_MetaData58` and `RtColl_ArrayDtorElem_18002ac40` above.
// ENDGAME B.3.7 B2: `SaveInfoSetThumbnail()` / `SaveInfoComparator()` (the
// 0x1800c3de0 / 0x1800c3db0 address doors) are gone: `SaveInfo::SetThumbnail`
// and `SaveInfo::Compare` are members, called / taken directly.
// B3R RT-IO: 0x18007f050 is RtCrt_Qsort_18007f050 (src/engine/crt), called by name.

// ---- the exported half ------------------------------------------------------

// GetSaveInfo returns `IO::Persistence::SaveGameInfo` BY VALUE — `?A` in
// `?GetSaveInfo@Persistence@IO@@QEBA?AUSaveGameInfo@12@H@Z` — so RDX is the
// hidden return slot and the index lands in R8D, exactly as
// 0x1800c1fe3 (`MOV R8D,R12D` / `LEA RDX,[RBP-0x61]` / `MOV RCX,R14`) has it.

// LoadMetaData does NOT have a hidden slot: it returns void (`QEAAX...`). Seven
// arguments, three of them past the register four, and this is the call site
// where reading Ghidra's C instead of the bytes would have cost the most —
// its rendering drops nothing here only because the decompiler happened to
// recover the prototype. The stack triple is written at 0x1800c22a0/0x22a9/
// 0x22b2 as [RSP+0x30]=&row.modsData, [RSP+0x28]=&field44Out, [RSP+0x20]=&h.
using LoadMetaDataFn = void (*)(SaveLoad* self, const char** description,
                                void** pixels, int* w, int* h, int* field44,
                                void* modsArray);

// ============================================================================
// Save::SaveLoad::UpdateList()          retail 0x1800c1ea0   ordinal 4519
// ----------------------------------------------------------------------------
// The biggest body in the class — 1,706 bytes, six states — and the one that
// builds the array everything else in this file indexes into. It has two jobs
// and does them in sequence: states 0-1 BUILD the row array from
// IO::Persistence's save list, states 2-4 then walk it and load one row's
// metadata per tick.
//
//   state 0  destroy the existing Array<SaveInfo>; if Persistence is usable,
//            drop its cached list and kick the worker thread. -> 1
//   state 1  build every row (see below), sort, hand the placeholder sprite to
//            the rows that have no thumbnail, reset the cursor. -> 2
//   state 2  scan forward from the cursor for the next row backed by a real
//            save; none left -> 5, otherwise ask Persistence for its info -> 3
//   state 3  once the worker is idle, build a SaveBlock from the EMBEDDED
//            SavedState and start the load. -> 4 (unconditionally)
//   state 4  success: LoadMetaData fills description/thumbnail/mods.
//            failure: empty description, and a BLACK thumbnail allocated at the
//            placeholder sprite's size. Either way, cursor += 1 -> 2
//   state 5  gated on `_minOperationTime < _elapsed`; reset the machine.
//
// FIVE THINGS A READER WOULD OTHERWISE "FIX".
//
// 1. THE ONE-PASS-PER-TICK SHAPE IS THE POINT. States 2-4 load exactly one
//    row's metadata per call and go back to 2. A save list of forty entries
//    therefore takes ~120 frames to fill in. Collapsing the cycle into a loop
//    would read as an obvious win and would block the frame on disk I/O for as
//    long as the whole list takes.
//
// 2. STATE 3 ADVANCES EVEN WHEN IT DOES NOTHING. Its two-part guard
//    (`!(persistence[0x18] & 1) && persistence[0x48] == 1`) sits INSIDE the
//    state, and `_progress = 4` is written outside it (0x1800c2251). So a tick
//    that arrives while the worker is busy skips the load and still advances,
//    and state 4's identical guard is what turns that into the "no metadata"
//    arm. The two guards are not redundant; they are how a busy worker degrades
//    to a blank row instead of stalling.
//
// 3. THE BLANK "NEW SAVE" ROW LEAVES TWO FIELDS UNINITIALISED, AND SO DO WE.
//    Retail's stack template writes +0x00, +0x08, +0x10, +0x18, +0x28, +0x30,
//    +0x38, +0x40, +0x44, +0x48, +0x50 — and never +0x0c, +0x20 or +0x24.
//    (ENDGAME B.3.7 B2 CORRECTION: this line used to add "+0x2c" to that list.
//    +0x28 is a QWORD store, 0x1800c1fc3 `MOV qword ptr [RBP-0x21],RSI`, so
//    +0x2c IS written — and both the old port and the new ctor write it.)
//    Those bytes are copied into the array as whatever was on the stack.
//    Zeroing them would be a normalisation of retail's bytes, so this port
//    leaves the local uninitialised and assigns exactly the fields retail
//    assigns. Nothing reads +0x20/+0x24 unless +0x18 is non-null, and
//    SetThumbnail always writes all three together, which is why retail can
//    afford it.
//
// 4. THE AUTOSAVE ROW IS SKIPPED ONLY IN "NEW SAVE" MODE. The whole
//    length-then-StrCompare dance at 0x1800c2077..0x1800c20ba runs only when
//    `_selectedIndex == -2`, i.e. the save dialog opened with a "new save"
//    slot; in load mode the autosave is a perfectly good row and is listed. The
//    comparison itself is retail's cheap String equality: compare the LENGTH
//    words at data[-8] first, and only call StrCompare when they match. An
//    empty name short-circuits to "equal" without calling StrCompare at all.
//
// 5. STATE 4's FAILURE ARM ALLOCATES A BLACK IMAGE, AND RECOMPUTES ITS SIZE
//    THREE TIMES. Retail reloads `_saveSprite.spriteIndex` and re-reads the sheet
//    entry before the MemAlloc, before the memset, and again before the
//    SetThumbnail call (0x1800c2404, 0x1800c245a, 0x1800c24ad). It is
//    transcribed as three calls to one helper rather than folded to one, for
//    the same reason OnVideoRestart's re-read of `_savesData` after Recreate is
//    transcribed: the reload is what retail relies on across a call that can
//    run arbitrary code, and folding it is a change of behaviour dressed as a
//    cleanup.
//
// ----------------------------------------------------------------------------
// ORACLE: PREDICTIVE, AND THIS BODY IS WHERE THE PREDICTOR'S LIMITS BITE
// HARDEST — said here in full rather than left for a reader to infer from a
// green board, because this is the largest body in the file and the one whose
// coverage is thinnest.
//
//   VERIFIED, and these are real, changing predictions on every real pass:
//     * the STATE MACHINE itself — every `_progress` transition, in every
//       state, including state 2's two-way exit and state 5's time gate;
//     * the cursor `field38`, both its reset in state 1 and every increment
//       state 2 and state 4 make;
//     * the worker handshake words at Persistence+0x48/+0x4c in state 0;
//     * state 5's four-field reset, including the `System::Time(0.0f)`
//       conversion, in retail's store order;
//     * the identity and every argument of EVERY suppressed call — which is
//       what pins the qsort base/count SHIFT, LoadMetaData's seventh argument,
//       and BeginGetSaveGameInfo's `2`. (Deliberately not spelled as a count:
//       a hard number in a comment goes stale the first time a branch moves,
//       and then reads as a measurement.)
//
//   NOT VERIFIED, each declared with Exclude() so `excluded=` counts it:
//     * the row array's contents and its {data,size,capacity} triple. Every
//       write into it goes through ArraySetSize, qsort, LoadMetaData or
//       SetThumbnail, all four of which are suppressed in predict mode.
//     * state 4's `description` and `field44`. Their predicted values are
//       LoadMetaData's out-params, and LoadMetaData is suppressed, so our
//       prediction would be "whatever the zeroed locals hold" — a guaranteed
//       false failure, not a check. The row is excluded on that arm and the
//       predictions are recorded for read-back only (the oracle counts those
//       as `unverifiable`, which is the honest bucket).
//     * the pixel block state 4's failure arm allocates: MemAlloc's return
//       address is not predictable, exactly as in UpdateLoadFromFile.
//
// THE WORKER-THREAD RACE, and why both Exclude()s below are UNCONDITIONAL.
// States 3 and 4 both branch on `persistence[0x48] == 1`, and that word is
// written by the save/load WORKER THREAD. A predicting round runs OUR body and
// then the DLL's, microseconds apart, and the worker can flip the flag in
// between — so the two halves can take DIFFERENT ARMS. That is not a defect in
// either body; it is what the oracle's own design (theirs runs for real,
// afterwards, on pristine state) costs on a body whose control flow depends on
// another thread. An Exclude placed inside the arm we happen to take is
// therefore unarmed in precisely the case that needs it, and a conditional one
// in state 4 would be worse than noisy: a round where we took the failure arm
// and the DLL took the success arm would report our `field44 = 0` as a MISMATCH
// and indict a correct port.
//
// *** WAVE 14 CLOSES THIS FINDING. *** It is left standing, with the correction
// beneath it, because the reasoning is what the next reader needs and because
// the WRONG half of it is the kind of claim that gets believed. Every predicting
// run of this site reports:
//
//     UNPREDICTED WRITE Save::SaveLoad::UpdateList  9 byte(s) ...
//     first at Save::SaveLoad [state 4 meta] +44
//
// i.e. during a STATE 4 round, nine bytes from SaveLoad+0x2c (decimal 44) —
// the embedded SavedState's `_size` and `_data` — change, and this body neither
// predicts nor excludes them. What is established: it is state 4, not state 3
// (the state was put into the Watch LABEL precisely because two successive
// guesses about which state it was were both wrong, and `excluded=` staying
// flat across a build that added Exclude()s is what exposed the second one).
// The writer was recorded here as unknown, with this sentence:
//
//     "`?LoadMetaData@SaveLoad@Save@@` (0x1800c1b30) only READS +0x2c/+0x30 —
//      `MOV EDX,[R14+0x2c]` and `MOV RDX,[R14+0x30]` at 0x1800c1b88/0x1800c1b99,
//      opening a MemoryStream over the loaded buffer — and its tail stores to
//      neither."
//
// *** THAT LAST CLAUSE IS FALSE, and LoadMetaData IS the writer. *** Its tail
// stores to BOTH, at 0x1800c1d98..0x1800c1db5:
//
//     MOV RCX,[R14+0x30] ; TEST RCX,RCX ; JZ ; CALL 0x1800c7ad0   ; MemFree
//     MOV qword ptr [R14+0x30],0x0
//     MOV dword ptr [R14+0x2c],0x0
//
// It frees the compressed image it has just consumed and zeroes the embedded
// SavedState in place. That accounts for the finding exactly: it is the state-4
// arm (the only arm that calls LoadMetaData), the bytes are +0x2c and +0x30, and
// the callee is one this site SUPPRESSES with out.Call, so no prediction of ours
// could ever have covered it. The lesson is the one this project keeps
// re-learning: the two reads were verified at named addresses and the "stores to
// neither" was not, and an unverified clause sitting beside two verified ones
// inherits their authority. (Wave 14 lane LOADMETA, which ported the function:
// src/engine/save/saveload_loadmeta.cpp.)
//
// It is STILL deliberately not excluded, and the reason has changed. It was
// "we do not know what writes these bytes"; it is now "these bytes are written
// by a suppressed callee we have since ported, and the honest bucket for a
// suppressed callee's footprint is `unpredicted=` until this site models it".
// An Exclude here would be defensible now and is left for whoever re-tunes this
// round, so that the count keeps naming a real, understood hole rather than
// disappearing into a sentence.
//
// ONE CALL IS DELIBERATELY *NOT* SUPPRESSED IN PREDICT MODE, and skipping it
// would be a bug rather than caution: `~SaveInfo` on the stack template at the
// bottom of state 1's loop. GetSaveInfo has just RETAINED a String into that
// template; if the append is suppressed and the destructor is too, every
// iteration leaks one save name. The destructor's entire footprint is our own
// stack local, so running it in predict mode changes nothing the round watches.
// The three const getters (GetSaveCount, GetSaveInfo, GetAutoSaveName) run for
// the same reason: they are `QEBA`, they balance their own refcounts, and
// suppressing them would leave the skip test with nothing to compare.
// ============================================================================

// Retail's `_saveSprite` -> SpriteSheet entry dimension read, which state 4's
// failure arm performs THREE times. `MOVSXD R8,[RDI+0x68]` then, only when the
// index is non-negative, `[sheet->entries + index*0x38 + 0x1c]` for the width
// and `+0x20` for the height (0 for a negative index) — Sprite::Width /
// Sprite::Height (0x180030bf0 / 0x180030c10), inlined, as UpdateSave's
// state 0 reads the same sprite.

// The stack template state 1 builds and appends is `SaveInfo`'s own template
// constructor now (SaveLoad.h) — ENDGAME B.3.7 B2 moved `InitSaveInfoTemplate`
// onto the class: the same eleven stores, +0x0c/+0x20/+0x24 unwritten.

}  // namespace

// ENDGAME B.3.7 B2: the body is the member (it was `UpdateListBody(self, out)`).
void Save::SaveLoad::UpdateList() {
    // Retail loads the Persistence singleton ONCE, at 0x1800c1ec9, before it
    // looks at `_progress` — so every state below shares this one read even
    // though Ghidra's C re-materialises `IO::Persistence::_singleton` inside
    // states 3 and 4.
    ::IO::Persistence* const persistence = PersistenceSingleton();
    const uint32_t progress = _progress;

    // ---- state 0 -----------------------------------------------------------
    if (progress == 0) {
        // 0x1800329c0, Array<SaveInfo>::~Array — ENDGAME B.3.7 B2: the
        // transcription (saveload.cpp) called directly, not through its
        // registered address door.
        RtSaveDetail::ArraySaveInfoDtor(&_saves);

        if ((persistence->_worker._runFlags & 1) == 0) {
            // retail 0x18002ac40 — see the banner on
            // `rtcoll::RtColl_ArrayDtorElem_18002ac40` above for why this is a
            // direct call by name rather than a member call. Its receiver is
            // the Array<SaveGameInfo> header at Persistence+0x80.
            rtcoll::RtColl_ArrayDtorElem_18002ac40(&persistence->_worker._saves);
            // The worker handshake, in retail's order: request code first, then
            // the result word, then the two events.
            //
            // THE SECOND PREDICTION IS KNOWN-FLAKY, and it is left in with its
            // eyes open. `+0x48` is the word the WORKER THREAD writes when it
            // finishes, so between retail's store of 0 and Verify()'s read the
            // worker can have already put 1 back. Observed exactly once, in the
            // this-test run:
            //
            //     MISMATCH ... Persistence+0x48 = 0 (result: pending)
            //     pre=0x1 ours=0x0 theirs=0x1
            //
            // — note the ABSENCE of the "[SELF-TEST: corrupted on purpose]"
            // tag, which is what says this one was real rather than the
            // control. It is the same worker-thread race the banner names for
            // states 3 and 4, showing up a third time, and it is a race in the
            // OBSERVATION, not a disagreement about what retail stores: both
            // bodies store 0 here. Forty-five uncorrupted rounds across two runs
            // reported wrong=0, so it is rare. Excluding it would trade a rare
            // false red for a permanent blind spot on the one handshake this
            // state exists to perform; a reader who sees it go red on exactly
            // this label should suspect the worker before suspecting the port.
            persistence->_worker._command = 4;   // request: list saves
            persistence->_worker._status  = 0;   // result: pending

            Platform::EventReset(persistence->_worker._hFinished);   // ResetEvent
            Platform::EventSet(persistence->_worker._hStart);        // SetEvent
        }

        _progress = 1;
        return;
    }

    // ---- state 1 -----------------------------------------------------------
    if (progress == 1) {
        // The "new save" slot, pushed FIRST so it lands at index 0 — which is
        // what makes the sort's base+0x58 shift below able to exclude it.
        if (_selectedIndex == -2) {
            // ENDGAME B.3.7 B2: SaveInfo's real template ctor placement-newed
            // into DELIBERATELY UNINITIALISED stack bytes — it writes exactly
            // the eleven fields retail's stack template writes (SaveLoad.h).
            alignas(SaveInfo) uint8_t blankStorage[sizeof(SaveInfo)];
            SaveInfo& blank = *new (blankStorage) SaveInfo(-1);
            // retail 0x1800332d0 — see the banner on `kSpec_MetaData58` above
            // for the hand-derived-row reasoning and the `_savesData` bridge.
            _saves.SetSize(_saves._size + 1, blank);   // 0x1800332d0, the typed 3-arg form
            // Not suppressed: see the banner. Its footprint is this stack local.
            blank.~SaveInfo();                        // 0x1800c3f40
        }

        if (persistence->GetSaveCount() > 0) {
            int32_t i = 0;
            do {
                alignas(SaveInfo) uint8_t rowStorage[sizeof(SaveInfo)];
                SaveInfo& row = *new (rowStorage) SaveInfo(i);   // see `blank` above

                // The by-value SaveGameInfo (?GetSaveInfo@, 0x1800b7bd0). Its
                // String is String::operator='d into the template and the
                // temporary is then destroyed as it leaves this scope — the
                // release at 0x1800c203b, not a spare drop of the row's name.
                {
                    RtIo::SaveGameInfo info = persistence->GetSaveInfo(i);
                    row.sortKey = info.sortKey;
                    row.name = info.name;
                }

                // Skip the autosave row, but ONLY in "new save" mode.
                bool skip = false;
                if (_selectedIndex == -2) {
                    // ENDGAME B.3.7 B2: a direct member call (was an
                    // RT_EXPORT_ONCE door); lands in `autoName` as retail's slot
                    // and is released as it leaves this scope.
                    System::String autoName = GetAutoSaveName();

                    // The length words at data[-8], zero for a null String:
                    // String::GetLength (0x1800063c0), inlined.
                    const int32_t lenRow  = row.name.GetLength();
                    const int32_t lenAuto = autoName.GetLength();
                    if (lenRow == lenAuto) {
                        // An empty (or null) name short-circuits to "equal" and
                        // StrCompare is never called — 0x1800c20a3..0x1800c20ac.
                        skip = (row.name._data == nullptr) || (lenRow == 0) ||
                               (System::String::StrCompare(row.name._data, autoName._data) == 0);
                    }
                }

                if (!skip) {
                    // retail 0x1800332d0 — see the banner on `kSpec_MetaData58`
                    // above.
                    _saves.SetSize(_saves._size + 1, row);   // 0x1800332d0
                }
                row.~SaveInfo();                          // 0x1800c3f40

                ++i;
                // Retail re-asks Persistence for the count EVERY iteration
                // (0x1800c2117) rather than hoisting it: the worker thread owns
                // that list and may still be appending to it.
            } while (i < persistence->GetSaveCount());
        }

        // The sort. When a blank row is present it is at index 0 and is
        // EXCLUDED from the range — see the comparator's note above.
        {
            SaveInfo* base  = _saves._data;
            int32_t   count = _saves._size;
            if (_selectedIndex == -2) {
                count -= 1;
                base  += 1;   // one 0x58-byte row
            }
            // ENDGAME B.3.7 B2: the comparator is SaveInfo's static member,
            // handed to qsort by address as retail hands 0x1800c3db0.
            ComparatorFn cmp    = &SaveInfo::Compare;
            RtCrt_Qsort_18007f050(base, static_cast<size_t>(static_cast<int64_t>(count)),
                                  sizeof(SaveInfo), cmp);
        }

        // Every row with no backing save inherits the placeholder sprite — the
        // two qwords SetSaveSprite wrote at +0x60/+0x68 (UI::Sprite's trivial
        // copy: both qwords, pad0C included). Retail RE-READS `_savesData` on
        // every iteration (0x1800c2160); transcribed.
        for (int32_t i = 0; i < _saves._size; ++i) {
            SaveInfo& row = _saves._data[i];
            if (row.saveIndex < 0) {
                row.thumbnail = _saveSprite;
            }
        }

        field38   = 0;   // the metadata cursor
        _progress = 2;
        return;
    }

    // ---- state 2 -----------------------------------------------------------
    if (progress == 2) {
        const int32_t count = _saves._size;
        if (static_cast<int32_t>(field38) < count) {
            const SaveInfo* base = _saves._data;
            for (;;) {
                const int32_t at = static_cast<int32_t>(field38);
                // saveIndex >= 0 -> a real save, stop here and load it.
                // saveIndex <  0 -> the blank "new save" row; step over it.
                if (base[at].saveIndex >= 0) break;
                const int32_t next = at + 1;
                field38 = static_cast<uint32_t>(next);   // skipping a row with no save
                if (next >= count) break;
            }
        }

        const int32_t at = static_cast<int32_t>(field38);
        if (at == count) {
            _progress = 5;   // list complete
            return;
        }

        const SaveInfo& row = _saves._data[at];
        // `MOV R8D,0x2` — the same `2` UpdateLoadFromFile passes, spelled there
        // as `LEA R8D,[RAX+0x2]` off a zeroed register.
        persistence->BeginGetSaveGameInfo(row.name, 2);
        _progress = 3;
        return;
    }

    // ---- state 3 -----------------------------------------------------------
    if (progress == 3) {
        // The guard is INSIDE the state; the transition is not. See point 2.
        if ((persistence->_worker._runFlags & 1) == 0 && persistence->_worker._status == 1) {
            RtIo::SaveBlock block{};
            // `ADD RCX,0x28` — the EMBEDDED SavedState, not the heap one at
            // +0x40 that UpdateSave allocates. Hidden return slot in RDX.
            // ENDGAME B.3.7 B2: a direct member call (was an RT_EXPORT_ONCE door).
            block = _embeddedState.SetupLoad();

            const SaveInfo& row = _saves._data[static_cast<int32_t>(field38)];
            persistence->BeginLoad(row.name, 1, &block);
        }
        _progress = 4;
        return;
    }

    // ---- state 4 -----------------------------------------------------------
    if (progress == 4) {
        const int32_t at  = static_cast<int32_t>(field38);
        SaveInfo&     row = _saves._data[at];


        if ((persistence->_worker._runFlags & 1) == 0 && persistence->_worker._status == 1) {
            // --- the metadata arrived ---
            void*       pixels      = nullptr;
            int         w = 0, h = 0, field44 = 0;
            {
                System::String description;

                // ENDGAME B.3.7 B2: two direct member calls (were an
                // RT_EXPORT_ONCE door and a by-address door onto our own
                // ports). `row.mods` is the row's own Array<ModInfo> (+0x48).
                LoadMetaData(description, &pixels, w, h, field44, row.mods);

                row.SetThumbnail(pixels, w, h);

                // Retail RE-READS field38 and _savesData here (0x1800c22e8)
                // rather than reusing `row`: SetThumbnail can run arbitrary
                // code. Pointer compare, release, adopt, AddRef:
                // String::operator= (0x1800c9810), inlined.
                _saves._data[static_cast<int32_t>(field38)].description = description;
                _saves._data[static_cast<int32_t>(field38)].field44 = field44;

                // The by-value String LoadMetaData filled is ours to drop: its
                // destructor, as it leaves this scope.
            }
        } else {
            // --- no metadata: empty description and a black thumbnail ---
            {
                // `LEA RDX,[0x1801551f0]` — the DLL's own "" literal. Our "" is
                // a different pointer with the same contents, and Set copies
                // the characters, so the resulting String is identical.
                System::String empty;
                empty.Set("");

                // Pointer compare, release, adopt, AddRef: String::operator=
                // (0x1800c9810), inlined; the temporary is released as it
                // leaves this scope.
                _saves._data[static_cast<int32_t>(field38)].description = empty;
            }

            _saves._data[static_cast<int32_t>(field38)].field44 = 0;

            // Three independent reads of the sprite dimensions — see point 5.
            const int32_t w1 = _saveSprite.Width();
            const int32_t h1 = _saveSprite.Height();
            // `IMUL EAX,R9D` then `MOVSXD RCX,EAX` then `SHL RCX,2`: the
            // product is computed in 32 bits and SIGN-extended before scaling.
            const int64_t bytes = static_cast<int64_t>(static_cast<int32_t>(h1 * w1)) << 2;
            void* pixels = MemAlloc(static_cast<uint64_t>(bytes), 4);

            const int32_t w2 = _saveSprite.Width();
            const int32_t h2 = _saveSprite.Height();
            const int64_t bytes2 = static_cast<int64_t>(static_cast<int32_t>(h2 * w2)) << 2;
            if (pixels != nullptr) {
                // 0x1800881b0 is the DLL's own memset, called with (p, 0, n).
                // Spelled as memset rather than resolved by address for the
                // reason UpdateSave spells FUN_180032320 out: there is nothing
                // an address door would buy over the identical operation.
                std::memset(pixels, 0, static_cast<size_t>(bytes2));
            }

            const int32_t w3 = _saveSprite.Width();
            const int32_t h3 = _saveSprite.Height();
            // ENDGAME B.3.7 B2: a direct member call (was a by-address door).
            _saves._data[static_cast<int32_t>(field38)].SetThumbnail(pixels, w3, h3);
        }

        field38 = field38 + 1;   // next row
        _progress = 2;
        return;
    }

    // ---- state 5 -----------------------------------------------------------
    if (progress == 5) {
        // The minimum-operation-time gate, the same one UpdateSave has and
        // UpdateDelete/UpdateLoadFromFile do not: `CMP [RCX+8],RAX / JLE`.
        if (_elapsed <= _minOperationTime) return;

        // Retail's store order at 0x1800c2519..0x1800c2532 — operation,
        // elapsed, progress, selectedIndex. Same as UpdateLoadFromFile's,
        // NOT UpdateDelete's.
        _operation     = kOpNone;
        _elapsed       = ZeroTime();
        _progress      = 0;
        _selectedIndex = -1;
    }
}

// ============================================================================
// Save::SaveLoad::OnVideoError()        retail 0x1800c1000   (exported, NO caller)
// ----------------------------------------------------------------------------
// ENDGAME B.3.7 B2: defined so the class carries every export it declares; NOT
// registered, because nothing reaches it — retail's own MainLoop::OnVideoError
// INLINES this very walk (src/engine/engine/mainloop_video.cpp, step 5).
// Read whole from the retail bytes:
//
//   1800c1019  CMP [RCX+0x58],EBX / JLE      — count read, loop skipped if <= 0
//   1800c1030  MOV RAX,[RBP+0x50]             — _saves._data RE-READ per row
//   1800c1034  MOV RDI,[RAX+RSI+0x18]         — row.imageBuffer; null -> next
//   1800c103e  CMP [RAX+RSI+0x40],R14D / JL   — row.saveIndex < 0 -> next
//   1800c1045  MOV RCX,[RDI+0x60]             — its ImageBufferData; if non-null
//   1800c1056  CALL [RAX] with EDX=1          —   scalar deleting dtor (slot 0)
//   1800c1058  MOV [RDI+0x60],R14             — ALWAYS null the companion
//   1800c1062  CMP EBX,[RBP+0x58] / JL        — count RE-READ per iteration
// ============================================================================
void Save::SaveLoad::OnVideoError() {
    for (int32_t i = 0; i < _saves._size; ++i) {
        const SaveInfo& row = _saves._data[i];
        Video::ImageBuffer* const img = row.imageBuffer;
        if (img == nullptr) continue;
        if (row.saveIndex < 0) continue;
        Video::ImageBufferData* const data = img->_data;
        if (data != nullptr) {
            // `CALL [RAX]` with EDX=1: slot 0, the (vector) deleting destructor.
            // B5 RES (B5_RULES D10): `delete` -- the virtual destructor, then the
            // base's class-scope operator delete (MemFree).
            delete data;
        }
        img->_data = nullptr;
    }
}
