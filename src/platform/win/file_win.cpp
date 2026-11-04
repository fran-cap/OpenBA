// ============================================================================
// src/platform/win/file_win.cpp -- Platform:: files over Win32
// ============================================================================
// The Windows half of src/platform/common/platform_file.h. Each body is the Win32
// call (or the fixed sequence) the ported engine code made itself until Phase D.4
// THREADS E.5, with the same arguments.
// ============================================================================

#include "platform_file.h"

#include <windows.h>

static_assert(sizeof(HANDLE) == sizeof(Platform::FileHandle), "a file handle is a HANDLE");
static_assert(Platform::kMaxPath == MAX_PATH, "kMaxPath is MAX_PATH");
static_assert(sizeof(HANDLE) == sizeof(Platform::FindHandle), "a find handle is a HANDLE");
static_assert(Platform::kInvalidPathAttributes == INVALID_FILE_ATTRIBUTES, "INVALID_FILE_ATTRIBUTES");
static_assert(Platform::kFileAttributeDirectory == FILE_ATTRIBUTE_DIRECTORY, "FILE_ATTRIBUTE_DIRECTORY");
static_assert(LOCALE_USER_DEFAULT == 0x400 && TIME_NOSECONDS == 2 && DATE_SHORTDATE == 1,
              "retail's FormatPart constants: locale 0x400, TIME_NOSECONDS 2, DATE_SHORTDATE 1");

// Platform::FindData IS WIN32_FIND_DATAW: FindFirst / FindNext fill it in place.
using Platform::FindData;
static_assert(sizeof(FindData) == sizeof(WIN32_FIND_DATAW), "FindData is WIN32_FIND_DATAW");
static_assert(offsetof(FindData, attributes)     == offsetof(WIN32_FIND_DATAW, dwFileAttributes), "attrs");
static_assert(offsetof(FindData, creationTime)   == offsetof(WIN32_FIND_DATAW, ftCreationTime), "ctime");
static_assert(offsetof(FindData, lastAccessTime) == offsetof(WIN32_FIND_DATAW, ftLastAccessTime), "atime");
static_assert(offsetof(FindData, lastWriteTime)  == offsetof(WIN32_FIND_DATAW, ftLastWriteTime), "mtime");
static_assert(offsetof(FindData, sizeHigh)       == offsetof(WIN32_FIND_DATAW, nFileSizeHigh), "size hi");
static_assert(offsetof(FindData, sizeLow)        == offsetof(WIN32_FIND_DATAW, nFileSizeLow), "size lo");
static_assert(offsetof(FindData, reserved0)      == offsetof(WIN32_FIND_DATAW, dwReserved0), "res0");
static_assert(offsetof(FindData, reserved1)      == offsetof(WIN32_FIND_DATAW, dwReserved1), "res1");
static_assert(offsetof(FindData, name)           == offsetof(WIN32_FIND_DATAW, cFileName), "name");
static_assert(offsetof(FindData, alternateName)  == offsetof(WIN32_FIND_DATAW, cAlternateFileName), "alt");
static_assert(sizeof(FindData::name) == sizeof(WIN32_FIND_DATAW::cFileName), "name size");

namespace {

// D4_RULES C2: the one place a PathChar becomes a Win32 wide string.
inline LPCWSTR Wide(const Platform::PathChar* p) { return reinterpret_cast<LPCWSTR>(p); }  // CAST-OK: win32
inline LPWSTR  Wide(Platform::PathChar* p)       { return reinterpret_cast<LPWSTR>(p); }   // CAST-OK: win32
inline Platform::FileHandle FromWin(HANDLE h)    { return static_cast<Platform::FileHandle>(h); }  // CAST-OK: win32
inline Platform::FindHandle FindFromWin(HANDLE h) { return static_cast<Platform::FindHandle>(h); }  // CAST-OK: win32
inline WIN32_FIND_DATAW* Win(FindData* d)        { return reinterpret_cast<WIN32_FIND_DATAW*>(d); }  // CAST-OK: win32

}  // namespace

namespace Platform {

FileHandle FileOpenRead(const PathChar* path) {
    return FromWin(CreateFileW(Wide(path), GENERIC_READ, FILE_SHARE_READ, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
}

FileHandle FileCreateTemp(const PathChar* prefix, PathChar* outPath) {
    wchar_t tempDir[MAX_PATH];
    GetTempPathW(MAX_PATH, tempDir);
    GetTempFileNameW(tempDir, Wide(prefix), 0, Wide(outPath));
    return FromWin(CreateFileW(Wide(outPath), GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
}

int32_t FileRead(FileHandle h, void* dst, uint32_t bytes, uint32_t* got) {
    DWORD n = 0;
    const BOOL ok = ReadFile(h, dst, bytes, &n, nullptr);
    *got = n;
    return ok;
}

int32_t FileWrite(FileHandle h, const void* src, uint32_t bytes, uint32_t* written) {
    DWORD n = 0;
    const BOOL ok = WriteFile(h, src, bytes, &n, nullptr);
    *written = n;
    return ok;
}

uint32_t FileSeek32(FileHandle h, int32_t pos) { return SetFilePointer(h, pos, nullptr, FILE_BEGIN); }
uint32_t FileSize(FileHandle h)                { return GetFileSize(h, nullptr); }

// fix.large_packages (NOT RETAIL, docs/extensions/handins/LARGEPKM.md).
int32_t FileSeek64(FileHandle h, int64_t pos) {
    if (pos < 0) return 0;
    LARGE_INTEGER d;
    d.QuadPart = pos;
    return SetFilePointerEx(h, d, nullptr, FILE_BEGIN) ? 1 : 0;
}

int64_t FileSize64(FileHandle h) {
    LARGE_INTEGER s;
    if (!GetFileSizeEx(h, &s)) return -1;
    return s.QuadPart;
}
int32_t  FileClose(FileHandle h)               { return CloseHandle(h); }

int32_t FileLastWrite(FileHandle h, uint64_t* fileTime) {
    FILETIME lastWrite;
    const BOOL ok = GetFileTime(h, nullptr, nullptr, &lastWrite);
    if (ok) *fileTime = (static_cast<uint64_t>(lastWrite.dwHighDateTime) << 32) | lastWrite.dwLowDateTime;
    return ok;
}

int32_t FileDelete(const PathChar* path)                   { return DeleteFileW(Wide(path)); }
int32_t FileMove(const PathChar* from, const PathChar* to) { return MoveFileW(Wide(from), Wide(to)); }
int32_t DirCreate(const PathChar* path)                    { return CreateDirectoryW(Wide(path), nullptr); }

FileHandle FileOpenReadShared(const PathChar* path) {
    return FromWin(CreateFileW(Wide(path), GENERIC_READ,
                               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
}
FileHandle FileCreateWrite(const PathChar* path) {
    return FromWin(CreateFileW(Wide(path), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, nullptr));
}
int32_t FileFlush(FileHandle h) { return FlushFileBuffers(h); }
int32_t FileReplace(const PathChar* from, const PathChar* to) {
    return MoveFileExW(Wide(from), Wide(to), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
}

uint32_t PathAttributes(const PathChar* path)          { return GetFileAttributesW(Wide(path)); }
uint32_t ExecutablePath(PathChar* buf, uint32_t size)  { return GetModuleFileNameW(nullptr, Wide(buf), size); }

FindHandle FindFirst(const PathChar* pattern, FindData* out) { return FindFromWin(FindFirstFileW(Wide(pattern), Win(out))); }
int32_t    FindNext(FindHandle h, FindData* out)             { return FindNextFileW(h, Win(out)); }
int32_t    FindClose(FindHandle h)                           { return ::FindClose(h); }

void FormatLocalDateTime(const uint32_t* fileTime, bool timePart, PathChar* out) {
    // Retail's frame writes nothing into these before the calls and ignores all three
    // results (value_types.cpp's banner).
    FILETIME local;
    SYSTEMTIME st;
    static_assert(sizeof(FILETIME) == 2 * sizeof(uint32_t), "a FILETIME is two dwords, low first");
    // NOT RETAIL (review F-152 / F-168, read of uninitialised memory): when a conversion
    // fails (a FILETIME of 0 west of UTC, GetFileTimeStamp's failure value; or one past
    // 0x7fffffff'ffffffff) retail formats its uninitialised SYSTEMTIME, or leaves the
    // output unwritten, and the caller builds a WideString from it, scanning stack garbage
    // for a terminator. Each result is tested here and a failure leaves the empty string,
    // as the POSIX twins do. A conversion that succeeds is retail's.
    out[0] = 0;
    if (!FileTimeToLocalFileTime(reinterpret_cast<const FILETIME*>(fileTime), &local))  // CAST-OK: win32
        return;
    if (!FileTimeToSystemTime(&local, &st))
        return;
    int written = 0;
    if (timePart)
        written = GetTimeFormatW(LOCALE_USER_DEFAULT, TIME_NOSECONDS, &st, nullptr, Wide(out), kDateTimeChars);
    else
        written = GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &st, nullptr, Wide(out), kDateTimeChars);
    if (written == 0)
        out[0] = 0;
}

}  // namespace Platform
