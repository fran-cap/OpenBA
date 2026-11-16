#pragma once
// ============================================================================
// src/engine/graphics/scene_ext.h -- NOT RETAIL: the engine end of the
// ext.draw_gather seams (docs/extensions/handins/DRAWGATHER.md)
// ============================================================================
// DESIGN.md hook rule 5: an engine-side seam is a null-by-default static the
// game side sets; src/engine never includes src/ext. Graphics::Scene carries
// four of them (Scene.h, all null by default):
//
//   _extClean          Scene::Clean's whole body, replaced (one null test at the top)
//   _extCulledGather   Scene::AddCulledDrawItems' whole body, replaced (ditto)
//   _extAddLog         the log below, which Scene::AddObject appends to
//   _extDrawPass1End   CULLPAR (ext.gather_threads): Renderer::Draw calls it after
//                      pass 1 so the gather helpers park (null unless they exist)
//
// SceneAddLog: every node id Scene::AddObject hands out while the extension is
// on, with its scene. The extension's side array (src/ext/draw_gather) keeps a
// copy of each grid node's world box; Geometry::Clean is the box's only writer
// on a live geometry, and Scene::AddObject is the one caller of it the
// extension's own Clean does not see -- so an id in this log is "refresh this
// node's record before trusting it". AddObject also runs on the Engine::Loader
// thread (world build) while the main thread drains the log (menu / loading
// screen draws), hence the spin lock; past the capacity the log only raises
// `overflow`, which the drain reports as "forget every record".
//
// Not a retail layout: nothing here is pinned, nothing is serialized.
// ============================================================================

#include <atomic>
#include <cstdint>

namespace Graphics {

class Scene;

class SceneAddLog {
public:
    static constexpr int kCapacity = 4096;

    struct Entry {
        const Scene* scene;
        uint32_t     id;
    };

    void Note(const Scene* scene, uint32_t id) {
        Lock();
        if (_count < kCapacity) _entries[_count++] = Entry{ scene, id };
        else                    _overflow = true;
        Unlock();
    }

    // Hands every entry to `f(const Entry&)` and then `overflow` (once, as
    // `g(bool)`), and empties the log -- all under the lock.
    template <class F, class G>
    void Drain(F&& f, G&& g) {
        Lock();
        for (int32_t i = 0; i < _count; ++i) f(_entries[i]);
        g(_overflow);
        _count    = 0;
        _overflow = false;
        Unlock();
    }

    int32_t PendingUnlocked() const { return _count; }   // tests / diagnostics only

private:
    void Lock()   { while (_lock.test_and_set(std::memory_order_acquire)) {} }
    void Unlock() { _lock.clear(std::memory_order_release); }

    std::atomic_flag _lock = ATOMIC_FLAG_INIT;
    int32_t          _count = 0;
    bool             _overflow = false;
    Entry            _entries[kCapacity];
};

}  // namespace Graphics
