// src/game/Action/action_base_cnr.cpp
// CreateNaturalResource / AnimateNaturalResource / RemoveNaturalResource
// shared exe sub-helpers (Wave 3, Action).
//
// The per-class Action .cpp files (createnaturalresource.cpp, animatenaturalresource.cpp,
// removenaturalresource.cpp) call these by name through Action/exe_bodies.h (B3R); the
// registration (RegisterActionCnrBatch in standalone/reg_action_cnr.cpp) stays for
// readers other blocks still have.
//
// Each function is a literal port of the decompiled exe helper — no behavior is
// invented or "improved". Every exe sub-call is a call of its body by name.
#include "Action/exe_bodies.h"
//

// The runtime type headers come FIRST: they pull src/engine/volume/Volume.h,
// whose global-scope `struct Rectangle` is hidden by <wingdi.h>'s `Rectangle()`
// once <windows.h> has been seen through resolve.h.  ENDGAME B.3.5 batch I2.
#include "graphics/Geometry.h"
#include "graphics/Mesh.h"      // ENDGAME B.3.5 batch K1: reaches Volume.h, so BEFORE resolve.h
#include <new>
#include "object/External.h"
#include "object/TypeInfo.h"
#include "graphics/Camera.h"
#include "Entity/exe_bodies.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Work/exe_bodies.h"              // WorkBase_FindComponent_3050, WorkAcc_FindComponent_*
#include "Component/exe_bodies.h"         // FloorComponent_FindResData, ModelComponentNotify_Reimpl
#include "Game/WorkScheduler.h"           // WorkScheduler::FindWorkAtCell (0x1400f8d50)
#include "Work/Work.h"
#include "Game/exe_bodies.h"              // Reimpl_MapGetTerrainPos
#include "Entity/ComponentEntity.h"   // ComponentEntity(ComponentDescription*) (0x1400c3720)
#include "Entity/EntityList.h"        // EntityList::RegisterEntity (0x1400c48a0)
#include "Game/exe_constants.h"           // kRdata_*
#include "system/MemoryAllocator.h"       // System::MemoryAllocator::MemAlloc
#include <cstring>
#include "math/Math.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "ext/registry.h"              // Ext::Fix().rock_seat
#include "ext/rock_seat/rock_seat.h"   // fix.rock_seat (docs/extensions/handins/ROCKSEAT.md)
#include "system/Value.h"      // ENDGAME B.3.4b System callers
#include "Component/ModelComponent.h"
#include "Component/GrowthComponent.h"
#include "Entity/ComponentEntity.h"   // ComponentEntity::FindComponent<T>
#include "Component/ModelDescription.h"
#include "Component/NaturalResourceDescription.h"
#include "Component/AutoPickupComponent.h"
#include "Component/ComponentDescription.h"
#include "Component/GrowthDescription.h"
#include "Component/MapComponent.h"
#include "Component/NaturalResourceComponent.h"
#include "Component/RawMaterialComponent.h"
#include "Component/RawMaterialDescription.h"
#include "Component/WorkComponent.h"
#include "Component/WorkPlaceDescription.h"
#include "Entity/CitizenBehavior.h"
#include "Entity/EntityList.h"
#include "Game/Map.h"
#include "ext/map_overlays/map_overlays.h"   // EXT SEAM (NOT RETAIL, docs/extensions/handins/MAPOVERLAYS.md): ext.map_overlays

// ============================================================
// Exe sub-helpers and constants (shared by this batch), called by name (B3R).
//   0x140003f60 ActCNR_FindComponent_3f60 (this file)   0x140003050 WorkBase_FindComponent_3050
//   0x140004680 FloorComponent_FindResData               0x1400c2b70 ModelComponentNotify_Reimpl
//   0x1400c3470 ActHunt_NotifyEntityChanged_c3470        0x140006330 WorkAcc_FindComponent_6330
// 0x1401260f0 closure sub-helpers:
//   0x1400c2ea0 ActHunt_MarkEntityRemoval_c2ea0          0x140003110 ActHunt_DynArraySetCount_3110
//   0x1400f8d50 RemovalCalc_helper_8d50 (RemovalTool_helper_8d50)
//   0x1400c3720 ComponentEntity_CtorFromDescription (ComponentEntity_CopyCtor)
//   0x1400223c0 ActRm_FindComponent_223c0                0x140129760 ActRm_PositionEntity_9760
//   0x140022480 FindDescription<WorkPlaceDescription> (template, result discarded)
//   0x140088ef0 ActRm_RegisterRawMaterial_88ef0          0x1400c48a0 EntityList_RegisterEntity_Impl
//   0x140022520 WorkAcc_FindComponent_22520              0x140044810 ActRm_DynArrayPush4_44810
// 0x140126540 closure: 0x14009f3e0 Reimpl_MapGetTerrainPos (CharacterComponent_impl_f3e0)
// 0x140126440 closure: 0x1401250f0 ActMvDep_AnimHelper_50f0 (AnimalComponent_helper_50f0)
// Import slots: 0x140152958 Math::Vector4::Vector4(const Scalar&), 0x140152b00
// System::Time::Time(float) = RtSystem_Time_Ctor.
// Float constants (Game/exe_constants.h): 0x1401720f4 (divisor in FUN_140120db0, a bit
// pattern), 0x140172234 (time scale in FUN_140120db0), 0x140172114 (half-bias in
// Entity_impl_6540), 0x140172220 (yaw scale in Entity_impl_6540 / 6440), 0x140172204
// (amount scale in FUN_1401260f0), 0x14017214c (g_fUVScale).
// ============================================================
namespace {

inline float RdataF(uint32_t bits) { float f; std::memcpy(&f, &bits, sizeof f); return f; }

}  // namespace

// (0x140003f60 ActCNR_FindComponent_3f60, the B4 FORWARD forwarder to
// ComponentEntity::FindComponent<ModelComponent>, had no caller left: deleted, B4 wave X.)


// ============================================================
// Reimpl of CreateNaturalResource_helper_cb00 (FUN_14009cb00) — cell terrain
// availability check. Returns 1 (true) in the low byte if the cell is unavailable
// (terrain bit 0/1, field_0x2A set, or pending in the field_0xE8 tree), else 0.
// ============================================================
uint64_t Map::CheckCellTerrain(int cellIndex) const {
    const MapCell& rec = cells.GetData()[cellIndex];
    uint32_t passBits = 1u << (rec.terrainType & 0x1f);
    uint64_t high = passBits;   // retail's RAX before the tree walk
    if (((passBits & 2) == 0) && ((passBits & 1) == 0) && (rec.field_0x2A == 0xff)) {
        const AvlNode28* node = field_0xE8.root;
        while (true) {
            while (true) {
                if (node == nullptr) return 0;
                if (node->key <= cellIndex) break;
                node = node->left;    // +0x10
            }
            if (cellIndex <= node->key) break;
            node = node->right;       // +0x18
        }
        // LEFT: retail returns the node pointer's bits with the low byte replaced by 1
        // (callers read only the low byte as the bool).
        high = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(node));   // CAST-OK: ptr_int
    }
    // CONCAT71(high>>8, 1)
    return (high & ~0xffull) | 1u;
}

// ============================================================
// Reimpl of FUN_140120db0 — GrowthComponent: set the growth progress.
// ============================================================
void GrowthComponent::SetProgress(float progress) {
    float growth = progress * growthScale;
    float uvScale = kRdata_14017214c;
    resource.Get()->resourceScale = growth;
    float clamped = (uvScale < growth) ? uvScale : growth;
    displayScale = clamped;

    ModelComponent* target = model.Get();
    if (!config->_b64) {
        float g = (growth <= uvScale) ? growth : uvScale;
        float scalar = g;
        {
            Math::Vector4 vec4((Math::Scalar(scalar)));   // slot 0x140152958
            target->meshInstance._color[0] = vec4.x;
            target->meshInstance._color[1] = vec4.y;
            target->meshInstance._color[2] = vec4.z;
            target->meshInstance._color[3] = vec4.w;
        }
    } else {
        float divisor = RdataF(kRdata_1401720f4);   // 0x1401720f4
        float scaled = clamped / divisor;
        float lo = 0.0f;
        if ((0.0f <= scaled) && (lo = scaled, uvScale < scaled)) lo = uvScale;
        float hi = growth;
        if (uvScale < growth) hi = uvScale;
        target->meshInstance._color[0] = lo;
        target->meshInstance._color[1] = lo;
        target->meshInstance._color[2] = hi;
        target->meshInstance._color[3] = uvScale;
    }

    const GrowthDescription* desc = config;
    float rf = GameUpdateLoop::_singleton->m_random->RandFloat();
    float timeScale = kRdata_140172234;   // 0x140172234
    System::Time timeObj = {};
    RtSystem_Time_Ctor(&timeObj, rf * static_cast<float>(desc->_i50) * timeScale);   // slot 0x140152b00

    uint32_t flagsOld = _flags;
    spawnTimer = timeObj;
    uint32_t flagsNew = flagsOld & 0xffffffef;
    _flags = flagsNew;
    if ((((flagsNew ^ flagsOld) & 0xffff) != 0) && (_entity != nullptr)) {
        _entity.Get()->RefreshEntityFlags();   // 0x1400c3470
    }
}

// ============================================================
// Reimpl of CreateNaturalResource_helper_52f0 (FUN_1401252f0) — the model's mesh-group
// count: the ModelDescription (type-gated) _meshGroupCount.
// ============================================================
uint32_t ModelComponent::MeshGroupCount() const {
    Description* desc = _description;
    const ModelDescription* d = (::Object::IsDerivedFromNullSafe(desc, ModelDescription::TypeId()))
                                    ? static_cast<const ModelDescription*>(desc) : nullptr;
    return static_cast<uint32_t>(d->_meshGroupCount);
}

// ============================================================
// Reimpl of FUN_140125320 — the mesh count of mesh group `group`.
// ============================================================
uint32_t ModelComponent::MeshCount(int group) const {
    Description* desc = _description;
    const ModelDescription* d = (::Object::IsDerivedFromNullSafe(desc, ModelDescription::TypeId()))
                                    ? static_cast<const ModelDescription*>(desc) : nullptr;
    return static_cast<uint32_t>(d->_meshGroups[group].meshCount);
}

// ============================================================
// Reimpl of FUN_1401253d0 — ModelComponent: select the mesh (group, index).
// Writes meshGroup and meshIndex, and on transitions (de)activates the model: slot 7
// (hide) when an old index was active; sets the mesh + slot 6 (show) when the new index is
// valid; finally notifies the entity (0x1400c2b70, 0).
// ============================================================
void ModelComponent::SelectMesh(uint16_t variant, int16_t subVariant) {
    // type-gate the description; the result is unused here.
    ::Object::IsDerivedFromNullSafe(_description, ModelDescription::TypeId());

    meshGroup = static_cast<int16_t>(variant);
    if (meshIndex >= 0) {
        OnRemove();   // slot 7
    }
    meshIndex = subVariant;
    if (subVariant >= 0) {
        Description* descMesh = _description;
        const ModelDescription* d = (::Object::IsDerivedFromNullSafe(descMesh, ModelDescription::TypeId()))
                                        ? static_cast<const ModelDescription*>(descMesh) : nullptr;
        int16_t sub = meshIndex;
        int16_t var = meshGroup;
        Graphics::GraphicsMesh* mesh = d->_meshGroups[var].meshes[sub];
        meshInstance.SetMesh(Object::ExternalPtr<Graphics::GraphicsMesh>(mesh));
        OnAdded();   // slot 6
    }
    _entity->SendEvent(0);   // 0x1400c2b70
}

// ============================================================
// Reimpl of AnimateNaturalResource_helper_6440 (FUN_140126440) — NaturalResourceComponent:
// set the animation index.
// If the description (type-gated 0x1401c7b68) _b64 == 0: if the dead bit (_flags bit 21) is
// set, run RemoveNatRes (FUN_1401260f0) with a null IBP. Else: play the animation on the
// entity's ModelComponent, set _flags bit 2, and on a low-16-bit flag change with a
// non-null entity, notify the entity.
// ============================================================
void NaturalResourceComponent::SetAnimation(uint32_t animIdx) {
    Description* desc = _description;
    const NaturalResourceDescription* d = (::Object::IsDerivedFromNullSafe(desc, NaturalResourceDescription::TypeId()))
                                              ? static_cast<const NaturalResourceDescription*>(desc) : nullptr;

    if (!d->_b64) {
        if (((_flags >> 0x15) & 1) != 0) {
            ComponentEntity* nullIBP = nullptr;
            RemoveResource(&nullIBP, 0);
        }
    } else {
        ModelComponent* model = _entity->FindComponent<ModelComponent>();   // 0x140003f60
        float uvScale = kRdata_14017214c;
        model->PlayAnimation(animIdx, 0.0f, 0.0f, uvScale);   // 0x1401250f0
        uint32_t flagsOld = _flags;
        uint32_t flagsNew = flagsOld | 4;
        _flags = flagsNew;
        if ((((flagsNew ^ flagsOld) & 0xffff) != 0) && (_entity != nullptr)) {
            _entity.Get()->RefreshEntityFlags();   // 0x1400c3470
        }
    }
}

// ============================================================
// Reimpl of Entity_impl_6540 (FUN_140126540) — CreateNaturalResource model placement.
// Positions the entity's MapComponent and ModelComponent at the cell, with jitter.
// ============================================================
uint64_t NaturalResourceComponent::PlaceAtCell(int cellIndex) {
    GameUpdateLoop* gameWorld = GameUpdateLoop::_singleton;
    Description* desc = _description;
    Map* map = gameWorld->m_game;
    System::Random* rng = gameWorld->m_random;
    int gridW = map->size;

    const NaturalResourceDescription* d = (::Object::IsDerivedFromNullSafe(desc, NaturalResourceDescription::TypeId()))
                                              ? static_cast<const NaturalResourceDescription*>(desc) : nullptr;
    float jitter = d->_f60;

    float halfBias = kRdata_140172114;   // 0x140172114
    float half = static_cast<float>(map->size >> 1) - halfBias;
    float cellX = static_cast<float>(cellIndex % map->size) - half;
    float cellY = static_cast<float>(cellIndex / map->size) - half;

    float inCell[2] = { cellX, cellY };
    float outPos[3] = { 0.0f, 0.0f, 0.0f };
    map->GetTerrainPos(outPos, inCell, '\0', nullptr);   // 0x14009f3e0

    float r1 = rng->RandFloatTol();
    float r2 = rng->RandFloatTol();
    float worldX = r1 * jitter + outPos[1];
    float worldY = r2 * jitter + outPos[0];
    float worldZ = outPos[2] + 0.0f;
    float yawScale = kRdata_140172220;   // 0x140172220
    float yaw = rng->RandFloatTol() * yawScale;

    MapComponent* pos = _entity->FindComponent<MapComponent>();       // 0x140004680
    ModelComponent* model = _entity->FindComponent<ModelComponent>(); // 0x140003f60

    pos->tileX0 = static_cast<int16_t>(cellIndex % gridW);
    pos->tileY0 = static_cast<int16_t>(cellIndex / gridW);
    // One qword store of 0x10001 in retail (H7): width 1, height 1, extraField 0.
    pos->tileX1 = 1;
    pos->tileY1 = 1;
    pos->extraField = 0;

    float pitch = 0.0f;
    Math::Vector3 offset(worldY, worldX, worldZ);  // FromYawPitchOffset offset = {local_98, local_94, local_90}
    Math::Transform xform = Math::Transform::FromYawPitchOffset(yaw, pitch, offset);
    model->transform = xform;
    model->yaw = yaw;
    // FIX rock_seat (NOT RETAIL, docs/extensions/handins/ROCKSEAT.md): a rock's mesh is drawn seated on
    // the slope; model->transform (serialized) stays retail's.
    if (Ext::Fix().rock_seat) {
        Ext::RockSeat::SetMeshTransform(model);
    } else {
        model->meshInstance.SetTransform(model->transform);
    }
    return 1;
}

// ============================================================
// Reimpl of FUN_1401260f0 — RemoveNaturalResource core removal/harvest.
// `citizenIBP` = the harvesting citizen's entity IBP, `harvestFlag`.
// Marks the entity for removal, then (if harvestable) spawns a raw-material entity at the
// cell, registers the harvest amount, and claims the cell for the pile.
// ============================================================
void NaturalResourceComponent::RemoveResource(ComponentEntity* const* citizenIBP, char harvestFlag) {
    GameUpdateLoop* gameWorld = GameUpdateLoop::_singleton;
    ComponentEntity* parent = _entity.Get();
    EntityList* entityList = gameWorld->m_entityList;

    // Mark the entity for removal unless already marked (bit 10 of its flags).
    if (parent != nullptr) {
        if (((parent->_entityFlags >> 10) & 1) == 0) {
            parent->MarkForRemoval();   // 0x1400c2ea0
            entityList->removalList.SetSize(entityList->removalList._size + 1, parent);   // 0x140003110
        }
    }

    if (!harvestable) {   // not harvestable
        return;
    }

    Description* desc = _description;
    const NaturalResourceDescription* d = (::Object::IsDerivedFromNullSafe(desc, NaturalResourceDescription::TypeId()))
                                              ? static_cast<const NaturalResourceDescription*>(desc) : nullptr;
    ComponentDescription* material = d->_building;
    if (material == nullptr) {
        return;
    }

    float uvScale = kRdata_14017214c;
    float frac = uvScale;
    if (resourceScale <= uvScale) frac = resourceScale;

    int amount;
    RawMaterialDescription* raw = nullptr;
    raw = material->FindDescription<RawMaterialDescription>();   // 0x140006330
    if (!mature) {
        amount = raw->_lowCreateCount;
    } else {
        amount = raw->_highCreateCount;
    }
    amount = static_cast<int>(static_cast<float>(amount) * frac);
    if (amount <= 0) {
        return;
    }

    // The cell index from the entity's MapComponent tile.
    const MapComponent* pos = _entity->FindComponent<MapComponent>();   // 0x140004680
    int cell = static_cast<int>(pos->tileY0) * gameWorld->m_game->size + static_cast<int>(pos->tileX0);

    // Query the existing surface work at the cell (for its +0x24).
    Work* surf = gameWorld->m_tutorial->FindWorkAtCell(cell);   // 0x1400f8d50
    uint32_t surfMatId = 0xffffffff;
    if (surf != nullptr) {
        surfMatId = static_cast<uint32_t>(surf->field_24);   // Work +0x24
    }

    // Spawn a new raw-material entity cloned from the resource description's template.
    ComponentEntity* mem = static_cast<ComponentEntity*>(System::MemoryAllocator::MemAlloc(0x48, 8));
    ComponentEntity* newEntity = mem
                      ? ::new (mem) ComponentEntity(d->_building)   // 0x1400c3720
                      : nullptr;

    RawMaterialComponent* rawMat = newEntity->FindComponent<RawMaterialComponent>();   // 0x1400223c0
    int rt = gameWorld->m_random->RandTol(2);
    // DAT_140172204 = 90.0f (rotation scale).
    float amtScale = kRdata_140172204;   // 0x140172204
    rawMat->PositionRawMaterial(cell, static_cast<float>(rt) * amtScale);   // 0x140129760 (0x1401262f8, unguarded)

    if (*citizenIBP != nullptr) {
        // retail 0x14012630f: 0x140022480 is FindDescription<WorkPlaceDescription> (type cell
        // 0x1401c8118) on the harvesting citizen's description. Its out slot (local_res8,
        // which held the cell) is overwritten at 0x14012634f / 0x14012636a before any read
        // and RAX is dead: retail discards the result. Spelled as retail makes the call.
        (void)(*citizenIBP)->_description->FindDescription<WorkPlaceDescription>();
    }

    rawMat->quantity = amount;        // 0x14012632a, unguarded
    rawMat->profession = nullptr;     // 0x14012632e

    if (surf != nullptr) {
        WorkComponent* work = newEntity->FindComponent<WorkComponent>();   // 0x140003050
        work->workFlags = static_cast<int32_t>(surfMatId);
    }

    // The town raw-material stock (0x14012638a), after the pile is set up and before the
    // register, in retail's order.
    ComponentEntity* regIBP = *citizenIBP;
    gameWorld->m_citizenList->CountProducedMaterial(&regIBP, harvestFlag, d->_building, amount);   // 0x140088ef0
    if (Ext::MapOverlays::g_on) {
        // ext.map_overlays: a natural resource cut, mined or harvested, at the workplace (the pile when none)
        Ext::MapOverlays::OnProducedAt(d->_building, regIBP != nullptr ? regIBP : newEntity, amount);
    }

    Object::InternalPtr<ComponentEntity> listIBP = newEntity;
    gameWorld->m_entityList->RegisterEntity(&listIBP,   // 0x1400c48a0
                   static_cast<int>(d->_building->_category));

    // The pile's cell claim ran inside RegisterEntity: MapComponent::OnAdded -> AddToMap
    // (every RawMaterial template sets _addOnCreate), as retail.

    if ((harvestFlag != 0) && (*citizenIBP != nullptr)) {
        AutoPickupComponent* out = (*citizenIBP)->FindComponent<AutoPickupComponent>();   // 0x140022520
        if (out != nullptr) {
            WorkComponent* work = newEntity->FindComponent<WorkComponent>();   // 0x140003050
            uint32_t v = static_cast<uint32_t>(work->workId);
            out->Append(v);   // 0x140044810
        }
    }
}
