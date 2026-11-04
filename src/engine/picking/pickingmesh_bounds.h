#pragma once
// src/engine/picking/pickingmesh_bounds.h
// The tests PickingMesh's SubMesh walk (0x18007ac20) applies on load to the two index fields a
// resource carries: a sub-mesh's parent and a triangle's vertex indices. Pure, so the unit
// tests reach them (tests/unit/review_pickingmesh_bounds_test.cpp).

#include <cstdint>

namespace PickingMeshBounds {

// NOT RETAIL (review S-015): Scene::AddMesh (0x1801252e0) reads the world-transform scratch at
// `dst[node->_parent]` (MOVSXD / IMUL 0x1c, no compare), and the scratch holds one transform
// per sub-mesh. A parent outside [0, nodeCount) from a malformed or mod resource is read as
// the root (0). Shipped data is always in range.
constexpr int32_t SavedParent(int32_t parent, int32_t nodeCount) {
    if (parent < 0 || parent >= nodeCount) {
        return 0;
    }
    return parent;
}

// NOT RETAIL (review S-016): Scene::Intersect (0x1801246c0) reads `verts[i0..i2]` of every
// triangle with no bound. A sub-mesh whose triangles name a vertex it does not have loses its
// triangles at load (it is then never hit), rather than reading past the vertex array on every
// pick.
constexpr bool TriangleInRange(uint16_t i0, uint16_t i1, uint16_t i2, int32_t vertexCount) {
    return i0 < vertexCount && i1 < vertexCount && i2 < vertexCount;
}

}  // namespace PickingMeshBounds
