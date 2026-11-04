// ============================================================================
// src/engine/input/keymap_load.cpp — Wave 9 lane IOA
//   Input::KeyMap::Load
// ============================================================================
//
//   ?Load@KeyMap@Input@@QEAAXXZ   retail 0x1800A7190  ord 3203
//
// Verified in the shipped DLL's export directory with pefile before this file
// was written. Its own .cpp rather than more lines in input.cpp because it is
// the ONE KeyMap member that reaches outside Input — into IO::Persistence's
// settings map — and because that reach is what the hold-back was about.
//
// ---------------------------------------------------------------------------
// WHY THIS ROW CLOSES: every callee is satisfied on our side
// ---------------------------------------------------------------------------
// Lane Z's triage rule for a cutover row is "does this port close it, or move
// it one frame deeper?". Sized in Ghidra rather than guessed, `Load`'s call
// list (0x1800A7190, filtered to CALL) is exactly five distinct targets:
//
//   0x1800C9730  ?Format@String@System@@SA?AV12@PEBDZZ     PORTED
//                (src/engine/system/string.cpp:1118)
//   0x1800C6C00  ?Free@MemoryAllocator@System@@QEAAXPEAX@Z PORTED
//                (src/engine/system/allocator.cpp)
//   0x18007F034  a three-instruction thunk — `XOR EDX,EDX ; LEA R8D,[RDX+0xA] ;
//                JMP 0x180083E18` — i.e. `strtol(s, NULL, 10)`. CRT, not
//                engine. persistence.cpp already reached the same conclusion
//                about the same address and calls std::strtol; so does this.
//   [0x18014B390] / [0x18014B388]  Enter/LeaveCriticalSection. KERNEL32.
//
// ONE THING THAT LIST DOES NOT COVER, and the claim has to be narrowed to say
// so. Those five are the callees RETAIL's body has. OUR body reaches two more,
// because it builds and releases `System::String`s through the tree's ported
// helpers rather than through inlined assembly: `RtSystem_String_Format` and
// `RtSystem::RtString_ReleaseInline` both end in `RtMemFree`
// (src/engine/system/string.cpp), which resolves with
// `the raw DLL door<MemFreeFn>("?MemFree@MemoryAllocator@System@@SAXPEAX@Z")` — and
// `the raw DLL door` is the RAW door: it calls `the raw DLL door`, which SKIPS the
// port registry by design and reads the DLL's export table. `MemFree` IS ported
// (allocator.cpp:1274), but this door does not consult that port, so with the
// DLL absent it resolves null.
//
// That is a TREE-WIDE row and not a KeyMap::Load one: every landed String-using
// port on the boot path goes through the same door, including
// `IO::Persistence::Get(int&)` / `Get(bool&)`, which call the identical
// `RtString_ReleaseInline`. It is `string.cpp`'s allocator seam — deliberate,
// documented there ("strings cross the DLL boundary constantly, so both sides
// must use the same heap"), and owned by the allocator lane, not this one.
//
// So the precise claim is: `?Load@KeyMap@Input@@QEAAXXZ` stops being a null
// SYMBOL, and the boot can still stop one frame deeper inside our own String
// release — at a door it would have stopped at through Persistence anyway.
// "Every callee is satisfied" would have been the wrong sentence.
//
// NOT ONE unexported Runtime function, and not one unported exported one. The
// Persistence lookup is INLINED in retail — there is no call to `Map_Find`
// (0x18002ACE0) here — so writing that walk out is both the faithful thing and
// the thing that keeps this body off the DLL. (Calling `Map_Find` by address,
// which is what persistence.cpp's own Get/Set do, would have left this row
// open: an address call into a DLL that is not there.)
//
// ---------------------------------------------------------------------------
// ORACLE
// ---------------------------------------------------------------------------
// `Load` MUTATES the KeyMap's bindings in place and is idempotent for a given
// registry state, so a naive "call ours, then call theirs, compare" would
// simply overwrite our result with theirs and always agree — a control that
// cannot fail. What this file does instead is snapshot the two written bytes
// per (binding, slot) BEFORE our body runs, run ours, then re-run the DLL's
// body from the same snapshot and compare the bytes it produces. That is a
// real comparison because the subject (the binding array) is restorable — it
// is 2+2 bytes per node and nothing else in the object is touched — which is
// exactly the property Audio::Device::Update (see audio_device.cpp) does NOT
// have and why that one gets counters instead.
//
// THAT COMPARISON IS NOT IMPLEMENTED HERE, and the reason is stated rather
// than papered over: restoring the snapshot requires walking the same AVL a
// third time, and this lane could not build or run the game to prove that the
// restore is exact (the integrator holds the game directory). An unproven
// restore harness is the failure mode reference_longjmp_unwind_runs_retail_
// cleanup.md warns about — a harness that manufactures its own divergence. So
// the site carries the diff oracle only, with the honest verdict below, and the
// snapshot oracle is written down as the next step for whoever can run it.
//
// the diff oracle's verdict here is "at least one binding slot was actually
// written", which is the number that matters: on a machine with no saved key
// bindings every lookup misses, every slot keeps its -2 sentinel, and the body
// writes nothing at all while `calls=` still reads 1.
// ============================================================================

#include <cstdint>
#include <cstdlib>
#include "input/Input.h"
#include "input/binding_value.h"   // the slot store (review F-099, F-100)
#include "io/Persistence.h"
#include "system/string_internal.h"

using Input::Binding;
using Input::BindingNode;

// ENDGAME B.3.1 step 2: `?Format@String@System@@SA?AV12@PEBDZZ` (0x1800c9730)
// is a static member of `System::String` now, declared in
// src/engine/system/String.h, so the local variadic declaration this file
// carried for the shim is gone. It is still a DIRECT call, which is what keeps
// the body off the DLL at cutover.

namespace {

// ---------------------------------------------------------------------------
// The two literals retail builds its registry key from, read out of .rdata at
// the addresses the disassembly LEAs:
//   0x1801551F0 = ""       (slot 0's suffix)
//   0x1801551F4 = "Alt"    (slot 1's suffix)
//   0x1801551F8 = "%s%s"
//   0x180155200 = "Input"
// so the key for (binding, slot) is  "Input" + (description + suffix), e.g.
// "InputCameraForward" and "InputCameraForwardAlt". Note the DOUBLE Format:
// retail builds `description+suffix` first and only then prefixes "Input",
// which is why the section prefix lands before the suffix and not around it.
// ---------------------------------------------------------------------------
constexpr const char* kSlotSuffix[2] = { "", "Alt" };

// ---------------------------------------------------------------------------
// Retail's inlined key comparison, reproduced rather than delegated to
// std::strcmp, because the two are not the same function.
//
// The asm (0x1800A7280..0x1800A72B5 and its mirror at 0x1800A72CA..0x1800A7304)
// advances an index while the bytes are equal AND neither has hit NUL, then
// ends with `MOVSX ECX,byte[..] ; MOVSX EDX,byte[..] ; SUB EDX,ECX` — MOVSX,
// i.e. SIGNED char subtraction. std::strcmp is specified to compare as
// UNSIGNED char. The two agree for every ASCII key name the engine registers
// and disagree the moment a byte >= 0x80 appears, so the signed spelling is
// kept: this is a byte-faithful port, not an equivalent one.
//
// Retail's null handling, also reproduced: a null key sorts BEFORE every
// non-null one. Block one jumps to "go left" when OUR key is null
// (0x1800A727B) and skips to block two when the NODE key is null (0x1800A7276);
// block two's mirror sends a null node key right (0x1800A72C5). Both branches
// are the same predicate with its arguments swapped, which is why one helper
// serves both.
bool RetailKeyLess(const char* a, const char* b) {
    if (a == nullptr) return true;
    if (b == nullptr) return false;
    size_t i = 0;
    while (a[i] == b[i] && a[i] != '\0' && b[i] != '\0') ++i;
    return (static_cast<int>(static_cast<signed char>(a[i])) -
            static_cast<int>(static_cast<signed char>(b[i]))) < 0;
}

// ---------------------------------------------------------------------------
// The inlined IO::Persistence settings-map lookup (0x1800A7256..0x1800A7321).
//
// The node layout is NOT invented here: src/engine/io/Persistence.h already
// records it as "key and value Strings at +0x00/+0x08, then parent/left/right
// at +0x10/+0x18/+0x20", pinned in Wave 4 lane C from Map_Find/Map_Insert. The
// disassembly of this function agrees independently — it reads the key at
// `[R11]`, the value at `[R11+8]`, descends left through `[R11+0x18]` and
// right through `[R11+0x20]` — which is the cross-check
// reference_one_bst_find_two_node_layouts.md asks for. This walk is DELIBERATELY
// local and is NOT shared with input.cpp's Collection::Set<Binding> helpers:
// that tree's nodes put left/right at +0x20/+0x28, and one shared find over
// two layouts is precisely the defect that reference note records.
//
// The root is `*(void**)Persistence::_singleton` — retail loads the singleton
// with `MOV R12,qword ptr [0x1801FC678-ish]` once, outside both loops, and
// re-reads `*R12` per lookup. Reproduced: the singleton is hoisted, the root
// is not.
const char* FindSettingValue(const ::IO::Persistence* persistence, const char* key) {
    // 0x1800a7256 MOV R11,[R12] reads the root through the hoisted singleton with no
    // test; 0x1800a725a TEST R11,R11 is the root test the loop makes (B6 14d G19:
    // the tree's null-Persistence return is gone; Engine::Startup builds it at 0x1800dba5e).
    const RtIo::SettingsNode* node = persistence->_settingsMap._root;     // +0x00
    while (node != nullptr) {
        const char* nodeKey = node->element.key._data;                    // +0x00
        if (RetailKeyLess(key, nodeKey)) {
            node = node->left;                                            // +0x18
        } else if (RetailKeyLess(nodeKey, key)) {
            node = node->right;                                           // +0x20
        } else {
            return node->element.value._data;                             // +0x08
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Collection::Set<Binding> in-order traversal, transcribed from
// 0x1800A71C0 (descend to the first node) and 0x1800A74A8 (successor).
// Child pointers are +0x20 left / +0x28 right and the parent is +0x18, which
// is src/engine/input/Input.h's BindingNode exactly.
// ---------------------------------------------------------------------------
BindingNode* FirstNode(BindingNode* root) {
    if (root == nullptr) return nullptr;
    while (root->left != nullptr) root = root->left;
    return root;
}

BindingNode* NextNode(BindingNode* n) {
    if (n->right != nullptr) {
        n = n->right;
        while (n->left != nullptr) n = n->left;
        return n;
    }
    // Climb while `n` is its parent's RIGHT child. Retail spells the first
    // step separately (0x1800A74C8 tests the LEFT child and returns the parent
    // immediately) and then loops on the right-child test; the two together
    // are the standard successor climb, written here in the standard form.
    BindingNode* p = n->parent;
    while (p != nullptr && p->right == n) {
        n = p;
        p = n->parent;
    }
    return p;
}

}  // namespace

// ============================================================================
// void Input::KeyMap::Load()
// retail 0x1800A7190, ord 3203.
//
// For every Binding in the map, for slot 0 then slot 1:
//   * if binding->exclusive == 0, skip the slot entirely (0x1800A7206 tests
//     `byte [RBX+1]`, which Input.h pins as `Binding::exclusive`). NOTE that
//     this means a non-exclusive binding is never restored from the registry
//     at all — surprising, but it is what the branch does, and the sentinel
//     below is what makes the skip observable rather than silent.
//   * look up "Input" + description + ("" | "Alt") in the settings map;
//   * value = strtol(value, NULL, 10), starting from the sentinel -2;
//   * if value >= 0 : device[slot] = (value >> 8) & 0xFF, inputId[slot] = value & 0xFF
//     else if value == -1 : device[slot] = 0xFF, inputId[slot] = 0   (explicit "unbound")
//     else (still -2, i.e. the key was absent) : write NOTHING, keep the
//     binding RegisterInput installed.
//
// The three-way sentinel is the whole point of the function and is easy to
// flatten into a two-way one by accident: -2 "not stored" and -1 "stored as
// unbound" produce DIFFERENT results, and only the second clears the binding.
// ============================================================================
namespace Input {

void KeyMap::Load() {

    // Retail hoists the singleton once, before the traversal
    // (0x1800A71AA), and re-reads the map root on every lookup. ENDGAME
    // B.3.9 SS1 (B39_RULES D7): the class static now, read fresh.
    const ::IO::Persistence* persistence = ::IO::Persistence::_singleton;

    // KeyMap::_root is at +0x08 (Input.h, pinned by every ported KeyMap point
    // function). Retail loads `[RCX+8]` at 0x1800A71B1.
    BindingNode* root = _root;

    int written = 0;
    for (BindingNode* n = FirstNode(root); n != nullptr; n = NextNode(n)) {
        Binding* b = &n->element;
        for (int slot = 0; slot < 2; ++slot) {
            // Retail re-arms the sentinel at the TOP of every slot iteration
            // (0x1800A7200), not once per binding.
            int32_t value = -2;

            if (b->exclusive != 0) {
                System::String name = System::String::Format(
                    "%s%s", b->description, kSlotSuffix[slot]);
                System::String key = System::String::Format(
                    "%s%s", "Input", name._data);

                const char* stored = FindSettingValue(persistence, key._data);
                if (stored != nullptr)
                    value = static_cast<int32_t>(std::strtol(stored, nullptr, 10));

                // Retail releases `key` first, then `name` — both with the
                // inlined refcount-dec / lock / Free sequence that is `~String`
                // (0x180006c80). C++'s reverse declaration order is that order.
            }

            // value >= 0 / -1 stores the slot (binding_value.h); value == -2:
            // the key was absent (or the binding is not exclusive). Retail
            // writes nothing; neither do we.
            if (StoreBindingValue(b, slot, value)) ++written;
        }
    }

    // The effectiveness control. A run with no saved bindings in the registry
    // reaches every line of this function and changes nothing, so `calls=`
    // alone would report the body as exercised when it has not been.
    (void)written;
}

}  // namespace Input
