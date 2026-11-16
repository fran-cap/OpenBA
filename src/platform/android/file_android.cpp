// ============================================================================
// src/platform/android/file_android.cpp -- Platform:: files and directories (Android)
// ============================================================================
// The Android twin of src/platform/posix/file_posix.cpp (src/platform/android/README.md):
// it replaces that file whole on Android. Every body is the POSIX one (bionic has the same
// calls) except two, whose Linux answers do not exist in an app process:
//   ExecutablePath  /proc/self/exe is /system/bin/app_process64. The game directory is the
//                   app's data folder (android_paths.h), so the "exe" is <data>/OpenBA there:
//                   IO::FileSystem::SetupDirectories strips the name and finds WinData/
//                   beside it, as it does beside OpenBA.exe on Windows.
//   FileCreateTemp  P_tmpdir is /data/local/tmp, which an app cannot write: the temp files
//                   go to <data>/tmp (same file system as the saves, so the move after a save
//                   is a rename).
// The rest of the POSIX file's banner holds for this one:
//
// Each body keeps the Win32 primitive's result shape (BOOL as 1 / 0, DWORD counts,
// 32-bit seek results) because the ported callers test them retail's way.
//
// PATHS: PathChar (UTF-16) -> UTF-8 with '\' -> '/' at the syscall
// (posix_path.h). Paths handed back (the exe path, the temp file name, directory
// entries) come back as UTF-16 in the OS spelling; System::Filename normalises
// them to '\' as it does every path.
//
// THE HANDLE is fd + 1 in the pointer slot: never 0 and never -1, the two values
// every opener's caller rejects (`LEA RAX,[h-1] ; CMP RAX,-3`).
//
// The risks docs/runtime_port/phase_d/plans/THREADS.md F lists are answered as in file_posix.cpp.
// ============================================================================

#include "platform_file.h"
#include "platform/posix/posix_path.h"
#include "android_paths.h"   // Platform::Android::DataDir

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "platform_time.h"   // FileTimeFromUnix

namespace Platform {

using Posix::ToNative;

namespace {

constexpr uint32_t kFileAttributeNormal   = 0x80u;           // FILE_ATTRIBUTE_NORMAL
constexpr uint32_t kFileAttributeReadOnly = 0x01u;           // FILE_ATTRIBUTE_READONLY
constexpr uint32_t kInvalidSetFilePointer = 0xFFFFFFFFu;     // INVALID_SET_FILE_POINTER
constexpr uint32_t kInvalidFileSize       = 0xFFFFFFFFu;     // INVALID_FILE_SIZE

FileHandle FromFd(int fd) {
    if (fd < 0) return kInvalidFileHandle;
    return reinterpret_cast<FileHandle>(static_cast<uintptr_t>(fd) + 1u);  // CAST-OK: ptr_int
}
int Fd(FileHandle h) {
    return static_cast<int>(reinterpret_cast<uintptr_t>(h) - 1u);  // CAST-OK: ptr_int
}

uint64_t MTimeFileTime(const struct stat& st) {
    return FileTimeFromUnix(static_cast<int64_t>(st.st_mtim.tv_sec),
                            static_cast<uint32_t>(st.st_mtim.tv_nsec));
}

uint32_t AttributesOf(const struct stat& st) {
    if (S_ISDIR(st.st_mode)) return kFileAttributeDirectory;
    return (st.st_mode & S_IWUSR) ? kFileAttributeNormal : kFileAttributeReadOnly;
}

// read / write until `bytes` or end of file: ReadFile on a disk file returns a
// short count only at the end.
int32_t ReadAll(int fd, void* dst, uint32_t bytes, uint32_t* got) {
    uint8_t* p = static_cast<uint8_t*>(dst);  // CAST-OK: byte_io
    uint32_t done = 0;
    while (done < bytes) {
        const ssize_t n = ::read(fd, p + done, bytes - done);
        if (n < 0) {
            if (errno == EINTR) continue;
            *got = done;
            return 0;
        }
        if (n == 0) break;
        done += static_cast<uint32_t>(n);
    }
    *got = done;
    return 1;
}

int32_t WriteAll(int fd, const void* src, uint32_t bytes, uint32_t* written) {
    const uint8_t* p = static_cast<const uint8_t*>(src);  // CAST-OK: byte_io
    uint32_t done = 0;
    while (done < bytes) {
        const ssize_t n = ::write(fd, p + done, bytes - done);
        if (n < 0) {
            if (errno == EINTR) continue;
            *written = done;
            return 0;
        }
        done += static_cast<uint32_t>(n);
    }
    *written = done;
    return 1;
}

// F.1: the cross-file-system half of MoveFileW. The destination must not exist
// (checked by the caller); the source is removed only once the copy is complete.
bool CopyThenUnlink(const std::string& from, const std::string& to) {
    const int in = ::open(from.c_str(), O_RDONLY | O_CLOEXEC);
    if (in < 0) return false;
    struct stat st;
    if (::fstat(in, &st) != 0 || S_ISDIR(st.st_mode)) {   // MoveFileW moves no directory across volumes
        ::close(in);
        return false;
    }
    const int out = ::open(to.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, st.st_mode & 0777);
    if (out < 0) {
        ::close(in);
        return false;
    }
    bool ok = true;
    char buf[65536];
    for (;;) {
        const ssize_t n = ::read(in, buf, sizeof(buf));
        if (n < 0) {
            if (errno == EINTR) continue;
            ok = false;
            break;
        }
        if (n == 0) break;
        uint32_t written = 0;
        if (!WriteAll(out, buf, static_cast<uint32_t>(n), &written)) {
            ok = false;
            break;
        }
    }
    if (ok && ::fsync(out) != 0) ok = false;
    ::close(in);
    if (::close(out) != 0) ok = false;
    if (!ok) {
        ::unlink(to.c_str());
        return false;
    }
    ::unlink(from.c_str());
    return true;
}

// ---- FindFirst / FindNext -----------------------------------------------------

// FsRtlIsNameInExpression's everyday subset, ASCII case-folded: '*' any run,
// '?' one character. "*.*" matches every name, as on Windows.
char16_t Upper(char16_t c) { return (c >= u'a' && c <= u'z') ? static_cast<char16_t>(c - 32) : c; }

bool Match(const char16_t* pat, const char16_t* name) {
    const char16_t* starPat = nullptr;
    const char16_t* starName = nullptr;
    while (*name != 0) {
        if (*pat == u'*') {
            starPat = ++pat;
            starName = name;
        } else if (*pat == u'?' || (*pat != 0 && Upper(*pat) == Upper(*name))) {
            ++pat;
            ++name;
        } else if (starPat != nullptr) {
            pat = starPat;
            name = ++starName;
        } else {
            return false;
        }
    }
    while (*pat == u'*') ++pat;
    return *pat == 0;
}

// NTFS returns a directory's entries in its upcase-table order: compare the
// upper-cased UTF-16 units. ASCII is folded here; that covers every name the
// game writes (packages, mods, saves).
bool NtfsLess(const std::u16string& a, const std::u16string& b) {
    const size_t n = a.size() < b.size() ? a.size() : b.size();
    for (size_t i = 0; i < n; ++i) {
        const char16_t x = Upper(a[i]), y = Upper(b[i]);
        if (x != y) return x < y;
    }
    return a.size() < b.size();
}

}  // namespace

struct FindObject {
    std::vector<FindData> entries;
    size_t next;
};

namespace {

void Fill(FindData* out, const std::u16string& name, const struct stat* st) {
    std::memset(out, 0, sizeof(*out));
    if (st != nullptr) {
        out->attributes = AttributesOf(*st);
        const uint64_t ft = MTimeFileTime(*st);
        out->lastWriteTime[0] = static_cast<uint32_t>(ft);
        out->lastWriteTime[1] = static_cast<uint32_t>(ft >> 32);
        out->creationTime[0] = out->lastAccessTime[0] = out->lastWriteTime[0];
        out->creationTime[1] = out->lastAccessTime[1] = out->lastWriteTime[1];
        const uint64_t size = S_ISDIR(st->st_mode) ? 0u : static_cast<uint64_t>(st->st_size);
        out->sizeLow = static_cast<uint32_t>(size);
        out->sizeHigh = static_cast<uint32_t>(size >> 32);
    } else {
        out->attributes = kFileAttributeNormal;
    }
    Posix::CopyOut(name, out->name, kMaxPath);
}

}  // namespace

FindHandle FindFirst(const PathChar* pattern, FindData* out) {
    // Split "dir\pattern" at the last separator (either spelling).
    const size_t len = Posix::PathLength(pattern);
    size_t cut = len;
    while (cut > 0 && pattern[cut - 1] != u'\\' && pattern[cut - 1] != u'/') --cut;
    std::string dir = cut == 0 ? std::string(".") : Posix::Utf16ToUtf8(pattern, cut, true);
    std::u16string glob(pattern + cut, pattern + len);
    if (glob == u"*.*") glob = u"*";

    DIR* d = ::opendir(dir.c_str());
    if (d == nullptr) return kInvalidFindHandle;
    const int dfd = ::dirfd(d);

    std::vector<std::u16string> names;
    while (struct dirent* e = ::readdir(d)) {
        const std::u16string name = Posix::Utf8ToUtf16(e->d_name, std::strlen(e->d_name));
        if (Match(glob.c_str(), name.c_str())) names.push_back(name);
    }
    // "." and ".." first (NTFS lists them first), then the upcase order.
    std::sort(names.begin(), names.end(), [](const std::u16string& a, const std::u16string& b) {
        const int ra = a == u"." ? 0 : a == u".." ? 1 : 2;
        const int rb = b == u"." ? 0 : b == u".." ? 1 : 2;
        if (ra != rb) return ra < rb;
        return NtfsLess(a, b);
    });

    FindObject* f = new FindObject();
    f->next = 0;
    f->entries.resize(names.size());
    for (size_t i = 0; i < names.size(); ++i) {
        const std::string native = Posix::Utf16ToUtf8(names[i].c_str(), names[i].size(), false);
        struct stat st;
        const bool haveStat = ::fstatat(dfd, native.c_str(), &st, 0) == 0;
        Fill(&f->entries[i], names[i], haveStat ? &st : nullptr);
    }
    ::closedir(d);

    if (f->entries.empty()) {   // ERROR_FILE_NOT_FOUND
        delete f;
        return kInvalidFindHandle;
    }
    *out = f->entries[f->next++];
    return f;
}

int32_t FindNext(FindHandle h, FindData* out) {
    if (h->next >= h->entries.size()) return 0;   // ERROR_NO_MORE_FILES
    *out = h->entries[h->next++];
    return 1;
}

int32_t FindClose(FindHandle h) {
    delete h;
    return 1;
}

// ---- files -------------------------------------------------------------------

FileHandle FileOpenRead(const PathChar* path) {
    return FromFd(::open(ToNative(path).c_str(), O_RDONLY | O_CLOEXEC));
}

// GetTempPathW + GetTempFileNameW + CreateFileW(CREATE_ALWAYS, share 0): mkstemp
// in <data>/tmp (the banner: P_tmpdir is not writable by an app) with the prefix's first
// three characters, as GetTempFileNameW uses. The name written to `outPath` is the OS spelling.
FileHandle FileCreateTemp(const PathChar* prefix, PathChar* outPath) {
    std::string tmpl = Android::DataDir() + "/tmp";
    ::mkdir(tmpl.c_str(), 0770);
    tmpl.push_back('/');
    const size_t plen = Posix::PathLength(prefix);
    tmpl += Posix::Utf16ToUtf8(prefix, plen < 3 ? plen : 3, false);
    tmpl += "XXXXXX";
    std::vector<char> buf(tmpl.begin(), tmpl.end());
    buf.push_back('\0');
    const int fd = ::mkostemp(buf.data(), O_CLOEXEC);
    const std::u16string name = Posix::Utf8ToUtf16(buf.data(), std::strlen(buf.data()));
    Posix::CopyOut(fd < 0 ? std::u16string() : name, outPath, kMaxPath);
    return FromFd(fd);
}

int32_t FileRead(FileHandle h, void* dst, uint32_t bytes, uint32_t* got) {
    return ReadAll(Fd(h), dst, bytes, got);
}

int32_t FileWrite(FileHandle h, const void* src, uint32_t bytes, uint32_t* written) {
    return WriteAll(Fd(h), src, bytes, written);
}

uint32_t FileSeek32(FileHandle h, int32_t pos) {
    const off_t r = ::lseek(Fd(h), static_cast<off_t>(pos), SEEK_SET);
    return r < 0 ? kInvalidSetFilePointer : static_cast<uint32_t>(r);
}

uint32_t FileSize(FileHandle h) {
    struct stat st;
    if (::fstat(Fd(h), &st) != 0) return kInvalidFileSize;
    return static_cast<uint32_t>(st.st_size);
}

// fix.large_packages (NOT RETAIL, docs/extensions/handins/LARGEPKM.md). off_t is 64-bit on every
// target this twin builds for.
static_assert(sizeof(off_t) == 8, "a 64-bit seek needs a 64-bit off_t");

int32_t FileSeek64(FileHandle h, int64_t pos) {
    if (pos < 0) return 0;
    return ::lseek(Fd(h), static_cast<off_t>(pos), SEEK_SET) == static_cast<off_t>(pos) ? 1 : 0;
}

int64_t FileSize64(FileHandle h) {
    struct stat st;
    if (::fstat(Fd(h), &st) != 0) return -1;
    return static_cast<int64_t>(st.st_size);
}

int32_t FileClose(FileHandle h) { return ::close(Fd(h)) == 0 ? 1 : 0; }

int32_t FileLastWrite(FileHandle h, uint64_t* fileTime) {
    struct stat st;
    if (::fstat(Fd(h), &st) != 0) return 0;
    *fileTime = MTimeFileTime(st);
    return 1;
}

int32_t FileDelete(const PathChar* path) { return ::unlink(ToNative(path).c_str()) == 0 ? 1 : 0; }

// MoveFileW: fails when the destination exists (rename() would replace it), and
// moves a file across file systems by copying it (F.1).
int32_t FileMove(const PathChar* from, const PathChar* to) {
    const std::string src = ToNative(from), dst = ToNative(to);
    struct stat st;
    if (::lstat(dst.c_str(), &st) == 0) return 0;   // ERROR_ALREADY_EXISTS
    if (::rename(src.c_str(), dst.c_str()) == 0) return 1;
    if (errno != EXDEV) return 0;
    return CopyThenUnlink(src, dst) ? 1 : 0;
}

int32_t DirCreate(const PathChar* path) { return ::mkdir(ToNative(path).c_str(), 0777) == 0 ? 1 : 0; }

FileHandle FileOpenReadShared(const PathChar* path) {
    return FromFd(::open(ToNative(path).c_str(), O_RDONLY | O_CLOEXEC));
}

FileHandle FileCreateWrite(const PathChar* path) {
    return FromFd(::open(ToNative(path).c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0666));
}

int32_t FileFlush(FileHandle h) { return ::fsync(Fd(h)) == 0 ? 1 : 0; }

// MoveFileExW(REPLACE_EXISTING | WRITE_THROUGH) without COPY_ALLOWED: a same-volume
// replace, which rename() is (the data was fsync'd by FileFlush).
int32_t FileReplace(const PathChar* from, const PathChar* to) {
    return ::rename(ToNative(from).c_str(), ToNative(to).c_str()) == 0 ? 1 : 0;
}

// ---- E.6: paths --------------------------------------------------------------

uint32_t PathAttributes(const PathChar* path) {
    struct stat st;
    if (::stat(ToNative(path).c_str(), &st) != 0) return kInvalidPathAttributes;
    return AttributesOf(st);
}

// GetModuleFileNameW(NULL, buf, size): <data>/OpenBA (the banner). Truncated to `size` units
// with a NUL, as GetModuleFileNameW truncates; the units written.
uint32_t ExecutablePath(PathChar* buf, uint32_t size) {
    const std::string& dir = Android::DataDir();
    if (dir.empty()) {
        if (size != 0) buf[0] = 0;
        return 0;
    }
    const std::string path = dir + "/OpenBA";
    return Posix::CopyOut(Posix::Utf8ToUtf16(path.c_str(), path.size()), buf, size);
}

// ---- E.7: the local date / time of a FILETIME ----------------------------------

// localtime_r + strftime("%x" / "%H:%M"): the C library's short date and an
// hour:minute time (GetDateFormatW DATE_SHORTDATE / GetTimeFormatW TIME_NOSECONDS).
// Retail formats uninitialised stack when the conversion fails; here a failed
// conversion yields the empty string.
void FormatLocalDateTime(const uint32_t* fileTime, bool timePart, PathChar* out) {
    const uint64_t ft = (static_cast<uint64_t>(fileTime[1]) << 32) | fileTime[0];
    const int64_t unixSeconds = static_cast<int64_t>(ft / 10000000u) - kFileTimeUnixEpochSeconds;
    const time_t t = static_cast<time_t>(unixSeconds);
    struct tm tmv;
    char text[kDateTimeChars];
    size_t n = 0;
    if (::localtime_r(&t, &tmv) != nullptr)
        n = ::strftime(text, sizeof(text), timePart ? "%H:%M" : "%x", &tmv);
    Posix::CopyOut(Posix::Utf8ToUtf16(text, n), out, kDateTimeChars);
}

}  // namespace Platform
