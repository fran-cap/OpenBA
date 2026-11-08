// ============================================================================
// src/engine/save/saveload_meta.cpp — Save::SaveLoad::SaveMetaData (Wave 14)
// ============================================================================
//
//   ?SaveMetaData@SaveLoad@Save@@QEAAXPEAXHHVString@System@@H@Z
//   retail 0x1800c1490, ordinal 3611
//   void SaveMetaData(void* pixels, int width, int height,
//                     System::String name /*BY VALUE*/, int nameMode)
//
// WHAT IT IS. The writer half of the save-file metadata record: the small
// header block that the save/load dialog reads to draw a row (thumbnail, name,
// mod list) WITHOUT loading the multi-megabyte game state behind it. It
// serialises that record into a compressed in-memory stream and parks the
// result in `this->_embeddedState` (SaveLoad+0x28), from which
// ?UpdateSave@SaveLoad builds SaveBlock entry 0.
//
// Its reader is ?LoadMetaData@SaveLoad@Save@@ (0x1800c1b30, a parallel Wave 14
// lane). Its ONE caller is ?UpdateSave@SaveLoad@Save@@ (0x1800c2a80), ported by
// Wave 13 in saveload_update.cpp, which resolves it BY NAME — so registering
// the mangled name below is what flips that call site onto this body.
//
// ----------------------------------------------------------------------------
// THE RECORD FORMAT, which falls out of the body in full
// ----------------------------------------------------------------------------
//     String  name            the save's display name (the by-value argument)
//     int32   width           thumbnail width
//     int32   height          thumbnail height
//     bytes   width*height*4  the BGRA thumbnail pixels
//     int32   nameMode        SetSaveName's second argument, carried through
//     int32   modCount        the number of ENABLED mods
//     modCount x {
//         String  name        Mod+0x00, the display name
//         String  path        Mod+0x58, RELATIVE to the data directory
//         uint32  versionMajor  <- Mod+0x2c
//         uint32  gameVersion   <- Mod+0x18
//         uint32  versionMinor  <- Mod+0x1c
//     }
//
// all of it through an IO::Serializer at VERSION 3 (`MOV R8D,0x3` at
// 0x1800c1570) over an IO::CompressedMemoryStream with TWO windows.
//
// *** THE FIELD MAPPING IS CONFIRMED BY ITS OWN READER. *** Wave 5 pinned
// Save::SaveLoad::ModInfo from ?CheckMod@SaveLoad@Save@@ (0x1800c1090), which
// compares ModInfo+0x10 against Mod+0x2c, ModInfo+0x18 against Mod+0x1c, and
// range-checks ModInfo+0x14. This body is the WRITER of exactly those three
// fields and stores exactly Mod+0x2c, Mod+0x18, Mod+0x1c into them, in that
// order. Writer and reader agree field for field; neither was derived from the
// other. (src/engine/save/SaveLoad.h carries the layout and needs no change.)
//
// *** AND THE PATH FIELD IS THE INVERSE OF UpdateLoad'S. *** The per-mod `path`
// is NOT Mod+0x58 verbatim. Retail takes the length of IO::FileSystem's data
// directory and ADVANCES the mod's Filename pointer by it:
//
//     1800c166d  MOV  RCX,[RAX]           ; dataDir.data (a hidden-retslot return)
//     1800c1675  MOV  EAX,[RCX + -0x8]    ; its LENGTH, from the String header
//     1800c1681  MOVSXD RDX,EAX           ; SIGN-extended
//     1800c1684  ADD  RDX,[RBX + 0x58]    ; + Mod::file.data
//     1800c168c  CALL 0x1800c95d0         ; String::Set(&work, that suffix)
//
// i.e. it STRIPS the data-directory prefix. ?UpdateLoad@SaveLoad (Wave 13,
// saveload_update.cpp:797) does `GetDataDirectory() + modInfo.path` and matches
// the result against Mod+0x58 — putting the prefix back. The two are exact
// inverses, which is the strongest available confirmation that this reading of
// an otherwise anonymous pointer add is right.
//
// ----------------------------------------------------------------------------
// FOUR THINGS THE DECOMPILER GETS WRONG HERE
// ----------------------------------------------------------------------------
// Every call this body makes was counted register by register out of the
// disassembly, per reference_ghidra_c_output_vs_disassembly.md. Four disagree
// with Ghidra's C, and three of the four would have been silent defects:
//
// 1. THE ELEMENT DESTROYER'S FIRST ARGUMENT IS DEAD. Ghidra prints
//    `FUN_1800341e0(pplVar8, plVar9, (int)local_120)` and has to invent
//    `pplVar8` to do it, because RCX genuinely holds different things on the
//    two paths that reach the call (the ReaderWriter, or &array). The callee
//    (0x1800341e0) spills RCX to its home slot at 0x1800341e9 and NEVER READS
//    IT AGAIN — its real signature is (ignored, ModInfo* data, int count).
//    Transcribed here as a two-argument helper, which is what it is.
//
// 2. THREE OF THE FIVE `SerializeUInt` CALLS LOSE THEIR THIRD ARGUMENT.
//    Ghidra prints `(*(code *)(*local_130)[0x13])(local_130, plVar7 + 1)` for
//    two of the three per-mod field writes and a three-argument form for the
//    first. All three are identical in the bytes: RCX, RDX, and `XOR R8D,R8D`
//    at 0x1800c17e4 / 0x1800c17fc / 0x1800c1811. The third argument is the
//    debug NAME (nullptr in retail), which every IO::ReaderWriter slot takes.
//
// 3. `MOV EAX,[RCX + -0x8]` IS A SIGNED LENGTH THAT IS THEN `MOVSXD`-EXTENDED
//    before the pointer add (0x1800c1681, quoted above). Ghidra's C renders it
//    as `(longlong)iVar5`, which happens to be right — but the same pattern is
//    what reference_ghidra_c_output_vs_disassembly.md records being printed as
//    a ZERO extension elsewhere, so it was read from the bytes.
//
// 4. `?GetDataDirectory@FileSystem@IO@@QEBA?AVFilename@System@@XZ` RETURNS A
//    CLASS BY VALUE. It is a MEMBER function, so the hidden return slot is RDX
//    and there are no other arguments (`LEA RDX,[RSP+0x40]` at 0x1800c165b).
//    A typedef that omits the slot is a wild write that still returns the right
//    answer (reference_hidden_retslot_wild_write.md);
//    RtSaveDetail::GetDataDirectoryFn already spells it correctly and is reused
//    rather than re-declared.
//
// ----------------------------------------------------------------------------
// THE BY-VALUE `System::String` — WHO DESTROYS IT
// ----------------------------------------------------------------------------
// `VString@System@@` in the mangled name is by VALUE, and a class with a
// non-trivial destructor is passed by hidden reference in the MS x64 ABI. So
// argument 5 arrives as a `String*` in the 5th stack slot (`MOV R15,[RBP+0x80]`
// at 0x1800c1597, where RBP+0x80 is entry-RSP+0x28) and argument 6, the int, is
// at entry-RSP+0x30.
//
// THE CALLEE DESTROYS IT. The last thing this function does before returning is
// the standard String release on `*name` (0x1800c1ab7..0x1800c1b0b) — decrement,
// and on reaching zero rewind the pointer by 8 and free that. Wave 13's
// UpdateSave comment already states the caller's half of this contract: it
// retains the name and does NOT release it, precisely because the callee will.
// The two halves now exist in the tree and agree; releasing here is required,
// not optional, and NOT releasing would leak the save name on every save.
//
// ----------------------------------------------------------------------------
// WHY THIS BODY IS NOT WRITTEN AGAINST AN RtSink
// ----------------------------------------------------------------------------
// The five Wave 13 state machines next door take an RtSink (src/engine/
// rt_sink.h). This one does not: its entire observable output is a heap block
// produced by running a real compressor over a real stream, so there is no set
// of scalar stores for a sink to carry.
// ============================================================================

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "saveload_internal.h"
#include "io/Stream.h"
#include "io/Serializer.h"
#include "io/Modification.h"
#include "system/Compression.h"
#include <new>
#include <vector>   // EXT SEAM (NOT RETAIL, docs/extensions/handins/EXTSAVE.md): the pseudo-mods' record buffer (SaveMetaData)

using RtSave::ModInfo;
using RtSave::SaveLoad;
using RtSaveDetail::MemAlloc;
using RtSaveDetail::MemFree;

namespace {

// ---------------------------------------------------------------------------
// Collection::Array<Save::SaveLoad::ModInfo> — the local this body builds.
// ---------------------------------------------------------------------------
// The standard Wave 2 Collection triple. Spelled locally because the three
// helpers below are the ModInfo INSTANTIATION of Array's members and are
// unexported internals of the DLL; nothing else in the tree reaches them.
struct ModInfoArray {
    ModInfo* data;       // +0x00
    int32_t  size;       // +0x08
    int32_t  capacity;   // +0x0c
};
static_assert(sizeof(ModInfoArray) == 0x10, "Collection::Array header is 16 bytes");

// ---------------------------------------------------------------------------
// 0x1800341e0 — release every element's two Strings over [data, data+count).
// ---------------------------------------------------------------------------
// UNEXPORTED (checked against the shipped export table with pefile, not
// inferred from the `AEAA`-vs-`QEAA` spelling of anything —
// reference_aeaa_is_private_not_unexported.md). It is a template instantiation
// the linker folded in, so there is no name to register; ported as a file-local
// static rather than reached through an address door, which keeps this file
// free of by-address calls that would go null at cutover
// (reference_by_address_door_dies_at_cutover.md).
//
// ORDER MATTERS AND IS TRANSCRIBED: +0x08 (path) is released BEFORE +0x00
// (name) — 0x180034217 reads [RBX+8], 0x180034270 reads [RBX]. The refcounts
// are independent so the order is invisible today; it is written the way the
// bytes have it so that a future reader diffing against retail sees a match.
//
// The `if (count <= 0) return` is retail's own first instruction
// (`TEST R8D,R8D ; JLE`), not a defensive guard added here.
void ModInfoDestroyRange(ModInfo* data, int32_t count) {
    if (count <= 0) return;
    for (int32_t i = 0; i < count; ++i) {
        data[i].~ModInfo();   // path, then name: ModInfo's member destructors
    }
}

// ---------------------------------------------------------------------------
// 0x180033080 — Array<ModInfo>::SetSize(int newSize, const ModInfo& fill)
// ---------------------------------------------------------------------------
// Grow/shrink, copy-constructing any NEW element from `fill`. This is the
// push_back the mod loop uses: SetSize(size + 1, &tmp).
//
// THE CAPACITY RULE IS NOT `capacity = newSize`. When the request is exactly
// one more than the current size AND the array is not empty, retail jumps the
// capacity to newSize*3 (`LEA EAX,[RDI + RDI*0x2]` at 0x1800330ce) — the
// amortised-growth arm. A plain `capacity = newSize` would reallocate on every
// single push and still be functionally correct, which is exactly why it has to
// be transcribed rather than simplified: the difference is invisible in output
// and visible in every allocation counter.
//
// The `dst == nullptr` test inside the loop is retail's (`ADD RCX,R9` at
// 0x180033113 setting ZF, then `JZ` at 0x18003311b). It can only fire when the
// allocation failed and i == 0; it is retail's behaviour, not a null guard this
// port invented (CLAUDE.md).
//
// NOTE WHAT IS *NOT* COPIED: ModInfo+0x1c. The element copy writes +0x00, +0x08,
// +0x10, +0x14 and +0x18 and stops (0x18003314c..0x18003315b). ModInfo::pad1c
// therefore holds allocator garbage in every element of this array, in retail
// as here — and it is never serialised, which is why that is harmless.
void ModInfoArraySetSize(ModInfoArray* a, int32_t newSize, const ModInfo* fill) {
    const int32_t oldSize = a->size;
    if (newSize == oldSize) return;

    ModInfo* oldData = a->data;
    a->size = newSize;

    if (newSize > a->capacity) {
        a->capacity = newSize;
        if (newSize == oldSize + 1 && oldSize != 0) a->capacity = newSize * 3;
        a->data = static_cast<ModInfo*>(
            MemAlloc(static_cast<uint64_t>(static_cast<int64_t>(a->capacity)) * 0x20, 8));
    }

    // `CMOVZ ECX,ESI` at 0x1800330f2: when the buffer did NOT move, the
    // elements below oldSize are already in place and the copy starts at
    // oldSize; when it moved, everything is copied from index 0.
    const int32_t start = (oldData == a->data) ? oldSize : 0;
    for (int32_t i = start; i < newSize; ++i) {
        ModInfo* dst = a->data + i;
        if (dst == nullptr) continue;
        const ModInfo* src = (i < oldSize) ? (oldData + i) : fill;

        // The element copy IS ModInfo's copy constructor (SaveLoad.h): name
        // and path zeroed-then-adopted with an AddRef, the three dwords.
        new (dst) ModInfo(*src);
    }

    if (oldData != a->data) {
        ModInfoDestroyRange(oldData, oldSize);
        if (oldData != nullptr) MemFree(oldData);
    } else if (oldSize > newSize) {
        ModInfoDestroyRange(oldData + newSize, oldSize - newSize);
    }
}

// ---------------------------------------------------------------------------
// 0x180034010 — Array<ModInfo>::SetSize(int newSize)
// ---------------------------------------------------------------------------
// The same function with no `fill`: new elements get two NULL Strings and their
// three ints are left untouched (0x1800340ff/0x180034102 are the only stores on
// that arm).
//
// *** THIS IS PROVABLY DEAD ON THIS PATH AND IS PORTED ANYWAY. *** Its only
// call site is guarded by `serializer.stream->direction == 0`, i.e. READING,
// and this body hard-codes direction = 1 twenty instructions after constructing
// the stream. It is the read arm of a Serialize routine that retail compiles
// once and uses in both directions. Transcribed rather than dropped, because
// "the branch cannot be taken" is a claim about today's single caller, and a
// deleted branch is not recoverable by the next reader.
void ModInfoArraySetSize(ModInfoArray* a, int32_t newSize) {
    const int32_t oldSize = a->size;
    if (newSize == oldSize) return;

    ModInfo* oldData = a->data;
    a->size = newSize;

    if (newSize > a->capacity) {
        a->capacity = newSize;
        if (newSize == oldSize + 1 && oldSize != 0) a->capacity = newSize * 3;
        a->data = static_cast<ModInfo*>(
            MemAlloc(static_cast<uint64_t>(static_cast<int64_t>(a->capacity)) * 0x20, 8));
    }

    const int32_t start = (oldData == a->data) ? oldSize : 0;
    for (int32_t i = start; i < newSize; ++i) {
        ModInfo* dst = a->data + i;
        if (dst == nullptr) continue;
        if (i < oldSize) {
            new (dst) ModInfo(oldData[i]);   // the copy constructor, as above
        } else {
            new (dst) ModInfo();             // the two String zero stores only
        }
    }

    if (oldData != a->data) {
        ModInfoDestroyRange(oldData, oldSize);
        if (oldData != nullptr) MemFree(oldData);
    } else if (oldSize > newSize) {
        ModInfoDestroyRange(oldData + newSize, oldSize - newSize);
    }
}

// ---------------------------------------------------------------------------
// The DLL entry points this body calls, all by MANGLED NAME.
// ---------------------------------------------------------------------------
// Every one of these is a real export (verified against the shipped export
// table before it was written down — a name the DLL does not contain resolves
// NULL in silence, reference_resolveexport_phantom_names.md). There is not one
// address door in this file.
// ENDGAME B.3.1 step 2: the String::Serialize and String::Set door typedefs
// are gone with their call sites — both are class members now.

// B3X W0: the ReaderWriter (SerializeInt +0xa0, SerializeUInt +0x98,
// SerializeBytes +0xC8, DeletingDtor slot 0) and the embedded MemoryStream's
// Close (slot 7, +0x38) are called BY NAME below. B.4 R-IO: so is the
// IO::Encoding factory, inside `IO::Serializer`'s constructor; the
// `ReadVtableSlot` offset accessor is gone.

// ENDGAME B.3.9 IO (W2): the RT_VTABLE_ONCE by-name vtable stamps went with
// the inlined CompressedMemoryStream ctor/dtor -- the real class stores its own.

// ---------------------------------------------------------------------------
} // namespace

// ============================================================================
// Save::SaveLoad::SaveMetaData — the body.        retail 0x1800c1490
// ============================================================================
// ENDGAME B.3.7 B2: the body is the member. `name` is the by-value String
// (hidden pointer, callee-destroyed — RULE B7-5), spelled as the class.
void Save::SaveLoad::SaveMetaData(void* pixels, int width, int height,
                                  System::String* name, int nameMode) {
    // ---- inlined ??0CompressedMemoryStream + ?Create(memoryStream, 2) ------
    // Left DELIBERATELY uninitialised: retail's ctor does not touch
    // CompressedStream+0x18 or +0x24 (src/engine/io/Stream.h records both as
    // never-initialised), so a `= {}` here would be a behavioural difference,
    // not tidiness.
    // ENDGAME B.3.9 IO (W2, foreign site): the REAL class on the same raw
    // storage -- base ctor, vptr, member ctor, the same three steps.
    alignas(16) unsigned char csStorage[sizeof(IO::CompressedMemoryStream)];
    IO::CompressedMemoryStream* const csObj =
        ::new (static_cast<void*>(csStorage)) IO::CompressedMemoryStream();
    IO::CompressedMemoryStream& cs = *csObj;

    // ---- inlined ?Create(memoryStream, 2) ----------------------------------
    cs._direction = 1;                          // 1 == writing
    cs._source    = &cs._memoryStream;

    // `Allocate(8, 4)` under the allocator lock, then System::Compression's
    // ctor on it — which is exactly the inlined body of MemAlloc(8, 4).
    // Retail stores the CTOR'S RETURN (`this`), or zero when the allocation
    // failed; transcribed, including the failure arm.
    void* compression = MemAlloc(8, 4);
    cs._compression = (compression != nullptr)
        ? ::new (compression) System::Compression()      // ??0Compression 0x1800c41a0
        : nullptr;

    cs.SetWindowCount(2);                       // 0x1800aa020

    // ---- inlined ??0Serializer@IO@@(stream, 3) ----------------------------
    // The stream's own Encoding decides which ReaderWriter gets built; the
    // direction chooses the slot. Version 3 is a literal in the bytes.
    // B.4 R-IO: that inlined body IS `IO::Serializer`'s constructor
    // (0x1800b9960) -- the two stores, then the factory by direction -- so the
    // real class is built here; its destructor is the inlined ~Serializer at
    // the end of the scope below, at the same position as before.
    {
        ::IO::Serializer ser(cs, 3);
        // Retail reloads `[RSP+0x28]` and re-walks the vtable before EVERY one of
        // the calls below; a by-name virtual call does the same (B3X W0).
        ::IO::ReaderWriter* const rwc = ser._readerWriter;
        // ---- the record -------------------------------------------------------
        // ENDGAME B.3.1 step 2: `?Serialize@String@System@@QEAAXAEAVSerializer@IO@@@Z`
        // (0x1800c9930) is the class's own member now. `name` is the registered
        // entry point's POD parameter, so it comes through the one bridge.
        name->Serialize(ser);

        // Retail serialises the ARGUMENT HOME SLOTS: it spills R8D/R9D at entry
        // (0x1800c1493/0x1800c1497) and hands their addresses to the scalar slot,
        // which takes an `int&`. Our parameters occupy the same role. On the write
        // path the slot only reads them.
        // The slot takes an `int&` and writes back through it on the read path, so
        // the argument itself is handed over, exactly as retail hands it the home
        // slot.
        rwc->SerializeInt(width,  nullptr);                 // +0xa0
        rwc->SerializeInt(height, nullptr);                 // +0xa0

        rwc->SerializeBytes(width * height * 4, pixels, nullptr);   // +0xC8

        rwc->SerializeInt(nameMode, nullptr);               // +0xa0

        // ---- collect the enabled mods -----------------------------------------
        // The Modification singleton pointer is hoisted (`MOV R14,[...]` once at
        // 0x1800c1606) but `_mods` and `_count` are RE-READ every iteration
        // (0x1800c1630, 0x1800c1766). Transcribed as re-reads: String::Set and the
        // array grow both allocate, and hoisting would be a guess about what the
        // allocator cannot do.
        ::IO::Modification* mods = RtSaveDetail::ModificationSingleton();

        ModInfoArray arr;
        arr.data     = nullptr;
        arr.size     = 0;
        arr.capacity = 0;

        // ENDGAME B.3.1 step 2: the `?Set@String@System@@QEAAXPEBD@Z` door is gone
        // with its one call site — it is `work.Set(...)` below.

        for (int32_t i = 0; i < mods->_mods._size; ++i) {
            RtIO::Mod* entry = &mods->_mods._data[i];
            if (entry->enabled == 0) continue;

            // ModInfo's real default ctor IS retail's two stores here (name and
            // path zeroed, +0x10.. left as the stack held them), and its real
            // destructor at the end of this iteration IS retail's two releases —
            // path, then name — after the append (see the end of the loop).
            ModInfo tmp;

            // The display name, adopted straight off the Mod entry with an AddRef
            // (String's adopt, 0x180006cb0's second half, on the Mod entry's
            // name buffer).
            System::StringPrivateAccess::Adopt(tmp.name, entry->name._data);

            // The path, with the data-directory prefix stripped. See the banner.
            //
            // ENDGAME B.3.1 step 2: the two locals are RAII objects and the
            // explicit scope ends exactly where retail's two releases are, so the
            // destructors fire there — work then dataDir, retail's own order, the
            // reverse of the declarations — and not at the end of the iteration.
            {
                System::Filename dataDir(                            // 0x180007bd0
                    RtSaveDetail::FileSystemSingleton()->GetDataDirectory());

                // GetLength (0x1800063c0) IS the null-guarded stored-length read.
                const int32_t dirLen = dataDir.GetLength();

                // `?Set@String@System@@QEAAXPEBD@Z` (0x1800c95d0) as itself; the
                // default ctor is retail's `work.data = nullptr`.
                System::String work;
                work.Set(entry->file._data + static_cast<int64_t>(dirLen));

                // Adopt with an AddRef into the zeroed path: String::operator=
                // on a null destination.
                tmp.path = work;
            }

            tmp.versionMajor = entry->crc;   // -> CheckMod status 4
            tmp.gameVersion  = entry->version;   // -> CheckMod status 2 (< 0x1000400)
            tmp.versionMinor = entry->userVersion;   // -> CheckMod status 3
            // tmp.pad1c is deliberately NOT written; retail does not write it and
            // the array copy above does not copy it.

            ModInfoArraySetSize(&arr, arr.size + 1, &tmp);
            // <- ~ModInfo(tmp): ReleaseStringSlot(path), then (name).
        }

        // EXT SEAM (NOT RETAIL, docs/extensions/handins/EXTSAVE.md): SaveLoad.h `_extExtraMods` --
        // the active class B / C extensions as pseudo-mods,
        // appended after the real ones through the same grow. Null under the
        // faithful profile, and 0 records while no B / C extension is active:
        // the record below is then retail's byte for byte.
        if (_extExtraMods != nullptr) {
            const int extCount = _extExtraMods(nullptr, 0);
            if (extCount > 0) {
                std::vector<ExtraModRecord> recs(static_cast<size_t>(extCount));
                const int got = _extExtraMods(recs.data(), extCount);
                for (int i = 0; i < got && i < extCount; ++i) {
                    ModInfo tmp;
                    tmp.name.Set(recs[i].name);
                    tmp.path.Set(recs[i].path);
                    tmp.versionMajor = static_cast<uint32_t>(recs[i].versionMajor);         // +0x10
                    tmp.gameVersion  = recs[i].gameVersion;                                // +0x14
                    tmp.versionMinor = static_cast<uint32_t>(recs[i].formatVersion);   // +0x18
                    // tmp.pad1c not written, as retail's loop above.
                    ModInfoArraySetSize(&arr, arr.size + 1, &tmp);
                }
            }
        }

        // ---- the mod block ----------------------------------------------------
        // `count` is a SEPARATE local from arr.size (retail copies it into a stack
        // slot at 0x1800c1775 and serialises THAT). It matters: the element loop
        // below is bounded by `count`, the destroy at the end by `arr.size`, and on
        // the read path SetSize is what makes them agree. Collapsing them into one
        // variable would be right today and wrong the moment this runs backwards.
        int32_t count = arr.size;
        rwc->SerializeInt(count, nullptr);                  // +0xa0

        if (ser._stream->_direction == 0) {        // read path; dead here, see above
            ModInfoArraySetSize(&arr, count);
        }

        for (int32_t i = 0; i < count; ++i) {
            ModInfo* e = arr.data + i;
            e->name.Serialize(ser);
            e->path.Serialize(ser);
            rwc->SerializeUInt(e->versionMajor, nullptr);   // +0x98
            rwc->SerializeUInt(e->gameVersion,  nullptr);   // +0x98
            rwc->SerializeUInt(e->versionMinor, nullptr);   // +0x98
        }

        ModInfoDestroyRange(arr.data, arr.size);
        if (arr.data != nullptr) MemFree(arr.data);
        arr.data     = nullptr;
        arr.size     = 0;
        arr.capacity = 0;

        // ---- inlined ~Serializer ----------------------------------------------
        // `if (rw) delete rw;` (slot 0, flags 1) is ~Serializer, 0x1800b99b0, run
        // by the scope's end here -- before the stream's Close, as retail.
    }

    // ---- inlined ?Close@CompressedMemoryStream, then the handoff ----------
    // `_headers._size > 0` is "has anything been written yet"; the direction
    // test keeps a read-mode stream from committing.
    if (cs._headerSize > 0 && cs._direction == 1)
        cs.Commit();                                            // 0x1800a9ee0
    cs._memoryStream.Close();                   // slot 7 +0x38, the member's own vtable

    // THE ONE SIDE EFFECT THAT LEAVES THIS FUNCTION: the finished record is
    // copied into the EMBEDDED SavedState at SaveLoad+0x28, which is what
    // ?UpdateSave builds SaveBlock entry 0 out of. A port that computed the
    // right bytes and did not publish them here would be invisible until a save
    // was loaded back (reference_port_must_reproduce_side_effects.md).
    //
    // ENDGAME B.3.7 B2: `SavedState::Store` is a member of this block's class
    // now, called directly (it was an RT_EXPORT_ONCE door onto our own port).
    _embeddedState.Store(cs._memoryStream);

    // ---- ~CompressedMemoryStream (retail inlines it here) ------------------
    // ENDGAME B.3.9 IO (W2, foreign site): the class's own destructor chain at
    // the position the inlined one had, through a qualified `T::~T()`
    // call (retail: slot 0, flags 0) (no free of the stack object): the Close guard + Commit, the
    // member's virtual Close, ~MemoryStream (+ its encoding), Release and the
    // three array destructors (all no-ops over what Release zeroed -- the dead
    // frees retail ships), then the base encoding free. Same steps, same order.
    csObj->IO::CompressedMemoryStream::~CompressedMemoryStream();

    // ---- destroy the BY-VALUE String argument ------------------------------
    // See the banner. This is the callee's half of a contract whose caller half
    // is already written down in saveload_update.cpp's UpdateSave.
    name->System::String::~String();
}

// EXT SEAM (NOT RETAIL, docs/extensions/handins/EXTSAVE.md): SaveLoad.h's `_extExtraMods`, null by default, set by the game side at
// boot (Ext::InitFromSettings). Null = retail's mod list exactly.
Save::SaveLoad::ExtraModsProvider Save::SaveLoad::_extExtraMods = nullptr;
