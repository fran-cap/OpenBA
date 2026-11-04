// engine/src/ui/buildui.cpp
// Clean reimplementation of BuildUI panel methods.
//
// BuildUI (0x50 bytes): Building info panel showing build progress.
// Layout:
//   +0x20 = dialog element (InternalBasePtr, from DialogController)
//   +0x28 = entity data source (InternalBasePtr, from DialogController)
//   +0x30 = row array ptr (each 0x10: labelStorage ptr, textTotal ptr)
//   +0x38 = row array count (int32)
//   +0x3C = row array capacity (int32)
//   +0x40 = progress element (InternalBasePtr)
//   +0x48 = textTotal element (InternalBasePtr)
//
// Decompiled from Application-x64.exe.

#include "Dialog/ui_panels.h"
#include "ui/UserInterface.h"
#include "object/TypeInfo.h"
#include "Entity/exe_bodies.h"
#include "Dialog/exe_bodies.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Object/factory_registration.h"
#include "system/String.h"
#include <cstdint>
#include <cstdio>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "ui/Element.h"  // ENDGAME B.3.6 UI callers (K3)
#include "ui/String.h"  // ENDGAME B.3.6 UI callers (K3)
#include "ui/Sprite.h"  // ENDGAME B.3.6 UI callers (K3)
#include "ui/SpriteSheet.h"  // ENDGAME B.3.6 UI callers (K3)
#include "ui/StringTable.h"  // ENDGAME B.3.6 UI callers (K3)
#include "object/External.h"  // ENDGAME B.3.6 UI callers (K3)
#include <new>  // ENDGAME B.3.6 UI callers (K3)
#include "ui/LabelElement.h"  // ENDGAME B.3.6 UI callers round 2 (K3)
#include "ui/ProgressElement.h"  // ENDGAME B.3.6 UI callers round 2 (K3)
#include "ui/TextElement.h"  // ENDGAME B.3.6 UI callers round 2 (K3)
#include "Component/BuildComponent.h"   // X::TypeId() (B.3.8 callers)
#include "Component/BuildDescription.h"   // X::TypeId() (B.3.8 callers)
#include "Component/ClearAreaComponent.h"   // X::TypeId() (B.3.8 callers)
#include "Component/RawMaterialDescription.h"   // X::TypeId() (B.3.8 callers)
#include "Entity/ComponentEntity.h"   // X::TypeId() (B.3.8 callers)
#include "Component/exe_bodies.h"   // B3R: WorkPlaceComponent_SetAccepting
#include "Component/MapComponent.h"          // B4 DLG-a: by-name reads
#include "Component/WorkPlaceComponent.h"
#include "Component/StorageComponent.h"
#include "Component/ComponentDescription.h"
#include "Game/Map.h"
#include "ui/CheckElement.h"

#ifdef _DEBUG
// Debug sprite logging wrappers (src/engine/ui/)
#include "ui/sprite_debug.h"   // Debug_Sprite_Ctor
extern void Debug_ImageElement_SetSprite(UI::ImageElement* self, const UI::Sprite* sprite);
extern void Debug_LabelElement_SetSprite(UI::LabelElement* self, const UI::Sprite* sprite);
#endif

// ============================================================
// CountOccupiedBuildTiles (0x140118330)
// ============================================================
// Counts tiles in the entity's building footprint that are occupied
// by other entities (not self). Uses the MapComponent footprint
// to iterate the tile grid and checks each tile's occupant.
//
// Retail reads ClearAreaComponent fields through RCX (a ClearAreaComponent
// member); LEFT a file-local function: the class header is COMP-c's.
// Returns: count of occupied tiles not owned by self.
static int CountOccupiedBuildTiles_Impl(ClearAreaComponent* clearArea)
{
    // The owning entity (Component::_entity) = self identity
    ComponentEntity* entityRef = clearArea->_entity.Get();

    // Find the MapComponent on the entity (FindComponent<MapComponent>, 0x140004680)
    MapComponent* bc = entityRef->FindComponent<MapComponent>();

    // The footprint, read signed (the tree's int16 reads; the header says uint16):
    //   tileX0 = originX, tileY0 = originY, tileX1 = width, tileY1 = height
    int originX = (int)(int16_t)bc->tileX0;
    int originY = (int)(int16_t)bc->tileY0;
    int width   = (int)(int16_t)bc->tileX1;
    int height  = (int)(int16_t)bc->tileY1;

    // The Map at gameWorld+0xF8: size = mapWidth, cells = the 0x30-byte cell array
    Map* map = GameUpdateLoop::_singleton->m_game;
    int mapWidth = map->size;
    const MapCell* tileArrayBase = map->cells.GetData();

    int count = 0;
    for (int row = 0; row < height; ++row) {
        for (int col = 0; col < width; ++col) {
            // Each tile entry is a 0x30-byte MapCell; occupant ptr is at offset +8
            int tileIndex = (originY + row) * mapWidth + originX + col;
            const ComponentEntity* occupant = tileArrayBase[tileIndex].occupant.Get();
            // Count tiles occupied by other entities (not self, not null)
            if (occupant != clearArea->_entity.Get() && occupant != nullptr) {
                count++;
            }
        }
    }
    return count;
}

// Helper: find element by name in parent element
static UI::Element* BuildUI_FindChildElem(UI::Element* parentElem, const char* name) {
    System::String nameStr(name);
    return parentElem->FindElement(nameStr).Get();
}

// The data source's ComponentEntity (retail's checked cast of +0x28).
static ComponentEntity* BuildUI_Entity(Object::Base* dataSource) {
    bool isEntity = ::Object::IsDerivedFromNullSafe(dataSource, ComponentEntity::TypeId());
    return isEntity ? static_cast<ComponentEntity*>(dataSource) : nullptr;
}

// The building's BuildDescription: its BuildComponent's Component::_description,
// checked (retail's IsDerivedFrom BuildDescription).
static BuildDescription* BuildUI_Description(BuildComponent* posComp) {
    if (posComp == nullptr) return nullptr;
    Description* desc = posComp->_description;
    bool isResource = ::Object::IsDerivedFromNullSafe(desc, BuildDescription::TypeId());
    return isResource ? static_cast<BuildDescription*>(desc) : nullptr;
}

// ---- BuildUI: PROMOTED to the real reimpl (2026-07-13) -----------------------
// The in-game "under construction" panel: a per-material storage table
// (delivered / required), a construction progress bar + percent, and a
// pause-construction button. It is a DialogController bound into a building's
// info dialog (created by EditTool select).
namespace buildui_sa {
    // Slot4 has run once the "progress" element (+0x40) is populated.
    inline bool IsSetUp(const BuildUI* self) { return self && self->progress != nullptr; }
}

// The finders the tree called (the generic walk) returned null for a null
// entity (a standalone guard; retail's FindComponent<T> instances have none):
// kept on every find below.

// ============================================================
// BuildUI::OnCreated (vtable +0x20) — Setup/populate build storage table
// ============================================================
// Finds "storageTable", "progress", "buttonPause" elements.
// Iterates storage table rows, populating each with resource sprite,
// label, and required amount from the BuildDescription.
// Sets up the progress bar and pause button state.
//
// Original: 0x1400B1F40.

// [4] 0x1400b1f40
void BuildUI::OnCreated() {
    if (!SA_DialogEntityAlive(this)) return;
    // Idempotent: the framework dispatches setup on attach; a re-drive would
    // append a second copy of the storage rows to +0x30 (count never reset)
    // — exactly the ProfessionUI double-populate class. Skip if already set up.
    if (buildui_sa::IsSetUp(this)) return;

    // Find "storageTable" element
    UI::Element* dialogElem = _container;
    UI::Element* tableElem = BuildUI_FindChildElem(dialogElem, "storageTable");

    // Find "progress" element, store at +0x40 (stored unchecked, read as a ProgressElement)
    progress = static_cast<UI::ProgressElement*>(BuildUI_FindChildElem(dialogElem, "progress"));

    // Progress maximum 1.0f
    progress->_maxValue = 1.0f;

    // Get entity from data source +0x28, check ComponentEntity
    ComponentEntity* entity = BuildUI_Entity(_dataSource);

    // Find the BuildComponent (FUN_14000d6c0) → its BuildDescription
    BuildComponent* posComp = entity ? entity->FindComponent<BuildComponent>() : nullptr;

    // Find the WorkPlaceComponent (FUN_140002e10)
    WorkPlaceComponent* workplaceComp = entity ? entity->FindComponent<WorkPlaceComponent>() : nullptr;

    // Find "buttonPause" element, set ID 6000
    UI::Element* buttonPause = BuildUI_FindChildElem(dialogElem, "buttonPause");
    buttonPause->SetElementId(6000);

    // Configure pause button based on workplace state
    if (workplaceComp == nullptr) {
        // No workplace — disable the button (Element slot 8)
        buttonPause->Enable(false);
    } else {
        // The button is a check: checked = !isAccepting
        char wpEnabled = workplaceComp->isAccepting;
        static_cast<UI::CheckElement*>(buttonPause)->_checked = (wpEnabled == 0) ? 1 : 0;
    }

    // Get the BuildDescription from the BuildComponent
    BuildDescription* resourceComp = BuildUI_Description(posComp);
    int resourceCount = 0;
    if (resourceComp) {
        resourceCount = resourceComp->_costCount;
    }

    // Find the ClearAreaComponent (FUN_14000a170) → storage info
    ClearAreaComponent* smComp = entity ? entity->FindComponent<ClearAreaComponent>() : nullptr;

    // Find first child's "textTotal" element → store at +0x48
    // First child (row 0) is the header/total row
    UI::Element* firstChild = tableElem->Child(0).Get();
    textTotal = static_cast<UI::TextElement*>(BuildUI_FindChildElem(firstChild, "textTotal"));

    // Set header textTotal: param 0 = "0", param 1 = storage capacity
    {
        System::String zeroStr("0");
        textTotal->SetParameter(0, zeroStr);

        // smComp (the site's build-material storage, found via fn_FindComponent_SM =
        // ClearAreaComponent) can be null in standalone if that component isn't present
        // / its type-id didn't resolve. Retail always has it; guard the deref so a
        // partial panel (no capacity number) beats a crash.
        int capacity = smComp ? (int)smComp->clearState : 0;
        System::String capStr = System::String::Format("%d", (int)(uint32_t)capacity);
        textTotal->SetParameter(1, capStr);
    }

    // Iterate storage table rows (skip first child = header)
    int childCount = tableElem->ChildCount();
    int rowIdx = 0;

    for (int i = 1; i < childCount; i++) {
        // Get row child element
        UI::Element* rowElem = tableElem->Child(i).Get();

        // Find "labelStorage" and "textTotal" in this row (stored unchecked)
        UI::LabelElement* labelStorage =
            static_cast<UI::LabelElement*>(BuildUI_FindChildElem(rowElem, "labelStorage"));
        UI::TextElement* rowTextTotal =
            static_cast<UI::TextElement*>(BuildUI_FindChildElem(rowElem, "textTotal"));

        // Push {labelStorage, textTotal} into the row array at +0x30.

        // (BuildUI::rows, the +0x30 Array)
        rows.SetSize(rows.GetSize() + 1, StorageRow{labelStorage, rowTextTotal});

        // Populate row content based on resource index
        BuildCost* resBase = resourceComp ? resourceComp->_costs : nullptr;
        if (rowIdx < resourceCount) {
            // Active resource row: set amount, sprite, text, tooltip
            ComponentDescription* resEntry = resBase[rowIdx].material;

            // Find RawMaterialDescription in the entry's sub-descriptions
            int compCount = resEntry->_subDescCount;
            RawMaterialDescription* resType = nullptr;
            for (int c = 0; c < compCount; c++) {
                Object::Base* comp = resEntry->_subDescs[c];
                // Get TypeId via vtable[2] and compare with RawMaterialDescription
                int typeId = comp->Type();
                if (typeId == (int)RawMaterialDescription::TypeId()) {
                    if (::Object::IsDerivedFromNullSafe(comp, RawMaterialDescription::TypeId())) {
                        resType = static_cast<RawMaterialDescription*>(comp);
                    }
                    break;
                }
            }

            // Calculate required amount: quantity * multiplier
            int entryCount = resBase[rowIdx].quantity;
            int multiplier = posComp->costMultiplier;
            {
                System::String reqStr =
                    System::String::Format("%d", (int)(uint32_t)(entryCount * multiplier));
                rowTextTotal->SetParameter(1, reqStr);
            }

            // Set row textTotal param 0 = "0" initially
            {
                System::String zeroStr("0");
                rowTextTotal->SetParameter(0, zeroStr);
            }

            if (resType) {
                // Set sprite from resource type
                #ifdef _DEBUG
                uint8_t spriteBuf[16];
                Debug_LabelElement_SetSprite(labelStorage,
                    Debug_Sprite_Ctor(spriteBuf, resType->_spriteSheet, &resType->_name));
#else
                UI::Sprite sprite(Object::ExternalPtr<UI::SpriteSheet>(resType->_spriteSheet),
                                  resType->_name);
                labelStorage->SetSprite(&sprite);
#endif

                // Set text from resource type
                UI::String text(Object::ExternalPtr<UI::StringTable>(resType->_stringTable),
                                resType->_name);
                labelStorage->SetText(&text);

                // Set tooltip: "ResourceTip" from the label's tooltip string table
                System::String tipNameStr("ResourceTip");
                UI::String tip(labelStorage->_toolTip.GetStringTable(), tipNameStr);
                labelStorage->SetToolTip(tip);
            }
        } else if (rowIdx == resourceCount && resourceComp != nullptr) {
            // Total row: shows total required labour
            int totalCount = resourceComp->_i30;
            int multiplier = posComp->costMultiplier;
            System::String totStr =
                System::String::Format("%d", (int)(uint32_t)(totalCount * multiplier));
            rowTextTotal->SetParameter(1, totStr);
        } else {
            // Empty row: clear text and sprite (blanks table rows beyond the material
            // count — a WoodHouse uses only 2 of the 7 storageItem cells). This mirrors the
            // retail else-branch of 0x1400b1f40, which builds an EMPTY UI::String via the
            // DEFAULT ctor (UI::String::String()) and SetText's it.
            {
                UI::String emptyStr;
                rowTextTotal->SetText(&emptyStr);

                UI::String emptyStr2;
                labelStorage->SetText(&emptyStr2);
            }

            {
#ifdef _DEBUG
                uint8_t emptySprite[16];
                Debug_LabelElement_SetSprite(labelStorage, Debug_Sprite_DefaultCtor(emptySprite));
#else
                UI::Sprite emptySprite;
                labelStorage->SetSprite(&emptySprite);
#endif
            }
        }

        rowIdx++;
    }
}

// ============================================================
// BuildUI::OnUpdateLayout (vtable +0x38) — Update build progress and materials
// ============================================================
// Updates the build progress bar and per-resource delivered amounts.
// Reads the StorageComponent to determine how many materials have
// been delivered for each resource type. Calculates total progress
// ratio and displays as percentage.
//
// Original: 0x1400B1A50.

// [7] 0x1400b1a50
void BuildUI::OnUpdateLayout() {
    // Per-frame update: the framework fires this every tick, including before
    // Slot4 populates (progress / rows still null). Guard.
    if (!buildui_sa::IsSetUp(this)) return;
    if (!SA_DialogEntityAlive(this)) return;

    // Get entity, check ComponentEntity
    ComponentEntity* entity = BuildUI_Entity(_dataSource);

    // Find the BuildComponent → its BuildDescription
    BuildComponent* posComp = entity ? entity->FindComponent<BuildComponent>() : nullptr;
    BuildDescription* resourceComp = BuildUI_Description(posComp);

    // Find the ClearAreaComponent (storage info)
    ClearAreaComponent* smComp = entity ? entity->FindComponent<ClearAreaComponent>() : nullptr;
    // Progress/percent math below unconditionally derefs smComp (+0x48 capacity, +0x10
    // entity self-ref via CountOccupiedBuildTiles). If the build-material storage
    // component isn't found (standalone: missing component / unresolved type-id), skip
    // this update rather than fault. Slot4 already set an initial "0/total" display.
    if (smComp == nullptr) return;

    // Check if build is complete: cost * multiplier <= laborProgress
    if (posComp != nullptr && resourceComp != nullptr) {
        int totalRequired = resourceComp->_i30;
        int multiplier = posComp->costMultiplier;
        if (totalRequired * multiplier <= posComp->laborProgress) {
            return;  // Build complete, nothing to update
        }
    }

    // Calculate remaining capacity: capacity - occupied tiles
    int capacity = smComp->clearState;
    int occupied = CountOccupiedBuildTiles_Impl(smComp);
    uint32_t remaining = (uint32_t)(capacity - occupied);

    // Update header textTotal param 0 with remaining count
    {
        System::String remStr = System::String::Format("%d", (int)remaining);
        textTotal->SetParameter(0, remStr);
    }

    int totalRequired = 0;
    uint32_t totalDelivered = 0;

    if (posComp == nullptr || resourceComp == nullptr) {
        // No resource component — just use capacity
        totalRequired = smComp->clearState;
        totalDelivered = remaining;
    } else {
        // Find StorageComponent for delivered counts (DAT_1401c7e78, FUN_140006270)
        StorageComponent* storage = entity ? entity->FindComponent<StorageComponent>() : nullptr;

        // Iterate resource rows, query the storage for delivered counts
        int resourceCount = resourceComp->_costCount;
        int rowTotal = rows._size;
        if (rowTotal > resourceCount) rowTotal = resourceCount;

        int sumDelivered = 0;
        int sumCapacity = 0;

        for (int i = 0; i < rowTotal; i++) {
            BuildCost* resBase = resourceComp->_costs;
            ComponentDescription* resEntry = resBase[i].material;

            // Search the storage items for the matching resource entry
            int smCount = storage ? storage->storageSlots._size : 0;
            uint64_t delivered = 0;
            if (smCount > 0) {
                // StorageComponent's items {resource, _count, _reserve}.
                const StorageComponent::Item* smArray = storage->storageSlots._data;
                for (int j = 0; j < smCount; j++) {
                    const ComponentDescription* smEntry = smArray[j]._resource;
                    if (smEntry == resEntry) {
                        if (j >= 0) {
                            // Delivered = item[j]._count - item[j]._reserve
                            int count = smArray[j]._count;
                            int reserved = smArray[j]._reserve;
                            delivered = (uint64_t)(uint32_t)(count - reserved);
                        }
                        break;
                    }
                }
            }

            // Update row textTotal param 0 with delivered count
            {
                System::String delStr = System::String::Format("%d", static_cast<int>(delivered));
                rows[i].total->SetParameter(0, delStr);
            }

            sumDelivered += (int)delivered;
            int entryCount = resBase[i].quantity;
            int multiplier = posComp->costMultiplier;
            sumCapacity += entryCount * multiplier;
        }

        // Update the total row (at index = rowTotal in the array)
        {
            System::String rowStr =
                System::String::Format("%d", (int)(uint32_t)posComp->laborProgress);
            rows[rowTotal].total->SetParameter(0, rowStr);
        }

        // Calculate totals for progress
        int multiplier = posComp->costMultiplier;
        totalRequired = resourceComp->_i30 * multiplier + (int)smComp->clearState + sumCapacity;
        totalDelivered = posComp->laborProgress + sumDelivered + remaining;
    }

    // Calculate fill ratio and percentage
    float ratio = 0.0f;
    if ((float)totalRequired != 0.0f) {
        ratio = (float)(int)totalDelivered / (float)totalRequired;
    }
    int percent = (int)(ratio * 100.0f + 0.5f);

    // Update progress bar
    progress->_currentValue = ratio;
    System::String pctStr = System::String::Format("%d", (int)(uint32_t)percent);
    progress->SetProgressTextParameter(0, pctStr);
}

// Slot9: On button click (event 0x12, id 6000), toggle workplace enabled state.
// [9] 0x1400b1ea0
void BuildUI::OnElementEvent(Object::InternalPtr<UI::Element>* /*element*/, int32_t param3, int32_t param4,
                             int32_t /*c*/, int32_t /*d*/, int32_t /*e*/, void* /*userData*/) {
    if (!SA_DialogEntityAlive(this)) return;

    if (param3 == 0x12 && param4 == 6000) {
        // Get entity at +0x28, check IsA ComponentEntity
        ComponentEntity* checkedEntity = BuildUI_Entity(_dataSource);

        // Find the WorkPlaceComponent on the entity. Retail (BuildUI_Update 0x1400b1ea0)
        // calls FUN_140002e10 — the DAT_1401c80e0 (WorkPlaceComponent) finder. It must NOT
        // be fn_FindComponent_Work: despite the name that helper is the DAT_1401c8070
        // (StorageLocation/WorkComponent) finder, FUN_140003050.
        WorkPlaceComponent* workplaceComp =
            checkedEntity ? checkedEntity->FindComponent<WorkPlaceComponent>() : nullptr;

        // Read the accepting byte (+0x50), toggle it. The building need not have a
        // WorkPlaceComponent at all. The toggle is 0x14012fc00
        // WorkPlaceComponent_SetAccepting, called by name (B3R).
        if (workplaceComp == nullptr) {
            std::fprintf(stderr, "[BuildUI] Slot9 pause click ignored: workplaceComp=%p\n",
                         (void*)workplaceComp);
            std::fflush(stderr);
            return;
        }
        char currentState = workplaceComp->isAccepting;
        // WorkPlaceComponent_SetAccepting's member (a pure forwarder).
        workplaceComp->SetAccepting(currentState == 0);
    }
}
