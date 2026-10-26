#pragma once
// src/engine/object/class_vptr.h -- a class's own vptr, recorded by its constructor.
//
// A few loops tell an object's exact class by its vptr: one load and a compare, with no
// virtual call on a line the loop has only prefetched. The readers are the staged walk
// prefetch (entitylist.cpp: CharacterComponent, ModelComponent, ParticleComponent),
// ext.walk_batch (walk_batch.cpp: MoveTo), the gather's dispatch prefetch (cull.cpp:
// Graphics::MeshInstance) and the splash's "is the next loop a GameUpdateLoop" test
// (splash_loop_stubs.cpp, GameUpdateLoop carries no type id).
//
// C++ cannot name a vtable, so the class's constructor records the vptr it has just been
// given: inside T's constructor body an object's vptr is T's own table, whatever the
// most-derived class. NOT RETAIL: retail has no such store; it changes no object and no
// result. Before the first construction the value is null, which no object's vptr equals,
// and that is the right answer: no object of the class exists yet. An object reaches a
// reader on another thread through a hand-off that already orders the store before the
// read, so the accesses are relaxed.
//
// This replaced DEFINE_EXE_VTABLE (2026-10-26), which named the table by its assembler
// symbol and had to be kept out of link-time optimisation.

#include <atomic>

namespace Object {

template <class T>
class ClassVptr {
public:
    // Called in T's constructor body. The load first: every construction after the first
    // stores nothing, so the shared line is only read.
    static void Record(const T* self) {
        const void* const vptr = *reinterpret_cast<const void* const*>(self);   // CAST-OK: vslot (the vptr T's ctor just set)
        if (s_vptr.load(std::memory_order_relaxed) != vptr) s_vptr.store(vptr, std::memory_order_relaxed);
    }

    // T's table, or null while no T has been constructed.
    static const void* Get() { return s_vptr.load(std::memory_order_relaxed); }

private:
    static inline std::atomic<const void*> s_vptr{nullptr};
};

}  // namespace Object
