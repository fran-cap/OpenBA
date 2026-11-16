// ============================================================================
// src/engine/application/application_lifecycle.cpp — Wave 12 lane B
//   Application::~Application, the copy constructor and operator=, plus the
//   three unexported `Collection::Set` bodies they are built out of.
// ============================================================================
//
//   ??1Application@0@QEAA@XZ                 retail 0x18009f520   ord 530
//   ??0Application@0@QEAA@AEBV00@@Z          retail 0x18000a0d0   ord 48
//   ??4Application@0@QEAAAEAV00@AEBV00@@Z    retail 0x18000a160   ord 1050
//
// No Win32 here — PLAN.md §4.1 decision 1. The two `Platform*` members of this
// family (PlatformConstruct, PlatformShutdown) are the seam and live in
// application_cursor_launch.cpp / application_platform.cpp.
//
// ---------------------------------------------------------------------------
// WHAT `this + 0x18` IS, AND WHY THE DESTRUCTOR IS THE DANGEROUS ROW
// ---------------------------------------------------------------------------
// It is the MESSAGE-CALLBACK `Collection::Set` — an AVL tree of
// {unsigned message; int (*)(HWND,UINT,WPARAM,LPARAM)} elements, root at
// +0x18 and count at +0x20 of the Application. That is not inferred from the
// destructor: `src/engine/application/message_callbacks.cpp` already models it
// from the other end (`RegisterMessageCallback` does `LEA RDI,[RCX+0x18]` and
// `INC dword ptr [RDI+8]`), and `src/engine/collection/set_avl.{h,cpp}` pins
// the node header — element 0x00..0x0f, parent +0x10, left +0x18, right +0x20,
// balance +0x28, height +0x2a, 0x30 bytes — from three independent bodies.
//
// A destructor over a TREE that frees only the root is a leak wearing a
// byte-identity claim (look-alike bodies can differ by one destroy pass: an
// earlier wave nearly shipped exactly that). So the recursion was proved from the
// disassembly BEFORE this body was written. `objdump -d -M intel` of
// 0x18000a4f0:
//
//   18000a4f0: test rdx,rdx
//   18000a4f3: je   18000a52c            ; null child -> return
//   18000a4ff: mov  rbx,rdx              ; save the node
//   18000a502: mov  rdx,[rdx+0x18]       ; node->left
//   18000a506: mov  rdi,rcx
//   18000a509: call 18000a4f0            ; *** ITSELF ***
//   18000a50e: mov  rdx,[rbx+0x20]       ; node->right
//   18000a512: mov  rcx,rdi
//   18000a515: call 18000a4f0            ; *** ITSELF ***
//   18000a51a: mov  rdx,rbx
//   18000a51d: call 180009c30            ; free THIS node, after both subtrees
//
// The two `call 0x18000a4f0` at 0x18000a509 and 0x18000a515 are calls to the
// function's own entry address — a POST-ORDER walk of the whole tree, not a
// two-child special case. The destructor at 0x18009f520 then open-codes the
// root step of that same walk (left, right, free root) rather than calling
// 0x18000a4f0 on the root itself, which is why a careless reading sees "two
// calls" and concludes "frees at most three nodes".
//
// EXPORT-TABLE STATUS OF THE TWO HELPERS THE BRIEF NAMED. Checked with pefile
// against the SHIPPED C:\GOG Games\Banished\Runtime-x64.dll export directory,
// not inferred from the `AEAA`/private spelling of anything (AEAA marks a
// private member, and this DLL exports its privates):
//
//   0x18000a4f0  Collection::Set<>::DestroyTree   NOT EXPORTED
//   0x180009c30  the allocator-lock Free wrapper  NOT EXPORTED  (its free is
//                reached here through `rtsetavl::SetNodeFree`, set_avl.cpp,
//                which ignores its first argument and calls
//                `System::MemoryAllocator::MemFree`)
//
// The same check says the five functions this file, application_cursor_launch.cpp
// and application_platform.cpp port ARE all exported (ordinals 530, 48, 1050, 3331,
// 3393).
//
// ---------------------------------------------------------------------------
// WHY THESE THREE SET BODIES ARE HERE AND NOT IN src/engine/collection/
// ---------------------------------------------------------------------------
// set_avl.cpp holds the four bodies that are element-type-AGNOSTIC. Of the
// three added here only DestroyTree (0x18000a4f0) and Clear (0x18000a440) are;
// CloneTree (0x18000a530) is NOT — it calls 0x18000a5b0 by direct rel32, and
// that is the one member of the group message_callbacks.cpp already documents
// as element-type-SPECIFIC (it allocates 0x30 and copies exactly 0x10 of
// element). Splitting the trio across two directories to place two of them
// "correctly" would separate a caller from the callee it is only ever reached
// through. They live with the Application bodies that call them, the way 0x18000a5b0 does.
//
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "application/Application.h"
#include "../collection/set_avl.h"

// ENDGAME B.3.7: `Application::Application` is the real class; a global
// `using RtApplication::Application;` is ill-formed next to the namespace.
using App = Application::Application;
using rtsetavl::Node;
using rtsetavl::Set;

// message_callbacks.cpp's element-specific node allocator, 0x18000a5b0. Called
// by CloneTree exactly as retail calls it: first argument passed through and
// never read, second the element source, third the parent link.
extern "C" Node* RtApp_SetAllocNode(void* unused, const void* src, Node* parent);

// ---------------------------------------------------------------------------
// The Set inside an Application. Retail reaches it as `LEA <reg>,[RCX+0x18]`
// in all three bodies below; the offset is asserted against the header rather
// than repeated as a literal.
// ---------------------------------------------------------------------------
static_assert(offsetof(App, _messageCallbacks) + offsetof(Set, root)
                  == 0x18, "Application's Set::root must land at +0x18");
static_assert(offsetof(App, _messageCallbacks) + offsetof(Set, count)
                  == 0x20, "Application's Set::count must land at +0x20");

namespace {

inline Set* SetOf(App* a) {
    return &a->_messageCallbacks;   // +0x18
}
inline const Set* SetOf(const App* a) {
    return &a->_messageCallbacks;
}

// ---------------------------------------------------------------------------
// THE FREE COUNTER. Incremented once per node this file hands to `SetNodeFree`;
// nothing reads it, and `CountNodes` below has no caller. Not thread-safe.
// ---------------------------------------------------------------------------
uint64_t g_freeCount = 0;

int CountNodes(const Node* n) {
    if (!n) return 0;
    return 1 + CountNodes(n->left) + CountNodes(n->right);
}

}  // namespace

// ============================================================================
// 0x18000a4f0 — Collection::Set<>::DestroyTree(self, node)
// ----------------------------------------------------------------------------
// POST-ORDER recursive destroy. See the banner for the disassembly that proves
// the two inner calls target this function's own entry.
//
// *** THE FIRST PARAMETER IS NEVER DEREFERENCED. *** It is loaded into RDI at
// 0x18000a506, restored into RCX at 0x18000a512, and handed to 0x180009c30 —
// which spills it to its home slot and never reads it (`rtsetavl::SetNodeFree`
// documents the same contract from the other side). It is kept in the signature
// because retail's callers pass two arguments.
// ============================================================================
extern "C" void RtApp_SetDestroyTree(void* self, Node* n) {
    if (!n) return;                              // 0x18000a4f0 TEST/JE
    RtApp_SetDestroyTree(self, n->left);         // 0x18000a509 — itself
    RtApp_SetDestroyTree(self, n->right);        // 0x18000a515 — itself
    ++g_freeCount;
    rtsetavl::SetNodeFree(self, n);              // 0x18000a51d
}

// ============================================================================
// 0x18000a530 — Collection::Set<>::CloneTree(self, src) -> Node*
// ----------------------------------------------------------------------------
// Deep copy, pre-order, parent links written by the PARENT after each child
// clone returns (0x18000a579 / 0x18000a592) rather than by the child itself.
//
// TWO THINGS THAT LOOK LIKE TIDYING AND ARE NOT:
//  * **The allocation result is used UNGUARDED.** 0x18000a54d writes
//    `[RAX+0x2a]` with no null test, so an allocation failure faults here in
//    retail. Reproduced (project rule: do not "fix" a faithful crash with a
//    null check — find the upstream null).
//  * **`height` is stored before `balance`** (0x18000a552 then 0x18000a55f),
//    and both are 16-bit stores. Order is unobservable but transcribed as
//    written so a later reader diffing against objdump sees the same sequence.
//
// The new node's `parent` comes back null from 0x18000a5b0 (R8 is zeroed at
// 0x18000a53f) and is overwritten by the caller for every node except the root
// of the clone — whose parent the CALLER of CloneTree leaves null, because the
// copy constructor and operator= store the returned pointer straight into
// `Set::root` without touching +0x10. Retail's; kept.
// ============================================================================
extern "C" Node* RtApp_SetCloneTree(void* self, const Node* src) {
    Node* n = RtApp_SetAllocNode(self, src, nullptr);   // 0x18000a548
    n->height  = src->height;                           // 0x18000a54d/0x18000a552
    n->balance = src->balance;                          // 0x18000a557/0x18000a55f

    if (src->left) {                                    // 0x18000a564/0x18000a56b
        Node* c = RtApp_SetCloneTree(self, src->left);  // 0x18000a570
        n->left   = c;                                  // 0x18000a575
        c->parent = n;                                  // 0x18000a579
    }
    if (src->right) {                                   // 0x18000a57d/0x18000a584
        Node* c = RtApp_SetCloneTree(self, src->right); // 0x18000a589
        n->right  = c;                                  // 0x18000a58e
        c->parent = n;                                  // 0x18000a592
    }
    return n;                                           // 0x18000a59b
}

// ============================================================================
// 0x18000a440 — Collection::Set<>::Clear(set)
// ----------------------------------------------------------------------------
// The root step of the post-order walk, open-coded exactly as the destructor
// open-codes it, then `root = 0; count = 0`. Both exit paths (empty tree at
// 0x18000a485, non-empty at 0x18000a472) write ROOT FIRST and then the count —
// the opposite order to the destructor, which writes the count first. Nothing
// reads either between the two stores; both orders are transcribed as written.
// ============================================================================
extern "C" void RtApp_SetClear(Set* set) {
    Node* root = set->root;                       // 0x18000a44a
    if (root) {                                   // 0x18000a450/0x18000a453
        RtApp_SetDestroyTree(set, root->left);    // 0x18000a459
        RtApp_SetDestroyTree(set, root->right);   // 0x18000a465
        ++g_freeCount;
        rtsetavl::SetNodeFree(set, root);         // 0x18000a46d
    }
    set->root  = nullptr;                         // 0x18000a474 / 0x18000a48c
    set->count = 0;                               // 0x18000a477 / 0x18000a48f
}

// ============================================================================
// Application::Application::~Application()      retail 0x18009f520   ord 530
// ??1Application@0@QEAA@XZ
// ----------------------------------------------------------------------------
//   Set* set = this + 0x18;                     ; LEA RBX,[RCX+0x18]
//   Node* root = set->root;                     ; MOV RDI,[RBX]
//   if (root) {
//       DestroyTree(set, root->left);           ; RDX = [RDI+0x18]
//       DestroyTree(set, root->right);          ; RDX = [RDI+0x20]
//       SetNodeFree(set, root);                 ; RDX = RDI
//   }
//   set->count = 0;                             ; MOV [RBX+0x8],EAX  (dword)
//   set->root  = nullptr;                       ; MOV [RBX],RAX      (qword)
//
// IT DOES NOT CALL Clear(0x18000a440) — it inlines it, with the two zero stores
// in the opposite order. Written out the same way; sharing the helper would be
// equivalent today and would silently diverge if either copy ever changed
// (the same reasoning message_callbacks.cpp gives for retail's duplicated
// empty-tree allocation).
//
// IT IS THE INVERSE OF THE CONSTRUCTOR ONLY FOR THE SET. `??0Application@0@`
// (0x18009f590, `Ctor_Application` in src/engine/engine/singleton_ctors.cpp) zeroes +0x18/+0x20,
// zeroes +0x28, publishes `_singleton`, zeroes +0x00, stores the HINSTANCE at
// +0x08 and writes the 0x0100 word at +0x10. The destructor undoes EXACTLY ONE
// of those: the Set. It does NOT clear `_singleton`, does not null the window
// or platform handle, and does not touch +0x28. That asymmetry is retail's —
// `Engine::Shutdown` (engine_lifecycle.cpp:853) calls this and then MemFree's
// the block, so the stale `_singleton` pointer is left dangling on purpose and
// nothing reads it again. Stated here rather than "fixed", because a port that
// helpfully nulled the singleton would diverge from retail on a path no oracle
// covers.
//
// LIVE ROUTE: `Engine::Shutdown` calls this directly
// (src/engine/engine/engine_lifecycle.cpp:853).
// ============================================================================
extern "C" void RtApp_Dtor(App* self) {

    Set* set = SetOf(self);                       // 0x18009f538
    Node* root = set->root;                       // 0x18009f541

    if (root) {                                   // 0x18009f544/0x18009f547
        RtApp_SetDestroyTree(set, root->left);    // 0x18009f550
        RtApp_SetDestroyTree(set, root->right);   // 0x18009f55c
        ++g_freeCount;
        rtsetavl::SetNodeFree(set, root);         // 0x18009f564
    }
    set->count = 0;                               // 0x18009f56b  (count FIRST)
    set->root  = nullptr;                         // 0x18009f56e

}

// ============================================================================
// Application::Application::Application(const Application&)
// ??0Application@0@QEAA@AEBV00@@Z              retail 0x18000a0d0   ord 48
// ----------------------------------------------------------------------------
// A field-by-field copy with a DEEP copy of the Set, and a self-copy guard that
// compares the two SET addresses rather than the two object addresses
// (`CMP R14,RDI` at 0x18000a122, where R14 = other+0x18 and RDI = this+0x18 —
// an equivalent test, transcribed as written).
//
// WHAT IT DOES NOT COPY, and this is the interesting part: +0x12..+0x17,
// +0x24..+0x27 and +0x29..+0x2f are never touched. It copies +0x00, +0x08, the
// two bytes at +0x10 and +0x11 SEPARATELY (two MOVZX/MOV byte pairs, not the
// ctor's single word store), the Set, and the byte at +0x28. Seventeen of the
// object's 0x30 bytes are therefore whatever the destination held. Faithful.
//
// The destination Set is zeroed BEFORE the guard (0x18000a11c/0x18000a11f), so
// a self-copy leaves an EMPTY set and LEAKS the tree that was there. Retail's.
//
// ============================================================================
extern "C" App* RtApp_CopyCtor(App* self, const App* other) {

    self->_windowHandle   = other->_windowHandle;    // 0x18000a0f1/0x18000a0f4
    self->_platformHandle = other->_platformHandle;  // 0x18000a0f7/0x18000a0fb
    self->_cursorEnabled       = other->_cursorEnabled;        // 0x18000a0ff/0x18000a103
    self->_running        = other->_running;         // 0x18000a106/0x18000a10a

    const Set* src = SetOf(other);                   // 0x18000a10d  LEA R14
    Set*       dst = SetOf(self);                    // 0x18000a111  LEA RDI

    dst->root  = nullptr;                            // 0x18000a11c
    dst->count = 0;                                  // 0x18000a11f

    if (src != dst) {                                // 0x18000a122/0x18000a125
        if (src->root)                               // 0x18000a127/0x18000a12a
            dst->root = RtApp_SetCloneTree(dst, src->root);   // 0x18000a132/0x18000a137
        dst->count = src->count;                     // 0x18000a13a/0x18000a13e
    }

    self->_quitRequested = other->_quitRequested;    // 0x18000a141/0x18000a145

    return self;                                     // 0x18000a148
}

// ============================================================================
// Application::Application::operator=(const Application&)
// ??4Application@0@QEAAAEAV00@AEBV00@@Z        retail 0x18000a160   ord 1050
// ----------------------------------------------------------------------------
// The copy constructor with one difference that matters: instead of zeroing the
// destination Set it CLEARS it — `if (dst->root) Clear(dst)` at
// 0x18000a1a4..0x18000a1ad — so the tree already there is freed rather than
// leaked. Everything else is the same sequence, including the same
// set-address self-assignment guard and the same untouched bytes.
//
// ONE ASYMMETRY WORTH NAMING. If `dst->root` is null the Clear is SKIPPED, so
// `dst->count` is NOT zeroed on that path; it is then overwritten by
// `dst->count = src->count` inside the same `if`. The only way to observe the
// difference is the state message_callbacks.cpp documents as reachable — a
// failed first registration leaves `count == 1` with `root == nullptr` — and
// even there the count is immediately replaced. Transcribed as written.
//
// ============================================================================
Application::Application& Application::Application::operator=(const Application& otherRef) {
    const Application* other = &otherRef;

    _windowHandle   = other->_windowHandle;    // 0x18000a175/0x18000a180
    _platformHandle = other->_platformHandle;  // 0x18000a183/0x18000a18a
    _cursorEnabled       = other->_cursorEnabled;        // 0x18000a18e/0x18000a195
    _running        = other->_running;         // 0x18000a198/0x18000a19c

    const Set* src = SetOf(other);                   // 0x18000a178  LEA R14
    Set*       dst = SetOf(this);                    // 0x18000a17c  LEA RBX

    if (src != dst) {                                // 0x18000a19f/0x18000a1a2
        if (dst->root)                               // 0x18000a1a4/0x18000a1a8
            RtApp_SetClear(dst);                     // 0x18000a1ad
        if (src->root)                               // 0x18000a1b2/0x18000a1b8
            dst->root = RtApp_SetCloneTree(dst, src->root);   // 0x18000a1bd/0x18000a1c2
        dst->count = src->count;                     // 0x18000a1c5/0x18000a1c9
    }

    _quitRequested = other->_quitRequested;    // 0x18000a1cc/0x18000a1da

    return *this;                                    // 0x18000a1dd
}
