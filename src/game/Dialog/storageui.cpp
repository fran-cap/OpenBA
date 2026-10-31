// src/game/Dialog/storageui.cpp
// Clean reimplementation of StorageUI panel methods.
//
// StorageUI (0x40 bytes): Storage contents display panel.
// Fields: Dialog/StorageUI.h (+0x30 the row array, StorageUIRow).
//
// Decompiled from Application-x64.exe.

#include "Dialog/ui_panels.h"
#include "Dialog/StorageUI.h"
#include "system/Value.h"
#include "ui/UserInterface.h"
#include "ui/DialogInstance.h"
#include "object/TypeInfo.h"
#include "system/String.h"
#include "object/External.h"   // ENDGAME B.3.3 step 6A
#include <cstdint>
#include <cstdlib>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "ui/Element.h"  // ENDGAME B.3.6 UI callers (K3)
#include "ui/String.h"  // ENDGAME B.3.6 UI callers (K3)
#include "ui/Sprite.h"  // ENDGAME B.3.6 UI callers (K3)
#include "ui/SpriteSheet.h"  // ENDGAME B.3.6 UI callers (K3)
#include "ui/StringTable.h"  // ENDGAME B.3.6 UI callers (K3)
#include "ui/LabelElement.h"  // ENDGAME B.3.6 UI callers round 2 (K3)
#include "ui/NumberElement.h"  // ENDGAME B.3.6 UI callers round 2 (K3)
#include "ui/TableElement.h"  // ENDGAME B.3.6 UI callers round 2 (K3)
#include "ui/ElementDescription.h"  // ENDGAME B.3.6 UI callers round 2 (K3)
#include "ui/TextElement.h"  // ENDGAME B.3.6 UI callers round 2 (K3)
#include "Object/factory_registration.h"
#include <cstddef>
#include "Component/RawMaterialDescription.h"   // X::TypeId() (B.3.8 callers)
#include "Component/ComponentDescription.h"
#include "Component/Description.h"
#include "Component/StorageComponent.h"
#include "Entity/ComponentEntity.h"   // X::TypeId() (B.3.8 callers)
#include "Dialog/exe_bodies.h"
#include "system/MemoryAllocator.h"   // B3R: the allocator doors

// One entry of OnUpdateLayout's temporary list (stride 0x18, built by the
// inlined Array push in 0x1400b8450, sorted by 0x1400213f0): a stored
// material, its RawMaterialDescription and the amount.
struct StorageUIItem {
    ComponentDescription*   resource;      // +0x00 the storage item's template
    RawMaterialDescription* rawMaterial;   // +0x08 its RawMaterialDescription (or null)
    int64_t                 count;         // +0x10 the amount (read as its low int)
};
static_assert(sizeof(StorageUIItem) == 0x18, "StorageUI item stride 0x18 (0x1400213f0)");

static void StorageUI_SortItems(StorageUIItem* items, int count);

// Helper: find element by name under `parentElem`
static UI::Element* StorageUI_FindChildElem(UI::Element* parentElem, const char* name) {
    System::String nameStr(name);
    return parentElem->FindElement(nameStr).Get();
}

// ============================================================
// StorageUI::OnCreated (vtable +0x20) — Setup storage table
// ============================================================
// Finds "storageTable" (or "storageTable2" fallback). Counts resource
// types via ExternalFactory::FindNext. Resizes table to fit, clones
// template elements into each cell. Finds "labelStorage" and
// "numberCount" sub-elements in each row, stores in dynamic array.
// Finally calls OnUpdateLayout (vtable +0x38).
//
// Original: 0x1400b8820.

// [4] 0x1400b8820
void StorageUI::OnCreated() {
    // Find "storageTable", fallback to "storageTable2"
    UI::Element* tableRaw = StorageUI_FindChildElem(_container, "storageTable");
    if (!tableRaw) {
        tableRaw = StorageUI_FindChildElem(_container, "storageTable2");
    }
    UI::TableElement* tableElem = static_cast<UI::TableElement*>(tableRaw);

    // Count resource types via ExternalFactory::FindNext
    uint32_t resourceTypeCount = 0;
    const ::Object::ExternalFactory* efactory = ::Object::ExternalFactory::_singleton;
    const ::Object::External* ext =
        efactory->FindNext(RawMaterialDescription::TypeId(), nullptr);
    while (ext != nullptr) {
        resourceTypeCount++;
        ext = efactory->FindNext(RawMaterialDescription::TypeId(), ext);
    }

    // The table's column count
    int colCount = tableElem->GetColumnCount();

    // Calculate row count: ceil(resourceTypeCount / colCount)
    int rowCount = ((int)(resourceTypeCount - 1 + (uint32_t)colCount) / colCount * colCount) / colCount;

    // The template: the first cell's ElementDescription
    UI::Element* firstChild = tableElem->Child(0).Get();
    const UI::ElementDescription* templateObj = firstChild->_description;

    // Ensure minimum row count from the table's current row count
    int minRows = tableElem->GetRowCount();
    if (rowCount < minRows) {
        rowCount = minRows;
    }

    // Resize the table
    tableElem->SetSize(rowCount, colCount);

    // Clone template elements into each table cell
    int totalCells = tableElem->ChildCount();
    for (int i = 0; i < totalCells; i++) {
        // ElementDescription slot 8 CreateInstance(ret, dialog) -- a RETSLOT
        // virtual (RULE B6-2) -- with the owning dialog's resource.
        Object::InternalPtr<UI::Element> clone;
        templateObj->CreateInstance(&clone, _dialog->_dialog);

        // Set cloned element as child of table
        tableElem->SetChild(&clone, i);
    }

    // Iterate all cells, find "labelStorage" and "numberCount" sub-elements
    totalCells = tableElem->ChildCount();
    for (int i = 0; i < totalCells; i++) {
        UI::Element* rowElem = tableElem->Child(i).Get();

        StorageUIRow row;
        row.labelStorage = static_cast<UI::LabelElement*>(StorageUI_FindChildElem(rowElem, "labelStorage"));
        row.numberCount = static_cast<UI::NumberElement*>(StorageUI_FindChildElem(rowElem, "numberCount"));
        row.cell = tableElem->Child(i).Get();

        // Append the row (Array<T>::SetSize with the fill).
        rows.SetSize(rows.GetSize() + 1, row);
    }

    // Slot 7 (vtable +0x38), virtual as retail
    OnUpdateLayout();
}

// ============================================================
// StorageUI::OnUpdateLayout (vtable +0x38) — Update storage contents
// ============================================================
// Gets entity's StorageComponent, iterates storage entries.
// For each entry with items, finds the RawMaterialDescription.
// Builds a temp array sorted alphabetically by resource name.
// Displays items in table rows with sprite, name, and count.
// Hides unused rows.
//
// Original: 0x1400b8450.

// [7] 0x1400b8450
void StorageUI::OnUpdateLayout() {
    if (!SA_DialogEntityAlive(this)) return;

    // The entity from the data source (IsA ComponentEntity)
    Object::Base* dataSource = _dataSource;
    ComponentEntity* entity = nullptr;
    if (::Object::IsDerivedFromNullSafe(dataSource, ComponentEntity::TypeId()))
        entity = static_cast<ComponentEntity*>(dataSource);

    // Find StorageComponent (retail FUN_140006270). The tree's finder returned
    // null for a null entity: kept.
    StorageComponent* storageComp = entity ? entity->FindComponent<StorageComponent>() : nullptr;

    // Build temp array of storage items
    StorageUIItem* sortedItems = nullptr;
    StorageUIItem* currentItems = nullptr;
    int itemCount = 0;
    int itemCapacity = 0;

    int smEntryCount = storageComp ? storageComp->storageSlots._size : 0;
    if (smEntryCount > 0) {
        itemCount = 0;

        for (int i = 0; i < smEntryCount; i++) {
            const StorageComponent::Item& entry = storageComp->storageSlots[i];
            int count = entry._count;

            if (count != 0) {
                ComponentDescription* resEntry = entry._resource;

                // Find the RawMaterialDescription in the entry's description list
                int compCount = resEntry->_subDescCount;
                RawMaterialDescription* resType = nullptr;
                for (int c = 0; c < compCount; c++) {
                    Description* comp = resEntry->_subDescs[c];
                    int typeId = comp->Type();
                    if (typeId == (int)RawMaterialDescription::TypeId()) {
                        if (::Object::IsDerivedFromNullSafe(comp, RawMaterialDescription::TypeId())) {
                            resType = static_cast<RawMaterialDescription*>(comp);
                        }
                        break;
                    }
                }

                // Build the 0x18-byte item {resEntry, resType, count}
                StorageUIItem item;
                item.resource = resEntry;
                item.rawMaterial = resType;
                item.count = count;

                // Push into temp array.
                // B.3.2 SKIPPED: this array's header is four independent locals
                // (sortedItems/currentItems/itemCount/itemCapacity), and its
                // lifetime is threaded through MemFree(currentItems) far below plus
                // a sort call in between; it is left as the hand-rolled growth.
                int newCount = itemCount + 1;
                if (newCount != itemCount) {
                    if (itemCapacity < newCount) {
                        itemCapacity = newCount;
                        if (itemCount != 0) {
                            itemCapacity = newCount * 3;
                        }
                        sortedItems = static_cast<StorageUIItem*>(
                            System::MemoryAllocator::MemAlloc((uint64_t)itemCapacity * sizeof(StorageUIItem), 8));
                    }

                    int copyStart = (currentItems == sortedItems) ? itemCount : 0;
                    for (int k = copyStart; k < newCount; k++) {
                        StorageUIItem* dst = sortedItems + k;
                        if (dst) {
                            const StorageUIItem* src = &item;
                            if (k < itemCount) {
                                src = currentItems + k;
                            }
                            *dst = *src;
                        }
                    }
                    if (currentItems != sortedItems) {
                        System::MemoryAllocator::MemFree(currentItems);
                    }
                    currentItems = sortedItems;
                    itemCount = newCount;
                }
            }
        }

        // Sort items alphabetically by resource name
        if (itemCount > 1) {
            StorageUI_SortItems(sortedItems, itemCount);
        }
    }

    // Populate table rows with sorted items
    int rowIdx = 0;
    if (itemCount > 0) {
        for (int i = 0; i < itemCount; i++) {
            if (rowIdx >= rows.GetSize()) break;

            StorageUIRow& row = rows[rowIdx];
            const RawMaterialDescription* resType = sortedItems[i].rawMaterial;
            int count = (int)sortedItems[i].count;

            // Set sprite from resource type
            UI::Sprite sprite(Object::ExternalPtr<UI::SpriteSheet>(resType->_spriteSheet), resType->_name);
            row.labelStorage->SetSprite(&sprite);

            // Set text from resource type
            UI::String text(Object::ExternalPtr<UI::StringTable>(resType->_stringTable), resType->_name);
            row.labelStorage->SetText(&text);

            // Set count
            row.numberCount->SetValue(static_cast<int32_t>(count));

            // Show both elements (flags: hide=0, show=1)
            row.labelStorage->SetFlagsOnOff(0, 1);
            row.numberCount->SetFlagsOnOff(0, 1);

            rowIdx++;
        }
    }

    // Hide unused rows
    int totalRows = rows.GetSize();
    for (int i = rowIdx; i < totalRows; i++) {
        // Hide both elements (flags: hide=1, show=0)
        rows[i].labelStorage->SetFlagsOnOff(1, 0);
        rows[i].numberCount->SetFlagsOnOff(1, 0);
    }

    // Free temp sorted items array
    System::MemoryAllocator::MemFree(currentItems);
}

// ============================================================
// StorageUI_SortItems — Introsort on 0x18-byte resource display structs
// ============================================================
// Faithful reimplementation of FUN_1400213f0.
// Sorts the {resource, rawMaterial, count} items alphabetically by resource
// name using introsort (quicksort + selection sort fallback).
//
// Comparison (FUN_1400b8370): builds a UI::String from the RawMaterialDescription's
// string table and name, converts it to System::String, and compares with
// System::String::operator<.

// Compare: returns true if name(a) < name(b)
static bool StorageUI_CompareItems(const StorageUIItem* a, const StorageUIItem* b) {
    const RawMaterialDescription* resA = a->rawMaterial;
    const RawMaterialDescription* resB = b->rawMaterial;

    // Build UIString -> convert to System::String for item A
    UI::String uiStrA(Object::ExternalPtr<UI::StringTable>(resA->_stringTable), resA->_name);
    System::String tempA(uiStrA.operator System::String());
    System::String sysStrA(tempA);

    // Build UIString -> convert to System::String for item B
    UI::String uiStrB(Object::ExternalPtr<UI::StringTable>(resB->_stringTable), resB->_name);
    System::String tempB(uiStrB.operator System::String());
    System::String sysStrB(tempB);

    return sysStrA < sysStrB;
}

// Swap two items
static inline void StorageUI_SwapItems(StorageUIItem* a, StorageUIItem* b) {
    StorageUIItem t = *a;
    *a = *b;
    *b = t;
}

// Selection sort for small partitions (operator_impl_5ad0 / FUN_140015ad0)
// Finds max element and swaps it to end, shrinking unsorted region.
static void StorageUI_SelectionSort(StorageUIItem* first, StorageUIItem* last) {
    for (StorageUIItem* end = last; first < end; end -= 1) {
        StorageUIItem* best = first;
        StorageUIItem* scan = first;
        while (scan + 1 <= end) {
            if (StorageUI_CompareItems(best, scan + 1)) {
                best = scan + 1;
            }
            scan += 1;
        }
        StorageUI_SwapItems(best, end);
    }
}

// Introsort: quicksort with selection sort fallback for <9 elements.
// Faithful reimplementation of FUN_1400213f0.
static void StorageUI_SortItems(StorageUIItem* items, int count) {
    if (count < 2) return;

    StorageUIItem* param_1 = items;                 // lo pointer (mutable)
    StorageUIItem* plVar11 = items + (count - 1);   // hi pointer

    // Explicit stack matching original (two parallel arrays of 62 entries)
    StorageUIItem* stackLo[62];
    StorageUIItem* stackHi[62];
    int64_t lVar7 = 0;  // stack depth

    for (;;) {
        int64_t span = (plVar11 - param_1) + 1;

        if (span < 9) {
            // Selection sort for small partition
            if (param_1 < plVar11) {
                StorageUI_SelectionSort(param_1, plVar11);
            }
            goto pop;
        }

        // Median-of-three pivot selection
        {
            StorageUIItem* plVar8 = param_1 + (span >> 1);  // mid = pivot

            if (StorageUI_CompareItems(plVar8, param_1))
                StorageUI_SwapItems(param_1, plVar8);
            if (StorageUI_CompareItems(plVar11, param_1))
                StorageUI_SwapItems(param_1, plVar11);
            if (StorageUI_CompareItems(plVar11, plVar8))
                StorageUI_SwapItems(plVar8, plVar11);

            // Partition
            StorageUIItem* plVar6 = param_1;   // forward scan pointer
            StorageUIItem* plVar9 = plVar11;   // backward scan pointer
            StorageUIItem* plVar10;            // trailing pointer (one behind plVar9)

        partition:
            // Forward scan: find element where pivot < elem
            if (plVar6 < plVar8) {
                do {
                    plVar6 += 1;
                    if (plVar8 <= plVar6) goto forward_past_pivot;
                } while (!StorageUI_CompareItems(plVar8, plVar6));
                if (plVar8 <= plVar6) goto forward_past_pivot;
            } else {
            forward_past_pivot:
                do {
                    plVar6 += 1;
                    if (plVar11 < plVar6) break;
                } while (!StorageUI_CompareItems(plVar8, plVar6));
            }

            // Backward scan: find element where pivot < elem is false
            do {
                plVar10 = plVar9;
                plVar9 = plVar10 - 1;
                if (plVar9 <= plVar8) break;
            } while (StorageUI_CompareItems(plVar8, plVar9));

            if (plVar6 <= plVar9) {
                // Swap and continue partitioning
                StorageUI_SwapItems(plVar6, plVar9);
                if (plVar8 == plVar9) {
                    plVar8 = plVar6;  // pivot moved
                }
                goto partition;
            }

            // Find final pivot boundary (plVar10 tracks the split point)
            if (plVar8 < plVar10) {
                do {
                    plVar10 -= 1;
                    if (plVar10 <= plVar8) goto scan_left;
                    bool less = StorageUI_CompareItems(plVar10, plVar8);
                    if (less) break;
                    bool greater = StorageUI_CompareItems(plVar8, plVar10);
                    if (greater) break;
                } while (true);
                if (plVar10 <= plVar8) goto scan_left;
            } else {
            scan_left:
                do {
                    plVar10 -= 1;
                    if (plVar10 <= param_1) break;
                    bool less = StorageUI_CompareItems(plVar10, plVar8);
                    if (less) break;
                    bool greater = StorageUI_CompareItems(plVar8, plVar10);
                    if (greater) break;
                } while (true);
            }

            // Decide which partition to push vs iterate (retail divides the byte
            // distances by the 0x18 stride; the element distances are the same)
            int64_t param_3 = plVar11 - plVar6;
            int64_t leftSize = plVar10 - param_1;

            if (param_3 <= leftSize) {
                // Left partition is large: push left, iterate right
                if (param_1 < plVar10) {
                    stackLo[lVar7] = param_1;
                    stackHi[lVar7] = plVar10;
                    lVar7++;
                }
                param_1 = plVar6;
                if (plVar11 <= plVar6) goto pop;
            } else {
                // Right partition is large: push right, iterate left
                if (plVar6 < plVar11) {
                    stackLo[lVar7] = plVar6;
                    stackHi[lVar7] = plVar11;
                    lVar7++;
                }
                plVar11 = plVar10;
                if (plVar10 <= param_1) goto pop;
            }
            continue;
        }

    pop:
        lVar7--;
        if (lVar7 < 0) return;
        param_1 = stackLo[lVar7];
        plVar11 = stackHi[lVar7];
    }
}

// ============================================================
// STANDALONE promotion — real StorageUI controller vtable
// ============================================================
// The citizen (and building) info panel's Inventory/Storage tab is driven by a
// StorageUI controller bound to the panel's `storageTable`. In standalone this
// type was a StubNop, so the tab never built or populated its rows. The class
// now has:
//
// Vtable offsets (verified from the exe StorageUI vtable @ 0x14015f970):
//   +0x00 (idx0) ScalarDtor (0x140021390: ~Array on the rows, ~DialogController)
//   +0x10 (idx2) GetTypeId
//   +0x20 (idx4) OnCreated = table build (setup; framework calls once). It calls
//                OnUpdateLayout (via vtable[7]) at its end to do the initial populate.
//   +0x38 (idx7) OnUpdateLayout = populate rows from the entity's carried inventory.
//   +0x40 (idx8)           = base DialogController::OnUpdate, inherited (table slot
//                0x14003a96c). OnCreated runs from DialogInstance::AddController
//                (0x18011343d); OnUpdateLayout every frame from
//                DialogInstance::ComputeLayout (B6 row 36).

// create 0x140021330: MemAlloc(0x40, 8), then the ctor (DialogController ->
// Internal: AddObject, the compiler's stamps, the field initialisers).
::Object::Base* StorageUI::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(StorageUI), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) StorageUI());
}
// Retail's createInPlace does not build a controller in place (B8-13); the
// tree's in-place entry did, and that is kept.
::Object::Base* StorageUI::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) StorageUI());
}

DEFINE_EXE_TYPE(StorageUI, "UI::DialogController")

