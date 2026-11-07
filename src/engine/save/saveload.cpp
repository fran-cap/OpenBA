// ============================================================================
// src/engine/save/saveload.cpp — Wave 5 lane C — Save::SaveLoad / Save::SavedState
// ============================================================================
// Ports all 17 of the real exports our tree calls on these two classes (Wave 14
// lane GENNAME closed the last one, GenerateSaveName, which this banner used to
// carry as the "16 of 17" gap). Every
// retail body below was decompiled AND disassembled at its real address in the
// SHIPPED Runtime-x64.dll (base 0x180000000) on 2026-09-06; both addresses and
// the oracle shape for each function are in docs/history/runtime_port/WAVE5_LANE_C.md.
//
// HELD BACK, with reasons (a held-back function with a stated reason is a
// result; a registered function nothing ever measured is not):
//
//   ?Draw@SaveLoad@Save@@…            0x1800c2eb0 — *** NO LONGER HELD BACK. ***
//       PORTED 2026-09-09 by Wave 14 lane DRAW, in src/engine/save/saveload_draw.cpp.
//       BOTH of this entry's reasons were wrong, and the correction is recorded
//       here rather than only in a wave doc because this is where the next
//       reader will meet the claim (PLAN §6 step 8).
//         * "it stamps five vtables this project does not own" — irrelevant and
//           also not a blocker. Those constructors are INLINED into Draw, so
//           there was never anything to port first; the four vtable symbols it
//           stamps are ordinary DATA EXPORTS of the shipped DLL (??_7Base@Object@@6B@,
//           ??_7External@Object@@6B@, ??_7MaterialInstance@Graphics@@6B@,
//           ??_7SpriteSheet@UI@@6B@) and resolve by name.
//         * "the load-bearing reason is the oracle: running both would draw
//           twice and leak a render target every frame" — the FREQUENCY was
//           wrong (Draw runs on exactly one frame per save, gated by
//           _captureFlags, not every frame) and the leak is the harness's to
//           fix, not the oracle's to suffer. saveload_draw.cpp's replay round
//           frees our readback buffer and rewinds _captureFlags, _capturePixels
//           and four Video::Context fields before handing the frame to the DLL,
//           so retail runs on pristine input and the game keeps RETAIL's result.
//       What the old comment WAS right about: MaterialInstance's and
//       SpriteSheet's vtables and SpriteContext::AddLine/SetScissor are still
//       individually unported. Draw does not need them to be.
//
//   ??1SaveLoad@Save@@QEAA@XZ         0x1800c3c90 — NO LONGER HELD BACK. This
//       entry described the destructor as "left on the DLL"; it has been ported
//       and registered since (see RtSave_SaveLoad_Dtor below and the
//       RegisterRuntimeExport at the bottom of this file). Corrected by Wave 14
//       lane SINGLETON, which had to know which of the five writers of
//       _singleton run OUR code and found this banner claiming one of them does
//       not. The oracle situation is unchanged and still honest: the dtor
//       frees, so nothing compares it.
//
//   ?_singleton@SaveLoad@Save@@0PEAV12@EA  0x1801fc6f0 — a DATA export, MINTED
//       and deliberately NOT promoted (Wave 14 lane SINGLETON). The claim above
//       — "a data symbol cannot be redirected before cutover" — was true of the
//       FUNCTION seam and false of the tree as it stands: Wave 9 lane C2's mint
//       registers data names and addresses through the same registry, so this
//       row COULD be redirected today. It is not, and the reasoning plus the
//       full reader/writer enumeration that would license it live in
//       standalone/reg_mint_promoted.cpp under "THE STANDING CASE FOR PROMOTING
//       AT CUTOVER". Short version: with the DLL present, registering the row
//       would CREATE a split (our writers move, retail's dead ?Get@ cannot),
//       and it buys nothing until the DLL is gone. What has NOT
//       changed is the rule the old text was protecting: the ctor and dtor
//       below must PUBLISH through `SaveLoadSingletonSlot()` rather than own a
//       static of their own, because that accessor is the door that follows the
//       registration. Verified per run by ProbeSaveLoadSingleton
//       (standalone/rt_orphan_probe.cpp, the old DLL-era switch).
//
// THE HAZARD THAT DEFINES THIS LANE. A wrong port here does not crash, it
// desyncs a save and surfaces later on an innocent record. So the oracle shape
// was chosen BEFORE each body was written, and three shapes are in use:
//
//   * FULL DIFF — both implementations run on the same inputs and the results
//     are compared. Only for functions that are read-only or idempotent:
//     GetOperation, GetSaveCount, GetSaveInfo, GetSaveState, CheckMod.
//
//   * SCRATCH-COPY DIFF — the function only writes into `*this` and allocates
//     nothing, so two private 0x90-byte copies of the live object can be made,
//     ours run on one and theirs on the other, the two compared byte for byte,
//     and only then is ours applied to the real object. Neither scratch run
//     touches shared state. Used by SetOperation, SetMinOperationTime,
//     SetSaveSprite, ??0SaveLoad, ??0SavedState, and — with refcount
//     compensation, see the comment there — SetSaveName.
//
//   * SCRATCH-OBJECT DIFF — theirs runs against a private replica of the
//     OUTPUT object rather than the live one, and the buffer it produces is
//     compared against ours and then freed. Used by Store and Retrieve, whose
//     whole job is to move bytes: running both against the live MemoryStream
//     would double-consume it, which is precisely the failure mode Wave 4
//     spent itself on.
//
// ~SavedState is ported with NO oracle and that is said plainly here rather
// than dressed up: it is nine instructions (`if (p) locked-free(p)`) and
// unambiguous, but nothing measures it.
//
// PLATFORM SEAM (PLAN §4.1 decision 1): nothing in these two classes touches
// Win32 directly. The critical sections retail inlines around Allocate/Free
// live inside MemoryAllocator::MemAlloc/MemFree, which this file calls as
// exports, so no `_win` twin is needed and <windows.h> stays out.
// ============================================================================

#include <cstdint>
#include "system/timer_internal.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "save/SaveLoad.h"
#include "io/Stream.h"
#include "video/ImageBuffer.h"   // B3X W0: ~SaveInfo deletes one by name
#include "system/Value.h"   // System::Time — Update / SetMinOperationTime
#include <new>                  // placement new into the retail hidden return slot
#include "system/string_internal.h"   // the String release helpers (Wave 2, lane String)
#include "saveload_internal.h"
#include "rt_crt.h"   // rtcrt::StrICmp, retail's _stricmp (D.4 Q9)
#include "system/Configuration.h"   // System::kModMinGameVersion

using RtSave::SaveLoad;
using RtSave::SavedState;
using RtSave::SaveInfo;
using RtSave::ModInfo;
using RtSave::Operation;
using RtIo::MemoryStream;

namespace {

// ----------------------------------------------------------------------------
// Dependencies. Every one of these is fetched from the SHIPPED DLL through
// the raw DLL door / the raw export door, never through ResolveExport.
//
// Two reasons, and they are different reasons:
//   1. Half of these (String::operator+, Filename::AdjustDelimiters,
//      MemAlloc/MemFree) are already ported by Waves 2 and 4. Resolving them
//      by NAME through the seam would make this lane's ordinary, non-diff path
//      silently depend on another lane's port being enabled. persistence.cpp
//      (Wave 4 lane C) made the same call for the same reason: a lane's job is
//      to be correct standing alone against the DLL.
//   2. the raw DLL door is GetProcAddress — an export-table binary search under the
//      loader lock. Two of them in one hot body cost Wave 2 five points of sim
//      time and were invisible to both profilers
//      (reference_rtoriginal_is_getprocaddress.md). the raw DLL door caches
//      per call site, which is why every accessor below is a function with a
//      `static` and not a bare the raw DLL door call.
// ----------------------------------------------------------------------------

// ----------------------------------------------------------------------------
// Dependencies. Wave 13 moved every definition that used to sit here into
// `saveload_internal.h`, unchanged, because `saveload_update.cpp` needs the
// same ones and two copies of `ReleaseStringSlot` would be two places to
// "fix" a transcribed idiom whose two dead stores look like mistakes. The
// comments explaining each one moved with it; nothing about the behaviour of
// this file changed.
// ----------------------------------------------------------------------------
using RtSaveDetail::MemAllocFn;
using RtSaveDetail::MemFreeFn;
using RtSaveDetail::SaveLoadVoidFn;
using RtSaveDetail::MemAlloc;
using RtSaveDetail::MemFree;
using RtSaveDetail::SaveLoadSingletonSlot;
using RtSaveDetail::FileSystemSingleton;
using RtSaveDetail::ModificationSingleton;
using RtSaveDetail::PersistenceSingleton;
using RtSaveDetail::SecondsPerTick;
using RtSaveDetail::ZeroTime;


}  // namespace

// ============================================================================
// Save::SaveLoad::SaveLoad()            retail 0x1800c3d30   ordinal 360
// ??0SaveLoad@Save@@QEAA@XZ
// ----------------------------------------------------------------------------
// Oracle: SCRATCH-COPY DIFF, with both scratches pre-filled with 0xCD rather
// than with the live object. That is deliberate: the ctor leaves seven fields
// untouched (see the header), so comparing two buffers that started identical
// and non-zero proves BOTH what it writes and what it does not — a plain
// zero-filled comparison would hide a missing store into an already-zero field.
//
// ORDER MATTERS. Theirs runs first, because the last thing the ctor does is
// publish `_singleton = this`; running theirs second would leave the DLL global
// pointing at a stack buffer that is about to go out of scope.
//
// Returns `this` in RAX (MOV RAX,RCX before the RET).
// ============================================================================
static void SaveLoad_CtorLogic(SaveLoad* self) {
    self->_operation        = RtSave::kOpNone;
    self->_elapsed          = 0;
    self->_minOperationTime = ZeroTime();
    self->_string20._data   = nullptr;
    // One qword store of 0 at +0x28 in retail, which is _id AND _size together,
    // then the pointer at +0x30 — see the embedded-SavedState note in the header.
    self->_embeddedState._id   = 0;
    self->_embeddedState._size = 0;
    self->_embeddedState._data = nullptr;
    self->field38           = 0;
    self->_selectedIndex    = -1;
    self->_saves._data      = nullptr;   // +0x50 qword
    self->_saves._size      = 0;         // +0x58 qword: _size and _capacity together
    self->_saves._capacity  = 0;
    self->_saveSprite.spriteSheet = nullptr;
    self->_saveSprite.spriteIndex = -1;
    self->_saveName._data   = nullptr;
    // Retail writes a BYTE here (MOV byte ptr [RCX+0x7c],DL), not a dword.
    self->_captureFlags     = 0;
    self->_captureWidth     = 0;
    self->_captureHeight    = 0;
    self->_capturePixels    = nullptr;
    if (SaveLoad** slot = SaveLoadSingletonSlot()) *slot = self;
}

SaveLoad* RtSave_SaveLoad_Ctor(SaveLoad* self) {
    SaveLoad_CtorLogic(self);   // runs last, so _singleton ends up pointing here
    return self;
}

// ============================================================================
// Save::SaveLoad::~SaveLoad()           retail 0x1800c3c90   ordinal 666
// ??1SaveLoad@Save@@QEAA@XZ
// ----------------------------------------------------------------------------
// WAVE 9 LANE SYS2, 2026-09-08 — PORTED. The Wave 5 holdback above it is
// preserved here in full because two of its three sentences were true and one
// was NOT, and which is which is the reason this got ported:
//
//   "No oracle is possible (it frees)"           -- STILL TRUE, see ORACLE.
//   "a wrong port corrupts the heap silently"    -- STILL TRUE.
//   "the object is a process-lifetime singleton
//    so it is unlikely ever to run"              -- **FALSE, and measurably so.**
//
// The third is what made the holdback look cheap, and it does not survive a
// grep. `??1SaveLoad@Save@@QEAA@XZ` has FOUR live call sites in this tree, all
// through `fn_SaveLoad_Dtor`, and every one of them is followed by a
// `fn_MemFreeStatic` of the same pointer — i.e. these are real deletes of a
// heap object, not a process-exit formality:
//
//   src/game/Dialog/mainmenudialog.cpp:218  MainMenuDialog_ReturnToMenu, the
//   src/game/Dialog/mainmenudialog.cpp:226    no-game and in-game branches
//   src/game/Game/gameupdateloop.cpp:3140   GameUpdateLoop::Shutdown
//   src/game/Tutorial/tutorialui.cpp:81     tutorial teardown
//
// Quit-to-menu runs this. So at cutover it is not a symbol that "might" be
// asked for -- attempt 14 recorded it as asked
// (wave9_attempt14_error.txt line 32), and with the DLL gone the resolve is
// NULL and `fn_SaveLoad_Dtor(saveLoad)` is a call through zero on the way back
// to the main menu.
//
// THE FULL RETAIL CHAIN, disassembled at all five addresses on 2026-09-08:
//
//   ~SaveLoad                0x1800c3c90   this file, below
//     -> FUN_1800329c0       0x1800329c0   Array<SaveInfo>::~Array, stride 0x58
//          -> FUN_1800c3f40  0x1800c3f40   ~SaveInfo (per element)
//               -> FUN_1800331f0 0x1800331f0  Array<ModInfo>::~Array, stride 0x20
//                    -> FUN_1800341e0 0x1800341e0  destroy N ModInfo in place
//
// FUN_1800341e0 releases the String at ModInfo+0x08 and THEN the one at +0x00,
// in that order, 0x20 bytes at a time — an independent confirmation of the
// ModInfo layout this file's own header pins from CheckMod.
//
// FOUR SaveInfo OFFSETS THIS DESTRUCTOR PINS THAT THE HEADER CALLS UNMODELLED.
// src/engine/save/SaveLoad.h deliberately declines to name +0x10..+0x37 and
// +0x40..+0x47, on the grounds that only UpdateList/LoadMetaData write them and
// naming from a guess is what PLAN §6 step 8 forbids. That stands — this port
// does NOT rename them. It reaches them as raw displacements, with what the
// destructor proves about each and nothing more:
//
//   +0x10  a System::String slot          (released with the refcount idiom)
//   +0x18  a pointer to a POLYMORPHIC object; retail calls vtable slot 0 with
//          EDX=1, i.e. the scalar DELETING destructor
//   +0x28  a raw heap block               (?MemFree@ static)
//   +0x40  an int32 ownership flag: BOTH +0x18 and +0x28 are torn down only
//          when it is >= 0. A negative value means "this SaveInfo does not own
//          them", the same -1-is-empty convention Sprite::index uses at +0x08.
//
// ORACLE: NONE, and that is a property of the function, not of this lane's
// effort. Everything it does is a free. "Call ours, call theirs, compare"
// double-frees every block in the object, and there is no scratch-copy form
// either: a 0x90-byte copy of the live SaveLoad aliases the SAME strings and
// the SAME array buffer, so running the DLL's body on the copy destroys the
// real object's heap before ours is allowed to run. What backs it instead:
//
//   1. Byte-for-byte transcription of all five bodies, disassembled (not
//      decompiled — reference_ghidra_c_output_vs_disassembly.md).
//   2. Every free routed through the SAME `MemFree()` accessor the rest of
//      this file already uses, so the heap stays whole-ours
//      (WAVE9.md §3). Retail's array-buffer frees are written out as
//      Lock(alloc+0x640); ?Free@(alloc, p); Unlock() -- which is what
//      ?MemFree@MemoryAllocator@System@@SAXPEAX@Z IS, and this file's
//      `MemFree` resolves to our port of exactly that.
//   3. The live path: quitting to the menu runs it, under the allocator
//      watchdog, and a wrong free surfaces there rather than nowhere.
//
// It is registered because at cutover the alternative to an unverifiable port
// is not "the DLL's body", it is a null call.
// ============================================================================

// ============================================================================
// Save::SaveLoad::ModInfo — its copy constructor and destructor (RULE B7-8).
// ----------------------------------------------------------------------------
// Neither is a function of its own in retail: both are the per-element arms of
// the Array<ModInfo> bodies, and they are transcribed from there.
//
// COPY — the assign arm of 0x180034010 / 0x180033080 (0x1800340b8..0x1800340ec):
//   [dst] = 0 ; if (src.name) { [dst] = src.name ; INC [src.name-4] }
//   [dst+8] = 0 ; if (src.path) { [dst+8] = src.path ; INC [src.path-4] }
//   +0x10, +0x14, +0x18 as three dwords; +0x1c NOT copied.
// The zero-adopt-AddRef of each String IS ??0String@System@@QEAA@AEBV01@@Z
// (0x180006cb0); the member initialisers run in declaration order, which is
// retail's store order (name, path, then the three dwords).
//
// DESTROY — one element of FUN_1800341e0: release +0x08 (path) FIRST, then
// +0x00 (name), with the String refcount idiom (both of its dead stores kept).
// That idiom IS ~String (0x180006c80), and the members are destroyed in reverse
// declaration order — path, then name — so the body is empty.
// ============================================================================
Save::SaveLoad::ModInfo::ModInfo(const ModInfo& other)
    : name(other.name),
      path(other.path),
      versionMajor(other.versionMajor),
      gameVersion(other.gameVersion),
      versionMinor(other.versionMinor) {}

Save::SaveLoad::ModInfo::~ModInfo() {}

// FUN_1800341e0 — destroy `count` ModInfo elements in place, stride 0x20,
// ascending. Retail's loop bails immediately when count <= 0 (the TEST/JLE is
// the first instruction, ahead of the prologue).
static void ModInfoRange_Destroy(RtSave::ModInfo* base, int32_t count) {
    if (count <= 0) return;
    for (int32_t i = 0; i < count; ++i) {
        base->Save::SaveLoad::ModInfo::~ModInfo();
        ++base;
    }
}

// FUN_1800331f0 — Collection::Array<ModInfo>::~Array. Retail's RCX is the ARRAY
// (`saveInfo + 0x48`), not the SaveInfo that contains it. The two trailing
// stores are ONE qword each: [arr+0] = 0 clears _data, and [arr+8] = 0 clears
// _size and _capacity together.
void RtSaveDetail::ArrayModInfoDtor(Collection::Array<Save::SaveLoad::ModInfo>* arr) {
    ModInfoRange_Destroy(arr->_data, arr->_size);
    if (arr->_data) MemFree(arr->_data);
    arr->_data = nullptr;
    arr->_size = 0;
    arr->_capacity = 0;
}

// ============================================================================
// Save::SaveLoad::SaveInfo::~SaveInfo()   FUN_1800c3f40 (unexported)
// ----------------------------------------------------------------------------
// What the destructor proves about the row:
//   +0x10  a System::String slot          (released with the refcount idiom)
//   +0x18  a pointer to a POLYMORPHIC object; retail calls vtable slot 0 with
//          EDX=1, i.e. the scalar DELETING destructor
//   +0x28  a raw heap block               (?MemFree@ static)
//   +0x40  BOTH +0x18 and +0x28 are torn down only when it is >= 0 — a
//          negative saveIndex is the blank row, which owns neither.
//
// The +0x18 branch tests the pointer, then the +0x40 flag, then the pointer
// AGAIN (0x1800c3f65 TEST RCX,RCX). The second test is redundant in the bytes
// too; it is transcribed as written rather than folded.
//
// ORDER: +0x18, +0x28, the mods Array (0x1800331f0), +0x38, +0x10. The body
// does the first three; the member destructors C++ runs AFTER it are, in
// reverse declaration order, `mods` (an Array the transcription has already
// emptied and zeroed: a no-op free of a null buffer), `name` (+0x38) and
// `description` (+0x10) — ~String IS the release idiom — then the trivial
// `thumbnail`. Nothing is released twice and nothing moves out of retail's
// order.
// ============================================================================
Save::SaveLoad::SaveInfo::~SaveInfo() {
    ::Video::ImageBuffer* owned = imageBuffer;
    const int32_t ownFlag = saveIndex;
    if (owned != nullptr && ownFlag >= 0 && owned != nullptr) {
        // MOV RAX,[RCX] ; MOV EDX,1 ; CALL [RAX]  -- scalar deleting dtor of the
        // row's Video::ImageBuffer (built by SetThumbnail), through its slot 0.
        delete owned;
    }

    void* block = thumbPixels;
    if (block != nullptr && ownFlag >= 0) MemFree(block);

    RtSaveDetail::ArrayModInfoDtor(&mods);

    // +0x38 then +0x10: the member destructors (see above). +0x38 is reached
    // in retail as [RBX+0x30]+0x8 -- the LEA RAX,[RBX+0x30] is the address of
    // an inlined Pair/String pair, not a separate object.
}
// (B3R W2-RT: the file-static SaveInfo_Dtor, the address door 0x1800c3f40's
// body, went with its row: nothing called it. ~SaveInfo above is the body.)

// FUN_1800329c0 — Collection::Array<SaveInfo>::~Array, stride 0x58.
// `_size` is read ONCE into EAX before the loop and used as the trip count, so
// an element destructor that changed the array's size (none does) would not be
// observed. Transcribed with the same single read.
void RtSaveDetail::ArraySaveInfoDtor(Save::SaveLoad::SaveInfoArray* arr) {
    RtSave::SaveInfo* e = arr->_data;
    const int32_t size = arr->_size;
    if (size > 0) {
        for (int32_t i = 0; i < size; ++i) { e->Save::SaveLoad::SaveInfo::~SaveInfo(); ++e; }
    }
    if (arr->_data) MemFree(arr->_data);
    arr->_data = nullptr;
    arr->_size = 0;
    arr->_capacity = 0;
}

// The ??1SaveLoad DOOR keeps its body (the class declares no destructor: see
// SaveLoad.h's banner — nothing destroys a SaveLoad in C++).
void RtSave_SaveLoad_Dtor(SaveLoad* self) {
    // NOT RETAIL (review S-004): wait for the persistence job in flight before anything is
    // freed. Retail ??1SaveLoad (0x1800c3c90) frees _embeddedState._data (+0x30) below while a
    // SaveJob may still read it as blocks[0] (UpdateSaveTail lends it; SaveJob's CRC and
    // FileWrite loop in persistence_worker.cpp): MainMenuDialog::ReturnToMenu (0x1400b4790)
    // and the quit (GameUpdateLoop::Shutdown, before Persistence ends) both destroy the
    // SaveLoad with no wait, a use-after-free on the worker thread. hFinished is manual-reset
    // and every enqueue resets it, so this returns at once when no job runs.
    // The wait is not gated on IsWorking(): bit 0 of _runFlags is set only once the worker
    // thread wakes (Thread::ThreadFunc), so a job enqueued this frame and not yet picked up
    // reads as idle while hFinished is already reset. hFinished alone covers both cases.
    if (::IO::Persistence* persistence = PersistenceSingleton()) {
        persistence->_worker.WaitForFinish();
    }

    // MOV qword ptr [0x1801fc6f0],0x0 -- unconditional, and it happens FIRST,
    // before anything can fault. Retail clears the singleton even when `this`
    // is not the singleton; that is what the bytes do.
    if (SaveLoad** slot = SaveLoadSingletonSlot()) *slot = nullptr;

    // SaveLoad has no C++ destructor (the header's banner): the two Strings'
    // releases are their destructors, run explicitly at retail's points.
    self->_saveName.System::String::~String();
    RtSaveDetail::ArraySaveInfoDtor(&self->_saves);

    // FUN_180006a10, the un-exported second copy of MemFree
    // (src/engine/engine/engine_lifecycle.cpp:262 records it as "MemFree
    // verbatim"). Routed through the same accessor, not the raw address. This
    // is the embedded SavedState's buffer (+0x30), freed inline by retail.
    void* buffer30 = self->_embeddedState._data;
    if (buffer30) MemFree(buffer30);

    self->_string20.System::String::~String();
}

// ============================================================================
// Save::SaveLoad::GetOperation() const  retail 0x1800c3c80   ordinal 2826
// ?GetOperation@SaveLoad@Save@@QEBA?AW4Operation@12@XZ
// ----------------------------------------------------------------------------
// ABI: the mangled return `?AW4Operation@12@` is an enum BY VALUE, and the
// lead flagged it as a possible hidden-return-slot trap. It is not one: an
// enum with a 4-byte underlying type fits in a register, so it comes back in
// EAX with no hidden slot. Confirmed against the retail bytes — the whole
// function is `MOV EAX,[RCX] ; RET`, RCX is `this` and RDX is untouched.
// (A hidden slot would have made `this` arrive in RDX; getting that wrong is
// the wild-write family of reference_hidden_retslot_wild_write.md.)
//
// Oracle: FULL DIFF — a single load, no side effects.
// ============================================================================
// ENDGAME B.3.7 B2: the body is the inline member (SaveLoad.h).

// ============================================================================
// Save::SaveLoad::GetSaveCount() const  retail 0x1800c3b80   ordinal 2885
// ?GetSaveCount@SaveLoad@Save@@QEBAHXZ
// ----------------------------------------------------------------------------
// `return *(int*)(this + 0x58)` — the _size half of the Array<SaveInfo> at
// +0x50. Oracle: FULL DIFF.
// ============================================================================

// ============================================================================
// Save::SaveLoad::GetSaveInfo(int) const  retail 0x1800c3b60  ordinal 2889
// ?GetSaveInfo@SaveLoad@Save@@QEBAAEBUSaveInfo@12@H@Z
// ----------------------------------------------------------------------------
// ABI: `AEBU` is a REFERENCE return, not a by-value struct return, so it comes
// back in RAX and there is no hidden slot. (`?AU` would have been by value and
// would have needed one.) The body is `this->_savesData + index * 0x58` with
// NO bounds check — index is sign-extended (MOVSXD) and multiplied, so a
// negative or out-of-range index produces a wild pointer. Reproduced exactly;
// CLAUDE.md's rule is that retail's unguarded arithmetic is load-bearing.
//
// Oracle: FULL DIFF on the returned pointer.
// ============================================================================

// ============================================================================
// Save::SaveLoad::GetAutoSaveName() const  retail 0x1800c1440   ordinal 2457
// ?GetAutoSaveName@SaveLoad@Save@@QEBA?AVString@System@@XZ
// ----------------------------------------------------------------------------
// ABI: `?AV...@@` is a class returned BY VALUE, so unlike GetOperation this
// DOES have a hidden return slot -- and `self` (RCX) is genuinely dead, not
// merely unused-by-convention: retail never stores RCX to its home slot and
// never reads it. Confirmed byte-identical in BOTH the profile build
// (0x180090a90) and retail (0x1800c1440):
//     MOV [RSP+0x10],RDX ; PUSH RBX ; SUB RSP,0x30    <- RDX = retslot, homed
//     MOV RBX,RDX                                     <- kept across the call
//     XOR EAX,EAX ; MOV [RDX],RAX                     <- ret->data = nullptr
//     LEA RDX,[0x180155b30] ; MOV RCX,RBX              <- "Auto\0" literal
//     CALL String::Set [0x1800c95d0]                   <- Set(ret, "Auto")
//     MOV RAX,RBX ; RET                                 <- return ret
// String::Set is already ported verbatim as RtSystem_String_Set
// (src/engine/system/string.cpp, same 0x1800c95d0 body), so this reduces to
// zeroing the slot and calling it -- retail always returns the literal "Auto"
// regardless of `self`.
//
// Oracle: FULL DIFF, the diff oracle against a genuine second call into the
// shipped export (not a self-check -- reference_shadow_marker_is_not_an_oracle.md),
// same shape as RtSystem_String_Concat's diff above. The function takes no
// meaningful input (every call is the same case), so the commit's input sweep
// instead varies `self` -- nullptr, a stack-garbage pointer, and a live
// SaveLoad -- to verify the "self is dead" claim rather than sweeping an
// argument that does nothing.
// ============================================================================
System::String Save::SaveLoad::GetAutoSaveName() const {
    // ENDGAME B.3.1 step 2: retail's `*ret = 0` IS the default ctor
    // (0x180006760) and the hidden return slot is where the String is born, so
    // the member builds its return value in place (NRVO) and Set (0x1800c95d0)
    // runs on it.
    System::String result;
    result.Set("Auto");
    return result;
}

// Save::SaveLoad::GenerateSaveName() const  retail 0x1800c1250  ordinal 2401
// ?GenerateSaveName@SaveLoad@Save@@QEBA?AVString@System@@XZ
// ----------------------------------------------------------------------------
// ABI: a const member returning System::String BY VALUE. The disassembly has
// `this` arriving in RCX and the hidden return slot in RDX — the MEMBER-
// function ordering (`this` is the first physical argument, the retslot is
// pushed to second), not the namespace-scope one (contrast
// RtSystem_String_Concat's banner in string.h, where the retslot IS first).
// This matches `NameGetterFn` at saveload_update.cpp:362 — `(self, ret)` — the
// typedef this function's two existing call sites already use.
//
// LOGIC (0x1800c1250..0x1800c1449; read from the disassembly, not the
// decompiled C, which collapses the do-while into goto soup Ghidra itself
// mistypes as `void`): try candidates "Save0", "Save1", "Save2", ... in order
// and return the first one that does not equal the `name` field (SaveInfo+
// 0x38, RtSave::SaveInfo::name) of any row in
// self->_savesData[0 .. self->_savesSize). The inlined per-row comparison is
// length-then-bytes with two empty/null strings counted equal — exactly what
// the already-ported `RtSystem::RtString_StrCompare` (string.cpp) computes,
// so this port calls that directly instead of re-transcribing a second copy
// of the same comparison. (A null row name, length 0, can never collide with
// a candidate — "SaveN" is never shorter than 5 bytes — so the reuse changes
// no observable outcome.)
//
// `self->_savesSize < 1` is tested ONCE PER OUTER ITERATION, right after the
// candidate is formatted and stored — not once before the loop — so an empty
// save list returns "Save0" on the very first pass without ever touching
// self->_savesData. Retail's retslot is zeroed UNCONDITIONALLY at entry
// (`*in_RDX = 0`), not released: this is the caller-pre-zeroes-the-String
// convention (both existing call sites already set `generated = nullptr`
// before calling), reproduced here rather than "fixed" into a release
// (CLAUDE.md — retail's unguarded behaviour is load-bearing, not a bug).
//
// Oracle: FULL DIFF on the returned string's CONTENT (RtString_StrCompare),
// not its pointer — ours and theirs each format their own fresh buffer, so
// equal candidates never share an address the way GetSaveInfo's pointer-
// identity oracle can rely on.
// ============================================================================
System::String Save::SaveLoad::GenerateSaveName() const {
    System::String result;     // retail's unconditional `*ret = 0`, NOT a release
    int index = 0;
    for (;;) {
        // The inlined "String::Format into a scratch, then refcount-assign
        // into *ret" idiom (0x1800c1290..0x1800c1310): ?Format@ (0x1800c9730)
        // builds `tmp` in place at refcount 1; the pointer-compare, release,
        // adopt, AddRef that follows IS String::operator= (0x1800c9810); the
        // scratch's own reference is released as `tmp` leaves the scope.
        {
            System::String tmp = System::String::Format("Save%i", index);
            result = tmp;
        }
        if (_saves._size < 1) break;
        bool collision = false;
        for (int i = 0; i < _saves._size; ++i) {
            // ENDGAME B.3.1 step 2: the static, spelled as itself (0x1800c98a0).
            if (System::String::StrCompare(_saves._data[i].name._data, result._data) == 0) {
                collision = true;
                break;
            }
        }
        if (!collision) break;
        ++index;
    }
    return result;
}

// ============================================================================
// Save::SaveLoad::GetSaveState()        retail 0x1800c3b50   ordinal 2891
// ?GetSaveState@SaveLoad@Save@@QEAAPEAVSavedState@2@XZ
// ----------------------------------------------------------------------------
// `return *(SavedState**)(this + 0x40)`. Note that ??0SaveLoad does NOT
// initialise +0x40 (see the header's list), so before any operation has run
// this returns whatever the allocator left there. Faithful.
// Oracle: FULL DIFF.
// ============================================================================

// ============================================================================
// Save::SaveLoad::SetOperation(Operation, int)  retail 0x1800c3c50  ordinal 3934
// ?SetOperation@SaveLoad@Save@@QEAAXW4Operation@12@H@Z
// ----------------------------------------------------------------------------
// Four stores and one global read. Note it zeroes _progress (+0x18), which the
// CONSTRUCTOR does not — so a freshly built SaveLoad has an uninitialised
// progress value until the first SetOperation. Faithful.
// Oracle: SCRATCH-COPY DIFF.
// ============================================================================
void Save::SaveLoad::SetOperation(Operation op, int index) {
    _operation         = op;
    const int64_t zero = ZeroTime();   // retail computes this BEFORE the two stores below
    _progress          = Progress::Start;
    _selectedIndex     = index;
    _elapsed           = zero;
}

// ============================================================================
// Save::SaveLoad::SetMinOperationTime(const Time&)  retail 0x1800c3c40  ord 3920
// ?SetMinOperationTime@SaveLoad@Save@@QEAAXAEBVTime@System@@@Z
// ----------------------------------------------------------------------------
// One qword copy. System::Time is a single int64 tick count (Wave 2 pinned it,
// src/engine/system/Value.h). Oracle: SCRATCH-COPY DIFF.
// ============================================================================
void Save::SaveLoad::SetMinOperationTime(const System::Time& time) {
    _minOperationTime = time._ticks;
}

// ============================================================================
// Save::SaveLoad::SetSaveSprite(UI::Sprite)   retail 0x1800c3b90  ordinal 3955
// ?SetSaveSprite@SaveLoad@Save@@QEAAXVSprite@UI@@@Z
// ----------------------------------------------------------------------------
// ABI, and the lead flagged this one specifically: `VSprite@UI@@` is a class
// BY VALUE. UI::Sprite is 16 bytes, which does not fit in a register, so the
// MS x64 ABI passes it by HIDDEN POINTER — the callee gets an address in RDX,
// not the value. Read out of the retail bytes rather than assumed:
//     MOV RAX,[RDX] ; MOV [RCX+0x60],RAX ; MOV RAX,[RDX+8] ; MOV [RCX+0x68],RAX
// RDX is dereferenced, so it is a pointer. Declaring the parameter as a value
// here would have made GCC read the caller's 16 bytes out of RDX:RSI and
// corrupt the sprite.
//
// This also matches the existing typedef in engine/funcs/engine_funcs.h:1102
// (`void (*)(void* saveLoad, void* sprite)`), which is already correct.
//
// Oracle: SCRATCH-COPY DIFF.
// ============================================================================

// ============================================================================
// Save::SaveLoad::SetSaveName(const String&, int)  retail 0x1800c3bb0  ord 3953
// ?SetSaveName@SaveLoad@Save@@QEAAXAEBVString@System@@H@Z
// ----------------------------------------------------------------------------
// A refcounted String assignment into +0x70 plus a mode int into +0x78. If the
// incoming data pointer is already the one stored, retail writes ONLY the mode
// and does no refcount work at all — that short path is why the compensation
// below is conditional.
//
// ORACLE: SCRATCH-COPY DIFF WITH REFCOUNT COMPENSATION, and this is the one
// place in the lane where the oracle needed real thought, so the reasoning is
// written out.
//
// The naive scratch diff does not work here. Both scratch runs manipulate the
// refcounts of the SAME two shared strings, so the old string would be
// decremented twice; if its count was 1, the first decrement frees it and the
// second writes through a freed block. That is the Wave 4 lane H defect
// (a String release applied where it did not belong) in a new costume.
//
// The fix is to make the extra pair of operations provably harmless:
//   * BEFORE the two scratch runs, add 2 to the OLD string's refcount, so the
//     two decrements can never reach zero and never free.
//   * AFTER them, subtract 2 from the NEW string's refcount, undoing the two
//     increments.
// The live object is then updated by one ordinary run of ours, and the net
// refcount effect of the whole thing is exactly one assignment. Nothing is
// freed early and nothing leaks.
// ============================================================================
void Save::SaveLoad::SetSaveName(const System::String& name, int mode) {
    // Pointer compare, release, adopt, AddRef: String::operator= (0x1800c9810),
    // inlined by retail.
    _saveName = name;
    _saveNameMode = mode;
}

// ============================================================================
// Save::SaveLoad::CheckMod(int, int) const   retail 0x1800c1090   ordinal 1691
// ?CheckMod@SaveLoad@Save@@QEBAHHH@Z
// ----------------------------------------------------------------------------
// The function Wave 4 lane H identified as the whole reason a mod-mismatched
// save used to stall (WAVE4_LANE_H.md §5a). It answers "does save[saveIndex]'s
// mod[modIndex] match something installed?" with the code SaveLoadDialog::DoLoad
// turns into a modal:
//     1  modsMissing     no installed mod has this path
//     4  modsChanged     major version differs
//     3  modsVersion     minor version differs
//     2  modsOutOfDate   the save's recorded game version is below 0x1000400
//     0  fine
//
// TWO THINGS LANE H's WRITE-UP GOT SLIGHTLY WRONG, corrected here from the
// disassembly rather than from its prose:
//   (a) it says the comparison uses "the save's recorded mod path string"
//       without saying which field. It is ModInfo+**0x08**, not +0x00. The
//       instruction is `LEA R8,[RBP+0x8]` immediately before the operator+
//       call. +0x00 is the display name our DoLoad trace prints.
//   (b) it says the linear search compares "the String at entry +0x58 by
//       length then _stricmp". Right — and worth keeping, because the length
//       pre-check reads `data[-8]`, i.e. the String allocation header, and
//       treats a null data pointer as length 0 rather than faulting.
//
// ORACLE: FULL DIFF. CheckMod allocates three temporary Strings and releases
// all three before returning, reads two DLL singletons, and mutates nothing —
// so calling it twice is safe and the second call sees identical inputs.
//
// _stricmp: retail calls its own CRT copy at 0x18007eefc. Ours calls the same
// body, ported: rtcrt::StrICmp (src/engine/crt/msvc_stricmp.cpp, D.4 Q9), so
// no host-CRT locale state can change the answer.
// ============================================================================
int Save::SaveLoad::CheckMod(int saveIndex, int modIndex) const {

    const ::IO::Modification* const mods = ModificationSingleton();
    // `MOVSXD R9,EDX ; IMUL R9,R9,0x58` then `SHL RBP,0x5` into +0x48's
    // buffer: GetSaveInfo's unchecked arithmetic, and the mods Array's _data.
    const SaveInfo* info = &GetSaveInfo(saveIndex);
    const ModInfo* mod = info->mods._data + modIndex;

    // Linear scan of IO::Modification: count at +0x10, array base at +0x8,
    // stride 0x80, the entry's path String at +0x58. Length first, then
    // _stricmp — a null String on either side counts as length 0.
    // `found`, `count` and `base` are DECLARED here only because `base` outlives
    // the string scope below; they are still READ where retail reads them,
    // after the three Strings exist.
    int found = -1;
    int32_t  count = 0;
    const RtIO::Mod* base = nullptr;

    // full = GetDataDirectory() + mod->path,  then AdjustDelimiters(full)
    //
    // The explicit scope below ends exactly where retail's three releases are,
    // so the destructors fire there and not at the end of the function.
    //
    // ENDGAME B.3.1 step 2. RETAIL'S RELEASE ORDER HERE IS work, dataDir,
    // joined (0x1800c25xx) — NOT UpdateLoad's work, joined, dataDir — so the
    // declarations are ordered so that reverse-destruction reproduces it
    // exactly: joined first, dataDir second, work last.
    //
    // `joined` is therefore default-constructed and then MOVE-assigned from
    // operator+'s prvalue. The move is a pure pointer steal (String.h), so the
    // refcount trace is identical to retail's construct-into-the-return-slot:
    // the buffer is born at 1 and stays at 1. A COPY assignment here would add
    // an AddRef/Release pair that retail never spends.
    {
    System::String   joined;
    System::Filename dataDir(                                   // 0x180007bd0
        FileSystemSingleton()->GetDataDirectory());

    // The `??HSystem@@YA?AVString@0@AEBV10@0@Z` door IS System::operator+
    // (0x1800c9440).
    joined = dataDir.AsString() + mod->path;

    // Retail adopts the concatenation's buffer into a fresh slot (with an
    // AddRef) and adjusts THAT, leaving `joined` itself untouched, so the two
    // Strings are released separately. Adopt-into-a-nulled-slot followed by
    // AdjustDelimiters IS ??0Filename@System@@QEAA@AEBVString@1@@Z
    // (0x18000c780), so this one line spends both.
    System::Filename work(joined);

    count = mods->_mods._size;
    base  = mods->_mods._data;
    for (int i = 0; i < count; ++i) {
        const char* entry = base[i].file._data;
        const int lenEntry = entry ? RtSystem::StringLength(entry) : 0;
        // GetLength (0x1800063c0) IS the null-guarded header read this line
        // used to spell by hand.
        const int lenWork  = work.GetLength();
        if (lenEntry == lenWork && rtcrt::StrICmp(entry, work._data) == 0) { found = i; break; }
    }
    }   // <- work, dataDir, joined destructors: retail's three releases

    int ours;
    if (found < 0) {
        ours = 1;
    } else {
        const RtIO::Mod& entry = base[found];
        // Equality only (`CMP ; JZ`): signedness is unobservable.
        if (static_cast<uint32_t>(entry.i2c) != mod->versionMajor)      ours = 4;
        else if (static_cast<uint32_t>(entry.i1c) != mod->versionMinor) ours = 3;
        else ours = (mod->gameVersion < System::kModMinGameVersion) ? 2 : 0;
    }

    return ours;
}

// ============================================================================
// Save::SaveLoad::Update(const Time&)   retail 0x1800c2e40   ordinal 4501
// ?Update@SaveLoad@Save@@QEAAXAEBVTime@System@@@Z
// ----------------------------------------------------------------------------
// The state machine's tick. Accumulates the frame delta into _elapsed and then
// dispatches on _operation to one of five exported UpdateXxx bodies, but ONLY
// while `!(Persistence::_singleton[0x18] & 1) && Persistence::_singleton[0x48]`
// — a gate this lane does not model further, because IO::Persistence's layout
// past +0x08 is explicitly not pinned (src/engine/io/Persistence.h models
// only _savePath at +0x90 and calls +0x08..+0x8f "NOT modelled"). Both are read
// here as raw displacements with that stated, rather than by inventing two
// field names in someone else's header.
//
// ORACLE: PARTIAL, and this is stated plainly rather than dressed up as a
// full one. The accumulation is checked in shadow mode against an independent
// replica; the DISPATCH is not checked by anything, because running it twice
// would perform the save/load/delete file work twice. What backs the dispatch
// is that all five targets stay on the DLL and the branch is five integer
// comparisons transcribed from a decompile — plus the end-to-end evidence that
// a save still saves and a load still loads.
//
// The five targets are resolved through RT_EXPORT_ONCE — the port registry,
// cached once per call site. WAVE 9 LANE V2, 2026-09-07 corrects this: the
// paragraph used to say the raw DLL door "NOT ResolveExport", on the grounds
// that the five are unported and a bare ResolveExport per frame would be the
// Wave 2 perf defect again. The perf half is right and RT_EXPORT_ONCE keeps it
// (one resolve per call site, then a static). The door half is wrong: this
// dispatch IS the work of Update, not an oracle, so it is a REAL DEPENDENCY.
// On the raw door it is a call through NULL the moment the DLL is absent; on
// the port door it picks up a port of any of the five the day one lands, and
// until then it is a named unresolved import at cutover instead of a crash.
// ============================================================================
void Save::SaveLoad::Update(const System::Time& delta) {
    _elapsed = _elapsed + delta._ticks;

    const ::IO::Persistence* p = PersistenceSingleton();
    if (p->_worker.IsWorking()) return;
    if (p->_worker._status == static_cast<int32_t>(::IO::PersistenceWorker::Status::Pending)) return;

    // ENDGAME B.3.7 B2: the five targets are members of this class now, called
    // directly (they were five RT_EXPORT_ONCE doors onto our own ports).
    switch (_operation) {
    case kOpList:         UpdateList();         break;
    case kOpLoad:         UpdateLoad();         break;
    case kOpLoadFromFile: UpdateLoadFromFile(); break;
    case kOpSave:         UpdateSave();         break;
    case kOpDelete:       UpdateDelete();       break;
    default:              break;
    }
}

// ============================================================================
// Save::SavedState::SavedState()        retail 0x1800c0ee0   ordinal 362
// ??0SavedState@Save@@QEAA@XZ
// ----------------------------------------------------------------------------
// Two qword zero stores. Oracle: SCRATCH-COPY DIFF over 0x10 bytes, both
// scratches pre-filled with 0xCD so the comparison proves the writes rather
// than agreeing on an accident of zeroed memory.
// ============================================================================

// ============================================================================
// Save::SavedState::~SavedState()       retail 0x1800c0ea0   ordinal 667
// ??1SavedState@Save@@QEAA@XZ
// ----------------------------------------------------------------------------
// `if (_data) { lock; Free(_singleton, _data); unlock; }` — which is exactly
// ?MemFree@MemoryAllocator@System@@SAXPEAX@Z (0x1800c7ad0), whose whole body is
// the same null check around the same locked Free. Retail inlined it here and
// calls the export elsewhere; the two are the same code.
//
// It does NOT null _data afterwards, so the object is left holding a dangling
// pointer. Faithful — this is a destructor, and its callers do not read it back.
//
// ORACLE: NONE. Running theirs as well would free the same block twice. Said
// plainly rather than hidden: this function is ported on the strength of nine
// unambiguous instructions and is measured only by `calls=`.
// ============================================================================
Save::SavedState::~SavedState() {
    if (_data != nullptr) MemFree(_data);
}

void RtSave_SavedState_Dtor(SavedState* self) {
    self->Save::SavedState::~SavedState();
}

// ============================================================================
// Save::SavedState::Store(IO::MemoryStream&)   retail 0x1800c0da0  ordinal 4112
// ?Store@SavedState@Save@@QEAAXAEAVMemoryStream@IO@@@Z
// ----------------------------------------------------------------------------
// Detaches a private copy of everything written into the stream so far:
//     src  = stream->_data                       (+0x18)
//     size = stream->vtbl[5](stream)             (+0x28 == GetCurrentPos)
//     if (_data) MemFree(_data);
//     _size = size;
//     _data = locked-Allocate(size, 4);          (== MemAlloc(size, 4))
//     memcpy(_data, src, _size);
//
// The virtual slot: src/engine/io/Stream.h (Wave 4 lane B) reads
// MemoryStream's exported vtable byte-wise — index 5 at displacement +0x28 is
// GetCurrentPos (0x1800ae9c0), a pure getter. Note the last memcpy length is
// re-loaded from `this->_size`, not from the local; identical value, but
// transcribed as written.
//
// The locked Allocate that retail inlines is byte-identical to
// ?MemAlloc@MemoryAllocator@System@@SAPEAX_KH@Z (decompiled both).
//
// ORACLE: SCRATCH-OBJECT DIFF, the real one. Theirs runs against a private
// zeroed SavedState, so its `if (_data) MemFree` is skipped and it allocates
// its own buffer; we then compare the two buffers byte for byte and free
// theirs. The stream is only READ by either run, so nothing is double-consumed
// — which is the failure mode this whole lane was warned about.
// ============================================================================
void Save::SavedState::Store(IO::MemoryStream& stream) {
    void* src = stream._data;
    // Slot 5 (+0x28) of the stream's table, GetCurrentPos — a real virtual call
    // on the real class, dispatching through the live object's (minted) table
    // exactly as retail's `CALL [RAX+0x28]` does.
    const uint32_t size = stream.GetCurrentPos();

    if (_data != nullptr) MemFree(_data);
    _size = size;
    _data = MemAlloc(size, 4);
    std::memcpy(_data, src, _size);
}

// ============================================================================
// Save::SavedState::Retrieve(IO::MemoryStream&)  retail 0x1800c0e50  ord 3586
// ?Retrieve@SavedState@Save@@QEAAXAEAVMemoryStream@IO@@@Z
// ----------------------------------------------------------------------------
//     MemoryStream::Open(stream, this->_size);      // 0x1800ae670
//     memcpy(stream->_data, this->_data, this->_size);
// and the memcpy is a tail JMP, with _size re-loaded after Open.
//
// MemoryStream::Open (decompiled) allocates a fresh buffer, stores it at +0x18,
// sets _size, zeroes _cursor, sets _direction = 0 (reading) and _ownsData = 1
// — and does NOT free whatever buffer the stream already held. That leak is
// retail's; it is also the reason theirs cannot simply be run a second time
// against the live stream in diff mode.
//
// ORACLE: SCRATCH-OBJECT DIFF. Theirs runs against a private copy of the
// stream OBJECT (0x30 bytes, taken before ours ran), so it allocates its own
// buffer, and we compare that buffer against the one ours produced and then
// free it. Both runs only read `*this`.
// ============================================================================
void Save::SavedState::Retrieve(IO::MemoryStream& stream) {
    // ENDGAME B.3.9 IO (W2, foreign site): ?Open@MemoryStream is the member.
    stream.Open(static_cast<int>(_size));
    std::memcpy(stream._data, _data, _size);
}

// ============================================================================
// Registration
// ----------------------------------------------------------------------------
// Every name below is spelled as a SINGLE string literal on ONE line. The
// census (tools/runtime_census.py) matches `RegisterRuntimeExport("<literal>"`
// and can see through neither a named constant nor a line break; both blind
// spots have already produced a silent under-count of live ports in this
// project (WAVE3.md's Object 21/41 was really 27/41).
//
// Every name was also checked against the shipped DLL's export directory with
// pefile before being written here — see the phantom-name section of
// docs/history/runtime_port/WAVE5_LANE_C.md, which found that two of the names our
// tree has been asking for do not exist in the DLL at all.
//
// TWO FUNCTIONS ARE REGISTERED TWICE, BY NAME *AND* BY ADDRESS.
// `engine/funcs/engine_funcs.cpp` resolves GetOperation (:2428) and
// GetSaveState (:2468) by NAME and then, if that returns null, falls back to
// `ResolveAddr(Module::Runtime, …)`. The name path works today, so the port is
// already reached — but the fallback is a live second door, and a name path
// that is later renamed or removed would send the call back to the DLL with no
// diagnostic whatsoever. That silent reversion is this project's single most
// repeated failure mode (the phantom-name family, the unregistered type-id
// slot, the null dispatch slot read under the wrong name), and closing it
// costs one line. `src/engine/achievement/goal.cpp` registers all eight of
// its ports both ways for the same reason.
//
// The `++n` deliberately does NOT fire on the address registrations: `n` is
// the count of PORTED FUNCTIONS, and these are second doors onto functions
// already counted, not extra ports. Incrementing here would inflate the census
// figure the project treats as authoritative over any agent's claim.
// ============================================================================
// ---------------------------------------------------------------------------
// Save::SaveLoad::Get() - retail 0x180030D40, ord 2425.  WAVE 23 LANE TAIL
//
// The WHOLE retail body:
//
//   180030d40  MOV RAX,qword ptr [0x1801fc6f0]
//   180030d47  RET
//
// `SA` is the static-member spelling: no `this`, returns `SaveLoad*`.
//
// ---------------------------------------------------------------------------
// WHY THIS ROW EXISTS AT ALL, SAID PLAINLY
// ---------------------------------------------------------------------------
// It is in the census's `called` column because standalone/rt_orphan_probe.cpp
// NAMES IT AS A STRING LITERAL, and that file says so itself. Nothing in any
// shipped binary calls it. Lane TAIL re-derived that independently rather than
// inheriting it:
//
//   * Runtime-x64.dll: `get_xrefs_to(0x180030d40)` returns exactly two
//     references, `0x1801B3028 [DATA]` (a .pdata unwind record) and the export
//     directory. No call site.
//   * Application-x64.exe (899 imports), VideoDX11-x64.dll (163) and
//     VideoDX9-x64.dll (146): the name is in none of their import tables.
//     Checked with pefile in this lane, not quoted from the prior record.
//
// SO THIS PORT CLOSES A ROW OUR OWN DIAGNOSTIC CREATED AND BUYS NO BEHAVIOUR
// TODAY. That is stated rather than dressed up. It is landed anyway for one
// reason that is NOT cosmetic:
//
//   `?_singleton@SaveLoad@Save@@0PEAV12@EA` IS PROMOTED (census ported=1), so
//   our storage is the live one and the DLL's .data copy is stale. Retail's
//   `Get` is an un-interceptable RIP-relative load of that stale copy - the
//   very fact reg_mint_promoted.cpp leans on to argue the promotion is safe.
//   Reading through RT_DATA_SLOT makes OUR `Get` return the live pointer, so
//   the symbol is correct at cutover instead of resolving null.
//
// ---------------------------------------------------------------------------
// THIS DOES NOT DARKEN THE ORACLE - CHECKED, NOT ASSUMED
// ---------------------------------------------------------------------------
// `ProbeSaveLoadSingleton` uses retail's `Get` as its oracle: it writes six
// sentinels into the DLL's raw slot and requires `Get` to hand each back. That
// is exactly the shape that went dark for five waves in Wave 5's Goal::Serialize
// probe, where the ours-side reached the body BY ADDRESS and so compared retail
// with itself (reference_registering_an_address_converts_a_selfcheck_into_an_oracle.md).
//
// It survives here because the probe resolves `Get` with **the raw export door**
// (standalone/rt_orphan_probe.cpp, `retailGet`), which skips the port registry
// by construction. Registering this body under the NAME door leaves that line
// pointing at the shipped bytes. The ADDRESS door is registered too, and that
// is the one that COULD have broken it - it does not, because the probe never
// resolves 0x180030D40 by address. Both doors were checked against that file
// before either was added.
// ---------------------------------------------------------------------------
Save::SaveLoad* Save::SaveLoad::Get() {
    // `SaveLoadSingletonSlot()` is this file's existing door on 0x1801FC6F0 -
    // the same one `SaveLoad_CtorLogic` publishes through (saveload.cpp:233).
    // Retail dereferences the slot unconditionally; the null check here is NOT
    // a behaviour guard but the slot-resolution contract RT_DATA_SLOT carries
    // (a name that has not been minted yet returns null rather than a slot),
    // and it is the same shape the three sibling accessors in
    // saveload_internal.h use.
    SaveLoad** slot = SaveLoadSingletonSlot();
    return slot ? *slot : nullptr;
}

// ?_singleton@SaveLoad@Save@@0PEAV12@EA (0x1801fc6f0) -- the class static (ENDGAME B.3.9 ENG, B39_RULES D7).
::Save::SaveLoad* ::Save::SaveLoad::_singleton = nullptr;
