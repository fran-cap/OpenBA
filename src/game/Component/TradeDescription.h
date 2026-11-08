#pragma once
// ============================================================================
// src/game/Component/TradeDescription.h — ENDGAME B.3.8 K1 (B38_RULES.md D1-D5, §2.2)
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §6):
//   reflected "TradeDescription", parent "Description", ti 0x1401c4bb8
//   create 0x14000dbd0: MemAlloc(0x70, 8), `??0External@Object@@`, the
//   Description stamp, the leaf stamp, the field defaults. createInPlace 0x14000dc10.
//   table 0x14015ad60, 10 slots:
//     [0] 0x14000dc50 scalar deleting dtor   [1] 0x14000dbc0 TypeName   [2] 0x14000dbb0 Type
//     [3] 0x14005ca00 GetFileVersion = 7   [4] External::Compile (import, inherited)
//     [5] 0x14005c7f0 Serialize (`MOV AL,1`)
//     [6] 0x14000dc20 Reconstruct   [7] 0x140001640 Destruct (shared)
//     [8] 0x14000dc40 GetComponentType: TradeComponent's TypeId
//     [9] 0x14000dc10 Construct (folded with createInPlace)
//   sizeof 0x70.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Component/Description.h"
#include "Object/exe_type.h"
#include "system/String.h"
namespace UI { class SpriteSheet; class StringTable; }   // B4 H4 (DLG-e)
class ComponentDescription;   // Component/ComponentDescription.h

// ---- the element types of TradeDescription's three arrays (B.4 COMP-f) ----
// Read off Serialize 0x14005c7f0 and its merchant body 0x14000f550.

// +0x38 element (0x18): a goods category of the trade panel. Serialize: +0x00
// through the uint door, then the two Strings.
struct TradeGoodsCategory {
    uint32_t       rawMaterialFlags;   // +0x00 enum (TradeComponent::Setup copies it into the buy orders)
    uint8_t        _pad04[4];          // +0x04
    System::String spriteName;         // +0x08 TradeUI: the sprite in _resourceSpriteSheet
    System::String textName;           // +0x10 TradeUI: the string in _resourceStringTable
};
static_assert(sizeof(TradeGoodsCategory) == 0x18, "TradeGoodsCategory stride 0x18");
static_assert(offsetof(TradeGoodsCategory, rawMaterialFlags) == 0x0, "TradeGoodsCategory::rawMaterialFlags");
static_assert(offsetof(TradeGoodsCategory, _pad04) == 0x4, "TradeGoodsCategory::_pad04");
static_assert(offsetof(TradeGoodsCategory, spriteName) == 0x08, "TradeGoodsCategory +0x08");
static_assert(offsetof(TradeGoodsCategory, textName) == 0x10, "TradeGoodsCategory +0x10");

// +0x48 element (0x48): a merchant. 0x14000f550: ExternalPtr (0x14000bf40, the
// StringTable door), String, the goods count + ExternalPtr array (0x140001bb0 /
// 0x1400049c0), the enum array (0x1400021d0 + the uint door), +0x40 int&, then
// +0x10 / +0x14 / +0x18 through the uint door.
struct TradeMerchant {
    UI::StringTable*       stringTable;      // +0x00 ExternalPtr (0x14000bf40)
    System::String         name;             // +0x08
    uint32_t               sellFlags;       // +0x10 enum
    uint32_t               fullPriceFlags;   // +0x14 categories bought at the full price (0x14005a520)
    uint32_t               discountFlags;    // +0x18 categories bought at the discount (0x14005a520)
    uint8_t                _pad1C[4];        // +0x1c
    ComponentDescription** goods;            // +0x20 Array<ExternalPtr<ComponentDescription>> data
    int32_t                goodsCount;       // +0x28
    int32_t                goodsCap;         // +0x2c
    uint32_t*              naturalResources;       // +0x30 Array<enum> data
    int32_t                naturalResourceCount;  // +0x38
    int32_t                naturalResourceCap;    // +0x3c
    int32_t                stockRolls;       // +0x40 manifest rolls, divided by the tier's rollDivisor (0x14005b580)
    uint8_t                _pad44[4];        // +0x44
};
static_assert(sizeof(TradeMerchant) == 0x48, "TradeMerchant stride 0x48");
static_assert(offsetof(TradeMerchant, stringTable) == 0x0, "TradeMerchant::stringTable");
static_assert(offsetof(TradeMerchant, _pad1C) == 0x1C, "TradeMerchant::_pad1C");
static_assert(offsetof(TradeMerchant, goodsCap) == 0x2C, "TradeMerchant::goodsCap");
static_assert(offsetof(TradeMerchant, naturalResourceCap) == 0x3C, "TradeMerchant::naturalResourceCap");
static_assert(offsetof(TradeMerchant, _pad44) == 0x44, "TradeMerchant::_pad44");
static_assert(offsetof(TradeMerchant, name) == 0x08, "TradeMerchant +0x08");
static_assert(offsetof(TradeMerchant, sellFlags) == 0x10, "TradeMerchant +0x10");
static_assert(offsetof(TradeMerchant, fullPriceFlags) == 0x14, "TradeMerchant +0x14");
static_assert(offsetof(TradeMerchant, discountFlags) == 0x18, "TradeMerchant +0x18");
static_assert(offsetof(TradeMerchant, goods) == 0x20, "TradeMerchant +0x20");
static_assert(offsetof(TradeMerchant, goodsCount) == 0x28, "TradeMerchant +0x28");
static_assert(offsetof(TradeMerchant, naturalResources) == 0x30, "TradeMerchant +0x30");
static_assert(offsetof(TradeMerchant, naturalResourceCount) == 0x38, "TradeMerchant +0x38");
static_assert(offsetof(TradeMerchant, stockRolls) == 0x40, "TradeMerchant +0x40");

// +0x58 element (0x14): a merchant tier, picked by the town's population
// (0x14005b4f0). Serialize: int, float, float, int, int.
struct TradeTier {
    int32_t populationLimit;   // +0x00 the tier applies below this population
    float   delayBase;         // +0x04 merchant delay, x300 s (0x14005b450)
    float   delayTolerance;    // +0x08
    int32_t rollDivisor;       // +0x0c divides TradeMerchant::stockRolls
    int32_t stockDivisor;      // +0x10 divides ValueDescription::_stackCount
};
static_assert(sizeof(TradeTier) == 0x14, "TradeTier stride 0x14");
static_assert(offsetof(TradeTier, populationLimit) == 0x0, "TradeTier::populationLimit");
static_assert(offsetof(TradeTier, delayBase) == 0x04, "TradeTier +0x04");
static_assert(offsetof(TradeTier, delayTolerance) == 0x08, "TradeTier +0x08");
static_assert(offsetof(TradeTier, rollDivisor) == 0x0c, "TradeTier +0x0c");
static_assert(offsetof(TradeTier, stockDivisor) == 0x10, "TradeTier +0x10");

class TradeDescription final : public Description {
    DECLARE_EXE_TYPE(TradeDescription)

public:
    TradeDescription();

    ~TradeDescription() override;                                          // [0] 0x14000dc50
    int32_t           GetFileVersion() const override { return 7; }    // [3] 0x14005ca00
    bool              Serialize(IO::Stream& stream) override;                    // [5] 0x14005c7f0
    Object::External* Reconstruct() override;                              // [6] 0x14000dc20
    void              Destruct() override;                                 // [7] 0x140001640
    Object::Type*     GetComponentType(Object::Type* ret) const override;  // [8] 0x14000dc40
    virtual Object::External* Construct();                                 // [9] 0x14000dc10

    // ---- fields (PUBLIC: the components read them at raw offsets today) ----
    UI::SpriteSheet* _resourceSpriteSheet;                    // +0x20 ExternalPtr (a raw pointer)
    UI::StringTable* _resourceStringTable;                    // +0x28 ExternalPtr (a raw pointer)
    ComponentDescription* _merchantTemplate;    // +0x30 ExternalPtr<ComponentDescription> (0x140002340): the merchant boat
    TradeGoodsCategory* _pairs;                 // +0x38 goods-category array data
    int32_t _pairCount;                         // +0x40
    int32_t _pairCap;                           // +0x44
    TradeMerchant* _merchants;                  // +0x48 merchant array data
    int32_t _merchantCount;                     // +0x50
    int32_t _merchantCap;                       // +0x54
    TradeTier* _prices;                         // +0x58 merchant-tier array data
    int32_t _priceCount;                        // +0x60
    int32_t _priceCap;                          // +0x64
    float _sicknessAmount;                                 // +0x68 ~0.333
    int32_t _sicknessTimeInMonths;                               // +0x6c
};

static_assert(sizeof(TradeDescription) == 0x70, "TradeDescription is 0x70 (create 0x14000dbd0 MemAlloc(0x70))");
static_assert(alignof(TradeDescription) == 8, "TradeDescription alignment");
static_assert(offsetof(TradeDescription, _resourceSpriteSheet) == 0x20, "TradeDescription::_resourceSpriteSheet @ +0x20");
static_assert(offsetof(TradeDescription, _resourceStringTable) == 0x28, "TradeDescription::_resourceStringTable @ +0x28");
static_assert(offsetof(TradeDescription, _merchantTemplate) == 0x30, "TradeDescription::_merchantTemplate @ +0x30");
static_assert(offsetof(TradeDescription, _pairs) == 0x38, "TradeDescription::_pairs @ +0x38");
static_assert(offsetof(TradeDescription, _pairCount) == 0x40, "TradeDescription::_pairCount @ +0x40");
static_assert(offsetof(TradeDescription, _pairCap) == 0x44, "TradeDescription::_pairCap @ +0x44");
static_assert(offsetof(TradeDescription, _merchants) == 0x48, "TradeDescription::_merchants @ +0x48");
static_assert(offsetof(TradeDescription, _merchantCount) == 0x50, "TradeDescription::_merchantCount @ +0x50");
static_assert(offsetof(TradeDescription, _merchantCap) == 0x54, "TradeDescription::_merchantCap @ +0x54");
static_assert(offsetof(TradeDescription, _prices) == 0x58, "TradeDescription::_prices @ +0x58");
static_assert(offsetof(TradeDescription, _priceCount) == 0x60, "TradeDescription::_priceCount @ +0x60");
static_assert(offsetof(TradeDescription, _priceCap) == 0x64, "TradeDescription::_priceCap @ +0x64");
static_assert(offsetof(TradeDescription, _sicknessAmount) == 0x68, "TradeDescription::_sicknessAmount @ +0x68");
static_assert(offsetof(TradeDescription, _sicknessTimeInMonths) == 0x6c, "TradeDescription::_sicknessTimeInMonths @ +0x6c");
