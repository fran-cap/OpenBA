// src/game/Component/storagedisplaycomponent.cpp
// Clean reimplementation of StorageDisplayComponent lifecycle methods from Application-x64.exe.
//
// StorageDisplayComponent (0x38 bytes) renders visual display objects for stored items
// on buildings (e.g. logs stacked outside a storage barn).
//
// Vtable at 0x14015a780. Six overrides:
//   Shutdown  (0x140056970) — Component_Serialize + empty Serializer block
//   Serialize (0x140056930) — RET 0 (3 bytes, unhookable no-op)
//   Draw      (0x140056730) — grid-based layout of the piles
//   OnCreate  (0x1400566b0) — remove the piles from the scene
//   Update    (0x1400566a0 -> 0x1400563e0) — proportional pile allocation (Refresh)
//   OnUpdate2 (0x140056940) — Draw + Update combined
//
// Decompiled from Application-x64.exe using Ghidra.

// graphics/*.h FIRST: they pull src/engine/volume/Volume.h, whose global-scope
// `struct Rectangle` is hidden by <wingdi.h>'s `Rectangle()` once <windows.h> has
// been seen through resolve.h.  ENDGAME B.3.5 caller batch I2.
#include "graphics/Scene.h"
#include "Component/RawMaterialDescription.h"
#include "Component/ModelDescription.h"
#include "graphics/Mesh.h"
#include "Component/StorageDisplayComponent.h"
#include "Component/StorageComponent.h"
#include "Component/MapComponent.h"
#include "Component/ComponentDescription.h"
#include "Entity/ComponentEntity.h"
#include "Game/Map.h"   // Map::size
#include "object/External.h"
#include "object/Internal.h"
#include "object/TypeInfo.h"
#include "graphics/Camera.h"
#include "Game/exe_bodies.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "system/MemoryAllocator.h"
#include "math/Math.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include "Object/factory_registration.h"   // ::Object::GetTypeIdByName (SA type-id fallback)
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch A
#include <new>
#include "Component/exe_bodies.h"
#include "Game/exe_constants.h"

// ============================================================
// Pile-rendering bring-up
// ============================================================
// The StorageYard/StorageCart carry a StorageDisplayComponent that renders visible
// stacks of the stored raw materials.

// RawMaterialDescription type-id (cell 0x1401c7cb8) — used by Refresh to size each
// material's pile. TypeId() carries the ctor sentinel 0xFFFFFFFF until
// Factory::Startup runs (D6); treat that the same as unresolved.
static uint32_t SDD_RawMatDescTypeId() {
    const uint32_t id = RawMaterialDescription::TypeId();
    if (id != 0xFFFFFFFF) return id;
    return 0;
}

// Exe helpers, by retail address:
//   0x140004680 FindComponent<MapComponent>
//   0x14000ca10 StorageDisplayComponent::SetPileCount = Array<Pile>::SetSize (the header)
//   0x14009f3e0 Reimpl_MapGetTerrainPos (CharacterComponent_impl_f3e0, map tile -> world;
//               CALL @140056869 in StorageDisplayComponent::Draw)
//   0x140006270 FindComponent<StorageComponent> (type DWORD_1401c7e78). Verified in the
//               real Update disassembly at 0x1400563e0: the CALL at 0x140056432 targets
//               0x140006270, NOT 0x140003050. (0x140003050 finds type 0x1401c8070 =
//               WorkComponent — which the yard ALSO carries, so the old wiring silently
//               found the WorkComponent and read StorageComponent field offsets off it,
//               an out-of-bounds read.)
//   0x1400562b0 StorageDisplayComponent::SetPileMesh (below)
//   0x14000cbd0 FindModelDescription (below)
//   the GameWorld cell 0x1401c3dd8 is g_pGameWorldSingleton; .rdata 0x140172204 = 90.0f
//   (checkerboard yaw), 0x140172114 = 0.5f (half-tile centre bias), 0x14017214c = 1.0f.

// ============================================================
// StorageDisplayComponent::Refresh (0x1400563e0, the slot [9] body)
// ============================================================
// Queries the StorageComponent on the entity, iterates its items, looks up the
// RawMaterialDescription of each material, and allocates piles in proportion to
// each material's weight.
//
// Original logic:
//   1. CheckPtr + FindComponent(MapComponent) + FindComponent(StorageComponent)
//   2. Read the storage item count and capacity (total capacity weight)
//   3. For each item with a count:
//      a. Get the material's ComponentDescription
//      b. Find its RawMaterialDescription
//      c. Compute fraction = (weight * count) / capacity * pileCount
//      d. Assign that many piles with the material's visual
//   4. Remaining piles get cleared (no mesh)
void StorageDisplayComponent::Refresh() {
    // FindComponent(MapComponent) — retail's result is unused
    (void)_entity->FindComponent<MapComponent>();   // 0x140004680

    StorageComponent* storage = _entity->FindComponent<StorageComponent>();   // 0x140006270

    int displayIndex = 0;
    int slotCount = 0;

    if (!storage) goto fill_remaining;

    slotCount = storage->storageSlots._size;

    {
        float uvScale = kRdata_14017214c;   // g_fUVScale, .rdata 0x14017214c
        int totalWeight = storage->capacity;
        int displayCount = piles._size;
        const uint32_t rawMatTid = SDD_RawMatDescTypeId();

        if (slotCount > 0 && totalWeight > 0) {
            for (int slotIdx = 0; slotIdx < slotCount; slotIdx++) {
                const StorageComponent::Item& item = storage->storageSlots[slotIdx];
                int itemCount = item._count;

                if (itemCount == 0) continue;

                // The material's description
                ComponentDescription* materialType = item._resource;

                // Find RawMaterialDescription on the material: the decompilation does a
                // manual sub-description search with a type-id check.
                RawMaterialDescription* rawMatDesc = nullptr;
                if (materialType && rawMatTid) {
                    int compCount = materialType->_subDescCount;
                    Description** compArray = materialType->_subDescs;
                    for (int c = 0; c < compCount; c++) {
                        Description* comp = compArray[c];
                        if (!comp) continue;   // guard partially-materialized SA entity
                        if (comp->Type() == rawMatTid) {
                            if (Object::IsDerivedFromNullSafe(comp, rawMatTid)) {
                                rawMatDesc = static_cast<RawMaterialDescription*>(comp);
                            }
                            break;
                        }
                    }
                }

                if (!rawMatDesc) continue;

                // Compute proportional display count
                int matWeight = rawMatDesc->_weight;
                float fraction = ((float)(matWeight * itemCount) / (float)totalWeight)
                                 * (float)displayCount;
                int assignCount = (int)fraction;

                // Assign piles
                if (assignCount > 0) {
                    for (int j = 0; j < assignCount; j++) {
                        SetPileMesh(displayIndex, uvScale, materialType);
                        displayIndex++;
                    }
                }

                // Check if we've filled all piles
                if (displayIndex >= displayCount) break;

                // Assign one more with partial fraction
                if (displayIndex < displayCount) {
                    float partial = fraction - (float)assignCount;
                    SetPileMesh(displayIndex, partial, materialType);
                    displayIndex++;
                }

                if (displayIndex >= displayCount) break;
            }
        }
    }

fill_remaining:
    // Clear the remaining piles (no mesh)
    {
        int displayCount = piles._size;
        if (displayIndex < displayCount) {
            while (displayIndex < displayCount) {
                SetPileMesh(displayIndex, 0.0f, nullptr);
                displayIndex++;
            }
        }
    }
}


// ============================================================
// StorageDisplayComponent::SetPileCount (0x14000ca10) is Array<Pile>::SetSize: the
// growth rule, copy-construct / default-construct / destroy order of the one
// SetSize core (Collection.h), with Pile's element operations (the header).
// ============================================================
// FindComponent-shaped finder over a ComponentDescription's sub-descriptions
// (0x14000cbd0): the first whose type id equals ModelDescription's (DAT_1401c7a88),
// if it IsDerivedFrom that type, else null. Faithful 1:1 port of the Ghidra decompile.
// ============================================================
static ModelDescription* FindModelDescription(const ComponentDescription* host) {
    const uint32_t modelTid = ModelDescription::TypeId();
    ModelDescription* found = nullptr;
    if (host && modelTid) {
        int count = host->_subDescCount;
        Description** arr = host->_subDescs;
        for (int i = 0; i < count; i++) {
            Description* comp = arr[i];
            if (!comp) continue;   // guard partially-materialized SA entity
            if (comp->Type() == modelTid) {
                if (Object::IsDerivedFromNullSafe(comp, modelTid))
                    found = static_cast<ModelDescription*>(comp);
                break;
            }
        }
    }
    return found;
}

// ============================================================
// StorageDisplayComponent::SetPileMesh (0x1400562b0)
// ============================================================
// A null material removes the pile from the scene; otherwise pick the mesh of the
// material model's second mesh group nearest the requested fraction and (re)add it.
void StorageDisplayComponent::SetPileMesh(int index, float fraction, ComponentDescription* material) {
    if (GameUpdateLoop::_singleton == nullptr) return;
    GameUpdateLoop* gw = GameUpdateLoop::_singleton;

    if (index >= piles._size) return;
    Graphics::Scene* scene = gw->m_graphicsScene;

    if (material == nullptr) {
        int32_t objId = piles[index].sceneId;
        if (objId >= 0) {
            scene->RemoveObject(objId);
            piles[index].sceneId = -1;
        }
        return;
    }

    // Select the mesh nearest the requested fraction. Retail reads the model's mesh
    // group [1] (ModelDescription+0x20 is the group array; +0x20 / +0x28 of it are
    // group[1]'s meshes / meshCount). Every deref below is null-guarded (the tree's)
    // so a partially-materialized SA material (no ModelDescription / empty group)
    // can't fault SetMesh — it just renders no pile for that slot.
    ModelDescription* model = FindModelDescription(material);   // 0x14000cbd0
    if (!model) return;                                     // material has no ModelDescription
    const ModelMeshGroup* groups = model->_meshGroups;
    if (!groups) return;                                    // ModelDescription has no mesh groups
    const ModelMeshGroup& group = groups[1];
    int lodCount = group.meshCount;
    if (lodCount <= 0) return;                              // empty group
    int lodIdx = lodCount - 1;
    int scaled = (int)((float)lodCount * fraction);
    if (scaled < lodIdx) lodIdx = scaled;
    if (lodIdx < 0) lodIdx = 0;                             // clamp (fraction may be ~0)
    Graphics::GraphicsMesh** meshArr = group.meshes;
    if (!meshArr) return;
    Graphics::GraphicsMesh* mesh = meshArr[lodIdx];
    if (!mesh) return;                                      // no mesh at this index

    Pile& pile = piles[index];
    int32_t objId = pile.sceneId;
    if (objId >= 0) {
        if (pile.mesh._mesh.Get() == mesh) {
            return;   // already showing this mesh (steady-state early-out)
        }
        scene->RemoveObject(objId);
    }
    pile.mesh.SetMesh(Object::ExternalPtr<Graphics::GraphicsMesh>(mesh));
    pile.sceneId = scene->AddObject(&pile.mesh);
}

// ============================================================================
// ENDGAME B.3.8 VIS — StorageDisplayComponent as a REAL class (StorageDisplayComponent.h).
//
// Before: a 48-slot StorageDisplayComponent_LiveVtable filled per create,
// Inert_ScalarDtor in slot 0 (only ~Internal).
// After: the compiler's 16-slot vtable and a real ctor/dtor chain
// (B38_RULES D1, D2); slot bodies are the ones the old table installed.
// ============================================================================
#include "Object/factory_registration.h"

// DRIVER: faithful — the pile refresh is the component's own per-frame Update
// (+0x48), dispatched natively by EntityList::Update's 5-bucket round-robin
// (entitylist.cpp Step 4) every cadence, exactly like retail.

// 0x140056a00: Component() is the CALL 0x1401188b0; the 0x14015a780 stamp is
// the compiler's; +0x28 = +0x30 = 0; flags |= 0xcb.
StorageDisplayComponent::StorageDisplayComponent()
    : piles() {
    OrFlagsInCtor(0xcb);
}

// ============================================================
// StorageDisplayComponent::Persist (vtable slot [3], 0x140056970)
// ============================================================
// Simple: Component_Serialize + one empty Serializer block (version 0).

void StorageDisplayComponent::Persist(IO::Stream& stream) {
    Component::Persist(stream);   // retail: CALL 0x1401189b0 (qualified)

    IO::Serializer ser(stream, 0);
}
// [4] 0x140056930 — the class's own `RET` body.
void StorageDisplayComponent::Startup() {}
// ============================================================
// StorageDisplayComponent::OnAdded (vtable slot [6], 0x140056730)
// ============================================================
// Grid-based layout: computes world positions for each pile from the building's
// tile rectangle, creates rotated transforms, applies them to the meshes.
//
// Original logic:
//   1. CheckPtr + FindComponent(MapComponent) on entity
//   2. Read the tile rectangle (width, height)
//   3. Resize the pile array to width*height
//   4. Read the map from gameState->+0xf8
//   5. For each pile:
//      a. Compute tile coords from linear index (col = i % width, row = i / width)
//      b. Add building origin offset (tileX0, tileY0)
//      c. Compute world position via tile-to-world conversion
//      d. Apply yaw rotation (alternating checkerboard pattern)
//      e. Build transform and set on the mesh

void StorageDisplayComponent::OnAdded() {
#ifdef _DEBUG
    printf("[StorageDisplayComp::Draw] ENTER self=%p\n", static_cast<void*>(this));
    fflush(stdout);
#endif

    MapComponent* mapComp = _entity->FindComponent<MapComponent>();   // 0x140004680
    if (!mapComp) return;

    int width = (int)mapComp->tileX1;
    int16_t tileX0 = mapComp->tileX0;
    int16_t tileY0 = mapComp->tileY0;
    int colCount = width;

    int height = (int)mapComp->tileY1;

    // Resize the pile array to width * height
    int totalSlots = width * height;
#ifdef _DEBUG
    printf("[StorageDisplayComp::Draw]   w=%d h=%d total=%d arrayBase=%p\n",
           width, height, totalSlots, static_cast<void*>(&piles));
    fflush(stdout);
#endif
    SetPileCount(totalSlots);   // 0x14000ca10
#ifdef _DEBUG
    printf("[StorageDisplayComp::Draw]   MeshResize done\n");
    fflush(stdout);
#endif

    // Get the map from game state
    if (GameUpdateLoop::_singleton == nullptr) return;
    Map* mapInfo = GameUpdateLoop::_singleton->m_game;
    if (!mapInfo) return;

    //   DAT_140172204 = 0x42b40000 = 90.0f (per-cell checkerboard yaw, degrees)
    //   DAT_140172114 = 0x3f000000 = 0.5f  (half-tile center bias)
    float yawAngle = kRdata_140172204;
    float halfTile = kRdata_140172114;

    int displayCount = piles._size;
    if (displayCount <= 0) return;

    int mapSize = mapInfo->size;
    float halfMap = (float)(mapSize >> 1) - halfTile;

    for (int i = 0; i < displayCount; i++) {
        int row = i / colCount + (int)tileY0;
        int col = i % colCount + (int)tileX0;
        int tileIndex = mapSize * row + col;

        // Build position vector from tile
        Math::Vector3 vec3Buf((float)(tileIndex % mapSize) - halfMap,
                              (float)(tileIndex / mapSize) - halfMap,
                              0.0f);

        // Convert tile to world position (f3e0). worldPos[0]/[1] are the world X/Y
        // on the yard footprint; the original drops worldPos[2] and uses z=0.
        float worldPos[4] = {0.f, 0.f, 0.f, 0.f};
        mapInfo->GetTerrainPos(worldPos, &vec3Buf.x, '\0', nullptr);   // 0x14009f3e0

        // Build transform with yaw rotation
        // Alternating checkerboard: if (row + col) is odd, use zero yaw
        float pitch = 0.0f;
        float yaw = yawAngle;
        if ((row + col) & 1) {
            yaw = 0.0f;
        }

        // FromYawPitchOffset(yaw, pitch = 0, offset). The OFFSET is the per-cell
        // world position {worldPos.x, worldPos.y, 0} — verified in the real Draw
        // disassembly (0x1400568c9/cf write worldPos.x/.y into the offset vec,
        // 0x1400568ba forces offset.z=0). The prior port passed a ZERO offset here,
        // which placed every pile mesh at the world origin (invisible on the yard).
        Math::Vector3 offset(worldPos[0], worldPos[1], 0.0f);
        Math::Transform transform = Math::Transform::FromYawPitchOffset(
            Math::Scalar(yaw), Math::Scalar(pitch), offset);

        // Apply transform to the mesh
        piles[i].mesh.SetTransform(transform);
    }
}

// ============================================================
// StorageDisplayComponent::OnRemove (vtable slot [7], 0x1400566b0)
// ============================================================
// Iterates the piles, removes each from the graphics scene if its scene id is
// valid (>= 0). Resets each to -1 after removal.

void StorageDisplayComponent::OnRemove() {
    int count = piles._size;
    Pile* array = piles._data;

    if (GameUpdateLoop::_singleton == nullptr) return;

    Graphics::Scene* scene = GameUpdateLoop::_singleton->m_graphicsScene;

    for (int i = 0; i < count; i++) {
        Pile& pile = array[i];
        int32_t sceneId = pile.sceneId;

        if (sceneId >= 0) {
            scene->RemoveObject(sceneId);
            pile.sceneId = -1; // Reset to -1
        }
    }
}

// [9] 0x1400566a0 (`JMP 0x1400563e0`) — the NATIVE +0x48 driver: Refresh alone
// (review F-215). The pile grid layout is slot 6 (OnAdded, at RegisterEntity) and
// slot 11 (Slot11, post-load) only; retail slot 9 never lays it out again per tick.
void StorageDisplayComponent::Update(const System::Time* /*time*/) {
    Refresh();
}

// ============================================================
// StorageDisplayComponent::Slot11 (vtable slot [11], 0x140056940)
// ============================================================
// Calls vtable Draw then Refresh (display rebuild + content refresh).
//
// Original disassembly:
//   MOV RAX,[RCX]            ; vtable
//   CALL [RAX+0x30]          ; vtable->Draw(self)
//   JMP Entity_impl_63e0     ; tail call to Refresh

void StorageDisplayComponent::Slot11() {
    OnAdded();   // vtable +0x30 (slot 6), a virtual dispatch in retail
    Refresh();
}

// create 0x14000c770: MemAlloc(0x38, 8), then the ctor.
::Object::Base* StorageDisplayComponent::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(StorageDisplayComponent), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) StorageDisplayComponent());
}
// Retail's createInPlace is `Internal::CreateInPlaceObject` (returns null); the
// tree's in-place entry built the object, and that is kept (B38 B8-13).
::Object::Base* StorageDisplayComponent::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) StorageDisplayComponent());
}

DEFINE_EXE_TYPE(StorageDisplayComponent, "Component")
