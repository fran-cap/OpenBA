// ============================================================================
// src/engine/save/saveload_loadmeta.cpp — Save::SaveLoad::LoadMetaData
//                                          (Wave 14, lane LOADMETA)
// ============================================================================
//
// ONE FUNCTION:
//   ?LoadMetaData@SaveLoad@Save@@QEAAXAEAVString@System@@PEAPEAXAEAH22AEAV?$Array@UModInfo@SaveLoad@Save@@@Collection@@@Z
//   retail 0x1800c1b30, ordinal 3208 (name, ordinal and address all read out of
//   the shipped export directory with pefile before this file was written —
//   reference_resolveexport_phantom_names.md).
//
// It is the READ half of the save-file metadata record: given the compressed
// byte image the async loader has already dropped into `this->_embeddedState`,
// it decompresses it, runs one IO::Serializer over it and hands the caller SIX
// out-parameters — description, thumbnail pixels, width, height, the format
// version, and the mod list the save was made with. Its single caller is
// ?UpdateList@SaveLoad state 4 (src/engine/save/saveload_update.cpp, Wave 13),
// which copies five of the six into the SaveInfo row the save/load dialog draws.
//
// ----------------------------------------------------------------------------
// SEVEN ARGUMENTS, AND WHY THAT IS THE FIRST THING SAID HERE
// ----------------------------------------------------------------------------
// `this` plus six, so THREE arrive on the stack (MS x64 puts args 5..7 at
// [entry_RSP+0x28], +0x30, +0x38). Retail loads them as [RBP+0x6f], [RBP+0x77]
// and [RBP+0x7f] with RBP = entry_RSP - 0x47, which is how each was identified:
//
//   RCX          SaveLoad*                     this
//   RDX          System::String&               description   (out)
//   R8           void**                        pixels        (out)
//   R9           int&                          width         (out)
//   [RSP+0x28]   int&                          height        (out)
//   [RSP+0x30]   int&                          version       (out)   -> R15
//   [RSP+0x38]   Collection::Array<ModInfo>&   mods          (out)
//
// This matters twice over. First, the caller's own comment in
// saveload_update.cpp already pinned the stack triple from the CALL side
// (0x1800c22a0/0x22a9/0x22b2) and the two readings agree, which is the only
// cross-check available for arguments a decompiler renders as names. Second,
// **a register-based screen cannot see arguments 5, 6 and 7 at all** — this
// project scored a positive control 3/3 on the UNION of two screens and only
// later found that one of them caught 1/3, precisely because it was blind to
// every 5th-or-later argument (reference_union_control_hides_a_blind_screen).
// The oracle at the bottom of this file therefore compares OUT-PARAMETER VALUES,
// not registers, and gives height, version and the mod array a screen each.
//
// ----------------------------------------------------------------------------
// GHIDRA'S C IS WRONG ABOUT THIS BODY IN TWO PLACES. THE DISASSEMBLY WINS.
// ----------------------------------------------------------------------------
// Both are the documented failure modes of reference_ghidra_c_output_vs_disassembly:
//
//  1. IT DROPS ARGUMENTS AT EVERY VTABLE CALL. The decompiler renders the
//     encoding factory call as `(**(code **)(*local_b8 + 0x18))()` — no
//     arguments at all — and the mod-element loop as
//     `(**(code **)(*local_d0 + 0x98))(local_d0, lVar5 + 4)`, dropping the
//     third. The bytes say three arguments at every one of them
//     (`XOR R8D,R8D` / `XOR R9D,R9D` immediately before each CALL). Every
//     indirect call below was counted off the disassembly.
//
//  2. IT DROPS A DEREFERENCE ON THE ONE BRANCH THAT DECIDES A WRITE.
//     `if (*(int *)(local_d8 + 1) == 0)` reads, in C, as "the serializer's
//     second qword". The bytes are `MOV RAX,[RSP+0x20]` (LOAD the serializer's
//     _stream) then `CMP dword ptr [RAX+0x8],0x0` — i.e. `ser._stream->direction`,
//     one level deeper. That branch is what gates Array<ModInfo>::SetSize, so
//     taking the decompiler's word for it would have tested the WRONG WORD and
//     either skipped the resize or run it on the writing path.
//
// ----------------------------------------------------------------------------
// WHAT THE BODY IS, STEP BY STEP (retail addresses in the code below)
// ----------------------------------------------------------------------------
// Every step maps onto an export this tree has ALREADY ported, because retail
// inlines the two-line wrappers rather than the work:
//
//   IO::CompressedMemoryStream cms;              // ??0CompressedMemoryStream
//   cms.GetMemoryStream().Open(_embeddedState._size);
//   memmove(cms._memoryStream._data, _embeddedState._data, _embeddedState._size);
//   cms.Open();                                  // == CompressedStream::Open(*this,_mem,1)
//   {   IO::Serializer ser(cms, 0);              // ctor INLINED, see below
//       version     = ser.GetVersion();
//       description.Serialize(ser);
//       ser << width; ser << height;
//       pixels = MemAlloc(width*height*4, 4);
//       ser.SerializeBytes(width*height*4, pixels);
//       if (version > 2) { ser << version; <the mod list>; }
//   }                                            // ~Serializer INLINED
//   cms.Close();                                 // ?Close@CompressedMemoryStream
//   <free _embeddedState>
//   cms.~CompressedMemoryStream();               // ??1CompressedMemoryStream
//
// The three wrapper-level calls (`Open`, `Close`, the ctor/dtor) are made
// through the CompressedMemoryStream exports rather than re-inlined here.
// That is not a shortcut: ?Open@CompressedMemoryStream (0x1800a9500) IS
// `CompressedStream::Open(*this, _memoryStream, 1)` and nothing else, and
// ?Close@ / ??1 are transcribed in src/engine/io/compressedstream.cpp from
// the same bytes retail inlined here — so the call reaches OUR transcription of
// the identical instructions, and it does not grow the cutover surface with a
// by-address door (reference_porting_can_grow_the_cutover_surface).
//
// THE SERIALIZER CTOR AND DTOR *ARE* RE-INLINED, deliberately. Constructing an
// IO::Serializer consumes a 12-byte header off the stream (see the big note in
// src/engine/io/Serializer.h), so it is not a wrapper — it is work, and the
// exact factory slot it picks (+0x18 reading, +0x20 writing) is chosen from
// `cms.direction`, a field this body must be seen to read. Both are written out
// instruction for instruction.
//
// ----------------------------------------------------------------------------
// FOUR THINGS A READER WOULD OTHERWISE "FIX"
// ----------------------------------------------------------------------------
//
// 1. `version` IS WRITTEN TWICE, AND THE SECOND WRITE IS NOT A BUG. The
//    out-param first receives ReaderWriter::GetVersion() — the format version
//    the BinaryReader read out of the stream header — and then, when that value
//    is > 2, the same reference is handed to Serialize(int&) and receives a
//    SECOND number read from the record body. Retail uses one variable for both
//    (R15 is the arg-6 pointer throughout) and so does this port. Splitting them
//    would change what the caller stores in SaveInfo+0x44.
//
// 2. THE MOD COUNT IS SEEDED FROM THE ARRAY, THEN OVERWRITTEN. `count` is
//    initialised from `mods._size` and immediately serialized — which on the
//    READ path replaces it with the stored count. That looks like a dead store
//    and is not: this is a shared read/write body, and on the writing path the
//    seed is the value that goes into the file.
//
// 3. THE ELEMENT BASE POINTER IS LOADED ONCE, BEFORE THE LOOP. Retail does
//    `MOV RBX,[RBX]` at 0x1800c1ced and then walks with `ADD RBX,0x20`; it never
//    re-reads `mods._data`. Re-reading it per iteration would be the "safer"
//    spelling and would be a different function.
//
// 4. THE THREE ModInfo VERSION FIELDS GO THROUGH THE **UNSIGNED** SLOT.
//    +0x10, +0x14 and +0x18 are serialized with ReaderWriter vtable +0x98
//    (kScalarUInt), not +0xa0 (kScalarInt). src/engine/save/SaveLoad.h names
//    two of them `int32_t` after CheckMod's SIGNED comparisons; both readings
//    are right about their own instruction, and the wire format is the unsigned
//    one. Nothing is reinterpreted here — the slot is chosen off the bytes.
//
// ----------------------------------------------------------------------------
// *** A WAVE 13 OPEN FINDING, CLOSED BY THIS LANE ***
// ----------------------------------------------------------------------------
// saveload_update.cpp's UpdateList carries an explicitly-open note: every
// predicting run reports `UNPREDICTED WRITE Save::SaveLoad::UpdateList 9 byte(s)
// ... first at Save::SaveLoad [state 4 meta] +44`, i.e. nine bytes at
// SaveLoad+0x2c — the embedded SavedState's `_size` and `_data` — change during
// a state-4 round and nothing predicts or excludes them. That note names
// LoadMetaData as having been checked and cleared: "only READS +0x2c/+0x30 ...
// and its tail stores to neither."
//
// **Its tail stores to both.** 0x1800c1d98..0x1800c1db5:
//     MOV RCX,[R14+0x30] ; TEST RCX,RCX ; JZ ; CALL 0x1800c7ad0   ; MemFree
//     MOV qword ptr [R14+0x30],0x0
//     MOV dword ptr [R14+0x2c],0x0
// LoadMetaData FREES the compressed image it just consumed and zeroes the
// SavedState in place. That is the writer, it is inside the suppressed callee
// on exactly the state-4 arm the finding names, and the byte count fits (the
// _size dword plus the low bytes of the _data pointer). The stale claim is
// corrected at its source in saveload_update.cpp by this commit, per PLAN §6
// step 8 — a comment that was believed for a wave is worse than none.
//
// ============================================================================

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>

#include "save/SaveLoad.h"
#include "io/Stream.h"
#include "io/Serializer.h"
#include "collection/Collection.h"
#include "../collection/array_internal.h"   // rtcoll::AsArray, ArraySpec — B.3.2 step 2
#include "system/string_internal.h"
#include "saveload_internal.h"

// ENDGAME B.3.2 step 2 — `kSpec_MetaEntry20` (src/engine/collection/array.cpp)
// has external linkage (namespace rtcoll there, non-static) but is declared
// `extern` in no header this batch may edit, so it is declared here, at FILE
// SCOPE and not inside the anonymous namespace below (which would give a
// re-declaration internal linkage and sever it from the real definition). See
// the banner on its call site for why this row and not the trait-derived one.
namespace rtcoll { extern const ArraySpec kSpec_MetaEntry20; }

namespace {

using RtSaveDetail::MemAlloc;
using RtSaveDetail::MemFree;

using RtSave::SaveLoad;
using RtSave::ModInfo;
using RtIo::CompressedMemoryStream;
using RtIo::MemoryStream;

using rt::io::Encoding;
using rt::io::ReaderWriter;
using rt::io::Serializer;

using ModArray = Collection::Array<ModInfo>;

// ENDGAME B.3.2 step 2: `0x180034010` names `kSpec_MetaEntry20`, the
// HAND-DERIVED row Collection.h's Array banner calls out by this exact
// address: String-adopting assign, String-zeroing init, String-releasing
// destroy, `byteCopy == false`. It is NOT what `ModArray`'s own
// (trait-derived) `SetSize` would run: `ModInfo` (src/engine/save/
// SaveLoad.h) stores its two Strings as raw `const char* name/path`, not a
// real `System::String` member, so `is_trivially_copyable<ModInfo>` reads
// true and the trait-derived spec would silently drop retail's refcount
// bump/release. Reached through the hand-derived row instead, per
// B32_RULES.md — never substitute the trait default for a row this shape.
// (`kSpec_MetaEntry20` itself is declared at file scope above — see there.)

// ---- callee shapes ---------------------------------------------------------
//
// EVERY ONE of these was counted off the disassembly, not off Ghidra's C, and
// none of them returns a class by value — so not one has a hidden return slot
// (tools/abi_retslot_audit.py agrees; a typedef that omits one is a wild write
// that still returns the right answer, reference_hidden_retslot_wild_write).
// ENDGAME B.3.1 step 2: the String::Serialize door typedef is gone with its
// three call sites — all three are `System::String::Serialize` now.
// ENDGAME B.3.2 step 2: the `SetSizeFn` door typedef is gone with its one call
// site — see `rtcoll::AsArray<&rtcoll::kSpec_MetaEntry20>(mods)->SetSize(...)`
// below.

// B.4 R-IO: the IO::Encoding factory (+0x18 / +0x20, arity 3 -- retail leaves
// R8D zeroed by `XOR R8D,R8D` at 0x1800c1bd1) is reached through
// `IO::Serializer`'s own constructor, and IO::ReaderWriter is called BY NAME
// (B3X W0) -- GetVersion +0x08, SerializeUInt +0x98, SerializeInt +0xa0,
// SerializeBytes +0xc8, DeletingDtor slot 0. The `CreateCodecFn` / `VSlot`
// slot readers and the `Rw` POD bridge are gone.

using LoadMetaDataFn = void (*)(SaveLoad*, const char**, void**,
                                int*, int*, int*, ModArray*);

// ENDGAME B.3.7 B2: `DestroyScratchModArray`, the by-address door onto
// FUN_1800331f0 (Array<ModInfo>::~Array) that only the retired oracle used, is
// gone with its last caller; `RtSaveDetail::ArrayModInfoDtor` (saveload.cpp) is
// that body, called directly where it is needed.

// (B.4 R-IO: `StringContentEqual`, the retired oracle's content compare, had
// no caller and is deleted.)

}  // namespace

// ============================================================================
// Save::SaveLoad::LoadMetaData   retail 0x1800c1b30   ordinal 3208
// ============================================================================
// ENDGAME B.3.7 B2: the body is the member; the out-parameters are the
// references retail's mangling spells (`AEAVString`, `AEAH` x3, `AEAV?$Array`).
void Save::SaveLoad::LoadMetaData(System::String& description, void** pixels,
                                  int& widthRef, int& heightRef, int& versionRef,
                                  ModArray& modsRef) {
    int* const width   = &widthRef;
    int* const height  = &heightRef;
    int* const version = &versionRef;
    ModArray* const mods = &modsRef;


    // ---- 0x1800c1b6c : IO::CompressedMemoryStream cms; ---------------------
    // 0x98 bytes of uninitialised stack, exactly as retail: the ctor writes the
    // fields it writes and leaves the documented gaps alone. Zeroing them first
    // would be a normalisation of retail's bytes.
    alignas(16) unsigned char cmsStorage[sizeof(CompressedMemoryStream)];
    // ENDGAME B.3.9 IO (W2, foreign site): the REAL class on that storage.
    IO::CompressedMemoryStream* const cmsObj =
        ::new (static_cast<void*>(cmsStorage)) IO::CompressedMemoryStream();

    // ---- 0x1800c1b90 / 0x1800c1ba1 : fill the embedded MemoryStream --------
    // Open allocates the buffer and sets _size/_ownsData; the copy that follows
    // reads `_embeddedState._size` a SECOND time from memory (`MOV R8D,[R14+0x2c]`
    // at 0x1800c1b95, after the Open call), and is transcribed with the same
    // second read. 0x18008a290 is memmove, not memcpy (src/engine/system/error.cpp).
    cmsObj->_memoryStream.Open(static_cast<int>(_embeddedState._size));   // ENDGAME B.3.9 IO: the member
    // NOT RETAIL (review S-032): the copy is skipped when there is nothing to copy. memmove
    // with a null source is undefined behaviour even for 0 bytes, and UpdateList state 3 can
    // skip SetupLoad / BeginLoad while state 4 still calls here, leaving _embeddedState
    // {nullptr, 0} (the stores at the end of the previous call). Retail's second read of
    // _size is kept.
    if (_embeddedState._size != 0) {
        std::memmove(cmsObj->_memoryStream._data,
                     _embeddedState._data,
                     _embeddedState._size);
    }

    // ---- 0x1800c1bb5 : CompressedStream::Open(cms, cms._memoryStream, 1) ---
    // Retail calls the base directly with the embedded stream and a window count
    // of 1; ?Open@CompressedMemoryStream is that call and nothing else. Its
    // bool result is DISCARDED here, exactly as retail discards it — a save whose
    // window table does not check out produces garbage metadata rather than an
    // error, and that is the shipped behaviour.
    cmsObj->Open();

    // ---- 0x1800c1bba..0x1800c1bea : IO::Serializer ser(cms, 0), INLINED ----
    // Field for field with ??0Serializer@IO@@ (0x1800b9960): store the stream,
    // null the codec, then ask the stream's Encoding for a reader or a writer
    // depending on the stream's own direction. `version` here is the literal 0
    // retail passes (`XOR R8D,R8D`), not the out-parameter of the same name.
    // B.4 R-IO: that inlined body IS the class's constructor, so the real
    // `IO::Serializer` is built; the scope's end is the inlined ~Serializer
    // at 0x1800c1d63, before cms.Close(), as before.
    {
        ::IO::Serializer ser(*cmsObj, 0);
        ::IO::ReaderWriter* const rw = ser._readerWriter;

        // ---- 0x1800c1bf7 : out-param 6 = the format version --------------------
        *version = rw->GetVersion();

        // ---- 0x1800c1c09 : out-param 1 = the description String ----------------
        // ENDGAME B.3.1 step 2: `?Serialize@String@System@@QEAAXAEAVSerializer@IO@@@Z`
        // (0x1800c9930) is the class's own member now. The out-param is modelled as
        // the String's single field (`const char**`) because that is the shape the
        // registered entry point's callers spell, so it comes through a
        // transitional cast (B31_RULES, "not-yet-converted").
        description.Serialize(ser);

        // ---- 0x1800c1c1c / 0x1800c1c34 : out-params 3 and 4 --------------------
        // Both through the SIGNED scalar slot (+0xa0) with a null field name.
        rw->SerializeInt(*width,  nullptr);
        rw->SerializeInt(*height, nullptr);

        // ---- 0x1800c1c3a..0x1800c1c93 : out-param 2 = the thumbnail ------------
        // `MOV ESI,[RDI] ; IMUL ESI,[RBX] ; SHL ESI,0x2` — a 32-bit product that is
        // then SIGN-extended by `MOVSXD RDX,ESI` for the allocator. Transcribed with
        // the 32-bit arithmetic and the sign extension intact rather than promoted to
        // 64-bit, because promoting it changes the size request on any w*h that
        // overflows. The locked Allocate retail inlines around
        // MemoryAllocator::_singleton+0x640 is byte-identical to the exported
        // MemAlloc (established by Wave 5 on SavedState::Store, same call shape).
        //
        // NOT RETAIL (review F-061): width and height come straight from the file,
        // and a product that leaves int32 is signed overflow (UB) here; retail's
        // IMUL wraps and sign-extends the result into the allocation. A negative
        // dimension or a byte count past INT32_MAX is a corrupt record: the
        // thumbnail is dropped (0 x 0, no pixel bytes read). Every product that
        // fits, i.e. every valid save, takes the retail arithmetic unchanged.
        if (*width < 0 || *height < 0 || static_cast<int64_t>(*width) * (*height) > INT32_MAX / 4) {
            *width  = 0;
            *height = 0;
        }
        const int32_t byteCount = (*width) * (*height) * 4;
        *pixels = MemAlloc(static_cast<uint64_t>(static_cast<int64_t>(byteCount)), 4);
        rw->SerializeBytes(byteCount, *pixels, nullptr);

        // ---- 0x1800c1c9a : the mod list, format version 3 and up ---------------
        if (*version > 2) {
            // The SECOND write to the version out-param. See note 1 in the header.
            rw->SerializeInt(*version, nullptr);

            // Seeded from the array (the writing path's value), then replaced by the
            // stored count on the reading path. See note 2.
            int count = mods->_size;
            rw->SerializeInt(count, nullptr);

            // `MOV RAX,[RSP+0x20] ; CMP dword ptr [RAX+0x8],0x0` — the stream the
            // SERIALIZER holds, dereferenced, then its direction. This is the
            // dereference Ghidra's C drops; see the header.
            // NOT RETAIL (review F-035): retail hands the stored count to SetSize
            // unbounded. A negative count, or one past kMaxSavedMods, is a corrupt
            // record that asks for count * 0x20 bytes (a failed allocation leaves
            // `_data` null under the init loop, a smaller one hangs on 2^28 entry
            // reads): it is read as no mods. Reading path only; the writing path
            // writes the array's own size.
            constexpr int kMaxSavedMods = 0x10000;
            if (ser._stream->_direction == 0 && (count < 0 || count > kMaxSavedMods))
                count = 0;

            if (ser._stream->_direction == 0)
                // retail 0x180034010 — Array<Save::MetaDataEntry>::SetSize(n), the
                // hand-derived kSpec_MetaEntry20 row (see the banner above `mods`
                // is a real `Collection::Array<ModInfo>*`, but its own trait-derived
                // SetSize is the wrong instantiation for this address).
                rtcoll::AsArray<&rtcoll::kSpec_MetaEntry20>(mods)->SetSize(count);

            // 0x1800c1ced: the base pointer is read ONCE. See note 3.
            ModInfo* e = mods->_data;
            for (int i = 0; i < count; ++i, ++e) {
                e->name.Serialize(ser);
                e->path.Serialize(ser);
                // +0x98, the UNSIGNED slot, for all three. See note 4.
                rw->SerializeUInt(e->versionMajor, nullptr);
                rw->SerializeUInt(e->gameVersion, nullptr);
                rw->SerializeUInt(e->versionMinor, nullptr);
            }
        }

        // ---- 0x1800c1d63 : ~Serializer, inlined --------------------------------
        // The scalar-DELETING destructor with flags = 1, i.e. the codec is freed:
        // `if (rw) delete rw;`, ~Serializer 0x1800b99b0, at the scope's end.
    }

    // ---- 0x1800c1d77 : cms.Close() -----------------------------------------
    // Retail inlines CompressedMemoryStream::Close (guard + Commit + the
    // embedded stream's virtual Close); src/engine/io/compressedstream.cpp
    // holds that transcription and this reaches it.
    cmsObj->IO::CompressedMemoryStream::Close();

    // ---- 0x1800c1d98 : release the consumed image --------------------------
    // THE WRITER Wave 13's UpdateList note went looking for. See the header.
    //
    // *** THE MemFree IS GUARDED; BOTH ZERO STORES ARE NOT. *** The JZ at
    // 0x1800c1d9c jumps to 0x1800c1da6, which is the FIRST zero store — so the
    // guard covers the free alone and both stores execute on every path. The
    // natural C++ for this,
    //     if (_data) { MemFree(_data); _data = 0; _size = 0; }
    // is WRONG, and wrong in a way that survives testing: whenever `_data` is
    // already null the two spellings leave an identical end state, so a byte pin
    // agrees. They diverge only when `_data` is null and `_size` is NOT — which
    // is exactly what a round that skipped BeginLoad leaves behind. Written out
    // in retail's shape, and it stays that shape.
    if (_embeddedState._data != nullptr) MemFree(_embeddedState._data);
    _embeddedState._data = nullptr;
    _embeddedState._size = 0;

    // ---- 0x1800c1db6 : ~CompressedMemoryStream, inlined by retail ----------
    cmsObj->IO::CompressedMemoryStream::~CompressedMemoryStream();   // flags 0: no free of the stack object

}
