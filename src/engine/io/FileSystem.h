#pragma once
// ============================================================================
// src/engine/io/FileSystem.h — IO::FileSystem binary layout
// ============================================================================
// Wave 4, lane D. Every offset below was read live in Ghidra on 2026-09-06
// against Runtime-x64.dll (image base 0x180000000) via the five functions
// this lane ports (filesystem.cpp): Exists, GetDataDirectory,
// GetSourceDirectory, GetPackfileId, and — indirectly, since it walks the
// same tree — nothing else. Only the fields those five touch are modelled;
// everything else is opaque padding.
//
// This is NOT the first documentation of this layout. Lane D's own
// src/game/Dialog/mod_dialog_vtables.cpp (2026-09-05, ModProbe_DumpFileSystem
// / ModProbe_DumpPackageIndex) already recovered +0x08 dataDirectory, +0x18/
// +0x19 the two Exists() gates, +0x20/+0x28 the packfile array+count, +0x30/
// +0x38 a package-filename array+count (NOT read by anything in this file),
// and +0x40 the path->packfile BST root — all from reading the exe-side probe
// code, not from decompiling FileSystem's own methods. This header adds the
// one thing that comment never had: a static_assert per offset, each pinned
// by the actual instruction in the method that reads it.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <type_traits>   // B.3.4 PACKAGE+FS: the "IO::FileSystem has no vptr" pins

#include "../system/String.h"
#include "../system/Filename.h"   // B.3.4: IO::FileSystem returns System::Filename by value
#include "../system/Value.h"      // System::DateTime (GetFileTimeStamp)
#include "../collection/Collection.h" // B.4 Y RT: _packfiles is Collection::Array<PackageFile*>
#include "Modification.h"             // B.4 Z RT2: RtIO::ModArrayOf, the _userPackFilenames header
#include "platform_file.h"            // Phase D.4 THREADS E.5: FileInfo::winHandle is a Platform::FileHandle

namespace RtIO {

// ---- The path -> packfile index BST --------------------------------------
// Walked identically by GetPackfileId (0x1800ab050), ExistsInPackfile
// (0x1800ab180) and LoadPackages (0x1800ab460) — all three do `node = node[3]`
// / `node = node[4]` for the children and read `*(uint*)(node + 1)` for the
// packed pack/file index. Only GetPackfileId's walk is ported here.
// B.4 acceptance (ruling a): ONE declaration of the node. The element is the
// Filename key and the {packIndex, fileIndex} pair: 0x180008840 copies the pair
// as one qword (`MOV RAX,[RDX+8] / MOV [RCX+8],RAX`), LoadPackages' override
// writes it as one qword store at [RBX+8], and the readers (GetPackfileId
// 0x1800ab050 `MOV ECX,[RBX+0x8]`, CreateFile's handle) read the two dwords.
// The node is `Collection::Set<PackfileIndexPair>::Node`: +0x10 parent (written
// by 0x180008840 `MOV [RSI+0x10],RDI`, read by the retrace 0x180025e10),
// +0x18 / +0x20 the children GetPackfileId descends (`node[3]` / `node[4]`),
// +0x28 a signed 16-bit balance and +0x2a a 16-bit height (0x180025e10;
// 0x180008840 zeroes both with one dword store); 0x30 bytes, align 8
// (0x180008520: `MOV EDX,0x30 / LEA R8D,[RDX-0x28]`).
struct PackfileIndexValue {
    uint32_t packIndex;   // +0x00 — index into FileSystem::_packfiles
    uint32_t fileIndex;   // +0x04 — entry index inside that PackageFile
};
static_assert(sizeof(PackfileIndexValue) == 8, "the packed qword 0x180008840 moves");
static_assert(offsetof(PackfileIndexValue, packIndex) == 0, "low dword");
static_assert(offsetof(PackfileIndexValue, fileIndex) == 4, "high dword");

struct PackfileIndexPair {
    System::Filename   key;     // +0x00 — dataDir-relative path, case-insensitive
    PackfileIndexValue value;   // +0x08
};
static_assert(sizeof(PackfileIndexPair) == 0x10, "the element of the 0x30-byte node");
static_assert(offsetof(PackfileIndexPair, key) == 0x0, "PackfileIndexPair::key");
static_assert(offsetof(PackfileIndexPair, value) == 0x08, "GetPackfileId reads +0x08 of the node");

using PackfileIndexSet  = Collection::Set<PackfileIndexPair>;
using PackfileIndexNode = PackfileIndexSet::Node;
static_assert(sizeof(PackfileIndexNode) == 0x30,
              "0x180008520 allocates 0x30 bytes, align 8");
static_assert(offsetof(PackfileIndexNode, parent) == 0x10,
              "0x180008840 MOV [RSI+0x10],RDI");
static_assert(offsetof(PackfileIndexNode, left)  == 0x18,
              "GetPackfileId 0x1800ab050: `puVar5 = puVar5[3]` (left descent)");
static_assert(offsetof(PackfileIndexNode, right) == 0x20,
              "GetPackfileId 0x1800ab050: `puVar5 = puVar5[4]` (right descent)");
static_assert(offsetof(PackfileIndexNode, balance) == 0x28, "0x180025e10 balance");
static_assert(offsetof(PackfileIndexNode, height)  == 0x2a, "0x180025e10 height");

// The {root, count} header at FileSystem+0x40 (the insert 0x180025a80 takes
// `this+0x40`; ClosePackages 0x1800abbf0 zeroes +0x48). Raw storage: the
// FileSystem ctor zeroes both words by name and ClosePackages frees the tree.
struct PackfileIndexTree {
    PackfileIndexNode* root;    // +0x00
    int32_t            count;   // +0x08
    int32_t            _pad0c;  // +0x0c — never written
};
static_assert(sizeof(PackfileIndexTree) == 0x10, "the {root, count} header");
static_assert(offsetof(PackfileIndexTree, _pad0c) == 0xC, "PackfileIndexTree::_pad0c");
static_assert(offsetof(PackfileIndexTree, count) == 0x08, "count @ +0x08");

// ---- PackageFile — only the field GetPackfileId returns -------------------
// Wave 4 lane D modelled only +0xA8. Wave 9 lane PKG adds the four fields
// LoadPackages / ClosePackages touch; everything else is still opaque. Each
// offset below is pinned by the instruction that reads or writes it, and every
// one of them was also seen being INITIALISED by ??0PackageFile@IO@@QEAA@XZ
// (0x1800b4440), which is the independent second pin.
// Wave 9 lane PKG2 adds the eight fields the class's own accessors read. Each
// is pinned by the one-instruction body that reads it — every accessor named
// below is `MOV EAX,[RCX+off] ; RET` or the String AddRef idiom, so the offset
// is the whole function and there is nothing to misread.
// B.4 Z LAST IO: the `PackageFileView` POD that stood here is deleted: nothing
// read it but its own asserts and `IO::PackageFile`'s alias asserts, and the
// class (src/engine/io/PackageFile.h) pins every one of these offsets with
// the same instructions. Its two facts not repeated there: +0x58 / +0x60 are
// Strings by ~PackageFile's release idiom (0x1800b45c0 / 0x1800b4599), purpose
// unknown; +0x78 / +0x7c are the only two fields the ctor 0x1800b4440 does not
// write (ReadPackfile does).
// One entry of the +0xB0 table. Stride 16, pinned three independent ways:
// GetFileName's `SHL RDX,0x4`, GetFileOffset's `[RAX + R8*8 + 0x8]` after
// `ADD R8,R8` (= index*16 + 8) and GetFileSize's `+ 0xc` (= index*16 + 12).
struct PackageFileEntry {
    System::Filename name;         // +0x00 — the Filename GetFileName copies (B.4 Z LAST IO: the class)
    uint32_t         offset;       // +0x08 — ?GetFileOffset@ 0x1800254c0
    uint32_t         size;         // +0x0C — ?GetFileSize@   0x1800254a0
};
static_assert(sizeof(PackageFileEntry) == 16,
              "GetFileName 0x180025470: `SHL RDX,0x4` is the stride");
static_assert(offsetof(PackageFileEntry, name) == 0x0, "PackageFileEntry::name");
static_assert(offsetof(PackageFileEntry, offset) == 0x08,
              "GetFileOffset 0x1800254cd: `MOV EAX,dword ptr [RAX + R8*0x8 + 0x8]`");
static_assert(offsetof(PackageFileEntry, size)   == 0x0C,
              "GetFileSize 0x1800254ad: `MOV EAX,dword ptr [RAX + R8*0x8 + 0xc]`");


// ---- IO::FileSystem::FileInfo ---------------------------------------------
// Moved here from src/engine/io/filesystem_setupdirs.cpp by Wave 9 lane
// PKG2 rung 3, because a second translation unit now needs it and two
// namespace-scope `RtIO::FileInfo` definitions with different members would be
// a silent ODR violation rather than a compile error.
//
// **AND ONE FIELD IS CORRECTED IN THE MOVE.** Lane FS modelled +0x08 as a
// `uint64_t size`, which was a reasonable reading of the one instruction it had
// (`MOV qword [RBP-0x28],R15` zeroes +0x08 and +0x0C together). It is two
// separate dwords, and three independent sites say so:
//
//   * `?ReadFile@FileSystem@IO@@` reads +0x0C as a READ CURSOR and advances it
//     (`ADD dword [RDI+0xc],ESI` at 0x1800ace87, and again at 0x1800acf37);
//   * `?OpenFile@FileSystem@IO@@` writes +0x08 and +0x0C from two different
//     values in the same breath — `MOV [RDI+8],R9D` (the entry SIZE) then
//     `MOV [RDI+0xc],ECX` (the cursor, 0) at 0x1800acb94/98;
//   * the FileInfo COPY CTOR (0x18000c110) copies +0x00, +0x04, +0x08, +0x0C
//     and +0x10 as five separate DWORD moves.
//
// Nothing broke while it was wrong: lane FS's only reader is
// `static_cast<int>(static_cast<uint32_t>(fi.size))`, which takes the low dword
// and so happened to be right. It would not have stayed right for anyone who
// used the field as a size on a file opened from inside a package, where +0x0C
// is a live cursor rather than zero.
// B.4 Z LAST IO (`behaviour:`): the two names are real `System::Filename`
// members and FileInfo has retail's special members:
//   * the default ctor is the one every retail site inlines (??0FileStream
//     0x1800aaa10, ??0PackageFile 0x1800b44f1.., OpenFile 0x1800ac8b3,
//     CreateFileW 0x1800acc6c, CloseFile 0x1800acce3, PlatformOpenFile
//     0x1800add08): handle -1, size / cursor 0 (one qword), baseOffset 0, the
//     two Filename() null stores, THEN winHandle = INVALID_HANDLE_VALUE. +0x04
//     and +0x14 are never written;
//   * the copy ctor is 0x18000c110 (five dwords, the qword at +0x18, then the
//     Filename copy ctor 0x1800067d0 on +0x20 and +0x28; +0x14 not copied:
//     it is padding, not a field);
//   * operator= is 0x18000c200 (the same moves, then Filename::operator= on
//     both names);
//   * the dtor is 0x18000c180 (+0x28 released first, then +0x20: reverse
//     declaration order).
// No move operations (retail has none): the declared copy members suppress
// them, so an rvalue FileInfo is copied, as retail's code does.
struct FileInfo {
    // `handle`'s tags (see the field): no file, a loose OS file, else a packed
    // `(packIndex << kPackIndexShift) | fileIndex` encoding.
    static constexpr int32_t  kHandleNone       = -1;            // nothing opened (ctor, CloseFile guard)
    static constexpr int32_t  kHandleLoose      = -2;            // a loose OS file; the real handle is winHandle
    static constexpr uint32_t kHandleLastPacked = 0xFFFFFFFDu;   // largest packed tag (ReadFile CMP -3 ; JA loose)
    static constexpr int32_t  kPackIndexShift   = 24;            // the pack index is the top byte (OpenFile SHL 0x18)
    static constexpr uint32_t kFileIndexMask    = 0x00FFFFFFu;   // the entry index within the package
    // Retail's unsigned discrimination, spelled once: true for a packed tag,
    // false for kHandleNone and kHandleLoose.
    static constexpr bool IsPackedHandle(int32_t h) {
        return static_cast<uint32_t>(h) <= kHandleLastPacked;
    }

    FileInfo()
        : handle(kHandleNone), size(0), cursor(0), baseOffset(0) {
        winHandle = Platform::kInvalidFileHandle;   // INVALID_HANDLE_VALUE, stored after the names as retail does
    }
    // +0x04 (`_unk04`) is retail's unwritten window: the default ctor leaves it
    // whatever the stack held and the copy / assign move the byte as is
    // (B6 C8). With String's release inline (B7 RT) GCC's -O2 inliner can see
    // the stack temporary of `*fi = FileInfo()` and reports the copy of that
    // field as a use of an uninitialised value; it is retail's byte.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wuninitialized"
    FileInfo(const FileInfo&) = default;              // 0x18000c110
    FileInfo& operator=(const FileInfo&) = default;   // 0x18000c200
#pragma GCC diagnostic pop
    ~FileInfo() = default;                            // 0x18000c180

    // +0x00 — WHAT KIND OF FILE THIS IS, and it is not a plain handle:
    //   -1  nothing opened
    //   -2  a LOOSE file; the real Win32 HANDLE is at +0x18
    //   otherwise a PACKED file, encoded as `(packIndex << 24) | fileIndex`
    //     (OpenFile 0x1800ac9c5: `MOV R8D,[RBX+8] ; SHL R8D,0x18 ; OR R8D,[RBX+0xc]`).
    // ReadFile discriminates with `CMP dword [RDX],-3 ; JA loose`, i.e. unsigned
    // -- so -1 and -2 both take the loose path -- and recovers the pack index as
    // the TOP BYTE (`MOVZX R10D,byte [RDX+3]`).
    int32_t  handle;
    int32_t  _unk04;      // +0x04 — never initialised by OpenFile; see the note
                          //         in filesystem_openfile.cpp on why the
                          //         oracle must exclude it.
    int32_t  size;        // +0x08 — byte size of the file or of the pack entry
    int32_t  cursor;      // +0x0C — READ POSITION, advanced by ReadFile
    int32_t  baseOffset;  // +0x10 — a pack entry's offset within the .pkg; 0 for loose
                          // +0x14 — padding: no ctor, copy or assign writes it
    Platform::FileHandle winHandle;   // +0x18 — the OS file handle (Platform::kInvalidFileHandle when there is none);
                                      //         on Windows the value is the Win32 HANDLE
    System::Filename _name0;   // +0x20 — the copy ctor is ??0Filename@ 0x1800067d0
    System::Filename _name1;   // +0x28
};
static_assert(sizeof(FileInfo) == 0x30, "FileInfo is 0x30 bytes (OpenFile's frame)");
static_assert(offsetof(FileInfo, handle) == 0x0, "FileInfo::handle");
static_assert(offsetof(FileInfo, _unk04) == 0x4, "FileInfo::_unk04");
static_assert(offsetof(FileInfo, size)       == 0x08,
              "OpenFile 0x1800acb94: `MOV dword ptr [RDI+0x8],R9D` (the entry size)");
static_assert(offsetof(FileInfo, cursor)     == 0x0C,
              "ReadFile 0x1800ace87: `ADD dword ptr [RDI+0xc],ESI`");
static_assert(offsetof(FileInfo, baseOffset) == 0x10,
              "OpenFile 0x1800acb9b: `MOV dword ptr [RDI+0x10],EDX` (the entry offset)");
static_assert(offsetof(FileInfo, winHandle)  == 0x18,
              "PlatformOpenFile 0x1800adddd: `MOV qword ptr [RBX+0x18],RAX` (CreateFileW)");
static_assert(offsetof(FileInfo, _name0)     == 0x20, "OpenFile: MOV [RBP-0x10],0");
static_assert(offsetof(FileInfo, _name1)     == 0x28, "OpenFile: MOV [RBP-0x8],0");

// ---- Collection::Array<System::Filename>, as FindFiles' out-parameter ------
// DECLARED, NOT DEFINED, and deliberately so. The 16-byte {_data,_size,
// _capacity} header is DEFINED in src/engine/io/filesystem_find.cpp, with
// its own three `static_assert`s pinned to 0x180026880's instructions, and that
// file reaches it through `rtcoll::AsArray<&kSpec_Filename>` — a HAND-DERIVED
// element spec that deliberately does NOT match
// `rtcoll::SpecFor<System::Filename>()` (see the note at `kSpec_Filename`).
// Naming `Collection::Array<System::Filename>` in the declaration below would
// invite exactly the silent substitution that comment forbids, so
// `IO::FileSystem::FindFiles` takes a pointer to this incomplete type instead —
// which is all the declaration needs and all the retail signature pins.
//
// B.4 P2 R-IO: DEFINED here now, so FindFiles' four callers (FileSystem::
// LoadPackages, Modification::RefreshList, the save-list job and FindFilesImpl
// itself) hold the out-parameter as this one type instead of four local
// spellings cast to it. Still the plain 16-byte header, NOT
// `Collection::Array<System::Filename>` -- the hand-derived spec rule above
// stands. B.4 Z LAST IO: `data` points at real `System::Filename` elements
// (the header stays trivial; the elements are built by the row's assign and
// released by each caller's loop, as retail).
struct FilenameArray {
    System::Filename*   data;       // +0x00
    int32_t             size;       // +0x08
    int32_t             capacity;   // +0x0c
};
static_assert(sizeof(FilenameArray) == 0x10, "Collection::Array is 16 bytes");
static_assert(offsetof(FilenameArray, data) == 0x0, "FilenameArray::data");
static_assert(offsetof(FilenameArray, size)     == 0x08, "0x180026880: MOV EBP,[RCX+0x8]");
static_assert(offsetof(FilenameArray, capacity) == 0x0c, "0x180026880: CMP ESI,[RCX+0xc]");

// ---- IO::FileSystem: the per-offset evidence ------------------------------
// B.4 Z LAST IO: the `RtIO::FileSystem` POD that stood here is deleted (nothing
// named it but its own alias asserts); the class `IO::FileSystem` below holds
// the same bytes with real members. Its per-field evidence is kept here, as
// it was written against the POD's field names:
//
// Full-size fields at +0x00/+0x08 are String, NOT Filename:
// retail casts the pointer at the call site (`(Filename*)(this+8)`), it never
// declares the field as one. Filename and String are layout-identical (see
// src/engine/system/Filename.h), so the cast is free either way; the type
// used here just matches what the field actually is.
// struct FileSystem {
//     String sourceDirectory;    // +0x00 — GetSourceDirectory's `(Filename*)this`
//     String dataDirectory;      // +0x08 — GetDataDirectory's `this+8`; GetPackfileId's prefix
//     // +0x10 — the PACK directory, and the third field
//     // ?SetupDirectories@FileSystem@IO@@ (0x1800abd00) writes: it is seeded as
//     // a copy of dataDirectory (retail's inlined String::operator= at
//     // 0x1800ac1b0 with RSI = this+0x10, RDI = this+8) and then overridden by a
//     // `-pathpkg` command-line switch. `?GetPackDirectory@FileSystem@IO@@`
//     // (0x180007c10) is its reader. Named by Wave 9 lane FS; it was
//     // `_unknown10[8]` before, and nothing anywhere read that padding.
//     String packDirectory;
//     uint8_t          existsInPackfileGate; // +0x18 — Exists()'s `CMP byte [RCX+0x18],0`
//     uint8_t          platformExistsGate;   // +0x19 — Exists()'s `CMP byte [RBX+0x19],0`
//     uint8_t          _unknown1A[0x20 - 0x1A];
//     PackageFileView** packfiles;         // +0x20 — GetPackfileId's `*(this+0x20)`, array of PackageFile*
//     int32_t          packfileCount;      // +0x28 — read by ModProbe_DumpPackageIndex, not by this lane
//     // +0x2C — the packfiles array's CAPACITY. Named by Wave 9 lane PKG from
//     // LoadPackages' two inlined array-growth loops, which read it
//     // (`CMP R14D,dword ptr [RSI+0x2c]` at 0x1800ab5d4) and write it
//     // (`MOV dword ptr [RSI+0x2c],EAX` at 0x1800ab5f0). So +0x20/+0x28/+0x2C is
//     // a Collection::Array<PackageFile*> laid out exactly like every other
//     // Array in this engine (_data / _size / _capacity — see
//     // src/engine/collection/Collection.h); retail INLINES the growth here
//     // rather than calling Array::SetSize, which is why the three fields were
//     // never named together before.
//     int32_t          packfileCapacity;
//     // +0x30/+0x38 — the USER (mod) pack filenames, array + count.
//     //
//     // CORRECTION, Wave 9 lane PKG: this comment used to call +0x38 "the flag
//     // IO::FileSystem::AreUserPackFilesEnabled returns". It is not a flag, it is
//     // an int COUNT, and the whole of that accessor (0x1800acfc0, four
//     // instructions) is `XOR EAX,EAX / CMP dword ptr [RCX+0x38],EAX / SETG AL`
//     // — i.e. it returns `count > 0`. Both readings agree on the gate that
//     // BANISHED_ACH_IGNORE_MODS turns on, so nothing built on the old wording is
//     // wrong; but LoadPackages' second loop uses +0x38 as a LOOP BOUND
//     // (`CMP R14D,dword ptr [RSI+0x38]` at 0x1800ab864) while indexing +0x30,
//     // and a bool there would have been a one-iteration loop.
//     String* userPackFilenames;   // +0x30
//     int32_t           userPackFilenameCount; // +0x38
//     int32_t           _unknown3C;            // +0x3C — the matching capacity; unread here
//     PackfileIndexNode* packfileIndexRoot; // +0x40 — GetPackfileId's `*(this+0x40)`
//     // +0x48 — the node count of the BST rooted at +0x40, so +0x40/+0x48 is one
//     // tree object. Pinned twice: the insert helper at 0x180025c70 takes
//     // `this+0x40` as its tree and does `INC dword ptr [R14+0x8]` on every node
//     // it creates, and ClosePackages zeroes it (`MOV dword ptr [RSI+0x8],EBP`
//     // at 0x1800abbf0, with RSI = this+0x40).
//     int32_t          packfileIndexCount;
// };

}  // namespace RtIO

// ============================================================================
// ENDGAME B.3.4 PACKAGE+FS commit 4 — the REAL `IO::FileSystem`, PARTIAL
// ============================================================================
//
// Design: B34_RULES.md §1.5 rule 1, §4.3; B34_BRIEFS.md §E.4.
//
// *** IT HAS NO VTABLE. *** There is no `??_7FileSystem@IO@@6B@` in the retail
// export table, no row for it in `standalone/rt_data_mint_tables.inc` and no
// `deleting_dtor.cpp` row; every exported member is `QEAA`/`QEBA`/`AEBA`. So,
// like `IO::Serializer` and `IO::Modification`, converting a
// member here cannot change which table any live object wears.
//
// *** THIS DECLARATION IS DELIBERATELY PARTIAL, AND SO IS THE CONVERSION. ***
// `IO::FileSystem` has nineteen registered members spread over nine files, and
// nine of them sit on the OPEN and READ path that B34_BRIEFS §D binds to
// byte-faithfulness while a separate agent root-causes an intermittent NULL
// resource there. Those bodies are NOT moved in this commit. What IS declared
// below is the four members whose doors live in `src/engine/io/filesystem.cpp`
// and whose bodies are pure reads — plus the whole LAYOUT, which is the part
// the next owner needs before it can move anything else.
//
// STILL FREE FUNCTIONS, by file, for whoever finishes this class:
//   (GetSaveDirectory is a member in filesystem.cpp since Phase D.4 SETTINGS
//                                 E.2; its OS half is Platform::DocumentsDir in
//                                 src/platform/win/settings_win.cpp, and
//                                 filesystem_win.cpp, the file that held it,
//                                 is deleted.)
// Everything else in `src/engine/io/filesystem*.cpp` is a member of the class
// below. The NON-`IO::FileSystem` bodies that share those files stay free
// functions on purpose: `RtIo_FileInfo_CopyCtor` / `RtIo_FileInfo_Assign` are
// `FileInfo`'s, and `RtIo_PackageFile_GetFileInfo` / `RtIo_PackageFile_GetFileName`
// are `IO::PackageFile`'s — a POLYMORPHIC class whose conversion is its own
// commit with its own qualified-call rule (B34_BRIEFS §A.1).
//
// *** COMMIT 5a UPDATE (second PACKAGE+FS owner). *** The four NON-§D files are
// converted: `filesystem_setupdirs.cpp` (SetupDirectories, GetRootDirectory),
// `filesystem_startup.cpp` (Startup), `filesystem_timestamp.cpp`
// (GetFileTimeStamp) and `filesystem_createfile.cpp` (CreatePath,
// PlatformCreateFile, CreateFileW, PlatformCloseFile, CloseFile and the
// DESTRUCTOR).
//
// *** COMMIT 5b UPDATE. *** The three §D-BOUND files are converted too, as a
// SEPARATE commit and with their bodies moved VERBATIM — identical control
// flow, identical callees, identical statement order:
// `filesystem_openfile.cpp` (OpenFile, ReadFile, PlatformOpenFile),
// `filesystem_packages.cpp` (GetPackDirectory, LoadPackages, ClosePackages) and
// `filesystem_find.cpp` (ExistsInPackfile, PlatformExists, FindFiles). No
// lock, no retry, no positioned read and no null check was added there, and
// nothing that makes the path spin was removed: the two-loader race
// (LOADER_READ_SPIN.md) is a known defect with its own pending fix.
// The list above is what is LEFT.
//
// *** AND ONE FACT IN THE COMMIT-4 BANNER WAS WRONG, CORRECTED HERE. *** It said
// `??1FileSystem@IO@@` lives in `iobase_vtables.cpp` and is outside PACKAGE+FS's
// file set. It does not and it is not: `RtIo_FileSystem_Dtor` is, and always was,
// in `filesystem_createfile.cpp` (which registers 0x1800ac6d0 there), and
// `iobase_vtables.cpp` contains no occurrence of the string "FileSystem" at all
// (grepped). B34_RULES §6.2 carried the same wrong attribution and is corrected
// in the same commit. The destructor therefore DOES convert, and §4.3's "no real
// constructors" rule is not in tension with that: that rule exists to stop a
// ctor/dtor door swapping the vtable a live object wears, and `IO::FileSystem`
// HAS NO VTABLE — it is the same exemption `IO::Serializer` gets in §4.3's own
// table ("Serializer is NON-polymorphic. It has no vptr, so converting its ctor
// cannot change any table").
//
// THE CONSTRUCTOR (`??0FileSystem@IO@@QEAA@XZ`, 0x1800ac7c0) IS STILL NOT PORTED
// ANYWHERE IN THE TREE and is not invented here. `~FileSystem()` being real does
// not make the class default-constructible into a valid state; every live
// FileSystem is still the one the singleton points at.
//
// *** THE TWO CACHED SINGLETON POINTERS (§E.4) ARE NOT RECONCILED HERE, AND
// *** THAT IS ON PURPOSE. *** `engine/funcs/engine_funcs.h` caches the same
// exported data symbol `?_singleton@FileSystem@IO@@0PEAV12@EA` (0x1801fc6a8)
// TWICE — `g_pFileSystemSingleton` and `g_pFileSystemSingleton2`, the latter
// annotated "separate exref for GetDataDirectory calls". A real
// `FileSystem::Get()` has to reconcile them, and `engine/funcs/` is NOT in this
// block's file set, so no `Get()` is declared: inventing one here would create
// a third spelling of a pointer that already has two. Whoever adds it must
// check BOTH cells, not assume one.
//
// *** THE THREE DIRECTORIES ARE `System::Filename` MEMBERS (B4 X-R3, H5). ***
// Every reader treats them as Filenames: GetSourceDirectory (0x180007b90),
// GetDataDirectory (0x180007bd0) and GetPackDirectory (0x180007c10) pass the
// field's address to the Filename COPY ctor (0x1800067d0), SetupDirectories
// assigns them as Filenames and hands `&field` to the path override as a
// `Filename*`; the length reads are String::GetLength, which Filename has. So
// the class holds them as Filenames, and the String->Filename view cast those
// readers needed (`SysFile`) goes.
// Retail's ctor 0x1800ac7c0 stores +0x00, +0x08, +0x10 = 0 inline (the default
// ctor's null store; `Ctor_FileSystem` in engine/singleton_ctors.cpp transcribes
// those three stores); retail's dtor 0x1800ac6d0 frees the index tree, the two
// arrays, THEN releases +0x10, +0x08, +0x00 with the inlined String release
// (0x1800ac720..0x1800ac79c, each under its own unwind state: member
// destructors; ~Filename 0x180006980 is that same release). Declared as
// Filenames, the compiler destroys the three after the
// dtor body in reverse declaration order: exactly that order, and the dtor no
// longer releases them by hand (one release, the compiler's).
// ============================================================================

namespace IO {

// The packfile array's element (src/engine/io/PackageFile.h, which includes
// this header, so the class is declared here rather than included).
class PackageFile;

class FileSystem {
public:
    // ?Exists@FileSystem@IO@@QEBA_NAEBVFilename@System@@@Z        0x1800ab310
    bool Exists(const System::Filename& path) const;

    // ?GetDataDirectory@FileSystem@IO@@QEBA?AVFilename@System@@XZ 0x180007bd0
    System::Filename GetDataDirectory() const;

    // ?GetSaveDirectory@FileSystem@IO@@QEBA?AVFilename@System@@XZ 0x1800acfd0
    // ENDGAME B.3.4a closing: the last FileSystem member to move. Unlike the
    // two directory getters above it reads NO field of `this` at all — retail
    // loads the pointer and never dereferences it (filesystem.cpp's comment on
    // the body records that from the disassembly) — so the body is a
    // Platform::DocumentsDir call, not a copy ctor. `const` matches retail's `QEBA`;
    // the by-value Filename return is the hidden-retslot ABI, as above.
    System::Filename GetSaveDirectory() const;

    // ?GetSourceDirectory@FileSystem@IO@@QEBA?AVFilename@System@@XZ 0x180007b90
    // Retail's `MOV RDX,RCX` passes `this` ITSELF to the Filename copy ctor,
    // which is only possible because FileSystem+0x00 IS a String/Filename
    // object. That is the strongest pin this header has on +0x00.
    System::Filename GetSourceDirectory() const;

    // ?GetPackfileId@FileSystem@IO@@QEBAIAEBVFilename@System@@@Z  0x1800ab050
    uint32_t GetPackfileId(const System::Filename& path) const;

    // ---- commit 5a: the members whose doors live in the four NON-§D files --
    // Each keeps retail's name; the door keeps its own name, its registration
    // lines and its signature (several are called cross-file by
    // file_stream.cpp, text_file_stream.cpp, persistence_saveinfo_job.cpp
    // and persistence_worker.cpp) and delegates here.

    // -- filesystem_setupdirs.cpp ---------------------------------------
    // ?GetRootDirectory@FileSystem@IO@@QEAA?AVFilename@System@@XZ 0x1800ad650
    // NON-const (`QEAA`), and retail never reads `this` — kept as the signature
    // says, exactly as the free function kept its unused `self`.
    System::Filename GetRootDirectory();
    // ?SetupDirectories@FileSystem@IO@@QEAAXXZ                    0x1800abd00
    void SetupDirectories();

    // -- filesystem_startup.cpp ---------------------------------------------
    // ?Startup@FileSystem@IO@@QEAA_NXZ                            0x1800abc50
    bool Startup();

    // -- filesystem_timestamp.cpp ---------------------------------------
    // ?GetFileTimeStamp@FileSystem@IO@@QEBA?AVDateTime@System@@AEBVFilename@4@@Z
    //                                                             0x1800ae270
    // Retail returns `System::DateTime` BY VALUE (Value.h: the raw FILETIME,
    // two dwords).
    System::DateTime GetFileTimeStamp(const System::Filename& path) const;

    // -- filesystem_createfile.cpp --------------------------------------
    // ?CreatePath@FileSystem@IO@@QEBA_NAEBVFilename@System@@@Z     0x1800ae430
    bool CreatePath(const System::Filename& path) const;
    // ?PlatformCreateFile@FileSystem@IO@@AEAA_NAEAUFileInfo@12@AEBVFilename@System@@@Z
    //                                                             0x1800ade60
    // ?PlatformCloseFile@FileSystem@IO@@AEAAXAEAUFileInfo@12@@Z    0x1800ae020
    // Both are `AEAA` — PRIVATE in retail, and this DLL exports its private
    // members (reference_aeaa_is_private_not_unexported.md). They are declared
    // PUBLIC here because each has its OWN registered door (0x1800ade60 and
    // 0x1800ae020 are separately registered by name and by address in
    // filesystem_createfile.cpp) and a door is a free function, which a
    // private member would not let in. Making the doors `friend`s instead would
    // hide a real fact — that these two are reachable from outside the class in
    // our tree — behind a keyword.
    bool PlatformCreateFile(RtIO::FileInfo& info, const System::Filename& path);
    void PlatformCloseFile(RtIO::FileInfo& info);
    // ?CreateFileW@FileSystem@IO@@QEAA?AUFileInfo@12@AEBVFilename@System@@@Z
    //                                                             0x1800acc50
    // Retail returns `FileInfo` BY VALUE through a hidden return slot (B.4 Z
    // LAST IO: FileInfo is a real class, so the return is retail's: the slot
    // is default-constructed in place, 0x1800acc6c..91).
    RtIO::FileInfo CreateFileW(const System::Filename& path);
    // ?CloseFile@FileSystem@IO@@QEAAXAEAUFileInfo@12@@Z            0x1800accc0
    void CloseFile(RtIO::FileInfo& info);
    // ---- commit 5b: the members whose doors live in the THREE §D-BOUND files
    // B34_BRIEFS §D binds `filesystem_openfile.cpp`, `filesystem_packages.cpp`
    // and `filesystem_find.cpp` to byte-faithfulness while the two-loader
    // race (LOADER_READ_SPIN.md) has its own pending fix. These nine bodies
    // moved VERBATIM — identical control flow, identical callees, identical
    // statement order. No lock, no retry, no positioned read and no null check
    // was added, and nothing that makes the path spin was removed.

    // -- filesystem_openfile.cpp ----------------------------------------
    // ?OpenFile@FileSystem@IO@@QEAA?AUFileInfo@12@AEBVFilename@System@@@Z
    //                                                             0x1800ac880
    // Retail returns FileInfo by value (hidden return slot), as CreateFileW.
    RtIO::FileInfo OpenFile(const System::Filename& name);
    // ?ReadFile@FileSystem@IO@@QEAA_NAEAUFileInfo@12@HPEAX@Z       0x1800ace00
    // RECURSIVE (0x1800ace7e re-enters itself with the containing package's
    // FileInfo). `ReadFile` is a plain function name in <windows.h>, not a
    // macro, so the member name does not collide; the Win32 call inside the
    // body is spelled `::ReadFile` and always was.
    bool ReadFile(RtIO::FileInfo& fi, int32_t n, void* dst);
    // NOT RETAIL (MAPREAD, the banner above ReadFile in filesystem_openfile.cpp): for a packed
    // `fi`, the mapped bytes a ReadFile of `n` bytes at cursor `cursor` would copy, when that
    // read would be served from the view IN FULL (got == n); null otherwise (loose, nested,
    // not mapped, a failing seek, short at end of file). ext.decode_ahead reads a block's
    // compressed bytes through it; PackedMapNoteRead then leaves the emulated file pointer
    // where that ReadFile would have left it.
    const uint8_t* PackedMapSpan(const RtIO::FileInfo& fi, int32_t cursor, int32_t n);
    void PackedMapNoteRead(const RtIO::FileInfo& fi, int32_t cursor, int32_t n);
    // NOT RETAIL (ext.create_on_demand, docs/extensions/handins/CREATEONDEMAND.md): where the bytes
    // of a packed `fi` live: its package's OS handle and the entry's absolute offset in that file
    // (the offset ReadFile's packed arm reads cursor 0 at, wide or not). False for a loose file, a
    // nested package, an index past the package table or an offset retail could not seek to.
    bool PackedEntryOrigin(const RtIO::FileInfo& fi, Platform::FileHandle* package, int64_t* offset) const;
    // ?PlatformOpenFile@FileSystem@IO@@AEAA_NAEAUFileInfo@12@AEBVFilename@System@@@Z
    //                                                             0x1800adce0
    // `AEAA`, private in retail, public here for the door reason above.
    bool PlatformOpenFile(RtIO::FileInfo& fi, const System::Filename& name);

    // -- filesystem_packages.cpp --------------------------------------------
    // ?GetPackDirectory@FileSystem@IO@@QEBA?AVFilename@System@@XZ  0x180007c10
    System::Filename GetPackDirectory() const;
    // ?ClosePackages@FileSystem@IO@@QEAAXXZ                        0x1800abab0
    void ClosePackages();
    // ?LoadPackages@FileSystem@IO@@QEAAXXZ                         0x1800ab460
    void LoadPackages();

    // -- filesystem_find.cpp --------------------------------------------
    // ?ExistsInPackfile@FileSystem@IO@@QEBA_NAEBVFilename@System@@@Z 0x1800ab180
    bool ExistsInPackfile(const System::Filename& path) const;
    // ?PlatformExists@FileSystem@IO@@AEBA_NAEBVFilename@System@@@Z   0x1800adc70
    // `AEBA`, private in retail; the `QEBA` spelling is not an export at all
    // and would resolve NULL forever. Public here for the door reason above.
    bool PlatformExists(const System::Filename& path) const;
    // ?FindFiles@FileSystem@IO@@QEBAXAEBVString@System@@0_NAEAV?$Array@VFilename@System@@@Collection@@@Z
    //                                                             0x1800ad7a0
    // The out-parameter is `RtIO::FilenameArray`, declared incomplete above;
    // that comment says why it is not `Collection::Array<System::Filename>`.
    // The two String parameters keep the POD spelling the registered door has,
    // because the door IS this body and the recursion lives in the file-local
    // `FindFilesImpl`, which is where the class spellings already are.
    void FindFiles(const System::String& dir, const System::String& pattern,
                   bool recursive, RtIO::FilenameArray* out) const;

    // ??1FileSystem@IO@@QEAA@XZ                                    0x1800ac6d0
    // A REAL destructor — see the banner. It is `QEAA`, non-virtual, and the
    // class has no vtable, so converting it cannot change what any live object
    // wears. There is deliberately NO constructor: 0x1800ac7c0 is unported.
    ~FileSystem();

    // ?_singleton@FileSystem@IO@@0PEAV12@EA (0x1801fc6a8) -- the class static
    // (ENDGAME B.3.9 IO, B39_RULES D7). Defined in filesystem.cpp;
    // RtIoPortInit redirects the minted row onto it, so the name door, the
    // address door, the manifest publish and this member are the same eight
    // bytes. Written by the ??0FileSystem door (engine/singleton_ctors.cpp).
    // PUBLIC while foreign readers still reach it (retail: private `0`).
    static FileSystem* _singleton;

    // ---- fix.large_packages (NOT RETAIL, docs/extensions/handins/LARGEPKM.md) ------
    // Engine-side seams (DESIGN.md hook rule 5), null by default and under the
    // faithful profile. Retail reads a packed entry at the int32
    // `baseOffset + cursor` (ReadFile 0x1800ace43), so a package past 2 GiB
    // fails or aliases; the fix gives such a package 64-bit entry offsets.
    //
    // _extPackagesMounted: run at the end of LoadPackages (every mount and
    // remount), when _packfiles has its final pack-index order. It sets
    // _extWidePackages / _extWideCount. ClosePackages clears those two.
    static void (*_extPackagesMounted)(FileSystem& fs);
    // One per pack index below _extWideCount: that package's absolute entry
    // offsets (`count` of them), or { nullptr, 0 } for a package that keeps
    // retail's int32 read. Owned by the fix (src/ext/large_packages/).
    struct ExtWidePackage {
        const int64_t* offsets;
        int32_t        count;
    };
    static const ExtWidePackage* _extWidePackages;
    static int32_t               _extWideCount;

    // ---- ext.package_cache (NOT RETAIL, docs/extensions/handins/PKGCACHE.md) -------
    // Engine-side seams (DESIGN.md hook rule 5), null by default and under the faithful profile.
    // _extIndexRestore: run by LoadPackages once _packfiles is final, before the loop that builds
    // _packfileIndex. true = the extension built the identical tree (and its fold index) from the
    // derived cache, and the loop does not run. _extIndexBuilt: run after the loop built the tree.
    static bool (*_extIndexRestore)(FileSystem& fs);
    static void (*_extIndexBuilt)(FileSystem& fs);

    // ---- ext.menu_entry (NOT RETAIL, docs/extensions/handins/MENUENTRY.md) ----------------------
    // Engine-side seam (DESIGN.md hook rule 5), null by default and under the faithful profile: run
    // at the end of every ClosePackages (LoadPackages' own included). The extension keeps a
    // fingerprint of the packages the last remount mounted; a close from anywhere but that
    // remount drops it.
    static void (*_extPackagesClosed)(FileSystem& fs);

    // ---- ext.create_on_demand (NOT RETAIL, docs/extensions/handins/CREATEONDEMAND.md) ----------
    // Engine-side seam (DESIGN.md hook rule 5), null by default and under the faithful profile:
    // run by PlatformCloseFile (through PackedMapRelease) for every handle it is about to close,
    // before CloseHandle. The extension keeps its own read-only mappings of the packages; a handle
    // being closed is never used to find one again (the OS may hand the value out anew).
    static void (*_extHandleClosing)(Platform::FileHandle h);

    // The absolute offset the packed arm of ReadFile seeks to for `fi` (a
    // PACKED FileInfo: handle = packIndex << 24 | fileIndex). With a wide
    // table for its package, the entry's 64-bit offset + cursor, `*wide` =
    // true. Otherwise retail's int32 `baseOffset + cursor`, sign-extended
    // (negative = the seek retail's SetFilePointer refuses; past 4 GiB it has
    // already wrapped, as in retail), `*wide` = false. Shared with the
    // mapped read (block readpath, issue #13).
    int64_t PackedReadOffset(const RtIO::FileInfo& fi, bool* wide) const;

    // ---- the pinned fields, at retail's offsets ---------------------------
    // PUBLIC for the same reason `IO::Serializer`'s two are: bodies outside
    // the class and the exe-side probes read these fields, and `offsetof` on
    // them has to be well formed for the pins below. The per-offset evidence
    // is the commented table above (the deleted POD's).
    System::Filename   _sourceDirectory;       // +0x00  ctor 0x1800ac7d8, dtor 0x1800ac775 (see the banner)
    System::Filename   _dataDirectory;         // +0x08  ctor 0x1800ac7e4, dtor 0x1800ac74a
    System::Filename   _packDirectory;         // +0x10  ctor 0x1800ac7f0, dtor 0x1800ac720
    uint8_t            _existsInPackfileGate;  // +0x18
    uint8_t            _platformExistsGate;    // +0x19
    uint8_t            _unknown1A[0x20 - 0x1A];
    // B.4 Y RT (`behaviour:`): the real Array. Retail's dtor 0x1800ac6d0 frees
    // +0x40 and +0x30 (the body), then ~Array<PackageFile*> on +0x20
    // (0x1800ac71a, J1: free + zero header), then the three Filenames: this
    // member's own ~Array, after the body and before the Filenames declared
    // above it. PackageFile* is trivially destructible: no element step.
    Collection::Array<PackageFile*> _packfiles; // +0x20  size +0x28, capacity +0x2c
    // B.4 Z RT2: the Array<Filename> as the typed 16-byte header (POD: no ctor /
    // dtor; the ctor 0x1800ac7c0 zeroes its three fields by name, the dtor
    // releases it through ~Array<Filename> 0x1800098d0).
    RtIO::ModArrayOf<System::Filename> _userPackFilenames;   // +0x30 {data, size +0x38, capacity +0x3c}
    RtIO::PackfileIndexTree _packfileIndex;     // +0x40 root, +0x48 count
};
static_assert(offsetof(FileSystem, _unknown1A) == 0x1A, "FileSystem::_unknown1A");

// The POD carries no `sizeof` assert because its tail is opaque padding and
// nothing pins the total size; the class inherits that honestly. A class MAY
// keep opaque padding, but it may never invent a field and may never shrink or
// grow the struct — so every offset below is the POD's, cross-asserted.
static_assert(offsetof(FileSystem, _sourceDirectory)      == 0x00, "GetSourceDirectory's `(Filename*)this`");
static_assert(offsetof(FileSystem, _dataDirectory)        == 0x08, "GetDataDirectory's `this+8`");
static_assert(offsetof(FileSystem, _packDirectory)        == 0x10, "GetPackDirectory 0x180007c10");
static_assert(offsetof(FileSystem, _existsInPackfileGate) == 0x18, "Exists' `CMP byte [RCX+0x18],0`");
static_assert(offsetof(FileSystem, _platformExistsGate)   == 0x19, "Exists' `CMP byte [RBX+0x19],0`");
static_assert(offsetof(FileSystem, _packfiles)            == 0x20, "GetPackfileId's `*(this+0x20)`");
static_assert(offsetof(FileSystem, _packfiles) + offsetof(Collection::Array<PackageFile*>, _size) == 0x28,
              "the packfile Array's size");
static_assert(offsetof(FileSystem, _packfiles) + offsetof(Collection::Array<PackageFile*>, _capacity) == 0x2c,
              "LoadPackages 0x1800ab5d4/5f0");
static_assert(offsetof(FileSystem, _userPackFilenames)    == 0x30, "the user pack filenames");
static_assert(offsetof(FileSystem, _packfileIndex) + offsetof(RtIO::PackfileIndexTree, root) == 0x40, "GetPackfileId's `*(this+0x40)`");
static_assert(offsetof(FileSystem, _packfileIndex) + offsetof(RtIO::PackfileIndexTree, count) == 0x48, "ClosePackages 0x1800abbf0");


static_assert(!std::is_polymorphic<FileSystem>::value,
              "IO::FileSystem must never acquire a vtable: retail has none for it, and a "
              "vptr would push every field eight bytes down under nineteen doors");
static_assert(std::is_standard_layout<FileSystem>::value,
              "the doors reach these fields through a reinterpret_cast");

// ---- PERF (NOT RETAIL, issue #13 MAPREAD): mapped package reads ------------------
// FileSystem::ReadFile's packed arm reads a package through a read-only mapping of the
// file behind the package's handle (Platform::FileMapOpen), made on the first packed
// read from that handle and kept in a side table in filesystem_openfile.cpp (the
// pinned FileSystem / PackageFile layouts gain no field). PlatformCloseFile drops a
// handle's mapping just before it closes the handle, so ClosePackages (every remount)
// unmaps every package. Not retail: an exact perf body, the same bytes and the same
// results as SetFilePointer + ReadFile (the banner above ReadFile).
void PackedMapRelease(Platform::FileHandle h);

}  // namespace IO

// ==== B3R W0-P0c: Runtime bodies reached by retail address ====
// ENDGAME B.3R (B3R_RULES R5): each body below is registered at the retail
// address in its trailing comment and read through the registry today. It is
// declared here, copied from its definition (linkage, namespace, types), so the
// reader can call it by name. Nothing calls it through this declaration yet.

namespace RtIO {
// ---- io/filesystem_find.cpp
void RtColl_ArraySetSizeFill_180026880(void* a, int n, const void* fill);  // 0x180026880
}  // namespace RtIO
// ==== end B3R W0-P0c ====
