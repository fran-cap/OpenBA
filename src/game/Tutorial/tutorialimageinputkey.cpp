// src/game/Tutorial/tutorialimageinputkey.cpp
// Clean reimplementation of TutorialImageInputKey vtable methods.
//
// TutorialImageInputKey (0x38 bytes) -- input key image display.
// Vtable: 0x140160b20, extends TutorialImage.
//
// Fields (from Ghidra decompilation):
//   +0x00: vtable ptr
//   +0x08: External base (0x18 bytes)
//   +0x20: ExternalPtr (image resource ref)
//   +0x28: int32 (serialized)
//   +0x2C: int32 (serialized)
//   +0x30: int32 inputId (serialized via stream vtable)
//
// Decompiled from Application-x64.exe using Ghidra.

#include "Tutorial/TutorialImageInputKey.h"
#include "Object/factory_registration.h"
#include "system/String.h"
#include "ui/UserInterface.h"
#include "Tutorial/exe_bodies.h"
#include "object/TypeInfo.h"
#include <cstdint>
#include <cstdio>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch D
#include <new>
#include "object/External.h"   // ENDGAME B.3.6 UI callers (K4)
#include "ui/Dialog.h"         // ENDGAME B.3.6 UI callers (K4)
#include "ui/Element.h"        // ENDGAME B.3.6 UI callers (K4)
#include "ui/TextElement.h"    // ENDGAME B.3.6 UI callers (K4)
#include "input/Input.h"       // ENDGAME B.3.7 callers (K3)
#include "ui/String.h"
#include "ui/ElementDescription.h"   // B8 RT2: ::Object::SerializeExternal<UI::ElementDescription> (the downcast needs it complete)

// hotkeytooltipui.cpp - the existing byte-faithful port of exe 0x140099c20.
namespace Game { void Hotkey_BuildKeyName(UI::String* retval, int device, int keyIndex); }

// ============================================================
// Exe helper, called by name (ENDGAME B.3.8, B6-5). The External ctor
// dispatch slot and the two vtable addresses this file cached lazily are
// gone: the compiler runs External() and stamps both tables.
// ============================================================

// TutorialImage_sub_7ad0 -- ExternalPtr serialize helper (exe 0x140027ad0)
// called by name: ::Object::SerializeExternal<UI::ElementDescription> (object/External.h)

// ============================================================
// Construction / destruction (ENDGAME B.3.8 block GOAL)
// ============================================================
// ctor 0x1400e08f0: External(), TutorialImage's stamp + zeroed fields
// (TutorialImage()), the own stamp. +0x30 is NOT written (retail's).
TutorialImageInputKey::TutorialImageInputKey() {}

// ============================================================
// Slot5: Serialize (0x1400e07f0)
// ============================================================
bool TutorialImageInputKey::Serialize(IO::Stream& stream) {
    // First serializer block: serialize +0x28, +0x2C as ints, then ExternalPtr at +0x20
    {
        IO::Serializer serializer(stream, 0);
        serializer.Serialize(imageWidth, nullptr);
        serializer.Serialize(imageHeight, nullptr);
        ::Object::SerializeExternal<UI::ElementDescription>(serializer, imageRef);
    }

    // Second serializer block: +0x30 (inputId) through the UINT door. Serializer
    // +0x08 is NOT "the internal stream": the Stream is at Serializer+0x00, and
    // +0x08 is the READERWRITER, whose vtable +0x98 is rw_vtbl::kScalarUInt.
    IO::Serializer serializer2(stream, 0);
    serializer2.Serialize(_inputId, nullptr);
    return true;
}

// ============================================================
// Slot6: Reconstruct (0x140027f50) / Slot8: Construct (0x140027f00)
// ============================================================
Object::External* TutorialImageInputKey::Reconstruct() {
    this->~TutorialImageInputKey();
    return ::new (static_cast<void*>(this)) TutorialImageInputKey();
}
Object::External* TutorialImageInputKey::Construct() {
    return ::new (static_cast<void*>(this)) TutorialImageInputKey();
}

// ============================================================
// Slot9: CreateElement (0x1400e06e0)
// ============================================================
// Creates a UI element from imageRef, finds a TextElement child,
// looks up the bound key name from KeyMap using inputId at +0x30,
// and sets the display text on the TextElement.
// Returns retIBP, exactly as retail does (every TutorialImage*::Slot9 ends
// `return param_2`). This is NOT cosmetic: TutorialInstance::RefreshStepUI
// feeds the return value straight to TableElement::SetChild as the child
// pointer, so a void return leaves whatever the last call left in RAX and the
// table gets a garbage child, which faults later inside UI::UserInterface::Update.
Object::InternalPtr<UI::Element>* TutorialImageInputKey::CreateElement(Object::InternalPtr<UI::Element>* retIBP,
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

    // 7. The KeyMap (Input::Configuration +0x00)
    const Input::KeyMap* keymap = nullptr;
    if (Input::Configuration::_singleton != nullptr) {
        Input::Configuration* step2 = Input::Configuration::_singleton;
        if (step2) keymap = step2->_keyMap;
    }

    // 8. Get device type and input index for inputId at +0x30
    int inputId = static_cast<int>(_inputId);
    int deviceType = 0;
    if (keymap)
        deviceType = keymap->GetDeviceForId(inputId, 0);

    int inputIndex = 0;
    if (keymap)
        inputIndex = keymap->GetInputForId(inputId, 0);

    // 9. Get key name as UI::String (a zeroed buffer, as the tree's was)
    UI::String uiStr(Object::ExternalPtr<UI::StringTable>(nullptr), 0);
    // 0x140099c20 is ported byte-for-byte as Hotkey_BuildKeyName (hotkeytooltipui.cpp);
    // retail's param_1 is the KeyMap container, and that port reads the same keyNames
    // table through g_gameContainer, so this is the identical call, not a fourth copy.
    Game::Hotkey_BuildKeyName(&uiStr, deviceType, inputIndex);

    // 10. Set text on the TextElement
    if (textRef)
        textRef->SetText(&uiStr);
    return retIBP;
}

// create 0x140027ea0: MemAlloc(0x38, 8), then the ctor.
::Object::Base* TutorialImageInputKey::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(TutorialImageInputKey), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(::new (p) TutorialImageInputKey());
}
// createInPlace 0x140027f00.
::Object::Base* TutorialImageInputKey::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(::new (storage) TutorialImageInputKey());
}

DEFINE_EXE_TYPE(TutorialImageInputKey, "TutorialImage")
