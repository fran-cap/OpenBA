#pragma once
// ============================================================================
// src/engine/input/Input.h — Input::KeyMap, Input::Configuration (and,
// through Device.h, Input::Device): THE REAL CLASSES (ENDGAME B.3 step 7,
// block B5). First written by Wave 8 lane D as `RtInput::` PODs.
// ============================================================================
//
// Retail Runtime-x64.dll, image base 0x180000000. Every offset below was read
// from the DISASSEMBLY (not Ghidra's decompiled C — see
// reference_ghidra_c_output_vs_disassembly.md) of the KeyMap point-lookup
// members, Device::GetValue/Pressed/Released/ResetState, and
// Configuration::Load/Save (all listed with retail addresses in
// docs/history/runtime_port/WAVE8_LANE_D.md sec. 1).
//
// ALL THREE CLASSES ARE NON-POLYMORPHIC (B37_RULES sec. 1): no `??_7` export,
// and every `??0`/`??1` head was read (B37_SLOTS.md sec. 7) — `??0KeyMap`
// (0x1800a7fb0) stores `dword [RCX] = 0` first, `??0Configuration`
// (0x1800e0de0) `qword [RCX] = 0` first, `??0Device` three State loops.
// None has a parent. Input has no mint row and no slot list: the block is
// member bodies + value types.
//
// B7-6 / B6-4: one definition per layout. The Wave 8 PODs were field-identical
// to the classes, so the POD BECAME the class and `RtInput::X` is an alias of
// `Input::X` (bottom of this file), kept for the callers outside block B5
// (src/diag/mouse_inject.cpp, src/engine/engine/singleton_ctors.cpp).
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <type_traits>

#include "Device.h"
#include "../collection/Collection.h"   // Collection::Array<int> (KeyMap::GetIdList)

namespace rtsetavl24 { struct Set; struct Node; }

namespace Input {

// ============================================================================
// Input::KeyMap::Binding / BindingNode — layout-only value types with no
// export of their own (B37_RULES sec. 1: "`Input::KeyMap`'s
// `Binding`/`BindingNode`"). Retail attests no name for either; they are the
// element and the node of the `Collection::Set<Binding>` embedded in KeyMap.
//
// NO user ctors (B7-8, B6-10): nothing constructs one through a C++ ctor in
// retail. `RegisterInput` builds a Binding on the STACK and leaves +0x02..+0x07
// and +0x14..+0x17 unwritten (0x1800a7eae..0x1800a7ed4), and the node is built
// by the AVL allocator 0x180023060 copying 0x18 raw bytes — a ctor would add
// stores retail does not make.
// ============================================================================

// One binding: an action id maps to up to 2 (device, deviceInput) slots
// ("primary"/"secondary"). 24 bytes — the `E24` element `Collection.h` already
// names as "Input::KeyMap's key-set element"; every byte pinned from
// `RegisterInput`'s disassembly (0x1800a7e70: the stack blob built at
// [RSP+0x20]..[RSP+0x37] before the call to the node-insert helper 0x180023060).
struct Binding {
    // device[slot] of an unbound slot (reads as int8 -1).
    static constexpr uint8_t kUnboundDevice         = 0xFF;
    // The settings.cfg value of a slot is `device << kDeviceShift | inputId`
    // (binding_value.h, keymap_save.cpp).
    static constexpr int32_t kDeviceShift           = 8;
    static constexpr int32_t kValueUnbound          = -1;     // the settings value of an unbound slot
    static constexpr int32_t kValueAbsent           = -2;     // the key is missing: the default stays
    // The largest device byte that is still >= 0 as an int8: bytes 3..0x7F are
    // invalid, 0x80..0xFF read as negative (unbound).
    static constexpr int32_t kMaxPositiveDeviceByte = 0x7F;

    uint8_t     id;            // +0x00 — BST key, UNSIGNED byte compare
    uint8_t     exclusive;     // +0x01 — RegisterInput's `bool exclusive` param, verbatim
    uint8_t     _pad02[6];     // +0x02..+0x07 — never written by RegisterInput; garbage
    const char* description;  // +0x08 — RegisterInput's `const char* name` param, verbatim
    uint8_t     device[2];    // +0x10 — device[slot], 0xFF == unbound
    uint8_t     inputId[2];   // +0x12 — deviceInput[slot]
    uint8_t     _pad14[4];    // +0x14..+0x17 — never written by RegisterInput; garbage
};
static_assert(sizeof(Binding) == 0x18, "Binding must be 0x18 (Collection::Set element)");
static_assert(offsetof(Binding, id) == 0x0, "Binding::id");
static_assert(offsetof(Binding, exclusive) == 0x1, "Binding::exclusive");
static_assert(offsetof(Binding, _pad02) == 0x2, "Binding::_pad02");
static_assert(offsetof(Binding, _pad14) == 0x14, "Binding::_pad14");
static_assert(offsetof(Binding, description) == 0x08, "Binding.description @ +0x08");
static_assert(offsetof(Binding, device)      == 0x10, "Binding.device @ +0x10");
static_assert(offsetof(Binding, inputId)     == 0x12, "Binding.inputId @ +0x12");
static_assert(std::is_trivial<Binding>::value, "Binding has no ctor (see above)");

// `Collection::Set<Binding>::Node` — align8(0x18)+0x00 == +0x18, matching
// Collection.h's S24 layout check exactly (parent +0x18, left +0x20,
// right +0x28, balance +0x30, height +0x32), and rtsetavl24::Node
// (src/engine/collection/set_avl_e24.h) byte for byte.
struct BindingNode {
    Binding      element;   // +0x00
    BindingNode* parent;    // +0x18
    BindingNode* left;      // +0x20
    BindingNode* right;     // +0x28
    int16_t      balance;   // +0x30
    int16_t      height;    // +0x32
    uint32_t     _padTail;  // +0x34
};
static_assert(sizeof(BindingNode) == 0x38, "BindingNode must be 0x38 (0x180023060 allocates 0x38)");
static_assert(offsetof(BindingNode, element) == 0x0, "BindingNode::element");
static_assert(offsetof(BindingNode, _padTail) == 0x34, "BindingNode::_padTail");
static_assert(offsetof(BindingNode, parent)  == 0x18, "BindingNode.parent @ +0x18");
static_assert(offsetof(BindingNode, left)    == 0x20, "BindingNode.left @ +0x20");
static_assert(offsetof(BindingNode, right)   == 0x28, "BindingNode.right @ +0x28");
static_assert(offsetof(BindingNode, balance) == 0x30, "BindingNode.balance @ +0x30");
static_assert(offsetof(BindingNode, height)  == 0x32, "BindingNode.height @ +0x32");
static_assert(std::is_trivial<BindingNode>::value, "BindingNode has no ctor (see above)");

// ============================================================================
// Input::KeyMap — 0x18 bytes, NON-POLYMORPHIC, held BY POINTER only
// (`Configuration+0x00`; `Configuration::Startup` 0x1800e0d30 allocates it
// with `MemAlloc(0x18, 4)` and INLINES the ctor's three zero stores).
//
// Not held by value anywhere, so no real ctor (B6-10): the ctor, the DEEP copy
// ctor (0x1800229d0 copies the binding tree), the dtor and operator= are
// declared and left undefined, which also keeps the class from being
// trivially copyable (B7-8: do not assert that it is).
//
// `_bindings` is a `Collection::Set<Binding>` embedded at +0x08: `_root` at
// +0x08, `_count` at +0x10 — pinned by ClearAll/ClearInput/GetDeviceForId/
// GetInputForId/GetValue/Pressed/Released/RegisterInput/SetInput reading/
// writing `*(KeyMap+8)` as the root and `RegisterInput` incrementing the dword
// at +0x10 on every insert. It stays two split fields, not a real
// `Collection::Set<Binding, ...>` member: Binding's key is an unsigned BYTE,
// which needs a compare policy Collection.h does not have (input.cpp's
// ClearAll note).
// ============================================================================
class KeyMap {
public:
    // ---- the 20 exports (B37_SLOTS.md sec. 6) -------------------------------
    void Load();                                                    // 0x1800a7190  keymap_load.cpp
    void Save();                                                    // 0x1800a7520  keymap_save.cpp
    int32_t GetIdForDeviceInput(int32_t device, int32_t input) const;   // 0x1800a7890  input.cpp
    int32_t GetInputForId(int32_t id, int32_t slot) const;          // 0x1800a7930  input.cpp
    int32_t GetDeviceForId(int32_t id, int32_t slot) const;         // 0x1800a7970  input.cpp
    // The three device-state readers take the JOYSTICK index first (only the
    // device-0 branch reads it: `MOVSXD RDI,EDX ; IMUL R8,RDI,0x1b0`) and the
    // binding id second (`CMP R8B,[node]`).
    bool Released(int32_t joystick, int32_t id) const;              // 0x1800a79b0  input.cpp
    bool Pressed(int32_t joystick, int32_t id) const;               // 0x1800a7a80  input.cpp
    int16_t GetValue(int32_t joystick, int32_t id) const;           // 0x1800a7b60  input.cpp
    void ClearAll();                                                // 0x1800a7d20  input.cpp
    void ClearInput(int32_t id, int32_t slot);                      // 0x1800a7d70  input.cpp
    void ClearInput(int32_t id, int32_t device, int32_t input);     // 0x1800a7db0  input.cpp
    void SetInput(int32_t id, int32_t slot, int32_t device, int32_t input);    // 0x1800a7e20 input.cpp
    void RegisterInput(int32_t id, int32_t device, int32_t input,
                       const char* name, bool exclusive);           // 0x1800a7e70  input.cpp

    // ---- declared, NOT PORTED: nothing in the tree calls these ------------
    void GetIdList(Collection::Array<int>& out) const;              // 0x1800a76c0  not ported
    float GetNormalizedValue(int32_t joystick, int32_t id) const;   // 0x1800a7c50  not ported
    // `TEST DL,DL ; JZ ; INC dword [RCX] ... DEC dword [RCX] ; MOV EAX,[RCX]`
    // (0x1800a7f90, read by block B5): the +0x00 counter's only writer found,
    // and the reason it is named `_suspendCount`.
    int32_t Suspend(bool suspend);                                  // 0x1800a7f90  not ported

    // ---- lifetime: declared, deliberately undefined (see the banner) -------
    KeyMap();                                                       // 0x1800a7fb0  inlined by Configuration::Startup
    KeyMap(const KeyMap& other);                                    // 0x1800229d0  not ported (DEEP copy)
    ~KeyMap();                                                      // 0x180022970  not ported
    KeyMap& operator=(const KeyMap& other);                         // 0x180022a40  not ported
    // The unexported deleting destructor 0x180053440 is the free function
    // `RtInput_KeyMap_DeletingDtor` (keymap_save.cpp), registered by address.

    // ---- the 0x18 bytes -----------------------------------------------------
    // +0x00 — was `_disableKeyboardCount`, named for its observed effect: when
    // `> 0` and a matched binding's slot is on device 1 (keyboard), GetValue/
    // Pressed/Released return 0/false for the WHOLE call. Its writer is
    // `Suspend(bool)` (above), so it is the suspend nesting count.
    int32_t      _suspendCount;   // +0x00
    int32_t      _pad04;          // +0x04 — never read by any ported fn
    BindingNode* _root;           // +0x08 — Collection::Set<Binding>::_root
    int32_t      _count;          // +0x10 — Collection::Set<Binding>::_count
    int32_t      _pad14;          // +0x14 — never read by any ported fn
};
static_assert(!std::is_polymorphic<KeyMap>::value, "Input::KeyMap has no vtable");
static_assert(offsetof(KeyMap, _suspendCount) == 0x00, "KeyMap._suspendCount @ +0x00");
static_assert(offsetof(KeyMap, _root)         == 0x08, "KeyMap._root @ +0x08");
static_assert(offsetof(KeyMap, _count)        == 0x10, "KeyMap._count @ +0x10");
static_assert(alignof(KeyMap) == 8, "qword-aligned (the root pointer)");
// B37_RULES sec. 2.3: `MemAlloc(0x18, 4)` in Configuration::Startup 0x1800e0d76.
static_assert(sizeof(KeyMap) == 0x18,
              "Input::KeyMap is 0x18 bytes -- Configuration::Startup 0x1800e0d76 allocates 0x18");
static_assert(offsetof(KeyMap, _pad04) == 0x4, "KeyMap::_pad04");
static_assert(offsetof(KeyMap, _pad14) == 0x14, "KeyMap::_pad14");

// ============================================================================
// Input::Configuration — 3 bools behind an owned KeyMap pointer. 0x10 bytes,
// NON-POLYMORPHIC, heap-only: `Engine::Startup` allocates it (`MemAlloc(0x10)`
// at 0x1800dbd51) and INLINES the ctor, ported as the door
// `Ctor_InputConfiguration` (singleton_ctors.cpp, Engine-owned) — that door
// STAYS, so the ctor below is declared and undefined. Fields pinned by BOTH
// Load (0x1800e0c80) and Save (0x1800e0c00), which agree on all three offsets
// and the registry key names.
//
// `?_singleton@Configuration@Input@@0PEAV12@EA` (0x1801fe768) STAYS a data slot
// read through RT_DATA_SLOT until the end of B.3 (carry-over 5).
// ============================================================================
class Configuration {
public:
    // ---- the 16 exports (B37_SLOTS.md sec. 6) -------------------------------
    void Save();                                     // 0x1800e0c00  input.cpp
    void Load();                                     // 0x1800e0c80  input_config_startup.cpp
    bool Startup();                                  // 0x1800e0d30  input_config_startup.cpp

    // ---- declared, NOT PORTED: nothing in the tree calls these ------------
    KeyMap* GetKeyMap();                             // 0x18000d800  not ported
    bool GetMouseClipping() const;                   // 0x18000d810  not ported
    bool IsLegacyEnabled() const;                    // 0x18000d820  not ported
    bool GetMouseDisplay() const;                    // 0x18000d830  not ported
    void SetMouseClipping(bool enable);              // 0x18000d840  not ported
    void SetLegacy(bool enable);                     // 0x18000d850  not ported
    void SetMouseDisplay(bool enable);               // 0x18000d860  not ported
    Configuration& operator=(const Configuration& other);  // 0x18000d870  not ported
    static Configuration* Get();                     // 0x18000d890  not ported
    // ?_singleton@Configuration@Input@@0PEAV12@EA (0x1801fe768) — the class static (ENDGAME B.3.9 ENG, B39_RULES D7).
    // The namespace's *PortInit redirects the minted storage row onto it, so
    // the name door, the address door and this member are the same bytes.
    // PUBLIC while foreign readers still reach the row through the doors.
    static Configuration* _singleton;
    void Shutdown();                                 // 0x1800e0d00  not ported

    // ---- lifetime: DOORS, declared, deliberately undefined -----------------
    Configuration();                                 // 0x1800e0de0  door Ctor_InputConfiguration
    ~Configuration();                                // 0x1800e0e10  door RtInput_Configuration_Dtor (input.cpp)

    // ---- the 0x10 bytes -----------------------------------------------------
    KeyMap*  _keyMap;          // +0x00 — owned; Startup allocates it
    bool     _mouseClipping;   // +0x08 — registry "Input"/"MouseClipping"
    // +0x09 — registry "Input"/"Legacy". When set, PlatformStartup skips ALL
    // DirectInput device creation (`CMP byte [RAX+9],0` at 0x1800e1899) and
    // both pollers take the GetAsyncKeyState path.
    bool     _legacy;          // +0x09
    bool     _mouseDisplay;    // +0x0A — registry "Input"/"MouseDisplay"
    uint8_t  _pad0b[5];        // +0x0B..+0x0F — the ctor's last store is byte +0x0a
};
static_assert(!std::is_polymorphic<Configuration>::value, "Input::Configuration has no vtable");
static_assert(offsetof(Configuration, _keyMap)        == 0x00, "Configuration._keyMap @ +0x00");
static_assert(offsetof(Configuration, _mouseClipping) == 0x08, "Configuration._mouseClipping @ +0x08");
static_assert(offsetof(Configuration, _legacy)        == 0x09, "Configuration._legacy @ +0x09");
static_assert(offsetof(Configuration, _mouseDisplay)  == 0x0A, "Configuration._mouseDisplay @ +0x0A");
static_assert(alignof(Configuration) == 8, "qword-aligned (the KeyMap pointer)");
// B37_RULES sec. 2.3: `MemAlloc(0x10)` at Engine::Startup 0x1800dbd51.
static_assert(sizeof(Configuration) == 0x10,
              "Input::Configuration is 0x10 bytes -- Engine::Startup 0x1800dbd51 allocates 0x10");
static_assert(offsetof(Configuration, _pad0b) == 0xB, "Configuration::_pad0b");


// The erased Set<E24> core's two views of a KeyMap's tree (input.cpp): the
// {_root, _count, _pad14} header as its Set, a BindingNode as its Node.
inline rtsetavl24::Set* AsSet24(void* p) {
    return reinterpret_cast<rtsetavl24::Set*>(p);   // CAST-OK: elem_stride
}
inline rtsetavl24::Node* AsNode24(BindingNode* n) {
    return reinterpret_cast<rtsetavl24::Node*>(n);  // CAST-OK: elem_stride
}

}  // namespace Input

// ============================================================================
// B7-6: the `RtInput::` spellings, now ALIASES of the classes, for the callers
// outside block B5 (src/diag/mouse_inject.cpp reads `RtInput::State` and
// `kMouseOffset`; singleton_ctors.cpp reads the offsets). The constants are
// the Device class's own offsets, cross-asserted.
// ============================================================================
namespace RtInput {

using KeyMap        = ::Input::KeyMap;
using Binding       = ::Input::Binding;
using BindingNode   = ::Input::BindingNode;
using Configuration = ::Input::Configuration;
using State         = ::Input::Device::State;
using InputDevice   = ::Input::Device::InputDevice;

constexpr int kJoystickCount       = ::Input::Device::kJoystickCount;
constexpr int kJoystickInputCount  = ::Input::Device::kJoystickInputCount;
constexpr int kJoystickStride      = 0x1B0;
constexpr int kKeyboardOffset      = 0x6C0;
constexpr int kKeyboardInputCount  = ::Input::Device::kKeyboardInputCount;
constexpr int kMouseOffset         = 0x1098;
constexpr int kMouseInputCount     = ::Input::Device::kMouseInputCount;
constexpr int kMouseClippingOffset = 0x11D0;
constexpr int kDirectInputOffset   = 0x11D8;
constexpr int kKeyboardComOffset   = 0x11E0;
constexpr int kMouseComOffset      = 0x11E8;
constexpr int kHasFocusOffset      = 0x11F0;
constexpr int kWheelAccumOffset    = 0x11F4;
constexpr int kLastCursorXOffset   = 0x11F8;
constexpr int kLastCursorYOffset   = 0x11FC;
// Was "a LOWER BOUND, not sizeof"; B37_RULES sec. 2.3 settled it from the
// allocation, so it now IS sizeof(Input::Device).
constexpr int kDeviceMappedSize    = 0x1200;

static_assert(kJoystickStride == (int)sizeof(State) * kJoystickInputCount, "joystick stride");
static_assert(kKeyboardOffset      == (int)offsetof(::Input::Device, _keyboard),       "alias");
static_assert(kMouseOffset         == (int)offsetof(::Input::Device, _mouse),          "alias");
static_assert(kMouseClippingOffset == (int)offsetof(::Input::Device, _mouseClipping),  "alias");
static_assert(kDirectInputOffset   == (int)offsetof(::Input::Device, _directInput),    "alias");
static_assert(kKeyboardComOffset   == (int)offsetof(::Input::Device, _keyboardDevice), "alias");
static_assert(kMouseComOffset      == (int)offsetof(::Input::Device, _mouseDevice),    "alias");
static_assert(kHasFocusOffset      == (int)offsetof(::Input::Device, _hasFocus),       "alias");
static_assert(kWheelAccumOffset    == (int)offsetof(::Input::Device, _wheelAccum),     "alias");
static_assert(kLastCursorXOffset   == (int)offsetof(::Input::Device, _lastCursorX),    "alias");
static_assert(kLastCursorYOffset   == (int)offsetof(::Input::Device, _lastCursorY),    "alias");
static_assert(kDeviceMappedSize    == (int)sizeof(::Input::Device),                    "alias");

}  // namespace RtInput

// ==== B3R RT-Core: Runtime bodies called by name ====
// ---- input/input.cpp -- ??1Configuration@Input@@QEAA@XZ (retail never reads
// `this`: the whole body is the `_singleton` null store).
void RtInput_Configuration_Dtor(void* selfPtr);  // 0x1800e0e10
// ==== end B3R RT-Core ====
