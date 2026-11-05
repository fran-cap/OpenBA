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
// MeshInstance::_extInstancedState hands the four pose lookups (Mesh.h) while a
// packet with copies is in flight.
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
    // Issue #253: the draw items pass A walked, how many found their geometry already copied
    // (no second copy, no type test) and how many kept the live object (WorldGeometry modes).
    uint32_t    items      = 0;
    uint32_t    reused     = 0;
    uint32_t    kept       = 0;
    // COMPPROF=4 (`verify`): the lean builder was checked against the reference one.
    bool        checked    = false;
    uint32_t    mismatch   = 0;         // bits: 1 result, 2 arena bytes, 4 item pointers
};

// Main thread, `r` being Renderer slot `slot` right after pass 1. `verify` (COMPPROF=4) also
// runs the reference builder (the pre-#253 one: a table probe per item in pass A and pass C)
// on the same items and compares its result, arena bytes and item pointers with the lean one's.
SnapshotResult BuildSnapshot(int32_t slot, Graphics::Renderer& r, bool terrainEdit, bool verify);

// MeshInstance::_extInstancedState: the frozen record of a copy (decided by its address:
// it lies in a slot's arena), null for anything else.
const Graphics::InstancedAnimationState* FrozenInstancedState(const Graphics::MeshInstance& mi);

// Uninstall (render thread joined): the two arenas are released.
void FreeSnapshots();

}  // namespace RenderPipe
}  // namespace Ext
