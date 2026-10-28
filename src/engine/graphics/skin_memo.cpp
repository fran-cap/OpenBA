// ============================================================================
// src/engine/graphics/skin_memo.cpp -- SKINMEMO: the memo's entry (skin_memo.h)
// ============================================================================
//
// NOT RETAIL. Faithful perf work (docs/extensions/handins/SKINMEMO.md).
// `SkinMemo::Bones` stands in front of `MeshInstance::GetBones` (retail
// 0x180100070, animationstate_transforms.cpp) at its three callers
// (`MeshInstance::Draw`, `EdgeMeshInstance::Draw`, `MeshInstance::
// SetSharedInstanceData`) while a `Renderer::Draw` is running. On a miss it
// runs GetBones itself, into the slot's region; on a hit it hands the caller the
// region another call filled earlier in the same Renderer::Draw from the same
// inputs. Either way the caller passes the pointer straight to
// `Context::SetBones(count, ...)`, which copies exactly `count` matrices.
// ============================================================================

#include "graphics/skin_memo.h"

#include <cstdint>
#include <cstring>
#include <vector>

#if defined(__x86_64__) || defined(_M_X64)
#include <x86intrin.h>
#endif

#include "math/Math.h"
#include "graphics/Mesh.h"
#include "graphics/GraphicsMesh.h"
#include "graphics/AnimationManager.h"
#include "graphics/AnimationState.h"
#include "graphics/InterpolatedState.h"

namespace SkinMemo {

namespace {

uint64_t Now() {
#if defined(__x86_64__) || defined(_M_X64)
    return (uint64_t)__rdtsc();
#else
    return 0;
#endif
}

// The pose array GetBones reads: a literal copy of its selection
// (animationstate_transforms.cpp, `MeshInstance::GetBones`, the `interp` block).
// If that selection changes, this must change with it; the COMPPROF=4 shadow
// would show the difference as a mismatch.
const Graphics::InterpolatedState<Math::Transform>*
PoseArray(const Graphics::MeshInstance& m) {
    // EXT SEAM (NOT RETAIL, docs/android/P13_OVERLAP.md §2.3): GetBones' frozen-copy branch.
    if (m._animation._instanced && Graphics::MeshInstance::_extInstancedState != nullptr) {
        if (const Graphics::InstancedAnimationState* frozen = Graphics::MeshInstance::_extInstancedState(m))
            return frozen->_nodes._data;
    }
    if (m._animation._instanced) {
        const int32_t handle =
            m._animation._layers._data[m._animation._layers._size - 1]._handle;
        const Graphics::AnimationManager::Instance& inst =
            Graphics::AnimationManager::_singleton->_instances._data[(handle >> 8) & 0xff];
        return inst._states._data[handle & 0xff]._nodes._data;
    }
    return m._nodes._data;
}

// Candidate 2's census (profile only): every node's two halves bit-identical.
bool AllStill(const Graphics::InterpolatedState<Math::Transform>* p, int32_t count) {
    for (int32_t k = 0; k < count; ++k)
        if (std::memcmp(&p[k]._current, &p[k]._previous, sizeof(Math::Transform)) != 0) return false;
    return true;
}

// The shadow's reference buffer (a hit recomputed with the faithful body).
std::vector<Math::Matrix> s_ref;

}  // namespace

const Math::Matrix* Bones(const Graphics::MeshInstance& src, float t,
                          const Math::Transform& root, int32_t capacity, int32_t* countOut) {
    const Graphics::GraphicsMesh::NodeArray& meshNodes = src.GetMesh().Get()->GetNodes();
    const int32_t count = meshNodes._size;
    if (g_depth <= 0 || count <= 0 || count > capacity || count > kMaxBones) {
        ++g_declines;
        return nullptr;
    }

    const bool instanced = src._animation._instanced != 0;
    const Graphics::InterpolatedState<Math::Transform>* const interp = PoseArray(src);

    Key key;
    key.interp    = interp;
    key.binds     = meshNodes._data;
    key.count     = count;
    key.instanced = instanced ? 1u : 0u;
    key.t         = Bits(t);
    if (instanced) {
        for (uint32_t& r : key.root) r = 0;   // GetBones ignores the root here
    } else {
        static_assert(sizeof(Math::Transform) == sizeof(key.root), "seven dwords");
        std::memcpy(key.root, &root, sizeof(key.root));
    }

    ++g_calls;
    if (instanced) ++g_callsInstanced;

    const uint64_t t0 = g_prof ? Now() : 0;
    const uint64_t h = HashKey(key);
    const int32_t slot = g_table.SlotOf(h);
    *countOut = count;

    if (g_table.Live(slot, key, g_epoch)) {
        ++g_hits;
        if (instanced) ++g_hitsInstanced;
        g_bonesSaved += (uint64_t)count;
        Math::Matrix* const memo = g_table.Region(slot);
        if (g_prof) g_hitTicks += Now() - t0;
        if (g_verify) {
            // SkinBones: the faithful body on the live inputs, all `count`
            // matrices compared bit for bit. The game gets the reference.
            if (s_ref.size() < (size_t)kMaxBones) s_ref.resize((size_t)kMaxBones);
            const int32_t n = src.Graphics::MeshInstance::GetBones(t, root, s_ref.data());
            int32_t firstBad = -1;
            for (int32_t k = 0; k < n && firstBad < 0; ++k)
                if (std::memcmp(&s_ref[(size_t)k], &memo[k], sizeof(Math::Matrix)) != 0) firstBad = k;
            const bool ok = n == count && firstBad < 0;
            if (g_report) g_report(ok, ((long long)(n != count) << 16) | (firstBad < 0 ? 0xffff : firstBad));
            return s_ref.data();
        }
        return memo;
    }

    ++g_misses;
    if (instanced) ++g_missesInstanced;
    if (g_table.Occupied(slot, g_epoch)) ++g_evictions;
    g_bonesComputed += (uint64_t)count;
    if (g_prof && !instanced && AllStill(interp, count)) ++g_stillUnique;
    Math::Matrix* const out = g_table.Claim(slot, key, g_epoch);
    src.Graphics::MeshInstance::GetBones(t, root, out);
    if (g_prof) g_missTicks += Now() - t0;
    return out;
}

}  // namespace SkinMemo
