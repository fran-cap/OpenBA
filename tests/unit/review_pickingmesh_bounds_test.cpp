// tests/unit/review_pickingmesh_bounds_test.cpp
// review S-015 and S-016: the index fields PickingMesh's SubMesh walk reads from a resource
// (src/engine/picking/pickingmesh_bounds.h). The walk sits in pickingmesh.cpp's anonymous
// namespace and Scene::AddMesh / Intersect need a scene; they call these. All platforms.

#include "unit.h"
#include "picking/pickingmesh_bounds.h"
#include <cstdint>

using PickingMeshBounds::SavedParent;
using PickingMeshBounds::TriangleInRange;

UNIT_TEST(review_pickingmesh_saved_parent) {
    // In range: kept, including a parent at or past its own node (AddMesh reads an
    // identity / earlier-computed entry there, which is defined).
    CHECK_EQ(SavedParent(0, 2), 0);
    CHECK_EQ(SavedParent(1, 2), 1);
    CHECK_EQ(SavedParent(4, 5), 4);

    // The finding's input: a 2-node mesh with parent 0x7fffffff.
    CHECK_EQ(SavedParent(0x7fffffff, 2), 0);
    CHECK_EQ(SavedParent(2, 2), 0);
    CHECK_EQ(SavedParent(-1, 2), 0);
    CHECK_EQ(SavedParent(INT32_MIN, 2), 0);
}

UNIT_TEST(review_pickingmesh_triangle_in_range) {
    CHECK(TriangleInRange(0, 1, 2, 3));
    CHECK(TriangleInRange(2, 2, 2, 3));
    CHECK(TriangleInRange(65535, 0, 1, 65536));

    // The finding's input: vCount = 3 and an index of 3, in each position.
    CHECK(!TriangleInRange(3, 0, 1, 3));
    CHECK(!TriangleInRange(0, 3, 1, 3));
    CHECK(!TriangleInRange(0, 1, 3, 3));
    CHECK(!TriangleInRange(65535, 65535, 65535, 3));
    // No vertices at all.
    CHECK(!TriangleInRange(0, 0, 0, 0));
}
