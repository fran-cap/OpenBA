// src/game/Tutorial/find_ui_element_by_path.cpp
//
// 0x1400E1870 — FindUIElementByPath(outElementIBP, String const& path, outDialog).
//
// This is the resolver behind every tutorial highlight and every "grey out the
// UI except this one control" step. Four call sites, all previously guarded by
// `if (!fn_FindUIElementByPath) DarkOnce(...)`:
//   TutorialSetupDialog::ShowListedElements  (tutorial_wave.cpp)   — the unhide list
//   TutorialSetupDialog::OpenHighlightDialog (tutorial_wave.cpp)   — branch B target
//   TutorialGoalButton::IsComplete           (tutorialgoalbutton.cpp)
//   TutorialGoalCount::IsComplete case 9     (tutorialgoalcount.cpp)
//
// The path is colon-separated and its LENGTH selects the lookup:
//   "Dialog:element"                     find dialog by type, element by name
//   "Dialog:Controller:element"          also narrow by controller type; look in
//                                        the controller's container first, then
//                                        fall back to the dialog
//   "Dialog:Controller:element:resource" additionally require the dialog's
//                                        ComponentEntity (+0x30) to have been
//                                        built from that resource file — this is
//                                        how a step points at ONE building's
//                                        panel rather than any panel of its kind
//
// Ported from the disassembly at 0x1400E1870-0x1400E1CB6; the Ghidra pseudo-C
// drops the String argument of both FindElement overloads and the type-filter
// argument of GetController, so the register assignments were read from the
// instruction stream (see [[reference-getcontroller-third-arg-is-a-type-filter]]).
//
// Splitting: retail calls FUN_1400E0C90, which builds a DynArray<String> by
// walking the path and cutting at every ':' (and at the end). We build the same
// parts with System::String::SubString so every part is a real System::String —
// the two FindElement overloads and String::operator!= all take String const&.

#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "system/String.h"
#include "system/Filename.h"
#include "object/External.h"   // ENDGAME B.3.3 step 6A
#include "ui/UserInterface.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "object/TypeInfo.h"   // ENDGAME B.3.3 step 6C
#include "ui/DialogInstance.h"   // ENDGAME B.3.6 UI callers (K4)
#include "ui/Element.h"                       // ENDGAME B.3.6 UI callers (K4)
#include "Entity/ComponentEntity.h"
#include "Component/ComponentDescription.h"
#include "ui/DialogController.h"
#include <new>
#include "Tutorial/exe_bodies.h"

namespace {

constexpr int kMaxParts = 8;   // retail's DynArray grows; no shipped path is longer


// 0x1400E0C90 — cut `path` at every ':' and once more at the end.
// Returns the number of parts written into `parts` (each a constructed String).
int SplitPath(const System::String& path, System::String* parts) {
    const char* data = path._data;
    int len = data ? path.GetLength() : 0;
    if (!data || len <= 0) return 0;

    int count = 0;
    int start = 0;
    for (int i = 0; i <= len && count < kMaxParts; ++i) {
        bool cut = (i < len && data[i] == ':') || (i == len);
        if (!cut) continue;
        // SubString's end is INCLUSIVE (retail: R9D = EBX-2 at a colon, EBX-1 at
        // the final character), so the cut point i is one past the last character
        // we want.
        parts[count] = path.SubString(start, i - 1);
        ++count;
        start = i + 1;
    }
    return count;
}

/// 0x1400105B0 — narrow a Base* to ComponentEntity, or null.
ComponentEntity* NarrowComponentEntity(Object::Base* obj) {
    if (!obj) return nullptr;
    return Object::IsDerivedFromNullSafe(obj, ComponentEntity::TypeId()) ? static_cast<ComponentEntity*>(obj) : nullptr;
}

// The 4-part tail: does this dialog's entity come from the named resource file?
// Retail compares the entity description's path filename (extension stripped)
// against parts[3] and SKIPS the dialog when they differ.
bool ResourceNameMismatch(const UI::DialogInstance* dialog, const System::String& wantName) {
    Object::Base* owner = dialog->_dataSource;
    if (!owner) return false;
    ComponentEntity* entity = NarrowComponentEntity(owner);
    if (entity == nullptr) return false;

    const ComponentDescription* description = entity->_description;
    if (!description) return false;

    System::Filename fullPath = description->GetPath();
    System::Filename leafName = fullPath.GetPathFilename(true);
    System::String   nameStr(leafName.AsString());

    return !(nameStr == wantName);
}

}  // namespace

Object::InternalPtr<UI::Element>* FindUIElementByPath_Impl(Object::InternalPtr<UI::Element>* out,
                                                          const System::String* pathStr,
                                                          UI::DialogInstance** outDialog) {
    if (!out) return out;
    *out = nullptr;

    if (!pathStr) return out;
    const System::String& path = *pathStr;
    if (path._data == nullptr || path.GetLength() == 0) {
        return out;
    }

    System::String parts[kMaxParts];
    int partCount = SplitPath(path, parts);
    // Fewer than two parts is not a path — retail returns a null InternalBasePtr.
    if (partCount < 2) {
        return out;
    }

    if (GameUpdateLoop::_singleton == nullptr) {
        return out;
    }
    const ::Object::Factory* factory = ::Object::Factory::_singleton;
    if (!factory) return out;

    uint32_t dialogTypeId = factory->GetTypeIdFromName(parts[0]._data, true);
    if (dialogTypeId == 0) {
        return out;
    }

    uint32_t controllerTypeId = 0;
    if (partCount >= 3)
        controllerTypeId = factory->GetTypeIdFromName(parts[1]._data, true);

    UI::UserInterface* ui = GameUpdateLoop::_singleton->m_userInterface;
    if (!ui) {
        return out;
    }

    // The open-dialog list. `_dialogs.GetFirst()` IS retail's GetFirstDialog
    // (0x180031150 = `*retval = this->_dialogs._head`), and the iterator is the
    // bare node pointer, so `it.node` is the same address retail hands to
    // CheckPtr and `it.node->element` the InternalBasePtr(DialogInstance*).
    UI::Element* found = nullptr;
    for (auto it = ui->_dialogs.GetFirst(); it && found == nullptr; ++it) {
        UI::DialogInstance* dialog = it.node->element;
        if (!dialog || !dialog->IsA(dialogTypeId)) continue;

        if (controllerTypeId == 0 || partCount < 3) {
            // "Dialog:element" — the element is looked up on the dialog itself.
            if (outDialog) *outDialog = dialog;
            UI::Element* elem = dialog->FindElement(parts[1]).Get();
            if (elem) found = elem;
            continue;
        }

        // "Dialog:Controller:element[:resource]"
        UI::DialogController* controller = dialog->GetController(controllerTypeId).Get();
        if (controller == nullptr) continue;

        if (partCount == 4) {
            if (ResourceNameMismatch(dialog, parts[3])) continue;
        }

        if (outDialog) *outDialog = dialog;

        // The controller's container first...
        UI::Element* elem = nullptr;
        {
            UI::Element* container = controller->GetContainer().Get();
            if (container) {
                elem = container->FindElement(parts[2]).Get();
            }
        }
        // ...then the dialog itself.
        if (elem == nullptr) {
            elem = dialog->FindElement(parts[2]).Get();
        }
        if (elem) found = elem;
    }

    *out = found;
    return out;
}
