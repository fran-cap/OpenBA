// src/game/Game/founding_spawn.cpp
// Retail founding-colonist spawn — the port of CitizenComponent_impl_0dd0_sub_1040
// (0x1400a1040) and the per-family arithmetic of CitizenComponent_impl_0dd0
// (0x1400a0dd0), which together create the start-town population.
//
// This REPLACED the hand-rolled SpawnOneCitizen in starttown.cpp (open H of
// docs/history/notes/NEXT_SESSION_BRIEF.md, section 4), which reproduced most of FUN_140117080's
// effects piecemeal (gender, mesh variant, spawn transform, name, colour, age,
// lifespan) and burned a second RNG stream. Graduated unconditional 2026-09-13 and
// the hand-rolled path deleted; this is now the only founding-colonist spawner.
//
// ---------------------------------------------------------------------------
// Retail sub_1040 (0x1400a1040), signature
//   (startConfig, InternalBasePtr* outCitizenComp, float* basePos, float age,
//    char genderSel, char educated)
// body, in order:
//   1. MemAlloc(0x48, 8); copy the ExternalRef triple at startConfig+0x48
//      ({String@+0x48, int@+0x50, External*@+0x58}) to a local, FUN_14001b1d0 on the
//      copy (re-resolve), ComponentEntity_CopyCtor(mem, resolvedTemplate).
//   2. wrap the entity in an IBP, Entity_FindComponent_Citizen -> out param, CheckPtr.
//   3. desc = *(comp+0x18); IsDerivedFrom(desc, DWORD_1401c7190 = CitizenDescription).
//   4. if (age < desc+0x44)  educated = 0.       // children are never educated
//   5. pos = { RandFloatTol()*jitter + base.x, RandFloatTol()*jitter + base.y, base.z }
//      with jitter = *(float*)(startConfig+0x40).  NB the draw ORDER: the FIRST draw
//      scales Y and the SECOND scales X (the decompile evaluates the Vector3 args
//      right-to-left). Preserved below so the RNG stream matches vanilla.
//   6. laborerProf = *(*(gw+0xb0) + 0x30)         // ProfessionList -> Laborer
//   7. FUN_140117080(comp, &pos, gender = (genderSel == '\0'), isChild = age < desc+0x44,
//                    age /*FLOAT*/, educated, &nullIBP, laborerProf)
//   8. FUN_14001b1d0(startConfig+0x48) again, then
//      FUN_1400c48a0(*(gw+0xe8), &entityIBP, *(int*)(template+0x20))   // EntityList add
//
// Retail impl_0dd0 (0x1400a0dd0) around it:
//   * scans the citizen template's sub-description array (+0x28 / count +0x30) for the
//     CitizenDescription — that is where the age ranges live;
//   * per family: adult A with genderSel='\0', adult B with '\x01', both aged
//       RandFloat() * (desc+0x54 * DAT_140172114 - desc+0x50) + desc+0x50
//     (a FRESH draw each), spouse cross-link +0x250 both ways, then Rand(maxKids+1)
//     children aged RandFloat() * desc+0x44, educated='\0', genderSel alternating from
//     TRUE and persisting ACROSS families, each joined to adult B via helper_68a0.
//   DAT_140172114 = 0.5f (read from the exe at 0x140172114: 00 00 00 3f), the same
//   constant starttown.cpp already names kCenterBias.
// The family loop itself stays in starttown.cpp (it owns the famEntities /
// g_foundingCitizens bookkeeping the founding-housing, laborer-seed and town-registration
// passes consume); this file exports the per-colonist spawn and the two age draws so
// every RNG draw and every description read stays in one place.
//
// ---------------------------------------------------------------------------
// Deviations from retail, all forced by standalone and all logged:
//   a) Entity_FindComponent_Citizen (0x140009790) is NOT registered, and the exe
//      type-id global it reads (DAT_1401c7158) is null on a default run. The finder is
//      reimplemented inline (identical body: scan entity+0x30 array of entity+0x38
//      IBPs, match vtable+0x10 GetTypeId) with the type id taken from the exe global
//      when present and ::Object::GetTypeIdByName("CitizenComponent") otherwise — the
//      same fallback the in-game birth path uses (citizencomponent.cpp).
//   b) IsDerivedFrom against DWORD_1401c7190 fails in standalone (the description's
//      type-id isn't recognised), which would leave retail's `desc` NULL and fault on
//      desc+0x44. Established derive-or-TRUST pattern: keep the check, but fall back to
//      the linked description at comp+0x18 when it fails.
//   c) FUN_14001b1d0 is not registered either; the equivalent is the inline
//      ResolveExternalByName_Impl. It is find-only and caches its result, so a re-resolve
//      that missed would null the template starttown.cpp loaded via FindOrLoad. The
//      caller therefore passes the already-resolved template as a fallback and we restore
//      the cache if the re-resolve comes back empty.
//   d) A garbage jitter radius (NaN / negative / absurd) is clamped to 0, same guard
//      SetCitizenSpawnTransform already applied. The field is read raw from the .rsc.

#include <new>
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "system/MemoryAllocator.h"   // System::MemoryAllocator::MemAlloc / MemFree
#include "Game/GameUpdateLoop.h"   // caller batch CB6: the world's named fields
#include "Entity/ComponentEntity.h"   // ComponentEntity(ComponentDescription*) (0x1400c3720)
#include "Entity/EntityList.h"        // EntityList::RegisterEntity (0x1400c48a0)
#include "Component/exe_bodies.h"            // CitizenComp_DeferredInit_117080
#include "Action/exe_bodies.h"               // Vector3Slot (the spawn position)
#include "Game/exe_bodies.h"                 // fn_Random_RandFloatTol
#include "Object/factory_registration.h"
#include "object/TypeInfo.h"                 // caller batch CB6: Object::Base::Type()
#include "Component/CitizenComponent.h"     // caller batch CB6: TypeId()
#include "Component/CitizenDescription.h"
#include "Component/ComponentDescription.h"   // the citizen template (B.4 GAME-a)
#include "Entity/ComponentEntity.h"
#include "Game/MapGenerator.h"               // the start config (B.4 GAME-a)
#include "Game/ProfessionList.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "system/Value.h"      // ENDGAME B.3.4b System caller batch (Random/Time/Color)
#include "object/External.h"   // ENDGAME B.3.9 SS2: Object::ExternalFactory::_singleton

// The ComponentEntity allocation (0x1400a1040's MemAlloc(0x48, 8)).
static constexpr int ENTITY_SIZE     = 0x48;

static constexpr float kAdultAgeBias = 0.5f;   // DAT_140172114

// CitizenComp_DeferredInit_117080 (0x140117080) — the whole per-citizen init, called by
// name (Component/exe_bodies.h), with the age argument typed as FLOAT: it was long
// declared `int` and only worked because every existing caller passed literal 0 (same
// bit pattern as 0.0f). See open H.

static int  s_trace = 0;

// ---------------------------------------------------------------------------
// Type ids — read directly (D5, ENDGAME B.3.9 CG3): cells 0x1401c7158 and
// 0x1401c7190 through the classes' own X::TypeId(), never the engine_funcs
// TypeId_* globals (which read the SAME cells).
// ---------------------------------------------------------------------------
static uint32_t TidCitizenComponent() {
    return CitizenComponent::TypeId();
}
static uint32_t TidCitizenDescription() {
    return CitizenDescription::TypeId();
}

// Entity_FindComponent_Citizen (0x140009790), reimplemented — see deviation (a).
// Retail writes the find into an InternalBasePtr through its ctor (its callers
// then CheckPtr the slot); the value is the bare component pointer.
static CitizenComponent* FoundingSpawn_FindCitizenComp(ComponentEntity* entity) {
    Component* found = nullptr;
    uint32_t want = TidCitizenComponent();
    if (entity && want != 0) {
        int count = entity->_components._size;                          // +0x38
        Object::InternalPtr<Component>* arr = entity->_components._data;    // +0x30
        for (int i = 0; i < count && arr; ++i) {
            Component* comp = arr[i].Get();
            if (!comp) continue;
            // Slot 2 on an entity component-array element.
            if (comp->Type() == want) {
                found = comp;
                break;
            }
        }
    }
    return static_cast<CitizenComponent*>(found);   // its type id matched
}

// The head of impl_0dd0: find the CitizenDescription in the citizen template's
// sub-description array (raw pointers, no IBPs; match on vtable+0x10 GetTypeId, then
// IsDerivedFrom-validate). Result cached per template — retail re-scans on each
// impl_0dd0 call, but the scan draws no RNG and reads nothing mutable.
static CitizenDescription* FoundingCitizenDescription(const ComponentDescription* citizenTemplate) {
    static const ComponentDescription* s_tmpl = nullptr;
    static CitizenDescription* s_desc = nullptr;
    static bool  s_cached = false;
    if (s_cached && citizenTemplate == s_tmpl) return s_desc;   // includes the null case
    CitizenDescription* found = nullptr;
    uint32_t want = TidCitizenDescription();
    if (citizenTemplate && want != 0) {
        const ComponentDescription* t = citizenTemplate;
        int count = t->_subDescCount;    // +0x30
        Description** arr = t->_subDescs;   // +0x28
        for (int i = 0; i < count && arr; ++i) {
            Description* desc = arr[i];
            if (!desc) continue;
            // Slot 2 on a template description-array element.
            if (desc->Type() == want) {
                // Retail keeps the hit only if IsDerivedFrom agrees; standalone trusts
                // the GetTypeId match when the derive check can't run (deviation (b)).
                found = static_cast<CitizenDescription*>(desc);
                break;
            }
        }
    }
    s_tmpl = citizenTemplate;
    s_desc = found;
    s_cached = true;
    if (found && s_trace < 24) {
        ++s_trace;
        // +0x50 / +0x54 are the header's pregnancy-age pair; impl_0dd0 draws the
        // founders' ages from them (the tree's ageMin / ageMax).
        fprintf(stderr, "[FoundingSpawn] CitizenDescription=%p tid=%u adultAge(+0x44)=%.2f "
                "ageMin(+0x50)=%.2f ageMax(+0x54)=%.2f -> adult range [%.2f, %.2f)\n",
                (void*)found, want, found->adultAge,
                found->minPregnancyAge,
                found->maxPregnancyAge,
                found->minPregnancyAge,
                found->maxPregnancyAge * kAdultAgeBias);
        fflush(stderr);
    } else if (!found) {
        fprintf(stderr, "[FoundingSpawn] NO CitizenDescription on template=%p (tid=%u) — "
                "ages will fall back to 0\n", citizenTemplate, want);
        fflush(stderr);
    }
    return found;
}


static System::Random* FoundingSpawn_Rng() {
    if (GameUpdateLoop::_singleton == nullptr) return nullptr;
    return GameUpdateLoop::_singleton->m_random;
}

// ---------------------------------------------------------------------------
// The two age draws of impl_0dd0. Each consumes exactly one RandFloat, in the same
// place in the stream retail consumes it (immediately before the sub_1040 call it
// feeds), so the caller must not hoist them.
// ---------------------------------------------------------------------------
extern "C" float StartTown_FoundingAdultAge(const ComponentDescription* citizenTemplate) {
    const CitizenDescription* desc = FoundingCitizenDescription(citizenTemplate);
    System::Random* rng = FoundingSpawn_Rng();
    if (!desc || !rng) return 0.0f;
    float ageMin = desc->minPregnancyAge;   // +0x50
    float ageMax = desc->maxPregnancyAge;   // +0x54
    float r = rng->RandFloat();
    // FAITHFUL, including the odd shape: the upper bound is HALF the description's max
    // age, so the range is [ageMin, ageMax*0.5). No clamp to the adult threshold —
    // the hand-rolled path added one; retail has none, and if this ever produces an
    // "adult" below desc+0x44 that IS what vanilla does with these .rsc numbers.
    return r * (ageMax * kAdultAgeBias - ageMin) + ageMin;
}

extern "C" float StartTown_FoundingChildAge(const ComponentDescription* citizenTemplate) {
    const CitizenDescription* desc = FoundingCitizenDescription(citizenTemplate);
    System::Random* rng = FoundingSpawn_Rng();
    if (!desc || !rng) return 0.0f;
    float adultAge = desc->adultAge;   // +0x44
    return rng->RandFloat() * adultAge;   // uniform [0, adult threshold)
}

// Retail's two FUN_14001b1d0 calls (steps 1 and 8) — see deviation (c). Returns the
// template pointer the CopyCtor source and the entity-list category are both read from.
static ComponentDescription* FoundingSpawn_RefreshTemplate(MapGenerator* startConfig, ComponentDescription* fallback) {
    if (!startConfig) return fallback;
    if (::Object::ExternalFactory::_singleton)
        startConfig->citizen.ResolveDescription();   // +0x48
    ::Object::External** cache = &startConfig->citizen.resolved;   // +0x58
    if (*cache == nullptr && fallback != nullptr) {
        *cache = fallback;                       // restore what FindOrLoad had loaded
        if (s_trace < 24) { ++s_trace;
            fprintf(stderr, "[FoundingSpawn] re-resolve of the citizen ExternalRef missed; "
                    "restored the FindOrLoad template %p\n", fallback);
            fflush(stderr);
        }
    }
    // The citizen template is a ComponentDescription (the template pools name ComponentDescription resources).
    return *cache ? static_cast<ComponentDescription*>(*cache) : fallback;
}

// ---------------------------------------------------------------------------
// CitizenComponent_impl_0dd0_sub_1040 (0x1400a1040)
//
// `genderSel` is retail's param_5: '\0' -> gender 1, '\x01' -> gender 0.
// `educated`  is retail's param_6 (forced to 0 for children by step 4).
// `entityList` is the caller's copy of *(gw+0xe8), which is what retail reads.
// Returns the new entity (raw pointer); *outCitizenComp receives the CheckPtr'd
// CitizenComponent the family links are written through.
// ---------------------------------------------------------------------------
extern "C" ComponentEntity* StartTown_SpawnColonistRetail(MapGenerator* startConfig, ComponentDescription* citizenTemplate,
                                                          EntityList* entityList, const float base[3],
                                                          float age, char genderSel, char educated,
                                                          CitizenComponent** outCitizenComp) {
    if (outCitizenComp) *outCitizenComp = nullptr;
    System::Random* rng = FoundingSpawn_Rng();

    // 1. allocate + copy-construct the entity from the (re-resolved) citizen template
    ComponentDescription* tmpl = FoundingSpawn_RefreshTemplate(startConfig, citizenTemplate);
    ComponentEntity* mem = static_cast<ComponentEntity*>(System::MemoryAllocator::MemAlloc(ENTITY_SIZE, 8));
    ComponentEntity* ent = (mem && tmpl)
                     ? ::new (mem) ComponentEntity(tmpl)   // 0x1400c3720
                     : nullptr;
    if (!ent) {
        fprintf(stderr, "[FoundingSpawn] BAIL: CopyCtor produced no entity "
                "(mem=%p tmpl=%p)\n", static_cast<void*>(mem), tmpl);
        fflush(stderr);
        return nullptr;
    }

    // The entity's InternalPtr (retail wraps it in an InternalBasePtr).
    ::Object::InternalPtr<ComponentEntity> entIBP(ent);

    // 2. find the CitizenComponent
    CitizenComponent* comp = FoundingSpawn_FindCitizenComp(entIBP.Get());
    if (!comp) {
        // Never true in retail (the entity is a copy of the citizen template, so it
        // always carries the component). Registering a citizen entity with no
        // CitizenComponent would seed the town list with something no consumer can
        // read, so drop the whole colonist instead.
        fprintf(stderr, "[FoundingSpawn] ABORT: entity %p has no CitizenComponent "
                "(tid=%u) — colonist dropped\n", (void*)ent, TidCitizenComponent());
        fflush(stderr);
        return nullptr;
    }

    // 3. the linked CitizenDescription (derive-or-trust, deviation (b))
    Description* desc = comp->_description;   // +0x18
    if (desc) {
        uint32_t tid = TidCitizenDescription();
        if (tid != 0 && !::Object::IsDerivedFromNullSafe(desc, tid)) {
            // Retail takes NULL here and then reads desc+0x44 off it — a fault. Keep the
            // linked description and say so once (deviation (b)).
            static bool s_said = false;
            if (!s_said) { s_said = true;
                fprintf(stderr, "[FoundingSpawn] IsDerivedFrom(desc=%p, CitizenDescription=%u) "
                        "false — trusting comp+0x18 (standalone derive gap)\n", (void*)desc, tid);
                fflush(stderr);
            }
        }
    }
    const float adultAge = desc ? static_cast<CitizenDescription*>(desc)->adultAge   // +0x44
                                : 0.0f;
    const bool isChild = (age < adultAge);

    // 4. children are never educated
    if (isChild) educated = '\0';

    // 5. spawn position — jittered around the town centre. Draw order preserved:
    //    first RandFloatTol scales Y, second scales X.
    float jitter = startConfig->citizenRadius;   // +0x40 (_citizenRadius)
    if (!(jitter == jitter) || jitter < 0.0f || jitter > 1.0e4f) jitter = 0.0f;  // deviation (d)
    float jy = (rng ? rng->RandFloatTol() : 0.0f) * jitter;
    float jx = (rng ? rng->RandFloatTol() : 0.0f) * jitter;
    Vector3Slot pos;
    pos.v.x = base[0] + jx;
    pos.v.y = base[1] + jy;
    pos.v.z = base[2];

    // 6. the Laborer profession key (ProfessionList at gw+0xb0, +0x30)
    Profession* laborer = nullptr;
    if (GameUpdateLoop::_singleton != nullptr) {
        ProfessionList* profList = GameUpdateLoop::_singleton->m_professionList;
        if (profList) {
            laborer = profList->laborerProfession;
        }
    }

    // 7. the whole per-citizen init
    CitizenComponent* nullIBP = nullptr;
    new (&nullIBP) ::Object::InternalBasePtr(nullptr);
    comp->Initialize(&pos.v,   // 0x140117080
                                    static_cast<uint32_t>(genderSel == '\0'),
                                    isChild ? '\x01' : '\0', age, educated, nullIBP,
                                    laborer);

    // 8. register with the world entity list under the template's category
    tmpl = FoundingSpawn_RefreshTemplate(startConfig, tmpl);
    int category = tmpl ? tmpl->_category   // +0x20
                        : 0;
    ::Object::InternalPtr<ComponentEntity> regIBP = entIBP;
    entityList->RegisterEntity(&regIBP, category);   // 0x1400c48a0

    // LEFT: starttown.cpp (its block) takes the component as bytes.
    if (outCitizenComp) *outCitizenComp = comp;

    // [WGPAR] the per-colonist line is gone (port-only noise on the loader's start-town
    // path); "[startTown::spawn] citizens: spawned N" and "[startTown::summary]" count them.
    return ent;
}
