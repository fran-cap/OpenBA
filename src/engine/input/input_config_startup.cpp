// ============================================================================
// src/engine/input/input_config_startup.cpp — Wave 9 lane ENG, iteration 3
// Input::Configuration::Startup + the unexported Load it tail-calls
// ============================================================================
//
//   | Export                                | Retail      | Ord  | Instrs |
//   |---------------------------------------|-------------|------|--------|
//   | ?Startup@Configuration@Input@@QEAA_NXZ | 0x1800e0d30 | 4085 |   43   |
//   | ?Load@Configuration@Input@@QEAAXXZ     | 0x1800e0c80 | 3199 |   25   |
//
// CORRECTED, Wave 20 Phase 0c. This table used to call 0x1800e0c80
// "(unexported)" and the paragraph below used to say it "has no name in either
// build and is registered by ADDRESS only". BOTH WERE FALSE, and re-running the
// very check they claim to have run is what showed it:
//
//   ?Load@Configuration@Input@@QEAAXXZ -> ordinal 3199, 0x1800e0c80
//
// It is a plain public export (QEAA). This is the documented trap -- this DLL
// exports its private members, so "unexported" is a conclusion that has to be
// MEASURED, never inferred from a mangling or from a name not being to hand
// (reference_aeaa_is_private_not_unexported.md). The cost was real: input.cpp
// had already ported and NAME-registered the same function, so the false belief
// produced a second body, and the export's two doors reached one each. This file
// holds the one body.
//
// `Engine::Startup` step 14 — the last of the four `Configuration::Startup`
// calls, and the last thing before `PlatformConfigure`. Off the current reach
// path (lane FS's `SetupDirectories` is the wall) and chosen because among the
// candidates that are off it, this is the one whose whole transitive closure is
// already ported bar ONE function.
//
//
// ---------------------------------------------------------------------------
// STARTUP (0x1800e0d30)
// ---------------------------------------------------------------------------
//   if (this->keyMap) DeletingDtor(this->keyMap);     ; 0x180053440, unexported
//   p = MemAlloc(0x18, 4);                            ; operator new, INLINED --
//                                                       lock, Allocate(0x18,4),
//                                                       unlock. `MOV EDX,0x18`
//                                                       then `LEA R8D,[RDX-0x14]`
//                                                       is how the 4 is built.
//   if (p) { *(int*)p = 0; q = p + 8; *(void**)q = 0; *(int*)(q+8) = 0; }
//   this->keyMap = p;                                 ; the NULL is stored too
//   Load(this);                                       ; 0x1800e0c80
//   return true;                                      ; MOV AL,1 -- no failure arm
//
// The `if (this->keyMap)` arm is DEAD on the boot path: `Engine::Startup` calls
// this immediately after `??0Configuration@Input@@`, which zeroes the field
// (lane X). It is kept because `Startup` is a public export and nothing stops a
// second call, which is exactly the case the arm exists for. 0x180053440 is an
// unnamed scalar deleting destructor (`RtInput_KeyMap_DeletingDtor`, keymap_save.cpp)
// — the SECOND such site in this lane, and like the first it sits on an arm no boot takes.
//
// The inlined `operator new` is `MemAlloc(0x18, 4)` and nothing else: the same
// 20-instruction body as the unexported 0x180011640 (see
// src/engine/engine/engine_lifecycle.cpp, where it was read in full), spelled
// out at this call site rather than called.
//
// ---------------------------------------------------------------------------
// LOAD (0x1800e0c80) — three registry reads
// ---------------------------------------------------------------------------
//   KeyMap::Load(this->keyMap);                       ; ord 3203, keymap_load.cpp
//   Persistence::Get("Input", "MouseClipping", &this->mouseClipping);
//   Persistence::Get("Input", "Legacy",        &this->legacy);
//   Persistence::Get("Input", "MouseDisplay",  &this->mouseDisplay);  ; tail call
//
// All four literals were read with `read_memory`, not inferred from the header:
// 0x180155200 = `Input`, 0x180157d30 = `MouseClipping`, 0x180157d40 = `Legacy`,
// 0x180157d48 = `MouseDisplay`. They agree with src/engine/input/Input.h,
// which derived them from this function and from `Save`.
//
// `?Get@Persistence@IO@@QEBA_NPEBD0AEA_N@Z` is the BOOL overload and is already
// PORTED (src/engine/io/persistence.cpp:151). Retail does not test any of the
// three return values: `Get`'s contract is to leave the out-parameter untouched
// when the key is absent, so a missing key keeps the constructor's value.
//
// `KeyMap::Load` is passed `this->keyMap` UNGUARDED (0x1800e0c8d is a bare
// `MOV RCX,[RCX]`), on the one path where the allocation above may have failed.
// Kept faithful.
//
// ---------------------------------------------------------------------------
// THIS ROW CLOSES COMPLETELY — the only one in this lane that does
// ---------------------------------------------------------------------------
// Per the triage rule, and this time the answer is unqualified: EVERY callee in
// the closure is already ported.
//
//   ?MemAlloc@MemoryAllocator@System@@SAPEAX_KH@Z  Wave 9 lane E
//   ?Load@KeyMap@Input@@QEAAXXZ                    Wave 9 lane IOA,
//                                                  src/engine/input/keymap_load.cpp:240
//   ?Get@Persistence@IO@@QEBA_NPEBD0AEA_N@Z        Wave 4 lane C,
//                                                  src/engine/io/persistence.cpp:151
//
// `KeyMap::Load` was UNPORTED when this lane enumerated the queue in iteration
// 1; lane IOA landed it since. RE-CHECKING the stated blocker instead of
// trusting a two-iteration-old list is what turned this from "closes and
// exposes one row" into "closes" — the lesson this wave keeps paying for, in
// the cheap direction for once.
//
// So `Engine::Startup` step 14 is DONE: nothing behind it is null at cutover.
// The one address with no name (0x180053440) sits on an arm no boot takes.
//
// ORACLE: none for either. `Startup` allocates and publishes into the live
// configuration object and `Load` reads the registry; a replay would allocate a
// second KeyMap and leak the first.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <cstdio>

#include "input/Input.h"
#include "io/Persistence.h"   // ENDGAME B.3.9 SS1: Persistence::_singleton
#include "system/MemoryAllocator.h"

// The unexported KeyMap scalar deleting destructor 0x180053440, ported in
// keymap_save.cpp. Startup's re-entry arm used
// to reach it through `ResolveAddr(Module::Runtime, 0x180053440)`; that door
// resolved to this same body, so it is now a direct call (ENDGAME B.3.7 B5).
Input::KeyMap* RtInput_KeyMap_DeletingDtor(Input::KeyMap* self);

namespace {

// The four literals, read out of the DLL rather than retyped from a header.
const char kSection[]      = "Input";          // 0x180155200
const char kMouseClipping[] = "MouseClipping";  // 0x180157d30
const char kLegacy[]        = "Legacy";         // 0x180157d40
const char kMouseDisplay[]  = "MouseDisplay";   // 0x180157d48

}  // namespace

namespace Input {

// ============================================================================
// ?Load@Configuration@Input@@QEAAXXZ — retail 0x1800e0c80, ordinal 3199.
// (Was labelled UNEXPORTED here. It is not -- see the banner.)
// ============================================================================
void Configuration::Load() {
    // Unguarded, exactly as retail: `MOV RCX,[RCX]` then straight into the
    // call. Was `RT_EXPORT_ONCE("?Load@KeyMap@Input@@QEAAXXZ")`, which resolved
    // to keymap_load.cpp's body; now the member call (ENDGAME B.3.7 B5).
    _keyMap->Load();

    // Retail reads Persistence::_singleton ONCE into RBX and uses it for all
    // three calls. ENDGAME B.3.9 SS1 (B39_RULES D7): the cell is the class
    // static now, so the slot-existence guard this used to need is gone; the
    // null `this` it may still hold is retail's and stays unguarded.
    // IO::Persistence::Get(bool&) (0x1800b8290), called by name (B3R).
    const ::IO::Persistence* persistence = ::IO::Persistence::_singleton;
    persistence->Get(kSection, kMouseClipping, _mouseClipping);
    persistence->Get(kSection, kLegacy,        _legacy);
    persistence->Get(kSection, kMouseDisplay,  _mouseDisplay);
}

// ============================================================================
// ?Startup@Configuration@Input@@QEAA_NXZ                   retail 0x1800e0d30
// ============================================================================
bool Configuration::Startup() {
    if (_keyMap != nullptr) {
        // 0x1800e0d54. The unnamed KeyMap scalar deleting destructor; no export
        // in either build. Dead on the boot path (the ctor just zeroed this
        // field) -- see the banner.
        RtInput_KeyMap_DeletingDtor(_keyMap);
    }

    // `MemAlloc(0x18, 4)` with KeyMap's ctor INLINED: three zero stores.
    auto* keyMap = static_cast<KeyMap*>(
        System::MemoryAllocator::MemAlloc(sizeof(KeyMap), 4));   // 0x1800c7a50
    if (keyMap) {
        keyMap->_suspendCount = 0;
        keyMap->_root         = nullptr;
        keyMap->_count        = 0;
    }
    // The null is STORED, not rejected -- 0x1800e0db6/0x1800e0db9.
    _keyMap = keyMap;

    Load();
    return true;
}

}  // namespace Input
