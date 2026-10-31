// ============================================================================
// src/ext/render_pipe/shadow.cpp -- RENDERPIPE: the RenderPipe shadow's hash
// ============================================================================
// NOT RETAIL. ext.render_pipe, BANISHED_STRESS_COMPPROF=4 only
// (docs/extensions/handins/RENDERPIPE0.md section 6).
//
// What pass 2 of a held packet reads, hashed FIELD BY FIELD (so every pad --
// RendererDrawItem +0x24 / +0x34 (SORTMEMO section 1.4), RendererDrawable +0x0c,
// RendererSceneEntry +0xfc, Camera +0x61..+0x63 / +0xd5..+0xd7, Geometry +0x14 /
// +0xbc -- is left out by construction, and no cast is needed):
//   * the Renderer's lists: every scene entry (its camera, scalars, drawable
//     ids), every used draw group (cameras, shader, mask, the three item lists),
//     every drawable;
//   * for every draw item's Geometry: its header +0x28..+0xbb (detail id, dirty
//     flags, material mask, the three boxes, the opacity pair, the transform
//     pair); a MeshInstance's node pairs, root offset, colour and -- when its
//     animation is instanced -- the shared pose GetBones picks; a
//     ParticleInstance's live count and live records;
//   * the SunLight's eleven Set dwords.
// The main thread hashes at hand-off; the emit re-hashes before and after the
// packet. A difference names a writer between the gather and the emit.
// ============================================================================
#include "ext/render_pipe/render_pipe_internal.h"
#include "ext/render_pipe/snapshot.h"

#include <cstdint>
#include <cstring>
#include <functional>   // std::hash<const void*>: a pointer's value without a cast

#include "graphics/AnimationManager.h"
#include "graphics/AnimationState.h"
#include "graphics/Camera.h"
#include "graphics/Geometry.h"
#include "graphics/GraphicsMesh.h"
#include "graphics/Mesh.h"
#include "graphics/ParticleInstance.h"
#include "graphics/SunLight.h"

namespace Ext {
namespace RenderPipe {
namespace {

class Hasher {
public:
    void U64(uint64_t v) { _h = (_h ^ v) * 0x100000001B3ull; _h ^= _h >> 29; }
    void I32(int32_t v)  { U64(static_cast<uint64_t>(static_cast<uint32_t>(v))); }
    void U32(uint32_t v) { U64(v); }
    void F(float f)      { uint32_t u; std::memcpy(&u, &f, 4); U64(u); }
    void P(const void* p) { U64(static_cast<uint64_t>(std::hash<const void*>{}(p))); }
    // A pointer the snapshot replaces by its copy's (a Geometry, a particle pool, a pose array):
    // left out when the live frame and its copies are compared (P13_OVERLAP.md).
    void Own(const void* p) {
        if (ownPointers) P(p);
    }
    uint64_t Value() const { return _h; }
    bool ownPointers = true;

    void V3(const Math::Vector3& v) { F(v.x); F(v.y); F(v.z); }
    void V4(const Math::Vector4& v) { F(v.x); F(v.y); F(v.z); F(v.w); }
    void Box(const ::Box& b) { V3(b.min); V3(b.max); }
    void Xf(const Math::Transform& t) {
        F(t.rotation.x); F(t.rotation.y); F(t.rotation.z); F(t.rotation.w); V3(t.translation);
    }
    void XfPair(const Graphics::InterpolatedState<Math::Transform>& s) { Xf(s._current); Xf(s._previous); }
    void Camera(const Graphics::Camera& c) {
        for (const Math::Vector4& plane : c._frustum.planes) V4(plane);
        U32(c._frustum.isOrthographic ? 1u : 0u);
        const Math::Transform3& t = c._cameraToWorld;
        V3(t.axisX); V3(t.axisY); V3(t.axisZ); V3(t.translation);
        for (float m : c._projection.m) F(m);
        U32(c._zUp);
    }

private:
    uint64_t _h = 0xCBF29CE484222325ull;
};

// What MeshInstance::GetBones reads (animationstate_transforms.cpp:701): the
// instanced pose array when `_animation._instanced`, the instance's own nodes
// otherwise; `count` = the mesh's node count.
void HashMesh(Hasher& h, const Graphics::MeshInstance& mi) {
    for (int32_t i = 0; i < mi._nodes._size; ++i) h.XfPair(mi._nodes._data[i]);
    h.V3(mi._rootAnimationOffset);
    for (float c : mi._color) h.F(c);
    h.I32(mi._sortBias);
    h.U32(mi._animation._instanced ? 1u : 0u);
    if (!mi._animation._instanced || mi._animation._layers._size <= 0) return;
    const Graphics::GraphicsMesh* const mesh = mi._mesh.Get();
    if (mesh == nullptr) return;
    const int32_t count = mesh->GetNodes()._size;
    const int32_t handle = mi._animation._layers._data[mi._animation._layers._size - 1]._handle;
    if (handle < 0) return;
    // P13_OVERLAP: a frozen copy's pose is its own record (snapshot.h).
    const Graphics::InstancedAnimationState* const frozen = FrozenInstancedState(mi);
    const Graphics::InterpolatedState<Math::Transform>* pose = nullptr;
    if (frozen != nullptr) {
        pose = frozen->_nodes._data;
    } else {
        const Graphics::AnimationManager::Instance& inst =
            Graphics::AnimationManager::_singleton->_instances._data[(handle >> 8) & 0xff];
        pose = inst._states._data[handle & 0xff]._nodes._data;
    }
    h.Own(pose);
    for (int32_t k = 0; k < count; ++k) h.XfPair(pose[k]);
}

void HashParticles(Hasher& h, const Graphics::ParticleInstance& pi) {
    const RtGraphics::ParticleBoundsRecord* const pool = pi._boundsPtr;
    h.Own(pool);
    if (pool == nullptr) return;
    h.I32(pool->_liveCount);
    for (int32_t i = 0; i < pool->_liveCount && i < pool->_records._size; ++i) {
        const RtGraphics::ParticleRecord& r = pool->_records._data[i];
        h.U64(static_cast<uint64_t>(r._lifetime));
        h.U64(static_cast<uint64_t>(r._age));
        h.V3(r._pos); h.V3(r._posPrev); h.V3(r._color); h.V3(r._colorPrev);
        h.F(r._alpha); h.F(r._alphaPrev); h.F(r._rot); h.F(r._rotPrev);
        h.F(r._size); h.F(r._sizePrev);
    }
}

void HashGeometry(Hasher& h, const Graphics::Geometry* g) {
    h.Own(g);
    if (g == nullptr) return;
    h.I32(g->_detailId);
    h.U32(g->_dirtyFlags);
    h.U32(g->_materialTypeFlags);
    h.Box(g->_localBounds);
    h.Box(g->_worldBounds);
    h.Box(g->_prevWorldBounds);
    h.F(g->_opacity);
    h.F(g->_opacityPrev);
    h.XfPair(g->_transform);
    if (const auto* mi = dynamic_cast<const Graphics::MeshInstance*>(g)) HashMesh(h, *mi);
    else if (const auto* pi = dynamic_cast<const Graphics::ParticleInstance*>(g)) HashParticles(h, *pi);
}

void HashItem(Hasher& h, const Graphics::RendererDrawItem& it) {
    h.Own(it._geometry);
    h.P(it._material);
    h.P(it._instance);
    h.U32(it._flags);
    h.U32(it._arg5);
    h.F(it._sortKey);
    h.I32(it._arg8);
    h.P(it._userData);
    h.U32(it._arg10);
    HashGeometry(h, it._geometry);
}

}  // namespace

uint64_t HashFrame(const Graphics::Renderer& r, const Graphics::SunLight* sun, bool ownPointers) {
    Hasher h;
    h.ownPointers = ownPointers;
    h.I32(r._sceneGroups._size);
    for (int32_t i = 0; i < r._sceneGroups._size; ++i) {
        const Graphics::RendererSceneEntry& e = r._sceneGroups._data[i];
        h.P(e._scene);
        h.P(e._renderBuffer);
        h.Camera(e._camera);
        h.I32(e._shaderDetail);
        h.I32(e._shader);
        h.U32(e._materialTypeFlags);
        h.I32(e._drawGroupIndex);
        h.I32(e._detailId);
        h.I32(e._drawableIds._size);
        for (int32_t j = 0; j < e._drawableIds._size; ++j) h.I32(e._drawableIds._data[j]);
    }
    h.I32(r._drawGroupsUsed);
    for (int32_t gi = 0; gi < r._drawGroupsUsed && gi < r._drawGroups._size; ++gi) {
        const Graphics::RendererDrawGroup& g = r._drawGroups._data[gi];
        h.P(g._renderBuffer);
        h.Camera(g._camera);
        h.Camera(g._cullCamera);
        h.I32(g._shader);
        h.U32(g._materialTypeFlags);
        for (const auto& list : g._items) {
            h.I32(list._size);
            for (int32_t k = 0; k < list._size; ++k) HashItem(h, list._data[k]);
        }
    }
    h.I32(r._drawables._size);
    for (int32_t i = 0; i < r._drawables._size; ++i) {
        h.P(r._drawables._data[i]._object);
        h.I32(r._drawables._data[i]._value);
    }
    h.P(sun);
    if (sun != nullptr) {
        h.V3(sun->_direction);
        h.V4(sun->_sun);
        h.V4(sun->_ambient);
    }
    return h.Value();
}

}  // namespace RenderPipe
}  // namespace Ext
