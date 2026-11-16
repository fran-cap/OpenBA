// src/game/Tutorial/tutorialsetupdialog.cpp
// TutorialSetupDialog — the tutorial setup that hides the UI except the
// elements a step names and opens a highlight on the one it wants clicked.
//
// TutorialSetupDialog (0x78 bytes), table 0x140160d20, TutorialSetup ->
// Object::External (NOT a DialogInstance). ENDGAME B.3.8 block GOAL: a real
// class (Tutorial/TutorialSetupDialog.h); the hand-built 48-slot table in
// tutorial_dialog_vtables.cpp is gone. Field map: the header.

#include "Tutorial/TutorialSetupDialog.h"
#include "ui/DialogInstance.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "system/Filename.h"
#include "system/String.h"
#include "object/External.h"   // ENDGAME B.3.3 step 6A
#include "object/Internal.h"   // Object::detail::InternalMemAlloc / InternalMemFree
#include "collection/Collection.h"
#include "Object/factory_registration.h"
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch D
#include "ui/UserInterface.h"   // ENDGAME B.3.6 UI callers (K4)
#include "Component/ComponentDescription.h"
#include "Entity/ComponentEntity.h"
#include "object/TypeInfo.h"
#include "Dialog/exe_bodies.h"
#include "Tutorial/exe_bodies.h"    // FindDialogByName_Impl
#include "Game/PlayerToolManager.h"
#include <cstdint>
#include <new>
#include "ui/Dialog.h"   // B8 RT2: ::Object::SerializeExternal<UI::Dialog> (the downcast needs it complete)

// ============================================================
// Construction / destruction (ENDGAME B.3.8 block GOAL)
// ============================================================
// ctor 0x1400e2cc0: External() (the base's real ctor), the TutorialSetup and
// TutorialSetupDialog stamps (the compiler's), +0x20..+0x58 zeroed, +0x60 = -1,
// +0x68 = 0, +0x70 = 0.
TutorialSetupDialog::TutorialSetupDialog()
    : shownPaths(), dialogNames(),
      highlightPath(), arrowRotation(0), _pad4C{}, highlightDialog(nullptr),
      toolbarRef(),
      toolbarIndex(0), _pad74{} {}

// B4 X7-GAME-A (H5): the +0x40 String is a member: String() is the qword 0 the
// ctor stored; its ~String runs after the dtor body (retail 0x1400e2bf0:
// ~String +0x58, ~String +0x40, then the Arrays +0x30 / +0x20).
System::String& TutorialSetupDialog::HighlightPath() {
    return highlightPath;
}

// The dtor 0x1400e2bf0: ~String +0x58, ~String +0x40, then the two String
// arrays (+0x30, then +0x20: the members, destroyed by the compiler after this body) — each an inlined Collection::Array<System::String>
// ::~Array (~String per element, MemFree the buffer, zero the header). The
// compiler re-stamps TutorialSetup and runs ~External (the tree ran a guarded
// ~Internal instead).
TutorialSetupDialog::~TutorialSetupDialog() {
    // B4 Y GAMEA: +0x58 toolbarRef's ~String (0x1400e2c20), then +0x40 (0x1400e2c2b)
    // and the two Arrays: the members', after this empty body.
}

// ============================================================
// Reconstruct [6] 0x140028600 / Destruct [7] 0x140001640 / Construct [11] 0x1400285f0
// ============================================================
// Reconstruct: vtable[0](this, 0), then the ctor 0x1400e2cc0 on `this`.
Object::External* TutorialSetupDialog::Reconstruct() {
    this->~TutorialSetupDialog();
    return ::new (static_cast<void*>(this)) TutorialSetupDialog();
}
void TutorialSetupDialog::Destruct() { this->~TutorialSetupDialog(); }
// Construct (also the createInPlace): `TEST RCX,RCX; JZ; CALL ctor`.
Object::External* TutorialSetupDialog::Construct() {
    return ::new (static_cast<void*>(this)) TutorialSetupDialog();
}

// ============================================================
// TutorialSetupDialog::Serialize (vtable +0x28, 0x1400e2a60)
// ============================================================
// Two String arrays (count, resize on load, elements), the highlight path,
// the highlight Dialog ExternalPtr, the arrow rotation, the toolbar
// ExternalRef (resolved on load) and the toolbar index. Retail does NOT call
// the base TutorialSetup::Serialize here. Returns true (MOV AL,1).
bool TutorialSetupDialog::Serialize(IO::Stream& stream) {
    IO::Serializer ser(stream, 0);

    // 1st String array (+0x20)
    int count1 = shownPaths.GetSize();
    ser.Serialize(count1, nullptr);
    if (static_cast<int>(ser.GetDirection()) == 0) {   // (IO::Serializer::GetDirection, retail 0x180006210)
        shownPaths.SetSize(count1);   // Array<System::String>::SetSize 0x140028860
    }
    if (count1 > 0) {
        System::String* p = shownPaths.GetData();
        for (uint32_t i = 0; i < static_cast<uint32_t>(count1); ++i, ++p) {
            p->Serialize(ser);
        }
    }

    // 2nd String array (+0x30)
    int count2 = dialogNames.GetSize();
    ser.Serialize(count2, nullptr);
    if (static_cast<int>(ser.GetDirection()) == 0) {
        dialogNames.SetSize(count2);   // 0x140028860
    }
    if (count2 > 0) {
        System::String* p = dialogNames.GetData();
        for (uint32_t i = 0; i < static_cast<uint32_t>(count2); ++i, ++p) {
            p->Serialize(ser);
        }
    }

    HighlightPath().Serialize(ser);
    ::Object::SerializeExternal<UI::Dialog>(ser, highlightDialog);   // ExternalPtr<UI::Dialog> (0x140028650)
    ser.Serialize(arrowRotation, nullptr);

    // Temporary Filename retail's compiler emitted as this scope's guard.
    System::Filename tempFilename;
    (void)tempFilename;

    toolbarRef.name.Serialize(ser);

    // On load: resolve the ExternalRef at +0x58 (FUN_14001b1d0 inlined): when
    // the factory generation moved, Find the name and keep it if it IsDerivedFrom
    // the building description type.
    if (static_cast<int>(ser.GetDirection()) == 0) {
        ::Object::ExternalFactory* efactory = ::Object::ExternalFactory::_singleton;
        if (toolbarRef.generation != efactory->_generation) {
            System::Filename filenameBuf(toolbarRef.name);
            ::Object::External* found = efactory->FindAs<::Object::External>(filenameBuf);
            bool valid = found && Object::IsDerivedFromNullSafe(found, ComponentDescription::TypeId());
            toolbarRef.resolved = valid ? found : nullptr;
            toolbarRef.generation = efactory->_generation;
        }
    }

    ser.Serialize(toolbarIndex, nullptr);
    return true;
}

// ============================================================
// TutorialSetupDialog::Begin (vtable +0x40, 0x1400e25f0)
// ============================================================
// The step opens: show the listed elements, open the highlight (without the
// already-open check), then for every named dialog that is open: follow its
// building (a building description data source) or close it (clearing edit
// mode first when the data source is a ComponentEntity).
void TutorialSetupDialog::Begin() {
    ShowListedElements();        // 0x1400e29f0
    OpenHighlightDialog('\0');   // 0x1400e2700

    int count2 = dialogNames.GetSize();
    for (int i = 0; i < count2; ++i) {
        Object::InternalPtr<UI::DialogInstance> instance;
        FindDialogByName_Impl(&instance, &dialogNames[i]);
        if (instance.Get() == nullptr) continue;

        Object::Base* basePtr = instance.Get()->_dataSource;   // DialogInstance::_dataSource

        // Original uses IsA first (null/type-safe), then IsDerivedFrom
        bool isBuild = basePtr->IsA(ComponentDescription::TypeId());
        if (isBuild) {
            bool derived = Object::IsDerivedFromNullSafe(basePtr, ComponentDescription::TypeId());
            ComponentDescription* entityForFollow = derived ? static_cast<ComponentDescription*>(basePtr) : nullptr;
            PlayerToolManager* playerController = GameUpdateLoop::_singleton->m_toolManager;
            playerController->ToggleUtilityDialog(entityForFollow);   // 0x140101a80
        } else {
            // Original: SelectTool(nullptr) only if IsA(ComponentEntity), but CloseDialog always
            bool isEntity = basePtr->IsA(ComponentEntity::TypeId());
            if (isEntity) {
                PlayerToolManager* playerController = GameUpdateLoop::_singleton->m_toolManager;
                playerController->SelectTool(nullptr);   // 0x140102b60 (the door was named SetEditMode)
            }
            UI::UserInterface* ui = GameUpdateLoop::_singleton->m_userInterface;
            Object::InternalPtr<UI::DialogInstance> instCopy = instance;
            ui->CloseDialog(&instCopy);
        }
    }
}

// ============================================================
// TutorialSetupDialog::Update (vtable +0x48, 0x1400e24c0)
// ============================================================
// Every tick: re-show the listed elements, re-open the highlight unless one
// is already open. `time` is unused.
void TutorialSetupDialog::Update(const System::Time* /*time*/) {
    ShowListedElements();          // 0x1400e29f0
    OpenHighlightDialog('\x01');   // 0x1400e2700
}

// ============================================================
// TutorialSetupDialog::End (vtable +0x50, 0x1400e2460)
// ============================================================
// The step completes: un-hide every dialog, close the open highlight.
void TutorialSetupDialog::End() {
    SetAllDialogFlags('\x01');   // 0x1400e2330

    Object::InternalPtr<UI::DialogInstance> instance = FindHighlightDialog();   // 0x1400e24f0
    if (instance.Get() != nullptr) {
        UI::UserInterface* ui = GameUpdateLoop::_singleton->m_userInterface;
        Object::InternalPtr<UI::DialogInstance> instPtr = instance;
        ui->CloseDialog(&instPtr);
    }
}

// create 0x1400285b0: MemAlloc(0x78, 8), then the ctor 0x1400e2cc0.
::Object::Base* TutorialSetupDialog::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(TutorialSetupDialog), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(::new (p) TutorialSetupDialog());
}
// createInPlace 0x1400285f0.
::Object::Base* TutorialSetupDialog::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(::new (storage) TutorialSetupDialog());
}

// DEFINE_EXE_TYPE(TutorialSetupDialog, "TutorialSetup") stays where its
// REGISTER_TYPE was — tutorial_dialog_vtables.cpp (B38_RULES B8-7).
