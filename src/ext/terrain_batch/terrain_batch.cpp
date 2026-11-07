// ============================================================================
// src/ext/terrain_batch/terrain_batch.cpp -- ext.terrain_batch: the run loop, the run
// buffers and the run draw
// ============================================================================
// NOT RETAIL. docs/extensions/handins/TERRAINBATCH.md; the contract is terrain_batch.h,
// the pure rules terrain_batch_rules.h.
// ============================================================================

// The runtime type headers first, as geometry_draw.cpp has them (Volume.h's `struct Box`
// before anything reaches <windows.h>).
#include "Game/WorldGeometry.h"
#include "graphics/Geometry.h"
#include "graphics/Camera.h"     // Graphics::Camera, Graphics::Renderer
#include "video/Context.h"
#include "video/Buffers.h"
#include "math/Math.h"
#include "volume/Volume.h"        // RtVolume_Frustum_MulTransform3
#include "volume/frustum_cull.h"  // FrustumCull_Prepare / FrustumCull_Test

#include "ext/terrain_batch/terrain_batch.h"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <new>
#include <vector>

namespace Ext {
namespace TerrainBatch {

namespace {

// retail's chunk z range (geometry_draw.cpp kChunkZMin / kChunkZRange: exe 0x14017228c,
// 0x1401721d0).
constexpr float kChunkZMin   = -16.0f;
constexpr float kChunkZRange = 32.0f;

// One WorldGeometry's run buffer. The game has at most two alive (the menu scene's and the
// town's); four slots leave room. Written under g_mutex; `ready` is published last.
struct Slot {
    std::atomic<const WorldGeometry*> owner{nullptr};
    std::atomic<bool> ready{false};
    Video::IndexBuffer* ib = nullptr;   // raw storage + Video_IndexBuffer_Ctor; never `delete`d
    bool    linked          = false;    // PlatformConstruct'ed (in the device's buffer list)
    int32_t indicesPerChunk = 0;        // retail's list length (chunkSize^2 x 6)
    int32_t vertsPerChunk   = 0;        // _chunkIndexStride
    int32_t copies          = 0;        // the longest run the buffer holds
};

constexpr int kSlots = 4;
Slot       g_slots[kSlots];
std::mutex g_mutex;

int32_t ChunksPerSide(const WorldGeometry& geo) {
    return (geo._chunkSize > 0) ? (geo._gridSize / geo._chunkSize) : 0;
}

int32_t IndicesPerChunk(const WorldGeometry& geo) {
    return geo._chunkSize * geo._chunkSize * 6;
}

// The geometry's ready slot, any thread.
const Slot* FindReady(const WorldGeometry& geo) {
    for (const Slot& s : g_slots) {
        if (s.owner.load(std::memory_order_acquire) != &geo) continue;
        if (!s.ready.load(std::memory_order_acquire)) return nullptr;
        return &s;
    }
    return nullptr;
}

// The geometry's slot, under g_mutex.
Slot* FindLocked(const WorldGeometry& geo) {
    for (Slot& s : g_slots) {
        if (s.owner.load(std::memory_order_relaxed) == &geo) return &s;
    }
    return nullptr;
}

void Unlink(Slot& s) {
    if (s.ib != nullptr && s.linked) {
        s.ib->PlatformDestruct();
    }
    s.linked = false;
}

// The inlined SetIndexBuffer of WorldGeometry::Draw (dirty bit 0x10 when the bound
// buffer or its offset differs).
void BindIndexBuffer(Video::Context& context, const Video::IndexBuffer* ib) {
    if (context._indexBuffer != ib || context._indexOffset != 0) {
        context._dirty |= RtVideo::kDirtyIndexBuffer;
        context._indexBuffer = ib;
        context._indexOffset = 0;
    }
}

}  // namespace

bool Ready(const WorldGeometry& geo) {
    const Slot* s = FindReady(geo);
    if (s == nullptr) return false;
    if (s->indicesPerChunk != IndicesPerChunk(geo)) return false;
    if (s->vertsPerChunk != geo._chunkIndexStride) return false;
    const RunLayout layout = LayoutFor(ChunksPerSide(geo), geo._chunkIndexStride);
    return layout.maxRun >= 1 && layout.maxRun <= s->copies;
}

void AddChunkItems(const WorldGeometry& geo, Graphics::Renderer& renderer,
                   const FrustumCullPlanes& localPlanes, bool cullVerify,
                   const Frustum& localCullFrustum, const Math::Transform3& selfInverse,
                   uint16_t materialKey, float uvScale) {
    const int32_t chunkCount = ChunksPerSide(geo);
    const RunLayout layout   = LayoutFor(chunkCount, geo._chunkIndexStride);
    const Graphics::MaterialInstance* matInst0 = geo._terrainMaterial;   // +0xF8

    const float fHalfGrid  = static_cast<float>(geo._gridSize >> 1);
    const float chunkSizeF = static_cast<float>(geo._chunkSize);
    const float zMin       = kChunkZMin;
    const float zMax       = kChunkZMin + kChunkZRange;

    // The shadow pass (shader 1: cull camera = the sun box, draw camera = the view): the
    // view frustum in the geometry's space, the light direction (the sun box's plane 0,
    // the direction Scene::AddCulledDrawItems extends casters along) and the margin.
    const bool shadow = renderer.GetShader() == 1;
    FrustumCullPlanes viewPlanes;
    float light[3] = { 0.0f, 0.0f, 0.0f };
    float margin   = 0.0f;
    bool  sweep    = false;
    if (shadow) {
        const Graphics::Camera& view = renderer.GetCamera();
        const Math::Transform3 viewToWorld = view._cameraToWorld;
        const Frustum viewFrustum = view.GetFrustum();
        Frustum viewWorld = {};
        Frustum viewLocal = {};
        RtVolume_Frustum_MulTransform3(&viewFrustum, &viewWorld, &viewToWorld);
        RtVolume_Frustum_MulTransform3(&viewWorld, &viewLocal, &selfInverse);
        FrustumCull_Prepare(&viewLocal, &viewPlanes);

        const float lx = localCullFrustum.planes[0].x;
        const float ly = localCullFrustum.planes[0].y;
        const float lz = localCullFrustum.planes[0].z;
        const float len = std::sqrt(lx * lx + ly * ly + lz * lz);
        if (len > 0.0f) {
            light[0] = lx / len;
            light[1] = ly / len;
            light[2] = lz / len;
            sweep = true;
        }
        // planes 2 / 3 are x >= -R and -x >= -R in the sun camera's space; a rigid
        // transform keeps w2 + w3 = -2R.
        float width = -(localCullFrustum.planes[2].w + localCullFrustum.planes[3].w);
        if (!(width > 0.0f)) width = 0.0f;
        margin = CasterMargin(width);
    }

    std::vector<uint8_t> visible(static_cast<size_t>(chunkCount > 0 ? chunkCount : 0));

    for (int i = 0; i < chunkCount; ++i) {
        for (int j = 0; j < chunkCount; ++j) {
            const float xmin = static_cast<float>(j) * chunkSizeF - fHalfGrid;
            const float ymin = static_cast<float>(i) * chunkSizeF - fHalfGrid;
            const float xmax = xmin + chunkSizeF;
            const float ymax = ymin + chunkSizeF;
            const ::Box box = { { xmin, ymin, zMin }, { xmax, ymax, zMax } };

            uint32_t r =
                FrustumCull_Test(&localPlanes, &box, static_cast<uint32_t>(FrustumPlanes::All), cullVerify);
            bool keep = r != static_cast<uint32_t>(FrustumPlanes::Outside);
            if (keep && shadow && sweep) {
                const Aabb chunk = { { xmin, ymin, zMin }, { xmax, ymax, zMax } };
                Aabb reach;
                if (CasterSweepBox(chunk, light, margin, reach)) {
                    const ::Box reachBox = { { reach.min[0], reach.min[1], reach.min[2] },
                                             { reach.max[0], reach.max[1], reach.max[2] } };
                    keep = FrustumCull_Test(&viewPlanes, &reachBox, static_cast<uint32_t>(FrustumPlanes::All),
                                            cullVerify) != static_cast<uint32_t>(FrustumPlanes::Outside);
                }
            }
            visible[static_cast<size_t>(j)] = keep ? 1 : 0;
        }
        ForEachRun(visible.data(), chunkCount, layout.maxRun, [&](int32_t col, int32_t len) {
            const uint16_t key2 = EncodeRun(layout, i, col, len);
            renderer.AddDrawItem(&geo, matInst0, materialKey, key2, 0.0f, uvScale, 1, nullptr, 0);
        });
    }
}

void DrawRun(const WorldGeometry& geo, Video::Context& context, uint32_t key) {
    const int32_t chunkSize     = geo._chunkSize;
    const int32_t scale         = geo._chunkIndexStride;   // +0xD4, vertices per chunk
    const int32_t primCount     = chunkSize * chunkSize * 6;
    const int32_t chunksPerSide = ChunksPerSide(geo);
    const RunLayout layout      = LayoutFor(chunksPerSide, scale);
    const Run run               = DecodeRun(layout, key);
    const int32_t start         = (chunksPerSide * run.row + run.col) * scale;

    // Retail's chunk branch, in its order: the height image, the vertex buffer, the index
    // buffer, the draw.
    context.SetImageBuffer(geo._heightImage, 6);
    context.SetVertexBuffer(&geo._vertexBuffer, 0, 0, 0x10);

    const Slot* s = FindReady(geo);
    const bool useRun = s != nullptr && s->indicesPerChunk == primCount && s->vertsPerChunk == scale &&
                        run.len <= s->copies;
    if (useRun) {
        BindIndexBuffer(context, s->ib);
        context.DrawIndexed(0, start, run.len * scale, 0, run.len * primCount, 1);
        return;
    }
    // No run buffer on this device for this geometry: retail's draw per chunk.
    BindIndexBuffer(context, &geo._indexBuffer);
    for (int32_t m = 0; m < run.len; ++m) {
        context.DrawIndexed(0, start + m * scale, scale, 0, primCount, 1);
    }
}

void OnTerrainIndices(const WorldGeometry& geo, const int16_t* indices, int32_t count) {
    if (indices == nullptr || count <= 0) return;
    const std::lock_guard<std::mutex> lock(g_mutex);

    Slot* s = FindLocked(geo);
    if (s == nullptr) {
        for (Slot& free : g_slots) {
            if (free.owner.load(std::memory_order_relaxed) == nullptr && free.ib == nullptr) {
                s = &free;
                break;
            }
        }
    }
    if (s == nullptr) {
        std::fprintf(stderr, "[TerrainBatch] no free run-buffer slot; this terrain draws per chunk\n");
        return;
    }
    s->ready.store(false, std::memory_order_release);

    const int32_t vertsPerChunk = geo._chunkIndexStride;
    const RunLayout layout = LayoutFor(ChunksPerSide(geo), vertsPerChunk);
    if (count != IndicesPerChunk(geo) || layout.maxRun < 1) {
        std::fprintf(stderr, "[TerrainBatch] grid %d / %d / %d does not fit the run key; per chunk\n",
                     geo._gridSize, geo._chunkSize, vertsPerChunk);
        return;
    }

    std::vector<uint16_t> pattern(static_cast<size_t>(count));
    std::memcpy(pattern.data(), indices, static_cast<size_t>(count) * sizeof(uint16_t));
    std::vector<uint16_t> runIndices(static_cast<size_t>(count) * static_cast<size_t>(layout.maxRun));
    FillRunIndices(pattern.data(), count, vertsPerChunk, layout.maxRun, runIndices.data());

    // A fresh device half for every build: Create on a live companion would overwrite its
    // D3D buffer without releasing it.
    if (s->ib == nullptr) {
        void* mem = ::operator new(sizeof(Video::IndexBuffer));
        s->ib = new (mem) Video::IndexBuffer;
        Video_IndexBuffer_Ctor(s->ib);   // size, dynamic, PlatformConstruct
        s->linked = true;
    } else {
        Unlink(*s);
        s->ib->PlatformConstruct();
        s->linked = true;
    }
    s->owner.store(&geo, std::memory_order_relaxed);
    if (s->ib->platformData == nullptr) return;

    const int bytes = static_cast<int>(runIndices.size() * sizeof(uint16_t));
    if (!s->ib->Create(bytes, false, runIndices.data())) {
        std::fprintf(stderr, "[TerrainBatch] run index buffer (%d bytes) not created; per chunk\n", bytes);
        return;
    }
    s->indicesPerChunk = count;
    s->vertsPerChunk   = vertsPerChunk;
    s->copies          = layout.maxRun;
    s->ready.store(true, std::memory_order_release);
}

void OnVideoError(const WorldGeometry& geo) {
    const std::lock_guard<std::mutex> lock(g_mutex);
    Slot* s = FindLocked(geo);
    if (s == nullptr) return;
    s->ready.store(false, std::memory_order_release);
    Unlink(*s);
}

void Forget(const WorldGeometry& geo) {
    const std::lock_guard<std::mutex> lock(g_mutex);
    Slot* s = FindLocked(geo);
    if (s == nullptr) return;
    s->ready.store(false, std::memory_order_release);
    Unlink(*s);
    ::operator delete(static_cast<void*>(s->ib));   // no ~IndexBuffer: it would unlink again
    s->ib = nullptr;
    s->indicesPerChunk = 0;
    s->vertsPerChunk   = 0;
    s->copies          = 0;
    s->owner.store(nullptr, std::memory_order_release);
}

}  // namespace TerrainBatch
}  // namespace Ext
