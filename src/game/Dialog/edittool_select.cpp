// src/game/Dialog/edittool_select.cpp
// EditTool::SelectEntity (Application-x64.exe 0x1400fd3b0): "select entity -> open
// its info dialog". Declared in Player/EditTool.h; the body lives here with the
// dialogs it opens.
//
// Original behavior (verified against the 0x1400fd3b0 DISASSEMBLY, not just the
// decompiler):
//   1. CheckPtr(entity); if lastEntityRef (+0x30) == entity -> return.
//   2. 0x1400fcc90(this) — close the previously-open panel (moved, see step 2 below).
//   3. ui = GameUpdateLoop::m_userInterface (+0x98).
//   4. Walk the open-dialog list (GetFirstDialog; node[0] = dialog, node+0x10 = next).
//      For each dialog: GetController(EntityUI); if the controller derives from
//      EntityUI and its entity (+0x30) == entity -> reuse this dialog.
//   5. Else find the UIDescription among the entity's ComponentDescription
//      sub-descriptions, and the entity's UIComponent (0x14002db60 =
//      FindComponent<UIComponent>, whose byte array is the controller data),
//      MemAlloc(0xB8), construct the DialogInstance from the UIDescription's
//      dialog resource (+0x28) with the entity as data source, then AddDialog.
//   6. Finalize: entityRef (+0x28) = dialog, lastEntityRef (+0x30) = entity,
//      0x1400fc9e0(this).
//
// ABI notes (from disassembly):
//   [0x140151ca0] GetFirstDialog: `*retbuf = this->_dialogs._head`, so the
//                                 UserInterface::_dialogs list is walked directly.
//   [0x140151768] GetController : RCX=dialog RDX=retbuf R8=typeId (Ghidra drops R8).
//   [0x140151518] DialogInstance ctor: RCX=self RDX=ui R8=dialog R9=&parent
//                                 [RSP+0x20]=entity [RSP+0x28]=ctrlData [RSP+0x30]=ctrlCount
//   [0x140151730] AddDialog: RCX=ui RDX=&ref
//   vtable stamp LEA 0x14015f5a0: the exe's copy of DialogInstance's table.
//
// Type ids: 0x1401c4f00 EntityUI, 0x1401c7f90 UIDescription, 0x1401c7fc8 UIComponent.

#include "object/TypeInfo.h"
#include "Game/GameUpdateLoop.h"
#include "Player/EditTool.h"
#include "Player/exe_bodies.h"
#include "Entity/ComponentEntity.h"
#include "Component/ComponentDescription.h"
#include "Component/UIComponent.h"
#include "Component/UIDescription.h"
#include "Dialog/EntityUI.h"
#include "Dialog/ToolUI.h"
#include "ui/UserInterface.h"
#include <cstdint>
#include "object/Internal.h"
#include "ui/DialogInstance.h"
#include "ui/UserInterface.h"
#include <new>
#include "Dialog/exe_bodies.h"
#include "system/MemoryAllocator.h"

// 0x1400fcc90 (deselect: clear the previous entity's outline + close the open
// panel) and 0x1400fc9e0 (mark the entity's UIComponents dirty and repaint the
// selection overlay) are ported in src/game/Player/tool_base_render.cpp as
// EditTool::HighlightClear (0x1400fcb40, cc90's outline half) and EditTool::HighlightSelect.

// [diag] ring of the last 4 entities SelectEntity committed — read back by
// tool_base_render.cpp's ReportBadSelectionRef when cb40 finds a non-canonical
// ref, to tell "a bad value was stored here" from "self isn't an EditTool".
extern "C" void* g_LastSelectStore[4] = {};
extern "C" int   g_LastSelectStoreIdx = 0;

// (B.4 P2 DLG-1: the forwarder EditTool_SelectEntity_Impl had no caller left; deleted.)

// ============================================================================
// 0x1400fd3b0 EditTool::SelectEntity(InternalPtr<ComponentEntity>* entity)
// ============================================================================
void EditTool::SelectEntity(const Object::InternalPtr<ComponentEntity>* selected) {
    ComponentEntity* entity = selected->Get();

    // 1. CheckPtr(entity); early-out if this entity is already the selected one.
    if (lastEntityRef == entity) return;

    // 2. (Retail calls 0x1400fcc90 here to tear down the previous panel + clear its
    //    outline. We DON'T, because our EditTool::OnKey pick handler re-runs this
    //    select PER FRAME while the pick button reads "pressed", firing for whatever
    //    entity is under the cursor. Clearing the old outline here would then wipe
    //    the current selection every frame the cursor drifts off it — e.g. a walking
    //    citizen leaves its outline flickering off. Instead we clear the previous
    //    outline at step 6, which is only reached when a NEW selection actually
    //    completes (a dialog is opening). A hover over a no-dialog entity returns
    //    early below and leaves the current outline intact.)

    // 3. The game's UserInterface (+0x98).
    UI::UserInterface* ui = GameUpdateLoop::_singleton->m_userInterface;

    UI::DialogInstance* resultDialog = nullptr;

    // 4. Walk the open-dialog list for an existing EntityUI dialog bound to this entity.
    //    `_dialogs.GetFirst()` IS retail's GetFirstDialog (0x180031150); the iterator
    //    is the bare node pointer.
    const uint32_t tidEntityUI = EntityUI::TypeId();   // 0x1401c4f00
    for (auto it = ui->_dialogs.GetFirst(); it; ++it) {
        UI::DialogInstance* dialog = it.node->element;

        UI::DialogController* controller =
            dialog ? dialog->GetController(tidEntityUI).Get() : nullptr;
        EntityUI* entityUI = ::Object::IsDerivedFromNullSafe(controller, tidEntityUI)
                                 ? static_cast<EntityUI*>(controller) : nullptr;
        if (entityUI && entityUI->entity == entity) {
            resultDialog = dialog;
            break;
        }
    }

    if (resultDialog == nullptr) {
        // 5. No existing dialog — find the UIDescription among the entity's
        //    sub-descriptions, then build a fresh DialogInstance.
        ComponentDescription* description = entity->_description;
        int count = description->_subDescCount;
        const uint32_t tidUIDesc = UIDescription::TypeId();   // 0x1401c7f90

        UIDescription* uiDesc = nullptr;
        for (int i = 0; i < count; i++) {
            Object::Base* item = description->_subDescs[i];
            if (item->Type() != tidUIDesc) continue;
            // Matched: require derived, else abort the whole select.
            if (!::Object::IsDerivedFromNullSafe(item, tidUIDesc)) return;
            uiDesc = static_cast<UIDescription*>(item);
            break;
        }
        if (uiDesc == nullptr) return;  // no UIDescription -> nothing to open

        // The entity's UIComponent supplies the controller data array.
        UIComponent* uiComp = entity->FindComponent<UIComponent>();   // 0x14002db60

        void* storage = System::MemoryAllocator::MemAlloc(0xB8, 8);
        UI::DialogInstance* newDialog = nullptr;
        if (storage != nullptr) {
            void* ctrlData = nullptr;
            int32_t ctrlCount = 0;
            if (uiComp != nullptr) {
                // UIComponent +0x28: the byte array {data, count, capacity}.
                auto& bytes = uiComp->data;
                ctrlCount = bytes.GetSize();
                ctrlData = bytes.GetData();
            }
            newDialog = ::new (storage) UI::DialogInstance(
                ui,
                uiDesc->_dialog,
                Object::InternalPtr<UI::DialogInstance>(),   // parent: null
                entity,
                ctrlData, ctrlCount);
        }

        // Null (alloc failed) -> abort before finalize.
        resultDialog = newDialog;
        if (newDialog == nullptr) return;

        Object::InternalPtr<UI::DialogInstance> addRef(newDialog);
        ui->AddDialog(&addRef);
    }

    // 6. Finalize. We're now committed to selecting `entity` (a dialog is opening),
    //    so THIS is the point to clear the previous entity's outline — cb40 clears
    //    the highlight on lastEntityRef, which is still the OLD entity until the
    //    store below. (Moved off the per-frame step-2 cc90 path so hover-picks over
    //    no-dialog entities can't wipe the current selection; see step 2.)
    HighlightClear();   // 0x1400fcb40

    // 6a. Close the PREVIOUSLY-open info dialog before replacing it with the new
    //     one. This is retail cc90's third action (0x1400fcc90): GetController(old,
    //     ToolUI); if the controller isn't pinned (+0x40 == 0) -> CloseDialog.
    //     Without this the old panel lingers and every click stacks another dialog.
    //     We do ONLY the close here (not cc90's ref-nulling): entityRef/lastEntityRef
    //     are overwritten immediately below, so nulling them would defeat the
    //     EditTool::OnKey per-frame `lastEntityRef != foundEntity` guard and re-fire
    //     the select every frame. A user-pinned panel is left open — matching retail.
    UI::DialogInstance* oldDialog = entityRef;
    if (oldDialog && oldDialog != resultDialog) {
        const uint32_t tidToolUI = ToolUI::TypeId();   // 0x1401c5788
        UI::DialogController* controller = oldDialog->GetController(tidToolUI).Get();
        ToolUI* toolUI = ::Object::IsDerivedFromNullSafe(controller, tidToolUI)
                             ? static_cast<ToolUI*>(controller) : nullptr;
        // Not pinned (+0x40 == 0) -> close it. (A null controller has no pin byte
        // to read, so a resolved controller is required first.)
        if (toolUI && toolUI->pinned == 0) {
            Object::InternalPtr<UI::DialogInstance> closeRef(oldDialog);
            ui->CloseDialog(&closeRef);
        }
    }

    entityRef = resultDialog;
    g_LastSelectStore[g_LastSelectStoreIdx & 3] = entity;
    g_LastSelectStoreIdx = (g_LastSelectStoreIdx + 1) & 3;
    lastEntityRef = entity;
    HighlightSelect();   // 0x1400fc9e0
}
