// src/diag/panel_hold_harness.cpp
//
// Open one building's info panel and LEAVE IT OPEN, so the layout can be looked at.
//
// Every existing way a panel opens in an unattended run closes it again immediately:
// panel_staffing_driver.cpp acquires the edit tool, selects the entity, clicks
// workerCountUp and calls EditTool_CloseInfoDialog on the way out. That is correct for
// seating, but it means a rendering or layout defect on a panel -- the tavern's squashed
// barrel icon, say -- can only be seen by a human clicking the building, which an
// autostart run has nobody to do.
//
// (The tavern barrel that motivated this turned out to be RETAIL behaviour -- the
// Inventory tab covers it in the real game too, confirmed against the shipped exe -- so
// that particular icon is NOT a defect and must not be "fixed". The harness earned its
// keep by settling the question, and the next layout report will need it just the same.)
//
// This does the same select, skips the close, and then stops. One panel, held open for
// the rest of the run, ready for a window capture.
//
// BANISHED_PANEL_HOLD=<ComponentTypeName>   e.g. ConsumeProduceComponent
// BANISHED_PANEL_HOLD_FRAME=<n>             when to open it (default 3000; leave time
//                                           for staffing and, for a tavern, for the
//                                           inventory to have something in it)
// BANISHED_PANEL_HOLD_CLICK=<Controller>:<elementId>
//                                           press one of the held panel's buttons by
//                                           delivering the click event (msg 0x12, id)
//                                           to that DialogController's handler (vtable
//                                           slot 9). Reaches only controllers the
//                                           DialogInstance itself owns — a controller on
//                                           an inserted page (the town hall's nomad tab,
//                                           ImmigrationUI) is NOT found; that one has its
//                                           own knob, BANISHED_IMMUI_AUTOCLICK (see
//                                           ui_dialog_stub_factories.cpp). Repeats every
// BANISHED_PANEL_HOLD_CLICK_EVERY=<frames>  (default 300) from the open frame on; a
//                                           press the controller has nothing to do with
//                                           is the same no-op as in retail.
//
// Diagnostic harness: it drives the same retail path a player click drives and writes
// nothing of its own.

#include "object/Internal.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Entity/ComponentEntity.h"   // B4: the component array by name
#include "Entity/EntityList.h"        // B4: GameUpdateLoop::m_entityList
#include "Object/factory_registration.h"
#include "Player/edittool_funcs.h"
#include "options.h"
#include "clamped_atoi.h"     // review F-087: checked parse of BANISHED_* text
#include "object/TypeInfo.h"   // Object::IsDerivedFromNullSafe
#include "ui/UserInterface.h"
#include "ui/DialogInstance.h"   // ENDGAME B.3.6 UI callers (K4)
#include <new>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
UI::DialogInstance* s_heldDialog = nullptr;
int   s_heldAtFrame = -1;

// BANISHED_PANEL_HOLD_CLICK — see the file header.
void PanelHoldClick(int frameNum) {
    static int   s_every = -2;
    static char  s_ctrl[64] = {0};
    static int   s_id = 0;
    if (s_every == -2) {
        const char* c = Opt().panelHoldClick;
        s_every = -1;
        if (c && c[0]) {
            const char* colon = std::strchr(c, ':');
            if (colon && colon - c < 63) {
                std::memcpy(s_ctrl, c, static_cast<size_t>(colon - c));
                s_ctrl[colon - c] = 0;
                s_id = HarnessAtoi(colon + 1);
                const char* ev = Opt().panelHoldClickEvery;
                s_every = (ev && ev[0]) ? HarnessAtoi(ev) : 300;
                if (s_every < 1) s_every = 300;
            }
        }
    }
    if (s_every < 0 || !s_heldDialog) return;
    if (frameNum < s_heldAtFrame + s_every || ((frameNum - s_heldAtFrame) % s_every) != 0) return;
    const uint32_t tid = ::Object::GetTypeIdByName(s_ctrl);
    // Find the controller the way the tutorial's FindUIElementByPath does: walk the UI
    // singleton's open-dialog list (gw+0x98; node = {IBP DialogInstance, .., next@+0x10})
    // and ask each DialogInstance for a controller of that type (GetController's third
    // argument is the controller TYPE id). The held panel is the only open dialog that
    // carries one.
    GameUpdateLoop* gw = (GameUpdateLoop::_singleton != nullptr) ? GameUpdateLoop::_singleton : nullptr;
    UI::UserInterface* uiSingleton =
        gw ? gw->m_userInterface : nullptr;   // +0x98
    if (!uiSingleton) return;
    UI::DialogController* ctrl = nullptr;
    int dialogs = 0;
    static bool s_listed = false;
    // `_dialogs.GetFirst()` IS retail's GetFirstDialog (0x180031150 = `*retval =
    // this->_dialogs._head`); the iterator is the bare node pointer.
    for (auto it = uiSingleton->_dialogs.GetFirst(); it && !ctrl; ++it) {
        UI::DialogInstance* dlgInst = it.node->element;   // DialogInstancePtr (UserInterface.h)
        if (!dlgInst) continue;
        ++dialogs;
        if (!s_listed) {
            // One-shot: what controllers does each open dialog carry? (DialogInstance
            // +0x78 = controller IBP array, +0x80 = count — UI::DialogInstance::
            // GetController 0x180113320 reads exactly these.)
            const int nc = dlgInst->_controllers._size;                   // +0x80
            UI::DialogController** arr = dlgInst->_controllers._data;      // +0x78
            std::fprintf(stderr, "[PanelHold] dialog #%d inst=%p controllers=%d:", dialogs, (void*)dlgInst, nc);
            for (int i = 0; i < nc && i < 32 && arr; ++i) {
                UI::DialogController* c = arr[i];
                uint32_t t = c ? c->Type() : 0;
                std::fprintf(stderr, " %08x", t);
            }
            std::fprintf(stderr, "\n");
            std::fflush(stderr);
        }
        const Object::InternalPtr<UI::DialogController> ctrlIBP = dlgInst->GetController(tid);
        ctrl = ctrlIBP.Get();
    }
    s_listed = true;
    // NOTE: a controller that lives on an inserted page (the town hall's nomad tab,
    // Dialog/Immigration.rsc -> ImmigrationUI) is not reachable from this walk, and
    // caching its instance across frames is unsafe because the panel re-creates its
    // controllers (a stale pointer crashed here 2026-09-05). Such a button is pressed
    // from inside the controller's own update instead: BANISHED_IMMUI_AUTOCLICK in
    // ui_dialog_stub_factories.cpp.
    if (!ctrl) {
        std::fprintf(stderr, "[PanelHold] frame %d: no %s controller on any of %d open dialogs (tid %08x)\n", frameNum, s_ctrl, dialogs, tid);
        std::fflush(stderr);
        return;
    }
    std::fprintf(stderr, "[PanelHold] frame %d: click %s id=%d (msg 0x12) ctrl=%p\n", frameNum, s_ctrl, s_id, ctrl);
    std::fflush(stderr);
    // slot 9: the controller's OnElementEvent (GetController hands back a
    // UI::DialogController; every exe panel overrides it as a real class, ENDGAME B.3.8).
    ctrl->OnElementEvent(
        nullptr, 0x12, s_id, 0, 0, 0, nullptr);
}
}  // namespace

namespace {

uint32_t Tid(const char* n) { return ::Object::GetTypeIdByName(n); }

Component* FindComp(ComponentEntity* ent, uint32_t tid) {
    if (!ent || !tid) return nullptr;
    const int n = ent->_components._size;                           // +0x38
    Object::InternalPtr<Component>* arr = ent->_components._data;       // +0x30
    if (!arr || n <= 0 || n > 4096) return nullptr;
    for (int i = 0; i < n; ++i) {
        Component* c = arr[i].Get();
        if (!c) continue;
        const uint32_t t = c->Type();
        if (t == tid) return Object::IsDerivedFromNullSafe(c, tid) ? c : nullptr;
    }
    return nullptr;
}

}  // namespace

// The tool manager (gameupdateloop.cpp hands its `m_toolManager`; its local
// declaration still spells the parameter void*).
extern "C" void PanelHoldHarness_Tick(PlayerToolManager* entityUI, int frameNum) {
    static int  s_at   = -2;
    static bool s_done = false;
    static const char* s_want = nullptr;

    if (s_at == -2) {
        s_want = Opt().panelHold;
        if (!s_want || !s_want[0]) { s_at = -1; return; }
        const char* f = Opt().panelHoldFrame;
        s_at = (f && f[0]) ? HarnessAtoi(f) : 3000;
        if (s_at < 1) s_at = 3000;
    }
    if (s_done) PanelHoldClick(frameNum);
    if (s_at < 0 || s_done || !entityUI || frameNum < s_at) return;

    GameUpdateLoop* gw = (GameUpdateLoop::_singleton != nullptr) ? GameUpdateLoop::_singleton : nullptr;
    if (!gw) return;
    EntityList* el = gw->m_entityList;   // +0xe8
    if (!el) return;
    Object::InternalPtr<ComponentEntity>* arr = el->masterArray._data;   // +0x08
    const int n = el->masterArray._size;   // +0x10
    const uint32_t tid = Tid(s_want);
    if (!arr || n <= 0 || !tid) {
        s_done = true;
        std::fprintf(stderr, "[PanelHold] no type id for \"%s\" — nothing opened\n", s_want);
        std::fflush(stderr);
        return;
    }

    for (int i = 0; i < n; ++i) {
        ComponentEntity* ent = arr[i].Get();
        if (!ent || !FindComp(ent, tid)) continue;

        EditTool* tool = EditTool_HarnessAcquire(entityUI);
        if (!tool) return;                    // tool not ready yet; try again next frame
        EditTool_HarnessSelectEntity(tool, ent);
        UI::DialogInstance* dialog = tool->entityRef;   // +0x28

        s_done = true;
        s_heldDialog = dialog;
        s_heldAtFrame = frameNum;
        // Deliberately NOT calling EditTool_CloseInfoDialog — the whole point is that the
        // panel stays up.
        std::fprintf(stderr, "[PanelHold] frame %d: opened %s panel ent=%p dialog=%p "
                             "(HELD OPEN — capture the window now)\n",
                     frameNum, s_want, (void*)ent, dialog);
        std::fflush(stderr);
        return;
    }
}
