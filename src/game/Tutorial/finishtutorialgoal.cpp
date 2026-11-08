// src/game/Tutorial/finishtutorialgoal.cpp
// FinishTutorialGoal (0x38 bytes) — achievement goal for completing the tutorial.
// ENDGAME B.3.8 block GOAL: a REAL class (Tutorial/FinishTutorialGoal.h) with a
// compiler-generated vtable; the retail evidence per slot is in the header.
//
// Fields (from Ghidra decompilation):
//   +0x30: void*    tutorialRef      — ExternalPtr<Tutorial>
//
// Decompiled from Application-x64.exe using Ghidra.

#include "Tutorial/FinishTutorialGoal.h"
#include "Tutorial/FinishTutorialInstance.h"
#include "object/External.h"
#include "object/TypeInfo.h"
#include "Tutorial/exe_bodies.h"   // 0x1400262b0 TutWave_SerializeTutorialRef
#include "object/Internal.h"      // Object::detail::InternalMemAlloc / InternalMemFree
#include "Object/factory_registration.h"
#include <cstdint>
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch D
#include "achievement/Goal.h"   // ENDGAME B.3.7 callers (K2): QUALIFIED base calls
#include <new>
#include "Tutorial/Tutorial.h"   // B8 RT2: ::Object::SerializeExternal<Tutorial> (the downcast needs it complete)

// ctor 0x1400d79e0 (inlined in create 0x1400260f0): Goal(), the vptr, the
// tutorial reference zeroed.
FinishTutorialGoal::FinishTutorialGoal() : _tutorial(nullptr) {}

// ============================================================
// FinishTutorialGoal::Serialize (vtable +0x28, 0x1400d7970)
// ============================================================
// Goal::Serialize, then the tutorial ExternalPtr at +0x30 through the
// tutorial-specific ExternalPtr serializer.
bool FinishTutorialGoal::Serialize(IO::Stream& stream) {
    // Achievement::Goal::Serialize (slot 5, 0x18009ee20) — QUALIFIED (B7-13).
    Achievement::Goal::Serialize(stream);

    // The serializer.
    IO::Serializer serializer(stream, 0);

    // Serialize the tutorial External* at +0x30
    ::Object::SerializeExternal<Tutorial>(serializer, _tutorial);   // 0x1400262b0

    return true;
}

// ============================================================
// FinishTutorialGoal::Reconstruct [6] 0x1400261a0 / Destruct [7] 0x140001640
// ============================================================
// retail 0x1400261a0: vtable[0](this, 0) (`~T()` (slot 0, flags 0): destroy in place),
// then the ctor on `this`.
Object::External* FinishTutorialGoal::Reconstruct() {
    this->~FinishTutorialGoal();
    return ::new (static_cast<void*>(this)) FinishTutorialGoal();
}
void FinishTutorialGoal::Destruct() { this->~FinishTutorialGoal(); }

// ============================================================
// FinishTutorialGoal::GetInstanceType [9] 0x1400261f0
// ============================================================
// `MOV EDX,[TypeInfo_FinishTutorialInstance]; MOV [RDX_retval],EDX; MOV RAX,RDX`
// — the B7-4 retslot shape of Achievement::Goal::GetInstanceType.
Object::Type* FinishTutorialGoal::GetInstanceType(Object::Type* ret) const {
    ret->_type = FinishTutorialInstance::TypeId();
    return ret;
}

// ============================================================
// FinishTutorialGoal::Construct (vtable +0x50, 0x140026150)
// ============================================================
Object::External* FinishTutorialGoal::Construct() {
    return ::new (static_cast<void*>(this)) FinishTutorialGoal();
}

// create 0x1400260f0: MemAlloc(0x38, 8), then the (inlined) ctor.
::Object::Base* FinishTutorialGoal::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(FinishTutorialGoal), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(::new (p) FinishTutorialGoal());
}
// createInPlace 0x140026150.
::Object::Base* FinishTutorialGoal::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(::new (storage) FinishTutorialGoal());
}

DEFINE_EXE_TYPE(FinishTutorialGoal, "Achievement::Goal")
