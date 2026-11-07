#pragma once
// ============================================================================
// src/game/Dialog/TradeUI.h — ENDGAME B.3.8 block PNL (B38_RULES D1-D5)
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §6): reflected "TradeUI", parent
//   "UI::DialogController", table 0x14015de80, 11 slots, sizeof 0x148.
//   create 0x140015f80: MemAlloc(0x148, 8), ctor 0x140086b80 (??0DialogController,
//   stamp, a qword zero at every offset +0x30..+0x140).
//   [0] 0x140016020 scalar deleting dtor (~TradeUI 0x140086a50 + MemFree on &1)
//   [1] TypeName  [2] Type  (DECLARE_EXE_TYPE)
//   [4] 0x140084dd0  OnCreated
//   [5] 0x140082a70  OnOpened
//   [6] 0x140082a00  OnClosed
//   [7] 0x140082150  OnUpdateLayout
//   [8] 0x1400827b0  OnUpdate
//   [9] 0x140081f20  OnElementEvent
//   [10] 0x1400868c0  OnSerializeSettings
//   every other slot is UI::DialogController's (inherited, not declared).
//
// ~TradeUI 0x140086a50: frees the ten Collection::Array members at +0x128, +0xb0,
//   +0xa0, +0x90, +0x80, +0x70, +0x60, +0x50, +0x40, +0x30 (MemFree the data, zero
//   both qwords) and chains ~DialogController. The arrays below are the real
//   template, so the compiler emits exactly that order (members in reverse).
//   Their elements are trivially destructible (retail destroys none).
//
// The element pointers are retail's InternalPtr<X> (8 bytes, no refcount),
// spelled as the typed raw pointer (B4 H4). Each is the FindElement result of
// the resource element whose NAME the comment gives (OnCreated 0x140084dd0).
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "ui/DialogController.h"
#include "collection/Collection.h"
#include "Object/exe_type.h"

class ComponentDescription;
class ValueDescription;
class RawMaterialDescription;
class ComponentEntity;
class TradeComponent;
namespace UI {
class ButtonElement;
class CheckElement;
class ComboElement;
class Element;
class LabelElement;
class NumberElement;
class SpinnerElement;
class TableElement;
class TextElement;
}

class TradeUI final : public UI::DialogController {
    DECLARE_EXE_TYPE(TradeUI)

public:
    // ---- the rows of the ten arrays ----------------------------------------
    // Every element type zeroes in its default ctor, as the monomorphised exe
    // array helpers do for a stride-8-aligned element.

    // +0x30: one per raw material the post has in its storage tree
    // (RefreshSellTable 0x140083c70), stride 0x20.
    struct SellItem {
        ComponentDescription*   resource    = nullptr;   // +0x00
        ValueDescription*       value       = nullptr;   // +0x08
        RawMaterialDescription* rawMaterial = nullptr;   // +0x10
        int32_t                 quantity    = 0;         // +0x18 the sell spinner (OnSellSpinner 0x140081a50)
        int32_t                 _pad1c      = 0;         // +0x1c
    };
    // +0x40: one per merchant buy order (RefreshBuyTable 0x140083100), stride 0x18.
    struct BuyItem {
        ComponentDescription* resource = nullptr;        // +0x00
        ValueDescription*     value    = nullptr;        // +0x08
        int32_t               quantity = 0;              // +0x10 the buy spinner (OnBuySpinner 0x1400819f0)
        int32_t               _pad14   = 0;              // +0x14
    };
    // +0x50: one per raw material the town knows (RefreshDisplay 0x1400846b0),
    // stride 0x18; the introsorts (0x1400162d0 / 0x1400165d0) reorder it.
    struct StorageItem {
        ComponentDescription*   resource    = nullptr;   // +0x00
        RawMaterialDescription* rawMaterial = nullptr;   // +0x08
        ValueDescription*       value       = nullptr;   // +0x10
    };
    // +0x60: the "ribbonSellRow" children (OnCreated), stride 0x20.
    struct SellRow {
        UI::LabelElement*   label     = nullptr;         // +0x00 child(1)
        UI::NumberElement*  stored    = nullptr;         // +0x08 child(0): the post's stock
        UI::NumberElement*  price     = nullptr;         // +0x10 child(2): TradeComponent price 0x14005a520
        UI::SpinnerElement* spinner   = nullptr;         // +0x18 child(3), id 3000 + i
    };
    // +0x70: the "ribbonBuyRow" children (OnCreated), stride 0x20.
    struct BuyRow {
        UI::LabelElement*   label    = nullptr;          // +0x00 child(1)
        UI::NumberElement*  price    = nullptr;          // +0x08 child(2): the order's unit price
        UI::SpinnerElement* spinner  = nullptr;          // +0x10 child(3), id 4000 + i
        UI::NumberElement*  quantity = nullptr;          // +0x18 child(0): the order's quantity
    };
    // +0x80: the "ribbonStorageRow" children (OnCreated), stride 0x18.
    struct StorageRow {
        UI::LabelElement*   label   = nullptr;           // +0x00 child(0)
        UI::SpinnerElement* spinner = nullptr;           // +0x08 child(1), id 2000 + i
        UI::NumberElement*  count   = nullptr;           // +0x10 child(2)
    };
    // +0x90: the "tablePurchase" rows (OnCreated), stride 0x20.
    struct OrderRow {
        UI::Element*        upButton   = nullptr;        // +0x00 cell child(0), id 5000 + i
        UI::Element*        downButton = nullptr;        // +0x08 cell child(1), id 6000 + i
        UI::LabelElement*   label      = nullptr;        // +0x10 the next cell
        UI::SpinnerElement* spinner    = nullptr;        // +0x18 the cell after, id 7000 + i
    };
    // +0xA0: the "tableOrder" cells (OnCreated), stride 8, id 8000 + i.
    struct PurchaseRow {
        UI::CheckElement* check = nullptr;               // +0x00
    };
    // +0xB0: the merchant's goods (TradeComponent 0x140059670), stride 8.
    struct PurchaseItem {
        ComponentDescription* resource = nullptr;        // +0x00
    };

    TradeUI() = default;                                 // 0x140086b80 (inlined in the factory 0x140015f80)

    ~TradeUI() override = default;                       // [0] 0x140016020 (0x140086a50)
    void OnCreated() override;   // [4] 0x140084dd0
    void OnOpened() override;   // [5] 0x140082a70
    void OnClosed() override;   // [6] 0x140082a00
    void OnUpdateLayout() override;   // [7] 0x140082150
    void OnUpdate(const System::Time& time) override;   // [8] 0x1400827b0
    void OnElementEvent(Object::InternalPtr<UI::Element>* element, UI::EventId a, int32_t b,
                        int32_t c, int32_t d, int32_t e, void* userData) override;   // [9] 0x140081f20
    void OnSerializeSettings(IO::Stream& stream) override;   // [10] 0x1400868c0

    // ---- non-virtual members (retail addresses) ---------------------------
    void OnTradeButton();                       // 0x140081420 (tradeui_checkout.cpp)
    void OnDismissButton();                     // 0x140081950
    void OnBuySpinner(int index);               // 0x1400819f0
    void OnSellSpinner(int index);              // 0x140081a50
    void OnStorageSpinner(int index);           // 0x140081ab0
    void OnComboPurchase();                     // 0x140081bc0
    void OnOrderAmountSpinner(int index);       // 0x140081c50
    void OnOrderSpinner(int index, int other);  // 0x140081d00
    void OnPurchaseButton(int index);           // 0x140081dd0
    void OnComboOrder();                        // 0x140081e80
    void UpdateBuyTotals();                     // 0x140082850
    void UpdateSellTotals();                    // 0x140082920
    void RefreshOrderDisplay();                 // 0x140082b20
    void RefreshSellSpinnerValues();            // 0x140082d80
    void RefreshOrderQuantityDisplay();         // 0x140082fb0
    void RefreshBuyTable();                     // 0x140083100
    void RefreshSellTable();                    // 0x140083c70
    void RefreshDisplay();                      // 0x1400846b0

    // ---- fields ------------------------------------------------------------
    Collection::Array<SellItem>     _sellItems;       // +0x30
    Collection::Array<BuyItem>      _buyItems;        // +0x40
    Collection::Array<StorageItem>  _storageItems;    // +0x50
    Collection::Array<SellRow>      _sellRows;        // +0x60
    Collection::Array<BuyRow>       _buyRows;         // +0x70
    Collection::Array<StorageRow>   _storageRows;     // +0x80
    Collection::Array<OrderRow>     _orderRows;       // +0x90
    Collection::Array<PurchaseRow>  _purchaseRows;    // +0xA0
    Collection::Array<PurchaseItem> _purchaseItems;   // +0xB0
    UI::ComboElement*  _comboOrder      = nullptr;    // +0xC0 "comboOrder"
    UI::TableElement*  _tableSell       = nullptr;    // +0xC8 "tableSell"
    UI::TableElement*  _tableBuy        = nullptr;    // +0xD0 "tableBuy"
    UI::TableElement*  _tableStorage    = nullptr;    // +0xD8 "tableStorage"
    UI::TableElement*  _tablePurchase   = nullptr;    // +0xE0 "tablePurchase"
    UI::TableElement*  _tableOrder      = nullptr;    // +0xE8 "tableOrder"
    UI::ButtonElement* _buttonTrade     = nullptr;    // +0xF0 "buttonTrade"
    UI::ButtonElement* _buttonDismiss   = nullptr;    // +0xF8 "buttonDismiss"
    UI::NumberElement* _numberSellTotal = nullptr;    // +0x100 "numberSellTotal"
    UI::NumberElement* _numberBuyTotal  = nullptr;    // +0x108 "numberBuyTotal"
    UI::TextElement*   _textNoStorage   = nullptr;    // +0x110 "textNoStorage"
    UI::TextElement*   _textTooMuch     = nullptr;    // +0x118 "textTooMuch"
    UI::TextElement*   _textNotEnough   = nullptr;    // +0x120 "textNotEnough"
    // +0x128: retail's dtor frees it as an Array; no TradeUI body the tree has
    // ported reads it, so its element type is not known (byte guess).
    Collection::Array<uint8_t> field_0x128;           // +0x128
    UI::ComboElement*  _comboPurchase   = nullptr;    // +0x138 "comboPurchase"
    UI::ComboElement*  _sortCombo       = nullptr;    // +0x140 "sortCombo"
};

static_assert(sizeof(TradeUI::SellItem) == 0x20, "sell item stride 0x20 (0x140016050)");
static_assert(offsetof(TradeUI::SellItem, resource) == 0x0, "SellItem::resource");
static_assert(offsetof(TradeUI::SellItem, value) == 0x8, "SellItem::value");
static_assert(offsetof(TradeUI::SellItem, rawMaterial) == 0x10, "SellItem::rawMaterial");
static_assert(offsetof(TradeUI::SellItem, _pad1c) == 0x1C, "SellItem::_pad1c");
static_assert(sizeof(TradeUI::BuyItem) == 0x18, "buy item stride 0x18 (0x140016130)");
static_assert(offsetof(TradeUI::BuyItem, resource) == 0x0, "BuyItem::resource");
static_assert(offsetof(TradeUI::BuyItem, value) == 0x8, "BuyItem::value");
static_assert(offsetof(TradeUI::BuyItem, _pad14) == 0x14, "BuyItem::_pad14");
static_assert(sizeof(TradeUI::StorageItem) == 0x18, "storage item stride 0x18 (0x140016200)");
static_assert(offsetof(TradeUI::StorageItem, resource) == 0x0, "StorageItem::resource");
static_assert(offsetof(TradeUI::StorageItem, rawMaterial) == 0x8, "StorageItem::rawMaterial");
static_assert(sizeof(TradeUI::SellRow) == 0x20, "sell row stride 0x20");
static_assert(offsetof(TradeUI::SellRow, label) == 0x0, "SellRow::label");
static_assert(offsetof(TradeUI::SellRow, stored) == 0x8, "SellRow::stored");
static_assert(offsetof(TradeUI::SellRow, price) == 0x10, "SellRow::price");
static_assert(sizeof(TradeUI::BuyRow) == 0x20, "buy row stride 0x20");
static_assert(offsetof(TradeUI::BuyRow, label) == 0x0, "BuyRow::label");
static_assert(offsetof(TradeUI::BuyRow, price) == 0x8, "BuyRow::price");
static_assert(offsetof(TradeUI::BuyRow, spinner) == 0x10, "BuyRow::spinner");
static_assert(sizeof(TradeUI::StorageRow) == 0x18, "storage row stride 0x18");
static_assert(offsetof(TradeUI::StorageRow, label) == 0x0, "StorageRow::label");
static_assert(offsetof(TradeUI::StorageRow, spinner) == 0x8, "StorageRow::spinner");
static_assert(sizeof(TradeUI::OrderRow) == 0x20, "order row stride 0x20");
static_assert(offsetof(TradeUI::OrderRow, upButton) == 0x0, "OrderRow::upButton");
static_assert(offsetof(TradeUI::OrderRow, downButton) == 0x8, "OrderRow::downButton");
static_assert(offsetof(TradeUI::OrderRow, label) == 0x10, "OrderRow::label");
static_assert(sizeof(TradeUI::PurchaseRow) == 0x08, "purchase row stride 8");
static_assert(offsetof(TradeUI::PurchaseRow, check) == 0x0, "PurchaseRow::check");
static_assert(sizeof(TradeUI::PurchaseItem) == 0x08, "purchase item stride 8");
static_assert(offsetof(TradeUI::PurchaseItem, resource) == 0x0, "PurchaseItem::resource");
static_assert(offsetof(TradeUI::SellItem, quantity) == 0x18, "SellItem::quantity @ +0x18");
static_assert(offsetof(TradeUI::BuyItem, quantity) == 0x10, "BuyItem::quantity @ +0x10");
static_assert(offsetof(TradeUI::StorageItem, value) == 0x10, "StorageItem::value @ +0x10");
static_assert(offsetof(TradeUI::SellRow, spinner) == 0x18, "SellRow::spinner @ +0x18");
static_assert(offsetof(TradeUI::BuyRow, quantity) == 0x18, "BuyRow::quantity @ +0x18");
static_assert(offsetof(TradeUI::StorageRow, count) == 0x10, "StorageRow::count @ +0x10");
static_assert(offsetof(TradeUI::OrderRow, spinner) == 0x18, "OrderRow::spinner @ +0x18");

static_assert(sizeof(TradeUI) == 0x148, "TradeUI is 0x148 (create 0x140015f80 MemAlloc(0x148))");
static_assert(alignof(TradeUI) == 8, "qword-aligned (the vptr)");
static_assert(offsetof(TradeUI, _sellItems)       == 0x30,  "TradeUI +0x30 (dtor 0x140086b46)");
static_assert(offsetof(TradeUI, _buyItems)        == 0x40,  "TradeUI +0x40 (dtor 0x140086b34)");
static_assert(offsetof(TradeUI, _storageItems)    == 0x50,  "TradeUI +0x50 (dtor 0x140086b22)");
static_assert(offsetof(TradeUI, _sellRows)        == 0x60,  "TradeUI +0x60 (dtor 0x140086b10)");
static_assert(offsetof(TradeUI, _buyRows)         == 0x70,  "TradeUI +0x70 (dtor 0x140086afe)");
static_assert(offsetof(TradeUI, _storageRows)     == 0x80,  "TradeUI +0x80 (dtor 0x140086ae3)");
static_assert(offsetof(TradeUI, _orderRows)       == 0x90,  "TradeUI +0x90 (dtor 0x140086ac8)");
static_assert(offsetof(TradeUI, _purchaseRows)    == 0xA0,  "TradeUI +0xA0 (dtor 0x140086aad)");
static_assert(offsetof(TradeUI, _purchaseItems)   == 0xB0,  "TradeUI +0xB0 (dtor 0x140086a92)");
static_assert(offsetof(TradeUI, _comboOrder)      == 0xC0,  "TradeUI +0xC0");
static_assert(offsetof(TradeUI, _tableSell)       == 0xC8,  "TradeUI +0xC8");
static_assert(offsetof(TradeUI, _tableBuy)        == 0xD0,  "TradeUI +0xD0");
static_assert(offsetof(TradeUI, _tableStorage)    == 0xD8,  "TradeUI +0xD8");
static_assert(offsetof(TradeUI, _tablePurchase)   == 0xE0,  "TradeUI +0xE0");
static_assert(offsetof(TradeUI, _tableOrder)      == 0xE8,  "TradeUI +0xE8");
static_assert(offsetof(TradeUI, _buttonTrade)     == 0xF0,  "TradeUI +0xF0");
static_assert(offsetof(TradeUI, _buttonDismiss)   == 0xF8,  "TradeUI +0xF8");
static_assert(offsetof(TradeUI, _numberSellTotal) == 0x100, "TradeUI +0x100");
static_assert(offsetof(TradeUI, _numberBuyTotal)  == 0x108, "TradeUI +0x108");
static_assert(offsetof(TradeUI, _textNoStorage)   == 0x110, "TradeUI +0x110");
static_assert(offsetof(TradeUI, _textTooMuch)     == 0x118, "TradeUI +0x118");
static_assert(offsetof(TradeUI, _textNotEnough)   == 0x120, "TradeUI +0x120");
static_assert(offsetof(TradeUI, field_0x128)      == 0x128, "TradeUI +0x128 (dtor 0x140086a75)");
static_assert(offsetof(TradeUI, _comboPurchase)   == 0x138, "TradeUI +0x138");
static_assert(offsetof(TradeUI, _sortCombo)       == 0x140, "TradeUI +0x140");

// ---- tradeui_sort.cpp: the _storageItems introsorts (RefreshDisplay 0x1400846b0)
// ByName: RCX = array, EDX = count. ByComponent: R8 = the address of the
// caller's TradeComponent* (its stockRoot tree gives the counts).
extern "C" void TradeUI_IntroSort_ByName_162d0(TradeUI::StorageItem* array, int count);  // 0x1400162d0
extern "C" void TradeUI_IntroSort_ByComponent_165d0(TradeUI::StorageItem* array, int count,
                                                    TradeComponent* const* ctx);  // 0x1400165d0

// ---- tradeui_leaves.cpp: the stock-tree insert (OnStorageSpinner 0x140081ab0)
// The (key, value) pair the insert copies into a new node's first 16 bytes
// (0x14000e1c0 / 0x14000e2c0: two qword loads from the entry, two qword stores
// at node +0x00 / +0x08). The caller (OnStorageSpinner) builds it with the value
// zero-extended, so the pad dword is zero.
struct TradeStockEntry {
    ComponentDescription* resource;   // +0x00 the key (TradeResourceNode::resource)
    int32_t               quantity;   // +0x08 the value (TradeResourceNode::quantity)
    uint8_t               _pad0C[4];  // +0x0c copied with the value's qword
};
static_assert(sizeof(TradeStockEntry) == 0x10, "TradeStockEntry is two qwords (0x14000e1c0)");
static_assert(offsetof(TradeStockEntry, resource) == 0x0, "TradeStockEntry::resource");
static_assert(offsetof(TradeStockEntry, quantity) == 0x08, "TradeStockEntry +0x08");
static_assert(offsetof(TradeStockEntry, _pad0C) == 0x0c, "TradeStockEntry +0x0c");

struct TradeResourceNode;   // Component/TradeComponent.h
template <class Node> struct AvlTreeHead;   // Action/PathAvl.h
// head = &TradeComponent::stock (the root, then the node count at +0x08).
// Returns `out`; *out is the inserted node, or the existing node of that key.
extern "C" TradeResourceNode** TradeUI_StorageTree_Insert_e1c0(AvlTreeHead<TradeResourceNode>* head,
                                                               TradeResourceNode** out,
                                                               const TradeStockEntry* entry);  // 0x14000e1c0
