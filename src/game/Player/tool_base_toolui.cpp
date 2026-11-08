// src/game/Player/tool_base_toolui.cpp
// Player tool-UI dialog open helper (DragTool / PlacementTool family).
//
// This is an exe sub-helper resolved via ResolveAddr(Module::Exe, 0x1400ff9e0)
// from src/game/Player/dragtool.cpp (g_fn_OpenToolDialog) and placementtool.cpp
// (s_fn_FUN_1400ff9e0). It resolves to 0 (the guarded call is silently skipped)
// unless registered. This file provides a faithful 1:1 reimplementation plus the
// supporting DialogInstance child-dialog vtable that the original installs.
//
// Player registration wave (round 2c — tool-ui). 1:1 port of the decompiled exe
// helper; do not "improve" behavior. Ghidra-verified against the disassembly.

#include "ui/UserInterface.h"
#include "ui/DialogInstance.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "system/Filename.h"
#include "object/External.h"   // ENDGAME B.3.3 step 6A
#include "system/MemoryAllocator.h"
#include "object/TypeInfo.h"
#include <cstdint>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "Component/ToolbarDescription.h"   // X::TypeId() (B.3.8 callers)
#include "Component/ComponentDescription.h"
#include "Game/PlayerToolManager.h"
#include "ui/DialogInstance.h"  // ENDGAME B.3.8 CB4: the real UI::DialogInstance
#include "ui/Dialog.h"          // UI::Dialog::TypeId() (D5, ENDGAME B.3.9 CG3)
#include <new>
#include "Player/exe_bodies.h"
#include "Entity/ComponentEntity.h"   // B4 S4 RT2: InternalPtr<ComponentEntity>::Get() needs the class complete

// ============================================================
// The ToolUI child dialog is a plain UI::DialogInstance (exe table 0x14015f5a0)
// ============================================================
// FUN_1400ff9e0 allocates 0xB8 bytes and runs the DialogInstance ctor with the
// constructor inlined, so the exe carries ITS OWN copy of UI::DialogInstance's
// vtable at 0x14015f5a0 (B38_SLOTS section 4: 10 slots, slot 0 the scalar
// deleting dtor 0x140020cb0 = `CALL [??1DialogInstance]; TEST BL,1; JZ; CALL
// [MemFree]`, slots 1..9 JMP-thunks onto the Runtime DialogInstance virtuals).
// It is NOT an exe class (B38_RULES 1.2): the object is a UI::DialogInstance.
//
// ENDGAME B.3.8 (CB4, D5 slot-copy site): the dialog is now built with the REAL
// constructor, `::new (dialog) UI::DialogInstance(...)`, so its vptr is the
// compiler's UI::DialogInstance table (block RB), whose slot 0 is the same
// dtor + MemFree-on-&1 and slots 1..9 the same members. That retires the old
// `g_ToolUI_ChildDialog_Vtable_Standalone[10]`, its `ToolUI_Dtor` slot-0 wrapper
// and `ToolUI_InitChildDialogVtable`, which copied slots 1..9 out of the minted
// Runtime table at registration time.
//
// ENDGAME B.3.9 close: the engine_funcs global `g_ToolUI_ChildDialog_Vtable` and
// ToolUI_ChildDialogVtable(), which pointed it at the compiler table by a
// hand-spelt `_ZTVN2UI14DialogInstanceE` symbol, are deleted: no stamper reads
// the global any more. 0x14015f5a0 stays UNregistered (reg_player_toolui.cpp:
// registering it makes the menu's BuildId child dispatch a real OnOpened).

// ============================================================
// OpenToolDialog (FUN_1400ff9e0)
// ============================================================
// Opens the placement/tool dialog ("Dialog/CreateBar.rsc"):
//   1. If param_1 (the tool's DialogInstance IBP slot) already holds a live
//      dialog, close it (UserInterface::CloseDialog) and null the IBP.
//   2. FindOrLoad the CreateBar dialog resource (External, type-checked vs the
//      External type id).
//   3. MemAlloc(0xB8, 8) a DialogInstance and run the DialogInstance ctor
//      (this, ui, resource, parentIBP=null, dataSource=descriptionPtr, 0, 0);
//      retail's inlined ctor stamps the exe's copy of its table (0x14015f5a0).
//   4. Wrap the new dialog in an IBP, store it in *param_1, and AddDialog it.
//   5. Init the tool's two float fields (param_1+0x8, param_1+0xc) to -1.0f
//      (0xbf800000) — the cached drag start/end coords.
//   6. Scan the description's component list (base +0x28, count +0x30) for the
//      type-id DWORD_1401c7f58; the original calls IsDerivedFrom on the first
//      match but discards the result (a probe with no stored side-effect — kept
//      for fidelity / its CheckPtr-style behaviour).
//   7. *param_2 = *param_1; return param_2.
//
// Caller signatures:
//   dragtool.cpp     : void* fn(void* dialogIBP, void* retval, uint64_t descriptionPtr)
//   placementtool.cpp: same shape.
//
// Closure (all satisfied):
//   - UserInterface::CloseDialog / AddDialog, InternalBasePtr ctor,
//     Object::IsDerivedFromNullSafe, System::MemoryAllocator::MemAlloc — called by
//     name; UI::DialogInstance's real ctor (block RB)
//   - ::Object::ExternalFactory::FindOrLoad — member call (B.3.3)
//   - g_pGameWorldSingleton, ExternalFactory::_singleton, UI::Dialog::TypeId()
//   - DWORD_1401c7f58  — exe type-id storage global (ResolveAddr, lazily)
//   No unregistered exe sub-calls.
// B.4 PLAYER: param_1 is the tool's owner, the PlayerToolManager (Tool +0x10):
// its +0x00 holds the dialog, +0x08 / +0x0C the two cached floats.
UI::DialogInstance** PlayerToolManager::OpenToolDialog(UI::DialogInstance** ret,
                                                           ComponentDescription* description) {
    UI::UserInterface* ui = GameUpdateLoop::_singleton->m_userInterface;

    // (1) Close any existing dialog and null the IBP.
    if (toolDialog != nullptr) {
        Object::InternalPtr<UI::DialogInstance> local(toolDialog);
        ui->CloseDialog(&local);
        toolDialog = nullptr;
    }

    ui = GameUpdateLoop::_singleton->m_userInterface;

    // (2) Load "Dialog/CreateBar.rsc" and type-check it against the Dialog type.
    const UI::Dialog* resource = nullptr;
    {
        System::Filename fname("Dialog/CreateBar.rsc");
        const Object::External* obj = nullptr;
        if (::Object::ExternalFactory::_singleton) {
            obj = ::Object::ExternalFactory::_singleton->FindOrLoad(fname);
        }
        if (obj && Object::IsDerivedFromNullSafe(obj, UI::Dialog::TypeId())) {
            resource = static_cast<const UI::Dialog*>(obj);
        }
    }

    // (3) Allocate + construct the DialogInstance (the real class: its compiler
    // table is the one retail's copy at 0x14015f5a0 describes).
    void* storage = System::MemoryAllocator::MemAlloc(0xB8, 8);
    UI::DialogInstance* dialog = nullptr;
    if (storage) {
        dialog = ::new (storage) UI::DialogInstance(
            ui, resource,
            Object::InternalPtr<UI::DialogInstance>(),           // parent: null
            description,                                         // the data source
            nullptr, 0);
    }

    // (4) Store it in the manager, and add it to the UI.
    toolDialog = dialog;
    {
        Object::InternalPtr<UI::DialogInstance> addIBP(dialog);
        ui->AddDialog(&addIBP);
    }

    // (5) Reset the two cached float coords (+0x08, +0x0C) to -1.0f. Retail
    // stores the dword 0xbf800000 twice.
    createBarScreenX = -1.0f;
    createBarScreenY = -1.0f;

    // (6) Probe the description's sub-descriptions for the ToolbarDescription
    // (cell 0x1401c7f58); retail calls IsDerivedFrom on the match and discards it.
    {
        const uint32_t tid = ToolbarDescription::TypeId();
        if (description) {
            int n = description->_subDescCount;
            Description** subs = description->_subDescs;
            for (int i = 0; i < n; i++) {
                if (subs[i]->Type() == tid) {
                    (void)Object::IsDerivedFromNullSafe(subs[i], tid);  // probe; result discarded
                    break;
                }
            }
        }
    }

    // (7) Return the manager's dialog through the hidden slot.
    *ret = toolDialog;
    return ret;
}
