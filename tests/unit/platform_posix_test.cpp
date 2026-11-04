// ============================================================================
// tests/unit/platform_posix_test.cpp -- Phase D.4 POSIX: the src/platform/posix twins
// ============================================================================
// Compiled on every host (CMakeLists.txt globs tests/unit/*.cpp) but EMPTY on Windows:
// it pins what only the POSIX bodies decide (plans/THREADS.md F, the risks):
//   F.1  FileMove copies + unlinks across file systems (EXDEV) and, like MoveFileW,
//        refuses an existing destination;
//   F.2  FindFirst yields NTFS's upper-case collation order, "." and ".." first, and
//        matches its pattern case-INsensitively;
//   F.3  a thread asked for 0x10000 bytes of stack gets at least 2 MB;
//   F.4  the performance counter runs at 10 MHz;
//   F.5  the path spelling: an absolute POSIX path read through System::Filename is
//        "\home\..." (no ':'), still absolute when the primitives turn it back;
// plus DocumentsDir (getpwuid, never an env var), the fd + 1 file handle, the in-place
// recursive lock and the mmap allocator seam.
// ============================================================================

#if !defined(_WIN32)

#include "unit.h"

#include "platform_file.h"
#include "platform_os.h"
#include "platform_settings.h"
#include "platform_sync.h"
#include "platform_thread.h"
#include "platform_time.h"
#include "system/Filename.h"
#include "system/MemoryAllocator.h"
#include "system/String.h"

#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using rt::system::RtSysMemPlatform;
extern RtSysMemPlatform g_plat;                               // src/engine/system/allocator.cpp
extern "C" void RtSystemAllocatorInstallPlatform();           // src/platform/posix/allocator_posix.cpp

namespace {

using Platform::PathChar;

std::u16string U16(const std::string& s) {
    std::u16string out;
    for (unsigned char c : s) out.push_back(static_cast<char16_t>(c));   // ASCII paths only
    return out;
}

std::string U8(const PathChar* w) {
    std::string out;
    for (; *w != 0; ++w) out.push_back(static_cast<char>(*w));            // ASCII names only
    return out;
}

// A fresh directory under `parent`, removed (one level of files) by the destructor.
struct ScratchDir {
    std::string path;
    explicit ScratchDir(const char* parent) {
        std::string tmpl = std::string(parent) + "/banished_posix_unit_XXXXXX";
        std::vector<char> buf(tmpl.begin(), tmpl.end());
        buf.push_back('\0');
        if (mkdtemp(buf.data()) != nullptr) path = buf.data();
    }
    ~ScratchDir() {
        if (path.empty()) return;
        // The tests below create files only; best-effort cleanup of the names they use.
        static const char* const kNames[] = {"b.PKG", "A.pkg", "Z.pkg", "_x.pkg", "c.txt",
                                             "a2.pkm", "moved.bin", "dst.bin", "src.bin",
                                             "probe.txt", nullptr};
        for (const char* const* n = kNames; *n != nullptr; ++n) unlink((path + "/" + *n).c_str());
        rmdir(path.c_str());
    }
    std::string File(const char* name) const { return path + "/" + name; }
};

void Touch(const std::string& path, const char* text) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (f == nullptr) return;
    std::fputs(text, f);
    std::fclose(f);
}

std::string Slurp(const std::string& path) {
    std::string out;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) return out;
    char buf[256];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    std::fclose(f);
    return out;
}

bool Exists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

dev_t DeviceOf(const char* path) {
    struct stat st;
    return stat(path, &st) == 0 ? st.st_dev : static_cast<dev_t>(-1);
}

// The engine's spelling of a host path: '/' -> '\' (System::Filename::AdjustDelimiters).
std::string EngineSpelling(std::string p) {
    for (char& c : p) if (c == '/') c = '\\';
    return p;
}

}  // namespace

// ---- F.1 -------------------------------------------------------------------------

UNIT_TEST(posix_file_move_refuses_an_existing_destination) {
    ScratchDir d(P_tmpdir);
    CHECK(!d.path.empty());
    Touch(d.File("src.bin"), "source");
    Touch(d.File("dst.bin"), "already here");
    const std::u16string from = U16(d.File("src.bin")), to = U16(d.File("dst.bin"));
    CHECK_EQ(Platform::FileMove(from.c_str(), to.c_str()), 0);      // MoveFileW: ERROR_ALREADY_EXISTS
    CHECK(Slurp(d.File("dst.bin")) == "already here");
    CHECK(Exists(d.File("src.bin")));
}

UNIT_TEST(posix_file_move_crosses_file_systems) {
    // A save is written in the temp dir and then moved into the save folder, which on
    // Linux is routinely another file system (tmpfs /tmp, /dev/shm, a WSL /mnt/c).
    // rename() fails there with EXDEV where MoveFileW copies: FileMove must copy.
#if defined(__APPLE__)
    const char* kOther = "."; // No second writable filesystem in the macOS spike sandbox.
#else
    const char* kOther = "/dev/shm";
#endif
    ScratchDir a(kOther), b(".");
    CHECK(!a.path.empty() && !b.path.empty());
    const bool crossDevice = DeviceOf(a.path.c_str()) != DeviceOf(b.path.c_str());
    if (!crossDevice)
        std::printf("  note: %s and . share a file system here; EXDEV not exercised\n", kOther);
    Touch(a.File("moved.bin"), "payload across devices");
    const std::u16string from = U16(a.File("moved.bin")), to = U16(b.File("moved.bin"));
    CHECK_EQ(Platform::FileMove(from.c_str(), to.c_str()), 1);
    CHECK(!Exists(a.File("moved.bin")));
    CHECK(Slurp(b.File("moved.bin")) == "payload across devices");
}

// ---- F.2 -------------------------------------------------------------------------

UNIT_TEST(posix_find_first_is_ntfs_ordered_and_case_insensitive) {
    ScratchDir d(P_tmpdir);
    CHECK(!d.path.empty());
    // '_' (0x5F) sorts AFTER 'Z' (0x5A) once upper-cased, but before 'b' (0x62) in a
    // case-sensitive byte order: the upcase collation is what NTFS returns.
    for (const char* n : {"b.PKG", "_x.pkg", "Z.pkg", "c.txt", "A.pkg", "a2.pkm"})
        Touch(d.File(n), "x");

    // The engine's spelling: '\' separators (FindFiles builds dir + "\*.pkg").
    const std::u16string pattern = U16(EngineSpelling(d.path) + "\\*.pkg");
    Platform::FindData fd;
    Platform::FindHandle h = Platform::FindFirst(pattern.c_str(), &fd);
    CHECK(h != Platform::kInvalidFindHandle);
    std::vector<std::string> names;
    if (h != Platform::kInvalidFindHandle) {
        do {
            names.push_back(U8(fd.name));
            CHECK((fd.attributes & Platform::kFileAttributeDirectory) == 0);
        } while (Platform::FindNext(h, &fd));
        CHECK_EQ(Platform::FindClose(h), 1);
    }
    const std::vector<std::string> want = {"A.pkg", "b.PKG", "Z.pkg", "_x.pkg"};
    CHECK(names == want);

    // "*" lists "." and ".." first, flagged as directories.
    const std::u16string all = U16(d.path + "/*");
    h = Platform::FindFirst(all.c_str(), &fd);
    CHECK(h != Platform::kInvalidFindHandle);
    if (h != Platform::kInvalidFindHandle) {
        CHECK(U8(fd.name) == ".");
        CHECK((fd.attributes & Platform::kFileAttributeDirectory) != 0);
        CHECK_EQ(Platform::FindNext(h, &fd), 1);
        CHECK(U8(fd.name) == "..");
        int files = 0;
        while (Platform::FindNext(h, &fd)) ++files;
        CHECK_EQ(files, 6);
        Platform::FindClose(h);
    }

    // No match: INVALID_HANDLE_VALUE, as FindFirstFileW's ERROR_FILE_NOT_FOUND.
    const std::u16string none = U16(d.path + "/*.sav");
    CHECK(Platform::FindFirst(none.c_str(), &fd) == Platform::kInvalidFindHandle);
}

// ---- F.3 -------------------------------------------------------------------------

namespace {
size_t g_stackSeen = 0;
uint32_t g_threadIdSeen = 0;
unsigned int StackProbe(void*) {
#if defined(__APPLE__)
    g_stackSeen = pthread_get_stacksize_np(pthread_self());
#else
    pthread_attr_t attr;
    if (pthread_getattr_np(pthread_self(), &attr) == 0) {
        pthread_attr_getstacksize(&attr, &g_stackSeen);
        pthread_attr_destroy(&attr);
    }
#endif
    g_threadIdSeen = Platform::CurrentThreadId();
    return 0;
}
}  // namespace

UNIT_TEST(posix_thread_stack_is_at_least_two_megabytes) {
    g_stackSeen = 0;
    Platform::ThreadHandle t = Platform::ThreadCreateSuspended(&StackProbe, nullptr, 0x10000);
    CHECK(t != nullptr);
    CHECK_EQ(g_stackSeen, static_cast<size_t>(0));        // suspended: not run yet
    CHECK_EQ(Platform::ThreadResume(t), 1u);              // the previous suspend count
    CHECK_EQ(Platform::ThreadJoin(t), 0u);                // WAIT_OBJECT_0
    CHECK(Platform::ThreadClose(t));
    CHECK(g_stackSeen >= static_cast<size_t>(2u * 1024u * 1024u));
    CHECK(g_threadIdSeen != 0 && g_threadIdSeen != 0xFFFFFFFFu);
    CHECK(g_threadIdSeen != Platform::CurrentThreadId());
}

// ---- F.4 -------------------------------------------------------------------------

UNIT_TEST(posix_perf_counter_runs_at_ten_megahertz) {
    int64_t hz = 0, a = 0, b = 0;
    CHECK(Platform::PerfFrequency(&hz));
    CHECK_EQ(hz, static_cast<int64_t>(10000000));
    CHECK(Platform::PerfCounter(&a));
    Platform::SleepMs(20);
    CHECK(Platform::PerfCounter(&b));
    const int64_t ticks = b - a;
    CHECK(ticks >= 150000);          // >= 15 ms at 10 MHz
    CHECK(ticks < 20000000);         // < 2 s: not a 1 GHz count
    // The wall clock lands after 2020-01-01 in FILETIME units.
    CHECK(Platform::NowFileTime() > Platform::FileTimeFromUnix(1577836800, 0));
}

// ---- F.5 + DocumentsDir ----------------------------------------------------------

UNIT_TEST(posix_documents_dir_is_the_xdg_data_home) {
    std::string dir;
    CHECK(Platform::DocumentsDir(dir));
    CHECK(!dir.empty() && dir[0] == '/');
#if defined(__APPLE__)
    const std::string tail = "/Documents";   // macOS: ~/Documents/Banished/Save, as the original game
#else
    const std::string tail = "/.local/share";
#endif
    CHECK(dir.size() > tail.size() &&
          dir.compare(dir.size() - tail.size(), tail.size(), tail) == 0);
    System::String s;
    CHECK(Platform::DocumentsDir(s));
    CHECK_STREQ(s._data, dir.c_str());
}

UNIT_TEST(posix_absolute_paths_read_through_filename_stay_absolute) {
    // GetRootDirectory: ExecutablePath -> WideString -> UTF-8 -> Filename (which forces
    // '\') -> GetPathDirectory. The POSIX exe path comes back '/'-absolute ...
    PathChar exe[0x1000];
    const uint32_t n = Platform::ExecutablePath(exe, 0x1000);
    CHECK(n > 1);
    CHECK(exe[0] == u'/');

    // ... and Filename spells it "\home\...": a leading '\', no ':' anywhere. The
    // Windows marker (the ':' at index 1 that GetPathExtension's `c > 1` test skips
    // and GetPathIdentifer stops at) is simply absent.
    const System::Filename root("/home/user/Banished/OpenBA");
    CHECK_STREQ(root._data, "\\home\\user\\Banished\\OpenBA");
    const System::Filename dir = root.GetPathDirectory();
    CHECK_STREQ(dir._data, "\\home\\user\\Banished\\");
    // ExpandRelativePath's identifier split of a candidate under that root: no ':' and
    // a path character seen, so no identifier (it then defaults to "resource").
    const System::Filename candidate("/home/user/Banished/data/x_resource.crs");
    const System::Filename ident = candidate.GetPathIdentifer();
    CHECK(ident._data == nullptr);

    // The primitives turn the engine spelling back into the absolute host path.
    ScratchDir d(P_tmpdir);
    Touch(d.File("probe.txt"), "p");
    const std::u16string engine = U16(EngineSpelling(d.File("probe.txt")));
    CHECK(engine[0] == u'\\');
    CHECK(Platform::PathAttributes(engine.c_str()) != Platform::kInvalidPathAttributes);
    const std::u16string engineDir = U16(EngineSpelling(d.path));
    CHECK(Platform::PathAttributes(engineDir.c_str()) == Platform::kFileAttributeDirectory);
    const std::u16string missing = U16(EngineSpelling(d.File("absent.txt")));
    CHECK(Platform::PathAttributes(missing.c_str()) == Platform::kInvalidPathAttributes);
}

// ---- files, locks, the allocator seam -----------------------------------------------

UNIT_TEST(posix_file_handles_are_fd_plus_one_and_round_trip) {
    PathChar tempName[Platform::kMaxPath];
    const std::u16string prefix = u"bns";
    Platform::FileHandle h = Platform::FileCreateTemp(prefix.c_str(), tempName);
    CHECK(h != Platform::kInvalidFileHandle && h != nullptr);
    const std::string name = U8(tempName);
    CHECK(name.find("/bns") != std::string::npos);

    uint32_t written = 0, got = 0;
    CHECK_EQ(Platform::FileWrite(h, "0123456789", 10, &written), 1);
    CHECK_EQ(written, 10u);
    CHECK_EQ(Platform::FileSize(h), 10u);
    uint64_t ft = 0;
    CHECK_EQ(Platform::FileLastWrite(h, &ft), 1);
    CHECK(ft > Platform::FileTimeFromUnix(1577836800, 0));
    CHECK_EQ(Platform::FileClose(h), 1);

    h = Platform::FileOpenRead(tempName);
    CHECK(h != Platform::kInvalidFileHandle && h != nullptr);
    CHECK_EQ(Platform::FileSeek32(h, 4), 4u);
    char buf[16] = {};
    CHECK_EQ(Platform::FileRead(h, buf, sizeof(buf), &got), 1);   // a short read at EOF succeeds
    CHECK_EQ(got, 6u);
    CHECK(std::memcmp(buf, "456789", 6) == 0);
    CHECK_EQ(Platform::FileClose(h), 1);

    CHECK_EQ(Platform::FileDelete(tempName), 1);
    CHECK(Platform::FileOpenRead(tempName) == Platform::kInvalidFileHandle);

    PathChar text[Platform::kDateTimeChars];
    const uint32_t when[2] = {static_cast<uint32_t>(ft), static_cast<uint32_t>(ft >> 32)};
    Platform::FormatLocalDateTime(when, true, text);
    CHECK(text[0] != 0 && text[2] == u':');                        // "%H:%M"
    Platform::FormatLocalDateTime(when, false, text);
    CHECK(text[0] != 0);
}

// Not analysed: the recursive re-take is the point, and clang's thread-safety analysis does not model it.
UNIT_TEST(posix_lock_is_recursive_in_place) NO_THREAD_SAFETY_ANALYSIS {
    Platform::LockStorage lock;
    std::memset(&lock, 0xCD, sizeof(lock));
    Platform::MutexInit(&lock, 5000);
    Platform::MutexLock(&lock);
    Platform::MutexLock(&lock);       // recursive: CRITICAL_SECTION semantics
    Platform::MutexUnlock(&lock);
    Platform::MutexUnlock(&lock);
    Platform::MutexDestroy(&lock);

    Platform::OnceFlag once = {nullptr};
    static int runs = 0;
    runs = 0;
    Platform::OnceRun(&once, [] { ++runs; });
    Platform::OnceRun(&once, [] { ++runs; });
    CHECK_EQ(runs, 1);
}

UNIT_TEST(posix_allocator_seam_reserves_commits_and_zeroes) {
    RtSystemAllocatorInstallPlatform();
    const uint64_t page = g_plat.pageSize();
    CHECK(page >= 4096 && (page & (page - 1)) == 0);
    uint8_t* base = static_cast<uint8_t*>(g_plat.reserve(nullptr, page * 4));  // CAST-OK: alloc_header
    CHECK(base != nullptr);
    CHECK(Platform::IsReadablePage(base) == false);                 // reserved, not committed
    CHECK(g_plat.commit(base + page, page) == base + page);
    CHECK(Platform::IsReadablePage(base + page));
    base[page] = 0x5A;
    g_plat.decommit(base + page, page);
    CHECK(Platform::IsReadablePage(base + page) == false);
    CHECK(g_plat.commit(base + page, page) == base + page);
    CHECK_EQ(base[page], 0);                                        // a recommitted page reads zero
    g_plat.release(base);
    CHECK(Platform::IsReadablePage(base + page) == false);
    g_plat.release(nullptr);                                        // VirtualFree(NULL) fails quietly
}

#endif  // !_WIN32
