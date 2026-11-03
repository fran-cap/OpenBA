// ============================================================================
// tests/unit/review_component_ub_test.cpp -- review F-108, F-161, F-162, F-163
// ============================================================================
// Four component reads of save-supplied values:
//   F-108  DecalComponent::OnAdded (0x14011b560) indexes DecalDescription::_materials with
//          the saved decalState, sign-extended (MOVSXD), with no bound.
//   F-163  the same body converts decalScale * 255 to a 64-bit int (CVTTSS2SI R10) and keeps
//          the low byte; a 32-bit cast is undefined for a product outside int's range.
//   F-161  CemeteryComponent's state-2 enter shifts 1 by the cell's zone byte (SHL EAX,CL:
//          the count is masked to 5 bits by the CPU; in C++ a count >= 32 is undefined).
//   F-162  CitizenComponent::Update indexes CitizenDescription::coldRate[3] with the saved
//          clothingLevel, with no bound.
// The constexpr checks fail to compile while the conversion or the shift is undefined.
// ============================================================================
#include "unit.h"
#include "Component/DecalComponent.h"
#include "Component/CemeteryComponent.h"
#include "Component/CitizenDescription.h"

#include <cstdint>
#include <type_traits>

namespace {

// Distinct non-null pointers standing for MaterialInstance objects (never dereferenced).
Graphics::MaterialInstance* FakeMaterial(uintptr_t n) {
    return reinterpret_cast<Graphics::MaterialInstance*>(0x1000 + n * 0x10);
}

}  // namespace

// F-108: retail's field is a signed int (MOVSXD at 0x14011b683).
UNIT_TEST(review_component_ub_decal_state_is_signed) {
    CHECK((std::is_same<decltype(DecalComponent::decalState), int32_t>::value));
}

UNIT_TEST(review_component_ub_decal_state_in_range) {
    Graphics::MaterialInstance* materials[2] = {FakeMaterial(1), FakeMaterial(2)};
    CHECK(DecalStateMaterial(materials, 2, 0) == FakeMaterial(1));
    CHECK(DecalStateMaterial(materials, 2, 1) == FakeMaterial(2));
}

UNIT_TEST(review_component_ub_decal_state_out_of_range) {
    // A description with one material; the slots past it stand for whatever follows the
    // allocation. A saved decalState of 3 or -1 names no material: no decal (AddDecal
    // returns -1 for a null material, 0x1400b0dbb).
    Graphics::MaterialInstance* storage[5] = {FakeMaterial(9), FakeMaterial(1), FakeMaterial(7),
                                              FakeMaterial(7), FakeMaterial(7)};
    Graphics::MaterialInstance* const* materials = storage + 1;
    CHECK(DecalStateMaterial(materials, 1, 0) == FakeMaterial(1));
    CHECK(DecalStateMaterial(materials, 1, 3) == nullptr);
    CHECK(DecalStateMaterial(materials, 1, -1) == nullptr);
    CHECK(DecalStateMaterial(materials, 1, INT32_MIN) == nullptr);
    CHECK(DecalStateMaterial(materials, 0, 0) == nullptr);
    CHECK(DecalStateMaterial(nullptr, 0, 0) == nullptr);
}

// F-163: the low byte of the int64 conversion, as retail. A float outside int's range is a
// multiple of 256, so its low byte is 0; a NaN or a value outside int64's range converts to
// 0x8000000000000000 (CVTTSS2SI's indefinite integer), low byte 0.
UNIT_TEST(review_component_ub_decal_alpha_byte) {
    constexpr uint8_t opaque = DecalAlphaByte(1.0f);
    constexpr uint8_t half   = DecalAlphaByte(0.5f);
    constexpr uint8_t clear  = DecalAlphaByte(0.0f);
    constexpr uint8_t wrap   = DecalAlphaByte(2.0f);    // 510 -> 0xFE
    constexpr uint8_t neg    = DecalAlphaByte(-1.0f);   // -255 -> 0x01
    constexpr uint8_t big    = DecalAlphaByte(1.0e8f);  // 2.55e10, past int32
    constexpr uint8_t huge   = DecalAlphaByte(1.0e30f); // past int64
    CHECK_EQ(opaque, (uint8_t)0xFF);
    CHECK_EQ(half, (uint8_t)0x7F);
    CHECK_EQ(clear, (uint8_t)0x00);
    CHECK_EQ(wrap, (uint8_t)0xFE);
    CHECK_EQ(neg, (uint8_t)0x01);
    CHECK_EQ(big, (uint8_t)0x00);
    CHECK_EQ(huge, (uint8_t)0x00);
    volatile float nanScale = __builtin_nanf("");
    CHECK_EQ(DecalAlphaByte(nanScale), (uint8_t)0x00);
    volatile float infScale = __builtin_inff();
    CHECK_EQ(DecalAlphaByte(infScale), (uint8_t)0x00);
}

// F-161: zones 5..8 are walkable; a zone byte >= 32 shifts by (zone & 31), as SHL does.
UNIT_TEST(review_component_ub_cemetery_zone_shift) {
    constexpr bool z2   = CemeteryWalkableZone(2);
    constexpr bool z5   = CemeteryWalkableZone(5);
    constexpr bool z8   = CemeteryWalkableZone(8);
    constexpr bool z9   = CemeteryWalkableZone(9);
    constexpr bool z37  = CemeteryWalkableZone(37);    // 37 & 31 = 5
    constexpr bool z40  = CemeteryWalkableZone(40);    // 40 & 31 = 8
    constexpr bool z41  = CemeteryWalkableZone(41);    // 41 & 31 = 9
    constexpr bool z255 = CemeteryWalkableZone(255);   // 255 & 31 = 31
    CHECK(!z2);
    CHECK(z5);
    CHECK(z8);
    CHECK(!z9);
    CHECK(z37);
    CHECK(z40);
    CHECK(!z41);
    CHECK(!z255);
}

// F-162: levels 0..2 read their rate; any other saved level reads level 0's.
UNIT_TEST(review_component_ub_citizen_cold_rate) {
    // Three rates, then the floats that follow coldRate in CitizenDescription (+0xe8, ...).
    const float storage[6] = {-5.0f, 10.0f, 6.0f, 3.0f, 99.0f, 98.0f};
    const float* rates = storage + 1;
    CHECK_EQ(CitizenColdRate(rates, 0), 10.0f);
    CHECK_EQ(CitizenColdRate(rates, 1), 6.0f);
    CHECK_EQ(CitizenColdRate(rates, 2), 3.0f);
    CHECK_EQ(CitizenColdRate(rates, 3), 10.0f);
    CHECK_EQ(CitizenColdRate(rates, -1), 10.0f);
    CHECK_EQ(CitizenColdRate(rates, INT32_MAX), 10.0f);
}
