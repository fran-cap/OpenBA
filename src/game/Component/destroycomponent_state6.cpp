// src/game/Component/destroycomponent_state6.cpp
// DestroyComponent state 6 — COLLAPSE / RUBBLE. The arm the fire and tornado
// lanes both take, and until now the one remaining "[DestroySM] NOT-IMPLEMENTED
// state-6" stub in the whole destruction pipeline.
//
// ── How the game gets here ──────────────────────────────────────────────────
// FireComponent/TornadoComponent call FUN_14011e7b0 (ported as
// FireComponent_BeginBurning_11e7b0, firecomponent_smstate.cpp), which
//   * refuses when the building is unfinished (BuildComponent+0x48 == 0),
//   * stamps DestroyComponent+0x50 = 1  (the COLLAPSE flag) and
//     DestroyComponent+0x48 = the burn Time  (the collapse COUNTDOWN),
//   * drives the entity's state machine to state 3.
// State-3 enter (0x14011d7b0) then branches on that +0x50 byte: 0 -> state 4
// (the ordinary player demolition / upgrade path), non-zero -> state 6. So the
// whole disaster lane lands here, and with the handlers stubbed the building just
// sat wearing its intact mesh until the fire's own state machine forced state 7.
//
// ── The three retail handlers ───────────────────────────────────────────────
// Registration site: DestroyDescription_helper_dd00 (0x14011dd00), which calls
// `sub_3e50(6, A=0x14011d4d0, B=0x14011d400, D=0x14011d490)`. Per
// controller_state_table.h the populator's A/B/D land in record slots +0x08 /
// +0x20 / +0x18 — so A = enter, B = update, D = trans38 (the real per-frame tick,
// the same column state 4/5 use for their ticks):
//
//   enter   0x14011d4d0  Destroy_State6Enter_d4d0
//              cancel every citizen action that targets this building, empty its
//              storage, then either take a rubble step (countdown already spent)
//              or re-point the materials to the burnt set.
//   update  0x14011d400  Destroy_State6Update_d400
//              one step while the countdown is still running, plus a coin-flip
//              extra step (Random::Rand(rng, 2) == 0).
//   trans38 0x14011d490  Destroy_State6Tick_d490
//              per-frame: comp+0x48 -= *time; on reaching 0, clamp to 0 and take a
//              rubble step. This is what makes a burning building visibly fall
//              apart over its burn duration.
//
// ── The rubble step (0x14011d5d0) ───────────────────────────────────────────
// Not a one-shot "swap in the rubble mesh" — it steps the ModelComponent's
// mesh-variant index (+0x166) DOWN by one, floored at DestroyDescription+0x30, and
// re-registers the geometry in Graphics::Scene. Buildings author their damage
// stages as descending model variants, so repeated steps walk intact -> damaged ->
// rubble. Each step also re-points the mesh materials (0x140125020: the burnt
// material set at DestroyDescription+0x28) and re-stamps the ground decal
// (DecalComponent+0x28 = DestroyDescription+0x2c — the scorch mark).
//
// ── DestroyDescription fields ───────────────────────────────────────────────
// The names are the AUTHORED ones (field-name strings in Tools-x64.exe, keys in
// Template/*.rsc); the roles were confirmed from these bodies:
//   +0x28 _damagedSkin     rubble MATERIAL-SET index  -> GraphicsMesh::GetMaterialList
//   +0x2c _damagedDecal    rubble DECAL state         -> DecalComponent+0x28
//   +0x30 _minDamageLevel  rubble MESH-VARIANT floor  -> ModelComponent+0x166 min
//
// ── STANDALONE deviations (each logged once at runtime) ─────────────────────
//   * ActionStack_AbortForEntity_c52c0 skips any action whose vtable slot 8/9 is
//     null. Our action vtables are partially-filled arrays, not the exe's, so an
//     unported slot must not be called — retail's are all real functions.
//
// DEFAULT-ON via the default-on table (graduated 2026-08-23 on a fire soak: 3 buildings
// collapsed 2 -> 1 -> 0 over 8 rubble steps, empty crash log). BANISHED_LIVE_DESTROY_COLLAPSE
// is now a kill-switch (=0) for A/B runs.

// graphics/*.h FIRST: they pull src/engine/volume/Volume.h, whose global-scope
// `struct Rectangle` is hidden by <wingdi.h>'s `Rectangle()` once <windows.h> has
// been seen through resolve.h.  ENDGAME B.3.5 caller batch I2.
#include "graphics/GraphicsMesh.h"
#include "Component/ModelDescription.h"   // B.3.8 CB1a (TypeId)
#include "Component/DestroyDescription.h"   // B.3.8 CB1a (TypeId)
#include "Component/DecalComponent.h"   // B.3.8 CB1a (TypeId)
#include "graphics/Mesh.h"
#include <new>
#include "object/External.h"
#include "object/TypeInfo.h"
#include "graphics/Camera.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Object/factory_registration.h"   // ::Object::GetTypeIdByName
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "system/Value.h"   // ENDGAME B.3.4b System caller batch
#include "Action/Action.h"  // B.3.x W0: Action slots 8/9 by name
#include "Component/exe_bodies.h"
#include "Component/pickingcomponent_funcs.h"
#include "Action/exe_bodies.h"
#include "Work/exe_bodies.h"
#include "Component/DestroyComponent.h"
#include "Component/ModelComponent.h"
#include "Component/StorageComponent.h"
#include "Component/PickingComponent.h"
#include "Entity/ComponentEntity.h"
#include "Entity/CitizenBehavior.h"
#include "Component/CitizenComponent.h"
#include "Component/CharacterComponent.h"

// ============================================================
// Tracing
// ============================================================
// Milestone lines are deliberately NOT behind the feature flag or a trace env: a
// gate that only speaks when the feature is enabled goes silent exactly when the
// feature starts working. Budgeted so a long burn cannot flood the log.
static int  s_milestones = 0;
static bool Milestone() { return s_milestones++ < 60; }

// ============================================================
// Helpers, called directly
// ============================================================
//   0x140003f60 ModelComponent finder    FindComponent<ModelComponent>
//   0x140006270 StorageComponent finder  FindComponent<StorageComponent>
//   0x140026a30 PickingComponent finder  FindComponent<PickingComponent>
//   0x14000b630 DecalComponent finder    FindComponent<DecalComponent>
//   0x1400c2b70 model-changed notify     ModelComponentNotify_Reimpl
//   0x140128f50 pick-mesh rebuild        PickingComponent_RegisterMesh
// 0x14000f090 was Collection::Array<Bytes<0x10,8>>::SetSize monomorphised
// into the exe (ENDGAME B.3.2 step 2: stride 0x10, align 8, zero tail) —
// converted at its call site below.
// Retail's `IsDerivedFrom(p, tid) ? p : nullptr` idiom on a component's description.
template <class T>
static T* AsType(Description* obj) {
    if (!obj || !T::TypeId()) return nullptr;
    return ::Object::IsDerivedFromNullSafe(obj, T::TypeId()) ? static_cast<T*>(obj) : nullptr;
}

// Every finder is ComponentEntity::FindComponent<T>. The tree answered null for a
// null entity (its old FindOn helper tested it); retail has no such test. Kept (B9).
template <class T>
static T* FindOn(ComponentEntity* entity) {
    if (!entity) return nullptr;
    return entity->FindComponent<T>();
}

// ============================================================
// FUN_1400c52c0 — abort every action on one action stack that targets an entity
// ============================================================
// (stack, &entityIBP). Stack layout: +0x00 items (IBP array), +0x08 count,
// +0x10 the current action's IBP, +0x18 the current index.
// Pass 1 asks each live action `vtable+0x48(this, &entityCopy)` — "do you reference
// this entity?". If NONE does, the stack is left completely alone. If one does, pass
// 2 tears down the WHOLE stack: for every slot, walk the action and its child chain
// (child at action+0x10) calling `vtable+0x40` (abort) on each, release the slot's
// IBP and store a null IBP; finally reset the current index to -1 and release the
// current-action IBP. That is what stops citizens walking into a collapsing building.
// The stack is the ActionStack (Action.h); its only caller is in this file.
extern "C" void ActionStack_AbortForEntity_c52c0(ActionStack* stack, ComponentEntity** pEntityIbp) {
    if (!stack || !pEntityIbp) return;
    int count = stack->actions._size;
    if (count <= 0) return;

    // Action slots 9 (GetTarget, +0x48) and 8 (CheckEntity, +0x40) by name: every
    // Action has a compiler table (Action.h bodies 0x1400c4a40 / 0x1400c4a30), so the
    // old null-slot skip of the partially-filled standalone arrays is gone.
    ::Object::InternalPtr<Action>* items = stack->actions._data;
    if (!items) return;

    bool hit = false;
    for (int i = 0; i < count && !hit; ++i) {
        Action* act = items[i].Get();
        if (!act) continue;
        act = items[i].Get();
        if (!act) continue;
        ComponentEntity* entityCopy = *pEntityIbp;  // retail copies the raw ptr to a local
        hit = act->GetTarget(&entityCopy);   // slot 9
    }
    if (!hit) return;

    for (int i = 0; i < count; ++i) {
        Action* cur = items[i].Get();
        if (!cur) continue;
        for (Action* p = cur; p != nullptr; ) {
            p->CheckEntity();   // slot 8, the file's "Abort"
            p = p->_next.Get();   // +0x10, the chain's next node
        }
        items[i].DeletePtr();
        items[i] = ::Object::InternalPtr<Action>();   // a null IBP
    }

    stack->current = -1;
    if (stack->field_0x10.Get() != nullptr) {
        stack->field_0x10.DeletePtr();
        stack->field_0x10 = ::Object::InternalPtr<Action>();   // a null IBP
    }

    if (Milestone()) {
        std::fprintf(stderr, "[Collapse] action stack %p aborted (%d slot(s)) — it targeted "
                             "the collapsing building\n", stack, count);
        std::fflush(stderr);
    }
}

// ============================================================
// FUN_140089e30 — TownManager: abort every citizen's actions for one entity
// ============================================================
// (townManager, &entityIBP). Walks the town's citizen roster (ptr @+0xa0,
// count @+0xa8) — the same array citizen_employ_cascade.cpp and citizen_onremove.cpp
// walk — and runs the abort above on each citizen's CitizenAIComponent action stack
// (citizen+0x278, list at +0x48).
extern "C" void TownMgr_AbortCitizenActionsFor_89e30(CitizenBehavior* townMgr, ComponentEntity** pEntityIbp) {
    if (!townMgr || !pEntityIbp) return;
    CitizenBehavior* tm = townMgr;
    int n = tm->citizens._size;
    if (n <= 0) return;
    Object::InternalPtr<CitizenComponent>* base = tm->citizens._data;
    if (!base) return;
    for (int i = 0; i < n; ++i) {
        ComponentEntity* entityCopy = *pEntityIbp;
        CitizenComponent* citizen = base[i].Get();
        if (!citizen) continue;
        CharacterComponent* ai = citizen->character.Get();
        if (!ai) continue;
        // CharacterComponent +0x48..+0x67 is retail's ActionStack sub-object (actionStack).
        ActionStack_AbortForEntity_c52c0(&ai->actionStack, &entityCopy);
    }
}

// ============================================================
// ModelDescription mesh lookup
// ============================================================
// desc+0x20 is an array of 0x18-byte variant groups whose +0x08 holds the
// per-model-variant mesh array. Same math as modelcomponent.cpp's
// GetMeshFromDescription (there: `groupArray + 8 + variantIdx*0x18`; here the
// disassembly's `groups + (variantIdx*3)*8 + 8` — identical).
// The groups are ModelMeshGroup (ModelDescription.h), their +0x08 the mesh array.
static Graphics::GraphicsMesh* MeshFromModelDesc(ModelDescription* modelDesc, int16_t variantIdx, int16_t modelVariant) {
    if (!modelDesc || variantIdx < 0 || modelVariant < 0) return nullptr;
    const ModelMeshGroup* groups = modelDesc->_meshGroups;
    if (!groups) return nullptr;
    Graphics::GraphicsMesh* const* group = groups[variantIdx].meshes;
    if (!group) return nullptr;
    return group[modelVariant];
}

// ============================================================
// FUN_140125020 — ModelComponent: re-point every mesh material to one material set
// ============================================================
// (modelComp, materialSetIdx). Bails when either variant index is negative. Reads
// the mesh for the CURRENT variant pair out of the ModelDescription, asks it for
// material set `materialSetIdx` (GraphicsMesh::GetMaterialList(int) -> const
// Collection::Array& {data @+0x00, count @+0x08}) and writes each entry into the
// MeshInstance at modelComp+0x28 (MeshInstance::SetMaterialInstance). This is what
// makes the collapsing building look CHARRED rather than merely lower-poly.
// A ModelComponent member (RCX = the component; declared in ModelComponent.h).
void ModelComponent::SetMaterialSet(int materialSetIdx) {
    int16_t variantIdx   = meshGroup;
    int16_t modelVariant = meshIndex;
    if (variantIdx < 0 || modelVariant < 0) return;

    ModelDescription* modelDesc = AsType<ModelDescription>(_description);
    const Graphics::GraphicsMesh* mesh = MeshFromModelDesc(modelDesc, variantIdx, modelVariant);
    if (!mesh) return;

    // The two doors this used to go through are `GraphicsMesh::GetMaterialList`
    // and `MeshInstance::SetMaterialInstance`; both are members now, so the
    // readiness guard (and the "dispatch slot unregistered" warning it drove)
    // has nothing left to test. The Array header is still read raw, exactly as
    // retail does: GetMaterialList returns `*(mesh+0x70) + skin*0x10`, which is
    // formed from a possibly-null buffer pointer, so the `!list` test stays.
    const Collection::Array<Object::ExternalPtr<Graphics::MaterialInstance> >* list =
        &mesh->GetMaterialList(materialSetIdx);
    if (!list) return;
    const Object::ExternalPtr<Graphics::MaterialInstance>* data = list->_data;
    int    n    = list->_size;
    if (!data || n <= 0) return;
    for (int i = 0; i < n; ++i)
        meshInstance.SetMaterialInstance(i, Object::ExternalPtr<Graphics::MaterialInstance>(data[i].Get()));
}

// ============================================================
// FUN_14011d5d0 — ONE rubble step
// ============================================================
// Steps the model down one damage stage and refreshes everything hanging off the
// mesh. Called from all three state-6 handlers.
void DestroyComponent::RubbleStep() {
    ModelComponent* modelComp = FindOn<ModelComponent>(_entity.Get());
    if (!modelComp) return;                   // retail: no ModelComponent, nothing to do

    DestroyDescription* dd = AsType<DestroyDescription>(_description);
    if (!dd) {
        // STANDALONE ONLY. Retail derefs this unguarded because a DestroyComponent's
        // +0x18 is always a DestroyDescription; the way it comes back null HERE is an
        // unseeded type-id, which is our artifact rather than the game's.
        static bool warned = false;
        if (!warned) {
            warned = true;
            std::fprintf(stderr, "[Collapse] rubble step ABORTED — DestroyComponent+0x18 did "
                                 "not cast to DestroyDescription (type-id unseeded?)\n");
            std::fflush(stderr);
        }
        return;
    }
    const int rubbleMaterialSet = dd->_damagedSkin;
    const int rubbleDecalState  = dd->_damagedDecal;
    const int rubbleMeshFloor   = dd->_minDamageLevel;

    // _minDamageLevel is very often a hugely negative number (0xE1E1E1E1), and that is
    // CORRECT — not a deserialize bug. The resource compiler always emits the slot but
    // fills it with uninitialised memory when the .rsc omits the field, and only
    // Template/Market.rsc authors it (`int _minDamageLevel = 1`). See the long note in
    // destroydescription.cpp. An earlier revision of this function "refused" an
    // out-of-range value and substituted 0; that produced the right answer for the
    // wrong reason and printed a misleading warning, so it is gone. Retail's ordering
    // below already absorbs the garbage.

    // One step down the damage-stage ladder. Retail (0x14011d5d0) clamps to >= 0 FIRST
    // and only THEN raises to the description's floor — so a negative _minDamageLevel
    // never binds and the building collapses all the way to variant 0, while Market's
    // floor of 1 stops it at variant 1. The order of these two tests is load-bearing.
    const int cur = static_cast<int>(modelComp->meshIndex);
    int next = cur - 1;
    if (next < 0) next = 0;
    if (next < rubbleMeshFloor) next = rubbleMeshFloor;

    if (next != cur) {
        // Slot 7 (OnRemove) removes the current geometry from Graphics::Scene and
        // slot 6 (OnAdded) adds the new one back — ModelComponent's members.
        ModelComponent* model = modelComp;
        if (cur >= 0) model->OnRemove();
        modelComp->meshIndex = static_cast<int16_t>(next);
        if (next >= 0) {
            ModelDescription* modelDesc = AsType<ModelDescription>(modelComp->_description);
            Graphics::GraphicsMesh* mesh = MeshFromModelDesc(modelDesc, modelComp->meshGroup,
                                           static_cast<int16_t>(next));
            modelComp->meshInstance.SetMesh(Object::ExternalPtr<Graphics::GraphicsMesh>(mesh));
            model->OnAdded();
        }
        if (Milestone()) {
            std::fprintf(stderr, "[Collapse] rubble step comp=%p mesh variant %d -> %d "
                                 "(floor %d, matSet %d, decal %d)\n",
                         (void*)this, cur, next, rubbleMeshFloor,
                         rubbleMaterialSet, rubbleDecalState);
            std::fflush(stderr);
        }
    }

    // Tell the owner entity its model changed (the MultiObjectComponent child forwarder).
    ComponentEntity* modelOwner = modelComp->_entity.Get();
    if (modelOwner) modelOwner->SendEvent(0);

    // Burnt materials on whatever mesh is now showing.
    modelComp->SetMaterialSet(rubbleMaterialSet);

    // Pick-mesh rebuild.
    PickingComponent* picking = FindOn<PickingComponent>(_entity.Get());   // 0x140026a30
    if (picking) {
        picking->RefreshMesh();
    }

    // Scorch decal: swap the DecalComponent's state, and if a decal is currently
    // placed (+0x2c decalId >= 0) release and re-place it so the new state shows.
    DecalComponent* decal = FindOn<DecalComponent>(_entity.Get());   // 0x14000b630
    if (decal) {
        if (rubbleDecalState != decal->decalState) {
            decal->decalState = rubbleDecalState;
            if (decal->decalId >= 0) {
                decal->OnRemove();
                decal->OnAdded();
            }
        }
    }
}

// ============================================================
// FUN_14011d4d0 — state 6 ENTER
// ============================================================
// Note retail returns 0 (not 1) from both exits here; the dispatcher treats the
// enter return as "handled", and state 6 requests no transition of its own — the
// fire's own state machine forces state 7 when the burn ends.
char DestroyComponent::State6Enter() {
    // Building has storage -> cancel everything the town was doing with it, then
    // empty the storage outright (the goods burn; state 4's haul-away never runs).
    if (FindOn<StorageComponent>(_entity.Get()) != nullptr) {
        if (GameUpdateLoop::_singleton != nullptr) {
            CitizenBehavior* townMgr = GameUpdateLoop::_singleton->m_citizenList;
            ComponentEntity* entityCopy = _entity.Get();
            if (townMgr) TownMgr_AbortCitizenActionsFor_89e30(townMgr, &entityCopy);
        }
        StorageComponent* storage = FindOn<StorageComponent>(_entity.Get());
        if (storage) {
            storage->storageSlots.SetSize(0);
        }
        if (Milestone()) {
            std::fprintf(stderr, "[Collapse] state-6 enter comp=%p — storage emptied, "
                                 "citizen actions cancelled\n", (void*)this);
            std::fflush(stderr);
        }
    }

    DestroyDescription* dd = AsType<DestroyDescription>(_description);
    int64_t countdown = burnTime._ticks;

    if (countdown <= 0) {
        // Already spent (an instant destruction, e.g. a tornado strike) — go straight
        // to the rubble stage.
        if (Milestone()) {
            std::fprintf(stderr, "[Collapse] state-6 enter comp=%p countdown already 0 "
                                 "-> immediate rubble\n", (void*)this);
            std::fflush(stderr);
        }
        RubbleStep();
        return 0;
    }

    // Countdown still running: keep the current mesh but stamp the burnt material set
    // on it right away, so the building starts looking wrong the moment it catches.
    ModelComponent* modelComp = FindOn<ModelComponent>(_entity.Get());
    if (modelComp && dd)
        modelComp->SetMaterialSet(dd->_damagedSkin);
    if (Milestone()) {
        std::fprintf(stderr, "[Collapse] state-6 enter comp=%p countdown=%lld — burning\n",
                     (void*)this, static_cast<long long>(countdown));
        std::fflush(stderr);
    }
    return 0;
}

// ============================================================
// FUN_14011d400 — state 6 UPDATE (record slot +0x20)
// ============================================================
// Runs only when the state machine has a transition pending. Retail takes one
// rubble step while the countdown is still running, and then a SECOND one on a
// coin flip — so both can fire in the same call. Kept as-is.
char DestroyComponent::State6Update() {
    if (FindOn<ModelComponent>(_entity.Get()) == nullptr) return 1;

    // Retail calls IsDerivedFrom(comp+0x18, DestroyDescription) here and DISCARDS the
    // result — no branch reads it. Not replicated: it has no side effect.

    if (burnTime._ticks > 0)
        RubbleStep();

    if (GameUpdateLoop::_singleton != nullptr) {
        System::Random* rng = GameUpdateLoop::_singleton->m_random;
        if (rng && rng->Rand(2u) == 0) RubbleStep();
    }
    return 1;
}

// ============================================================
// FUN_14011d490 — state 6 TICK (record slot +0x18, the per-frame column)
// ============================================================
// comp+0x48 is the collapse countdown in raw System::Time units, seeded by
// FireComponent_BeginBurning_11e7b0. Subtract this frame's delta; when it reaches
// zero, clamp it and take the rubble step that puts the building in its final state.
char DestroyComponent::State6Tick(const System::Time* time) {
    int64_t remaining = burnTime._ticks;
    if (remaining == 0) return 1;
    remaining -= time ? time->_ticks : 0;
    burnTime._ticks = remaining;
    if (remaining > 0) return 1;

    burnTime._ticks = 0;
    if (Milestone()) {
        std::fprintf(stderr, "[Collapse] state-6 countdown expired comp=%p -> rubble step\n", (void*)this);
        std::fflush(stderr);
    }
    RubbleStep();
    return 1;
}
