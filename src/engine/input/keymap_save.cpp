// ============================================================================
// src/engine/input/keymap_save.cpp — Wave 9 lane INPUT
//   Input::KeyMap::Save, and the unexported KeyMap deleting destructor.
// ============================================================================
//
//   | What                                | Retail      | Ord  | Instrs |
//   |-------------------------------------|-------------|------|--------|
//   | ?Save@KeyMap@Input@@QEAAXXZ          | 0x1800a7520 | 3607 |   79   |
//   | (unexported) KeyMap deleting dtor    | 0x180053440 |  --  |   26   |
//
// The mangled name was checked against Runtime-x64.dll's export directory with
// pefile before a line was written — `QEAA` (public), not `AEAA`; lane PKG2
// nearly shipped the private spelling of a helper and that resolves NULL
// silently and forever. 0x180053440 has no name in EITHER build and is
// registered by ADDRESS only.
//
// ---------------------------------------------------------------------------
// WHY THESE TWO TOGETHER
// ---------------------------------------------------------------------------
// They are the last two by-address doors the Input cluster leaves open. Both
// were listed by `tools/addr_door_audit.py --csv` as unregistered:
//
//   0x1800a7520  src/engine/input/input.cpp:615               (Configuration::Save)
//   0x180053440  src/engine/engine/engine_lifecycle.cpp:858
//                src/engine/input/input_config_startup.cpp:180
//
// `Configuration::Save` already CALLS 0x1800a7520 by address, so on the DLL-less
// boot that call executes address 0 — the failure class attempts 13 and 17 both
// died on. 0x180053440 sits on `Engine::Shutdown`'s input-configuration
// teardown and on `Configuration::Startup`'s re-entry arm.
//
// ---------------------------------------------------------------------------
// CLOSURE, COUNTED BEFORE WRITING (lane PICK's rule)
// ---------------------------------------------------------------------------
//   Save (0x1800a7520)
//     0x1800c9730  ?Format@String@System@@SA?AV12@PEBDZZ   PORTED (string.cpp)
//     0x1800b80c0  IO::Persistence::Set(int)               PORTED (persistence.cpp:188)
//     0x1800c6c00  ?Free@MemoryAllocator@System@@…         PORTED (allocator.cpp)
//     [0x18014b390]/[0x18014b388]  Enter/LeaveCriticalSection — KERNEL32
//   deleting dtor (0x180053440)
//     0x180023130  Set<E24> destroy subtree                PORTED THIS LANE
//                                                          (set_avl_e24.cpp)
//     0x180009c30  the allocator-lock Free wrapper         REGISTERED
//     0x180006a10  the guarded object free                 REGISTERED
//                                                          (video/context_lifecycle.cpp:697)
//
// **Zero new unregistered doors.** Both are called DIRECTLY here rather than
// through a resolver wherever the callee is ours, which is what keeps these
// bodies off the DLL at cutover.
//
// ---------------------------------------------------------------------------
// SAVE IS `LOAD` RUN BACKWARDS, AND THE ASYMMETRY IS REAL
// ---------------------------------------------------------------------------
// `keymap_load.cpp` (lane IOA) is the mirror image and was read first. Two
// differences are NOT symmetric and both are retail's:
//
//  1. **Load does TWO `String::Format`s, Save does ONE.** Load builds
//     `description+suffix` and then prefixes `"Input"` itself, because it walks
//     `IO::Persistence`'s settings map directly and that map is keyed on the
//     CONCATENATION. Save hands `"Input"` to `Persistence::Set` as a separate
//     `section` argument and lets it do the joining (0x1800a75d3 loads
//     0x180155200 into RDX, the section slot). Same key, one Format.
//  2. **Load has a THREE-WAY sentinel and Save has a two-way value.** Load
//     starts at -2 ("key absent, write nothing"), which Save has no counterpart
//     for: Save always writes, and an unbound slot is written as the explicit
//     -1 that Load reads back as "stored as unbound".
//
// What IS symmetric, and is the thing to get right: `if (binding.exclusive ==
// 0) continue` skips the slot entirely on BOTH sides (0x1800a7586 here,
// 0x1800a7206 in Load). A non-exclusive binding is neither saved nor restored.
//
// Value encoding, from 0x1800a7590..0x1800a75a7:
//     value = -1;
//     if ((int8_t)device[slot] >= 0)
//         value = inputId[slot] | (((int)(int8_t)device[slot]) << 8);
// which is exactly what Load decodes with `(v >> 8) & 0xFF` / `v & 0xFF`.
//
// ---------------------------------------------------------------------------
// ORACLE
// ---------------------------------------------------------------------------
// **Save is a PURE READ of the KeyMap** — it mutates only `IO::Persistence`'s
// settings map, never a binding — and its writes are IDEMPOTENT and
// value-identical, which is the property Wave 8.5 measured for
// `SerializeReferenceTable` and used to license a full ours-then-theirs replay
// rather than a prefix stop. So the oracle is **OUR WRITER, THEIR READER, THEN
// THEIR WRITER, THEIR READER** — the same shape lane D built for
// `KeyMap::RegisterInput`:
//
//   1. ours runs, writing for real, recording every (key, value) it emitted;
//   2. every recorded key is read back with the SHIPPED
//      `?Get@Persistence@IO@@QEBA_NPEBD0AEAH@Z` (via `the raw DLL door`, so the
//      registry cannot hand back our own port) -> the value OUR writer left;
//   3. the SHIPPED `?Save@KeyMap@Input@@QEAAXXZ` runs on the same KeyMap;
//   4. every key is read back again -> the value THEIR writer left;
//   5. all three must agree, per key.
//
// Nothing is corrupted by a failing comparison, because THEIRS RUNS LAST: the
// map ends in exactly the state a retail Save would have left it in. A key ours
// wrote WRONG is caught at step 5 (theirs overwrote it with a different value);
// a key ours FAILED to write is caught too, because step 2 reads the stale
// value or nothing at all and step 4 reads retail's.
//
// **What this oracle CANNOT see, said out loud:** an EXTRA key ours emits that
// theirs does not. Both read-backs would agree on it, because both are reading
// what our own writer put there. The compensating check is the count — we
// record how many settings our body emitted and assert it equals two per
// exclusive binding — but that is our own bookkeeping, not retail's, and it is
// labelled as such below. A recorded-key-set comparison would need the shipped
// `Persistence::Set` detoured, which is a NEAR call inside the DLL that our
// registry cannot intercept.
//
// A NEGATIVE CONTROL runs alongside it: a scratch key is written and read back,
// and the comparison is required to distinguish the value it wrote from that
// value plus one. If it cannot, the per-key agreements above prove nothing and
// the line says so.
//
// The dtor has NO oracle and gets a self-check plus a did-work counter: it
// FREES the tree, so there is nothing left to compare, and the shipped body's
// frees go through a near call our registry cannot see. Its correctness rests
// on `set_avl_e24.cpp`'s `DestroySubtree`, which the lockstep probe covers.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>

#include "input/Input.h"
#include "io/Persistence.h"   // ENDGAME B.3.9 SS1: Persistence::_singleton
#include "video/Context.h"    // Rt_GuardedObjectFree (0x180006a10)
#include "../collection/set_avl_e24.h"
#include "system/string_internal.h"

using Input::Binding;
using Input::BindingNode;

// ENDGAME B.3.1 step 2: `?Format@String@System@@SA?AV12@PEBDZZ` (0x1800c9730)
// is a static member of `System::String` now, so the local variadic declaration
// this file carried for the shim is gone — as in keymap_load.cpp.

// IO::Persistence::Set(section, key, int) — our Wave 4 port, registered at
// 0x1800b80c0. Called DIRECTLY: a resolver call would be a door, and this one
// has a body on our side already.

namespace {

// The two literals retail builds the key from, read with `read_memory` out of
// .rdata at the addresses the disassembly LEAs (0x1801551f0 / 0x1801551f4), and
// the section at 0x180155200 — the same three keymap_load.cpp records:
//   0x1801551F0 = ""      0x1801551F4 = "Alt"      0x180155200 = "Input"
constexpr const char* kSlotSuffix[2] = { "", "Alt" };
constexpr const char* kSection       = "Input";

// ---------------------------------------------------------------------------
// Collection::Set<Binding> in-order traversal. Retail spells it inline at
// 0x1800a7555 (descend LEFT to the first node) and 0x1800a7653 (successor:
// right subtree's leftmost, else climb while we are a right child). Byte for
// byte the same walk keymap_load.cpp transcribed from 0x1800a71c0 / 0x1800a74a8
// — the two are independent disassemblies of the same shape, which is the
// cross-check. Child pointers are +0x20 left / +0x28 right, parent +0x18
// (src/engine/input/Input.h).
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
    BindingNode* p = n->parent;
    while (p != nullptr && p->right == n) {
        n = p;
        p = n->parent;
    }
    return p;
}

// ---------------------------------------------------------------------------
// The emission recorder — the sink both sides of the oracle write into.
//
// A key is up to 64 bytes; the engine's longest binding description is well
// under that, and an overrun is TRUNCATED and COUNTED rather than dropped, so a
// truncation cannot masquerade as agreement.
// ---------------------------------------------------------------------------
struct Emission { char key[64]; int value; };

constexpr int kMaxEmissions = 256;
Emission g_emit[kMaxEmissions];
int      g_emitCount   = 0;
int      g_emitOverflow = 0;
bool     g_recording   = false;

void Record(const char* key, int value) {
    if (g_emitCount >= kMaxEmissions) { ++g_emitOverflow; return; }
    Emission& e = g_emit[g_emitCount++];
    std::snprintf(e.key, sizeof(e.key), "%s", key ? key : "(null)");
    e.value = value;
}

// The body's one write point. It ALWAYS performs the real write — the oracle
// observes through the read-back, it does not divert the write — and records
// the pair when the diff arm asked it to.
void EmitSetting(::IO::Persistence* persistence, const char* key, int value) {
    if (g_recording) Record(key, value);
    persistence->Set(kSection, key, value);
}

}  // namespace

// ============================================================================
// void Input::KeyMap::Save()
// retail 0x1800a7520, ord 3607.
//
// For every Binding in the map, for slot 0 then slot 1:
//   * if binding->exclusive == 0, skip the slot (0x1800a7586);
//   * value = -1, or (device << 8) | inputId when device >= 0;
//   * key = Format("%s%s", description, "" | "Alt");
//   * Persistence::Set("Input", key, value);
//   * release the temporary String.
//
// The Persistence singleton is hoisted ONCE before the traversal (retail
// 0x1800a7541 `MOV R15,[0x1801fc6b8]`), BEFORE the empty-tree test — so an
// empty KeyMap still reads it. Faithful, and harmless.
// ============================================================================
namespace Input {

void KeyMap::Save() {

    // ENDGAME B.3.9 SS1 (B39_RULES D7): the class static now, read fresh.
    ::IO::Persistence* const persistence = ::IO::Persistence::_singleton;

    // KeyMap::_root is at +0x08 (0x1800a7548 `MOV RBX,[RCX+8]`).
    BindingNode* root = _root;

    int written = 0;
    for (BindingNode* n = FirstNode(root); n != nullptr; n = NextNode(n)) {
        Binding* b = &n->element;
        for (int slot = 0; slot < 2; ++slot) {
            if (b->exclusive == 0) continue;      // 0x1800a7586, JZ to the increment

            // 0x1800a7590..0x1800a75a7. EDI starts at -1 and is only rebuilt
            // when the device byte is non-negative; 0xFF (unbound) is negative
            // and leaves the -1 standing.
            int32_t value = Binding::kValueUnbound;
            const int8_t device = static_cast<int8_t>(b->device[slot]);
            if (device >= 0)
                value = static_cast<int32_t>(b->inputId[slot]) |
                        (static_cast<int32_t>(device) << Binding::kDeviceShift);

            System::String key = System::String::Format(
                "%s%s", b->description, kSlotSuffix[slot]);
            EmitSetting(persistence, key._data, value);
            // `key` releases here — retail's 0x1800a75f2, now ~String.
            ++written;
        }
    }

    g_recording = false;

    // The effectiveness control. A KeyMap with no exclusive bindings reaches
    // every line of this function and emits nothing, while `calls=` still reads
    // 1 — count what the body actually did, inside the body
    // (reference_probe_needs_an_effectiveness_control.md).
    (void)written;
}

}  // namespace Input

// ============================================================================
// The KeyMap deleting destructor — retail 0x180053440, unexported.
//
//   Set* s = (Set*)((char*)this + 8);            0x180053460 `LEA RBX,[RCX+8]`
//   Node* root = s->root;
//   if (root) {
//       DestroySubtree(s, root->left);           0x180023130
//       DestroySubtree(s, root->right);
//       SetNodeFree(s, root);                    0x180009c30
//   }
//   s->root = nullptr; s->count = 0;             0x180053493/96
//   GuardedFree(this);                           0x180006a10
//   return this;                                 0x1800534a1 `MOV RAX,RDI`
//
// It returns `this` in RAX and takes NO deleting-flag argument, so it is the
// scalar delete MSVC folded at the `delete keyMap` sites — not the exported
// `??1KeyMap@Input@@QEAA@XZ` (0x180022970), which is a DIFFERENT function that
// does not free the object. The two must not be confused: registering this body
// under the `??1` name would free the object out from under a stack KeyMap.
//
// `s->root`/`s->count` are zeroed AFTER the tree is gone and BEFORE the object
// is freed, i.e. into memory that is about to be released. Retail does it;
// kept.
//
// The three-step teardown is written out rather than delegated to
// `KeyMap::ClearAll` (0x1800a7d20), which has the identical body — because
// retail emitted it inline here and calling ClearAll would add a call the
// original does not make.
// ============================================================================
Input::KeyMap* RtInput_KeyMap_DeletingDtor(Input::KeyMap* self) {

    // KeyMap's +0x08 {_root, _count, _pad14} handed to the rtsetavl24 core
    // through input.cpp's two views (Input::AsSet24 / AsNode24).
    void* const setPtr = &self->_root;
    Input::BindingNode* const root = self->_root;
    const int countBefore = self->_count;
    const unsigned visitsBefore = rtsetavl24::g_destroyVisits;

    if (root) {
        rtsetavl24::DestroySubtree(Input::AsSet24(setPtr), Input::AsNode24(root->left));
        rtsetavl24::DestroySubtree(Input::AsSet24(setPtr), Input::AsNode24(root->right));
        rtsetavl24::SetNodeFree(setPtr, root);
    }
    self->_root  = nullptr;
    self->_count = 0;

    // 0x180006a10 — `if (p) { Enter; Free(_singleton, p); Leave; }`, one
    // argument (video/context_lifecycle.cpp).
    Rt_GuardedObjectFree(self);

    // No comparison is possible: the tree is freed, so there is nothing left to
    // compare against, and the shipped body reaches 0x180009c30 through a near
    // call our registry cannot intercept. What IS checkable is that the destroy
    // walk reached one node per counted binding, minus the root this body frees
    // itself.
    const unsigned visited = rtsetavl24::g_destroyVisits - visitsBefore;
    const bool ok = (root == nullptr)
                  ? (visited == 0 && countBefore == 0)
                  : (static_cast<int>(visited) == countBefore - 1);
    char a[128];
    std::snprintf(a, sizeof(a), "destroy walk visited %u of count-1 = %d",
                  visited, countBefore - 1);
    return self;
}
