#pragma once
// src/game/Tutorial/exe_bodies.h -- declarations of the exe bodies DEFINED in
// src/game/Tutorial/ (ENDGAME B.3R, block W0-P0c, B3R_RULES R5).
//
// Each body here is declared once, copied from its DEFINITION (linkage
// included), grouped by defining file; its retail address is the trailing
// comment. The defining .cpp includes this header so the compiler checks
// each pair. Readers call these by name.
//
// Also here: the declarations and inline bodies of this module that lived in
// engine/funcs/engine_funcs.h, moved verbatim with their comments (that
// header is gone since B3R W2-EZ: its includers include this one directly).

#include <cstdint>
// The inline body's own needs (it reached these through engine/funcs/engine_funcs.h
// until B3R W2-EZ deleted it):
#include <cstring>                               // memcpy
#include <new>                                   // placement new
#include "ui/UserInterface.h"                    // UI::UserInterface::_dialogs
#include "ui/DialogInstance.h"     // UI::DialogInstance::GetController
#include "object/Internal.h"                     // Object::InternalPtr
#include "object/TypeInfo.h"                     // Object::Factory, Object::Base::IsA
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton

// ---- find_ui_element_by_path.cpp
namespace UI { class Element; class DialogInstance; }
Object::InternalPtr<UI::Element>* FindUIElementByPath_Impl(Object::InternalPtr<UI::Element>* out,  // B8: stays free -- free in retail: RCX is the InternalPtr<UI::Element> return slot, constructed through the InternalBasePtr ctor (CALL [0x140152a90] at 0x1400e191c)
                                                          const System::String* pathStr,
                                                          UI::DialogInstance** outDialog);  // 0x1400e1870

// ---- tutorial_base.cpp
namespace IO { class Serializer; }
namespace UI { class ElementDescription; }
class TutorialStep; class TutorialImage; class TutorialSetup; class TutorialGoal;

// ---- tutorial_wave.cpp
class Tutorial;
namespace IO { class Serializer; }
namespace UI { class Dialog; }
class PlayerToolManager;
class ComponentDescription;

// ---- inline bodies (moved from engine/funcs/engine_funcs.h; no .cpp)

// --- FindDialogByName (FUN_1400e1600) — REIMPLEMENTED inline ---
// Parses dialog name string (splits on ':'), gets type IDs via Factory::GetTypeIdFromName,
// walks UI::UserInterface dialog linked list with IsA checks, optionally matches controller type.
// Non-trivial return: RCX=retval(InternalBasePtr*), RDX=name(String*)
// NOTE: the body of exe 0x1400e1600 (the two door pointers once bound to it are gone).
inline Object::InternalPtr<UI::DialogInstance>* FindDialogByName_Impl(
        Object::InternalPtr<UI::DialogInstance>* retval, const System::String* nameStr) {
    const char* str = nameStr->_data;

    // Empty string or null -> return null
    if (str == nullptr || nameStr->GetLength() == 0) {
        *retval = nullptr;
        return retval;
    }

    // Split string on ':'
    int strLen = nameStr->GetLength();

    // Count parts (max 2 — "DialogType" or "DialogType:ControllerType")
    int colonPos = -1;
    for (int i = 0; i < strLen; i++) {
        if (str[i] == ':') { colonPos = i; break; }
    }

    // Extract part1 (and optionally part2)
    char part1[256] = {};
    char part2[256] = {};
    int partCount = 1;

    if (colonPos >= 0) {
        if (colonPos < 255) {
            for (int i = 0; i < colonPos; i++) part1[i] = str[i];
            part1[colonPos] = 0;
        }
        int part2Len = strLen - colonPos - 1;
        if (part2Len > 0 && part2Len < 255) {
            for (int i = 0; i < part2Len; i++) part2[i] = str[colonPos + 1 + i];
            part2[part2Len] = 0;
        }
        partCount = 2;
    } else {
        int copyLen = (strLen < 255) ? strLen : 255;
        for (int i = 0; i < copyLen; i++) part1[i] = str[i];
        part1[copyLen] = 0;
    }

    // Get type IDs from Factory (retail: **import slot 0x140152760)
    const ::Object::Factory* factoryObj = ::Object::Factory::_singleton;
    if (!factoryObj) {
        *retval = nullptr;
        return retval;
    }

    uint32_t dialogTypeId = factoryObj->GetTypeIdFromName(part1, true);
    if (dialogTypeId == 0) {
        *retval = nullptr;
        return retval;
    }

    uint32_t controllerTypeId = 0;
    if (partCount == 2 && part2[0] != 0) {
        controllerTypeId = factoryObj->GetTypeIdFromName(part2, true);
    }

    // Get UI singleton
    GameUpdateLoop* gameWorld = GameUpdateLoop::_singleton;
    if (!gameWorld) {
        *retval = nullptr;
        return retval;
    }
    UI::UserInterface* ui = gameWorld->m_userInterface;
    if (!ui) {
        *retval = nullptr;
        return retval;
    }

    // Walk the dialog list.
    // `_dialogs.GetFirst()` IS retail's UI::UserInterface::GetFirstDialog
    // (0x180031150, literally `*retval = this->_dialogs._head`), reached here
    // through dispatch slot 0x140151ca0; the Iterator is the bare node pointer,
    // so `it.node` is exactly what retail hands to CheckPtr, and `++it` is
    // retail's `node = *(node + 0x10)`.
    // Original: LEA RDX,[RBP+0x48]; MOV RCX,RSI; CALL [0x140151ca0]
    for (auto it = ui->_dialogs.GetFirst(); it; ++it) {
        // CheckPtr on the iterator node (InternalBasePtr) before dereferencing.
        // Original: MOV RCX,RSI; CALL [0x140152ab0] (fn_InternalBasePtr_CheckPtr)

        UI::DialogInstance* dialog = it.node->element;
        if (dialog) {
            // Check IsA against dialogTypeId
            // ?IsA@Base@Object@@QEBA_NI@Z 0x1800491c0, the member (B3R Leftovers).
            if (dialog->IsA(dialogTypeId)) {
                if (controllerTypeId == 0 || partCount != 2) {
                    // Match found, no controller check needed
                    *retval = dialog;
                    return retval;
                }
                // Need to check controller type too.
                // Original reloads node and calls CheckPtr again before GetController:
                //   MOV RSI,[RBP+0x48]; MOV RCX,RSI; CALL CheckPtr
                //   MOV R8D,EBX(=controllerTypeId); LEA RDX,[retbuf]; MOV RCX,[RSI]; CALL GetController
                // GetController takes controllerTypeId as R8 — it does the type matching
                // internally and returns a matching controller or null.
                if (dialog->GetController(controllerTypeId).Get() != nullptr) {
                    // Controller matched — return this dialog
                    *retval = dialog;
                    return retval;
                }
                // Controller didn't match — continue searching
            }
        }
        // `++it` is retail's next-node step:
        // MOV RAX,[RBP+0x48]; MOV RSI,[RAX+0x10]; MOV [RBP+0x48],RSI
    }

    // Not found
    *retval = nullptr;
    return retval;
}
