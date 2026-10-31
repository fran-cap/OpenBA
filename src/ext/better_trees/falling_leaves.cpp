// ============================================================================
// src/ext/better_trees/falling_leaves.cpp -- natural tree growth's falling leaves (NATURAL_GROWTH.md §7)
// ============================================================================
// NOT RETAIL. The header has the contract, leaf_fall.h the pure part.
//
// THE TREES. The map's occupants are walked a slice per frame (the whole map in kScanSlices frames); an
// occupant is a deciduous tree when it carries a NaturalResourceComponent and a model whose materials
// include a billboard canopy that is not the evergreen one (the canopies natural_growth.h rewrites). The
// answer is kept per description. Each pass keeps the trees' positions and crown boxes, no pointers.
//
// THE DRAW. One Graphics::Geometry in the scene's global list, drawn like WeatherParticleGeometry
// (weather_particle_draw.cpp): one instance at the origin, the cards' world positions in the dynamic
// vertex stream, the snow particle material's program with our atlas in its texture slot. The main thread
// builds the cards (LeavesFrame); Draw, which ext.render_pipe may run on its render thread, copies the
// last finished set under a lock and records the camera the next set is built for.
// ============================================================================
#include "ext/better_trees/falling_leaves.h"

#include "ext/better_trees/better_trees.h"
#include "ext/better_trees/leaf_fall.h"
#include "ext/resources/sheet_builder.h"

#include "Component/ComponentDescription.h"
#include "Component/EnvironmentDescription.h"
#include "Component/ModelComponent.h"
#include "Component/NaturalResourceComponent.h"
#include "Entity/ComponentEntity.h"
#include "Game/Calendar.h"
#include "Game/GameUpdateLoop.h"
#include "Game/Map.h"
#include "Game/Weather.h"
#include "Game/WeatherSimulation.h"
#include "graphics/Camera.h"
#include "graphics/Material.h"
#include "graphics/Scene.h"
#include "math/Math.h"
#include "system/MemoryAllocator.h"
#include "video/Context.h"

#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <new>
#include <string>
#include <unordered_map>
#include <vector>

namespace Ext {
namespace BetterTrees {

namespace {

using LeafFall::Vertex;
static_assert(sizeof(Vertex) == 0x14, "the particle program's vertex is 0x14 bytes");

constexpr int   kScanSlices  = 16;      // the whole map in 16 frames
constexpr float kRadius      = 45.0f;   // trees dropping leaves: within this of the camera (horizontally)
constexpr float kMaxHeight   = 110.0f;  // above this over the ground the leaves are sub-pixel: none drop
constexpr float kBound       = 1024.0f; // WeatherParticleGeometry's bounds

// What Draw shares with the main thread.
struct Shared {
    std::mutex mutex;
    std::vector<Vertex> cards;     // the last finished set
    bool  haveCamera = false;
    float position[3] = {};
    float right[3] = {1.0f, 0.0f, 0.0f};
    float up[3] = {0.0f, 0.0f, 1.0f};
    float forward[3] = {0.0f, 1.0f, 0.0f};
};

class LeavesGeometry final : public Graphics::Geometry {
public:
    explicit LeavesGeometry(Graphics::MaterialInstance* material) : _material(material) {
        SetTransform(Math::Transform::Identity());
        ::Box box;
        box.min.x = -kBound;
        box.min.y = -kBound;
        box.min.z = -kBound;
        box.max.x = kBound;
        box.max.y = kBound;
        box.max.z = kBound;
        SetBounds(box);
        _materialTypeFlags = 1u << (material->_shaderClass & 0x1f);
    }
    ~LeavesGeometry() override = default;

    void Update(const System::Time& /*time*/) override {}

    // WeatherParticleGeometry::AddDrawItems' item (weather_particles.cpp), every frame: Draw is also where
    // the camera the next set of cards is built for is read.
    void AddDrawItems(const Graphics::Scene& /*scene*/, Graphics::Renderer& renderer) const override {
        renderer.AddDrawItem(this, _material, 0xffff, 0, 0.0f, 1.0f, 1, nullptr, 0);
    }

    void Draw(Video::Context& context, const Graphics::Camera& camera, Graphics::ExternalPtrHandle /*material*/,
              float /*opacity*/, int /*a*/, int /*b*/, int /*c*/) const override;

    mutable Shared shared;

private:
    Graphics::MaterialInstance* _material;
};

void LeavesGeometry::Draw(Video::Context& context, const Graphics::Camera& camera, Graphics::ExternalPtrHandle,
                          float, int, int, int) const {
    const Math::Transform3 view(camera._cameraToWorld);
    std::lock_guard<std::mutex> lock(shared.mutex);
    // the camera the next set is built for (weather_particle_draw.cpp: col0 and col2 span the view plane)
    shared.haveCamera = true;
    shared.position[0] = view.translation.x;
    shared.position[1] = view.translation.y;
    shared.position[2] = view.translation.z;
    shared.right[0] = view.axisX.x;
    shared.right[1] = view.axisX.y;
    shared.right[2] = view.axisX.z;
    shared.up[0] = view.axisZ.x;
    shared.up[1] = view.axisZ.y;
    shared.up[2] = view.axisZ.z;
    shared.forward[0] = view.axisY.x;
    shared.forward[1] = view.axisY.y;
    shared.forward[2] = view.axisY.z;
    const int vertices = static_cast<int>(shared.cards.size());
    if (vertices == 0) return;
    const int indices = vertices / 4 * 6;

    // the weather's state: full intensity, its two depth fades (o2 = the fade inputs, saturated)
    context.SetColor(Math::Vector4(1.0f, 1.0f, 1.0f, 1.0f));
    const float nearFadeStart = 90.0f;   // gone beyond 90 units of depth, fading from 70
    const float nearFade = 70.0f;
    const float farFade = 0.5f;          // and fading in over the first 2 units in front of the camera
    const float farFadeStart = 2.0f;
    context.SetInverseScale(-nearFadeStart, 1.0f / (nearFade - nearFadeStart), -farFade,
                            1.0f / (farFadeStart - farFade));
    if (context.GetMaxDynamicInstances(vertices, static_cast<int>(sizeof(Vertex)), indices) < 1) return;
    const Math::Matrix origin(Math::Vector4(1.0f, 0.0f, 0.0f, 0.0f), Math::Vector4(0.0f, 1.0f, 0.0f, 0.0f),
                              Math::Vector4(0.0f, 0.0f, 1.0f, 0.0f), Math::Vector4(0.0f, 0.0f, 0.0f, 1.0f));
    context.SetTransform(0, origin);
    if (!context.BeginInstancedIndexedVertices(vertices, static_cast<int>(sizeof(Vertex)), indices, 1)) return;
    const size_t bytes = static_cast<size_t>(vertices) * static_cast<size_t>(context._dynamicVertexStride);
    std::memcpy(context._dynamicVertexCursor, shared.cards.data(), bytes);
    context._dynamicVertexCursor += bytes;
    uint16_t*& out = context._dynamicIndexCursor;
    for (int q = 0; q < vertices / 4; ++q) {
        const uint16_t base = static_cast<uint16_t>(q * 4);
        out[0] = base;
        out[1] = static_cast<uint16_t>(base + 1);
        out[2] = static_cast<uint16_t>(base + 2);
        out[3] = base;
        out[4] = static_cast<uint16_t>(base + 2);
        out[5] = static_cast<uint16_t>(base + 3);
        out += 6;
    }
    context.EndInstancedIndexedVertices(0);
}

// ---- the module's state (main thread) --------------------------------------------------------------
struct State {
    Graphics::Scene* scene = nullptr;
    LeavesGeometry* geometry = nullptr;
    Ext::Resources::SheetObjects atlas;
    bool atlasFailed = false;
    const void* map = nullptr;
    int slice = 0;
    std::vector<LeafFall::Tree> scanning;   // the pass in progress
    std::vector<LeafFall::Tree> trees;      // the last complete pass
    std::unordered_map<const void*, bool> deciduous;   // per ComponentDescription
    LeafFall::Field field;
    std::vector<LeafFall::Tree> near;
    std::vector<Vertex> cards;
    int64_t lastGameTime = -1;
    std::chrono::steady_clock::time_point lastClock;
    std::chrono::steady_clock::time_point lastTick;   // when the game clock last moved
    bool clockSet = false;
};
State g;

std::string Lower(const char* s) {
    std::string r = s != nullptr ? s : "";
    for (char& c : r) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return r;
}

std::string PathOf(const Object::External* object) {
    if (object == nullptr) return std::string();
    const System::Filename path = object->GetPath();
    return path._data != nullptr ? path._data : "";
}

// A deciduous canopy: a billboard material that is not the evergreen one (NATURAL_GROWTH.md §6.1, S1).
// Decided per mesh (its first material instance): every tree species is the one description
// Template/NaturalResourceTree.rsc, with a mesh per species.
bool IsDeciduous(ComponentEntity* entity, ModelComponent* model) {
    const auto& materials = model->meshInstance._materials;
    if (materials._size < 1) return false;
    const void* key = materials._data[0].Get();
    auto found = g.deciduous.find(key);
    if (found != g.deciduous.end()) return found->second;
    bool yes = false;
    std::string seen;
    for (int i = 0; i < materials._size; ++i) {
        const Graphics::MaterialInstance* mi = materials._data[i].Get();
        const Graphics::Material* base = mi != nullptr ? mi->_material.Get() : nullptr;
        const std::string path = Lower(PathOf(base).c_str());
        if (path.find("billboard") != std::string::npos && path.find("evergreen") == std::string::npos) yes = true;
        if (!seen.empty()) seen += ", ";
        seen += path;
    }
    g.deciduous.emplace(key, yes);
    std::fprintf(stderr, "[NaturalGrowth] leaves: %s mesh %d.%d %s (materials: %s)\n",
                 PathOf(static_cast<const Object::External*>(entity->_description)).c_str(), model->meshGroup,
                 model->meshIndex, yes ? "drops leaves" : "keeps them", seen.c_str());
    return yes;
}

void ScanSlice(Map* map) {
    if (map != g.map) {
        g.map = map;
        g.slice = 0;
        g.scanning.clear();
        g.trees.clear();
        g.deciduous.clear();
        g.field.Clear();
    }
    const int cells = map->size * map->size;
    const int per = (cells + kScanSlices - 1) / kScanSlices;
    const int from = g.slice * per;
    const int to = from + per < cells ? from + per : cells;
    for (int i = from; i < to; ++i) {
        ComponentEntity* e = map->cells.GetData()[i].occupant.Get();
        if (e == nullptr || e->FindComponent<NaturalResourceComponent>() == nullptr) continue;
        ModelComponent* model = e->FindComponent<ModelComponent>();
        if (model == nullptr || model->modelId < 0 || model->meshIndex < 0) continue;
        if (!IsDeciduous(e, model)) continue;
        const Math::Vector3& t = model->transform.translation;
        // one tree per entity: the cell its own position falls in (a footprint may cover several)
        const ::Box& b = model->meshInstance._worldBounds;
        if (b.max.z <= b.min.z) continue;   // not cleaned yet
        LeafFall::Tree tree;
        tree.x = t.x;
        tree.y = t.y;
        tree.baseZ = t.z;
        tree.crown[0] = b.min.x;
        tree.crown[1] = b.min.y;
        tree.crown[2] = b.min.z;
        tree.crown[3] = b.max.x;
        tree.crown[4] = b.max.y;
        tree.crown[5] = b.max.z;
        bool dup = false;
        for (size_t k = g.scanning.size() > 8 ? g.scanning.size() - 8 : 0; k < g.scanning.size(); ++k) {
            if (g.scanning[k].x == tree.x && g.scanning[k].y == tree.y) dup = true;
        }
        if (!dup) g.scanning.push_back(tree);
    }
    if (++g.slice >= kScanSlices) {
        g.slice = 0;
        g.trees.swap(g.scanning);
        g.scanning.clear();
    }
}

// The material: our atlas in the snow particle material's texture slot.
bool EnsureGeometry(GameUpdateLoop& game) {
    if (g.geometry != nullptr && g.scene == game.m_graphicsScene) return true;
    if (g.geometry != nullptr) {
        // a scene that is gone (its list went with it): only the geometry is left to free
        delete g.geometry;
        g.geometry = nullptr;
        g.scene = nullptr;
    }
    if (g.atlasFailed || game.m_graphicsScene == nullptr || game.m_weather == nullptr) return false;
    if (g.atlas.material == nullptr) {
        const EnvironmentDescription* env = game.m_weather->environment;
        const Graphics::MaterialInstance* snow = nullptr;
        if (env != nullptr) {
            for (int i = 0; i < env->_weatherArray._size; ++i) {
                const Weather* w = env->_weatherArray._data[i].weather;
                if (w != nullptr && w->material != nullptr && !w->velocityStretch) snow = w->material;   // not rain's streaks
            }
        }
        if (snow == nullptr || snow->_material.Get() == nullptr || snow->_textures._size < 1) {
            std::fprintf(stderr, "[NaturalGrowth] leaves: no snow particle material in this climate: no leaves\n");
            g.atlasFailed = true;
            return false;
        }
        const std::vector<uint8_t> pixels = LeafFall::PaintAtlas();
        const Ext::Resources::SheetBitmap bitmap{"leaves", "NaturalGrowth", LeafFall::kAtlasSize,
                                                 LeafFall::kAtlasSize, pixels.data()};
        Ext::Resources::SheetSpec spec;
        spec.sheetWidth = LeafFall::kAtlasSize;
        spec.sheetHeight = LeafFall::kAtlasSize;
        spec.padForFiltering = false;
        spec.materialName = "NaturalGrowthLeaves";
        spec.imageName = "NaturalGrowthLeaves";
        const char* slot = snow->_textures._data[0].name._data;
        g.atlas = Ext::Resources::BuildSheetObjects(spec, &bitmap, 1,
                                                    const_cast<Graphics::Material*>(snow->_material.Get()), slot);
        if (g.atlas.material == nullptr) {
            g.atlasFailed = true;
            return false;
        }
        std::fprintf(stderr, "[NaturalGrowth] leaves: atlas %dx%d on %s (slot %s)\n", LeafFall::kAtlasSize,
                     LeafFall::kAtlasSize, PathOf(snow->_material.Get()).c_str(), slot != nullptr ? slot : "?");
    }
    void* mem = System::MemoryAllocator::MemAlloc(sizeof(LeavesGeometry), 16);
    if (mem == nullptr) return false;
    g.geometry = ::new (mem) LeavesGeometry(g.atlas.material);
    g.scene = game.m_graphicsScene;
    g.scene->AddGlobalObject(g.geometry);
    return true;
}

void DropGeometry() {
    if (g.geometry != nullptr) {
        if (g.scene != nullptr) g.scene->RemoveGlobalObject(g.geometry);
        delete g.geometry;
    }
    g.geometry = nullptr;
    g.scene = nullptr;
    g.field.Clear();
}

}  // namespace

void LeavesFrame(GameUpdateLoop& game) {
    if (!NaturalGrowthOn() || game.m_dayCounter == nullptr || game.m_game == nullptr) {
        if (g.geometry != nullptr) DropGeometry();
        return;
    }
    const float months = game.m_dayCounter->yearTime.AsSeconds() / 300.0f;   // NgRewrite::kMonthSeconds
    ScanSlice(game.m_game);
    // nothing to do outside the autumn (the first tree starts at fallStart - autumnSpread * 0.8): the
    // geometry leaves the scene, so the rest of the year draws exactly as without the part
    if (months < 6.0f || months > 9.5f) {
        if (g.geometry != nullptr) DropGeometry();
        return;
    }
    if (!EnsureGeometry(game)) return;

    // real time while the game runs; nothing moves while it is paused (its clock, which only the fixed
    // steps advance, has stood still for a quarter of a second)
    const auto now = std::chrono::steady_clock::now();
    float dt = g.clockSet ? std::chrono::duration<float>(now - g.lastClock).count() : 0.0f;
    g.lastClock = now;
    g.clockSet = true;
    if (dt > 0.1f) dt = 0.1f;
    if (game.m_gameTime != g.lastGameTime) {
        g.lastGameTime = game.m_gameTime;
        g.lastTick = now;
    }
    if (std::chrono::duration<float>(now - g.lastTick).count() > 0.25f) dt = 0.0f;

    Shared& sh = g.geometry->shared;
    float cam[3];
    float right[3];
    float up[3];
    float forward[3];
    {
        std::lock_guard<std::mutex> lock(sh.mutex);
        if (!sh.haveCamera) return;
        std::memcpy(cam, sh.position, sizeof cam);
        std::memcpy(right, sh.right, sizeof right);
        std::memcpy(up, sh.up, sizeof up);
        std::memcpy(forward, sh.forward, sizeof forward);
    }
    // the trees around where the camera looks: its view ray's point at ground height (the camera's own
    // spot when it looks level), at most kRadius ahead
    float ahead = forward[2] < -0.05f ? cam[2] / -forward[2] : 0.0f;
    if (ahead > kRadius) ahead = kRadius;
    const float centre[2] = {cam[0] + forward[0] * ahead, cam[1] + forward[1] * ahead};
    g.near.clear();
    for (const LeafFall::Tree& t : g.trees) {
        const float dx = t.x - centre[0];
        const float dy = t.y - centre[1];
        if (dx * dx + dy * dy > kRadius * kRadius || cam[2] - t.baseZ > kMaxHeight) continue;
        const float loss = LeafFall::AutumnLoss(t.x, t.y, months);
        if (loss <= 0.0f || loss >= 1.0f) continue;
        g.near.push_back(t);
    }
    g.field.Step(dt, months, g.near);
    g.cards.clear();
    LeafFall::BuildQuads(g.field.Leaves(), right, up, g.cards);
    std::lock_guard<std::mutex> lock(sh.mutex);
    sh.cards.swap(g.cards);
}

void LeavesShutdown(Graphics::Scene* scene) {
    if (g.geometry != nullptr && g.scene != scene) g.scene = nullptr;   // not in this scene's list
    DropGeometry();
    Ext::Resources::DestroySheetObjects(g.atlas);
    g.atlasFailed = false;
    g.map = nullptr;
    g.trees.clear();
    g.scanning.clear();
    g.deciduous.clear();
    g.field.Clear();
    g.clockSet = false;
}

}  // namespace BetterTrees
}  // namespace Ext
