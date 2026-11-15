// ============================================================================
// src/engine/ui/dialogcontrollerconfig.cpp — VTABLE BATCH 9
//   UI::DialogControllerConfig: the factory pair, the destructor and the type
//   record (slots 1/2 are DECLARE_TYPE's; 3/5/6/7 stay pure; 4 is External::Compile).
// ============================================================================
//
// ??_7DialogControllerConfig@UI@@6B@ (retail 0x180156650), 8 slots:
//   0  deleting dtor  0x18003b600  DialogControllerConfig.h (the class's own, B.3.9)
//   1  TypeName       0x18003b470  DECLARE_TYPE (DialogControllerConfig.h)
//   2  Type           0x18003b450  DECLARE_TYPE (DialogControllerConfig.h)
//   3  purecall       0x18007ec28  pure, inherited from Object::External
//   4  External::Compile 0x1800066e0  already ported
//   5,6,7 purecall    0x18007ec28  pure, inherited from Object::External
// plus ??1DialogControllerConfig@UI@@UEAA@XZ (0x18003b4f0) and the factory pair.
//
// ENDGAME B.3.9 block UI-D: the dtor door and the two slot doors, whose only
// reader was the minted `??_7DialogControllerConfig@UI@@6B@` (and its
// RT_DELDTOR row), are retired with it; the FACTUI1 row is replaced by the
// class's own factory pair below.
//
// LAYOUT: this class adds NOTHING over `Object::External` -- sizeof 0x20 is
// exactly `External`'s own size (src/engine/object/External.h), which is
// also what the deleting destructor's `MOV EDX,0x20` at 0x18003b632 says.
// `DialogController::_config` (src/engine/ui/DialogController.h) holds an
// `ExternalPtr<DialogControllerConfig>`, so this class exists purely to be
// pointed at -- it carries no fields of its own for AddController to fill.
//
// ??1DialogControllerConfig@UI@@UEAA@XZ   0x18003b4f0, two instructions:
//     *(void**)this = &??_7DialogControllerConfig@UI@@6B@;   (dead store)
//     Object::External::~External(this);   -- a tail jump in retail.
// In the port both are the compiler's (the empty destructor below).
// ============================================================================

#include <cstdint>

#include "ui/DialogControllerConfig.h"

namespace RtUI {

namespace {

}  // namespace

}  // namespace RtUI

// ============================================================================
// ENDGAME B.3.6 block B — UI::DialogControllerConfig's two own slots are
// members now (src/engine/ui/DialogControllerConfig.h). Neither reads
// `this`: both read the class's own BaseTypeInfo `_typeInfo` (+0x08 name, +0x00 type id).
// ============================================================================
namespace UI {

// ?CreateNewObject@ / ?CreateInPlaceObject@DialogControllerConfig@UI@@
// 0x18003b480 / 0x18003b490: each is `XOR EAX,EAX ; RET` — the class is
// abstract, so its reflection record builds nothing (the FACTUI1 row this
// replaces returned null for the same reason).
Object::Base* DialogControllerConfig::CreateNewObject() { return nullptr; }
Object::Base* DialogControllerConfig::CreateInPlaceObject(void*) { return nullptr; }

// ??1DialogControllerConfig@UI@@UEAA@XZ 0x18003b4f0: restamp + tail-jump
// ~External, both the compiler's — the empty body is retail's.
DialogControllerConfig::~DialogControllerConfig() {}

}  // namespace UI

// ============================================================================
// ENDGAME B.3.9 TI-UI (B39 D6): UI::DialogControllerConfig's own type record, built at CRT init
// (head-insert into Object::BaseTypeInfo::_firstTypeInfo). Literals copied from
// the retired minted row ?_typeInfo@DialogControllerConfig@UI@@ (0x1801f9e80): name "DialogControllerConfig", parent
// "Object::External" (retail ?_ParentTypeName@DialogControllerConfig@UI@@), no file extension; factories:
// the class's own.
// ============================================================================
DEFINE_TYPE(UI::DialogControllerConfig, "Object::External", nullptr)
