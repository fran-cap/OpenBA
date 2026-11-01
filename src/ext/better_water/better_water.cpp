// ============================================================================
// src/ext/better_water/better_water.cpp -- ext.better_water's shore pass (docs/extensions/handins/BETTERWATER.md)
// ============================================================================
// NOT RETAIL. better_water.h has the design. This file owns the "twin" programs: for each water
// VideoProgram the game binds (River, Lake), one VideoProgram of ours with the same vertex layout,
// vertex program and render state and the shore pixel program instead of the water's. They are made
// through the engine's own Video classes, so every backend creates and binds them as it does a
// program read from a package (DX11 takes the DXBC, Vulkan converts it with dxbc-spirv, Metal with
// airconv): no backend code of our own.
//
// LIFETIME. The twins are made on first use, on the drawing thread, and live for the process. A
// twin remembers the water program it stands in for and the parts it copied (Twin, Matches): a world
// load or a device restart (MainLoop::OnVideoRestart) makes new water programs, nothing matches and a
// new twin is made in the next slot. A replaced twin is left alive (a backend may still hold it in
// cached state): two small objects per world load, sharing the one shore pixel program.
// ============================================================================

#include "ext/better_water/better_water.h"
#include "ext/better_water/shore_params.h"

#include "ext/registry.h"
#if BANISHED_TWEAK_OVERLAY
#include "ext/tweak/ext_tweaks.h"   // the debug overlay's live ShoreParams
#endif

#include "better_water/shore_overlay_lake_ps.h"    // src/render/common: g_bw_PSShoreLake (tools/water_shaders.py)
#include "better_water/shore_overlay_river_ps.h"   // src/render/common: g_bw_PSShoreRiver
#include "better_water/shore_strip_ps.h"           // src/render/common: g_bw_PSShoreStrip
#include "better_water/shore_contact_lake_ps.h"    // src/render/common: g_bw_PSContactLake
#include "better_water/shore_contact_river_ps.h"   // src/render/common: g_bw_PSContactRiver
#include "better_water/scene_depth_backend.h"      // src/render/common: the backend's depth copy
#include "ext/better_water/shore_strip.h"

#include "Game/WorldGeometry.h"
#include "video/Buffers.h"

#include "video/Context.h"
#include "video/ShaderProgram.h"
#include "video/VideoProgram.h"

#include <cstdio>
#include <cstring>
#include <atomic>
#include <mutex>
#include <new>

namespace Ext {
namespace BetterWater {

namespace {

// The shore pixel programs, one per Kind, shared by every twin of that kind. Made once; null after a
// failed create.
constexpr int kKinds = 5;
Video::PixelProgram* g_shorePixel[kKinds] = {};
bool g_shorePixelTried[kKinds] = {};

struct Twin {
    // The game's water program and the parts the twin was made from. A twin is reused only while
    // all four still match: a world load frees the water programs and makes new ones (often at the
    // same address), and a device restart gives them new companions.
    const Video::VideoProgram* water = nullptr;
    Kind kind = Kind::River;
    const void* waterData = nullptr;
    const void* layout = nullptr;
    const void* vertex = nullptr;
    const void* state = nullptr;
    Video::VideoProgram* twin = nullptr;
};

// River and Lake per world, the menu backdrop's pair and a mod's own water materials. When every
// slot is taken the oldest is replaced (round robin): its water program is gone by then.
constexpr int kMaxTwins = 8;
Twin g_twins[kMaxTwins];
int g_nextSlot = 0;

bool Matches(const Twin& t, const Video::VideoProgram* water, Kind kind) {
    return t.water == water && t.kind == kind && t.waterData == static_cast<const void*>(water->_data) &&
           t.layout == static_cast<const void*>(water->_vertexLayout.Get()) &&
           t.vertex == static_cast<const void*>(water->_vertexProgram.Get()) &&
           t.state == static_cast<const void*>(water->_renderState.Get());
}

Video::PixelProgram* ShorePixel(Kind kind) {
    const int k = static_cast<int>(kind);
    if (g_shorePixelTried[k]) return g_shorePixel[k];
    g_shorePixelTried[k] = true;
    const uint8_t* bytes = g_bw_PSShoreRiver;
    int size = static_cast<int>(sizeof(g_bw_PSShoreRiver));
    const char* name = "river";
    if (kind == Kind::Lake) {
        bytes = g_bw_PSShoreLake;
        size = static_cast<int>(sizeof(g_bw_PSShoreLake));
        name = "lake";
    } else if (kind == Kind::Strip) {
        bytes = g_bw_PSShoreStrip;
        size = static_cast<int>(sizeof(g_bw_PSShoreStrip));
        name = "strip";
    } else if (kind == Kind::ContactRiver) {
        bytes = g_bw_PSContactRiver;
        size = static_cast<int>(sizeof(g_bw_PSContactRiver));
        name = "river contact";
    } else if (kind == Kind::ContactLake) {
        bytes = g_bw_PSContactLake;
        size = static_cast<int>(sizeof(g_bw_PSContactLake));
        name = "lake contact";
    }

    // The factory's own create (what a package load runs): the class operator new is not ported.
    Video::PixelProgram* p = static_cast<Video::PixelProgram*>(Video::PixelProgram::CreateNewObject());
    if (p == nullptr || p->_data == nullptr) {
        std::fprintf(stderr, "[BetterWater] no device: the shore program was not made\n");
        return nullptr;
    }
    // One permutation: a backend binds variant 0 for every shader detail the program lacks.
    p->_byteCode.SetSize(1);
    Collection::Array<uint8_t>& blob = p->_byteCode[0];
    blob.SetSize(size);
    std::memcpy(blob._data, bytes, static_cast<size_t>(size));
    if (!p->PlatformCreate()) {
        std::fprintf(stderr, "[BetterWater] the device refused the shore program: no shore on this device\n");
        return nullptr;
    }
    std::fprintf(stderr, "[BetterWater] %s shore program made (%d bytes)\n", name, size);
    g_shorePixel[k] = p;
    return p;
}

Video::VideoProgram* MakeTwin(const Video::VideoProgram* water, Video::PixelProgram* shore) {
    Video::VideoProgram* t = static_cast<Video::VideoProgram*>(Video::VideoProgram::CreateNewObject());
    if (t == nullptr || t->_data == nullptr) return nullptr;
    t->_vertexLayout  = water->_vertexLayout;
    t->_vertexProgram = water->_vertexProgram;
    t->_pixelProgram  = shore;
    t->_renderState   = water->_renderState;
    t->PlatformCreate();
    return t;
}

const Video::VideoProgram* TwinFor(const Video::VideoProgram* water, Kind kind) {
    Video::PixelProgram* shore = ShorePixel(kind);
    if (shore == nullptr) return nullptr;

    for (const Twin& t : g_twins) {
        if (Matches(t, water, kind)) return t.twin;
    }
    // A new water program (a world load, the menu backdrop, a device restart). The twin it replaces
    // is left alive: a backend may still hold it in cached state, and it is a few hundred bytes.
    Twin& t = g_twins[g_nextSlot];
    g_nextSlot = (g_nextSlot + 1) % kMaxTwins;
    t.water = water;
    t.kind = kind;
    t.waterData = water->_data;
    t.layout = water->_vertexLayout.Get();
    t.vertex = water->_vertexProgram.Get();
    t.state = water->_renderState.Get();
    t.twin = MakeTwin(water, shore);
    std::fprintf(stderr, "[BetterWater] twin made for water program %p -> %p\n",
                 static_cast<const void*>(water), static_cast<const void*>(t.twin));
    return t.twin;
}

// ---- the shore strip ---------------------------------------------------------------------------
// One WorldGeometry's strip: its mesh and the two buffers (raw storage + the engine's own ctors, as
// ext.terrain_batch keeps its run index buffer). The game has at most two geometries alive (the menu
// backdrop's and the town's); four slots leave room. Built and drawn on the drawing thread; the
// geometry's destructor and OnVideoError reach it from the main thread, hence the mutex.
struct StripSlot {
    const WorldGeometry* owner = nullptr;
    // What the strip was built from: a reload or the terrain brush changes one of them.
    const void* heights = nullptr;
    const void* waterVertices = nullptr;
    int32_t waterCount = -1;
    bool tried = false;                    // built (or found empty) for this key
    bool ready = false;                    // buffers live on the current device
    Strip::Mesh mesh;
    Video::VertexBuffer* vb = nullptr;     // never `delete`d: operator delete after PlatformDestruct
    Video::IndexBuffer* ib = nullptr;
    bool linked = false;
};

constexpr int kStripSlots = 4;
StripSlot g_strips[kStripSlots];
std::mutex g_stripMutex;
// Set by the first strip built: until then the geometry seams (OnVideoError, Forget) return at once,
// so the faithful path never takes the mutex.
std::atomic<bool> g_anyStrip{false};

StripSlot* FindStrip(const WorldGeometry& geo) {
    for (StripSlot& s : g_strips) {
        if (s.owner == &geo) return &s;
    }
    return nullptr;
}

void UnlinkStrip(StripSlot& s) {
    if (s.linked) {
        s.vb->PlatformDestruct();
        s.ib->PlatformDestruct();
    }
    s.linked = false;
    s.ready = false;
}

// The strip's buffers on the current device, from its mesh. False when there is nothing to draw.
bool UploadStrip(StripSlot& s) {
    if (s.mesh.indices.empty()) return false;
    if (s.vb == nullptr) {
        s.vb = new (::operator new(sizeof(Video::VertexBuffer))) Video::VertexBuffer;
        Video_VertexBuffer_Ctor(s.vb);   // size, dynamic, PlatformConstruct
        s.ib = new (::operator new(sizeof(Video::IndexBuffer))) Video::IndexBuffer;
        Video_IndexBuffer_Ctor(s.ib);
    } else {
        // A fresh device half: Create on a live companion would overwrite its buffer unreleased.
        UnlinkStrip(s);
        s.vb->PlatformConstruct();
        s.ib->PlatformConstruct();
    }
    s.linked = true;
    if (s.vb->platformData == nullptr || s.ib->platformData == nullptr) return false;
    const int vbytes = static_cast<int>(s.mesh.vertices.size() * sizeof(Strip::Vertex));
    const int ibytes = static_cast<int>(s.mesh.indices.size() * sizeof(uint16_t));
    if (!s.ib->Create(ibytes, false, s.mesh.indices.data()) ||
        !s.vb->Create(vbytes, false, s.mesh.vertices.data())) {
        std::fprintf(stderr, "[BetterWater] the strip's buffers (%d + %d bytes) were not made\n", vbytes,
                     ibytes);
        return false;
    }
    s.ready = true;
    return true;
}

// The geometry's strip, built or rebuilt when its water changed; null when it has nothing to draw.
StripSlot* StripFor(const WorldGeometry& geo) {
    const void* heights = geo._heights.data;
    const void* waterVertices = geo._contourVertices.data;
    const int32_t waterCount = geo._contourVertices.size;

    StripSlot* s = FindStrip(geo);
    if (s == nullptr) {
        for (StripSlot& f : g_strips) {
            if (f.owner == nullptr) {
                s = &f;
                break;
            }
        }
        if (s == nullptr) return nullptr;
        s->owner = &geo;
        g_anyStrip.store(true, std::memory_order_release);
    }
    const bool same =
        s->heights == heights && s->waterVertices == waterVertices && s->waterCount == waterCount;
    if (!same || !s->tried) {
        s->heights = heights;
        s->waterVertices = waterVertices;
        s->waterCount = waterCount;
        s->tried = true;
        Strip::Build(geo._heights.data, geo._fieldD0, geo._gridSize >> 1,
                     reinterpret_cast<const Strip::Vertex*>(geo._contourVertices.data), waterCount, s->mesh);
        std::fprintf(stderr, "[BetterWater] strip built: %d cells, %d vertices, %d chunk(s)\n",
                     s->mesh.cells, static_cast<int>(s->mesh.vertices.size()),
                     static_cast<int>(s->mesh.chunks.size()));
        s->ready = false;
    }
    if (!s->ready && !UploadStrip(*s)) return nullptr;
    return s;
}

}  // namespace

bool Enabled() { return Ext::Ext().better_water; }

bool StripEnabled() { return Ext::Ext().better_water && Ext::Val().better_water_mode >= 2; }

bool ContactEnabled() { return StripEnabled() && SceneDepthBackend::Supported(); }

void AfterFrameCopy(void* context) {
    const bool on = ContactEnabled();
    SceneDepthBackend::Request(on);
    if (on) SceneDepthBackend::Copy(context);
}

ContactParams CurrentContactParams() {
#if BANISHED_TWEAK_OVERLAY
    return Ext::Tweak::WaterValues().contact;
#else
    return ContactParams{};
#endif
}

StripParams CurrentStripParams() {
#if BANISHED_TWEAK_OVERLAY
    return Ext::Tweak::WaterValues().strip;
#else
    return StripParams{};
#endif
}

ShoreParams CurrentParams() {
#if BANISHED_TWEAK_OVERLAY
    return Ext::Tweak::WaterValues().p;
#else
    return ShoreParams{};
#endif
}

void DrawShore(Video::Context& context, Kind kind, int32_t a, int32_t b, int32_t c, int32_t d) {
    // The water material bound its VideoProgram (VideoProgram::SetupShaders) before WorldGeometry's
    // draw ran; that is the program the twin stands in for.
    const Video::VideoProgram* water = context._pixelConstantStaging.videoProgram;
    if (water == nullptr) return;
    const Video::VideoProgram* twin = TwinFor(water, kind);
    if (twin == nullptr) return;

    // The look goes to the shore program as its MaterialConstants (b1): written into the context's
    // material staging for this draw only, and retail's put back after it (dirty bit 0x8000 uploads
    // the staging at the next draw), so the next draw item sees exactly what it would have.
    float saved[4][4];
    std::memcpy(saved, context._pixelConstantStaging.material, sizeof(saved));
    const ShoreParams params = CurrentParams();
    static_assert(sizeof(params) == sizeof(saved), "ShoreParams fills the material staging");
    std::memcpy(context._pixelConstantStaging.material, &params, sizeof(saved));
    context._dirty |= 0x8000u;

    twin->SetupShaders(context);
    context.DrawIndexed(0, a, b, c, d, 1);

    // Fancy's contact foam: a third draw of the range with the contact program, the backend's scene
    // depth copy bound at t4 for it alone (slot 4 put back after: no water material uses it).
    const Video::ImageBuffer* depth = ContactEnabled() ? SceneDepthBackend::Image() : nullptr;
    if (depth != nullptr) {
        const Kind contactKind = kind == Kind::Lake ? Kind::ContactLake : Kind::ContactRiver;
        const Video::VideoProgram* contact = TwinFor(water, contactKind);
        if (contact != nullptr) {
            const ContactParams cp = CurrentContactParams();
            static_assert(sizeof(cp) == sizeof(saved), "ContactParams fills the material staging");
            std::memcpy(context._pixelConstantStaging.material, &cp, sizeof(saved));
            context._dirty |= 0x8000u;
            const Video::ImageBuffer* slot4 = context._imageBuffers[4];
            context.SetImageBuffer(depth, 4);
            contact->SetupShaders(context);
            context.DrawIndexed(0, a, b, c, d, 1);
            context.SetImageBuffer(slot4, 4);
        }
    }

    std::memcpy(context._pixelConstantStaging.material, saved, sizeof(saved));
    context._dirty |= 0x8000u;
    water->SetupShaders(context);
}

void DrawStrip(const WorldGeometry& geo, Video::Context& context) {
    const Video::VideoProgram* water = context._pixelConstantStaging.videoProgram;
    if (water == nullptr) return;
    const Video::VideoProgram* twin = TwinFor(water, Kind::Strip);
    if (twin == nullptr) return;

    const std::lock_guard<std::mutex> lock(g_stripMutex);
    StripSlot* s = StripFor(geo);
    if (s == nullptr) return;

    // The strip's look, as DrawShore hands Plain's (the material staging for this draw only).
    float saved[4][4];
    std::memcpy(saved, context._pixelConstantStaging.material, sizeof(saved));
    const StripParams params = CurrentStripParams();
    static_assert(sizeof(params) == sizeof(saved), "StripParams fills the material staging");
    std::memcpy(context._pixelConstantStaging.material, &params, sizeof(saved));
    context._dirty |= 0x8000u;

    twin->SetupShaders(context);
    context.SetVertexBuffer(s->vb, 0, 0, static_cast<int32_t>(sizeof(Strip::Vertex)));
    // The inlined SetIndexBuffer of WorldGeometry::Draw (dirty bit 0x10).
    if (context._indexBuffer != s->ib || context._indexOffset != 0) {
        context._dirty |= 0x10u;
        context._indexBuffer = s->ib;
        context._indexOffset = 0;
    }
    // A contour range's DrawIndexed arguments: base vertex, vertex count, first index, index count.
    for (const Strip::Chunk& c : s->mesh.chunks) {
        context.DrawIndexed(0, c.vertexStart, c.vertexCount, c.indexStart, c.indexCount, 1);
    }

    std::memcpy(context._pixelConstantStaging.material, saved, sizeof(saved));
    context._dirty |= 0x8000u;
    water->SetupShaders(context);
}

void OnVideoError(const WorldGeometry& geo) {
    if (!g_anyStrip.load(std::memory_order_acquire)) return;
    const std::lock_guard<std::mutex> lock(g_stripMutex);
    StripSlot* s = FindStrip(geo);
    if (s != nullptr) UnlinkStrip(*s);
}

void Forget(const WorldGeometry& geo) {
    if (!g_anyStrip.load(std::memory_order_acquire)) return;
    const std::lock_guard<std::mutex> lock(g_stripMutex);
    StripSlot* s = FindStrip(geo);
    if (s == nullptr) return;
    UnlinkStrip(*s);
    // No ~VertexBuffer / ~IndexBuffer: they would PlatformDestruct (unlink) again.
    ::operator delete(static_cast<void*>(s->vb));
    ::operator delete(static_cast<void*>(s->ib));
    *s = StripSlot{};
}

}  // namespace BetterWater
}  // namespace Ext
