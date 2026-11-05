// ============================================================================
// src/engine/graphics/xform_memo.cpp -- XFORMMEMO: the memo's entry (xform_memo.h)
// ============================================================================
//
// NOT RETAIL. Faithful perf work (issue #253). `XformMemo::Transform` stands in
// front of Geometry slot 6 `GetInstanceTransform` at its two callers (the
// instancing loop of `Renderer::DrawItemList`, batchc_gfx_leaves.cpp, and of
// ext.draw_batch's copy) while a `SkinMemo::Scope` is open. On a miss it runs
// the virtual call itself, into the slot's matrix; on a hit it hands the caller
// the matrix an earlier call of the same Renderer::Draw produced from the same
// inputs. Either way the caller passes the pointer straight to
// `Context::SetTransform`, which copies the 64 bytes.
//
// Retail passes the caller's stack `view` matrix as the hidden return slot, so
// after the loop `view` holds the last transform. No caller reads `view` after
// `SetCamera` (both loops end in Geometry::Draw, which takes the cull camera),
// so a hit that leaves it untouched changes nothing.
// ============================================================================

#include "graphics/xform_memo.h"

#include <cstdint>
#include <cstring>
#include <typeinfo>

#include "math/Math.h"
#include "graphics/Geometry.h"
#include "graphics/Mesh.h"
#include "graphics/skin_memo.h"   // the scope and its epoch

namespace XformMemo {

const Math::Matrix* Transform(const Graphics::Geometry& geometry, int32_t packed, float t,
                              Math::Matrix* scratch) {
    // Only an exact MeshInstance: its body is the pure function the key covers
    // (EdgeMeshInstance and ParticleInstance read other state).
    if (SkinMemo::g_depth <= 0 || typeid(geometry) != typeid(Graphics::MeshInstance)) {
        ++g_declines;
        return geometry.GetInstanceTransform(scratch, packed, t);
    }

    ++g_calls;
    const uint32_t tb = Bits(t);
    const uint64_t epoch = SkinMemo::g_epoch;
    const int32_t home = g_table.SlotOf(HashKey(&geometry, packed, tb));
    int32_t s = home;
    bool evicts = false;

    if (const Math::Matrix* const hit = g_table.Find(home, &geometry, packed, tb, epoch, &s, &evicts)) {
        ++g_hits;
        if (g_verify) {
            // XformMemo: the virtual call on the live inputs, all 64 bytes
            // compared. The game gets the reference.
            const Math::Matrix* const ref = geometry.GetInstanceTransform(scratch, packed, t);
            const bool ok = std::memcmp(ref, hit, sizeof(Math::Matrix)) == 0;
            if (g_report) g_report(ok, static_cast<long long>(packed));
            return ref;
        }
        return hit;
    }

    ++g_misses;
    if (evicts) ++g_evictions;
    Math::Matrix* const out = g_table.Claim(s, &geometry, packed, tb, epoch);
    return geometry.GetInstanceTransform(out, packed, t);
}

}  // namespace XformMemo
