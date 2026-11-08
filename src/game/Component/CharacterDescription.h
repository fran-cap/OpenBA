#pragma once
// ============================================================================
// src/game/Component/CharacterDescription.h — ENDGAME B.3.8 K1 (B38_RULES.md D1-D5, §2.2)
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §6):
//   reflected "CharacterDescription", parent "Description", ti 0x1401c70b0
//   create 0x14002f9c0: MemAlloc(0x48, 8), `??0External@Object@@`, the
//   Description stamp, the leaf stamp, the field defaults. createInPlace 0x14002fa00.
//   table 0x1401623f0, 10 slots:
//     [0] 0x14002fa40 scalar deleting dtor   [1] 0x14002f9b0 TypeName   [2] 0x14002f9a0 Type
//     [3] 0x140109ec0 GetFileVersion = 6   [4] External::Compile (import, inherited)
//     [5] 0x140109dd0 Serialize (`MOV AL,1`)
//     [6] 0x14002fa10 Reconstruct   [7] 0x140001640 Destruct (shared)
//     [8] 0x14002fa30 GetComponentType: CharacterComponent's TypeId
//     [9] 0x14002fa00 Construct (folded with createInPlace)
//   slot 0: the tree's hand-built slot 0 never ran ~External; the compiler chain now does (B38 D2, behaviour)
//   sizeof 0x48.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Component/Description.h"
#include "Object/exe_type.h"

class CharacterDescription final : public Description {
    DECLARE_EXE_TYPE(CharacterDescription)

public:
    CharacterDescription();

    ~CharacterDescription() override = default;                            // [0] 0x14002fa40
    int32_t           GetFileVersion() const override { return 6; }    // [3] 0x140109ec0
    bool              Serialize(IO::Stream& stream) override;                    // [5] 0x140109dd0
    Object::External* Reconstruct() override;                              // [6] 0x14002fa10
    void              Destruct() override;                                 // [7] 0x140001640
    Object::Type*     GetComponentType(Object::Type* ret) const override;  // [8] 0x14002fa30
    virtual Object::External* Construct();                                 // [9] 0x14002fa00

    // ---- fields (PUBLIC: the components read them at raw offsets today) ----
    float _radius;                          // +0x20
    float _headingOffset;                                 // +0x24 high dword of the ctor's 8-byte +0x20 store
    float _turnFilter;                           // +0x28 0.85
    float _defaultAnimationBlendTime;                          // +0x2c 0.1
    float _slowSpeed;                                 // +0x30
    float _fastSpeed;                                 // +0x34
    float _fastestSpeed;                                 // +0x38
    bool _forceHeight;                                  // +0x3c
    uint8_t _pad3d[3];                          // +0x3d never written by the ctor
    float _height;                                 // +0x40
    float _minHeight;                                 // +0x44
};

static_assert(sizeof(CharacterDescription) == 0x48, "CharacterDescription is 0x48 (create 0x14002f9c0 MemAlloc(0x48))");
static_assert(offsetof(CharacterDescription, _pad3d) == 0x3D, "CharacterDescription::_pad3d");
static_assert(alignof(CharacterDescription) == 8, "CharacterDescription alignment");
static_assert(offsetof(CharacterDescription, _radius) == 0x20, "CharacterDescription::_radius @ +0x20");
static_assert(offsetof(CharacterDescription, _headingOffset) == 0x24, "CharacterDescription::_headingOffset @ +0x24");
static_assert(offsetof(CharacterDescription, _turnFilter) == 0x28, "CharacterDescription::_turnFilter @ +0x28");
static_assert(offsetof(CharacterDescription, _defaultAnimationBlendTime) == 0x2c, "CharacterDescription::_defaultAnimationBlendTime @ +0x2c");
static_assert(offsetof(CharacterDescription, _slowSpeed) == 0x30, "CharacterDescription::_slowSpeed @ +0x30");
static_assert(offsetof(CharacterDescription, _fastSpeed) == 0x34, "CharacterDescription::_fastSpeed @ +0x34");
static_assert(offsetof(CharacterDescription, _fastestSpeed) == 0x38, "CharacterDescription::_fastestSpeed @ +0x38");
static_assert(offsetof(CharacterDescription, _forceHeight) == 0x3c, "CharacterDescription::_forceHeight @ +0x3c");
static_assert(offsetof(CharacterDescription, _height) == 0x40, "CharacterDescription::_height @ +0x40");
static_assert(offsetof(CharacterDescription, _minHeight) == 0x44, "CharacterDescription::_minHeight @ +0x44");
