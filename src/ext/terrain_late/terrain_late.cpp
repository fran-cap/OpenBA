// ============================================================================
// src/ext/terrain_late/terrain_late.cpp -- ext.terrain_late: the Opaque-list move and the late state
// ============================================================================
// NOT RETAIL. docs/extensions/handins/TERRAINLATE.md; the contract is terrain_late.h, the pure rules
// terrain_late_rules.h.
// ============================================================================

// The runtime type headers first, as geometry_draw.cpp has them (Volume.h's `struct Box` before
// anything reaches <windows.h>).
#include "Game/WorldGeometry.h"
#include "graphics/Geometry.h"
#include "graphics/Camera.h"     // Graphics::RendererDrawItem, Graphics::Renderer::_extOpaqueOrder
#include "graphics/Material.h"   // Graphics::MaterialInstance, Graphics::Material
#include "video/Context.h"
#include "video/RenderState.h"
#include "video/VideoProgram.h"

#include "ext/registry.h"
#include "ext/terrain_late/terrain_late.h"
#include "ext/terrain_late/terrain_late_rules.h"

#include <typeinfo>

namespace Ext {
namespace TerrainLate {

namespace {

using Graphics::RendererDrawItem;

// The terrain pass's render state: the material's Normal-shader program (pass 0, the list the main
// entry draws with) and its state. Null when the material has none.
const Video::RenderState* NormalPassState(const Graphics::MaterialInstance* instance) {
    if (instance == nullptr) return nullptr;
    const Graphics::Material* material = instance->_material.Get();
    if (material == nullptr || material->_passes._size <= 0) return nullptr;
    const Video::VideoProgram* pass0 = material->_passes.GetData()[0].Get();
    if (pass0 == nullptr) return nullptr;
    return pass0->_renderState.Get();
}

bool StateSafe(const Video::RenderState* state) {
    if (state == nullptr) return false;
    PassState s;
    s.depthTest  = state->_depth.test;
    s.depthWrite = state->_depth.write;
    s.depthFunc  = state->_depth.func;
    s.blend      = state->_alphaBlend.enabled;
    return LateSafe(s);
}

// The geometry classes by type_info address (typeid(*g) is one load through the vtable; a new address
// pays one type_info compare). Only the pass-1 thread uses it.
struct WorldTypeCache {
    static constexpr int kSlots = 16;
    const std::type_info* type[kSlots] = {};
    bool                  world[kSlots] = {};
    int                   count = 0;
};
thread_local WorldTypeCache t_types;

bool IsWorldGeometry(const Graphics::Geometry* g) {
    if (g == nullptr) return false;
    const std::type_info& t = typeid(*g);
    for (int i = 0; i < t_types.count; ++i) {
        if (t_types.type[i] == &t) return t_types.world[i];
    }
    const bool world = t == typeid(WorldGeometry);
    if (t_types.count < WorldTypeCache::kSlots) {
        t_types.type[t_types.count] = &t;
        t_types.world[t_types.count] = world;
        ++t_types.count;
    }
    return world;
}

// The last terrain material checked and its verdict (a list has one terrain material).
thread_local const Graphics::MaterialInstance* t_checkedInstance = nullptr;
thread_local const Video::RenderState*         t_checkedState    = nullptr;
thread_local bool                              t_checkedSafe     = false;

bool InstanceSafe(const Graphics::MaterialInstance* instance) {
    const Video::RenderState* state = NormalPassState(instance);
    if (instance != t_checkedInstance || state != t_checkedState) {
        t_checkedInstance = instance;
        t_checkedState    = state;
        t_checkedSafe     = StateSafe(state);
    }
    return t_checkedSafe;
}

// A terrain chunk or run item: drawn by a WorldGeometry with its terrain material (its water ranges,
// decals and roads use other materials and are never in the Opaque list's terrain run).
bool IsTerrainItem(const RendererDrawItem& item) {
    if (!IsWorldGeometry(item._geometry)) return false;
    const WorldGeometry* world = static_cast<const WorldGeometry*>(item._geometry);
    return item._instance == world->_terrainMaterial && InstanceSafe(item._instance);
}

// The less-test copy of the terrain's state, made once per source state on the drawing thread (as
// show_grid's GridState) and kept for the life of the process.
const Video::RenderState* LateState(const Video::RenderState* source) {
    static const Video::RenderState* s_source = nullptr;
    static Video::RenderState*       s_late   = nullptr;
    if (source == s_source && s_late != nullptr) return s_late;
    Video::RenderState* state = static_cast<Video::RenderState*>(Video::RenderState::CreateNewObject());
    if (state == nullptr) return nullptr;
    Video::RenderState::Depth depth = source->_depth;
    depth.func = LateDepthFunc(depth.func);
    state->Initialize(source->_alphaBlend, depth, source->_stencil, source->_cullMode, source->_fillMode,
                      source->_scissorEnable);
    s_late   = state;
    s_source = source;
    return s_late;
}

}  // namespace

void Sync() {
    const bool install = CurrentProfile() != Profile::Faithful;
    ::Graphics::Renderer::_extOpaqueOrder = install ? &OrderOpaque : nullptr;
    ::WorldGeometry::_extTerrainLate      = install ? &Ext().terrain_late : nullptr;
}

void OrderOpaque(RendererDrawItem* items, int32_t count) {
    if (!Ext().terrain_late) return;
    MoveMatchingToBack(items, count, [](const RendererDrawItem& item) { return IsTerrainItem(item); });
}

void BindLateState(Video::Context& context, const WorldGeometry& geo) {
    const Video::RenderState* source = NormalPassState(geo._terrainMaterial);
    if (source == nullptr || context._renderState != source || !StateSafe(source)) return;
    const Video::RenderState* late = LateState(source);
    if (late == nullptr) return;
    context._dirty |= RtVideo::kDirtyRenderState;
    context._renderState = late;
}

}  // namespace TerrainLate
}  // namespace Ext
