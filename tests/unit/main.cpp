// tests/unit/main.cpp -- runs every UNIT_TEST in link order. Exit code = failures (a
// skipped test, UNIT_SKIP in unit.h, is counted and listed but never fails the run).
//
// `--shard K/N` runs only the tests whose position in the list is K modulo N (0 <= K < N), so N
// processes started with K = 0 .. N-1 run every test exactly once between them; `tools/build.sh unit
// --shards N` starts them in parallel and adds up their summaries (tests/unit/README.md).
// `--filter TEXT` runs only the tests whose name contains TEXT (applied before the shard cut).
#include "unit.h"

#include <chrono>

namespace {
bool ParseShard(const char* arg, int& index, int& count) {
    char slash = 0;
    char tail = 0;
    if (std::sscanf(arg, "%d%c%d%c", &index, &slash, &count, &tail) != 3 || slash != '/') return false;
    return count >= 1 && index >= 0 && index < count;
}
}  // namespace

int main(int argc, char** argv) {
    int shardIndex = 0;
    int shardCount = 1;
    const char* filter = nullptr;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--filter") == 0 && i + 1 < argc) {
            filter = argv[++i];
            continue;
        }
        if (std::strcmp(argv[i], "--shard") == 0 && i + 1 < argc && ParseShard(argv[i + 1], shardIndex, shardCount)) {
            ++i;
            continue;
        }
        std::fprintf(stderr, "usage: unit [--shard K/N] [--filter TEXT]   (0 <= K < N)\n");
        return 2;
    }
    int tests = 0;
    int position = 0;
    for (unit::Test* t = unit::Head(); t; t = t->next) {
        if (filter && !std::strstr(t->name, filter)) continue;
        if (position++ % shardCount != shardIndex) continue;
        const int before = unit::Failures();
        const int skippedBefore = unit::Skipped();
        unit::Current() = t->name;
        const auto t0 = std::chrono::steady_clock::now();
        t->fn();
        const double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        unit::Current() = nullptr;
        ++tests;
        const char* verdict = unit::Failures() != before ? "FAIL"
                            : unit::Skipped() != skippedBefore ? "skip" : "ok  ";
        // A test of a second or more says how long it took: the suite's wall time is a CI cost.
        if (sec >= 1.0)
            std::printf("%s %s (%.1f s)\n", verdict, t->name, sec);
        else
            std::printf("%s %s\n", verdict, t->name);
        // Flushed per test: stdout to a pipe or a file is block-buffered off Windows, and a crash
        // would otherwise lose the lines that say which test was running.
        std::fflush(stdout);
    }
    std::printf("%d tests, %d failures, %d skipped\n", tests, unit::Failures(), unit::Skipped());
    for (unit::Skip* s = unit::Skips(); s; s = s->next)
        std::printf("  skipped %s: %s\n", s->name, s->reason);
    return unit::Failures() ? 1 : 0;
}
