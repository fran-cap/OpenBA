// src/game/Component/residencecomponent.cpp
// Clean reimplementation of ResidenceComponent lifecycle methods from Application-x64.exe.
//
// ResidenceComponent is a ControllerComponent subclass (0x60 bytes).
// Manages citizen residence assignments (housing).
//
// Vtable at 0x1401642c0. Three non-trivial overrides:
//   Persist  (0x14012a170) — serialization with Time + arrays
//   Startup  (0x14012a120) — IsDerivedFrom check + field init from description
//   OnRemove (0x14012a0e0) — evict the residents + unregister from the town
//
// The family roster (moved here from citizen_assignworkplace.cpp / citizencomponent.cpp:
// retail's receiver is the ResidenceComponent — 0x14012a4f0 reads [RCX+0x18] as a
// ResidenceDescription (cell 0x1401c7d28) and [RCX+0x50] as the family array):
//   0x14012a4f0  IsFamilyFull
//   0x14012a550  RemoveResident  (find + compact + decrement; empty house -> icons / model / audio)
//   0x14012a710  AddResident     (work slot, audio, model, append + qsort)
//   0x140129ff0  CountResidents  (sum of every family's count)
//   0x140129e20  the qsort comparator (file-local)
//
// Decompiled from Application-x64.exe using Ghidra.

#include "Component/ResidenceComponent.h"
#include <cstdint>
#include "Component/CitizenComponent.h"
#include "Component/ResidenceDescription.h"
#include "Component/StatusIconComponent.h"
#include "Component/WorkComponent.h"
#include "Component/ModelComponent.h"
#include "Component/AmbientEmitterComponent.h"   // what Entity::FindAudioComponent (0x140002280) returns
#include "Entity/ComponentEntity.h"
#include "Entity/CitizenBehavior.h"   // CitizenBehavior::field_0x20 (the residence list)
#include "Component/firecomponent_funcs.h"
#include "object/TypeInfo.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Game/exe_constants.h"
#include "system/MemoryAllocator.h"
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch A
#include "math/Math.h"
#include "audio/Audio.h"       // ENDGAME B.3.7 callers (K4)
#include <cstdio>
#include "rt_libm.h"   // RtCrt_Qsort_18007f050: the MSVC CRT qsort (the exe's 0x14003ae10)
#include <new>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "system/Value.h"   // ENDGAME B.3.4b System caller batch
#include "Component/exe_bodies.h"
#include "Action/exe_bodies.h"

// The exe helpers this file calls, by retail address:
//   0x140037ad0 ResidenceComponent::SetFamilyCount (below)
//   0x140037c50 ResidenceComponent::SerializeFamilies (below)
//   0x140129ef0 ResidenceComponent::EvictResidents (below)
//   0x140002cc0 Array<T>::Remove (DynArray8_EraseByValue)
//   0x140007960 CopyFamily (below)
//   0x1401163d0 CitizenComponent::SetHome
//   0x140152a98 ?IsDerivedFrom@Base@Object@@ (import slot)

// ============================================================
// The body of [7] 0x14012a0e0 (and of the state-3 enter 0x140129920)
// ============================================================
// Evicts the residents (0x140129ef0), then removes the owning entity from
// the town manager's residence list at +0x20.
//
// Original decompilation:
//   ResidenceComponent_helper_9ef0(self)
//   local_res8 = *(self+0x10)    // owner entity
//   FUN_140002cc0(*(g_pGameWorldSingleton+0xf0)+0x20, &local_res8)

void ResidenceComponent::EvictAndUnregister() {
    EvictResidents();

    if (GameUpdateLoop::_singleton == nullptr)
        return;

    // The owner entity, as the InternalBasePtr value retail builds on the stack
    ComponentEntity* entityPtr = _entity.Get();

    CitizenBehavior* worldMgr = GameUpdateLoop::_singleton->m_citizenList;

    // Unregister the entity from the town's residence entity list (+0x20)
    worldMgr->field_0x20.Remove(entityPtr);   // 0x140002cc0
}

// Resident per-family member-array copy on grow.
// CALLER-BUG FIX: the literal was 0x140035ec0, which is MID-FUNCTION inside the
// resize helper FUN_140035d50 (its body is 0x140035d50-0x140035ec8). The actual
// per-element copy the resize loop CALLs is FUN_140007960 (verified: the only
// CALL 0x140007960 inside FUN_140035d50 is at 0x140035e17, the inner-move site).
// ============================================================
// Reimpl of FUN_140007960 (Array<InternalPtr<CitizenComponent>>::operator=, the
// family member array deep copy). Frees dst's old buffer, copies src's count,
// allocates count*8 (8-byte stride, align 8), and copies the pointers
// element-by-element. No-op when dst==src. Faithful 1:1 port of the Ghidra decompile.
// ============================================================
static void CopyFamily(ResidenceComponent::Family* dst, const ResidenceComponent::Family* src) {
    if (dst == src) return;

    System::MemoryAllocator::MemFree(dst->_data);
    dst->_size = 0;
    dst->_capacity = 0;
    dst->_data = nullptr;
    dst->_size = src->_size;
    int count = dst->_size;
    if (count > 0) {
        dst->_capacity = count;
        dst->_data = static_cast<Object::InternalPtr<CitizenComponent>*>(
            System::MemoryAllocator::MemAlloc((uint64_t)((int64_t)count << 3), 8));
    }
    int n = dst->_size;
    for (int i = 0; i < n; i++) {
        Object::InternalPtr<CitizenComponent>* slot = dst->_data + i;
        if (slot) {
            *slot = src->_data[i];
        }
    }
}

// ============================================================
// ResidenceComponent::SetFamilyCount (0x140037ad0, ResidenceComponent_Controller_sub_7ad0:
//   Array<Family>::SetSize on residentSlots)
// ============================================================
// On grow, new tail families are zeroed; existing families are copied with
// CopyFamily when the buffer was reallocated. On shrink the dropped families'
// member buffers are MemFree'd. When realloc happens, every old family is freed
// and the old buffer released.
void ResidenceComponent::SetFamilyCount(int newCount) {
    int oldCount = residentSlots._size;
    if (newCount == oldCount) return;

    Family* oldBuf = residentSlots._data;
    residentSlots._size = newCount;
    if (residentSlots._capacity < newCount) {
        residentSlots._capacity = newCount;
        if (newCount == oldCount + 1 && oldCount != 0) {
            residentSlots._capacity = newCount * 3;
        }
        residentSlots._data = static_cast<Family*>(
            System::MemoryAllocator::MemAlloc((uint64_t)((int64_t)residentSlots._capacity << 4), 8));
    }

    int start = (oldBuf == residentSlots._data) ? oldCount : 0;
    if (start < newCount) {
        Family* srcSlot = oldBuf + start;
        for (int i = start; i < newCount; i++) {
            Family* slot = residentSlots._data + i;
            if (i < oldCount) {
                if (slot) {
                    slot->_data = nullptr;
                    slot->_size = 0;
                    slot->_capacity = 0;
                    CopyFamily(slot, srcSlot);   // 0x140007960
                }
            } else {
                if (slot) { slot->_data = nullptr; slot->_size = 0; slot->_capacity = 0; }
            }
            srcSlot += 1;
        }
    }

    if (oldBuf == residentSlots._data) {
        // No realloc: free the member buffers of the shrunk-off tail
        if (newCount < oldCount) {
            Family* p = oldBuf + newCount;
            for (int i = 0; i < oldCount - newCount; i++) {
                System::MemoryAllocator::MemFree(p->_data);
                p->_data = nullptr; p->_size = 0; p->_capacity = 0;
                p += 1;
            }
        }
    } else {
        // Reallocated: free every old family's member buffer, then the old buffer
        Family* p = oldBuf;
        for (int i = 0; i < oldCount; i++) {
            System::MemoryAllocator::MemFree(p->_data);
            p->_data = nullptr; p->_size = 0; p->_capacity = 0;
            p += 1;
        }
        if (oldBuf) System::MemoryAllocator::MemFree(oldBuf);
    }
}

// ============================================================
// ResidenceComponent::SerializeFamilies (0x140037c50, ResidenceComponent_helper_7c50):
//   serialize the nested resident IBP arrays
// ============================================================
// For each of `count` families: serialize the member count; on load grow the
// member array; then serialize each member InternalBasePtr and clear it if not
// derived from the resident type.
void ResidenceComponent::SerializeFamilies(IO::Serializer& ser, int count, Family* families) {
    if (count <= 0) return;
    Family* entry = families;
    for (int o = 0; o < count; o++) {
        int innerCount = entry->_size;
        ser.Serialize(innerCount, nullptr);
        // On load (direction 0): resize the member array
        if (static_cast<int>(ser.GetDirection()) == 0) {   // IO::Serializer::GetDirection, retail 0x180006210
            entry->SetSize(innerCount);
        }
        Object::InternalPtr<CitizenComponent>* inner = entry->_data;
        for (int i = 0; i < innerCount; i++) {
            // InternalPtr<CitizenComponent>::Serialize: InternalBasePtr::Serialize, then a
            // non-null VALUE not derived from CitizenComponent is cleared. (The value, not
            // the pointer: with 0x1401c7158 registered the slot existed from startup but
            // only held the CRC after a post-Startup fill.)
            inner[i].Serialize(ser, CitizenComponent::TypeId());
        }
        entry += 1;   // 16-byte stride
    }
}

// ============================================================
// ResidenceComponent::EvictResidents (0x140129ef0, ResidenceComponent_helper_9ef0):
//   every resident of every family is sent home-less (SetHome(null, -1)), which
//   removes it from this roster — the loop re-reads the count until it is 0.
// ============================================================
void ResidenceComponent::EvictResidents() {
    // Mesh flags 6 and 7 — clear two MeshInstance status/skin flags on the house.
    // STANDALONE: skipped. In standalone the house's ModelComponent mesh was force-set
    // to the completed-build variant (starttown.cpp StartTown_SpawnStructures); this
    // SetMeshFlag op manipulates the same MeshInstance and, with our partial/inert mesh
    // state, hides the building (it assumes the real game's fully-live mesh pipeline).
    // The registration below (push into townMgr+0x20) is the part standalone needs.

    // Evict each family's residents (+0x50, count +0x58, stride 0x10)
    int slotCount = residentSlots._size;
    for (int s = 0; s < slotCount; s++) {
        int innerCount = residentSlots[s]._size;
        while (innerCount > 0) {
            CitizenComponent* citizen = residentSlots[s]._data[0].Get();
            citizen->SetHome(nullptr, -1);   // 0x1401163d0 with a null IBP
            innerCount = residentSlots[s]._size;
        }
    }
}

// ============================================================
// The family roster (moved from Component/citizencomponent.cpp and
// Component/citizen_assignworkplace.cpp; COMP-b left them free)
// ============================================================

// ResidenceComponent::IsFamilyFull (0x14012a4f0): the family's member count
// against ResidenceDescription::_maxResidentsPerFamily (+0x24), `SETGE`.
bool ResidenceComponent::IsFamilyFull(int family) const {
    Description* obj = _description;
    const ResidenceDescription* res =
        Object::IsDerivedFromNullSafe(obj, ResidenceDescription::TypeId())
            ? static_cast<const ResidenceDescription*>(obj) : nullptr;
    // A guard of the tree's: retail 0x14012a538 reads [0+0x24] when the
    // description is not a ResidenceDescription.
    int eduLevel = res ? res->_maxResidentsPerFamily : 0;
    int cap = residentSlots[family]._size;
    return eduLevel <= cap;
}

namespace {

// The live Audio::Scene lives at gameWorld+0x78 (disasm: MOV RAX,[0x1401c3dd8];
// MOV RDI,[RAX+0x78]). Null in standalone unless the audio scene was built.
inline Audio::Scene* AudioScene() {
    if (GameUpdateLoop::_singleton == nullptr) return nullptr;
    return GameUpdateLoop::_singleton->m_audioScene;
}

// ------------------------------------------------------------
// qsort comparator — faithful port of CitizenComponent_helper_9e20 (0x140129e20).
// Elements are raw 8-byte citizen-component pointers. Ranks by "has-guardian"
// (+0x258 == 0 -> +0x1000) and "is-child/education-state==1" (+0x38 == 1 -> +0x100),
// preferring guardianed adults toward the front. CheckPtr resolves each lazy ptr on a
// stack copy before the field reads (exactly as the original does).
// ------------------------------------------------------------
int RosterCompare_9e20(const void* pa, const void* pb) {   // LEFT: qsort's comparator signature (int (*)(const void*, const void*)); each points at a CitizenComponent* slot
    // qsort's element pointers: each element is a CitizenComponent pointer.
    const CitizenComponent* ea = *reinterpret_cast<CitizenComponent* const*>(pa);   // CAST-OK: crt
    const CitizenComponent* eb = *reinterpret_cast<CitizenComponent* const*>(pb);   // CAST-OK: crt

    const CitizenComponent* a258 = ea->_parent.Get();   // +0x258 parent
    int a38 = ea->gender;                         // +0x38 gender

    const CitizenComponent* b258 = eb->_parent.Get();
    int b38 = eb->gender;

    uint32_t ub = (b38 == 1) ? 0x100u : 0u;
    uint32_t ua = (a38 == 1) ? 0x100u : 0u;
    uint32_t lhs = ub | ((b258 == nullptr) ? 0x1000u : 0u);
    uint32_t rhs = ((a258 == nullptr) ? 0x1000u : 0u) | ua;
    return static_cast<int>(lhs - rhs);
}

}  // namespace

// ============================================================
// ResidenceComponent::CountResidents (0x140129ff0) — sum of the member counts
// across every family (+0x58 families, +0x50 the family array, count @ +8).
// The exe hand-unrolls the sum 4-wide; this is the identical scalar equivalent.
// ============================================================
uint32_t ResidenceComponent::CountResidents() const {
    int professions = residentSlots._size;
    uint32_t total = 0;
    for (int i = 0; i < professions; ++i)
        total += static_cast<uint32_t>(residentSlots[i]._size);
    return total;
}

// ============================================================
// ResidenceComponent::RemoveResident (0x14012a550) — REMOVE a citizen from a family.
//   family     = the household slot
//   citizenIBP = InternalBasePtr wrapping the citizen component to remove
// Roster bookkeeping (find + compact + decrement) is the faithful core and runs
// unconditionally. When the house goes fully empty, the exe tears down the status
// icon + 3D audio emitter — that whole branch is inert and guarded here.
// ============================================================
void ResidenceComponent::RemoveResident(int family, Object::InternalPtr<CitizenComponent>* citizenIBP) {

    // --- Find the citizen's index in this family's array. ---
    Family* rec = &residentSlots[family];
    int idx = -1;
    {
        int count = rec->_size;
        Object::InternalPtr<CitizenComponent>* arr = rec->_data;
        for (int i = 0; i < count; ++i) {
            if (arr[i] == *citizenIBP) {
                idx = i;
                break;
            }
        }
    }

    // --- Compact the array down over the removed slot, then decrement count. ---
    // (Faithful to 0x14012a5b6..603, which reloads the table pointer here.)
    rec = &residentSlots[family];
    int cnt = rec->_size;
    // idx >= 0 is guaranteed by the caller invariant (the citizen being removed was
    // assigned to this home/slot, hence present in its roster). The original does
    // an out-of-bounds array[-1] shift on the assert-only "can't find item in priority
    // list" path; guarding the shift keeps that impossible path a crash-safe no-op
    // while remaining byte-identical for every real removal.
    if (idx >= 0) {
        Object::InternalPtr<CitizenComponent>* base = rec->_data;
        for (int i = idx; i + 1 < cnt; ++i) {
            Object::InternalPtr<CitizenComponent>* slot = base + i;
            if (slot)
                *slot = base[i + 1];
        }
    }
    rec->_size = cnt - 1;   // DEC dword[rec+8] — unconditional, as in the original.

    // --- Inert: house became empty -> clear status icon + tear down audio emitter. ---
    if (CountResidents() == 0) {
        // Status-icon toggles (flags 6 and 7 off). The icon may be absent.
        for (int flag = 6; flag <= 7; ++flag) {
            StatusIconComponent* icon = _entity->FindComponent<StatusIconComponent>();   // 0x140002f90
            if (icon)
                icon->SetFlag(static_cast<uint8_t>(flag), 0);   // 0x14012c3f0
        }
        // Model refresh (variant 3 = idle/empty).
        _entity->SendEvent(3);
        // 3D positional audio teardown: the house's crowd loop (slot [1] of the
        // residence templates' `ambientemitter`: WoodHouse / StoneHouse / Hostel list
        // FireEffect, CrowdEffect) goes quiet when the last resident leaves.
        {
            // 0x14012a6ba: FindComponent<AmbientEmitterComponent> (0x140002280), its
            // result read UNGUARDED (every residence template carries the component).
            AmbientEmitterComponent* em = _entity->FindComponent<AmbientEmitterComponent>();
            Audio::Scene* scene = AudioScene();
            if (scene) {
                // Retail 0x14012a550 reads slot [1] ([slots+0x10]) of the emitter array.
                AmbientEmitterSlot* slot = &em->slots._data[1];
                uint32_t id = slot->handle;
                scene->RemoveEmitter(id, em->position);
                int remaining = scene->GetEmitterCount(id);
                if (remaining == 0) scene->StopEffect3D(id);
                slot->handle = 0;
            }
        }
    }
}

// ============================================================
// ResidenceComponent::AddResident (0x14012a710) — ADD a citizen to a family.
//   family / citizenIBP as above.
// Material reservation (guarded) + inert audio reposition (guarded) run first; the
// FAITHFUL CORE is: model refresh when the family was empty (guarded), then
// grow-array append of the citizen ptr, then qsort the family when it now holds >1.
// ============================================================
void ResidenceComponent::AddResident(int family, Object::InternalPtr<CitizenComponent>* citizenIBP) {
    // --- Material reserve (guarded; the WorkComponent may be absent). ---
    WorkComponent* work = _entity->FindComponent<WorkComponent>();   // 0x140003050
    if (work && work->IsWorkPosted()) {
        work->CancelWork();
    }

    // --- Inert: house was empty -> move emitter to model position + set volume. ---
    if (CountResidents() == 0) {
        // 0x14012a764: FindComponent<AmbientEmitterComponent> (0x140002280). The first
        // resident moves the house's emitters to the model's position and starts slot
        // [1], the crowd loop (Audio/Effects/CrowdEffect.rsc: the second entry of the
        // residence templates' `ambientemitter`, `_addOnCreate = false`).
        AmbientEmitterComponent* em = _entity->FindComponent<AmbientEmitterComponent>();
        // Model/transform position (guarded; component may be absent).
        Vector3 pos(0.0f, 0.0f, 0.0f);
        {
            ModelComponent* model = _entity->FindComponent<ModelComponent>();   // 0x140003f60
            if (model) {
                pos.x = model->transform.translation.x;
                pos.y = model->transform.translation.y;
                pos.z = model->transform.translation.z;
            }
        }
        Audio::Scene* scene = AudioScene();
        {
            // 0x14012a7a4: `pos == em->position` through the import slot 0x140152a70
            // (Math::Vector3::operator==, Runtime 0x1800150a0, inline in Math.h).
            // Unequal: every playing slot's emitter moves, then the position is cached.
            if (!(pos == em->position)) {
                if (scene) {
                    int nEm = em->slots._size;
                    AmbientEmitterSlot* emArr = em->slots._data;
                    for (int i = 0; i < nEm; ++i) {
                        uint32_t id = emArr[i].handle;
                        if (id != 0)
                            scene->MoveEmitter(id, em->position, pos);
                    }
                }
                em->position.x = pos.x;
                em->position.y = pos.y;
                em->position.z = pos.z;
            }
            em->PlaySlot(1);   // 0x14012a898-0x14012a8a5: RCX = em, EDX = 1, CALL 0x140042c60
            // Retail 0x14012a710 reads slot [1] ([slots+0x10] / [slots+0x14]).
            AmbientEmitterSlot* slot = &em->slots._data[1];
            if (static_cast<uint32_t>(slot->handle) == 0) {
                slot->volume = 1.0f;   // retail stores 0x3f800000
            } else {
                float uv = kRdata_14017214c;   // g_fUVScale 0x14017214c
                if (slot->volume != uv) {
                    slot->volume = 1.0f;   // retail stores 0x3f800000
                    if (scene)
                        scene->SetEmitterVolume(slot->handle, em->position, uv);
                }
            }
        }
    }

    // --- FAITHFUL CORE: model refresh on the family's first member, then append. ---
    Family* rec = &residentSlots[family];
    if (rec->_size == 0) {
        _entity->SendEvent(2);   // variant 2 = occupied
    }
    // Grow the family array to count+1 and append the citizen pointer (*citizenIBP).
    rec->SetSize(rec->_size + 1, *citizenIBP);   // 0x140003110
    // Re-read count (3110 updated it) and sort the family when it holds >1 member.
    int newCount = rec->_size;
    if (newCount > 1)
        RtCrt_Qsort_18007f050(rec->_data, static_cast<size_t>(newCount), 8, &RosterCompare_9e20);   // CALL 0x14003ae10
}

// ===== Standalone factory (Foundation D → Keystone D states) =====
// Inert-but-valid ResidenceComponent: size 0x60 + Object::Internal base + capability
// flags +0x20=0x82 (from ResidenceComponent_Ctor) + zeroed body.
//
// LIVE bring-up (BANISHED_LIVE_RESIDENCE, default ON via the default-on table):
//   * vtable index 4 (+0x20, 0x14012a120, Serialize/Startup) — resizes the resident
//     array at +0x50 from the description capacity (desc+0x20). Dispatched at spawn
//     by the ComponentEntity StartupComponents pass (FUN_1400c30a0), gated on entity
//     flag 0x80 + comp flag bit 7 (0x82 has it).
//   * vtable index 7 (+0x38, 0x14012a0e0, Ghidra "OnCreate") — helper_9ef0 (re-assign
//     resident workplaces / evict) + FUN_140002cc0 which REMOVES the owner entity from
//     the townMgr+0x20 residence list (same body as the state-3 demolish enter
//     helper_9920). The ADD to that list is the state-2 enter below.
//   * vtable index 16/17 (GetDefaultState=2 / CreateState=&node@+0x28) + the REAL
//     controller state table — the retail mechanism (ResidenceDescription_Setup
//     0x14014f8b0 → helper_a350 0x14012a350) that makes StateMachineComponent link
//     and tick the residence node. This is what drives firewood heating: the state-2
//     per-frame handler is the heat tick 0x140129970.
//
// Residence state graph (helper_a350, decompile-verified):
//   state 2 (built):    enter = helper_9de0 (append house to townMgr+0x20);
//                       trans38 (per-frame) = 0x140129970 heat tick; transitions -> {3}
//   state 3 (demolish): enter = helper_9920 (evict + remove from townMgr+0x20);
//                       transitions -> {6, 4, 5} (targets only, no handlers)
//
// Start-town houses are stamped SM pendingState=2 (starttown.cpp), so the first
// StateMachine::Tick runs the state-2 Enter (list registration) and then the heat
// tick every frame — the retail flow; the old StartTown_RegisterTownResidences
// manual append is removed to avoid double-registration.
#include "Object/factory_registration.h"
#include "Component/controller_state_table.h"
#include <atomic>
#include <cstring>

// State-2 enter -- retail 0x140129de0, whole (row 1260: the body was split out as the
// free TownRegistry_RegisterResidence with three tests and an outcome log retail lacks).
// The owner entity's IBP VALUE (+0x10) is copied raw into a local (0x140129de4 /
// 0x140129ded) and appended to the town manager's residence list ([[0x1401c3dd8]+0xf0]
// +0x20, count +0x28) by SetSizeFill(count + 1, &local) (0x140129e09, 0x140003110),
// nothing tested. XOR AL,AL: returns 0, so the SM Enter walk continues past this node.
char ResidenceComponent::EnterBuilt() {
    ComponentEntity* entity = _entity.Get();
    CitizenIbpList<ComponentEntity>& residences =
        GameUpdateLoop::_singleton->m_citizenList->field_0x20;
    residences.SetSizeFill(residences._size + 1, &entity);
    return 0;
}

// State-3 enter — faithful helper_9920 (0x140129920): evict via helper_9ef0 + remove
// from townMgr+0x20 via FUN_140002cc0. That is exactly the slot-7 body.
char ResidenceComponent::EnterDemolish() {
    // Discriminator (2026-07-31): this body ERASES the house from townMgr+0x20, and
    // something is running it during StartTown right after each state-2 append, which is
    // what keeps the residence count pinned at 0. Two call sites can reach it — this
    // state-3 enter and the slot-7 removal hook — so each says which one fired.
    static int n = 0;
    if (n < 12) { ++n;
        fprintf(stderr, "[Residence] ERASE via STATE-3 DEMOLISH enter: comp=%p\n", static_cast<void*>(this));
        fflush(stderr);
    }
    EvictAndUnregister();
    return 0;
}

// The table's handler thunks: the dispatcher calls (component, sm[, time]).
static char ResidenceEnterBuilt(ResidenceComponent* c, StateMachine* /*sm*/)    { return c->EnterBuilt(); }      // 0x140129de0
static char ResidenceEnterDemolish(ResidenceComponent* c, StateMachine* /*sm*/) { return c->EnterDemolish(); }   // 0x140129920
static char ResidenceHeatTick(ResidenceComponent* c, StateMachine* /*sm*/, const System::Time* dt) {
    if (!c) return 1;   // a guard of the tree's
    return c->HeatTick(dt);                                                                             // 0x140129970
}

using ResidenceCtrlDispatch = ::ControllerLive::Dispatcher<ResidenceCtrlTag>;

// Built ONCE at static-init time, as retail's static initializer does (B38 D6/D7).
static void ResidenceComponent_PopulateStateTable() {
    using namespace ::ControllerLive;
    auto& t = ResidenceCtrlDispatch::table;
    if (t.count != 0) return;  // idempotent
    TableRegisterState(t, 2, &ResidenceEnterBuilt,
                       nullptr, &ResidenceHeatTick,
                       nullptr);
    TableAddTransition(t, 3, nullptr);
    TableRegisterState(t, 3, &ResidenceEnterDemolish,
                       nullptr, nullptr, nullptr);
    TableAddTransition(t, 6, nullptr);
    TableAddTransition(t, 4, nullptr);
    TableAddTransition(t, 5, nullptr);
}
static const bool s_residenceStateTableBuilt = (ResidenceComponent_PopulateStateTable(), true);

// ============================================================================
// ENDGAME B.3.8 MI-A — ResidenceComponent as a REAL class (ResidenceComponent.h).
//
// Before: a 48-slot hand-built `ResidenceComponent_LiveVtable` filled at every
// create (FillTypedVtable + FinishLiveVtable), `Inert_ScalarDtor` in slot 0 (only
// `~Internal`), the ControllerLive::Dispatcher<ResidenceCtrlTag> table stamped into
// raw bytes at +0x28. After: the compiler's 18-slot vtable, a real constructor and
// destructor chain, the controller a member object (B38_RULES D1, D2, D6).
// ============================================================================

// 0x14012a2a0. Component() is the CALL 0x1401188b0; the stamps are the compiler's;
// `_controller(this)` is the member's back-pointer; +0x48 = +0x50 = +0x58 = 0; then
// `_flags |= 0x82`.
ResidenceComponent::ResidenceComponent()
    : _controller(this), heatRemaining{0} {
    OrFlagsInCtor(0x82);
}

// 0x14012a230 (inlined in [0] 0x140037610): the body is empty. CALL 0x1400175d0 on +0x50
// (0x14012a254) is ~residentSlots: every family's ~Array (MemFree, data = 0, the size +
// capacity qword = 0), then the slot buffer freed and +0x50 / +0x58 zeroed; then the
// controller's restamp (0x14012a26a, ~_controller) -- reverse declaration order -- and
// the bases' restamps and ~Internal (the compiler's chain).
ResidenceComponent::~ResidenceComponent() {
}

// create 0x140037440: MemAlloc(0x60, 8), then the ctor.
::Object::Base* ResidenceComponent::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(ResidenceComponent), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) ResidenceComponent());
}
// Retail's createInPlace is `Internal::CreateInPlaceObject` (returns null); the
// tree's in-place entry built the object, and that is kept (B38 B8-13).
::Object::Base* ResidenceComponent::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) ResidenceComponent());
}

// ============================================================
// ResidenceComponent::Persist (vtable slot [3], 0x14012a170)
// ============================================================
// Serializes Component base + empty serializer block + versioned
// serializer with: family count (+0x58), family array resize on load,
// family array serialize, and Time::Serialize at +0x48.
//
// Original decompilation:
//   Component_Serialize(self, stream)
//   Serializer_Ctor(local1, stream, 0); Serializer_Dtor(local1)    // empty block
//   Serializer_Ctor(local2, stream, 0)
//   local_res8 = *(int*)(self+0x58)
//   Serialize_Int(local2, &local_res8, null)
//   if loading: ResizeResidentArray(self+0x50, local_res8)
//   SerializeResidentArray(local2, local_res8, *(self+0x50))
//   Time::Serialize(self+0x48, local2)
//   Serializer_Dtor(local2)

void ResidenceComponent::Persist(IO::Stream& stream) {
    // Call base Component_Serialize
    Component::Persist(stream);   // retail: CALL 0x1401189b0 (qualified)

    // Empty serializer block (version 0)
    {
        IO::Serializer empty(stream, 0);
    }

    // Versioned serializer block (version 0) with resident data
    IO::Serializer ser(stream, 0);

    // Serialize the family count via a local copy
    // (same pattern as ConsumeProduceComponent — local copy needed so resize
    // sees old vs new count correctly)
    int localCount = residentSlots._size;
    ser.Serialize(localCount, nullptr);

    // On load (direction == 0): resize the family array
    if (static_cast<int>(ser.GetDirection()) == 0) {
        SetFamilyCount(localCount);   // 0x140037ad0
    }

    // Serialize the families
    SerializeFamilies(ser, localCount, residentSlots._data);

    // Serialize Time at +0x48
    heatRemaining.Serialize(ser);
}

// ============================================================
// ResidenceComponent::Startup (vtable slot [4], 0x14012a120)
// ============================================================
// Checks IsDerivedFrom on +0x18 with ResidenceDescription type,
// reads the family count from the description at +0x20, resizes
// the family array at +0x50 to it.
//
// Original decompilation:
//   obj = *(self+0x18)
//   derived = IsDerivedFrom(obj, DAT_1401c7d28)
//   pvVar2 = derived ? obj : nullptr
//   ResizeResidentArray(self+0x50, *(pvVar2+0x20))

void ResidenceComponent::Startup() {
    Description* obj = _description;
    if (!obj) return;

    bool derived = obj->IsDerivedFrom(ResidenceDescription::TypeId());   // import slot 0x140152a98
    ResidenceDescription* desc = derived ? static_cast<ResidenceDescription*>(obj) : nullptr;
    if (!desc) return;

    // Resize the family array at +0x50 to the description's family count (+0x20)
    SetFamilyCount(desc->_maxFamilies);   // 0x140037ad0
}

// [7] 0x14012a0e0 — helper_9ef0 (evict the residents) + remove the owner entity from
// the townMgr+0x20 residence list, dispatched by FUN_1400c2ea0 at entity removal.
// See the note on EnterDemolish — this is the other route into the townMgr+0x20
// erase, so it says which one fired.
static std::atomic<int> s_residenceSlot7Logged{0};
void ResidenceComponent::OnRemove() {
    if (s_residenceSlot7Logged.fetch_add(1, std::memory_order_relaxed) < 12) {
        fprintf(stderr, "[Residence] ERASE via SLOT-7 removal hook: comp=%p\n", static_cast<void*>(this));
        fflush(stderr);
    }
    EvictAndUnregister();
}

DEFINE_EXE_TYPE(ResidenceComponent, "ControllerComponent")
