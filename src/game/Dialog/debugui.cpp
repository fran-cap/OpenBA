// src/game/Dialog/debugui.cpp
// Clean reimplementation of DebugUI panel methods.
//
// DebugUI (0x130 bytes) -- developer debug/cheat panel controller.
// Extends DialogController (0x30 base).
// Contains resource spawn buttons, disease/weather controls, rendering
// debug toggles, lighting color sliders, and performance counters.
//
// Decompiled from Application-x64.exe, with element names recovered
// from Application-x64-profile.exe debug strings.
//
// Slot4 (setup), Slot7, Slot8, and Slot9 are cleanly reimplemented here.

// The runtime type headers come FIRST: they pull src/engine/volume/Volume.h,
// whose global-scope `struct Rectangle` is hidden by <wingdi.h>'s `Rectangle()`
// once <windows.h> has been seen through resolve.h.  ENDGAME B.3.5 batch I2.
#include "ui/TextElement.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/ComboElement.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/NumberElement.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/Element.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/String.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/StringTable.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/Sprite.h"   // ENDGAME B.3.6 UI callers (K2)
#include <new>   // ENDGAME B.3.6 UI callers (K2)
#include "graphics/Scene.h"
#include "Dialog/ui_panels.h"
#include "Dialog/debugui_funcs.h"
#include "ui/UserInterface.h"
#include "ui/DialogInstance.h"
#include "object/TypeInfo.h"
#include "graphics/Camera.h"
#include "Entity/exe_bodies.h"
#include "Game/exe_bodies.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Component/AmbientEmitterComponent.h"   // the weather sound entity's emitters
#include "Component/ComponentDescription.h"
#include "Entity/ComponentEntity.h"
#include "Entity/EntityList.h"
#include "math/Math.h"
#include "system/String.h"
#include "system/Filename.h"
#include "object/External.h"   // ENDGAME B.3.3 step 6A
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "system/Value.h"      // ENDGAME B.3.4b System callers: System::Time / System::Random
#include "audio/Audio.h"       // ENDGAME B.3.7 callers (K4)
#include "Object/factory_registration.h"
#include <cstddef>
#include "Component/CitizenComponent.h"   // X::TypeId() (B.3.8 callers)
#include "Component/ComponentDescription.h"   // X::TypeId() (ENDGAME B.3.9)
#include "Component/RawMaterialDescription.h"   // X::TypeId() (ENDGAME B.3.9)
#include "Component/InteractComponent.h"   // X::TypeId() (ENDGAME B.3.9)
#include "Component/InteractDescription.h"   // X::TypeId() (ENDGAME B.3.9)
#include "Component/CitizenDescription.h"   // X::TypeId() (ENDGAME B.3.9)
#include "Game/Disease.h"   // X::TypeId() (ENDGAME B.3.9)
#include "Component/PopulationDescription.h"   // X::TypeId() (ENDGAME B.3.9)
#include "engine/MainLoop.h"   // ENDGAME B.3.9 SS2: Engine::MainLoop::_singleton
#include "video/Context.h"     // Video::Context's submit counters (+0x2150..+0x215C)
#include "Component/exe_bodies.h"             // B3R: the bodies called by name
#include "Action/exe_bodies.h"                // B3R: Pf_RecursiveChildSum_123070
#include "Entity/ComponentEntity.h"   // ComponentEntity(ComponentDescription*) (0x1400c3720)
#include "Entity/EntityList.h"        // EntityList::RegisterEntity (0x1400c48a0)
#include "Component/firecomponent_funcs.h"    // B3R: FireComponent_Ignite_610
#include "Component/diseasecomponent_funcs.h" // B3R: DiseaseComp_Infect
#include "Entity/CitizenBehavior.h"            // B4 DLG-a: the town lists
#include "Component/PastureComponent.h"
#include "Component/PopulationComponent.h"
#include "system/MemoryAllocator.h"   // B3R: the allocator doors
#include "ui/SliderElement.h"          // B4 DLG-a: the typed DebugUI fields
#include "ui/CheckElement.h"
#include "ui/ImageElement.h"
#include "ui/ButtonElement.h"
#include "Entity/ComponentEntity.h"    // FindComponent<T>
#include "Component/FireComponent.h"
#include "Component/EnvironmentDescription.h"
#include "Game/WeatherParticleGeometry.h"
#include "Game/ProfessionList.h"
#include "Game/Calendar.h"              // GameUpdateLoop::m_dayCounter
#include "ext/registry.h"                 // Ext::Fix().debug_spawn_origin
#include "ext/debug_spawn_origin/debug_spawn_origin.h"
#include "ext/debug_spawn_origin/debug_spawn_origin_rule.h"
#include "Game/WeatherSimulation.h"     // GameUpdateLoop::m_weather
#include "Game/Weather.h"               // EnvironmentDescription::WeatherChoice::weather
#include "Component/DiseaseComponent.h" // DiseaseComponent::Infect
#include "Component/Description.h"      // Description::Type

// ============================================================
// Static helpers (no DebugUI field is read through their first argument)
// ============================================================

// Game_GetMonthFromTime (FUN_1400932A0)
// Converts the day counter's time to a month index 0-11.
// param_1 = the day counter (GameUpdateLoop +0x128, the Calendar); its
// yearTime is at +8.
static int DebugUI_GetMonthFromTime_Impl(const Calendar* weatherMgr) {
    float seconds = weatherMgr->yearTime.AsSeconds();
    int month = (int)((seconds / 3600.0f) * 12.0f);  // 3600s/yr (DAT_14017225c), 12 months
    if (month > 11) month = 0;
    return month;
}

// The weather simulation (GameUpdateLoop +0x130, 0x78 bytes; Game/WeatherSimulation.h):
// +0x00 the ExternalPtr<EnvironmentDescription> (the Environment.rsc keyframe
// table), +0x10 the WeatherType (weatherType), +0x50 the per-weather particle geometries.

// DebugUI::SetDayLighting (FUN_140061780)
// Reads one month's lighting keyframe from the environment, writes the sliders.
// The keyframe is EnvironmentDescription::_months[month] (+0x38 + month*0x20):
// sun = the dword at +0x04 (R at byte 2, G byte 1, B byte 0), ambient = +0x00,
// fog = +0x08; fog near / far / max at +0x0c / +0x10 / +0x14; sun heading /
// pitch at +0x18 / +0x1c.
void DebugUI::SetDayLighting(int month) {
    // weatherTable = **(gameWorld+0x130) == envRes (null if Environment.rsc
    // failed to load; guard so the reads below don't hit a wild low address).
    WeatherSimulation* weatherSim = GameUpdateLoop::_singleton->m_weather;
    EnvironmentDescription* env = weatherSim ? weatherSim->environment : nullptr;
    if (!env) return;
    const EnvironmentDescription::WeatherEntry& entry = env->_months[month];

    valueSunR->_value = (float)(uint8_t)(entry.lightColor >> 16);   // sunR  (+0x3e)
    valueSunG->_value = (float)(uint8_t)(entry.lightColor >> 8);    // sunG  (+0x3d)
    valueSunB->_value = (float)(uint8_t)entry.lightColor;           // sunB  (+0x3c)
    valueSunH->_value = entry.sunHeading;                              // sunH  (+0x50)
    valueSunP->_value = entry.sunPitch;                             // sunP  (+0x54)
    valueAmbR->_value = (float)(uint8_t)(entry.ambientColor >> 16);      // ambR  (+0x3a)
    valueAmbG->_value = (float)(uint8_t)(entry.ambientColor >> 8);       // ambG  (+0x39)
    valueAmbB->_value = (float)(uint8_t)entry.ambientColor;              // ambB  (+0x38)
    valueFogR->_value = (float)(uint8_t)(entry.fogColor >> 16);      // fogR  (+0x42)
    valueFogG->_value = (float)(uint8_t)(entry.fogColor >> 8);       // fogG  (+0x41)
    valueFogB->_value = (float)(uint8_t)entry.fogColor;              // fogB  (+0x40)
    valueFogN->_value = entry.fogNear;                              // fogN  (+0x44)
    valueFogF->_value = entry.fogFar;                                // fogF  (+0x48)
    valueFogM->_value = entry.fogMax * 255.0f;                 // fogM  (+0x4c, normalize)
}

// DebugUI::WriteLightingColors (FUN_140061530)
// Reads the sliders, writes packed ARGB colours into the month's keyframe.
// Inverse of SetDayLighting: slider -> keyframe.
void DebugUI::WriteLightingColors(int month) {
    // weatherTable == *(weatherSim+0) == envRes (Environment.rsc keyframe table).
    // Null if Environment.rsc failed to load; without this guard the per-month
    // writes below would target (0 + month*0x20) — a wild low-address store.
    WeatherSimulation* weatherSim = GameUpdateLoop::_singleton->m_weather;
    EnvironmentDescription* env = weatherSim ? weatherSim->environment : nullptr;
    if (!env) return;
    EnvironmentDescription::WeatherEntry& entry = env->_months[month];

    float sunR = valueSunR->_value;
    float sunG = valueSunG->_value;
    // Pack sun ARGB at entry+0x3c
    entry.lightColor = ((int)sunR << 8 | (int)sunG) << 8
        | (int)valueSunB->_value | 0xFF000000;

    float ambR = valueAmbR->_value;
    float ambG = valueAmbG->_value;
    // Pack amb ARGB at entry+0x38
    entry.ambientColor = ((int)ambR << 8 | (int)ambG) << 8
        | (int)valueAmbB->_value | 0xFF000000;

    float fogR = valueFogR->_value;
    float fogG = valueFogG->_value;
    // Pack fog ARGB at entry+0x40
    entry.fogColor = ((int)fogR << 8 | (int)fogG) << 8
        | (int)valueFogB->_value | 0xFF000000;

    entry.sunHeading     = valueSunH->_value;           // sunH (+0x50)
    entry.sunPitch    = valueSunP->_value;           // sunP (+0x54)
    entry.fogNear     = valueFogN->_value;           // fogN (+0x44)
    entry.fogFar       = valueFogF->_value;           // fogF (+0x48)
    entry.fogMax = valueFogM->_value / 255.0f;  // fogM (+0x4c)
}

// DebugUI::OnDaySliderChanged (FUN_1400607E0)
// Reads slider value (0-359), converts to game time, resets weather, sets lighting.
// DAT_140172238 = 360.0f (slider range), DAT_14017225c = 3600.0f (seconds/year)
// DAT_1401721cc = 30.0f (days/month for month index calc)
void DebugUI::OnDaySliderChanged() {
    GameUpdateLoop* gameWorld = GameUpdateLoop::_singleton;
    float sliderVal = sliderDay->_value;

    // Convert slider (0-359) to game time in ticks
    System::Time time;
    RtSystem_Time_Ctor(&time, (sliderVal / 360.0f) * 3600.0f);

    // Set game world time (the Calendar's yearTime at +8)
    gameWorld->m_dayCounter->yearTime._ticks = time._ticks;

    // Reset weather time to 0
    System::Time zeroTime;
    RtSystem_Time_Ctor(&zeroTime, 0.0f);
    GameUpdateLoop::_singleton->m_weather->Update(&zeroTime);   // 0x1400944e0

    // Update lighting sliders for the new month
    int monthIdx = (int)(sliderVal / 30.0f);  // 30 days/month
    SetDayLighting(monthIdx);
}

// DebugUI::OnDrawComboChanged (FUN_14005FC60)
// Element 0x166, the drawCombo (+0x38): retail reads the selected row
// (`CMP EDI,0x4 ; JL`: rows 0..3 are not population entries), fetches its item
// data and builds an InternalBasePtr from it that nothing reads (a bare
// `mov [rcx],rdx` into a stack slot, 0x14005fca1) — the call has no effect.
void DebugUI::OnDrawComboChanged() {
    int selIdx = drawCombo->_currentItem;  // selected item index
    if (selIdx > 3) {
        void* itemData = drawCombo->GetItemData(selIdx);
        (void)itemData;   // retail's discarded InternalBasePtr(itemData)
    }
}

// ============================================================
// DebugUI::OnWeatherComboChanged (FUN_1400608A0)
// Element 0x163, the comboWeather (+0x108): shows the selected weather's
// particle geometry and fades every other one out.
//
// The weather simulation (world+0x130), read as it has no class:
//   +0x00 = ExternalPtr<EnvironmentDescription>: _weatherArray (0x10 stride:
//           {weather descriptor, audio descriptor}), _weatherCount
//   +0x10 = state field (int, set to 0 on show, 3 on hide)
//   +0x50 = WeatherParticleGeometry* per weather entry
//   +0x60 = the sound entity per weather entry (InternalPtr<ComponentEntity>)
//
// Original: 0x1400608A0
// ============================================================
void DebugUI::OnWeatherComboChanged() {
    // The DrawGeometry ctor / init are 0x1400d3f10 WeatherParticle_GeoCtor and
    // 0x1400d3f80 WeatherParticle_Config, called by name (B3R; the old
    // "not reimplemented" bail-out never ran: both are registered).

    // Get selected combo index
    int selectedIdx = comboWeather->_currentItem;

    // Item data is the weather index itself (AddItem stored `(void*)i`).
    void* itemData = comboWeather->GetItemData(selectedIdx);
    int selectedDrawIdx = (int)(intptr_t)itemData;  // CAST-OK: ptr_int

    // The weather simulation from the game world
    GameUpdateLoop* world = GameUpdateLoop::_singleton;
    WeatherSimulation* drawMgr = world->m_weather;  // R14 = plVar1

    EnvironmentDescription* env = drawMgr->environment;  // *plVar1
    int drawCount = (int)env->_weatherArray._size;
    EnvironmentDescription::WeatherChoice* descArray = env->_weatherArray._data;  // {descriptor, audio descriptor} pairs

    Graphics::Geometry** geometryArray = drawMgr->effects._data;  // R14+0x50 (WeatherParticleGeometry each)
    // R14+0x60 = the sound entity per entry (drawMgr->emitters), written by the audio
    // branch below.

    for (int i = 0; i < drawCount; i++) {
        if (selectedDrawIdx == i) {
            // Selected entry — create and show geometry if not already created
            if (geometryArray[i] == nullptr) {
                // Allocate and construct DrawGeometry (0xf0 bytes)
                void* geomMem = System::MemoryAllocator::MemAlloc(0xf0, 8);
                WeatherParticleGeometry* geometry = nullptr;
                if (geomMem) {
                    // 0x1400d3f10, the ctor (WeatherParticle_GeoCtor's body; its null
                    // test is the `if` above)
                    geometry = ::new (geomMem) WeatherParticleGeometry();
                }
                geometryArray[i] = geometry;

                // Initialize with descriptor data
                Weather* descPtr = descArray[i].weather;
                static_cast<WeatherParticleGeometry*>(geometryArray[i])->Configure(descPtr);   // 0x1400d3f80

                // Add to graphics scene
                Graphics::Scene* graphicsScene = world->m_graphicsScene;
                graphicsScene->AddGlobalObject(geometryArray[i]);

                // Showing: the weather is Rain
                drawMgr->weatherType = WeatherType::Rain;

                // 0x1400609a4-0x140060b4f: an entry with a sound template (the
                // WeatherChoice's description) gets its sound entity: built from the
                // template (MemAlloc(0x48) + 0x1400c3720), stored in emitters[i], and
                // registered with the entity list under the template's category.
                ComponentDescription* audioDescPtr = descArray[i].description;
                if (audioDescPtr != nullptr) {
                    void* entMem = System::MemoryAllocator::MemAlloc(0x48, 8);
                    ComponentEntity* soundEntity = entMem
                        ? ::new (entMem) ComponentEntity(descArray[i].description)   // 0x1400c3720
                        : nullptr;
                    drawMgr->emitters._data[i] = soundEntity;
                    Object::InternalPtr<ComponentEntity> listIBP = drawMgr->emitters._data[i];
                    world->m_entityList->RegisterEntity(&listIBP,   // 0x1400c48a0
                        static_cast<int>(descArray[i].description->_category));

                    // 0x140060a5f: FindComponent<AmbientEmitterComponent> (0x140002280),
                    // read unguarded. Every live emitter moves to the origin (MoveTo's
                    // inlined body, 0x140060a64-0x140060afe), then slot [0] is muted:
                    // volume 0, SetEmitterVolume on a live handle whose volume was not 0.
                    AmbientEmitterComponent* audio =
                        drawMgr->emitters._data[i]->FindComponent<AmbientEmitterComponent>();
                    audio->MoveTo(Math::Vector3(0.0f, 0.0f, 0.0f));   // 0x140042960
                    AmbientEmitterSlot* slot0 = audio->slots._data;
                    if (slot0->handle == 0) {
                        slot0->volume = 0.0f;
                    } else if (slot0->volume != 0.0f) {
                        slot0->volume = 0.0f;
                        world->m_audioScene->SetEmitterVolume(slot0->handle, audio->position, 0.0f);
                    }
                }
            }
        } else {
            // Non-selected entry — hide geometry if it exists
            WeatherParticleGeometry* geometry = static_cast<WeatherParticleGeometry*>(geometryArray[i]);
            if (geometry != nullptr) {
                // Fade direction -1.0f (0xbf800000) to hide
                geometry->_fadeDirection = -1.0f;
                // Hiding: the weather is Sunny
                drawMgr->weatherType = WeatherType::Sunny;
            }
        }
    }
}

// SetEngineNextLoop — now inline in engine_funcs.h as SetEngineNextLoop()
// (was static SetEngineNextLoop_Impl here, promoted to shared inline)

// ============================================================
// Game_DestroyAllRawMaterials (FUN_140060000)
// Iterates ExternalFactory for all entities with ComponentDescription (DAT_1401c7318).
// For each entity, searches components for RawMaterialDescription (DAT_1401c7cb8).
// If found, destroys the entity via Game_DestroyEntity.
// ============================================================
static void Game_DestroyAllRawMaterials_Impl() {
    ::Object::ExternalFactory* factory = ::Object::ExternalFactory::_singleton;

    // Iterate all entities with ComponentDescription type
    ::Object::External* entity =
        factory->FindNextAs<::Object::External>(ComponentDescription::TypeId(), nullptr);
    while (entity) {
        // IsDerivedFrom check against ComponentDescription
        bool isBuild = ::Object::IsDerivedFromNullSafe(entity, ComponentDescription::TypeId());
        ComponentDescription* buildEntity =
            isBuild ? static_cast<ComponentDescription*>(entity) : nullptr;

        // Search entity's sub-descriptions for RawMaterialDescription
        int compCount = buildEntity->_subDescCount;
        Description** compArray = buildEntity->_subDescs;   // COMP-c typed _subDescs (merge fix)

        for (int i = 0; i < compCount; i++) {
            Description* comp = compArray[i];
            // Get component type via vtable[2] (+0x10)
            uint32_t typeId = comp->Type();
            if (typeId == RawMaterialDescription::TypeId()) {
                // Re-read component pointer from array and confirm with IsDerivedFrom
                Description* rawComp = compArray[i];
                bool isRawMat = ::Object::IsDerivedFromNullSafe(rawComp, RawMaterialDescription::TypeId());
                if (isRawMat && rawComp != nullptr) {
                    // Destroy the entity (re-read the list from the global each time)
                    // The door was named fn_Game_DestroyEntity; it is bound to
                    // 0x140091e60 RawMaterialComponent_RegisterRawMaterial (B3R: the
                    // binding decides, INVENTORY_E §5.8).
                    // (RawMaterialComponent_RegisterRawMaterial's member.)
                    GameUpdateLoop::_singleton->m_citizenList->RegisterRawMaterial(buildEntity);
                }
                break;
            }
        }

        entity = factory->FindNextAs<::Object::External>(ComponentDescription::TypeId(), entity);
    }
}

// The town lists (GameUpdateLoop +0xF0).
static CitizenBehavior* DebugUI_Town() {
    return GameUpdateLoop::_singleton->m_citizenList;
}

// ============================================================
// Game_SpawnItemsInStorage (FUN_140060290)
// Iterates the storage list at gameWorld+0xF0, sub-list at +0x90/+0x98.
// For each storage, spawns up to 5 items if current count < capacity.
// The lists are CitizenBehavior's pastures (+0x90 / +0x98).
// ============================================================
static void Game_SpawnItemsInStorage_Impl() {
    CitizenBehavior* town = DebugUI_Town();

    int storageCount = town->pastures._size;
    if (storageCount <= 0) return;

    for (int i = 0; i < storageCount; i++) {
        PastureComponent* storage = town->pastures._data[i].Get();

        // Check if storage has items to spawn (component+0x28 != 0)
        if (storage->livestockType == nullptr) continue;

        for (int j = 0; j < 5; j++) {
            int currentCount = storage->animals._size;
            // 0x140128570 PastureComponent::ComputeCapacity (the forwarder
            // PastureComponent_ComputeCapacity zero-extended its int; this reads the int).
            int capacity = storage->ComputeCapacity();
            if (capacity <= currentCount) break;

            // Spawn one item
            AnimalComponent* noParent = nullptr;   // the InternalBasePtr(nullptr) retail builds
            storage->SpawnLivestock(&noParent);   // 0x140128670 (door misnamed fn_StorageComponent_SpawnItem)
        }
    }
}

// ============================================================
// Game_SpawnCitizens' new citizen (inlined in retail 0x140060380): a homeless citizen of
// random gender and age placed at `position` and registered. Shared by the use-node path and
// fix.debug_spawn_origin's origin path, so both make the same citizen.
// ============================================================
static void Game_SpawnCitizenAt(System::Random* random, const Math::Vector3* position) {
    // Random gender
    uint32_t gender = random->Rand(static_cast<uint32_t>(2));
    uint32_t genderFlag = (gender != 0) ? 1 : 0;

    // Load citizen template
    System::Filename filenameBuf("Template/Citizen.rsc");
    ::Object::External* extTemplate =
        ::Object::ExternalFactory::_singleton->FindOrLoadAs<::Object::External>(filenameBuf);

    // Determine entity description (RDI in disasm)
    // If extTemplate is null, use null.
    // If non-null, IsDerivedFrom check; use result (may be null).
    ComponentDescription* entityDesc = nullptr;
    if (extTemplate == nullptr) {
        entityDesc = nullptr;
    } else {
        bool isBuildDesc = ::Object::IsDerivedFromNullSafe(extTemplate, ComponentDescription::TypeId());
        entityDesc = isBuildDesc ? static_cast<ComponentDescription*>(extTemplate) : nullptr;
    }

    // Allocate and create entity (0x48 bytes)
    ComponentEntity* memBlock = static_cast<ComponentEntity*>(System::MemoryAllocator::MemAlloc(0x48, 8));
    ComponentEntity* newEntity = nullptr;
    if (memBlock != nullptr) {
        newEntity = ::new (memBlock) ComponentEntity(entityDesc);   // 0x1400c3720
    }

    // Find the CitizenComponent (DAT_1401c7158) on the new entity:
    // FindComponent<CitizenComponent> inlined in retail.
    CitizenComponent* citizen = newEntity->FindComponent<CitizenComponent>();

    // Get CitizenDescription (DAT_1401c7190) from Component::_description
    Description* citDescRaw = citizen->_description;
    bool isCitDesc = ::Object::IsDerivedFromNullSafe(citDescRaw, CitizenDescription::TypeId());
    CitizenDescription* citDesc = isCitDesc ? static_cast<CitizenDescription*>(citDescRaw) : nullptr;

    // Get profession list from game (re-read global)
    Profession* profList = GameUpdateLoop::_singleton->m_professionList->laborerProfession;

    // Null parent InternalBasePtr
    CitizenComponent* parentIBP = nullptr;

    // Read age range from CitizenDescription (+0x50 / +0x54)
    float minAge = citDesc->minPregnancyAge;
    float randAge = random->RandFloat();
    float maxAge = citDesc->maxPregnancyAge;
    float age = randAge * (maxAge - minAge) + minAge;

    citizen->Initialize(position,
                        genderFlag, '\0',
                        age, '\x01', parentIBP,
                        profList);   // 0x140117080

    // Register entity with entity manager
    // Use the new entity (the InternalBasePtr value) as the entity ref
    Object::InternalPtr<ComponentEntity> entityRef = newEntity;
    GameUpdateLoop::_singleton->m_entityList->RegisterEntity(&entityRef,
                                                             entityDesc->_category);   // 0x1400c48a0
}

// ============================================================
// Game_SpawnCitizens (FUN_140060380)
// Spawns up to 4 new citizens at buildings that have residence
// components with available capacity. Iterates all buildings,
// spawning multiple citizens per building until full or 4 total.
// ============================================================
static void Game_SpawnCitizens_Impl() {
    GameUpdateLoop* game = GameUpdateLoop::_singleton;
    System::Random* random = game->m_random;
    // The building list: CitizenBehavior +0x00 / +0x08.
    CitizenBehavior* town = DebugUI_Town();
    int buildingCount = town->storageLocations._size;

    if (buildingCount == 0) return;

    int spawnedTotal = 0; // R13D — total citizens spawned across all buildings
    do {
        int placedThisPass = 0;   // NOT RETAIL: fix.debug_spawn_origin's test below
        // Inner loop: scan all buildings
        int buildIdx = 0; // local_res10
        if (buildingCount > 0) {
            do {
                // The building (the array is re-read per pass)
                ComponentEntity* building = town->storageLocations._data[buildIdx].Get();

                if (building != nullptr) {

                    // Find the InteractComponent (DAT_1401c7900) on this building:
                    // FindComponent<InteractComponent> inlined in retail.
                    InteractComponent* resComp = building->FindComponent<InteractComponent>();

                    if (resComp != nullptr) {
                        // Inner while(true) loop: spawn citizens at this building until full
                        int spawnedHere = 0; // R12D — per-building spawn counter
                        while (true) {
                            // Get the InteractDescription from Component::_description (+0x18)
                            Description* desc = resComp->_description;
                            bool isResDesc = ::Object::IsDerivedFromNullSafe(desc, InteractDescription::TypeId());
                            InteractDescription* resDesc = isResDesc ? static_cast<InteractDescription*>(desc) : nullptr;

                            // Capacity: the use-node count (+0x30)
                            // GetOccupantCount returns values that add to this
                            int capacityAccum = resDesc->_useNodes.count;

                            // Sum occupant counts from the use locations (+0x28 / +0x30)
                            int childCount = resComp->useLocations._size;
                            if (childCount > 0) {
                                Object::InternalPtr<InteractComponent>* childArrayBase = resComp->useLocations._data;
                                int si = 0;
                                do {
                                    const Object::InternalPtr<InteractComponent>* pChild = childArrayBase + si;
                                    // 0x140123070 InteractNodeCount (the forwarder
                                    // Pf_RecursiveChildSum_123070's body; door misnamed
                                    // fn_ResidenceComponent_GetOccupantCount)
                                    int occResult = pChild->Get()->NodeCount(0);
                                    capacityAccum += occResult;
                                    si++;
                                } while (si < childCount);
                            }

                            // Break if no more room: spawnedHere >= capacityAccum
                            if (spawnedHere >= capacityAccum) break;

                            // Get spawn point for this slot
                            Vector3Slot spawnPos;
                            // 0x140122e20 Pf_EntityResolvePos_2e20 (door misnamed
                            // fn_ResidenceComponent_GetSpawnPoint)
                            resComp->NodePosition(&spawnPos.v, 0, spawnedHere, nullptr);

                            // Check if we've reached the global limit of 4
                            if (spawnedTotal >= 4) {
                                // Original doesn't exit; it just skips spawn and
                                // increments spawnedHere until capacity is hit
                                spawnedHere++;
                                continue;
                            }

                            spawnedTotal++;
                            Game_SpawnCitizenAt(random, &spawnPos.v);
                            placedThisPass++;
                            spawnedHere++;
                        } // while(true) — per-building spawn loop
                    }
                }

                // Advance to next building
                buildIdx++;
                // Re-read building count (may change if entities are added)
                buildingCount = town->storageLocations._size;
            } while (buildIdx < buildingCount);
        }
        // FIX debug_spawn_origin (docs/retail_bugs.md section 29): a pass that placed nobody (no
        // storage has a use node, e.g. only stockpiles) repeats for ever in retail and the game
        // hangs; with the row on the rest are placed by the world origin and the press ends.
        if (Ext::DebugSpawnOrigin::PlaceAtOrigin(Ext::Fix().debug_spawn_origin, placedThisPass)) {
            Math::Vector3 origin;
            if (Ext::DebugSpawnOrigin::OriginSpawnPoint(&origin)) {
                for (int i = spawnedTotal; i < Ext::DebugSpawnOrigin::kSpawnCount; i++) {
                    Game_SpawnCitizenAt(random, &origin);
                }
            }
            return;
        }
    } while (spawnedTotal < 4);
}

// ============================================================
// DebugUI::TriggerDisasterOnBuilding (FUN_14005FF30)
// Picks a random building from the building list, reads disease
// combo selection, and applies disease disaster to the building.
// ============================================================
void DebugUI::TriggerDisasterOnBuilding() {
    GameUpdateLoop* game = GameUpdateLoop::_singleton;
    CitizenBehavior* town = DebugUI_Town();

    // Pick a random citizen (CitizenBehavior +0xA0 / +0xA8; the tree called it the
    // building list)
    uint32_t buildingCount = (uint32_t)town->citizens._size;
    if (buildingCount == 0) return;
    uint32_t randIdx = game->m_random->Rand(static_cast<uint32_t>(buildingCount));

    // Get disease description from combo selection
    UI::ComboElement* combo = diseaseCombo;
    if (!combo) return;
    int diseaseIdx = combo->_currentItem;
    ::Object::ExternalList* externalList = game->m_diseaseList;
    if (!externalList) return;
    ::Object::External* diseaseEntry = externalList->_resources[diseaseIdx];
    bool isDisease = ::Object::IsDerivedFromNullSafe(diseaseEntry, Disease::TypeId());
    Disease* diseaseDesc = isDisease ? static_cast<Disease*>(diseaseEntry) : nullptr;
    if (!diseaseDesc) return;

    // Get building entity from building array
    CitizenComponent* building = town->citizens._data[(int)randIdx].Get();
    if (!building) return;

    // Apply disease to building
    building->Infect(diseaseDesc);   // 0x140116b90 (door named fn_DisasterComponent_Infect)
}

// ============================================================
// Game_StartRandomFire (FUN_14005FEB0)
// Picks a random building, finds its FireComponent, ignites it.
// ============================================================
static void Game_StartRandomFire_Impl() {
    GameUpdateLoop* game = GameUpdateLoop::_singleton;
    // The building list: CitizenBehavior +0x20 / +0x28.
    CitizenBehavior* town = DebugUI_Town();
    uint32_t buildingCount = (uint32_t)town->residences._size;

    if (buildingCount == 0) return;

    uint32_t randIdx = game->m_random->Rand(static_cast<uint32_t>(buildingCount));

    // Get random building entity
    ComponentEntity* building = town->residences._data[randIdx].Get();

    // Find FireComponent on the building (FindComponent<FireComponent>, cell 0x1401c41e0)
    FireComponent* fireComp = building->FindComponent<FireComponent>();

    // Ignite
    fireComp->Ignite();   // 0x140049610
}

// ============================================================
// Game_SpawnTornado (FUN_14005FD20)
// Loads Template/Tornado.rsc, creates entity, registers it.
// ============================================================
static void Game_SpawnTornado_Impl() {
    GameUpdateLoop* game = GameUpdateLoop::_singleton;

    // Load tornado template
    System::Filename filenameBuf("Template/Tornado.rsc");
    ::Object::External* extTemplate =
        ::Object::ExternalFactory::_singleton->FindOrLoadAs<::Object::External>(filenameBuf);

    // Cast to entity description (must be derived from ComponentDescription type)
    ComponentDescription* entityDesc = nullptr;
    if (extTemplate) {
        bool isEntityDesc = ::Object::IsDerivedFromNullSafe(extTemplate, ComponentDescription::TypeId());
        if (isEntityDesc) {
            entityDesc = static_cast<ComponentDescription*>(extTemplate);
        }
    }

    // filenameBuf releases via its destructor (System::Filename) at scope exit.

    // Allocate and create entity (0x48 bytes)
    ComponentEntity* newEntity = nullptr;
    ComponentEntity* memBlock = static_cast<ComponentEntity*>(System::MemoryAllocator::MemAlloc(0x48, 8));
    if (memBlock) {
        newEntity = ::new (memBlock) ComponentEntity(entityDesc);   // 0x1400c3720
    }

    // Register entity with entity manager
    Object::InternalPtr<ComponentEntity> entityRef = newEntity;
    game->m_entityList->RegisterEntity(&entityRef,
                                  entityDesc->_category);   // 0x1400c48a0
}

// ============================================================
// Game_InfectRandomCitizen (FUN_14005FE10)
// Picks a random citizen from the citizen list, infects them.
// ============================================================
static void Game_InfectRandomCitizen_Impl() {
    GameUpdateLoop* game = GameUpdateLoop::_singleton;
    // The disease components: CitizenBehavior +0xF0 / +0xF8.
    CitizenBehavior* town = DebugUI_Town();

    uint32_t citizenCount = (uint32_t)town->diseases._size;
    if (citizenCount == 0) return;

    uint32_t randIdx = game->m_random->Rand(static_cast<uint32_t>(citizenCount));

    // Get random citizen
    Object::InternalPtr<DiseaseComponent>* citizenArray = town->diseases._data;
    if (!citizenArray) return;
    DiseaseComponent* citizen = citizenArray[randIdx].Get();
    if (!citizen) return;

    // Infect citizen's disease component
    citizen->Infect();   // 0x1400483a0 (DiseaseComp_Infect's member)
}

// ENDGAME B.3.9: every global the old debugui_sa::EnsureGlobals() healed now has a
// ported TypeId(): ComponentDescription (cell 0x1401c7318), RawMaterialDescription
// (0x1401c7cb8), InteractComponent (0x1401c7900), InteractDescription (0x1401c7938),
// CitizenDescription (0x1401c7190), Disease (was TypeId_DiseaseDescription, cell
// 0x1401c5948 -- Table 1's authority, not the name), PopulationDescription (was
// TypeId_DrawableType, cell 0x1401c47c8 -- Table 1's authority), CitizenComponent
// (0x1401c7158), and UI::Element (was TypeId_Element, healed here under the WRONG
// by-name guess "Internal" -- the cell is exref 0x140151660, UI::Element's own
// _typeInfo per Table 1 / standalone/reg_element_typeids.cpp, matching this file's
// OWN correct comment on the FindElemBase guard further down). The lazy-cache
// namespace is dead.

// ============================================================
// DebugUI::OnUpdate (vtable +0x40) -- Update performance counters
// ============================================================
// Reads 4 debug counters from engine state, updates NumberElements.
// Smooths FPS/frame timing with exponential moving average (alpha=0.95).
// Displays smoothed FPS, frame ms, and total ms (* 1000).
//
// Engine state: MainLoop::_context (+0x10) holds counters:
//   +0x2150, +0x2154, +0x2158, +0x215C (int debug stats)
// Timing: MainLoop +0x58 (_frameTime) and +0x68 (_frameUpdateTime).
//
// Original: 0x1400610C0
// [8] 0x1400610c0
void DebugUI::OnUpdate(const System::Time& /*time*/) {
    // perf counters (self-guarded), then keep swatches/day live (the tree's
    // per-frame drive of slot 7, a standalone deviation).
    [this] {
        // Disasm: MOV RAX,[slot] / MOV RDX,[RAX] / MOV RSI,[RDX+0x10]
        ::Engine::MainLoop* engineObj = ::Engine::MainLoop::_singleton;
        if (!engineObj) return;
        const Video::Context* engineState = engineObj->_context;
        if (!engineState) return;

        // Read and display 4 debug counters from engine state
        if (numberBatches) numberBatches->SetValue(engineState->_drawsSubmitted);      // +0x2158
        if (numberDrawCalls) numberDrawCalls->SetValue(engineState->_objectsSubmitted); // +0x215C
        if (numberTriangles) numberTriangles->SetValue(engineState->_trianglesSubmitted); // +0x2150
        if (numberVertices) numberVertices->SetValue(engineState->_verticesSubmitted);  // +0x2154

        // Read frame time and total time from engine object (NOT engineState)
        // Original: *(*(exref 0x140151440) + 0x68) and + 0x58
        System::Time frameTime = { engineObj->_frameUpdateTime };
        System::Time totalTime = { engineObj->_frameTime };
        float frameSeconds = frameTime.AsSeconds();
        float totalSeconds = totalTime.AsSeconds();

        // Exponential moving average smoothing constants
        const float alpha = 0.95f;
        const float beta  = 0.05f;

        float instantFPS = (totalSeconds > 0.0f) ? (1.0f / totalSeconds) : 0.0f;
        smoothedTotalMS = smoothedTotalMS * alpha + totalSeconds * beta;
        smoothedFPS     = instantFPS * beta + smoothedFPS * alpha;
        smoothedFrameMS = smoothedFrameMS * alpha + frameSeconds * beta;

        // Display smoothed values
        if (numberFPS) numberFPS->SetValue(static_cast<int32_t>((int)smoothedFPS));
        if (numberFrameMS) numberFrameMS->SetValue(static_cast<float>(smoothedFrameMS * 1000.0f));
        if (numberTotalMS) numberTotalMS->SetValue(static_cast<float>(smoothedTotalMS * 1000.0f));
    }();
    DebugUI::OnUpdateLayout();
}

// ============================================================
// DebugUI::OnUpdateLayout (vtable +0x38) -- Update day text and color swatches
// ============================================================
// 1. Gets first child of sliderDay (+0x78), formats day text
// 2. Reads RGB slider values for sun/amb/fog, packs ARGB, sets color
//    on preview ImageElements via vtable[10] (SetColor at +0x50)
//
// Original: 0x140061280
// [7] 0x140061280
void DebugUI::OnUpdateLayout() {
    // --- Day slider text update ---
    // Original: CheckPtr(+0x78), get child, CheckPtr child, IsDerivedFrom, IBP ctor,
    //           CheckPtr(+0x78) again for value, CheckPtr child for format, SetText
    if (sliderDay) {
        UI::Element* childRaw = sliderDay->Child(0).Get();
        // IsDerivedFrom check + IBP wrap (matches original)
        bool isText = childRaw && ::Object::IsDerivedFromNullSafe(childRaw, UI::Element::TypeId());
        UI::TextElement* textElem = isText ? static_cast<UI::TextElement*>(childRaw) : nullptr;

        float dayValue = sliderDay->_value;

        if (textElem) {
            // UI::String(textElem->_text's table, day / 30): the table and the
            // (untouched) pad come from the element's own text.
            UI::String dayText = textElem->_text;
            dayText.index = (int)dayValue / 30;
            textElem->SetText(&dayText);
        }
    }

    // --- Sun color swatch ---
    if (imageColorSun) {
        float sunB = valueSunB->_value;
        float sunG = valueSunG->_value;
        uint32_t sunColor = ((int)valueSunR->_value << 8
            | (int)sunG) << 8 | (int)sunB | 0xFF000000;
        imageColorSun->SetColor(sunColor);
    }

    // --- Ambient color swatch ---
    if (imageColorAmb) {
        float ambB = valueAmbB->_value;
        float ambG = valueAmbG->_value;
        uint32_t ambColor = ((int)valueAmbR->_value << 8
            | (int)ambG) << 8 | (int)ambB | 0xFF000000;
        imageColorAmb->SetColor(ambColor);
    }

    // --- Fog color swatch ---
    if (imageColorFog) {
        float fogB = valueFogB->_value;
        float fogG = valueFogG->_value;
        uint32_t fogColor = ((int)valueFogR->_value << 8
            | (int)fogG) << 8 | (int)fogB | 0xFF000000;
        imageColorFog->SetColor(fogColor);
    }
}

// ============================================================
// DebugUI::OnElementEvent (vtable +0x48) -- Event dispatch
// ============================================================
// Handles button clicks, combo changes, and slider changes for
// the debug panel. Dispatches to sub-functions for resource spawning,
// disasters, citizen spawning, debug render toggles, weather/lighting.
//
// Event types (a):
//   0x12 = button click → IDs 300-339 (resource buttons, disaster buttons, debug toggles)
//   0x14 = combo change → ID 0x163 (comboWeather), ID 0x166 (drawCombo)
//   0x17 = slider change → ID 0x154 (sliderDay), IDs 0x155-0x162 (color sliders)
//
// Original: 0x140060BC0
// [9] 0x140060bc0
void DebugUI::OnElementEvent(Object::InternalPtr<UI::Element>* element, UI::EventId a, int32_t b,
                             int32_t /*c*/, int32_t /*d*/, int32_t /*e*/, void* /*userData*/) {
    GameUpdateLoop* game = GameUpdateLoop::_singleton;

    // ---- Combo change (0x14) ----
    if (a == UI::EventId::SelectionChanged) {
        if (b == 0x166) {
            OnDrawComboChanged();
        } else if (b == 0x163) {
            // Weather combo changed (select the visible weather)
            OnWeatherComboChanged();
        }
        return;
    }

    // ---- Slider/number change (0x17) ----
    if (a == UI::EventId::SliderChanged) {
        if (b == 0x154) {
            // Day slider changed → update game time and lighting
            OnDaySliderChanged();
        } else if ((unsigned)(b - 0x155) <= 0xD) {
            // Color sliders (0x155-0x162) changed → write lighting colors
            int month = DebugUI_GetMonthFromTime_Impl(game->m_dayCounter);
            WriteLightingColors(month);
        }
        return;
    }

    // ---- Button click (0x12) ----
    if (a != UI::EventId::Activated)
        return;

    // Range check: element IDs 300 (0x12C) through 339 (0x153)
    if ((unsigned)(b - 300) > 0x39)
        return;

    // Get citizen list from game world (+0xF0)
    CitizenBehavior* citizenList = game->m_citizenList;

    switch (b) {
    // --- Resource spawn buttons ---
    // Each calls CitizenBehavior::SpawnResources(resourceFlag, count). The comments name the
    // element OnCreated gives the id (buttonTool -> 0x131 at 0x140061d7e, and so on in order).
    case 300:  citizenList->SpawnResources(RawMaterialFlags::Wood,     50);   return;  // buttonWood
    case 301:  citizenList->SpawnResources(RawMaterialFlags::Stone,    50);   return;  // buttonStone
    case 302:  citizenList->SpawnResources(RawMaterialFlags::Iron,     50);   return;  // buttonIron
    case 303:  citizenList->SpawnResources(RawMaterialFlags::FuelBit7, 50);   return;  // buttonFuel
    case 304:  citizenList->SpawnResources(RawMaterialFlags::FuelBit8, 50);   return;  // buttonCoal
    case 305:  citizenList->SpawnResources(RawMaterialFlags::Tool,     50);   return;  // buttonTool
    case 306:  citizenList->SpawnResources(RawMaterialFlags::Edible,   1000); return;  // buttonFood
    case 307:  citizenList->SpawnResources(RawMaterialFlags::Health,   25);   return;  // buttonHealth
    case 308:  citizenList->SpawnResources(RawMaterialFlags::Clothing, 25);   return;  // buttonClothes
    case 309:  citizenList->SpawnResources(RawMaterialFlags::Textile,  50);   return;  // buttonTextile
    case 310:  citizenList->SpawnResources(RawMaterialFlags::Alcohol,  100);  return;  // buttonAlcohol
    case 311:  citizenList->SpawnResources(0x40000,   100);  return;  // Resource type 18
    case 312:  citizenList->SpawnResources(0x80000,   100);  return;  // Resource type 19
    case 313:  citizenList->SpawnResources(0x100000,  100);  return;  // Resource type 20
    case 314:  citizenList->SpawnResources(0x200000,  100);  return;  // Resource type 21
    case 315:  citizenList->SpawnResources(0x400000,  100);  return;  // Resource type 22
    case 316:  citizenList->SpawnResources(0x800000,  100);  return;  // Resource type 23
    case 317:  citizenList->SpawnResources(0x1000000, 100);  return;  // Resource type 24
    case 318:  citizenList->SpawnResources(0x2000000, 100);  return;  // Resource type 25
    case 319:  citizenList->SpawnResources(0x4000000, 100);  return;  // Resource type 26
    case 320:  citizenList->SpawnResources(0x8000000, 100);  return;  // Resource type 27

    // --- Disaster / entity manipulation buttons ---
    case 321:  Game_TriggerDisasterByType_Impl(0); return;  // Trigger disaster type 0
    case 322:  Game_TriggerDisasterByType_Impl(1); return;  // Trigger disaster type 1
    case 323:  Game_TriggerDisasterByType_Impl(2); return;  // Trigger disaster type 2
    case 324:  Game_DestroyAllRawMaterials_Impl();  return;  // Destroy all loose raw material entities
    case 325:  Game_SpawnItemsInStorage_Impl();    return;  // Spawn items in all storage
    case 326:  Game_SpawnCitizens_Impl();          return;  // Spawn new citizens
    case 327:  TriggerDisasterOnBuilding();        return;  // Disaster on random building
    case 328:  Game_StartRandomFire_Impl();        return;  // Start random fire
    case 329:  Game_SpawnTornado_Impl();           return;  // Spawn tornado
    case 330:  Game_InfectRandomCitizen_Impl();    return;  // Infect random citizen

    // --- Debug render flag toggles (checkboxes) ---
    // `element` points at the caller's InternalPtr (the checkbox element).
    // Retail copies *element to a local and passes &local to ToggleDebugFlag.
    case 331: { Object::InternalPtr<UI::Element> check = *element; ToggleDebugFlag(&check, 0x1);   return; }
    case 332: { Object::InternalPtr<UI::Element> check = *element; ToggleDebugFlag(&check, 0x2);   return; }
    case 333: { Object::InternalPtr<UI::Element> check = *element; ToggleDebugFlag(&check, 0x4);   return; }
    case 334: { Object::InternalPtr<UI::Element> check = *element; ToggleDebugFlag(&check, 0x8);   return; }
    case 335: { Object::InternalPtr<UI::Element> check = *element; ToggleDebugFlag(&check, 0x10);  return; }
    case 336: { Object::InternalPtr<UI::Element> check = *element; ToggleDebugFlag(&check, 0x20);  return; }
    case 337: { Object::InternalPtr<UI::Element> check = *element; ToggleDebugFlag(&check, 0x40);  return; }
    case 338: { Object::InternalPtr<UI::Element> check = *element; ToggleDebugFlag(&check, 0x80);  return; }
    case 339: { Object::InternalPtr<UI::Element> check = *element; ToggleDebugFlag(&check, 0x200); return; }

    default: return;
    }
}

// B4 FORWARD: 1 outside caller(s) (src/diag/disaster_harness.cpp, which reaches
// the tornado button with a null DebugUI; case 329 reads no DebugUI field).
void DebugUI_Slot9(DebugUI* self, Object::InternalPtr<UI::Element>* element, UI::EventId param3, int param4) {
    self->DebugUI::OnElementEvent(element, param3, param4, 0, 0, 0, nullptr);
}

// ============================================================
// DebugUI::ToggleDebugFlag -- helper for debug render checkboxes
// ============================================================
// Reads the checkbox state (CheckElement::_checked, +0xD8), sets or clears a bit
// in the game world's flags (GameUpdateLoop::m_gameFlags, +0xD0).
//
// Original: 0x14005FCC0
void DebugUI::ToggleDebugFlag(Object::InternalPtr<UI::Element>* check, uint32_t flag) {
    GameUpdateLoop* game = GameUpdateLoop::_singleton;

    // CheckPtr resolves the InternalBasePtr (a bare RET), then the element is read
    // as the CheckElement the checkbox is (retail's InternalPtr<CheckElement>).
    UI::CheckElement* element = static_cast<UI::CheckElement*>(check->Get());
    bool checked = element->_checked != 0;

    if (checked) {
        game->m_gameFlags |= flag;
    } else {
        game->m_gameFlags &= ~flag;
    }
}

// ============================================================
// DebugUI::OnCreated (vtable +0x20) -- Setup / element binding
// ============================================================
// Finds 50+ named UI elements in the dialog, assigns element IDs for
// event dispatch, stores persistent elements in DebugUI struct fields,
// populates disease/draw/weather combo boxes, sets slider min/max
// ranges, loads initial day-of-year from game time, and finds all
// performance counter NumberElements.
//
// Original: 0x1400619E0

// Helper: Find element by name, wrap it through IsDerivedFrom.
static UI::Element* DebugUI_FindElemBase(UI::Element* parentElem, const char* name) {
    System::String nameBuf(name);
    UI::Element* rawElem = parentElem->FindElement(nameBuf).Get();
#ifdef _DEBUG
    if (!rawElem) printf("[DebugUI::Slot4] FindElemBase('%s') = NULL\n", name);
    else printf("[DebugUI::Slot4] FindElemBase('%s') = %p\n", name, static_cast<void*>(rawElem));
#endif
    // The tree checks UI::Element's own id (exe slot 0x140151660) for every
    // element; retail checks each field's own class (the check / combo / slider
    // type cells) — a known deviation, kept.
    if (!rawElem || !::Object::IsDerivedFromNullSafe(rawElem, UI::Element::TypeId())) {
        rawElem = nullptr;
    }
    return rawElem;
}

// Helper: Find element, set element ID.
// Returns nullptr if element not found (e.g., Debug Menu mod missing elements).
static UI::Element* DebugUI_FindAndSetId(UI::Element* parentElem, const char* name, int id) {
    UI::Element* elem = DebugUI_FindElemBase(parentElem, name);
    if (elem) elem->SetElementId(id);
    return elem;
}

// [4] 0x1400619e0
void DebugUI::OnCreated() {
    // Slot4 unconditionally dereferences ObjectInternal (every FindElemBase),
    // DiseaseDescription (diseaseCombo populate) and DrawableType (drawCombo
    // populate). Skip the whole setup rather than fault if any failed to
    // resolve; all three reliably resolve post-Engine::Startup.
    if (!UI::Element::TypeId() || !Disease::TypeId() || !PopulationDescription::TypeId()) {
        return;
    }
    UI::Element* parentElem = _container;

    // ==========================================================
    // 1. Resource spawn buttons (IDs 300..0x136)
    // ==========================================================
    DebugUI_FindAndSetId(parentElem, "buttonWood",     300);   // 0x12C
    DebugUI_FindAndSetId(parentElem, "buttonStone",    0x12D);
    DebugUI_FindAndSetId(parentElem, "buttonIron",     0x12E);
    DebugUI_FindAndSetId(parentElem, "buttonFuel",     0x12F);
    DebugUI_FindAndSetId(parentElem, "buttonCoal",     0x130);
    DebugUI_FindAndSetId(parentElem, "buttonTool",     0x131);
    DebugUI_FindAndSetId(parentElem, "buttonFood",     0x132);
    DebugUI_FindAndSetId(parentElem, "buttonHealth",   0x133);
    DebugUI_FindAndSetId(parentElem, "buttonClothes",  0x134);
    DebugUI_FindAndSetId(parentElem, "buttonTextile",  0x135);
    DebugUI_FindAndSetId(parentElem, "buttonAlcohol",  0x136);

    // buttonCustom0..buttonCustom9 (IDs 0x26E..0x277)
    {
        int id = 0x26E;
        for (int i = 0; i < 10; i++, id++) {
            char nameBuf[32];
            snprintf(nameBuf, sizeof(nameBuf), "buttonCustom%i", i);
            DebugUI_FindAndSetId(parentElem, nameBuf, id);
        }
    }

    // ==========================================================
    // 2. Spawn action buttons (IDs 0x141..0x146)
    // ==========================================================
    DebugUI_FindAndSetId(parentElem, "buttonCrop",      0x141);
    DebugUI_FindAndSetId(parentElem, "buttonOrchard",   0x142);
    DebugUI_FindAndSetId(parentElem, "buttonAnimals",   0x143);
    DebugUI_FindAndSetId(parentElem, "buttonVisible",   0x144);
    DebugUI_FindAndSetId(parentElem, "buttonLivestock", 0x145);
    DebugUI_FindAndSetId(parentElem, "buttonCitizen",   0x146);

    // ==========================================================
    // 3. Disease combo + disease button (ID 0x147)
    // ==========================================================
    {
        this->diseaseCombo = static_cast<UI::ComboElement*>(DebugUI_FindElemBase(parentElem, "diseaseCombo"));

        if (this->diseaseCombo) {
            // Populate from ExternalList (resource descriptions at gameWorld+0xA8).
            // STANDALONE: this list can be null/unpopulated, and
            // GetResourceCount(null) faults reading null+0x28. Guard so a missing
            // list yields an empty combo instead of crashing.
            GameUpdateLoop* gameWorld_dc = GameUpdateLoop::_singleton;
            ::Object::ExternalList* externalList = gameWorld_dc ? gameWorld_dc->m_diseaseList : nullptr;
            int resourceCount = externalList ? externalList->GetResourceCount() : 0;
            for (int i = 0; i < resourceCount; i++) {
                ::Object::External* resource = externalList->_resources[i];
                // Retail's checked cast. Every entry of the Diseases.rsc ExternalList
                // IS a Disease, so this never narrows to null in practice — retail
                // relies on that and derefs the result unguarded two lines later
                // (0x140062560..0x140062578). Kept faithful: one combo row per list
                // entry, so the row index stays a direct index into the list, which is
                // exactly how DebugUI::TriggerDisasterOnBuilding (0x14005ff30) reads
                // the selection back.
                bool isDisease = ::Object::IsDerivedFromNullSafe(resource, Disease::TypeId());
                Disease* disease = isDisease ? static_cast<Disease*>(resource) : nullptr;
                if (!disease) continue;

                // Row text = StringTable::GetText(disease->stringTable(+0x20),
                //                                 &out, disease->stringName(+0x28))
                // — retail's 0x14006257c call, whose third argument Ghidra hides.
                System::String textBuf = disease->_stringTable->GetText(disease->_name);
                UI::Sprite sprite;
                this->diseaseCombo->AddItem(sprite, textBuf, nullptr);
            }
            this->diseaseCombo->SetCurrentItem(0);
        }
    }
    DebugUI_FindAndSetId(parentElem, "diseaseButton", 0x147);

    // ==========================================================
    // 4. Disaster buttons (IDs 0x148..0x14A)
    // ==========================================================
    DebugUI_FindAndSetId(parentElem, "fireButton",        0x148);
    DebugUI_FindAndSetId(parentElem, "tornadoButton",     0x149);
    DebugUI_FindAndSetId(parentElem, "cropDiseaseButton", 0x14A);

    // ==========================================================
    // 5. Cheat checkboxes (IDs 0x14B..0x153) with initial state
    // ==========================================================
    GameUpdateLoop* gameWorld = GameUpdateLoop::_singleton;
    uint32_t cheatFlags = gameWorld->m_gameFlags;

    // CheckElement::_checked (+0xD8) from one bit of the game flags.
    auto initCheck = [&](const char* name, int id, uint32_t bit) {
        UI::Element* elem = DebugUI_FindAndSetId(parentElem, name, id);
        if (elem) static_cast<UI::CheckElement*>(elem)->_checked = (cheatFlags >> bit) & 1;
    };
    initCheck("zeroCostCheck",      0x14B, 0);
    initCheck("noHungerCheck",      0x14C, 1);
    initCheck("noColdCheck",        0x14D, 2);
    initCheck("noHealthCheck",      0x14E, 3);
    initCheck("noSickCheck",        0x14F, 4);
    initCheck("noCropDiseaseCheck", 0x150, 5);
    initCheck("noFireCheck",        0x151, 6);
    initCheck("noTornadoCheck",     0x152, 7);
    // educatedCheck -- bit 9 (bit 8 is skipped)
    initCheck("educatedCheck",      0x153, 9);

    // ==========================================================
    // 6. Draw combo (ID 0x166) with drawable type entries
    // ==========================================================
    {
        this->drawCombo = static_cast<UI::ComboElement*>(DebugUI_FindElemBase(parentElem, "drawCombo"));

        if (this->drawCombo) {
            this->drawCombo->SetElementId(0x166);

            // Populate from the population list at gameWorld+0xF0 (+0xC0/+0xC8).
            // STANDALONE: guard a null list (empty combo instead of a fault).
            CitizenBehavior* town = DebugUI_Town();
            int drawCount = town ? town->populations._size : 0; // +0xC8
            int selectedIndex = 0;
            for (int i = 0; i < drawCount; i++) {
                Component* entry = town->populations._data[i].Get();   // +0xC0
                Description* inner = entry->_description;
                bool isDrawable = ::Object::IsDerivedFromNullSafe(inner, PopulationDescription::TypeId());
                PopulationDescription* drawableDesc = isDrawable ? static_cast<PopulationDescription*>(inner) : nullptr;

                UI::Sprite sprite;
                this->drawCombo->AddItem(sprite, drawableDesc->_name, entry);

                // Retail compares the entry with an InternalBasePtr built from null.
                if (entry == nullptr) {
                    selectedIndex = i + 4;
                }
            }
            this->drawCombo->SetCurrentItem(selectedIndex);
        }
    }

    // ==========================================================
    // 7. Lighting sliders -- find, store, set IDs
    // ==========================================================
    // Original finds all slider elements first, stores IBPs, sets IDs.
    // Min/max are set later (after image elements) matching original order.
    auto findSlider = [&](const char* name, int id) {
        UI::SliderElement* slider = static_cast<UI::SliderElement*>(DebugUI_FindElemBase(parentElem, name));
        slider->SetElementId(id);
        return slider;
    };

    sliderDay = findSlider("sliderDay", 0x154);
    valueSunR = findSlider("valueSunR", 0x155);
    valueSunG = findSlider("valueSunG", 0x156);
    valueSunB = findSlider("valueSunB", 0x157);
    valueSunH = findSlider("valueSunH", 0x158);
    valueSunP = findSlider("valueSunP", 0x159);
    valueAmbR = findSlider("valueAmbR", 0x15A);
    valueAmbG = findSlider("valueAmbG", 0x15B);
    valueAmbB = findSlider("valueAmbB", 0x15C);
    valueFogR = findSlider("valueFogR", 0x15D);
    valueFogG = findSlider("valueFogG", 0x15E);
    valueFogB = findSlider("valueFogB", 0x15F);
    valueFogN = findSlider("valueFogN", 0x160);
    valueFogF = findSlider("valueFogF", 0x161);
    valueFogM = findSlider("valueFogM", 0x162);

    // ==========================================================
    // 8. Color preview images (no IDs, just stored)
    // ==========================================================
    this->imageColorSun = static_cast<UI::ImageElement*>(DebugUI_FindElemBase(parentElem, "imageColorSun"));
    this->imageColorAmb = static_cast<UI::ImageElement*>(DebugUI_FindElemBase(parentElem, "imageColorAmb"));
    this->imageColorFog = static_cast<UI::ImageElement*>(DebugUI_FindElemBase(parentElem, "imageColorFog"));

    // ==========================================================
    // 8b. Slider min/max — set AFTER image elements (matches original order)
    // ==========================================================
    // SliderElement::_minimum (+0x118) / _maximum (+0x11C).
    auto setMinMax = [](UI::SliderElement* slider, float minVal, float maxVal) {
        slider->_minimum = minVal;
        slider->_maximum = maxVal;
    };

    setMinMax(sliderDay, 0.0f, 359.0f);
    setMinMax(valueSunR, 0.0f, 255.0f);
    setMinMax(valueSunG, 0.0f, 255.0f);
    setMinMax(valueSunB, 0.0f, 255.0f);
    setMinMax(valueSunH, -180.0f, 180.0f);
    setMinMax(valueSunP, 0.0f, 90.0f);
    setMinMax(valueAmbR, 0.0f, 255.0f);
    setMinMax(valueAmbG, 0.0f, 255.0f);
    setMinMax(valueAmbB, 0.0f, 255.0f);
    setMinMax(valueFogR, 0.0f, 255.0f);
    setMinMax(valueFogG, 0.0f, 255.0f);
    setMinMax(valueFogB, 0.0f, 255.0f);
    setMinMax(valueFogN, -50.0f, 500.0f);
    setMinMax(valueFogF, -50.0f, 500.0f);
    setMinMax(valueFogM, 0.0f, 255.0f);

    // ==========================================================
    // 9. Weather combo (ID 0x163) with weather preset entries
    // ==========================================================
    {
        this->comboWeather = static_cast<UI::ComboElement*>(DebugUI_FindElemBase(parentElem, "comboWeather"));

        if (this->comboWeather) {
            // Add "None" entry with userData = -1
            System::String noneBuf("None");
            UI::Sprite sprite;
            this->comboWeather->AddItem(sprite, noneBuf, (void*)(intptr_t)-1);  // CAST-OK: ptr_int

            // Populate from the environment's weather list (weather sim +0x00).
            // STANDALONE: guard a null weather manager / list.
            WeatherSimulation* weatherListPtr = gameWorld->m_weather;
            EnvironmentDescription* env = weatherListPtr ? weatherListPtr->environment : nullptr;
            int weatherCount = env ? (int)env->_weatherArray._size : 0;
            int selectedIndex = 0;
            for (int i = 0; i < weatherCount; i++) {
                const ::Object::External* entry = env->_weatherArray[i].weather;
                // `GetPath` returns by value through a hidden return slot and the callee
                // CONSTRUCTS into it; copy-initialising the local is the same store.
                System::Filename pathBuf = entry->GetPath();
                System::Filename filenameBuf = pathBuf.GetPathFilename(false);

                UI::Sprite spr;
                this->comboWeather->AddItem(spr, filenameBuf.AsString(), (void*)(intptr_t)i);  // CAST-OK: ptr_int

                // Check if this weather is currently active (its geometry fading in)
                WeatherParticleGeometry* active = static_cast<WeatherParticleGeometry*>(weatherListPtr->effects[i]);
                if (active != nullptr && active->_fadeDirection > 0.0f) {
                    selectedIndex = i + 1;
                }
            }
            this->comboWeather->SetCurrentItem(selectedIndex);
            this->comboWeather->SetElementId(0x163);
        }
    }

    // ==========================================================
    // 10. Action buttons (IDs 0x164, 0x165)
    // ==========================================================
    this->buttonSave = static_cast<UI::ButtonElement*>(DebugUI_FindElemBase(parentElem, "buttonSave"));
    if (this->buttonSave) this->buttonSave->SetElementId(0x164);

    this->buttonMenuScene = static_cast<UI::ButtonElement*>(DebugUI_FindElemBase(parentElem, "buttonMenuScene"));
    if (this->buttonMenuScene) this->buttonMenuScene->SetElementId(0x165);

    // ==========================================================
    // Initialize day slider from current game time
    // ==========================================================
    {
        // STANDALONE: gw+0x128 (weather time) can be null; Time_AsSeconds(null+8)
        // would fault. SetDayLighting derefs **(gw+0x130) (the weather table), also
        // null-able — only call it when that chain is live.
        const Calendar* weatherTime = gameWorld->m_dayCounter;
        float timeSeconds = weatherTime ? weatherTime->yearTime.AsSeconds() : 0.0f;
        // Convert time to month index: (seconds / secondsPerYear) * monthsPerYear
        // DAT_14017225c = 3600.0f, DAT_1401721b8 = 12.0f
        int monthIndex = (int)(timeSeconds / 3600.0f * 12.0f);
        if (monthIndex > 11) monthIndex = 0;

        WeatherSimulation* weatherLive = gameWorld->m_weather;
        if (this->sliderDay) {
            this->sliderDay->_value = (float)(monthIndex * 30);
            if (weatherLive && weatherLive->environment) SetDayLighting(monthIndex);
        }
    }

    // ==========================================================
    // 11. Performance counter elements (stored in struct fields)
    // ==========================================================
    this->numberFrameMS   = static_cast<UI::NumberElement*>(DebugUI_FindElemBase(parentElem, "numberFrameMS"));
    this->numberTotalMS   = static_cast<UI::NumberElement*>(DebugUI_FindElemBase(parentElem, "numberTotalMS"));
    this->numberFPS       = static_cast<UI::NumberElement*>(DebugUI_FindElemBase(parentElem, "numberFPS"));
    this->numberDrawCalls = static_cast<UI::NumberElement*>(DebugUI_FindElemBase(parentElem, "numberDrawCalls"));
    this->numberTriangles = static_cast<UI::NumberElement*>(DebugUI_FindElemBase(parentElem, "numberTriangles"));
    this->numberVertices  = static_cast<UI::NumberElement*>(DebugUI_FindElemBase(parentElem, "numberVertices"));
    this->numberBatches   = static_cast<UI::NumberElement*>(DebugUI_FindElemBase(parentElem, "numberBatches"));

    // Zero the smoothing accumulators
    this->smoothedFPS = 0.0f;
    this->smoothedFrameMS = 0.0f;
    this->smoothedTotalMS = 0.0f;
}

// ============================================================
// STANDALONE promotion — real DebugUI controller vtable
// ============================================================
// The developer debug/cheat panel is opened by the game's own data-driven
// dialog system (a .rsc that names DebugUI as its controller, opened via the
// dev keybind). In standalone this type was registered as a passive
// UI_DIALOG_STUB (all slots StubNop) in ui_dialog_stub_factories.cpp, so the
// panel window rendered from its .rsc but Slot4 never ran. DebugUI is a real
// class now; the slots are the members above (table 0x14015b870):
//   +0x20 (idx4) OnCreated, +0x30 (idx6) OnClosed (no-op), +0x38 (idx7)
//   OnUpdateLayout, +0x40 (idx8) OnUpdate (perf counters, then OnUpdateLayout
//   every frame: a standalone deviation), +0x48 (idx9) OnElementEvent.

// create 0x140010ec0: MemAlloc(0x130, 8), then the ctor (DialogController ->
// Internal: AddObject, the compiler's stamps, the field initialisers).
::Object::Base* DebugUI::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(DebugUI), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) DebugUI());
}
// Retail's createInPlace does not build a controller in place (B8-13); the
// tree's in-place entry did, and that is kept.
::Object::Base* DebugUI::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) DebugUI());
}

DEFINE_EXE_TYPE(DebugUI, "DialogController")

