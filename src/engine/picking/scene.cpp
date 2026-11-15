// ============================================================================
// src/engine/picking/scene.cpp — Wave 5 lane D — Picking::Scene / PickingMesh
// ============================================================================
//
// Ports the nine `Picking` exports our tree calls (docs/runtime_port/PLAN.md
// §4.2 wave 5, §6). Layouts and the evidence for every offset are in
// src/engine/picking/Scene.h; the per-function write-up, the oracle
// arguments and everything this lane could NOT verify are in
// docs/history/runtime_port/WAVE5_LANE_D.md.
//
//   | Export                          | Retail      | Ord  | Oracle          |
//   |---------------------------------|-------------|------|-----------------|
//   | ??0Scene@Picking@@QEAA@XZ       | 0x1801258e0 |  376 | FULL (scratch)  |
//   | ??1Scene@Picking@@QEAA@XZ       | 0x180125890 |  672 | post-state only |
//   | ?AddMesh@Scene@Picking@@...     | 0x1801252e0 | 1571 | FULL (scratch)  |
//   | ?GetUserData@Scene@Picking@@... | 0x1800722d0 | 2998 | FULL            |
//   | ?Intersect@Scene@Picking@@...   | 0x1801246c0 | 3081 | FULL, tol 0     |
//   | ?RemoveMesh@Scene@Picking@@...  | 0x180125290 | 3560 | post-state only |
//   | ?SetMeshUserData@Scene@...      | 0x180125240 | 3917 | FULL (replay)   |
//   | ?SetTransform@Scene@Picking@@...| 0x1800721b0 | 3993 | FULL (replay)   |
//   | ?Vertices@PickingMesh@Picking...| 0x180071e10 | 4536 | FULL            |
//
// All nine names were checked against the shipped export directory of
// C:\GOG Games\Banished\Runtime-x64.dll with pefile BEFORE any body was
// written (reference_resolveexport_phantom_names.md: a name the DLL does not
// contain resolves to NULL in silence and the census still counts the port).
// The addresses and ordinals above are that query's output.
//
// THE GRID INTERNALS
// ------------------
// `Picking::Scene` is a spatial hash. The five grid internals its bodies call
// (`GridAdd`, `GridReindex`, `GridUnthread`, `GridFreeEntry`, `CellIterAdvance`),
// and the fourteen bodies they reach, are ported in
// src/engine/picking/scene_grid.cpp and called by name through `rtpickgrid::`.
//
// IS THE CLASS SPLIT?  No -- every Scene method this process ever invokes is
// now ours. `Picking::Scene` exports 21 symbols; our tree calls nine of them
// (the table above), and a grep across code/, engine/, standalone/, runtime/
// and src/render/dx11/ finds NO call site for any of the other twelve -- not Update, not
// Clip, not GetMesh / GetAABB / GetLocalAABB / GetTransform / GetUserValue, not
// the copy ctor or operator=. And `get_xrefs_to` on Update, on Clip and on all
// nine ported addresses returns only DATA references (the export address and
// name tables, plus .pdata unwind records): Runtime-x64.dll never calls any of
// them internally either. So there is no path by which the DLL and this port
// both mutate one Scene, which is the state-corruption hazard PLAN §4.1
// decision 5 is about. The unported twelve are dormant code in a DLL nothing
// dispatches into.
//
// ENDGAME B.3 step 7, block B6 — WHAT CHANGED IN THIS FILE
// --------------------------------------------------------
// `Picking::Scene` is a real class now (src/engine/picking/Scene.h). The
// bodies below are its MEMBERS, and the one door at the bottom,
// `RtPicking_Scene_Ctor`, is a one-line QUALIFIED delegation:
//   * it runs `Scene()`, which delegates to `Scene(16.0f)` (closure_batch2b.cpp), as
//     retail's 0x1801258e0 does;
//   * the destructor is the IMPLICIT one, which is retail's four
//     `~Array` calls in retail's order (Scene.h banner);
//   * the five grid internals (`GridAdd`, `GridReindex`, `GridUnthread`,
//     `GridFreeEntry`, `CellIterAdvance`) are DIRECT calls into `rtpickgrid::`;
//   * `CellIter` is scene_grid.h's one definition.
// `Vertices` is a PickingMesh member (PickingMesh.h).
// ============================================================================

#include <cstdint>
#include <cstring>

#include "picking/Scene.h"
#include "../collection/array_internal.h"   // ENDGAME B.3.2 step 2: kSpec_Transform, AsArray
#include "scene_grid.h"

namespace {

using Entry    = Picking::Scene::Entry;
using SubMesh  = Picking::PickingMesh::SubMesh;
using Triangle = Picking::PickingMesh::Triangle;
using rtpickgrid::CellIter;

// ============================================================================
// The float constants retail loads from .rdata, verified by reading the bytes
// out of the shipped DLL rather than trusting Ghidra's rendering ("-NAN" in a
// decompile is 0xffffffff, not a float).
//   DAT_18016169c = 0x3f800000 =  1.0f
//   DAT_1801616fc = 0x41800000 = 16.0f      (the Scene's cell size)
//   DAT_180161740 = 0x4cbebc20 =  1.0e8f    (RtMathConst::kBigPos)
//   DAT_18016177c = 0xccbebc20 = -1.0e8f    (RtMathConst::kBigNeg)
//   DAT_1801617c0 = 0x80000000              (sign-flip mask, i.e. unary minus)
// ============================================================================
const float kOne       =  1.0f;
const float kCellSize  = 16.0f;
const float kBigPos    =  1.0e8f;
const float kBigNeg    = -1.0e8f;

// ============================================================================
// Math helpers, transliterated from the retail bodies rather than called.
//
// Both of these ARE ported and registered by Wave 1 (src/engine/math/
// transform.cpp), and retail CALLs both. They are duplicated here on purpose:
// both bodies below were re-derived from the retail decompile and then checked
// to agree with Wave 1's term-for-term, which is an independent confirmation of
// that port rather than a dependency on it.
// ============================================================================

// ??DQuaternion@Math@@QEBA?AV01@AEBV01@@Z  retail 0x18001e280.
// MSVC ABI at every call site in this file: RCX = this, RDX = hidden return
// slot, R8 = rhs. Written exactly as retail associates the terms.
void QuatMul(const Quaternion* a, Quaternion* ret, const Quaternion* b) {
    const float bx = b->x, by = b->y, bz = b->z, bw = b->w;
    const float ax = a->x, ay = a->y, az = a->z, aw = a->w;
    const float x = (ax * bw + bx * aw + az * by) - ay * bz;
    const float y = (ay * bw - az * bx) + by * aw + bz * ax;
    const float w = ((bw * aw - ax * bx) - ay * by) - bz * az;
    const float z = ((az * bw + ay * bx) - by * ax) + bz * aw;
    ret->x = x; ret->y = y; ret->w = w; ret->z = z;
}

// ?Inverse@Transform@Math@@QEBA?AV12@XZ  retail 0x18005db50.
// Rigid inverse: rotation' = conjugate(q), translation' from a quaternion
// rotate of -translation. Retail's exact term order, not the textbook form.
void TransformInverse(const ::Transform* self, ::Transform* ret) {
    const float qx = self->rotation.x, qy = self->rotation.y;
    const float qz = self->rotation.z, qw = self->rotation.w;
    const float nqx = -qx, nqy = -qy, nqz = -qz;
    const float ntx = -self->translation.x;
    const float nty = -self->translation.y;
    const float ntz = -self->translation.z;

    const float t10 = (ntx * qw + nqx * 0.0f + nty * nqz) - ntz * nqy;
    const float t11 = (nqy * 0.0f - ntx * nqz) + nty * qw + ntz * nqx;
    const float t7  = ((ntx * nqy + nqz * 0.0f) - nty * nqx) + ntz * qw;
    const float t6  = ((qw * 0.0f - ntx * nqx) - nty * nqy) - ntz * nqz;

    ret->rotation.x = nqx;
    ret->rotation.y = nqy;
    ret->rotation.z = nqz;
    ret->rotation.w = qw;
    ret->translation.x = (t6 * qx + t10 * qw + t7 * qy) - t11 * qz;
    ret->translation.y = (t11 * qw - t7 * qx) + t6 * qy + t10 * qz;
    ret->translation.z = ((t11 * qx + t7 * qw) - t10 * qy) + t6 * qz;
}

// The sqrtf retail calls at 0x180128110 is the CRT's, which for a finite
// non-negative argument is the SQRTSS instruction (its whole prologue is the
// NaN / negative / infinity error path into `_invoke_matherr`). Intersect only
// ever hands it a sum of three squares, so this is bit-identical. Written with
// __builtin_sqrtf so no libm call appears on the pick path.
inline float RtSqrtf(float v) { return __builtin_sqrtf(v); }

// Scene+0x48 (_entries) and +0x30 (_links) accessors, spelled once so a wrong stride cannot be
// written twice.
inline Entry* EntryAt(const Picking::Scene* s, int32_t idx) { return &s->_entries._data[idx]; }
inline int32_t LinkEntry(const Picking::Scene* s, int32_t link) { return s->_links[link]._entry; }
inline int32_t LinkNext(const Picking::Scene* s, int32_t link)  { return s->_links[link]._next; }

}  // namespace

// ############################################################################
// Picking::Scene's member bodies (ENDGAME B.3.7 block B6).
// ############################################################################
namespace Picking {

// ============================================================================
// Picking::Scene::SetMeshUserData(unsigned int, void*, int, int, callback)
// retail 0x180125240, ordinal 3917
//
// Four stores. NOTE the argument count: five after `this`, so the callback is
// the SIXTH register argument and lands on the stack. A signature that dropped
// it would leave entry+0x40 holding whatever the caller happened to have there
// (reference_hidden_retval_and_int_pointer_args.md).
// ============================================================================
void Scene::SetMeshUserData(uint32_t id, void* userData, int32_t userValue,
                            int32_t userValue2, HitCallback callback) {
    Entry* e = &_entries[id];
    e->_userData   = userData;
    e->_userValue  = userValue;
    e->_userValue2 = userValue2;
    e->_callback   = callback;
}

// ============================================================================
// Picking::Scene::SetTransform(unsigned int, const Transform&)
// retail 0x1800721b0, ordinal 3993
//
// Seven dwords into the entry's ROOT sub-mesh transform (transforms[0]), then
// the grid re-index. It does NOT touch the other sub-mesh transforms, so a
// mesh whose root moves keeps stale child transforms until AddMesh rebuilds
// them — that is retail's behaviour and is reproduced here.
// ============================================================================
void Scene::SetTransform(uint32_t id, const Math::Transform& transform) {
    _entries[id]._transforms.GetData()[0] = transform;   // seven dword moves
    rtpickgrid::GridReindex(this, id);                  // CALL 0x1800724d0
}

// ============================================================================
// Picking::Scene::AddMesh(ExternalPtr<PickingMesh>, const Transform&, void*,
//                         int, int, callback)
// retail 0x1801252e0, ordinal 1571
//
// Flattens the mesh's sub-mesh hierarchy into a scratch Array<Transform> —
// node 0 gets the caller's root transform verbatim, node i gets
// worldOf(parent(i)) composed with node i's local transform — then hands the
// scratch, plus the user data, to the grid inserter, which DEEP-COPIES it into
// the new entry. That deep copy is why the scratch is freed on the way out.
//
// ABI: the ExternalPtr is passed BY VALUE and is one qword, so it occupies RDX
// like any pointer (B7-5); the callback is the seventh argument, on the stack.
// ============================================================================
uint32_t Scene::AddMesh(Object::ExternalPtr<PickingMesh> mesh, const Math::Transform& root,
                        void* userData, int32_t userValue, int32_t userValue2,
                        HitCallback callback) {
    const PickingMesh* pm = mesh.Get();

    AddDesc desc;
    desc._transforms._data = nullptr;
    desc._transforms._size = 0;
    desc._transforms._capacity = 0;
    desc._userValue  = userValue;
    desc._userValue2 = userValue2;
    desc._callback   = callback;
    desc._mesh       = mesh.Get();
    desc._userData   = userData;

    // retail 0x180059140 — `kSpec_Transform`, NOT trait-equivalent: the tail
    // init writes retail's identity transform through the hand-derived row.
    rtcoll::AsArray<&rtcoll::kSpec_Transform>(&desc._transforms)
        ->SetSize(pm->_nodes._size);
    ::Transform* dst = desc._transforms.GetData();

    // Node 0 is the root: the caller's transform, copied dword for dword.
    dst[0] = root;                                      // seven dword moves

    // worldOf(i) = worldOf(parent(i)) * localOf(i), transliterated from retail.
    // The rotation goes through the quaternion product; the translation is an
    // inlined quaternion-rotate of the local offset by the parent rotation,
    // added to the parent's translation. Retail's association order is kept
    // verbatim — this is float arithmetic under a bit-exact oracle.
    if (desc._transforms.GetSize() > 1) {
        for (int i = 1; i < desc._transforms.GetSize(); ++i) {
            const SubMesh* node = &pm->_nodes._data[i];
            const ::Transform* parent = &dst[node->_parent];

            const float pw = parent->rotation.w;
            const float pz = parent->rotation.z, npz = -pz;
            const float py = parent->rotation.y, npy = -py;
            const float px = parent->rotation.x, npx = -px;

            const float lx = node->_local.translation.x;
            const float ly = node->_local.translation.y;
            const float lz = node->_local.translation.z;

            const float t19 = ((pw * 0.0f - lx * px) - ly * py) - lz * pz;
            const float t18 = ((lx * py + pz * 0.0f) - ly * px) + lz * pw;
            const float t17 = (py * 0.0f - lx * pz) + ly * pw + lz * px;
            const float t23 = (lx * pw + px * 0.0f + ly * pz) - lz * py;

            const float ptz = parent->translation.z;
            const float pty = parent->translation.y;
            const float ptx = parent->translation.x;

            QuatMul(&parent->rotation, &dst[i].rotation, &node->_local.rotation);

            dst[i].translation.x = ((t23 * pw + npx * t19 + t18 * npy) - t17 * npz) + ptx;
            dst[i].translation.y = (t17 * pw - npx * t18) + t19 * npy + t23 * npz + pty;
            dst[i].translation.z = ((npx * t17 + t18 * pw) - t23 * npy) + t19 * npz + ptz;
        }
    }

    const uint32_t id = rtpickgrid::GridAdd(this, &desc);   // CALL 0x18007af70

    desc._transforms.~Array();   // retail 0x1800739e0 (J1; Math::Transform is trivially destructible)
    return id;
}

// ============================================================================
// Picking::Scene::RemoveMesh(unsigned int)
// retail 0x180125290, ordinal 3560   ?RemoveMesh@Scene@Picking@@QEAAXI@Z
//
// `AddMesh` and `RemoveMesh` are the two halves of one free list — `AddMesh`
// pops `Scene::_freeEntry` and `RemoveMesh` pushes onto it, so the two must
// always be the same implementation (`pickingcomponent.cpp` records the crash
// a split once produced). The retail body is two calls and nothing else.
// ============================================================================
void Scene::RemoveMesh(uint32_t id) {
    Entry* e = &_entries[id];
    const uint16_t w = e->_cellW;
    const uint16_t h = e->_cellH;
    rtpickgrid::GridUnthread(this, &e->_cellX, w, h, (int32_t)id);   // CALL 0x180072950
    rtpickgrid::GridFreeEntry(this, id);                             // CALL 0x18007b390
}

// ============================================================================
// Picking::Scene::Intersect(const Vector3& start, const Vector3& dir,
//                           const Scalar& maxDist, Vector3& outPos,
//                           Vector3& outNormal) const
// retail 0x1801246c0, ordinal 3081
//
// A ray cast over the grid. For every entry whose cell rectangle the ray's
// XY-projected extent touches, for every sub-mesh, the ray is pulled into the
// sub-mesh's local space by the INVERSE of its world transform and tested
// against every triangle with a Moller-Trumbore variant. The nearest hit
// within `maxDist` wins; the return value is the ENTRY index (what GetUserData
// and SetTransform take), or -1.
//
// *** IT IS DECLARED const AND IT MUTATES THE SCENE. *** `INC dword ptr
// [RCX+0x5c]` at 0x1801247bf allocates a fresh query stamp, and each visited
// entry's +0x00 is set to it so an entry spanning several cells is tested
// once. `_queryStamp` is `mutable` for that (Scene.h): the constness is retail's, and it is a lie.
//
// Bit equality with retail is the contract: this is a faithful transliteration
// of retail's own float expressions, in retail's association order.
// ============================================================================
int32_t Scene::Intersect(const Vector3& startRef, const Vector3& dirRef,
                         const Math::Scalar& maxDistRef,
                         Vector3& outPosRef, Vector3& outNormalRef) const {
    const Scene* self = this;
    const Vector3* start = &startRef;
    const Vector3* dir = &dirRef;
    const Math::Scalar* maxDist = &maxDistRef;
    Vector3* outPos = &outPosRef;
    Vector3* outNormal = &outNormalRef;

    // ---- the ray's XY extent, clamped into the +/-1e8 world box ---------
    float oy = start->y;
    float ox = start->x;
    float maxY = oy;      if (oy < kBigNeg)  maxY = kBigNeg;
    float maxX = kBigNeg; if (kBigNeg <= ox) maxX = ox;
    float minY = oy;      if (kBigPos < oy)  minY = kBigPos;
    float minX = kBigPos; if (ox <= kBigPos) minX = ox;

    const float endY = oy + maxDist->v * dir->y;
    const float endX = ox + maxDist->v * dir->x;
    if (maxY <= endY) maxY = endY;
    if (maxX <= endX) maxX = endX;
    if (endY <= minY) minY = endY;
    if (endX <= minX) minX = endX;

    const float inv = self->_invCellSize;
    float bestT = maxDist->v;

    self->_queryStamp = self->_queryStamp + 1;
    const int32_t stamp = self->_queryStamp;
    int bestId = -1;

    // Cell indices are TRUNCATED, not floored (CVTTSS2SI at 0x180124818).
    CellIter it;
    it.minX = (int32_t)(inv * minX);
    it.minY = (int32_t)(inv * minY);
    it.maxX = (int32_t)(inv * maxX);
    it.maxY = (int32_t)(inv * maxY);
    it.curX = it.minX - 1;
    it.curY = it.minY;
    it.stamp = stamp;
    it.link = -1;
    it.scene = self;

    rtpickgrid::CellIterAdvance(&it);                   // CALL 0x18007b6d0
    if (it.link >= 0) EntryAt(self, LinkEntry(self, it.link))->_queryStamp = stamp;
    if (it.link == -1) {
        return -1;
    }

    for (;;) {
        const int32_t curLink = it.link;
        Entry* entry = EntryAt(self, LinkEntry(self, curLink));

        int32_t sub = 0;
        if (entry->_transforms.GetSize() > 0) {
            do {
                // Re-read the entry through the link each iteration, as retail
                // does (it reloads links[curLink]._entry at the top of every
                // sub-mesh pass rather than hoisting it).
                Entry* e = EntryAt(self, LinkEntry(self, curLink));
                const PickingMesh* pm = e->_mesh;
                const ::Transform* world = &e->_transforms[sub];

                ::Transform invX;
                TransformInverse(world, &invX);
                const float iqx = invX.rotation.x, iqy = invX.rotation.y;
                const float iqz = invX.rotation.z, iqw = invX.rotation.w;
                const float niqx = -iqx, niqy = -iqy, niqz = -iqz;

                // ray ORIGIN into sub-mesh local space
                float px = start->x, py = start->y, pz = start->z;
                float c26 = (iqw * px + iqx * 0.0f + py * iqz) - pz * iqy;
                float c23 = (iqy * 0.0f - px * iqz) + iqw * py + pz * iqx;
                float c19 = ((iqw * 0.0f - px * iqx) - py * iqy) - pz * iqz;
                float c20 = ((px * iqy + iqz * 0.0f) - py * iqx) + iqw * pz;
                // *** THE TRANSLATION IS ADDED LAST, TO THE FULLY ACCUMULATED
                // ROTATED PART — Wave 9 lane N, and this was the Finding 2
                // defect. Retail 0x180124B56-0x180124BB4 loads each translation
                // component and performs exactly ONE `addss` against a register
                // that already holds the whole rotated sum:
                //     movss xmm0,[rbp-0x58] / addss xmm0,xmm3   -> local.x
                //     movss xmm0,[rbp-0x54] / addss xmm0,xmm2   -> local.y
                //     movss xmm0,[rbp-0x50] / addss xmm0,xmm7   -> local.z
                // The parenthesisation below is load-bearing; do not
                // "simplify" it (21 divergences in 29,867 hitting rays before).
                const float localOx = invX.translation.x +
                    ((c19 * niqx + iqw * c26 + c20 * niqy) - c23 * niqz);
                const float localOy = invX.translation.y +
                    (((iqw * c23 - c20 * niqx) + c19 * niqy) + c26 * niqz);
                const float localOz = invX.translation.z +
                    ((((c23 * niqx + iqw * c20) - c26 * niqy) + c19 * niqz));

                // ray DIRECTION into sub-mesh local space (rotation only)
                px = dir->x; py = dir->y; pz = dir->z;
                float d19 = (iqw * px + iqx * 0.0f + py * iqz) - pz * iqy;
                float d20 = (iqy * 0.0f - px * iqz) + iqw * py + pz * iqx;
                float d23 = ((iqw * 0.0f - px * iqx) - py * iqy) - pz * iqz;
                float d17 = ((px * iqy + iqz * 0.0f) - py * iqx) + iqw * pz;
                const float localDx = (d23 * niqx + iqw * d19 + d17 * niqy) - d20 * niqz;
                const float localDy = (iqw * d20 - d17 * niqx) + d23 * niqy + d19 * niqz;
                const float localDz = ((d20 * niqx + iqw * d17) - d19 * niqy) + d23 * niqz;

                const SubMesh* node = &pm->_nodes._data[sub];
                const Vector3*  verts = node->_vertices.GetData();
                const Triangle* tris  = node->_triangles.GetData();
                const int32_t triCount = node->_triangles.GetSize();

                for (int32_t t = 0; t < triCount; ++t) {
                    const uint32_t i0 = tris[t].i0;
                    const uint32_t i1 = tris[t].i1;
                    const uint32_t i2 = tris[t].i2;

                    const float v0y = verts[i0].y;
                    const float v0z = verts[i0].z;
                    const float v0x = verts[i0].x;

                    const float tvx = localOx - v0x;
                    const float e1z = verts[i1].z - v0z;
                    const float e1x = verts[i1].x - v0x;
                    const float e2x = verts[i2].x - v0x;
                    const float e2y = verts[i2].y - v0y;
                    const float e2z = verts[i2].z - v0z;
                    const float tvy = localOy - v0y;
                    const float e1y = verts[i1].y - v0y;

                    const float p27 = e2z * localDy - e2y * localDz;
                    const float p25 = e2x * localDz - e2z * localDx;
                    const float p22 = e2y * localDx - e2x * localDy;
                    const float det = p25 * e1y + p27 * e1x + p22 * e1z;

                    const float tvz = localOz - v0z;
                    const float q23 = tvy * e1z - tvz * e1y;
                    const float q20 = tvz * e1x - tvx * e1z;
                    const float q32 = tvx * e1y - tvy * e1x;

                    const float u = tvy * p25 + tvx * p27 + tvz * p22;
                    const float v = q20 * localDy + q23 * localDx + q32 * localDz;

                    if (0.0f < det && 0.0f <= u && u <= det && 0.0f <= v &&
                        v + u <= det) {
                        // Retail 0x180124ecc-0x180124ef1: ((t1 + t2) + t3) *
                        // (1.0f / det) — which is what `a + b + c` means in C.
                        float hitT = (q20 * e2y + q23 * e2x + q32 * e2z) * (kOne / det);

                        // *** THE TIE-BREAK IS `<=`, AND IT MATCHES RETAIL. ***
                        // retail 0x180124ef5:
                        //   comiss xmm3,xmm8(0.0) / jb  skip   => 0 <= hitT
                        //   movss  xmm0,[rsp+0x28](bestT)
                        //   comiss xmm0,xmm3      / jb  skip   => hitT <= bestT
                        if (0.0f <= hitT && hitT <= bestT) {
                            const float dz = dir->z, dy = dir->y;
                            const float sz = start->z, sy = start->y;
                            outPos->x = hitT * dir->x + start->x;
                            outPos->y = hitT * dy + sy;
                            outPos->z = hitT * dz + sz;

                            // local-space face normal, then normalised
                            float nY = e2x * e1z - e2z * e1x;
                            float nZ = e2y * e1x - e2x * e1y;
                            float nX = e2z * e1y - e2y * e1z;
                            const float len = RtSqrtf(nY * nY + nX * nX + nZ * nZ);
                            if (len != 0.0f) {
                                const float r = kOne / len;
                                nZ = r * nZ;
                                nX = r * nX;
                                nY = r * nY;
                            }

                            // rotate it into world space: q * (n,0) * conj(q),
                            // with q the sub-mesh's WORLD rotation (not the
                            // inverse computed above).
                            const Quaternion conj = { -world->rotation.x,
                                                      -world->rotation.y,
                                                      -world->rotation.z,
                                                       world->rotation.w };
                            const Quaternion pure = { nX, nY, nZ, 0.0f };
                            Quaternion tmp, rotated;
                            QuatMul(&world->rotation, &tmp, &pure);
                            QuatMul(&tmp, &rotated, &conj);
                            outNormal->x = rotated.x;
                            outNormal->y = rotated.y;
                            outNormal->z = rotated.z;

                            bestT  = hitT;
                            bestId = LinkEntry(self, curLink);
                        }
                    }
                }
                ++sub;
            } while (sub < EntryAt(self, LinkEntry(self, curLink))->_transforms.GetSize());
        }

        // Walk the rest of this cell's link chain, skipping entries already
        // tested during this query.
        bool jumped = false;
        it.link = LinkNext(self, curLink);
        while (it.link >= 0) {
            if (EntryAt(self, LinkEntry(self, it.link))->_queryStamp != stamp) {
                jumped = true;
                break;
            }
            it.link = LinkNext(self, it.link);
        }
        if (jumped) {
            EntryAt(self, LinkEntry(self, it.link))->_queryStamp = stamp;
            continue;
        }

        rtpickgrid::CellIterAdvance(&it);               // CALL 0x18007b6d0
        if (it.link >= 0) EntryAt(self, LinkEntry(self, it.link))->_queryStamp = stamp;
        if (it.link == -1) break;
    }

    return bestId;
}

}  // namespace Picking

// ============================================================================
// THE DOOR — the default constructor and `RtPicking_Scene_Ctor`, which
// GameUpdateLoop::Load calls on its raw storage.
// ============================================================================

// ??0Scene@Picking@@QEAA@XZ  0x1801258e0 — retail's whole body is
// `Scene(this, 16.0f); return this;` (the 0x18007aef0 ctor, then MOV RAX,RBX
// at 0x180125904). The allocation it runs (10,000 bucket heads) happens on
// the LOADER thread when GameUpdateLoop::Load builds the scene.
namespace Picking {
Scene::Scene() : Scene(kCellSize) {}
}  // namespace Picking

// The in-place door GameUpdateLoop::Load calls on its 0x60-byte raw storage
// (gameupdateloop_load.cpp): it runs the default constructor above.
extern "C" Picking::Scene* RtPicking_Scene_Ctor(void* selfv) {
    return ::new (selfv) Picking::Scene();
}
