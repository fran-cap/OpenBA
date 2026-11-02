// src/game/Work/consumeproduce.cpp
// Clean C++ reimplementation of ConsumeProduce Work class.
// Source: Game/Work/ConsumeProduce.cpp (from profile build debug strings)
//
// ConsumeProduce -- consume/produce work cycle. Size = 0x78.
// Inherits: Object::Internal -> Work (0x48) -> ConsumeProduce
// Ctor: 0x1400e9340, Vtable: 0x140161220
//
// Methods implemented:
//   Ctor (0x1400e9340), ScalarDtor (0x14002a190), Serialize (0x1400e9020)
//   Execute (vtable slot 4, 0x1400e7840)
//   OnComplete (vtable slot 6, 0x1400e77c0)
//   OnCancel (vtable slot 7, 0x1400e7780)
//   Cleanup (vtable slot 8, 0x1400e8f30)
//   Validate (vtable slot 9, 0x1400e7530)
//   GetTarget (vtable slot 10, 0x1400e8fb0)
//

#include "Work/ConsumeProduce.h"
#include "object/TypeInfo.h"
#include "Entity/exe_bodies.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "system/MemoryAllocator.h"
#include "Work/exe_bodies.h"
#include "Action/exe_bodies.h"      // B3R: bodies called by name
#include "Component/exe_bodies.h"   // B3R: bodies called by name
#include "object/Type.h"            // B3R: RtObject_Type_Serialize
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch C
#include "system/Value.h"      // ENDGAME B.3.4b System callers
#include "redzone_alloc.h"   // RedzoneAlloc_PrintBacktrace (wild-write diagnostic)
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <new>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "Component/ConsumeProduceComponent.h"     // ENDGAME B.3.9 D5
#include "Component/ConsumeProduceDescription.h"   // ENDGAME B.3.9 D5
#include "Component/StatusIconComponent.h"         // ENDGAME B.3.9 D5
#include "Component/StorageComponent.h"            // ENDGAME B.3.9 D5
#include "Component/RawMaterialDescription.h"      // ENDGAME B.3.9 D5
#include "Component/WorkComponent.h"               // ENDGAME B.3.9 D5
#include "Component/CitizenComponent.h"            // ENDGAME B.3.9 D5
#include "Component/CharacterComponent.h"
#include "Component/ModelComponent.h"
#include "Component/InteractComponent.h"
#include "Component/StorageLocationComponent.h"
#include "Component/WorkPlaceComponent.h"
#include "Entity/ComponentEntity.h"
#include "Game/Map.h"
#include "Entity/CitizenBehavior.h"
#include "Component/ComponentDescription.h"
#include "Component/Description.h"
#include "Action/CompleteTask.h"
#include "Action/PathSearch.h"   // RawArray (PickBestDropTarget)
#include "ext/registry.h"         // Ext::Fix().input_fetch_room
#include "ext/input_fetch_room/input_fetch_room_rule.h"   // fix.input_fetch_room: the fetch count

// ============================================================
// Sub-functions, called by name (B3R; Work/Action/Component exe_bodies.h)
// ============================================================
//   slot 0x1401516b0 Object::Type::Serialize     RtObject_Type_Serialize (object/Type.h)
//   0x1400f7100 Work::ValidateTask                 Work::ValidateTask (member)
//   0x1400c5400 task list cleanup                  WorkBase_TaskListCleanup_c5400
//   0x140002e10 FindComponent (DWORD_1401c80e0)    AttendanceComponent_GetJobHostComponent
//   0x14012c3f0 StatusIconComponent::SetFlag       (member)
//   0x140119470 the weighted recipe pick           PickConsumeRecipe
//   0x140122e20 InteractComponent node position    InteractNodePosition
//   0x14008d590 / 0x14012d5d0                      PickBestDropTarget / FindBestAdjacentCell
//   0x140115e60 CitizenComponent::LongTermPlan     (member)
//   0x14012cf10 StorageComponent::QueryCount       (member)
//   0x1401193a0 ConsumeProduceAction_helper_93a0   WorkLeaf_ConsumeProduce_93a0
//               (the caller reads the low byte, as retail's AL)
//   0x14012b310 ConsumeProduceAction_helper_b310   WorkLeaf_ConsumeProduce_b310
//   0x14009c480 Map::FillZones                     (member)
//   0x1400bef70 SetupConsumeProduceWorkChain       WorkAct_ConsumeProduceCtor_bef70
// Object::Internal ctor/dtor (IAT 0x140152768 / 0x140152770) and Work::Serialize
// (0x1400f7400) are the real base classes. 0x1400032e0 / 0x1400021d0 are
// the field_58 / field_68 members' SetSize.

// ============================================================
// Constructor (0x1400e9340)
// Object::Internal::Internal() -> Work fields -> ConsumeProduce vtable + fields
// ============================================================
// The Work base init and the two stamps are the compiler's. field_48 (bool) and
// field_4c/field_54 (ints) are NOT initialized by retail's ctor.
ConsumeProduce::ConsumeProduce()
    : Work(),
      field_50(0) {        // Type; +0x58 / +0x68 are the two arrays' ctors
}

// BaseDtor 0x1400e9190. The stamps (ConsumeProduce, then Work) and the `~Internal`
// tail call are the compiler's.
ConsumeProduce::~ConsumeProduce() {
    // The +0x68 int array, then the +0x58 IBP array: the members' ~Array (MemFree, then
    // data / count / capacity zeroed) in reverse declaration order after this empty body.
}

// ============================================================
// Serialize (0x1400e9020)
// Work::Serialize + Serializer(version=0) + IBP array + int array + fields
// ============================================================
void ConsumeProduce::Persist(IO::Stream& stream) {
    // Work::Persist (0x1400f7400, a direct call in retail — QUALIFIED here)
    Work::Persist(stream);

    // Create IO::Serializer with version 0 (destroyed at the end, as retail)
    IO::Serializer serializer(stream, 0);

    // Check if loading (direction 0 means loading)
    bool isLoading = (static_cast<int>(serializer.GetDirection()) == 0);   // IO::Serializer::GetDirection, retail 0x180006210

    // --- Serialize IBP array count (+0x60) ---
    // Use a local copy like the original does (original serializes into a stack local,
    // not directly into self->field_60). This avoids issues if ResizeIBPArray
    // modifies field_58._size as a side-effect.
    int ibpCount = field_58._size;
    serializer.Serialize(ibpCount, nullptr);

    // If loading, resize the IBP array to match deserialized count.
    // 0x1400032e0 == Collection::Array<InternalPtr>::SetSize (kSpec_PtrZero).
    if (isLoading) {
        field_58.SetSize(ibpCount);
    }

    // Serialize each InternalBasePtr in the array
    Object::InternalPtr<CitizenComponent>* ibpArray = field_58._data;
#ifdef _DEBUG
    if (ibpCount > 0 && ibpArray == nullptr) {
        printf("[ConsumeProduce::Serialize] BUG: ibpCount=%d but ibpArray=NULL! this=%p isLoading=%d field_58=%p field_58._size=%d\n",
               ibpCount, (void*)this, (int)isLoading, (void*)field_58._data, field_58._size);
    }
#endif
    for (int i = 0; i < ibpCount; i++) {
        ibpArray[i].InternalBasePtr::Serialize(serializer);

        // After deserialize, validate the entity type.
        //
        // GUARDED because standalone GENUINELY DIFFERS from retail here, not to paper
        // over a faithful crash: in retail DAT_1401c7158 is always a live CitizenComponent
        // type id, but in our build that .rdata slot is registered only when
        // BANISHED_LIVE_CITIZENAI_CITID is set. That flag GRADUATED 2026-08-26 and is now
        // default-on (kill-switch =0), so the common case is a live id — but the guard
        // stays, because the kill-switch still produces a NULL slot and this is
        // Serialize-side, which autostart never reaches, so a regression here would only
        // ever surface as a save/load crash. See standalone/reg_citizenai_typeids.cpp.
        //
        // When the id is unavailable we KEEP the ref rather than clearing it. Retail's
        // intent is "drop refs that are not citizen entities"; with no id we cannot tell,
        // and these refs are citizens in the overwhelming majority, so clearing them would
        // destroy live state — strictly worse than skipping a check we cannot perform.
        // Same both-pointer-and-value shape as completetask.cpp's reader.
        CitizenComponent* ptr = ibpArray[i].Get();
        if (ptr != nullptr) {
            bool valid = Object::IsDerivedFromNullSafe(ptr, CitizenComponent::TypeId());  // DAT_1401c7158
            if (!valid) {
                ibpArray[i] = nullptr;  // null out invalid refs
            }
        }
    }

    // --- Serialize int array count (+0x70) ---
    int intCount = field_68._size;
    serializer.Serialize(intCount, nullptr);

    // If loading, resize the int array.
    // 0x1400021d0 == Collection::Array<int32>::SetSize (retail
    // ConsumeProduceAction_Shutdown calls it at 1400e90ff): a grown tail is
    // left as the allocation found it, like retail. Safe: the resize runs only when
    // loading and the bulk int read below writes all intCount elements from the stream.
    if (isLoading) {
        field_68.SetSize(intCount);
    }

    // Bulk-serialize the int array
    // Original: IO::Serializer::Serialize(serializer, count, intArrayPtr, name)
    serializer.Serialize(intCount, field_68._data, nullptr);

    // --- Serialize ConsumeProduce fields ---
    serializer.Serialize(*reinterpret_cast<bool*>(&field_48), nullptr);   // LEFT: WORK:H8, byte_io candidate: retail 0x1400e911a / 0x1400e9126 hands &this+0x48 to slot 0x140152a58 = Serialize(bool&), the byte as retail's bool, which Execute compares raw; typed bool (tried, B11 -O2), Execute's `field_48 == 0` (cmpb $0x0 / jne) becomes cmpb $0x1 / je, which reads a loaded byte > 1 differently; the field stays uint8_t  // CAST-OK: byte_io (S4 ruling)
    serializer.Serialize(field_4c, nullptr);    // int at +0x4C
    RtObject::RtObject_Type_Serialize(&field_50, &serializer);            // Type at +0x50
    serializer.Serialize(field_54, nullptr);    // int at +0x54
}

// ============================================================
// Helpers for Execute
// ============================================================

// Find a component by type ID on an entity (iterates component list at +0x30/+0x38).
// Returns the component pointer, or nullptr if not found.
// Find a DESCRIPTION by type id in an object's child list at +0x28 (count +0x30).
//
// This is NOT the same walk as ComponentEntity::FindComponent, and the difference is
// load-bearing: a ComponentEntity keeps its COMPONENT array at +0x30 with the count at
// +0x38, while a description keeps its CHILD list at +0x28 with the count at +0x30. A
// StorageComponent slot record holds a DESCRIPTION in its first qword, so the material
// lookup has to use this form. Retail inlines exactly this loop at 0x1400e7d0f
// (`*(int*)(uVar11 + 0x30)` / `*(longlong*)(uVar11 + 0x28)`), and HappinessComponent
// ::IsActive walks the same slot the same way.
//
// Reusing the component-list finder here is what made Execute fault on its very first
// live pass once slot-18 CreateWork started minting work: reading the count from
// matEntity+0x38 and the array from +0x30 landed inside the slot's (capacity, stored)
// int pair, so the "component pointer" came back as 0x0000000500000005 -- two adjacent
// int32 5s -- and the vtable read off it was the access violation.
static Description* FindDescriptionOnObject(const ComponentDescription* obj, uint32_t typeId) {
    int count = obj->_subDescCount;
    Description** arr = obj->_subDescs;
    for (int i = 0; i < count; i++) {
        Description* c = arr[i];
        uint32_t t = c->Type();   // slot 2
        if (t == typeId) {
            return Object::IsDerivedFromNullSafe(c, typeId) ? c : nullptr;
        }
    }
    return nullptr;
}

// Finalize action: call vtable +0x50, set citizen state flags, store citizen ref.
// The citizen slot handed to Execute is an InternalPtr<CitizenComponent> (its
// +0x10 entity, +0x30, +0x54, +0x88, +0xB0, +0x1F0 and +0x278 reads are
// CitizenComponent's fields).
static int FinalizeAction(ConsumeProduce* self, CitizenComponent** citizenIBP, int priority, int slotIndex) {
    CitizenComponent* citizen = *citizenIBP;

    // Call vtable slot +0x50 (GetTarget): the workplace's profession. Retail writes it
    // into the caller's reused scratch stack slot (dead after this read); a typed local.
    Profession* targetBuf;
    Profession* actionPtr = *self->GetTarget(&targetBuf);

    citizen->statusFlags |= 4;
    citizen->field_0x30 = priority;
    citizen->field_0xB0 = 0;

    if (actionPtr == citizen->profession && priority >= 0) {
        citizen->happinessFlags |= 0x20000;
    }

    // Store citizen entity ref at self->field_58[slotIndex]
    self->field_58[slotIndex] = *citizenIBP;
    return 2;
}



// ============================================================
// Execute (0x1400e7840) -- vtable slot 4
//
// Main production work logic. Returns 1 (retry) or 2 (action pushed).
// ============================================================
int ConsumeProduce::Execute(CitizenComponent** citizenIBP, System::Time* /*context*/) {
    GameUpdateLoop* gameWorld = GameUpdateLoop::_singleton;
    Map* map = gameWorld->m_game;
    CitizenBehavior* worldEntityList = gameWorld->m_citizenList;

    // --- Find InteractComponent on work entity ---
    InteractComponent* posComp = field_10->FindComponent<InteractComponent>();

    // --- Find ConsumeProduceComponent (type 0x1401c7350) on work entity ---
    ConsumeProduceComponent* smComp = field_10.Get()->FindComponent<ConsumeProduceComponent>();

    // --- Get ConsumeProduceDescription from the component's description (+0x18) ---
    Description* descBase = smComp->_description;
    bool isDerived = Object::IsDerivedFromNullSafe(descBase, ConsumeProduceDescription::TypeId());
    ConsumeProduceDescription* cpDesc = isDerived ? static_cast<ConsumeProduceDescription*>(descBase) : nullptr;

    // --- Find StorageLocationComponent (for storage slot lookup) ---
    StorageLocationComponent* storageLoc = field_10.Get()->FindComponent<StorageLocationComponent>();

    // --- Check resource depletion (bVar6) ---
    // Get active product index from ConsumeProduce component
    bool resourcesDepleted = false;
    int activeProductIdx = smComp->PickConsumeRecipe();   // 0x140119470

    // The recipe array (+0x28, 0x28 stride)
    ConsumeProduceRecipe* recipes = cpDesc->_recipes;

    if (smComp->consumeResourceId < 0) {
        // Multi-product mode: check ALL products for depletion
        int productCount = cpDesc->_recipeCount;
        if (productCount > 0) {
            ConsumeProduceRecipe* productPtr = recipes;
            for (int i = 0; i < productCount; i++) {
                int materialType = productPtr->materialType;   // +0x18
                productPtr++;  // stride = 0x28 bytes
                // Check world material supply vs demand (the town's material table, +0x170)
                ProfessionResourceEntry* matArray = worldEntityList->professionResources._data;
                int supply = matArray[materialType].currentCount;   // +0x00
                int demand = matArray[materialType].maxCount;       // +0x08
                if (demand <= supply) {
                    resourcesDepleted = true;
                }
            }
        }
    } else {
        // Single product mode: check specific active product
        int materialType = recipes[activeProductIdx].materialType;   // +0x18
        ProfessionResourceEntry* matArray = worldEntityList->professionResources._data;
        int supply = matArray[materialType].currentCount;   // +0x00
        int demand = matArray[materialType].maxCount;       // +0x08
        resourcesDepleted = (demand <= supply);
    }

    // --- Find StatusIconComponent (type 0x1401c7e08) on work entity ---
    StatusIconComponent* statusIcon = field_10->FindComponent<StatusIconComponent>();

    // Set active state = 2, enabled = resourcesDepleted
    statusIcon->SetFlag(2, resourcesDepleted ? '\x01' : '\0');   // 0x14012c3f0

    // --- Find ConsumeProduceComponent (type 0x1401c7350) for produce check ---
    ConsumeProduceComponent* smComp2 = field_10->FindComponent<ConsumeProduceComponent>();

    // Check produce threshold
    uint64_t canProduce = smComp2->CanProduce();
    // [CPWork] the gate that decides whether the tavern is allowed to make more at
    // all (retail 0x1401193a0 -- a stock-vs-limit test against the "Alcohol Limit 500"
    // the panel shows). Printed on CHANGE only; a permanent 0 here means the work runs
    // forever and produces nothing, which reads exactly like a broken produce path.
    {
        static int n = 0; static int last = -1;
        const int cp = (int)(char)canProduce;
        if (n < 8 && cp != last) { ++n; last = cp;
            fprintf(stderr, "[CPWork] canProduce(threshold)=%d depleted=%d\n",
                    cp, resourcesDepleted ? 1 : 0);
            fflush(stderr);
        }
    }

    // ============================================================
    // BRANCH: Not depleted
    // ============================================================
    if (!resourcesDepleted) {
        if ((char)canProduce == '\0') {
            goto handle_depleted_or_idle;
        }

        // --- Pick random empty output slot ---
        uint32_t startSlot = gameWorld->m_random->Rand((uint32_t)field_58._size);
        int totalSlots = field_58._size;
        uint32_t slot = startSlot;

        if (totalSlots < 1) {
            return 1;
        }

        // Find an empty slot (field_58[slot] == 0)
        Object::InternalPtr<CitizenComponent>* slotCitizens = field_58._data;
        int attempts = 0;
        while (slotCitizens[slot] != nullptr) {
            slot++;
            if ((int)slot >= totalSlots) {
                slot = 0;
            }
            attempts++;
            if (attempts >= totalSlots) {
                return 1;  // All slots occupied
            }
        }

        if (slot == 0xffffffff) {
            return 1;
        }

        // --- Find StorageComponent on work entity ---
        StorageComponent* storage = field_10->FindComponent<StorageComponent>();

        // --- Setup loop vars for product iteration ---
        ConsumeProduceRecipe* recipe = &recipes[activeProductIdx];   // the active recipe (0x28 stride)

        // Get citizen position data for cell index calculation: CharacterComponent
        // (+0x278) -> ModelComponent (+0x68) -> transform translation (+0x158)
        const Math::Vector3& citizenPos = (*citizenIBP)->character->modelComponent->transform.translation;

        (void)citizenPos.z;  // posHeight (+0x160) -- read but not used in reimpl
        int mapSize = map->size;
        int halfMap = mapSize >> 1;
        int cellX = (int)(citizenPos.x + (float)halfMap);
        int cellY = (int)(citizenPos.y + (float)halfMap);
        if (cellX < 0) cellX = 0;
        if (cellX >= mapSize) cellX = mapSize - 1;
        if (cellY < 0) cellY = 0;
        if (cellY >= mapSize) cellY = mapSize - 1;
        int citizenCell = cellY * mapSize + cellX;

        // --- Iterate input materials for active product ---
        // [CPWork] Execute had NEVER run before slot-18 CreateWork was wired, so its
        // first live pass faulted here: the component finder was handed matEntity
        // 0x0000000500000005 -- two adjacent int32 5s, i.e. a (capacity,stored) pair read
        // where a good POINTER belongs. Print the storage the loop is actually walking so
        // it can be compared against the one HappinessComponent::IsActive reports for the
        // same building; if they differ, the StorageComponent finder is the fault, not the
        // slot stride. Budgeted, outcome-only.
        {
            static int n = 0;
            if (n < 6) { ++n;
                StorageComponent::Item* const items = storage ? storage->storageSlots._data : nullptr;
                void* const sd = items;
                void* const slot0 = items ? items->_resource : nullptr;
                fprintf(stderr, "[CPWork] Execute storageComp=%p ibp=%p data=%p count=%d "
                        "slot0=%p tid_storage=%u activeProduct=%d cpSel(+0x50)=%d\n",
                        static_cast<void*>(storage), static_cast<void*>(storage), sd,
                        storage ? storage->storageSlots._size : -1,
                        slot0,
                        StorageComponent::TypeId(),
                        activeProductIdx, smComp->consumeResourceId);
                fflush(stderr);
            }
        }
        // The storage item array (+0x28, StorageComponent::Item, 0x10 stride)
        int inputCount = storage->storageSlots._size;
        if (inputCount > 0) {
            for (int inputIdx = 0; inputIdx < inputCount; inputIdx++) {
                // Get material entity from storage component
                StorageComponent::Item& storageItem = storage->storageSlots[inputIdx];
                ComponentDescription* matEntity = storageItem._resource;

                // Check if this material entity matches the required material for active product
                bool matMatches = (recipe->product == matEntity);
                if (!matMatches) {
                    int64_t matCheckCount = (int64_t)recipe->inputCount;
                    if (matCheckCount > 0) {
                        ConsumeProduceInput* matCheckPtr = recipe->inputs;
                        for (int64_t j = 0; j < matCheckCount; j++) {
                            if (matCheckPtr[j].material == matEntity) {
                                matMatches = true;
                                break;
                            }
                        }
                    }
                }

                if (matMatches) {
                    continue;
                }

                // Material doesn't match -- need to find and haul it
                // Get available amount from storage
                RawMaterialDescription* rawMatComp = static_cast<RawMaterialDescription*>(
                    FindDescriptionOnObject(matEntity, RawMaterialDescription::TypeId()));

                // Check remaining amount in storage: stored - reserved
                int available = storageItem._count - storageItem._reserve;

                // Find StorageComponent on the citizen's entity
                StorageComponent* homeStorage =
                    (*citizenIBP)->_entity->FindComponent<StorageComponent>();

                // Check if home storage has capacity for the material
                int homeCapacity = homeStorage->capacity;
                int homeStored = homeStorage->QueryCount();   // 0x14012cf10

                if (homeCapacity - homeStored < available * rawMatComp->_weight) {
                    // Not enough capacity -- check citizen's own storage
                    StorageComponent* citizenStorage =
                        (*citizenIBP)->_entity->FindComponent<StorageComponent>();

                    int cap2 = citizenStorage->capacity;
                    int stored2 = citizenStorage->QueryCount();   // 0x14012cf10
                    available = (cap2 - stored2) / rawMatComp->_weight;
                }

                if (available == 0) {
                    // No resources available -- replan citizen
                    (*citizenIBP)->LongTermPlan();   // 0x140115e60
                    return 2;
                }

                // --- Search for storage with this resource ---
                float searchPos[3] = { 0.0f, 0.0f, 0.0f };

                // Get entity ref for pathfinding
                ComponentEntity* entityRef = field_10.Get();

                // 0x1400e7eec: the drop target into [RBP-0x60]; 0x1400e7ef1 MOV RDI,[RBP-0x60]
                // keeps it in RDI for the setup at 0x1400e837c.
                ComponentEntity* foundEntity = nullptr;
                worldEntityList->PickBestDropTarget(&foundEntity,
                    (uint32_t)field_28, matEntity,
                    available, '\x01', searchPos, &field_10);   // 0x14008d590

                if (foundEntity != nullptr) {
                    // Check passability
                    uint64_t passable = map->ValidateCell(0xe1f, (uint32_t)citizenCell,
                                                             (uint32_t)field_28);
                    if ((char)passable == '\0') {
                        return 1;
                    }

                    // Get terrain position for storage ([RBP-0x28], zeroed at 0x1400e7f24 / 0x1400e7f2c)
                    float terrainPos[4] = {};
                    bool haveTarget = false;
                    if (storageLoc == nullptr) {
                        // 0x1400e8080 CMP [RBP-0x68],0 (the InteractComponent): none -> next input.
                        if (posComp != nullptr) {
                            // 0x1400e833a-0x1400e8377: InteractNodePosition(posComp, out, 0, 0, null)
                            // with the drop-target slot [RBP-0x60] reused as the 12-byte out (the
                            // entity is already in RDI); the three floats go to terrainPos, then
                            // fall through into 0x1400e837c, the setup the StorageLocation arm
                            // reaches. The tree gives the out its own Vector3 local.
                            Vector3Slot nodePos;
                            posComp->NodePosition(&nodePos.v, 0, 0, nullptr);   // 0x140122e20
                            terrainPos[0] = nodePos.v.x;
                            terrainPos[1] = nodePos.v.y;
                            terrainPos[2] = nodePos.v.z;
                            haveTarget = true;
                        }
                    } else {
                        // Use storage slot lookup
                        int slotMapSize = map->size;
                        int slotCellX = (int)(searchPos[0] + (float)(slotMapSize >> 1));
                        int slotCellY = (int)(searchPos[1] + (float)(slotMapSize >> 1));
                        if (slotCellX < 0) slotCellX = 0;
                        if (slotCellX >= slotMapSize) slotCellX = slotMapSize - 1;
                        if (slotCellY < 0) slotCellY = 0;
                        if (slotCellY >= slotMapSize) slotCellY = slotMapSize - 1;

                        int slotResult = storageLoc->FindBestAdjacentCell(slotMapSize * slotCellY + slotCellX,
                            terrainPos);   // 0x14012d5d0 (the forwarder dropped field_28)
                        haveTarget = (slotResult >= 0);   // 0x1400e7f9d JNS 0x1400e837c
                    }
                    if (haveTarget) {
                        // 0x1400e837c: set up pathfinding and create the work action
                        ComponentEntity* destEntity = foundEntity;   // [RBP+0x98] = RDI
                        CitizenComponent* destEntityRef = *citizenIBP;

                        // The hidden return slot: retail reuses the town-list slot, whose
                        // value is dead past this call; the tree gives the slot its own local.
                        ::Object::InternalPtr<Action> pickup;
                        WorkAct_GatherRawMatCtor_bf790(
                            &pickup, &destEntityRef,
                            field_20, &destEntity,
                            searchPos, &entityRef, terrainPos,
                            matEntity, available, slot, 0, '\x01', 0, 0);

                        int priority = field_20;
                        return FinalizeAction(this, citizenIBP, priority, slot);
                    }
                }
            }
        }

        // --- Second pass: check each input material for nearby storage ---
        bool allInputsAvailable = true;
        int inputMatCount = recipe->inputCount;
        for (int matIdx = 0; matIdx < inputMatCount; matIdx++) {
            ComponentDescription* matEntity2 = recipe->inputs[matIdx].material;

            // Check if this material is already in our storage
            int existingAmount = 0;
            int storageSlotCount = storage->storageSlots._size;
            if (storageSlotCount > 0) {
                StorageComponent::Item* storageSlots = storage->storageSlots._data;
                for (int si = 0; si < storageSlotCount; si++) {
                    if (storageSlots[si]._resource == matEntity2) {
                        if (si >= 0) {
                            existingAmount = storageSlots[si]._count - storageSlots[si]._reserve;
                        }
                        break;
                    }
                }
            }

            int required = recipe->inputs[matIdx].count;
            if (existingAmount < required) {
                allInputsAvailable = false;

                // Search for this material in nearby entities
                float searchPos2[3] = { 0.0f, 0.0f, 0.0f };

                // Get building position for search: the transform translation (+0x158)
                const Math::Vector3& bldgPos = (*citizenIBP)->character->modelComponent->transform.translation;

                mapSize = map->size;
                halfMap = mapSize >> 1;
                int bx = (int)(bldgPos.x + (float)halfMap);
                int by = (int)(bldgPos.y + (float)halfMap);
                if (bx < 0) bx = 0;
                if (bx >= mapSize) bx = mapSize - 1;
                if (by < 0) by = 0;
                if (by >= mapSize) by = mapSize - 1;

                // 0x1400e81f8-0x1400e823b: the input's RawMaterialDescription (RSI), read by
                // the carry-room test below (its weight, +0x48).
                RawMaterialDescription* rawMat2 = static_cast<RawMaterialDescription*>(
                    FindDescriptionOnObject(matEntity2, RawMaterialDescription::TypeId()));

                ComponentEntity* foundStorage = nullptr;
                worldEntityList->PickBestDropTarget(&foundStorage,
                    mapSize * by + bx, matEntity2, 1, '\0', searchPos2,
                    &field_10);   // 0x14008d590

                if (foundStorage != nullptr) {
                    // Found storage -- check if it has the material available
                    StorageComponent* fStorage = foundStorage->FindComponent<StorageComponent>();

                    // Check if storage has the material
                    int fSlotCount = fStorage->storageSlots._size;
                    if (fSlotCount > 0) {
                        StorageComponent::Item* fSlots = fStorage->storageSlots._data;
                        for (int fi = 0; fi < fSlotCount; fi++) {
                            if (fSlots[fi]._resource == matEntity2) {
                                if (fi >= 0 && fSlots[fi]._count != fSlots[fi]._reserve) {
                                    // Material available -- the citizen's own storage.
                                    // 0x1400e85b6-0x1400e85ee: no room for one unit
                                    // (capacity +0x38 - QueryCount < weight) -> replan.
                                    StorageComponent* carry =
                                        (*citizenIBP)->_entity->FindComponent<StorageComponent>();
                                    if (carry->capacity - carry->QueryCount() < rawMat2->_weight) {
                                        (*citizenIBP)->LongTermPlan();   // 0x1400e8686 -> 0x140115e60
                                        return 2;
                                    }

                                    StorageComponent* homeStorage2 =
                                        (*citizenIBP)->_entity->FindComponent<StorageComponent>();

                                    int hs2Count = homeStorage2->QueryCount();   // 0x14012cf10
                                    if (hs2Count >= 1) {
                                        // 0x1400e867b JLE 0x1400e8717 not taken: already
                                        // carrying something -> replan (0x1400e8683).
                                        (*citizenIBP)->LongTermPlan();   // 0x140115e60
                                        return 2;
                                    }
                                    // FIX input_fetch_room (NOT RETAIL, docs/retail_bugs.md §25): ask
                                    // for what the workplace can take, not retail's -1 (a full carry
                                    // the drop-off delivers whole or not at all); none fits -> this
                                    // input is passed over like a store with nothing unreserved.
                                    const int fetchCount = Ext::InputFetchRoom::FetchCount(
                                        Ext::Fix().input_fetch_room, storage->capacity, storage->QueryCount(),
                                        homeStorage2->capacity, hs2Count, rawMat2->_weight);
                                    if (fetchCount == 0) {
                                        break;
                                    }
                                    {
                                        // Storage empty -- set up path to fetch material
                                        ComponentEntity* fetchDest = foundStorage;
                                        Vector3Slot fetchPos;
                                        posComp->NodePosition(&fetchPos.v, 0, slot, nullptr);   // 0x140122e20

                                        ComponentEntity* pathSrc = field_10.Get();

                                        // arg2 is the CITIZEN entity IBP, not the world entity
                                        // list. FindPath immediately does *(*arg2 + 0x278) to
                                        // reach the CitizenAIComponent and clean its task list;
                                        // handing it (GameWorld+0xf0) walked a non-canonical
                                        // "list" pointer and faulted in TaskListCleanup.
                                        // Retail (0x1400e87b9): [RBP] = *R12 (= *entityIBP) and
                                        // LEA RDX,[RBP] — the same &(*entityIBP) the sibling
                                        // call at 0x1400e83f4 passes.
                                        CitizenComponent* pathCitizen = *citizenIBP;

                                        // The hidden return slot: retail reuses foundStorage's
                                        // slot (dead past this call); the tree gives it its own local.
                                        ::Object::InternalPtr<Action> pickup;
                                        WorkAct_GatherRawMatCtor_bf790(
                                            &pickup, &pathCitizen,
                                            field_20, &pathSrc,
                                            &fetchPos.v.x, &fetchDest, searchPos2,
                                            matEntity2, fetchCount, slot, 1, '\0', 0, 0);   // retail -1 (0x1400e8785)

                                        int priority2 = field_20;
                                        return FinalizeAction(this, citizenIBP, priority2, slot);
                                    }
                                }
                                break;
                            }
                        }
                    }
                }
            }
        }

        // --- Third pass: check assigned positions for validity ---
        // Retail 0x1400e7840 bounds BOTH position loops below by *(int*)(self+0x70) — the
        // status INT ARRAY's own count (param_1[0xe]) — not by the citizen-slot count at
        // +0x60 (param_1[0xc]). The port used field_58._size: a workplace with more worker slots
        // than produce positions (e.g. 20 vs 18) then read and WROTE statusArray[18..19],
        // which is the next heap block's boundary tag (+1 then +2 in its high dword, the
        // "free-list link with wrong high bits" teardown crash in the 800x10 stress run,
        // caught 2026-09-04 by a hardware watchpoint: Execute wrote 1, OnComplete wrote 2).
        int totalPositions = (int)field_68._size;  // number of output positions (= int array count)
        uint32_t firstFreePos = 0xffffffff;
        int firstFreeIndex = -1;
        uint32_t freeCount = 0;

        for (int posIdx = 0; posIdx < totalPositions; posIdx++) {
            Vector3Slot posVec;
            posComp->NodePosition(&posVec.v, 2, posIdx, nullptr);   // 0x140122e20

            mapSize = map->size;
            halfMap = mapSize >> 1;
            int px = (int)(posVec.v.x + (float)halfMap);
            int py = (int)(posVec.v.y + (float)halfMap);
            if (px < 0) px = 0;
            if (px >= mapSize) px = mapSize - 1;
            if (py < 0) py = 0;
            if (py >= mapSize) py = mapSize - 1;
            int posCell = py * mapSize + px;

            // Check if position has status 2 but no structure (the cell's occupant, +0x08)
            int* statusArray = field_68._data;
            MapCell* structBase = map->cells.GetData();
            if (statusArray[posIdx] == 2 &&
                structBase[posCell].occupant == nullptr) {
                statusArray[posIdx] = 0;
            }

            if (statusArray[posIdx] == 0) {
                freeCount++;
                if ((int)firstFreePos < 0) {
                    firstFreePos = (uint32_t)posIdx;
                    firstFreeIndex = posCell;
                }
            }
        }

        // If all positions are free, mark as not producing
        if (freeCount == (uint32_t)totalPositions) {
            field_48 = 0;
        }

        // If no free position found, mark as producing
        if ((int)firstFreePos < 0) {
            field_48 = 1;
        } else if (field_48 == 0 && allInputsAvailable) {
            goto setup_produce_action;
        }

        // --- Check each position for workplace assignment ---
        for (int posIdx2 = 0; posIdx2 < totalPositions; posIdx2++) {
            Vector3Slot checkPos;
            posComp->NodePosition(&checkPos.v, 2, posIdx2, nullptr);   // 0x140122e20

            mapSize = map->size;
            halfMap = mapSize >> 1;
            int cx = (int)(checkPos.v.x + (float)halfMap);
            int cy = (int)(checkPos.v.y + (float)halfMap);
            if (cx < 0) cx = 0;
            if (cx >= mapSize) cx = mapSize - 1;
            if (cy < 0) cy = 0;
            if (cy >= mapSize) cy = mapSize - 1;
            int checkCell = cy * mapSize + cx;

            MapCell* structBase2 = map->cells.GetData();
            ComponentEntity* structEntity = structBase2[checkCell].occupant.Get();   // +0x08

            if (structEntity != nullptr) {
                if (structEntity != field_10.Get()) {
                    // Found a different entity at this position -- its WorkComponent
                    WorkComponent* locComp = structEntity->FindComponent<WorkComponent>();

                    CitizenComponent* citizenRef3 = *citizenIBP;
                    // GameUpdateLoop +0x118, the WorkScheduler
                    WorkScheduler* assignMgr = gameWorld->m_tutorial;
                    uint64_t assigned = assignMgr->TryAssignWorkplace(&citizenRef3, locComp->workId, '\x01');
                    if ((char)assigned != '\0') {
                        return 2;
                    }
                }
            }
        }

        if ((int)firstFreePos < 0) {
            return 1;
        }

    setup_produce_action:
        // --- Set up produce action ---
        // Find StatusIconComponent again and toggle state 3
        StatusIconComponent* statusIcon2 = field_10->FindComponent<StatusIconComponent>();
        statusIcon2->SetFlag(3, !allInputsAvailable ? '\x01' : '\0');   // 0x14012c3f0

        if (allInputsAvailable) {
            // Get position for the output slot
            Vector3Slot producePos;
            float produceHeading[2] = {};
            posComp->NodePosition(&producePos.v, 0, slot, produceHeading);   // 0x140122e20

            mapSize = map->size;
            halfMap = mapSize >> 1;
            int prodCellX = (int)(producePos.v.y + (float)halfMap);
            int prodCellY = (int)(producePos.v.x + (float)halfMap);
            if (prodCellY < 0) prodCellY = 0;
            if (prodCellY >= mapSize) prodCellY = mapSize - 1;
            if (prodCellX < 0) prodCellX = 0;
            if (prodCellX >= mapSize) prodCellX = mapSize - 1;

            // Recalculate map zones
            map->FillZones();   // 0x14009c480

            // Get cell structs for passability check
            MapCell* structBase3 = map->cells.GetData();
            MapCell* srcCellStruct = &structBase3[citizenCell];
            MapCell* dstCellStruct = &structBase3[prodCellX * mapSize + prodCellY];

            uint32_t srcMask = 1 << (dstCellStruct->terrainType & 0x1f);   // +0x29
            uint32_t dstMask = 1 << (srcCellStruct->terrainType & 0x1f);
            uint32_t combined = srcMask | dstMask;
            uint32_t passFlags = combined | 0xe1f;

            if ((passFlags & dstMask) != 0 && (passFlags & srcMask) != 0 &&
                ((combined & 0x1e0) != 0 ||
                 (srcCellStruct->zone != 0 &&   // +0x26
                  dstCellStruct->zone != 0 &&
                  srcCellStruct->zone == dstCellStruct->zone))) {

                // Check if enough consume cycles have passed
                char canFinish = '\0';
                if (cpDesc->_i40 <= field_4c) {
                    canFinish = '\x01';
                    field_4c -= cpDesc->_i40;

                    // Increment produce counter on ConsumeProduceComponent
                    ConsumeProduceComponent* smComp3 = field_10->FindComponent<ConsumeProduceComponent>();
                    smComp3->consumeState += 1;
                }

                // Create work action chain
                CitizenComponent* citizenRef4 = *citizenIBP;
                ComponentEntity* workEntityRef = field_10.Get();
                // The tree's read of the qword at +0x50, truncated to int: field_50
                int actionPriority = field_50;

                WorkAct_ConsumeProduceCtor_bef70(
                    &citizenRef4,
                    field_20,
                    &workEntityRef,
                    &producePos.v.x,
                    produceHeading[0],
                    field_10->_description,
                    recipe->product,   // +0x10
                    (uint32_t)canFinish,
                    &actionPriority,
                    (uint32_t)field_54,
                    firstFreeIndex,
                    cpDesc->_f44,
                    (canFinish ? 0x8000 : 0) | ((firstFreePos | 0x100) << 8) | slot);

                // Update status array for the position
                if (canFinish != '\0') {
                    field_68[(int)firstFreePos] = 1;
                }

                int priority3 = field_20;
                return FinalizeAction(this, citizenIBP, priority3, slot);
            }
        }
        return 1;
    }

    // ============================================================
    // BRANCH: Depleted resources or threshold not met
    // ============================================================
    if ((char)canProduce != '\0') {
    handle_depleted_or_idle:
        // Nothing to do here -- fall through
        (void)0;
    }

    // ResetProductionState IS 0x14012b310, and b310 is not a passive "reset": it drives
    // the ResourceProduce state machine 13 -> 14 (PAUSED). State 14's enter calls
    // SetAccepting(wpc, false), which raises the "work is disabled at this location"
    // sign AND evicts the entire worker roster — permanently, because state 14 has no
    // per-frame handler and the only 14 -> 13 requester is the panel's resume event.
    //
    // Retail GUARDS this call; we did not. At 0x1400e8eca:
    //     TEST AL,AL
    //     JNZ  0x1400e8ee1        ; skip straight to the SetActiveState(3,0) tail
    //     CALL 0x14012b310        ; only reached when AL == 0
    // AL is helper_93a0 (0x1401193a0) — our `canProduce`. So the (depleted &&
    // canProduce) arm must SKIP b310 and only set model state 3, keeping its workers.
    //
    // Symptom this caused: a woodcutter works normally until the depletion test flips
    // (~500-1000 frames), is still under its produce limit (canProduce=1), and where
    // retail would idle the model and keep the roster, we paused the building and
    // threw the worker back to laborer. The `[CPWork] canProduce=1 depleted=1` line
    // logged immediately before State14Enter is exactly this arm.
    //
    // The !depleted && !canProduce path (the goto above) still reaches b310, since
    // canProduce == 0 there — which is retail's intent: nothing to make, so stand down.
    if ((char)canProduce == '\0') {
        smComp->RequestProduce();
    }

    // Set active state = 3, disabled
    statusIcon->SetFlag(3, '\0');   // 0x14012c3f0

    return 1;
}

// ============================================================
// OnComplete (0x1400e77c0) -- vtable slot 6
// Stores null IBP into the assigned citizen slot, updates state tracking.
// param4 encodes: bits [6:0] = slot index, bit 15 = update int array,
//   bits [14:8] = int array index, bit 16 = increment field_4c
// ============================================================
int ConsumeProduce::OnComplete(CitizenComponent** /*param2*/, ComponentEntity** /*param3*/, int arg3) {
    const uint32_t param4 = static_cast<uint32_t>(arg3);
    // Clear the citizen assignment: store null IBP into slot
    uint32_t slotIdx = param4 & 0x7F;
    field_58[slotIdx] = nullptr;   // the InternalBasePtr ctor with null

    // If bit 15 set, mark the int array slot as state 2 (completed)
    if ((param4 >> 15) & 1) {
        int* intArray = field_68._data;
        uint32_t intIdx = (param4 >> 8) & 0x7F;
        // TEARDOWN_WILD_WRITE.md: retail (0x1400e77c0) stores this 2 unbounded, and a
        // 4-mod-8 store of a 2 past a small int array is exactly the teardown damage.
        // Report an index past the array's own count BEFORE the faithful store below,
        // with everything needed to say how the index and the array came apart.
        if ((int)intIdx >= field_68._size || slotIdx >= (uint32_t)field_58._size) {
            std::fprintf(stderr,
                "[WILDWRITE] ConsumeProduce::OnComplete OUT OF RANGE: work=%p entity=%p "
                "workId=%d param4=0x%x -> posIdx=%u (count %d, cap %d, array %p) "
                "slotIdx=%u (count %d)\n",
                (void*)this, field_10.Get(), field_20, param4, intIdx,
                field_68._size, field_68._capacity, (void*)intArray, slotIdx, field_58._size);
            RedzoneAlloc_PrintBacktrace("ConsumeProduce::OnComplete");
        }
        intArray[intIdx] = 2;
    }

    // If bit 16 set, increment the produce counter
    if (param4 & 0x10000) {
        field_4c += 1;
    }

    return 0;
}

// ============================================================
// OnCancel (0x1400e7780) -- vtable slot 7
// Delegates to OnComplete with bit 16 cleared (no produce increment).
// ============================================================
int ConsumeProduce::OnCancel(CitizenComponent** param2, ComponentEntity** param3, int arg3) {
    const uint32_t param4 = static_cast<uint32_t>(arg3);
    // Retail (disassembly): copy *R8 and *RDX to stack slots, BTR R9D,0x10, then
    // dispatch vtable +0x30 = slot 6 OnComplete(&copy2, &copy3, R9) and return
    // its result.
    uint32_t modifiedParam4 = param4 & 0xFFFEFFFF;  // clear bit 16
    ComponentEntity* copy3 = *param3;
    CitizenComponent* copy2 = *param2;
    return OnComplete(&copy2, &copy3, static_cast<int>(modifiedParam4));
}

// ============================================================
// Cleanup (0x1400e8f30) -- vtable slot 8
// Iterates assigned citizens in the IBP array, clears their task state.
// ============================================================
int ConsumeProduce::Cleanup() {
    int count = field_58._size;
    if (count <= 0) return 0;

    Object::InternalPtr<CitizenComponent>* ibpArray = field_58._data;

    for (int i = 0; i < count; i++) {
        if (ibpArray[i] == nullptr) continue;

        // CheckPtr on the citizen IBP; its CharacterComponent (+0x278)
        CharacterComponent* character = ibpArray[i]->character.Get();

        // Call task list cleanup on the character's action list (+0x48)
        character->actionStack.Clear();
    }
    return 0;
}

// ============================================================
// Validate (0x1400e7530) -- vtable slot 9
// Checks if all assigned citizen tasks are still valid.
// Returns: 1 = all valid, 0 = some invalid (with cleanup).
// ============================================================
int ConsumeProduce::Validate() {
    int count = field_58._size;
    if (count <= 0) {
        // No assigned citizens -- return valid
        return 1;
    }

    Object::InternalPtr<CitizenComponent>* ibpArray = field_58._data;
    bool allValid = true;
    int validCount = 0;
    int* tempTypes = nullptr;  // dynamic array of type IDs per valid citizen
    int tempCapacity = 0;

    for (int i = 0; i < count; i++) {
        CitizenComponent* entityIBP = ibpArray[i].Get();
        if (entityIBP == nullptr) continue;

        // Validate task for this citizen
        CompleteTask* taskResult = nullptr;
        CitizenComponent* ibpBuf = entityIBP;
        uint8_t valid = ValidateTask(nullptr, &ibpBuf, &taskResult);   // 0x1400f7100

        if (valid == 0) {
            // Task invalid -- clear citizen from slot (the InternalBasePtr ctor with null)
            ibpArray[i] = nullptr;
            allValid = false;
        } else {
            // Task valid -- track the type for cross-referencing
            int typeTag = taskResult->taskParam;   // +0x34

            int newCount = validCount + 1;
            if (newCount != validCount) {
                int newCap = tempCapacity;
                if (tempCapacity < newCount) {
                    newCap = newCount;
                    if (validCount != 0) {
                        newCap = newCount * 3;
                    }
                    int* newArr = (int*)System::MemoryAllocator::MemAlloc((uint64_t)newCap * 4, 4);
                    // Copy old data if buffer changed
                    int startIdx = (tempTypes == newArr) ? validCount : 0;
                    for (int j = startIdx; j < newCount; j++) {
                        if (j < validCount && tempTypes != newArr) {
                            newArr[j] = tempTypes[j];
                        } else if (j >= validCount) {
                            newArr[j] = (typeTag >> 4) & 0xF;
                        }
                    }
                    if (tempTypes != newArr) {
                        System::MemoryAllocator::MemFree(tempTypes);
                    }
                    tempTypes = newArr;
                    tempCapacity = newCap;
                } else {
                    // Buffer has capacity -- just set the new element
                    if (tempTypes) {
                        tempTypes[validCount] = (typeTag >> 4) & 0xF;
                    }
                }
                validCount = newCount;
            }
        }
    }

    // If some citizens were invalidated, update the int array state
    if (!allValid) {
        int* intArray = field_68._data;
        int intCount = field_68._size;
        for (int k = 0; k < intCount; k++) {
            // Check if this slot's type is still present in valid citizens
            bool found = false;
            for (int j = 0; j < validCount; j++) {
                if (tempTypes[j] == k) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                // No valid citizen handles this slot -- mark as needs-redo
                if (intArray[k] == 1) {
                    intArray[k] = 2;
                }
            }
        }
    }

    System::MemoryAllocator::MemFree(tempTypes);
    return allValid ? 1 : 0;
}

// ============================================================
// GetTarget (0x1400e8fb0) -- vtable slot 10
// Returns the target position from the entity's position component.
// Hidden return ptr ABI: RCX=self, RDX=retbuf
// ============================================================
Profession** ConsumeProduce::GetTarget(Profession** retbuf) {
    // CheckPtr on the entity IBP at +0x10

    // Find the WorkPlaceComponent (type 0x1401c80e0) on the entity
    WorkPlaceComponent* workPlace = field_10->FindComponent<WorkPlaceComponent>();

    // CheckPtr on the found component

    // The workplace's profession (+0x48) into the return slot
    *retbuf = workPlace->profession;

    return retbuf;
}

// Slot11 (+0x58) — retail 0x14002a0f0 is `return 0 < *(int*)(self + 0x60)` (the shared
// body ClearStorage also uses: tree count at +0x60, not the +0x50 the generic
// Work_Slot11_HasActiveTasks reads). Was pinned to 0 by Work_SlotRet0 (2026-09-05).
int ConsumeProduce::GetPriority() {
    return field_58._size > 0 ? 1 : 0;
}

// Slot12 (+0x60) — retail 0x1400e9010, this class's own `RET 0` (every other Work
// table shares 0x140006b10). The same behaviour; declared so the table matches.
void ConsumeProduce::Slot12(int /*version*/) {}

// ============================================================
// Reflection, factory and the seam (ENDGAME B.3.8, B38_RULES D4/D5)
// ============================================================
// Before: a hand-built 13-slot `g_ConsumeProduce_Vtable` (slot 1 null, slot 2 a lazily
// cached id) and REGISTER_TYPE over ConsumeProduce_Factory / _CreateInPlace.

// create 0x14002a150: MemAlloc(sizeof, 8), then the ctor.
::Object::Base* ConsumeProduce::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(ConsumeProduce), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) ConsumeProduce());
}
// Retail's createInPlace is the imported Internal::CreateInPlaceObject (returns null);
// the tree's in-place entry built the object, and that is kept (B8-13).
::Object::Base* ConsumeProduce::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) ConsumeProduce());
}


#include "Object/factory_registration.h"

DEFINE_EXE_TYPE(ConsumeProduce, "Work")
