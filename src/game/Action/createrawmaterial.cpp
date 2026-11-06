// src/game/Action/createrawmaterial.cpp
// Clean C++ reimplementation of CreateRawMaterial action methods.
// Source: Game/Action/CreateRawMaterial.cpp (from profile build debug strings)
//
// CreateRawMaterial -- 0x58 bytes. Spawns raw material entities from a resource building.
// Inherits: Object::Internal -> Action (0x20) -> CreateRawMaterial
// Vtable at 0x14015fd10 (10 slots).
//
// Fields from Serialize:
//   +0x20: description entity (ExternalPtr/type ref, serialized via FUN_140002340)
//   +0x28: InternalBasePtr (source building)
//   +0x30: InternalBasePtr (optional secondary entity)
//   +0x38: InternalBasePtr (optional tertiary entity)
//   +0x40: int (total quantity)
//   +0x44: int (material type index)
//   +0x48: uint (placement flags)
//   +0x4c: float (yield multiplier)
//   +0x50: bool (natural resource flag)
//   +0x51: bool (place near flag)
//
// Decompiled from Application-x64.exe using Ghidra.

#include "CreateRawMaterial.h"
#include "object/TypeInfo.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Action/exe_bodies.h"            // Pf_FindPlacementCell_bd40, ActRm_* helpers
#include "Game/exe_bodies.h"              // Reimpl_ExternalPtr_Serialize_StringTable
#include "Entity/ComponentEntity.h"   // ComponentEntity(ComponentDescription*) (0x1400c3720)
#include "Entity/EntityList.h"        // EntityList::RegisterEntity (0x1400c48a0)
#include "Entity/EntityList.h"
#include "Entity/CitizenBehavior.h"   // CitizenBehavior::CountProducedMaterial (0x140088ef0)
#include "Game/Map.h"
#include "system/MemoryAllocator.h"       // System::MemoryAllocator::MemAlloc
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch C
#include "Action/saved_bounds.h"
#include "Component/AutoPickupComponent.h"
#include "Component/CitizenDescription.h"
#include "Component/ComponentDescription.h"
#include "Component/Description.h"
#include "Component/WorkComponent.h"
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <new>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "system/Value.h"      // ENDGAME B.3.4b System callers
#include "Component/CitizenComponent.h"
#include "Entity/ComponentEntity.h"
#include "Component/RawMaterialDescription.h"
#include "Component/RawMaterialComponent.h"
#include "Component/WorkPlaceDescription.h"
#include "ext/map_overlays/map_overlays.h"   // EXT SEAM (NOT RETAIL, docs/extensions/handins/MAPOVERLAYS.md): ext.map_overlays

// ============================================================
// Helpers, called by name (B3R)
//   0x140002340 Reimpl_ExternalPtr_Serialize_StringTable -- serialize StringHash/type ref
//   0x1400c3720 ComponentEntity_CtorFromDescription      -- clone entity from description
//   0x14009bd40 Pf_FindPlacementCell_bd40 -- find placement cell
//               (mapGrid*, cellIndex, naturalResourceFlag, outEntityPtr*, placementFlags)
//   0x140129760 ActRm_PositionEntity_9760 -- position entity at cell with rotation
//   0x140088ef0 ActRm_RegisterRawMaterial_88ef0 -- register raw material with resource tracker
//               (entityList, sourceIBP, placeNearFlag, descEntity, quantity)
//   0x1400c48a0 EntityList_RegisterEntity_Impl -- register entity with entity list
//   0x140022520 WorkAcc_FindComponent_22520 -- get child entities from source building
//               (member ABI: RCX=entity, RDX=retbuf)
//   0x140003050 WorkBase_FindComponent_3050 -- find component on entity (ResourceDescription?)
// ============================================================

/// Constant: rotation scale factor (90.0f) at 0x140172204
static const float ROTATION_SCALE = 90.0f;

// ============================================================
// The RawMaterialDescription among a template's sub-descriptions (retail inlines the
// walk: slot 2 Type(), then IsDerivedFrom).
// ============================================================
static RawMaterialDescription* FindRawMaterialDesc(const ComponentDescription* descEntity) {
    int count = descEntity->_subDescCount;
    Description** subDescs = descEntity->_subDescs;

    for (int i = 0; i < count; i++) {
        Description* comp = subDescs[i];
        uint32_t typeId = comp->Type();   // slot 2
        if (typeId == RawMaterialDescription::TypeId()) {
            // Verify with IsDerivedFrom
            if (::Object::IsDerivedFromNullSafe(comp, RawMaterialDescription::TypeId())) {
                return static_cast<RawMaterialDescription*>(comp);
            }
        }
    }
    return nullptr;
}

// ============================================================
// Serialize (0x1400c6f30)
// Versioned serialization with version 3 serializer.
// Version gates at 0, 1, 2, and 3 control which fields are serialized.
// ============================================================
void CreateRawMaterial::Persist(IO::Stream& stream) {
    Action::Persist(stream);   // inlined in retail: Serializer(stream, 0), status +0x18

    // Serialize resource data (version 3)
    IO::Serializer ser(stream, 3);

    // Type reference (+0x20)
    ::Object::SerializeExternal<ComponentDescription>(ser, descEntity);   // 0x140002340

    // The working citizen (+0x28)
    sourceBuilding.InternalBasePtr::Serialize(ser);
    if (!sourceBuilding.IsNull()) {
        if (!::Object::IsDerivedFromNullSafe(sourceBuilding, CitizenComponent::TypeId())) {
            sourceBuilding = nullptr;
        }
    }

    ser.Serialize(materialCellIdx, nullptr);   // +0x44
    ser.Serialize(yieldMultiplier, nullptr);   // +0x4C
    ser.Serialize(naturalResFlag, nullptr);    // +0x50
    ser.Serialize(totalQuantity, nullptr);     // +0x40
    ser.Serialize(placeNearFlag, nullptr);     // +0x51

    // Version > 0: secondary entity IBP (+0x30)
    int version = ser.GetVersion();
    if (version > 0) {
        secondaryEntity.InternalBasePtr::Serialize(ser);
        if (!secondaryEntity.IsNull()) {
            if (!::Object::IsDerivedFromNullSafe(secondaryEntity, ComponentEntity::TypeId())) {
                secondaryEntity = nullptr;
            }
        }
    }

    // Version < 2: default placement flags; >= 2: serialize uint (+0x48)
    version = ser.GetVersion();
    if (version < 2) {
        placementFlags = 4;
    } else {
        ser.Serialize(placementFlags, nullptr);
    }

    // Version > 2: tertiary entity IBP (+0x38)
    version = ser.GetVersion();
    if (version > 2) {
        tertiaryEntity.InternalBasePtr::Serialize(ser);
        if (!tertiaryEntity.IsNull()) {
            if (!::Object::IsDerivedFromNullSafe(tertiaryEntity, ComponentEntity::TypeId())) {
                tertiaryEntity = nullptr;
            }
        }
    }
}


int  CreateRawMaterial::AbstractImpl(ActionStack* actionStack) {
    (void)actionStack;
    GameUpdateLoop* gameWorld = GameUpdateLoop::_singleton;

    // The material template at +0x20
    ComponentDescription* descEntity = this->descEntity;

    // Find RawMaterialDescription among the template's sub-descriptions
    RawMaterialDescription* rawMatDesc = FindRawMaterialDesc(descEntity);

    // The working citizen's education picks the yield
    CitizenComponent* citizen = sourceBuilding.Get();

    int yield;
    if (citizen->education < citizen->description->maxEducation) {
        yield = rawMatDesc->_lowCreateCount;    // +0x40
    } else {
        yield = rawMatDesc->_highCreateCount;   // +0x44
    }

    // Apply yield multiplier
    float yieldMul = yieldMultiplier;
    int totalItems = (int)((float)yield * yieldMul);

    if (totalItems == 0) {
        return 0;
    }

    // Max items per PILE = the carrier's volume capacity / this material's per-unit volume.
    // Retail 0x1400c6c22-2c, verified at instruction level:
    //     MOV EAX,[R15+0x40]     ; R15 = this, +0x40 = ctor arg7
    //     CDQ
    //     IDIV dword ptr [RBX+0x48]  ; RBX = rawMatDesc, +0x48 = per-unit VOLUME
    //     MOV ECX,EAX            ; ECX = cap, loop-invariant
    //     MOV [RBP+0x50],EAX     ; stashed; reloaded into ECX at the loop tail (6c6ef3)
    // then inside the loop at 6c6c40-46: MOV EAX,R13D / CMP R13D,ECX / CMOVG EAX,ECX
    // = min(totalItems, cap). The division is done ONCE, before the loop — matched here.
    //
    // This port previously used rawMatDesc+0x48 (the per-unit volume) DIRECTLY as the cap,
    // i.e. it dropped the division entirely, so every pile produced or culled has been the
    // wrong size since the function was written.
    //
    // action+0x40 is CreateRawMaterial's ctor arg7, and all four retail construction sites
    // pass StorageComponent+0x38 — the carrier's max volume capacity (0x1400bed3f loads it
    // as MOV R12D,[RAX+0x38] and 0x1400bed9f stores it to RSP+0x30 = arg7; our ported site
    // in action_pf_roots_drop.cpp does the same). So this is "how many whole units fit in
    // the hauler", which is why it caps the per-entity batch.
    int unitVolume = rawMatDesc->_weight;   // +0x48
    int carrierVolume = totalQuantity;      // +0x40
    // B6 (14d G66): retail divides with no test (0x1400c6c22 MOV EAX,[R15+0x40]; CDQ;
    // IDIV dword ptr [RBX+0x48]). The old "STANDALONE-ONLY TRIPWIRE" covered a then-unported
    // construction path; all four retail sites (0x1400bb7b0, 0x1400bbea0, 0x1400beae0,
    // 0x1400bef70, the xrefs of 0x1400c71a0) are ported and pass StorageComponent+0x38, and
    // Persist restores +0x40. A zero cap would loop in retail as here: that is the finding.
    int maxPerEntity = carrierVolume / unitVolume;

    // NOT RETAIL (review F-106): materialCellIdx comes from the save (Persist +0x44) and the
    // loop below reads Map::cells at it and drops the pile there; retail has no bound. An
    // index off the map fails the action (2) before the first read.
    if (!ActionBounds::IsMapCell(materialCellIdx, gameWorld->m_game->cells.GetSize())) {
        return 2;
    }

    const bool mapOverlays = Ext::MapOverlays::g_on;   // ext.map_overlays: tested once, above the loop

    // Spawn loop: create entities for all items
    while (totalItems > 0) {
        int batchSize = totalItems;
        if (maxPerEntity < totalItems) {
            batchSize = maxPerEntity;
        }

        // Check if natural resource flag needs to be set
        if (!naturalResFlag) {
            int32_t matIdx = materialCellIdx;
            if (gameWorld->m_game->cells.GetData()[matIdx].occupant != nullptr) {
                naturalResFlag = true;
            }
        }

        // Find placement cell
        int32_t matIdx = materialCellIdx;
        int cellIndex;
        if (naturalResFlag) {
            // Natural resource placement: use helper
            ComponentEntity* thirdEntity = tertiaryEntity.Get();
            cellIndex = gameWorld->m_game->FindPlacementCell(matIdx, '\x01', &thirdEntity, placementFlags);   // 0x14009bd40
        } else {
            cellIndex = matIdx;
        }

        if (cellIndex < 0) {
            // No valid cell found, still consume items
            batchSize = batchSize;  // keep batchSize as is
        } else {
            // Allocate new ComponentEntity from description
            ComponentEntity* newEntityMem = static_cast<ComponentEntity*>(System::MemoryAllocator::MemAlloc(0x48, 8));
            ComponentEntity* newEntity = nullptr;
            if (newEntityMem != nullptr) {
                newEntity = ::new (newEntityMem) ComponentEntity(descEntity);   // 0x1400c3720
            }

            // The new entity's reference (an InternalBasePtr in retail)
            ComponentEntity* entityIBP = newEntity;

            // The RawMaterialComponent on the new entity (an inlined FindComponent)
            RawMaterialComponent* comp = entityIBP->FindComponent<RawMaterialComponent>();

            // Position entity at cell with random rotation
            int randVal = gameWorld->m_random->RandTol(2);
            comp->PositionRawMaterial(cellIndex, (float)randVal * ROTATION_SCALE);   // 0x140129760

            // Handle secondary entity (+0x30) if present
            if (secondaryEntity.Get() != nullptr) {
                const ComponentDescription* secDesc = secondaryEntity->_description;

                // Search secondary entity's description for its WorkPlaceDescription
                int secCount = secDesc->_subDescCount;
                for (int i = 0; i < secCount; i++) {
                    Description* secComp = secDesc->_subDescs[i];
                    uint32_t secTypeId = secComp->Type();   // slot 2
                    if (secTypeId == WorkPlaceDescription::TypeId()) {
                        ::Object::IsDerivedFromNullSafe(secComp, WorkPlaceDescription::TypeId());
                        break;
                    }
                }
            }

            // Set quantity on new entity component
            comp->quantity = batchSize;
            comp->profession = nullptr;

            // Register raw material with the town
            ComponentEntity* sourceIBP = secondaryEntity.Get();
            gameWorld->m_citizenList->CountProducedMaterial(   // 0x140088ef0
                &sourceIBP,
                placeNearFlag, descEntity, batchSize);
            if (mapOverlays) {
                // ext.map_overlays: produced by work, at the workplace (the pile when there is none)
                Ext::MapOverlays::OnProducedAt(descEntity, sourceIBP != nullptr ? sourceIBP : entityIBP, batchSize);
            }

            // Register entity with entity list
            Object::InternalPtr<ComponentEntity> entityRef = entityIBP;
            int typeGroup = static_cast<int>(descEntity->_category);
            gameWorld->m_entityList->RegisterEntity(&entityRef, typeGroup);   // 0x1400c48a0

            // Handle place-near behavior: queue the pile's work id on the secondary's auto pickup
            if (placeNearFlag && secondaryEntity.Get() != nullptr) {
                AutoPickupComponent* autoPickup = secondaryEntity->FindComponent<AutoPickupComponent>();   // 0x140022520
                if (autoPickup != nullptr) {
                    WorkComponent* work = entityIBP->FindComponent<WorkComponent>();   // 0x140003050
                    autoPickup->Append(static_cast<uint32_t>(work->workId));   // 0x140044810
                }
            }
        }

        totalItems -= batchSize;
    }

    return 0;
}

// ============================================================
// GetTarget (0x1400c6aa0)
// Returns true if param matches the citizen's entity (+0x28 -> +0x10), or +0x30, or +0x38.
// ============================================================
bool CreateRawMaterial::GetTarget(ComponentEntity** param) {
    void* target = *param;

    // The citizen's owning entity (Component +0x10)
    if (target == sourceBuilding->_entity.Get()) {
        return 1;
    }

    // Check +0x30 (secondary entity)
    if (secondaryEntity.Get() == target) {
        return 1;
    }

    // Check +0x38 (tertiary entity)
    if (tertiaryEntity.Get() == target) {
        return 1;
    }

    return 0;
}

// ============================================================
// class CreateRawMaterial (CreateRawMaterial.h) — ENDGAME B.3.8 ACT.
// The hand-built g_CreateRawMaterial_Vtable[10], its per-class type-id cache and the
// Action_Alloc / Action_InitInPlace factories are gone: the compiler emits
// the table, DECLARE_EXE_TYPE answers slots 1/2.
// ============================================================
#include "Object/factory_registration.h"

// create 0x1400222d0 (inlined ctor). The tree's factory zeroed every field above
// +0x10; kept (not a behaviour change of this step).
CreateRawMaterial::CreateRawMaterial()
    : descEntity(nullptr),
      sourceBuilding(nullptr),
      secondaryEntity(nullptr),
      tertiaryEntity(nullptr),
      totalQuantity(0),
      materialCellIdx(0),
      placementFlags(0),
      yieldMultiplier(0.0f),
      naturalResFlag(false),
      placeNearFlag(false),
      _pad52{} {}

// [3] 0x1400c6f30
// [4] 0x1400c6b40
// [9] 0x1400c6aa0

::Object::Base* CreateRawMaterial::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(CreateRawMaterial), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) CreateRawMaterial());
}
::Object::Base* CreateRawMaterial::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) CreateRawMaterial());
}


// ============================================================
// FUN_1400c71a0 — CreateRawMaterial_Action CopyCtor
// Base Internal ctor, Action -> CreateRawMaterial vtable; copies +0x20=param_2 (raw u64),
// three IBP fields (+0x28 from param_4, +0x30 from param_5, +0x38 from param_10), three
// int fields (+0x40=param_7, +0x44=param_8, +0x48=param_11), one FLOAT (+0x4c=param_3, the
// XMM2 arg stored via MOVSS), and two bytes (+0x50=param_9, +0x51=1).
// ============================================================
// 0x1400c71a0: +0x20 = param_2 (raw) ; +0x28/+0x30/+0x38 = *param_4 / *param_5 /
// *param_10 ; +0x40/+0x44/+0x48 = param_7/8/11 ; +0x4c = param_3 (MOVSS, the XMM2
// float) ; +0x50 = param_9 ; +0x51 = 1. param_6 is not read.
CreateRawMaterial::CreateRawMaterial(ComponentDescription* desc, float yield, CitizenComponent* const* source,
                                     ComponentEntity* const* secondary, uint64_t /*unused6*/,
                                     uint32_t quantity, uint32_t cellIdx, bool naturalRes,
                                     ComponentEntity* const* tertiary, uint32_t placement)
    : descEntity(desc),
      sourceBuilding(*source),
      secondaryEntity(*secondary),
      tertiaryEntity(*tertiary),
      totalQuantity(static_cast<int32_t>(quantity)),
      materialCellIdx(static_cast<int32_t>(cellIdx)),
      placementFlags(placement),
      yieldMultiplier(yield),
      naturalResFlag(naturalRes),
      placeNearFlag(true) {}

DEFINE_EXE_TYPE(CreateRawMaterial, "Action")
