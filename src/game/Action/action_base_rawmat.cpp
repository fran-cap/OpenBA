// src/game/Action/action_base_rawmat.cpp
// Wave 3 (Action) — raw-material cluster (batch E) closure-safe leaf sub-helpers.
//
// Shared exe sub-helpers called by name (Action/exe_bodies.h, B3R) from
// src/game/Action/{consumerawmaterial,pickuprawmaterial,pickupstoredrawmaterial,
// createrawmaterial}.cpp. This file provides faithful 1:1 ports of the six that are *leaf*
// (pure array ops, MemAlloc/MemFree, or engine_funcs export-resolved pointers) — i.e.
// whose entire sub-callee closure is already satisfiable. The registration point is
// RegisterActionRawMatBatch() in standalone/reg_action_rawmat.cpp.
#include "Action/exe_bodies.h"
//
// TAIL LANDED (this file): 0x140129760 PositionEntity and 0x140088ef0 RegisterRawMaterial
// — the prior "deferred" note for both was STALE. PositionEntity's getters (0x140004680,
// 0x140003f60 = Entity_ClearPathQueue) were already registered (component-nature / CNR
// waves), so its closure was already satisfied; it just needed the port. RegisterRawMaterial's
// deps are now provided here: 0x140013320 (FindComp), 0x140058fd0 (ArrayUpsert), 0x14000d9d0
// (VectorClone), plus the already-registered 0x140088f90 (GetResourceCategory).
//
// DEFERRED (still NOT registered):
//   0x14009bd40 CreateRawMaterial FindPlacementCell — 260-instruction expanding-ring spiral
//     with heavy decompiler variable-aliasing, on an UNEXERCISED citizen-action path (a wrong
//     port would be undetectable latent corruption). Only dep 0x14009f3e0 is registered, so it
//     is unblockable-but-deferred to its own careful pass with in-game validation.
//
// Each function is a 1:1 port of the decompiled exe helper (Ghidra). Do NOT "improve".

// The runtime type headers come FIRST: they pull src/engine/volume/Volume.h,
// whose global-scope `struct Rectangle` is hidden by <wingdi.h>'s `Rectangle()`
// once <windows.h> has been seen through resolve.h.  ENDGAME B.3.5 batch I2.
#include "graphics/Geometry.h"
#include <new>
#include "object/TypeInfo.h"
#include "graphics/Camera.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Component/exe_bodies.h"     // FloorComponent_FindResData
#include "Game/exe_bodies.h"          // Reimpl_MapGetTerrainPos
#include "Game/exe_constants.h"       // kRdata_140172114
#include "system/MemoryAllocator.h"   // System::MemoryAllocator::MemAlloc / MemFree
#include "math/Math.h"
#include "Object/factory_registration.h"   // ::Object::GetTypeIdByName (standalone type-id fallback)
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "Component/RawMaterialComponent.h"   // ENDGAME B.3.8 CB3: RawMaterialComponent::TypeId()
#include "Component/RawMaterialDescription.h"
#include "Component/TrackerComponent.h"
#include "Entity/ComponentEntity.h"   // ComponentEntity::FindComponent<T>
#include "Component/AutoPickupComponent.h"
#include "Component/ComponentDescription.h"
#include "Component/MapComponent.h"
#include "Component/ModelComponent.h"
#include "Component/StorageComponent.h"
#include "Entity/CitizenBehavior.h"
#include "Game/Calendar.h"
#include "Game/Map.h"
#include "Action/PathSearch.h"   // RawArray

// Exe sub-helpers / engine constants used by the PositionEntity port below, called by
// name (B3R):
//   0x140004680 FloorComponent_FindResData / 0x140003f60 ActCNR_FindComponent_3f60 --
//               member-ABI FindComponent (self, outIBP) -> outIBP.
//   0x14009f3e0 Reimpl_MapGetTerrainPos -- GridToWorld (mapGrid, outVec3, inVec2, char flag,
//               void* ptr).
//   DAT_140172114 = 0.5f cell-centre offset (kRdata_140172114).
// (The slope threshold 0x14017213c and height-diff tolerance 0x140172248 this file also
// resolved had no reader here -- FindPlacementCell lives in action_pf_roots_misc.cpp.)

// ============================================================
// FUN_14012d150 — StorageComponent::SubtractItem(material, count)
// ============================================================
// Walk the items to the material, subtract count; if the item's count reaches 0, compact
// the array down one and decrement the count. Pure array ops — leaf.
void StorageComponent::SubtractItem(const ComponentDescription* material, int count) {
    int n = storageSlots._size;
    if (n <= 0) return;
    const StorageComponent::Item* p = storageSlots._data;
    int idx = 0;
    while (p->_resource != material) {
        idx++;
        p += 1;
        if (storageSlots._size <= idx) return;
    }
    StorageComponent::Item* items = storageSlots._data;
    items[idx]._count -= count;
    if (items[idx]._count == 0) {
        int i = idx;
        int at = idx;
        if (i < storageSlots._size) {
            do {
                i++;
                if (i < storageSlots._size) {
                    storageSlots[at] = storageSlots[at + 1];   // [idx] = [idx+1]
                }
                at += 1;
            } while (i < storageSlots._size);
        }
        storageSlots._size -= 1;
    }
}

// ============================================================
// FUN_14012d000 — StorageComponent::AddItem(material, count)
// ============================================================
// If the material is already present, add to its count. Else append a {material, count, 0}
// item, growing the array (grows to 3x on a non-first append) via MemAlloc + copy +
// MemFree. Leaf (alloc only).
void StorageComponent::AddItem(ComponentDescription* material, int count) {
    // DIAGNOSTIC, not a guard — appending a count<=0 entry is legal here in retail too, but
    // it creates a stack that SpillAllStorage (action_pf_roots_drop.cpp) can never drain:
    // that loop's exit test is the stack COUNT while its work is gated on the entry's own
    // count, so a {material, 0} entry spins the game forever (observed 2026-07-31 after the
    // first pasture cull). Retail shipped assertions on exactly this invariant — the profile
    // build carries "_count >= 0 && _reserve >= 0 && _count - _reserve >= 0" and
    // "_items[index]._count >= 0" on StorageComponent::Draw — so naming the caller at the
    // moment of the write is retail-shaped. Do NOT turn this into `if (count <= 0) return;`.
    if (count <= 0) {
        std::fprintf(stderr, "[StorageAdd] ZERO/NEG COUNT: storage=%p resType=%p count=%d "
                     "stacks(+0x30)=%d capacity(+0x38)=%d — this entry will hang the spill loop\n",
                     static_cast<const void*>(this), static_cast<const void*>(material), count,
                     storageSlots._size, capacity);
        std::fflush(stderr);
    }
    int idx = 0;
    int n = storageSlots._size;
    if (n > 0) {
        const StorageComponent::Item* p = storageSlots._data;
        do {
            if (p->_resource == material) {
                storageSlots[idx]._count = storageSlots[idx]._count + count;
                return;
            }
            idx++;
            p += 1;
        } while (idx < storageSlots._size);
    }

    int oldN = storageSlots._size;
    int newN = oldN + 1;
    if (newN == oldN) return;  // never, but mirror the guard

    StorageComponent::Item* oldBuf = storageSlots._data;
    storageSlots._size = newN;
    if (storageSlots._capacity < newN) {
        storageSlots._capacity = newN;
        if (oldN != 0) storageSlots._capacity = newN * 3;
        storageSlots._data = static_cast<StorageComponent::Item*>(System::MemoryAllocator::MemAlloc(
                          static_cast<uint64_t>(storageSlots._capacity) << 4, 8));
    }
    int srcIdx = (oldBuf == storageSlots._data) ? oldN : 0;
    for (int i = srcIdx; i < newN; i++) {
        StorageComponent::Item* dst = storageSlots._data + i;
        if (!dst) continue;
        if (i < oldN) {
            *dst = oldBuf[i];
        } else {
            dst->_resource = material;
            dst->_count = count;   // {count, 0}
            dst->_reserve = 0;
        }
    }
    if (oldBuf != storageSlots._data)
        System::MemoryAllocator::MemFree(oldBuf);
}

// ============================================================
// FUN_14000da70 — Array<{material, count}>::SetSize (a 16-byte element).
// ============================================================
// Resize to newCount: grow buffer (3x on a +1 growth from non-empty) via MemAlloc, copy
// existing entries, zero new entries (the original zeroes only bytes [+0..+12) of each new
// 16-byte slot), MemFree old. Leaf (alloc only).
// The one body (Action/exe_bodies.h: MaterialArraySetSize<Holder> is the typed wrapper).
void ActRm_ArrayResize16_da70(RawArray<MaterialCount16>* arr, int newCount) {
    int oldCount = arr->size;
    if (newCount == oldCount) return;

    MaterialCount16* oldBuf = arr->data;
    arr->size = newCount;
    if (arr->capacity < newCount) {
        arr->capacity = newCount;
        if (newCount == oldCount + 1 && oldCount != 0) arr->capacity = newCount * 3;
        arr->data = static_cast<MaterialCount16*>(System::MemoryAllocator::MemAlloc(static_cast<uint64_t>(arr->capacity) << 4, 8));
    }
    int srcIdx = (oldBuf == arr->data) ? oldCount : 0;
    for (int i = srcIdx; i < newCount; i++) {
        MaterialCount16* dst = arr->data + i;
        if (!dst) continue;
        if (i < oldCount) {
            *dst = oldBuf[i];
        } else {
            dst->material = nullptr;
            dst->count = 0;
        }
    }
    if (oldBuf != arr->data)
        System::MemoryAllocator::MemFree(oldBuf);
}

// ============================================================
// FUN_140044810 — AutoPickupComponent: append a value to the +0x30 int array.
// ============================================================
// Gated on isEnabled. Append value: grow buffer (3x on a +1 growth from non-empty) via
// MemAlloc, copy existing ints, write value at the new slot, MemFree old. Leaf.
void AutoPickupComponent::Append(uint32_t value) {
    if (!isEnabled) return;

    int oldN = pileWorkIds._size;
    int newN = oldN + 1;
    if (newN == oldN) return;

    int32_t* oldBuf = pileWorkIds._data;
    pileWorkIds._size = newN;
    if (pileWorkIds._capacity < newN) {
        pileWorkIds._capacity = newN;
        if (oldN != 0) pileWorkIds._capacity = newN * 3;
        pileWorkIds._data = static_cast<int32_t*>(System::MemoryAllocator::MemAlloc(static_cast<uint64_t>(pileWorkIds._capacity) << 2, 4));
    }
    int srcIdx = (oldBuf == pileWorkIds._data) ? oldN : 0;
    for (int i = srcIdx; i < newN; i++) {
        int32_t* dst = pileWorkIds._data + i;
        if (!dst) continue;
        *dst = (i < oldN) ? oldBuf[i] : static_cast<int32_t>(value);
    }
    if (oldBuf != pileWorkIds._data)
        System::MemoryAllocator::MemFree(oldBuf);
}

// ============================================================
// FUN_140088f90 — resource category index from a material's RawMaterialDescription flags.
// ============================================================
// Find the RawMaterialDescription among the material template's sub-descriptions (Type()
// then IsDerivedFrom), then map the first set bit of its _flags to a category index
// (faithful bit->index table). Returns -1 if none. Leaf.
int ResourceCategory(const ComponentDescription* material) {
    uint32_t typeId = RawMaterialDescription::TypeId();
    const RawMaterialDescription* comp = nullptr;
    int n = material->_subDescCount;
    Description** base = material->_subDescs;
    for (int idx = 0; idx < n; idx++) {
        Description* c = base[idx];
        uint32_t t = c->Type();   // slot 2
        if (t == typeId) {
            if (c->IsDerivedFrom(typeId)) comp = static_cast<const RawMaterialDescription*>(c);
            break;
        }
    }
    // B6 (14d G59): 0x140089001 CMOVNZ RDI,RBX over RDI = 0; 0x140089005 MOV ECX,[RDI+0x5c]
    // with no test (a miss falls through to the same read).

    uint32_t f = comp->_flags;
    if ((f >> 10) & 1) return 0;
    if ((f >> 11) & 1) return 1;
    if ((f >> 12) & 1) return 2;
    if ((f >> 7)  & 1) return 3;
    if ((f >> 8)  & 1) return 9;
    if ((f >> 9)  & 1) return 4;
    if ((f >> 5)  & 1) return 5;
    if ((f >> 0xd) & 1) return 6;
    if ((f >> 0xe) & 1) return 7;
    if ((f >> 0x11) & 1) return 8;
    if ((f >> 0x10) & 1) return 10;
    if ((f >> 0x12) & 1) return 0xb;
    if ((f >> 0x13) & 1) return 0xc;
    if ((f >> 0x14) & 1) return 0xd;
    if ((f >> 0x15) & 1) return 0xe;
    if ((f >> 0x16) & 1) return 0xf;
    if ((f >> 0x17) & 1) return 0x10;
    if ((f >> 0x18) & 1) return 0x11;
    if ((f >> 0x19) & 1) return 0x12;
    if ((f >> 0x1a) & 1) return 0x13;
    if ((f >> 0x1b) & 1) return 0x14;
    return -1;
}

// ============================================================
// FUN_14000d9d0 — Array<{material, count}> copy-assign (dst <- src).
// ============================================================
// Frees dst's buffer, resets count/cap to 0, sets count=cap=src.count, allocs a fresh
// src.count*0x10 buffer, copies each 16-byte element from src. No-op if dst==src.
// The one body (Action/exe_bodies.h: MaterialArrayAssign<Holder> is the typed wrapper).
void ActRm_VectorClone_d9d0(RawArray<MaterialCount16>* dst, const RawArray<MaterialCount16>* src) {
    if (dst == src) return;
    System::MemoryAllocator::MemFree(dst->data);
    dst->size = 0;          // count(+8) and cap(+0xc): one 8-byte write in retail (H7)
    dst->capacity = 0;
    dst->data = nullptr;
    int srcN = src->size;
    dst->size = srcN;
    if (srcN > 0) {
        dst->capacity = srcN;
        dst->data = static_cast<MaterialCount16*>(System::MemoryAllocator::MemAlloc(static_cast<uint64_t>(srcN) << 4, 8));
    }
    int n = dst->size;
    for (int i = 0; i < n; i++) {
        MaterialCount16* d = dst->data + i;
        if (!d) continue;
        *d = src->data[i];
    }
}

// ============================================================
// FUN_140058fd0 (CreateRawMaterial_helper_8fd0) — TrackerComponent: year-aware {material,
// count} upsert.
// ============================================================
// If the calendar year changed since trackerId, copy the +0x28 list into +0x38 (d9d0) and
// reset the +0x28 list. Then upsert {material, count} into +0x28: if present add, else
// append (grow 3x on a non-first append). Leaf.
void TrackerComponent::Upsert(ComponentDescription* material, int value) {
    TrackerComponent::EntryList* live = &displayed;
    TrackerComponent::EntryList* archive = &produced;
    int gen = GameUpdateLoop::_singleton->m_dayCounter->year;
    if (gen != trackerId) {
        MaterialArrayAssign(archive, live);   // 0x14000d9d0
        System::MemoryAllocator::MemFree(live->_data);
        live->_data = nullptr;
        // Retail stores EIGHT bytes here -- `*(undefined8 *)(param_1 + 0x30) = 0;` --
        // clearing the count AND the capacity (an earlier 4-byte port left a stale capacity:
        // the next upsert then committed a count over a NULL array -> ACCESS_VIOLATION).
        live->_size = 0;
        live->_capacity = 0;
        trackerId = gen;
    }

    int n = live->_size;
    if (n > 0) {
        const TrackerComponent::Entry* p = live->_data;
        int idx = 0;
        do {
            if (p->_resource == material) {
                live->_data[idx]._count = live->_data[idx]._count + value;
                return;
            }
            idx++;
            p += 1;
        } while (idx < live->_size);
    }

    int oldN = live->_size;
    int newN = oldN + 1;
    TrackerComponent::Entry* oldBuf = live->_data;
    live->_size = newN;
    if (live->_capacity < newN) {
        live->_capacity = newN;
        if (oldN != 0) live->_capacity = newN * 3;
        live->_data = static_cast<TrackerComponent::Entry*>(System::MemoryAllocator::MemAlloc(
                          static_cast<uint64_t>(live->_capacity) << 4, 8));
    }
    int srcIdx = (oldBuf == live->_data) ? oldN : 0;
    for (int i = srcIdx; i < newN; i++) {
        TrackerComponent::Entry* d = live->_data + i;
        if (!d) continue;
        if (i < oldN) {
            *d = oldBuf[i];
        } else {
            d->_resource = material;
            d->_count = value;
            d->_pad0C[0] = 0; d->_pad0C[1] = 0; d->_pad0C[2] = 0; d->_pad0C[3] = 0;   // the qword store's high dword
        }
    }
    if (oldBuf != live->_data)
        System::MemoryAllocator::MemFree(oldBuf);
}

// ============================================================
// FUN_140088ef0 — register produced raw material: the source entity's TrackerComponent (when
// placeNear) gets the upsert, and the town's per-category resource count grows.
// ============================================================
void CitizenBehavior::CountProducedMaterial(ComponentEntity* const* source,
                         char placeNear, ComponentDescription* material, int quantity) {
    // Row 1140: the slot itself is read unguarded (0x140088f04 CMP qword ptr [RDX],0x0);
    // only *source (0x140088f12) and placeNear (0x140088f14) are tested.
    if (*source != nullptr && placeNear != '\0') {
        TrackerComponent* tracker = (*source)->FindComponent<TrackerComponent>();   // 0x140013320
        if (tracker != nullptr) {
            tracker->Upsert(material, quantity);   // 0x140058fd0
        }
    }
    int cat = ResourceCategory(material);   // 0x140088f90
    if (cat >= 0) {
        professionResources._data[cat].producedCount += quantity;
    }
}

// ============================================================
// FUN_140129760 (CreateRawMaterial_helper_9760) — place a raw-material entity.
// ============================================================
// Place the component's entity at cell `cellIndex` with `rotation`: its MapComponent gets
// the tile (col/row), a 1x1 footprint and the rotation normalized to [0,360); its
// ModelComponent gets the Transform built from (yaw=rotation, pitch=0, offset=worldPos),
// the raw rotation as yaw, then its MeshInstance's SetTransform. worldPos comes from
// GridToWorld (f3e0). Instruction-verified against the disassembly.
void Component::PositionRawMaterial(int cellIndex, float rotation) {
    ComponentEntity* entity = _entity.Get();
    Map* map = GameUpdateLoop::_singleton->m_game;
    int mapWidth = map->size;
    int col = cellIndex % mapWidth;   // EDI (remainder)
    int row = cellIndex / mapWidth;   // ESI (quotient)

    MapComponent* objA = entity->FindComponent<MapComponent>();       // 0x140004680
    ModelComponent* objB = entity->FindComponent<ModelComponent>();   // 0x140003f60

    int rot = static_cast<int>(rotation);   // CVTTSS2SI = truncate toward zero
    if (rot < 0) { do { rot += 0x168; } while (rot < 0); }
    // B6 (14d G60): retail writes the MapComponent's tile, footprint and rotation and the
    // ModelComponent's transform with no test of either finder result (0x140129760: CheckPtr,
    // then the +0x28 / +0x2a / +0x2c / +0x30 stores, and +0x148..+0x168 / SetTransform).
    objA->tileX0 = static_cast<int16_t>(col);
    objA->tileY0 = static_cast<int16_t>(row);
    objA->tileX1 = 1;                         // one dword store of 0x10001 in retail (H7)
    objA->tileY1 = 1;
    objA->extraField = rot;

    // GridToWorld: localPos = ((cellIndex%w) - half, (cellIndex/w) - half, 0).
    float half = static_cast<float>(mapWidth >> 1) - kRdata_140172114;   // 0x140172114
    float localPos[3];
    localPos[0] = static_cast<float>(cellIndex % mapWidth) - half;
    localPos[1] = static_cast<float>(cellIndex / mapWidth) - half;
    localPos[2] = 0.0f;
    float worldPos[3] = {0, 0, 0};
    map->GetTerrainPos(worldPos, localPos, '\0', nullptr);   // 0x14009f3e0

    {
        float yaw = rotation, pitch = 0.0f;
        // FromYawPitchOffset out (7 dwords copied to +0x148..+0x160)
        Math::Transform xform = Math::Transform::FromYawPitchOffset(
            yaw, pitch, Math::Vector3(worldPos[0], worldPos[1], worldPos[2]));
        objB->transform = xform;
        objB->yaw = rotation;
        objB->meshInstance.SetTransform(objB->transform);
    }
}
