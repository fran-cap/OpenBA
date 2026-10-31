// src/game/Dialog/workplaceui.cpp
// WorkPlaceUI — the workplace worker-count panel (Dialog/Work.rsc:workPlace).
//
// RETAIL (Application-x64.exe): UI::DialogController subclass, 0x68 bytes.
//   +0x30 workPlaceCount (LabelElement)  +0x38 maxCount (TextElement)
//   +0x40 workerCountUp  +0x48 workerCountDown  (ButtonElement)
//   +0x50 jobCountUp     +0x58 jobCountDown     (ButtonElement)
//   +0x60 workplaceIndex (CycleWorkplace's roster cursor)
//
// B.4 DLG-e: the bodies are members; fields are read by name through the
// component classes (WorkPlaceComponent, WorkComponent, WorkDescription,
// CitizenBehavior, CitizenComponent, Profession).

#include "Dialog/WorkPlaceUI.h"
#include "ui/UserInterface.h"
#include "ui/DialogInstance.h"
#include "object/TypeInfo.h"
#include "Game/exe_bodies.h"
#include "Dialog/exe_bodies.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Game/ProfessionList.h"
#include "Game/Profession.h"
#include "Action/exe_bodies.h"           // B3R: Pf_RecursiveChildSum_123070
#include "Object/internal_factory_audit.h"   // ObjAudit::IsRegistered (stale weak-ref guard)
#include "system/String.h"
#include "system/Value.h"
#include <cstdint>
#include <cstdio>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "ui/Element.h"  // ENDGAME B.3.6 UI callers (K3)
#include "ui/Sprite.h"  // ENDGAME B.3.6 UI callers (K3)
#include "ui/SpriteSheet.h"  // ENDGAME B.3.6 UI callers (K3)
#include "object/External.h"  // ENDGAME B.3.6 UI callers (K3)
#include <new>  // ENDGAME B.3.6 UI callers (K3)
#include "ui/LabelElement.h"  // ENDGAME B.3.6 UI callers round 2 (K3)
#include "ui/TextElement.h"  // ENDGAME B.3.6 UI callers round 2 (K3)
#include "ui/ButtonElement.h"
#include "Object/factory_registration.h"
#include <cstddef>
#include "Component/WorkPlaceComponent.h"
#include "Component/WorkComponent.h"
#include "Component/WorkDescription.h"
#include "Component/BuildComponent.h"
#include "Component/InteractComponent.h"
#include "Component/CitizenComponent.h"
#include "Component/CitizenDescription.h"
#include "Entity/ComponentEntity.h"
#include "Entity/CitizenBehavior.h"
#include "Component/exe_bodies.h"   // CitizenEmploy_Strat_*, WorkPlaceComponent_SetMaxWorkers, fn_StorageCountQuery
#include "Game/PlayerToolManager.h"   // CenterOnEntity 0x140103040

namespace {

// FindElement(name) on the panel container, narrowed as OnCreated 0x140087940 narrows
// each find: IsDerivedFrom on T's type id (the import slots 0x140151668 LabelElement,
// 0x140151508 TextElement, 0x1401514f8 ButtonElement) with CMOVNZ, null otherwise.
template <class T>
T* FindNamedElement(UI::Element* container, const char* name) {
    System::String nameStr(name);
    UI::Element* found = container->FindElement(nameStr).Get();
    // The member, unguarded: no null test before any of 0x140087940's IsDerivedFrom
    // calls (0x1400879a8, 0x140087a1f, ...; B7 LX row 993).
    return found->IsDerivedFrom(T::TypeId()) ? static_cast<T*>(found) : nullptr;
}

// Faithful visibility subset of OnUpdateLayout (0x140087080): show the
// workPlace panel only while the building has an active workplace profession.
void UpdateVisibility(WorkPlaceUI* self) {
    UI::Element* panelElem = self->_container;
    if (!panelElem) return;  // controller not bound to its element yet

    ::Object::Base* source = self->_dataSource;
    ComponentEntity* entity = ::Object::IsDerivedFromNullSafe(source, ComponentEntity::TypeId())
        ? static_cast<ComponentEntity*>(source) : nullptr;
    // SA deviation (named): FindComponent on a null entity finds nothing here,
    // as the tree's guarded finder did.
    WorkPlaceComponent* workPlace = entity ? entity->FindComponent<WorkPlaceComponent>() : nullptr;

    // Show only when the workplace has an active profession; hidden on a
    // completed house/barn/pile whose construction workplace has cleared it.
    bool show = workPlace != nullptr && workPlace->profession != nullptr;
    panelElem->SetFlagsOnOff(show ? 0u : 1u, show ? 1u : 0u);
}

// ------------------------------------------------------------------
// Count population — faithful subset of OnUpdateLayout (0x140087080).
// ------------------------------------------------------------------
// The SA promotion originally ported ONLY the show/hide decision, so the
// workPlace panel's "X of Y" label (workPlaceCount) and the profession-count
// label (maxCount) stayed at their .rsc placeholders. This reproduces the
// retail count writes and arms the five controls with the ids OnCreated
// (0x140087940) stamps, for a panel OnCreated skipped.
void PopulateCounts(WorkPlaceUI* self) {
    UI::Element* panelElem = self->_container;
    if (!panelElem) return;

    ::Object::Base* source = self->_dataSource;
    ComponentEntity* entity = ::Object::IsDerivedFromNullSafe(source, ComponentEntity::TypeId())
        ? static_cast<ComponentEntity*>(source) : nullptr;
    if (!entity) return;

    WorkPlaceComponent* workPlace = entity->FindComponent<WorkPlaceComponent>();
    if (!workPlace) return;

    Profession* profession = workPlace->profession;
    if (!profession) return;  // no active profession -> panel hidden, nothing to show

    // Discover the two count elements once (they don't move).
    if (!self->workPlaceCount)
        self->workPlaceCount = FindNamedElement<UI::LabelElement>(panelElem, "workPlaceCount");
    if (!self->maxCount)
        self->maxCount = FindNamedElement<UI::TextElement>(panelElem, "maxCount");

    // Arm ALL FIVE interactive controls so their clicks route to OnElementEvent
    // with the retail ids (0x140087940):
    //   workerCountUp 0x1450  workerCountDown 0x1451  (assign/unassign worker)
    //   jobCountUp    0x1452  jobCountDown    0x1453  (raise/lower job slots)
    //   workPlaceButton 0x1454                         (cycle workplaces / centre camera)
    struct Arm {
        static void One(UI::ButtonElement*& slot, UI::Element* panel, const char* name, int id) {
            if (slot) return;
            slot = FindNamedElement<UI::ButtonElement>(panel, name);
            if (slot) slot->SetElementId(id);
        }
    };
    Arm::One(self->workerCountUp,   panelElem, "workerCountUp",   0x1450);
    Arm::One(self->workerCountDown, panelElem, "workerCountDown", 0x1451);
    Arm::One(self->jobCountUp,      panelElem, "jobCountUp",      0x1452);
    Arm::One(self->jobCountDown,    panelElem, "jobCountDown",    0x1453);
    // workPlaceButton has no field in retail; find and arm it each pass until it
    // takes (idempotent: SetElementId writes the element id).
    {
        UI::ButtonElement* button = FindNamedElement<UI::ButtonElement>(panelElem, "workPlaceButton");
        if (button && button->_elementId != 0x1454)
            button->SetElementId(0x1454);
    }

    // Roster = seated workers/builders at this site.
    uint32_t roster = static_cast<uint32_t>(workPlace->workers._size);

    // Capacity = the site's WorkComponent capacity, on the workplace's entity.
    uint32_t capacity = 0;
    {
        WorkComponent* work = workPlace->_entity->FindComponent<WorkComponent>();
        if (work) {
            if (work->workState == 0) work->workState = work->CalcMaxWorkers();   // 0x14012e5c0
            capacity = static_cast<uint32_t>(work->workState);
        }
    }

    // workPlaceCount: profession sprite + "roster / capacity".
    if (UI::LabelElement* label = self->workPlaceCount) {
        if (profession->spriteSheet) {
            UI::Sprite sprite(Object::ExternalPtr<UI::SpriteSheet>(profession->spriteSheet),
                              profession->stringName);
            label->SetSprite(&sprite);
        }
        System::String fmt1 = System::String::Format("%d", static_cast<uint64_t>(roster));
        label->SetParameter(0, fmt1);
        System::String fmt2 = System::String::Format("%d", static_cast<uint64_t>(capacity));
        label->SetParameter(1, fmt2);
    }

    // maxCount: town-wide count of adult, not-in-school citizens whose
    // profession matches this workplace's.
    if (UI::TextElement* text = self->maxCount) {
        int assigned = 0;
        CitizenBehavior* town = GameUpdateLoop::_singleton
            ? GameUpdateLoop::_singleton->m_citizenList
            : nullptr;
        if (town) {
            for (int i = 0; i < town->citizens._size && town->citizens._data; ++i) {
                CitizenComponent* citizen = town->citizens._data[i].Get();
                if (!citizen) continue;
                // CONTAINMENT, not a fix. CheckPtr is an EMPTY function in the shipping
                // Runtime (see internal_factory_audit.h): a stale weak ref in the town's
                // citizen array never self-invalidates, because retail clears refs
                // explicitly at removal — which our removal path does not yet do
                // everywhere. Without this, a freed-and-reused block reads garbage at
                // +0x288 and faults on desc+0x44 (live crash 2026-07-25: opening a
                // building panel under LIVE_DESTROY entity churn). Validate against the
                // InternalFactory handle table before dereferencing. The stale entry
                // itself still needs fixing at the removal site.
                if (!ObjAudit::IsRegistered(citizen)) continue;
                CitizenDescription* desc = citizen->description;
                if (!desc) continue;
                // Adult (age >= adultAge) AND not in school.
                if (desc->adultAge > citizen->age) continue;
                if (citizen->school != nullptr) continue;
                if (citizen->profession == profession) ++assigned;
            }
        }
        System::String fmt = System::String::Format("%d", static_cast<uint64_t>(static_cast<uint32_t>(assigned)));
        text->SetParameter(0, fmt);
    }
}

}  // namespace

// ------------------------------------------------------------------
// WorkPlaceUI::AdjustJobCount (0x140087610) — jobCountUp / jobCountDown
// ------------------------------------------------------------------
//   wpc = FindComponent<WorkPlaceComponent>(entity);            // FUN_140002e10
//   wc  = FindComponent<WorkComponent>(wpc->_entity);           // FUN_140003050
//   if (wc->workState == 0) wc->workState = CalcStartingJobCount(wc);   // FUN_14012e5c0
//   SetMaxWorkers(wpc, wc->workState + delta);                  // 0x14012fb00
// No clamping here — retail bounds the player with the button-enable tail in
// OnUpdateLayout (up: capacity < WorkDescription _i2c; down: capacity > _i28, or
// > 1 on the laborer branch), so the spinner is bounded by a greyed arrow.
void WorkPlaceUI::AdjustJobCount(int delta) {
    ::Object::Base* source = _dataSource;
    ComponentEntity* entity = ::Object::IsDerivedFromNullSafe(source, ComponentEntity::TypeId())
        ? static_cast<ComponentEntity*>(source) : nullptr;
    if (!entity) return;

    WorkPlaceComponent* workPlace = entity->FindComponent<WorkPlaceComponent>();
    // SA deviation (named): retail derefs both of these unguarded, because the
    // workPlace panel is only ever instanced for a real workplace entity. In SA the
    // panel template is shared and can be bound to an entity without a WorkPlace/Work
    // component, so bail with a logged reason.
    if (!workPlace) {
        fprintf(stderr, "[WorkPlaceUI] jobCount delta=%+d ABORTED: no WorkPlaceComponent on "
                        "entity=%p\n", delta, static_cast<void*>(entity));
        fflush(stderr);
        return;
    }

    WorkComponent* work = workPlace->_entity->FindComponent<WorkComponent>();
    if (!work) {   // same named SA deviation as the workPlace bail above
        fprintf(stderr, "[WorkPlaceUI] jobCount delta=%+d ABORTED: no WorkComponent on wpc=%p\n",
                delta, static_cast<void*>(workPlace));
        fflush(stderr);
        return;
    }
    if (work->workState == 0) work->workState = work->CalcMaxWorkers();   // 0x14012e5c0
    const int cur = work->workState;

    // No g_wpcRosterOnly wrapper here: the ported SetMaxWorkers body holds that guard
    // across its own eviction loop (destroycomponent_workerrelease.cpp).
    const int rosterBefore = workPlace->workers._size;
    workPlace->SetMaxWorkers(cur + delta);   // 0x14012fb00

    fprintf(stderr, "[WorkPlaceUI] jobCount delta=%+d %s WorkComponent+0x30 %d->%d (asked %d) "
                    "roster %d->%d\n",
            delta, "applied",
            cur, work->workState, cur + delta,
            rosterBefore, workPlace->workers._size);
    fflush(stderr);
}

// ------------------------------------------------------------------
// WorkPlaceUI::CycleWorkplace (0x1400876d0) — workPlaceButton
// ------------------------------------------------------------------
// Advances the roster cursor (wrapping at the roster count) and centres the
// camera on that worker's entity.
void WorkPlaceUI::CycleWorkplace() {
    ::Object::Base* source = _dataSource;
    ComponentEntity* entity = ::Object::IsDerivedFromNullSafe(source, ComponentEntity::TypeId())
        ? static_cast<ComponentEntity*>(source) : nullptr;
    WorkPlaceComponent* workPlace = entity->FindComponent<WorkPlaceComponent>();

    if (workPlace->workers._size == 0) return;

    workplaceIndex += 1;
    if (workPlace->workers._size <= workplaceIndex)
        workplaceIndex = 0;

    CitizenComponent* worker = workPlace->workers[workplaceIndex].Get();
    ComponentEntity* workerEntity = worker->_entity.Get();

    UI::DialogInstance* dialogInst = _dialog;
    GameUpdateLoop::_singleton->m_toolManager->CenterOnEntity(&dialogInst, &workerEntity);   // 0x140103040
}

// ============================================================
// WorkPlaceUI::OnCreated (0x140087940)
// ============================================================
// Finds the six named elements, stamps the button ids (and the
// workPlaceButton's), and hides the job-count arrows when the work
// description does not allow changing the job count.
void WorkPlaceUI::OnCreated() {
    if (!SA_DialogEntityAlive(this)) return;
    UI::Element* dialogElem = _container;

    workPlaceCount  = FindNamedElement<UI::LabelElement>(dialogElem, "workPlaceCount");
    maxCount        = FindNamedElement<UI::TextElement>(dialogElem, "maxCount");
    workerCountUp   = FindNamedElement<UI::ButtonElement>(dialogElem, "workerCountUp");
    workerCountDown = FindNamedElement<UI::ButtonElement>(dialogElem, "workerCountDown");
    jobCountUp      = FindNamedElement<UI::ButtonElement>(dialogElem, "jobCountUp");
    jobCountDown    = FindNamedElement<UI::ButtonElement>(dialogElem, "jobCountDown");

    workerCountUp->SetElementId(0x1450);
    workerCountDown->SetElementId(0x1451);
    jobCountUp->SetElementId(0x1452);
    jobCountDown->SetElementId(0x1453);

    UI::ButtonElement* workplaceButton = FindNamedElement<UI::ButtonElement>(dialogElem, "workPlaceButton");
    workplaceButton->SetElementId(0x1454);

    ::Object::Base* source = _dataSource;
    ComponentEntity* entity = ::Object::IsDerivedFromNullSafe(source, ComponentEntity::TypeId())
        ? static_cast<ComponentEntity*>(source) : nullptr;
    // SA deviation (named): the tree's guarded finder answered null for a null entity.
    WorkComponent* work = entity ? entity->FindComponent<WorkComponent>() : nullptr;

    // DECLARED DEVIATION (2026-08-15, when idx4 was first wired into the vtable).
    // Retail derefs the WorkComponent unguarded here because a WorkPlaceUI is only
    // ever built over an entity that carries one. Ours is not: the workPlace panel is
    // part of a SHARED dialog template that the framework instantiates for every
    // selectable building, so this pass also runs over houses, barns and piles that
    // legitimately have no WorkComponent — the case CLAUDE.md names as the one where a
    // guard belongs. The buttons found above keep their .rsc default state, so
    // skipping here is a strict no-op for those buildings.
    if (!work) return;
    Description* descSource = work->_description;
    WorkDescription* workDesc = ::Object::IsDerivedFromNullSafe(descSource, WorkDescription::TypeId())
        ? static_cast<WorkDescription*>(descSource) : nullptr;

    if (workDesc && !workDesc->_b20) {
        jobCountUp->SetFlagsOnOff(1, 0);
        jobCountDown->SetFlagsOnOff(1, 0);
    }
}

// ============================================================
// WorkPlaceUI::OnUpdateLayout (0x140087080)
// ============================================================
// Shows the panel while the workplace has a profession, fills the worker
// counts and profession icon, and enables the four arrows.
void WorkPlaceUI::OnUpdateLayout() {
    if (!SA_DialogEntityAlive(this)) return;
    UpdateVisibility(this);
    PopulateCounts(this);

    ::Object::Base* source = _dataSource;
    ComponentEntity* entity = ::Object::IsDerivedFromNullSafe(source, ComponentEntity::TypeId())
        ? static_cast<ComponentEntity*>(source) : nullptr;

    // Retail also runs a discarded IsDerivedFrom(workPlace->_description,
    // WorkPlaceDescription) here.
    WorkPlaceComponent* workPlace = entity->FindComponent<WorkPlaceComponent>();
    WorkComponent* work = entity->FindComponent<WorkComponent>();

    Description* descSource = work->_description;
    WorkDescription* workDesc = ::Object::IsDerivedFromNullSafe(descSource, WorkDescription::TypeId())
        ? static_cast<WorkDescription*>(descSource) : nullptr;

    // No profession: the building is complete (or not a workplace) — hide the panel.
    if (workPlace->profession == nullptr) {
#ifdef _DEBUG
        printf("[WorkPlaceUI] HIDDEN: wpComp=%p profession(+0x48)=NULL entity=%p\n",
               static_cast<void*>(workPlace), static_cast<void*>(entity));
#endif
        _container->SetFlagsOnOff(1, 0);
        return;
    }

    _container->SetFlagsOnOff(0, 1);

    ProfessionList* professions = GameUpdateLoop::_singleton->m_professionList;
#ifdef _DEBUG
    printf("[WorkPlaceUI] entity=%p wpComp=%p workComp=%p jobType=%p jt+0x20=%d prof=%p\n",
        static_cast<void*>(entity), static_cast<void*>(workPlace), static_cast<void*>(work),
        static_cast<void*>(workDesc), workDesc ? static_cast<int>(workDesc->_b20) : -1,
        static_cast<void*>(workPlace->profession));
#endif
    bool showJobArrows = workDesc->_b20;
    if (!showJobArrows && workPlace->profession == professions->field_0x38) {
        // The laborer-style profession: the arrows show while a BuildComponent exists.
        if (entity->FindComponent<BuildComponent>() != nullptr)   // 0x14000d6c0
            showJobArrows = true;
    }
    if (showJobArrows) {
        jobCountUp->SetFlagsOnOff(0, 1);
        jobCountDown->SetFlagsOnOff(0, 1);
    } else {
        jobCountUp->SetFlagsOnOff(1, 0);
        jobCountDown->SetFlagsOnOff(1, 0);
    }

    uint32_t workerCount = static_cast<uint32_t>(workPlace->workers._size);

    // The capacity of the WorkComponent on the workplace's own entity.
    WorkComponent* siteWork = workPlace->_entity->FindComponent<WorkComponent>();
    if (siteWork->workState == 0) siteWork->workState = siteWork->CalcMaxWorkers();   // 0x14012e5c0
    uint32_t capacity = static_cast<uint32_t>(siteWork->workState);

    {
        Profession* profession = workPlace->profession;
        UI::Sprite sprite(Object::ExternalPtr<UI::SpriteSheet>(profession->spriteSheet),
                          profession->stringName);
        workPlaceCount->SetSprite(&sprite);
    }
    {
        System::String fmtStr = System::String::Format("%d", workerCount);
        workPlaceCount->SetParameter(0, fmtStr);
    }
    {
        System::String fmtStr = System::String::Format("%d", capacity);
        workPlaceCount->SetParameter(1, fmtStr);
    }

    // Count the town's adults of this profession, and its laborers.
    CitizenBehavior* town = GameUpdateLoop::_singleton->m_citizenList;
    Profession* laborer = professions->field_0x30;

    int workplaceWorkerCount = 0;
    int laborerCount = 0;

    int citizenCount = town->citizens._size;
    for (int i = 0; i < citizenCount; i++) {
        CitizenComponent* citizen = town->citizens._data[i].Get();

        // Retail 0x1400873d2: COMISS XMM0,[RCX+0x44] / JC — CF is set only when
        // XMM0 is strictly BELOW, so the skip is `<`, not `<=`. A citizen sitting
        // exactly on the adulthood threshold counts as an adult.
        const float citAge = citizen->age;
        const float adultAge = citizen->description->adultAge;
        const bool inSchool = citizen->school != nullptr;
        if (citAge < adultAge) continue;
        if (inSchool) continue;

        Profession* assignedProf = citizen->profession;
        if (assignedProf == workPlace->profession) workplaceWorkerCount++;
        if (assignedProf == laborer) laborerCount++;
    }

    {
        System::String fmtStr = System::String::Format("%d", workplaceWorkerCount);
        maxCount->SetParameter(0, fmtStr);
    }

    workerCountUp->Enable(laborerCount > 0);
    workerCountDown->Enable(workplaceWorkerCount > 0);

    // Job arrows: the entity's own WorkComponent (not the workplace entity's).
    if (work->workState == 0) work->workState = work->CalcMaxWorkers();   // 0x14012e5c0
    int wpMaxCount = work->workState;

    // The discriminator for "6/6 assigned but no citizens available": `workers` comes from
    // the roster while `assigned`/`laborers` come from the citizen loop. Debug builds only:
    // it runs on every UI update of an open workplace window (2.4 million lines in a tester's
    // 2-hour session when it shipped). stderr, not stdout: stdout stalls in this build.
#ifdef _DEBUG
    fprintf(stderr,
        "[WorkPlaceUI] workers=%u/%u assigned=%d laborers=%d wpMax=%d citizens=%d "
        "wpProf=%p laborProf30=%p jt+0x28=%d jt+0x2c=%d jt+0x34=%d\n",
        workerCount, capacity, workplaceWorkerCount, laborerCount, wpMaxCount,
        citizenCount, static_cast<void*>(workPlace->profession), static_cast<void*>(laborer),
        workDesc ? workDesc->_i28 : -1,
        workDesc ? workDesc->_i2c : -1,
        workDesc ? static_cast<int>(workDesc->_b34) : -1);
    fflush(stderr);
#endif

    if (workPlace->profession == professions->field_0x38) {
        // The laborer-style profession with a BuildComponent: the limit is the
        // smaller of the description's maximum and the InteractComponent's count.
        if (entity->FindComponent<BuildComponent>() != nullptr) {   // 0x14000d6c0
            int maxCap = workDesc->_i2c;
            InteractComponent* interact = entity->FindComponent<InteractComponent>();   // 0x140002ed0
            int posCount = interact->NodeCount(1);   // 0x140123070
            if (posCount < maxCap) {
                InteractComponent* interact2 = entity->FindComponent<InteractComponent>();   // 0x140002ed0
                maxCap = interact2->NodeCount(1);   // 0x140123070
            }
            jobCountUp->Enable(wpMaxCount < maxCap);
            jobCountDown->Enable(wpMaxCount > 1);
            return;
        }
    }

    int jobMinCount = workDesc->_i28;

    jobCountUp->Enable(wpMaxCount < workDesc->_i2c);
    jobCountDown->Enable(wpMaxCount != jobMinCount && wpMaxCount > jobMinCount);
}

// [8] STANDALONE DEVIATION (retail inherits the base slot) — the tree's wiring, kept.
// The framework dispatches only slot 8 each frame, so the refresh is driven
// here and stays live as buildings complete.
void WorkPlaceUI::OnUpdate(const System::Time& /*time*/) {
    WorkPlaceUI::OnUpdateLayout();
}

// ============================================================
// WorkPlaceUI::OnElementEvent (0x1400877e0)
// ============================================================
// The panel has TWO spinners, and retail gives them DIFFERENT jobs:
//
//   0x1452 / 0x1453  jobCountUp/Down   -> AdjustJobCount(+/-1) (0x140087610)
//        raises/lowers the slot cap via SetMaxWorkers; lowering evicts the
//        surplus. It seats nobody — it only opens/closes seats.
//   0x1450 / 0x1451  workerCountUp/Down -> FindAvailableBuilding (0x14008d100)
//        + the seat strategies / RemoveWorker (0x140091180): the seat/unseat
//        pair that actually converts a laborer into a worker of this profession.
//   0x1454           workPlaceButton   -> CycleWorkplace (0x1400876d0)
void WorkPlaceUI::OnElementEvent(Object::InternalPtr<UI::Element>* /*element*/, int32_t msg, int32_t id,
                                 int32_t /*c*/, int32_t /*d*/, int32_t /*e*/, void* /*userData*/) {
    if (!SA_DialogEntityAlive(this)) return;
    if (msg != 0x12) return;                        // button click
    if (id < 0x1450 || id > 0x1454) return;         // one of the 5 armed controls

    if (id == 0x1454) { CycleWorkplace(); return; }
    if (id == 0x1452 || id == 0x1453) { AdjustJobCount(id == 0x1452 ? 1 : -1); return; }

    // Remaining ids are the worker assign/unassign pair.
    const bool add = (id == 0x1450);

    ::Object::Base* source = _dataSource;
    ComponentEntity* entity = ::Object::IsDerivedFromNullSafe(source, ComponentEntity::TypeId())
        ? static_cast<ComponentEntity*>(source) : nullptr;
    if (!entity) return;

    WorkPlaceComponent* workPlace = entity->FindComponent<WorkPlaceComponent>();
    if (!workPlace) return;
    Profession* profession = workPlace->profession;
    if (!profession) return;

    if (!GameUpdateLoop::_singleton) return;
    CitizenBehavior* town = GameUpdateLoop::_singleton->m_citizenList;
    if (!town) return;

    // NO g_wpcRosterOnly HOLD HERE: this path runs at gameplay time, when the
    // visual toggle is meant to run, so Attach/Detach do their full work.
    if (add) {
        // Add one worker: request a slot, then convert idle -> working -> student -> any.
        ComponentEntity* retBuf[2] = {nullptr, nullptr};
        town->FindAvailableBuilding(retBuf, profession);   // 0x14008d100
        ComponentEntity* slot = retBuf[0];
        ComponentEntity* slotCopy = slot;
        // Each strategy returns its `out` argument, so the result is read there.
        CitizenComponent* found = nullptr;
        town->FindWorkerToRelocate(&found, &slotCopy, profession);   // 0x14008cbf0
        // Faithful to retail driver dd10 (0x14007dd10): FindWorking (0x14008ca20)
        // unconditionally anchors *availPtr, so retail gates it behind a real
        // building (slot != 0). FindIdle / FindStudent / FindAny guard it themselves.
        if (found == nullptr && slot != nullptr) {
            slotCopy = slot;
            town->FindNearestHousedLaborer(&found, &slotCopy, profession);   // 0x14008ca20
        }
        if (found == nullptr) {
            slotCopy = slot;
            town->FindLaborerFromFullestHouse(&found, &slotCopy, profession);   // 0x14008c5f0
        }
        if (found == nullptr) {
            slotCopy = slot;
            CitizenComponent* anyBuf = nullptr;
            town->FindIdleLaborer(&anyBuf, &slotCopy, profession);   // 0x14008c4b0
        }
    } else {  // 0x1451 — remove one worker of this profession
        town->RemoveWorker(profession);   // 0x140091180
    }
}

// create 0x140016dc0: MemAlloc(0x68, 8), then the ctor (DialogController ->
// Internal: AddObject, the compiler's stamps, the field initialisers).
::Object::Base* WorkPlaceUI::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(WorkPlaceUI), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) WorkPlaceUI());
}
// Retail's createInPlace does not build a controller in place (B8-13); the
// tree's in-place entry did, and that is kept.
::Object::Base* WorkPlaceUI::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) WorkPlaceUI());
}

DEFINE_EXE_TYPE(WorkPlaceUI, "UI::DialogController")
