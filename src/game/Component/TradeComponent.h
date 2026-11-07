#pragma once
// ============================================================================
// src/game/Component/TradeComponent.h — ENDGAME B.3.8 block MI-B
//
// `TradeComponent` (the trading post: merchant table, buy orders, dock state) as
// a real C++ class with a compiler-generated vtable and its state controller as
// an EMBEDDED MEMBER at +0x88, after the class's own 0x60 bytes (B38_RULES.md
// D1-D6; there is no multiple inheritance). The engine/types POD is a forwarder.
//
// RETAIL (B38_SLOTS.md §2 / §6, bytes read 2026-09-26):
//   reflected "TradeComponent", parent "ControllerComponent", ti 0x1401c4b80
//   create 0x14000db60: MemAlloc(0xa8, 8) + ctor 0x14005c430
//   ctor 0x14005c430: Component ctor; +0x28 = 0; +0x2c = -1; +0x30..+0x58 = 0;
//     +0x60 = 0 (qword), +0x68 = 0 (dword); `MOV byte [RBX+0x71],0`; +0x78 = +0x80
//     = 0; member at +0x88 (0x14015f070, 0x14015ac80), back-pointer +0xa0;
//     `_flags |= 3`.
//   dtor 0x14005c340: member restamp; frees the +0x60 BST (0x14000e320 on both
//     children + MemFree), +0x50, +0x40 and the +0x30 name array (0x14000ef10 +
//     MemFree), zeroing each; restamps; ~Internal. [0] 0x14000dc80 = dtor + MemFree.
//   table 0x14015acd0, 18 slots: [3] 0x14005bd50 Persist, [6] 0x14005c1d0 OnAdded
//     (Ghidra "Draw"), [7] 0x14005c190 OnRemove (Ghidra "OnCreate"), [13]
//     0x14005c140 CommandMask, [14] 0x14005c000 Command, [16] 0x1400026c0
//     `MOV EAX,2`, [17] 0x14000dba0 `LEA RAX,[RCX+0x88]`; every other slot =
//     ControllerComponent's / Component's.
//   member table 0x14015ac80: the table-driven controller over the class's state
//     table (ControllerLive::Dispatcher<TradeCtrlTag>).
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Component/ControllerComponent.h"
#include "Component/controller_state_table.h"
#include "Object/exe_type.h"
#include "object/Internal.h"   // Object::InternalPtr<T>
#include "system/String.h"
#include "system/Value.h"      // System::Time
#include "Action/PathAvl.h"      // AvlTreeHead<Node> (the +0x60 stock tree header)
#include "collection/Collection.h" // Collection::Array (the +0x30 / +0x40 / +0x50 arrays)

struct TradeCtrlTag { using Owner = class TradeComponent; };   // the handlers' receiver (controller_state_table.h)
class ComponentDescription;   // Component/ComponentDescription.h
class ComponentEntity;        // Entity/ComponentEntity.h

// ---- the element types of TradeComponent's arrays and tree (B.4 COMP-f) ----
// Read off retail's ctor 0x14005c430, dtor 0x14005c340, Persist 0x14005bd50, the
// resize bodies 0x14000e760 / 0x14000e8d0 / 0x14000f090, the name-array dtor
// 0x14000ef10 and the tree bodies 0x14000ebe0 / 0x14000e320 / 0x14000e360.

// +0x30 element (0x20): one per merchant of the TradeDescription. 0x14000ef10 frees
// `goods` then runs ~String on +0x00; Persist serializes String, the goods count,
// the goods (ExternalPtr<ComponentDescription> array 0x1400049c0), then +0x08.
// `goods` grows through retail's stride-8 row (Persist's 0x140001bb0 SetSize(n): stride 8,
// align 8, the new tail ZEROED, no element destroy): kSpec_PtrZero (Entity/EntityList.h).
// The element's lifetime stays the +0x30 row's (kSpec_TradeMerchantOrder below:
// 0x14000e760's two construct arms and 0x14000ef10's destroy), unchanged.
namespace rtcoll { extern const ArraySpec kSpec_PtrZero; }   // 0x180008d60
struct TradeMerchantOrder {
    using GoodsList = Collection::Array<ComponentDescription*, Collection::DefaultAllocator,
                                        Collection::ExplicitSpec<&rtcoll::kSpec_PtrZero>>;

    System::String         merchantName;   // +0x00 NameDB pick (Setup 0x14005c220)
    int32_t                orderKind;      // +0x08 0 none, 1 once, 2 standing (0x14005b580)
    uint8_t                _pad0C[4];      // +0x0c
    GoodsList              goods;          // +0x10 Array<ExternalPtr<ComponentDescription>> (count +0x18, cap +0x1c)
};
static_assert(sizeof(TradeMerchantOrder) == 0x20, "TradeMerchantOrder stride 0x20 (0x14000e760)");
static_assert(offsetof(TradeMerchantOrder, merchantName) == 0x0, "TradeMerchantOrder::merchantName");
static_assert(offsetof(TradeMerchantOrder, _pad0C) == 0xC, "TradeMerchantOrder::_pad0C");
static_assert(offsetof(TradeMerchantOrder, orderKind) == 0x08, "TradeMerchantOrder +0x08");
static_assert(offsetof(TradeMerchantOrder, goods) == 0x10, "TradeMerchantOrder +0x10");
static_assert(offsetof(TradeMerchantOrder, goods) + offsetof(TradeMerchantOrder::GoodsList, _size) == 0x18, "TradeMerchantOrder +0x18");
static_assert(offsetof(TradeMerchantOrder, goods) + offsetof(TradeMerchantOrder::GoodsList, _capacity) == 0x1c, "TradeMerchantOrder +0x1c");

// +0x40 element (0xc): the standing buy orders, one per TradeDescription goods
// category. Persist writes +0x00 through the uint door, then +0x08, then +0x04.
struct TradeBuyOrder {
    uint32_t rawMaterialFlags;   // +0x00 the category's mask (ANDed with RawMaterialDescription::_flags)
    int32_t  categoryIndex;      // +0x04 the category's index (Setup 0x14005c220)
    int32_t  amount;             // +0x08 units wanted (0x140059ad0)
};
static_assert(sizeof(TradeBuyOrder) == 0xc, "TradeBuyOrder stride 0xc (0x14000e8d0)");
static_assert(offsetof(TradeBuyOrder, rawMaterialFlags) == 0x0, "TradeBuyOrder::rawMaterialFlags");
static_assert(offsetof(TradeBuyOrder, categoryIndex) == 0x04, "TradeBuyOrder +0x04");
static_assert(offsetof(TradeBuyOrder, amount) == 0x08, "TradeBuyOrder +0x08");

// +0x50 element (0x10): the docked merchant's manifest (0x14005b580). Persist:
// ExternalPtr<ComponentDescription> (0x140002340), int, int.
struct TradeManifestLine {
    ComponentDescription* good;   // +0x00 ExternalPtr<ComponentDescription>
    int32_t               price;  // +0x08 unit price
    int32_t               stock;  // +0x0c units the merchant has
};
static_assert(sizeof(TradeManifestLine) == 0x10, "TradeManifestLine stride 0x10 (0x14000f090)");
static_assert(offsetof(TradeManifestLine, good) == 0x0, "TradeManifestLine::good");
static_assert(offsetof(TradeManifestLine, price) == 0x08, "TradeManifestLine +0x08");
static_assert(offsetof(TradeManifestLine, stock) == 0x0c, "TradeManifestLine +0x0c");

// A node (0x30, MemAlloc(0x30, 8)) of the AVL map ComponentDescription -> int:
// the +0x60 stock tree (0x14000ebe0) and 0x140059ad0's purchase tally.
struct TradeResourceNode {
    ComponentDescription* resource;   // +0x00 the key, ExternalPtr<ComponentDescription>
    int32_t               quantity;   // +0x08 the value (only the low dword is read)
    uint8_t               _pad0C[4];  // +0x0c
    TradeResourceNode*    parent;     // +0x10
    TradeResourceNode*    left;       // +0x18 lesser keys
    TradeResourceNode*    right;      // +0x20 greater keys
    int16_t               balance;    // +0x28 right height - left height
    int16_t               height;     // +0x2a
    uint8_t               _pad2C[4];  // +0x2c
};
static_assert(sizeof(TradeResourceNode) == 0x30, "TradeResourceNode is MemAlloc(0x30)");
static_assert(offsetof(TradeResourceNode, resource) == 0x0, "TradeResourceNode::resource");
static_assert(offsetof(TradeResourceNode, _pad0C) == 0xC, "TradeResourceNode::_pad0C");
static_assert(offsetof(TradeResourceNode, _pad2C) == 0x2C, "TradeResourceNode::_pad2C");
static_assert(offsetof(TradeResourceNode, quantity) == 0x08, "TradeResourceNode +0x08");
static_assert(offsetof(TradeResourceNode, parent) == 0x10, "TradeResourceNode +0x10");
static_assert(offsetof(TradeResourceNode, left) == 0x18, "TradeResourceNode +0x18");
static_assert(offsetof(TradeResourceNode, right) == 0x20, "TradeResourceNode +0x20");
static_assert(offsetof(TradeResourceNode, balance) == 0x28, "TradeResourceNode +0x28");
static_assert(offsetof(TradeResourceNode, height) == 0x2a, "TradeResourceNode +0x2a");

// The +0x60 stock tree: the header plus the release retail's dtor 0x14005c340 does
// at 0x14005c380..0x14005c3b7 (both subtrees through 0x14000e320, then the root;
// +0x60 = 0 qword, +0x68 = 0 dword). Defined in tradecomponent.cpp.
// The ctor is retail's two stores (0x14005c492 qword root = 0, 0x14005c495 dword count = 0);
// +0x0C is not written (B6 row 27).
// 0x140059ad0's purchase tally is one too: retail builds the header on its stack
// (0x140059b2b qword [RBP-0x51] = 0, 0x140059b2f dword [RBP-0x49] = 0) and releases it
// with this dtor inlined (0x14005a440..0x14005a470, LEA RCX,[RBP-0x51] into 0x14000e320).
struct TradeStockTree : AvlTreeHead<TradeResourceNode> {
    TradeStockTree() { root = nullptr; count = 0; }
    ~TradeStockTree();
    // AVL retrace from `start` up to the root; RCX is the header (its root slot):
    // 0x14000ebe0's insert hands &stock (this+0x60), 0x140059ad0 LEA RCX,[RBP-0x51].
    void Retrace(TradeResourceNode* start);   // 0x14000e360 (tradecomponent_serialize.cpp)
};
static_assert(sizeof(TradeStockTree) == 0x10, "TradeStockTree is the 0x10 Set header");

// The element rows of the +0x30 and +0x50 arrays (defined in tradecomponent.cpp /
// storagecomponent.cpp). +0x30's destroy is 0x14000ef10; +0x50's is 0x14000f090's
// row (copy two qwords, zero the new tail, no element destroy).
namespace CitizenBehaviorRows { extern const rtcoll::ArraySpec kSpec_Pod16; }
extern const rtcoll::ArraySpec kSpec_TradeMerchantOrder;

class TradeComponent final : public ControllerComponent {
    DECLARE_EXE_TYPE(TradeComponent)

public:
    using StateController = ::ControllerLive::TableController<TradeCtrlTag>;

    TradeComponent();                                                    // 0x14005c430

    // 0x14005c340: the body is empty; ~_controller (the +0x88 restamp), then the four
    // members below (+0x60, +0x50, +0x40, +0x30), then ~ControllerComponent.
    ~TradeComponent() override;                                         // [0]  0x14000dc80
    void          Persist(IO::Stream& stream) override;                       // [3]  0x14005bd50
    void          OnAdded() override;                                   // [6]  0x14005c1d0
    void          OnRemove() override;                                  // [7]  0x14005c190
    uint32_t      CommandMask(int command) override;                    // [13] 0x14005c140
    void          Command(int command) override;                        // [14] 0x14005c000
    int         GetDefaultState() const override { return SmId(BuildingState::Built); }  // [16] 0x1400026c0
    Controller* GetController() override { return &_controller; }       // [17] 0x14000dba0

    // ---- non-virtual members (retail addresses; B.4 COMP-f) ----
    void Setup();                                                       // 0x14005c220
    int  PickMerchantTier();                                            // 0x14005b4f0
    void ArmMerchantTimer();                                            // 0x14005b450
    void BuildMerchantManifest();                                       // 0x14005b580
    void AutoTrade();                                                   // 0x140059ad0
    void SpawnPurchasedLivestock(ComponentDescription* tmpl, unsigned int qty);   // 0x14005ace0
    void* GetOrderName(System::String* out);                            // 0x140015fd0 the docked merchant's name (+0x2c into +0x30), or a null String
    void  GetBuyEntries(void* outArray) const;                          // 0x140059670 the docked merchant's buy list (out: TradeUI::_purchaseItems or a stack array, 8-byte stride)
    void  ExtendMerchantStay();                                         // 0x14005a490 raise the town's merchant weight / stay time to the description's
    void  AddPurchaseOrder(ComponentDescription* resource, int8_t addFlag);   // 0x140059490 toggle `resource` in the docked merchant's order
    int   GetResourcePrice(const ComponentDescription* resource) const;  // 0x14005a520
    // The controller's state handlers (table 0x14005c650; RCX = this).
    char EnterIdle();                                                   // 0x14005abd0 state 2 Built enter
    char TickIdle(StateMachine* sm, const System::Time* deltaTime);                         // 0x14005aa60 state 2 Built per-frame
    char EnterInbound();                                                // 0x14005aa50 state 0x12 MerchantInbound enter
    char OnMerchantDocked(StateMachine* sm);                                    // 0x14005a850 state 0x12 event 9 MerchantDocked
    char OnMerchantLost(StateMachine* sm);                                      // 0x14005a9c0 state 0x12 event 0xb MerchantLost
    char EnterDocked();                                                 // 0x14005a800 state 0x13 MerchantDocked enter
    char TickDocked(StateMachine* sm, const System::Time* deltaTime);                       // 0x14005a780 state 0x13 per-frame
    char LeaveDocked();                                                 // 0x14005a6d0 state 0x13 update
    char Teardown();                                                    // 0x14005b160 transition 3 Demolish

    // PUBLIC while unconverted readers (TradeUI, the trade harness) read them.
    int32_t  tradeState;        // +0x28 auto-trade mode: 1 on arrival, 2 on departure (Persist int&)
    int32_t  tradeId;           // +0x2c the docked merchant's index, -1 = none
    using MerchantOrderArray = Collection::Array<TradeMerchantOrder, Collection::DefaultAllocator,
                                                 Collection::ExplicitSpec<&kSpec_TradeMerchantOrder>>;
    using ManifestArray = Collection::Array<TradeManifestLine, Collection::DefaultAllocator,
                                            Collection::ExplicitSpec<&CitizenBehaviorRows::kSpec_Pod16>>;
    // The dtor's body is empty: these four are released in reverse declaration order
    // (+0x60, +0x50, +0x40, +0x30), exactly the order of retail's 0x14005c340.
    MerchantOrderArray merchantOrders;     // +0x30 (count +0x38, cap +0x3c; elements freed through 0x14000ef10)
    Collection::Array<TradeBuyOrder> buyOrders;   // +0x40 (count +0x48, cap +0x4c)
    ManifestArray manifest;                // +0x50 (count +0x58, cap +0x5c; SetSize 0x14000f090)
    TradeStockTree stock;                  // +0x60 the stock tree (TradeUI's sell rows): root +0x60, node count
                                           // +0x68; +0x6c (_pad0C) never written by retail's ctor
    bool     workEnabled;       // +0x70 Command's cached toggle (Persist bool&; NOT written by the ctor)
    bool     tradeFlag;         // +0x71 the trade panel is open: the stay timer waits (Persist bool&)
    uint8_t  _pad72[6];         // +0x72
    System::Time merchantTimer;            // +0x78 countdown to the next merchant / its stay
    Object::InternalPtr<ComponentEntity> merchant;   // +0x80 the merchant boat entity (Persist
                                                     //       InternalBasePtr + IsDerivedFrom ComponentEntity)
    StateController _controller;   // +0x88 the embedded controller (D6)
};

static_assert(sizeof(TradeComponent) == 0xa8, "TradeComponent is 0xa8 (create 0x14000db60)");
static_assert(offsetof(TradeComponent, _pad72) == 0x72, "TradeComponent::_pad72");
static_assert(alignof(TradeComponent) == 8, "TradeComponent alignment");
static_assert(offsetof(TradeComponent, tradeState) == 0x28, "TradeComponent +0x28");
static_assert(offsetof(TradeComponent, tradeId) == 0x2c, "TradeComponent +0x2c (ctor 0x14005c46a)");
static_assert(offsetof(TradeComponent, merchantOrders) == 0x30, "TradeComponent +0x30");
static_assert(offsetof(TradeComponent, merchantOrders) + offsetof(TradeComponent::MerchantOrderArray, _size) == 0x38, "TradeComponent +0x38");
static_assert(offsetof(TradeComponent, merchantOrders) + offsetof(TradeComponent::MerchantOrderArray, _capacity) == 0x3c, "TradeComponent +0x3c");
static_assert(offsetof(TradeComponent, buyOrders) == 0x40, "TradeComponent +0x40");
static_assert(offsetof(TradeComponent, buyOrders) + offsetof(Collection::Array<TradeBuyOrder>, _size) == 0x48, "TradeComponent +0x48");
static_assert(offsetof(TradeComponent, buyOrders) + offsetof(Collection::Array<TradeBuyOrder>, _capacity) == 0x4c, "TradeComponent +0x4c");
static_assert(offsetof(TradeComponent, manifest) == 0x50, "TradeComponent +0x50");
static_assert(offsetof(TradeComponent, manifest) + offsetof(TradeComponent::ManifestArray, _size) == 0x58, "TradeComponent +0x58");
static_assert(offsetof(TradeComponent, manifest) + offsetof(TradeComponent::ManifestArray, _capacity) == 0x5c, "TradeComponent +0x5c");
static_assert(offsetof(TradeComponent, stock) + offsetof(AvlTreeHead<TradeResourceNode>, root) == 0x60, "TradeComponent +0x60 (ctor 0x14005c492)");
static_assert(offsetof(TradeComponent, stock) + offsetof(AvlTreeHead<TradeResourceNode>, count) == 0x68, "TradeComponent +0x68 (ctor 0x14005c495)");
static_assert(offsetof(TradeComponent, workEnabled) == 0x70, "TradeComponent +0x70");
static_assert(offsetof(TradeComponent, tradeFlag) == 0x71, "TradeComponent +0x71 (ctor 0x14005c498)");
static_assert(offsetof(TradeComponent, merchantTimer) == 0x78, "TradeComponent +0x78");
static_assert(offsetof(TradeComponent, merchant) == 0x80, "TradeComponent +0x80");
static_assert(offsetof(TradeComponent, _controller) == 0x88,
              "TradeComponent's controller member at +0x88 (slot 17 0x14000dba0: LEA RAX,[RCX+0x88])");
static_assert(offsetof(TradeComponent, _controller) + offsetof(ControllerComponent::Controller, _owner) == 0xa0,
              "controller back-pointer at +0xa0 (ctor 0x14005c4ce)");
