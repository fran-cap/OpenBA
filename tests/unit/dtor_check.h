#pragma once
// ============================================================================
// tests/unit/dtor_check.h — the destructor facts after the normal-virtual
// cutover (B3X W1, B3X_RULES X4/X5)
// ============================================================================
// Retail slot 0 of every Object::Base / Graphics::Geometry / IO::Stream /
// IO::ReaderWriter / Engine::UpdateLoop class is an MSVC scalar deleting
// destructor `T* (T*, unsigned flags)`: flags bit 0 frees `this` through
// MemoryAllocator::MemFree, bit 1 is the vector arm (count at `this - 8`,
// elements destroyed downward, block freed at `this - 8`). The product now
// declares `virtual ~T()` instead, and GCC spends two slots on it:
//
//   compiler slot 0  D1, the complete-object destructor  == old flags 0
//   compiler slot 1  D0, the deleting destructor         == old flags 1: the
//                    chain, then the CLASS-SCOPE `operator delete` (MemFree)
//
// and there is no vector deleting destructor at all: `delete[]` on a `new T[n]`
// block runs the complete destructors itself, last element first.
//
// What the retired tests proved with a flags argument is proven here the C++
// way:
//   flags 1  `delete p`   — the destructor chain ran once AND a recording
//                           class-scope `operator delete` received `p`;
//   flags 0  `p->~T()`    — the destructor ran once and that operator delete
//                           was NOT called;
//   flags 3  `new T[3]; delete[] a` — three destructors, DOWNWARD
//                           (CheckArrayDelete).
// ============================================================================
#include "unit.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <type_traits>

namespace dtor_check {

// ---------------------------------------------------------------------------
// The recording free. A stand-in's class-scope `operator delete` calls
// NoteFree(p) and then frees the block with the function that allocated it
// (in the unit build InternalMemAlloc / TypeTreeAlloc are std::malloc, so a
// stand-in allocated through them frees with their partner or std::free).
// ---------------------------------------------------------------------------
inline int         g_frees    = 0;
inline const void* g_lastFree = nullptr;
inline void NoteFree(const void* p) { ++g_frees; g_lastFree = p; }
inline void ResetFrees() { g_frees = 0; g_lastFree = nullptr; }

// T's unsized `operator delete` is a CLASS-SCOPE member (the root's or the
// class's own ??3): its D0 never reaches the global `::operator delete`, i.e.
// the CRT free(), on MemoryAllocator storage.
template <class T, class = void>
struct HasClassDelete : std::false_type {};
template <class T>
struct HasClassDelete<T, std::void_t<decltype(static_cast<void (*)(void*)>(&T::operator delete))>>
    : std::true_type {};

// A heap stand-in for a concrete T: T itself, plus a recording class-scope
// `operator delete` that frees with std::free. Allocate it with std::malloc
// (NewOnMalloc below), never with a new-expression.
template <class T>
struct Recording final : T {
    using T::T;
    static void operator delete(void* p) { NoteFree(p); std::free(p); }
};

template <class R, class... Args>
inline R* NewOnMalloc(Args&&... args) {
    void* m = std::malloc(sizeof(R));
    return ::new (m) R(static_cast<Args&&>(args)...);
}

// ---------------------------------------------------------------------------
// The destructor pair read RAW from an object's table (compiler slots 0 and
// 1). D1 on an object means it must never be destroyed again (a stack probe
// is placement-new'd into a buffer for that); D0 needs a heap block and a
// class-scope operator delete that frees it the way it was allocated.
// ---------------------------------------------------------------------------
template <class Fn>
inline Fn CompilerEntry(const void* obj, int i) {
    static_assert(sizeof(Fn) == sizeof(void*), "a plain function pointer");
    const void* raw = (*reinterpret_cast<const void* const* const*>(obj))[i];
    Fn f;
    std::memcpy(&f, &raw, sizeof f);
    return f;
}
using DtorEntry = void (*)(void*);
inline void CallD1(void* obj) { CompilerEntry<DtorEntry>(obj, 0)(obj); }
inline void CallD0(void* obj) { CompilerEntry<DtorEntry>(obj, 1)(obj); }

// ---------------------------------------------------------------------------
// The retired vector arm. `new T[3]; delete[] a;` — Itanium's delete[] runs
// the complete destructor of every element, last one first, and frees the
// block once (through T's `operator delete[]` if it has one, else the global
// one: the product declares none, so both halves are the global pair here).
// `count` / `order` are the test's destructor log: the class's destructor (or
// the stand-in's) must append `this` exactly once per object.
// ---------------------------------------------------------------------------
// Set only by dtor_check_test.cpp while it shows the helper failing on
// purpose: the failure is still counted, but not printed as a FAIL line.
inline bool g_quietExpectedFailure = false;

template <class T, class Order>
inline void CheckArrayDelete(int& count, const Order& order, const char* file, int line) {
    T* a = new T[3];
    const void* e0 = static_cast<const void*>(&a[0]);
    const void* e1 = static_cast<const void*>(&a[1]);
    const void* e2 = static_cast<const void*>(&a[2]);
    count = 0;
    delete[] a;
    if (count != 3 || order[0] != e2 || order[1] != e1 || order[2] != e0) {
        if (!g_quietExpectedFailure) std::printf("  FAIL %s:%d: delete[] of 3: %d destructor runs, want 3 in downward order\n",
                    file, line, count);
        ++unit::Failures();
    }
}

}  // namespace dtor_check

// A stand-in's recording class-scope `operator delete` (heap blocks from
// std::malloc / InternalMemAlloc / TypeTreeAlloc, all malloc in the unit build).
#define DTOR_CHECK_RECORDING_DELETE \
    static void operator delete(void* p) { ::dtor_check::NoteFree(p); std::free(p); }
// The same for an object that lives in a buffer / array / member and must not
// be freed: it records and frees nothing.
#define DTOR_CHECK_RECORDING_NOFREE_DELETE \
    static void operator delete(void* p) { ::dtor_check::NoteFree(p); }

// `delete[]` of a three-element `new T[]` runs three destructors, downward.
#define CHECK_ARRAY_DELETE(T, count, order) \
    dtor_check::CheckArrayDelete<T>((count), (order), __FILE__, __LINE__)
