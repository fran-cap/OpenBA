#pragma once
// ============================================================================
// src/game/Component/AmbientEnvironmentDescription.h — ENDGAME B.3.8 K1 (B38_RULES.md D1-D5, §2.2)
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §6):
//   reflected "AmbientEnvironmentDescription", parent "Description", ti 0x1401c3f08
//   create 0x140001f40: MemAlloc(0x38, 8), `??0External@Object@@`, the
//   Description stamp, the leaf stamp, the field defaults. createInPlace 0x140001fb0.
//   table 0x140158bf0, 10 slots:
//     [0] 0x140002100 scalar deleting dtor   [1] 0x140001f30 TypeName   [2] 0x140001f20 Type
//     [3] 0x140043a70 GetFileVersion = 2   [4] External::Compile (import, inherited)
//     [5] 0x140043950 Serialize (`MOV AL,1`)
//     [6] 0x140002010 Reconstruct   [7] 0x140001640 Destruct (shared)
//     [8] 0x140002070 GetComponentType: AmbientEnvironmentComponent's TypeId
//     [9] 0x140001fb0 Construct (folded with createInPlace)
//   sizeof 0x38.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Component/Description.h"
#include "Object/exe_type.h"
#include "collection/Collection.h"   // Collection::Array (the +0x20 biome entries)

class ComponentDescription;             // Component/ComponentDescription.h
namespace Audio { class SoundEffect; }

// One biome entry (0x28; Serialize 0x140043950: the enum through the uint door,
// the ExternalPtr<ComponentDescription> (0x140002340), two ExternalPtr<SoundEffect>
// (0x1400019a0), the int; resize 0x140002540 defaults the int to 1). No key
// names: the names say what AmbientEnvironmentComponent::Update does with them.
struct AmbientEnvironmentBiome {
    uint32_t              terrainMask;   // +0x00 bit per terrain type the biome covers
    uint8_t               _pad04[4];     // +0x04
    ComponentDescription* entityFilter;  // +0x08 the occupant's description to match (null = any)
    Audio::SoundEffect*   daySound;      // +0x10
    Audio::SoundEffect*   nightSound;    // +0x18
    int32_t               weight;        // +0x20
    uint8_t               _pad24[4];     // +0x24
};
static_assert(sizeof(AmbientEnvironmentBiome) == 0x28, "AmbientEnvironmentBiome stride 0x28 (0x140002540)");
static_assert(offsetof(AmbientEnvironmentBiome, terrainMask) == 0x0, "AmbientEnvironmentBiome::terrainMask");
static_assert(offsetof(AmbientEnvironmentBiome, _pad04) == 0x4, "AmbientEnvironmentBiome::_pad04");
static_assert(offsetof(AmbientEnvironmentBiome, _pad24) == 0x24, "AmbientEnvironmentBiome::_pad24");
static_assert(offsetof(AmbientEnvironmentBiome, entityFilter) == 0x08, "AmbientEnvironmentBiome::entityFilter @ +0x08");
static_assert(offsetof(AmbientEnvironmentBiome, daySound) == 0x10, "AmbientEnvironmentBiome::daySound @ +0x10");
static_assert(offsetof(AmbientEnvironmentBiome, nightSound) == 0x18, "AmbientEnvironmentBiome::nightSound @ +0x18");
static_assert(offsetof(AmbientEnvironmentBiome, weight) == 0x20, "AmbientEnvironmentBiome::weight @ +0x20");

class AmbientEnvironmentDescription final : public Description {
    DECLARE_EXE_TYPE(AmbientEnvironmentDescription)

public:
    AmbientEnvironmentDescription();

    ~AmbientEnvironmentDescription() override;                             // [0] 0x140002100
    int32_t           GetFileVersion() const override { return 2; }    // [3] 0x140043a70
    bool              Serialize(IO::Stream& stream) override;                    // [5] 0x140043950
    Object::External* Reconstruct() override;                              // [6] 0x140002010
    void              Destruct() override;                                 // [7] 0x140001640
    Object::Type*     GetComponentType(Object::Type* ret) const override;  // [8] 0x140002070
    virtual Object::External* Construct();                                 // [9] 0x140001fb0

    // ---- fields (PUBLIC: the components read them at raw offsets today) ----
    // +0x20 Array<AmbientEnvironmentBiome> {data, count +0x28, capacity +0x2c} (H5, typed:
    // the ctor inlined in create 0x140001f40 zeroes +0x20 / +0x28 as qwords, the dtor
    // 0x140002100 MemFrees +0x20 and zeroes both qwords before ~Description; no element
    // destroy). Resized by 0x140002540 (AmbientArrayResize_Reimpl).
    Collection::Array<AmbientEnvironmentBiome> _entries;
    float _minTemp;                                 // +0x30
    float _maxTemp;                                 // +0x34
};

static_assert(sizeof(AmbientEnvironmentDescription) == 0x38, "AmbientEnvironmentDescription is 0x38 (create 0x140001f40 MemAlloc(0x38))");
static_assert(alignof(AmbientEnvironmentDescription) == 8, "AmbientEnvironmentDescription alignment");
static_assert(offsetof(AmbientEnvironmentDescription, _entries) == 0x20, "AmbientEnvironmentDescription::_entries @ +0x20");
static_assert(offsetof(AmbientEnvironmentDescription, _entries._size) == 0x28, "AmbientEnvironmentDescription entry count @ +0x28");
static_assert(offsetof(AmbientEnvironmentDescription, _entries._capacity) == 0x2c, "AmbientEnvironmentDescription entry capacity @ +0x2c");
static_assert(offsetof(AmbientEnvironmentDescription, _minTemp) == 0x30, "AmbientEnvironmentDescription::_minTemp @ +0x30");
static_assert(offsetof(AmbientEnvironmentDescription, _maxTemp) == 0x34, "AmbientEnvironmentDescription::_maxTemp @ +0x34");
