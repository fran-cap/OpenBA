#pragma once
// ============================================================================
// src/platform/common/platform_file.h -- files and directories (Phase D.4, THREADS
// seam, E.5 / E.6)
// ============================================================================
// D4_RULES C1: namespace `Platform`, free functions, NO OS header. Windows bodies:
// src/platform/win/file_win.cpp, one Win32 call (or retail's fixed sequence) each.
//
// PATHS are `const Platform::PathChar*` (D4_RULES C2): `char16_t` (UTF-16, 2 bytes on
// every platform) since ABI step C.5, the unit of System::WideString::_data, so a
// caller passes `ws._data` unchanged. The Win32 bodies cast to `const wchar_t*` at
// the call; a POSIX body converts UTF-16 to UTF-8.
//
// RESULTS keep Win32's shapes, because the ported callers test them the way
// retail's code does:
//   * a "BOOL" result is an int32_t, nonzero on success. A POSIX impl returns 1 /
//     0: PersistenceWorker::DeleteJob computes `(ok != 1) + 1`, retail's own
//     arithmetic.
//   * the byte counts are uint32_t (a DWORD), written on every call.
//   * FileSeek32 / FileSize return the 32-bit value the callers compare
//     (SetFilePointer / GetFileSize with no high word: offsets are 32-bit).
//
// THE HANDLE. `FileHandle` is a pointer to an INCOMPLETE type, 8 bytes, the type of
// IO::FileInfo::winHandle (+0x18). On Windows the value IS the HANDLE. Two values
// mean "no file": kInvalidFileHandle (INVALID_HANDLE_VALUE, what FileInfo's ctor
// stores) and null; every opener's caller rejects both (`LEA RAX,[h-1] ; CMP
// RAX,-3`), so a POSIX impl hands out fd + 1 and never 0 or -1 (plans/THREADS.md A.5).
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace Platform {

using PathChar = char16_t;              // D4_RULES C2: flipped from wchar_t by ABI C.5

struct FileObject;                      // never defined on Windows: the pointer is the HANDLE
using FileHandle = FileObject*;
static_assert(sizeof(FileHandle) == 8, "a FileHandle fills IO::FileInfo's 8-byte +0x18 slot");

// INVALID_HANDLE_VALUE: all bits set.
inline FileHandle const kInvalidFileHandle = reinterpret_cast<FileHandle>(~uintptr_t{0});  // CAST-OK: win32

// MAX_PATH: the size, in PathChars, of FileCreateTemp's output buffer.
constexpr uint32_t kMaxPath = 260;

// CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
// FILE_ATTRIBUTE_NORMAL, NULL): FileSystem::PlatformOpenFile 0x1800addb3 and
// GetFileTimeStamp 0x1800ae2c0's argument block.
FileHandle FileOpenRead(const PathChar* path);

// FileSystem::PlatformCreateFile 0x1800ade91..0x1800adee5, in retail's order:
// GetTempPathW(MAX_PATH, dir); GetTempFileNameW(dir, prefix, 0, outPath);
// CreateFileW(outPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
// NULL). `outPath` holds kMaxPath PathChars. A POSIX impl: mkstemp in the temp dir.
FileHandle FileCreateTemp(const PathChar* prefix, PathChar* outPath);

int32_t  FileRead(FileHandle h, void* dst, uint32_t bytes, uint32_t* got);           // ReadFile
int32_t  FileWrite(FileHandle h, const void* src, uint32_t bytes, uint32_t* written); // WriteFile
uint32_t FileSeek32(FileHandle h, int32_t pos);    // SetFilePointer(h, pos, NULL, FILE_BEGIN)
uint32_t FileSize(FileHandle h);                   // GetFileSize(h, NULL)
// NOT RETAIL (fix.large_packages, docs/extensions/handins/LARGEPKM.md): the 64-bit twins of the
// two above. Retail's file layer is 32-bit; these run only for a package the fix gave 64-bit
// entry offsets (larger than 2 GiB). FileSeek64: 1 on success, 0 on failure (`pos` < 0 fails).
// FileSize64: the whole size, -1 on failure.
int32_t  FileSeek64(FileHandle h, int64_t pos);    // SetFilePointerEx(h, pos, NULL, FILE_BEGIN)
int64_t  FileSize64(FileHandle h);                 // GetFileSizeEx(h, &size)
int32_t  FileClose(FileHandle h);                  // CloseHandle
// GetFileTime(h, NULL, NULL, &lastWrite): FILETIME units (100 ns since 1601 UTC),
// low dword in the low half. `*fileTime` is written only on success.
int32_t  FileLastWrite(FileHandle h, uint64_t* fileTime);

int32_t  FileDelete(const PathChar* path);                        // DeleteFileW
// MoveFileW: it MOVES A FILE ACROSS VOLUMES (copy + delete), so a POSIX impl copies
// and unlinks on EXDEV (plans/THREADS.md F.1).
int32_t  FileMove(const PathChar* from, const PathChar* to);
int32_t  DirCreate(const PathChar* path);                         // CreateDirectoryW(path, NULL)
// NOT RETAIL: DirCreate on every prefix of `dir` (either separator), so the missing parents are made
// too; an existing directory, a drive ("C:") or a UNC host fail harmlessly. true when `dir` is a
// directory afterwards. One body over DirCreate / PathAttributes (src/platform/common/dir_chain.cpp):
// the settings twins' WriteFileAtomic and the derived cache's Store::Open.
bool     EnsureDirChain(const PathChar* dir);

// ---- settings.cfg's file layer (D4_RULES C3; not retail) ---------------------
// src/platform/win/settings_win.cpp's ReadWholeFile / WriteFileAtomic, with the
// exact arguments they used before this seam existed. They are separate from the
// retail-shaped FileOpenRead / FileMove above because their semantics differ:
//   FileOpenReadShared  CreateFileW(GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE |
//                       FILE_SHARE_DELETE, OPEN_EXISTING, NORMAL): a reader that never
//                       blocks a concurrent replace of the same file.
//   FileCreateWrite     CreateFileW(GENERIC_WRITE, share 0, CREATE_ALWAYS, NORMAL) at
//                       the caller's path (FileCreateTemp picks a %TEMP% name).
//   FileFlush           FlushFileBuffers.
//   FileReplace         MoveFileExW(MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH):
//                       replaces an existing destination, where MoveFileW (FileMove)
//                       fails. A POSIX impl: fsync + rename (same directory).
FileHandle FileOpenReadShared(const PathChar* path);
FileHandle FileCreateWrite(const PathChar* path);
int32_t    FileFlush(FileHandle h);
int32_t    FileReplace(const PathChar* from, const PathChar* to);

// ---- E.6: directories and paths ---------------------------------------------

// GetFileAttributesW: the attribute bits, or kInvalidPathAttributes when the path
// does not exist (callers test existence with `!= kInvalidPathAttributes`).
constexpr uint32_t kInvalidPathAttributes = 0xFFFFFFFFu;    // INVALID_FILE_ATTRIBUTES
constexpr uint32_t kFileAttributeDirectory = 0x10u;          // FILE_ATTRIBUTE_DIRECTORY
uint32_t PathAttributes(const PathChar* path);

// GetModuleFileNameW(NULL, buf, size): the running executable's path; the length
// written. A POSIX impl reads /proc/self/exe.
uint32_t ExecutablePath(PathChar* buf, uint32_t size);

// Directory enumeration: FindFirstFileW / FindNextFileW / FindClose.
// FindData is WIN32_FIND_DATAW field for field (fixed-width types; file_win.cpp
// asserts every offset against the Win32 struct), so the Windows impl fills the
// caller's own object in place. A POSIX impl fills `attributes` (the directory
// bit) and `name` only; it matches the pattern case-INsensitively and returns
// entries in NTFS's upper-case collation order (plans/THREADS.md F.2: the mod and
// save lists take this order).
struct FindData {
    uint32_t attributes;          // +0x000 dwFileAttributes
    uint32_t creationTime[2];     // +0x004 ftCreationTime   (low, high)
    uint32_t lastAccessTime[2];   // +0x00c ftLastAccessTime
    uint32_t lastWriteTime[2];    // +0x014 ftLastWriteTime
    uint32_t sizeHigh;            // +0x01c nFileSizeHigh
    uint32_t sizeLow;             // +0x020 nFileSizeLow
    uint32_t reserved0;           // +0x024
    uint32_t reserved1;           // +0x028
    PathChar name[kMaxPath];      // +0x02c cFileName
    PathChar alternateName[14];   // cAlternateFileName
};
static_assert(offsetof(FindData, name) == 0x2c, "cFileName follows the eleven dwords");

struct FindObject;                      // never defined on Windows: the pointer is the HANDLE
using FindHandle = FindObject*;
static_assert(sizeof(FindHandle) == 8, "a FindHandle is an 8-byte HANDLE");
// INVALID_HANDLE_VALUE, FindFirst's failure value (a find handle is never null).
inline FindHandle const kInvalidFindHandle = reinterpret_cast<FindHandle>(~uintptr_t{0});  // CAST-OK: win32

FindHandle FindFirst(const PathChar* pattern, FindData* out);     // FindFirstFileW
int32_t    FindNext(FindHandle h, FindData* out);                 // FindNextFileW: the BOOL
int32_t    FindClose(FindHandle h);                               // FindClose

// ---- E.7: the local date / time of a FILETIME ---------------------------------

// System::DateTime::DateAsString 0x1800c56a0 / TimeAsString 0x1800c55b0, the OS
// half: FileTimeToLocalFileTime(fileTime) + FileTimeToSystemTime, then
// GetTimeFormatW(LOCALE_USER_DEFAULT, TIME_NOSECONDS) when `timePart`, else
// GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE), into `out` (kDateTimeChars
// PathChars, the unit WideString holds). `fileTime` points at the two dwords of a
// FILETIME (low first): System::DateTime's own storage. Retail ignores both
// conversion results and formats uninitialised stack on failure; every body here
// writes the empty string into `out` when the conversion fails (NOT RETAIL, review
// F-152 / F-168). A POSIX impl: localtime_r + strftime("%x" / "%H:%M").
constexpr uint32_t kDateTimeChars = 0x100;
void FormatLocalDateTime(const uint32_t* fileTime, bool timePart, PathChar* out);

// ---- read-only file mapping (NOT RETAIL; issue #13 MAPREAD) ---------------------
// The whole file behind an open read handle, mapped read-only: FileSystem::ReadFile's
// packed arm serves a mounted package's reads by memcpy from the view instead of
// SetFilePointer + ReadFile on the package's one handle (filesystem_openfile.cpp).
// Windows: CreateFileMappingW(PAGE_READONLY) + MapViewOfFile(FILE_MAP_READ), the
// section handle in `os`. POSIX (Linux and macOS, file_map_posix.cpp): mmap(PROT_READ,
// MAP_PRIVATE), `os` unused. FileMapOpen returns false, the mapping zeroed, for an
// empty file or when the OS refuses; the caller then keeps the handle path.
struct FileMapping {
    const uint8_t* data = nullptr;
    uint64_t       size = 0;
    void*          os   = nullptr;
};
bool FileMapOpen(FileHandle h, FileMapping* out);
void FileMapClose(FileMapping* m);

// ReadFile's result over a mapped file, without the call: a read of `bytes` (a DWORD)
// at absolute offset `pos` copies what lies before end of file and reports that count,
// exactly what SetFilePointer(pos) + ReadFile(bytes) does on a file of `size` bytes
// (a position at or past the end reads 0). Pure; tests/unit/file_map_read_test.cpp.
inline uint32_t FileMapRead(const FileMapping& m, uint64_t pos, uint32_t bytes, void* dst) {
    if (pos >= m.size) return 0;
    const uint64_t left = m.size - pos;
    const uint32_t got  = left < bytes ? static_cast<uint32_t>(left) : bytes;
    std::memcpy(dst, m.data + pos, got);
    return got;
}

// ---- an exclusive read / write file (NOT RETAIL; issue #13 COMPRESSAO) --------------------
// The derived-data cache's files (src/ext/derived_cache/derived_cache.h). FileOpenExclusive opens
// `path` for reading and writing, creating it when absent, and holds it exclusively: a second
// opener, in this process or another, fails until FileClose. Windows: CreateFileW(GENERIC_READ |
// GENERIC_WRITE, share 0, OPEN_ALWAYS, NORMAL) -- the share mode IS the lock. POSIX
// (file_rw_posix.cpp, Linux and macOS): open(O_RDWR | O_CREAT, 0644) + flock(LOCK_EX | LOCK_NB);
// the handle is fd + 1 as everywhere. Returns kInvalidFileHandle on failure (held elsewhere included).
// FileTruncate sets the file's size to `size` (SetFilePointerEx + SetEndOfFile / ftruncate);
// 1 on success. A mapped file cannot be truncated on Windows: truncate before FileMapOpen.
FileHandle FileOpenExclusive(const PathChar* path);
int32_t    FileTruncate(FileHandle h, int64_t size);
// The cache's data files while derived.lock is held: read / write, created when absent, and readable
// by another process meanwhile (the lock loser reads them with FileOpenReadShared, whose share mode
// allows this handle's writes), which may also rename over or delete them. A second writer is
// refused. Windows: CreateFileW(GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_DELETE,
// OPEN_ALWAYS, NORMAL). POSIX: open(O_RDWR | O_CREAT, 0644) and no flock: derived.lock is the
// writers' exclusion, and a reader keeps the inode it opened across a rename. `sequentialScan`
// (derived.dat: appended to, read in whole payloads) adds FILE_FLAG_SEQUENTIAL_SCAN (POSIX:
// posix_fadvise(POSIX_FADV_SEQUENTIAL) where the platform has it; macOS has not).
FileHandle FileOpenWriteShared(const PathChar* path, bool sequentialScan);

}  // namespace Platform
