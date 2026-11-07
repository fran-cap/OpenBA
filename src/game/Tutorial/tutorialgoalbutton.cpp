// src/game/Goal/tutorialgoalbutton.cpp
// Clean reimplementation of TutorialGoalButton vtable methods.
//
// TutorialGoalButton (0x30 bytes) — tutorial goal that waits for a UI element state.
// Vtable: 0x140160800
// Extends TutorialGoal -> Object::External
//
// Fields (from Ghidra decompilation):
//   +0x20: System::String elementPath — dotted path to UI element (e.g. "DialogType.ElementName")
//   +0x28: bool invertCheck — selects which bit of element flags to check
//
// Check logic (Slot8):
//   Finds UI element via FUN_1400e1870 path lookup.
//   If found, wraps in InternalBasePtr, checks IsDerivedFrom(Object::Internal).
//   Reads flags at element+0x34:
//     invertCheck=true:  returns ~(flags >> 23) & 1  (bit 23 NOT set = success)
//     invertCheck=false: returns (flags >> 22) & 1   (bit 22 set = success)
//
// Decompiled from Application-x64.exe using Ghidra.

#include "Tutorial/TutorialGoalButton.h"
#include "ui/UserInterface.h"
#include "object/External.h"
#include "system/String.h"
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B; InternalMemAlloc / InternalMemFree
#include "Object/factory_registration.h"
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch D
#include "ui/Element.h"
#include "object/TypeInfo.h"
#include "Tutorial/exe_bodies.h"   // 0x1400e1870 FindUIElementByPath_Impl
#include <cstdint>
#include <new>

// ============================================================
// Construction / destruction (ENDGAME B.3.8 block GOAL)
// ============================================================
// ctor 0x1400dee90 (inlined in create 0x140027290 / createInPlace 0x140027300):
// External(), the TutorialGoal and TutorialGoalButton stamps (the compiler's),
// then the two fields zeroed.
TutorialGoalButton::TutorialGoalButton() : elementPath(), invertCheck(false), _pad29{} {}

// B4 X7-GAME-A (H5): the +0x20 String is a member: String() is the qword 0 the
// ctor stored; its ~String runs after the dtor body (retail 0x1400273b0).
System::String& TutorialGoalButton::ElementPath() {
    return elementPath;
}

// The dtor inside [0] 0x1400273b0: ~String on +0x20; the compiler re-stamps
// TutorialGoal and runs ~External.
TutorialGoalButton::~TutorialGoalButton() {}   // ~String +0x20: the member's

// ============================================================
// TutorialGoalButton::Serialize (vtable +0x28, 0x1400deda0)
// ============================================================
// 1. TutorialGoal::Serialize (its empty Serializer scope)
// 2. a Serializer over the stream: String +0x20, bool +0x28
bool TutorialGoalButton::Serialize(IO::Stream& stream) {
    TutorialGoal::Serialize(stream);   // QUALIFIED: the base body

    // The serializer for our fields.
    IO::Serializer ser(stream, 0);

    // elementPath (+0x20)
    ElementPath().Serialize(ser);

    // invertCheck (+0x28) — bool
    ser.Serialize(invertCheck, nullptr);
    return true;
}

// ============================================================
// TutorialGoalButton::Reconstruct [6] 0x140027350 / Destruct [7] 0x140001640
// ============================================================
// `this->~T()` (retail: slot 0, flags 0 -- destroy in place), then the ctor on `this`.
Object::External* TutorialGoalButton::Reconstruct() {
    this->~TutorialGoalButton();
    return ::new (static_cast<void*>(this)) TutorialGoalButton();
}
void TutorialGoalButton::Destruct() { this->~TutorialGoalButton(); }

// ============================================================
// TutorialGoalButton::IsComplete (vtable +0x40, 0x1400dece0)
// ============================================================
// Finds the UI element by path, checks its flag bits.
bool TutorialGoalButton::IsComplete(const System::Time* /*time*/, void* /*instance*/) {
    // Find UI element by elementPath (+0x20)
    Object::InternalPtr<UI::Element> elementIBP;
    FindUIElementByPath_Impl(&elementIBP, &ElementPath(), nullptr);   // 0x1400e1870

    UI::Element* element = elementIBP.Get();
    if (element == nullptr) {
        return false;
    }

    // Check the element IsDerivedFrom UI::Element
    bool isDerived = Object::IsDerivedFromNullSafe(element, UI::Element::TypeId());
    UI::Element* validElement = isDerived ? element : nullptr;
    if (validElement == nullptr) {
        return false;
    }

    // The element's flags (+0x34)
    uint32_t flags = validElement->_flags;

    if (invertCheck) {
        // Bit 23 NOT set = success
        return (flags & UI::kElementFlagDisabled) == 0;
    }
    // Bit 22 set = success
    return (flags & UI::kElementFlagWasActivated) != 0;
}

// ============================================================
// TutorialGoalButton::Construct (vtable +0x48, 0x140027300)
// ============================================================
Object::External* TutorialGoalButton::Construct() {
    return ::new (static_cast<void*>(this)) TutorialGoalButton();
}

// create 0x140027290: MemAlloc(0x30, 8), then the (inlined) ctor.
::Object::Base* TutorialGoalButton::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(TutorialGoalButton), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(::new (p) TutorialGoalButton());
}
// createInPlace 0x140027300.
::Object::Base* TutorialGoalButton::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(::new (storage) TutorialGoalButton());
}

DEFINE_EXE_TYPE(TutorialGoalButton, "TutorialGoal")
