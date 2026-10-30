// ============================================================================
// tests/unit/copy_drift_test.cpp -- the literal-copy drift guard
// ============================================================================
// Three unit tests hold LITERAL COPIES of a production body, rewritten over plain structs
// so they can run on the host (the real bodies need a CharacterComponent, a ModelComponent,
// the pathfinder thread and the GameUpdateLoop singleton, which no host test can stand up):
//
//   still_path_test.cpp      Tail        <- citizenai_executor.cpp, the move tick's tail
//   ext_walk_batch_test.cpp  TailRef     <- the same tail
//   ext_walk_batch_test.cpp  ExecuteRef  <- moveto.cpp, MoveTo::Execute case 2
//   still_terrain_test.cpp   SampleHeight<- worldgeometry_ray.cpp, the terrain ray cast (also
//                                          driven for real through Map::GetTerrainPos there)
//
// The copies are restructured (a Walker struct for the character, the model and the
// description; helper calls for the retail constants), so their text cannot be compared
// with the production text. What CAN be pinned is the production text itself: each
// region sits between `[COPY-PINNED begin]` and `[COPY-PINNED end]` comment lines in
// src/, and this test hashes the region (all whitespace removed) and compares it with
// the value pinned below. A change to a pinned body therefore FAILS HERE, naming the copy
// to re-sync and the hash to re-pin; before 2026-10-17 such a change was invisible.
//
// Paths are relative to the repository root, the working directory of `run-unit` and
// of tools/build.sh unit. Run from anywhere else the test skips rather than passes.
// ============================================================================
#include "unit.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

bool ReadFile(const char* path, std::string& out) {
    FILE* f = std::fopen(path, "rb");
    if (f == nullptr) return false;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    std::fclose(f);
    return true;
}

// The region between the two markers, every whitespace byte removed. `pinned`
// names the copy a reader must re-sync (printed on a mismatch).
struct Region {
    const char* file;
    const char* copies;    // the test copies that mirror this region
    uint64_t    hash;      // the pinned FNV-1a 64 of the whitespace-stripped region
};

uint64_t Fnv1a(const std::string& s) {
    uint64_t h = 1469598103934665603ull;
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ull; }
    return h;
}

// Returns 0 when the file cannot be read, 1 on a match, 2 on a mismatch (and prints), 3
// when the markers are missing or out of order (and prints).
int CheckRegion(const Region& r) {
    std::string text;
    if (!ReadFile(r.file, text)) return 0;
    const char* kBegin = "[COPY-PINNED begin]";
    const char* kEnd   = "[COPY-PINNED end]";
    const size_t b = text.find(kBegin);
    const size_t e = b == std::string::npos ? std::string::npos : text.find(kEnd, b);
    if (b == std::string::npos || e == std::string::npos) {
        std::printf("  %s: the [COPY-PINNED begin] / [COPY-PINNED end] markers are missing\n", r.file);
        return 3;
    }
    // From the end of the begin marker's line to the start of the end marker's line.
    const size_t from = text.find('\n', b);
    std::string region = text.substr(from + 1, e - (from + 1));
    std::string stripped;
    stripped.reserve(region.size());
    for (char c : region)
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') stripped.push_back(c);
    const uint64_t h = Fnv1a(stripped);
    if (h == r.hash) return 1;
    std::printf("  %s: the [COPY-PINNED] region changed (hash 0x%016llx, pinned 0x%016llx).\n"
                "    Re-sync the literal copies in %s, then re-pin the hash in\n"
                "    tests/unit/copy_drift_test.cpp.\n",
                r.file, static_cast<unsigned long long>(h), static_cast<unsigned long long>(r.hash),
                r.copies);
    return 2;
}

const Region kRegions[] = {
    {"src/game/Component/citizenai_executor.cpp",
     "still_path_test.cpp (Tail) and ext_walk_batch_test.cpp (TailRef)",
     0xe65865fb0a534c76ull},
    {"src/game/Action/moveto.cpp",
     "ext_walk_batch_test.cpp (ExecuteRef)",
     0x55388122af05a199ull},
    {"src/game/Game/worldgeometry_ray.cpp",
     "still_terrain_test.cpp (World::SampleHeight, World::RayTri)",
     0xf57b5bc7a095d6a1ull},
};

}  // namespace

UNIT_TEST(copy_drift_pinned_regions_unchanged) {
    std::string probe;
    if (!ReadFile(kRegions[0].file, probe))
        UNIT_SKIP("the pinned sources are not readable: run the unit from the repository root");
    for (const Region& r : kRegions) {
        const int rc = CheckRegion(r);
        CHECK(rc != 0);          // readable (the first one was)
        CHECK(rc != 3);          // markers present
        CHECK(rc != 2);          // unchanged
    }
}

// The guard can tell: a region with one token changed hashes differently, and the
// whitespace normalisation ignores indentation and line ends but not tokens.
UNIT_TEST(copy_drift_hash_discriminates) {
    const std::string a = "int x = 1;\n  return x;\n";
    const std::string b = "int x = 1;\r\n\treturn x;\r\n";   // CRLF + indent: the same region
    const std::string c = "int x = 2;\n  return x;\n";       // one token: another region
    auto strip = [](const std::string& s) {
        std::string o;
        for (char ch : s) if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n') o.push_back(ch);
        return o;
    };
    CHECK_EQ(Fnv1a(strip(a)), Fnv1a(strip(b)));
    CHECK(Fnv1a(strip(a)) != Fnv1a(strip(c)));
}
