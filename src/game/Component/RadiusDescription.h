#pragma once
// ============================================================================
// src/game/Component/RadiusDescription.h — ENDGAME B.3.8 K1 (B38_RULES.md D1-D5, §2.2)
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §6):
//   reflected "RadiusDescription", parent "Description", ti 0x1401c4870
//   create 0x14000b9e0: MemAlloc(0x30, 8), `??0External@Object@@`, the
//   Description stamp, the leaf stamp, the field defaults. createInPlace 0x14000ba50.
//   table 0x14015a580, 10 slots:
//     [0] 0x14000bb10 scalar deleting dtor   [1] 0x14000b9d0 TypeName   [2] 0x14000b9c0 Type
//     [3] 0x140055590 GetFileVersion = 1   [4] External::Compile (import, inherited)
//     [5] 0x140055530 Serialize (`MOV AL,1`)
//     [6] 0x14000bab0 Reconstruct   [7] 0x140001640 Destruct (shared)
//     [8] 0x140004950 GetComponentType: 0 (data-only: shared body 0x140004950 `MOV dword [RDX],0`)
//     [9] 0x14000ba50 Construct (folded with createInPlace)
//   sizeof 0x30.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Component/Description.h"
#include "Object/exe_type.h"

namespace Graphics { class MaterialInstance; }   // graphics/Material.h

class RadiusDescription final : public Description {
    DECLARE_EXE_TYPE(RadiusDescription)

public:
    RadiusDescription();

    ~RadiusDescription() override = default;                               // [0] 0x14000bb10
    int32_t           GetFileVersion() const override { return 1; }    // [3] 0x140055590
    bool              Serialize(IO::Stream& stream) override;                    // [5] 0x140055530
    Object::External* Reconstruct() override;                              // [6] 0x14000bab0
    void              Destruct() override;                                 // [7] 0x140001640
    Object::Type*     GetComponentType(Object::Type* ret) const override;  // [8] 0x140004950
    virtual Object::External* Construct();                                 // [9] 0x14000ba50
    int                AddHighlight(int cell) const;                       // 0x140055430 the radius outline round `cell`, -1 without a material

    // ---- fields (PUBLIC: the components read them at raw offsets today) ----
    Graphics::MaterialInstance* _material;      // +0x20 ExternalPtr<Graphics::MaterialInstance> (0x140007140: type cell 0x140151350)
    int32_t _radius;                            // +0x28
    uint8_t _pad2c[4];                          // +0x2c never written by the ctor
};

static_assert(sizeof(RadiusDescription) == 0x30, "RadiusDescription is 0x30 (create 0x14000b9e0 MemAlloc(0x30))");
static_assert(offsetof(RadiusDescription, _pad2c) == 0x2C, "RadiusDescription::_pad2c");
static_assert(alignof(RadiusDescription) == 8, "RadiusDescription alignment");
static_assert(offsetof(RadiusDescription, _material) == 0x20, "RadiusDescription::_material @ +0x20");
static_assert(offsetof(RadiusDescription, _radius) == 0x28, "RadiusDescription::_radius @ +0x28");
