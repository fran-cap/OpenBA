#pragma once
// ============================================================================
// src/game/Component/CitizenDescription.h — ENDGAME B.3.8 K1 (B38_RULES.md D1-D5, §2.2)
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §6):
//   reflected "CitizenDescription", parent "Description", ti 0x1401c7190
//   create 0x140030170: MemAlloc(0x148, 8), `??0External@Object@@`, the
//   Description stamp, the leaf stamp, the field defaults. createInPlace 0x1400301b0.
//   table 0x140162600, 10 slots:
//     [0] 0x1400301f0 scalar deleting dtor   [1] 0x140030160 TypeName   [2] 0x140030150 Type
//     [3] 0x140114e00 GetFileVersion = 17   [4] External::Compile (import, inherited)
//     [5] 0x140114900 Serialize (`MOV AL,1`)
//     [6] 0x1400301c0 Reconstruct   [7] 0x140001640 Destruct (shared)
//     [8] 0x1400301e0 GetComponentType: CitizenComponent's TypeId
//     [9] 0x1400301b0 Construct (folded with createInPlace)
//   slot 0: the tree's hand-built slot 0 never ran ~External; the compiler chain now does (B38 D2, behaviour)
//   sizeof 0x148.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Component/Description.h"
#include "Object/exe_type.h"
#include "collection/Collection.h"   // Collection::Array (_refs)

class Profession;
namespace Graphics { class GraphicsMesh; }   // graphics/GraphicsMesh.h (the _refs entries)

// The mesh table grows through exe 0x140001bb0 (stride 8, align 8, the new tail ZEROED,
// no element destroy) and is freed by the inlined ~Array (dtor 0x140030217: MemFree, then
// +0x138 = +0x140 = 0, no element release): kSpec_PtrZero (Entity/EntityList.h).
namespace rtcoll { extern const ArraySpec kSpec_PtrZero; }   // 0x180008d60

class CitizenDescription final : public Description {
    DECLARE_EXE_TYPE(CitizenDescription)

public:
    CitizenDescription();

    ~CitizenDescription() override;                                        // [0] 0x1400301f0
    int32_t           GetFileVersion() const override { return 17; }    // [3] 0x140114e00
    bool              Serialize(IO::Stream& stream) override;                    // [5] 0x140114900
    Object::External* Reconstruct() override;                              // [6] 0x1400301c0
    void              Destruct() override;                                 // [7] 0x140001640
    Object::Type*     GetComponentType(Object::Type* ret) const override;  // [8] 0x1400301e0
    virtual Object::External* Construct();                                 // [9] 0x1400301b0

    // ---- fields (PUBLIC: the components read them at raw offsets today) ----
    // B.4 W0-H1: every type from the ctor 0x140114e70 (MOV dword = float bit
    // pattern or int) and the serializer 0x140114900 (float / int overloads; the
    // serializer passes no names). Named where retail's readers say what the
    // field does (CitizenComponent::Update 0x140112e70, the happiness update
    // 0x140112ae0, the finders 0x140111590 / 0x1401116f0); the rest keep the
    // typed guess names `_fNN` / `_iNN`. +0x94 / +0x98 / +0x9c / +0x12c are
    // written by the ctor and never serialized.
    Profession* _professions[3];                // +0x20 ExternalPtr<Profession> x3 (0x140018020 serialize):
                                                //       [0] the school's (TryEnrolInSchool 0x140111840),
                                                //       [1] CitizenComponent+0x230's (0x1401116f0),
                                                //       [2] the care building's (0x140111590)
    float _f38;                                 // +0x38
    float _f3c;                                 // +0x3c
    float agingRate;                            // +0x40 age += dt * agingRate (Update)
    float adultAge;                             // +0x44 age >= adultAge: adult (Update, PostLifeEvent)
    float marriageAge;                          // +0x48 age >= marriageAge: the spouse search (Update)
    float _f4c;                                 // +0x4c
    float minPregnancyAge;                      // +0x50 a pregnancy starts only in [min, max] (Update)
    float maxPregnancyAge;                      // +0x54
    float pregnancyTime;                        // +0x58 CitizenComponent::pregnancyTimer = pregnancyTime (Update)
    float _f5c;                                 // +0x5c after a birth: timer = -(rand * _f60 + _f5c)
    float _f60;                                 // +0x60
    float _f64;                                 // +0x64 childbirth risk at full health (Update)
    float _f68;                                 // +0x68 childbirth risk at zero health (Update)
    float _f6c;                                 // +0x6c
    float maxHealth;                            // +0x70 health / maxHealth (Update)
    float _f74;                                 // +0x74
    float foodInterval;                         // +0x78 ConsumeFood each Time(foodInterval * k) (Update)
    float _f7c;                                 // +0x7c
    float _f80;                                 // +0x80
    float _f84;                                 // +0x84
    float _f88;                                 // +0x88
    float _f8c;                                 // +0x8c
    float _f90;                                 // +0x90
    float _f94;                                 // +0x94
    float _f98;                                 // +0x98
    float _f9c;                                 // +0x9c
    float happinessInterval;                    // +0xa0 the happiness update each Time(happinessInterval * k)
    float maxHappiness;                         // +0xa4 happiness is clamped to [0, maxHappiness]
    int32_t _ia8;                               // +0xa8 compared with CitizenComponent::consumedTypes._size
    int32_t _iac;                               // +0xac compared with CitizenComponent::consumedTypes._size
    int32_t _ib0;                               // +0xb0 compared with CitizenComponent::field_0x84
    float _fb4;                                 // +0xb4
    float _fb8;                                 // +0xb8
    int32_t _ibc;                               // +0xbc
    float hungerRate;                           // +0xc0 hunger += dt * hungerRate / k (Update)
    float _fc4;                                 // +0xc4
    float _fc8;                                 // +0xc8
    float _fcc;                                 // +0xcc base of the hungry-icon threshold (Update)
    float maxHunger;                            // +0xd0 hunger >= maxHunger -> HandleLifeEvent(0)
    float _fd4;                                 // +0xd4
    float _fd8;                                 // +0xd8
    float coldRate[3];                          // +0xdc float[3], bulk-serialized; each 10.0; indexed by
                                                //       CitizenComponent::clothingLevel (Update)
    float _fe8;                                 // +0xe8
    float _fec;                                 // +0xec base of the cold-icon threshold (Update)
    float maxColdness;                          // +0xf0 coldness >= maxColdness -> HandleLifeEvent(1)
    float warmTemperature;                      // +0xf4 Weather+0x18 >= warmTemperature: coldness = 0
    int32_t _if8;                               // +0xf8
    int32_t _ifc;                               // +0xfc
    int32_t _i100;                              // +0x100
    int32_t _i104;                              // +0x104
    int32_t _i108;                              // +0x108
    float _f10c;                                // +0x10c
    int32_t _i110;                              // +0x110
    float _f114;                                // +0x114
    int32_t maxEducation;                       // +0x118 education is raised to it (Update)
    float _f11c;                                // +0x11c
    float _f120;                                // +0x120
    float _f124;                                // +0x124
    float _f128;                                // +0x128
    float _f12c;                                // +0x12c
    float _f130;                                // +0x130
    uint8_t _pad134[4];                         // +0x134 never written by the ctor
    // +0x138 Array<ExternalPtr<GraphicsMesh>> (count +0x140, capacity +0x144; entries are
    // non-owning; Serialize streams it through 0x14001bcc0, the GraphicsMesh ExternalPtr
    // array; ShowTool 0x1401173b0 hands an entry to MeshInstance::SetMesh)
    using MeshTable = Collection::Array<Graphics::GraphicsMesh*, Collection::DefaultAllocator,
                                        Collection::ExplicitSpec<&rtcoll::kSpec_PtrZero>>;
    MeshTable _refs;
};

static_assert(sizeof(CitizenDescription) == 0x148, "CitizenDescription is 0x148 (create 0x140030170 MemAlloc(0x148))");
static_assert(alignof(CitizenDescription) == 8, "CitizenDescription alignment");
static_assert(offsetof(CitizenDescription, _professions) == 0x20, "CitizenDescription::_professions @ +0x20");
static_assert(offsetof(CitizenDescription, _f38) == 0x38, "CitizenDescription::_f38 @ +0x38");
static_assert(offsetof(CitizenDescription, _f3c) == 0x3c, "CitizenDescription::_f3c @ +0x3c");
static_assert(offsetof(CitizenDescription, agingRate) == 0x40, "CitizenDescription::agingRate @ +0x40");
static_assert(offsetof(CitizenDescription, adultAge) == 0x44, "CitizenDescription::adultAge @ +0x44");
static_assert(offsetof(CitizenDescription, marriageAge) == 0x48, "CitizenDescription::marriageAge @ +0x48");
static_assert(offsetof(CitizenDescription, _f4c) == 0x4c, "CitizenDescription::_f4c @ +0x4c");
static_assert(offsetof(CitizenDescription, minPregnancyAge) == 0x50, "CitizenDescription::minPregnancyAge @ +0x50");
static_assert(offsetof(CitizenDescription, maxPregnancyAge) == 0x54, "CitizenDescription::maxPregnancyAge @ +0x54");
static_assert(offsetof(CitizenDescription, pregnancyTime) == 0x58, "CitizenDescription::pregnancyTime @ +0x58");
static_assert(offsetof(CitizenDescription, _f5c) == 0x5c, "CitizenDescription::_f5c @ +0x5c");
static_assert(offsetof(CitizenDescription, _f60) == 0x60, "CitizenDescription::_f60 @ +0x60");
static_assert(offsetof(CitizenDescription, _f64) == 0x64, "CitizenDescription::_f64 @ +0x64");
static_assert(offsetof(CitizenDescription, _f68) == 0x68, "CitizenDescription::_f68 @ +0x68");
static_assert(offsetof(CitizenDescription, _f6c) == 0x6c, "CitizenDescription::_f6c @ +0x6c");
static_assert(offsetof(CitizenDescription, maxHealth) == 0x70, "CitizenDescription::maxHealth @ +0x70");
static_assert(offsetof(CitizenDescription, _f74) == 0x74, "CitizenDescription::_f74 @ +0x74");
static_assert(offsetof(CitizenDescription, foodInterval) == 0x78, "CitizenDescription::foodInterval @ +0x78");
static_assert(offsetof(CitizenDescription, _f7c) == 0x7c, "CitizenDescription::_f7c @ +0x7c");
static_assert(offsetof(CitizenDescription, _f80) == 0x80, "CitizenDescription::_f80 @ +0x80");
static_assert(offsetof(CitizenDescription, _f84) == 0x84, "CitizenDescription::_f84 @ +0x84");
static_assert(offsetof(CitizenDescription, _f88) == 0x88, "CitizenDescription::_f88 @ +0x88");
static_assert(offsetof(CitizenDescription, _f8c) == 0x8c, "CitizenDescription::_f8c @ +0x8c");
static_assert(offsetof(CitizenDescription, _f90) == 0x90, "CitizenDescription::_f90 @ +0x90");
static_assert(offsetof(CitizenDescription, _f94) == 0x94, "CitizenDescription::_f94 @ +0x94");
static_assert(offsetof(CitizenDescription, _f98) == 0x98, "CitizenDescription::_f98 @ +0x98");
static_assert(offsetof(CitizenDescription, _f9c) == 0x9c, "CitizenDescription::_f9c @ +0x9c");
static_assert(offsetof(CitizenDescription, happinessInterval) == 0xa0, "CitizenDescription::happinessInterval @ +0xa0");
static_assert(offsetof(CitizenDescription, maxHappiness) == 0xa4, "CitizenDescription::maxHappiness @ +0xa4");
static_assert(offsetof(CitizenDescription, _ia8) == 0xa8, "CitizenDescription::_ia8 @ +0xa8");
static_assert(offsetof(CitizenDescription, _iac) == 0xac, "CitizenDescription::_iac @ +0xac");
static_assert(offsetof(CitizenDescription, _ib0) == 0xb0, "CitizenDescription::_ib0 @ +0xb0");
static_assert(offsetof(CitizenDescription, _fb4) == 0xb4, "CitizenDescription::_fb4 @ +0xb4");
static_assert(offsetof(CitizenDescription, _fb8) == 0xb8, "CitizenDescription::_fb8 @ +0xb8");
static_assert(offsetof(CitizenDescription, _ibc) == 0xbc, "CitizenDescription::_ibc @ +0xbc");
static_assert(offsetof(CitizenDescription, hungerRate) == 0xc0, "CitizenDescription::hungerRate @ +0xc0");
static_assert(offsetof(CitizenDescription, _fc4) == 0xc4, "CitizenDescription::_fc4 @ +0xc4");
static_assert(offsetof(CitizenDescription, _fc8) == 0xc8, "CitizenDescription::_fc8 @ +0xc8");
static_assert(offsetof(CitizenDescription, _fcc) == 0xcc, "CitizenDescription::_fcc @ +0xcc");
static_assert(offsetof(CitizenDescription, maxHunger) == 0xd0, "CitizenDescription::maxHunger @ +0xd0");
static_assert(offsetof(CitizenDescription, _fd4) == 0xd4, "CitizenDescription::_fd4 @ +0xd4");
static_assert(offsetof(CitizenDescription, _fd8) == 0xd8, "CitizenDescription::_fd8 @ +0xd8");
static_assert(offsetof(CitizenDescription, coldRate) == 0xdc, "CitizenDescription::coldRate @ +0xdc");
static_assert(offsetof(CitizenDescription, _fe8) == 0xe8, "CitizenDescription::_fe8 @ +0xe8");
static_assert(offsetof(CitizenDescription, _fec) == 0xec, "CitizenDescription::_fec @ +0xec");
static_assert(offsetof(CitizenDescription, maxColdness) == 0xf0, "CitizenDescription::maxColdness @ +0xf0");
static_assert(offsetof(CitizenDescription, warmTemperature) == 0xf4, "CitizenDescription::warmTemperature @ +0xf4");
static_assert(offsetof(CitizenDescription, _if8) == 0xf8, "CitizenDescription::_if8 @ +0xf8");
static_assert(offsetof(CitizenDescription, _ifc) == 0xfc, "CitizenDescription::_ifc @ +0xfc");
static_assert(offsetof(CitizenDescription, _i100) == 0x100, "CitizenDescription::_i100 @ +0x100");
static_assert(offsetof(CitizenDescription, _i104) == 0x104, "CitizenDescription::_i104 @ +0x104");
static_assert(offsetof(CitizenDescription, _i108) == 0x108, "CitizenDescription::_i108 @ +0x108");
static_assert(offsetof(CitizenDescription, _f10c) == 0x10c, "CitizenDescription::_f10c @ +0x10c");
static_assert(offsetof(CitizenDescription, _i110) == 0x110, "CitizenDescription::_i110 @ +0x110");
static_assert(offsetof(CitizenDescription, _f114) == 0x114, "CitizenDescription::_f114 @ +0x114");
static_assert(offsetof(CitizenDescription, maxEducation) == 0x118, "CitizenDescription::maxEducation @ +0x118");
static_assert(offsetof(CitizenDescription, _f11c) == 0x11c, "CitizenDescription::_f11c @ +0x11c");
static_assert(offsetof(CitizenDescription, _f120) == 0x120, "CitizenDescription::_f120 @ +0x120");
static_assert(offsetof(CitizenDescription, _f124) == 0x124, "CitizenDescription::_f124 @ +0x124");
static_assert(offsetof(CitizenDescription, _f128) == 0x128, "CitizenDescription::_f128 @ +0x128");
static_assert(offsetof(CitizenDescription, _f12c) == 0x12c, "CitizenDescription::_f12c @ +0x12c");
static_assert(offsetof(CitizenDescription, _f130) == 0x130, "CitizenDescription::_f130 @ +0x130");
static_assert(offsetof(CitizenDescription, _pad134) == 0x134, "CitizenDescription::_pad134 @ +0x134");
static_assert(offsetof(CitizenDescription, _refs) == 0x138, "CitizenDescription::_refs @ +0x138");
static_assert(offsetof(CitizenDescription, _refs) + offsetof(CitizenDescription::MeshTable, _size) == 0x140, "CitizenDescription::_refs count @ +0x140");
static_assert(offsetof(CitizenDescription, _refs) + offsetof(CitizenDescription::MeshTable, _capacity) == 0x144, "CitizenDescription::_refs capacity @ +0x144");

// The cold rate CitizenComponent::Update (0x140112e70) multiplies by:
// `coldRate[clothingLevel]` (0x14011305d MOVSXD RAX,[RDI+0x1e4];
// 0x140113068 MULSS XMM0,[RCX+RAX*4+0xdc]).
inline float CitizenColdRate(const float* coldRates, int32_t clothingLevel) {
    // NOT RETAIL (review F-162): retail reads coldRate[3] with no bound. clothingLevel is
    // restored from the save (citizencomponent_serialize.cpp), and a level outside 0..2 reads
    // past the array (undefined behaviour). Such a level reads level 0's rate, the level a
    // citizen falls back to when its clothing wears out. The game writes only 0, 1 and 2.
    if (clothingLevel < 0 || clothingLevel > 2)
        return coldRates[0];
    return coldRates[clothingLevel];
}
