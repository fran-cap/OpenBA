// ============================================================================
// src/ext/render_pipe/snapshot.h -- RENDERPIPE: the frozen copies of a packet's
// geometry, so the emit's pass 2 can overlap the next Frame's sim steps
// ============================================================================
// NOT RETAIL. ext.render_pipe (docs/android/P13_OVERLAP.md section 2.2).
//
// At the end of the main half (main thread, the sim quiescent), BuildSnapshot
// copies every distinct Geometry the packet Renderer's draw items reference into
// the slot's arena -- the object's bytes, then the arrays pass 2 reads (pose
// pairs, particle records, weather particles, the decal / road batches) -- and
// points each item at its copy. Pass 2 then runs its unchanged code on copies the
// steps cannot reach. An instanced MeshInstance's pose lives in the
// AnimationManager; its copy carries a frozen record that
// MeshInstance::_extInstancedState hands the four pose lookups (Mesh.h), for the
// thread inside a SnapshotScope of that slot only.
//
// A packet holding a geometry type, a drawable or a WorldGeometry mode the
// snapshot does not cover, or built while ext.terrain_edit is active, is refused:
// the Renderer is left untouched and the frame keeps the fence.
//
// Slot s's arena is rewritten only by the main half that fills Renderer slot s,
// which pipe_state.h never hands out while slot s's packet is pending.
// ============================================================================
#pragma once

#include <cstdint>

namespace Graphics {
class Renderer;
class MeshInstance;
struct InstancedAnimationState;
}

namespace Ext {
namespace RenderPipe {

struct SnapshotResult {
    bool        ok         = false;     // the items now point at copies
    uint32_t    geometries = 0;         // distinct geometries copied
    uint64_t    bytes      = 0;         // arena bytes written
    const char* refused    = nullptr;   // !ok: why (a type name, or the reason)
};

// Main thread, `r` being Renderer slot `slot` right after pass 1.
SnapshotResult BuildSnapshot(int32_t slot, Graphics::Renderer& r, bool terrainEdit);

// The calling thread reads slot `slot`'s copies while the scope lives (the emit of an
// overlapped packet; the main thread's shadow hash of the copies).
class SnapshotScope {
public:
    explicit SnapshotScope(int32_t slot);
    ~SnapshotScope();
    SnapshotScope(const SnapshotScope&) = delete;
    SnapshotScope& operator=(const SnapshotScope&) = delete;
};

// MeshInstance::_extInstancedState: the frozen record of a copy in the calling thread's
// scope, null for anything else.
const Graphics::InstancedAnimationState* FrozenInstancedState(const Graphics::MeshInstance& mi);

// Uninstall (render thread joined): the two arenas are released.
void FreeSnapshots();

}  // namespace RenderPipe
}  // namespace Ext
