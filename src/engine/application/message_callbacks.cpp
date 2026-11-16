// ============================================================================
// src/engine/application/message_callbacks.cpp — Wave 9 lane VID2
//   Application::{Register,Unregister}MessageCallback and the one
//   element-type-specific AVL helper they own.
// ============================================================================
//
// WHY THESE TWO
// -------------
// `?PlatformStartup@Device@Video@@AEAA_NXZ` (src/engine/video/device_startup.cpp)
// registers two USER32 message callbacks as its last act; Input
// (src/engine/input/input_platform_startup.cpp, input_platform_shutdown.cpp) and
// the DX11 `DeviceData` (src/render/dx11/devicedata.cpp) register and remove
// their own.
//
// | Export | Retail | Ord |
// |---|---|---|
// | `?RegisterMessageCallback@Application@1@QEAAXIP6AHPEAUHWND__@@I_K_J@Z@Z` | 0x18009fa60 | 3530 |
// | `?UnregisterMessageCallback@Application@1@QEAAXI@Z` | 0x18009fa20 | 4467 |
//
// Both names were checked against the shipped export directory with pefile
// BEFORE any body was written.
//
// THE SPLIT WITH set_avl.cpp
// --------------------------
// The four generic AVL bodies (retrace, two rotations, erase) never touch the
// element and are shared by every `Collection::Set` instantiation with a
// matching node header, so they live in src/engine/collection/set_avl.cpp.
// `0x18000a5b0` is the one member of the group that IS element-type-specific —
// it allocates 0x30 bytes and copies exactly 0x10 of element — so it lives
// here, beside RegisterMessageCallback; CloneTree (application_lifecycle.cpp) calls it too.
// ============================================================================

#include <cstddef>
#include <cstdint>

#include "application/Application.h"
#include "../collection/set_avl.h"
#include "system/MemoryAllocator.h"

// ENDGAME B.3.7: `Application::Application` is the real class; a global
// `using RtApplication::Application;` is ill-formed next to the namespace.
using App = Application::Application;
using rtsetavl::Node;
using rtsetavl::Set;

// The two layouts must agree, and this is where that is enforced. The header
// declares the Set as an `rtsetavl::Set` member (`_messageCallbacks`); if either
// side ever moves, this fails at compile time rather than corrupting a live tree.
static_assert(offsetof(App, _messageCallbacks) + offsetof(Set, root)
                  == 0x18, "Application's Set::root must land at +0x18");
static_assert(offsetof(App, _messageCallbacks) + offsetof(Set, count)
                  == 0x20, "Application's Set::count must land at +0x20");

namespace {

// The 16-byte element (Application.h). `0x18000a5b0` copies it as two qword
// loads/stores, so the padding bytes after `message` are copied verbatim from
// the caller's stack rather than zeroed — see AllocNode. The callback type is
// the mangled `P6AHPEAUHWND__@@I_K_J@Z`, Application::MessageCallback.
using Element = App::MessageCallbackEntry;


// ---------------------------------------------------------------------------
// The allocator boundary.
//
// `0x18000a5b0` open-codes `EnterCriticalSection(*0x1801fc718 + 0x640);
// Allocate(*0x1801fc718, 0x30, 8); LeaveCriticalSection(...)`. That triple IS
// `?MemAlloc@MemoryAllocator@System@@SAPEAX_KH@Z` at 0x1800c7a50 — lane Q
// disassembled it and proved the composition instruction for instruction.
// Called by name (B3R).
// ---------------------------------------------------------------------------
void* MemAlloc(uint64_t bytes, int align) {
    return System::MemoryAllocator::MemAlloc(bytes, align);
}

}  // namespace

// ============================================================================
// 0x18000a5b0 — allocate a node, copy the element, link the parent.
// ============================================================================
// *** THE FIRST PARAMETER IS NEVER READ. *** RCX is spilled nowhere and used
// nowhere: the body's first act is `MOV RBP,R8` / `MOV RSI,RDX`, and RCX is
// destroyed at 0x18000a5fa by the allocator singleton load. The call sites
// agree in the stronger form — RegisterMessageCallback does not set RCX at all
// before either call (0x18009fb32 / 0x18009fb45 set only RDX and R8), so
// whatever RCX holds there is leftover. **A register that is not set by the
// caller and not read by the callee is not a parameter.**
//
// This is the same reading, the callee's disassembly deciding which registers are parameters,
// that settles `FUN_180023060`, and lane D's `KeyMap::RegisterInput` is the
// precedent for the resulting signature.
//
// THE ELEMENT COPY IS TWO QWORDS, NOT A FIELD COPY (0x18000a61b..0x18000a625).
// The four bytes at element+0x04 are therefore whatever the caller's stack held
// — RegisterMessageCallback builds the element with `MOV [RAX-0x20],EDX`
// (the message id, a DWORD) and never writes +0x04, so **the padding carries a
// dword of the caller's stack frame into the tree**. Reproduced, not repaired:
// an oracle that compared those bytes would fail 100% of calls, and zeroing them
// would differ from retail in a way nothing here would ever report.
//
// On allocation failure retail returns null and the CALLER stores that null as
// the child link — it does not check. Faithful.
// ============================================================================
extern "C" Node* RtApp_SetAllocNode(void* /*unused, never read*/,
                                    const void* src, Node* parent) {
    Node* n = static_cast<Node*>(MemAlloc(0x30, 8));
    if (!n) return nullptr;

    const uint64_t* s = static_cast<const uint64_t*>(src);  // CAST-OK: elem_stride
    uint64_t* d = reinterpret_cast<uint64_t*>(n);  // CAST-OK: elem_stride
    d[0] = s[0];                       // 0x18000a61b — element +0x00..+0x07
    d[1] = s[1];                       // 0x18000a621 — element +0x08..+0x0f

    n->parent  = parent;               // 0x18000a629
    n->left    = nullptr;              // 0x18000a62f
    n->right   = nullptr;              // 0x18000a633
    n->balance = 0;                    // 0x18000a637 — one DWORD store covering
    n->height  = 0;                    //               both 16-bit words
    return n;
}

// ============================================================================
// ?RegisterMessageCallback@Application@1@QEAAXIP6AHPEAUHWND__@@I_K_J@Z@Z
//                                                      retail 0x18009fa60
// ============================================================================
// Retail's shape, kept exactly:
//
//   1. build the element on the stack (id, callback);
//   2. if the tree is EMPTY: allocate a node INLINE — not through 0x18000a5b0 —
//      store it as the root, and increment the count. **No retrace.**
//   3. otherwise descend, and on a key that is already present RETURN WITHOUT
//      INCREMENTING THE COUNT;
//   4. on reaching a leaf, allocate through 0x18000a5b0, link it as that leaf's
//      left or right child, retrace, and increment the count.
//
// FOUR THINGS THAT LOOK LIKE TIDYING AND ARE NOT:
//
//  * **The empty-tree path duplicates the allocation rather than calling
//    0x18000a5b0.** Retail inlines it at 0x18009fa92..0x18009faf5 with a null
//    parent. Written out here the same way; sharing the helper would be
//    equivalent today and would silently diverge if either copy ever differed.
//  * **The empty-tree path increments the count even when the allocation
//    FAILED** — `XOR EAX,EAX / JMP 0x18009faf8` stores a null root and falls
//    into the same `INC [RDI+8]`. So a failed first registration leaves
//    `count == 1` with `root == nullptr`. Retail's; kept.
//  * **The descent is UNSIGNED.** `CMP EDX,EAX / JC` — JC is below, not less.
//    Message ids are unsigned (0x232 is WM_EXITSIZEMOVE) so a signed
//    compare would order them the same today, but the ids come from callers this
//    lane does not own.
//  * **The retrace is handed the address of a stack local** that the descent
//    keeps updated (`MOV [RSP+0x50],RAX` on every step). It must be a real
//    local, because Retrace re-reads and WRITES through it.
//
// ============================================================================
void Application::Application::RegisterMessageCallback(uint32_t id, MessageCallback callback) {
    App* app = this;
    Set* set = &app->_messageCallbacks;   // retail: this + 0x18

    // The element, built on OUR stack exactly as retail builds it on its own —
    // `message` written, `pad04` left alone. See AllocNode's banner.
    Element elem;                      // deliberately uninitialised
    elem.message  = id;                // 0x18009fa83
    elem.callback = callback;          // 0x18009fa86

    Node* root = set->root;
    if (!root) {
        // ---- empty tree: inline allocation, no retrace -----------------
        Node* n = static_cast<Node*>(MemAlloc(0x30, 8));
        if (n) {
            uint64_t* d = reinterpret_cast<uint64_t*>(n);  // CAST-OK: elem_stride
            const uint64_t* s = reinterpret_cast<const uint64_t*>(&elem);  // CAST-OK: elem_stride
            d[0] = s[0];
            d[1] = s[1];
            n->parent  = nullptr;
            n->left    = nullptr;
            n->right   = nullptr;
            n->balance = 0;
            n->height  = 0;
        }
        set->root = n;                 // 0x18009faf8 — stores null on failure
        set->count += 1;               // 0x18009fb63 — reached either way
        return;
    }

    // ---- descend, tracking the parent in a slot the retrace will rewrite ----
    Node* slot = root;                 // [RSP+0x50]
    for (;;) {
        const unsigned key = *reinterpret_cast<const unsigned*>(slot);  // CAST-OK: elem_stride
        if (id < key) {                            // JC -> left
            Node* l = slot->left;
            if (!l) {
                slot->left = RtApp_SetAllocNode(nullptr, &elem, slot);
                break;
            }
            slot = l;
        } else if (key >= id) {                    // JNC -> equal -> return
            return;                                // count NOT incremented
        } else {
            Node* r = slot->right;
            if (!r) {
                slot->right = RtApp_SetAllocNode(nullptr, &elem, slot);
                break;
            }
            slot = r;
        }
    }

    rtsetavl::Retrace(set, &slot);     // 0x18009fb56
    set->count += 1;                   // 0x18009fb63
}

// ============================================================================
// ?UnregisterMessageCallback@Application@1@QEAAXI@Z    retail 0x18009fa20
// ============================================================================
// Twelve instructions: descend by unsigned key, and on a hit TAIL-JUMP into the
// erase (0x18009fa47 `JMP 0x18000aba0`). A miss returns silently and does NOT
// touch the count — the count is decremented inside the erase, so a miss
// leaving it alone falls out of the structure rather than from a check.
// ============================================================================
void Application::Application::UnregisterMessageCallback(uint32_t id) {
    App* app = this;
    Set* set = &app->_messageCallbacks;   // retail: this + 0x18
    for (Node* n = set->root; n; ) {
        const unsigned key = *reinterpret_cast<const unsigned*>(n);  // CAST-OK: elem_stride
        if (id < key)       n = n->left;    // 0x18009fa41
        else if (key >= id) { rtsetavl::Erase(set, n); return; }  // 0x18009fa47
        else                n = n->right;   // 0x18009fa3b
    }
}
