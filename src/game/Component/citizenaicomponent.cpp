// src/game/Component/citizenaicomponent.cpp
// Clean reimplementation of CitizenAIComponent::Update from Application-x64.exe.
//
// CitizenAIComponent (0xD0 bytes) is the core citizen AI decision loop.
// Runs every game tick per citizen. Logic:
//   1. Decrement 16 cooldown timers at +0x40..+0xBF, clamp to 0
//   2. Increment general accumulators at +0xC0 and +0xC8
//   3. Find home storage, query resource count — if empty, reset +0xC8
//   4. Check hunger: citizen health +0x44 vs config threshold +0xCC
//      - If hungry and not in transition states: try to release workplace,
//        then transition to hunger state
//   5. Check thirst: citizen health +0x48 vs config threshold +0xEC
//      - Similar logic to hunger
//   6. If neither hungry nor thirsty: check if citizen can find housing
//   7. Check education eligibility and age transitions
//
// Vtable at 0x140162470.
// Original: 0x14010f3e0
//
// Decompiled from Application-x64.exe using Ghidra.

#include "Component/CitizenAIComponent.h"
#include <cstdint>
#include "Component/RawMaterialDescription.h"   // B.3.8 CB1a (TypeId)
// EXT SEAM (NOT RETAIL, docs/extensions/handins/RESTRICTEDFOODS.md): ext.restricted_foods
// (class C), Ext::RestrictedFoods::g_on, Ext::RestrictedFoods::IsBlocked
#include "ext/restricted_foods/restricted_foods.h"
#include "ext/registry.h"                        // EXT SEAM (NOT RETAIL): Ext::Fix().stranded_wade
#include "ext/stranded_wade/stranded_wade.h"     // EXT SEAM (NOT RETAIL, docs/retail_bugs.md §19)
#include "Component/CitizenComponent.h"   // ENDGAME B.3.8 CIT (TypeId)
#include "Component/CitizenDescription.h" // B.4 W1 COMP-b: description fields by name
#include "Component/CharacterComponent.h"
#include "Component/AttendanceComponent.h"
#include "Game/Profession.h"
#include "Component/ResidenceComponent.h" // B.3.8 CB1a (TypeId)
#include "Component/StorageComponent.h"   // B.3.8 CB1a (TypeId)
#include "options.h"
#include <new>
#include "object/TypeInfo.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "Component/exe_bodies.h"
#include "Work/exe_bodies.h"
#include "system/MemoryAllocator.h"   // B3R: the allocator doors
#include "Entity/ComponentEntity.h"   // ComponentEntity::FindComponent<T>
#include "Component/ModelComponent.h"    // ModelComponent::meshInstance (the age-mesh colour)
#include "Component/ComponentDescription.h"   // StorageComponent::Item::_resource sub-descriptions
#include "system/Value.h"                // System::Time

// Force-work harness target: the first start-town structure (StorageBarn), captured
// in starttown.cpp. Used only when BANISHED_FORCE_WORK is set (Opt().forceWork). See Update.
extern ComponentEntity* g_pForceWorkBuilding;

// Phase B-real force-ForesterWork driver (forester_force_harness.cpp, env
// BANISHED_FORCE_FORESTERWORK). Constructs + pushes a ForesterWork action onto ONE
// force-worked citizen's CharacterComponent action stack (replacing the inert building
// ResourceProduceComponent controller) so the executor ticks ForesterWork::Execute ->
// real tree harvest -> production. Returns true if it owns the citizen this tick (the
// caller then skips the competing AI state machine). Self-gated; no-op when env unset.
extern "C" bool StartTown_ForcePushForesterWork(CitizenComponent* citizen);
// Organic-construction test driver (construction_force_harness.cpp, env
// BANISHED_FORCE_CONSTRUCT). Same contract: drives ONE citizen's construction Work
// (gather -> build) at the demo foundation; owns the citizen when active; no-op when unset.
extern "C" bool StartTown_ForcePushConstructionWork(CitizenComponent* citizen);

// ============================================================
// Exe helpers, called by name (Component/Work exe_bodies.h):
//   StorageComponent_QueryCount 0x14012cf10, Entity_FindComponent_Storage 0x140008090,
//   Storage_GetResourceCount 0x14012a9d0, CitizenComponent_ReleaseWorkplace 0x1401152e0,
//   CitizenComponent_CanFindHousing 0x140115be0, CitizenAIComponent_TransitionState
//   0x14010db90, _CheckEducation 0x14010f7a0, _CheckAge 0x14010f890.
// ============================================================

// Shared type-id cells (defined below with the callee reimpls).
static void EnsureCallees();

// ============================================================
// CitizenAIComponent::Update (vtable slot [9], 0x14010f3e0) -- the body
// ============================================================
// Main citizen AI tick. RDX = pointer to the elapsed System::Time (int64 ticks).
// timers[14] / timers[15] (+0xB0 / +0xB8) are the cooldowns of states 0xe / 0xf.
void CitizenAIComponent::Decide(const System::Time* time) {
    int64_t dt = time->_ticks;

    // --- Phase 1: count the 16 per-state timers down, clamped at 0 ---
    for (int i = 0; i < 16; i++) {
        timers[i]._ticks -= dt;
        if (timers[i]._ticks < 0) {
            timers[i]._ticks = 0;
        }
    }

    // --- Phase 2: the two accumulators ---
    leisureTimer._ticks += dt;
    dataC8._ticks += dt;

    // --- Phase 3: the citizen's own StorageComponent (what it carries) ---
    // 0x14010f447-0x14010f4bc: FindComponent<StorageComponent> (cell 0x1401c7e78) on
    // the citizen's entity, then QueryCount 0x14012cf10 on the result, unguarded.
    StorageComponent* storageComp = citizen->_entity->FindComponent<StorageComponent>();
    if (storageComp->QueryCount() == 0) {
        dataC8._ticks = 0;
    }

    // --- Phase 4: the seeking flags ---
    uint8_t hungerFlag = seekingFood;
    uint8_t thirstFlag = seekingWarmth;
    int aiState = state;

    bool isInNeedState = false;
    if ((hungerFlag != 0 && aiState == 0x0E) || (thirstFlag != 0 && aiState == 0x0F)) {
        isInNeedState = true;
    }

    // --- Phase 5: hunger ---
    CitizenComponent* citizenData = citizen.Get();
    float health = citizenData->hunger;
    float* pHungerThresh = &citizenData->description->_fcc;

    if (health <= *pHungerThresh || health == *pHungerThresh || isInNeedState ||
        ((uint32_t)(aiState - 0x0E) < 2) ||
        timers[14]._ticks > 0) {
        // Not hungry enough to transition, or in special state
        if (hungerFlag != 0) {
            citizenData = citizen.Get();
            health = citizenData->hunger;
            pHungerThresh = &citizenData->description->_fcc;

            if (health < *pHungerThresh || health == *pHungerThresh) {
                seekingFood = 0;
            }
        }
    } else {
        // Hungry: eat at home first when the home has food
        if (citizen->home != nullptr) {
            ResidenceComponent* homeStorage = citizen->home->FindComponent<ResidenceComponent>();
            int resourceCount = homeStorage->GetFoodCount();

            if (resourceCount > 0) {
                citizen->EatFrom(citizen->home.Get());
            }
        }

        // Re-check hunger after eating
        citizenData = citizen.Get();
        health = citizenData->hunger;
        pHungerThresh = &citizenData->description->_fcc;

        if (*pHungerThresh <= health && health != *pHungerThresh) {
            TransitionState(1);
            seekingFood = (state == 0x0E) ? 1 : 0;
        }
    }

    // --- Phase 6: coldness ---
    citizenData = citizen.Get();
    float hydration = citizenData->coldness;
    float* pThirstThresh = &citizenData->description->_fec;

    // Re-read flags (may have been modified above)
    thirstFlag = seekingWarmth;
    aiState = state;
    isInNeedState = ((seekingFood != 0 && aiState == 0x0E) ||
                     (thirstFlag != 0 && aiState == 0x0F));

    if (hydration <= *pThirstThresh || hydration == *pThirstThresh || isInNeedState ||
        ((uint32_t)(aiState - 0x0E) < 2) ||
        timers[15]._ticks > 0) {
        // Not cold enough or in special state
        if (thirstFlag != 0) {
            citizenData = citizen.Get();
            hydration = citizenData->coldness;
            pThirstThresh = &citizenData->description->_fec;

            if (hydration < *pThirstThresh || hydration == *pThirstThresh) {
                seekingWarmth = 0;
            }
        }
    } else {
        // Cold: transition to the warming state
        TransitionState(1);
        seekingWarmth = (state == 0x0F) ? 1 : 0;
    }

    // --- Phase 7: housing check ---
    if (seekingFood == 0 && seekingWarmth == 0) {
        char canFindHouse = citizen->CanFindHousing();
        if (canFindHouse != 0 && state != 2) {
            TransitionState(1);
        }
    }

    // --- Phase 8: an action is running -> nothing more to decide ---
    if (citizen->character->actionStack.current + 1 > 0) {
        return;
    }

    // The action stack is empty: schooling, the age mesh, and a new state
    CheckEducation();
    CheckAge();
    TransitionState(0);
}

// ============================================================
// Callee reimpls (Component reg wave, batch "people")
// These are the exe helper bodies the call sites above resolve+call.
// Ported faithfully from Ghidra; sub-deps resolved via guarded ResolveAddr
// (same pattern as the rest of code/Game). Registered in reg_component_people.cpp.
// ============================================================

// Shared type-ID globals used by the storage scans / finders.
static uint32_t* s_TypeId_ResidenceComponent = nullptr; // DAT_1401c7cf0
static uint32_t* s_TypeId_RawMaterial = nullptr;         // DAT_1401c7cb8
static uint32_t s_tidResidence_local = 0;   // ENDGAME B.3.9 C1: namespace-scope, no lazy cache (B6-5)
static uint32_t s_tidRawMaterial_local = 0;

// The REAL StorageComponent finder (0x140006270, tid c7e78) — the exe's
// Storage_GetResourceCount (0x14012a9d0) calls THIS, not the misnamed
// 0x140008090 residence finder (disasm 0x14012a9ef: CALL 0x140006270).

// CheckAge sub-deps (CitizenComponent setup helpers).

// TransitionState sub-deps.


static bool s_calleeResolved = false;
static void EnsureCallees() {
    if (s_calleeResolved) return;
    // ENDGAME B.3.9 (B39_RULES.md D5): 0x1401c7cf0 / 0x1401c7cb8 are registered
    // type-id cells (INVENTORY_C Table 1: ResidenceComponent, RawMaterialDescription),
    // read BY CELL as the class's static TypeId() instead of the raw exe address (the
    // Residence value is unconditionally overwritten again below by ::Update's
    // per-tick repoint; caching class statics into namespace-scope storage keeps
    // B6-5's no-lazy-first-use-cache rule).
    s_tidResidence_local = ResidenceComponent::TypeId();
    s_tidRawMaterial_local = RawMaterialDescription::TypeId();
    s_TypeId_ResidenceComponent = &s_tidResidence_local;
    s_TypeId_RawMaterial = &s_tidRawMaterial_local;
    s_calleeResolved = true;
}

// ============================================================
// CitizenComponent age-mesh setup helpers (Component reg wave, batch B).
// 0x140115110 SetupYoung / 0x1401151a0 SetupAdult / 0x140115230 SetupElder.
// Each writes a uniform RGB scale (a .rdata float constant) into the citizen's
// MeshInstance colour-tint fields (+0x134/+0x138/+0x13c) and the citizen's index
// (self+0x34, as float) into +0x140. The MeshInstance is reached through the
// InternalBasePtr at citizen+0x278 (deref +0x68). CheckPtr both the IBP and the
// resolved local copy, exactly like the original. Closure: only CheckPtr (engine).
// Faithful ports; the only difference vs the exe is the scale constant address.
// ============================================================
// Byte-faithful scale constants (DAT_14017214c/12c/118). HARDCODED literals, not
// read via ResolveAddr(Module::Exe, .rdata) — that returns NULL in standalone, so
// the previous `s_pXScale ? *s_pXScale : 1.0f` fallback collapsed EVERY age-state
// to 1.0, meaning children (SetupElder) never shrank and the per-frame OnUpdate2
// age→scale switch silently overwrote any external scale with 1.0. The C++
// literals compile to the exact bit patterns: 1.0f=0x3f800000, 0.83f=0x3f547ae1,
// 0.66f=0x3f28f5c3.
static constexpr float kScaleYoung = 1.0f;   // grown / adult full-size
static constexpr float kScaleAdult = 0.83f;  // intermediate life-state
static constexpr float kScaleElder = 0.66f;  // child / small render

// 0x140115110 (and its two twins): the citizen's index (+0x34, as float) and the
// scale constant, then CharacterComponent (+0x278) -> its ModelComponent (+0x68) ->
// the MeshInstance colour (+0x134..+0x140), every pointer read unguarded.
static void CitizenComp_SetupAgeMesh(CitizenComponent* citizen, float scale) {
    const int idx = citizen->field_0x34;
    ModelComponent* model = citizen->character.Get()->modelComponent.Get();
    model->meshInstance._color[0] = scale;
    model->meshInstance._color[1] = scale;
    model->meshInstance._color[2] = scale;
    model->meshInstance._color[3] = (float)idx;
}

void CitizenComponent::SetupYoung() {
    CitizenComp_SetupAgeMesh(this, kScaleYoung);
}
void CitizenComponent::SetupAdult() {
    CitizenComp_SetupAgeMesh(this, kScaleAdult);
}
void CitizenComponent::SetupElder() {
    CitizenComp_SetupAgeMesh(this, kScaleElder);
}


// (CitizenComp_StorageComponent_QueryCount, a second port of StorageComponent::QueryCount 0x14012cf10 with a
// null return retail lacks -- known_port_issues row 551 -- went in B6 once every caller called the member.)

// ResidenceComponent::GetFoodCount (0x14012a9d0):
// walks to the owner entity's StorageComponent (finder 0x140006270 — NOT the
// 0x140008090 residence finder; disasm-verified) then sums (count - reserved)
// of inventory rows whose material has the EDIBLE flag (bit 5 at +0x5c).
// [MISPORT FIX 2026-07-19] This used to call the residence finder and read the
// ResidenceComponent's +0x28/+0x30 as inventory rows — which only "worked"
// because the controller state node at +0x28 was absent (inert factory) and the
// start-town food seed hacked rows into it. With the real residence state node
// wired (heat tick bring-up), +0x28 is the node vtable; the exe-faithful path
// reads the sibling StorageComponent.
int ResidenceComponent::GetFoodCount() const {
    EnsureCallees();
    // 0x140006270 (was its WorkBase_FindComponent_6270 forwarder)
    StorageComponent* storage = _entity->FindComponent<StorageComponent>();
    int total = 0;   // no storage test: 0x14012aa0a CMP [RAX+0x30] straight off the finder
    // EXT SEAM (NOT RETAIL, docs/extensions/handins/RESTRICTEDFOODS.md): ext.restricted_foods (class C):
    // tested once here, above the loop.
    const bool restrictFoods = Ext::RestrictedFoods::g_on;
    int rowCount = storage->storageSlots._size;
    if (0 < rowCount) {
        int row = 0;
        do {
            const StorageComponent::Item* arr = storage->storageSlots._data;
            const ComponentDescription* inv = arr[row]._resource;
            const RawMaterialDescription* mat = nullptr;
            int n = inv->_subDescCount;
            for (int j = 0; j < n; j++) {
                Description* sub = inv->_subDescs[j];
                uint32_t tid = sub->Type();   // slot 2
                if (tid == *s_TypeId_RawMaterial) {
                    mat = sub->IsDerivedFrom(*s_TypeId_RawMaterial)   // CALL [0x140152a98], 0x14012aae1
                              ? static_cast<const RawMaterialDescription*>(sub) : nullptr;
                    break;
                }
            }
            if ((mat->_flags >> 5 & 1) != 0 &&   // 0x14012aa75 MOV ECX,[RAX+0x5c], no test
                // EXT SEAM (NOT RETAIL, docs/extensions/handins/RESTRICTEDFOODS.md): a blocked food does not
                // count as home food
                !(restrictFoods && Ext::RestrictedFoods::IsBlocked(mat))) {
                total += (arr[row]._count - arr[row]._reserve);
            }
            row++;
        } while (row < storage->storageSlots._size);
    }
    return total;
}

// CitizenAIComponent::TransitionState (0x14010db90):
// state-machine transition. Tries the queued state; if invalid, builds a
// candidate list and enters the first state whose handler started an action.
void CitizenAIComponent::TransitionState(char param) {
    EnsureCallees();
    // EXT SEAM (NOT RETAIL, docs/retail_bugs.md §19): fix.stranded_wade. A citizen cut off from his
    // home's zone (homeless: the town hall's) whose walk there was refused on the zone mismatch wades
    // home (or to the town hall) instead of asking for the refused walk again (src/ext/stranded_wade/).
    if (Ext::Fix().stranded_wade && Ext::StrandedWade::Rescue(this)) return;
    int queued = queuedState;
    char ok = 0;
    if (queued >= 0) {
        ok = ValidateAndEnterState(queued);
    }
    if (queued < 0 || ok == '\0') {
        int idx = 0;
        StateCandidateList list = {nullptr, 0, 0};  // {ptr, count, capacity}
        BuildCandidateStateList(&list, param);
        StateCandidate* arr = list.data;
        int n = list.count;
        StateCandidate* p = arr;
        if (0 < n) {
            do {
                ValidateAndEnterState(p->id);
                if (0 < citizen->character->actionStack.current + 1) {
                    state = arr[idx].id;
                    break;
                }
                idx++;
                p += 1;
            } while (idx < n);
        }
        System::MemoryAllocator::MemFree(arr);
    } else {
        state = queuedState;
        queuedState = -1;
    }
}

// CitizenAIComponent::CheckEducation (0x14010f7a0):
// raises the citizen's StorageComponent capacity to its profession's value if what it
// carries is below it. Retail: FindComponent<StorageComponent> 0x140006270 at 0x14010f7e1 and
// 0x14010f830; profession +0x1f0 -> +0x50 read at 0x14010f7f2 / 0x14010f7f9; capacity +0x38
// compared at 0x14010f80c and stored at 0x14010f85e; QueryCount 0x14012cf10 at 0x14010f845.
// No null test anywhere (profession and storage are dereferenced unguarded).
void CitizenAIComponent::CheckEducation() {
    EnsureCallees();
    StorageComponent* storage = citizen->_entity->FindComponent<StorageComponent>();
    int cap = citizen->profession->priority;
    if (storage->capacity != cap) {
        StorageComponent* storage2 = citizen->_entity->FindComponent<StorageComponent>();
        if (storage2->QueryCount() < cap) {
            storage->capacity = cap;
        }
    }
}

// CitizenAIComponent::CheckAge (0x14010f890):
// routes the citizen to the young/adult/elder mesh setup from its age against the
// description's adultAge and whether it attends a school.
void CitizenAIComponent::CheckAge() {
    EnsureCallees();
    CitizenComponent* d = citizen.Get();
    float ageThresh = d->description->adultAge;
    float age = d->age;
    if (ageThresh <= age && d->school == nullptr) {
        citizen->SetupYoung();
        return;
    }
    if (citizen->school != nullptr) {
        citizen->SetupAdult();
        return;
    }
    citizen->SetupElder();
}

// ===== Standalone factory (Foundation D — component instantiation) =====
// size 0xd0 (CitizenAIComponent_Factory MemAlloc), flags 0x89 (Ctor 0x14010fb70:
// Component base sets +0x20=0x80, leaf OR 0x89 => 0x89).
#include "Object/factory_registration.h"
#include <cstdlib>
#include <cstring>

// ============================================================================
// ENDGAME B.3.8 CIT — CitizenAIComponent as a REAL class (CitizenAIComponent.h).
//
// Before: a hand-expanded COMPONENT_LIVE_FACTORY_S — a 48-slot
// CitizenAIComponent_LiveVtable filled at every create (slots 3 Serialize,
// 4 Startup, 9 Update), Inert_ScalarDtor in slot 0. After: the compiler's
// 16-slot vtable and a real ctor/dtor chain (B38_RULES D1, D2). Slot 3 is wired
// by construction: a class's table cannot lose its serializer.
// ============================================================================
#include "Object/factory_registration.h"

static std::atomic<int> s_caiForceWorkLog{0};   // the [ForceWork] trace cap (harness only)


// 0x14010fb70. Component() is the CALL 0x1401188b0; the 0x140162470 stamp is
// the compiler's; flags |= 0x89. The sixteen System::Time() at +0x40 are zero.
// +0x28 = 0xc (state, 0x14010fba0) and +0x2c = -1 (queued state, 0x14010fba7)
// are the ctor's own stores.
CitizenAIComponent::CitizenAIComponent()
    : state(0xc), queuedState(-1), seekingFood(false), seekingWarmth(false), _pad32{},
      citizen(nullptr), timers{}, leisureTimer{0}, dataC8{0} {
    OrFlagsInCtor(0x89);
}

// [4] Startup 0x14010f920: +0x38 = FindComponent<CitizenComponent> (0x140009790,
// cell 0x1401c7158) on the owner, read unguarded. The ctor 0x14010fb70 holds every
// other initial value (state 0xc, queuedState -1, the two seeking flags 0).
void CitizenAIComponent::Startup() {
    citizen = _entity->FindComponent<CitizenComponent>();
}

// [6] 0x14010f880: `JMP 0x14010f890`, the whole body is CheckAge. Dispatched by
// ComponentEntity::OnAddedPass at EntityList::RegisterEntity (the ctor's flags 0x89
// carry bit 0), after StartupComponents found the sibling CitizenComponent (+0x38)
// and that component's Startup resolved its description (+0x288): the citizen's
// age mesh is set at registration, not first at the next AI tick.
void CitizenAIComponent::OnAdded() {
    CheckAge();   // 0x14010f890
}

// [9] 0x14010f3e0. RDX is a pointer to the elapsed System::Time (EntityList
// bucket tick 0x1400c42af). Retail's body is Decide, with no test of +0x38, of the
// citizen's entity, description or CharacterComponent.
//
// The three force harnesses below are the tree's, each behind its existing Opt()
// flag (BANISHED_FORCE_WORK, BANISHED_FORCE_FORESTERWORK, BANISHED_FORCE_CONSTRUCT;
// options.h); with the flags off (the default) Update is retail's.
void CitizenAIComponent::Update(const System::Time* time) {
    // FORCE-WORK HARNESS (Opt().forceWork): point ONE unhoused citizen's home
    // (+0x238) at the first start-town structure, so State_Work has a target.
    const char* e = Opt().forceWork;
    if (e && e[0] && e[0] != '0' && g_pForceWorkBuilding && citizen->home == nullptr) {
        new (&citizen->home) ::Object::InternalBasePtr(g_pForceWorkBuilding);
        if (s_caiForceWorkLog.fetch_add(1, std::memory_order_relaxed) < 6) {
            fprintf(stderr, "[ForceWork] citizen=%p workplace+0x238=%p\n",
                    (void*)citizen.Get(), g_pForceWorkBuilding);
            fflush(stderr);
        }
    }
    // FORESTER / CONSTRUCTION HARNESSES (Opt().forceForesterWork / forceConstruct):
    // when one owns the citizen this tick, the AI decision is skipped.
    if (StartTown_ForcePushForesterWork(citizen.Get())) return;
    if (StartTown_ForcePushConstructionWork(citizen.Get())) return;

    Decide(time);
}

// [15] 0x14010f340 (150 B). The message id, the source and both arguments are
// never read: any message is "decide now". The work / AI action ctors install a
// SendComponentMessage aimed at this component as the action stack's completion
// action (ActionStack +0x10); the executor's flush 0x1400c4bb0 runs it when the
// stack drains, and this re-decides in the same tick instead of the next one.
//   0x14010f34d  System::Time(0.0f)                      (import 0x140152b00)
//   0x14010f35b  CheckPtr(+0x38); citizen->Update(&t)     (vtable +0x48, slot 9)
//   0x14010f37e  entity = citizen->_entity (+0x10), CheckPtr
//   0x14010f3a4  entity +0x40 bit 10 (marked for removal) set -> return
//   0x14010f3af  System::Time(0.0f); this->Update(&t)    (vtable +0x48, slot 9)
// No test of +0x38 or of the entity: retail has none.
void CitizenAIComponent::OnMessage(int /*message*/, void* /*data*/, uint32_t /*arg0*/, uint32_t /*arg1*/) {
    System::Time time{0};
    RtSystem_Time_Ctor(&time, 0.0f);
    citizen->Update(&time);

    ComponentEntity* entity = citizen->_entity.Get();
    if (((entity->_entityFlags >> 10) & 1) == 0) {
        RtSystem_Time_Ctor(&time, 0.0f);
        Update(&time);   // slot 9, this component's own Update
    }
}

// create 0x14002fb20: MemAlloc(0xd0, 8), then the ctor.
::Object::Base* CitizenAIComponent::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(CitizenAIComponent), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) CitizenAIComponent());
}
// Retail's createInPlace is `Internal::CreateInPlaceObject` (returns null); the
// tree's in-place entry built the object, and that is kept (B38 B8-13).
::Object::Base* CitizenAIComponent::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) CitizenAIComponent());
}

DEFINE_EXE_TYPE(CitizenAIComponent, "Component")
