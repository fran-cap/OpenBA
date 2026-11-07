// ============================================================================
// src/engine/io/filesystem_openfile.cpp — Wave 9 lane PKG2, rung 3
// IO::FileSystem::OpenFile / ReadFile / PlatformOpenFile / CloseFile helpers
// ============================================================================
//
//   | Export                                                | Retail      | Ord  | Instrs |
//   |-------------------------------------------------------|-------------|------|--------|
//   | ?OpenFile@FileSystem@IO@@QEAA?AUFileInfo@12@AEBV…@Z   | 0x1800ac880 | 3309 |  264   |
//   | ?ReadFile@FileSystem@IO@@QEAA_NAEAUFileInfo@12@HPEAX@Z| 0x1800ace00 | 3460 |   90   |
//   | ?PlatformOpenFile@FileSystem@IO@@AEAA_NAEAU…@Z        | 0x1800adce0 | 3384 |  101   |
//   | ?GetFileInfo@PackageFile@IO@@QEBA?AUFileInfo@…@XZ     | 0x180025400 | 2601 |   15   |
//
// plus the unexported FileInfo copy constructor at `0x18000c110` (29 instrs),
// which `GetFileInfo` is a three-line wrapper around.
//
// Names and ordinals printed out of Runtime-x64.dll's export directory with
// `pefile`. `tools/export_name_audit.py` re-checks them.
//
//
// This file was `filesystem_openfile_win.cpp` until Phase D.4 THREADS E.5: the
// bodies below call CreateFileW, ReadFile, SetFilePointer and GetFileSize, now
// through Platform::FileOpenRead / FileRead / FileSeek32 / FileSize
// (platform_file.h, the same argument blocks). All four Win32 entry points were
// resolved out of the RUNTIME DLL's own import table with `pefile`, not guessed
// from the call shape — `tools/dispatch_slot_map.py` is for the EXE's imports
// and reports these as "NOT AN IMPORT SLOT":
//
//     0x18014b320 CreateFileW   0x18014b110 ReadFile
//     0x18014b0f0 SetFilePointer 0x18014b0f8 GetFileSize
//
// ---------------------------------------------------------------------------
// 1. WHAT `FileInfo::handle` ACTUALLY IS — the fact the whole rung turns on
// ---------------------------------------------------------------------------
// It is not a handle. It is a discriminated tag:
//
//     -1   nothing opened
//     -2   a LOOSE file; the real Win32 HANDLE is at +0x18
//     else a PACKED file, encoded `(packIndex << 24) | fileIndex`
//
// OpenFile builds that word with `MOV R8D,[RBX+8] ; SHL R8D,0x18 ; OR R8D,[RBX+0xc]`
// (0x1800ac9c5) straight out of the packfile-index BST node lane PKG's
// LoadPackages fills in. ReadFile takes it apart with `MOVZX R10D,byte [RDX+3]`
// — **the pack index is recovered as the top BYTE**, which only works because
// the shift is 24 and is why the field cannot be modelled as an opaque handle.
//
// The discrimination is UNSIGNED: `CMP dword [RDX],-3 ; JA loose`. -1 and -2 are
// both above 0xFFFFFFFD, so both take the loose path; every real pack encoding
// is below it. A signed test would send every pack whose index is >= 0x80 down
// the wrong path, and there is no such pack today — which is exactly the kind of
// thing that works until it does not.
//
// ---------------------------------------------------------------------------
// 2. OpenFile's SHAPE
// ---------------------------------------------------------------------------
//   key = name->data + strlen(this->dataDirectory)      // STRIP the prefix
//   if (strlen(name) > strlen(dataDirectory)) {
//       String k; k.Set(key); Filename::AdjustDelimiters(&k);
//       node = <BST walk of this->packfileIndexRoot, _stricmp>;
//       release(k);
//       if (node && this->existsInPackfileGate) {
//           lf.handle     = (node->packIndex << 24) | node->fileIndex;
//           e             = &packfiles[node->packIndex]->entries[node->fileIndex];
//           lf.size       = e->size;      // +0xC of the 16-byte entry
//           lf.baseOffset = e->offset;    // +0x8
//           lf.cursor     = 0;
//           goto emit;
//       }
//   }
//   if (!this->platformExistsGate) { *ret = <empty>; goto done; }
//   if (!PlatformOpenFile(this, &lf, name)) { *ret = <empty>; goto done; }
//   lf.handle = -2;
// emit:
//   *ret = lf (field by field), Filename-copy the two names
//
// **THE PACK-ENTRY READS ARE AN INDEPENDENT CONFIRMATION OF RUNG 1'S LAYOUT.**
// `PackageFileEntry` was pinned in iteration 1 from `?GetFileSize@`/`?GetFileOffset@`
// (`+0xC` and `+0x8`, stride 16). OpenFile reaches the same two dwords by a
// completely different route — `[pkg+0xb0] + fileIndex*16`, at `+0xc` and `+0x8`
// (0x1800ac9ee / 0x1800aca0c) — and agrees. Two unrelated functions, one layout.
//
// ---------------------------------------------------------------------------
// 3. ReadFile IS RECURSIVE, and that is the elegant part
// ---------------------------------------------------------------------------
//   if ((uint32)fi->handle <= 0xFFFFFFFD) {          // PACKED
//       FileInfo pack = packfiles[fi->handle >> 24]->GetFileInfo();
//       int abs = fi->baseOffset + fi->cursor;
//       if ((uint32)pack.handle > 0xFFFFFFFD)        // the .pkg is itself loose
//           if (SetFilePointer(pack.winHandle, abs, 0, FILE_BEGIN) != abs) fail;
//       ok = ReadFile(this, &pack, n, dst);          // <-- ITSELF
//       if (ok) fi->cursor += n;
//       ~pack;
//       return ok;
//   }
//   // LOOSE
//   DWORD got = 0;
//   ::ReadFile(fi->winHandle, dst, n, &got, NULL);
//   fi->cursor += got;
//   return got == n;
//
// So a packed read seeks the containing .pkg's handle and then re-enters itself
// with the PACK's FileInfo, which takes the loose branch. One body, both cases.
// A port that flattened the recursion into an if/else would be equivalent today
// and would stop being equivalent the moment a pack lived inside a pack.
//
// ---------------------------------------------------------------------------
// 4. THE UNINITIALISED FIELD, and what it does to the oracle
// ---------------------------------------------------------------------------
// OpenFile's local FileInfo initialises +0x00, +0x08, +0x0C, +0x10, +0x18, +0x20
// and +0x28 — **not +0x04**. The emit block then copies it out anyway:
// `MOV EAX,[RBP-0x2c] ; MOV [RDI+4],EAX` (0x1800acb8e). Retail returns a dword
// of its own stack garbage in every FileInfo it produces.
//
// That is faithful and it is reproduced. It also means **the oracle must not
// compare +0x04** — ours and theirs have different stacks, so it differs on
// every call and would report a 100% divergence
// (reference_oracle_must_not_compare_unwritten_bytes.md). The exclusion is
// stated at the comparison site, not hidden in a memcmp length.
//
// ---------------------------------------------------------------------------
// 5. WHAT IS NOT PORTED
// ---------------------------------------------------------------------------
//   ?CloseFile@FileSystem@IO@@   0x1800accc0, ord 1726 — reached through the
//       proper door. It is the teardown half and lane PKG's ClosePackages
//       already calls it; porting the OPEN half without it is deliberate, and it
//       is named in the registration block rather than silently absent.
//   FUN_18000c200 — FileInfo::operator=, used by PlatformOpenFile to reset the
//       caller's FileInfo. Called by ADDRESS through ResolveAddr, which is the
//       proper door for an unexported internal, and named below.
//   FUN_18007eefc — the CRT `_stricmp` OpenFile's BST walk needs. Same address
//       lane PKG reaches for `IndexFind`; it is one of the four doors the
//       integrator's address-door census flagged, and this rung ADDS a second
//       live caller of it rather than removing the first.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdio>

#include "rt_libm.h"        // rtcrt::StrICmp
#include "platform_file.h"    // Platform::FileOpenRead / FileRead / FileSeek32 / FileSize
#include "io/FileSystem.h"
#include "io/PackageFile.h"   // ENDGAME B.3.4a closing: the two members
#include "system/String.h"
#include "system/Filename.h"
#include "system/WideString.h"
#include "system/string_internal.h"
#include "io/packfile_fold_index.h"   // block PATHINDEX: the hash index beside _packfileIndex

#include <new>        // placement new into a raw FileInfo Filename slot
#include <atomic>     // MAPREAD: the emulated file pointer
#include <mutex>      // MAPREAD: the side table's lock

// ENDGAME B.3.1 step 2: this file no longer forward-declares lane String's
// `RtSystem_Filename_AdjustDelimiters` (0x1800c6970). Every call it made was
// the tail of a mutating Filename entry point — the `const char*` ctor
// (0x180026cf0) or `operator=(const String&)` (0x180026d40) — and the class
// spells those as themselves, with AdjustDelimiters private inside them.

namespace RtIO {


namespace {

// The CRT `_stricmp` OpenFile's BST walk uses: 0x18007eefc, rtcrt::StrICmp
// (src/engine/crt/msvc_stricmp.cpp), called by name (B3R RT-IO).
// It must NOT be replaced by std::strcasecmp or shared with
// src/engine/system/configuration.cpp's comparator: 0x18007eec0
// (`__ascii_stricmp`, the tail it jumps to) lowers only 'A'-'Z' and compares
// UNSIGNED, while the config map inlines a SIGNED-char strcmp. They disagree on
// any byte >= 0x80 and a package path can contain one.
int PathStrICmp(const char* a, const char* b) {
    return rtcrt::StrICmp(a, b);
}

// FileInfo::operator= is FileInfo's own (src/engine/io/FileSystem.h).

inline int32_t StrLenOf(const char* data) {
    // retail: `if (p) len = [p-8] else len = 0` — the String block's length word.
    return data ? RtSystem::StringLength(data) : 0;
}

}  // namespace

// ============================================================================
// FileInfo's copy constructor (0x18000c110) and operator= (0x18000c200)
// ============================================================================
// B.4 Z LAST IO: the two hand-written bodies that stood here
// (`RtIo_FileInfo_CopyCtor` / `RtIo_FileInfo_Assign`) are FileInfo's own
// implicit members now (src/engine/io/FileSystem.h): five dword moves, the
// qword at +0x18, then the Filename copy ctor (0x1800067d0) / Filename::
// operator= on +0x20 and +0x28; +0x14 is padding and not copied.

// ============================================================================
// ?GetFileInfo@PackageFile@IO@@QEBA?AUFileInfo@FileSystem@2@XZ  retail 0x180025400
// ============================================================================
// `return FileInfo(this->_fileInfo)`, where `_fileInfo` is the PackageFile's own
// embedded FileInfo at +0xC0 — the same +0xC0 lane PKG's ClosePackages hands to
// CloseFile, which is the independent confirmation of the offset.
// The body is `IO::PackageFile::GetFileInfo()` (src/engine/io/PackageFile.h
// declares it; the definition is in filesystem_packages.cpp beside GetFileName).
// B.4 R-IO: the `RtIo_PackageFile_GetFileInfo` door is gone; ReadFile below,
// its one caller, calls the member on the typed `_packfiles` element.

// ============================================================================
// ?PlatformOpenFile@FileSystem@IO@@AEAA_NAEAUFileInfo@12@AEBV…@Z  0x1800adce0
// ============================================================================
// ENDGAME B.3.4 PACKAGE+FS commit 5b: the three IO::FileSystem bodies in this
// file move onto the class VERBATIM; the doors keep their names, their
// `extern "C"` linkage, their signatures and every registration line, and
// delegate in one line each. No qualified `IO::` call is needed: IO::FileSystem
// has no vtable at all, so B34_BRIEFS section A.1's re-entry hazard cannot
// arise. This file is bound by B34_BRIEFS section D — nothing was added,
// removed or reordered, and in particular NO lock, NO retry, NO positioned read
// and NO null check was introduced on the open/read path. The two-loader race
// LOADER_READ_SPIN.md describes has its own pending fix and it is not this one.

// B.4 R-IO: the `RtIo_FileSystem_ReadFile` / `RtIo_FileSystem_OpenFile` doors
// that stood here are gone; every caller calls the member on
// `IO::FileSystem::_singleton`.

}  // namespace RtIO

// ============================================================================
// `IO::FileSystem`'s member bodies for this file — B34_BRIEFS section D BINDING
// ============================================================================
// Defined outside `namespace RtIO` because a member of `IO::X` must appear in a
// namespace ENCLOSING the class (B34_BRIEFS section A.2); these bodies reach
// this translation unit's `RtIO`-scoped names — including the
// anonymous-namespace helpers `PathStrICmp` and `StrLenOf`
// — as `RtIO::X`. Every POD is spelled `RtIO::` or `RtSystem::` and every real
// class `System::` (B34_RULES section 7.2).
//
// *** VERBATIM. *** Identical control flow, identical callees, identical
// statement order, identical comments. The only changes are the receiver's
// spelling (`self->field` to `_field`) and three call sites that were ALREADY
// direct calls within this file and are now direct MEMBER calls of the same
// bodies: ReadFile's recursion into itself (0x1800ace7e) and OpenFile's call to
// PlatformOpenFile (0x1800acace). Neither ever went through a resolve door, so
// neither changes which body runs.
namespace IO {

bool FileSystem::PlatformOpenFile(RtIO::FileInfo& fiRef,
                                  const System::Filename& name) {
    RtIO::FileInfo* const fi = &fiRef;
    // retail never reads `this` here

    // An empty FileInfo, assigned over the caller's — retail builds it on the
    // stack and calls FileInfo::operator= rather than clearing in place, which
    // is what releases whatever the caller's Strings were holding.
    // `*fi = FileInfo();` — FileInfo's default ctor (1800add08..1800add30: the
    // four fields, the two Filename() null stores, winHandle), operator=
    // (1800add3f), then ~FileInfo on the temporary (1800add45 +0x28, 1800add75
    // +0x20). B.4 Z LAST IO: the temporary's +0x04 is no longer zeroed (retail
    // leaves it whatever the stack held, §4; the oracle excludes it).
    *fi = RtIO::FileInfo();

    // WideString ws(*name); CreateFileW(ws, ...). ??0WideString(const String&);
    // the block ends at retail's 1800adde1 release, so ~WideString runs there.
    Platform::FileHandle h;
    {
        const System::WideString ws(name.AsString());        // 1800add98 / 1800addad

        // 1800addb3..1800addd7 — CreateFileW with the argument block, transcribed
        // rather than named from habit: dwDesiredAccess 0x80000000 (GENERIC_READ),
        // dwShareMode 1 (FILE_SHARE_READ), lpSecurityAttributes NULL,
        // dwCreationDisposition 3 (OPEN_EXISTING), dwFlagsAndAttributes 0x80
        // (FILE_ATTRIBUTE_NORMAL), hTemplateFile NULL: Platform::FileOpenRead.
        h = Platform::FileOpenRead(ws._data);
        fi->winHandle = h;      // 1800adddd
    }                           // 1800adde1

    // `LEA RAX,[RCX-1] ; CMP RAX,-3 ; JA fail` — an UNSIGNED test that rejects
    // exactly two values, INVALID_HANDLE_VALUE (-1) and NULL (0): unsigned
    // `h - 1 > 2^64 - 3` holds for h - 1 in {2^64 - 2, 2^64 - 1}, i.e. for
    // h == 0 and h == -1 and for nothing else. B.4 R-IO: `winHandle` is the
    // HANDLE it holds, so the two values are named.
    if (h == nullptr || h == Platform::kInvalidFileHandle) { // 1800ade12
        return false;                                        // 1800ade3c
    }

    fi->cursor = static_cast<int32_t>(
        Platform::FileSeek32(h, 0));                         // 1800ade20/26 SetFilePointer
    fi->size = static_cast<int32_t>(Platform::FileSize(h));  // 1800ade2f/35 GetFileSize

    return true;                                             // 1800ade38
}

// ============================================================================
// fix.large_packages (NOT RETAIL, docs/extensions/handins/LARGEPKM.md)
// ============================================================================
// The seams FileSystem.h declares; src/ext/large_packages/ sets them.
void (*FileSystem::_extPackagesMounted)(FileSystem& fs) = nullptr;
const FileSystem::ExtWidePackage* FileSystem::_extWidePackages = nullptr;
int32_t FileSystem::_extWideCount = 0;

// ext.package_cache (NOT RETAIL, docs/extensions/handins/PKGCACHE.md): src/ext/package_cache/ sets them.
bool (*FileSystem::_extIndexRestore)(FileSystem& fs) = nullptr;
void (*FileSystem::_extIndexBuilt)(FileSystem& fs) = nullptr;

// ext.menu_entry (NOT RETAIL, docs/extensions/handins/MENUENTRY.md): src/ext/menu_entry/ sets it.
void (*FileSystem::_extPackagesClosed)(FileSystem& fs) = nullptr;

int64_t FileSystem::PackedReadOffset(const RtIO::FileInfo& fi, bool* wide) const {
    const uint32_t tag = static_cast<uint32_t>(fi.handle);
    const uint32_t packIndex = tag >> RtIO::FileInfo::kPackIndexShift;
    const uint32_t fileIndex = tag & RtIO::FileInfo::kFileIndexMask;
    if (_extWidePackages != nullptr && packIndex < static_cast<uint32_t>(_extWideCount)) {
        const ExtWidePackage& w = _extWidePackages[packIndex];
        // The low dword must be the FileInfo's own offset: a FileInfo opened
        // before a remount (a different package at this index) keeps retail's read.
        if (fileIndex < static_cast<uint32_t>(w.count) &&
            static_cast<uint32_t>(w.offsets[fileIndex]) == static_cast<uint32_t>(fi.baseOffset)) {
            *wide = true;
            return w.offsets[fileIndex] + fi.cursor;
        }
    }
    *wide = false;
    // Retail's int32 sum (1800ace43/46), wrapping as the 32-bit ADD does.
    return static_cast<int32_t>(static_cast<uint32_t>(fi.baseOffset) + static_cast<uint32_t>(fi.cursor));
}

// ============================================================================
// PERF (NOT RETAIL, issue #13 MAPREAD): the packed arm's mapped read
// ============================================================================
// Retail serves a packed read with SetFilePointer + ReadFile on the package's one
// handle: two syscalls per read, 1.4 M of them on a MegaMod boot (6.3 GB). Here the
// file behind that handle is mapped once (Platform::FileMapOpen, on the first packed
// read from it) and the read is a memcpy from the view, with the result ReadFile
// would give: the bytes before end of file, `got == n`.
//
// EXACT, INCLUDING THE FAILURES. SetFilePointer(h, abs, NULL, FILE_BEGIN) with a
// 32-bit `abs`:
//   * abs >= 0 always succeeds (also past end of file, where ReadFile then reads 0);
//   * abs < 0 fails, returns INVALID_SET_FILE_POINTER (0xFFFFFFFF) and leaves the file
//     pointer where it was. Retail compares that return to `abs`, so every abs < -1
//     fails the read, and abs == -1 PASSES: ReadFile then reads at the stale pointer,
//     wherever the previous read left it. The table keeps that pointer (`pointer`,
//     what SetFilePointer + ReadFile would have left), so abs == -1 reads there too.
//     Until the first mapped read the OS pointer is still the true one, and an
//     abs == -1 read takes the handle path.
// A read of n < 0 (a DWORD past 2 GiB) also takes the handle path: ReadFile's
// behaviour on a buffer that short is the OS's, not a memcpy's.
// The handle path is retail's body, unchanged, and is all that runs for a package
// whose file did not map (empty, or the OS refused).
//
// LIFETIME: the mapping lives as long as the handle; PlatformCloseFile releases it
// before CloseHandle (ClosePackages, so every remount and mod toggle). The table is
// read by the loader and the audio stream thread (a streamed .wav inside a package
// reaches this arm through its own FileStream), so it is locked; the emulated pointer
// is an atomic, where retail's OS pointer was shared by both threads unlocked.
namespace {

constexpr int64_t kPointerUnknown = INT64_MIN;

struct PackedMap {
    Platform::FileHandle  handle = nullptr;   // null: free slot
    Platform::FileMapping map;                // data null: did not map, the handle path
    std::atomic<int64_t>  pointer{kPointerUnknown};
};

std::mutex g_packedMapLock;
PackedMap  g_packedMaps[256];                 // the pack index is one byte: <= 256 handles

// The mapping of `h`, made on first use. Null only when the table is full.
PackedMap* PackedMapFor(Platform::FileHandle h) {
    std::lock_guard<std::mutex> lock(g_packedMapLock);
    PackedMap* free = nullptr;
    for (PackedMap& m : g_packedMaps) {
        if (m.handle == h) return &m;
        if (m.handle == nullptr && free == nullptr) free = &m;
    }
    if (free == nullptr) return nullptr;
    free->handle = h;
    Platform::FileMapOpen(h, &free->map);
    free->pointer.store(kPointerUnknown, std::memory_order_relaxed);
    return free;
}

// True when the read was served from the view (`*ok` is its result); false sends it
// down retail's handle path.
bool PackedMapRead(PackedMap& m, int64_t abs, int32_t n, void* dst, bool* ok) {
    if (m.map.data == nullptr || n < 0) return false;
    int64_t pos = abs;
    if (abs < 0) {
        if (abs != -1) {
            *ok = false;                       // SetFilePointer failed: no read
            return true;
        }
        pos = m.pointer.load(std::memory_order_relaxed);
        if (pos == kPointerUnknown) return false;
    }
    const uint32_t got = Platform::FileMapRead(m.map, static_cast<uint64_t>(pos),
                                               static_cast<uint32_t>(n), dst);
    m.pointer.store(pos + got, std::memory_order_relaxed);
    *ok = static_cast<int32_t>(got) == n;
    return true;
}

}  // namespace

namespace {

// The mapping behind a packed `fi`'s package, with the read's absolute offset (retail's int32
// sum, 1800ace43/46); null for a loose or nested FileInfo or an unmapped package.
PackedMap* PackedMapOfEntry(Collection::Array<PackageFile*>& packfiles, const RtIO::FileInfo& fi,
                            int32_t cursor, int64_t* abs) {
    if (!RtIO::FileInfo::IsPackedHandle(fi.handle)) return nullptr;        // loose
    const uint32_t packIndex = static_cast<uint32_t>(fi.handle) >> RtIO::FileInfo::kPackIndexShift;
    if (packIndex >= static_cast<uint32_t>(packfiles._size)) return nullptr;
    const PackageFile* const pkg = packfiles[packIndex];
    if (RtIO::FileInfo::IsPackedHandle(pkg->_file.handle)) return nullptr;         // nested
    PackedMap* const m = PackedMapFor(pkg->_file.winHandle);
    if (m == nullptr || m->map.data == nullptr) return nullptr;
    *abs = static_cast<int32_t>(static_cast<uint32_t>(fi.baseOffset) + static_cast<uint32_t>(cursor));
    return m;
}

}  // namespace

const uint8_t* FileSystem::PackedMapSpan(const RtIO::FileInfo& fi, int32_t cursor, int32_t n) {
    int64_t abs = 0;
    const PackedMap* const m = PackedMapOfEntry(_packfiles, fi, cursor, &abs);
    if (m != nullptr && _extWidePackages != nullptr) {
        // fix.large_packages: the 64-bit offset the packed arm reads at (PackedReadOffset is
        // relative to fi.cursor: the entry's wide base plus `cursor`).
        bool wide = false;
        const int64_t at = PackedReadOffset(fi, &wide);
        if (wide) abs = at - fi.cursor + cursor;
    }
    if (m == nullptr || n < 0 || abs < 0) return nullptr;
    if (static_cast<uint64_t>(abs) + static_cast<uint64_t>(n) > m->map.size) return nullptr;
    return m->map.data + abs;
}

void FileSystem::PackedMapNoteRead(const RtIO::FileInfo& fi, int32_t cursor, int32_t n) {
    int64_t abs = 0;
    PackedMap* const m = PackedMapOfEntry(_packfiles, fi, cursor, &abs);
    if (m == nullptr) return;
    if (_extWidePackages != nullptr) {
        bool wide = false;
        const int64_t at = PackedReadOffset(fi, &wide);
        if (wide) abs = at - fi.cursor + cursor;
    }
    m->pointer.store(abs + n, std::memory_order_relaxed);
}

// NOT RETAIL (ext.create_on_demand, FileSystem.h): PackedMapSpan's arithmetic for cursor 0, without
// needing the engine's own mapping.
bool FileSystem::PackedEntryOrigin(const RtIO::FileInfo& fi, Platform::FileHandle* package,
                                   int64_t* offset) const {
    if (!RtIO::FileInfo::IsPackedHandle(fi.handle)) return false;        // loose
    const uint32_t packIndex = static_cast<uint32_t>(fi.handle) >> RtIO::FileInfo::kPackIndexShift;
    if (packIndex >= static_cast<uint32_t>(_packfiles._size)) return false;
    const PackageFile* const pkg = _packfiles[packIndex];
    if (RtIO::FileInfo::IsPackedHandle(pkg->_file.handle)) return false;         // nested
    int64_t abs = static_cast<int32_t>(static_cast<uint32_t>(fi.baseOffset));
    if (_extWidePackages != nullptr) {
        bool wide = false;
        const int64_t at = PackedReadOffset(fi, &wide);
        if (wide) abs = at - fi.cursor;
    }
    if (abs < 0) return false;
    *package = pkg->_file.winHandle;
    *offset  = abs;
    return true;
}

void (*FileSystem::_extHandleClosing)(Platform::FileHandle h) = nullptr;

void PackedMapRelease(Platform::FileHandle h) {
    // EXT SEAM (NOT RETAIL, docs/extensions/handins/CREATEONDEMAND.md): ext.create_on_demand forgets
    // this handle before it is closed. Null by default and under the faithful profile.
    if (FileSystem::_extHandleClosing != nullptr) FileSystem::_extHandleClosing(h);
    std::lock_guard<std::mutex> lock(g_packedMapLock);
    for (PackedMap& m : g_packedMaps) {
        if (m.handle != h) continue;
        Platform::FileMapClose(&m.map);
        m.handle = nullptr;
        m.pointer.store(kPointerUnknown, std::memory_order_relaxed);
        return;
    }
}

// NOT RETAIL (review F-141, signed overflow): retail's 32-bit ADD of two int32 FileInfo fields
// wraps; in C++ an int32 sum past INT32_MAX is undefined. Formed in uint32: the same bits.
static int32_t WrapAdd32(int32_t a, int32_t b) {
    return static_cast<int32_t>(static_cast<uint32_t>(a) + static_cast<uint32_t>(b));
}

// ============================================================================
// ?ReadFile@FileSystem@IO@@QEAA_NAEAUFileInfo@12@HPEAX@Z      retail 0x1800ace00
// ============================================================================
bool FileSystem::ReadFile(RtIO::FileInfo& fiRef, int32_t n, void* dst) {
    RtIO::FileInfo* const fi = &fiRef;

    // UNSIGNED: -1 and -2 both fall through to the loose path (§1).
    if (!RtIO::FileInfo::IsPackedHandle(fi->handle)) {       // 1800ace23  JA
        // ---- LOOSE ------------------------------------------------------
        uint32_t got = 0;                                    // 1800acf0f
        Platform::FileRead(fi->winHandle, dst,
                           static_cast<uint32_t>(n), &got);  // 1800acf2a ReadFile
        fi->cursor += static_cast<int32_t>(got);             // 1800acf37
        return static_cast<int32_t>(got) == n;               // 1800acf3c  SETZ
    }

    // ---- PACKED ---------------------------------------------------------
    // The pack index is the TOP BYTE of the tag (§1).
    const uint32_t packIndex =
        static_cast<uint32_t>(reinterpret_cast<const uint8_t*>(&fi->handle)[3]);  // retail reads the TOP BYTE of the tag (MOVZX byte [+3]); a shift would change the load width  // CAST-OK: byte_io
    PackageFile* const pkg = _packfiles[packIndex];          // 1800ace31/39

    RtIO::FileInfo pack = pkg->IO::PackageFile::GetFileInfo();  // 1800ace3d

    const int32_t abs = WrapAdd32(fi->baseOffset, fi->cursor);   // 1800ace43/46
    // EXT SEAM (NOT RETAIL, docs/extensions/handins/LARGEPKM.md): fix.large_packages gives a
    // package past 2 GiB 64-bit entry offsets; every other package takes retail's int32 seek.
    bool wide = false;
    const int64_t abs64 = (_extWidePackages != nullptr) ? PackedReadOffset(*fi, &wide) : 0;
    bool ok;
    PackedMap* map = nullptr;                                // MAPREAD (banner above)
    if (RtIO::FileInfo::IsPackedHandle(pack.handle)) {        // 1800ace4d  JBE
        // The containing .pkg is itself packed — no seek, just recurse.
        ok = true;
    } else {
        map = PackedMapFor(pack.winHandle);
        if (map != nullptr && PackedMapRead(*map, wide ? abs64 : abs, n, dst, &ok)) {
            if (ok) fi->cursor = WrapAdd32(fi->cursor, n);   // 1800ace87
            return ok;
        }
        if (wide)
            ok = Platform::FileSeek64(pack.winHandle, abs64) != 0;
        else
            ok = (static_cast<int32_t>(Platform::FileSeek32(
                      pack.winHandle, abs)) == abs);         // 1800ace61/67 SetFilePointer
    }
    if (ok) {
        const int32_t before = pack.cursor;
        ok = ReadFile(pack, n, dst);                         // 1800ace7e — ITSELF
        if (ok) fi->cursor = WrapAdd32(fi->cursor, n);       // 1800ace87
        // MAPREAD: a handle-path read moved the OS pointer to abs + got; once mapped
        // reads have run, the emulated pointer must follow it (an abs == -1 read with
        // the pointer still unknown leaves it unknown: the OS pointer stays true).
        const int64_t at = wide ? abs64 : abs;
        if (map != nullptr && at >= 0)
            map->pointer.store(at + (pack.cursor - before), std::memory_order_relaxed);
    }

    // ~pack (FileInfo's dtor: 1800acea1 +0x28, then 1800aceda +0x20) runs at
    // the return. B.4 Z LAST IO: GetFileInfo's copy now AddRefs the two names
    // (retail's copy ctor 0x18000c110, called at 180025428); the POD copy did
    // not, while this release was already there.
    return ok;                                               // 1800acf07
}

// ============================================================================
// ?OpenFile@FileSystem@IO@@QEAA?AUFileInfo@12@AEBVFilename@System@@@Z  0x1800ac880
// ============================================================================
// A FileInfo-RETURNING member, so the hidden return slot is the SECOND argument
// (RCX = this, RDX = ret) and RAX is the slot on the way out — `MOV RAX,RDI` at
// 0x1800acc2b. Getting that wrong is reference_hidden_retslot_wild_write.md.
RtIO::FileInfo FileSystem::OpenFile(const System::Filename& name) {

    // FileInfo's default ctor (1800ac8b3..: the four fields, the two
    // Filename() null stores 0x180006220, winHandle), so that the
    // FileInfo::operator= PlatformOpenFile runs over it below has a live
    // object to release. ~lf runs at every return (1800acbc9 / 1800acbfa).
    RtIO::FileInfo lf;
    // +0x04 is DELIBERATELY left as-is: retail never writes it and copies it out
    // anyway (§4). Reading it below reproduces that; the oracle excludes it.

    bool emitted = false;

    const int32_t nameLen = RtIO::StrLenOf(name._data);      // 1800ac8e2
    const int32_t dirLen  = RtIO::StrLenOf(_dataDirectory._data); // 1800ac8f2
    if (nameLen > dirLen) {                                  // 1800ac904  JLE skips
        // Strip the data-directory prefix; the packfile index is keyed on the
        // remainder, which is what LoadPackages inserted.
        // The BST walk. Same tree and same comparator as lane PKG's IndexFind,
        // and the same null asymmetries: a null on EITHER side stops the descent
        // rather than comparing.
        RtIO::PackfileIndexNode* node = _packfileIndex.root;  // 1800ac938
        {
            // Set-then-AdjustDelimiters IS the Filename const char* ctor,
            // ??0Filename@System@@QEAA@PEBD@Z (0x180026cf0); the inner scope is
            // retail's release point at 0x1800ac991, before the node is used.
            System::Filename key(name._data + dirLen);       // 1800ac91a..932

            // NOT RETAIL (block PATHINDEX): the hash index answers when it can
            // (it declines while any node key is null, the arm this walk treats
            // differently from the others).
            RtIO::PackfileIndexNode* indexed = nullptr;
            const bool answered = ::IO::PackIndexFind(&_packfileIndex, key._data, &indexed);
            if (answered && !OPENBA_PATHINDEX_CHECK)
                node = nullptr;

            while (node != nullptr) {                        // 1800ac98c
                const char* const nk = node->element.key._data;            // 1800ac945
                if (nk != nullptr && key._data != nullptr &&
                    RtIO::PathStrICmp(key._data, nk) < 0) {  // 1800ac955  JNS skips
                    node = node->left;                       // 1800ac962  +0x18
                    continue;
                }
                if (key._data != nullptr && nk != nullptr &&
                    RtIO::PathStrICmp(nk, key._data) < 0) {  // 1800ac978
                    node = node->right;                      // 1800ac985  +0x20
                    continue;
                }
                break;                                       // found
            }
            ::IO::PackIndexCheck(answered, key._data, indexed, node);
            if (answered && !OPENBA_PATHINDEX_CHECK)
                node = indexed;
        }                                                    // ~key  1800ac991

        if (node != nullptr && _existsInPackfileGate != 0) {  // 1800ac9bf
            lf.handle = static_cast<int32_t>(
                (node->element.value.packIndex << RtIO::FileInfo::kPackIndexShift)
                | node->element.value.fileIndex);                               // 1800ac9c5..d1
            const PackageFile* const pkg = _packfiles[node->element.value.packIndex];
            const RtIO::PackageFileEntry* const e = &pkg->_entries._data[node->element.value.fileIndex];
            lf.size       = static_cast<int32_t>(e->size);   // 1800ac9ee  +0xc
            lf.baseOffset = static_cast<int32_t>(e->offset); // 1800aca0c  +0x8
            lf.cursor     = 0;                               // 1800aca13
            emitted = true;                                  // JMP 1800acb8b
        }
    }

    if (!emitted) {
        if (_platformExistsGate == 0) {                      // 1800aca1e
            // FileInfo's default ctor in the hidden return slot (1800aca28..4c).
            return RtIO::FileInfo();
        } else if (!PlatformOpenFile(lf, name)) {            // 1800acace
            return RtIO::FileInfo();                         // 1800acadb..acaff
        } else {
            lf.handle = RtIO::FileInfo::kHandleLoose;        // 1800acb77
            emitted   = true;
        }
    }

    // 1800acb8b: FileInfo's copy ctor (0x18000c110, inlined: the fields
    // including retail's uninitialised +0x04 (section 4), then the Filename
    // copy ctor 0x1800067d0 on both names, 1800acbae / 1800acbbc) into the
    // hidden return slot; then ~lf (1800acbc9 +0x28, 1800acbfa +0x20). Returned
    // through a const reference so the copy, not a move, builds the slot.
    const RtIO::FileInfo& emittedInfo = lf;
    return emittedInfo;                                      // 1800acc2b
}

}  // namespace IO

