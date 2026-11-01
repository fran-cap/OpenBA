// src/game/Component/citizen_onremove.cpp
// CitizenComponent::OnRemove (0x140113ca0) — the removed-from-world teardown that
// clears EVERY reference the rest of the town holds to a dying citizen.
//
// Why this matters: CitizenComponent_OnAdd (0x140114000) appends the citizen to the
// town manager's citizen list (townMgr+0xa0) and nothing ever removed it again, so
// every death left a permanent dangling pointer in the town's own list. Retail does
// not survive that by luck — Object::InternalBasePtr::CheckPtr is an EMPTY function
// in the shipping Runtime (see Object/internal_factory_audit.h), so retail's whole
// correctness story for removal IS this function clearing the refs explicitly.
// ObjAudit reporting bad=0 does not contradict this: the InternalFactory handle
// table is intact; it is the town's lists that hold the stale pointers.
//
// Ghidra-verified 1:1 against the decompile of 0x140113ca0. Eight steps, in order:
//   1. prologue teardown — hide the carried tool (0x1401176e0), release the partner
//      slot +0x208 (0x140115a50, no re-roll) and the two queued-worker link slots
//      +0x228 / +0x230 (0x1401159b0 / 0x140111650)
//   2. AssignWorkplace(self, null, -1)         — workplace link +0x238 / slot +0x240
//   3. LeaveRoster_2040(self->+0x258, &self)   — unlink from the host's roster ring
//   4. family ring +0x250 (spouse) / +0x260 (ring head):
//        no spouse  -> per member: under-age (age +0x3c <= desc(+0x288)+0x44) dies via
//                      HandleLifeEvent(member, 4); otherwise it is emancipated —
//                      sub_1fa0(member, null), AssignWorkplace(null,-1), release its
//                      partner slot
//        spouse     -> re-home the whole ring onto the spouse (sub_1fa0(member,&spouse))
//      then clear the spouse's back-ref and our own +0x250
//   5. AssignHousing(self, null)               — housing link +0x248
//   6. profession list — when +0x1f0 is not the default profession (*(gw+0xb0)+0x30),
//      ask the town for a replacement worker (0x14008c3d0) and post life event 0xf
//      (replacement found, named after the deceased) or 0x10 (nobody available)
//   7. TradeWork_impl_3480(gw+0x120, &ownerEntity)  — trade-work list + open dialogs
//   8. FUN_140002cc0(townMgr+0xa0, &self)      — the citizen-list removal itself
//
// Every callee is already ported AND registered (checked explicitly — "ported but
// unregistered" is how a pause button ended up calling address 0):
//   0x1401176e0 ActCt_HideTool_1176e0                 reg_action_complete.cpp
//   0x140115a50 AttendanceComponent_ReleasePartnerSlot reg_component_gap_A.cpp
//   0x1401159b0 AttendanceComponent_ReleaseLink59b0    reg_component_gap_A.cpp
//   0x140111650 AttendanceComponent_ReleaseLink1650    reg_component_gap_A.cpp
//   0x1401163d0 CitizenComponent_AssignWorkplace       reg_citizen_assignworkplace.cpp
//   0x140112040 CitizenComp_LeaveRoster_2040           reg_citizen_25.cpp
//   0x140111fa0 CitizenComp_sub_1fa0                   reg_component_people.cpp
//   0x140111ee0 CitizenComp_AssignHousing              reg_component_gap_A.cpp
//   0x140116dd0 CitizenComponent_HandleLifeEvent       reg_citizen_lifeevent_handler.cpp
//   0x14008c3d0 CitizenList_AssignWorker_c3d0          reg_townhallui.cpp
//   0x140113af0 CitizenComponent_PostLifeEvent         reg_eventlog_citizen.cpp
//   0x140103480 NomadComp_FUN_140103480                reg_component_people.cpp
//   0x140002cc0 Array<T>::Remove               reg_component_visuals.cpp
//
// Dispatch: 0x140113ca0 has no CALL xrefs in the exe — all four are DATA, the
// CitizenComponent vtable slot 7 (0x140162580+0x38) and three sibling vtables. Slot 7
// is the removed-from-world broadcast that MarkForRemoval (0x1400c2ea0) fires over
// every component whose capability flags (+0x20) have bit 1 set; CitizenComponent's
// flags are 0xCB, so the broadcast already reaches us — it has just been landing on
// the inert no-op stub. citizencomponent.cpp installs this into
// CitizenComponent_LiveVtable[7] under BANISHED_LIVE_CITIZEN_ONREMOVE.
//
// No standalone deviation is left (B8 RES, row 310): the `character` / description /
// world / profession-list / AssignWorker-result / town-list tests and the two ring-walk
// progress breaks went -- retail 0x140113ca0 tests none of them, and the component is
// built by its real ctor (toolSceneId -1, so HideTool returns before reading +0x278).

// EXT SEAM (NOT RETAIL, docs/extensions/handins/FAMILYTREE.md): ext.family_tree (class C)
#include "ext/family_tree/family_tree.h"
#include <new>
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Game/PlayerToolManager.h"   // PlayerToolManager::OnEntityRemoved (0x140103480)
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "Component/CitizenComponent.h"
#include "Component/CitizenDescription.h"    // B.4 W1 COMP-b: description fields by name
#include "Game/ProfessionList.h"
#include "Entity/CitizenBehavior.h"   // m_citizenList->citizens (+0xA0)
#include "Component/exe_bodies.h"
#include "Action/exe_bodies.h"

// ============================================================
// CitizenComponent::OnRemove (0x140113ca0) — vtable slot 7 (+0x38)
// ============================================================

// [7] 0x140113ca0 — the removed-from-world broadcast (MarkForRemoval 0x1400c2ea0,
// flags bit 1): leave the town citizen list.
void CitizenComponent::OnRemove() {
    // EXT SEAM (NOT RETAIL, docs/extensions/handins/FAMILYTREE.md): ext.family_tree (class C), a death
    Ext::FamilyTree::OnRemoved(this);
    // ---- 1. prologue teardown -------------------------------------------------
    HideTool();                                      // 0x1401176e0 (0x140113cb3, no test)
    LeaveSchool('\0');         // 0x140115a50, +0x208
    ReleaseCareBuilding();                  // 0x1401159b0, +0x228
    ReleaseLink230();                  // 0x140111650, +0x230

    // ---- 2. home link (+0x238 / +0x240) ---------------------------------------
    SetHome(nullptr, -1);                                       // 0x1401163d0

    // ---- 3. leave the parent's children ring (+0x258 back-ref) ----------------
    if (_parent != nullptr) {
        _parent->RemoveChild(this);                             // 0x140112040
    }

    // ---- 4. family: spouse (+0x250) / children ring head (+0x260) -------------
    Object::InternalPtr<CitizenComponent>* spouseSlot = &spouse;
    if (spouseSlot->Get() == nullptr) {
        // No surviving parent: each child either dies with us (still a child)
        // or is emancipated into an unattached adult.
        // 0x140113d7f-0x140113ea5: `while (+0x260 != 0)`, no progress test; the
        // under-age compare reads OUR description (+0x288) unguarded (0x140113d9c).
        // Termination (row 1207): HandleLifeEvent(4) runs MarkForRemoval (0x1400c2ea0)
        // when the member's entity lacks 0x400, and a citizen entity carries flag 0x2
        // (the ctor's 0xcb), so MarkForRemoval runs this OnRemove on the member BEFORE
        // setting 0x400 -- step 3 unlinks it from our ring. A member whose entity already
        // has 0x400 went through that same broadcast, so it is no longer in the ring.
        Object::InternalPtr<CitizenComponent>* ringHead = &_firstChild;
        CitizenComponent* member = ringHead->Get();
        while (member != nullptr) {
            float memberAge = member->age;
            bool underAge = memberAge <= description->adultAge;

            if (underAge) {
                ringHead->Get()->HandleLifeEvent(4, nullptr);   // 0x140116dd0
            } else {
                CitizenComponent* memberIBP = member;
                memberIBP->SetParent(nullptr);              // 0x140111fa0: detach from our ring

                memberIBP->SetHome(nullptr, -1);

                // Retail inlines FUN_140115a50's body (reroll = 0) here verbatim:
                // detach the member from its school, clear the school's status-icon
                // flag 2, null the +0x208 slot.
                memberIBP->LeaveSchool('\0');
            }

            member = ringHead->Get();
        }
    } else {
        // A spouse survives: re-home the whole ring onto them.
        // 0x140113d2f-0x140113d6c: `while (+0x260 != 0)`, no progress test.
        Object::InternalPtr<CitizenComponent>* ringHead = &_firstChild;
        while (ringHead->Get() != nullptr) {
            ringHead->Get()->SetParent(spouseSlot->Get());            // 0x140111fa0
        }
    }

    // Clear the spouse's back-ref to us and our own spouse link (no-op with no spouse).
    if (spouseSlot->Get() != nullptr) {
        spouseSlot->Get()->spouse = nullptr;
        *spouseSlot = nullptr;
    }

    // ---- 5. workplace (+0x248) ------------------------------------------------
    // Read BEFORE SetWorkplace clears it: the vacated workplace is what step 6 passes
    // to the replacement-worker search as the "near here" hint.
    ComponentEntity* formerHouse = workplace.Get();
    if (formerHouse != nullptr) {
        SetWorkplace(nullptr);                              // 0x140111ee0
    }

    // ---- 6. profession list ---------------------------------------------------
    // 0x140113f1a-0x140113f5c: the world, its profession list and AssignWorker's result
    // are read with no test (B8 RES, row 310).
    GameUpdateLoop* gw = GameUpdateLoop::_singleton;
    {
        ProfessionList* professionRoot = gw->m_professionList;
        Profession* defaultProfession = professionRoot->field_0x30;
        if (profession != defaultProfession) {
            CitizenComponent* retbuf[2] = { nullptr, nullptr };
            ComponentEntity* nearIBP = formerHouse;
            CitizenComponent** result = gw->m_citizenList->AssignWorker(retbuf, &nearIBP, profession);   // 0x14008c3d0
            CitizenComponent* replacement = *result;

            CitizenComponent* target;
            int   eventType;
            const char* name;
            if (replacement == nullptr) {
                target = this; eventType = 0x10; name = nullptr;   // nobody to take the job
            } else {
                // the deceased's name (+0x28, H5 raw System::String: its data pointer)
                name = this->name._data;
                target = replacement;
                eventType = 0xf;                                    // replacement found
            }
            target->PostLifeEvent(eventType, 1, name);             // 0x140113af0
        }

        // ---- 7. trade-work list + any dialog bound to our entity ---------------
        ComponentEntity* ownerIBP = _entity.Get();
        gw->m_toolManager->OnEntityRemoved(&ownerIBP);   // 0x140103480

        // ---- 8. the town citizen list (townMgr+0xa0) — the whole point ---------
        CitizenComponent* selfIBP = this;
        CitizenBehavior* townMgr = GameUpdateLoop::_singleton->m_citizenList;   // re-read, 0x140113fc5
        townMgr->citizens.Remove(selfIBP);   // 0x140002cc0, +0xA0
    }
}
