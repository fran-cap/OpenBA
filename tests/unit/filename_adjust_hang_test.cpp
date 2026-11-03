// ============================================================================
// tests/unit/filename_adjust_hang_test.cpp -- review finding F-078:
//   Filename(const char*) never returned on some "\.." names
// ============================================================================
// AdjustDelimiters (retail 0x1800c6970) keeps `lastDelim` across its outer
// re-scans. After a splice, a stale `lastDelim` at or past the next "\.."
// splices a string no shorter than the one it started from, and the outer loop
// then repeats that forever (the same string, or one that grows by a byte per
// turn). The inputs below are inline; a watchdog turns the hang into a failure.
// ============================================================================
#include "unit.h"
#include "system/Filename.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

using System::Filename;

namespace {

// Ends the process with a failure when a test body has not finished in time:
// before the fix the AdjustDelimiters call below never returns.
class Watchdog {
public:
    explicit Watchdog(const char* what) : _what(what) {
        _thread = std::thread([this] {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            while (!_done.load()) {
                if (std::chrono::steady_clock::now() > deadline) {
                    std::printf("  FAIL %s: AdjustDelimiters did not return within 10 s\n", _what);
                    std::fflush(stdout);
                    std::_Exit(1);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
        });
    }
    ~Watchdog() {
        _done.store(true);
        _thread.join();
    }

private:
    const char* _what;
    std::atomic<bool> _done{false};
    std::thread _thread;
};

std::string Adjusted(const char* raw, size_t n) {
    const std::string in(raw, n);
    Filename f(in.c_str());
    return f._data ? std::string(f._data, static_cast<size_t>(f.GetLength())) : std::string("(null)");
}

}  // namespace

UNIT_TEST(filename_adjust_hang_same_string_splice_returns) {
    // The fuzz input (package_cache2/filename_adjust_hang, 9 bytes):
    // 5c 2e 2e 5c 5c 2e 2e 08 2e  = "\..\\..\x08."
    // The first splice gives "\..\x08." with lastDelim = 3 left over; the
    // re-scan then splices "\..\x08." into itself forever.
    static const char kInput[] = { '\x5c', '\x2e', '\x2e', '\x5c', '\x5c', '\x2e', '\x2e', '\x08', '\x2e' };
    Watchdog dog("filename_adjust_hang_same_string_splice_returns");
    const std::string out = Adjusted(kInput, sizeof(kInput));
    CHECK_EQ(out, std::string("\\..\x08."));
}

UNIT_TEST(filename_adjust_hang_growing_splice_returns) {
    // 5c 2e 2e 61 5c 5c 2e 2e 61 61  = "\..a\\..aa"
    // The first splice gives "\..aaa" with lastDelim = 4 left over; each
    // re-scan then splices it into a string one byte LONGER ("\..aaaa", ...),
    // so a check for "spliced == current" alone would not end this one.
    static const char kInput[] = { '\x5c', '\x2e', '\x2e', '\x61', '\x5c', '\x5c', '\x2e', '\x2e', '\x61', '\x61' };
    Watchdog dog("filename_adjust_hang_growing_splice_returns");
    const std::string out = Adjusted(kInput, sizeof(kInput));
    CHECK_EQ(out, std::string("\\..aaa"));
}

UNIT_TEST(filename_adjust_hang_rooted_dotdot_returns) {
    // 5c 2e 2e 5c 61 5c 2e 2e 5c 62  = "\..\a\..\b"
    // The first splice gives "\..\b" with lastDelim = 3 left over; the
    // re-scan then splices "\..\b" into itself forever.
    static const char kInput[] = { '\x5c', '\x2e', '\x2e', '\x5c', '\x61', '\x5c', '\x2e', '\x2e', '\x5c', '\x62' };
    Watchdog dog("filename_adjust_hang_rooted_dotdot_returns");
    const std::string out = Adjusted(kInput, sizeof(kInput));
    CHECK_EQ(out, std::string("\\..\\b"));
}

UNIT_TEST(filename_adjust_hang_valid_splices_unchanged) {
    // Ordinary ".." paths still collapse as retail does.
    Watchdog dog("filename_adjust_hang_valid_splices_unchanged");
    CHECK_EQ(Adjusted("a\\b\\..\\c", 8), std::string("a\\c"));
    CHECK_EQ(Adjusted("Data/Game/../Template/Tree.rsc", 30), std::string("Data\\Template\\Tree.rsc"));
}
