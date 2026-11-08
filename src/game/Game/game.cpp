// src/game/Game/game.cpp
// Clean reimpl of the exe-internal Game/KeyMap container ctor.
//
// Original: FUN_140001050 (Application-x64.exe).
// Decompiled body (paraphrased):
//   - zero param_1[0..0x14] (21 qwords)
//   - UI::Sprite::Sprite(param_1 + 0x15)   // 16 bytes at slot 0x15
//   - UI::Sprite::Sprite(param_1 + 0x17)   // 16 bytes at slot 0x17
//   - param_1[0x19] = param_1[0x1a] = param_1[0x1b] = 0
//   - g_pKeyMapSingleton = param_1
//
// Total struct size: 0x1c qwords = 0xe0 bytes. The slot index 0x15..0x18 holds
// two 16-byte UI::Sprite values.

#include "game.h"
#include "ui/UserInterface.h"
#include "object/External.h"
#include "object/Internal.h"
#include "object/TypeInfo.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Entity/CitizenBehavior.h"   // m_citizenList's class (B.4 GAME-a)
#include "Entity/ComponentEntity.h"
#include "Component/Component.h"
#include "system/MemoryAllocator.h"   // System::MemoryAllocator::MemAlloc / MemFree
#include "Component/StorageComponent.h"   // ENDGAME B.3.9 TR
#include "Component/RawMaterialDescription.h"   // ENDGAME B.3.9 TR
#include "Component/SelectDescription.h"   // ENDGAME B.3.9 TR
#include "Component/ComponentDescription.h"   // ENDGAME B.3.9 TR
#include <cstdint>
#include <cstring>
#include <new>
#include "ui/Sprite.h"   // ENDGAME B.3.6 UI callers (K4)
#include "Game/exe_bodies.h"
#include "Action/exe_bodies.h"   // StorageCanAdd, StorageAdd

extern "C" GameContainer* Game_Ctor(GameContainer* self) {
    // Zero the entire 0xe0-byte struct, including the regions where the
    // sprites will be initialized — Sprite default ctor is itself a 16-byte
    // zero-init in the engine, so this is safe.
    // (Raw memory: GameContainer is never constructed as a C++ object.)
    void* raw = self;
    std::memset(raw, 0, 0xe0);

    // Two embedded UI::Sprite default ctors at +0xA8 and +0xB8.
    ::new (&self->spriteWhite) UI::Sprite();           // B.3.6: value-type ctor in place (B6-10)
    ::new (&self->spriteSaveLoadEmpty) UI::Sprite();   // B.3.6: value-type ctor in place (B6-10)

    // Publish the container (DAT_1401c3de8, 0x1400010f1).
    g_gameContainer = self;
    return self;
}

// The exe cell 0x1401c3de8 (B3R STORAGE; game.h).
GameContainer* g_gameContainer = nullptr;

// 0x140091a20 — queue a ComponentDescription for removal: appended to the
// +0x120 list unless it is already there (the tree's Game_RemoveEntity).
void CitizenBehavior::RemoveEntity(ComponentDescription* entity) {
    // Check if entity is already in the removal queue
    int count = availableFarmTypes._size;
    ComponentDescription** arr = availableFarmTypes._data;
    for (int i = 0; i < count; i++) {
        if (arr[i] == entity) return;  // already queued
    }

    // Dynamic array push
    int newCount = count + 1;
    if (newCount != count) {
        ComponentDescription** oldArr = arr;
        availableFarmTypes._size = newCount;
        int capacity = availableFarmTypes._capacity;
        if (capacity < newCount) {
            // Grow: triple capacity if non-empty, else use newCount
            capacity = (count != 0) ? newCount * 3 : newCount;
            availableFarmTypes._capacity = capacity;
            availableFarmTypes._data = static_cast<ComponentDescription**>(
                System::MemoryAllocator::MemAlloc((uint64_t)capacity * 8, 8));
        }
        ComponentDescription** newArr = availableFarmTypes._data;
        int copyStart = (oldArr == newArr) ? count : 0;
        for (int i = copyStart; i < newCount; i++) {
            if (i < count) {
                newArr[i] = oldArr[i];  // copy existing
            } else {
                newArr[i] = entity;     // append new
            }
        }
        if (oldArr != newArr) {
            System::MemoryAllocator::MemFree(oldArr);
        }
    }
}

// moved from engine_funcs.h (ENDGAME B.3.9 TR): reads X::TypeId() of the cell's class.
void Game_TriggerDisasterByType_Impl(int disasterType) {
    if (!::Object::ExternalFactory::_singleton) return;   // ENDGAME B.3.9 SS2 (merged)

    const ::Object::ExternalFactory* efactory = ::Object::ExternalFactory::_singleton;
    GameUpdateLoop* gameWorld = GameUpdateLoop::_singleton;
    if (!gameWorld) return;
    CitizenBehavior* citizenList = gameWorld->m_citizenList;   // gw+0xf0

    // Iterate all ComponentDescription-type externals
    ::Object::External* ext =
        efactory->FindNextAs<::Object::External>(ComponentDescription::TypeId(), nullptr);
    while (ext != nullptr) {
        if (::Object::IsDerivedFromNullSafe(ext, ComponentDescription::TypeId())) {
            ComponentDescription* desc = static_cast<ComponentDescription*>(ext);
            // Find SelectDescription sub-component
            int compCount = desc->_subDescCount;    // +0x30
            Description** compArray = desc->_subDescs;   // +0x28

            // First pass: check if entity HAS a SelectDescription
            bool hasSelect = false;
            for (int i = 0; i < compCount; i++) {
                const Description* comp = compArray[i];
                uint32_t typeId = comp->Type();   // slot 2
                if (typeId == SelectDescription::TypeId()) {
                    if (::Object::IsDerivedFromNullSafe(comp, SelectDescription::TypeId()) && comp != nullptr) {
                        hasSelect = true;
                    }
                    break;
                }
            }

            if (hasSelect) {
                // Second pass: find the SelectDescription and check type
                const SelectDescription* selectDesc = nullptr;
                for (int i = 0; i < compCount; i++) {
                    const Description* comp = compArray[i];
                    uint32_t typeId = comp->Type();   // slot 2
                    if (typeId == SelectDescription::TypeId()) {
                        if (::Object::IsDerivedFromNullSafe(comp, SelectDescription::TypeId())) {
                            selectDesc = static_cast<const SelectDescription*>(comp);
                        }
                        break;
                    }
                }
                // Check if SelectDescription.type (+0x40) matches requested disaster type
                if (selectDesc && static_cast<int>(selectDesc->_group) == disasterType) {
                    citizenList->RemoveEntity(desc);   // 0x140091a20
                }
            }
        }
        ext = efactory->FindNextAs<::Object::External>(ComponentDescription::TypeId(), ext);
    }
}

void CitizenBehavior::SpawnResources(uint32_t resourceFlag, int count) {

    // Dynamic array for matching entities
    // Ghidra uses local_48 = array ptr, local_40 = packed CONCAT44(capacity, count)
    ComponentDescription** matchArr = nullptr;
    int matchCount = 0;
    int matchCapacity = 0;

    // Phase 1: Build array of entity pointers whose RawMaterialDescription flags match
    // resourceFlag. The description array is +0x110 (data) / +0x118 (count).
    if (knownRawMaterials._size > 0) {
        int entityIdx = 0;
        do {
            ComponentDescription* entityDesc = knownRawMaterials._data[entityIdx];
            ComponentDescription* entity = entityDesc;

            // Find the RawMaterialDescription among its sub-descriptions (+0x28/+0x30)
            const RawMaterialDescription* natResComp = nullptr;
            int compIdx = 0;
            if (entityDesc->_subDescCount > 0) {
                do {
                    const Description* comp = entityDesc->_subDescs[compIdx];
                    uint32_t typeId = comp->Type();   // slot 2
                    if (typeId == RawMaterialDescription::TypeId()) {
                        bool isDerived = ::Object::IsDerivedFromNullSafe(comp, RawMaterialDescription::TypeId());
                        natResComp = nullptr;
                        if (isDerived) {
                            natResComp = static_cast<const RawMaterialDescription*>(comp);
                        }
                        goto phase1_found;
                    }
                    compIdx++;
                } while (compIdx < entityDesc->_subDescCount);
            }
            natResComp = nullptr;

        phase1_found:
            {
                // Check resource flags match (natResComp+0x5c & resourceFlag)
                // Original code does NOT null-check — relies on entities always having the component
                ComponentDescription** oldArr = matchArr;
                int oldCount = matchCount;
                if ((natResComp->_flags & resourceFlag) != 0) {
                    int newCount = matchCount + 1;
                    if (newCount != matchCount) {  // overflow guard (matches original JZ)
                        matchCount = newCount;
                        if (matchCapacity < newCount) {
                            // Growth: if first alloc, capacity = newCount; else capacity = newCount * 3
                            matchCapacity = newCount;
                            if (oldArr != nullptr) {
                                matchCapacity = newCount * 3;
                            }
                            matchArr = static_cast<ComponentDescription**>(
                                System::MemoryAllocator::MemAlloc((uint64_t)matchCapacity * 8, 8));
                        }

                        // Copy old entries + append new entity
                        // If matchArr == oldArr (no realloc needed), start from old count (just append)
                        // If matchArr != oldArr (reallocated), start from 0 (copy all + append)
                        uint32_t copyStart = 0;
                        if (oldArr == matchArr) {
                            copyStart = (uint32_t)oldCount;
                        }
                        int copyIdx = (int)copyStart;
                        if (copyIdx < newCount) {
                            ComponentDescription** dstSlot = matchArr + copyIdx;
                            ComponentDescription** srcSlot = oldArr + copyIdx;
                            do {
                                if (dstSlot != nullptr) {
                                    ComponentDescription** source = &entity;
                                    if ((int)copyStart < oldCount) {
                                        source = srcSlot;
                                    }
                                    *dstSlot = *source;
                                }
                                copyStart++;
                                srcSlot++;
                                dstSlot++;
                            } while ((int)copyStart < newCount);
                        }

                        // Free old array if it was reallocated
                        if (oldArr != matchArr) {
                            System::MemoryAllocator::MemFree(oldArr);
                        }
                    }
                }
            }

            entityIdx++;
        } while (entityIdx < knownRawMaterials._size);

        // Phase 2: Round-robin distribute items across citizens' StorageComponents
        // Citizens are InternalBasePtr entries: +0x00 = array ptr, +0x08 = count
        if (matchCount > 0 && count > 0) {
            int spawned = 0;

            while (true) {
                bool anyAdded = false;
                int citizenIdx = 0;
                if (storageLocations._size < 1) break;

                do {
                    // Each citizen entry is an InternalBasePtr — load then CheckPtr
                    ComponentEntity* citizen = storageLocations._data[citizenIdx].Get();

                    // Find StorageComponent on citizen
                    // Citizen component array is IBP entries at citizen+0x30, count at citizen+0x38
                    StorageComponent* storageComp = nullptr;
                    int cCompIdx = 0;
                    if (citizen->_components._size > 0) {
                        do {
                            // Each component entry is an InternalBasePtr — CheckPtr before reading
                            const Object::InternalPtr<Component>* compIBP = &citizen->_components[cCompIdx];

                            uint32_t typeId = compIBP->Get()->Type();   // slot 2
                            if (typeId == StorageComponent::TypeId()) {
                                // Re-CheckPtr the IBP slot and read again
                                const Object::InternalPtr<Component>* compIBP2 = &citizen->_components[cCompIdx];
                                Component* compVal = compIBP2->Get();

                                bool isDerived = ::Object::IsDerivedFromNullSafe(compVal, StorageComponent::TypeId());
                                storageComp = nullptr;
                                if (isDerived) {
                                    storageComp = static_cast<StorageComponent*>(compVal);
                                }
                                goto phase2_found;
                            }
                            cCompIdx++;
                        } while (cCompIdx < citizen->_components._size);
                    }
                    storageComp = nullptr;

                phase2_found:
                    {
                        StorageComponent* storageIBP = storageComp;

                        // Try to add one item via round-robin
                        if (spawned < count) {
                            ComponentDescription** matchSlot = matchArr + (spawned % matchCount);

                            // CheckPtr the storage IBP before use

                            // Call original exe CanAdd function (0x14012cd80)
                            uint32_t canAdd = storageIBP->CanAdd(*matchSlot, 1);   // 0x14012cd80
                            if ((char)canAdd != '\0') {
                                // CheckPtr again before AddItem

                                // Call original exe AddItem function (0x14012d000)
                                storageIBP->AddItem(*matchSlot, 1);   // 0x14012d000
                                spawned++;
                                anyAdded = true;
                            }
                        }
                    }

                    citizenIdx++;
                } while (citizenIdx < storageLocations._size);

                if (!anyAdded || spawned >= count) break;
            }
        }
    }

    // Free match array
    System::MemoryAllocator::MemFree(matchArr);
}
