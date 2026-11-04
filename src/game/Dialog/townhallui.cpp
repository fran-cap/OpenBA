// src/game/Dialog/townhallui.cpp
// Clean reimplementation of TownHallUI vtable methods.
//
// TownHallUI (0x1E8 bytes), vtable at 0x14015dce0.
// Extends DialogController (0x30 base). Multi-tab town overview panel.
// The field layout is TownHallUI.h's.
//
// All 15 TownHallUI methods are members (retail addresses in the header):
//   InitGeneral, InitGraph, InitProfessions, InitProduction,
//   InitInventory, InitAgriculture, UpdateGraph, UpdateGeneral,
//   UpdateProfessions, UpdateProduction, UpdateAgriculture,
//   UpdateInventory, UpdateAll, OnProfessionButton, OnProfessionSpinner
//
// READS LEFT AT AN OFFSET (B.4, owner named on each line):
//   GameUpdateLoop::m_citizenList (the town manager, CitizenBehavior: WORK-a):
//     its citizen list, homes, statistics history, production records, entity
//     trees and lists.
//   GameUpdateLoop::m_dayCounter (W0: void*): the year and the time of year.
//   ProfessionList::arrayData (GAME-b: void*, H5 raw) and Profession's String
//     keys (GAME-b: H5 raw).
//   StorageComponent::storageSlots (W0 raw storage).
//   ComponentDescription::_subDescs (COMP-c: void**).
//   UI::Element::_description (R-UI-a: void*).
//   The exe helpers' untyped parameters (the
//   CitizenEmploy strategies, Avl38SetInsert 0x140014890: toolbar_utility_helpers.cpp).

#include "Dialog/Panels.h"
#include "Dialog/TownHallUI.h"
#include "ui/UserInterface.h"
#include "ui/DialogInstance.h"
#include "object/TypeInfo.h"
#include "Game/exe_bodies.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Game/ProfessionList.h"
#include "Game/Profession.h"
#include "Component/ValueDescription.h"   // ENDGAME B.3.9 TR: ValueDescription::TypeId()
#include "Component/CitizenComponent.h"
#include "Entity/CitizenBehavior.h"
#include "collection/Collection.h"
#include "system/String.h"
#include "system/Filename.h"
#include "object/External.h"   // ENDGAME B.3.3 step 6A
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch D
#include "io/Stream.h"
#include "system/Value.h"    // ENDGAME B.3.4b System callers: System::Time
#include <new>
#include "ui/Element.h"      // ENDGAME B.3.6 UI callers (K1)
#include "ui/ElementDescription.h"  // ENDGAME B.3.6 UI callers (K1)
#include "ui/String.h"       // ENDGAME B.3.6 UI callers (K1)
#include "ui/Sprite.h"       // ENDGAME B.3.6 UI callers (K1)
#include "ui/StringTable.h"  // ENDGAME B.3.6 UI callers (K1)
#include "ui/TextElement.h"       // ENDGAME B.3.6 UI callers round 2 (K1)
#include "ui/LabelElement.h"      // ENDGAME B.3.6 UI callers round 2 (K1)
#include "ui/NumberElement.h"     // ENDGAME B.3.6 UI callers round 2 (K1)
#include "ui/NumberEditElement.h" // ENDGAME B.3.6 UI callers round 2 (K1)
#include "ui/ComboElement.h"      // ENDGAME B.3.6 UI callers round 2 (K1)
#include "ui/GraphElement.h"      // ENDGAME B.3.6 UI callers round 2 (K1)
#include "ui/SpinnerElement.h"    // ENDGAME B.3.6 UI callers round 2 (K1)
#include "ui/TableElement.h"      // ENDGAME B.3.6 UI callers round 2 (K1)
#include "ui/TabElement.h"        // ENDGAME B.3.6 UI callers round 2 (K1)
#include "ui/ButtonElement.h"      // X::TypeId() (B.3.9 CG2)
#include "Component/RawMaterialDescription.h"   // X::TypeId() (B.3.9 CG2)
#include "Component/CitizenDescription.h"   // X::TypeId() (B.3.9 CG2)
#include "Component/SelectDescription.h"   // X::TypeId() (B.3.9 CG2)
#include "Component/ComponentDescription.h"   // X::TypeId() (B.3.9 CG2)
#include "Component/ResidenceComponent.h"   // X::TypeId() (B.3.9 CG2)
#include "Component/TradeComponent.h"   // X::TypeId() (B.3.9 CG2)
#include "Component/StorageComponent.h"   // X::TypeId() (B.3.9 CG2)
#include "Component/WorkPlaceComponent.h"   // X::TypeId() (B.3.9 CG2)
#include "Component/WorkComponent.h"   // X::TypeId() (B.3.9 CG2)
#include "Entity/ComponentEntity.h"
#include "Object/factory_registration.h"
#include <cstddef>
#include "Dialog/exe_bodies.h"
#include "Component/exe_bodies.h"   // B3R: CitizenEmploy_Strat_*
#include "system/MemoryAllocator.h"   // B3R: the allocator doors
#include "Action/PathAvl.h"   // AvlRebalance (0x140013480)
#include "Work/exe_bodies.h"   // B3R: WorkBase_TreeNodeFree_14c30
#include "Game/Calendar.h"     // GameUpdateLoop::m_dayCounter (year, yearTime)
#include "Game/PlayerToolManager.h"   // CenterOnEntity 0x140103040
#include "Component/AttendanceComponent.h"   // B4 S4 RT2: InternalPtr<AttendanceComponent>::Get() needs the class complete

// ============================================================
// Helpers
// ============================================================

// Find element by name in dialog (the tree's InitGeneral/InitGraph lookups
// keep it without a type check; retail 0x1400807f0 checks IsDerivedFrom).
static UI::Element* TownHall_FindElemRaw(UI::Element* dialogElem, const char* name) {
    System::String strBuf(name);
    return dialogElem->FindElement(strBuf).Get();
}

// Find element by name with IsDerivedFrom check
template <class T>
static T* TownHall_FindElem(UI::Element* dialogElem, const char* name) {
    System::String strBuf(name);
    UI::Element* elem = dialogElem->FindElement(strBuf).Get();

    T* castElem = nullptr;
    if (elem) {
        if (::Object::IsDerivedFromNullSafe(elem, T::TypeId())) castElem = static_cast<T*>(elem);
    }
    return castElem;
}

// Keep an element only when IsDerivedFrom T (retail's inlined narrowing).
template <class T>
static T* CastElement(UI::Element* elem) {
    if (!elem) return nullptr;
    if (::Object::IsDerivedFromNullSafe(elem, T::TypeId())) return static_cast<T*>(elem);
    return nullptr;
}

// ============================================================
// TownHallUI::InitGeneral (0x1400807f0)
// ============================================================
void TownHallUI::InitGeneral() {
    UI::Element* dialogElem = _container;

    numberYear      = static_cast<UI::NumberElement*>(TownHall_FindElemRaw(dialogElem, "numberYear"));
    textMonth       = static_cast<UI::TextElement*>(TownHall_FindElemRaw(dialogElem, "textMonth"));
    numberHomes     = static_cast<UI::NumberElement*>(TownHall_FindElemRaw(dialogElem, "numberHomes"));
    numberCitizens  = static_cast<UI::NumberElement*>(TownHall_FindElemRaw(dialogElem, "numberCitizens"));
    numberFamily    = static_cast<UI::NumberElement*>(TownHall_FindElemRaw(dialogElem, "numberFamily"));
    numberAdults    = static_cast<UI::NumberElement*>(TownHall_FindElemRaw(dialogElem, "numberAdults"));
    numberStudents  = static_cast<UI::NumberElement*>(TownHall_FindElemRaw(dialogElem, "numberStudents"));
    numberChildren  = static_cast<UI::NumberElement*>(TownHall_FindElemRaw(dialogElem, "numberChildren"));
    percentClothed  = static_cast<UI::TextElement*>(TownHall_FindElemRaw(dialogElem, "percentClothed"));
    percentEducated = static_cast<UI::TextElement*>(TownHall_FindElemRaw(dialogElem, "percentEducated"));

    UI::Element* heartsElem = TownHall_FindElemRaw(dialogElem, "groupHearts");
    TownHall_GroupRatingInit(&groupHearts, &heartsElem);   // 0x14006abf0

    UI::Element* starsElem = TownHall_FindElemRaw(dialogElem, "groupStars");
    TownHall_GroupRatingInit(&groupStars, &starsElem);   // 0x14006abf0
}

// ============================================================
// TownHallUI::InitGraph (0x14007f0a0)
// ============================================================
void TownHallUI::InitGraph() {
    UI::Element* dialogElem = _container;

    graphData  = static_cast<UI::GraphElement*>(TownHall_FindElemRaw(dialogElem, "graphData"));
    textXMin   = static_cast<UI::TextElement*>(TownHall_FindElemRaw(dialogElem, "textXMin"));
    timeCombo  = static_cast<UI::ComboElement*>(TownHall_FindElemRaw(dialogElem, "timeCombo"));
    graphCombo = static_cast<UI::ComboElement*>(TownHall_FindElemRaw(dialogElem, "graphCombo"));

    timeCombo->UI::Element::SetElementId(0x1964);

    graphCombo->UI::Element::SetElementId(0x1965);

    UI::NumberElement** const numberPtrs[5] = { &number0, &number1, &number2, &number3, &number4 };
    for (int i = 0; i < 5; i++) {
        System::String fmtBuf = System::String::Format("number%i", (int)i);
        *numberPtrs[i] = static_cast<UI::NumberElement*>(_container->FindElement(fmtBuf).Get());
    }

    UI::LabelElement** const labelPtrs[4] = { &label0, &label1, &label2, &label3 };
    for (int i = 0; i < 4; i++) {
        System::String fmtBuf = System::String::Format("label%i", (int)i);
        *labelPtrs[i] = static_cast<UI::LabelElement*>(_container->FindElement(fmtBuf).Get());
    }

    // Load string tables
    {
        System::Filename filenameBuf("Dialog/StringTable.rsc:graphYears");
        ::Object::External* ext = ::Object::ExternalFactory::_singleton->FindOrLoadAs<::Object::External>(filenameBuf);
        graphYearsTable = (ext && ::Object::IsDerivedFromNullSafe(ext, UI::StringTable::TypeId()))
            ? static_cast<UI::StringTable*>(ext) : nullptr;
    }
    {
        System::Filename filenameBuf("Dialog/StringTable.rsc:graphTypes");
        ::Object::External* ext = ::Object::ExternalFactory::_singleton->FindOrLoadAs<::Object::External>(filenameBuf);
        graphTypesTable = (ext && ::Object::IsDerivedFromNullSafe(ext, UI::StringTable::TypeId()))
            ? static_cast<UI::StringTable*>(ext) : nullptr;
    }

    // Populate time combo
    for (int i = 0; i < 7; i++) {
        UI::String uiStr(::Object::ExternalPtr<UI::StringTable>(graphYearsTable), i);
        System::String sysStrBuf;
        ::new (&sysStrBuf) System::String(uiStr.operator System::String());
        UI::Sprite emptySprite;
        timeCombo->AddItem(emptySprite, sysStrBuf, nullptr);
    }

    // Populate graph combo (skip empty entries)
    int typeCount = graphTypesTable->GetCount();
    int maxEntries = (13 < typeCount) ? typeCount : 13;
    for (int i = 0; i < maxEntries; i++) {
        System::String textBuf;
        ::new (&textBuf) System::String(graphTypesTable->GetText(i));
        bool isEmpty = textBuf.GetLength() == 0;

        if (!isEmpty) {
            UI::String uiStr(::Object::ExternalPtr<UI::StringTable>(graphTypesTable), i);
            System::String sysStrBuf;
            ::new (&sysStrBuf) System::String(uiStr.operator System::String());
            UI::Sprite emptySprite;
            graphCombo->AddItem(emptySprite, sysStrBuf, nullptr);
        }
    }

    timeCombo->SetCurrentItem(0);
    graphCombo->SetCurrentItem(0);

    UpdateGraph();
}

// ============================================================
// TownHallUI::InitProfessions (0x140080160)
// ============================================================
// Sets up the professions table: finds "tableProfessions", sizes it
// to profession count, clones rows from template, extracts child
// elements (label, numberEdit, number, spinner, button) per row,
// inserts profession type into BST, sets text/sprite/tooltip/IDs,
// and pushes profession entries into the dynamic array at +0x40.
void TownHallUI::InitProfessions() {
    // Find "tableProfessions" and cast to TableElement
    UI::TableElement* table = TownHall_FindElem<UI::TableElement>(_container, "tableProfessions");

    // Get profession list from game singleton
    ProfessionList* profList = GameUpdateLoop::_singleton->m_professionList;

    // Get template row (Child(0)) and its description (the clonable object at
    // field +0xB8 of the template element — NOT vtable+0xB8)
    UI::Element* templateChild = table->Child(0).Get();
    const UI::ElementDescription* cloneObj =
        templateChild->_description;

    // Set table size to profession count x 1
    int profCount = profList->professions.GetSize();
    table->SetSize(profCount, 1);

    // Iterate rows
    for (int i = 0; i < profCount; i++) {
        // Clone template row into table slot: cloneObj is the template row's
        // Element::_description, so this is UI::ElementDescription slot 8,
        // CreateInstance (retslot, B6-2).
        ::Object::InternalPtr<UI::Element> cloneBuf;
        ::Object::InternalPtr<UI::Element>* clonedElem =
            cloneObj->CreateInstance(&cloneBuf, _dialog->_dialog);

        table->SetChild(clonedElem, i, 0);

        // Get the cloned row element
        UI::Element* rowElem = table->Child(i).Get();

        // Get row's child(1) — the spinner/content container
        UI::Element* contentElem = rowElem->Child(1).Get();

        // Profession row (0x40 bytes for ProfArrayPush), zeroed
        ProfessionRow profEntry = {};

        // Row child 0: LabelElement (profession icon/name)
        UI::LabelElement* label = CastElement<UI::LabelElement>(rowElem->Child(0).Get());
        profEntry.label = label;

        // Content child 0: NumberEditElement (desired worker count spinner)
        profEntry.desired = CastElement<UI::NumberEditElement>(contentElem->Child(0).Get());

        // Row child 2: NumberElement (current count display)
        profEntry.slotsNumber = CastElement<UI::NumberElement>(rowElem->Child(2).Get());

        // Row child 3: NumberElement (another counter)
        profEntry.buildingsNumber = CastElement<UI::NumberElement>(rowElem->Child(3).Get());

        // Row child 4: ButtonElement (go-to button)
        UI::ButtonElement* button = CastElement<UI::ButtonElement>(rowElem->Child(4).Get());
        profEntry.button = button;

        // Get profession type entry: profList->types[i]
        Profession* profTypeId = profList->professions.GetData()[i];
        profEntry.workers = i;  // profession index

        // Insert into profession BST at +0x50
        ProfessionNode* curNode = professionTree.root;

        if (curNode == nullptr) {
            // Allocate first BST node (0x30 bytes)
            ProfessionNode* newNode = static_cast<ProfessionNode*>(System::MemoryAllocator::MemAlloc(0x30, 8));
            if (newNode) {
                newNode->key = profTypeId;          // +0x00: key
                newNode->index = (uint64_t)i;       // +0x08: index
                newNode->parent = nullptr;          // +0x10: parent
                newNode->left = nullptr;            // +0x18: left
                newNode->right = nullptr;           // +0x20: right
                newNode->balance = 0;               // +0x28: balance
                newNode->height = 0;
            }
            professionTree.root = newNode;
        } else {
            // Walk BST and insert
            ProfessionNode* parent = nullptr;
            bool inserted = false;
            while (!inserted) {
                parent = curNode;
                if (profTypeId < curNode->key) {
                    // Go left
                    if (curNode->left == nullptr) {
                        ProfessionNode* newNode = static_cast<ProfessionNode*>(System::MemoryAllocator::MemAlloc(0x30, 8));
                        if (newNode) {
                            newNode->key = profTypeId;
                            newNode->index = (uint64_t)i;
                            newNode->parent = parent;
                            newNode->left = nullptr;
                            newNode->right = nullptr;
                            newNode->balance = 0;
                            newNode->height = 0;
                        }
                        parent->left = newNode;
                        // Rebalance
                        AvlRebalance(&professionTree.root, &curNode);   // 0x140013480
                        inserted = true;
                    } else {
                        curNode = curNode->left;
                    }
                } else if (profTypeId > curNode->key) {
                    // Go right
                    if (curNode->right == nullptr) {
                        ProfessionNode* newNode = static_cast<ProfessionNode*>(System::MemoryAllocator::MemAlloc(0x30, 8));
                        if (newNode) {
                            newNode->key = profTypeId;
                            newNode->index = (uint64_t)i;
                            newNode->parent = parent;
                            newNode->left = nullptr;
                            newNode->right = nullptr;
                            newNode->balance = 0;
                            newNode->height = 0;
                        }
                        parent->right = newNode;
                        AvlRebalance(&professionTree.root, &curNode);   // 0x140013480
                        inserted = true;
                    } else {
                        curNode = curNode->right;
                    }
                } else {
                    // Equal — duplicate, skip (go to label setup)
                    inserted = true;
                }
            }
        }
        professionTree.count = professionTree.count + 1;

        // Set label text from the profession:
        //   +0x20 = StringTable pointer, +0x28 = SpriteSheet pointer,
        //   +0x30 = name key, +0x38 = tooltip key (System::Strings)
        Profession* prof = profTypeId;
        UI::String uiStr(::Object::ExternalPtr<UI::StringTable>(prof->stringTable),
                         prof->stringName);
        UI::String uiStrCopy = uiStr;
        label->SetText(&uiStrCopy);

        // Set sprite on label
        UI::Sprite sprBuf(::Object::ExternalPtr<UI::SpriteSheet>(prof->spriteSheet),
                          prof->stringName);
        UI::Sprite sprCopy = sprBuf;
        label->SetSprite(&sprCopy);

        // Set tooltip
        UI::String ttStr(::Object::ExternalPtr<UI::StringTable>(prof->stringTable),
                         prof->stringDesc);
        UI::String ttCopy = ttStr;
        label->SetToolTip(ttCopy);

        // Set spinner element IDs on the content container (child 1 of row)
        static_cast<UI::SpinnerElement*>(contentElem)->SetElementId(i + 0x1966);

        // Set inner child(0) of content container with same ID
        contentElem->Child(0).Get()->SetElementId(i + 0x1966);

        // Set button element ID
        button->UI::Element::SetElementId(i + 0x1999);

        // Push profession entry into dynamic array at +0x40
        professionRows.SetSize(professionRows.GetSize() + 1, profEntry);   // 0x140015800

        // profEntry's destructor frees its entity list (it was zeroed, so
        // nothing to free): MemFree, then the three fields zeroed.
    }
}

// ============================================================
// TownHallUI::InitProduction (0x14007fc30)
// ============================================================
// Sets up the production limits table. Finds "limits" TableElement,
// iterates its rows (linear children in groups of colCount),
// extracting 5 NumberElement columns per row into the production
// limits array at +0xF0. Then finds combo elements and populates
// the production year combo from the graphYears string table.
void TownHallUI::InitProduction() {
    // Find "limits" and cast to TableElement
    UI::TableElement* table = TownHall_FindElem<UI::TableElement>(_container, "limits");

    // Read column count from table
    int colCount = table->_columnCount;

    // Iterate rows of the table
    // Children are laid out linearly. Each "row" spans colCount children.
    // Column 0 = row label (skipped), columns 1-5 = NumberElements.
    // childBase starts at 2 (first data column after header row),
    // increments by colCount each row.
    int totalChildren = table->_rowCount;
    int childBase = 2;  // starts at child index 2 (skip first row header)

    UI::NumberElement* ProductionRow::* const columns[5] = {
        &ProductionRow::stock, &ProductionRow::used, &ProductionRow::produced,
        &ProductionRow::usedPrev, &ProductionRow::producedPrev };

    for (int row = 0; row < totalChildren; row++) {
        ProductionRow entry = {};  // 5 * 8 = 0x28 bytes per production entry

        // 5 NumberElement columns per row at indices:
        //   childBase-1, childBase, childBase+1, childBase+2, childBase+3
        int indices[5] = {childBase - 1, childBase, childBase + 1, childBase + 2, childBase + 3};
        for (int col = 0; col < 5; col++) {
            entry.*columns[col] = CastElement<UI::NumberElement>(table->Child(indices[col]).Get());
        }

        // Push entry into production limits array at +0xF0
        prodLimits.SetSize(prodLimits.GetSize() + 1, entry);   // 0x1400159b0

        childBase += colCount;  // advance to next row
    }

    // Find "productionCombo" ComboElement
    productionCombo = TownHall_FindElem<UI::ComboElement>(_container, "productionCombo");

    // Find "limitPrevUsedHeader" TextElement
    limitPrevUsedHdr = TownHall_FindElem<UI::TextElement>(_container, "limitPrevUsedHeader");

    // Find "limitPrevProducedHeader" TextElement
    limitPrevProdHdr = TownHall_FindElem<UI::TextElement>(_container, "limitPrevProducedHeader");

    // Load graphYears string table and populate combo
    UI::StringTable* yearsTable = nullptr;
    {
        System::Filename filenameBuf("Dialog/StringTable.rsc:graphYears");
        ::Object::External* ext = ::Object::ExternalFactory::_singleton->FindOrLoadAs<::Object::External>(filenameBuf);
        yearsTable = (ext && ::Object::IsDerivedFromNullSafe(ext, UI::StringTable::TypeId()))
            ? static_cast<UI::StringTable*>(ext) : nullptr;
    }

    // Add 7 combo items from graphYears string table
    for (int i = 0; i < 7; i++) {
        UI::String uiStr(::Object::ExternalPtr<UI::StringTable>(yearsTable), i);
        System::String sysStrBuf;
        ::new (&sysStrBuf) System::String(uiStr.operator System::String());
        UI::Sprite emptySprite;
        productionCombo->AddItem(emptySprite, sysStrBuf, nullptr);
    }
    productionCombo->SetCurrentItem(0);
}

// ============================================================
// TownHallUI::InitInventory (0x14007f660)
// ============================================================
// Sets up the inventory table. Finds "inventoryTable" TableElement,
// counts resource types via ExternalFactory, sizes/clones table rows,
// extracts LabelElement + NumberElement pairs from each cell,
// pushes them into the inventory table array at +0x118.
// Then finds and populates inventoryCombo and sortCombo.
void TownHallUI::InitInventory() {
    // Find "inventoryTable" and cast to TableElement
    UI::TableElement* table = TownHall_FindElem<UI::TableElement>(_container, "inventoryTable");

    // Count resource types via ExternalFactory::FindNext
    int resourceCount = 0;
    uint32_t resTypeId = RawMaterialDescription::TypeId();
    const ::Object::ExternalFactory* efactory = ::Object::ExternalFactory::_singleton;
    const ::Object::External* extIter = efactory->FindNext(resTypeId, nullptr);
    while (extIter) {
        resourceCount++;
        extIter = efactory->FindNext(resTypeId, extIter);
    }

    // Get column count and template row description
    int colCount = table->_columnCount;

    UI::Element* templateChild = table->Child(0).Get();
    const UI::ElementDescription* cloneObj =
        templateChild->_description;

    // Size table: ceil(resourceCount / colCount) rows
    int rowCount = ((colCount - 1 + resourceCount) / colCount * colCount) / colCount;
    table->SetSize(rowCount, colCount);

    // Clone template rows into all cells
    int totalChildren = table->ChildCount();
    for (int i = 0; i < totalChildren; i++) {
        // Clone: UI::ElementDescription slot 8, CreateInstance (retslot, B6-2).
        ::Object::InternalPtr<UI::Element> cloneBuf;
        ::Object::InternalPtr<UI::Element>* clonedElem =
            cloneObj->CreateInstance(&cloneBuf, _dialog->_dialog);

        table->SetChild(clonedElem, i);

    }

    // Extract label + number elements from each cell
    totalChildren = table->ChildCount();
    for (int i = 0; i < totalChildren; i++) {
        // Get cell->child(0) = LabelElement
        UI::Element* cell = table->Child(i).Get();
        UI::LabelElement* labelElem = CastElement<UI::LabelElement>(cell->Child(0).Get());

        // Get cell->child(1) = NumberElement
        UI::Element* cell2 = table->Child(i).Get();
        UI::NumberElement* numElem = CastElement<UI::NumberElement>(cell2->Child(1).Get());

        // Push {label, number} into the inventory table array at +0x118.
        invTable.SetSize(invTable.GetSize() + 1, InventoryRow{labelElem, numElem});
    }

    // Load inventoryType string table
    UI::StringTable* invTypeTable = nullptr;
    {
        System::Filename filenameBuf("Dialog/StringTable.rsc:inventoryType");
        ::Object::External* ext = ::Object::ExternalFactory::_singleton->FindOrLoadAs<::Object::External>(filenameBuf);
        invTypeTable = (ext && ::Object::IsDerivedFromNullSafe(ext, UI::StringTable::TypeId()))
            ? static_cast<UI::StringTable*>(ext) : nullptr;
    }

    // Find "inventoryCombo" ComboElement
    inventoryCombo = TownHall_FindElem<UI::ComboElement>(_container, "inventoryCombo");

    // Populate inventory combo with 3 items
    for (int i = 0; i < 3; i++) {
        UI::String uiStr(::Object::ExternalPtr<UI::StringTable>(invTypeTable), i);
        System::String sysStrBuf;
        ::new (&sysStrBuf) System::String(uiStr.operator System::String());
        UI::Sprite emptySprite;
        inventoryCombo->AddItem(emptySprite, sysStrBuf, nullptr);
    }
    inventoryCombo->SetCurrentItem(0);

    // Find "sortCombo" ComboElement
    sortCombo = TownHall_FindElem<UI::ComboElement>(_container, "sortCombo");
    sortCombo->SetCurrentItem(0);
}

// ============================================================
// TownHallUI::InitAgriculture (0x14007ebf0)
// ============================================================
// Sets up the agriculture table. Iterates ExternalFactory for
// ComponentDescription entities, finds SelectDescription sub-components,
// inserts matching building descriptions into BST at +0x1D8.
// Then sizes/clones table rows and extracts LabelElement from
// each cell into the dynamic array at +0x1C8.
void TownHallUI::InitAgriculture() {
    // Find "agriTable" and cast to TableElement
    UI::TableElement* table = TownHall_FindElem<UI::TableElement>(_container, "agriTable");

    // Iterate ExternalFactory for ComponentDescription entities
    uint32_t buildTypeId = ComponentDescription::TypeId();
    uint32_t selectDescId = SelectDescription::TypeId();

    const ::Object::ExternalFactory* efactoryAgri = ::Object::ExternalFactory::_singleton;
    const ::Object::External* buildDesc = efactoryAgri->FindNext(buildTypeId, nullptr);
    while (buildDesc) {
        // Cast to ComponentDescription (IsDerivedFrom check)
        const ComponentDescription* buildComp = nullptr;
        if (::Object::IsDerivedFromNullSafe(buildDesc, buildTypeId)) {
            buildComp = static_cast<const ComponentDescription*>(buildDesc);
        }

        if (buildComp) {
            // Iterate sub-components to find SelectDescription
            int subCount = buildComp->_subDescCount;

            for (int j = 0; j < subCount; j++) {
                ::Object::Base* subComp = buildComp->_subDescs[j];
                // Check if sub-component's type matches SelectDescription
                // Virtual call at vtable+0x10 returns type ID
                int subTypeId = subComp->Type();

                if (subTypeId == (int)selectDescId) {
                    // Found a SelectDescription sub-component
                    ::Object::Base* subObj = buildComp->_subDescs[j];
                    if (::Object::IsDerivedFromNullSafe(subObj, selectDescId) && subObj) {
                        // Insert buildComp into agriculture BST at +0x1D8
                        const ComponentDescription* key = buildComp;
                        AgricultureNode* curNode = agriBST.root;

                        if (curNode == nullptr) {
                            // First node (0x28 bytes)
                            AgricultureNode* newNode = static_cast<AgricultureNode*>(System::MemoryAllocator::MemAlloc(0x28, 8));
                            if (newNode) {
                                newNode->key = key;
                                newNode->parent = nullptr;
                                newNode->left = nullptr;
                                newNode->right = nullptr;
                                newNode->balance = 0;
                                newNode->height = 0;
                            }
                            agriBST.root = newNode;
                            agriBST.count = agriBST.count + 1;
                        } else {
                            // Walk BST and insert
                            AgricultureNode* parent = nullptr;
                            bool inserted = false;
                            while (!inserted) {
                                parent = curNode;
                                if (key < curNode->key) {
                                    if (curNode->left == nullptr) {
                                        AgricultureNode* newNode = static_cast<AgricultureNode*>(System::MemoryAllocator::MemAlloc(0x28, 8));
                                        if (newNode) {
                                            newNode->key = key;
                                            newNode->parent = parent;
                                            newNode->left = nullptr;
                                            newNode->right = nullptr;
                                            newNode->balance = 0;
                                            newNode->height = 0;
                                        }
                                        parent->left = newNode;
                                        AvlRebalance(&agriBST.root, &curNode);   // 0x1400149a0
                                        inserted = true;
                                    } else {
                                        curNode = curNode->left;
                                    }
                                } else if (key > curNode->key) {
                                    if (curNode->right == nullptr) {
                                        AgricultureNode* newNode = static_cast<AgricultureNode*>(System::MemoryAllocator::MemAlloc(0x28, 8));
                                        if (newNode) {
                                            newNode->key = key;
                                            newNode->parent = parent;
                                            newNode->left = nullptr;
                                            newNode->right = nullptr;
                                            newNode->balance = 0;
                                            newNode->height = 0;
                                        }
                                        parent->right = newNode;
                                        AvlRebalance(&agriBST.root, &curNode);   // 0x1400149a0
                                        inserted = true;
                                    } else {
                                        curNode = curNode->right;
                                    }
                                } else {
                                    // Duplicate — already in BST
                                    inserted = true;
                                }
                            }
                            agriBST.count = agriBST.count + 1;
                        }
                    }
                    break;  // Found matching sub-component, stop inner loop
                }
            }
        }

        buildDesc = efactoryAgri->FindNext(buildTypeId, buildDesc);
    }

    // Now set up the table with agriculture BST count
    int agriCount = agriBST.count;
    int colCount = table->_columnCount;

    UI::Element* templateChild = table->Child(0).Get();
    const UI::ElementDescription* cloneObj =
        templateChild->_description;

    // Size table: ceil(agriCount / colCount) rows
    int rowCount = ((colCount - 1 + agriCount) / colCount * colCount) / colCount;
    table->SetSize(rowCount, colCount);

    // Clone template rows into all cells
    int totalChildren = table->ChildCount();
    for (int i = 0; i < totalChildren; i++) {
        // Clone: UI::ElementDescription slot 8, CreateInstance (retslot, B6-2).
        ::Object::InternalPtr<UI::Element> cloneBuf;
        ::Object::InternalPtr<UI::Element>* clonedElem =
            cloneObj->CreateInstance(&cloneBuf, _dialog->_dialog);

        table->SetChild(clonedElem, i);

    }

    // Extract LabelElement from each cell into array at +0x1C8
    totalChildren = table->ChildCount();
    for (int i = 0; i < totalChildren; i++) {
        // Cast child to LabelElement
        UI::LabelElement* labelElem = CastElement<UI::LabelElement>(table->Child(i).Get());

        // Push the label into the label array at +0x1C8.
        agriLabels.SetSize(agriLabels.GetSize() + 1, labelElem);
    }
}

// ============================================================
// TownHallUI::UpdateGraph (0x14007e5b0)
// ============================================================
// Reads time/graph combo selections, populates the graph element
// with historical data series, and updates y-axis labels.
void TownHallUI::UpdateGraph() {
    // The town manager and its stat-history series (+0x150: an Array of int
    // Arrays, one per series; series 0's count is the current tick).
    CitizenBehavior* town =
        GameUpdateLoop::_singleton->m_citizenList;
    const Collection::Array<int>* history = town->field_0x150._data;

    // Cache game tick
    cachedGameTick = history[0]._size;

    // Read combo selections
    int timeIdx = timeCombo->_currentItem;
    int graphIdx = graphCombo->_currentItem;

    // Data series indices (-1 = hidden)
    int graphSeries[5] = {-1, -1, -1, -1, 0};
    uint32_t colors[4] = {0xffffff00, 0xff00ff00, 0xff8080ff, 0xffff0000};

    int stepSize = 0, pointCount = 0;
    uint32_t ticksPerStep = 0;

    switch (timeIdx) {
    case 0: stepSize=1;  pointCount=0x30; ticksPerStep=0xc;  break;
    case 1: stepSize=1;  pointCount=0x60; ticksPerStep=0x18; break;
    case 2: stepSize=2;  pointCount=0x78; ticksPerStep=5;    break;
    case 3: stepSize=4;  pointCount=0x78; ticksPerStep=10;   break;
    case 4: stepSize=8;  pointCount=0x96; ticksPerStep=0x19; break;
    case 5: stepSize=0x10; pointCount=0x96; ticksPerStep=0x32; break;
    case 6: stepSize=0x20; pointCount=0x96; ticksPerStep=100; break;
    }

    switch (graphIdx) {
    case 0:  graphSeries[0]=0;    graphSeries[1]=1;  graphSeries[2]=2;  graphSeries[3]=3;  break;
    case 1:  graphSeries[0]=4;    graphSeries[1]=6;  graphSeries[2]=5;  graphSeries[3]=7;  break;
    case 2:  graphSeries[0]=0x17; graphSeries[1]=-1; break;
    case 3:  graphSeries[0]=8;    graphSeries[1]=-1; break;
    case 4:  graphSeries[0]=0xb;  graphSeries[1]=-1; break;
    case 5:  graphSeries[0]=0xe;  graphSeries[1]=-1; break;
    case 6:  graphSeries[0]=0x11; graphSeries[1]=-1; break;
    case 7:  graphSeries[0]=0x23; graphSeries[1]=-1; break;
    case 8:  graphSeries[0]=0x14; graphSeries[1]=-1; break;
    case 9:  graphSeries[0]=0x1a; graphSeries[1]=-1; break;
    case 10: graphSeries[0]=0x1d; graphSeries[1]=-1; break;
    case 11: graphSeries[0]=0x20; graphSeries[1]=-1; break;
    case 12: graphSeries[0]=0x26; graphSeries[1]=-1; break;
    case 13: graphSeries[0]=0x29; graphSeries[1]=-1; break;
    case 14: graphSeries[0]=0x2c; graphSeries[1]=-1; break;
    case 15: graphSeries[0]=0x2f; graphSeries[1]=-1; break;
    case 16: graphSeries[0]=0x32; graphSeries[1]=-1; break;
    case 17: graphSeries[0]=0x35; graphSeries[1]=-1; break;
    case 18: graphSeries[0]=0x38; graphSeries[1]=-1; break;
    case 19: graphSeries[0]=0x3b; graphSeries[1]=-1; break;
    case 20: graphSeries[0]=0x3e; graphSeries[1]=-1; break;
    case 21: graphSeries[0]=0x41; graphSeries[1]=-1; break;
    case 22: graphSeries[0]=0x44; graphSeries[1]=-1; break;
    }

    pointCount += 1;
    graphSeries[4] = pointCount;

    // Set x-axis step
    graphData->_xDivisions = (int32_t)ticksPerStep;

    // Set x-axis min label (UIString from graphYearsTable at timeIdx)
    UI::String xMinStr(::Object::ExternalPtr<UI::StringTable>(graphYearsTable), timeIdx);
    UI::String xMinCopy = xMinStr;
    textXMin->SetText(&xMinCopy);

    // Remove existing graphs
    graphData->RemoveGraphs();

    int currentTick = history[0]._size;
    int maxVal = 0;
    int graphCount = 0;

    UI::LabelElement* const* const labelSlots[4] = { &label0, &label1, &label2, &label3 };
    for (int g = 0; g < 4; g++) {
        int seriesIdx = graphSeries[g];
        UI::LabelElement* const* labelPtr = labelSlots[g];

        if (seriesIdx < 0) {
            (*labelPtr)->SetFlagsOnOff(1, 0);
        } else {
            graphData->AddGraph(colors[g]);

            int startIdx;
            if (currentTick / stepSize < pointCount) {
                int padCount = pointCount - currentTick / stepSize;
                for (int p = 0; p < padCount; p++) {
                    graphData->AddGraphPoint(graphCount, 0);
                }
                startIdx = 0;
            } else {
                startIdx = currentTick - pointCount * stepSize;
            }

            for (int64_t idx = startIdx; idx < currentTick; idx += stepSize) {
                int val = history[seriesIdx]._data[idx];
                if (val > maxVal) maxVal = val;
                graphData->AddGraphPoint(graphCount, val);
            }

            // Set label color
            (*labelPtr)->_iconColor = colors[g];

            // Set label text from string table
            UI::String lblStr((*labelPtr)->_text.GetStringTable(), seriesIdx);
            UI::String lblCopy = lblStr;
            (*labelPtr)->SetText(&lblCopy);

            (*labelPtr)->SetFlagsOnOff(0, 1);

            pointCount = graphSeries[4];
            graphCount++;
        }
    }

    // Round up y-axis max
    if (maxVal < 5) maxVal = 5;
    int scale;
    if      (maxVal < 10)   scale = 1;
    else if (maxVal < 100)  scale = 10;
    else if (maxVal < 1000) scale = 100;
    else                    scale = 1000;
    int yMax = ((maxVal - 1 + scale) / scale) * scale;

    graphData->_yRange = (float)yMax;

    // Y-axis labels: max, 3/4, 1/2, 1/4
    float quarter = (float)yMax * 0.25f;
    number0->SetValue(static_cast<int32_t>(yMax));
    number1->SetValue(static_cast<int32_t>((int)(quarter * 3.0f)));
    number2->SetValue(static_cast<int32_t>((int)(quarter * 2.0f)));
    number3->SetValue(static_cast<int32_t>((int)quarter));
}

// ============================================================
// TownHallUI::UpdateGeneral (0x14007d030)
// ============================================================
void TownHallUI::UpdateGeneral() {
    CitizenBehavior* town =
        GameUpdateLoop::_singleton->m_citizenList;

    int totalCitizens = town->citizens._size;
    int adults = 0, students = 0, children = 0, familyCount = 0;
    float clothedSum = 0.0f, educatedSum = 0.0f, healthSum = 0.0f, happinessSum = 0.0f;
    float perCitizen = (totalCitizens > 0) ? (1.0f / (float)totalCitizens) : 0.0f;

    for (int i = 0; i < totalCitizens; i++) {
        CitizenComponent* citizen = town->citizens._data[i].Get();

        float age = citizen->age;
        float adultAge = citizen->description->adultAge;
        AttendanceComponent* school = citizen->school.Get();

        if (age < adultAge || school != nullptr) {
            if (school == nullptr) { children++; }
            else                   { students++; }
        } else {
            adults++;
            if (citizen->description->maxEducation <= citizen->education) {
                clothedSum += 1.0f;
            }
            CitizenComponent* spouse = citizen->spouse.Get();
            if (spouse == nullptr) {
                if (citizen->spouse == nullptr) {
                    familyCount++;
                }
            } else {
                if (citizen->gender == 0) {
                    familyCount++;
                }
            }
        }

        happinessSum += citizen->happiness;
        healthSum += citizen->health;
        if (citizen->clothingLevel != 0) {
            educatedSum += 1.0f;
        }
    }

    float happinessAvg = happinessSum * perCitizen;
    float educatedPct = educatedSum * perCitizen * 100.0f;
    float clothedPct = (adults > 0) ? ((clothedSum / (float)adults) * 100.0f) : 0.0f;

    // Year
    int year = GameUpdateLoop::_singleton->m_dayCounter->year;
    numberYear->SetValue(static_cast<int32_t>(year));

    // Month
    float seconds = GameUpdateLoop::_singleton->m_dayCounter->yearTime.AsSeconds();
    int monthIdx = (int)((seconds / 3600.0f) * 12.0f);
    if (monthIdx > 11) monthIdx = 0;

    UI::String monthStr(textMonth->_text.GetStringTable(), monthIdx);
    UI::String monthCopy = monthStr;
    textMonth->SetText(&monthCopy);

    // Home count
    int homeCount = town->field_0x20._size;
    numberHomes->SetValue(static_cast<int32_t>(homeCount));

    // Citizen counts
    numberCitizens->SetValue(static_cast<int32_t>(totalCitizens));
    numberAdults->SetValue(static_cast<int32_t>(adults));
    numberStudents->SetValue(static_cast<int32_t>(students));
    numberChildren->SetValue(static_cast<int32_t>(children));

    // Educated % (at +0xa0: retail writes it to "percentClothed")
    {
        System::String fmtBuf = System::String::Format("%i", (int)(uint32_t)(int)(educatedPct + 0.5f));
        percentClothed->SetParameter(0, fmtBuf);
    }

    // Clothed % (at +0xa8: retail writes it to "percentEducated")
    {
        System::String fmtBuf = System::String::Format("%i", (int)(uint32_t)(int)(clothedPct + 0.5f));
        percentEducated->SetParameter(0, fmtBuf);
    }

    // Family count
    numberFamily->SetValue(static_cast<int32_t>(familyCount));

    // Hearts (health) and Stars (happiness) ratings
    if (totalCitizens < 1) {
        TownHall_GroupRatingUpdate(&groupHearts, 0.0f, 1.0f);   // 0x14006aed0
        TownHall_GroupRatingUpdate(&groupStars, 0.0f, 1.0f);   // 0x14006aed0
    } else {
        CitizenComponent* firstCitizen = town->citizens._data[0].Get();
        Description* desc = firstCitizen->_description;
        CitizenDescription* citizenDesc = ::Object::IsDerivedFromNullSafe(desc, CitizenDescription::TypeId())
            ? static_cast<CitizenDescription*>(desc) : nullptr;

        TownHall_GroupRatingUpdate(&groupHearts, healthSum * perCitizen,
                              citizenDesc->maxHealth);

        // Re-read first citizen for the happiness maximum
        firstCitizen = town->citizens._data[0].Get();
        desc = firstCitizen->_description;
        citizenDesc = ::Object::IsDerivedFromNullSafe(desc, CitizenDescription::TypeId())
            ? static_cast<CitizenDescription*>(desc) : nullptr;

        TownHall_GroupRatingUpdate(&groupStars, happinessAvg,
                              citizenDesc->maxHappiness);
    }
}

// ============================================================
// TownHallUI::UpdateProfessions (0x14007ccb0) — the production tab
// ============================================================
void TownHallUI::UpdateProfessions() {
    int comboIdx = productionCombo->_currentItem;

    uint32_t yearSpan = 0;
    switch (comboIdx) {
    case 0: yearSpan = 1; break;
    case 1: yearSpan = 2; break;
    case 2: yearSpan = 5; break;
    case 3: yearSpan = 10; break;
    case 4: yearSpan = 25; break;
    case 5: yearSpan = 50; break;
    case 6: yearSpan = 100; break;
    }

    // Set header text parameters
    {
        System::String fmtBuf = System::String::Format("%i", (int)yearSpan);
        limitPrevUsedHdr->SetParameter(0, fmtBuf);
    }
    {
        System::String fmtBuf = System::String::Format("%i", (int)yearSpan);
        limitPrevProdHdr->SetParameter(0, fmtBuf);
    }

    Calendar* timePtr = GameUpdateLoop::_singleton->m_dayCounter;
    int currentYearTick = timePtr->year * 0x30 - 0x30;
    float secs = timePtr->yearTime.AsSeconds();
    uint32_t monthTick = (uint32_t)((secs / 3600.0f) * 48.0f);
    if ((int)monthTick > 0x2f) monthTick = 0;
    int endTick = monthTick + currentYearTick;
    int prevEnd = timePtr->year * 0x30 - 0x31;
    int prevStart = (timePtr->year - (int)yearSpan - 1) * 0x30;

    CitizenBehavior* town =
        GameUpdateLoop::_singleton->m_citizenList;
    uint32_t resIdx = 10;
    int prodCount = prodLimits.GetSize();

    if (prodCount > 0) {
        for (int i = 0; i < prodCount; i++) {
            int stock = town->professionResources._data[i].currentCount;

            prodLimits[i].stock->SetValue(static_cast<int32_t>(stock));

            int usedCur = town->QueryProductionStat(resIdx - 1, currentYearTick, endTick);   // 0x140090f20
            int usedStock = town->professionResources._data[i].field_0x0C;
            int prodCur = town->QueryProductionStat(resIdx, currentYearTick, endTick);   // 0x140090f20
            int prodStock = town->professionResources._data[i]._field10;
            int usedPrev = town->QueryProductionStat(resIdx - 1, prevStart, prevEnd);   // 0x140090f20
            int prodPrev = town->QueryProductionStat(resIdx, prevStart, prevEnd);   // 0x140090f20

            prodLimits[i].used->SetValue(static_cast<int32_t>(usedCur + usedStock));

            prodLimits[i].produced->SetValue(static_cast<int32_t>(prodCur + prodStock));

            prodLimits[i].usedPrev->SetValue(static_cast<int32_t>(usedPrev));

            prodLimits[i].producedPrev->SetValue(static_cast<int32_t>(prodPrev));

            resIdx += 3;
        }
    }
}

// ============================================================
// BST in-order traversal helpers for TownHallUI
// ============================================================

// In-order successor for the inventory tree
static TownHallUI::InventoryNode* InvBST_Next(TownHallUI::InventoryNode* node) {
    if (!node) return nullptr;
    TownHallUI::InventoryNode* right = node->right;
    if (right) {
        while (right->left) right = right->left;
        return right;
    }
    // Go up: find first ancestor where we came from the left
    TownHallUI::InventoryNode* parent = node->parent;
    while (parent) {
        if (parent->left == node) return parent; // came from left child
        if (parent->right == node) {
            node = parent;
            parent = parent->parent;
        } else {
            break;
        }
    }
    return nullptr;
}

// In-order successor for the agriculture tree
static TownHallUI::AgricultureNode* AgriBST_Next(TownHallUI::AgricultureNode* node) {
    if (!node) return nullptr;
    TownHallUI::AgricultureNode* right = node->right;
    if (right) {
        while (right->left) right = right->left;
        return right;
    }
    TownHallUI::AgricultureNode* parent = node->parent;
    while (parent) {
        if (parent->left == node) return parent;
        if (parent->right == node) {
            node = parent;
            parent = parent->parent;
        } else {
            break;
        }
    }
    return nullptr;
}

// Find a description's sub-description by type: the find retail inlines in
// UpdateProduction 0x14007c7f0 (RawMaterialDescription cell 0x1401c7cb8, ValueDescription,
// and SelectDescription 0x1401c48a8 in the agriculture pass): count re-read per pass,
// slot 2 ([vtbl+0x10]) against T's cell, IsDerivedFrom on the first match; no test of the
// description, the array or an element.
template <class T>
static T* TownHall_FindDescription(const ComponentDescription* desc) {
    for (int i = 0; i < desc->_subDescCount; i++) {
        if (desc->_subDescs[i]->Type() == T::TypeId()) {
            Description* match = desc->_subDescs[i];
            return match->IsDerivedFrom(T::TypeId()) ? static_cast<T*>(match) : nullptr;   // the member (B7 LX row 993)
        }
    }
    return nullptr;
}

// Look up a key in the inventory tree (+0x138). Returns node or nullptr.
static TownHallUI::InventoryNode* InvBST_Find(TownHallUI::InventoryNode* root, const ComponentDescription* key) {
    TownHallUI::InventoryNode* node = root;
    while (node) {
        const ComponentDescription* nodeKey = node->key;
        if (key < nodeKey)
            node = node->left;
        else if (key > nodeKey)
            node = node->right;
        else
            return node;
    }
    return nullptr;
}

// ============================================================
// TownHallUI::UpdateProduction (0x14007c7f0) — the inventory tab
// ============================================================
// Traverses invBST (+0x138) in-order, collects entries with
// ResourceType components, sorts them, then updates the
// inventory table UI elements (+0x118 array).
void TownHallUI::UpdateProduction() {
    // Read combo selections
    int invComboIdx = inventoryCombo->_currentItem;
    int sortComboIdx = sortCombo->_currentItem;

    // Collect entries from invBST in-order traversal
    using ProdEntry = TownHallInventoryEntry;   // Dialog/TownHallUI.h

    Collection::Array<ProdEntry> entryArr;

    InventoryNode* bstRoot = invBST.root;
    if (bstRoot) {
        // Start at leftmost node
        InventoryNode* node = bstRoot;
        while (node->left) node = node->left;
        while (node) {
            const ComponentDescription* entityPtr = node->key;

            // Find ResourceType component on entity
            RawMaterialDescription* resourceComp = TownHall_FindDescription<RawMaterialDescription>(entityPtr);

            // Read counter value at selected index
            int counterVal = node->counts[invComboIdx];

            // Push entry into dynamic array
            entryArr.SetSize(entryArr.GetSize() + 1,
                             ProdEntry{entityPtr, resourceComp, counterVal, 0});

            node = InvBST_Next(node);
        }
    }

    // Sort entries. The sort is retail's own in-place body over the array's
    // buffer (COLLECTION_INVENTORY §4: introsort stays a free function, it does
    // not become an Array<T> member), so it is handed GetData()/GetSize().
    ProdEntry* entries    = entryArr.GetData();
    const int  entryCount = entryArr.GetSize();
    if (sortComboIdx == 0) {
        if (entryCount > 1)
            TownHall_InventorySortAsc(entries, entryCount, &sortComboIdx, nullptr);   // 0x140015200
    } else {
        if (entryCount > 1)
            TownHall_InventorySortDesc(entries, entryCount, &sortComboIdx, nullptr);   // 0x140015500
    }

    // Update UI elements for each entry
    int displayIdx = 0;
    if (entryCount > 0) {
        for (int i = 0; i < entryCount; i++) {
            const ComponentDescription* entityPtr = entries[i].entityPtr;

            // Find ValueDescription component on entity (retail reads +0x20..+0x38 of the
            // result unguarded, 0x14007cac8)
            ValueDescription* valDesc = TownHall_FindDescription<ValueDescription>(entityPtr);

            // Create sprite from ValueDescription+0x20 (spriteSheet) and +0x28 (spriteName)
            UI::Sprite spriteBuf(::Object::ExternalPtr<UI::SpriteSheet>(valDesc->_sprite), valDesc->_name);

            // Set sprite on label element
            invTable[i].label->SetSprite(&spriteBuf);

            // Create UIString from ValueDescription+0x30 (stringTable) and +0x38 (stringName)
            UI::String uiStrBuf(::Object::ExternalPtr<UI::StringTable>(valDesc->_ref30), valDesc->_s38);

            // Set text on label element
            invTable[i].label->SetText(&uiStrBuf);

            // Set number value on the number element (+8 from label)
            invTable[i].count->SetValue(static_cast<int32_t>(entries[i].value));

            // Set active flag (vtable +0x50 with -1)
            invTable[i].count->SetColor(static_cast<uint32_t>(-1));
        }
        displayIdx = entryCount;
    }

    // Clear remaining entries
    if (displayIdx < invTable.GetSize()) {
        for (int i = displayIdx; i < invTable.GetSize(); i++) {
            // Empty sprite
            UI::Sprite emptySprite;
            invTable[i].label->SetSprite(&emptySprite);

            // Empty string: String(ExternalPtr<StringTable>(null), name) stores {null, -1}
            // without reading the name (sprite.cpp, retail 0x18011da60) == the default ctor.
            UI::String emptyStr;
            invTable[i].label->SetText(&emptyStr);

            // Set value to 0
            invTable[i].count->SetValue(static_cast<int32_t>(0));

            // Set inactive flag (vtable +0x50 with 0)
            invTable[i].count->SetColor(0u);
        }
    }

    // entryArr frees its own buffer (rtcoll::MemFree, the same 0x1800c7ad0 that
    // fn_MemFreeStatic resolves) when it goes out of scope here.
}

// ============================================================
// TownHallUI::UpdateAgriculture (0x14007c220)
// ============================================================
// Builds temp BST from entity list, traverses agriBST (+0x1D8)
// in-order, updates agriculture label elements with sprites
// and text from SelectDescription components.
void TownHallUI::UpdateAgriculture() {
    CitizenBehavior* town =
        GameUpdateLoop::_singleton->m_citizenList;

    // Build temporary BST from the town's description list (+0x120/+0x128)
    AgricultureNode* tempBSTRoot = nullptr;
    int tempBSTCount = 0;
    int entityCount = town->field_0x120._size;

    for (int i = 0; i < entityCount; i++) {
        const ComponentDescription* entityPtr = town->field_0x120._data[i];

        if (!tempBSTRoot) {
            // First node — allocate root
            AgricultureNode* node = static_cast<AgricultureNode*>(System::MemoryAllocator::MemAlloc(0x28, 8));
            if (node) {
                node->key = entityPtr;
                node->parent = nullptr;
                node->left = nullptr;
                node->right = nullptr;
                node->balance = 0;
                node->height = 0;
                tempBSTRoot = node;
            }
        } else {
            // Insert into existing BST
            AgricultureNode* current = tempBSTRoot;
            AgricultureNode* parent = nullptr;
            while (current) {
                const ComponentDescription* curKey = current->key;
                if (entityPtr == curKey) goto next_entity; // already exists
                parent = current;
                if (entityPtr < curKey)
                    current = current->left;
                else
                    current = current->right;
            }
            // Allocate new node
            AgricultureNode* node = static_cast<AgricultureNode*>(System::MemoryAllocator::MemAlloc(0x28, 8));
            if (node) {
                node->key = entityPtr;
                node->parent = parent;
                node->left = nullptr;
                node->right = nullptr;
                node->balance = 0;
                node->height = 0;
                if (entityPtr < parent->key)
                    parent->left = node;
                else
                    parent->right = node;
                // Rebalance
                AgricultureNode* newNodeAddr = node;
                AvlRebalance(&tempBSTRoot, &newNodeAddr);   // 0x1400149a0
            }
        }
        tempBSTCount++;
next_entity: ;
    }

    // Traverse agriBST (+0x1D8) in-order
    AgricultureNode* agriBSTRoot = agriBST.root;
    int displayIdx = 0;

    if (agriBSTRoot) {
        // Go to leftmost node
        AgricultureNode* node = agriBSTRoot;
        while (node->left) node = node->left;

        while (node) {
            const ComponentDescription* entityPtr = node->key;

            // Look up entity in temp BST
            AgricultureNode* found = nullptr;
            AgricultureNode* searchNode = tempBSTRoot;
            while (searchNode) {
                const ComponentDescription* sk = searchNode->key;
                if (entityPtr < sk)
                    searchNode = searchNode->left;
                else if (entityPtr > sk)
                    searchNode = searchNode->right;
                else {
                    found = searchNode;
                    break;
                }
            }

            if (found) {
                // Find SelectDescription component (DAT_1401c48a8) on entity
                SelectDescription* selectDesc = TownHall_FindDescription<SelectDescription>(entityPtr);

                // Original always sets sprite/text when found in BST (even if selectDesc is NULL)
                // Create sprite from SelectDescription+0x20/+0x28
                UI::Sprite spriteBuf(::Object::ExternalPtr<UI::SpriteSheet>(selectDesc->_ref20), selectDesc->_s28);

                agriLabels[displayIdx]->SetSprite(&spriteBuf);

                // Create UIString from SelectDescription+0x30/+0x38
                UI::String uiStrBuf(::Object::ExternalPtr<UI::StringTable>(selectDesc->_ref30), selectDesc->_s38);

                agriLabels[displayIdx]->SetText(&uiStrBuf);

                // Vtable +0x50 call (SetColor) — 0xFFC0C0C0 = opaque silver/gray
                agriLabels[displayIdx]->SetColor(0xFFC0C0C0u);
            } else {
            }

            // Show element (SetFlagsOnOff hide=0, show=1)
            agriLabels[displayIdx]->SetFlagsOnOff(0, 1);

            displayIdx++;

            // Move to next node in-order
            node = AgriBST_Next(node);
        }
    }

    // Hide remaining label elements
    if (displayIdx < agriLabels.GetSize()) {
        for (int i = displayIdx; i < agriLabels.GetSize(); i++) {
            agriLabels[i]->SetFlagsOnOff(1, 0);
        }
    }

    // Free temporary BST
    if (tempBSTRoot) {
        WorkBase_TreeNodeFree(&tempBSTRoot, tempBSTRoot->left);   // 0x140014c30
        WorkBase_TreeNodeFree(&tempBSTRoot, tempBSTRoot->right);   // 0x140014c30
        System::MemoryAllocator::MemFree(tempBSTRoot);
    }
}

// ============================================================
// TownHallUI::UpdateInventory (0x14007def0) — the professions tab
// ============================================================
// For each profession, zeros counters, resizes entity arrays,
// iterates entities to count workers per profession, finds
// WorkPlaceComponent / WorkComponent to calculate
// max worker slots, updates NumberEditElement values.
void TownHallUI::UpdateInventory() {
    CitizenBehavior* town =
        GameUpdateLoop::_singleton->m_citizenList;
    ProfessionList* profList = GameUpdateLoop::_singleton->m_professionList;
    int profCount = profList->professions.GetSize();

    // Phase 1: Zero counters and resize entity arrays for each profession
    for (int p = 0; p < profCount; p++) {
        ProfessionRow& profEntry = professionRows[p];
        profEntry.workers = 0;  // worker count = 0
        profEntry.slots = 0;    // secondary count = 0
        // Resize entity ptr array at +0x30 to 0 elements
        profEntry.entities.SetSize(0);   // 0x1400032e0 (the 8-byte instantiation; retail 0x14007df47 XOR EDX,EDX)
    }

    // Phase 2: For each profession, look up the town's profession tree
    // (+0x100) to find the buildings of that profession, count worker slots
    for (int p = 0; p < profCount; p++) {
        Profession* profTypeId = profList->professions.GetData()[p];

        // Look up profession type in the town's profession tree (+0x100)
        ProfessionBucket* bstNode = town->field_0x100._root;
        while (bstNode) {
            Profession* nodeKey = bstNode->element.key;
            if (profTypeId < nodeKey)
                bstNode = bstNode->left;
            else if (profTypeId > nodeKey)
                bstNode = bstNode->right;
            else
                break;
        }

        int slotCount = 0;
        if (bstNode) {
            slotCount = bstNode->element.entities._size;
        }

        // For each slot in the profession BST node
        for (int slot = 0; slot < slotCount; slot++) {
            ComponentEntity* entity = bstNode->element.entities._data[slot].Get();

            // Push the entity into this profession row's entity array at +0x30.
            Collection::Array<ComponentEntity*>& profEntities = professionRows[p].entities;
            profEntities.SetSize(profEntities.GetSize() + 1, entity);

            // Check entity for WorkPlaceComponent -> active flag + worker count
            WorkPlaceComponent* workplaceComp = entity->FindComponent<WorkPlaceComponent>();

            // Check if the workplace accepts workers (workplace+0x50 != 0)
            if (workplaceComp->isAccepting != 0) {
                // Find WorkPlaceComponent again to get the WorkComponent
                WorkPlaceComponent* workplaceComp2 = entity->FindComponent<WorkPlaceComponent>();

                // Get parent entity (workplace+0x10)
                ComponentEntity* parentEntity = workplaceComp2->_entity.Get();

                // Find the WorkComponent (DAT_1401c8070)
                WorkComponent* workComp = parentEntity->FindComponent<WorkComponent>();

                // Get max count
                int maxCount = workComp->workState;
                if (maxCount == 0) {
                    maxCount = workComp->CalcMaxWorkers();   // 0x14012e5c0
                    workComp->workState = maxCount;
                }

                professionRows[p].slots += maxCount;
            }
        }

        // Update number+count display
        ProfessionRow& profEntry = professionRows[p];
        profEntry.slotsNumber->SetValue(static_cast<int32_t>(profEntry.slots));

        int entityArrayCount = profEntry.entities.GetSize();
        profEntry.buildingsNumber->SetValue(static_cast<int32_t>(entityArrayCount));

        // Set visibility via vtable+0x40 (UI::Element slot 8, Enable)
        profEntry.button->Enable(entityArrayCount > 0);
    }

    // Phase 3: Count adult citizens per profession
    int citizenCount = town->citizens._size;
    for (int c = 0; c < citizenCount; c++) {
        CitizenComponent* citizen = town->citizens._data[c].Get();

        float age = citizen->age;
        float adultAge = citizen->description->adultAge;
        AttendanceComponent* school = citizen->school.Get();

        if (age >= adultAge && school == nullptr) {
            // Adult citizen — look up profession in profBST (+0x50)
            Profession* profId = citizen->profession;
            ProfessionNode* profNode = professionTree.root;
            while (profNode) {
                Profession* nodeKey = profNode->key;
                if (profId < nodeKey)
                    profNode = profNode->left;
                else if (profId > nodeKey)
                    profNode = profNode->right;
                else
                    break;
            }
            if (profNode) {
                int profIdx = (int)profNode->index;
                professionRows[profIdx].workers += 1;
            }
        }
    }

    // Phase 4: Update spinner elements with final counts
    // First profession entry
    professionRows[0].desired->SetValue(professionRows[0].workers);
    professionRows[0].desired->SetMinMax(professionRows[0].workers, professionRows[0].workers);

    // Remaining profession entries
    int totalProfs = professionRows.GetSize();
    for (int p = 1; p < totalProfs; p++) {
        professionRows[p].desired->SetValue(professionRows[p].workers);

        professionRows[p].desired->SetMinMax(
            0, professionRows[0].workers + professionRows[p].workers);
    }
}

// ============================================================
// TownHallUI::UpdateAll (0x14007d590)
// ============================================================
// Collects entities from 4 entity lists in game data, zeros
// and populates invBST counters, then updates inventory and
// agriculture table UI based on component types.
void TownHallUI::UpdateAll() {
    CitizenBehavior* town =
        GameUpdateLoop::_singleton->m_citizenList;

    // Phase 1: For each tracked description (+0x110/+0x118),
    // zero or insert entry in invBST
    int trackedCount = town->field_0x110._size;
    for (int i = 0; i < trackedCount; i++) {
        ComponentDescription* entityPtr = town->field_0x110._data[i];

        // Look up in invBST
        InventoryNode* bstNode = InvBST_Find(invBST.root, entityPtr);
        if (bstNode) {
            // Zero the counters (retail's qword store over the first two, H7)
            bstNode->counts[0] = 0;
            bstNode->counts[1] = 0;
            bstNode->counts[2] = 0;
        } else {
            // Insert new node with zeroed counters
            // (+0x14 is left as the stack had it, as before.)
            InventoryEntry entry;
            entry.key = entityPtr;
            entry.counts[0] = 0;   // +0x08 / +0x0C: one qword store (H7: two stores)
            entry.counts[1] = 0;
            entry.counts[2] = 0;
            Avl38SetInsert(&invBST, entry);   // 0x140014890
        }
    }

    // Phase 2: Collect all entities from 4 entity lists
    // The town's first four arrays (+0x00/+0x10/+0x20/+0x30, each {data, count})
    Collection::Array<ComponentEntity*> entityArr;

    Object::InternalPtr<ComponentEntity>* const lists[4] = { town->field_0x00._data, town->field_0x10._data, town->field_0x20._data, town->field_0x30._data };
    const int32_t listCounts[4] = { town->field_0x00._size, town->field_0x10._size, town->field_0x20._size, town->field_0x30._size };
    for (int listIdx = 0; listIdx < 4; listIdx++) {
        for (int j = 0; j < listCounts[listIdx]; j++) {
            entityArr.SetSize(entityArr.GetSize() + 1, lists[listIdx][j].Get());
        }
    }

    // Phase 3: For each collected entity, find components and update BST counters
    ComponentEntity** entities = entityArr.GetData();
    int entityCount = entityArr.GetSize();
    for (int i = 0; i < entityCount; i++) {
        ComponentEntity* entity = entities[i];

        // Find StorageComponent (DAT_1401c7e78)
        StorageComponent* storage = entity->FindComponent<StorageComponent>();

        // Find ResidenceComponent (DAT_1401c7cf0)
        ResidenceComponent* home = entity->FindComponent<ResidenceComponent>();

        int counterIndex; // which of the 3 counters to increment
        if (home != nullptr) {
            counterIndex = 2; // a home (the "Items in Homes" column)
        } else {
            // Check for TradeComponent (DAT_1401c4b80)
            TradeComponent* building = entity->FindComponent<TradeComponent>();
            if (building != nullptr)
                counterIndex = 1; // a trading post
            else
                counterIndex = 0; // storage
        }

        // Iterate StorageComponent resource entries
        if (storage == nullptr) continue;
        int resourceCount = storage->storageSlots._size;

        for (int r = 0; r < resourceCount; r++) {
            StorageComponent::Item* items = storage->storageSlots._data;
            ComponentDescription* resourceEntity = items[r]._resource;
            int resourceQty = items[r]._count;

            // Look up resource entity in invBST
            InventoryNode* bstNode = InvBST_Find(invBST.root, resourceEntity);
            if (bstNode) {
                // Increment the appropriate counter
                bstNode->counts[counterIndex] += resourceQty;
            }
        }
    }

    // entityArr frees its own buffer (rtcoll::MemFree, the same door
    // fn_MemFreeStatic resolves) when it goes out of scope here.
}

// ============================================================
// TownHallUI::OnProfessionButton (0x14007dca0)
// ============================================================
void TownHallUI::OnProfessionButton(int index) {
    ProfessionRow& entryBase = professionRows[index];

    int entityCount = entryBase.entities.GetSize();
    if (entityCount == 0) return;

    profButtonIdx++;
    int cycleIdx = (int)profButtonIdx;
    if (cycleIdx >= entityCount) cycleIdx = 0;
    profButtonIdx = (uint32_t)cycleIdx;

    ComponentEntity* entityPtr = entryBase.entities[cycleIdx];
    UI::DialogInstance* dialogInst = _dialog;
    GameUpdateLoop::_singleton->m_toolManager->CenterOnEntity(&dialogInst, &entityPtr);   // 0x140103040
}

// ============================================================
// TownHallUI::OnProfessionSpinner (0x14007dd10)
// ============================================================
void TownHallUI::OnProfessionSpinner(int index) {
    // NO g_wpcRosterOnly guard (removed 2026-08-17, together with the two sibling
    // spinners this comment used to point at). The fault it named — READ of 0x10 in
    // Detach's e710 getter on a LOWER, which this handler does via
    // fn_CitizenList_RemoveWorker — is now guarded at the source
    // (workplacecomponent.cpp:276 returns early on a null accessor). "Our objects can't
    // survive it" stopped being true when that guard landed. Meanwhile the flag also
    // gates Attach's clear of the "no workers" status bit, so holding it left the ? icon
    // up over every building staffed from this tab. Measured on the lower path too: a
    // seat -> unseat -> re-seat cycle drives Detach's full visual tail with no fault.

    // Get desired vs current count
    int desired = professionRows[index].desired->GetValue();
    int current = professionRows[index].workers;

    if (current < desired) {
        int toAdd = desired - current;
        for (int i = 0; i < toAdd; i++) {
            // FindAvailableBuilding takes the town manager and the profession as integers.
            CitizenBehavior* const townKey = GameUpdateLoop::_singleton->m_citizenList;
            Profession* const profKey = GameUpdateLoop::_singleton->m_professionList->professions.GetData()[index];

            ComponentEntity* retBuf[2] = {nullptr, nullptr};
            townKey->FindAvailableBuilding(retBuf, profKey);   // 0x14008d100 (door misnamed fn_CitizenList_RequestWorker)
            ComponentEntity* slot = retBuf[0];

            // Try idle -> working -> student -> any. Each strategy returns its
            // `out` argument, so the result is read there.
            ComponentEntity* slotCopy = slot;
            CitizenComponent* found = nullptr;
            townKey->FindWorkerToRelocate(&found, &slotCopy, profKey);   // 0x14008cbf0

            if (found == nullptr) {
                if (slot != nullptr) {
                    slotCopy = slot;
                    townKey->FindNearestHousedLaborer(&found, &slotCopy, profKey);   // 0x14008ca20
                    if (found != nullptr) continue;
                }
                slotCopy = slot;
                townKey->FindLaborerFromFullestHouse(&found, &slotCopy, profKey);   // 0x14008c5f0

                if (found == nullptr) {
                    slotCopy = slot;
                    CitizenComponent* anyBuf = nullptr;
                    townKey->FindIdleLaborer(&anyBuf, &slotCopy, profKey);   // 0x14008c4b0
                }
            }
        }
    }

    if (desired < current) {
        int toRemove = current - desired;
        for (int i = 0; i < toRemove; i++) {
            CitizenBehavior* citizenList = GameUpdateLoop::_singleton->m_citizenList;
            Profession* profession = GameUpdateLoop::_singleton->m_professionList->professions.GetData()[index];
            citizenList->RemoveWorker(profession);   // 0x140091180
        }
    }

    // Refresh display
    UpdateInventory();
}

namespace townhallui_sa {

// ---- exe-.rdata type-id globals the TownHallUI paths used to dereference ----
// ENDGAME B.3.9 CG2: every one of the 19 TypeId_* globals this lazy-fill
// serviced is now read through its class static (X::TypeId() — either the
// ported Runtime static for the UI:: widget/element classes, or the exe
// DECLARE_EXE_TYPE static for the Component/Description classes), which
// always answers, so EnsureGlobals retires. B3R: the empty EnsureGlobals that
// was left went with its calls.

// Every global/function the Serialize+Startup+per-frame paths dereference
// unguarded must be live, or a null-deref faults (no SEH under MinGW). B3R: the
// twelve engine_funcs door pointers this tested were always non-null and are
// called by name now; only the ExternalFactory singleton is left.
bool Ready() {
    if (!(::Object::ExternalFactory::_singleton)) return false;
    return true;
}

// The town-side data the update paths read unguarded: townMgr (gw+0xf0) with its
// citizen roster, the profession list (gw+0xb0), the game clock (gw+0x128), and
// the townMgr history/production record blocks (+0x150 / +0x170) that the graph
// and production tabs index into.
bool TownReady() {
    if (!(GameUpdateLoop::_singleton != nullptr)) return false;
    GameUpdateLoop* gw = GameUpdateLoop::_singleton;
    CitizenBehavior* townMgr =
        gw->m_citizenList;
    ProfessionList* profList = gw->m_professionList;
    if (!townMgr || !profList) return false;
    if (profList->professions.GetSize() <= 0 || profList->professions.GetData() == nullptr) return false;
    if (gw->m_dayCounter == nullptr) return false;
    if (townMgr->field_0x150._data == nullptr) return false;           // stat-history series
    if (townMgr->professionResources._data == nullptr) return false;   // production records
    return true;
}

static void Trace(const char* what, const TownHallUI* self) {
    fprintf(stderr, "[TownHallUI] %s self=%p ready=%d town=%d built=%d\n",
            what, static_cast<const void*>(self), Ready() ? 1 : 0, TownReady() ? 1 : 0,
            self ? self->professionRows.GetSize() : -1);
    fflush(stderr);
}

// InitProfessions pushed at least one row once Serialize has run.
inline bool Built(const TownHallUI* self) { return self->professionRows.GetSize() != 0; }

// Controller bound to its element + dialog instance (framework sets +0x18/+0x20).
inline bool Bound(const TownHallUI* self) {
    return self->_dialog != nullptr && self->_container != nullptr;
}

// One-shot gate diagnostic — prints WHICH component of Ready()/TownReady() is
// missing (a silently-skipping gate looks identical to "blank dialog"). Logs a
// handful of lines once per session; also callable at world-build time via
// TownHallSA_Diag (gameupdateloop.cpp) so the dialog-independent gates are
// visible without opening the panel.
void DiagPrint(const char* where, const TownHallUI* self) {
    // ENDGAME B.3.9 CG2 / TR: every type-id entry this diagnostic used to list
    // retired — their class statics always answer. B3R: so did the twelve
    // "MISSING fn" door-pointer entries (never null; the bodies are called by name).
    fprintf(stderr, "[TownHallSA] %s: self=%p bound=%d built=%d ready=%d town=%d extFactory=%d\n",
            where, static_cast<const void*>(self),
            self ? (Bound(self) ? 1 : 0) : -1,
            self ? (Built(self) ? 1 : 0) : -1,
            Ready() ? 1 : 0, TownReady() ? 1 : 0,
            (::Object::ExternalFactory::_singleton) ? 1 : 0);
    if (GameUpdateLoop::_singleton != nullptr) {
        GameUpdateLoop* gw = GameUpdateLoop::_singleton;
        CitizenBehavior* townMgr =
            gw->m_citizenList;
        ProfessionList* profList = gw->m_professionList;
        fprintf(stderr, "[TownHallSA]   gw=%p townMgr=%p profList=%p profCnt=%d clock=%p hist(+150)=%p prod(+170)=%p\n",
                static_cast<void*>(gw), gw->m_citizenList, static_cast<void*>(profList),
                profList ? profList->professions.GetSize() : -1,
                gw->m_dayCounter,
                townMgr ? townMgr->field_0x150._data : nullptr,
                townMgr ? static_cast<void*>(townMgr->professionResources._data) : nullptr);
        // UpdateGeneral's two data sources: the town citizen list (+0xa0/+0xa8,
        // CitizenComponent_OnAdd) and the residence list (+0x20/+0x28, helper_9de0).
        if (townMgr)
            fprintf(stderr, "[TownHallSA]   citizens(+0xa8)=%d homes(+0x28)=%d\n",
                    townMgr->citizens._size, townMgr->field_0x20._size);
    } else {
        fprintf(stderr, "[TownHallSA]   GameUpdateLoop::_singleton=%p\n",
                static_cast<void*>(GameUpdateLoop::_singleton));
    }
    fflush(stderr);
}

}  // namespace townhallui_sa

// ============================================================
// TownHallUI::OnCreated (vtable +0x20, 0x140081000)
// ============================================================
// [4] 0x140081000
void TownHallUI::OnCreated() {
    if (!townhallui_sa::Bound(this) || !townhallui_sa::Ready() || !townhallui_sa::TownReady()) { townhallui_sa::Trace("Serialize SKIP", this); return; }
    if (townhallui_sa::Built(this)) return;   // idempotence: the framework or self-heal already built
    townhallui_sa::Trace("Serialize", this);

    tabElement = TownHall_FindElem<UI::TabElement>(_container, "tabs");

    InitGeneral();
    InitProfessions();
    InitProduction();
    InitInventory();
    InitGraph();
    InitAgriculture();
}

// ============================================================
// TownHallUI::OnSerializeSettings (vtable +0x50, 0x140080db0)
// ============================================================
// [10] 0x140080db0
void TownHallUI::OnSerializeSettings(IO::Stream& stream) {
    if (!townhallui_sa::Built(this) || !townhallui_sa::Ready() || !townhallui_sa::TownReady()) return;

    int tabIndex = tabElement->_activeTab;
    int combo1 = productionCombo->_currentItem;
    int combo2 = inventoryCombo->_currentItem;
    int combo3 = sortCombo->_currentItem;
    int combo4 = timeCombo->_currentItem;
    int combo5 = graphCombo->_currentItem;

    IO::Serializer serializer(stream, 1);

    serializer.Serialize(tabIndex, nullptr);
    serializer.Serialize(combo1, nullptr);
    serializer.Serialize(combo2, nullptr);

    int version = serializer.GetVersion();
    if (version > 0) {
        serializer.Serialize(combo3, nullptr);
    }
    serializer.Serialize(combo4, nullptr);
    serializer.Serialize(combo5, nullptr);

    if (serializer.GetDirection() == IO::kReading) {
        tabElement->SetActiveTab(tabIndex);
        productionCombo->SetCurrentItem(combo1);
        inventoryCombo->SetCurrentItem(combo2);
        sortCombo->SetCurrentItem(combo3);
        timeCombo->SetCurrentItem(combo4);
        graphCombo->SetCurrentItem(combo5);
        UpdateGraph();
    }
}

// ============================================================
// TownHallUI::OnUpdateLayout (vtable +0x38)
// ============================================================
// [7] 0x14007d550
void TownHallUI::OnUpdateLayout() {
    if (!townhallui_sa::Bound(this) || !townhallui_sa::Ready() || !townhallui_sa::TownReady()) return;
    if (!townhallui_sa::Built(this)) TownHallUI::OnCreated();
    if (!townhallui_sa::Built(this)) return;
    UpdateGeneral();
    UpdateProfessions();
    UpdateProduction();
    UpdateAgriculture();
}

// ============================================================
// TownHallUI::OnUpdate (vtable +0x40)
// ============================================================
// [8] 0x14007e550
void TownHallUI::OnUpdate(const System::Time& /*time*/) {
    static bool s_diagOnce = false;
    if (!s_diagOnce) { s_diagOnce = true; townhallui_sa::DiagPrint("first OnUpdate", this); }
    if (!townhallui_sa::Bound(this) || !townhallui_sa::Ready() || !townhallui_sa::TownReady()) return;
    if (!townhallui_sa::Built(this)) {
        TownHallUI::OnCreated();
        if (!townhallui_sa::Built(this)) return;
        TownHallUI::OnUpdateLayout();
        return;
    }
    CitizenBehavior* town =
        GameUpdateLoop::_singleton->m_citizenList;
    int currentTick = town->field_0x150._data[0]._size;

    if (this->cachedGameTick != currentTick) {
        this->cachedGameTick = currentTick;
        UpdateGraph();
    }

    UpdateInventory();
    UpdateAll();
    if ((agriBST.tail)++ % 30 == 0) TownHallUI::OnUpdateLayout();
}

// ============================================================
// TownHallUI::OnElementEvent (vtable +0x48)
// ============================================================
// [9] 0x14007c180
void TownHallUI::OnElementEvent(Object::InternalPtr<UI::Element>* /*element*/, int32_t msg, int32_t id,
                                int32_t /*c*/, int32_t /*d*/, int32_t /*e*/, void* /*userData*/) {
    if (!townhallui_sa::Built(this) || !townhallui_sa::Ready() || !townhallui_sa::TownReady()) return;
    int profCount = GameUpdateLoop::_singleton->m_professionList->professions.GetSize();

    if (msg == 0x12) {
        if (id > 0x1998 && id <= 0x1999 + profCount)
            OnProfessionButton(id - 0x1999);
    } else if (msg == 0x14) {
        if (id == 0x1964 || id == 0x1965)
            UpdateGraph();
    } else if (msg == 0x15) {
        if (id > 0x1965 && id <= 0x1966 + profCount)
            OnProfessionSpinner(id - 0x1966);
    }
}

// ============================================================
// STANDALONE promotion — real TownHallUI controller vtable
// ============================================================
// The town-hall panel (Dialog/TownHall.rsc — the General statistics / Professions /
// Production / Inventory / Graph / Agriculture tab dialog opened by clicking a Town
// Hall) is driven by a TownHallUI controller. In standalone this type was a StubNop
// (all slots no-op, see ui_dialog_stub_factories.cpp — that stub registration is
// REMOVED there to avoid a duplicate REGISTER_TYPE), so the dialog opened with every
// field left on its .rsc template default: the "general statistics" numbers never
// populated. This installs a real per-class vtable wiring the fully-reimplemented
// methods above, following the StorageUI/WorkPlaceUI promotion pattern.
//
// Vtable offsets (verified from the exe TownHallUI vtable @ 0x14015dce0):
//   +0x00 (idx0)  ScalarDtor — frees the prof/prod/inv/agri arrays + 3 BSTs
//   +0x10 (idx2)  GetTypeId
//   +0x20 (idx4)  OnCreated  = element discovery + InitGeneral/Professions/
//                 Production/Inventory/Graph/Agriculture (framework setup call)
//   +0x38 (idx7)  OnUpdateLayout = UpdateGeneral/Professions/Production/Agriculture
//   +0x40 (idx8)  OnUpdate   = per-frame: graph refresh on tick change +
//                 UpdateInventory + UpdateAll (the framework dispatches ONLY +0x40
//                 each frame, so the self-heal build + a throttled refresh
//                 are driven here — keeps the General tab's month/counts live)
//   +0x48 (idx9)  OnElementEvent = profession buttons 0x1999+, graph combos
//                 0x1964/0x1965 (msg 0x14), profession spinners 0x1966+ (msg 0x15)
//   +0x50 (idx10) OnSerializeSettings = tab/combo state persist (IO::Serializer stream)
//
// The exe helper functions these methods call are ported in townhall_helpers.cpp
// + citizen_employ_cascade.cpp and called by name.


// World-build-time gate diagnostic (called once from GameUpdateLoop::Update at an
// early frame, see gameupdateloop.cpp). B3R: the publish this called
// (townhallui_sa::EnsureGlobals) had been emptied at B.3.9 -- every global it
// filled is a class static now -- and is deleted. The entry point stays while
// gameupdateloop.cpp calls it.
extern "C" void TownHallSA_Diag(int frameNum) {
    static bool s_done = false;
    if (s_done || frameNum < 120) return;
    s_done = true;
}

// ~TownHallUI (0x1400810d0): the body is empty. The owned containers are members
// and are destroyed in reverse declaration order after it -- agriBST, agriLabels,
// invBST, invTable, prodLimits, groupStars (half, whole), groupHearts (half, whole),
// professionTree, professionRows (each row's entity list, then the buffer:
// 0x140015930) -- retail's descending-offset order; then ~DialogController ->
// ~Internal.
TownHallUI::~TownHallUI() {
}






// create 0x140014660: MemAlloc(0x1e8, 8), then the ctor (DialogController ->
// Internal: AddObject, the compiler's stamps, the field initialisers).
::Object::Base* TownHallUI::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(TownHallUI), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) TownHallUI());
}
// Retail's createInPlace does not build a controller in place (B8-13); the
// tree's in-place entry did, and that is kept.
::Object::Base* TownHallUI::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) TownHallUI());
}

DEFINE_EXE_TYPE(TownHallUI, "UI::DialogController")
