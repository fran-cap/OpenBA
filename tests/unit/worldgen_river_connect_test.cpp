// ============================================================================
// tests/unit/worldgen_river_connect_test.cpp -- cd460's river-point labeling and connect pass
// ============================================================================
// wg_f390_rivers (0x1400cd460, src/game/Game/worldgen_f390_rivers.cpp) labels the river
// points into clusters of touching points (wg_RiverClusterLabels: a point touching several
// clusters merges them into the lowest label) and joins every other cluster to cluster 0
// (wg_RiverConnectClusters: two Random::Rand draws per extra cluster, {x, y, 10.0} points
// every 2.0 units from the drawn member toward its nearest cluster-0 member, the cluster
// then merged into cluster 0). These tests pin both on hand-placed points.
// ============================================================================
#include "unit.h"
#include "Game/TerrainHeightfield.h"
#include "system/MemoryAllocator.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>
#include <vector>

namespace {

using StampArray = Collection::Array<TerrainStamp>;

union FieldStorage {
    FieldStorage() {}
    ~FieldStorage() {}
    TerrainHeightfield hf;
};

void AddPoint(TerrainHeightfield* hf, float x, float y, float radius) {
    TerrainStamp rec = { x, y, radius };
    hf->riverPoints.SetSize(hf->riverPoints._size + 1, rec);
}

std::vector<int32_t> Labels(const std::vector<TerrainStamp>& points) {
    std::vector<int32_t> labels(points.size(), 7);
    wg_RiverClusterLabels(points.data(), static_cast<int>(points.size()), labels.data());
    return labels;
}

// The host has no seated MemoryAllocator: the connect pass's buckets and the river-point
// growth go through the instrument seat, armed with the CRT heap
// (compressed_stream_bounds_test.cpp does the same), which is also what the host's
// Collection::Array uses for the points AddPoint stores and ~StampArray releases.
void* SeatAlloc(uint64_t size, int) {
    return std::malloc(static_cast<size_t>(size));
}
void SeatFree(void* p) {
    std::free(p);
}
struct HeapSeat {
    HeapSeat() {
        System::g_memoryAllocatorInstrument.memAlloc = &SeatAlloc;
        System::g_memoryAllocatorInstrument.memFree = &SeatFree;
    }
    ~HeapSeat() {
        System::g_memoryAllocatorInstrument.memAlloc = nullptr;
        System::g_memoryAllocatorInstrument.memFree = nullptr;
    }
};

// Runs the connect pass over the points already in hf, labelled first.
void Connect(TerrainHeightfield* hf) {
    HeapSeat seat;
    int count = hf->riverPoints._size;
    std::vector<int32_t> labels(static_cast<size_t>(count), 0);
    wg_RiverClusterLabels(hf->riverPoints._data, count, labels.data());
    wg_RiverConnectClusters(hf, labels.data(), count);
}

bool SameRandom(const System::Random& a, const System::Random& b) {
    return std::memcmp(&a, &b, sizeof(System::Random)) == 0;
}

}  // namespace

// Separate points keep their own label: point i's fresh label is i.
UNIT_TEST(river_labels_separate_points_get_their_index) {
    std::vector<TerrainStamp> pts = { {0, 0, 1}, {100, 0, 1}, {200, 0, 1} };
    std::vector<int32_t> labels = Labels(pts);
    CHECK_EQ(labels[0], 0);
    CHECK_EQ(labels[1], 1);
    CHECK_EQ(labels[2], 2);
}

// A point touching two clusters merges both into the lower label (retail relabels every
// earlier point whose label was touched, 0x1400cdd57).
UNIT_TEST(river_labels_bridge_point_merges_both_clusters) {
    // p2 is 50 from p0 and p1: 50 * 1.2 = 60 < 60 + 1.
    std::vector<TerrainStamp> pts = { {0, 0, 1}, {100, 0, 1}, {50, 0, 60} };
    std::vector<int32_t> labels = Labels(pts);
    CHECK_EQ(labels[0], 0);
    CHECK_EQ(labels[1], 0);
    CHECK_EQ(labels[2], 0);
}

// Labels are not contiguous after a merge, and a later bridge lowers a whole cluster.
UNIT_TEST(river_labels_later_bridge_lowers_a_cluster) {
    std::vector<TerrainStamp> pts = {
        {0, 0, 1},      // 0: alone
        {100, 0, 1},    // 1: alone
        {200, 0, 1},    // 2: alone
        {100, 1, 1},    // 3: touches 1 (1 * 1.2 < 2) -> 1
        {150, 0, 60},   // 4: touches 1, 2 and 3 (50 * 1.2 < 61) -> 1, and 2 becomes 1
    };
    std::vector<int32_t> labels = Labels(pts);
    CHECK_EQ(labels[0], 0);
    CHECK_EQ(labels[1], 1);
    CHECK_EQ(labels[2], 1);
    CHECK_EQ(labels[3], 1);
    CHECK_EQ(labels[4], 1);
}

// The join test is strict: len * 1.2 == r_i + r_j does not join.
UNIT_TEST(river_labels_join_is_strict) {
    std::vector<TerrainStamp> pts = { {0, 0, 3}, {5, 0, 3} };   // 5 * 1.2 = 6 == 3 + 3
    std::vector<int32_t> labels = Labels(pts);
    CHECK_EQ(labels[1], 1);
}

// One cluster: no draw, no point.
UNIT_TEST(river_connect_single_cluster_draws_nothing) {
    FieldStorage storage;
    TerrainHeightfield* hf = new (&storage.hf) TerrainHeightfield();
    new (&hf->random) System::Random(1234u);
    AddPoint(hf, 0, 0, 5);
    AddPoint(hf, 4, 0, 5);
    Connect(hf);
    CHECK_EQ(hf->riverPoints._size, 2);
    System::Random reference(1234u);
    CHECK(SameRandom(hf->random, reference));
    hf->riverPoints.~StampArray();
}

// Two clusters 10 apart: points at the drawn member and every 2.0 toward cluster 0, the
// end point excluded (the loop runs while length > travelled), radius 10.
UNIT_TEST(river_connect_two_clusters_steps_every_two_units) {
    FieldStorage storage;
    TerrainHeightfield* hf = new (&storage.hf) TerrainHeightfield();
    new (&hf->random) System::Random(99u);
    AddPoint(hf, 0, 0, 5);
    AddPoint(hf, 10, 0, 5);   // 10 * 1.2 = 12 > 10: a second cluster
    Connect(hf);
    CHECK_EQ(hf->riverPoints._size, 7);
    if (hf->riverPoints._size == 7) {
        const float xs[5] = { 10.0f, 8.0f, 6.0f, 4.0f, 2.0f };
        for (int i = 0; i < 5; ++i) {
            const TerrainStamp& p = hf->riverPoints._data[2 + i];
            CHECK_EQ(p.x, xs[i]);
            CHECK_EQ(p.y, 0.0f);
            CHECK_EQ(p.radius, 10.0f);
        }
    }
    // Two draws: Rand(size of cluster 1), then Rand(size of cluster 0).
    System::Random reference(99u);
    reference.Rand(1u);
    reference.Rand(1u);
    CHECK(SameRandom(hf->random, reference));
    hf->riverPoints.~StampArray();
}

// A third cluster joins the merged cluster 0: its nearest member is the point of the
// cluster merged before it, not point 0.
UNIT_TEST(river_connect_later_cluster_joins_the_merged_cluster) {
    FieldStorage storage;
    TerrainHeightfield* hf = new (&storage.hf) TerrainHeightfield();
    new (&hf->random) System::Random(7u);
    AddPoint(hf, 0, 0, 1);
    AddPoint(hf, 100, 0, 1);
    AddPoint(hf, 104, 0, 1);   // 4 * 1.2 = 4.8 > 2: its own cluster
    Connect(hf);
    // 3 points + 50 on the 100-long segment + 2 on the 4-long one.
    CHECK_EQ(hf->riverPoints._size, 55);
    if (hf->riverPoints._size == 55) {
        CHECK_EQ(hf->riverPoints._data[3].x, 100.0f);
        CHECK_EQ(hf->riverPoints._data[52].x, 2.0f);
        CHECK_EQ(hf->riverPoints._data[53].x, 104.0f);
        CHECK_EQ(hf->riverPoints._data[54].x, 102.0f);
    }
    System::Random reference(7u);
    reference.Rand(1u);   // cluster 1's member
    reference.Rand(1u);   // cluster 0's start
    reference.Rand(1u);   // cluster 2's member
    reference.Rand(2u);   // the merged cluster 0's start
    CHECK(SameRandom(hf->random, reference));
    hf->riverPoints.~StampArray();
}
