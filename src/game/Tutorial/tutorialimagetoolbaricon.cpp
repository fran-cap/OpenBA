// src/game/Tutorial/tutorialimagetoolbaricon.cpp
// Clean reimplementation of TutorialImageToolbarIcon vtable methods.
//
// TutorialImageToolbarIcon (0x48 bytes) -- toolbar icon image display.
// Vtable: 0x140160b90, extends TutorialImage.
//
// Fields (from Ghidra decompilation):
//   +0x00: vtable ptr
//   +0x08: External base (0x18 bytes)
//   +0x20: ExternalPtr (image resource ref)
//   +0x28: int32 (serialized)
//   +0x2C: int32 (serialized)
//   +0x30: System::String (toolbar name, serialized)
//   +0x38: int32 string tag / ExternalFactory version (-1 init)
//   +0x40: void* resolved Toolbar ptr (cached)
//
// Decompiled from Application-x64.exe using Ghidra.

#include "Tutorial/TutorialImageToolbarIcon.h"
#include "Object/factory_registration.h"
#include "ui/UserInterface.h"
#include "system/Filename.h"
#include "system/String.h"
#include "Tutorial/exe_bodies.h"
#include "object/TypeInfo.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch D
#include <new>
#include "object/External.h"   // ENDGAME B.3.6 UI callers (K4)
#include "ui/Dialog.h"         // ENDGAME B.3.6 UI callers (K4)
#include "ui/Element.h"        // ENDGAME B.3.6 UI callers (K4)
#include "ui/Sprite.h"         // ENDGAME B.3.6 UI callers (K4)
#include "ui/ImageElement.h"   // ENDGAME B.3.6 UI callers (K4)
#include "Component/ToolbarDescription.h"
#include "Component/ComponentDescription.h"
#include "Component/Description.h"
#include "Game/Toolbar.h"
#include "ui/ElementDescription.h"   // B8 RT2: ::Object::SerializeExternal<UI::ElementDescription> (the downcast needs it complete)

// ============================================================
// Exe helpers, resolved PER CALL (ENDGAME B.3.8, B6-5). The External ctor
// dispatch slot and the two vtable addresses this file cached lazily are
// gone: the compiler runs External() and stamps both tables.
// ============================================================

// TutorialImage_sub_7ad0 -- ExternalPtr serialize helper (exe 0x140027ad0)
// called by name: TutBase_SerializeImageRef (Tutorial/exe_bodies.h)

// FUN_140027de0 -- resolves toolbar from String at +0x30
// called by name: TutBase_ResolveToolbarRef (Tutorial/exe_bodies.h)

// ============================================================
// Construction / destruction (ENDGAME B.3.8 block GOAL)
// ============================================================
// ctor 0x1400e0c30: External(), TutorialImage's stamp + zeroed fields
// (TutorialImage()), the own stamp, then String +0x30 = 0, tag -1, ref null.
TutorialImageToolbarIcon::TutorialImageToolbarIcon()
    : _toolbarRef() {}

// The dtor inside [0] 0x140028090: ~String +0x30 (0x1400280c0, B4 Y GAMEA: the
// member's, after this empty body); the compiler re-stamps TutorialImage and
// runs ~External.
TutorialImageToolbarIcon::~TutorialImageToolbarIcon() {}

// The resolved Toolbar entry (NamedExternalRef::ResolveToolbar validated the
// type: a downcast within Object::External's hierarchy).
static const Toolbar* ResolvedToolbar(const NamedExternalRef& ref) {
    const Object::External* found = ref.resolved;
    return static_cast<const Toolbar*>(found);
}

// ============================================================
// Slot5: Serialize (0x1400e0af0)
// ============================================================
bool TutorialImageToolbarIcon::Serialize(IO::Stream& stream) {
    // First serializer block: serialize +0x28, +0x2C as ints, then ExternalPtr at +0x20
    {
        IO::Serializer serializer(stream, 0);
        serializer.Serialize(imageWidth, nullptr);
        serializer.Serialize(imageHeight, nullptr);
        ::Object::SerializeExternal<UI::ElementDescription>(serializer, imageRef);
    }

    // Second serializer block: Filename ctor, String::Serialize, conditional toolbar resolve
    IO::Serializer serializer2(stream, 0);

    // Filename temp retail's compiler emitted for this scope.
    System::Filename filenameBuf;
    (void)filenameBuf;

    // String::Serialize on +0x30
    _toolbarRef.name.Serialize(serializer2);

    // If loading (serializer stream mode == 0), resolve toolbar
    if (serializer2._stream) {
        int streamMode = static_cast<int>(serializer2.GetDirection());   // IO::Serializer::GetDirection, retail 0x180006210
        if (streamMode == 0) {
            _toolbarRef.ResolveToolbar();
        }
    }
    return true;
}

// ============================================================
// Slot6: Reconstruct (0x140028070) / Slot8: Construct (0x140028060)
// ============================================================
Object::External* TutorialImageToolbarIcon::Reconstruct() {
    this->~TutorialImageToolbarIcon();
    return ::new (static_cast<void*>(this)) TutorialImageToolbarIcon();
}
Object::External* TutorialImageToolbarIcon::Construct() {
    return ::new (static_cast<void*>(this)) TutorialImageToolbarIcon();
}

// ============================================================
// Slot9: CreateElement (0x1400e0940)
// ============================================================
// Creates a UI element from imageRef, finds an ImageElement child,
// resolves toolbar ref, constructs a Sprite from the toolbar icon data,
// and sets the sprite on the ImageElement.
// If the Toolbar's +0x58 template is null, uses its +0x28 (spriteSheet) and
// +0x38 (spriteName) directly. Otherwise walks the template's
// sub-descriptions for its ToolbarDescription and uses its +0x20/+0x28.
// Returns retIBP, exactly as retail does (every TutorialImage*::Slot9 ends
// `return param_2`). This is NOT cosmetic: TutorialInstance::RefreshStepUI
// feeds the return value straight to TableElement::SetChild as the child
// pointer, so a void return leaves whatever the last call left in RAX and the
// table gets a garbage child, which faults later inside UI::UserInterface::Update.
Object::InternalPtr<UI::Element>* TutorialImageToolbarIcon::CreateElement(Object::InternalPtr<UI::Element>* retIBP,
                                                                          const UI::Dialog* dialog) {
    // 1. Create UI element from imageRef at +0x20
    *retIBP = dialog->CreateElement(Object::ExternalPtr<UI::ElementDescription>(imageRef));

    // 2. CheckPtr on the returned IBP

    // 3. FindElementType — find ImageElement child
    UI::Element* element = retIBP->Get();
    UI::Element* foundObj = element->FindElementType(UI::ImageElement::TypeId()).Get();

    // 4. CheckPtr on found element

    // 5. IsDerivedFrom check — verify it's an ImageElement
    UI::ImageElement* imageElem = nullptr;
    if (foundObj) {
        if (Object::IsDerivedFromNullSafe(foundObj, UI::ImageElement::TypeId()))
            imageElem = static_cast<UI::ImageElement*>(foundObj);
    }

    // 7. Resolve toolbar ref from String at +0x30
    _toolbarRef.ResolveToolbar();

    // 8. Get resolved toolbar object at +0x40
    const Toolbar* toolbar = ResolvedToolbar(_toolbarRef);
    UI::SpriteSheet* spriteSheet = nullptr;
    const System::String* spriteName = nullptr;

    if (toolbar) {
        const ComponentDescription* componentList = toolbar->componentDescription;

        if (componentList == nullptr) {
            // No template — use toolbar's direct sprite data
            _toolbarRef.ResolveToolbar();
            _toolbarRef.ResolveToolbar();

            toolbar = ResolvedToolbar(_toolbarRef); // re-read after resolve
            spriteSheet = toolbar->spriteSheet;
            spriteName = &toolbar->spriteName;
        } else {
            // Has a template — walk it for its ToolbarDescription
            _toolbarRef.ResolveToolbar();

            componentList = toolbar->componentDescription;
            int count = componentList->_subDescCount;
            Description** arrayData = componentList->_subDescs;

            const ToolbarDescription* matchedComp = nullptr;
            for (int i = 0; i < count; i++) {
                Description* comp = arrayData[i];
                if (!comp) continue;

                // GetTypeId (vtable +0x10)
                uint32_t typeId = comp->Type();

                if (typeId == ToolbarDescription::TypeId()) {
                    // IsDerivedFrom check
                    if (Object::IsDerivedFromNullSafe(comp, ToolbarDescription::TypeId())) {
                        matchedComp = static_cast<const ToolbarDescription*>(comp);
                    }
                    break;
                }
            }

            if (matchedComp) {
                spriteSheet = matchedComp->_spriteSheet;
                spriteName = &matchedComp->_name;
            } else {
                // Fallback: use toolbar's direct data (matchedComp stays null)
                spriteSheet = toolbar->spriteSheet;
                spriteName = &toolbar->spriteName;
            }
        }
    }

    // 9. Construct Sprite from spriteSheet + spriteName ({null, 0} otherwise,
    // the tree's zeroed buffer)
    UI::Sprite sprite(Object::ExternalPtr<UI::SpriteSheet>(nullptr), 0);
    if (spriteName)
        sprite = UI::Sprite(Object::ExternalPtr<UI::SpriteSheet>(spriteSheet), *spriteName);

    // 10. Set sprite on the ImageElement
    if (imageElem)
        imageElem->SetSprite(&sprite);
    return retIBP;
}

// create 0x140028020: MemAlloc(0x48, 8), then the ctor.
::Object::Base* TutorialImageToolbarIcon::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(TutorialImageToolbarIcon), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(::new (p) TutorialImageToolbarIcon());
}
// createInPlace 0x140028060.
::Object::Base* TutorialImageToolbarIcon::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(::new (storage) TutorialImageToolbarIcon());
}

DEFINE_EXE_TYPE(TutorialImageToolbarIcon, "TutorialImage")
