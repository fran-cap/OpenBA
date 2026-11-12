// src/game/Dialog/tradeui.cpp
// Clean reimplementation of TradeUI methods.
//
// TradeUI (0x148 bytes): Trading post panel — shows incoming merchants,
// trade items, buy/sell interface with order management.
// Extends DialogController (0x30 base). The field layout is TradeUI.h's.
//
// The panel's subject is DialogController::_dataSource narrowed to a
// ComponentEntity (retail: IsDerivedFrom 0x140152a98 against the ComponentEntity
// cell 0x1401c5d70, then the InternalBasePtr ctor); its TradeComponent is the
// "trading post" every body reads (FindComponent<TradeComponent>, 0x140015140).
//
// READS LEFT AT AN OFFSET (B.4, owner named):
//   TradeComponent +0x30/+0x40/+0x48/+0x50/+0x58/+0x60 (COMP-f): the order,
//     order-amount and buy-order arrays and the storage-order tree are `void*`
//     in TradeComponent.h, their element types undeclared.
//   StateMachineComponent +0x50/+0x51/+0x52 (COMP-e): the state bytes are one
//     `uint32_t stateFlags` in the header.
//   StorageComponent::storageSlots (W0): the 0x10-byte item is raw storage.
//   GameUpdateLoop::m_citizenList +0x110/+0x118 (CitizenBehavior, WORK-a): the
//     raw-material list.
//   ComponentDescription::_subDescs (COMP-c): `void**`.
//   UI::Element::_description (R-UI-a): `void*`.

#include "Dialog/ui_panels.h"
#include "Dialog/TradeUI.h"
#include "system/Value.h"
#include "ui/UserInterface.h"
#include "object/TypeInfo.h"
#include "Entity/exe_bodies.h"
#include "Dialog/exe_bodies.h"
#include "Component/exe_bodies.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Component/ValueDescription.h"   // ENDGAME B.3.9 TR: ValueDescription::TypeId()
#include "Component/TradeDescription.h"   // ENDGAME B.3.9 TR: TradeDescription::TypeId()
#include "Component/TradeComponent.h"
#include "Component/StorageComponent.h"
#include "system/String.h"
#include "system/Filename.h"
#include "object/External.h"   // ENDGAME B.3.3 step 6A
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch D
#include "io/Stream.h"
#include <new>
#include "ui/Element.h"      // ENDGAME B.3.6 UI callers (K1)
#include "ui/ElementDescription.h"  // ENDGAME B.3.6 UI callers (K1)
#include "ui/DialogInstance.h"
#include "ui/String.h"       // ENDGAME B.3.6 UI callers (K1)
#include "ui/Sprite.h"       // ENDGAME B.3.6 UI callers (K1)
#include "ui/StringTable.h"  // ENDGAME B.3.6 UI callers (K1)
#include "ui/TextElement.h"       // ENDGAME B.3.6 UI callers round 2 (K1)
#include "ui/LabelElement.h"      // ENDGAME B.3.6 UI callers round 2 (K1)
#include "ui/NumberElement.h"     // ENDGAME B.3.6 UI callers round 2 (K1)
#include "ui/ComboElement.h"      // ENDGAME B.3.6 UI callers round 2 (K1)
#include "ui/SpinnerElement.h"    // ENDGAME B.3.6 UI callers round 2 (K1)
#include "ui/TableElement.h"      // ENDGAME B.3.6 UI callers round 2 (K1)
#include "ui/TabElement.h"        // ENDGAME B.3.6 UI callers round 2 (K1)
#include "ui/ButtonElement.h"      // X::TypeId() (B.3.9 CG2)
#include "ui/CheckElement.h"
#include "Entity/ComponentEntity.h"   // X::TypeId() (B.3.9 CG2)
#include "Component/ComponentDescription.h"
#include "Component/RawMaterialDescription.h"   // X::TypeId() (B.3.9 CG2)
#include "Component/StateMachineComponent.h"   // X::TypeId() (B.3.9 CG2)
#include "Component/BuildingState.h"
#include "Component/ComponentDescription.h"   // X::TypeId() (B.3.9 CG2)
#include "Component/SelectDescription.h"   // X::TypeId() (B.3.9 CG2)
#include "system/MemoryAllocator.h"   // B3R: the allocator doors
#include "Entity/CitizenBehavior.h"   // the town's raw-material list (+0x110 / +0x118)
// EXT SEAM (NOT RETAIL, docs/extensions/handins/TRADERREQUEST.md): ext.trader_request (class C), the request row
#include "ext/trader_request/trader_request.h"


// Retail's sell/buy tables call the min/max setter through exe .rdata dispatch slot
// 0x140151cf8 (RefreshSellTable 0x1400841df, RefreshBuyTable 0x140083629,
// RefreshDisplay 0x140084be8 — one shared slot for all three tables). That slot's
// name string is literally "?SetMinMax@SpinnerElement@UI@@QEAAXHH@Z" (ordinal 3918):
// the element is a UI::SpinnerElement (0x140151cf0 is the SpinnerElement type slot;
// reg_element_typeids.cpp:36 documents the old "SliderElement" mislabel), so those
// sites call UI::SpinnerElement::SetMinMax directly (ENDGAME B.3.6).

// Slot map (byte offset = slot * 8), recorded against the retail vtable 0x14015de80:
//   slot 4  (+0x20) OnCreated        — the full UI SETUP (element lookup + table
//                   building). It must run before any other slot touches the caches.
//   slot 5  (+0x28) OnOpened         — mark tables for layout recalc
//   slot 6  (+0x30) OnClosed         — clear the post's dirty flag
//   slot 7  (+0x38) OnUpdateLayout   — trade value calcs, warning text, button enable
//   slot 8  (+0x40) OnUpdate         — per-frame refresh when the order count changes
//   slot 9  (+0x48) OnElementEvent   — event dispatcher (spinners/combos/buttons)
//   slot 10 (+0x50) OnSerializeSettings — serialize the tab/sort state
namespace tradeui_sa {
    // Slot 4 builds the element caches (+0xC0..+0x140) every other slot dereferences.
    // Same hazard ProfessionUI documents: the framework dispatches the per-frame slots
    // before setup has run, so they must skip until the caches exist.
    bool IsSetUp(const TradeUI* self) {
        return self->_tableSell != nullptr &&   // +0xC8
               self->_tableBuy != nullptr;      // +0xD0
    }
    void Trace(const char* slot, const TradeUI* self) {
        static int n=0; if (n>=32) return; ++n;
        // stderr, NOT stdout — two independent reasons, both already project rules:
        //  1. `std::printf` does not survive the release build. log_strip.h defines
        //     `printf(...)` as `((void)0)`, so the QUALIFIED call expands to
        //     `std::((void)0)` and fails with "expected unqualified-id before '('".
        //     This single line broke `make standalone` for the whole tree.
        //  2. stdout STALLS in this exe (standalone_debug.log froze at 22KB on the splash
        //     while stderr kept growing all session), so a printf-based harness prints
        //     NOTHING and looks identical to one that never fired — which is exactly the
        //     symptom this trace was added to investigate.
        std::fprintf(stderr, "[TradeUI] %s self=%p tableSell(+0xC8)=%p tableBuy(+0xD0)=%p "
               "buttonTrade(+0xF0)=%p sell.n(+0x38)=%d buy.n(+0x48)=%d\n", slot,
               static_cast<const void*>(self),
               self?static_cast<const void*>(self->_tableSell):nullptr,
               self?static_cast<const void*>(self->_tableBuy):nullptr,
               self?static_cast<const void*>(self->_buttonTrade):nullptr,
               self?self->_sellItems._size:0,
               self?self->_buyItems._size:0);
        std::fflush(stderr);
    }
    // Slot 4 is idempotent-guarded for the same reason ProfessionUI's Slot5 is: a second
    // run re-clones table rows and appends to the element arrays instead of clearing.
}

// ============================================================
// OnOpened (vtable +0x28): Mark tables for layout recalc, set the post's dirty flag
// ============================================================
// Original: 0x140082a70
//
// Marks tableSell (+0xC8) and tableBuy (+0xD0) as needing size recalculation.
// Then finds the TradeComponent on the owning entity and sets its
// dirty flag (byte at +0x71) to 1, triggering a visual refresh.

// [5] 0x140082a70
void TradeUI::OnOpened() {
    if (!tradeui_sa::IsSetUp(this)) return;
    tradeui_sa::Trace("Slot5-draw", this);

    // Mark sell and buy tables for layout recalculation
    _tableSell->SetNeedsSizeCalc();
    _tableBuy->SetNeedsSizeCalc();

    // Get owning entity and find the TradeComponent
    ::Object::Base* source = _dataSource;
    ComponentEntity* entity = ::Object::IsDerivedFromNullSafe(source, ComponentEntity::TypeId())
        ? static_cast<ComponentEntity*>(source) : nullptr;
    TradeComponent* trade = entity->FindComponent<TradeComponent>();

    // Set the post's dirty flag
    trade->tradeFlag = 1;
}

// ============================================================
// OnClosed (vtable +0x30): Clear the post's dirty flag
// ============================================================
// Original: 0x140082a00
//
// Finds the TradeComponent on the owning entity, clears its dirty flag
// (byte at +0x71 = 0). Called when panel is hidden/deactivated.

// [6] 0x140082a00
void TradeUI::OnClosed() {
    if (!tradeui_sa::IsSetUp(this)) return;

    ::Object::Base* source = _dataSource;
    ComponentEntity* entity = ::Object::IsDerivedFromNullSafe(source, ComponentEntity::TypeId())
        ? static_cast<ComponentEntity*>(source) : nullptr;
    TradeComponent* trade = entity->FindComponent<TradeComponent>();

    trade->tradeFlag = 0;
}

// ============================================================
// OnCreated (vtable +0x20): Full UI setup — element lookup, table building
// ============================================================
// Original: 0x140084dd0
//
// Phase 1: Finds 15 named elements and stores them in self.
// Phase 2: Sets element IDs on buttons, combos, and sort combo.
// Phase 3: Builds tablePurchase rows with child spinner elements (stride 0x20).
// Phase 4: Counts resource types via ExternalFactory, counts selectable
//          build types, sizes tableStorage/tableSell/tableBuy/tableOrder,
//          clones template elements, finds row sub-elements (spinners etc).
// Phase 5: Calls RefreshDisplay, RefreshSellTable, RefreshBuyTable.

// Helper: find a named element under the dialog's container and keep it only when
// it IsDerivedFrom T (retail: FindElement, then the IsDerivedFrom import).
template <class T>
static T* TradeUI_FindTypedElement(UI::Element* container, const char* name) {
    System::String strBuf(name);
    UI::Element* elem = container->FindElement(strBuf).Get();
    T* result = nullptr;
    if (elem) {
        bool derived = ::Object::IsDerivedFromNullSafe(elem, T::TypeId());
        if (derived) result = static_cast<T*>(elem);
    }
    return result;
}

// Helper: clone template into all table cells
// templateObj must be read from first child's +0xB8 BEFORE calling SetSize,
// because SetSize destroys/replaces children.
static void TradeUI_CloneTableCells(const TradeUI* self, UI::TableElement* table,
                                    const UI::ElementDescription* templateObj) {
    int totalCells = table->ChildCount();
    for (int i = 0; i < totalCells; i++) {
        // Clone via the template's slot 8, UI::ElementDescription::CreateInstance
        // (retslot, B6-2). templateObj is Element::_description (+0xB8) of the
        // table's Child(0) — every caller passes TradeUI_GetTableTemplate's result.
        ::Object::InternalPtr<UI::Element> clone;
        templateObj->CreateInstance(&clone, self->_dialog->_dialog);

        table->SetChild(&clone, i);
    }
}

// Helper: read template from table's first child +0xB8 (must be called BEFORE SetSize)
static const UI::ElementDescription* TradeUI_GetTableTemplate(UI::TableElement* table) {
    UI::Element* firstChild = table->Child(0).Get();
    if (!firstChild) return nullptr;
    return firstChild->_description;
}

// ============================================================
// Helper: find a description sub-component by typeId
// ============================================================
// The description's sub-description array (+0x28, count +0x30); retail stops at
// the first type-id match and keeps it only when IsDerivedFrom agrees.
static ::Object::Base* FindDescComponent(const ComponentDescription* descEntity, uint32_t typeId)
{
    if (!descEntity) return nullptr;
    int count = descEntity->_subDescCount;
    for (int i = 0; i < count; i++) {
        ::Object::Base* comp = descEntity->_subDescs[i];
        int tid = comp->Type();
        if ((uint32_t)tid == typeId) {
            if (::Object::IsDerivedFromNullSafe(comp, typeId))
                return comp;
            return nullptr;
        }
    }
    return nullptr;
}

// ============================================================
// Helper: set spinner increment based on max value
// ============================================================
static void SetSpinnerIncrement(UI::SpinnerElement* spinner)
{
    int maxVal = spinner->GetMaxValue();
    if (maxVal < 0x15) {
        spinner->SetIncrement(1);
    } else if (maxVal < 0x33) {
        spinner->SetIncrement(5);
    } else if (maxVal < 0x65) {
        spinner->SetIncrement(0x14);
    }
    // >= 0x65: no increment set (uses default)
}

// ============================================================
// Helper: set element name from External resource path
// ============================================================
static void SetElementNameFromExternal(UI::Element* element, const ::Object::External* external)
{
    // `GetPath` returns by value through a hidden return slot and the callee
    // CONSTRUCTS into it; copy-initialising the local is the same store.
    System::Filename pathBuf = external->GetPath();
    System::Filename nameStr = pathBuf.GetPathFilename(false);
    element->SetName(nameStr.AsString());
}

// ============================================================
// RefreshSellTable (0x140083c70)
// ============================================================
// Rebuilds the sell table data and UI.
//
// Phase 1: Get entity components (TradeComponent, StorageComponent).
// Phase 2: Iterate resource entries from the game world's resource table.
//          For each resource type, check the post's storage BST for availability.
//          If available (count > 0), find ValueDescription + ResourceType sub-components
//          and push a 0x20-byte sell data entry.
// Phase 3: Build state check, then populate sell table UI cells.
//          For each sell entry: set sprite, text, counts, spinner limits, color.
// Phase 4: Clear unused sell table cells.

void TradeUI::RefreshSellTable()
{
    // Get entity, TradeComponent, StorageComponent
    ::Object::Base* source = _dataSource;
    ComponentEntity* entity = ::Object::IsDerivedFromNullSafe(source, ComponentEntity::TypeId())
        ? static_cast<ComponentEntity*>(source) : nullptr;
    TradeComponent* trade = entity->FindComponent<TradeComponent>();

    // Check TradeDescription on the post
    ::Object::IsDerivedFromNullSafe(trade->_description, TradeDescription::TypeId());

    StorageComponent* storage = entity->FindComponent<StorageComponent>();

    // Get resource table from game world singleton (+0xf0 = resource manager)
    CitizenBehavior* resourceMgr = GameUpdateLoop::_singleton->m_citizenList;
    int resourceCount = resourceMgr->knownRawMaterials._size;

    // Clear sell data array at +0x30 (stride 0x20)
    _sellItems.Clear();   // 0x140016050

    // Phase 2: Iterate resources and build sell entries
    if (resourceCount > 0) {
        for (int ri = 0; ri < resourceCount; ri++) {
            ValueDescription* valueDesc = nullptr;
            RawMaterialDescription* resTypeComp = nullptr;

            // Get resource entity from resource table
            ComponentDescription* resEntity = resourceMgr->knownRawMaterials._data[ri];
            const ComponentDescription* resKey = resEntity;

            // Search the post's storage BST for this resource
            TradeResourceNode* bstNode = trade->stock.root;
            bool found = false;
            while (bstNode != nullptr) {
                if (resKey < bstNode->resource) {
                    // left at +0x18. Confirmed byte-exact: the node is
                    // { key +0x00, value +0x08, parent +0x10, left +0x18,
                    //   right +0x20, balance +0x28 } -- see the layout block in
                    // tradecomponent_serialize.cpp (disassembled from this field's
                    // own insert/rotate bodies) and retail's descent in
                    // TutorialGoalCount::Check case 7 (0x1400df290).
                    bstNode = bstNode->left;  // left
                } else if (resKey <= bstNode->resource) {
                    // Found — check if count (at node+0x08) > 0
                    if (bstNode != nullptr && bstNode->quantity != 0) {
                        // Find ValueDescription and ResourceType in resource entity
                        int descCount = resEntity->_subDescCount;
                        for (int d = 0; d < descCount; d++) {
                            ::Object::Base* comp = resEntity->_subDescs[d];
                            int tid = comp->Type();
                            if ((uint32_t)tid == RawMaterialDescription::TypeId()) {
                                if (::Object::IsDerivedFromNullSafe(comp, RawMaterialDescription::TypeId()))
                                    resTypeComp = static_cast<RawMaterialDescription*>(comp);
                                break;
                            }
                        }
                        // Find ValueDescription
                        for (int d = 0; d < descCount; d++) {
                            ::Object::Base* comp = resEntity->_subDescs[d];
                            int tid = comp->Type();
                            if ((uint32_t)tid == ValueDescription::TypeId()) {
                                if (::Object::IsDerivedFromNullSafe(comp, ValueDescription::TypeId()))
                                    valueDesc = static_cast<ValueDescription*>(comp);
                                break;
                            }
                        }
                        found = true;
                    }
                    break;
                } else {
                    bstNode = bstNode->right;  // right
                }
            }

            if (!found) continue;

            // Build 0x20-byte sell data entry and push to array at +0x30.
            _sellItems.SetSize(_sellItems.GetSize() + 1,
                               SellItem{resEntity, valueDesc, resTypeComp, 0, 0});
        }
    }

    // Phase 3: Get build state, populate sell table UI
    int sellCount = _sellItems._size;
    uint64_t sellCountU = (uint64_t)sellCount;

    // Find the state machine on the entity
    StateMachineComponent* buildStateComp = entity->FindComponent<StateMachineComponent>();

    // Check trade state: buildState byte +0x52 (or +0x50 if negative)
    bool tradeActive = false;
    if (buildStateComp) {
        int8_t state52 = buildStateComp->stateMachine.pendingState;
        int8_t stateVal = state52;
        if (state52 < 0) stateVal = buildStateComp->stateMachine.currentInput;
        tradeActive = (stateVal == StateByte(BuildingState::MerchantDocked));
    }

    // Populate sell table cells
    if (sellCount > 0) {
        for (int i = 0; i < sellCount; i++) {
            SellItem& sellData = _sellItems[i];
            SellRow& sellUI = _sellRows[i];

            // Get sellable count from the post
            int sellableCount = trade->GetResourcePrice(sellData.resource);

            bool hasStock = (sellableCount > 0);
            uint32_t color = hasStock ? System::kColorWhite : System::kColorGrey;

            // Set sprite from ValueDescription (+0x20=spriteSheet, +0x28=spriteName)
            ValueDescription* valueDesc = sellData.value;
            UI::Sprite sprite(::Object::ExternalPtr<UI::SpriteSheet>(valueDesc->_sprite), valueDesc->_name);
            sellUI.label->SetSprite(&sprite);

            // Set text from ValueDescription (+0x30=stringTable, +0x38=stringName)
            UI::String uiStr(::Object::ExternalPtr<UI::StringTable>(valueDesc->_stringTable), valueDesc->_stringName);
            sellUI.label->SetText(&uiStr);

            // Set sellable count on number element (sellUI+0x10)
            sellUI.price->SetValue(static_cast<int32_t>(sellableCount));

            // Search the storage items for the matching resource to get the stock
            int orderQty = 0;
            int smOrderCount = storage->storageSlots._size;
            if (smOrderCount > 0) {
                StorageComponent::Item* orderArr = storage->storageSlots._data;
                for (int j = 0; j < smOrderCount; j++) {
                    if (orderArr[j]._resource == sellData.resource) {
                        if (j >= 0) {
                            orderQty = orderArr[j]._count
                                       - orderArr[j]._reserve;
                        }
                        break;
                    }
                }
            }

            // Set order qty on number element (sellUI+0x08)
            sellUI.stored->SetValue(static_cast<int32_t>(orderQty));

            // Set spinner enabled state
            bool spinnerEnabled = tradeActive && hasStock;
            sellUI.spinner->Enable(spinnerEnabled);

            // Set number and label elements enabled
            sellUI.stored->Enable(true);
            sellUI.price->Enable(true);

            // Set spinner min/max
            int spinnerMax = 0;
            if (tradeActive && hasStock) {
                spinnerMax = sellableCount;
            }
            // sellUI+0x18 is a SpinnerElement (exe slot 0x140151cf8 = SpinnerElement::SetMinMax)
            sellUI.spinner->SetMinMax(0, spinnerMax);

            // Set spinner increment based on max value
            SetSpinnerIncrement(sellUI.spinner);

            // Reset spinner value to 0
            sellUI.spinner->SetValue(0);

            // Set element name from resource path
            SetElementNameFromExternal(sellUI.spinner, sellData.resource);

            // Set color on label, number, and count elements
            sellUI.label->SetColor(color);
            sellUI.stored->SetColor(color);
            sellUI.price->SetColor(color);
        }
    }

    // Phase 4: Clear unused sell table cells
    int totalSellCells = 0;
    totalSellCells = _tableSell->_rowCount;
    if ((int)sellCountU < totalSellCells) {
        for (int i = (int)sellCountU; i < totalSellCells; i++) {
            SellRow& cellUI = _sellRows[i];

            // Set empty sprite
            UI::Sprite emptySprite;
            cellUI.label->SetSprite(&emptySprite);

            // Set empty text
            UI::String emptyStr;
            cellUI.label->SetText(&emptyStr);

            // Disable spinner, number, and label elements (null-guard each)
            if (cellUI.spinner)
                cellUI.spinner->Enable(false);
            if (cellUI.stored)
                cellUI.stored->Enable(false);
            if (cellUI.price)
                cellUI.price->Enable(false);

            // cellUI+0x18 is a SpinnerElement (exe slot 0x140151cf8 = SpinnerElement::SetMinMax)
            if (cellUI.spinner)
                cellUI.spinner->SetMinMax(0, 0);
        }
    }
}

// ============================================================
// RefreshBuyTable (0x140083100)
// ============================================================
// Rebuilds the buy table data and UI.
//
// Phase 1: Get entity components (TradeComponent + TradeDescription).
// Phase 2: Set up order header text ("textName"/"textOrder") with
//          trade order name and current merchant info.
// Phase 3: Build buy entries array from trade orders (trade+0x50/+0x58).
// Phase 4: Populate buy table UI cells with sprite/text/numbers.
// Phase 5: Clear unused buy table cells.
// Phase 6: Build purchase reserved entries (+0xb0/+0xb8).
// Phase 7: Set combo and enable state for order combo.
// Phase 8: Populate purchase button array with checked states.

void TradeUI::RefreshBuyTable()
{
    // Get entity, TradeComponent, TradeDescription
    ::Object::Base* source = _dataSource;
    ComponentEntity* entity = ::Object::IsDerivedFromNullSafe(source, ComponentEntity::TypeId())
        ? static_cast<ComponentEntity*>(source) : nullptr;
    TradeComponent* trade = entity->FindComponent<TradeComponent>();

    Description* description = trade->_description;
    bool isTrade = ::Object::IsDerivedFromNullSafe(description, TradeDescription::TypeId());
    TradeDescription* tradeDesc = isTrade
        ? static_cast<TradeDescription*>(description) : nullptr;

    // Clear buy data array at +0x40 (stride 0x18)
    _buyItems.Clear();   // 0x140016130

    // Phase 2: Set order header text elements
    for (int ti = 0; ti < 2; ti++) {
        const char* elemName = (ti == 0) ? "textName" : "textOrder";
        UI::TextElement* textElem = TradeUI_FindTypedElement<UI::TextElement>(_container, elemName);

        // Set text parameters based on current order
        int orderIdx = trade->tradeId;
        if (orderIdx < 0) {
            // No active order — text stays default
        } else {
            // Get order entry from trade description
            const TradeMerchant* orderEntry = tradeDesc->_merchants + orderIdx;

            // Set parameter 0: order name string
            System::String orderNameBuf;
            trade->GetOrderName(&orderNameBuf);   // 0x140015fd0
            textElem->SetParameter(0, orderNameBuf);

            // Set parameter 1: UI string from order entry
            UI::String uiStr(::Object::ExternalPtr<UI::StringTable>(orderEntry->stringTable),
                             orderEntry->name);
            // Convert to System::String
            System::String sysStrBuf;
            ::new (&sysStrBuf) System::String(uiStr.operator System::String());
            textElem->SetParameter(1, sysStrBuf);
        }

        // Show/hide text element based on order validity
        // vtable+0x50 is a visibility function: EDX=0 hides, EDX=-1 shows
        // Original asm: orderIdx<0 → XOR EDX,EDX; orderIdx>=0 → OR EDX,0xffffffff
        // Slot 10 of UI::Element is SetColor(uint32); the receiver is the
        // FindElement("textName"/"textOrder") result above.
        textElem->SetColor(static_cast<uint32_t>((orderIdx < 0) ? 0 : -1));
    }

    // Phase 3: Build buy entries from trade orders
    int tradeOrderCount = trade->manifest._size;
    if (tradeOrderCount > 0) {
        for (int oi = 0; oi < tradeOrderCount; oi++) {
            // Get trade order entry
            ComponentDescription* orderEntry = trade->manifest._data[oi].good;

            // Find ValueDescription in order entry's description
            ValueDescription* valDesc = static_cast<ValueDescription*>(
                FindDescComponent(orderEntry, ValueDescription::TypeId()));

            // Build 0x18-byte buy data entry and push to array at +0x40.
            _buyItems.SetSize(_buyItems.GetSize() + 1, BuyItem{orderEntry, valDesc, 0, 0});
        }
    }

    // Phase 4: Populate buy table UI cells
    int buyCount = trade->manifest._size;
    uint64_t buyCountU = (uint64_t)buyCount;

    if (buyCount > 0) {
        uintptr_t buyDataIndex = 0;
        for (int i = 0; i < buyCount; i++) {
            BuyRow& buyUI = _buyRows[i];

            TradeManifestLine* buyOrder = trade->manifest._data + buyDataIndex;
            ComponentDescription* orderDesc = buyOrder->good;

            // Find ValueDescription
            ValueDescription* valDesc = static_cast<ValueDescription*>(
                FindDescComponent(orderDesc, ValueDescription::TypeId()));

            // Set sprite from ValueDescription
            UI::Sprite sprite(::Object::ExternalPtr<UI::SpriteSheet>(valDesc->_sprite), valDesc->_name);
            buyUI.label->SetSprite(&sprite);

            // Set text from ValueDescription
            UI::String uiStr(::Object::ExternalPtr<UI::StringTable>(valDesc->_stringTable), valDesc->_stringName);
            buyUI.label->SetText(&uiStr);

            // Set quantity number (buyUI+0x18)
            buyUI.quantity->SetValue(
                static_cast<int32_t>(buyOrder->stock));

            // Set price number (buyUI+0x08)
            buyUI.price->SetValue(
                static_cast<int32_t>(buyOrder->price));

            // Set spinner min/max (buyUI+0x10 is a SpinnerElement, exe slot 0x140151cf8)
            buyUI.spinner->SetMinMax(
                0, buyOrder->stock);

            // Set spinner increment
            SetSpinnerIncrement(buyUI.spinner);

            // Set element name from resource path
            SetElementNameFromExternal(buyUI.spinner, orderDesc);

            // Enable all sub-elements
            buyUI.spinner->Enable(true);
            buyUI.quantity->Enable(true);
            buyUI.price->Enable(true);

            buyDataIndex += 1;
        }
    }

    // Phase 5: Clear unused buy table cells
    int totalBuyCells = _tableBuy->_rowCount;
    if ((int)buyCountU < totalBuyCells) {
        for (int i = (int)buyCountU; i < totalBuyCells; i++) {
            BuyRow& cellUI = _buyRows[i];

            // Set empty sprite
            UI::Sprite emptySprite;
            cellUI.label->SetSprite(&emptySprite);

            // Set empty text
            UI::String emptyStr;
            cellUI.label->SetText(&emptyStr);

            // cellUI+0x10 is a SpinnerElement (exe slot 0x140151cf8 = SpinnerElement::SetMinMax)
            if (cellUI.spinner)
                cellUI.spinner->SetMinMax(0, 0);

            if (cellUI.spinner)
                cellUI.spinner->Enable(false);
            if (cellUI.quantity)
                cellUI.quantity->Enable(false);
            if (cellUI.price)
                cellUI.price->Enable(false);
        }
    }

    // Phase 6: Build purchase reserved entries (+0xb0/+0xb8)
    int orderIdx = trade->tradeId;
    if (orderIdx < 0) {
        // No active order — clear purchase list (the data, then the size and
        // capacity: retail's one qword store at +0xb8, H7)
        System::MemoryAllocator::MemFree(_purchaseItems._data);
        _purchaseItems._data = nullptr;
        _purchaseItems._size = 0;
        _purchaseItems._capacity = 0;
    } else {
        // Populate purchase reserved entries via helper
        trade->GetBuyEntries(&_purchaseItems);
    }

    // Phase 7: Set order combo current item and enabled state
    int comboIdx = 0;
    if (trade->tradeId >= 0) {
        comboIdx = trade->merchantOrders._data[trade->tradeId].orderKind;
    }
    _comboOrder->SetCurrentItem(comboIdx);

    bool comboEnabled = (trade->tradeId >= 0);
    _comboOrder->Enable(comboEnabled);

    // Phase 8: Populate purchase button array with checked states
    int purchaseCount = _purchaseItems._size;
    if (purchaseCount > 0) {
        for (int pi = 0; pi < purchaseCount; pi++) {
            ComponentDescription* purchaseEntity = _purchaseItems[pi].resource;

            // Find ValueDescription for this purchase entity
            ValueDescription* valDesc = static_cast<ValueDescription*>(
                FindDescComponent(purchaseEntity, ValueDescription::TypeId()));

            // Set sprite and text on purchase button element
            int childCount = _purchaseRows[pi].check->ChildCount();

            for (int ci = 0; ci < childCount; ci++) {
                UI::Element* child = _purchaseRows[pi].check->Child(ci).Get();

                // IsDerivedFrom TextElement check
                UI::TextElement* childElem = nullptr;
                if (child) {
                    bool isText = ::Object::IsDerivedFromNullSafe(child, UI::TextElement::TypeId());
                    if (isText) childElem = static_cast<UI::TextElement*>(child);
                }

                if (childElem != nullptr) {
                    // Set sprite
                    UI::Sprite sprite(::Object::ExternalPtr<UI::SpriteSheet>(valDesc->_sprite), valDesc->_name);
                    static_cast<UI::LabelElement*>(childElem)->SetSprite(&sprite);

                    // Set text
                    UI::String uiStr(::Object::ExternalPtr<UI::StringTable>(valDesc->_stringTable), valDesc->_stringName);
                    childElem->SetText(&uiStr);
                }

                childCount = _purchaseRows[pi].check->ChildCount();
            }

            // Enable purchase button
            _purchaseRows[pi].check->Enable(true);

            // Check if this purchase entity is in the current order's selection list
            TradeMerchantOrder* orderData = trade->merchantOrders._data + trade->tradeId;
            int selCount = orderData->goods._size;
            if (selCount > 0) {
                ComponentDescription** selList = orderData->goods._data;
                for (int si = 0; si < selCount; si++) {
                    if (selList[si] == _purchaseItems[pi].resource) {
                        _purchaseRows[pi].check->_checked = 1;
                        break;
                    }
                }
            }
        }
    }

    // Clear unused purchase buttons
    int totalPurchaseSlots = _purchaseRows._size;
    if (purchaseCount < totalPurchaseSlots) {
        for (int i = purchaseCount; i < totalPurchaseSlots; i++) {
            // Clear children
            int ci = 0;
            int childCount = _purchaseRows[i].check->ChildCount();
            while (ci < childCount) {
                UI::Element* child = _purchaseRows[i].check->Child(ci).Get();

                UI::TextElement* childElem = nullptr;
                if (child) {
                    bool isText = ::Object::IsDerivedFromNullSafe(child, UI::TextElement::TypeId());
                    if (isText) childElem = static_cast<UI::TextElement*>(child);
                }

                if (childElem != nullptr) {
                    UI::Sprite emptySprite;
                    static_cast<UI::LabelElement*>(childElem)->SetSprite(&emptySprite);

                    UI::String emptyStr;
                    childElem->SetText(&emptyStr);
                }

                ci++;
                childCount = _purchaseRows[i].check->ChildCount();
            }

            // Disable button and clear checked state
            _purchaseRows[i].check->Enable(false);

            _purchaseRows[i].check->_checked = 0;
        }
    }
}

// [4] 0x140084dd0
void TradeUI::OnCreated() {
    // Slot 4 is idempotent-guarded for the same reason ProfessionUI's Slot5 is: a second
    // run re-clones table rows and appends to the element arrays instead of clearing.
    if (tradeui_sa::IsSetUp(this)) { tradeui_sa::Trace("Slot4-skip(already set up)", this); return; }
    tradeui_sa::Trace("Slot4-setup", this);

    // --- Get entity and find the TradeComponent ---
    ::Object::Base* source = _dataSource;
    ComponentEntity* entity = ::Object::IsDerivedFromNullSafe(source, ComponentEntity::TypeId())
        ? static_cast<ComponentEntity*>(source) : nullptr;
    TradeComponent* trade = entity->FindComponent<TradeComponent>();

    // --- Phase 1: Find all 15 named elements ---
    // ENDGAME B.3.9 CG2: TypeId_HelpButtonElem shares its initialiser exref
    // (0x1401514f8) with TypeId_ButtonElement -> UI::ButtonElement::TypeId();
    // TypeId_HelpComboElem shares its initialiser exref (0x140151690) with
    // TypeId_ComboElement -> UI::ComboElement::TypeId() (Table 3 name lies —
    // neither is a distinct "help" element class).

    // Tables (IsDerivedFrom TableElement)
    _tableSell     = TradeUI_FindTypedElement<UI::TableElement>(_container, "tableSell");
    _tableBuy      = TradeUI_FindTypedElement<UI::TableElement>(_container, "tableBuy");
    _tableStorage  = TradeUI_FindTypedElement<UI::TableElement>(_container, "tableStorage");
    _tablePurchase = TradeUI_FindTypedElement<UI::TableElement>(_container, "tablePurchase");
    _tableOrder    = TradeUI_FindTypedElement<UI::TableElement>(_container, "tableOrder");

    // Number elements (IsDerivedFrom NumberElement)
    _numberSellTotal = TradeUI_FindTypedElement<UI::NumberElement>(_container, "numberSellTotal");
    _numberBuyTotal  = TradeUI_FindTypedElement<UI::NumberElement>(_container, "numberBuyTotal");

    // Buttons (IsDerivedFrom ButtonElement)
    _buttonTrade = TradeUI_FindTypedElement<UI::ButtonElement>(_container, "buttonTrade");
    _buttonTrade->SetElementId(0x4e20);

    _buttonDismiss = TradeUI_FindTypedElement<UI::ButtonElement>(_container, "buttonDismiss");
    _buttonDismiss->SetElementId(0x4e21);

    // Text elements (IsDerivedFrom TextElement)
    _textNoStorage = TradeUI_FindTypedElement<UI::TextElement>(_container, "textNoStorage");
    _textTooMuch   = TradeUI_FindTypedElement<UI::TextElement>(_container, "textTooMuch");
    _textNotEnough = TradeUI_FindTypedElement<UI::TextElement>(_container, "textNotEnough");

    // Combo elements (IsDerivedFrom ComboElement)
    _comboPurchase = TradeUI_FindTypedElement<UI::ComboElement>(_container, "comboPurchase");
    // Set comboPurchase current item from the post's purchase selection
    int purchaseIdx = trade->tradeState;
    _comboPurchase->SetCurrentItem(purchaseIdx);
    _comboPurchase->SetElementId(0x4e22);

    _comboOrder = TradeUI_FindTypedElement<UI::ComboElement>(_container, "comboOrder");
    _comboOrder->SetElementId(0x4e23);

    _sortCombo = TradeUI_FindTypedElement<UI::ComboElement>(_container, "sortCombo");
    _sortCombo->SetElementId(199);   // 0xC7
    _sortCombo->SetCurrentItem(0);

    // --- Phase 2: Build tablePurchase row array (stride 0x20, 4 elements per row) ---
    // tablePurchase uses FLAT indexing with stride 3 per logical row:
    //   child[k]   = compound cell with sub-child(0)=up, sub-child(1)=down
    //   child[k+1] = label element (standalone)
    //   child[k+2] = spinner element (standalone)
    // +0xC0 = row count. Element IDs: up=baseId-1000, down=baseId, spinner=baseId+1000
    int purchaseRowCount = _tablePurchase->_rowCount;

    int baseId = 6000;
    int flatIdx = 0;
    for (int row = 0; row < purchaseRowCount; row++) {
        // child[flatIdx] = compound cell -> sub-child(0) = up button
        UI::Element* cell = _tablePurchase->Child(flatIdx).Get();
        UI::Element* upButton = cell->Child(0).Get();

        // same compound cell -> sub-child(1) = down button
        cell = _tablePurchase->Child(flatIdx).Get();
        flatIdx++;
        UI::Element* downButton = cell->Child(1).Get();

        // child[flatIdx] = label (standalone)
        UI::LabelElement* label = static_cast<UI::LabelElement*>(_tablePurchase->Child(flatIdx).Get());

        // child[flatIdx+1] = spinner (standalone)
        UI::SpinnerElement* spinner = static_cast<UI::SpinnerElement*>(_tablePurchase->Child(flatIdx + 1).Get());

        flatIdx += 2;  // advance past label + spinner (total +3 per row)

        // Set element IDs
        if (upButton) upButton->SetElementId(baseId - 1000);

        if (downButton) downButton->SetElementId(baseId);

        // The spinner carries the id the msg-0x15 (spinner changed) notification sends
        // as param4. Retail tags it through SpinnerElement::SetElementId (exe dispatch slot
        // 0x140151cd8 -> "?SetElementId@SpinnerElement@UI@@QEAAXH@Z", Runtime 0x1800d7e10),
        // NOT Element::SetUserData and NOT the Element base SetElementId. The override
        // writes +0x88 on the spinner AND on the inner edit element at *(spinner+0xa0), so
        // both halves of the widget answer to the same id. Faithful, verified at the four
        // retail call sites (EDX = base+0x3e8 / +0x7d0 / +0xbb8 / +0xfa0).
        if (spinner) spinner->SetElementId(baseId + 1000);

        // Build 0x20-byte entry {up, down, label, spinner} and push to the
        // order-entries array at +0x90.
        _orderRows.SetSize(_orderRows.GetSize() + 1,
                           OrderRow{upButton, downButton, label, spinner});

        baseId++;
    }

    // --- Phase 3: Count resources and selectable builds via ExternalFactory ---
    int resourceTypeCount = 0;
    const ::Object::ExternalFactory* etuFactory = ::Object::ExternalFactory::_singleton;
    const ::Object::External* ext = etuFactory->FindNext(RawMaterialDescription::TypeId(), nullptr);
    while (ext != nullptr) {
        resourceTypeCount++;
        ext = etuFactory->FindNext(RawMaterialDescription::TypeId(), ext);
    }

    int selectableCount = 0;
    const ::Object::External* buildExt = etuFactory->FindNext(ComponentDescription::TypeId(), nullptr);
    while (buildExt != nullptr) {
        // Check if this ComponentDescription external has a SelectDescription sub-component
        bool isDerivedBuild = ::Object::IsDerivedFromNullSafe(buildExt, ComponentDescription::TypeId());
        const ComponentDescription* buildComp =
            isDerivedBuild ? static_cast<const ComponentDescription*>(buildExt) : nullptr;
        if (buildComp) {
            int compCount = buildComp->_subDescCount;
            for (int c = 0; c < compCount; c++) {
                ::Object::Base* comp = buildComp->_subDescs[c];
                int typeId = comp->Type();
                if (typeId == (int)SelectDescription::TypeId()) {
                    ::Object::Base* selectComp = nullptr;
                    if (::Object::IsDerivedFromNullSafe(comp, SelectDescription::TypeId()))
                        selectComp = comp;
                    if (selectComp) selectableCount++;
                    break;
                }
            }
        }
        buildExt = etuFactory->FindNext(ComponentDescription::TypeId(), buildExt);
    }

    // --- Phase 4a: Setup tableStorage ---
    int storageColCount = _tableStorage->_columnCount;

    // Get template BEFORE SetSize (SetSize destroys children)
    {
        int rowCount = ((resourceTypeCount - 1 + storageColCount) / storageColCount * storageColCount)
                       / storageColCount;
        const UI::ElementDescription* storageTemplate = TradeUI_GetTableTemplate(_tableStorage);
        _tableStorage->SetSize(rowCount, storageColCount);

        TradeUI_CloneTableCells(this, _tableStorage, storageTemplate);
    }

    // Iterate tableStorage cells, find "ribbonStorageRow" sub-elements
    int storageCellCount = _tableStorage->ChildCount();
    for (int i = 0; i < storageCellCount; i++) {
        // Find "ribbonStorageRow" in cell
        System::String strBuf("ribbonStorageRow");
        UI::Element* cell = _tableStorage->Child(i).Get();
        UI::Element* rowElem = cell->FindElement(strBuf).Get();

        // child(0) = label, child(1) = spinner, child(2) = number
        UI::LabelElement* e0 = static_cast<UI::LabelElement*>(rowElem->Child(0).Get());
        UI::SpinnerElement* e1 = static_cast<UI::SpinnerElement*>(rowElem->Child(1).Get());
        UI::NumberElement* e2 = static_cast<UI::NumberElement*>(rowElem->Child(2).Get());

        // child(1) is a SpinnerElement, not a slider: retail tags it through
        // SpinnerElement::SetElementId (slot 0x140151cd8), which sets +0x88 on the spinner
        // and on its inner edit element at *(spinner+0xa0). Retail call site 0x140085d40,
        // EDX = i + 0x7d0. See the tablePurchase loop above.
        e1->SetElementId(i + 2000);

        // Build 0x18-byte entry {e0, e1, e2} and push to the storage-entries array
        // at +0x80.
        _storageRows.SetSize(_storageRows.GetSize() + 1, StorageRow{e0, e1, e2});
    }

    // --- Phase 4b: Setup tableSell ---
    int sellColCount = _tableSell->_columnCount;

    {
        int rowCount = (((resourceTypeCount - 1 + sellColCount) / sellColCount) * sellColCount)
                       / sellColCount;
        const UI::ElementDescription* sellTemplate = TradeUI_GetTableTemplate(_tableSell);
        _tableSell->SetSize(rowCount, sellColCount);

        TradeUI_CloneTableCells(this, _tableSell, sellTemplate);
    }

    // Iterate tableSell cells, find "ribbonSellRow" sub-elements (4 children per row)
    int sellCellCount = _tableSell->_rowCount;
    for (int i = 0; i < sellCellCount; i++) {
        System::String strBuf("ribbonSellRow");
        UI::Element* cell = _tableSell->Child(i).Get();
        UI::Element* rowElem = cell->FindElement(strBuf).Get();

        // Get 4 child elements from ribbonSellRow:
        // child(0)=stock number, child(1)=LabelElement, child(2)=price number, child(3)=SpinnerElement
        UI::NumberElement* spinElem = static_cast<UI::NumberElement*>(rowElem->Child(0).Get());
        UI::LabelElement* labelElem = static_cast<UI::LabelElement*>(rowElem->Child(1).Get());
        UI::NumberElement* numElem = static_cast<UI::NumberElement*>(rowElem->Child(2).Get());
        UI::SpinnerElement* sliderElem = static_cast<UI::SpinnerElement*>(rowElem->Child(3).Get());

        // child(3) is a SpinnerElement (the "SliderElement" naming is the same mislabel
        // reg_element_typeids.cpp:36 documents). Retail call site 0x14008614e, EDX = i+0xbb8.
        sliderElem->SetElementId(i + 3000);

        // Build 0x20-byte entry {child1, child0, child2, child3} and push to the
        // sell-entries array at +0x60.
        _sellRows.SetSize(_sellRows.GetSize() + 1,
                          SellRow{labelElem, spinElem, numElem, sliderElem});
    }

    // --- Phase 4c: Setup tableBuy ---
    int buyColCount = _tableBuy->_columnCount;

    {
        // tableBuy size: rows for (selectableCount + resourceTypeCount - 1) items
        int totalItems = selectableCount + resourceTypeCount - 1;

        int rowCount = (((totalItems + buyColCount) / buyColCount) * buyColCount) / buyColCount;
        const UI::ElementDescription* buyTemplate = TradeUI_GetTableTemplate(_tableBuy);
        _tableBuy->SetSize(rowCount, buyColCount);

        TradeUI_CloneTableCells(this, _tableBuy, buyTemplate);
    }

    // Iterate tableBuy cells, find "ribbonBuyRow" sub-elements (4 children per row)
    int buyCellCount = _tableBuy->_rowCount;
    for (int i = 0; i < buyCellCount; i++) {
        System::String strBuf("ribbonBuyRow");
        UI::Element* cell = _tableBuy->Child(i).Get();
        UI::Element* rowElem = cell->FindElement(strBuf).Get();

        // child(0)=quantity number, child(1)=LabelElement, child(2)=price number, child(3)=SpinnerElement
        UI::NumberElement* spinElem = static_cast<UI::NumberElement*>(rowElem->Child(0).Get());
        UI::LabelElement* labelElem = static_cast<UI::LabelElement*>(rowElem->Child(1).Get());
        UI::NumberElement* numElem = static_cast<UI::NumberElement*>(rowElem->Child(2).Get());
        UI::SpinnerElement* sliderElem = static_cast<UI::SpinnerElement*>(rowElem->Child(3).Get());

        // child(3) is a SpinnerElement (same mislabel as the sell loop).
        // Retail call site 0x14008653e, EDX = i + 0xfa0.
        sliderElem->SetElementId(i + 4000);

        // Build 0x20-byte entry {child1, child2, child3, child0} and push to the
        // buy-entries array at +0x70.
        _buyRows.SetSize(_buyRows.GetSize() + 1,
                         BuyRow{labelElem, numElem, sliderElem, spinElem});
    }

    // --- Phase 4d: Setup tableOrder ---
    uint32_t orderColCount = static_cast<uint32_t>(_tableOrder->_columnCount);

    {
        int totalOrderItems = selectableCount + orderColCount + resourceTypeCount - 1;

        int rowCount = ((int)(totalOrderItems / (int)orderColCount * (int)orderColCount))
                       / (int)orderColCount;
        const UI::ElementDescription* orderTemplate = TradeUI_GetTableTemplate(_tableOrder);
        _tableOrder->SetSize(rowCount, (int)orderColCount);

        TradeUI_CloneTableCells(this, _tableOrder, orderTemplate);
    }

    // Iterate tableOrder cells, set element IDs (0x1F40+i) on the purchase check cells
    int orderCellCount = _tableOrder->ChildCount();
    for (int i = 0; i < orderCellCount; i++) {
        UI::Element* typed = _tableOrder->Child(i).Get();
        typed->SetElementId(i + 0x1F40);

        // Push 8-byte entry (just the element pointer) into the purchase array
        // at +0xA0.
        _purchaseRows.SetSize(_purchaseRows.GetSize() + 1,
                              PurchaseRow{static_cast<UI::CheckElement*>(typed)});
    }

    // --- Phase 5: Call refresh functions ---
    RefreshDisplay();
    RefreshSellTable();
    RefreshBuyTable();

    // EXT SEAM (NOT RETAIL, docs/extensions/handins/TRADERREQUEST.md): ext.trader_request (class C), the
    // request row under the Orders tab's order combo
    Ext::TraderRequest::PanelOpened(this);
}

// ============================================================
// OnUpdateLayout (vtable +0x38): Trade value calculations
// ============================================================
// Original: 0x140082150
//
// Calculates total sell/buy values from order arrays and resource values.
// Checks storage capacity, shows/hides warning text (textNoStorage,
// textTooMuch, textNotEnough). Enables/disables trade and dismiss buttons
// based on trade state and merchant presence.
//
// sell orders: self+0x30 = ptr, self+0x38 = count, stride 0x20
//   entry[i]: +0x00 = resource entity ptr, +0x18 = quantity int
// buy orders: self+0x40 = ptr, self+0x48 = count, stride 0x18
//   entry[j]: +0x00 = resource entity ptr, +0x10 = quantity int
// resource entity: +0x28 = component array ptr, +0x30 = component count
//   component value at +0x48 (identified by vtable+0x10 returning TypeId_RawMaterialDescription)
// merchant state check: BuildState component, byte at +0x52 (or +0x50 if negative)
//   0x13 = BuildingState::MerchantDocked (enables dismiss button)
// storage capacity: StorageComponent+0x38 vs StorageComponent_QueryCount
//   bStorageFull = capacity < storageCount + (buyTotal - sellTotal)

// Find resource value for a given resource entity ptr by searching its component array
// Returns 0 if not found. Resource type identified via vtable+0x10 == TypeId_RawMaterialDescription.
static int FindResourceValue(const ComponentDescription* resourceEntity) {
    if (!resourceEntity) return 0;
    int compCount = resourceEntity->_subDescCount;
    for (int k = 0; k < compCount; k++) {
        ::Object::Base* comp = resourceEntity->_subDescs[k];
        if (!comp) continue;
        // Call vtable+0x10 to get type ID
        int typeId = comp->Type();
        if (typeId == (int)RawMaterialDescription::TypeId()) {
            // Verify with IsDerivedFrom, then return value at +0x48
            bool isRes = ::Object::IsDerivedFromNullSafe(comp, RawMaterialDescription::TypeId());
            if (isRes) return static_cast<RawMaterialDescription*>(comp)->_weight;
            break;
        }
    }
    return 0;
}

// [7] 0x140082150
void TradeUI::OnUpdateLayout() {
    if (!tradeui_sa::IsSetUp(this)) return;
    tradeui_sa::Trace("Slot7-values", this);

    // EXT SEAM (NOT RETAIL, docs/extensions/handins/TRADERREQUEST.md): ext.trader_request (class C), the
    // request row follows the post's request (an arrival uses a "next visit" request up)
    Ext::TraderRequest::PanelLayout(this);

    // Find the TradeComponent and StorageComponent
    ::Object::Base* source = _dataSource;
    ComponentEntity* entity = ::Object::IsDerivedFromNullSafe(source, ComponentEntity::TypeId())
        ? static_cast<ComponentEntity*>(source) : nullptr;
    TradeComponent* trade = entity->FindComponent<TradeComponent>();
    (void)trade;

    StorageComponent* storageComp = entity->FindComponent<StorageComponent>();

    // --- Compute sell total ---
    int sellTotal = 0;
    int sellCount = _sellItems._size;
    for (int i = 0; i < sellCount; i++) {
        int qty = _sellItems[i].quantity;
        if (qty != 0) {
            int resVal = FindResourceValue(_sellItems[i].resource);
            sellTotal += resVal * qty;
        }
    }

    // --- Compute buy total ---
    int buyTotal = 0;
    int buyCount = _buyItems._size;
    for (int j = 0; j < buyCount; j++) {
        int qty = _buyItems[j].quantity;
        if (qty != 0) {
            int resVal = FindResourceValue(_buyItems[j].resource);
            buyTotal += resVal * qty;
        }
    }

    // --- Read displayed totals from NumberElements ---
    int displaySellTotal = _numberSellTotal->_intValue;
    int displayBuyTotal  = _numberBuyTotal->_intValue;

    bool bOverBought  = displaySellTotal < displayBuyTotal;  // buy value > sell value
    bool bBuySell     = displayBuyTotal  < displaySellTotal; // buy < sell (textTooMuch case)

    // --- Storage capacity check ---
    int storageCapacity = storageComp->capacity;
    int storageCount = storageComp->QueryCount();
    bool bStorageFull = storageCapacity < storageCount + (buyTotal - sellTotal);

    // --- Show/hide warning texts ---
    if (bOverBought) {
        // Buy value exceeds sell value — show "not enough" text with difference
        System::String strBuf = System::String::Format("%d", (unsigned)(displayBuyTotal - displaySellTotal));
        _textNotEnough->SetParameter(0, strBuf);
        _textNoStorage->SetFlagsOnOff(UI::kElementFlagHidden, 0);  // hide textNoStorage
        _textTooMuch->SetFlagsOnOff(UI::kElementFlagHidden, 0);    // hide textTooMuch
        _textNotEnough->SetFlagsOnOff(0, UI::kElementFlagHidden);  // show textNotEnough
    } else if (bStorageFull) {
        // Storage overflow — show "no storage" text
        _textNoStorage->SetFlagsOnOff(0, UI::kElementFlagHidden);  // show textNoStorage
        _textTooMuch->SetFlagsOnOff(UI::kElementFlagHidden, 0);    // hide textTooMuch
        _textNotEnough->SetFlagsOnOff(UI::kElementFlagHidden, 0);  // hide textNotEnough
    } else if (!bBuySell) {
        // Totals are equal (no net trade) — hide all warning texts
        _textNoStorage->SetFlagsOnOff(UI::kElementFlagHidden, 0);  // hide textNoStorage
        _textTooMuch->SetFlagsOnOff(UI::kElementFlagHidden, 0);    // hide textTooMuch
        _textNotEnough->SetFlagsOnOff(UI::kElementFlagHidden, 0);  // hide textNotEnough
    } else {
        // Sell value exceeds buy value — show "too much" text with difference
        System::String strBuf = System::String::Format("%d", (unsigned)(displaySellTotal - displayBuyTotal));
        _textTooMuch->SetParameter(0, strBuf);
        _textNoStorage->SetFlagsOnOff(UI::kElementFlagHidden, 0);  // hide textNoStorage
        _textTooMuch->SetFlagsOnOff(0, UI::kElementFlagHidden);    // show textTooMuch
        _textNotEnough->SetFlagsOnOff(UI::kElementFlagHidden, 0);  // hide textNotEnough
    }

    // --- Find BuildState (merchant) component and check merchant arrival state ---
    StateMachineComponent* buildState = entity->FindComponent<StateMachineComponent>();

    // State byte at +0x52 (primary), +0x50 (fallback if negative)
    int8_t stateByte = buildState->stateMachine.pendingState;
    if (stateByte < 0) stateByte = buildState->stateMachine.currentInput;
    bool merchantArrived = (stateByte == StateByte(BuildingState::MerchantDocked));

    // --- Enable/disable buttonDismiss (enabled when merchant is present) ---
    _buttonDismiss->Enable(merchantArrived);

    // --- Enable/disable buttonTrade ---
    // Enabled only if: buyTotal.value > 0, no storage overflow, no over-bought
    bool tradeEnabled = (displayBuyTotal > 0) && !bStorageFull && !bOverBought;
    _buttonTrade->Enable(tradeEnabled);

    // --- Layout recalc for sell and buy tables ---
    _tableSell->SetNeedsSizeCalc();
    _tableBuy->SetNeedsSizeCalc();

    // --- Refresh table displays ---
    RefreshOrderQuantityDisplay();
    RefreshSellSpinnerValues();
    RefreshOrderDisplay();
}

// ============================================================
// OnUpdate (vtable +0x40): Refresh on order changes
// ============================================================
// Original: 0x1400827b0
//
// Finds the TradeComponent, checks if the buy order count (+0x48 on self)
// differs from the post's order count (+0x58). If changed,
// refreshes sell, buy, and order tables.

// [8] 0x1400827b0
void TradeUI::OnUpdate(const System::Time& /*time*/) {
    if (!tradeui_sa::IsSetUp(this)) return;

    // Get owning entity and find the TradeComponent
    ::Object::Base* source = _dataSource;
    ComponentEntity* entity = ::Object::IsDerivedFromNullSafe(source, ComponentEntity::TypeId())
        ? static_cast<ComponentEntity*>(source) : nullptr;
    TradeComponent* trade = entity->FindComponent<TradeComponent>();

    // Check if order count changed
    int selfOrderCount = _buyItems._size;
    int buildingOrderCount = trade->manifest._size;

    if (selfOrderCount != buildingOrderCount) {
        // Order count changed — refresh sell, buy, and order tables
        RefreshSellTable();
        RefreshBuyTable();
        RefreshDisplay();
    }

    // Always update totals
    UpdateSellTotals();
    UpdateBuyTotals();
}

// ============================================================
// OnElementEvent (vtable +0x48): Event dispatcher
// ============================================================
// Original: 0x140081f20
//
// Dispatches UI events based on type and element ID:
//   eventType 0x15: storage spinners (0x7d0+), sell spinners (0xbb8+),
//     buy spinners (0xfa0+), order amount spinners (0x1b58+)
//   eventType 0x12: buttonTrade (0x4e20), buttonDismiss (0x4e21),
//     orderUp spinners (0x1388+), orderDown spinners (0x1770+),
//     purchase buttons (0x1f40+)
//   eventType 0x14: comboPurchase (0x4e22), comboOrder (0x4e23), sortCombo (0xc7)
//
// Note: The three event type sections fall through to a shared check for
// eventType 0x14 IDs, so eventType 0x12 events can also trigger 0x14 handlers
// if no 0x12 match was found.


// [9] 0x140081f20
void TradeUI::OnElementEvent(Object::InternalPtr<UI::Element>* /*element*/, UI::EventId eventType, int32_t eventId,
                             int32_t /*c*/, int32_t /*d*/, int32_t /*e*/, void* /*userData*/) {
    if (!tradeui_sa::IsSetUp(this)) return;
    tradeui_sa::Trace("Slot9-event", this);

    // EXT SEAM (NOT RETAIL, docs/extensions/handins/TRADERREQUEST.md): ext.trader_request (class C), the
    // Orders tab's request row's two combos
    if (Ext::TraderRequest::PanelEvent(this, eventType, eventId)) return;


    // The id ladder: 0 = Activated (0x12), two on = SelectionChanged, one more = ValueChanged.
    int switchVal = static_cast<int32_t>(eventType) - static_cast<int32_t>(UI::EventId::Activated);
    if (switchVal == 0) {
        // --- eventType 0x12: click events ---
        if (eventId == 0x4e20) {
            // buttonTrade — the CHECKOUT, OnTradeButton (0x140081420) in
            // src/game/Dialog/tradeui_checkout.cpp.
            OnTradeButton();
            return;
        }
        if (eventId == 0x4e21) {
            OnDismissButton();
            return;
        }
        // orderUp spinners: id range [0x1388, 0x1388 + orderCount]
        {
            int orderCount = _orderRows._size;
            if (eventId >= 0x1388 && eventId <= orderCount + 0x1388) {
                OnOrderSpinner(eventId - 0x1388, eventId - 0x1389);
                goto check_type14;
            }
        }
        // orderDown spinners: id range [0x1770, 0x1770 + orderCount]
        {
            int orderCount = _orderRows._size;
            if (eventId >= 0x1770 && eventId <= orderCount + 0x1770) {
                // Note: second arg is idx+1 relative to base (idx-0x176f = idx-0x1770+1)
                OnOrderSpinner(eventId - 0x1770, eventId - 0x176f);
                goto check_type14;
            }
        }
        // purchase buttons: id range [0x1f40, 0x1f40 + purchaseCount]
        {
            int purchaseCount = _purchaseRows._size;
            if (eventId >= 0x1f40 && eventId <= purchaseCount + 0x1f40) {
                OnPurchaseButton(eventId - 0x1f40);
                goto check_type14;
            }
        }
        goto check_type14;
    }

    switchVal -= 2;
    if (switchVal == 0) {
        // --- eventType 0x14: combo/sort selection events ---
        // (handled below in check_type14)
        goto check_type14;
    }

    switchVal -= 1;
    if (switchVal != 0) {
        return;
    }

    // --- eventType 0x15: spinner change events ---
    // storage spinners: id range [0x7d0, 0x7d0 + storageCount)
    {
        int storageCount = _storageItems._size;
        if (eventId >= 0x7d0 && eventId < storageCount + 0x7d0) {
            OnStorageSpinner(eventId - 0x7d0);
            return;
        }
    }
    // sell spinners: id range [0xbb8, 0xbb8 + tableSell rows)
    if (eventId >= 0xbb8) {
        int sellChildCount = _tableSell->_rowCount;
        if (eventId < sellChildCount + 0xbb8) {
            OnSellSpinner(eventId - 0xbb8);
            return;
        }
    }
    // buy spinners: id range [0xfa0, 0xfa0 + tableBuy rows)
    if (eventId >= 0xfa0) {
        int buyChildCount = _tableBuy->_rowCount;
        if (eventId < buyChildCount + 0xfa0) {
            OnBuySpinner(eventId - 0xfa0);
            return;
        }
    }
    // order amount spinners: id range [0x1b58, 0x1b58 + orderCount)
    {
        int orderCount = _orderRows._size;
        if (eventId >= 0x1b58 && eventId < orderCount + 0x1b58) {
            OnOrderAmountSpinner(eventId - 0x1b58);
            return;
        }
    }
    return;

check_type14:
    // Shared check for combo/sort events (reached from type 0x12 and 0x14)
    if (eventId == 0x4e22) {
        OnComboPurchase();
        return;
    }
    if (eventId == 0x4e23) {
        OnComboOrder();
        return;
    }
    if (eventId == 0xc7) {
        RefreshDisplay();
    }
}

// ============================================================
// OnSerializeSettings (vtable +0x50): Serialize tab/sort state
// ============================================================
// Original: 0x1400868c0
//
// Finds "tradeTabs" TabElement, serializes active tab index and
// sortCombo selection via IO::Serializer. On load, restores tab
// and combo state, then refreshes the sorted display.

// [10] 0x1400868c0
void TradeUI::OnSerializeSettings(IO::Stream& stream) {
    if (!tradeui_sa::IsSetUp(this)) return;

    // Find "tradeTabs" TabElement
    UI::TabElement* tabElement = TradeUI_FindTypedElement<UI::TabElement>(_container, "tradeTabs");

    // Read current tab index from TabElement (+0xf8)
    int tabIndex = tabElement->_activeTab;

    // Read current sortCombo selection (+0x140 element, +0x118 = combo index)
    int sortIndex = _sortCombo->_currentItem;

    // Create serializer
    IO::Serializer serializer(stream, 1);

    // Only serialize if version > 0 (newer save format)
    int version = serializer.GetVersion();
    if (version > 0) {
        serializer.Serialize(sortIndex, nullptr);
        serializer.Serialize(tabIndex, nullptr);
    }

    // On load (mode == 0), restore tab and combo state
    if (serializer.GetDirection() == IO::kReading) {
        tabElement->SetActiveTab(tabIndex);

        _sortCombo->SetCurrentItem(sortIndex);

        // Refresh sorted display
        RefreshDisplay();
    }
}

// ============================================================
// UpdateBuyTotals (0x140082850): Sum buy order values into numberBuyTotal
// ============================================================
// Iterates all buy orders (self+0x40, stride 0x18). Each entry: +0x10 = quantity.
// Unit price: the TradeComponent's buy-order array (+0x50, stride 0x10: +0x00
// entity, +0x08 unit price, +0x0c quantity).
// Then sets numberBuyTotal (+0x108) to the accumulated total.

void TradeUI::UpdateBuyTotals() {
    ::Object::Base* source = _dataSource;
    ComponentEntity* entity = ::Object::IsDerivedFromNullSafe(source, ComponentEntity::TypeId())
        ? static_cast<ComponentEntity*>(source) : nullptr;
    TradeComponent* trade = entity->FindComponent<TradeComponent>();

    int total = 0;
    int buyCount = _buyItems._size;

    TradeManifestLine* resourceTypeBase = trade->manifest._data;
    for (int i = 0; i < buyCount; i++) {
        int qty = _buyItems[i].quantity;
        if (qty != 0) {
            // Price lives at entry+8 of the TradeComponent's buy-order array
            // (trade+0x50, stride 0x10: +0x00 entity, +0x08 unit price,
            //  +0x0c quantity — the same layout TradeComponent_RemoveBuyOrder
            //  edits in tradeui_checkout.cpp).
            //
            // The index register is PRE-incremented before the load, which the
            // decompiler renders as a bare "-8": at 0x1400828cd the induction
            // variable becomes (i+1)*0x10 and the load at 0x1400828d1 reads
            // [-8] off it, i.e. i*0x10 + 8 — NOT i*0x10 - 8. Transcribing the
            // "-8" literally read entry i-1's price and, at i == 0, read eight
            // bytes off the front of the array.
            int price = resourceTypeBase[i].price;
            total += price * qty;
        }
    }

    _numberBuyTotal->SetValue(static_cast<int32_t>(total));
}

// ============================================================
// UpdateSellTotals (0x140082920): Sum sell order values into numberSellTotal
// ============================================================
// Iterates all sell orders (self+0x30, stride 0x20). Each entry: +0x18 = quantity.
// Uses the TradeComponent price lookup (0x14005a520) for the unit value.
// Sets numberSellTotal (+0x100) to accumulated total.

void TradeUI::UpdateSellTotals() {
    ::Object::Base* source = _dataSource;
    ComponentEntity* entity = ::Object::IsDerivedFromNullSafe(source, ComponentEntity::TypeId())
        ? static_cast<ComponentEntity*>(source) : nullptr;
    TradeComponent* trade = entity->FindComponent<TradeComponent>();

    int total = 0;
    int sellCount = _sellItems._size;

    for (int i = 0; i < sellCount; i++) {
        int qty = _sellItems[i].quantity;
        if (qty != 0) {
            // 0x14005a520: the post's price for this resource
            int unitVal = trade->GetResourcePrice(_sellItems[i].resource);
            total += qty * unitVal;
        }
    }

    _numberSellTotal->SetValue(static_cast<int32_t>(total));
}

// ============================================================
// OnBuySpinner (0x1400819f0): Store buy spinner value into buy order entry
// ============================================================
void TradeUI::OnBuySpinner(int idx) {
    int val = _buyRows[idx].spinner->GetValue();

    _buyItems[idx].quantity = val;

    UpdateBuyTotals();
}

// ============================================================
// OnSellSpinner (0x140081a50): Store sell spinner value into sell order entry
// ============================================================
void TradeUI::OnSellSpinner(int idx) {
    int val = _sellRows[idx].spinner->GetValue();

    _sellItems[idx].quantity = val;

    UpdateSellTotals();
}

// ============================================================
// OnComboPurchase (0x140081bc0): Save comboPurchase selection to the post
// ============================================================
// Reads comboPurchase (+0x138) current item index (+0x118).
// Stores it to the TradeComponent's field at +0x28.

void TradeUI::OnComboPurchase() {
    ::Object::Base* source = _dataSource;
    ComponentEntity* entity = ::Object::IsDerivedFromNullSafe(source, ComponentEntity::TypeId())
        ? static_cast<ComponentEntity*>(source) : nullptr;
    TradeComponent* trade = entity->FindComponent<TradeComponent>();

    // Read combo selection
    uint32_t comboIdx = static_cast<uint32_t>(_comboPurchase->_currentItem);

    // Store to trade +0x28
    trade->tradeState = comboIdx;
}

// ============================================================
// OnComboOrder (0x140081e80): Save comboOrder selection to the post's order record
// ============================================================
// Reads comboOrder (+0xC0) current item index (+0x118).
// Stores to the post's order slot at: orders_ptr + (curOrderSlot * 0x20 + 8)
// where curOrderSlot = trade +0x2c.

void TradeUI::OnComboOrder() {
    ::Object::Base* source = _dataSource;
    ComponentEntity* entity = ::Object::IsDerivedFromNullSafe(source, ComponentEntity::TypeId())
        ? static_cast<ComponentEntity*>(source) : nullptr;
    TradeComponent* trade = entity->FindComponent<TradeComponent>();

    // Read comboOrder selection
    uint32_t comboIdx = static_cast<uint32_t>(_comboOrder->_currentItem);

    // Store to the post's current order slot +8
    int curSlot = trade->tradeId;
    TradeMerchantOrder* ordersBase = trade->merchantOrders._data;
    ordersBase[static_cast<uint32_t>(curSlot)].orderKind = comboIdx;   // the index is zero-extended (as the tree's)
}

// ============================================================
// OnOrderAmountSpinner (0x140081c50): Save order amount to the post's order table
// ============================================================
// Reads order amount spinner from orderEntries (+0x90, stride 0x20, spinner at +0x18).
// Stores to the post's order amount at: trade.orders (+0x40) + idx*0xc + 8.

void TradeUI::OnOrderAmountSpinner(int idx) {
    ::Object::Base* source = _dataSource;
    ComponentEntity* entity = ::Object::IsDerivedFromNullSafe(source, ComponentEntity::TypeId())
        ? static_cast<ComponentEntity*>(source) : nullptr;
    TradeComponent* trade = entity->FindComponent<TradeComponent>();

    // Read spinner value from orderEntries[idx].spinner
    int val = _orderRows[idx].spinner->GetValue();

    // Store to the post's order amount table: trade.ordersAmt (+0x40) + idx*0xc + 8
    TradeBuyOrder* ordersAmt = trade->buyOrders._data;
    ordersAmt[idx].amount = val;
}

// ============================================================
// OnOrderSpinner (0x140081d00): Swap two order entries in the post's order table
// ============================================================
// Swaps entries at indices param_2 and param_3 in the post's 0xc-stride order array (+0x40).
// Used when up/down order spinner buttons are clicked.

void TradeUI::OnOrderSpinner(int idx, int idx2) {
    ::Object::Base* source = _dataSource;
    ComponentEntity* entity = ::Object::IsDerivedFromNullSafe(source, ComponentEntity::TypeId())
        ? static_cast<ComponentEntity*>(source) : nullptr;
    TradeComponent* trade = entity->FindComponent<TradeComponent>();

    TradeBuyOrder* ordersBase = trade->buyOrders._data;

    // Swap 12-byte entries at idx and idx2 (stride 0xc)
    TradeBuyOrder* a = ordersBase + idx;
    TradeBuyOrder* b = ordersBase + idx2;

    uint32_t a0 = a->rawMaterialFlags;
    int32_t  a1 = a->categoryIndex;
    int32_t  a2 = a->amount;

    a->rawMaterialFlags = b->rawMaterialFlags;
    a->categoryIndex    = b->categoryIndex;
    a->amount           = b->amount;

    b->rawMaterialFlags = a0;
    b->categoryIndex    = a1;
    b->amount           = a2;
}

// ============================================================
// OnDismissButton (0x140081950): Dismiss the merchant
// ============================================================
// Finds the state machine on the owning entity. Reads merchant
// state byte from +0x51 (sign-extended). Posts event 10 to the controller
// node at buildState+0x48 to trigger dismissal, then refreshes
// display and both sell/buy tables.
//
// Retail's register setup is the PostEvent call's, not RefreshDisplay's
// (RefreshDisplay 0x1400846b0 reads only RCX):
//   RCX = *(buildState+0x48)           — the controller node (`this`)
//   RDX = buildState + 0x48            — the state machine
//   R8D = MOVSX byte [buildState+0x51] — the state index
//   R9D = 10                           — the event id
//   CALL [RAX+0x10]                    — Controller slot 2, PostEvent

void TradeUI::OnDismissButton() {
    ::Object::Base* source = _dataSource;
    ComponentEntity* entity = ::Object::IsDerivedFromNullSafe(source, ComponentEntity::TypeId())
        ? static_cast<ComponentEntity*>(source) : nullptr;

    // Find the state machine (FUN_140003ea0)
    StateMachineComponent* buildState = entity->FindComponent<StateMachineComponent>();

    // Read merchant state byte from +0x51 (sign-extended, no fallback)
    int8_t stateByte = buildState->stateMachine.activeState;

    // Post event 10 (MerchantDismissed) to the current controller node:
    // Controller slot 2, PostEvent(stateMachine, stateIndex, eventId), with the
    // state machine at +0x48 and the state byte at +0x51 (0x1400819a9..0x1400819c2).
    ControllerComponent::Controller* node = buildState->stateMachine.current;
    node->PostEvent(&buildState->stateMachine, stateByte, SmId(BuildingEvent::MerchantDismissed));

    // Refresh display and tables
    RefreshDisplay();
    RefreshSellTable();
    RefreshBuyTable();
}

// ============================================================
// OnPurchaseButton (0x140081dd0): Toggle a purchase order entry
// ============================================================
// Reads the checked state of the purchase button at index param_2 (+0xd8 = checked byte).
// Gets the corresponding resource entity from self's purchaseReserved (+0xb0, stride 8).
// Calls the post's AddPurchaseOrder(resource, checkedState).

void TradeUI::OnPurchaseButton(int idx) {
    ::Object::Base* source = _dataSource;
    ComponentEntity* entity = ::Object::IsDerivedFromNullSafe(source, ComponentEntity::TypeId())
        ? static_cast<ComponentEntity*>(source) : nullptr;
    TradeComponent* trade = entity->FindComponent<TradeComponent>();

    // Read checked state byte at +0xd8
    int8_t checked = static_cast<int8_t>(_purchaseRows[idx].check->_checked);

    // Get corresponding resource entity from purchaseReserved (+0xb0, stride 8)
    ComponentDescription* resourceEntity = _purchaseItems[idx].resource;

    // 0x140059490
    trade->AddPurchaseOrder(resourceEntity, checked);
}

// ============================================================
// OnStorageSpinner (0x140081ab0): Update storage resource assignment
// ============================================================
// Reads the new storage spinner value for resource at idx.
// Looks up resource entity from self's storage items (+0x50, stride 0x18).
// Searches the post's storage BST (+0x60) for the resource.
// If found: update quantity. If not found: insert new entry.
// Then refreshes sell table and display.

void TradeUI::OnStorageSpinner(int idx) {
    // storageEntries at self+0x80, stride 0x18, spinner at +0x08
    int newVal = _storageRows[idx].spinner->GetValue();

    ::Object::Base* source = _dataSource;
    ComponentEntity* entity = ::Object::IsDerivedFromNullSafe(source, ComponentEntity::TypeId())
        ? static_cast<ComponentEntity*>(source) : nullptr;
    TradeComponent* trade = entity->FindComponent<TradeComponent>();

    // Get resource entity ptr from the storage items (+0x50, stride 0x18, first field)
    ComponentDescription* resourceEntity = _storageItems[idx].resource;

    // Search BST for existing entry and update or insert
    // The insert/update logic (FUN_14000e1c0) handles both cases:
    // If resource already in BST: update quantity
    // If not found: insert new leaf
    TradeStockEntry newEntry[1] = { { resourceEntity, newVal, { 0, 0, 0, 0 } } };
    TradeResourceNode* retval = nullptr;
    TradeUI_StorageTree_Insert_e1c0(&trade->stock, &retval, newEntry);   // 0x14000e1c0

    // If we got a result back (found existing), update its qty
    if (retval) {
        retval->quantity = newVal;
    }


    RefreshSellTable();
    RefreshDisplay();
}

// ============================================================
// RefreshOrderQuantityDisplay (0x140082fb0): Update storage count numbers
// ============================================================
// For each storage entry (self+0x50, stride 0x18):
//   - Gets resource entity from the storage item
//   - Searches StorageComponent's list (at +0x28/+0x30) for matching entity
//   - Sets NumberElement value (storage row's number at +0x10) to found qty

void TradeUI::RefreshOrderQuantityDisplay() {
    ::Object::Base* source = _dataSource;
    ComponentEntity* entity = ::Object::IsDerivedFromNullSafe(source, ComponentEntity::TypeId())
        ? static_cast<ComponentEntity*>(source) : nullptr;
    TradeComponent* trade = entity->FindComponent<TradeComponent>();
    (void)trade;

    // Check the post's base type — used later for sprite/text lookups
    // IsDerivedFrom call on the post's type desc at +0x18 — just a type check, result unused here
    // ::Object::IsDerivedFromNullSafe(trade->_description, DAT_1401c4bb8);

    StorageComponent* storageComp = entity->FindComponent<StorageComponent>();

    int storageCount = _storageItems._size;

    if (storageCount > 0) {
        for (int i = 0; i < storageCount; i++) {
            ComponentDescription* resourceEntity = _storageItems[i].resource;   // re-read each iteration like original

            // Search StorageComponent's item list for this resource entity
            // List entries: pairs [resource_entity_ptr, qty] each 0x10 bytes (2 x 8-byte)
            int listCount = storageComp->storageSlots._size;
            StorageComponent::Item* itemList = storageComp->storageSlots._data;

            int foundQty = 0;
            for (int64_t j = 0; j < listCount; j++) {
                if (itemList[j]._resource == resourceEntity) {
                    if (j >= 0) {
                        foundQty = itemList[j]._count;
                    }
                    break;
                }
            }

            // Set NumberElement value
            _storageRows[i].count->SetValue(static_cast<int32_t>(foundQty));
        }
    }
}

// ============================================================
// RefreshSellSpinnerValues (0x140082d80): Update sell spinners with available amounts
// ============================================================
// For each sell order entry (self+0x30, stride 0x20):
//   - Gets the post's price for the resource (0x14005a520)
//   - Searches StorageComponent's list for current stored qty
//   - Sets NumberElement (+0x08) to storedQty
//   - Sets SpinnerElement (+0x18) min=0, max=storedQty (or 0 if no merchant / price<=0)
//   - Sets spinner increment (1 if max<=0x14, 5 if max<=0x32, 20 if max<=0x64, skip if >0x64)
// Also checks merchant state: if not arrived, force max=0 (spinners disabled).

void TradeUI::RefreshSellSpinnerValues() {
    ::Object::Base* source = _dataSource;
    ComponentEntity* entity = ::Object::IsDerivedFromNullSafe(source, ComponentEntity::TypeId())
        ? static_cast<ComponentEntity*>(source) : nullptr;
    TradeComponent* trade = entity->FindComponent<TradeComponent>();

    // Type check on the post's class info at +0x18 (unused result, but done in original)
    // ::Object::IsDerivedFromNullSafe(trade->_description, DAT_1401c4bb8);

    StorageComponent* storageComp = entity->FindComponent<StorageComponent>();

    int sellCount = _sellItems._size;

    // Check merchant state: find the state machine
    StateMachineComponent* bsComp = entity->FindComponent<StateMachineComponent>();

    // Byte at +0x52 (fallback +0x50 if negative) compared to 0x13 (BuildingState::MerchantDocked).
    int8_t stateByte = bsComp->stateMachine.pendingState;
    if (stateByte < 0) stateByte = bsComp->stateMachine.currentInput;
    bool merchantArrived = (stateByte == StateByte(BuildingState::MerchantDocked));

    if (sellCount > 0) {
        for (int i = 0; i < sellCount; i++) {
            SellRow& entryBase = _sellRows[i];   // re-read each iteration
            ComponentDescription* resourceEntity = _sellItems[i].resource;

            // Get the post's price for this resource
            int available = trade->GetResourcePrice(resourceEntity);

            // Search StorageComponent's item list for current stored qty
            int listCount = storageComp->storageSlots._size;
            StorageComponent::Item* itemList = storageComp->storageSlots._data;
            int storedQty = 0;
            for (int64_t j = 0; j < listCount; j++) {
                if (itemList[j]._resource == resourceEntity) {
                    if (j >= 0) {
                        storedQty = itemList[j]._count;
                    }
                    break;
                }
            }

            // Set NumberElement (+0x08) to storedQty
            entryBase.stored->SetValue(static_cast<int32_t>(storedQty));

            // Determine spinner max: storedQty if merchant arrived AND available > 0, else 0
            int spinnerMax = storedQty;
            if (!merchantArrived || available < 1) {
                spinnerMax = 0;
            }

            // sellUI+0x18 is a SpinnerElement (exe slot 0x140151cf8 = SpinnerElement::SetMinMax)
            entryBase.spinner->SetMinMax(0, spinnerMax);

            // Re-read spinner ptr for GetMaxValue (matches original pattern)
            int actualMax = entryBase.spinner->GetMaxValue();

            // Set increment based on max value; skip SetIncrement entirely if max > 0x64
            if (actualMax <= 0x14) {
                entryBase.spinner->SetIncrement(1);
            } else if (actualMax <= 0x32) {
                entryBase.spinner->SetIncrement(5);
            } else if (actualMax <= 0x64) {
                entryBase.spinner->SetIncrement(20);
            }
            // else: max > 0x64 (100), do NOT call SetIncrement (original skips it)
        }
    }
}

// ============================================================
// RefreshOrderDisplay (0x140082b20): Update order table UI rows
// ============================================================
// For each active order in trade.orders (+0x40, count at trade+0x48, stride 0xc):
//   entry +0 = type index, entry +4 = resource type index, entry +8 = amount
// Gets sprite from the TradeDescription sprite sheet and sets it on LabelElement.
// Gets name text from the TradeDescription string table and sets it on the label.
// Sets SpinnerElement value (order amount).
// Also handles enable/disable on the first two elements differently.

void TradeUI::RefreshOrderDisplay() {
    ::Object::Base* source = _dataSource;
    ComponentEntity* entity = ::Object::IsDerivedFromNullSafe(source, ComponentEntity::TypeId())
        ? static_cast<ComponentEntity*>(source) : nullptr;
    TradeComponent* trade = entity->FindComponent<TradeComponent>();

    int iVar7 = 0;

    int orderCount = trade->buyOrders._size;
    if (orderCount <= 0) return;

    for (int64_t idx = 0; idx < orderCount; idx++) {
        // UI entry array at self+0x90, stride 0x20
        OrderRow& uiEntry = _orderRows[(int)idx];

        // Read order amount from the post's orders array (stride 0xc)
        TradeBuyOrder* ordersBase = trade->buyOrders._data;
        int amount = ordersBase[idx].amount;

        // Set spinner value (uiEntry[3] = offset +0x18)
        uiEntry.spinner->SetValue(amount);

        // Get the description and typeIdx for sprite/string lookups
        TradeBuyOrder* ordersBase2 = trade->buyOrders._data;
        int typeIdx = ordersBase2[idx].categoryIndex;

        // IsDerivedFrom check for TradeDescription — original does this each iteration
        Description* description = trade->_description;
        bool isTradeDesc = ::Object::IsDerivedFromNullSafe(description, TradeDescription::TypeId());
        TradeDescription* tradeDesc = isTradeDesc
            ? static_cast<TradeDescription*>(description) : nullptr;

        // Sprite name is at pairs + (typeIdx*3 + 1)*8 (the 0x18-stride pair array)
        TradeGoodsCategory* entriesBase = tradeDesc->_pairs;
        const System::String* spriteName = &entriesBase[typeIdx].spriteName;
        UI::Sprite spriteTemp(::Object::ExternalPtr<UI::SpriteSheet>(tradeDesc->_resourceSpriteSheet), *spriteName);

        // Copy constructed sprite to local for SetSprite (matches MOVAPS/MOVDQA in original)
        UI::Sprite spriteLocal = spriteTemp;

        uiEntry.label->SetSprite(&spriteLocal);

        // Get the description and typeIdx AGAIN for string lookup (original re-reads)
        TradeBuyOrder* ordersBase3 = trade->buyOrders._data;
        typeIdx = ordersBase3[idx].categoryIndex;

        description = trade->_description;
        isTradeDesc = ::Object::IsDerivedFromNullSafe(description, TradeDescription::TypeId());
        tradeDesc = isTradeDesc
            ? static_cast<TradeDescription*>(description) : nullptr;

        // String name is at pairs + typeIdx*0x18 + 0x10
        entriesBase = tradeDesc->_pairs;
        const System::String* stringName = &entriesBase[typeIdx].textName;
        UI::String stringTemp(::Object::ExternalPtr<UI::StringTable>(tradeDesc->_resourceStringTable), *stringName);

        // Copy constructed string to local for SetText (matches MOVAPS/MOVDQA in original)
        UI::String stringLocal = stringTemp;

        uiEntry.label->SetText(&stringLocal);

        // Enable/disable logic:
        // i==0: disable uiEntry[0] (first up/down button)
        // i==orderCount-1: disable uiEntry[1] (second up/down button)
        if (idx == 0) {
            uiEntry.upButton->Enable(false);
        } else {
            if (iVar7 == trade->buyOrders._size - 1) {
                uiEntry.downButton->Enable(false);
            }
        }

        iVar7++;
    }
}

// ============================================================
// RefreshDisplay (0x1400846b0): Rebuild sorted storage table display
// ============================================================
// Original: 0x1400846b0 (336 lines). Takes only `this` (RDX/R8/R9 are never read).
//
// Iterates the global raw-material list (game world +0xF0), finds
// ResourceType and ValueDescription sub-objects on each entity,
// builds a display array (stride 0x18: {entity, resourceComp, valueDescComp}).
//
// Finds TradeComponent and StorageComponent on the owning post entity.
// Sorts entries by name (sort combo == 0) or by quantity (sort combo != 0).
//
// For each entry: sets sprite icon + name text on label element, looks up
// inventory count from StorageComponent's item array, looks up order amount
// from TradeComponent's BST, sets NumberElement and SpinnerElement values.
// Clears remaining UI rows beyond the data.

void TradeUI::RefreshDisplay() {
    // Get the raw-material list from game world singleton (+0xF0)
    CitizenBehavior* tradeList = GameUpdateLoop::_singleton->m_citizenList;
    int entityCount = tradeList->knownRawMaterials._size;

    // Clear display entries array at self+0x50
    _storageItems.Clear();   // 0x140016200

    // Build display entries: for each entity, find ResourceType + ValueDescription
    for (int i = 0; i < entityCount; i++) {
        ComponentDescription* entity = tradeList->knownRawMaterials._data[i];

        RawMaterialDescription* resourceComp = nullptr;
        {
            int compCount = entity ? entity->_subDescCount : 0;
            for (int c = 0; c < compCount; c++) {
                ::Object::Base* comp = entity->_subDescs[c];
                if (!comp) continue;
                if ((uint32_t)comp->Type() == RawMaterialDescription::TypeId()) {
                    if (::Object::IsDerivedFromNullSafe(comp, RawMaterialDescription::TypeId()))
                        resourceComp = static_cast<RawMaterialDescription*>(comp);
                    break;
                }
            }
        }
        ValueDescription* valueComp = nullptr;
        {
            int compCount = entity ? entity->_subDescCount : 0;
            for (int c = 0; c < compCount; c++) {
                ::Object::Base* comp = entity->_subDescs[c];
                if (!comp) continue;
                if ((uint32_t)comp->Type() == ValueDescription::TypeId()) {
                    if (::Object::IsDerivedFromNullSafe(comp, ValueDescription::TypeId()))
                        valueComp = static_cast<ValueDescription*>(comp);
                    break;
                }
            }
        }

        // Push 0x18-byte entry {entity, resourceComp, valueComp} into the display
        // array at +0x50.
        _storageItems.SetSize(_storageItems.GetSize() + 1,
                              StorageItem{entity, resourceComp, valueComp});
    }

    // Get owning entity -> TradeComponent + StorageComponent
    ::Object::Base* source = _dataSource;
    ComponentEntity* ownerEntity = ::Object::IsDerivedFromNullSafe(source, ComponentEntity::TypeId())
        ? static_cast<ComponentEntity*>(source) : nullptr;

    // Find TradeComponent (cell 0x1401c4b80)
    TradeComponent* trade = ownerEntity->FindComponent<TradeComponent>();

    // Validate TradeDescription derivation (result unused — validation only)
    ::Object::IsDerivedFromNullSafe(trade->_description, TradeDescription::TypeId());

    // Find StorageComponent
    StorageComponent* storage = ownerEntity->FindComponent<StorageComponent>();

    // Sort entries based on sort combo selection (+0x140 = sortCombo element)
    int entryCount = _storageItems._size;
    int sortMode = _sortCombo->_currentItem;

    if (sortMode == 0) {
        // Sort by name
        if (entryCount > 1) {
            TradeUI_IntroSort_ByName_162d0(_storageItems._data, entryCount);
        }
    } else {
        // Sort by quantity (uses TradeComponent as context)
        TradeComponent* sortCtx = trade;
        TradeUI_IntroSort_ByComponent_165d0(_storageItems._data, entryCount, &sortCtx);
    }

    // Re-read count (may have been modified)
    entryCount = _storageItems._size;

    // --- Display loop: populate UI rows with data ---
    for (int i = 0; i < entryCount; i++) {
        StorageItem& entry = _storageItems[i];
        StorageRow& uiRow = _storageRows[i];

        ValueDescription* valueDesc = entry.value;  // ValueDescription component

        // Set sprite icon from ValueDescription (+0x20=spriteSheet, +0x28=spriteName)
        UI::Sprite spriteTmp(::Object::ExternalPtr<UI::SpriteSheet>(valueDesc->_sprite), valueDesc->_name);
        UI::Sprite spriteVal = spriteTmp;
        uiRow.label->SetSprite(&spriteVal);

        // Set text name from ValueDescription (+0x30=stringTable, +0x38=stringName)
        // Original uses fn_UIString_Ctor — TextElement::SetText expects UI::String, not System::String
        UI::String strBuf(::Object::ExternalPtr<UI::StringTable>(valueDesc->_stringTable), valueDesc->_stringName);
        UI::String strVal = strBuf;
        uiRow.label->SetText(&strVal);

        // Lookup inventory count from StorageComponent item array (+0x28/+0x30)
        // Each item is 0x10 bytes: {key(uint64), countA(int32), countB(int32)}
        // Result = countA - countB (available = total - committed)
        int invCount = 0;
        int scItemCount = storage->storageSlots._size;
        StorageComponent::Item* scItems = storage->storageSlots._data;
        for (int j = 0; j < scItemCount; j++) {
            StorageComponent::Item* itemAddr = scItems + j;
            if (itemAddr->_resource == entry.resource) {
                if (j >= 0) {
                    invCount = itemAddr->_count - itemAddr->_reserve;
                }
                break;
            }
        }

        // Set NumberElement value (at uiRow + 0x10)
        uiRow.count->SetValue(static_cast<int32_t>(invCount));

        // Lookup trade order amount from TradeComponent BST (tree at +0x60)
        // Node: {key(+0), value(+8), color(+10), left(+18), right(+20)}
        int orderAmt = 0;
        const ComponentDescription* key = entry.resource;
        TradeResourceNode* bstNode = trade->stock.root;
        while (bstNode != nullptr) {
            if (key < bstNode->resource) {
                bstNode = bstNode->left;   // left child at +0x18
            } else if (key > bstNode->resource) {
                bstNode = bstNode->right;  // right child at +0x20
            } else {
                orderAmt = bstNode->quantity;  // value at +0x08
                break;
            }
        }

        // Set SpinnerElement value (at uiRow + 0x08)
        uiRow.spinner->SetValue(orderAmt);

        // Enable spinner and number elements via vtable[8] (+0x40) = SetEnabled
        uiRow.spinner->Enable(true);
        uiRow.count->Enable(true);

        // spinSlot is storage entry[1] = SpinnerElement (confirmed via vtable at runtime)
        uiRow.spinner->SetMinMax(0, 9999);

        // Set spinner element name to resource path filename.
        // `GetPath` returns by value through a hidden return slot and the callee
        // CONSTRUCTS into it; copy-initialising the local is the same store.
        System::Filename pathBuf = entry.resource->GetPath();
        System::Filename filenameBuf = pathBuf.GetPathFilename(false);
        uiRow.spinner->SetName(filenameBuf.AsString());
    }

    // --- Clear remaining UI rows beyond data ---
    int uiRowCount = _storageRows._size;
    for (int i = entryCount; i < uiRowCount; i++) {
        StorageRow& uiRow = _storageRows[i];

        // Set blank sprite
        UI::Sprite blankSprite;
        UI::Sprite spriteVal = blankSprite;
        uiRow.label->SetSprite(&spriteVal);

        // Set blank text — must use UI::String default ctor, not System::String
        UI::String blankStr;
        UI::String strVal = blankStr;
        uiRow.label->SetText(&strVal);

        // Disable spinner (at +8) via vtable SetEnabled
        uiRow.spinner->Enable(false);

        // Disable number (at +0x10) via vtable SetEnabled
        uiRow.count->Enable(false);

        // spinSlot is storage entry[1] = SpinnerElement (confirmed via vtable at runtime)
        uiRow.spinner->SetMinMax(0, 0);
    }
}

// ============================================================
// NOTE: this block used to defer three functions as "needs opus". All three have
// since landed, so the list is kept only to say where they went — a deferral list
// that outlives the deferral is exactly the kind of comment this project keeps
// getting bitten by.
//
//   fn_TradeUI_RefreshSellTable (0x140083c70) -> TradeUI::RefreshSellTable, above
//   fn_TradeUI_RefreshBuyTable  (0x140083100) -> TradeUI::RefreshBuyTable, above
//   fn_TradeUI_OnTradeButton    (0x140081420) -> src/game/Dialog/tradeui_checkout.cpp
//
// The old description of the checkout said it "destroys/removes/spawns entities".
// That was read off two Ghidra mislabels and is wrong in the way that matters:
// 0x140091e60 and 0x140091a20 are the town AVAILABILITY-POOL registrars, not
// entity teardown. See the header block of tradeui_checkout.cpp.
// ============================================================
