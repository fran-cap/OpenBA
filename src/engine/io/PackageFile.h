#pragma once
// ============================================================================
// src/engine/io/PackageFile.h — the REAL `IO::PackageFile`
// ENDGAME B.3.4 PACKAGE+FS commit 3.   Design: B34_RULES.md §1.4, §4.2, §4.3;
// B34_BRIEFS.md §E.2, §E.3.
// ============================================================================
//
// There was no header for this class before this commit. Its layout was
// scattered across `FileSystem.h`'s `PackageFileView` (a PARTIAL view of the
// eleven fields other files read) and the raw offsets
// `src/engine/io/package_file.cpp`'s constructor writes. This header is
// written FROM THAT CONSTRUCTOR, which is the strongest source a layout can
// have: `??0PackageFile@IO@@QEAA@XZ` (retail 0x1800b4440) writes the WHOLE
// 0xf0-byte object, store by store, and `src/engine/io/package_file.cpp`'s
// `PackageFileCtorBody` already carries every one of those stores with the
// address of the instruction it reproduces.
//
// Every field below is pinned by a `static_assert` against either that store or
// the one-instruction accessor that reads it. Where the constructor's store
// list SKIPS a span, that span is explicit padding with the skip recorded — it
// is not extended from a decompile (the rule `Persistence.h`'s banner states
// for its own file and `FileSystem.h`'s +0x58/+0x60 note states for naming).
//
// ----------------------------------------------------------------------------
// IT IS AN `Object::External` SUBCLASS, AND SLOT 8 IS ITS OWN
// ----------------------------------------------------------------------------
// `??_7PackageFile@IO@@6B@` @ 0x180155700 has 9 slots, bounded by a null qword
// at 0x180155748. Slots 1..7 are `Object::External`'s own eight minus its slot
// 0, in External's exact order (B34_RULES §1.4) — which is what makes
// `package_file_serialize.cpp`'s "slot 5" `External::Serialize`.
//
// *** THE SLOT-8 QUESTION B34_BRIEFS §E.2 LEFT OPEN IS ANSWERED HERE:
//     `Construct` IS DECLARED BY `IO::PackageFile` ITSELF, not by an
//     unmodelled class between `External` and it. *** Four independent pieces
//     of evidence, all read out of retail `Runtime-x64.dll` (function_count
//     15290) this pass:
//
//   1. **0x1800252f0 is reachable from exactly one vtable.** `get_xrefs_to`
//      returns three references and no more: the export-table entry at
//      0x1801b26ec, the EXTERNAL entry-point row, and **0x180155740**, which is
//      `??_7PackageFile@IO@@6B@ + 0x40` — slot 8. No other class's table holds
//      this body.
//   2. **`Object::External`'s own table stops at eight.** Re-read byte-wise:
//      0x1801547a0 = { 0x180006640, 0x1800066c0, 0x1800066d0, _purecall,
//      0x1800066e0, _purecall, _purecall, _purecall }, then a NULL qword at
//      0x1801547e0 immediately followed by the "External" type-name string.
//      `Construct` is absent from it, so slot 8 is ADDED below External.
//   3. **Fifty-eight classes across nine namespaces each export their own
//      `?Construct@<Leaf>@<NS>@@UEAAPEAVExternal@Object@@XZ`** — `UI` (30),
//      `Graphics` (8), `Video` (5), `Audio` (2), `Collision2D` (2),
//      `Picking`, `Pathfinding`, `Achievement`, `Object::ExternalList` and this
//      one — and **there is no `?Construct@` on any base class at all.** Each
//      sits inside a per-class reflection-boilerplate block whose members
//      (`_TypeName`, `_ParentTypeName`, `Type`, `TypeId`, `TypeName`,
//      `CreateNewObject`, `CreateInPlaceObject`, `Reconstruct`, `Construct`,
//      `Destruct`) are emitted at consecutive addresses — i.e. a macro expanded
//      in each class BODY, which is what puts the virtual in each class.
//   4. **A second, independent 9-slot instance has the identical shape.**
//      `Achievement::Achievement`, a direct `External` subclass, has its table
//      at 0x180154740: slots 1..7 in External's order (slot 4 is 0x1800066e0,
//      External's own `Compile`, INHERITED), slot 8 = 0x1800012f0 `Construct`,
//      bounded by `Object::Base`'s table at 0x180154788. So "External plus
//      exactly one added virtual" is the family's general shape, not something
//      peculiar to `PackageFile`.
//
// This also agrees with what B.3.3 already committed to for the same situation
// one class over: `src/engine/object/External.h` declares
// `virtual External* Construct();  // [8] 0x1800075e0` on `Object::ExternalList`
// with the note "so `Construct` is ExternalList's OWN new virtual: External's
// table stops at eight". This header does the same thing for the same reason.
//
// ----------------------------------------------------------------------------
// IT GETS NO REAL CONSTRUCTOR AND NO REAL DESTRUCTOR — B34_RULES §4.3
// ----------------------------------------------------------------------------
// `IO::PackageFile` is constructed TWO ways, and neither is ours to take over:
//
//   * a `standalone/rt_factories_io.*` object-creation replay row (an
//     `RtFactOp::Call` into `??0PackageFile@IO@@QEAA@XZ`), and
//   * `src/engine/io/filesystem_packages.cpp`'s `MemAlloc(0xF0)` followed by
//     the same ctor door.
//
// Both leave the object wearing the MINTED `??_7PackageFile@IO@@6B@`, whose
// nine rows are LIVE (B34_RULES §4.2: not one of the nineteen IO minted rows
// and not one of the fourteen IO deleting-dtor registrations may retire in this
// step). A real C++ constructor would stamp GCC's table instead and swap the
// table under the replay.
//
// THE SHAPE THAT ENFORCES IT is B34_BRIEFS §A.4's: slot 0 is re-declared
// `override = 0`, so **the class is ABSTRACT and nothing can construct one**.
// That is not a workaround, it is the honest statement: slot 0's body lives in
// `src/engine/object/deleting_dtor.cpp` as
// `RT_DELDTOR(0x1800259d0, …, 0x0f0, "IO::PackageFile")`, which is also what
// FILLS slot 0 of the live minted table, and §4.2 forbids retiring it. The
// destructor (0x1800b4540, `src/engine/io/package_file_dtor.cpp`) stays a
// transcribed body for the same reason: it performs retail's base->derived->base
// vptr restamp BY HAND, and a compiler-generated destructor chain would do
// something different with a table other code still reads.
//
// The class is therefore NOT marked `final`: unlike `IO::BinaryReader` and
// friends, its slot ORDER has to be pinned by a concrete probe subclass in
// `tests/unit/`, and `final` would make that impossible.
//
// ----------------------------------------------------------------------------
// THE TYPE RECORD
// ----------------------------------------------------------------------------
// `IO::PackageFile` is the only reflected `Object::External` subclass in the
// namespace (retail record 0x1801f9ac8). It was a MINTED row until ENDGAME
// B.3.9 TI-REST; now the class takes `DECLARE_TYPE` and
// src/engine/io/io_type_records.cpp `DEFINE_TYPE`s it, and `Type()`,
// `TypeId()` and `TypeName()` read the class's own record.
//
// ----------------------------------------------------------------------------
// THE FIVE STRINGS AND THE TWO STRING ARRAYS ARE MEMBERS (B4 X-R3, H5)
// ----------------------------------------------------------------------------
// Retail's ctor 0x1800b4440 zeroes +0x20 / +0x30 (the two Array<String>
// headers, 0x1800b4489..95) and then +0x40 .. +0x60 (0x1800b4499..a9) before
// any other field. Retail's dtor 0x1800b4540: ~FileInfo on +0xc0
// (0x1800b456c), the entry array +0xb0 (0x1800b4579), the preview +0x98 and
// icon +0x80 arrays (0x1800b4586 / 0x1800b4593), THEN the inlined String
// release on +0x60, +0x58, +0x50, +0x48, +0x40 (0x1800b4599..0x1800b4658),
// THEN ~Array<String> on +0x30 and +0x20 (0x1800b465c / 0x1800b4666), then
// ~External (0x1800b4679). Typed, the compiler constructs these seven in
// declaration order before the ctor body and destroys them in reverse
// declaration order after the dtor body (whose four hand-written steps are
// the four raw members above them): exactly retail's sequence.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <type_traits>

#include "FileSystem.h"                // RtIO::FileInfo
#include "Modification.h"              // RtIO::Mod and the Array assigns GetIconData / GetPreviewData call
#include "../object/External.h"          // Object::External — the base
#include "../object/Internal.h"          // Object::detail::InternalMemFree
#include "../system/String.h"
#include "../collection/Collection.h"   // B.4 Z LAST IO: the entry array member

namespace rtcoll {
extern const ArraySpec kSpec_PkgEntry;   // 0x1800298f0  Array<IO::PackageFile::Entry> (array.cpp)
}  // namespace rtcoll

namespace IO {

class PackageFile : public Object::External {
public:
    // The package header word (ReadPackfile, package_file_read.cpp): bit 31 says
    // the directory is compressed, bits 0..30 are its offset.
    static constexpr uint32_t kHeaderCompressedBit  = 0x80000000u;   // BT 31
    static constexpr uint32_t kHeaderOffsetMask     = 0x7fffffffu;   // BTR 31
    static constexpr int32_t  kDefaultPriority      = 0x2710;        // 10000, the ctor's / uncompressed default
    static constexpr uint32_t kPlaceholderIconPixel = 0xff000000u;   // the 1x1 opaque-black default icon
    // ---- ENDGAME B.3.9 IO: CONCRETE ------------------------------------------
    // The two banners above ("NO REAL CONSTRUCTOR", "NO DESTRUCTOR
    // DECLARATION") describe B.3.4. In B.3.9 the minted ??_7PackageFile row,
    // its RT_DELDTOR row, the FACTREST replay and the ??0 / ??1 / slot doors
    // are retired; every PackageFile is built by this constructor
    // (LoadPackages, CreateNewObject / CreateInPlaceObject, Construct,
    // Reconstruct) and wears the compiler's table.
    //
    // ??0PackageFile@IO@@QEAA@XZ 0x1800b4440: External() (Base's and
    // External's stores; the vptrs are the compiler's), then retail's stores
    // in retail's order -- including the FileInfo's out-of-order +0x18 last.
    // NOT written: +0x78/+0x7c, +0x90/+0x94, +0xad..+0xaf, FileInfo +0x04 and
    // +0x14 (i.e. +0xc4 and +0xd4).
    // The two Arrays (1800b4489..95) and the five Strings (1800b4499..a9) are
    // their members' own constructors, run in declaration order first.
    // B4 X7-RT: the icon and preview Arrays are members; the four scalars
    // before them move into the init list with them, so the stores keep
    // retail's order (+0x68, +0x70, +0x80/+0x88, +0x98/+0xa0, then the body).
    PackageFile()
        : Object::External(),
          _version(0), _userVersion(0),                         // 1800b44ad (one qword)
          _priority(kDefaultPriority), _platform(0),            // 1800b44b1 (one qword 0x2710)
          _iconData(),                                          // 1800b44b9 / 1800b44c0
          _previewData(),                                       // 1800b44c7 / 1800b44ce
          // B.4 Z LAST IO: the last four are members constructed in
          // declaration order, which is retail's store order.
          _crc(0),                                              // 1800b44d5
          _overridable(true),                                   // 1800b44dc
          _entries(),                                           // 1800b44e3 / 1800b44ea
          _file() {                                             // 1800b44fd..1800b4523 (FileInfo's ctor)
    }

    // ---- the nine slots of ??_7PackageFile@IO@@6B@ (0x180155700) ----------
    // [0] 0x1800259d0 — MSVC's VECTOR deleting destructor (`MOV EDX,0xf0`):
    // `flags & 2` reads the int32 count at `this - 8`, destroys downward, frees
    // `this - 8` on `flags & 1` and returns it; otherwise ~PackageFile, then
    // 0x1800251e0 (PackageFile's operator delete: the locked MemFree) on
    // `flags & 1`. Nothing `new[]`s a PackageFile.
    // ??1PackageFile@IO@@UEAA@XZ 0x1800b4540 — ENDGAME B.3.9 IO: retail's
    // body, moved from the retired door into package_file_dtor.cpp. After it
    // the compiler destroys the five Strings and the two String Arrays (the
    // banner) and tails into ~External (retail's JMP at 0x1800b4679).
    // Virtual (B3X W1).
    ~PackageFile() override;

    // [1] 0x180025150 TypeName / [2] 0x180025130 Type, the static TypeId
    // 0x180025140, `_TypeName` / `_ParentTypeName` and the factory pair
    // 0x180025240 / 0x1800252c0 (package_file.cpp): retail's macro (ENDGAME
    // B.3.9 TI-REST, B39_RULES D6). The record is `DEFINE_TYPE`d in
    // src/engine/io/io_type_records.cpp.
    DECLARE_TYPE(PackageFile)
public:
    int32_t       GetFileVersion() const override;              // [3] 0x1800b6080
    // [4] 0x1800b6090 — PackageFile's OWN override, NOT External's inherited
    // 0x1800066e0. `MOV EAX,0x1 ; RET`: a package is never "compiled", so the
    // slot reports the same result whatever Filename it is handed and the
    // parameter is genuinely unread. Its body is ported but has NEVER been
    // registered (there is no `?Compile@PackageFile@IO@@` row anywhere in the
    // tree, by name or by address); that is unchanged here, because this commit
    // adds no registration.
    CompileResult Compile(const System::Filename& path) override;  // [4] 0x1800b6090
    // [5] 0x1800b60a0. The parameter is `IO::Stream&` in retail
    // (`?Serialize@PackageFile@IO@@UEAA_NAEAVStream@2@@Z`) and `void*` here
    // because `Object::External` declares the slot that way until STREAMS
    // lands `IO::Stream`. Changing it is block 4's job, on the base, and this
    // override follows it for free.
    bool          Serialize(IO::Stream& stream) override;             // [5] 0x1800b60a0
    Object::External* Reconstruct() override;                   // [6] 0x1800252d0
    void          Destruct() override;                          // [7] 0x180025300
    // [8] — PackageFile's OWN new virtual. See the banner for the four pieces
    // of evidence that it is declared HERE and not on an unmodelled class
    // between `Object::External` and this one. Its body is a bare
    // `TEST RCX,RCX / JNZ ??0PackageFile` tail jump, so the null branch is
    // real and not defensive: the factory calls it on whatever the allocator
    // returned, including nullptr.
    virtual Object::External* Construct();                      // [8] 0x1800252f0

    // ---- the non-virtual exported members ---------------------------------
    // All `QEBA`/`QEAA`. The three String getters return BY VALUE and AddRef
    // rather than copy — retail's bodies are the null-then-AddRef idiom, which
    // is `System::String`'s copy constructor exactly.
    System::String GetName() const;         // ?GetName@        0x180025320
    System::String GetAuthor() const;       // ?GetAuthor@      0x180025360
    System::String GetDescription() const;  // ?GetDescription@ 0x1800253a0

    int32_t  GetVersion() const;      // 0x1800253d0  MOV EAX,[RCX+0x68]
    int32_t  GetUserVersion() const;  // 0x1800253e0  MOV EAX,[RCX+0x6c]
    int32_t  GetPriority() const;     // 0x1800254e0  MOV EAX,[RCX+0x70]
    int32_t  GetPlatform() const;     // 0x1800253f0  MOV EAX,[RCX+0x74]
    int32_t  GetIconWidth() const;    // 0x180025510  MOV EAX,[RCX+0x78]
    int32_t  GetIconHeight() const;   // 0x180025520  MOV EAX,[RCX+0x7c]
    uint32_t GetCRC() const;          // 0x180025500  MOV EAX,[RCX+0xa8]
    int32_t  GetFileCount() const;    // 0x1800254d0  MOV EAX,[RCX+0xb8]

    // 0x1800254f0 is `MOVZX EAX,byte ptr [RCX+0xac]` — a ZERO extension of the
    // byte. The field is the one LoadPackages' index pass tests, so a wrong
    // width here would change package precedence, not just a returned bool.
    bool CanOverride() const;         // ?CanOverride@ 0x1800254f0

    // Both index-takers SIGN-extend the index (`MOVSXD R8,EDX`) and neither
    // bounds-checks. No guard is added: the caller bounds it with
    // GetFileCount, and a guard here would skip work rather than restore it.
    uint32_t GetFileOffset(int index) const;  // 0x1800254c0
    uint32_t GetFileSize(int index) const;    // 0x1800254a0

    // The two counted-array getters, both returning `Collection::Array<T>` BY
    // VALUE into a hidden return slot. The element types come from the export
    // names, not from the call sites: `?$Array@I` (unsigned int) for the icon
    // at +0x80, `?$Array@E` (unsigned char) for the preview at +0x98. Both
    // carry retail's self-compare guard (`LEA RDX,[RCX+0x80] ; CMP RBX,RDX`),
    // which can only be true if a caller passes the member's own address as the
    // return slot — transcribed, not dropped.
    //
    // Returned BY VALUE, as retail's `?AV?$Array@I` / `?AV?$Array@E` spell it:
    // the callee zeroes the caller's slot (NRVO) and assigns into it. The
    // bodies stay in modification_refresh.cpp.
    Collection::Array<uint32_t> GetIconData() const;   // 0x180025530
    Collection::Array<uint8_t>  GetPreviewData() const; // 0x180025580

    // ?ReadPackfile@PackageFile@IO@@QEAA_NAEBVFilename@System@@@Z 0x1800b46b0
    bool ReadPackfile(const System::Filename& path);

    // ---- ENDGAME B.3.4a closing: the last two PackageFile bodies ----------
    // Both lived in FileSystem's files (filesystem_openfile.cpp,
    // filesystem_packages.cpp) because that is where their CALLERS are, not
    // because they are FileSystem's. Their doors stay in those files with their
    // names and registrations; only the bodies moved here.
    //
    // ?GetFileInfo@PackageFile@IO@@QEBA?AUFileInfo@FileSystem@2@XZ 0x180025400
    // Returns the EMBEDDED `_file` at +0xc0 BY VALUE — a copy ctor into the
    // hidden return slot, no bounds check and nothing else.
    RtIO::FileInfo GetFileInfo() const;

    // ?GetFileName@PackageFile@IO@@QEBA?AVFilename@System@@H@Z     0x180025450
    // `MOVSXD RDX,R8D ; SHL RDX,0x4 ; ADD RDX,qword ptr [RCX+0xb0]` — a
    // SIGN-extended index into `_entries` at stride 16, then the Filename copy
    // ctor. NO BOUNDS CHECK, deliberately: the caller bounds the index with
    // GetFileCount and a guard here would skip work rather than restore it.
    System::Filename GetFileName(int index) const;

    // Retail's factory pair (B39_RULES D2), declared by DECLARE_TYPE above,
    // replacing the FACTREST replay row: MemAlloc(0xf0, 8) + the ctor, and the
    // null-tested in-place ctor. Defined and registered (name + VA) in
    // package_file.cpp.

    // ---- the 0xd0 bytes PackageFile adds, from +0x20 ----------------------
    // PUBLIC, like `Object::External`'s and `IO::Serializer`'s, because the
    // unconverted bodies in filesystem_packages.cpp, filesystem_find.cpp
    // and package_file_serialize.cpp reach these fields directly, and `offsetof`
    // on them has to be well formed for the pins below.

    // +0x20 and +0x30 — TWO `Collection::Array<System::String>` headers. Their
    // MEANING is unknown and is NOT invented here: `package_file_serialize.cpp`
    // refuses to name them on the argument that "a Serialize pins a field's
    // TYPE and its POSITION IN THE RECORD STREAM and says nothing about its
    // meaning", which is the same argument FileSystem.h:101 makes for
    // +0x58/+0x60. What IS known: the serializer writes each as
    // count-then-elements through `StringArraySerialize`, and sizes the array
    // from the count on the reading side only.
    Collection::Array<System::String> _stringArray20;  // +0x20  ctor 0x1800b4489, dtor 0x1800b4666
    Collection::Array<System::String> _stringArray30;  // +0x30  ctor 0x1800b4491, dtor 0x1800b465c

    // The five refcounted Strings ~PackageFile releases, in the order the
    // three getters and the serializer name them (see the banner).
    System::String _name;         // +0x40
    System::String _author;       // +0x48
    System::String _description;  // +0x50
    System::String _string58;     // +0x58 — purpose unknown; type from ~PackageFile
    System::String _string60;     // +0x60 — purpose unknown; type from ~PackageFile

    int32_t  _version;      // +0x68
    int32_t  _userVersion;  // +0x6c
    // The ctor seeds +0x70/+0x74 with ONE qword 0x2710, i.e. priority 10000 and
    // platform 0. +0x70 is the only field the packfile sort compares
    // (0x1800264a0's whole comparator is `CMP dword ptr [RCX+0x70],EAX`).
    int32_t  _priority;     // +0x70
    int32_t  _platform;     // +0x74
    // The only two fields the CONSTRUCTOR does not initialise: its store list
    // jumps +0x70 -> +0x80. That is retail leaving them to ReadPackfile, not a
    // gap in the transcription.
    int32_t  _iconWidth;    // +0x78
    int32_t  _iconHeight;   // +0x7c

    // +0x80 — `Collection::Array<unsigned int>` (`?GetIconData@` spells
    // `?$Array@I`). B4 X7-RT (`behaviour:`): a typed member; its destructor is
    // retail's 0x1800b4593 CALL 0x1800111a0, run after ~PackageFile's body and
    // after the preview array's (reverse declaration order).
    Collection::Array<uint32_t> _iconData;   // +0x80 {data, size +0x88, capacity +0x8c}

    // +0x90 — A REAL int32 FIELD THE CONSTRUCTOR SKIPS. Its store list runs
    // …, PutQ(0x88, 0), PutQ(0x98, 0), … so +0x90..+0x97 is never written by
    // `??0PackageFile@IO@@`, exactly like +0x78/+0x7c — retail leaves it to
    // ReadPackfile. And ReadPackfile is also what pins it:
    // `package_file_read.cpp` reads it as an `int32_t` and passes it to
    // `CompressedStream::Read` as the BYTE COUNT for the +0x98 array
    // (retail 0x1800b4e09), immediately after reading the array's own header.
    // So it is the preview blob's on-disk byte length, held separately from the
    // `Array`'s element count at +0xa0.
    //
    // It is NOT absorbed into a neighbour: giving it to the icon array would
    // make that a four-word header, and giving it to the preview array would
    // move +0x98, which `?GetPreviewData@`'s `LEA RDX,[RCX+0x98]` pins. +0x94
    // is untouched by anything read this pass and stays explicit padding.
    int32_t  _previewByteCount;  // +0x90
    int32_t  _pad94;             // +0x94 — NOT PINNED

    // +0x98 — `Collection::Array<unsigned char>` (`?GetPreviewData@` spells
    // `?$Array@E`). B4 X7-RT: a typed member; its destructor is retail's
    // 0x1800b4586 CALL 0x1800112a0, the first one after the body.
    Collection::Array<uint8_t> _previewData;  // +0x98 {data, size +0xa0, capacity +0xa4}

    // +0xa8 — the package CRC, which is ALSO the packfile id: `?GetCRC@`
    // (0x180025500) and `FileSystem::GetPackfileId` (0x1800ab050) hit the same
    // four bytes. They are one field, not two.
    uint32_t _crc;           // +0xa8
    // +0xac — born 1 in the constructor (`MOV byte ptr [RCX+0xac],0x1`).
    // LoadPackages' index pass reads it on the package that ALREADY owns a
    // colliding path: nonzero means the later package takes the entry over.
    // That is the whole of the loose-file / .pkg / .pkm precedence rule.
    bool     _overridable;   // +0xac  Serialize's bool slot (+0x78)
    uint8_t  _padAd[3];      // +0xad

    // +0xb0 — Collection::Array<Entry>, stride 16. B.4 Z LAST IO
    // (`behaviour:`): a real member on retail's own row, `kSpec_PkgEntry`
    // (0x1800298f0). Its ~Array is retail's 0x1800269a0 (the row's destroy
    // over the live range, the J1 free, the header zeroed), which ~PackageFile
    // 0x1800b4540 runs right after ~FileInfo (+0xc0) and before the two
    // Arrays below it: reverse declaration order.
    using EntryArray = Collection::Array<RtIO::PackageFileEntry, Collection::DefaultAllocator,
                                         Collection::ExplicitSpec<&rtcoll::kSpec_PkgEntry>>;
    EntryArray _entries;     // +0xb0  data, +0xb8 size, +0xbc capacity

    // +0xc0 — the EMBEDDED `IO::FileSystem::FileInfo`, 0x30 bytes, which is
    // what makes the object exactly 0xf0. The constructor does `ADD RCX,0xc0`
    // at 0x1800b44f1 and then writes the FileInfo's own fields in retail's
    // out-of-order sequence (+0x00, +0x08, +0x10, +0x20, +0x28, and only then
    // +0x18 = -1, the Win32 HANDLE).
    RtIO::FileInfo _file;    // +0xc0 .. +0xef  ctor 1800b44fd..23, dtor 0x1800b456c (0x18000c180)
};

// ---- the layout pins ------------------------------------------------------
// The size is not a guess: `RT_DELDTOR(0x1800259d0, 0x1800b4540, 0x0f0,
// "IO::PackageFile")` carries it, and the constructor's last store is the
// FileInfo's +0x18 at object offset 0xd8, inside a 0x30-byte member starting at
// 0xc0 — 0xc0 + 0x30 == 0xf0 exactly.
static_assert(sizeof(PackageFile) == 0xf0,
              "IO::PackageFile is 0xf0 bytes -- the element size of its deleting dtor 0x1800259d0");
static_assert(offsetof(PackageFile, _pad94) == 0x94, "PackageFile::_pad94");
static_assert(offsetof(PackageFile, _padAd) == 0xAD, "PackageFile::_padAd");
static_assert(sizeof(Object::External) == 0x20,
              "PackageFile's own fields start where External's end");

static_assert(offsetof(PackageFile, _stringArray20)      == 0x20, "ctor 1800b4489 PutQ(0x20,0)");
static_assert(offsetof(PackageFile, _stringArray20) + offsetof(Collection::Array<System::String>, _size) == 0x28,
              "Serialize 1800b6153 reads the count at +0x28");
static_assert(offsetof(PackageFile, _stringArray30)      == 0x30, "ctor 1800b4491 PutQ(0x30,0)");
static_assert(offsetof(PackageFile, _stringArray30) + offsetof(Collection::Array<System::String>, _size) == 0x38,
              "Serialize 1800b6193 reads the count at +0x38");
static_assert(offsetof(PackageFile, _name)        == 0x40, "GetName 0x18002532a MOV RAX,[RCX+0x40]");
static_assert(offsetof(PackageFile, _author)      == 0x48, "GetAuthor 0x18002536a");
static_assert(offsetof(PackageFile, _description) == 0x50, "GetDescription 0x1800253aa");
static_assert(offsetof(PackageFile, _string58)    == 0x58, "~PackageFile 0x1800b45c0 releases [RBX+0x58]");
static_assert(offsetof(PackageFile, _string60)    == 0x60, "~PackageFile 0x1800b4599 releases [RBX+0x60]");
static_assert(offsetof(PackageFile, _version)     == 0x68, "GetVersion 0x1800253d0");
static_assert(offsetof(PackageFile, _userVersion) == 0x6c, "GetUserVersion 0x1800253e0");
static_assert(offsetof(PackageFile, _priority)    == 0x70, "sort 0x1800264a0 CMP dword [RCX+0x70],EAX");
static_assert(offsetof(PackageFile, _platform)    == 0x74, "GetPlatform 0x1800253f0");
static_assert(offsetof(PackageFile, _iconWidth)   == 0x78, "GetIconWidth 0x180025510");
static_assert(offsetof(PackageFile, _iconHeight)  == 0x7c, "GetIconHeight 0x180025520");
static_assert(offsetof(PackageFile, _iconData)    == 0x80, "GetIconData 0x180025530 LEA RDX,[RCX+0x80]");
static_assert(offsetof(PackageFile, _iconData) + offsetof(Collection::Array<uint32_t>, _size) == 0x88,
              "Serialize 1800b620f reads the count at +0x88");
static_assert(offsetof(PackageFile, _previewByteCount) == 0x90,
              "ReadPackfile 0x1800b4e09 reads [this+0x90] as the byte count of the "
              "+0x98 array read; the ctor's store list jumps PutQ(0x88,0) to PutQ(0x98,0)");
static_assert(offsetof(PackageFile, _previewData) == 0x98, "GetPreviewData 0x180025580 LEA RDX,[RCX+0x98]");
static_assert(offsetof(PackageFile, _previewData) + offsetof(Collection::Array<uint8_t>, _size) == 0xa0,
              "ctor 1800b44ce PutQ(0xA0,0)");
static_assert(offsetof(PackageFile, _crc)         == 0xa8, "GetCRC 0x180025500 / GetPackfileId 0x1800ab050");
static_assert(offsetof(PackageFile, _overridable) == 0xac, "ctor 1800b44dc MOV byte [RCX+0xac],1");
static_assert(offsetof(PackageFile, _entries)     == 0xb0, "GetFileName 0x180025475 ADD RDX,[RCX+0xb0]");
static_assert(offsetof(PackageFile, _entries) + offsetof(PackageFile::EntryArray, _size) == 0xb8,
              "LoadPackages 0x1800ab889 MOV EAX,[RCX+0xb8]");
static_assert(sizeof(PackageFile::EntryArray) == 0x10, "the 16-byte Array header");
static_assert(offsetof(PackageFile, _file)        == 0xc0, "ctor 1800b44f1 ADD RCX,0xc0");
static_assert(sizeof(RtIO::FileInfo) == 0x30, "the embedded FileInfo is what makes 0xc0 + 0x30 == 0xf0");

// (B.4 Z LAST IO: the cross-asserts against the `RtIO::PackageFileView` POD
// are gone with the POD; the pins above are the class's own.)

// The class IS polymorphic and that is the point — unlike `IO::Serializer`
// and `IO::Modification`, this one has a live nine-row minted
// vtable. What must not drift is the SLOT COUNT, and nothing a header can
// assert covers that; `tests/unit/io_vtable_abi_test.cpp`'s
// `package_file_adds_exactly_one_virtual_at_slot_8_over_External` is the guard.
static_assert(std::is_polymorphic<PackageFile>::value,
              "IO::PackageFile is an Object::External subclass with a nine-slot table");

}  // namespace IO
