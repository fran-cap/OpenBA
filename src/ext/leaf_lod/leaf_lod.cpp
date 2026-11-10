// ============================================================================
// src/ext/leaf_lod/leaf_lod.cpp -- ext.leaf_lod: fewer, larger leaf cards when zoomed out (leaf_lod.h)
// ============================================================================
// NOT RETAIL. docs/extensions/LEAF_LOD.md. The reduction itself is leaf_lod_build.cpp (pure); this file
// finds the canopy meshes as they draw, reads their geometry back, keeps the levels' buffers and answers
// the draw's seam.
//
// LIFETIMES. A mesh's levels (an Entry) never change once the hook can see them: a rebuild (the debug
// overlay moved keep / grow) publishes a new Entry and retires the old one, which is released
// kRetireFrames frames later, long after any draw that read it has ended. Forget and OnVideoLost run while
// nothing draws.
// ============================================================================
#include "ext/leaf_lod/leaf_lod.h"
#include "ext/leaf_lod/leaf_lod_build.h"
#include "ext/leaf_lod/leaf_lod_params.h"
#include "ext/registry.h"
#if BANISHED_TWEAK_OVERLAY
#include "ext/tweak/ext_tweaks.h"
#endif

#include "Game/GameUpdateLoop.h"
#include "Game/PlayerController.h"
#include "graphics/GraphicsMesh.h"
#include "graphics/Material.h"
#include "graphics/Mesh.h"
#include "io/Serializer.h"
#include "io/Stream.h"
#include "object/External.h"
#include "video/Buffers.h"
#include "video/CompanionData.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <new>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace Ext {
namespace LeafLod {

namespace {

constexpr int kRetireFrames = 60;   // frames a replaced Entry outlives its last possible draw
constexpr int kSettleFrames = 20;   // frames the overlay's keep / grow must hold still before a rebuild

Look CurrentLook() {
#if BANISHED_TWEAK_OVERLAY
    return Tweak::LeafLodValues();
#else
    return Look{};
#endif
}

std::string PathOf(const Object::External* object) {
    if (object == nullptr) return std::string();
    const System::Filename path = object->GetPath();
    std::string r = path._data != nullptr ? path._data : "";
    for (char& c : r) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return r;
}

// ---- a mesh's levels ----------------------------------------------------------------------------

struct Set {
    Video::VertexBuffer* vb = nullptr;
    Video::IndexBuffer*  ib = nullptr;
    std::vector<Graphics::GraphicsMesh::TriangleList> lists;   // by the level's list index
    std::vector<uint8_t> use;                                  // 1: the list draws from this set
};

struct Entry {
    Set levels[kLevels];
};

struct Slot {
    Entry* entry = nullptr;   // null: not a canopy mesh, or not built yet
    bool canopy = false;
};

std::shared_mutex g_mutex;
std::unordered_map<const Graphics::GraphicsMesh*, Slot> g_meshes;
std::vector<const Graphics::GraphicsMesh*> g_pending;   // canopy meshes waiting for their levels (Frame)
std::atomic<uint32_t> g_generation{1};                  // bumped whenever g_meshes' entries change
std::atomic<int> g_level{0};                            // the level in force, 0 = the mesh's own canopy

struct Retired {
    Entry* entry;
    int frame;
};
std::vector<Retired> g_retired;   // main thread
int g_frame = 0;
float g_builtKeep = -1.0f;
float g_builtGrow = -1.0f;
float g_builtOutline = -1.0f;
float g_wantKeep = -1.0f;   // the overlay's newest keep / grow, and the frame they last moved
float g_wantGrow = -1.0f;
float g_wantOutline = -1.0f;
int g_wantFrame = 0;

void ReleaseSet(Set& s) {
    if (s.vb != nullptr) {
        s.vb->PlatformDestruct();
        ::operator delete(s.vb);
    }
    if (s.ib != nullptr) {
        s.ib->PlatformDestruct();
        ::operator delete(s.ib);
    }
    s.vb = nullptr;
    s.ib = nullptr;
}

void ReleaseEntry(Entry* e) {
    if (e == nullptr) return;
    for (Set& s : e->levels) ReleaseSet(s);
    delete e;
}

// ---- the mesh's own geometry --------------------------------------------------------------------

// A plain writing MemoryStream (its destructor is protected; this subclass is what ends it).
struct FlatStream : IO::MemoryStream {};

// The buffer's bytes through its companion's save arm (a staging copy on DX11, a read back on Vulkan; a
// buffer whose create is deferred is made first). Empty when there is nothing to read.
template <class Data>
std::vector<uint8_t> ReadBack(Data* data, int32_t size) {
    std::vector<uint8_t> out;
    if (data == nullptr || size <= 0) return out;
    alignas(16) uint8_t storage[sizeof(FlatStream)];
    FlatStream* fs = ::new (static_cast<void*>(storage)) FlatStream();
    if (fs->Create(size + 256)) {
        {
            IO::Serializer serializer(*fs, 0);
            data->Serialize(&serializer);
            // the save arm writes the dynamic flag, the size, then the payload: the last `size` bytes
            const uint32_t end = fs->GetCurrentPos();
            if (end >= static_cast<uint32_t>(size) && fs->_data != nullptr) {
                const uint8_t* bytes = static_cast<const uint8_t*>(fs->_data) + (end - static_cast<uint32_t>(size));
                out.assign(bytes, bytes + size);
            }
        }
    }
    fs->~FlatStream();
    return out;
}

bool IsBillboard(const Graphics::GraphicsMesh* mesh, int32_t materialIndex) {
    if (mesh->_skinMaterials._size < 1) return false;
    const auto& skin = mesh->_skinMaterials._data[0];
    if (materialIndex < 0 || materialIndex >= skin._size) return false;
    const Graphics::MaterialInstance* mi = skin._data[materialIndex].Get();
    const Graphics::Material* m = mi != nullptr ? mi->_material.Get() : nullptr;
    return m != nullptr && PathOf(m).find("billboard") != std::string::npos;
}

// Does the mesh's first level draw a rigid list with a billboard material?
bool IsCanopyMesh(const Graphics::GraphicsMesh* mesh) {
    if (mesh->_detailLevels._size < 1) return false;
    const Graphics::GraphicsMesh::DetailLevel& level = mesh->_detailLevels._data[0];
    for (int32_t i = 0; i < level._triangleLists._size; ++i) {
        const Graphics::GraphicsMesh::TriangleList& l = level._triangleLists._data[i];
        if (l._node >= 0 && IsBillboard(mesh, l._materialIndex)) return true;
    }
    return false;
}

bool Upload(Set& s, const Reduced& r) {
    s.vb = new (::operator new(sizeof(Video::VertexBuffer))) Video::VertexBuffer;
    Video_VertexBuffer_Ctor(s.vb);   // size, dynamic, PlatformConstruct
    s.ib = new (::operator new(sizeof(Video::IndexBuffer))) Video::IndexBuffer;
    Video_IndexBuffer_Ctor(s.ib);
    if (s.vb->platformData == nullptr || s.ib->platformData == nullptr) return false;
    const int vbytes = static_cast<int>(r.vertices.size());
    const int ibytes = static_cast<int>(r.indices.size() * sizeof(uint16_t));
    if (vbytes == 0 || ibytes == 0) return false;
    // Create copies the bytes (a seeded static buffer); it takes them as void*
    std::vector<uint8_t> v = r.vertices;
    std::vector<uint16_t> i = r.indices;
    return s.ib->Create(ibytes, false, i.data()) && s.vb->Create(vbytes, false, v.data());
}

// The mesh's levels for `look`, or null (logged) when its geometry could not be read or rebuilt.
Entry* Build(const Graphics::GraphicsMesh* mesh, const Look& look, bool log) {
    const Graphics::GraphicsMesh::DetailLevel& level = mesh->_detailLevels._data[0];
    const std::vector<uint8_t> vertices = ReadBack(level._vertexBuffer.platformData, level._vertexBuffer.size);
    const std::vector<uint8_t> indexBytes = ReadBack(level._indexBuffer.platformData, level._indexBuffer.size);
    if (vertices.empty() || indexBytes.empty()) {
        std::fprintf(stderr, "[LeafLod] %s: its buffers read back empty (%d + %d bytes), left as it is\n",
                     PathOf(mesh).c_str(), static_cast<int>(vertices.size()), static_cast<int>(indexBytes.size()));
        return nullptr;
    }
    std::vector<uint16_t> indices(indexBytes.size() / sizeof(uint16_t));
    std::memcpy(indices.data(), indexBytes.data(), indices.size() * sizeof(uint16_t));

    std::vector<List> lists(static_cast<size_t>(level._triangleLists._size));
    for (int32_t i = 0; i < level._triangleLists._size; ++i) {
        const Graphics::GraphicsMesh::TriangleList& l = level._triangleLists._data[i];
        lists[i].baseVertex = l._baseVertex;
        lists[i].vertexCount = l._vertexCount;
        lists[i].startIndex = l._startIndex;
        lists[i].indexCount = l._indexCount;
        lists[i].canopy = l._node >= 0 && IsBillboard(mesh, l._materialIndex);
    }

    const Plan plan = Analyse(vertices.data(), vertices.size(), indices.data(), indices.size(), lists);
    if (plan.cards == 0) {
        if (log) {
            std::fprintf(stderr, "[LeafLod] %s: no leaf cards in %d lists, left as it is\n", PathOf(mesh).c_str(),
                         static_cast<int>(lists.size()));
        }
        return nullptr;
    }
    Entry* e = new Entry();
    int last = 0;
    size_t bytes = 0;
    Shape shape;
    shape.keepLast = look.keep;
    shape.grow = look.grow;
    shape.outline = look.outline;
    for (int n = 0; n < kLevels; ++n) {
        const float progress = static_cast<float>(n + 1) / static_cast<float>(kLevels);
        const Reduced r = Reduce(plan, vertices.data(), vertices.size(), indices.data(), progress, shape);
        last = r.drawn;
        bytes += r.vertices.size() + r.indices.size() * sizeof(uint16_t);
        Set& s = e->levels[n];
        if (!Upload(s, r)) {
            if (log) {
                std::fprintf(stderr, "[LeafLod] %s: level %d was not made, left as it is\n", PathOf(mesh).c_str(),
                             n + 1);
            }
            ReleaseEntry(e);
            return nullptr;
        }
        s.lists.resize(lists.size());
        s.use.assign(lists.size(), 0);
        for (size_t i = 0; i < lists.size(); ++i) {
            s.lists[i] = level._triangleLists._data[i];
            s.lists[i]._startIndex = r.lists[i].startIndex;
            s.lists[i]._indexCount = r.lists[i].indexCount;
            // every rigid list draws from the set (they share the level's vertex and index buffers)
            s.use[i] = level._triangleLists._data[i]._node >= 0 ? 1 : 0;
        }
    }
    if (log) {
        std::fprintf(stderr, "[LeafLod] %s: %d lists, %d leaf cards, %d levels down to %d cards, %d KB\n",
                     PathOf(mesh).c_str(), static_cast<int>(lists.size()), plan.cards, kLevels, last,
                     static_cast<int>(bytes / 1024));
    }
    return e;
}

// ---- the hook (the drawing thread) --------------------------------------------------------------

Entry* EntryOf(const Graphics::GraphicsMesh* mesh) {
    {
        std::shared_lock<std::shared_mutex> lock(g_mutex);
        auto it = g_meshes.find(mesh);
        if (it != g_meshes.end()) return it->second.entry;
    }
    const bool canopy = IsCanopyMesh(mesh);
    std::unique_lock<std::shared_mutex> lock(g_mutex);
    auto it = g_meshes.find(mesh);
    if (it != g_meshes.end()) return it->second.entry;
    Slot slot;
    slot.canopy = canopy;
    g_meshes.emplace(mesh, slot);
    if (canopy) g_pending.push_back(mesh);
    return nullptr;
}

void DrawLists(const Graphics::MeshInstance& instance, int32_t level, int32_t list,
               const Video::VertexBuffer** vertexBuffer, const Video::IndexBuffer** indexBuffer,
               const Graphics::GraphicsMesh::TriangleList** range) {
    if (level != 0) return;
    const Graphics::GraphicsMesh* mesh = instance.GetMesh().Get();
    thread_local const Graphics::GraphicsMesh* lastMesh = nullptr;
    thread_local Entry* lastEntry = nullptr;
    thread_local uint32_t lastGeneration = 0;
    const uint32_t generation = g_generation.load(std::memory_order_acquire);
    if (mesh != lastMesh || generation != lastGeneration) {
        lastEntry = EntryOf(mesh);
        lastMesh = mesh;
        lastGeneration = generation;
    }
    const int force = g_level.load(std::memory_order_relaxed);
    if (lastEntry == nullptr || force <= 0) return;
    const Set& s = lastEntry->levels[std::min(force, kLevels) - 1];
    if (list < 0 || static_cast<size_t>(list) >= s.use.size() || s.use[list] == 0) return;
    *vertexBuffer = s.vb;
    *indexBuffer = s.ib;
    *range = &s.lists[list];
}

// ---- the main thread ----------------------------------------------------------------------------

void ReleaseAll() {
    Graphics::MeshInstance::_extDrawLists = nullptr;
    g_level.store(0, std::memory_order_relaxed);
    {
        std::unique_lock<std::shared_mutex> lock(g_mutex);
        for (auto& kv : g_meshes) ReleaseEntry(kv.second.entry);
        g_meshes.clear();
        g_pending.clear();
        g_generation.fetch_add(1, std::memory_order_release);
    }
    for (Retired& r : g_retired) ReleaseEntry(r.entry);
    g_retired.clear();
}

void Publish(const Graphics::GraphicsMesh* mesh, Entry* e) {
    std::unique_lock<std::shared_mutex> lock(g_mutex);
    auto it = g_meshes.find(mesh);
    if (it == g_meshes.end()) {
        ReleaseEntry(e);
        return;
    }
    if (it->second.entry != nullptr) g_retired.push_back(Retired{it->second.entry, g_frame});
    it->second.entry = e;
    g_generation.fetch_add(1, std::memory_order_release);
}

}  // namespace

void Frame(GameUpdateLoop& game) {
    if (!Ext::Ext().leaf_lod || game.m_playerController == nullptr) {
        g_level.store(0, std::memory_order_relaxed);
        Graphics::MeshInstance::_extDrawLists = nullptr;
        return;
    }
    ++g_frame;
    const Look look = CurrentLook();
    const int level = look.force >= 0 ? std::min(look.force, kLevels)
                                      : LevelAt(game.m_playerController->distance, look.start, look.end, kLevels);
    g_level.store(level, std::memory_order_relaxed);

    // retired levels no draw can still hold
    size_t alive = 0;
    for (Retired& r : g_retired) {
        if (g_frame - r.frame >= kRetireFrames) {
            ReleaseEntry(r.entry);
        } else {
            g_retired[alive++] = r;
        }
    }
    g_retired.resize(alive);

    // the look's keep / grow moved (the debug overlay): every built mesh again, once the sliders have held
    // still for kSettleFrames (a rebuild makes kLevels buffer pairs per mesh)
    std::vector<const Graphics::GraphicsMesh*> build;
    bool log = true;
    Look built = look;
    if (g_builtKeep < 0.0f) {
        g_builtKeep = look.keep;
        g_builtGrow = look.grow;
        g_builtOutline = look.outline;
    } else if (look.keep != g_builtKeep || look.grow != g_builtGrow || look.outline != g_builtOutline) {
        if (look.keep != g_wantKeep || look.grow != g_wantGrow || look.outline != g_wantOutline) {
            g_wantKeep = look.keep;
            g_wantGrow = look.grow;
            g_wantOutline = look.outline;
            g_wantFrame = g_frame;
        }
        if (g_frame - g_wantFrame >= kSettleFrames) {
            log = false;
            g_builtKeep = look.keep;
            g_builtGrow = look.grow;
            g_builtOutline = look.outline;
            std::shared_lock<std::shared_mutex> lock(g_mutex);
            for (auto& kv : g_meshes) {
                if (kv.second.entry != nullptr) build.push_back(kv.first);
            }
        } else {
            // a mesh first seen meanwhile is built with the look the others have
            built.keep = g_builtKeep;
            built.grow = g_builtGrow;
            built.outline = g_builtOutline;
        }
    }
    {
        std::unique_lock<std::shared_mutex> lock(g_mutex);
        build.insert(build.end(), g_pending.begin(), g_pending.end());
        g_pending.clear();
    }
    for (const Graphics::GraphicsMesh* mesh : build) {
        Entry* e = Build(mesh, built, log);
        if (e != nullptr) Publish(mesh, e);
    }
    Graphics::MeshInstance::_extDrawLists = &DrawLists;
}

void Forget() {
    ReleaseAll();
}

void OnVideoLost() {
    ReleaseAll();
}

}  // namespace LeafLod
}  // namespace Ext
