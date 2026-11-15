// src/game/Tutorial/tutorialimage.cpp
// Clean reimplementation of TutorialImage vtable methods.
//
// TutorialImage (0x30 bytes) — base tutorial image class.
// Vtable: 0x140160a40, extends Object::External.
//
// Fields (from Ghidra decompilation + Serialize analysis):
//   +0x00: void*    vtable
//   +0x08: uint8_t  _extBase[0x18]  — External base fields
//   +0x20: void*    imageRef         — External* to image resource
//   +0x28: int32_t  imageWidth       — image width
//   +0x2C: int32_t  imageHeight      — image height
//
// Decompiled from Application-x64.exe using Ghidra.

#include "Tutorial/TutorialImage.h"
#include "ui/UserInterface.h"
#include "Tutorial/exe_bodies.h"
#include "object/TypeInfo.h"
#include "object/Internal.h"      // Object::detail::InternalMemAlloc / InternalMemFree
#include "Object/factory_registration.h"
#include <cstdint>
#include <cstdio>
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch D
#include <new>
#include "object/External.h"   // ENDGAME B.3.6 UI callers (K4)
#include "ui/Dialog.h"         // ENDGAME B.3.6 UI callers (K4)
#include "ui/Element.h"        // ENDGAME B.3.6 UI callers (K4)
#include "ui/ElementDescription.h"   // B8 RT2: ::Object::SerializeExternal<UI::ElementDescription> (the downcast needs it complete)

// ExternalPtr serializer for the imageRef field at +0x20 (exe 0x140027ad0).
// ENDGAME B.3.8 (B6-5): resolved per call, not through a lazily filled static
// (the loader thread serializes tutorial resources).
// called by name: ::Object::SerializeExternal<UI::ElementDescription> (object/External.h)

// ============================================================
// Construction / destruction (ENDGAME B.3.8 block GOAL)
// ============================================================
// The ctor inlined in create 0x140027980 / createInPlace 0x1400279e0:
// External(), the stamp (the compiler's), the image fields zeroed.
TutorialImage::TutorialImage() : imageRef(nullptr), imageWidth(0), imageHeight(0) {}

// ============================================================
// TutorialImage::Serialize (vtable +0x28, 0x1400e0330)
// ============================================================
// imageWidth (+0x28), imageHeight (+0x2C), then the ExternalPtr at +0x20.
bool TutorialImage::Serialize(IO::Stream& stream) {
    // The serializer, version 0.
    IO::Serializer serializer(stream, 0);

    serializer.Serialize(imageWidth, nullptr);
    serializer.Serialize(imageHeight, nullptr);

    // Serialize ExternalPtr (imageRef) at +0x20
    ::Object::SerializeExternal<UI::ElementDescription>(serializer, imageRef);
    return true;
}

// ============================================================
// TutorialImage::Reconstruct [6] 0x140027a30 / Destruct [7] 0x140001640
// ============================================================
Object::External* TutorialImage::Reconstruct() {
    this->~TutorialImage();
    return ::new (static_cast<void*>(this)) TutorialImage();
}
void TutorialImage::Destruct() { this->~TutorialImage(); }

// ============================================================
// TutorialImage::Construct (vtable +0x40, 0x1400279e0)
// ============================================================
Object::External* TutorialImage::Construct() {
    return ::new (static_cast<void*>(this)) TutorialImage();
}

// ============================================================
// TutorialImage::CreateElement (vtable +0x48, 0x1400e0300)
// ============================================================
// Dialog::CreateElement(dialog, retval, imageRef); returns the RETBUF (retail
// `return param_2`). NOT cosmetic: TutorialInstance::RefreshStepUI feeds the
// return value straight to TableElement::SetChild as the child pointer.
Object::InternalPtr<UI::Element>* TutorialImage::CreateElement(Object::InternalPtr<UI::Element>* ret,
                                                               const UI::Dialog* dialog) {
    *ret = dialog->CreateElement(Object::ExternalPtr<UI::ElementDescription>(imageRef));
    return ret;
}

// create 0x140027980: MemAlloc(0x30, 8), then the (inlined) ctor.
::Object::Base* TutorialImage::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(TutorialImage), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(::new (p) TutorialImage());
}
// createInPlace 0x1400279e0.
::Object::Base* TutorialImage::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(::new (storage) TutorialImage());
}

DEFINE_EXE_TYPE(TutorialImage, "Object::External")
