// src/game/Tutorial/tutorialimagehotkey.cpp
// Clean reimplementation of TutorialImageHotKey vtable methods.
//
// TutorialImageHotKey (0x48 bytes) — displays hot key image in tutorial steps.
// Vtable: 0x140160ab0
// Extends TutorialImage -> Object::External
//
// Fields (from Ghidra decompilation of Ctor at 0x1400e0680):
//   +0x00: vtable pointer
//   +0x08: External base (0x18 bytes)
//   +0x20: void* imageRef — External pointer to image resource (TutorialImage base)
//   +0x28: int32_t field28 — base image param 1 (serialized)
//   +0x2c: int32_t field2c — base image param 2 (serialized)
//   +0x30: System::String hotkeyName — hotkey identifier string
//   +0x38: int32_t field38 — initialized to -1
//   +0x3c: padding
//   +0x40: void* field40 — zeroed (pointer, used in Slot9 for KeyMap lookup)
//
// Decompiled from Application-x64.exe using Ghidra.

#include "Tutorial/TutorialImageHotKey.h"
#include "Object/factory_registration.h"
#include "ui/UserInterface.h"
#include "Tutorial/exe_bodies.h"
#include "object/TypeInfo.h"
#include "system/String.h"
#include <cstdint>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch D
#include <new>
#include "object/External.h"   // ENDGAME B.3.6 UI callers (K4)
#include "ui/Dialog.h"         // ENDGAME B.3.6 UI callers (K4)
#include "ui/Element.h"        // ENDGAME B.3.6 UI callers (K4)
#include "ui/TextElement.h"    // ENDGAME B.3.6 UI callers (K4)
#include "input/Input.h"       // ENDGAME B.3.7 callers (K3)
#include "ui/String.h"
#include "Game/Toolbar.h"
#include "ui/ElementDescription.h"   // B8 RT2: ::Object::SerializeExternal<UI::ElementDescription> (the downcast needs it complete)

// The resolved Toolbar entry (NamedExternalRef::ResolveToolbar validated the
// type: a downcast within Object::External's hierarchy).
static const Toolbar* ResolvedToolbar(const NamedExternalRef& ref) {
    const Object::External* found = ref.resolved;
    return static_cast<const Toolbar*>(found);
}

// hotkeytooltipui.cpp - the existing byte-faithful port of exe 0x140099c20.
namespace Game { void Hotkey_BuildKeyName(UI::String* retval, int device, int keyIndex); }

// ============================================================
// Exe helpers, resolved PER CALL (ENDGAME B.3.8, B6-5: the loader thread
// serializes tutorial resources; no first-call cache)
// ============================================================

// TutorialImage External ref serialize helper (0x140027ad0)
// called by name: TutBase_SerializeImageRef (Tutorial/exe_bodies.h)

// FUN_140027de0 — resolves hotkey string to External ref via ExternalFactory
// called by name: TutBase_ResolveToolbarRef (Tutorial/exe_bodies.h)

// ============================================================
// Construction / destruction (ENDGAME B.3.8 block GOAL)
// ============================================================
// ctor 0x1400e0680: External(), the TutorialImage stamp + its two zeroed
// fields (TutorialImage()), the own stamp, then the derived fields.
TutorialImageHotKey::TutorialImageHotKey()
    : hotkeyRef() {}

// The dtor inside [0] 0x140027d70: ~String +0x30 (0x140027da0, B4 Y GAMEA: the
// member's, after this empty body); the compiler re-stamps TutorialImage and
// runs ~External.
TutorialImageHotKey::~TutorialImageHotKey() {}

// ============================================================
// TutorialImageHotKey::Serialize (vtable +0x28, 0x1400e0540)
// ============================================================
// Phase 1: the TutorialImage fields (int +0x28, int +0x2c, external ref +0x20)
// Phase 2: the hot key string +0x30, resolved on load
bool TutorialImageHotKey::Serialize(IO::Stream& stream) {
    // Phase 1: Base TutorialImage fields
    {
        IO::Serializer ser1(stream, 0);
        ser1.Serialize(imageWidth, nullptr);
        ser1.Serialize(imageHeight, nullptr);
        ::Object::SerializeExternal<UI::ElementDescription>(ser1, imageRef);
    }

    // Phase 2: HotKey-specific fields
    IO::Serializer ser2(stream, 0);

    // Serialize String at +0x30
    hotkeyRef.name.Serialize(ser2);

    // On a read, resolve the hotkey string to an External ref: retail tests the
    // STREAM DIRECTION, not the record version (0x1400e0540: [[serializer]+8] == 0).
    if (static_cast<int>(ser2.GetDirection()) == 0) {
        hotkeyRef.ResolveToolbar();
    }
    return true;
}

// ============================================================
// TutorialImageHotKey::Reconstruct [6] 0x140027d50 / Construct [8] 0x140027d40
// ============================================================
Object::External* TutorialImageHotKey::Reconstruct() {
    this->~TutorialImageHotKey();
    return ::new (static_cast<void*>(this)) TutorialImageHotKey();
}
Object::External* TutorialImageHotKey::Construct() {
    return ::new (static_cast<void*>(this)) TutorialImageHotKey();
}

// ============================================================
// TutorialImageHotKey::Slot9 (vtable +0x48, 0x1400e0420)
// ============================================================
// Creates a UI element from imageRef, finds a TextElement child,
// resolves the hotkey ref, looks up the bound key name from KeyMap,
// and sets the display text on the TextElement.
Object::InternalPtr<UI::Element>* TutorialImageHotKey::CreateElement(Object::InternalPtr<UI::Element>* retIBP,
                                                                     const UI::Dialog* dialog) {
    // 1. Create UI element from imageRef at +0x20
    *retIBP = dialog->CreateElement(Object::ExternalPtr<UI::ElementDescription>(imageRef));

    // 2. CheckPtr on the returned IBP

    // 3. FindElementType — find TextElement child
    UI::Element* element = retIBP->Get();
    UI::Element* foundObj = element->FindElementType(UI::TextElement::TypeId()).Get();

    // 4. CheckPtr on found element

    // 5. IsDerivedFrom check — verify it's a TextElement
    UI::TextElement* textRef = nullptr;
    if (foundObj) {
        if (Object::IsDerivedFromNullSafe(foundObj, UI::TextElement::TypeId()))
            textRef = static_cast<UI::TextElement*>(foundObj);
    }

    // 7. Resolve hotkey ref from string at +0x30
    hotkeyRef.ResolveToolbar();

    // 8. The KeyMap (Input::Configuration +0x00)
    const Input::KeyMap* keymap = nullptr;
    if (Input::Configuration::_singleton != nullptr) {
        Input::Configuration* step2 = Input::Configuration::_singleton;
        if (step2) keymap = step2->_keyMap;
    }

    // 9. Read key ID from the resolved Toolbar's +0x40
    const Toolbar* resolvedRef = ResolvedToolbar(hotkeyRef);
    int keyId = 0;
    if (resolvedRef)
        keyId = resolvedRef->shortcutKey;

    // 10. Get device type for this key
    int deviceType = 0;
    if (keymap)
        deviceType = keymap->GetDeviceForId(keyId, 0);

    // 11. Resolve hotkey ref again (matches original)
    hotkeyRef.ResolveToolbar();

    // 12. Get input index for this key
    int inputIndex = 0;
    if (keymap)
        inputIndex = keymap->GetInputForId(keyId, 0);

    // 13. Get key name as UI::String (a zeroed buffer, as the tree's was)
    UI::String uiStr(Object::ExternalPtr<UI::StringTable>(nullptr), 0);
    // LANE1 row 36: 0x140099c20 is unregistered, so fn_GetInputKeyName was
    // permanently null and every tutorial key name rendered blank. The function is
    // already ported byte-for-byte as Hotkey_BuildKeyName (hotkeytooltipui.cpp);
    // retail's param_1 is the KeyMap container and that port looks up the same
    // *g_pKeyMapSingleton, so this is the identical call, not a fourth copy.
    Game::Hotkey_BuildKeyName(&uiStr, deviceType, inputIndex);

    // 14. Set text on the TextElement
    if (textRef)
        textRef->SetText(&uiStr);

    return retIBP;
}

// create 0x140027d00: MemAlloc(0x48, 8), then the ctor.
::Object::Base* TutorialImageHotKey::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(TutorialImageHotKey), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(::new (p) TutorialImageHotKey());
}
// createInPlace 0x140027d40.
::Object::Base* TutorialImageHotKey::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(::new (storage) TutorialImageHotKey());
}

DEFINE_EXE_TYPE(TutorialImageHotKey, "TutorialImage")
