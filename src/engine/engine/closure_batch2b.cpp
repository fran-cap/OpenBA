// ============================================================================
// src/engine/engine/closure_batch2b.cpp — cutover closure batch 2b
// ============================================================================
//
//   0x18004da00  Engine_Application_ScalarDeletingDtor   116 B  (unexported)
//
// `~Application` immediately followed by `operator delete(this)` — a SCALAR
// deleting destructor, and NOT a member of the `??_E` vector family (no flags
// argument, no array cookie, no vector branch).
//
// Engine::Shutdown in src/engine/engine/engine_lifecycle.cpp identifies this address
// and works around it by calling the two halves separately
// (`??1Application@0@QEAA@XZ` then MemFree) — with a note that the exported
// destructor "writes the two adjacent zeroes in the opposite order to the
// inlined copy". This file ports the inlined copy itself; nothing calls it
// (Engine::Shutdown still calls the two halves).
// ============================================================================

#include <cstdint>

#include "application/Application.h"   // B3R: RtApp_SetDestroyTree
#include "system/MemoryAllocator.h"    // B3R: rtalloc::FreeLockedIgnoringSelf
#include "video/Context.h"             // Rt_GuardedObjectFree (0x180006a10)

namespace {

// The two Collection::Set primitives the inlined `~Application` uses on its
// member set at +0x18. Both are unexported internals in retail:
//   0x18000a4f0  the RECURSIVE subtree free — `if (n) { rec(n->left);
//                rec(n->right); freeNode(n); }`, left at +0x18 and right at
//                +0x20. It is its own caller (RtApp_SetDestroyTree).
//   0x180009c30  the single-node free.
// B3R: both called by name (application_lifecycle.cpp, allocator.cpp).
void SetFreeSubtree(rtsetavl::Set* set, rtsetavl::Node* node) {
    RtApp_SetDestroyTree(set, node);
}
void SetFreeNode(rtsetavl::Set* set, rtsetavl::Node* node) {
    rtalloc::FreeLockedIgnoringSelf(set, node);
}

}  // namespace

// ============================================================================
// Transcription, from the body:
//
//     set  = this + 0x18;                  // Collection::Set<...>
//     root = *(void**)set;
//     if (root) {
//         SetFreeSubtree(set, root->left);   // root + 0x18
//         SetFreeSubtree(set, root->right);  // root + 0x20
//         SetFreeNode(set, root);
//     }
//     *(void**)(this + 0x18) = 0;          // root  = null
//     *(uint32_t*)(this + 0x20) = 0;       // count = 0   <-- a DWORD
//     operator delete(this);               // 0x180006a10
//     return this;
//
// The ROOT ITSELF is freed with the non-recursive helper after its two subtrees
// — not by handing the root to the recursive one. Same number of frees either
// way; reproduced because it is one fewer call than the tidy version and a
// profile or a watchpoint can see the difference.
//
// The count store is a DWORD at +0x20, not a qword, and the pointer store at
// +0x18 is a qword: the same Set header shape src/engine/io/Persistence.h
// records for its own settings map (root at +0x00, an int32 count at +0x08).
// ============================================================================
extern "C" void* RtEngine_Application_ScalarDeletingDtor(::Application::Application* self) {
    rtsetavl::Set* set = &self->_messageCallbacks;   // this + 0x18
    if (rtsetavl::Node* root = set->root) {
        SetFreeSubtree(set, root->left);             // root + 0x18
        SetFreeSubtree(set, root->right);            // root + 0x20
        SetFreeNode(set, root);
    }
    set->root  = nullptr;
    set->count = 0;
    Rt_GuardedObjectFree(self);                  // 0x180006a10, `operator delete`
    // (This body has no caller: its 0x18004da00 row had no reader.)
    return self;
}
