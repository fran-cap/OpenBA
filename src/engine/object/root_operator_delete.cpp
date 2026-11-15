// ============================================================================
// src/engine/object/root_operator_delete.cpp — B3X W1, the normal-virtual cutover
// ============================================================================
//
// The class-scope `operator delete` of the five hierarchy ROOTS whose retail
// slot 0 is a deleting destructor: `Object::Base`, `Graphics::Geometry`,
// `IO::Stream`, `IO::ReaderWriter` and `Engine::UpdateLoop`.
//
// WHY THEY EXIST. Every class under these roots now declares `virtual ~T()`.
// GCC emits a deleting destructor (D0) for each: the destructor chain, then the
// class's `operator delete`. Retail's slot-0 bodies free with
// `?MemFree@MemoryAllocator@System@@SAXPEAX@Z` (0x1800c7ad0) on `flags & 1` —
// every one of them, whatever the spelling (INVENTORY_A §3) — and so does every
// retail `??3X@Ns@@SAXPEAX@Z` (proved for 88 classes, the UI ones in
// ui_operator_delete.cpp). Without these members
// D0 would call the GLOBAL `::operator delete`, i.e. the CRT `free()`, on
// MemoryAllocator arena storage: silent heap corruption. A class-scope
// `operator delete` is inherited, so one per root covers the tree; a class that
// declares its own (retail's per-class `??3`) hides it with the same body.
//
// Only the unsized form is declared, so D0 calls exactly this function.
//
// REGISTRY-FREE by construction, like internal_class.cpp: the product arm calls
// `System::MemoryAllocator::MemFree` directly (B3R W0-A0; it went through
// `Object::detail::InternalMemFree`, which is that call), and the unit build
// links this file (tests/unit/sources.cmake) with the host arm, `std::free` — the
// same split internal_class.cpp has.
// ============================================================================

#include <cstdlib>

#include "object/TypeInfo.h"
#include "object/Internal.h"
#include "graphics/Geometry.h"
#include "io/Serializer.h"
#include "io/Stream.h"
#include "engine/UpdateLoop.h"
#if !defined(BANISHED_UNIT_TEST)
#include "system/MemoryAllocator.h"
#endif

namespace {

inline void RootFree(void* p) {
#if defined(BANISHED_UNIT_TEST)
    std::free(p);
#else
    System::MemoryAllocator::MemFree(p);   // ?MemFree@MemoryAllocator@System@@SAXPEAX@Z
#endif
}

}  // namespace

void Object::Base::operator delete(void* p)        { RootFree(p); }
void Graphics::Geometry::operator delete(void* p)  { RootFree(p); }
void IO::Stream::operator delete(void* p)          { RootFree(p); }
void IO::ReaderWriter::operator delete(void* p)    { RootFree(p); }
void Engine::UpdateLoop::operator delete(void* p)  { RootFree(p); }
