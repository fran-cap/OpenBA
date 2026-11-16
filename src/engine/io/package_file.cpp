// ============================================================================
// src/engine/io/package_file.cpp — Wave 9 lane PKG2
// IO::PackageFile — the cutover wall of attempt 14
// ============================================================================
//
// ATTEMPT 14 DIED HERE. `RtIo_FileSystem_LoadPackages +0x184`, EXEC of address
// 0, on a heap block whose first 0x38 bytes were all zero, immediately after
// `[RtResolve] MISS "??0PackageFile@IO@@QEAA@XZ" -> NULL`. The crash handler
// labelled it VCALL-of-0; it is the CONSTRUCTOR CALL itself (RAX=0, RCX=RDX=the
// fresh 0xF0 block, which is why the object reads as all-zero — nothing had run
// on it yet). Lane PKG allocated the block, resolved the constructor to
// nothing, and called it.
//
// This file holds the class's slot bodies, factory pair and leaf accessors. The
// constructor is `IO::PackageFile`'s own (PackageFile.h); `?ReadPackfile@` is in
// package_file_read.cpp.
//
//   | Export                                        | Retail      | Ord  | Size |
//   |-----------------------------------------------|-------------|------|------|
//   | ??0PackageFile@IO@@QEAA@XZ                    | 0x1800b4440 |  289 | 48 i |
//   | ?Construct@PackageFile@IO@@UEAAPEAV…XZ        | 0x1800252f0 | 1834 |  4 i |
//   | ?Type@PackageFile@IO@@UEBAIXZ                 | 0x180025130 | 4210 |  2 i |
//   | ?TypeId@PackageFile@IO@@SAIXZ                 | 0x180025140 | 4314 |  2 i |
//   | ?TypeName@PackageFile@IO@@UEBAPEBDXZ          | 0x180025150 | 4417 |  2 i |
//   | ?GetName@PackageFile@IO@@QEBA?AV…XZ           | 0x180025310 | 2806 | 13 i |
//   | ?GetAuthor@PackageFile@IO@@QEBA?AV…XZ         | 0x180025350 | 2456 | 13 i |
//   | ?GetDescription@PackageFile@IO@@QEBA?AV…XZ    | 0x180025390 | 2566 | 13 i |
//   | ?GetVersion@PackageFile@IO@@QEBAHXZ           | 0x1800253d0 | 3010 |  2 i |
//   | ?GetUserVersion@PackageFile@IO@@QEBAHXZ       | 0x1800253e0 | 3003 |  2 i |
//   | ?GetPlatform@PackageFile@IO@@QEBAHXZ          | 0x1800253f0 | 2848 |  2 i |
//   | ?GetFileCount@PackageFile@IO@@QEBAHXZ         | 0x180025440 | 2600 |  2 i |
//   | ?GetFileSize@PackageFile@IO@@QEBAIH@Z         | 0x1800254a0 | 2605 |  5 i |
//   | ?GetFileOffset@PackageFile@IO@@QEBAIH@Z       | 0x1800254c0 | 2603 |  5 i |
//   | ?GetPriority@PackageFile@IO@@QEBAHXZ          | 0x1800254e0 | 2856 |  2 i |
//   | ?CanOverride@PackageFile@IO@@QEBA_NXZ         | 0x1800254f0 | 1683 |  2 i |
//   | ?GetCRC@PackageFile@IO@@QEBAIXZ               | 0x180025500 | 2473 |  2 i |
//   | ?GetIconWidth@PackageFile@IO@@QEBAHXZ         | 0x180025510 | 2699 |  2 i |
//   | ?GetIconHeight@PackageFile@IO@@QEBAHXZ        | 0x180025520 | 2698 |  2 i |
//   | ?GetFileVersion@PackageFile@IO@@UEBAHXZ       | 0x1800b6080 | 2642 |  2 i |
//   | ?Compile@PackageFile@IO@@UEAA?AW4…@Z          | 0x1800b6090 | 1744 |  2 i |
//
// EVERY NAME AND EVERY ORDINAL IN THAT TABLE WAS PRINTED OUT OF
// Runtime-x64.dll's OWN EXPORT DIRECTORY with `pefile`, not transcribed from a
// decompile and not typed from memory. Lane PKG's header records that its own
// first draft carried eight hand-reconstructed ordinals and SEVEN WERE WRONG;
// an ordinal is exactly the kind of detail that reads as evidence and is never
// checked by the compiler.
//
// NO NEW WIN32 API AND NO NEW IMPORT LIBRARY.
//
// ---------------------------------------------------------------------------
// 1. THE CONSTRUCTOR, INSTRUCTION BY INSTRUCTION
// ---------------------------------------------------------------------------
// 0x1800b4440, 48 instructions, and it CALLS NOTHING. It is a straight run of
// stores into `this` plus `MOV RAX,RDX` (the saved `this`) on the way out.
// `IO::PackageFile`'s constructor (PackageFile.h) makes them in retail's order;
// the vptrs are the compiler's.
//
//   1800b4454  LEA RAX,[0x180154788] / MOV [RCX],RAX   ??_7Base@Object@@6B@
//   1800b445e  LEA RAX,[0x1801547a0] / MOV [RCX],RAX   ??_7External@Object@@6B@
//   1800b4468  LEA RAX,[RCX+8] ; MOV [RAX],R8          +0x08 = 0
//   1800b4477  MOV [RCX+0x10],R8                       +0x10 = 0
//   1800b447b  MOV [RCX+0x18],R8                       +0x18 = 0
//   1800b447f  LEA RAX,[0x180155700] / MOV [RCX],RAX   ??_7PackageFile@IO@@6B@
//   1800b4489  +0x20 .. +0x68 = 0                      ten qword stores
//   1800b44b1  MOV qword [RCX+0x70],0x2710             priority=10000, platform=0
//   1800b44b9  +0x80, +0x88 = 0
//   1800b44c7  +0x98, +0xa0 = 0
//   1800b44d5  MOV dword [RCX+0xa8],R8D                the CRC / packfile id
//   1800b44dc  MOV byte  [RCX+0xac],1                  overridable = TRUE
//   1800b44e3  +0xb0, +0xb8 = 0                        the file table
//   1800b44f1  ADD RCX,0xc0 — the embedded FileSystem::FileInfo:
//               +0xc0 dword = 0xFFFFFFFF, +0xc8 = 0, +0xd0 dword = 0,
//               +0xe0 = 0, +0xe8 = 0, and then +0xd8 = -1 (out of order,
//               transcribed in retail's order below).
//
// THE THREE VTABLE STORES. Two of them are dead by inspection: nothing between
// 0x1800b445b and 0x1800b4486 reads [this+0], and there is no call in the
// window, so only ??_7PackageFile survives (MSVC's inlined base constructors).
// The port's vptrs are the compiler's.
//
// FIELDS THE CONSTRUCTOR DOES NOT WRITE. +0x78/+0x7C (the icon dimensions,
// which ?GetIconWidth@/?GetIconHeight@ read), +0x90, +0xAD..+0xAF, +0xC4,
// +0xD4. The store list jumps 0x70 -> 0x80 and 0x88 -> 0x98; that is retail
// leaving them to ReadPackfile, not a gap in this transcription.
//
// ---------------------------------------------------------------------------
// 2. WHERE THE REST OF LoadPackages' INNER LOOP LIVES
// ---------------------------------------------------------------------------
// LoadPackages' inner loop is `MemAlloc(0xF0) -> ??0PackageFile -> ReadPackfile
// -> append`. The constructor is PackageFile.h's, and
// `?ReadPackfile@PackageFile@IO@@QEAA_NAEBVFilename@System@@@Z` (0x1800b46b0,
// ord 3461, 6,597 BYTES) — the .pkg format parser — is in
// src/engine/io/package_file_read.cpp.
//
// ---------------------------------------------------------------------------
// 3. THE ACCESSORS, AND WHY THEY ARE IN THIS FILE AT ALL
// ---------------------------------------------------------------------------
// Twenty of the twenty-one rows are leaves — `MOV EAX,[RCX+off] ; RET`, or the
// String AddRef idiom, or a constant. They are `IO::PackageFile` members
// (PackageFile.h).
//
// ---------------------------------------------------------------------------
// 4. WHERE THE REST OF THE CLASS IS
// ---------------------------------------------------------------------------
//   ?ReadPackfile@PackageFile@IO@@…        0x1800b46b0  ord 3461  6,597 bytes
//       The .pkg parser: package_file_read.cpp.
//   ??1PackageFile@IO@@UEAA@XZ             0x1800b4540  ord  639
//       The destructor: package_file_dtor.cpp.
//   FUN_1800259d0 (the DELETING destructor, vtable slot 0)
//       The compiler's deleting destructor over ~PackageFile.
//   ?Reconstruct@ 0x1800252d0 and ?Destruct@ 0x180025300
//       Both drive vtable slot 0; their bodies are below in this file.
//   ?Serialize@PackageFile@IO@@UEAA_NAEAVStream@2@@Z   0x1800b60a0
//       package_file_serialize.cpp.
//   ?GetFileInfo@ 0x180025400 — filesystem_packages.cpp.
//   ?GetIconData@ 0x180025530, ?GetPreviewData@ 0x180025580 —
//       modification_refresh.cpp.
//   ??0PackageFile@IO@@QEAA@AEBV01@@Z (copy ctor, 0x1800255d0),
//   ??4PackageFile@IO@@ (operator=, 0x180025790),
//   ?WritePackfile@ / ?ExtractPackFile@ / ?BuildPackageFile@ /
//   ?AreResourcesEqual@ / ?BuildFileSet@ / ?ClosePackFile@
//       The TOOLKIT half of the class — building and writing packages. Nothing
//       in the game's boot or run path reaches any of them; they are the
//       resource compiler's side, and none is ported.
//
// ---------------------------------------------------------------------------
// 7. THE VTABLE — ??_7PackageFile@IO@@6B@
// ---------------------------------------------------------------------------
// 0x180155700, ord 1471, EXPORTED. Nine slots, counted by READING THE BYTES at
// 0x180155700 rather than trusting the mangled name: nine consecutive qwords
// are .text addresses (0x1800259d0, 0x180025150, 0x180025130, 0x1800b6080,
// 0x1800b6090, 0x1800b60a0, 0x1800252d0, 0x180025300, 0x1800252f0) and the
// tenth is zero, followed by the ASCII "%08lX%04hX%04hX…" of the next .rdata
// object. Slots 0..8 are the deleting destructor, TypeName, Type,
// GetFileVersion, Compile, Serialize, Reconstruct, Destruct and Construct, all
// members of `IO::PackageFile`, and the object wears the compiler's table.
//
// ---------------------------------------------------------------------------
// 8. THE +0x70 CORRECTION
// ---------------------------------------------------------------------------
// src/engine/io/FileSystem.h modelled +0x70 as a `uint64_t priority`,
// because the constructor's store is one qword. It is TWO dwords: `?GetPriority@`
// reads the low half and `?GetPlatform@` reads `[RCX+0x74]`. The header now
// says so. The single qword store seeds priority=0x2710 AND platform=0, which
// is why the qword reading was never wrong about the BYTES — only about what
// they mean. Nothing in the tree read the field as a qword (grepped before the
// change), and no offset or static_assert moved.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <new>          // placement new into the retail hidden return slot
#include <atomic>

#include "io/FileSystem.h"
#include "io/PackageFile.h"   // B.3.4 PACKAGE+FS: the real class
#include "system/String.h"
#include "system/string_internal.h"

namespace RtIO {


namespace {

// ---- sizes ----------------------------------------------------------------
// sizeof(IO::PackageFile). Not guessed: LoadPackages allocates `MOV EDX,0xf0`
// per package (0x1800ab55b) and the deleting destructor at 0x1800259d0 passes
// `MOV EDX,0xf0` to the array-delete helper. Two independent sites agree.
constexpr size_t kPackageFileSize = 0xF0;

// ---- the type-info record -------------------------------------------------
// ?Type@ (0x180025130) reads `dword ptr [0x1801f9ac8]` and ?TypeName@
// (0x180025150) reads `qword ptr [0x1801f9ad0]` — the class's own
// BaseTypeInfo, at +0x00 and +0x08. ENDGAME B.3.9 TI-REST: that record is the
// class's `DEFINE_TYPE` static (io_type_records.cpp) and the three accessors
// are DECLARE_TYPE's; the atomic slot over the minted record is gone.

// ENDGAME B.3.9 IO: the PutQ/PutD/PutB transcription of ??0PackageFile
// (`PackageFileCtorBody`, with its three minted vptr stamps) is gone; the
// stores are `IO::PackageFile`'s real constructor (PackageFile.h), in the same
// order, and the vptrs are the compiler's.

// ENDGAME B.3.1 step 2: the shared "null the slot, AddRef if non-null" body the
// three String getters used to hand-roll is exactly `System::String`'s copy
// ctor (0x180006cb0, `Adopt` in string.cpp) -- null-then-AddRef, no freeing
// branch, which is a getter's whole contract. The three call sites below now
// construct that ctor into their hidden return slot directly.
//
//   180025327  MOV qword ptr [RDX],RAX     ; RAX = 0, the return slot
//   18002532a  MOV RAX,qword ptr [RCX+off]
//   18002532e  TEST RAX,RAX / JZ
//   180025333  MOV qword ptr [RDX],RAX
//   180025336  INC dword ptr [RAX + -0x4]  ; the refcount, block+4

}  // namespace

// ============================================================================
// ENDGAME B.3.9 IO: the ??0PackageFile door (its callers -- LoadPackages, the
// FACTREST replay, Construct, Reconstruct -- build the class directly now) and
// the ?Construct@ slot door are gone.
// ============================================================================
// ENDGAME B.3.4 PACKAGE+FS commit 3: every body below is a member of the real
// `IO::PackageFile` (src/engine/io/PackageFile.h).
//
// ENDGAME B.3.9 IO: the seven slot doors (Type, TypeName, GetFileVersion,
// Compile, Serialize, Reconstruct, Destruct) and Construct's are gone with the
// minted ??_7PackageFile row, their only reader. B3R RT-IO: the ?TypeId@ door
// and the three String-getter doors (GetName / GetAuthor / GetDescription)
// went with their last registrations; the members stand.

// ============================================================================
// The scalar field reads. Each is one MOV; the offset IS the function.
// ============================================================================
// B.4 R-IO: the eight `RtIo_PackageFile_Get*` doors that stood here (each a
// reinterpret of a PackageFileView* onto the member) had no caller left and are
// deleted; the members below are the bodies.

// 0x1800254f0 is `MOVZX EAX,byte ptr [RCX+0xac]` — a ZERO extension of the
// byte, not a sign extension and not a dword read. The field is the same one
// LoadPackages' index pass tests, so a wrong width here would change package
// precedence, not just a returned bool. (`IO::PackageFile::CanOverride`, below.)

// ============================================================================
// The two index-taking entry accessors (`IO::PackageFile::GetFileSize` /
// `GetFileOffset`, below).
// ============================================================================
//   1800254a0  MOV RAX,qword ptr [RCX+0xb0]
//   1800254a7  MOVSXD R8,EDX          <- SIGN extension of the int index
//   1800254aa  ADD R8,R8              <- *2, and the *8 is in the SIB scale
//   1800254ad  MOV EAX,dword ptr [RAX + R8*0x8 + 0xc]
//
// So the address is `entries + index*16 + 0xc`, and the index is SIGN-extended.
// Ghidra's C prints that MOVSXD as a zero extension (three Wave 6 defects); it is
// written as int64 here because that is what the instruction says. There is no
// bounds check in either body, and none is added — the caller bounds it with
// GetFileCount, and a guard here would skip work rather than restore it.

}  // namespace RtIO

// ============================================================================
// ENDGAME B.3.4 PACKAGE+FS — `IO::PackageFile`'s member bodies
// ============================================================================
//
// A member of `IO::X` cannot be DEFINED inside `namespace RtIO` — `IO` and
// `RtIO` are siblings, so a definition has to appear in a namespace ENCLOSING
// the class (B34_BRIEFS section A.2). This block therefore sits after
// `}  // namespace RtIO` and reaches that namespace's file-local helpers as
// `RtIO::X`, which works for its anonymous-namespace members too in this same
// translation unit.
//
// Inside `namespace IO` a bare `PackageFile` binds to the REAL CLASS and a bare
// `String`, `Filename` or `Stream` would bind to whatever `IO` sees first, so
// every POD is spelled `RtIO::` / `RtSystem::` and every real class `System::`
// (B34_RULES section 7.2, THE GREP HAZARD).
//
// ENDGAME B.3.9 IO: EVERYTHING MOVED. The constructor and destructor are the
// class's own (PackageFile.h / package_file_dtor.cpp), slot 0 is concrete, and
// every PackageFile -- LoadPackages, the factory pair below, Construct,
// Reconstruct -- wears the compiler's table. The minted row, its RT_DELDTOR
// row and the FACTREST replay are retired.
// ============================================================================

namespace IO {

// ---- the reflection triple, slots 1 and 2 plus the static companion --------
// `?Type@` (0x180025130), `?TypeId@` (0x180025140) and `?TypeName@`
// (0x180025150) are DECLARE_TYPE's bodies over the class's own record
// (ENDGAME B.3.9 TI-REST; io_type_records.cpp). Neither reads RCX.

// ---- slots 3 and 4: the two constant-returning slots -----------------------
// 0x1800b6080: MOV EAX,0x5 ; RET. The .pkg format version this build writes and
// accepts. A constant in the CODE, not a field -- reading it off the object
// would be wrong even though it would usually agree.
int32_t PackageFile::GetFileVersion() const {
    return 5;
}

// 0x1800b6090: MOV EAX,0x1 ; RET. `External::CompileResult`, and the value is 1
// on every path -- a package is never "compiled", so the slot reports the same
// result whatever Filename it is handed and the parameter is genuinely unread.
Object::External::CompileResult PackageFile::Compile(const System::Filename& path) {
    (void)path;
    return 1;
}

// ---- slots 6 and 7 ---------------------------------------------------------
//   ?Destruct@        180025300  MOV RAX,qword ptr [RCX]   ; this->vtable
//                     180025303  XOR EDX,EDX               ; flags = 0
//                     180025305  JMP  qword ptr [RAX]      ; TAIL, slot 0
//
//   ?Reconstruct@     1800252d6  MOV RAX,qword ptr [RCX]   ; this->vtable
//                     1800252d9  XOR EDX,EDX               ; flags = 0
//                     1800252de  CALL qword ptr [RAX]      ; slot 0
//                     1800252e8  JMP  0x1800b4440          ; TAIL, ??0PackageFile
//
// FLAGS = 0, which in the deleting-destructor family means DESTRUCT IN PLACE
// AND DO NOT FREE: the object's storage belongs to the factory that owns it.
// Passing 1 would free a block the caller still holds. The constant is read off
// `XOR EDX,EDX` at both sites, not assumed.
// ENDGAME B.3.9 IO: the object wears the compiler's table, so the virtual
// slot-0 call is `~T()` (slot 0, flags 0) (flags 0: destroy in place, no free).
void PackageFile::Destruct() {
    this->~PackageFile();                                      // 180025305, flags 0
}

// "Destroy in place, then run the constructor over the same storage." The
// declared return is `Object::External*`; retail returns whatever the
// tail-jumped constructor leaves in RAX, which `??0PackageFile@IO@@` sets to
// `this` -- placement construction over the same storage.
Object::External* PackageFile::Reconstruct() {
    this->~PackageFile();                                      // 1800252de, flags 0
    return ::new (this) PackageFile();                    // 1800252e8 ??0PackageFile
}

// ---- slot 8: PackageFile's OWN virtual -------------------------------------
//   1800252f0  TEST RCX,RCX
//   1800252f3  JNZ 0x1800b4440      <- tail jump straight into the constructor
//   1800252f9  XOR EAX,EAX / RET
//
// See src/engine/io/PackageFile.h's banner for the four pieces of retail
// evidence that this virtual is declared on `IO::PackageFile` and not on an
// unmodelled class between `Object::External` and it.
// ENDGAME B.3.9 IO: the retired door held retail's null test; a virtual call
// always has a `this`, so the member is the tail jump alone.
Object::External* PackageFile::Construct() {
    return ::new (this) PackageFile();                    // 1800252f3 ??0PackageFile
}

// ?CreateNewObject@PackageFile@IO@@SAPEAVBase@Object@@XZ  0x180025240
// The FACTREST row this replaces: ?MemAlloc@MemoryAllocator (0x1800c7a50, the
// same function Object::detail::InternalMemAlloc reaches) of 0xf0 bytes,
// align 8; retail's null branch; then the constructor (call at 0x1800252a1).
Object::Base* PackageFile::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(PackageFile), 8);
    if (p == nullptr) return nullptr;
    return ::new (p) PackageFile();
}

// ?CreateInPlaceObject@PackageFile@IO@@SAPEAVBase@Object@@PEAX@Z  0x1800252c0
// retail's null test, then the constructor (call at 0x1800252c3).
Object::Base* PackageFile::CreateInPlaceObject(void* place) {
    if (place == nullptr) return nullptr;
    return ::new (place) PackageFile();
}

// ---- the three String getters ----------------------------------------------
// Retail's body is the null-then-AddRef idiom, which is `System::String`'s copy
// constructor exactly (0x180006cb0): it nulls the return slot, loads the
// member, and AddRefs only if it is non-null. The members are System::String
// members (B4 X-R3).
//
//   180025327  MOV qword ptr [RDX],RAX     ; RAX = 0, the return slot
//   18002532a  MOV RAX,qword ptr [RCX+off]
//   18002532e  TEST RAX,RAX / JZ
//   180025333  MOV qword ptr [RDX],RAX
//   180025336  INC dword ptr [RAX + -0x4]  ; the refcount, block+4
System::String PackageFile::GetName() const {
    return System::String(_name);                              // +0x40
}

System::String PackageFile::GetAuthor() const {
    return System::String(_author);                            // +0x48
}

System::String PackageFile::GetDescription() const {
    return System::String(_description);        // +0x50
}

// ---- the scalar field reads ------------------------------------------------
// Each is one MOV; the offset IS the function, and these eight bodies are now
// the ONLY place in the tree that spells it.
int32_t  PackageFile::GetVersion() const     { return _version; }      // 0x1800253d0 +0x68
int32_t  PackageFile::GetUserVersion() const { return _userVersion; }  // 0x1800253e0 +0x6c
int32_t  PackageFile::GetPriority() const    { return _priority; }     // 0x1800254e0 +0x70
int32_t  PackageFile::GetPlatform() const    { return _platform; }     // 0x1800253f0 +0x74
int32_t  PackageFile::GetIconWidth() const   { return _iconWidth; }    // 0x180025510 +0x78
int32_t  PackageFile::GetIconHeight() const  { return _iconHeight; }   // 0x180025520 +0x7c
uint32_t PackageFile::GetCRC() const         { return _crc; }          // 0x180025500 +0xa8
int32_t  PackageFile::GetFileCount() const   { return _entries._size; }   // 0x180025440 +0xb8

// 0x1800254f0 is `MOVZX EAX,byte ptr [RCX+0xac]` -- a ZERO extension of the
// byte, not a sign extension and not a dword read. The field is the same one
// LoadPackages' index pass tests, so a wrong width here would change package
// precedence, not just a returned bool.
bool PackageFile::CanOverride() const {
    return _overridable != 0;
}

// ---- the two index-taking entry accessors ----------------------------------
//   1800254a0  MOV RAX,qword ptr [RCX+0xb0]
//   1800254a7  MOVSXD R8,EDX          <- SIGN extension of the int index
//   1800254aa  ADD R8,R8              <- *2, and the *8 is in the SIB scale
//   1800254ad  MOV EAX,dword ptr [RAX + R8*0x8 + 0xc]
//
// So the address is `entries + index*16 + 0xc`, and the index is SIGN-extended.
// Ghidra's C prints that MOVSXD as a zero extension (three Wave 6 defects); it is
// written as int64 here because that is what the instruction says. There is NO
// bounds check in either body and none is added -- the caller bounds it with
// GetFileCount, and a guard here would skip work rather than restore it.
uint32_t PackageFile::GetFileSize(int index) const {
    return _entries._data[static_cast<int64_t>(index)].size;          // +0x0C
}

uint32_t PackageFile::GetFileOffset(int index) const {
    return _entries._data[static_cast<int64_t>(index)].offset;        // +0x08
}

}  // namespace IO
