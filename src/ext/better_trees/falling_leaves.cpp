// ============================================================================
// src/ext/better_trees/falling_leaves.cpp -- natural tree growth's falling leaves (NATURAL_GROWTH.md §7)
// ============================================================================
// NOT RETAIL. The header has the contract, leaf_fall.h the pure part.
//
// THE TREES. The map's occupants are walked a slice per frame (the whole map in kScanSlices frames); an
// occupant is a deciduous tree when it carries a NaturalResourceComponent and a mesh whose materials
// include a billboard canopy that is not the evergreen one (the canopies natural_growth.h rewrites). Every
// tree species is the one description Template/NaturalResourceTree.rsc, so the answer is kept per mesh
// (its first material instance). Each pass keeps the trees' positions and crown boxes, no pointers.
//
// THE DRAW. One Graphics::Geometry in the scene's global list, drawn as the herbs are: the installed
// Material/AlphaTest/AlphaTestMaterial.rsc with our leaf atlas in its "diffuse" slot, so the cards are lit
// by the sun, receive the shadow map (the normal pass) and cast shadows (its depth pass). One instance
// carries the cards' origin; the cards are in the dynamic vertex stream in ModelLayout (leaf_fall.h). The
// main thread builds them from the player's camera (LeavesFrame); Draw, which ext.render_pipe may run on
// its render thread and which the renderer calls once per pass (the shadow, normal and reflection
// passes), copies the last finished set under a lock.
// ============================================================================
#include "ext/better_trees/falling_leaves.h"

#include "ext/better_trees/better_trees.h"
#include "ext/better_trees/leaf_fall.h"
#include "ext/resources/sheet_builder.h"

#include "Component/ComponentDescription.h"
#include "Component/ModelComponent.h"
#include "Component/NaturalResourceComponent.h"
#include "Entity/ComponentEntity.h"
#include "Game/Calendar.h"
#include "Game/GameUpdateLoop.h"
#include "Game/Map.h"
#include "Game/PlayerController.h"
#include "graphics/Camera.h"
#include "graphics/Material.h"
#include "graphics/Scene.h"
#include "math/Math.h"
#include "system/MemoryAllocator.h"
#include "video/Context.h"

#include <cctype>
#include <chrono>
#include <cmath>
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

constexpr int   kScanSlices  = 16;      // the whole map in 16 frames
constexpr float kRadius      = 45.0f;   // trees dropping leaves: within this of where the camera looks
constexpr float kMaxHeight   = 110.0f;  // above this over the ground the leaves are sub-pixel: none drop
constexpr float kBound       = 1024.0f; // WeatherParticleGeometry's bounds
constexpr const char* kMaterial = "Material/AlphaTest/AlphaTestMaterial.rsc";   // the herbs' material

// What Draw shares with the main thread.
struct Shared {
    std::mutex mutex;
    std::vector<Vertex> cards;   // the last finished set
    float origin[3] = {};        // what their positions are relative to
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

    // A mesh's item (mesh.cpp: its detail id as the sort key) without the mesh, as the weather's
    // (weather_particles.cpp): the renderer calls Draw for each of the material's passes.
    void AddDrawItems(const Graphics::Scene& /*scene*/, Graphics::Renderer& renderer) const override {
        {
            std::lock_guard<std::mutex> lock(shared.mutex);
            if (shared.cards.empty()) return;
        }
        renderer.AddDrawItem(this, _material, 2, 0, 0.0f, 1.0f, 1, nullptr, 0xffffffffu);
    }

    void Draw(Video::Context& context, const Graphics::Camera& camera, Graphics::ExternalPtrHandle material,
              float opacity, int a, int b, int c) const override;

    mutable Shared shared;

private:
    Graphics::MaterialInstance* _material;
};

void LeavesGeometry::Draw(Video::Context& context, const Graphics::Camera& /*camera*/,
                          Graphics::ExternalPtrHandle /*material*/, float /*opacity*/, int /*a*/, int /*b*/,
                          int /*c*/) const {
    std::lock_guard<std::mutex> lock(shared.mutex);
    const int vertices = static_cast<int>(shared.cards.size());
    if (vertices == 0) return;
    const int indices = vertices / 4 * 6;
    const int stride = static_cast<int>(sizeof(Vertex));
    if (context.GetMaxDynamicInstances(vertices, stride, indices) < 1) return;
    // the instance as a standalone rigid mesh sets it (meshinstance_draw.cpp): the cards' origin as the
    // translation, a unit scale
    Math::Transform world = Math::Transform::Identity();
    world.translation.x = shared.origin[0];
    world.translation.y = shared.origin[1];
    world.translation.z = shared.origin[2];
    context.SetTransformScale(0, world.AsMatrix(), Math::Vector4(1.0f, 1.0f, 1.0f, 0.0f));
    if (!context.BeginInstancedIndexedVertices(vertices, stride, indices, 1)) return;
    const size_t bytes = static_cast<size_t>(vertices) * static_cast<size_t>(context._dynamicVertexStride);
    std::memcpy(context._dynamicVertexCursor, shared.cards.data(), bytes);
    context._dynamicVertexCursor += bytes;
    uint16_t*& out = context._dynamicIndexCursor;
    for (int q = 0; q < vertices / 4; ++q) {
        const uint16_t base = static_cast<uint16_t>(q * 4);
        // wound to face the camera: AlphaTestMaterial culls back faces (the weather's material does not)
        out[0] = base;
        out[1] = static_cast<uint16_t>(base + 2);
        out[2] = static_cast<uint16_t>(base + 1);
        out[3] = base;
        out[4] = static_cast<uint16_t>(base + 3);
        out[5] = static_cast<uint16_t>(base + 2);
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
    std::unordered_map<const void*, bool> deciduous;   // per mesh (its first material instance)
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
        // one tree per entity (a footprint may cover several neighbouring cells)
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

// The material: our atlas in AlphaTestMaterial's "diffuse" slot (the herbs' one texture).
bool EnsureGeometry(GameUpdateLoop& game) {
    if (g.geometry != nullptr && g.scene == game.m_graphicsScene) return true;
    if (g.geometry != nullptr) {
        // a scene that is gone (its list went with it): only the geometry is left to free
        delete g.geometry;
        g.geometry = nullptr;
        g.scene = nullptr;
    }
    if (g.atlasFailed || game.m_graphicsScene == nullptr) return false;
    if (g.atlas.material == nullptr) {
        const std::vector<uint8_t> pixels = LeafFall::PaintAtlas();
        const Ext::Resources::SheetBitmap bitmap{"leaves", "NaturalGrowth", LeafFall::kAtlasSize,
                                                 LeafFall::kAtlasSize, pixels.data()};
        Ext::Resources::SheetSpec spec;
        spec.sheetWidth = LeafFall::kAtlasSize;
        spec.sheetHeight = LeafFall::kAtlasSize;
        spec.padForFiltering = false;
        spec.materialName = "NaturalGrowthLeaves";
        spec.imageName = "NaturalGrowthLeaves";
        g.atlas = Ext::Resources::BuildSheetObjects(spec, &bitmap, 1, kMaterial, "diffuse");
        if (g.atlas.material == nullptr) {
            std::fprintf(stderr, "[NaturalGrowth] leaves: no %s: no leaves\n", kMaterial);
            g.atlasFailed = true;
            return false;
        }
        std::fprintf(stderr, "[NaturalGrowth] leaves: atlas %dx%d on %s\n", LeafFall::kAtlasSize,
                     LeafFall::kAtlasSize, kMaterial);
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
    if (!NaturalGrowthOn() || game.m_dayCounter == nullptr || game.m_game == nullptr ||
        game.m_playerController == nullptr) {
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

    // the player's camera (weather_particle_draw.cpp: its X and Z axes span the view plane, Y looks ahead)
    const Math::Transform3 view(game.m_playerController->camera._cameraToWorld);
    const float cam[3] = {view.translation.x, view.translation.y, view.translation.z};
    const float right[3] = {view.axisX.x, view.axisX.y, view.axisX.z};
    const float up[3] = {view.axisZ.x, view.axisZ.y, view.axisZ.z};
    const float forward[3] = {view.axisY.x, view.axisY.y, view.axisY.z};

    // the trees around where the camera looks: its view ray's point at ground height (the camera's own
    // spot when it looks level), at most kRadius ahead
    float ahead = forward[2] < -0.05f ? cam[2] / -forward[2] : 0.0f;
    if (ahead > kRadius) ahead = kRadius;
    const float centre[2] = {cam[0] + forward[0] * ahead, cam[1] + forward[1] * ahead};
    g.near.clear();
    float ground = 0.0f;
    for (const LeafFall::Tree& t : g.trees) {
        const float dx = t.x - centre[0];
        const float dy = t.y - centre[1];
        if (dx * dx + dy * dy > kRadius * kRadius || cam[2] - t.baseZ > kMaxHeight) continue;
        const float loss = LeafFall::AutumnLoss(t.x, t.y, months);
        if (loss <= 0.0f || loss >= 1.0f) continue;
        g.near.push_back(t);
        ground += t.baseZ;
    }
    g.field.Step(dt, months, g.near);

    // the cards around a whole-unit origin at the view's centre (int16 / 512 reaches +-64 units), lit as
    // facing up and a little towards the camera
    const float origin[3] = {std::floor(centre[0]), std::floor(centre[1]),
                             std::floor(g.near.empty() ? 0.0f : ground / static_cast<float>(g.near.size()))};
    float facing[3] = {-forward[0] * 0.5f, -forward[1] * 0.5f, 1.0f - forward[2] * 0.5f};
    const float len = std::sqrt(facing[0] * facing[0] + facing[1] * facing[1] + facing[2] * facing[2]);
    for (float& f : facing) f /= len;
    g.cards.clear();
    LeafFall::BuildQuads(g.field.Leaves(), right, up, facing, origin, g.cards);
    std::lock_guard<std::mutex> lock(g.geometry->shared.mutex);
    g.geometry->shared.cards.swap(g.cards);
    std::memcpy(g.geometry->shared.origin, origin, sizeof origin);
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
