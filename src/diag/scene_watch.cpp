// src/diag/scene_watch.cpp — see scene_watch.h for what this measures and why.
//
// Scene layout used here (Runtime Graphics::Scene, read-only):
//   +0x10  Node*  nodes        (0x18 bytes per node)
//   +0x18  int    count
//   node+0x00  Geometry*  obj
//   node+0x10  int        slot state; < 0 means the slot is free
// Confirmed against Graphics::Scene::Clean 0x1801023f0 and AddObject 0x1801026a0.

#include "scene_watch.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <unordered_map>
#include <unordered_set>
#include <map>
#include <set>
#include <string>
#include "options.h"
#include "append_format.h"   // Diag::AppendFormat: the census lines' bounded builder
#include "graphics/Geometry.h"   // Graphics::Geometry::AddDrawItems (B3X W0)
#include "graphics/Mesh.h"       // RtGraphics_MeshInstance_AddDrawItems (B3R W2-RES)
#include "Component/ModelComponent.h"   // the owner's meshGroup / meshIndex
#include "graphics/GraphicsMesh.h" // B4: GraphicsMesh::_detailLevels, DetailLevel
#include "object/VSlot.h"        // Object::SlotAddressOf
#include "graphics/Scene.h"      // B4: Scene::_nodes, SceneNode
#include "graphics/Camera.h"     // B4: Renderer, RendererDrawGroup
#include "graphics/Material.h"   // B4: MaterialInstance::_material, Material::_passes
#include "video/VideoProgram.h"  // B4: the pass's programs
#include "video/ShaderProgram.h" // B4: ShaderProgram::_byteCode
#include "platform_os.h"         // Platform::QueryMemoryRegion (was VirtualQuery; D.4 LINUX-1)

namespace {

bool Enabled() {
    static int on = -1;
    if (on < 0) {
        on = Opt().sceneWatch ? 1 : 0;
    }
    return on == 1;
}

struct Note {
    void*       geometry;
    const char* who;
    int32_t        frame;
};

std::unordered_map<uint32_t, Note>& Notes() {
    static std::unordered_map<uint32_t, Note> m;
    return m;
}

// A real vtable lives in a mapped image (our exe's .rdata or a DLL's). A vtable
// pointer in MEM_PRIVATE memory means the object was freed and the block reused.
bool VtableLooksLikeCode(void* vt) {
    static std::unordered_set<uintptr_t> good, bad;
    const uintptr_t v = reinterpret_cast<uintptr_t>(vt);  // CAST-OK: ptr_int
    if (v < 0x10000 || (v & 7) != 0) return false;
    if (good.count(v)) return true;
    if (bad.count(v))  return false;

    Platform::MemoryRegion region;
    if (!Platform::QueryMemoryRegion(vt, region)) { bad.insert(v); return false; }
    const bool ok = region.committed && region.image;
    (ok ? good : bad).insert(v);
    return ok;
}

bool Readable(void* p) {
    const uintptr_t a = reinterpret_cast<uintptr_t>(p);  // CAST-OK: ptr_int
    return a >= 0x10000 && a < 0x00007FFFFFFFFFFFULL && (a & 7) == 0;
}

} // namespace

// BANISHED_SCENE_ADDTALLY=1 — running count of Scene::AddObject calls per call
// site, plus how many DISTINCT geometries each site has ever added.
//
// Added 2026-09-04 chasing an unbounded Graphics::Scene growth (nodes climbing a
// steady 75/frame — 63,357 scene objects in a 5,355-entity tutorial world by frame
// 780), which showed up as StatusIconGeom_Draw eating 67% of frame time and the
// frame rate decaying from 60 to 8 fps over a couple of minutes.
//
// calls >> distinct at one site means that site re-adds the SAME geometry every
// frame without removing the previous registration — i.e. its "already in the
// scene" guard is not holding. That is the signature to look for.
void SceneWatch_AddTally(const char* who, void* geometry) {
    static int on = -1;
    if (on < 0) {
        on = Opt().sceneAddTally ? 1 : 0;
    }
    if (!on) return;

    struct Site { long long calls; std::set<void*> distinct; };
    static std::map<std::string, Site>* sites = new std::map<std::string, Site>();
    static long long total = 0;

    Site& st = (*sites)[who ? who : "?"];
    ++st.calls;
    if (st.distinct.size() < 100000) st.distinct.insert(geometry);

    if (++total % 2000 == 0) {
        std::fprintf(stderr, "[SceneAdd] %lld AddObject calls so far:\n", total);
        for (auto& kv : *sites) {
            std::fprintf(stderr, "[SceneAdd]   %-46s calls=%-10lld distinct-geoms=%zu%s\n",
                         kv.first.c_str(), kv.second.calls, kv.second.distinct.size(),
                         (kv.second.calls > (long long)kv.second.distinct.size() * 2)
                             ? "   <-- RE-ADDING the same geometry" : "");
        }
        std::fflush(stderr);
    }
}

void SceneWatch_Note(uint32_t sceneId, void* geometry, const char* who) {
    SceneWatch_AddTally(who, geometry);
    if (!Enabled()) return;
    Notes()[sceneId] = Note{geometry, who, 0};
}

void SceneWatch_Check(Graphics::Scene* scene) {
    if (!Enabled() || !scene) return;

    static int32_t   s_frame  = 0;
    static int    s_report = 0;
    static bool   s_seen   = false;
    ++s_frame;

    Graphics::SceneNode* nodes = scene->_grid._nodes.GetData();   // +0x10
    const int count = scene->_grid._nodes.GetSize();              // +0x18
    if (!nodes || count <= 0 || count > (1 << 22)) return;

    int bad = 0, firstBad = -1;
    void* firstObj = nullptr;
    void* firstVt  = nullptr;

    for (int i = 0; i < count; ++i) {
        const Graphics::SceneNode& node = nodes[i];
        if (node._cell < 0) continue;   // free slot (+0x10)
        void* obj = node._object;       // +0x00
        if (!Readable(obj)) {
            ++bad;
            if (firstBad < 0) { firstBad = i; firstObj = obj; firstVt = nullptr; }
            continue;
        }
        // The object's vtable word, read raw to tell a live object from a freed block.
        void* vt = *reinterpret_cast<void**>(obj);  // CAST-OK: crash_walk
        if (VtableLooksLikeCode(vt)) continue;
        ++bad;
        if (firstBad < 0) { firstBad = i; firstObj = obj; firstVt = vt; }
    }

    if (bad == 0) return;
    if (s_report >= 12) return;
    ++s_report;

    const bool firstEver = !s_seen;
    s_seen = true;

    std::fprintf(stderr,
        "[SceneWatch]%s frame=%d nodes=%d BAD=%d firstBad=index %d obj=%p vtable=%p\n",
        firstEver ? " FIRST" : "", s_frame, count, bad, firstBad, firstObj, firstVt);

    auto it = Notes().find(static_cast<uint32_t>(firstBad));
    if (it != Notes().end())
        std::fprintf(stderr, "[SceneWatch]   id %d was last registered by %s "
                             "(geometry=%p)%s\n",
                     firstBad, it->second.who, it->second.geometry,
                     it->second.geometry == firstObj ? "  <== SAME OBJECT" : "  (different object — id was reused)");
    else
        std::fprintf(stderr, "[SceneWatch]   id %d has no record from our AddObject "
                             "sites — the engine or an unported path registered it\n",
                     firstBad);

    // The freed block often still carries recognisable remnants (the old vtable in
    // an adjacent field, the owner IBP, our factory flag word).
    if (Readable(firstObj)) {
        std::fprintf(stderr, "[SceneWatch]   obj dump:");
        for (int q = 0; q < 8; ++q)
            std::fprintf(stderr, " %016llx",
                         (unsigned long long)*reinterpret_cast<uint64_t*>(reinterpret_cast<uintptr_t>(firstObj) + q * 8));  // CAST-OK: crash_walk
        std::fprintf(stderr, "\n");
        // Geometry is embedded at owner+0x28 on every drawable component in this
        // port, so dump the presumed owner header too (vtable, flags, owner IBP).
        uintptr_t owner = reinterpret_cast<uintptr_t>(firstObj) - 0x28;  // CAST-OK: crash_walk
        if (Readable(reinterpret_cast<void*>(owner))) {  // CAST-OK: crash_walk
            std::fprintf(stderr, "[SceneWatch]   owner(obj-0x28) dump:");
            for (int q = 0; q < 6; ++q)
                std::fprintf(stderr, " %016llx",
                             (unsigned long long)*reinterpret_cast<uint64_t*>(owner + q * 8));  // CAST-OK: crash_walk
            std::fprintf(stderr, "\n");
        }
    }
    std::fflush(stderr);
}

// ---------------------------------------------------------------------------
// SceneWatch_Census — see scene_watch.h for what this measures and why.
// ---------------------------------------------------------------------------
namespace {

bool CensusEnabled() {
    static int on = -1;
    if (on < 0) {
        on = Opt().sceneCensus ? 1 : 0;
    }
    return on == 1;
}

struct GeoPos { float x, y, z; int cell; int32_t frame; };

// Distance a walker can cover in one frame is small; anything past this is a
// teleport, which is what "the model popped somewhere else" actually is.
constexpr float kJumpDist = 2.0f;

} // namespace

void SceneWatch_Census(Graphics::Scene* scene) {
    if (!CensusEnabled() || !scene) return;

    static int32_t s_frame = 0;
    ++s_frame;

    Graphics::SceneNode* nodes = scene->_grid._nodes.GetData();   // +0x10
    const int count = scene->_grid._nodes.GetSize();              // +0x18
    if (!nodes || count <= 0 || count > (1 << 22)) return;

    static std::unordered_map<void*, GeoPos> s_last;

    int live = 0, moved = 0, jumped = 0, added = 0, staleprev = 0;
    int degenerate = 0, zerobox = 0, posOutside = 0, noRefresh = 0;
    int dirty = 0, layer0 = 0, rebucketed = 0;
    long long detailSum = 0;
    void* jumpExample = nullptr;
    float jx = 0, jy = 0, jz = 0, px = 0, py = 0, pz = 0;
    void* staleExample = nullptr;
    float sx = 0, sy = 0, sz = 0;
    float sbx0 = 0, sby0 = 0, sbz0 = 0, sbx1 = 0, sby1 = 0, sbz1 = 0;

    for (int i = 0; i < count; ++i) {
        const Graphics::SceneNode& node = nodes[i];
        if (node._cell < 0) continue;   // free slot (+0x10)
        Graphics::Geometry* geo = node._object;   // +0x00
        void* obj = geo;
        if (!Readable(obj)) continue;
        const int cell = node._cell;
        ++live;

        // Graphics::Scene::Clean (0x1801023f0) is the per-frame refresh: for every live
        // node whose DIRTY word at geo+0x2c is non-zero it dispatches the geometry's
        // vtable slot 8 (+0x40, "recompute my bounds; tell me if they moved") and, when
        // that returns true, re-buckets the node through FUN_18005ee30. node+0x10 IS
        // that bucket — a Morton cell index — so counting how often it changes measures
        // whether the refresh actually runs. A world where things move but nothing is
        // ever re-bucketed culls them against the cell they were FIRST inserted into,
        // which on screen is a model that pops in and out as the view changes.
        if (geo->_dirtyFlags != 0) ++dirty;               // +0x2c
        if (geo->_materialTypeFlags == 0) ++layer0;       // +0x30

        const float x = geo->_transform._current.translation.x;    // +0x94
        const float y = geo->_transform._current.translation.y;    // +0x98
        const float z = geo->_transform._current.translation.z;    // +0x9c
        // Previous transform the renderer interpolates FROM (+0xB0..+0xB8).
        const float ox = geo->_transform._previous.translation.x;
        const float oy = geo->_transform._previous.translation.y;
        const float oz = geo->_transform._previous.translation.z;
        const float pdx = x - ox, pdy = y - oy, pdz = z - oz;
        if (pdx * pdx + pdy * pdy + pdz * pdz > kJumpDist * kJumpDist) ++staleprev;

        // --- CULL INPUTS ------------------------------------------------------
        // Graphics::Scene::AddCulledDrawItems (Runtime 0x1801017a0) decides whether a
        // node is drawn from exactly three things on the Geometry:
        //   +0x30 layer mask   & the render pass mask
        //   +0x28 detail level <= renderer+0x48
        //   +0x4c..+0x60 world AABB (min xyz / max xyz) vs Volume::Frustum::Contains
        // A geometry whose AABB no longer contains its own origin is carrying a STALE
        // box: the frustum test then answers about where the model USED to be, so it
        // vanishes and reappears as the camera moves. That is the shape of the "models
        // keep popping" report, and it is invisible to the membership and position
        // counts above because both of those stay perfectly stable.
        //
        // +0x20 is the counter Graphics::Geometry::SetTransform (0x1800155a0) tests
        // before dispatching vtable slot 4 — the box refresh. Zero there means the box
        // is never recomputed when the transform changes, which is one way to get a
        // stale box, so count it separately from the symptom.
        const float bx0 = geo->_worldBounds.min.x;        // +0x4c
        const float by0 = geo->_worldBounds.min.y;        // +0x50
        const float bz0 = geo->_worldBounds.min.z;        // +0x54
        const float bx1 = geo->_worldBounds.max.x;        // +0x58
        const float by1 = geo->_worldBounds.max.y;        // +0x5c
        const float bz1 = geo->_worldBounds.max.z;        // +0x60
        if (bx1 < bx0 || by1 < by0 || bz1 < bz0) {
            ++degenerate;
        } else if (bx0 == 0.0f && by0 == 0.0f && bz0 == 0.0f &&
                   bx1 == 0.0f && by1 == 0.0f && bz1 == 0.0f) {
            ++zerobox;
        } else {
            // Slack, for meshes whose origin sits on an edge of their own bounds.
            const float k = 0.5f;
            if (x < bx0 - k || x > bx1 + k || y < by0 - k || y > by1 + k ||
                z < bz0 - k || z > bz1 + k) {
                ++posOutside;
                if (!staleExample) {
                    staleExample = obj;
                    sx = x; sy = y; sz = z;
                    sbx0 = bx0; sby0 = by0; sbz0 = bz0;
                    sbx1 = bx1; sby1 = by1; sbz1 = bz1;
                }
            }
        }
        detailSum += geo->_detailId;                      // +0x28
        // +0x20 is the size word of the header's `_attached` Array (+0x18); the
        // harness's "refresh counter" reading is kept in its log text.
        if (geo->_attached.GetSize() == 0) ++noRefresh;   // +0x20

        auto it = s_last.find(obj);
        if (it == s_last.end()) {
            ++added;
            s_last.emplace(obj, GeoPos{x, y, z, cell, s_frame});
            continue;
        }
        GeoPos& p = it->second;
        if (p.cell != cell) { ++rebucketed; p.cell = cell; }
        if (p.x != x || p.y != y || p.z != z) {
            ++moved;
            const float dx = x - p.x, dy = y - p.y, dz = z - p.z;
            if (dx * dx + dy * dy + dz * dz > kJumpDist * kJumpDist) {
                ++jumped;
                if (!jumpExample) {
                    jumpExample = obj;
                    jx = x; jy = y; jz = z; px = p.x; py = p.y; pz = p.z;
                }
            }
            p.x = x; p.y = y; p.z = z;
        }
        p.frame = s_frame;
    }

    // Anything we tracked that no longer appears in a live node has LEFT the scene.
    int removed = 0;
    for (auto it = s_last.begin(); it != s_last.end(); ) {
        if (it->second.frame != s_frame) { it = s_last.erase(it); ++removed; }
        else ++it;
    }

    // Report every second, and immediately on any teleport — a jump is the whole
    // point of the probe and rate-limiting it away would hide the event.
    static int s_jumpReports = 0;
    const bool due = (s_frame % 60) == 0;
    if (jumped > 0 && s_jumpReports < 40) {
        ++s_jumpReports;
        std::fprintf(stderr,
            "[SceneCensus] frame=%d JUMPED=%d e.g. geo=%p (%.2f,%.2f,%.2f) -> "
            "(%.2f,%.2f,%.2f)\n",
            s_frame, jumped, jumpExample, px, py, pz, jx, jy, jz);
        std::fflush(stderr);
    }
    if (due) {
        std::fprintf(stderr,
            "[SceneCensus] frame=%d nodes=%d live=%d added=%d removed=%d moved=%d "
            "jumped=%d staleprev=%d | bbox degenerate=%d zero=%d posOutside=%d "
            "noRefresh(+0x20==0)=%d meanDetail=%.2f | dirty=%d layer0=%d rebucketed=%d\n",
            s_frame, count, live, added, removed, moved, jumped, staleprev,
            degenerate, zerobox, posOutside, noRefresh,
            live ? (double)detailSum / (double)live : 0.0,
            dirty, layer0, rebucketed);
        if (staleExample)
            std::fprintf(stderr,
                "[SceneCensus]   stale-box e.g. geo=%p pos=(%.2f,%.2f,%.2f) "
                "box=(%.2f,%.2f,%.2f)..(%.2f,%.2f,%.2f)\n",
                staleExample, sx, sy, sz, sbx0, sby0, sbz0, sbx1, sby1, sbz1);
        std::fflush(stderr);
    }
}

// ---------------------------------------------------------------------------
// BANISHED_LOD_CENSUS — see scene_watch.h for the mechanism this measures.
// ---------------------------------------------------------------------------

namespace {

bool ForceLod0() {
    static int on = -1;
    if (on < 0) {
        on = Opt().forceLod0 ? 1 : 0;
    }
    return on == 1;
}

bool LodEnabled() {
    static int on = -1;
    if (on < 0) {
        on = Opt().lodCensus ? 1 : 0;
    }
    // BANISHED_FORCE_LOD0 needs the same per-frame walk to do its clamping, so it
    // implies the census; the numbers it prints are wanted for that A/B anyway.
    return on == 1 || ForceLod0();
}

// Only Graphics::MeshInstance runs the distance pick. Identify one by its
// AddDrawItems slot (vtable index 3, +0x18) rather than by the class vtable, so
// the test still holds for any subclass that inherits the retail implementation.
//
// The token is the body the port registry held at 0x180100f20
// (?AddDrawItems@MeshInstance@Graphics@@..., ordinal 1550), which this function
// used to look up there (B3R W2-RES: the registry is gone, the token is the same
// body by name). NOTE for the close: that body is the extern "C" wrapper in
// src/engine/graphics/mesh.cpp, not the member Graphics::MeshInstance::
// AddDrawItems that a compiler-built MeshInstance vtable holds, and no code in
// the tree stores the wrapper in a vtable: the compare can match no object
// today, exactly as before this change.
bool IsMeshInstance(void* obj) {
    const uintptr_t addDrawItemsOurs =
        reinterpret_cast<uintptr_t>(&RtGraphics_MeshInstance_AddDrawItems);  // CAST-OK: ptr_int
    // The object's vtable word, read raw to tell a live object from a freed block.
    void** vt = *reinterpret_cast<void***>(obj);  // CAST-OK: crash_walk
    if (!VtableLooksLikeCode(vt)) return false;
    // The table entry for Graphics::Geometry::AddDrawItems (retail slot 3,
    // +0x18), taken from the member rather than a literal index (object/VSlot.h).
    const uintptr_t addDrawItems = reinterpret_cast<uintptr_t>(  // CAST-OK: ptr_int
        ::Object::SlotAddressOf(obj, &::Graphics::Geometry::AddDrawItems));
    return addDrawItems == addDrawItemsOurs;
}

struct MeshInfo {
    int   levels;
    int   instances;
    float dist[8];
    int   tris[8];    // level+0xa0: TriangleList count. ZERO means the level draws NOTHING.
    void* vb[8];      // level+0x20: vertex buffer object
    void* ib[8];      // level+0x40: index buffer object
    bool  dumped;
};

} // namespace

void SceneWatch_LodCensus(Graphics::Scene* scene, const Graphics::Camera* camera, int variantIdx) {
    if (!LodEnabled() || !scene || !camera) return;

    static int32_t s_frame = 0;
    ++s_frame;

    Graphics::SceneNode* nodes = scene->_grid._nodes.GetData();   // +0x10
    const int count = scene->_grid._nodes.GetSize();              // +0x18
    if (!nodes || count <= 0 || count > (1 << 22)) return;

    // The camera is PlayerController::camera; the eye is Camera::_cameraToWorld's
    // translation (+0x64 + 0x24).
    const float camX = camera->_cameraToWorld.translation.x;
    const float camY = camera->_cameraToWorld.translation.y;
    const float camZ = camera->_cameraToWorld.translation.z;

    static std::unordered_map<void*, float> s_scale;   // geometry -> last visualScale.x
    struct MeshVariants { int n; int16_t vi[12]; int16_t mv[12]; int count[12]; };
    static std::unordered_map<void*, MeshVariants> s_variants;
    struct MeshRef { void* mesh; void* mats; };
    static std::unordered_map<void*, MeshRef> s_mesh;   // geometry -> mesh/material it drew with
    static std::unordered_map<void*, int>      s_lod;    // geometry -> last chosen level
    static std::unordered_map<void*, MeshInfo> s_meshes; // GraphicsMesh -> its table

    int instances = 0, multiLevel = 0, flips = 0, fellBack = 0, prevBoxEmpty = 0;
    int variantHist[9] = {0};
    int variantMiss = 0, variantNotReady = 0;
    int meshSwaps = 0, matSwaps = 0;
    int faded = 0, fadedOut = 0, tinted = 0, oddDetailId = 0;
    int variantOutOfBand = 0;
    int scaleMoved = 0, scaleJumped = 0;
    void* scaleExample = nullptr; float scaleFrom = 0.0f, scaleTo = 0.0f;
    void* swapExample = nullptr; void* swapFrom = nullptr; void* swapTo = nullptr;
    int lodHist[8] = {0};
    void* flipExample = nullptr;
    int   flipFrom = 0, flipTo = 0;
    float flipDist = 0.0f;

    for (auto& kv : s_meshes) kv.second.instances = 0;
    for (auto& kv : s_variants) kv.second.n = 0;

    for (int i = 0; i < count; ++i) {
        const Graphics::SceneNode& node = nodes[i];
        if (node._cell < 0) continue;   // free slot (+0x10)
        Graphics::Geometry* geo = node._object;   // +0x00
        void* obj = geo;
        if (!Readable(obj) || !IsMeshInstance(obj)) continue;
        // Its AddDrawItems slot is MeshInstance's: the downcast stays in Geometry's hierarchy.
        Graphics::MeshInstance* mi = static_cast<Graphics::MeshInstance*>(geo);

        void* mesh = mi->_mesh.Get();   // +0xc0 ExternalPtr<GraphicsMesh>
        if (!Readable(mesh)) continue;
        ++instances;

        // With the detail pick frozen (BANISHED_FORCE_LOD0), the only remaining way a
        // model's APPEARANCE can change is for something to swap the resource under it:
        // Graphics::MeshInstance::SetMesh rewrites MeshInstance+0xc0 (and the material
        // array at +0xc8). A tree that visibly goes from leaves to no leaves is exactly
        // that swap, so count it and name a culprit. ModelComponent picks its mesh from
        // the description with (variantIdx +0x164, modelVariant +0x166), so a mesh that
        // changes mid-run means one of those two moved after the load.
        {
            // Per-object FADE. Graphics::MeshInstance::AddDrawItems hands AddDrawItem
            // `alpha_prev * geo+0x80 + alpha_cur * geo+0x7c` (both 1.0 from the Geometry
            // ctor) as its param_6, and AddDrawItem sets bit 0x8000 on the sort key
            // whenever that value is not exactly 1.0 — i.e. it moves the item into the
            // transparent bucket. So this pair is a real per-object dissolve, and it is
            // the last thing left that can change a model's appearance while its mesh,
            // material and detail level all stay fixed.
            const float a0 = geo->_opacity;       // +0x7c
            const float a1 = geo->_opacityPrev;   // +0x80
            if (a0 != 1.0f || a1 != 1.0f) ++faded;
            if (a0 < 0.999f) ++fadedOut;
            // +0x10c..+0x118 is the MeshInstance's own RGBA tint (1,1,1,1 from its ctor).
            // +0x10c..+0x118 is the growth VISUAL SCALE (GrowthComponent writes it as
            // ModelComponent+0x134, and the MeshInstance sits at ModelComponent+0x28, so
            // the two are the same four floats). The Geometry ctor seeds it to
            // (1,1,1,1), i.e. UNWRITTEN READS AS FULLY GROWN — so a plant that has not
            // been ticked yet draws mature and then SNAPS to its real size the moment
            // the growth pass first reaches it. That snap is a tree changing size in one
            // frame, which is what "popping" looks like. Measure the step, not just the
            // count: an ease is content, a jump is the defect.
            const float* vs = mi->_color;         // +0x10c
            if (vs[0] != 1.0f || vs[1] != 1.0f || vs[2] != 1.0f || vs[3] != 1.0f)
                ++tinted;
            {
                auto sit = s_scale.find(obj);
                if (sit == s_scale.end()) {
                    s_scale.emplace(obj, vs[0]);
                } else if (sit->second != vs[0]) {
                    const float d = vs[0] > sit->second ? vs[0] - sit->second
                                                        : sit->second - vs[0];
                    ++scaleMoved;
                    if (d > 0.05f) {
                        ++scaleJumped;
                        if (!scaleExample) {
                            scaleExample = obj; scaleFrom = sit->second; scaleTo = vs[0];
                        }
                    }
                    sit->second = vs[0];
                }
            }
            if (geo->_detailId != 2) ++oddDetailId;   // +0x28

            void* matArr = mi->_materials.GetData();  // +0xc8
            auto it2 = s_mesh.find(obj);
            if (it2 == s_mesh.end()) {
                s_mesh.emplace(obj, MeshRef{mesh, matArr});
            } else {
                if (it2->second.mesh != mesh) {
                    ++meshSwaps;
                    if (!swapExample) {
                        swapExample = obj; swapFrom = it2->second.mesh; swapTo = mesh;
                    }
                    it2->second.mesh = mesh;
                }
                if (it2->second.mats != matArr) { ++matSwaps; it2->second.mats = matArr; }
            }
        }

        // The GraphicsMesh's detail-level table (+0x50 data / +0x58 count, stride 0xb0),
        // through MeshInstance::GetMesh (retail 0x180016a20).
        Graphics::GraphicsMesh* gmesh = mi->GetMesh().Get();
        // BANISHED_FORCE_LOD0=1 — pin every mesh to its highest detail level.
        // Graphics::MeshInstance::AddDrawItems only runs the distance pick when
        // GraphicsMesh+0x58 (the level count) exceeds 1, so clamping it to 1 makes
        // level 0 draw at every distance. The levels array itself is untouched.
        // This is the A/B for "is the detail swap what I am seeing pop?": with it on,
        // the swap cannot happen, so anything still popping is NOT the LOD.
        if (ForceLod0() && gmesh->_detailLevels._size > 1)
            gmesh->_detailLevels._size = 1;
        const int n = gmesh->_detailLevels._size;
        Graphics::GraphicsMesh::DetailLevel* levels = gmesh->_detailLevels._data;

        // Which (variantIdx, modelVariant) pair produced this mesh. ModelComponent embeds
        // its MeshInstance at +0x28 (FUN_1401253d0 addresses it as param_1+5), and keeps
        // variantIdx at +0x164 / modelVariant at +0x166 — so from the MeshInstance they
        // are at +0x13c and +0x13e. ModelComponent::SetVariant derives the mesh from the
        // description with exactly that pair, so a mesh reached by more than one pair, or
        // by a pair outside the description's range, is a mis-pick.
        {
            // The OWNING ModelComponent's meshGroup / meshIndex (+0x164 / +0x166), reached
            // from its embedded MeshInstance (+0x28) by the member's offset: a MeshInstance
            // has no pointer back to its owner (the step is the diagnostic's own; the range
            // test below rejects an instance that is not a ModelComponent's).
            const ModelComponent* owner = reinterpret_cast<const ModelComponent*>(   // LEFT: diagnostic container_of, a FLOOR ruling candidate (B4 Z LAST CODE): a MeshInstance has no pointer back to its owner, so the embedded-member-to-ModelComponent step has no typed spelling; the range test below rejects a non-ModelComponent instance  // CAST-OK: crash_walk (S4 ruling)
                reinterpret_cast<const char*>(mi) - offsetof(ModelComponent, meshInstance));   // LEFT: the same container_of step (FLOOR ruling candidate, line above)  // CAST-OK: crash_walk (S4 ruling)
            const int16_t vi = owner->meshGroup;
            const int16_t mv = owner->meshIndex;
            if (vi >= -1 && vi < 64 && mv >= -1 && mv < 64) {
                MeshVariants& mvs = s_variants[mesh];
                bool seen = false;
                for (int k = 0; k < mvs.n; ++k)
                    if (mvs.vi[k] == vi && mvs.mv[k] == mv) { ++mvs.count[k]; seen = true; break; }
                if (!seen && mvs.n < 12) {
                    mvs.vi[mvs.n] = vi; mvs.mv[mvs.n] = mv; mvs.count[mvs.n] = 1; ++mvs.n;
                }
            } else {
                ++variantOutOfBand;
            }
        }

        MeshInfo& info = s_meshes[mesh];
        if (info.levels == 0) {
            info.levels = n;
            info.dumped = false;
            for (int k = 0; k < 8; ++k) {
                info.dist[k] = 0.0f; info.tris[k] = -1; info.vb[k] = nullptr; info.ib[k] = nullptr;
                if (k >= n || !Readable(levels)) continue;
                const Graphics::GraphicsMesh::DetailLevel& lv = levels[k];
                info.dist[k] = lv._distance;                          // +0x10
                // Graphics::MeshInstance::AddDrawItems emits one draw item per entry of
                // level+0xa0 — a level whose count is zero emits NOTHING, so the model just
                // stops being rendered past that level's distance. On screen that is exactly
                // "the model popped out", and it is invisible to every cull, transform and
                // membership measurement, all of which stay perfectly healthy.
                info.tris[k] = lv._triangleLists._size;               // +0xa0
                info.vb[k]   = lv._vertexBuffer.platformData;          // +0x20
                info.ib[k]   = lv._indexBuffer.platformData;           // +0x40
            }
        }
        ++info.instances;

        // Graphics::Renderer::AddDrawItem (Runtime 0x1801057b0) silently DROPS an item
        // whose material has no shader variant at the group's variant index:
        //     idx = groupState+0x1b8   (BeginDrawGroup's param_4)
        //     mat = matInstance+0x28;  requires 0 <= idx < *(int*)(mat+0x30)
        //     and the variant's +0x20/+0x28/+0x30 programs must all be non-null
        //     with a positive size at +0x48.
        // For the main scene pass that index is renderer+0x3c, which THIS PORT writes
        // every frame from VideoConfig+0x30 (the shadow-quality setting). A setting
        // past a material's variant count drops every draw item using that material,
        // with no error anywhere - the model is simply absent.
        {
            Object::ExternalPtr<Graphics::MaterialInstance>* matInsts = mi->_materials.GetData();  // +0xc8
            if (Readable(matInsts)) {
                Graphics::MaterialInstance* minst = matInsts[0].Get();
                if (Readable(minst)) {
                    Graphics::Material* mat = minst->_material.Get();          // +0x28
                    if (Readable(mat)) {
                        const int variants = mat->_passes.GetSize();            // +0x30
                        if (variants >= 0 && variants < 8) ++variantHist[variants];
                        else                               ++variantHist[8];
                        if (variantIdx < 0 || variantIdx >= variants) {
                            ++variantMiss;
                        } else {
                            Object::ExternalPtr<Video::VideoProgram>* arr = mat->_passes.GetData();  // +0x28
                            Video::VideoProgram* v = Readable(arr) ? arr[variantIdx].Get() : nullptr;
                            if (!Readable(v)) {
                                ++variantNotReady;
                            } else {
                                Video::VertexLayout*  p0 = v->_vertexLayout.Get();    // +0x20
                                Video::VertexProgram* p1 = v->_vertexProgram.Get();   // +0x28
                                Video::PixelProgram*  p2 = v->_pixelProgram.Get();    // +0x30
                                const bool ok =
                                    p0 && Readable(p1) && Readable(p2) &&
                                    p1->_byteCode.GetSize() > 0 &&                  // +0x48
                                    p2->_byteCode.GetSize() > 0;
                                if (!ok) ++variantNotReady;
                            }
                        }
                    }
                }
            }
        }

        if (n <= 1 || !Readable(levels)) continue;
        ++multiLevel;

        // Retail union: current world box geo+0x4c..+0x60 with the previous one
        // geo+0x64..+0x78.
        const ::Box& cb = geo->_worldBounds;       // +0x4c
        const ::Box& pb = geo->_prevWorldBounds;   // +0x64
        const float minx = cb.min.x < pb.min.x ? cb.min.x : pb.min.x;
        const float miny = cb.min.y < pb.min.y ? cb.min.y : pb.min.y;
        const float minz = cb.min.z < pb.min.z ? cb.min.z : pb.min.z;
        const float maxx = cb.max.x > pb.max.x ? cb.max.x : pb.max.x;
        const float maxy = cb.max.y > pb.max.y ? cb.max.y : pb.max.y;
        const float maxz = cb.max.z > pb.max.z ? cb.max.z : pb.max.z;
        // The ctor seeds the previous box with the empty sentinel (+/-1e8), so a
        // geometry whose Clean never rolled it still unions to the current box.
        // Count them anyway: it tells us whether the roll is happening at all.
        if (pb.min.x > pb.max.x) ++prevBoxEmpty;

        const float dx = camX - (minx + maxx) * 0.5f;
        const float dy = camY - (miny + maxy) * 0.5f;
        const float dz = camZ - (minz + maxz) * 0.5f;
        const float d2 = dx * dx + dy * dy + dz * dz;

        int lod = 0;
        bool matched = false;
        for (int k = 0; k < n; ++k) {
            const float d = levels[k]._distance;   // +0x10
            if (d2 < d * d) { lod = k; matched = true; break; }
        }
        if (!matched) ++fellBack;   // beyond every level: retail draws level 0
        if (lod >= 0 && lod < 8) ++lodHist[lod];

        auto it = s_lod.find(obj);
        if (it == s_lod.end()) {
            s_lod.emplace(obj, lod);
        } else if (it->second != lod) {
            ++flips;
            if (!flipExample) {
                flipExample = obj; flipFrom = it->second; flipTo = lod;
                flipDist = d2 > 0.0f ? std::sqrt(d2) : 0.0f;
            }
            it->second = lod;
        }
    }

    // One-time dump of every distinct mesh's distance table. A table that is not
    // strictly increasing, or that holds a zero/negative/absurd distance, sends the
    // pick to the wrong level at a distance that the camera crosses constantly.
    if (s_frame == 120) {
        for (auto& kv : s_meshes) {
            MeshInfo& info = kv.second;
            if (info.dumped) continue;
            info.dumped = true;
            // Diag::AppendFormat clamps the offset at a cut-off field: `off += snprintf(...)`
            // stepped past buf and the next call wrapped `sizeof(buf) - off` (review F-011).
            char buf[512]; int off = 0;
            for (int k = 0; k < info.levels && k < 8; ++k)
                off = Diag::AppendFormat(buf, sizeof(buf), off,
                                         "%sL%d{d=%.1f tris=%d vb=%p ib=%p}", k ? " " : "", k,
                                         info.dist[k], info.tris[k], info.vb[k], info.ib[k]);
            if (info.levels == 0) std::snprintf(buf, sizeof(buf), "-");
            bool sane = info.levels >= 1 && info.levels <= 8;
            for (int k = 0; sane && k < info.levels && k < 8; ++k) {
                if (!(info.dist[k] > 0.0f) || info.dist[k] > 1.0e6f) sane = false;
                if (k && info.dist[k] <= info.dist[k - 1])           sane = false;
            }
            bool empty = false;
            for (int k = 0; k < info.levels && k < 8; ++k)
                if (info.tris[k] <= 0 || !info.vb[k] || !info.ib[k]) empty = true;
            char vb2[320]; int vo = 0; vb2[0] = 0;
            auto vit = s_variants.find(kv.first);
            if (vit != s_variants.end())
                for (int k = 0; k < vit->second.n; ++k)
                    vo = Diag::AppendFormat(vb2, sizeof(vb2), vo, "%s(%d,%d)x%d",
                                            k ? "," : "", vit->second.vi[k], vit->second.mv[k],
                                            vit->second.count[k]);
            std::fprintf(stderr, "[LodCensus]   mesh=%p variants(vi,mv)=[%s]\n", kv.first, vb2);
            std::fprintf(stderr, "[LodCensus] mesh=%p levels=%d instances=%d %s%s%s\n",
                         kv.first, info.levels, info.instances, buf,
                         sane  ? "" : "   <== TABLE NOT MONOTONIC/POSITIVE",
                         empty ? "   <== A LEVEL DRAWS NOTHING" : "");
        }
        std::fflush(stderr);
    }

    static int s_scaleReports = 0;
    if (scaleJumped > 0 && s_scaleReports < 40) {
        ++s_scaleReports;
        std::fprintf(stderr, "[LodCensus] frame=%d SCALE JUMPS=%d (moved=%d) e.g. geo=%p "
                             "%.3f -> %.3f in ONE frame\n",
                     s_frame, scaleJumped, scaleMoved, scaleExample, scaleFrom, scaleTo);
        std::fflush(stderr);
    }

    static int s_swapReports = 0;
    if (meshSwaps > 0 && s_swapReports < 30) {
        ++s_swapReports;
        std::fprintf(stderr, "[LodCensus] frame=%d MESH SWAPS=%d matSwaps=%d e.g. geo=%p "
                             "mesh %p -> %p\n",
                     s_frame, meshSwaps, matSwaps, swapExample, swapFrom, swapTo);
        std::fflush(stderr);
    }

    static int s_flipReports = 0;
    if (flips > 0 && s_flipReports < 30) {
        ++s_flipReports;
        std::fprintf(stderr, "[LodCensus] frame=%d FLIPS=%d e.g. geo=%p %d -> %d at dist=%.1f\n",
                     s_frame, flips, flipExample, flipFrom, flipTo, flipDist);
        std::fflush(stderr);
    }
    if ((s_frame % 60) == 0) {
        std::fprintf(stderr,
            "[LodCensus] frame=%d meshInstances=%d multiLevel=%d meshes=%zu flips=%d "
            "fellBackToL0=%d prevBoxEmpty=%d | lod0=%d lod1=%d lod2=%d lod3=%d "
            "| variantIdx=%d MISS=%d notReady=%d variants[1..4]=%d/%d/%d/%d "
            "| meshSwaps=%d matSwaps=%d faded=%d fadedOut=%d scaled=%d oddDetail=%d scaleMoved=%d scaleJumped=%d "
            "cam=(%.1f,%.1f,%.1f)\n",
            s_frame, instances, multiLevel, s_meshes.size(), flips, fellBack, prevBoxEmpty,
            lodHist[0], lodHist[1], lodHist[2], lodHist[3],
            variantIdx, variantMiss, variantNotReady,
            variantHist[1], variantHist[2], variantHist[3], variantHist[4],
            meshSwaps, matSwaps, faded, fadedOut, tinted, oddDetailId,
            scaleMoved, scaleJumped,
            camX, camY, camZ);
        std::fflush(stderr);
    }
}

void SceneWatch_DrawItemCensus(Graphics::Renderer* renderer) {
    if (!LodEnabled() || !renderer) return;

    static int32_t s_frame = 0;
    ++s_frame;

    Graphics::RendererDrawGroup* groups = renderer->_drawGroups.GetData();   // +0x10
    const int used = renderer->_drawGroupsUsed;                              // +0x38
    static int s_why = 0;
    if (!groups || used <= 0 || used > 64) {
        if (s_why < 3) {
            ++s_why;
            std::fprintf(stderr, "[DrawCensus] frame=%d NO GROUPS: renderer=%p groups=%p "
                                 "used(+0x38)=%d cap(+0x18)=%d scenes(+0x8)=%d\n",
                         s_frame, static_cast<void*>(renderer), static_cast<void*>(groups), used,
                         renderer->_drawGroups.GetCapacity(),   // +0x18
                         renderer->_sceneGroups.GetSize());     // +0x08
            std::fflush(stderr);
        }
        return;
    }

    static int s_groupDump = 0;
    if (s_frame == 120 || s_frame == 600) ++s_groupDump;
    int total = 0, mainGroup = 0;
    for (int g = 0; g < used; ++g) {
        const Graphics::RendererDrawGroup& st = groups[g];   // stride 0x1f0
        int sum = 0;
        for (int k = 0; k < 3; ++k)
            sum += st._items[k].GetSize();                  // +0x1c0 + k*0x10 + 8
        total += sum;
        if (g == 0) mainGroup = sum;
        // groupState+0x1b8 is BeginDrawGroup's param_4 — the shader-VARIANT index that
        // Graphics::Renderer::AddDrawItem indexes each material's variant array with;
        // +0x1bc is the layer mask. Read them rather than infer them: Renderer::Draw
        // feeds BeginDrawGroup the scene entry's +0xec (AddScene's param_6), NOT
        // renderer+0x3c, so the obvious guess is wrong.
        if (s_groupDump < 2) {
            std::fprintf(stderr, "[DrawCensus] group %d/%d variantIdx(+0x1b8)=%d mask(+0x1bc)=0x%x "
                                 "items=%d\n",
                         g, used, st._shader,                // +0x1b8
                         st._materialTypeFlags, sum);        // +0x1bc
        }
    }

    // A smoothly moving camera should change this count smoothly. A step change of
    // hundreds between consecutive frames IS the pop, and locating it in the group
    // breakdown says whether the shadow passes or the main pass dropped the objects.
    static int s_prev = -1;
    static int s_stepReports = 0;
    if (s_prev >= 0) {
        const int step = total > s_prev ? total - s_prev : s_prev - total;
        if (step > 100 && s_stepReports < 30) {
            ++s_stepReports;
            std::fprintf(stderr, "[DrawCensus] frame=%d STEP %d -> %d (%+d) groups=%d\n",
                         s_frame, s_prev, total, total - s_prev, used);
            std::fflush(stderr);
        }
    }
    s_prev = total;
    if (s_groupDump && (s_frame == 120 || s_frame == 600)) std::fflush(stderr);

    if ((s_frame % 60) == 0) {
        std::fprintf(stderr, "[DrawCensus] frame=%d drawItems=%d group0=%d groups=%d\n",
                     s_frame, total, mainGroup, used);
        std::fflush(stderr);
    }
}
