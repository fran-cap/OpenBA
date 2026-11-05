// src/game/Action/createnaturalresource.cpp
// Clean C++ reimplementation of CreateNaturalResource action methods.
// Source: Game/Action/CreateNaturalResource.cpp (from profile build debug strings)
//
// CreateNaturalResource -- 0x38 bytes. Creates a natural resource entity in the world.
// Inherits: Object::Internal -> Action (0x20) -> CreateNaturalResource
// Vtable at 0x14015fca0 (10 slots).
//
// Decompiled from Application-x64.exe using Ghidra.

#include "CreateNaturalResource.h"
#include "object/TypeInfo.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Action/exe_bodies.h"            // CheckCellTerrain, NaturalResourcePlaceAtCell, SetGrowthProgress, ModelMesh*
#include "Action/saved_bounds.h"   // ActionBounds::IsMapCell (review S-005)
#include "Game/Map.h"
#include "Entity/EntityList.h"
#include "Component/ComponentDescription.h"
#include "Component/ModelComponent.h"
#include "Component/NaturalResourceComponent.h"
#include "Game/exe_bodies.h"            // Reimpl_ExternalPtr_Serialize_StringTable
#include "Entity/ComponentEntity.h"   // ComponentEntity(ComponentDescription*) (0x1400c3720)
#include "Entity/EntityList.h"        // EntityList::RegisterEntity (0x1400c48a0)
#include "system/MemoryAllocator.h"       // System::MemoryAllocator::MemAlloc
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch C
#include "Object/factory_registration.h"   // ::Object::GetTypeIdByName (diagnostic below)
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <new>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "system/Value.h"      // ENDGAME B.3.4b System callers
#include "Component/GrowthComponent.h"   // ENDGAME B.3.8 CB3: GrowthComponent::TypeId()
#include "Entity/ComponentEntity.h"

// ============================================================
// Helpers, called by name (B3R)
//   0x140002340 Reimpl_ExternalPtr_Serialize_StringTable -- serialize StringHash/type ref
//   0x1400c3720 ComponentEntity_CtorFromDescription      -- clone entity from description
//   0x14009cb00 ActCNR_CheckTerrain_cb00                 -- check terrain validity
//   0x1400052c0 WorkBase_FindComponent_52c0  -- find component by type (ModelComponent;
//               member ABI: RCX=entity, RDX=retbuf)
//   0x140126540 ActCNR_SetupModelAtCell_126540 (Entity_impl_6540) -- set up model at cell
//   0x14000c460 ActCNR_FindComponent_c460    -- find component (GrowthComponent)
//   0x140120db0 ActCNR_SetGrowthProgress_120db0 -- set growth progress
//   0x140003f60 ActCNR_FindComponent_3f60    -- find component (NaturalResourceComponent)
//   0x1401252f0 ActCNR_NR_GetVariantCount_1252f0 / 0x140125320 ActCNR_NR_GetSubVariantCount_125320
//   0x1401253d0 ActCNR_NR_SetVariant_1253d0  -- set NR variant
//   0x1400c48a0 EntityList_RegisterEntity_Impl -- register entity with entity list
// ============================================================

// ============================================================
// Serialize (0x1400c6890)
// Serializes: status(+0x18), typeRef(+0x20), int(+0x30), versioned entity(+0x28)
// ============================================================
void CreateNaturalResource::Persist(IO::Stream& stream) {
    Action::Persist(stream);   // inlined in retail: Serializer(stream, 0), status +0x18

    // Serialize resource data (version 1)
    IO::Serializer ser(stream, 1);
    ::Object::SerializeExternal<ComponentDescription>(ser, descEntity);   // 0x140002340
    ser.Serialize(cellIndex, nullptr);

    int version = ser.GetVersion();
    if (version > 0) {
        parentEntity.InternalBasePtr::Serialize(ser);
        if (!parentEntity.IsNull()) {
            if (!::Object::IsDerivedFromNullSafe(parentEntity, ComponentEntity::TypeId())) {
                parentEntity = nullptr;
            }
        }
    }
}

// ============================================================
// AbstractImpl (0x1400c6650)
// Creates a natural resource entity in the world.
// Checks if the map cell is available, allocates entity from description,
// sets up model/NR/growth components, randomizes variant, registers entity.
// Returns 0 on success, 2 on failure (cell occupied or invalid).
// ============================================================
int  CreateNaturalResource::AbstractImpl(ActionStack* actionStack) {
    (void)actionStack;
    GameUpdateLoop* gameWorld = GameUpdateLoop::_singleton;
    Map* map = gameWorld->m_game;

    const int32_t cellIndex = this->cellIndex;
    MapCell* cells = map->cells.GetData();

    // The four early-outs below all return the same failure code, so a caller can only see
    // "no resource was created", never why. That matters here: the orchard plant chain sends
    // a bind message immediately after this action and dereferences the entity this action
    // was supposed to leave on the tile, so a silent bail surfaces as a null deref two frames
    // later in a different subsystem. Name the branch, a few times per run.
    static int s_bailReported = 0;
    #define CNR_BAIL(why) do {                                                      \
        if (s_bailReported++ < 4) {                                                 \
            std::fprintf(stderr, "[CreateNR] cell %d: %s — no resource created\n",  \
                         cellIndex, (why));                                         \
            std::fflush(stderr);                                                    \
        }                                                                           \
        return 2;                                                                   \
    } while (0)

    // NOT RETAIL (review S-005): cellIndex comes from the save (Persist +0x30); retail
    // 0x1400c6650 indexes Map::cells with it unbounded (MOVSXD R8,[R14+0x30], no compare
    // with the cell count). An index off the map fails the action (2) before the first read.
    if (!ActionBounds::IsMapCell(cellIndex, map->cells.GetSize())) {
        return 2;
    }

    // Check if the cell already has an entity at the structure slot (+8)
    if (cells[cellIndex].occupant != nullptr) {
        CNR_BAIL("tile already holds an entity");
    }

    // Check if the cell's owner matches our parent entity
    if (cells[cellIndex].blocker != parentEntity.Get()) {
        CNR_BAIL("tile owner != the action's parent entity");
    }

    // Check terrain validity for this cell
    uint64_t terrainResult = map->CheckCellTerrain(cellIndex);   // 0x14009cb00
    if ((char)terrainResult != '\0') {
        CNR_BAIL("terrain check says the cell is unavailable");
    }

    // Check terrain byte flag
    if (cells[cellIndex].footprintCount != 0) {
        CNR_BAIL("tile status byte (+0x28) is set");
    }

    // Allocate a new ComponentEntity copied from description at +0x20
    ComponentDescription* desc = descEntity;
    ComponentEntity* newEntityMem = static_cast<ComponentEntity*>(System::MemoryAllocator::MemAlloc(0x48, 8));
    ComponentEntity* newEntity = nullptr;
    if (newEntityMem != nullptr) {
        newEntity = ::new (newEntityMem) ComponentEntity(desc);   // 0x1400c3720
    }

    // The new entity's reference (an InternalBasePtr in retail)
    ComponentEntity* entityIBP = newEntity;

    // Find the NaturalResourceComponent -- place its model at the cell
    NaturalResourceComponent* naturalResource = entityIBP->FindComponent<NaturalResourceComponent>();   // 0x1400052c0
    naturalResource->PlaceAtCell(cellIndex);   // 0x140126540

    // Find and set up GrowthComponent -- set progress to 0.0
    GrowthComponent* growth = entityIBP->FindComponent<GrowthComponent>();   // 0x14000c460
    // Retail derefs both finder results unguarded, and it is right to: the description this
    // action carries is always a natural-resource template that HAS both components. When one
    // comes back null the description (or the entity copy) is wrong upstream, and the crash
    // alone cannot say which. Report the whole chain once, then let the faithful deref stand.
    if (!naturalResource || !growth || !newEntity) {
        static bool s_reported = false;
        if (!s_reported) {
            s_reported = true;
            std::fprintf(stderr, "[CreateNR] MISSING COMPONENT on the spawned resource: "
                         "desc=%p descTypeGroup=%d cell=%d entityMem=%p newEntity=%p "
                         "modelComp=%p growthComp=%p\n",
                         static_cast<void*>(desc), desc ? static_cast<int>(desc->_category) : -1, cellIndex,
                         static_cast<void*>(newEntityMem), static_cast<void*>(newEntity),
                         static_cast<void*>(naturalResource), static_cast<void*>(growth));
            // Two very different causes look identical from here: the TEMPLATE genuinely has
            // no GrowthComponent, or the finder's type-id .rdata slot is unregistered/zero so
            // it misses on every entity (reference_registered_but_filled_with_zero). Separate
            // them by re-scanning with the by-NAME id, which does not depend on that slot.
            const uint32_t tidByName = GrowthComponent::TypeId();
            int compN = newEntity ? newEntity->_components._size : 0;
            Object::InternalPtr<Component>* compA = newEntity ? newEntity->_components._data : nullptr;
            int growthByName = 0;
            for (int ci = 0; ci < compN && compA; ++ci) {
                Component* c = compA[ci].Get();
                if (!c) continue;
                uint32_t t = c->Type();   // slot 2
                if (tidByName && t == tidByName) ++growthByName;
            }
            std::fprintf(stderr, "[CreateNR]   GrowthComponent::TypeId()=%u | components on entity=%d, "
                         "matching by NAME=%d  => %s\n",
                         tidByName, compN, growthByName,
                         growthByName > 0 ? "THE TEMPLATE HAS ONE — the finder's type-id slot is "
                                            "the bug" : "the template genuinely has no "
                                            "GrowthComponent — look upstream at the description");
            std::fflush(stderr);
        }
    }
    growth->SetProgress(0.0f);   // 0x140120db0

    // Find the ModelComponent and randomize its mesh variant
    System::Random* random = gameWorld->m_random;

    uint32_t variantCount = entityIBP->FindComponent<ModelComponent>()->MeshGroupCount();   // 0x140003f60, 0x1401252f0
    uint32_t variant = random->Rand(variantCount);

    ModelComponent* model = entityIBP->FindComponent<ModelComponent>();      // 0x140003f60
    ModelComponent* modelSet = entityIBP->FindComponent<ModelComponent>();   // 0x140003f60

    uint32_t subVariantCount = model->MeshCount((int)variant);   // 0x140125320
    uint32_t subVariant = random->Rand(subVariantCount);
    modelSet->SelectMesh((uint16_t)variant, (int16_t)subVariant);   // 0x1401253d0

    // Register entity with the entity list
    Object::InternalPtr<ComponentEntity> entityRef = entityIBP;
    int typeGroup = static_cast<int>(descEntity->_category);
    gameWorld->m_entityList->RegisterEntity(&entityRef, typeGroup);   // 0x1400c48a0

    return 0;
}

// ============================================================
// GetTarget (0x1400c6610)
// Returns true if param matches entity at +0x28.
// ============================================================
bool CreateNaturalResource::GetTarget(ComponentEntity** param) {
    return (*param == parentEntity.Get()) ? 1 : 0;
}

// ============================================================
// class CreateNaturalResource (CreateNaturalResource.h) — ENDGAME B.3.8 ACT.
// The hand-built g_CreateNaturalResource_Vtable[10], its per-class type-id cache and the
// Action_Alloc / Action_InitInPlace factories are gone: the compiler emits
// the table, DECLARE_EXE_TYPE answers slots 1/2.
// ============================================================
#include "Object/factory_registration.h"

// create 0x1400221d0 (inlined ctor). The tree's factory zeroed every field above
// +0x10; kept (not a behaviour change of this step).
CreateNaturalResource::CreateNaturalResource()
    : descEntity(nullptr),
      parentEntity(nullptr),
      cellIndex(0),
      _pad34{} {}

// [3] 0x1400c6890
// [4] 0x1400c6650
// [9] 0x1400c6610

::Object::Base* CreateNaturalResource::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(CreateNaturalResource), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) CreateNaturalResource());
}
::Object::Base* CreateNaturalResource::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) CreateNaturalResource());
}


// ============================================================
// FUN_1400c6a20 — CreateNaturalResource_Action_CopyCtor
//   RCX=self, RDX=param_2 (raw u64 @ +0x20), R8=param_3, R9D=param_4 (int @ +0x30).
//   +0x20 = param_2 ; +0x28 = *param_3 ; +0x30 = param_4.
// ============================================================
// 0x1400c6a20: +0x20 = param_2 (raw) ; +0x28 = *param_3 ; +0x30 = param_4.
CreateNaturalResource::CreateNaturalResource(ComponentDescription* desc,
                                             ComponentEntity* const* parent, uint32_t cell)
    : descEntity(desc),
      parentEntity(*parent),
      cellIndex(static_cast<int32_t>(cell)) {}

DEFINE_EXE_TYPE(CreateNaturalResource, "Action")
