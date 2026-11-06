#pragma once
// ============================================================================
// src/game/Component/RawMaterialDescription.h — ENDGAME B.3.8 K1 (B38_RULES.md D1-D5, §2.2)
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §6):
//   reflected "RawMaterialDescription", parent "Description", ti 0x1401c7cb8
//   create 0x1400372d0: MemAlloc(0x60, 8), `??0External@Object@@`, the
//   Description stamp, the leaf stamp, the field defaults. createInPlace 0x140037310.
//   table 0x1401641f0, 10 slots:
//     [0] 0x140037350 scalar deleting dtor   [1] 0x1400372c0 TypeName   [2] 0x1400372b0 Type
//     [3] 0x140129630 GetFileVersion = 8   [4] External::Compile (import, inherited)
//     [5] 0x140129520 Serialize (`MOV AL,1`)
//     [6] 0x140037320 Reconstruct   [7] 0x140001640 Destruct (shared)
//     [8] 0x140037340 GetComponentType: RawMaterialComponent's TypeId
//     [9] 0x140037310 Construct (folded with createInPlace)
//   field names are the .rsc names (the retired engine/types POD gave them)
//   sizeof 0x60.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Component/Description.h"
#include "Object/exe_type.h"
#include "system/String.h"
namespace UI { class SpriteSheet; class StringTable; }   // B4 H4 (DLG-e)

// (Phase D.2: moved here from engine/types/RawMaterialDescription.h; no reader yet.)
// RawMaterialFlags bitfield — field +0x5C, tested via bitwise AND.
//
// [CORRECTED 2026-07-19 — binary-verified] The old table here naively followed the
// ToolKit .rsc enum listing order (Edible=bit0, Fuel=bit5, ...) and was WRONG.
// Ground truth recovered from the exe:
//   * Eat loop (0x1401152e0) ORs flag bits {0,1,2,4} into the citizen's diet mask
//     (+0x54) — those are the four diet groups; bit 3 is edible-without-group (ale).
//   * Storage_GetResourceCount (0x14012a9d0) sums rows with bit 5  -> 0x20 = EDIBLE.
//   * Storage_GetResourceCount2 (0x14012ab00) + the residence heat tick
//     (0x140129970) key on bit 6                                   -> 0x40 = FUEL.
//   * Entity_impl_ef40 (0x14008ef40) category map: cat0->bit5, cat1->bit6,
//     cat2->bit9, cat3->bit13, cat4->bit14.
//   * Start-resource seed masks (0x1400a1f00): 0x20 = starting food,
//     0x40 = starting firewood.
namespace RawMaterialFlags {
    // Diet groups (bits 0/1/2/4; exact name->bit order among the four unverified):
    constexpr uint32_t DietGroup0 = 0x00000001;
    constexpr uint32_t DietGroup1 = 0x00000002;
    constexpr uint32_t DietGroup2 = 0x00000004;
    constexpr uint32_t EdibleNoGroup = 0x00000008;  // bit 3: edible w/o diet group (ale)
    constexpr uint32_t DietGroup3 = 0x00000010;
    constexpr uint32_t Edible     = 0x00000020;  // bit 5 — EDIBLE master flag (verified)
    constexpr uint32_t Fuel       = 0x00000040;  // bit 6 — FIREWOOD (verified)
    // Bits 7+ — read from the installed game's templates (RawMaterialDescription::_flags dumped in game,
    // 2026-11-06, MAPOVERLAYS.md §2) and consistent with the ef40 category map above (cat2 = bit 9 = tools,
    // cat3 = bit 13 = herbs, cat4 = bit 14 = coats). The old toolkit-order guesses (Wood 0x80, Stone 0x100,
    // Iron 0x200, ...) were wrong. Firewood is 0xc0 and coal 0x140: bits 7 and 8 are set on those fuels only;
    // their meaning is not known.
    constexpr uint32_t FuelBit7   = 0x00000080;  // bit 7  (firewood 0xc0)
    constexpr uint32_t FuelBit8   = 0x00000100;  // bit 8  (coal 0x140)
    constexpr uint32_t Tool       = 0x00000200;  // bit 9  tool, steel tool (ef40 cat2)
    constexpr uint32_t Wood       = 0x00000400;  // bit 10 logs
    constexpr uint32_t Stone      = 0x00000800;  // bit 11
    constexpr uint32_t Iron       = 0x00001000;  // bit 12
    constexpr uint32_t Health     = 0x00002000;  // bit 13 herbs (ef40 cat3)
    constexpr uint32_t Clothing   = 0x00004000;  // bit 14 coats; the winter coat is 0xc000 (ef40 cat4)
    constexpr uint32_t Textile    = 0x00010000;  // bit 16 leather, wool
    constexpr uint32_t Alcohol    = 0x00020000;  // bit 17 ale
    // Custom flags continue upward (modding).
}

class RawMaterialDescription final : public Description {
    DECLARE_EXE_TYPE(RawMaterialDescription)

public:
    RawMaterialDescription();

    ~RawMaterialDescription() override = default;                          // [0] 0x140037350
    int32_t           GetFileVersion() const override { return 8; }    // [3] 0x140129630
    bool              Serialize(IO::Stream& stream) override;                    // [5] 0x140129520
    Object::External* Reconstruct() override;                              // [6] 0x140037320
    void              Destruct() override;                                 // [7] 0x140001640
    Object::Type*     GetComponentType(Object::Type* ret) const override;  // [8] 0x140037340
    virtual Object::External* Construct();                                 // [9] 0x140037310

    uint32_t GetPickupCategory() const;   // 0x140129480: the Pickup work's category bits from _flags

    // ---- fields (PUBLIC: the components read them at raw offsets today) ----
    UI::SpriteSheet* _spriteSheet;              // +0x20 SpriteSheet* (.rsc _spriteSheet)
    UI::StringTable* _stringTable;              // +0x28 StringTable* (.rsc _stringTable)
    System::String _name;                       // +0x30 .rsc _name (full-size sprite name)
    System::String _nameSmall;                  // +0x38 .rsc _nameSmall
    int32_t _lowCreateCount;                    // +0x40
    int32_t _highCreateCount;                   // +0x44
    int32_t _weight;                            // +0x48
    int32_t _createChance;                      // +0x4c
    float _carryScale;                          // +0x50 0.33
    int32_t _i54;                               // +0x54 the POD's _pad54: zeroed, serialized as an int
    bool _unknown58;                            // +0x58
    uint8_t _pad59[3];                          // +0x59 never written by the ctor
    uint32_t _flags;                            // +0x5c RawMaterialFlags bitfield (.rsc _flags)
};

static_assert(sizeof(RawMaterialDescription) == 0x60, "RawMaterialDescription is 0x60 (create 0x1400372d0 MemAlloc(0x60))");
static_assert(offsetof(RawMaterialDescription, _pad59) == 0x59, "RawMaterialDescription::_pad59");
static_assert(alignof(RawMaterialDescription) == 8, "RawMaterialDescription alignment");
static_assert(offsetof(RawMaterialDescription, _spriteSheet) == 0x20, "RawMaterialDescription::_spriteSheet @ +0x20");
static_assert(offsetof(RawMaterialDescription, _stringTable) == 0x28, "RawMaterialDescription::_stringTable @ +0x28");
static_assert(offsetof(RawMaterialDescription, _name) == 0x30, "RawMaterialDescription::_name @ +0x30");
static_assert(offsetof(RawMaterialDescription, _nameSmall) == 0x38, "RawMaterialDescription::_nameSmall @ +0x38");
static_assert(offsetof(RawMaterialDescription, _lowCreateCount) == 0x40, "RawMaterialDescription::_lowCreateCount @ +0x40");
static_assert(offsetof(RawMaterialDescription, _highCreateCount) == 0x44, "RawMaterialDescription::_highCreateCount @ +0x44");
static_assert(offsetof(RawMaterialDescription, _weight) == 0x48, "RawMaterialDescription::_weight @ +0x48");
static_assert(offsetof(RawMaterialDescription, _createChance) == 0x4c, "RawMaterialDescription::_createChance @ +0x4c");
static_assert(offsetof(RawMaterialDescription, _carryScale) == 0x50, "RawMaterialDescription::_carryScale @ +0x50");
static_assert(offsetof(RawMaterialDescription, _i54) == 0x54, "RawMaterialDescription::_i54 @ +0x54");
static_assert(offsetof(RawMaterialDescription, _unknown58) == 0x58, "RawMaterialDescription::_unknown58 @ +0x58");
static_assert(offsetof(RawMaterialDescription, _flags) == 0x5c, "RawMaterialDescription::_flags @ +0x5c");
