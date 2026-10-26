// src/engine/graphics/mesh_vtable_alias.cpp -- Graphics::MeshInstance's compiler-vtable alias,
// kept out of LTO (the game-side twin, and the reason, is src/game/Object/exe_vtables.cpp).
//
// The gather's dispatch prefetch (cull.cpp, GATHERPF arm 2) tells a MeshInstance by its
// vptr: equality with the address point below means the dynamic type is exactly
// MeshInstance; only that arm's hints depend on it, so a mismatch only means those hints
// are not issued. The array is the compiler's vtable group (Itanium: offset-to-top,
// typeinfo, then the address point a MeshInstance's vptr holds), reached by its assembler
// name because C++ cannot name a vtable; CMakeLists.txt compiles this file without -flto
// so the optimiser never sees that declaration beside the type.

#include "abi.h"   // RT_ASM_LABEL

extern const void* const GatherPf_MeshInstanceVtableGroup[]
    RT_ASM_LABEL("_ZTVN8Graphics12MeshInstanceE");

namespace Graphics {

const void* GatherPf_MeshInstanceVtable() {
    return &GatherPf_MeshInstanceVtableGroup[2];
}

}  // namespace Graphics
