// src/game/Game/weather_particle_draw.cpp
//
// Faithful port of the weather-particle Geometry Draw vtable slot (+0x10),
// exe FUN_1400d3120 (Application-x64.exe @ 0x1400d3120).
//
// This is the renderer for the falling precipitation (rain / snow) particle
// field. The particle simulation stores a compact array of particles that
// live in a single box-sized cell (edge = desc.boxSize). To make the weather
// blanket the whole area around the camera without simulating millions of
// particles, Draw *tiles* that one cell over a 5x5x5 grid (offsets -2..+2 on
// each axis, snapped to the box grid around the camera position) and draws the
// same particle field once per visible tile via hardware instancing, one
// Context::SetTransform translation matrix per tile.
//
// ALGORITHM (line-for-line with FUN_1400d3120):
//   1. Copy the camera Transform3 (camera+0x64). Its columns are the billboard
//      basis vectors and its translation (col at +0x24) is the camera position.
//         col0 = T[+0x00..+0x08]   col1 = T[+0x0c..+0x14]
//         col2 = T[+0x18..+0x20]   camPos = T[+0x24..+0x2c]
//   2. Snap the camera position to the box grid:
//         snapped = floor(camPos / boxSize) * boxSize   (per component)
//   3. Frustum-cull the 5x5x5 tile grid. For tile (i,j,k) in [-2..2]^3:
//         center = snapped + boxSize*(i,j,k)
//         aabb   = [center, center + (boxSize,boxSize,boxSize)]
//      Transform the camera frustum by the camera transform and test it against
//      each aabb; if Contains(...) != 0x40 (0x40 == fully outside sentinel) the
//      tile is kept and its cell-center (center + boxSize/2) is appended to the
//      visible-tile list.
//   4. Context::SetColor(1,1,1, geo.intensity) and Context::SetInverseScale from
//      the descriptor near/far fade bounds (desc+0x4c/0x50 and +0x54/0x58).
//   5. Build the per-particle billboard quads into a dynamic staging buffer
//      (4 verts/particle, 0x14 bytes/vert). Billboard axes:
//         - default (desc.velocityStretch == 0): camera-facing, axisU = col2*size/2,
//           axisV = col0*size/2, constant for all particles.
//         - velocityStretch: per-particle, axisV = normalize(vel x col1)*size/2,
//           axisU = normalize(axisV x col1)*size/2, and the trailing edge uses the
//           particle's prevPos so the quad streaks along the velocity.
//      Quad corners:
//         v0 = pos   + axisU - axisV      v1 = pos   + axisU + axisV
//         v2 = altpos- axisU + axisV      v3 = altpos- axisU - axisV
//      Each vertex = { x,y,z, 0.0f, uvColor } with the four corner uv/color
//      dwords 0x00000000, 0x000000ff, 0x0000ffff, 0x0000ff00.
//   6. Instanced submit: batch = min(GetMaxDynamicInstances, visibleTiles).
//      For each batch, SetTransform a translation matrix per tile, then
//      BeginInstancedIndexedVertices, memcpy the vertex block into the context
//      dynamic-vertex cursor (ctx+0x20e0, stride ctx+0x20e8) and the index block
//      into the dynamic-index cursor (ctx+0x20f0), then EndInstancedIndexedVertices.
//
// Struct layout (verified against the 0x1400d3120 disassembly):
//   Geometry `geo`:
//     +0xc0 float intensity   +0xc8 Weather* descriptor
//     +0xd0 index buffer ptr  +0xd8 int indexCount
//     +0xe0 particle array    +0xe8 int particleCount
//   Particle (stride 0x24): +0x00 pos[3]  +0x0c prevPos[3]  +0x18 velocity[3]
//   Weather descriptor (geo+0xc8):
//     +0x40 bool velocityStretch  +0x44 float particleSize  +0x48 float boxSize
//     +0x4c/+0x50 near fade lo/hi  +0x54/+0x58 far fade lo/hi
//   Video::Context cursors: +0x20e0 vtx write ptr, +0x20e8 int vtx stride,
//     +0x20f0 idx write ptr.
//   Camera+0x64 = Transform3 (0x70 bytes).
//
// Constants (bit-exact): g_fUVScale = 1.0f, DAT_140172114 = 0.5f,
//   DAT_1401722c0 = 0x80000000 (float negate via XOR), FUN_140131390 = floorf.
//
// The pure vector math (Vector3 ctor / Cross / Normalize / operator*= / scalar
// broadcast) is inlined in plain C++ — it is bit-identical to the Runtime
// Math::Vector3 helpers the exe calls (Cross computes this x other; the by-value
// Normalize returns this/|this|). Only the operations that touch opaque engine
// state (Transform3 copy, Camera::GetFrustum, Frustum::operator*/Contains,
// Matrix::Matrix, and the six Video::Context methods) are resolved from
// Runtime-x64.dll.

// ENDGAME B.3.5 Video/Graphics caller batch: Graphics::Camera and Video::Context
// are real classes. Both headers go AHEAD of "resolve.h" — resolve.h pulls
// <windows.h>, whose <wingdi.h> declares a global `Rectangle` FUNCTION that hides
// `Volume::Rectangle`'s class name inside Volume.h's own static_asserts.
#include "graphics/Camera.h"
#include "WeatherParticleGeometry.h"   // caller batch CB6: Draw is the class's
#include "Weather.h"                   // caller batch CB6: the descriptor's fields
#include <atomic>
#include "video/Context.h"
#include "math/Math.h"
#include "object/External.h"
#include "system/MemoryAllocator.h"   // System::MemoryAllocator::MemAlloc / MemFree
#include "volume/Volume.h"   // RtVolume_Frustum_MulTransform3
#include "volume/frustum_cull.h"   // CULLROUTE: the prepared table (Contains, retail 0x1800f8e80)
#include <new>
#include <cstdint>
#include <cmath>
#include <cstring>

namespace {

// ------------------------------------------------------------------
// Runtime function pointers (lazy, resolved once on first Draw call).
// ------------------------------------------------------------------
// ENDGAME B.3.5 Video/Graphics caller batch: SEVEN doors left this block —
// `?GetFrustum@Camera@Graphics@@QEBA?AVFrustum@Volume@@XZ` and the six
// `Video::Context` members below, which are now direct member calls:
//     0x18001a8e0  ?SetColor@Context@Video@@QEAAXAEBVVector4@Math@@@Z
//     0x18001a880  Context::SetInverseScale(float,float,float,float)
//     0x1800f21d0  Context::GetMaxDynamicInstances(int,int,int) const
//     0x18001a6b0  ?SetTransform@Context@Video@@QEAAXHAEBVMatrix@Math@@@Z
//     0x1800f2130  Context::BeginInstancedIndexedVertices(int,int,int,int)
//     0x1800f2110  Context::EndInstancedIndexedVertices(int)
// The four Math/Volume doors that stayed then are direct calls now (B3R).
// Called by name (B3R): ??0Transform3@Math@@QEAA@AEBV01@@Z -> the Math::Transform3 copy
// ctor (the registered RtMath_Transform3_CtorCopy is a memcpy of the class), ??D /
// ?Contains@Frustum@Volume@@ -> RtVolume_Frustum_MulTransform3 / _Contains
// (volume/Volume.h), ??0Matrix@Math@@QEAA@AEBVVector4@1@000@Z (Runtime 0x18001e540) ->
// the Math::Matrix column ctor (RtMath_Matrix_CtorColumns assigns Matrix(c0, c1, c2, c3)).

// ------------------------------------------------------------------
// Inlined Vector3 math (bit-identical to the Math::Vector3 Runtime helpers).
// ------------------------------------------------------------------
inline void v3_cross(float out[3], const float a[3], const float b[3]) {
    // this (a) x other (b)
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}
inline void v3_normalize(float out[3], const float a[3]) {
    float len = std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
    if (len > 1e-20f) {
        float inv = 1.0f / len;
        out[0] = a[0] * inv; out[1] = a[1] * inv; out[2] = a[2] * inv;
    } else {
        out[0] = 0.0f; out[1] = 0.0f; out[2] = 0.0f;
    }
}

// One billboard corner in the dynamic vertex stream (0x14): position, w = 0,
// then the packed corner uv dword.
struct ParticleVertex {
    float    position[4];
    uint32_t corner;
};
static_assert(sizeof(ParticleVertex) == 0x14, "the particle vertex stride is 0x14");

} // namespace

// ============================================================
// WeatherParticleGeometry::Draw — exe FUN_1400d3120 (Geometry Draw vtable +0x10)
//   this      : particle Geometry (0xf0 bytes)
//   videoCtx  : Video::Context
//   cam       : Graphics::Camera
// Caller batch CB6: the body of the old `WeatherParticle_Draw(geo, context,
// camera)` free function (its one caller was this slot, which delegated to it;
// no address binding), moved into the member. The `geo + off` reads became the
// class's named fields; the pointer-null test went with the pointers (the slot
// receives references). The dead one-shot flag is a namespace-scope atomic (B6-5).
namespace { std::atomic<bool> s_weatherDrawWarned{false}; }

void WeatherParticleGeometry::Draw(Video::Context& videoCtx, const Graphics::Camera& cam,
                                   Graphics::ExternalPtrHandle /*material*/,
                                   float /*opacity*/, int /*a*/, int /*b*/, int /*c*/) const {
    // B.3.5: the seven Video/Graphics readiness terms are gone with their doors;
    // B3R: the four Math/Volume ones too.
    if (!System::MemoryAllocator::_singleton) {
        // Cascade-safe: skip the weather draw rather than crash on a null fn ptr.
        s_weatherDrawWarned.store(true);
        return;
    }

    if (!_descriptor) return;                             // +0xC8 Weather descriptor
    const Weather* const desc = _descriptor;

    const float boxSize      = desc->size;            // Weather +0x48
    const float particleSize = desc->particleSize;    // Weather +0x44
    const bool  velStretch   = desc->velocityStretch; // +0x40
    const int   particleCount = _particles.size;      // +0xE8 count
    const int   indexCount    = _indices._size;       // +0xD8
    const float intensity     = _intensity;           // +0xC0

    // (1) Copy the camera Transform3 (camera+0x64) and pull its basis columns.
    const Math::Transform3 camXform(cam._cameraToWorld);

    const float col0[3] = { camXform.axisX.x, camXform.axisX.y, camXform.axisX.z };
    const float col1[3] = { camXform.axisY.x, camXform.axisY.y, camXform.axisY.z };
    const float col2[3] = { camXform.axisZ.x, camXform.axisZ.y, camXform.axisZ.z };
    const float camPos[3] = { camXform.translation.x, camXform.translation.y, camXform.translation.z };

    const float boxHalf = boxSize * 0.5f;   // DAT_140172114 = 0.5f

    // (2) Snap camera position to the box grid (floor(camPos/box)*box).
    float snapped[3];
    for (int c = 0; c < 3; ++c)
        snapped[c] = std::floor(camPos[c] / boxSize) * boxSize;

    // (3) Frustum-cull the 5x5x5 tile grid. Visible cell-centers collected here.
    //     Sized generously and zero-initialised: the instanced submit loop can
    //     over-read past the visible count (faithful to the original, which reads
    //     into a large zero-inited stack buffer), and Matrix::Matrix reads a 4th
    //     float one Vector3 past the position pointer.
    const int kTileCap = 384;                 // >= 125 visible + batch over-read + 1
    float tiles[kTileCap * 3];
    std::memset(tiles, 0, sizeof(tiles));
    int visibleTiles = 0;

    // worldFrustum = GetFrustum(camera) * camXform. `Graphics::Camera::GetFrustum`
    // is NON-VIRTUAL and returns `::Frustum` by value (B5-12).
    const ::Frustum fr = cam.GetFrustum();
    ::Frustum worldFrustum;
    RtVolume_Frustum_MulTransform3(&fr, &worldFrustum, &camXform);

    // CULLROUTE (NOT RETAIL, faithful perf work; volume/frustum_cull.h,
    // docs/extensions/handins/CULLROUTE.md): the prepared table, built after the
    // frustum's only writer (the MulTransform3 just above). Nothing in the loop
    // writes `worldFrustum` (its address reaches only Prepare, as const), so every
    // tile test is RtVolume_Frustum_Contains's answer bit for bit.
    FrustumCullPlanes worldPlanes;
    FrustumCull_Prepare(&worldFrustum, &worldPlanes);
    const bool cullVerify = g_frustumCullVerify != 0;

    for (int i = -2; i < 3; ++i) {            // z (outer)
        for (int j = -2; j < 3; ++j) {        // y (middle)
            for (int k = -2; k < 3; ++k) {    // x (inner)
                float center[3] = {
                    boxSize * (float)k + snapped[0],
                    boxSize * (float)j + snapped[1],
                    boxSize * (float)i + snapped[2],
                };
                // AABB = { center (min), center + box (max) }.
                ::Box aabb;
                aabb.min.x = center[0];           aabb.min.y = center[1];           aabb.min.z = center[2];
                aabb.max.x = center[0] + boxSize; aabb.max.y = center[1] + boxSize; aabb.max.z = center[2] + boxSize;
                uint32_t r = FrustumCull_Test(&worldPlanes, &aabb, Bits(FrustumPlanes::All), cullVerify);
                if (r != Bits(FrustumPlanes::Outside)) {
                    if (visibleTiles < kTileCap) {
                        tiles[visibleTiles * 3 + 0] = center[0] + boxHalf;
                        tiles[visibleTiles * 3 + 1] = center[1] + boxHalf;
                        tiles[visibleTiles * 3 + 2] = center[2] + boxHalf;
                    }
                    ++visibleTiles;
                }
            }
        }
    }
    FrustumCull_CountSite(kFrustumCullSiteWeatherTiles, 125u);   // CULLROUTE: 5x5x5 tests, once per call

    // (4) Per-draw context state.
    const Math::Vector4 color(1.0f, 1.0f, 1.0f, intensity);
    videoCtx.SetColor(color);

    const float g_fUVScale = 1.0f;            // 0x14017214c
    videoCtx.SetInverseScale(
        -desc->nearFadeStart,                                     // -near_hi (+0x50, XOR sign flip)
        g_fUVScale / (desc->nearFade - desc->nearFadeStart),      // 1 / (near_lo - near_hi)
        -desc->farFade,                                           // -far_hi (+0x58)
        g_fUVScale / (desc->farFadeStart - desc->farFade));       // 1 / (far_lo - far_hi)

    // (5) Build the per-particle billboard quads (4 verts * 0x14 bytes each).
    const int   kVtxStride = 0x14;
    const int   vtxCount   = particleCount * 4;
    ParticleVertex* staging = nullptr;
    if (vtxCount > 0) {
        staging = static_cast<ParticleVertex*>(System::MemoryAllocator::MemAlloc(static_cast<uint64_t>(vtxCount) * kVtxStride, 4));
    }

    // Camera-facing billboard axes (default; overwritten per-particle if stretch).
    float axisU[3] = { col2[0] * (particleSize * 0.5f),
                       col2[1] * (particleSize * 0.5f),
                       col2[2] * (particleSize * 0.5f) };
    float axisV[3] = { col0[0] * (particleSize * 0.5f),
                       col0[1] * (particleSize * 0.5f),
                       col0[2] * (particleSize * 0.5f) };

    // Corner uv/color dwords (packed shorts baked per corner).
    static const uint32_t kCornerUV[4] = { 0x00000000u, 0x000000ffu, 0x0000ffffu, 0x0000ff00u };

    if (staging && particleCount > 0) {
        const WeatherParticle* particles = _particles.data;   // +0xE0
        ParticleVertex* out = staging;
        for (int p = 0; p < particleCount; ++p) {
            const WeatherParticle& particle = particles[p];
            const float* pos   = particle.position;                 // +0x00 pos
            const float* alt   = velStretch ? particle.previousPosition   // +0x0c prevPos
                                            : particle.position;        // else pos

            if (velStretch) {
                // Velocity-oriented axes: velocity x col1, then that x col1.
                const float* vel = particle.velocity;               // +0x18 velocity
                float perpA[3], perpAn[3], perpB[3], perpBn[3];
                v3_cross(perpA, vel, col1);
                v3_normalize(perpAn, perpA);
                v3_cross(perpB, perpAn, col1);
                v3_normalize(perpBn, perpB);
                for (int c = 0; c < 3; ++c) {
                    axisU[c] = perpBn[c] * (particleSize * 0.5f);   // local_918
                    axisV[c] = perpAn[c] * (particleSize * 0.5f);   // local_928
                }
            }

            float v0[3], v1[3], v2[3], v3[3];
            for (int c = 0; c < 3; ++c) {
                v0[c] = pos[c] + axisU[c] - axisV[c];
                v1[c] = pos[c] + axisU[c] + axisV[c];
                v2[c] = alt[c] - axisU[c] + axisV[c];
                v3[c] = alt[c] - axisU[c] - axisV[c];
            }

            const float* verts[4] = { v0, v1, v2, v3 };
            for (int q = 0; q < 4; ++q) {
                out->position[0] = verts[q][0];
                out->position[1] = verts[q][1];
                out->position[2] = verts[q][2];
                out->position[3] = 0.0f;
                out->corner = kCornerUV[q];
                ++out;
            }
        }
    }

    // (6) Instanced submit, batched by the max dynamic-instance count.
    int batch = videoCtx.GetMaxDynamicInstances(vtxCount, kVtxStride, indexCount);
    if (visibleTiles < batch) batch = visibleTiles;

    if (visibleTiles > 0) {
        const Math::Vector4 mcol0(1.0f, 0.0f, 0.0f, 0.0f);
        const Math::Vector4 mcol1(0.0f, 1.0f, 0.0f, 0.0f);
        const Math::Vector4 mcol2(0.0f, 0.0f, 1.0f, 0.0f);

        int tileBase = 0;
        do {
            if (batch > 0) {
                for (int t = 0; t < batch; ++t) {
                    // Translation matrix: identity basis + tile cell-center. The
                    // fourth column's w is the next float of tiles[] (retail reads a
                    // Vector4 at the Vector3); the buffer is padded and zeroed.
                    const float* tile = &tiles[(tileBase + t) * 3];
                    const Math::Matrix matrix(mcol0, mcol1, mcol2,
                                              Math::Vector4(tile[0], tile[1], tile[2], tile[3]));
                    videoCtx.SetTransform(t, matrix);
                }
            }
            if (videoCtx.BeginInstancedIndexedVertices(vtxCount, kVtxStride, indexCount, batch)) {
                int stride = videoCtx._dynamicVertexStride;   // +0x20e8

                // Copy the whole particle vertex block into the dynamic vertex cursor.
                size_t vtxBytes = static_cast<size_t>(vtxCount) * stride;
                std::memcpy(videoCtx._dynamicVertexCursor, staging, vtxBytes);   // +0x20e0
                videoCtx._dynamicVertexCursor += vtxBytes;

                // Copy the shared particle index buffer into the dynamic index cursor.
                // Context +0x20f0, the dynamic index cursor (16-bit indices).
                uint16_t*& idxCur = videoCtx._dynamicIndexCursor;
                size_t idxBytes = static_cast<size_t>(indexCount) * 2;
                std::memcpy(idxCur, _indices._data, idxBytes);   // +0xD0
                idxCur += static_cast<size_t>(indexCount);   // idxBytes / 2

                videoCtx.EndInstancedIndexedVertices(0);
            }
            tileBase += batch;
        } while (tileBase < visibleTiles);
    }

    if (staging)
        System::MemoryAllocator::MemFree(staging);
}
