#pragma once
// ============================================================================
// src/engine/ui/DialogControllerConfig.h — UI::DialogControllerConfig
// ============================================================================
//
// ENDGAME B.3 step 6, block B. A BASE FOR EXE CONFIG CLASSES and nothing more:
// it adds no field and no virtual to `Object::External`, and four of its eight
// slots stay `_purecall`.
//
// PARENT: `Object::External`, proved by
//   0x18003b440  ?_ParentTypeName@DialogControllerConfig@UI@@CAPEBDXZ -> "Object::External"
//
// `??_7DialogControllerConfig@UI@@6B@` @ 0x180156650, EIGHT slots
// (B36_SLOTS.md section 1):
//
//   [0] +0x00  0x18003b600  (unexported) vector deleting dtor
//   [1] +0x08  0x18003b470  ?TypeName@DialogControllerConfig@UI@@UEBAPEBDXZ
//   [2] +0x10  0x18003b450  ?Type@DialogControllerConfig@UI@@UEBAIXZ
//   [3] +0x18  0x18007ec28  _purecall   (External::GetFileVersion, still pure)
//   [4] +0x20  0x1800066e0  ?Compile@External@Object@@  INHERITED — not declared
//   [5] +0x28  0x18007ec28  _purecall   (Serialize)
//   [6] +0x30  0x18007ec28  _purecall   (Reconstruct)
//   [7] +0x38  0x18007ec28  _purecall   (Destruct)
//
// sizeof 0x20 = sizeof(Object::External): the deleting dtor's `MOV EDX,0x20`,
// and the four exe subclasses (ControlUIConfig, ResourceLimitUIConfig,
// StatusIconUIConfig, UpgradeUIConfig — EXE_SUBCLASSES.md 1C) place their own
// fields at +0x20 and derive from this class.
//
// ABSTRACT (slots 3/5/6/7 are pure in retail; its factory pair is
// `xor eax,eax ; ret`). ENDGAME B.3 step 9 (block UI-D, B39_RULES D1/D2): slot 0
// is the class's own vector deleting dtor, the minted
// `??_7DialogControllerConfig@UI@@6B@` and its RT_DELDTOR row are retired, and
// the factory pair is the class's own (dialogcontrollerconfig.cpp). Real
// (empty) constructor since ENDGAME B.3.8 block RB.
// ============================================================================

#include <cstdint>
#include <cstddef>

#include "../object/External.h"
#include "../object/Internal.h"   // Object::detail::InternalMemFree

namespace UI {

class DialogControllerConfig : public Object::External {
public:
    // [0] 0x18003b600 — MSVC's VECTOR deleting destructor, both arms exactly as
    // the retired RT_DELDTOR row ran them (`MOV EDX,0x20`, the element size).
    // The class's OWN (B9-4): the four exe configs declare theirs.
    // `??1DialogControllerConfig@UI@@UEAA@XZ` 0x18003b4f0. EMPTY, which is
    // retail's body (restamp + ~External, both the compiler's).
    ~DialogControllerConfig() override;

    // ENDGAME B.3.9 TI-UI: ONE reflection block (B39 D6); `_typeInfo` is the
    // class's own record (DEFINE_TYPE in the class's TU), no longer minted.
    // retail: TypeName 0x18003b470, Type 0x18003b450, CreateNewObject 0x18003b480, CreateInPlaceObject 0x18003b490
    DECLARE_TYPE(DialogControllerConfig)
public:
    // [3] GetFileVersion, [5] Serialize, [6] Reconstruct, [7] Destruct: PURE,
    //     inherited from Object::External unchanged. [4] Compile: External's.

    // ---- exported, NOT PORTED, no caller: declared, deliberately undefined --
    DialogControllerConfig(const DialogControllerConfig& other);            // 0x18003b520
    DialogControllerConfig& operator=(const DialogControllerConfig& other); // 0x18003b590
    // 0x18003b460 — the type id of the class's own record (dialogcontrollerconfig.cpp).
    // Retail's factory pair (B39_RULES D2): both are `XOR EAX,EAX ; RET` (the
    // class is abstract). Defined and registered in dialogcontrollerconfig.cpp.

protected:
    // ??0DialogControllerConfig@UI@@QEAA@XZ  0x18003b4a0 — the REAL
    // constructor (ENDGAME B.3.8 block RB, RULE B8-1). Retail's body is
    // `??0External` + its own vptr store and NOTHING else (the class adds no
    // field), so the compiler-emitted chain IS the whole port and the body is
    // empty. The four exe configs run it as their base constructor (the subclass
    // inventory: docs/history/endgame_b/b36_session/EXE_SUBCLASSES.md 1C).
    DialogControllerConfig() {}

    // RULE B5-15 / B6-7, COPIED from B36_SLOTS section 3:
    //   0x18003b430  ?_TypeName@       -> "DialogControllerConfig"
    //   0x18003b440  ?_ParentTypeName@ -> "Object::External"
};

static_assert(sizeof(DialogControllerConfig) == 0x20,
              "UI::DialogControllerConfig is 0x20 bytes (MOV EDX,0x20 in 0x18003b600; "
              "it adds nothing to Object::External)");
static_assert(sizeof(DialogControllerConfig) == sizeof(Object::External),
              "no field of its own");
static_assert(alignof(DialogControllerConfig) == 8, "qword-aligned (the vptr)");

}  // namespace UI
