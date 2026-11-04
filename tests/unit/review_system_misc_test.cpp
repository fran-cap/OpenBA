// ============================================================================
// tests/unit/review_system_misc_test.cpp -- review 2026-11-03, group system-misc
// ============================================================================
// One test per finding the unit build can reach (docs/history/review_20261103/
// findings_p8.json). Every malformed input is inline.
//   * F-102  MemoryAllocator::Allocate: a request near 2^64 wraps `size + 7` / `total + 8`
//            to a tiny block; it must end in FatalOutOfMemory like any other request the
//            heap cannot hold. Run in a child process (the fatal path exits), Windows
//            through CreateProcessW, POSIX through fork.
//   * F-125  String::FormatIndexed: '@n' past the arguments the caller passed reads an
//            empty String, not a vararg that was never passed.
//   * F-126  Random::Serialize: a saved _index of 624 or more cannot index past _mt.
//   * F-142  BinaryReader::Skip: a length word near INT_MAX wraps to retail's position.
//   * F-149  String::ToInteger: eleven digits wrap to retail's 32-bit value.
//   * F-151  Timer::CurrentTimeTicks: eleven days of uptime still read eleven days.
//   * F-152 / F-168  Platform::FormatLocalDateTime (Windows only: the POSIX twins already
//            return the empty string): a FILETIME the conversion refuses formats as "".
// F-142 and F-149 return retail's wrapped value both before and after their fix; what
// the fix removes is the signed overflow, which only a sanitizer or -ftrapv build sees.
// ============================================================================
#include "unit.h"

#include <cstdint>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>

#include "io/Serializer.h"
#include "io/Stream.h"
#include "platform_file.h"
#include "platform_time.h"
#include "system/Error.h"
#include "system/MemoryAllocator.h"
#include "system/String.h"
#include "system/Timer.h"
#include "system/Value.h"

#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {

// A read-only stream over a byte vector; every SetCurrentPos is logged.
struct ByteStream final : IO::Stream {
    std::vector<uint8_t> bytes;
    int32_t pos = 0;
    std::vector<int32_t> seeks;

    bool Write(int, const void*) override { return false; }
    bool Read(int n, void* dst) override {
        if (n < 0 || static_cast<size_t>(pos) + static_cast<size_t>(n) > bytes.size()) return false;
        std::memcpy(dst, bytes.data() + pos, static_cast<size_t>(n));
        pos += n;
        return true;
    }
    bool SetCurrentPos(int p) override {
        seeks.push_back(p);
        pos = p;
        return true;
    }
    uint32_t GetCurrentPos() const override { return static_cast<uint32_t>(pos); }
    uint32_t GetSize() const override { return static_cast<uint32_t>(bytes.size()); }
};

void PutU32(std::vector<uint8_t>& out, uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>(v >> (8 * i)));
}

// IO::Serializer's two words ({stream, readerWriter}), as decode_exact_test.cpp builds
// them: the BinaryReader constructor consumes the record header {magic, length, version}.
struct Reader {
    alignas(8) uint8_t serBytes[16] = {};
    IO::Serializer& ser = *reinterpret_cast<IO::Serializer*>(serBytes);
    IO::BinaryReader* rw = nullptr;
    alignas(IO::BinaryReader) uint8_t rwBytes[sizeof(IO::BinaryReader)] = {};

    explicit Reader(ByteStream& s) {
        ser._stream = &s;
        rw = ::new (static_cast<void*>(rwBytes)) IO::BinaryReader(ser, 0);
        ser._readerWriter = rw;
    }
    // The destructor would read the record trailer; nothing here owns memory.
};

}  // namespace

// ---------------------------------------------------------------------------
// F-126
// ---------------------------------------------------------------------------
UNIT_TEST(sysmisc_random_serialize_bounds_the_saved_index) {
    std::vector<uint8_t> rec;
    PutU32(rec, 0xaaaaaaaau);        // magic
    PutU32(rec, 4 * 625 + 4);        // length
    PutU32(rec, 0);                  // version
    PutU32(rec, 0x270 + 8);          // _index: past the 624-word state
    for (uint32_t i = 0; i < 624; ++i) PutU32(rec, i * 2654435761u);
    ByteStream s;
    s.bytes = rec;
    Reader r(s);
    System::Random rng(1);
    rng.Serialize(r.ser);
    CHECK(rng._index < 0x270u);
    CHECK_EQ(rng._mt[1], 2654435761u);   // the state words are retail's read
    // An in-range index loads as saved.
    rec[12] = 0x17;
    rec[13] = 0x02;                  // 0x217
    ByteStream s2;
    s2.bytes = rec;
    Reader r2(s2);
    System::Random rng2(1);
    rng2.Serialize(r2.ser);
    CHECK_EQ(rng2._index, 0x217u);
}

// ---------------------------------------------------------------------------
// F-142
// ---------------------------------------------------------------------------
UNIT_TEST(sysmisc_binaryreader_skip_wraps_like_retail) {
    std::vector<uint8_t> rec;
    PutU32(rec, 0xaaaaaaaau);
    PutU32(rec, 0x7fffffffu);        // the record length word, near INT_MAX
    PutU32(rec, 0);
    ByteStream s;
    s.bytes = rec;
    Reader r(s);
    r.rw->Skip();
    CHECK_EQ(s.seeks.size(), static_cast<size_t>(1));
    // retail: MOV EDX,[RCX+0x24] / ADD EDX,[RCX+0x20], a 32-bit add: 8 + 0x7fffffff wraps.
    CHECK_EQ(static_cast<uint32_t>(s.seeks[0]), 0x80000007u);
}

// ---------------------------------------------------------------------------
// F-149
// ---------------------------------------------------------------------------
UNIT_TEST(sysmisc_tointeger_wraps_like_retail) {
    // 99999999999 mod 2^32 = 1215752191; retail's 32-bit LEA chain wraps to it.
    CHECK_EQ(System::String::ToInteger("99999999999"), 1215752191);
    CHECK_EQ(System::String::ToInteger("-99999999999"), -1215752191);
    CHECK_EQ(System::String::ToInteger("2147483648"), static_cast<int>(0x80000000u));
    CHECK_EQ(System::String::ToInteger("  -42x"), -42);
    CHECK_EQ(System::String::ToInteger("+7"), 7);
}

// ---------------------------------------------------------------------------
// F-125
// ---------------------------------------------------------------------------
UNIT_TEST(sysmisc_formatindexed_reads_only_the_passed_args) {
    // The eventlog case-0 shape: two args, a template naming @2 and @9 (a mod's text).
    System::String a = System::String::FormatIndexed("@0 @1 @2 @9!", "Spring", "Year 3");
    CHECK_STREQ(a._data, "Spring Year 3  !");
    System::String b = System::String::FormatIndexed("@1-@0", "x", "y");
    CHECK_STREQ(b._data, "y-x");
    System::String c = System::String::FormatIndexed("no params", "x");
    CHECK_STREQ(c._data, "no params");
}

// ---------------------------------------------------------------------------
// F-151
// ---------------------------------------------------------------------------
UNIT_TEST(sysmisc_timer_ticks_after_eleven_days) {
    (void)System::Timer::CurrentTimeTicks();   // runs PlatformStartup
    const int64_t savedInitial = System::Timer::_systemInitialTick;
    const int64_t freq = System::Timer::_systemTicksPerSecond;
    CHECK(freq > 0);
    int64_t now = 0;
    Platform::PerfCounter(&now);
    const int64_t elevenDays = 11ll * 24 * 3600;
    System::Timer::_systemInitialTick = now - elevenDays * freq;
    const int64_t ticks = System::Timer::CurrentTimeTicks();
    const float seconds = System::Timer::CurrentTime();
    System::Timer::_systemInitialTick = savedInitial;
    CHECK(ticks >= elevenDays * 1000000ll);
    CHECK(ticks < (elevenDays + 60) * 1000000ll);
    CHECK(seconds >= static_cast<float>(elevenDays) - 1.0f);
    CHECK(seconds < static_cast<float>(elevenDays + 60));
}

// ---------------------------------------------------------------------------
// F-152 / F-168
// ---------------------------------------------------------------------------
UNIT_TEST(sysmisc_failed_datetime_conversion_formats_empty) {
#if defined(_WIN32)
    // FILETIME 0xffffffff'ffffffff: FileTimeToSystemTime refuses it in every time zone.
    const uint32_t ft[2] = {0xffffffffu, 0xffffffffu};
    for (int part = 0; part < 2; ++part) {
        Platform::PathChar out[Platform::kDateTimeChars];
        for (uint32_t i = 0; i < Platform::kDateTimeChars; ++i) out[i] = 0xcccc;
        Platform::FormatLocalDateTime(ft, part == 1, out);
        CHECK_EQ(static_cast<uint32_t>(out[0]), 0u);
    }
    System::DateTime bad;
    bad._lowDateTime = 0xffffffffu;
    bad._highDateTime = 0xffffffffu;
    const System::String date = bad.DateAsString();
    const System::String time = bad.TimeAsString();
    CHECK(date._data == nullptr || date._data[0] == 0);
    CHECK(time._data == nullptr || time._data[0] == 0);
    // A valid time still formats.
    const uint32_t good[2] = {0xd53e8000u, 0x01d2b28du};   // 2017-03-31
    Platform::PathChar out[Platform::kDateTimeChars];
    Platform::FormatLocalDateTime(good, false, out);
    CHECK(out[0] != 0);
#else
    UNIT_SKIP("Windows twin only: the POSIX FormatLocalDateTime already returns \"\" on failure");
#endif
}

// ---------------------------------------------------------------------------
// F-102 -- the child half: allocate near 2^64 on a fresh heap. Reaching the end means
// the request was handed a block; the fixed allocator ends the process in
// FatalOutOfMemory (exit code -1) before that.
// ---------------------------------------------------------------------------
namespace {

[[noreturn]] void AllocatorWrapChild() {
    RtSystemAllocatorInstallPlatform();
    System::Error::_hideDisplay = true;   // no box: DisplayStopIfRequested exits
    System::MemoryAllocator a{};
    a.minBin = 0xbf;
    if (!a.Startup(1, 0)) {
#if defined(_WIN32)
        ExitProcess(2u);
#else
        _exit(2);
#endif
    }
    void* big = a.Allocate(0xFFFFFFFFFFFFFFF8ull, 8);
    void* next = a.Allocate(64, 8);
    // Before the fix both come back, and `next` lies inside `big`'s block.
    const int overlap = (big != nullptr && next == big) ? 3 : 0;
#if defined(_WIN32)
    ExitProcess(static_cast<UINT>(overlap));
#else
    _exit(overlap);
#endif
}

}  // namespace

#if defined(_WIN32)
// Runs only when this process was started as the child (its command line names it).
UNIT_TEST(sysmisc_allocator_wrap_child) {
    if (std::wcsstr(GetCommandLineW(), L"--filter sysmisc_allocator_wrap_child") == nullptr) return;
    AllocatorWrapChild();
}
#endif

UNIT_TEST(sysmisc_allocator_huge_request_is_fatal) {
#if defined(_WIN32)
    wchar_t exe[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, exe, MAX_PATH);
    CHECK(n > 0 && n < MAX_PATH);
    std::wstring cmd = L"\"";
    cmd += exe;
    cmd += L"\" --filter sysmisc_allocator_wrap_child";
    STARTUPINFOW si;
    std::memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = nullptr;
    si.hStdOutput = nullptr;
    si.hStdError = nullptr;
    PROCESS_INFORMATION pi;
    std::memset(&pi, 0, sizeof(pi));
    std::vector<wchar_t> line(cmd.begin(), cmd.end());
    line.push_back(0);
    const BOOL ok = CreateProcessW(exe, line.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                                   nullptr, nullptr, &si, &pi);
    CHECK(ok);
    if (!ok) return;
    WaitForSingleObject(pi.hProcess, 60000);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    std::printf("  [sysmisc] allocator child exit code 0x%lx\n", static_cast<unsigned long>(code));
    CHECK_EQ(static_cast<uint32_t>(code), 0xffffffffu);
#else
    std::fflush(nullptr);
    const pid_t pid = fork();
    CHECK(pid >= 0);
    if (pid < 0) return;
    if (pid == 0) AllocatorWrapChild();
    int status = 0;
    waitpid(pid, &status, 0);
    std::printf("  [sysmisc] allocator child status 0x%x\n", status);
    CHECK(WIFEXITED(status));
    CHECK_EQ(WEXITSTATUS(status), 255);
#endif
}
