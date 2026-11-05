// tests/unit/ext_family_tree_picture_depth_test.cpp -- ext.family_tree's whole-town picture on a deep tree
// (review F-256, F-266). A save-tail tree is any parent / spouse graph Tree::Decode accepts, so a chain as
// long as the picture's own cap (kPictureMaxPeople) must lay out on a small stack, and the layout of an
// ordinary town must not change.
#include "unit.h"
#include "ext/family_tree/family_picture.h"
#include "ext/family_tree/family_tree.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <pthread.h>
#endif

using namespace Ext::FamilyTree;

namespace {

// A stack well under the 2 MB main-thread reserve, so a walk that recurses once per generation overflows in
// every flavour and a walk on an explicit stack does not.
constexpr size_t kSmallStack = 512u * 1024u;

struct DeepRun {
    bool spouses = false;   // a spouse chain instead of a mother chain
    int people = 0;
    int gens = 0;
};

Tree Chain(bool spouses) {
    Tree t;
    std::vector<uint32_t> ids;
    uint32_t prev = 0;
    for (int i = 0; i < kPictureMaxPeople; ++i) {
        if (spouses) {
            ids.push_back(t.Add("S" + std::to_string(i), static_cast<uint8_t>(i & 1), false, i, 20, 0, 0));
        } else {
            prev = t.Add("P" + std::to_string(i), 0, i > 0, i, 0, prev, 0);
        }
    }
    for (size_t i = 0; spouses && i + 1 < ids.size(); ++i)
        t.Marry(ids[i], ids[i + 1], static_cast<int32_t>(i));
    // through the save tail, the way a hostile record arrives
    std::vector<uint8_t> bytes;
    t.Encode(bytes);
    Tree back;
    if (!back.Decode(bytes.data(), bytes.size())) return Tree();
    return back;
}

void LayOut(DeepRun* run) {
    const Tree t = Chain(run->spouses);
    const PictureLayout layout = BuildPictureLayout(t, false, nullptr);
    run->people = layout.people;
    run->gens = layout.gens;
}

#ifdef _WIN32
DWORD WINAPI DeepThread(LPVOID arg) {
    LayOut(static_cast<DeepRun*>(arg));
    return 0;
}

bool RunOnSmallStack(DeepRun* run) {
    HANDLE h =
        CreateThread(nullptr, kSmallStack, DeepThread, run, STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
    if (h == nullptr) return false;
    WaitForSingleObject(h, INFINITE);
    CloseHandle(h);
    return true;
}
#else
void* DeepThread(void* arg) {
    LayOut(static_cast<DeepRun*>(arg));
    return nullptr;
}

bool RunOnSmallStack(DeepRun* run) {
    pthread_attr_t attr;
    if (pthread_attr_init(&attr) != 0) return false;
    pthread_attr_setstacksize(&attr, kSmallStack);
    pthread_t th;
    const bool started = pthread_create(&th, &attr, DeepThread, run) == 0;
    pthread_attr_destroy(&attr);
    if (started) pthread_join(th, nullptr);
    return started;
}
#endif

}  // namespace

// A 6000-long mother chain: one cluster per person, each below the one before.
UNIT_TEST(ext_family_tree_picture_deep_parent_chain_small_stack) {
    DeepRun run;
    run.spouses = false;
    CHECK(RunOnSmallStack(&run));
    CHECK_EQ(run.people, kPictureMaxPeople);
    CHECK_EQ(run.gens, kPictureMaxPeople);
}

// A 6000-long spouse chain: one marriage cluster, walked person to person.
UNIT_TEST(ext_family_tree_picture_deep_spouse_chain_small_stack) {
    DeepRun run;
    run.spouses = true;
    CHECK(RunOnSmallStack(&run));
    CHECK_EQ(run.people, kPictureMaxPeople);
    CHECK_EQ(run.gens, 1);
}

// The layout of a mixed town (remarriages, half siblings, a loop through a marriage, several lineages and
// bands) is the same as before the walks moved to explicit stacks: a hash of every box, couple and family,
// recorded on origin/main fa150cf7f.
UNIT_TEST(ext_family_tree_picture_layout_unchanged) {
    Tree t;
    uint32_t seed = 12345u;
    const auto next = [&seed](uint32_t mod) {
        seed = seed * 1664525u + 1013904223u;
        return (seed >> 8) % mod;
    };
    std::vector<uint32_t> ids;
    for (int i = 0; i < 900; ++i) {
        uint32_t mother = 0;
        uint32_t father = 0;
        if (!ids.empty() && next(5) != 0) {
            mother = ids[next(static_cast<uint32_t>(ids.size()))];
            father = ids[next(static_cast<uint32_t>(ids.size()))];
        }
        const std::string name = "N" + std::to_string(i) + std::string(next(9), 'x');
        const uint8_t gender = static_cast<uint8_t>(next(2));
        const int32_t age = static_cast<int32_t>(next(40));
        const uint32_t id = t.Add(name, gender, mother != 0, i, age, mother, father);
        ids.push_back(id);
        if (next(3) == 0 && ids.size() > 1) t.Marry(id, ids[next(static_cast<uint32_t>(ids.size()))], i);
        if (next(11) == 0) t.Died(ids[next(static_cast<uint32_t>(ids.size()))], i, 50, 1);
    }
    uint64_t h = 1469598103934665603ull;
    const auto mix = [&h](int64_t v) {
        h ^= static_cast<uint64_t>(v);
        h *= 1099511628211ull;
    };
    for (const bool living : {false, true}) {
        const PictureLayout layout = BuildPictureLayout(t, living, nullptr);
        mix(layout.people);
        mix(layout.gens);
        mix(layout.rows);
        mix(layout.width);
        for (const PictureBox& b : layout.boxes) {
            mix(b.id);
            mix(b.gen);
            mix(b.x);
            mix(b.w);
        }
        for (const auto& c : layout.couples) {
            mix(c.first);
            mix(c.second);
        }
        for (const PictureFamily& f : layout.families) {
            mix(f.mother);
            mix(f.father);
            for (const uint32_t k : f.kids) mix(k);
        }
    }
    std::printf("    picture layout hash %016llx\n", static_cast<unsigned long long>(h));
    CHECK_EQ(h, 0xa81c14c6a251f31cull);
}
