// ============================================================================
// src/engine/input/input.cpp — Wave 8 lane D: Input::KeyMap + Input::Device
// + Input::Configuration
// ============================================================================
//
// Ports 15 of the 21 exports our tree calls (docs/history/runtime_port/WAVE8_LANE_D.md
// has the full table, the oracle argument for every function, and the 6
// hold-backs' written reasons). Every offset used below came from the
// DISASSEMBLY of the retail function at the address named in each comment —
// not Ghidra's decompiled C, which mangles this class's pointer arithmetic
// badly enough to be actively misleading (reference_ghidra_c_output_vs_
// disassembly.md). Layout is in src/engine/input/Input.h.
//
// Nothing here is registered as a vtable slot: neither class exports a
// `??_7`, and this lane verified (not assumed) that none of the 15 ported
// bodies dereferences a vtable pointer anywhere — see the type header.
//
// ENDGAME B.3.7 block B5: the bodies are MEMBERS of the real classes
// `Input::KeyMap` / `Input::Device` / `Input::Configuration`
// (src/engine/input/Input.h, Device.h); the registered free functions are
// one-line doors. `KeyMap::Released` (0x1800a79b0) is new here.
// ============================================================================

#include <cstdint>
#include <cstdio>
#include <cstring>
#include "input/Device.h"   // ENDGAME B.3.9 ENG: the class static
#include "input/Input.h"
#include "io/Persistence.h"   // ENDGAME B.3.9 SS1: Persistence::_singleton
#include "system/MemoryAllocator.h"   // rtalloc::FreeLockedIgnoringSelf
#include "../collection/set_avl_e24.h"

using Input::Binding;
using Input::BindingNode;
using State = Input::Device::State;

// Already-ported Wave 4 IO::Persistence entry points (src/engine/io/
// persistence.cpp). Not declared in any header there; redeclared here with
// the exact signature, same pattern rt_port.h's own Module/ResolveAddr use.

namespace {

// ============================================================================
// Collection::Set<Binding> helpers shared with every point function below.
// ============================================================================

// Exact-match descent: id < node.id -> left(+0x20); id > node.id -> right
// (+0x28); id == node.id -> found. Pinned identically (down to the branch
// order) in the disassembly of GetDeviceForId (0x1800a7970),
// GetInputForId (0x1800a7930), GetValue (0x1800a7b60), Pressed (0x1800a7a80),
// Released (0x1800a79b0), SetInput (0x1800a7e20), ClearInput/2 (0x1800a7d70),
// ClearInput/3 (0x1800a7db0) and the search half of RegisterInput
// (0x1800a7e70). Returns nullptr on no match, exactly like retail's RAX ending
// at 0 — every caller below reproduces retail's un-null-guarded use of that
// result (CLAUDE.md: a faithful crash on a bad id is not ours to fix).
BindingNode* FindBinding(const Input::KeyMap* km, uint8_t id) {
    BindingNode* n = km->_root;
    while (n) {
        if (id < n->element.id) {
            n = n->left;
        } else if (id <= n->element.id) {
            return n;  // id == n->element.id
        } else {
            n = n->right;
        }
    }
    return nullptr;
}

BindingNode* Leftmost(BindingNode* n) {
    while (n && n->left) n = n->left;
    return n;
}

// In-order successor via parent chain. Pinned by GetIdForDeviceInput's
// disassembly (0x1800a7890: right-then-leftmost if a right child exists,
// otherwise walk up while `node == parent->right`).
BindingNode* Successor(BindingNode* n) {
    if (n->right) return Leftmost(n->right);
    BindingNode* p = n->parent;
    while (p && n == p->right) {
        n = p;
        p = p->parent;
    }
    return p;
}

// Device::_singleton, an exported DATA symbol (retail 0x1801fe770), read
// through the port-aware data door (carry-over 5: it stays a slot until the
// end of B.3), exactly like engine_funcs.cpp's own g_pInputDeviceSingleton.
Input::Device* DeviceSingleton() {
    return ::Input::Device::_singleton;
}

// Resolve a (device, joystick) pair to the base of that device's State block,
// exactly as GetValue/Pressed/Released's shared dispatch does: device 0 =
// joystick block `joystick` (the function's OWN first int parameter, threaded
// through unchanged), device 1 = keyboard, device 2 = mouse, anything else =
// nullptr (retail's R8 stays 0 in that branch too — a faithful potential null
// deref on an invalid device index, not a case this port invents). Retail
// neither bounds the joystick index nor requires `d` to be non-null; the
// member arithmetic below is the same address arithmetic
// (d + joystick * 0x1b0, d + 0x6c0, d + 0x1098) and tests neither.
const State* DeviceArrayBase(const Input::Device* d, int32_t device, int32_t joystick) {
    switch (device) {
        case static_cast<int32_t>(Input::Device::InputDevice::Joystick): return d->_joystick[joystick];
        case static_cast<int32_t>(Input::Device::InputDevice::Keyboard): return d->_keyboard;
        case static_cast<int32_t>(Input::Device::InputDevice::Mouse):    return d->_mouse;
        default: return nullptr;
    }
}

// ---------------------------------------------------------------------------
// ClearAll and RegisterInput reach four unexported Collection::Set<E24> bodies
// (set_avl_e24.cpp: 0x180023060 / 0x180023130 / 0x180023170; allocator.cpp:
// 0x180009c30). B3R: called by name; the atomic by-address caches are gone.
// ---------------------------------------------------------------------------
// (AsSet24 / AsNode24, the two views of a KeyMap's tree the erased core
// takes, are inline in Input.h, so keymap_save.cpp's inlined teardown reaches
// the core through them too.)

// The three device-state readers of KeyMap (GetValue / Pressed / Released)
// share ONE shape and differ only in which State byte they read and what they
// return. Retail spells it out three times; the per-slot loop is written once
// here and each member passes its field reader.
//
//   for slot in 0..1:
//     d = (int8)binding.device[slot]
//     if (_suspendCount > 0 && d == 1) return 0   <- WHOLE call, not `continue`
//     if (d < 0) continue                          (0xFF unbound)
//     v = state(base(d, joystick))[inputId[slot]]
//     if (v) return v
//   return 0
//
// ORDER: retail tests the suspend condition BEFORE the unbound test
// (Released 0x1800a79f1..0x1800a7a05: `TEST EBX,EBX ; JLE` / `CMP [..+0x10],1 ;
// JZ return0` / THEN `TEST CL,CL ; JS next`). A device byte of 1 is never
// negative, so the two orders agree on every input; retail's is kept.
template <typename T, typename Read>
T ReadBoundState(const Input::KeyMap* km, int32_t joystick, int32_t id, Read read) {
    BindingNode* n = FindBinding(km, static_cast<uint8_t>(id));
    const Input::Device* dev = DeviceSingleton();
    for (int slot = 0; slot < 2; ++slot) {
        // Faithful to retail: a missing id leaves n == nullptr and this reads
        // through a near-null address — the same crash retail would take.
        const int8_t device = static_cast<int8_t>(n->element.device[slot]);
        if (km->_suspendCount > 0 && device == static_cast<int8_t>(Input::Device::InputDevice::Keyboard))
            return T();
        if (device < 0) continue;
        const State* base = DeviceArrayBase(dev, device, joystick);
        const T v = read(base[n->element.inputId[slot]]);
        if (v) return v;
    }
    return T();
}

}  // namespace

// ############################################################################
// Input::KeyMap — the member bodies (ENDGAME B.3.7 block B5: moved off the
// registered free functions, which are one-line doors at the bottom).
// ############################################################################
namespace Input {


// ?ClearAll@KeyMap@Input@@QEAAXXZ  retail 0x1800a7d20  ord 1706
//
// A full teardown of the binding tree. The recursive subtree-destroy and the
// single-node free are Collection::Set<Binding> internals shared with every
// other Set instantiation in the DLL, called by address exactly as retail's
// own disassembly does — both take `&this->_root` as their first argument
// (0x1800a7d20: `LEA RBX,[RCX+8]` feeds RCX at both call sites), not `this`.
// NOT CONVERTED to a `Collection::Set` member (ENDGAME B.3.2 step 2):
// `Binding`'s key is an unsigned BYTE id compared with a plain unsigned
// less-than, not `Collection::AVLCompare`'s string compare, so it would need a
// new `Compare` policy (Collection.h's `detail` namespace) before
// `Collection::Set<Binding, ByteIdCompare>::Clear` could stand in for these
// address doors. That is Collection-side policy work, not Input's.
void KeyMap::ClearAll() {
    void* setPtr = &_root;
    BindingNode* root = _root;
    if (root) {
        rtsetavl24::DestroySubtree(AsSet24(setPtr), AsNode24(root->left));    // 0x180023130
        rtsetavl24::DestroySubtree(AsSet24(setPtr), AsNode24(root->right));
        rtalloc::FreeLockedIgnoringSelf(setPtr, root);                          // 0x180009c30
    }
    _count = 0;
    _root = nullptr;
}

// ?ClearInput@KeyMap@Input@@QEAAXHH@Z  retail 0x1800a7d70  ord 1709
// ClearInput(id, slot): unbind ONE slot of the binding with this id.
// Un-guarded on a missing id, faithfully (retail's RAX==0 write-through): the
// write targets address (0+slot+0x10/0x12) — the same crash retail would take.
void KeyMap::ClearInput(int32_t id, int32_t slot) {
    BindingNode* n = FindBinding(this, static_cast<uint8_t>(id));
    n->element.device[slot]  = Binding::kUnboundDevice;
    n->element.inputId[slot] = 0;
}

// ?ClearInput@KeyMap@Input@@QEAAXHHH@Z  retail 0x1800a7db0  ord 1710
// ClearInput(id, device, inputId): unbind whichever slot (of the id's two)
// currently matches (device, inputId). Checks BOTH slots (retail's loop does
// not early-out on a match — see 0x1800a7db0: JNZ 0x1800a7e02 from the match
// branch too, straight into the increment). `n` may be nullptr here and is
// faithfully NOT guarded.
void KeyMap::ClearInput(int32_t id, int32_t device, int32_t input) {
    BindingNode* n = FindBinding(this, static_cast<uint8_t>(id));
    for (int slot = 0; slot < 2; ++slot) {
        int8_t  d = static_cast<int8_t>(n->element.device[slot]);
        uint8_t k = n->element.inputId[slot];
        if (d == device && k == input) {
            n->element.device[slot] = Binding::kUnboundDevice;
            n->element.inputId[slot] = 0;
        }
    }
}

// ?RegisterInput@KeyMap@Input@@QEAAXHHHPEBD_N@Z  retail 0x1800a7e70  ord 3529
//
// The node allocator FUN_180023060's first argument is never read
// (0x180023097 `MOV RCX,RBX` destroys it before any use), so the real
// signature is `BindingNode* AllocNode(<unused>, const Binding* src,
// BindingNode* parent)`. See docs/history/runtime_port/WAVE8_LANE_D.md.
//
// SHAPE (0x1800a7e70). Retail descends the tree TWICE and that is kept:
//   1. a read-only descent that returns early if `id` is already registered;
//   2. the element is built on the stack;
//   3. a second descent from the root tracking the parent, then the new node is
//      linked as that leaf's left or right child, rebalanced, and _count++.
// The empty-tree path skips the rebalance and increments BEFORE storing _root
// (0x1800a7eeb then 0x1800a7eee) -- order kept.
//
// The id compare is UNSIGNED (retail `CMP DL,CL; JC` -- JC is below, not less).
// Slot 1 is initialised UNBOUND: device[1] = 0xFF, inputId[1] = 0
// (0x1800a7eca, 0x1800a7ecf). _pad02 and _pad14 are NOT written by retail, so
// the node inherits whatever the stack held there -- faithful.
void KeyMap::RegisterInput(int32_t id, int32_t device, int32_t input,
                           const char* name, bool exclusive) {
    // 0x180023060 / 0x180023170 (set_avl_e24.cpp), called by name (B3R).
    auto allocNode = [](void* unused, const Binding* src, BindingNode* parent) {
        return reinterpret_cast<BindingNode*>(  // CAST-OK: elem_stride
            rtsetavl24::AllocNode(unused, src, AsNode24(parent)));
    };
    auto rebalance = [](void* setPtr, BindingNode** parentSlot) {
        rtsetavl24::Retrace(AsSet24(setPtr), reinterpret_cast<rtsetavl24::Node**>(parentSlot));  // CAST-OK: elem_stride
    };

    BindingNode* root = _root;
    const uint8_t key = static_cast<uint8_t>(id);

    // 1. Already registered? Retail's first descent (0x1800a7e90) tracks no
    //    parent and leaves the root in RBX untouched, which is what the
    //    `TEST RBX,RBX` at 0x1800a7ed9 then re-reads.
    for (BindingNode* n = root; n; ) {
        const uint8_t nid = n->element.id;
        if (key < nid)       n = n->left;
        else if (nid >= key) return;       // equal -> already registered
        else                 n = n->right;
    }

    // 2. Build the element (0x1800a7eae..0x1800a7ed4). _pad02/_pad14 are left
    //    alone deliberately: retail does not write them either.
    Binding elem;
    elem.id          = key;
    elem.exclusive   = static_cast<uint8_t>(exclusive);
    elem.description = name;
    elem.device[0]   = static_cast<uint8_t>(device);
    elem.device[1]   = Binding::kUnboundDevice;
    elem.inputId[0]  = static_cast<uint8_t>(input);
    elem.inputId[1]  = 0;

    void* setPtr = &_root;

    if (!root) {
        // 3a. Empty tree: the node IS the root, with a null parent.
        BindingNode* node = allocNode(nullptr, &elem, nullptr);
        _count += 1;          // retail increments first (0x1800a7eeb)
        _root = node;
    } else {
        // 3b. Second descent, tracking the parent in a stack slot the
        //     rebalance step is then handed BY ADDRESS (0x1800a7f56:
        //     `LEA RDX,[RSP+0x50]`), so `parent` must be a real local.
        BindingNode* parent = root;
        for (;;) {
            const uint8_t nid = parent->element.id;
            if (key < nid) {
                if (!parent->left) {
                    parent->left = allocNode(nullptr, &elem, parent);
                    break;
                }
                parent = parent->left;
            } else if (nid >= key) {
                return;                     // equal -> already registered
            } else {
                if (!parent->right) {
                    parent->right = allocNode(nullptr, &elem, parent);
                    break;
                }
                parent = parent->right;
            }
        }
        rebalance(setPtr, &parent);
        _count += 1;
    }
}

// ?GetDeviceForId@KeyMap@Input@@QEBAHHH@Z  retail 0x1800a7970  ord 2571
// Returns -1 both for "id not registered" AND "slot unbound" (0xFF sign-
// extends to -1 through the SAME `MOVSX` retail uses — 0x1800a7995..799d).
int32_t KeyMap::GetDeviceForId(int32_t id, int32_t slot) const {
    BindingNode* n = FindBinding(this, static_cast<uint8_t>(id));
    return n ? static_cast<int8_t>(n->element.device[slot]) : -1;
}

// ?GetInputForId@KeyMap@Input@@QEBAHHH@Z  retail 0x1800a7930  ord 2713
// Zero-extended (retail: MOVZX at 0x1800a7958) — unlike GetDeviceForId, an
// unbound slot's inputId byte is 0, already a valid "no input" value.
int32_t KeyMap::GetInputForId(int32_t id, int32_t slot) const {
    BindingNode* n = FindBinding(this, static_cast<uint8_t>(id));
    return n ? static_cast<int32_t>(n->element.inputId[slot]) : -1;
}

// ?GetIdForDeviceInput@KeyMap@Input@@QEBAHHH@Z  retail 0x1800a7890  ord 2700
// The one KeyMap accessor that is NOT a key lookup: an in-order walk of the
// WHOLE tree searching every binding's two slots for (device, inputId),
// returning the first match's id. Pinned from the disassembly's leftmost/
// successor shape (0x1800a78a0..0x1800a791d).
int32_t KeyMap::GetIdForDeviceInput(int32_t device, int32_t input) const {
    for (BindingNode* n = Leftmost(_root); n; n = Successor(n)) {
        for (int slot = 0; slot < 2; ++slot) {
            if (static_cast<int8_t>(n->element.device[slot]) == device &&
                static_cast<int32_t>(n->element.inputId[slot]) == input) {
                return n->element.id;
            }
        }
    }
    return -1;
}

// ?GetValue@KeyMap@Input@@QEBAFHH@Z  retail 0x1800a7b60  ord 3005
// The first non-zero State::value (+0x04) among the binding's bound slots.
// `_suspendCount > 0 && slot's device == 1` SHORT-CIRCUITS THE WHOLE FUNCTION
// to 0 (retail 0x1800a7adb/e0 both jump to the function's single 0-return,
// not just `continue` — checked in the disassembly, not the misleading
// decompile).
int16_t KeyMap::GetValue(int32_t joystick, int32_t id) const {
    return ReadBoundState<int16_t>(this, joystick, id,
        [](const State& s) -> int16_t { return s.value; });
}

// ?Pressed@KeyMap@Input@@QEBA_NHH@Z  retail 0x1800a7a80  ord 3432
// Same shape as GetValue, reading State::pressed (+0x06).
bool KeyMap::Pressed(int32_t joystick, int32_t id) const {
    return ReadBoundState<bool>(this, joystick, id,
        [](const State& s) -> bool { return s.pressed; });
}

// ?Released@KeyMap@Input@@QEBA_NHH@Z  retail 0x1800a79b0
// PORTED by block B5 (it was the one KeyMap reader nothing registered):
// read as disassembly, it is Pressed's body with the State byte at +0x07
// (`MOVZX ECX,byte [R8+RDX*8+0x7]` at 0x1800a7a45) instead of +0x06. Same
// descent (`CMP R8B,R9B ; JC left ; CMP R9B,R8B ; JNC found`), same miss path
// (`XOR EAX,EAX` and then the unguarded slot reads through 0), same suspend
// short-circuit (`MOV EBX,[RCX] ; TEST EBX,EBX ; JLE` then `CMP [..+0x10],1`),
// same joystick stride (`IMUL R8,R8,0x1b0`). Its tree callers (dragtool.cpp x2)
// used `fn_KeyMap_Released`, bound only to the UNREGISTERED exe slot 0x1401528f0
// and so always null (B37_RULES 6.3 defect 1). Caller batch K3 made both member
// calls and the B.3.7 close deleted the global: this body is live.
bool KeyMap::Released(int32_t joystick, int32_t id) const {
    return ReadBoundState<bool>(this, joystick, id,
        [](const State& s) -> bool { return s.released; });
}

// ?SetInput@KeyMap@Input@@QEAAXHHHH@Z  retail 0x1800a7e20  ord 3896
// SetInput(id, slot, device, inputId): overwrite one slot directly (no
// search over the two slots — the caller names which one). Faithfully
// unguarded on a missing id.
void KeyMap::SetInput(int32_t id, int32_t slot, int32_t device, int32_t input) {
    BindingNode* n = FindBinding(this, static_cast<uint8_t>(id));
    n->element.inputId[slot] = static_cast<uint8_t>(input);
    n->element.device[slot]  = static_cast<uint8_t>(device);
}

// ############################################################################
// Input::Device — the four const readers / reset of latched state
// ############################################################################

// ?GetValue@Device@Input@@QEBAFW4InputDevice@12@HH@Z  retail 0x1800e1660
// ord 3004. DeviceArrayBase returns nullptr for a device value outside
// {0,1,2} — faithful to retail (its R8 stays 0 in that branch too), NOT
// guarded: the State* below reproduces retail's exact near-null address.
int16_t Device::GetValue(InputDevice device, int32_t joystick, int32_t input) const {
    return DeviceArrayBase(this, static_cast<int32_t>(device), joystick)[input].value;
}

// ?Pressed@Device@Input@@QEBA_NW4InputDevice@12@HH@Z  retail 0x1800e16c0  ord 3431.
bool Device::Pressed(InputDevice device, int32_t joystick, int32_t input) const {
    return DeviceArrayBase(this, static_cast<int32_t>(device), joystick)[input].pressed;
}

// ?Released@Device@Input@@QEBA_NW4InputDevice@12@HH@Z  retail 0x1800e1720  ord 3545.
bool Device::Released(InputDevice device, int32_t joystick, int32_t input) const {
    return DeviceArrayBase(this, static_cast<int32_t>(device), joystick)[input].released;
}

// ?ResetState@Device@Input@@QEAAXXZ  retail 0x1800e0ef0  ord 3579
// Zeroes exactly the three State blocks (joystick/keyboard/mouse), whole
// records this time (unlike the ctor); deliberately does NOT touch +0x11D0
// (_mouseClipping), matching retail (0x1800e0ef0's three loops stop before it).
void Device::ResetState() {
    std::memset(_joystick, 0, sizeof(_joystick));
    std::memset(_keyboard, 0, sizeof(_keyboard));
    std::memset(_mouse, 0, sizeof(_mouse));
}

// ############################################################################
// Input::Configuration::Save
// ############################################################################

// ?Save@Configuration@Input@@QEAAXXZ  retail 0x1800e0c00  ord 3604
// `CALL 0x1800a7520` (KeyMap::Save, a near call) then three Persistence::Set
// bools. The KeyMap call used to be a by-address `ResolveAddr(0x1800a7520)`
// door; it is now the direct member call (the door resolved to the same body,
// keymap_save.cpp's). The Persistence singleton read stays a data slot.
void Configuration::Save() {
    _keyMap->Save();

    // ENDGAME B.3.9 SS1 (B39_RULES D7): the class static now, read fresh.
    ::IO::Persistence* const persistence = ::IO::Persistence::_singleton;

    persistence->Set("Input", "MouseClipping", _mouseClipping);
    persistence->Set("Input", "Legacy", _legacy);
    persistence->Set("Input", "MouseDisplay", _mouseDisplay);
}

}  // namespace Input

// ============================================================================
// Register
// ============================================================================
// ---------------------------------------------------------------------------
// Input::Configuration::~Configuration() - retail 0x1800E0E10, ord 555.
// WAVE 23 LANE TAIL.
//
// The WHOLE retail body:
//
//   1800e0e10  MOV qword ptr [0x1801fe768],0x0
//   1800e0e1b  RET
//
// The exact twin of `??1Configuration@Audio@@` (0x1800DE0D0) one namespace
// over, down to the instruction encoding, differing only in which slot it
// nulls. THEY WERE STILL READ SEPARATELY: this lane's brief forbids letting
// one namespace's answer stand in for another's, and a two-instruction body is
// exactly the shape where "same as the other one" would have been assumed
// rather than checked. Both were disassembled at their own addresses.
//
// `QEAA`, the NON-VIRTUAL spelling, verified present in the export directory
// with pefile; the `UEAA` virtual spelling is absent and would resolve NULL.
//
// The address written is `?_singleton@Configuration@Input@@0PEAV12@EA`
// (0x1801FE768, ord 4802) - another of this lane's DATA rows - so the store
// goes through RT_DATA_SLOT for the same reason the Audio twin's does. Note
// that this file's existing `input_device_update.cpp` already documents
// 0x1801FE768 under that name, which is an independent confirmation of the
// identity rather than a second reading of the same source.
// ---------------------------------------------------------------------------
void RtInput_Configuration_Dtor(void* /*selfPtr - never read*/) {
          // unconditional; there is no branch

    ::Input::Configuration::_singleton = nullptr;

}

// ?_singleton@Configuration@Input@@0PEAV12@EA (0x1801fe768) -- the class static (ENDGAME B.3.9 ENG, B39_RULES D7);
// RtInputPortInit redirects the minted storage row onto it.
::Input::Configuration* ::Input::Configuration::_singleton = nullptr;

// ?_singleton@Device@Input@@0PEAV12@EA (0x1801fe770) -- the class static (ENDGAME B.3.9 ENG, B39_RULES D7);
// RtInputPortInit redirects the minted storage row onto it.
::Input::Device* ::Input::Device::_singleton = nullptr;
