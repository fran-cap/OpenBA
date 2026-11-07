// src/game/Component/buildcomponent.cpp
// Clean reimplementation of BuildComponent vtable methods.
//
// BuildComponent (0x78 bytes) — construction management component.
// Vtable: 0x140162260
//
// Fields (from Ghidra decompilation):
//   +0x48: int32  laborProgress accumulator (init 0) — raised +1 per delivered
//          build-labor SendComponentMessage (Slot15/+0x78); read by State8Update
//   +0x4C: int32  costMultiplier (init 1) — target = matComp[0x30] * this
//   +0x50: byte   buildFlag (serialized)
//   +0x58: void*  entityArray1 (InternalBasePtr array ptr)
//   +0x60: int    entityArray1Count
//   +0x68: void*  entityArray2 (InternalBasePtr array ptr)
//   +0x70: int    entityArray2Count
//
// Decompiled from Application-x64.exe using Ghidra.

#include "Component/BuildComponent.h"
#include "Component/MultiObjectComponent.h"   // B.3.8 CB1a (TypeId)
#include "Component/ModelDescription.h"   // B.3.8 CB1a (TypeId)
#include "Component/ModelComponent.h"
#include "Component/PickingComponent.h"
#include "object/TypeInfo.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch A
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include "Component/exe_bodies.h"

#include "Object/factory_registration.h"   // Object::GetTypeIdByName
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "Action/exe_bodies.h"
#include "Component/pickingcomponent_funcs.h"
#include "Entity/ComponentEntity.h"   // ComponentEntity::FindComponent<T>
#include "Entity/CitizenBehavior.h"

extern "C" void* g_pConstructionDemoSite;


// ============================================================
// Type ID globals needed by BuildComponent
// ============================================================


// ============================================================
// BuildComponent::Shutdown (vtable slot [3], 0x140108D80)
// ============================================================
// Serializes BuildComponent fields via IO::Serializer.
// Calls Component_Serialize for base class, then two versioned blocks:
//   - Version 0: empty (just ctor/dtor)
//   - Version 1: buildState, buildProgress, buildFlag,
//     entityArray1 (IBP array with IsDerivedFrom check),
//     entityArray2 (IBP array with IsDerivedFrom check)

// ============================================================
// BuildComponent::OnCreate (vtable slot [7], 0x140108CB0)
// ============================================================
// Removes component from the removal queue. Creates InternalBasePtr
// from self, searches the removal queue at GameState+0xF0+0xD0.

// 0x140017550 is Collection::Array<InternalPtr<T>>::Find (Collection.h; B9 RT).




// ============================================================
// Reimpl of FUN_140108d40 / body 0x140108720 — BuildComponent::OnAdded
//   (rebuild the worker/storage child lists and refresh build progress)
// ============================================================
// SLOT: vtable INDEX 6 (+0x30) — the one-shot OnAdded broadcast dispatched by
// ComponentEntity_OnAddedPass (componententity.cpp:701) at EntityList_RegisterEntity
// time, for components whose +0x20 bit 0 is set (BuildComponent's flags are 0x83, so
// it qualifies). It is NOT a per-frame draw-tick: verified 2026-07-27, the function's
// only xref is 0x140162290 == vtable_BuildComponent(0x140162260) + 0x30, and +0x40
// (index 8) is 0x140118a70, a shared empty `return;` stub. Ghidra's "Draw" label is
// the familiar slot-6 mislabel. The body is BuildComponent::FillModelLists.
//
// Faithful port of the Ghidra decompilation. The finders are
// ComponentEntity::FindComponent<T>: 0x140003f60 (ModelComponent), 0x140026a30
// (PickingComponent), 0x140004740 (MultiObjectComponent); 0x140128f50 is
// PickingComponent_RegisterMesh.
// 0x140003110 was Collection::Array<void*>::SetSize(n, fill) monomorphised
// into the exe (ENDGAME B.3.2 step 2) — converted at its call sites below.
// Type-id VALUES. The .rdata globals are never seeded (ResolveAddr(Exe,.rdata)==NULL)
// so we fall back to
// the runtime type registry by name — otherwise block 1's IsDerivedFrom is guarded out
// and the site's ModelComponent never gets appended to +0x58 (empty mesh list).
//   0x1401c7a88 = ModelDescription (checked on the found ModelComponent's +0x18 desc)
//   0x1401c7a50 = ModelComponent   (block 2 collects these into +0x58)
//   0x1401c7c10 = PickingComponent (block 2 collects these into +0x68) — CORRECTED
//                 2026-07-19: writer is PickingComponent_RegisterType 0x14014f6f5; was
//                 wrongly resolved by name as "StorageComponent" (wrong CRC → block-2
//                 scan matched the wrong component type)
//   0x1401c7ac0 = MultiObjectComponent (block 4 reads +0x58 build multiplier) —
//                 CORRECTED 2026-07-19: writer MultiObjectComponent_RegisterType
//                 0x14014f475; was wrongly resolved as "WorkPlaceComponent"
static uint32_t s_tid_ModelDesc  = 0;   // DAT_1401c7a88
static uint32_t s_tid_ModelComp  = 0;   // DWORD_1401c7a50
static uint32_t s_tid_Picking    = 0;   // DAT_1401c7c10
static uint32_t s_tid_MultiObj   = 0;   // DWORD_1401c7ac0
static bool s_bcDrawResolved = false;
static void LatchDrawTypeIds() {
    if (s_bcDrawResolved) return;
    s_tid_ModelDesc = ModelDescription::TypeId();
    s_tid_ModelComp = ModelComponent::TypeId();
    s_tid_Picking   = PickingComponent::TypeId();
    s_tid_MultiObj  = MultiObjectComponent::TypeId();
    s_bcDrawResolved = true;
}

// Scan an entity's component list (_components / _components._size) for the first
// component whose Type() (slot 2) == typeId; verify with IsDerivedFrom. The tree's
// null-type and null-slot guards are the tree's own (retail's inlined finder has
// neither); they are kept, not extended.
static Component* BC_FindComponentByType(ComponentEntity* compHost, uint32_t typeId) {
    if (!typeId) return nullptr;
    int count = compHost->_components._size;
    for (int i = 0; i < count; i++) {
        Component* comp = compHost->_components[i].Get();
        if (!comp) continue;
        uint32_t tid = comp->Type();
        if (tid == typeId) {
            if (::Object::IsDerivedFromNullSafe(comp, typeId)) return comp;
            return nullptr;
        }
    }
    return nullptr;
}

// 0x140108720, the OnAdded body.
void BuildComponent::FillModelLists() {
    LatchDrawTypeIds();

    if (GameUpdateLoop::_singleton == nullptr) return;
    // NOTE: the instant-build gate that used to sit here has MOVED to
    // OnAdded, because that is where retail keeps it. 0x140108d40 is
    // only a 4-instruction thunk:
    //     MOV RAX,[0x1401c3dd8]; TEST byte [RAX+0xd0],0x1; JZ 0x140108720; RET
    // The gate is in the THUNK; the body at 0x140108720 is ungated and has two callers —
    // the thunk (slot 6 / OnAdded) and 0x140107bb0 inside BuildDescription_helper_7ae0,
    // i.e. State8Enter's instant branch (ported 2026-07-27). Keeping the gate in the body
    // would wrongly suppress that second caller, since it runs precisely when the bit IS
    // set — and that caller is the whole insta-build completion path.

    // --- Stage 1: append the site's own ModelComponent (whose +0x18 ModelDescription
    //     has a non-empty frame group) to the +0x58 mesh list / +0x68 storage list.
    if (_entity->FindComponent<ModelComponent>() != nullptr) {
        Description* objBase = _entity->FindComponent<ModelComponent>()->_description;
        ModelDescription* multi = (s_tid_ModelDesc &&
                       ::Object::IsDerivedFromNullSafe(objBase, s_tid_ModelDesc))
                                      ? static_cast<ModelDescription*>(objBase) : nullptr;
        if (multi && multi->_meshGroupCount > 0) {
            Object::InternalPtr<ModelComponent> b1(_entity->FindComponent<ModelComponent>());
            // exe 0x140003110 IS Collection::Array<void*>::SetSize(n, fill) (kSpec_PtrZero).
            modelComponents.SetSizeFill(modelComponents._size + 1, &b1);
            Object::InternalPtr<PickingComponent> b2(_entity->FindComponent<PickingComponent>());
            pickingComponents.SetSizeFill(pickingComponents._size + 1, &b2);
        }
    }

    // --- Stage 2: iterate the MultiObjectComponent's child entities, collecting the
    //     ModelComponent and PickingComponent of each into the two lists.
    if (_entity->FindComponent<MultiObjectComponent>() != nullptr) {
        MultiObjectComponent* multiObj = _entity->FindComponent<MultiObjectComponent>();
        // LEFT: the child count is the low dword of the untyped +0x50 (MultiObjectComponent.h, COMP-d)
        int jobCount = multiObj->children._size;
        for (int j = 0; j < jobCount; j++) {
            ComponentEntity* host = multiObj->children[j].Get();
            Component* buildComp = BC_FindComponentByType(host, s_tid_ModelComp);
            Object::InternalPtr<Component> ib1(buildComp);
            modelComponents.SetSizeFill(modelComponents._size + 1, &ib1);

            ComponentEntity* host2 = multiObj->children[j].Get();
            Component* storeComp = BC_FindComponentByType(host2, s_tid_Picking);
            Object::InternalPtr<Component> ib2(storeComp);
            pickingComponents.SetSizeFill(pickingComponents._size + 1, &ib2);
        }
    }


    // --- Stage 3: walk the collected model list, resetting each model to
    //     build-controlled; register each picking entry via Entity_impl_8f50.
    //
    // NOTE (2026-07-27): the `mark >= 0` branch below is retail's "reset this model to
    // build-controlled" — hide it via the model's +0x38, stamp modelVariant = -1 — so the
    // state-8 ramp can drive its construction frames. Running it on an already-finished
    // building un-frames it permanently (nothing restores a frame; it never enters state
    // 8). Retail does not special-case that here; it suppresses this whole function via
    // the instant-build bit at the top (gw+0xd0 bit 0), which the start-town spawner sets
    // across each building's setup + RegisterEntity. See StartTown_SpawnStructures.
    int buildCount = modelComponents._size;
    for (int i = 0; i < buildCount; i++) {
        ModelComponent* node = modelComponents[i].Get();
        int16_t mark = node->meshIndex;
        if (mark == -1) {
            ComponentEntity* host = node->_entity.Get();
            int hc = host->_components._size;
            for (int k = 0; k < hc; k++) {
                // Retail passes EDX = 0 here too (0x140108a2e XOR EDX,EDX before
                // CALL [RAX+0x60] at 0x140108a33); the old C call dropped it.
                host->_components[k].Get()->OnEvent(0);   // slot 12
            }
        } else {
            if (mark >= 0) {
                node->OnRemove();   // slot 7
            }
            node->meshIndex = (int16_t)0xffff;
            ComponentEntity* host = node->_entity.Get();
            int hc = host->_components._size;
            for (int k = 0; k < hc; k++) {
                host->_components[k].Get()->OnEvent(0);   // slot 12
            }
        }
        if (pickingComponents[i].Get() != nullptr) {
            pickingComponents[i].Get()->RefreshMesh();   // 0x140128f50
        }
    }

    // --- Stage 4: multiplier = 1; then the MultiObjectComponent's +0x58.
    costMultiplier = 1;
    Component* wp = BC_FindComponentByType(_entity.Get(), s_tid_MultiObj);
    if (wp != nullptr) {
        MultiObjectComponent* wp2 = static_cast<MultiObjectComponent*>(
            BC_FindComponentByType(_entity.Get(), s_tid_MultiObj));
        // Retail: bc+0x4c = MultiObjectComponent+0x58 (the build-effort multiplier), copied
        // with no test (the decompile's `*(int*)(param_1+0x4c) = (int)local_res8[0xb]`).
        // The component's ctor sets 1 and the placement tools set the piece count (review
        // F-175: the old clamp dated from when the component was an inert stub).
        costMultiplier = static_cast<int>(wp2->multiCount);
    }

}

// ============================================================================
// ENDGAME B.3.8 MI-A — BuildComponent as a REAL class (BuildComponent.h).
//
// Before: CONTROLLER_LIVE_FACTORY_STATES (a 48-slot table re-filled at every
// create, slots 3/6/7/15 re-installed from PopulateStateTable), `Inert_ScalarDtor` in slot 0 (only `~Internal`), the
// ControllerLive::Dispatcher<BuildCtrlTag> table stamped into raw bytes at +0x28.
// After: the compiler's 18-slot vtable, a real constructor and destructor
// chain, the controller a member object (B38_RULES D1, D2, D6).
// ============================================================================
#include "Object/factory_registration.h"
#include "Component/BuildComponent.h"
#include "Component/controller_state_table.h"
// Build state graph (recovered from BuildDescription_Setup -> helper_9090 ->
// sub_f500/sub_f620, 0x140109090): three states 1 (default), 8, 2.
//   state 1: enter=helper_8290, setup=sub_81e0   ; transitions -> {8, 3}
//   state 8: enter=helper_7ae0, update=helper_7600, setup=sub_76e0
//                                                 ; transitions -> {2 (helper_75e0), 3}
//   state 2: (no handlers)                        ; transitions -> {3}
// (state-id 3 is a transition target only — not a registered state of this table;
//  it is resolved up the SM node chain.)
using BuildCtrlDispatch = ::ControllerLive::Dispatcher<BuildCtrlTag>;

// The state handlers are BuildComponent members (buildcomponent_state1.cpp /
// buildcomponent_state8.cpp). The table (controller_state_table.h) holds plain
// handler addresses called as handler([node+0x18], sm): [node+0x18] is the
// controller's back-pointer, this component, so each entry is a one-line
// adapter that calls the member on it. The null-receiver returns are the tree's
// (retail's handlers have none); kept, not extended.
static char BuildState1Enter(BuildComponent* bc, StateMachine* sm)   { return bc ? bc->State1Enter(sm) : 1; }    // sub_8290 (slot +0x18)
static char BuildState1Update(BuildComponent* bc, StateMachine* sm, const System::Time* /*time*/)  { return bc ? bc->State1Update(sm) : 1; }   // sub_81e0 (slot +0x38, per-frame)
static char BuildState8Enter(BuildComponent* bc, StateMachine* sm)   { return bc ? bc->State8Enter(sm) : 1; }    // sub_7ae0 (slot +0x18)
static char BuildState8Update(BuildComponent* bc, StateMachine* sm, const System::Time* /*time*/)  { return bc ? bc->State8Update(sm) : 1; }   // sub_76e0 (slot +0x38, per-frame)
static char BuildState8OneShot(BuildComponent* bc, StateMachine* sm) { return bc ? bc->State8OneShot(sm) : 1; }  // sub_7600 (slot +0x20, exit-transition)
static void BuildState8To2Action(BuildComponent* bc)         { if (bc) bc->State8To2Action(); }  // sub_75e0 (slot +0x28, 8->2 action)

// Table field->node-slot mapping (see controller_state_table.h + buildcomponent_state8.cpp):
//   enterHandler (arg2) -> slot +0x18 Enter        (on state entry)
//   trans38Handler(arg4) -> slot +0x38 DrawAlt     (PER-FRAME; altDraw=1 every frame)
//   updateHandler(arg5) -> slot +0x20 Update       (only when a transition is pending)
//   transition handler   -> slot +0x28 OnState     (the 8->2 action)
// TableRegisterState(t, id, enterH, trans30H, trans38H, updateH). Built ONCE at
// static-init time, as retail's static initializer does (B38 D6/D7).
static void BuildComponent_PopulateStateTable() {
    using namespace ::ControllerLive;
    auto& t = BuildCtrlDispatch::table;
    if (t.count != 0) return;  // idempotent
    // state 1 GatherMaterials: enter=State1Enter (spawner); per-frame=State1Update (materials -> req 8).
    TableRegisterState(t, BuildingState::GatherMaterials, &BuildState1Enter,
                       nullptr, &BuildState1Update, nullptr);
    TableAddTransition(t, BuildingState::Construct, nullptr);
    TableAddTransition(t, BuildingState::Demolish, nullptr);
    // state 8 Construct: enter=State8Enter (mesh reset); per-frame=State8Update (progress -> frames,
    //          complete -> req 2); exit-transition=State8OneShot; 8->2 action=Finalize.
    TableRegisterState(t, BuildingState::Construct, &BuildState8Enter,
                       nullptr, &BuildState8Update,
                       &BuildState8OneShot);
    TableAddTransition(t, BuildingState::Built, &BuildState8To2Action);
    TableAddTransition(t, BuildingState::Demolish, nullptr);
    TableRegisterState(t, BuildingState::Built, nullptr, nullptr, nullptr, nullptr);
    TableAddTransition(t, BuildingState::Demolish, nullptr);
}
static const bool s_buildStateTableBuilt = (BuildComponent_PopulateStateTable(), true);

// 0x140108fc0. Component() is the CALL 0x1401188b0; the stamps are the compiler's;
// `_controller(this)` is the member's back-pointer; +0x48 = 0, +0x4c = 1 (the cost
// multiplier, 0x140109023), +0x50 = 0 (byte), +0x58..+0x70 = 0; then
// `_flags |= 0x83`.
BuildComponent::BuildComponent()
    : _controller(this), laborProgress(0), costMultiplier(1), buildFlag(0), _pad51{} {
    OrFlagsInCtor(0x83);
}

// 0x140108f30: the body is empty. ~pickingComponents (+0x68/+0x70, 0x140108f55) and
// then ~modelComponents (+0x58/+0x60, 0x140108f69) free and zero the two lists, then
// ~_controller restamps +0x28 (0x140108f8b) and the bases restamp and ~Internal:
// reverse declaration order, retail's.
BuildComponent::~BuildComponent() {
}

// create 0x14002f2b0: MemAlloc(0x78, 8), then the ctor.
::Object::Base* BuildComponent::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(BuildComponent), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) BuildComponent());
}
// Retail's createInPlace is `Internal::CreateInPlaceObject` (returns null); the
// tree's in-place entry built the object, and that is kept (B38 B8-13).
::Object::Base* BuildComponent::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) BuildComponent());
}

// [3] 0x140108d80 — laborProgress/costMultiplier/buildFlag (+0x48/+0x4c/+0x50) and
// both component lists round-trip (save/load wave 2026-08-27).
void BuildComponent::Persist(IO::Stream& stream) {
    // Base class serialize (retail's direct CALL 0x1401189b0)
    Component::Persist(stream);

    IO::Stream& io = stream;

    // Version 0 block (empty — just opens and closes)
    { IO::Serializer ser0(io, 0); }

    // Version 1 block
    IO::Serializer ser1(io, 1);

    ser1.Serialize(laborProgress, nullptr);
    ser1.Serialize(costMultiplier, nullptr);
    ser1.Serialize(buildFlag, nullptr);

    // the model list's count (+0x60)
    int count1 = modelComponents._size;
    ser1.Serialize(count1, nullptr);

    // On load: resize array
    if (static_cast<int>(ser1.GetDirection()) == 0) {   // IO::Serializer::GetDirection, retail 0x180006210
        // Collection::Array<T>::SetSize — retail 0x1400032e0, stride 8 align 8, new tail zeroed.
        // Element is the 8-byte InternalBasePtr slot serialized below (byte-copy, zeroing).
        modelComponents.SetSize(count1);
    }

    // Serialize each element, nulled unless IsDerivedFrom ModelComponent (ti 0x1401c7a50)
    Object::InternalPtr<ModelComponent>* arr1 = modelComponents._data;
    if (count1 > 0) {
        for (int i = 0; i < count1; i++) {
            arr1[i].Serialize(ser1, ModelComponent::TypeId());
        }
    }

    // the picking list's count (+0x70)
    int count2 = pickingComponents._size;
    ser1.Serialize(count2, nullptr);

    // On load: resize array2
    if (static_cast<int>(ser1.GetDirection()) == 0) {   // IO::Serializer::GetDirection, retail 0x180006210
        // Collection::Array<T>::SetSize — retail 0x1400032e0, stride 8 align 8, new tail zeroed.
        // Element is the 8-byte InternalBasePtr slot serialized below (byte-copy, zeroing).
        pickingComponents.SetSize(count2);
    }

    // Serialize each element, nulled unless IsDerivedFrom PickingComponent (ti 0x1401c7c10)
    Object::InternalPtr<PickingComponent>* arr2 = pickingComponents._data;
    if (count2 > 0) {
        for (int i = 0; i < count2; i++) {
            arr2[i].Serialize(ser1, PickingComponent::TypeId());
        }
    }
}

// [6] 0x140108d40 — the real OnAdded broadcast, a thunk over FillModelLists
// (0x140108720): it fills the mesh (+0x58/+0x60) and picking (+0x68/+0x70) lists
// from the site's own ModelComponent and from each MultiObjectComponent child,
// HIDES each collected model (retail's "this model is now build-controlled"
// transition — a placed foundation carries no building mesh until
// State8Enter/State8Update sets a construction frame; the spawner's instant-build
// bit makes the thunk return before the body for pre-built buildings), and writes
// +0x4c from MultiObjectComponent+0x58.
void BuildComponent::OnAdded() {
    if (GameUpdateLoop::_singleton != nullptr) {
        if (GameUpdateLoop::_singleton->m_gameFlags & 1) return;   // instant build -> OnAdded does nothing (TEST byte [RAX+0xd0],1)
    }
    FillModelLists();
}

// [7] 0x140108cb0 — the removed-from-world broadcast (NOT OnCreate: misnamed in
// Ghidra exactly like CitizenComponent's 0x140113ca0). It removes the site from the
// town's pending-build registry (townMgr+0xd0); the 0x83 capability flags have bit 1
// set, so the MarkForRemoval broadcast reaches it.
void BuildComponent::OnRemove() {
    // 0x140017550 is CitizenIbpList<T>::Find (Collection::Array) and 0x1401c3dd8 is
    // g_pGameWorldSingleton: called directly, no first-call cache (B.3.8, B6-5).
    if (!GameUpdateLoop::_singleton)
        return;

    // Create InternalBasePtr on stack — original passes this (component ptr)
    Object::InternalPtr<BuildComponent> ptrBuf(this);

    CitizenBehavior* worldData = GameUpdateLoop::_singleton->m_citizenList;

    // Find in removal queue (CitizenBehavior::buildComponents, +0xD0 / +0xD8)
    int idx = worldData->buildComponents.Find(ptrBuf);   // 0x140017550

    if (idx >= 0) {
        int count = worldData->buildComponents._size;

        // Shift remaining elements down (each is 8 bytes / InternalBasePtr)
        if (idx < count) {
            int64_t offset = (int64_t)idx;   // in elements
            do {
                int nextIdx = idx + 1;
                if (nextIdx < count) {
                    Object::InternalPtr<BuildComponent>* pSlot = worldData->buildComponents._data + offset;
                    if (pSlot != nullptr) {
                        *pSlot = nullptr;
                        *pSlot = pSlot[1];
                    }
                }
                idx = nextIdx;
                offset += 1;
            } while (idx < count);
        }

        // Decrement count
        worldData->buildComponents._size = count - 1;
    }
}

// [15] 0x140108710 — THE build-labor +0x48 progress credit. The build action chain
// (FUN_1400c0690 / BuildStructure_SetupBuildActions) pushes a SendComponentMessage
// targeting the BuildComponent (type-id c7008) with message-code 0. When the
// citizen's PerformWork(3s) completes, SendComponentMessage::AbstractImpl
// (sendcomponentmessage.cpp) calls this slot, which credits ONE unit of labour into
// +0x48; State8Update ramps the build meshes as +0x48 climbs toward target
// (matComp[0x30] * bc[0x4c]) and requests 8->2 completion once it is reached.
void BuildComponent::OnMessage(int message, void* /*data*/, uint32_t /*arg0*/, uint32_t /*arg1*/) {
    if (message == 0) laborProgress += 1;
}

DEFINE_EXE_TYPE(BuildComponent, "ControllerComponent")
