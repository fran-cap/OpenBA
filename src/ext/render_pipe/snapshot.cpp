// ============================================================================
// src/ext/render_pipe/snapshot.cpp -- RENDERPIPE: the frozen copies of a packet's
// geometry (snapshot.h)
// ============================================================================
// NOT RETAIL. ext.render_pipe (docs/android/P13_OVERLAP.md section 2.2).
//
// What each copy carries, from the bodies pass 2 runs on it:
//   MeshInstance        Draw, GetBones, GetInstanceTransform, GetNodeToWorld,
//                       SetSharedInstanceData, SkinMemo: the Geometry header,
//                       _mesh (a resource), _materials, _animation (+ the
//                       instanced record, frozen: _nodes, _pose, its layers),
//                       _nodes, _rootAnimationOffset, _color
//   EdgeMeshInstance    Draw, GetInstanceTransform: the header, _mesh, and
//                       _followed, re-pointed at the followed mesh's copy
//   ParticleInstance /  Draw, GetInstanceTransform, ext.draw_batch's smoke run:
//   ParticleSystemInstance / TornadoInstance
//                       the header and the _boundsPtr record with its _records
//   StatusIconGeometry  Draw: inline fields and a sprite sheet (a resource)
//   WeatherParticleGeometry  Draw: _descriptor (a resource), _particles, _indices
//   WorldGeometry       Draw's decal / road modes and ext.draw_batch's decal run:
//                       the header, _transform / _opacity, the two batch arrays
//                       (the batches the packet draws deep, the others emptied).
//                       Its other modes keep the live object (P13_OVERLAP 2.2).
// Pointers into the live object's graph that pass 2 never follows stay as they
// are (Geometry::_attached / _attachParent, WorldGeometry's terrain arrays).
// ============================================================================
#include "ext/render_pipe/snapshot.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>   // std::hash<const void*>, std::less<const void*>: no pointer casts
#include <memory>
#include <type_traits>
#include <typeinfo>
#include <vector>

#include "graphics/AnimationManager.h"
#include "graphics/AnimationState.h"
#include "graphics/Camera.h"        // Renderer, RendererDrawItem, RendererDrawGroup, RendererDrawable
#include "graphics/Geometry.h"
#include "graphics/Mesh.h"
#include "graphics/ParticleInstance.h"
#include "graphics/SunLight.h"
#include "Component/StatusIconComponent.h"
#include "Game/Tornado.h"
#include "Game/WeatherParticleGeometry.h"
#include "Game/WorldGeometry.h"
#include "Game/WorldGeometryElements.h"

namespace Ext {
namespace RenderPipe {
namespace {

using Graphics::Geometry;
using Graphics::InstancedAnimationState;
using Graphics::MeshInstance;

static_assert(__STDCPP_DEFAULT_NEW_ALIGNMENT__ >= 16, "the arena's base is 16-byte aligned");
constexpr size_t kAlign = 16;

constexpr size_t Round(size_t n) {
    return (n + kAlign - 1) & ~(kAlign - 1);
}

template <class T>
size_t ArrayBytes(int32_t count) {
    return count > 0 ? Round(static_cast<size_t>(count) * sizeof(T)) : 0;
}

// Arena bytes viewed as the copy they hold.
template <class T>
T* Place(unsigned char* at) {
    return static_cast<T*>(static_cast<void*>(at));   // CAST-OK: alloc_header
}

// One Renderer slot's copies. `begin` / `end` bound the last snapshot's bytes; the main
// thread alone writes them (BuildSnapshot, FreeSnapshots) and both threads read them
// without a lock in FrozenInstancedState, hence the atomics (relaxed: see there).
struct Arena {
    std::unique_ptr<unsigned char[]>        storage;
    size_t                                  capacity = 0;
    std::atomic<const unsigned char*>       begin{nullptr};
    std::atomic<const unsigned char*>       end{nullptr};

    unsigned char* Base() const { return storage.get(); }
    bool Holds(const void* p) const {
        const std::less<const void*> before;
        const unsigned char* const b = begin.load(std::memory_order_relaxed);
        const unsigned char* const e = end.load(std::memory_order_relaxed);
        return b != nullptr && !before(p, b) && before(p, e);
    }
    void Clear() {
        begin.store(nullptr, std::memory_order_relaxed);
        end.store(nullptr, std::memory_order_relaxed);
    }
};

Arena g_arenas[2];

// The word before a MeshInstance copy: its frozen instanced record (null: not instanced).
struct alignas(16) MeshHeader {
    const InstancedAnimationState* frozen;
    const MeshInstance*            copy;
};

enum class Kind : uint8_t { Mesh, Edge, Particle, StatusIcon, Weather, WorldBatches };

struct Entry {
    const Geometry* live   = nullptr;
    Geometry*       copy   = nullptr;
    size_t          offset = 0;     // in the arena
    size_t          size   = 0;     // sizeof the object's dynamic type
    Kind            kind   = Kind::Mesh;
    int32_t         state  = -1;    // Mesh, instanced: the index of its frozen record
};

struct StateEntry {
    const InstancedAnimationState* live   = nullptr;
    InstancedAnimationState*       copy   = nullptr;
    size_t                         offset = 0;
};

// An open-addressing table from a live pointer to an index, cleared by generation.
class PointerTable {
public:
    void Reset(size_t expected) {
        size_t want = 64;
        while (want < expected * 2) want *= 2;
        if (_slots.size() < want) {
            _slots.assign(want, Slot{});
            _gen = 0;
        }
        ++_gen;
        if (_gen == 0) {   // wrapped: clear once
            for (Slot& s : _slots) s.gen = 0;
            _gen = 1;
        }
    }
    // The index stored for `key`, or -1; `insert` >= 0 stores it when absent.
    int32_t Find(const void* key, int32_t insert) {
        const size_t mask = _slots.size() - 1;
        size_t i = Mix(key) & mask;
        for (;;) {
            Slot& s = _slots[i];
            if (s.gen != _gen) {
                if (insert >= 0) {
                    s.gen   = _gen;
                    s.key   = key;
                    s.index = insert;
                }
                return -1;
            }
            if (s.key == key) return s.index;
            i = (i + 1) & mask;
        }
    }
    size_t Capacity() const { return _slots.size(); }

private:
    static size_t Mix(const void* p) {
        const uint64_t h = static_cast<uint64_t>(std::hash<const void*>{}(p));
        return static_cast<size_t>((h >> 4) * 0x9E3779B97F4A7C15ull >> 16);
    }
    struct Slot {
        const void* key   = nullptr;
        int32_t     index = 0;
        uint32_t    gen   = 0;
    };
    std::vector<Slot> _slots;
    uint32_t          _gen = 0;
};

// The builder's scratch, kept across frames (main thread only).
struct Builder {
    std::vector<Entry>      entries;
    std::vector<StateEntry> states;
    PointerTable            geometries;
    PointerTable            stateTable;
    std::vector<uint32_t>   decalMark;   // per WorldGeometry::_decals index: drawn this packet
    std::vector<uint32_t>   roadMark;
    // Per marked batch: the arena offset of its vertices and indices. They are reserved when the
    // batch is marked, after any entry admitted since the WorldGeometry, so not next to its arrays.
    std::vector<size_t>     decalAt;
    std::vector<size_t>     roadAt;
    uint32_t                markGen = 0;
    const WorldGeometry*    world   = nullptr;
};
Builder g_builder;

constexpr uint32_t kModeMask  = 0xE000u;   // WorldGeometry::Draw's mode bits of the item key
constexpr uint32_t kModeDecal = 0x2000u;
constexpr uint32_t kModeRoad  = 0x4000u;

const InstancedAnimationState* LiveInstancedState(const MeshInstance& mi) {
    const int32_t handle = mi._animation._layers._data[mi._animation._layers._size - 1]._handle;
    return &Graphics::AnimationManager::_singleton->_instances._data[(handle >> 8) & 0xff]
                ._states._data[handle & 0xff];
}

size_t StateBytes(const InstancedAnimationState& s) {
    return Round(sizeof(InstancedAnimationState)) +
           ArrayBytes<Graphics::AnimationState::Layer>(s._state._layers._size) +
           ArrayBytes<Math::Transform>(s._pose._size) +
           ArrayBytes<Graphics::InterpolatedState<Math::Transform>>(s._nodes._size);
}

size_t MeshBytes(const MeshInstance& mi) {
    return Round(sizeof(MeshHeader)) + Round(sizeof(MeshInstance)) +
           ArrayBytes<Object::ExternalPtr<Graphics::MaterialInstance>>(mi._materials._size) +
           ArrayBytes<Graphics::AnimationState::Layer>(mi._animation._layers._size) +
           ArrayBytes<Graphics::InterpolatedState<Math::Transform>>(mi._nodes._size);
}

// The copy's array header still points at the live elements: copy them to `at` and point at them.
template <class A>
void DeepCopy(A& array, unsigned char*& at) {
    using T = std::remove_reference_t<decltype(*array._data)>;
    if (array._data == nullptr) return;
    const int32_t n = array._size > 0 ? array._size : 0;
    if (n > 0) std::memcpy(at, array._data, static_cast<size_t>(n) * sizeof(T));
    array._data     = Place<T>(at);
    array._capacity = n;
    at += ArrayBytes<T>(n);
}

template <class T>
void DeepCopyRaw(RawArray<T>& array, unsigned char*& at) {
    if (array.data == nullptr) return;
    const int32_t n = array.size > 0 ? array.size : 0;
    if (n > 0) std::memcpy(at, array.data, static_cast<size_t>(n) * sizeof(T));
    array.data     = Place<T>(at);
    array.capacity = n;
    at += ArrayBytes<T>(n);
}

// Pass A: the entry for `g`, made (and its bytes counted) the first time it is seen.
// Returns its index, or -1 with `why` set when the type is not covered.
int32_t Admit(Builder& b, const Geometry* g, size_t& bytes, const char** why);

int32_t AdmitMesh(Builder& b, const MeshInstance* mi, size_t& bytes) {
    const int32_t index = static_cast<int32_t>(b.entries.size());
    const int32_t found = b.geometries.Find(mi, index);
    if (found >= 0) return found;
    Entry e;
    e.live = mi;
    e.kind = Kind::Mesh;
    e.size = sizeof(MeshInstance);
    e.offset = bytes;
    bytes += MeshBytes(*mi);
    if (mi->_animation._instanced && mi->_animation._layers._size > 0) {
        const InstancedAnimationState* const live = LiveInstancedState(*mi);
        const int32_t stateIndex = static_cast<int32_t>(b.states.size());
        int32_t s = b.stateTable.Find(live, stateIndex);
        if (s < 0) {
            StateEntry se;
            se.live   = live;
            se.offset = bytes;
            bytes += StateBytes(*live);
            b.states.push_back(se);
            s = stateIndex;
        }
        e.state = s;
    }
    b.entries.push_back(e);
    return index;
}

int32_t Admit(Builder& b, const Geometry* g, size_t& bytes, const char** why) {
    const int32_t known = b.geometries.Find(g, -1);
    if (known >= 0) return known;
    const std::type_info& t = typeid(*g);
    if (t == typeid(MeshInstance)) return AdmitMesh(b, static_cast<const MeshInstance*>(g), bytes);

    Entry e;
    e.live = g;
    e.offset = bytes;
    if (t == typeid(Graphics::EdgeMeshInstance)) {
        const auto* const edge = static_cast<const Graphics::EdgeMeshInstance*>(g);
        if (edge->_followed != nullptr) {
            if (typeid(*edge->_followed) != typeid(MeshInstance)) {
                *why = typeid(*edge->_followed).name();
                return -1;
            }
            AdmitMesh(b, edge->_followed, bytes);
            e.offset = bytes;
        }
        e.kind = Kind::Edge;
        e.size = sizeof(Graphics::EdgeMeshInstance);
        bytes += Round(e.size);
    } else if (t == typeid(Graphics::ParticleInstance) || t == typeid(Graphics::ParticleSystemInstance) ||
               t == typeid(TornadoInstance)) {
        const auto* const pi = static_cast<const Graphics::ParticleInstance*>(g);
        e.kind = Kind::Particle;
        e.size = t == typeid(Graphics::ParticleInstance)         ? sizeof(Graphics::ParticleInstance)
                 : t == typeid(Graphics::ParticleSystemInstance) ? sizeof(Graphics::ParticleSystemInstance)
                                                                 : sizeof(TornadoInstance);
        bytes += Round(e.size);
        if (pi->_boundsPtr != nullptr) {
            bytes += Round(sizeof(RtGraphics::ParticleBoundsRecord)) +
                     ArrayBytes<RtGraphics::ParticleRecord>(pi->_boundsPtr->_records._size);
        }
    } else if (t == typeid(StatusIconGeometry)) {
        e.kind = Kind::StatusIcon;
        e.size = sizeof(StatusIconGeometry);
        bytes += Round(e.size);
    } else if (t == typeid(WeatherParticleGeometry)) {
        const auto* const w = static_cast<const WeatherParticleGeometry*>(g);
        e.kind = Kind::Weather;
        e.size = sizeof(WeatherParticleGeometry);
        bytes += Round(e.size) + ArrayBytes<WeatherParticle>(w->_particles.size) +
                 ArrayBytes<uint16_t>(w->_indices._size);
    } else if (t == typeid(WorldGeometry)) {
        // Only its decal / road items reach here (the caller keeps the others live).
        const auto* const world = static_cast<const WorldGeometry*>(g);
        if (b.world != nullptr) {
            *why = "a second WorldGeometry";
            return -1;
        }
        b.world = world;
        e.kind = Kind::WorldBatches;
        e.size = sizeof(WorldGeometry);
        bytes += Round(e.size) + ArrayBytes<GeometryBatch>(world->_decals._size) +
                 ArrayBytes<GeometryBatch>(world->_roadBatches._size);
        // The drawn batches' vertices and indices are counted as the items mark them.
    } else {
        *why = t.name();
        return -1;
    }
    const int32_t index = static_cast<int32_t>(b.entries.size());
    b.geometries.Find(g, index);
    b.entries.push_back(e);
    return index;
}

// A decal / road item: mark its batch (counted once).
bool MarkBatch(Builder& b, uint32_t mode, uint32_t key, size_t& bytes) {
    const WorldGeometry& world = *b.world;
    const bool decal = mode == kModeDecal;
    const GeometryBatchArray& array = decal ? world._decals : world._roadBatches;
    std::vector<uint32_t>& marks = decal ? b.decalMark : b.roadMark;
    std::vector<size_t>& at = decal ? b.decalAt : b.roadAt;
    const int32_t index = static_cast<int32_t>(key & 0xFFFF1FFFu);   // WorldGeometry::Draw's key
    if (array._data == nullptr || index < 0 || index >= array._size) return false;
    if (marks.size() < static_cast<size_t>(array._size)) {
        marks.resize(static_cast<size_t>(array._size), 0);
        at.resize(static_cast<size_t>(array._size), 0);
    }
    if (marks[static_cast<size_t>(index)] == b.markGen) return true;
    marks[static_cast<size_t>(index)] = b.markGen;
    at[static_cast<size_t>(index)] = bytes;
    const GeometryBatch& batch = array._data[index];
    bytes += ArrayBytes<GeometryBatchVertex>(batch.vertices._size) + ArrayBytes<uint16_t>(batch.indices._size);
    return true;
}

// Pass B, one entry: the copy at its offset.
void CopyState(StateEntry& s, unsigned char* base) {
    unsigned char* at = base + s.offset;
    std::memcpy(at, s.live, sizeof(InstancedAnimationState));
    InstancedAnimationState* const copy = Place<InstancedAnimationState>(at);
    at += Round(sizeof(InstancedAnimationState));
    DeepCopy(copy->_state._layers, at);
    DeepCopy(copy->_pose, at);
    DeepCopy(copy->_nodes, at);
    s.copy = copy;
}

void CopyMesh(Entry& e, const Builder& b, unsigned char* base) {
    unsigned char* at = base + e.offset;
    MeshHeader* const header = Place<MeshHeader>(at);
    at += Round(sizeof(MeshHeader));
    std::memcpy(at, e.live, sizeof(MeshInstance));
    MeshInstance* const copy = Place<MeshInstance>(at);
    at += Round(sizeof(MeshInstance));
    DeepCopy(copy->_materials, at);
    DeepCopy(copy->_animation._layers, at);
    DeepCopy(copy->_nodes, at);
    header->frozen = e.state >= 0 ? b.states[static_cast<size_t>(e.state)].copy : nullptr;
    header->copy   = copy;
    e.copy = copy;
}

void CopyParticle(Entry& e, unsigned char* base) {
    unsigned char* at = base + e.offset;
    std::memcpy(at, e.live, e.size);
    auto* const copy = Place<Graphics::ParticleInstance>(at);
    at += Round(e.size);
    if (copy->_boundsPtr != nullptr) {
        std::memcpy(at, copy->_boundsPtr, sizeof(RtGraphics::ParticleBoundsRecord));
        auto* const record = Place<RtGraphics::ParticleBoundsRecord>(at);
        at += Round(sizeof(RtGraphics::ParticleBoundsRecord));
        DeepCopy(record->_records, at);
        copy->_boundsPtr = record;
    }
    e.copy = copy;
}

void CopyWorld(Entry& e, const Builder& b, unsigned char* base) {
    unsigned char* at = base + e.offset;
    std::memcpy(at, e.live, sizeof(WorldGeometry));
    auto* const copy = Place<WorldGeometry>(at);
    at += Round(sizeof(WorldGeometry));
    for (int pass = 0; pass < 2; ++pass) {
        GeometryBatchArray& array = pass == 0 ? copy->_decals : copy->_roadBatches;
        const std::vector<uint32_t>& marks = pass == 0 ? b.decalMark : b.roadMark;
        const std::vector<size_t>& reserved = pass == 0 ? b.decalAt : b.roadAt;
        if (array._data == nullptr) continue;
        const int32_t n = array._size > 0 ? array._size : 0;
        if (n > 0) std::memcpy(at, array._data, static_cast<size_t>(n) * sizeof(GeometryBatch));
        GeometryBatch* const batches = Place<GeometryBatch>(at);
        array._data     = batches;
        array._capacity = n;
        at += ArrayBytes<GeometryBatch>(n);
        for (int32_t i = 0; i < n; ++i) {
            GeometryBatch& batch = batches[i];
            const bool drawn = static_cast<size_t>(i) < marks.size() && marks[static_cast<size_t>(i)] == b.markGen;
            if (drawn) {
                // At the bytes MarkBatch reserved: the ones after this entry's arrays may
                // belong to an entry admitted after it.
                unsigned char* data = base + reserved[static_cast<size_t>(i)];
                DeepCopy(batch.vertices, data);
                DeepCopy(batch.indices, data);
            } else {
                // Never read for this packet: emptied rather than left pointing at live data.
                batch.vertices._data     = nullptr;
                batch.vertices._size     = 0;
                batch.vertices._capacity = 0;
                batch.indices._data      = nullptr;
                batch.indices._size      = 0;
                batch.indices._capacity  = 0;
            }
        }
    }
    e.copy = copy;
}

void CopyEntry(Entry& e, const Builder& b, unsigned char* base) {
    switch (e.kind) {
    case Kind::Mesh:
        CopyMesh(e, b, base);
        break;
    case Kind::Particle:
        CopyParticle(e, base);
        break;
    case Kind::Weather: {
        unsigned char* at = base + e.offset;
        std::memcpy(at, e.live, sizeof(WeatherParticleGeometry));
        auto* const copy = Place<WeatherParticleGeometry>(at);
        at += Round(sizeof(WeatherParticleGeometry));
        DeepCopyRaw(copy->_particles, at);
        DeepCopy(copy->_indices, at);
        e.copy = copy;
        break;
    }
    case Kind::WorldBatches:
        CopyWorld(e, b, base);
        break;
    case Kind::Edge:
    case Kind::StatusIcon: {
        unsigned char* const at = base + e.offset;
        std::memcpy(at, e.live, e.size);
        e.copy = Place<Geometry>(at);
        break;
    }
    }
}

}  // namespace

SnapshotResult BuildSnapshot(int32_t slot, Graphics::Renderer& r, bool terrainEdit) {
    SnapshotResult result;
    if (terrainEdit) {
        result.refused = "ext.terrain_edit";
        return result;
    }
    Builder& b = g_builder;
    b.entries.clear();
    b.states.clear();
    b.world = nullptr;
    ++b.markGen;
    if (b.markGen == 0) {
        std::fill(b.decalMark.begin(), b.decalMark.end(), 0u);
        std::fill(b.roadMark.begin(), b.roadMark.end(), 0u);
        b.markGen = 1;
    }

    // ---- pass A: admit every drawn geometry, count the bytes ------------------
    size_t itemCount = 0;
    for (int32_t gi = 0; gi < r._drawGroupsUsed && gi < r._drawGroups._size; ++gi) {
        for (const auto& list : r._drawGroups._data[gi]._items) itemCount += static_cast<size_t>(list._size);
    }
    b.geometries.Reset(itemCount + 16);
    b.stateTable.Reset(64);

    for (int32_t i = 0; i < r._drawables._size; ++i) {
        const Graphics::Light* const light = r._drawables._data[i]._object;
        if (light == nullptr || typeid(*light) != typeid(Graphics::SunLight)) {
            result.refused = light != nullptr ? typeid(*light).name() : "a null drawable";
            return result;
        }
    }

    size_t bytes = 0;
    for (int32_t gi = 0; gi < r._drawGroupsUsed && gi < r._drawGroups._size; ++gi) {
        for (const auto& list : r._drawGroups._data[gi]._items) {
            for (int32_t k = 0; k < list._size; ++k) {
                const Graphics::RendererDrawItem& item = list._data[k];
                const Geometry* const g = item._geometry;
                if (g == nullptr) {
                    result.refused = "a null geometry";
                    return result;
                }
                if (typeid(*g) == typeid(WorldGeometry)) {
                    const uint32_t key = item._arg5;
                    const uint32_t mode = key & kModeMask;
                    if (mode != kModeDecal && mode != kModeRoad) continue;   // the live object (2.2)
                    const char* why = nullptr;
                    if (Admit(b, g, bytes, &why) < 0) {
                        result.refused = why;
                        return result;
                    }
                    if (!MarkBatch(b, mode, key, bytes)) {
                        result.refused = "a decal / road key out of range";
                        return result;
                    }
                    continue;
                }
                const char* why = nullptr;
                if (Admit(b, g, bytes, &why) < 0) {
                    result.refused = why;
                    return result;
                }
            }
        }
    }

    // ---- pass B: the arena, then the copies ---------------------------------
    Arena& arena = g_arenas[slot & 1];
    arena.Clear();   // nothing of the slot is a copy until its items point at the new ones
    if (arena.capacity < bytes) {
        const size_t grown = bytes + bytes / 2 + 4096;
        arena.storage.reset(new unsigned char[grown]);
        arena.capacity = grown;
    }
    unsigned char* const base = arena.Base();
    for (StateEntry& s : b.states) CopyState(s, base);
    for (Entry& e : b.entries) CopyEntry(e, b, base);
    // An edge mesh follows its mesh's copy.
    for (Entry& e : b.entries) {
        if (e.kind != Kind::Edge) continue;
        auto* const edge = static_cast<Graphics::EdgeMeshInstance*>(e.copy);
        if (edge->_followed == nullptr) continue;
        const int32_t followed = b.geometries.Find(edge->_followed, -1);
        edge->_followed = static_cast<MeshInstance*>(b.entries[static_cast<size_t>(followed)].copy);
    }

    // ---- pass C: every item at its copy -------------------------------------
    for (int32_t gi = 0; gi < r._drawGroupsUsed && gi < r._drawGroups._size; ++gi) {
        for (auto& list : r._drawGroups._data[gi]._items) {
            for (int32_t k = 0; k < list._size; ++k) {
                Graphics::RendererDrawItem& item = list._data[k];
                const int32_t index = b.geometries.Find(item._geometry, -1);
                if (index >= 0) item._geometry = b.entries[static_cast<size_t>(index)].copy;
            }
        }
    }

    arena.begin.store(base, std::memory_order_relaxed);
    arena.end.store(base + bytes, std::memory_order_relaxed);

    result.ok         = true;
    result.geometries = static_cast<uint32_t>(b.entries.size());
    result.bytes      = bytes;
    return result;
}

// The seam is installed only while a packet with copies is in flight (render_pipe.cpp
// BeforePublish), so this runs on the main thread's pose lookups during those steps too:
// no thread_local (an emutls call per lookup under MinGW GCC: +10 % update at speed 50,
// P13_OVERLAP section 10), just the address. A live object never lies in an arena. The
// render thread asks only for its packet's copies, in a slot the main thread does not
// rewrite while that packet is pending (pipe_state.h); a racing read of the OTHER slot's
// bounds can at most send a copy to its own header, which answers the same. The main
// thread is the bounds' only writer, so its own reads are exact. The publish / join
// mutexes order the copies' bytes before the render thread reads them.
const InstancedAnimationState* FrozenInstancedState(const MeshInstance& mi) {
    if (!g_arenas[0].Holds(&mi) && !g_arenas[1].Holds(&mi)) return nullptr;
    // A MeshInstance copy is preceded by its header (CopyMesh).
    const unsigned char* const object = static_cast<const unsigned char*>(static_cast<const void*>(&mi));   // CAST-OK: alloc_header
    const MeshHeader* const header = static_cast<const MeshHeader*>(static_cast<const void*>(object - Round(sizeof(MeshHeader))));   // CAST-OK: alloc_header
    return header->copy == &mi ? header->frozen : nullptr;
}

void FreeSnapshots() {
    for (Arena& a : g_arenas) {
        a.Clear();
        a.storage.reset();
        a.capacity = 0;
    }
}

}  // namespace RenderPipe
}  // namespace Ext
