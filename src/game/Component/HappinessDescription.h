#pragma once
// ============================================================================
// src/game/Component/HappinessDescription.h — ENDGAME B.3.8 K1 (B38_RULES.md D1-D5, §2.2)
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §6):
//   reflected "HappinessDescription", parent "Description", ti 0x1401c42f8
//   create 0x140005c10: MemAlloc(0x30, 8), `??0External@Object@@`, the
//   Description stamp, the leaf stamp, the field defaults. createInPlace 0x140005c80.
//   table 0x140159660, 10 slots:
//     [0] 0x140005d50 scalar deleting dtor   [1] 0x140005c00 TypeName   [2] 0x140005bf0 Type
//     [3] 0x14004ad50 GetFileVersion = 3   [4] External::Compile (import, inherited)
//     [5] 0x14004acb0 Serialize (`MOV AL,1`)
//     [6] 0x140005ce0 Reconstruct   [7] 0x140001640 Destruct (shared)
//     [8] 0x140005d40 GetComponentType: HappinessComponent's TypeId
//     [9] 0x140005c80 Construct (folded with createInPlace)
//   sizeof 0x30.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Component/Description.h"
#include "Object/exe_type.h"

class HappinessDescription final : public Description {
    DECLARE_EXE_TYPE(HappinessDescription)

public:
    HappinessDescription();

    ~HappinessDescription() override = default;                            // [0] 0x140005d50
    int32_t           GetFileVersion() const override { return 3; }    // [3] 0x14004ad50
    bool              Serialize(IO::Stream& stream) override;                    // [5] 0x14004acb0
    Object::External* Reconstruct() override;                              // [6] 0x140005ce0
    void              Destruct() override;                                 // [7] 0x140001640
    Object::Type*     GetComponentType(Object::Type* ret) const override;  // [8] 0x140005d40
    virtual Object::External* Construct();                                 // [9] 0x140005c80

    // ---- fields (PUBLIC: the components read them at raw offsets today) ----
    uint32_t _happinessType;        // +0x20 enum: 0..4 the map influence kind, 5 = always active
    bool _needsWorkers;             // +0x24 IsActive 0x14004a550: a staffed, open workplace
    bool _needsGoods;               // +0x25 IsActive 0x14004a550: a stored good matching _goodsFlags
    uint8_t _pad26[2];              // +0x26 never written by the ctor
    uint32_t _goodsFlags;           // +0x28 RawMaterialDescription::_flags mask (serialized as an enum)
    int32_t _idleRange;                               // +0x2c
};

static_assert(sizeof(HappinessDescription) == 0x30, "HappinessDescription is 0x30 (create 0x140005c10 MemAlloc(0x30))");
static_assert(offsetof(HappinessDescription, _pad26) == 0x26, "HappinessDescription::_pad26");
static_assert(alignof(HappinessDescription) == 8, "HappinessDescription alignment");
static_assert(offsetof(HappinessDescription, _happinessType) == 0x20, "HappinessDescription::_happinessType @ +0x20");
static_assert(offsetof(HappinessDescription, _needsWorkers) == 0x24, "HappinessDescription::_needsWorkers @ +0x24");
static_assert(offsetof(HappinessDescription, _needsGoods) == 0x25, "HappinessDescription::_needsGoods @ +0x25");
static_assert(offsetof(HappinessDescription, _goodsFlags) == 0x28, "HappinessDescription::_goodsFlags @ +0x28");
static_assert(offsetof(HappinessDescription, _idleRange) == 0x2c, "HappinessDescription::_idleRange @ +0x2c");
