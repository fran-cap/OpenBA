#pragma once
// ============================================================================
// src/game/Dialog/TownHallUI.h — ENDGAME B.3.8 block PNL (B38_RULES D1-D5)
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §6): reflected "TownHallUI", parent
//   "UI::DialogController", table 0x14015dce0, 11 slots, sizeof 0x1e8.
//   create 0x140014660: MemAlloc(0x1e8, 8), ??0DialogController, stamp, fields.
//   [0] 0x1400146b0 scalar deleting dtor (~TownHallUI + MemFree on &1)
//   [1] TypeName  [2] Type  (DECLARE_EXE_TYPE)
//   [4] 0x140081000  OnCreated
//   [7] 0x14007d550  OnUpdateLayout
//   [8] 0x14007e550  OnUpdate
//   [9] 0x14007c180  OnElementEvent
//   [10] 0x140080db0  OnSerializeSettings
//   every other slot is UI::DialogController's (inherited, not declared).
//
// ~TownHallUI 0x1400810d0: frees the agriculture tree (+0x1d8) and label array
// (+0x1c8), the inventory tree (+0x138) and row array (+0x118), the production
// rows (+0xf0), the two rating groups (+0xd0, +0xb0: each two arrays, the half
// one first), the profession tree (+0x50) and the profession rows (+0x40,
// 0x140015930) -- strictly descending offset, then ~DialogController.
//
// The element pointers are retail's InternalPtr<X> (8 bytes, no refcount),
// spelled as the typed raw pointer (B4 H4); each is the FindElement result of
// the resource element its comment names (InitGeneral 0x1400807f0 and the
// other Init bodies).
//
// B.4 Y GAMEB (behaviour:): the eleven owned containers are typed members.
// The ctor (0x140081280) zeroes each one where its member ctor does; ~TownHallUI
// has an empty body and the members are destroyed in reverse declaration order,
// which is retail's descending-offset order above.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "ui/DialogController.h"
#include "Object/exe_type.h"
#include "collection/Collection.h"   // Collection::Array (the row arrays)
#include "Dialog/RatingGroup.h"      // the hearts / stars groups (+0xB0 / +0xD0)
#include "Player/exe_bodies.h"        // Avl38FreeSubtree (the three trees' subtree free)
#include "system/MemoryAllocator.h"

class ComponentDescription;
class ComponentEntity;
class Profession;
class RawMaterialDescription;
namespace UI {
class ButtonElement;
class ComboElement;
class GraphElement;
class LabelElement;
class NumberEditElement;
class NumberElement;
class StringTable;
class TabElement;
class TextElement;
}

// UpdateProduction's (0x14007c7f0) display entry, stride 0x18: what the two sorts
// 0x140015200 / 0x140015500 order (their comparators read +0x08 and +0x10).
struct TownHallInventoryEntry {
    const ComponentDescription* entityPtr;   // +0x00 the inventory tree's key
    RawMaterialDescription*     desc;        // +0x08 its RawMaterialDescription
    int32_t                     value;       // +0x10 the count shown
    int32_t                     _pad;        // +0x14
};
static_assert(sizeof(TownHallInventoryEntry) == 0x18, "inventory entry stride 0x18 (0x140015200)");
static_assert(offsetof(TownHallInventoryEntry, entityPtr) == 0x0, "TownHallInventoryEntry::entityPtr");
static_assert(offsetof(TownHallInventoryEntry, _pad) == 0x14, "TownHallInventoryEntry::_pad");
static_assert(offsetof(TownHallInventoryEntry, desc) == 0x08, "TownHallInventoryEntry::desc @ +0x08");
static_assert(offsetof(TownHallInventoryEntry, value) == 0x10, "TownHallInventoryEntry::value @ +0x10");

// The profession-row array's hand-derived row (Dialog/townhall_helpers.cpp): the
// growth body 0x140015800 deep-copies each row's entity list (0x140015dd0) and
// destroys every removed / old row (0x140015e50): a deep assign together with a
// per-element destroy, which a trait-derived row cannot express (Collection.h).
namespace TownHallRows {
extern const rtcoll::ArraySpec kSpec_ProfessionRow;   // 0x140015800 Array<TownHallUI::ProfessionRow> (+0x40)
}

class TownHallUI final : public UI::DialogController {
    DECLARE_EXE_TYPE(TownHallUI)

public:
    // +0x40: one row per profession (InitProfessions 0x140080160), stride 0x40.
    // Destroying a row frees its entity list (0x140015930's per-row MemFree, the
    // data qword and the count / capacity qword zeroed).
    struct ProfessionRow {
        int32_t                workers;        // +0x00 adult citizens with the profession
        int32_t                slots;          // +0x04 worker slots over its buildings
        UI::LabelElement*      label;          // +0x08 row child(0)
        UI::NumberEditElement* desired;        // +0x10 content child(0), id 0x1966 + i
        UI::NumberElement*     slotsNumber;    // +0x18 row child(2)
        UI::NumberElement*     buildingsNumber;// +0x20 row child(3)
        UI::ButtonElement*     button;         // +0x28 row child(4), id 0x1999 + i
        Collection::Array<ComponentEntity*> entities;   // +0x30
    };
    using ProfessionRowArray = Collection::Array<ProfessionRow, Collection::DefaultAllocator,
                                                 Collection::ExplicitSpec<&TownHallRows::kSpec_ProfessionRow>>;
    // +0xB0 / +0xD0: a rating group (Dialog/RatingGroup.h; TownHall_GroupRatingInit
    // 0x14006abf0), the one CitizenUI and StatusBarUI embed too.
    using RatingGroup = ::RatingGroup;
    // The three trees' owning header {root, int32 count}: retail's ctor
    // (0x140081280) writes the root qword and the count dword; its dtor
    // (0x1400810d0) frees the root's left subtree, its right subtree (the
    // recursive free 0x140014c30 / 0x140014b00 / 0x14000e320, one body per node
    // width), the root itself, then zeroes the root and the count. The tail dword
    // is never written by retail; the tree zeroes it at construction.
    template <class Node>
    struct OwnedTree {
        Node*    root  = nullptr;   // +0x00
        int32_t  count = 0;         // +0x08
        uint32_t tail  = 0;         // +0x0C
        OwnedTree() = default;
        OwnedTree(const OwnedTree&) = delete;
        OwnedTree& operator=(const OwnedTree&) = delete;
        ~OwnedTree() {
            Node* n = root;
            if (n != nullptr) {
                Avl38FreeSubtree(this, n->left);
                Avl38FreeSubtree(this, n->right);
                System::MemoryAllocator::MemFree(n);
            }
            root = nullptr;
            count = 0;
        }
    };
    // +0xF0: one row per "limits" table row (InitProduction 0x14007fc30), stride 0x28.
    struct ProductionRow {
        UI::NumberElement* stock;         // +0x00
        UI::NumberElement* used;          // +0x08 this period's use
        UI::NumberElement* produced;      // +0x10 this period's production
        UI::NumberElement* usedPrev;      // +0x18 the previous span's use
        UI::NumberElement* producedPrev;  // +0x20 the previous span's production
    };
    // +0x118: one per "inventoryTable" cell (InitInventory 0x14007f660), stride 0x10.
    struct InventoryRow {
        UI::LabelElement*  label;         // +0x00 cell child(0)
        UI::NumberElement* count;         // +0x08 cell child(1)
    };
    // +0x50: the profession tree node (0x30; key = the Profession, value = its row).
    struct ProfessionNode {
        Profession*     key;       // +0x00 compared by address (unsigned)
        uint64_t        index;     // +0x08
        ProfessionNode* parent;    // +0x10
        ProfessionNode* left;      // +0x18
        ProfessionNode* right;     // +0x20
        int16_t         balance;   // +0x28 (retrace 0x140013480: MOV word ptr [R8+0x28])
        int16_t         height;    // +0x2A (retrace 0x140013480: MOV word ptr [R8+0x2a])
        int32_t         _pad2c;    // +0x2C
    };
    // +0x138: the inventory tree node (0x38; key = the raw material's description).
    // The 0x18-byte element is the node's base: the shared insert 0x140014890
    // copies it whole (B.4 Y GAMEB).
    struct InventoryEntry {
        ComponentDescription* key; // +0x00 compared by address (unsigned)
        int32_t        counts[3];  // +0x08 used / produced / storage (UpdateAll 0x14007d590)
        int32_t        _pad14;     // +0x14
    };
    struct InventoryNode : InventoryEntry {
        InventoryNode* parent;     // +0x18
        InventoryNode* left;       // +0x20
        InventoryNode* right;      // +0x28
        int16_t        balance;    // +0x30 (the shared retrace 0x140014b40: int16)
        int16_t        height;     // +0x32 (int16)
        int32_t        _pad34;     // +0x34
    };
    // +0x1D8: the agriculture tree node (0x28; key = the building's description).
    struct AgricultureNode {
        const ComponentDescription* key;   // +0x00 compared by address (unsigned)
        AgricultureNode* parent;   // +0x08
        AgricultureNode* left;     // +0x10
        AgricultureNode* right;    // +0x18
        int16_t          balance;  // +0x20 (retrace 0x1400149a0: MOV word ptr [RDX+0x20])
        int16_t          height;   // +0x22 (retrace 0x1400149a0: MOV word ptr [RDX+0x22])
        int32_t          _pad24;   // +0x24
    };

    TownHallUI() = default;                              // 0x140081280 (create 0x140014660)

    // 0x1400146b0 minus the free; ~TownHallUI is 0x1400810d0. The body is empty:
    // the owned containers are members, destroyed in reverse declaration order
    // (+0x1d8, +0x1c8, +0x138, +0x118, +0xf0, +0xd0 / +0xe0, +0xb0 / +0xc0, +0x50,
    // +0x40: retail's order), then ~DialogController -> ~Internal.
    ~TownHallUI() override;                                       // [0] 0x1400146b0
    void OnCreated() override;   // [4] 0x140081000
    void OnUpdateLayout() override;   // [7] 0x14007d550
    void OnUpdate(const System::Time& time) override;   // [8] 0x14007e550
    void OnElementEvent(Object::InternalPtr<UI::Element>* element, UI::EventId a, int32_t b,
                        int32_t c, int32_t d, int32_t e, void* userData) override;   // [9] 0x14007c180
    void OnSerializeSettings(IO::Stream& stream) override;   // [10] 0x140080db0

    // ---- non-virtual members (retail addresses) ---------------------------
    void UpdateAgriculture();                   // 0x14007c220
    void UpdateProduction();                    // 0x14007c7f0 (inventory tab)
    void UpdateProfessions();                   // 0x14007ccb0 (production tab)
    void UpdateGeneral();                       // 0x14007d030
    void UpdateAll();                           // 0x14007d590
    void OnProfessionButton(int index);         // 0x14007dca0
    void OnProfessionSpinner(int index);        // 0x14007dd10
    void UpdateInventory();                     // 0x14007def0 (professions tab)
    void UpdateGraph();                         // 0x14007e5b0
    void InitAgriculture();                     // 0x14007ebf0
    void InitGraph();                           // 0x14007f0a0
    void InitInventory();                       // 0x14007f660
    void InitProduction();                      // 0x14007fc30
    void InitProfessions();                     // 0x140080160
    void InitGeneral();                         // 0x1400807f0

    // ---- fields --------------------------------------------------------------
    UI::TabElement* tabElement = nullptr;         // +0x30 "tabs"
    uint32_t profButtonIdx = 0;                   // +0x38 current profession button cycle index
    uint8_t _pad3C[4] = {};                       // +0x3C gap
    ProfessionRowArray professionRows;            // +0x40 one row per profession
    OwnedTree<ProfessionNode> professionTree;     // +0x50 profession tree (sorted by Profession)
    // --- General tab elements ---
    UI::NumberElement* numberYear = nullptr;      // +0x60 "numberYear"
    UI::TextElement* textMonth = nullptr;         // +0x68 "textMonth"
    UI::NumberElement* numberHomes = nullptr;     // +0x70 "numberHomes"
    UI::NumberElement* numberFamily = nullptr;    // +0x78 "numberFamily"
    UI::NumberElement* numberCitizens = nullptr;  // +0x80 "numberCitizens"
    UI::NumberElement* numberAdults = nullptr;    // +0x88 "numberAdults"
    UI::NumberElement* numberStudents = nullptr;  // +0x90 "numberStudents"
    UI::NumberElement* numberChildren = nullptr;  // +0x98 "numberChildren"
    UI::TextElement* percentClothed = nullptr;    // +0xA0 "percentClothed" (retail writes the EDUCATED % here, 0x14007d030)
    UI::TextElement* percentEducated = nullptr;   // +0xA8 "percentEducated" (retail writes the CLOTHED % here)
    RatingGroup groupHearts;                      // +0xB0 "groupHearts"
    RatingGroup groupStars;                       // +0xD0 "groupStars"
    // --- Production tab elements ---
    Collection::Array<ProductionRow> prodLimits;  // +0xF0 one per "limits" row (growth 0x1400159b0)
    UI::ComboElement* productionCombo = nullptr;  // +0x100 "productionCombo"
    UI::TextElement* limitPrevUsedHdr = nullptr;  // +0x108 "limitPrevUsedHeader"
    UI::TextElement* limitPrevProdHdr = nullptr;  // +0x110 "limitPrevProducedHeader"
    // --- Inventory tab elements ---
    Collection::Array<InventoryRow> invTable;     // +0x118 one per "inventoryTable" cell
    UI::ComboElement* inventoryCombo = nullptr;   // +0x128 "inventoryCombo"
    UI::ComboElement* sortCombo = nullptr;        // +0x130 "sortCombo" (sort order)
    OwnedTree<InventoryNode> invBST;              // +0x138 inventory tree {root, count +0x140, +0x144 tail}
    int32_t cachedGameTick = 0;                   // +0x148 cached game time tick for graph refresh
    uint8_t _pad14C[4] = {};                      // +0x14C gap
    // --- Graph tab elements ---
    UI::StringTable* graphYearsTable = nullptr;   // +0x150 "Dialog/StringTable.rsc:graphYears"
    UI::StringTable* graphTypesTable = nullptr;   // +0x158 "Dialog/StringTable.rsc:graphTypes"
    UI::GraphElement* graphData = nullptr;        // +0x160 "graphData"
    UI::ComboElement* timeCombo = nullptr;        // +0x168 "timeCombo" (time range)
    UI::ComboElement* graphCombo = nullptr;       // +0x170 "graphCombo" (graph data type)
    UI::TextElement* textXMin = nullptr;          // +0x178 "textXMin" (x-axis min label)
    UI::NumberElement* number0 = nullptr;         // +0x180 "number0" (y-axis max)
    UI::NumberElement* number1 = nullptr;         // +0x188 "number1" (y-axis 3/4)
    UI::NumberElement* number2 = nullptr;         // +0x190 "number2" (y-axis 1/2)
    UI::NumberElement* number3 = nullptr;         // +0x198 "number3" (y-axis 1/4)
    UI::NumberElement* number4 = nullptr;         // +0x1A0 "number4" (y-axis min)
    UI::LabelElement* label0 = nullptr;           // +0x1A8 "label0" (graph legend 0)
    UI::LabelElement* label1 = nullptr;           // +0x1B0 "label1" (graph legend 1)
    UI::LabelElement* label2 = nullptr;           // +0x1B8 "label2" (graph legend 2)
    UI::LabelElement* label3 = nullptr;           // +0x1C0 "label3" (graph legend 3)
    // --- Agriculture tab elements ---
    Collection::Array<UI::LabelElement*> agriLabels;   // +0x1C8 one per agriculture table cell
    // +0x1D8 agriculture tree (sorted by description). Its tail dword +0x1E4 is
    // padding in retail (nothing reads it); the tree's OnUpdate counts its
    // throttled OnUpdateLayout refresh there (STANDALONE DEVIATION).
    OwnedTree<AgricultureNode> agriBST;
};

static_assert(sizeof(TownHallUI::ProfessionRow) == 0x40, "profession row stride 0x40 (0x140015800)");
static_assert(offsetof(TownHallUI::ProfessionRow, workers) == 0x0, "ProfessionRow::workers");
static_assert(offsetof(TownHallUI::ProfessionRow, slots) == 0x4, "ProfessionRow::slots");
static_assert(offsetof(TownHallUI::ProfessionRow, label) == 0x8, "ProfessionRow::label");
static_assert(offsetof(TownHallUI::ProfessionRow, desired) == 0x10, "ProfessionRow::desired");
static_assert(offsetof(TownHallUI::ProfessionRow, slotsNumber) == 0x18, "ProfessionRow::slotsNumber");
static_assert(offsetof(TownHallUI::ProfessionRow, buildingsNumber) == 0x20, "ProfessionRow::buildingsNumber");
static_assert(offsetof(TownHallUI::ProfessionRow, button) == 0x28, "ProfessionRow::button");
static_assert(offsetof(TownHallUI::ProfessionRow, entities) == 0x30, "ProfessionRow::entities @ +0x30");
static_assert(sizeof(TownHallUI::OwnedTree<TownHallUI::InventoryNode>) == 0x10, "tree header {root, count, tail}");
static_assert(sizeof(TownHallUI::ProductionRow) == 0x28, "production row stride 0x28 (0x1400159b0)");
static_assert(offsetof(TownHallUI::ProductionRow, stock) == 0x0, "ProductionRow::stock");
static_assert(offsetof(TownHallUI::ProductionRow, used) == 0x8, "ProductionRow::used");
static_assert(offsetof(TownHallUI::ProductionRow, produced) == 0x10, "ProductionRow::produced");
static_assert(offsetof(TownHallUI::ProductionRow, usedPrev) == 0x18, "ProductionRow::usedPrev");
static_assert(offsetof(TownHallUI::ProductionRow, producedPrev) == 0x20, "ProductionRow::producedPrev");
static_assert(sizeof(TownHallUI::InventoryRow) == 0x10, "inventory row stride 0x10");
static_assert(offsetof(TownHallUI::InventoryRow, label) == 0x0, "InventoryRow::label");
static_assert(offsetof(TownHallUI::InventoryRow, count) == 0x8, "InventoryRow::count");
static_assert(sizeof(TownHallUI::ProfessionNode) == 0x30, "profession node 0x30 (MemAlloc(0x30))");
static_assert(offsetof(TownHallUI::ProfessionNode, key) == 0x0, "ProfessionNode::key");
static_assert(offsetof(TownHallUI::ProfessionNode, index) == 0x8, "ProfessionNode::index");
static_assert(offsetof(TownHallUI::ProfessionNode, parent) == 0x10, "ProfessionNode::parent");
static_assert(offsetof(TownHallUI::ProfessionNode, right) == 0x20, "ProfessionNode::right");
static_assert(offsetof(TownHallUI::ProfessionNode, _pad2c) == 0x2C, "ProfessionNode::_pad2c");
static_assert(offsetof(TownHallUI::ProfessionNode, left) == 0x18, "ProfessionNode::left @ +0x18 (0x140081234)");
static_assert(offsetof(TownHallUI::ProfessionNode, balance) == 0x28, "ProfessionNode::balance @ +0x28 (int16, 0x140013480)");
static_assert(offsetof(TownHallUI::ProfessionNode, height) == 0x2A, "ProfessionNode::height @ +0x2A (int16, 0x140013480)");
static_assert(sizeof(TownHallUI::InventoryNode) == 0x38, "inventory node 0x38");
static_assert(offsetof(TownHallUI::InventoryNode, right) == 0x28, "InventoryNode::right");
static_assert(offsetof(TownHallUI::InventoryNode, _pad34) == 0x34, "InventoryNode::_pad34");
static_assert(offsetof(TownHallUI::InventoryNode, left) == 0x20, "InventoryNode::left @ +0x20 (0x140081163)");
static_assert(sizeof(TownHallUI::InventoryEntry) == 0x18, "the 0x18-byte element 0x140014890 copies");
static_assert(offsetof(TownHallUI::InventoryEntry, key) == 0x0, "InventoryEntry::key");
static_assert(offsetof(TownHallUI::InventoryEntry, counts) == 0x8, "InventoryEntry::counts");
static_assert(offsetof(TownHallUI::InventoryEntry, _pad14) == 0x14, "InventoryEntry::_pad14");
static_assert(offsetof(TownHallUI::InventoryNode, parent) == 0x18, "InventoryNode::parent @ +0x18");
static_assert(offsetof(TownHallUI::InventoryNode, balance) == 0x30, "InventoryNode::balance @ +0x30 (int16, 0x140014b40)");
static_assert(offsetof(TownHallUI::InventoryNode, height) == 0x32, "InventoryNode::height @ +0x32 (int16, 0x140014b40)");
static_assert(sizeof(TownHallUI::AgricultureNode) == 0x28, "agriculture node 0x28 (MemAlloc(0x28))");
static_assert(offsetof(TownHallUI::AgricultureNode, key) == 0x0, "AgricultureNode::key");
static_assert(offsetof(TownHallUI::AgricultureNode, parent) == 0x8, "AgricultureNode::parent");
static_assert(offsetof(TownHallUI::AgricultureNode, right) == 0x18, "AgricultureNode::right");
static_assert(offsetof(TownHallUI::AgricultureNode, _pad24) == 0x24, "AgricultureNode::_pad24");
static_assert(offsetof(TownHallUI::AgricultureNode, left) == 0x10, "AgricultureNode::left @ +0x10 (0x14008110b)");
static_assert(offsetof(TownHallUI::AgricultureNode, balance) == 0x20, "AgricultureNode::balance @ +0x20 (int16, 0x1400149a0)");
static_assert(offsetof(TownHallUI::AgricultureNode, height) == 0x22, "AgricultureNode::height @ +0x22 (int16, 0x1400149a0)");

static_assert(sizeof(TownHallUI) == 0x1e8, "TownHallUI is 0x1e8 (create 0x140014660 MemAlloc(0x1e8))");
static_assert(offsetof(TownHallUI, number1) == 0x188, "TownHallUI::number1");
static_assert(offsetof(TownHallUI, number2) == 0x190, "TownHallUI::number2");
static_assert(offsetof(TownHallUI, number3) == 0x198, "TownHallUI::number3");
static_assert(offsetof(TownHallUI, label1) == 0x1B0, "TownHallUI::label1");
static_assert(offsetof(TownHallUI, label2) == 0x1B8, "TownHallUI::label2");
static_assert(alignof(TownHallUI) == 8, "qword-aligned (the vptr)");
static_assert(offsetof(TownHallUI, tabElement) == 0x30, "tabElement @ 0x30");
static_assert(offsetof(TownHallUI, profButtonIdx) == 0x38, "profButtonIdx @ 0x38");
static_assert(offsetof(TownHallUI, professionRows) == 0x40, "professionRows @ 0x40 (dtor 0x140081254)");
static_assert(offsetof(TownHallUI, professionRows._size) == 0x48, "professionRows._size @ 0x48");
static_assert(offsetof(TownHallUI, professionTree) == 0x50, "professionTree @ 0x50 (dtor 0x14008121c)");
static_assert(offsetof(TownHallUI, professionTree.count) == 0x58, "professionTree.count @ 0x58 (dtor 0x140081251)");
static_assert(offsetof(TownHallUI, numberYear) == 0x60, "numberYear @ 0x60");
static_assert(offsetof(TownHallUI, textMonth) == 0x68, "textMonth @ 0x68");
static_assert(offsetof(TownHallUI, numberHomes) == 0x70, "numberHomes @ 0x70");
static_assert(offsetof(TownHallUI, numberFamily) == 0x78, "numberFamily @ 0x78");
static_assert(offsetof(TownHallUI, numberCitizens) == 0x80, "numberCitizens @ 0x80");
static_assert(offsetof(TownHallUI, numberAdults) == 0x88, "numberAdults @ 0x88");
static_assert(offsetof(TownHallUI, numberStudents) == 0x90, "numberStudents @ 0x90");
static_assert(offsetof(TownHallUI, numberChildren) == 0x98, "numberChildren @ 0x98");
static_assert(offsetof(TownHallUI, percentClothed) == 0xA0, "percentClothed @ 0xA0");
static_assert(offsetof(TownHallUI, percentEducated) == 0xA8, "percentEducated @ 0xA8");
static_assert(offsetof(TownHallUI, groupHearts) == 0xB0, "groupHearts @ 0xB0 (dtor 0x1400811ee)");
static_assert(offsetof(TownHallUI, groupStars) == 0xD0, "groupStars @ 0xD0 (dtor 0x1400811c0)");
static_assert(offsetof(TownHallUI, prodLimits) == 0xF0, "prodLimits @ 0xF0 (dtor 0x1400811a5)");
static_assert(offsetof(TownHallUI, prodLimits._size) == 0xF8, "prodLimits._size @ 0xF8");
static_assert(offsetof(TownHallUI, productionCombo) == 0x100, "productionCombo @ 0x100");
static_assert(offsetof(TownHallUI, limitPrevUsedHdr) == 0x108, "limitPrevUsedHdr @ 0x108");
static_assert(offsetof(TownHallUI, limitPrevProdHdr) == 0x110, "limitPrevProdHdr @ 0x110");
static_assert(offsetof(TownHallUI, invTable) == 0x118, "invTable @ 0x118 (dtor 0x14008118a)");
static_assert(offsetof(TownHallUI, invTable._size) == 0x120, "invTable._size @ 0x120");
static_assert(offsetof(TownHallUI, inventoryCombo) == 0x128, "inventoryCombo @ 0x128");
static_assert(offsetof(TownHallUI, sortCombo) == 0x130, "sortCombo @ 0x130");
static_assert(offsetof(TownHallUI, invBST) == 0x138, "invBST @ 0x138 (dtor 0x14008114f)");
static_assert(offsetof(TownHallUI, invBST.count) == 0x140, "invBST.count @ 0x140 (dtor 0x140081187)");
static_assert(offsetof(TownHallUI, cachedGameTick) == 0x148, "cachedGameTick @ 0x148");
static_assert(offsetof(TownHallUI, graphYearsTable) == 0x150, "graphYearsTable @ 0x150");
static_assert(offsetof(TownHallUI, graphTypesTable) == 0x158, "graphTypesTable @ 0x158");
static_assert(offsetof(TownHallUI, graphData) == 0x160, "graphData @ 0x160");
static_assert(offsetof(TownHallUI, timeCombo) == 0x168, "timeCombo @ 0x168");
static_assert(offsetof(TownHallUI, graphCombo) == 0x170, "graphCombo @ 0x170");
static_assert(offsetof(TownHallUI, textXMin) == 0x178, "textXMin @ 0x178");
static_assert(offsetof(TownHallUI, number0) == 0x180, "number0 @ 0x180");
static_assert(offsetof(TownHallUI, number4) == 0x1A0, "number4 @ 0x1A0");
static_assert(offsetof(TownHallUI, label0) == 0x1A8, "label0 @ 0x1A8");
static_assert(offsetof(TownHallUI, label3) == 0x1C0, "label3 @ 0x1C0");
static_assert(offsetof(TownHallUI, agriLabels) == 0x1C8, "agriLabels @ 0x1C8 (dtor 0x140081134)");
static_assert(offsetof(TownHallUI, agriLabels._size) == 0x1D0, "agriLabels._size @ 0x1D0");
static_assert(offsetof(TownHallUI, agriBST) == 0x1D8, "agriBST @ 0x1D8 (dtor 0x1400810f7)");
static_assert(offsetof(TownHallUI, agriBST.count) == 0x1E0, "agriBST.count @ 0x1E0 (dtor 0x140081131)");
static_assert(offsetof(TownHallUI, agriBST.tail) == 0x1E4, "agriBST.tail @ 0x1E4");

// townhall_helpers.cpp: the rating group's two bodies are declared in Dialog/RatingGroup.h.

// 0x140014890 (Dialog/toolbar_utility_helpers.cpp): the shared 0x38-node
// find-or-insert, the inventory tree's typed wrapper (the PinnedToolNode one is in
// Dialog/exe_bodies.h).
TownHallUI::InventoryNode* Avl38SetInsert(TownHallUI::OwnedTree<TownHallUI::InventoryNode>* set,
                                          const TownHallUI::InventoryEntry& rec);   // 0x140014890
