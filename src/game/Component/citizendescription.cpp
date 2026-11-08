// src/game/Component/citizendescription.cpp
// Clean reimplementation of CitizenDescription (Object::External "Description").
//
// CitizenDescription (0x148 bytes) — extends Object::External (0x20 base).
// Vtable at 0x140162600.  GetVersion = 17 (0x11).
//
// This is the field-heavy citizen tuning block (Template\Citizen.rsc). Layout +
// defaults come from ctor FUN_140114e70 @ 0x140114e70; serialize stream order
// from CitizenComponent_OnCreate (Serialize) @ 0x140114900.
//
// Special members:
//   +0x20,+0x28,+0x30  ExternalRef<Profession> (FUN_140018020, single ptr each)
//   +0xdc..+0xe7       float[3] bulk-serialized (count=3, slot 0x140151ea8)
//   +0x138 / +0x140    dynamic External* array (ptr / count): resize via
//                      FUN_140001bb0, element-serialize via FUN_14001bcc0
//
// Serialize stream order (EXACT, from 0x140114900):
//   float: +0x44, +0x6c,+0x70,+0x74,+0x78,+0x7c,+0x80,+0x84,+0x88,+0x8c,+0x90,
//          +0x38,+0x3c,+0x40, +0xc0, +0xd8;
//   floatArray3(+0xdc);
//   float: +0x48,+0x4c,+0x50,+0x54,+0x58,+0x5c,+0x60, +0xc4,+0xd4,+0xc8,+0xcc,+0xd0,
//          +0xe8,+0xec,+0xf0,+0xf4;
//   int +0x110;
//   int countTmp=+0x140 (serialize); if loading resize(+0x138,count); refArray(+0x138,count);
//   float +0x114; int +0x118; float +0x120,+0x11c,+0x128,+0x124,+0x130;
//   float +0xa4; int +0xa8,+0xac; float +0xa0; int +0xb0; float +0xb4,+0xb8; int +0xbc;
//   ExternalRef +0x20; ExternalRef +0x28; ExternalRef +0x30;
//   float +0x64,+0x68; int +0xf8,+0xfc,+0x100,+0x104,+0x108; float +0x10c.
//
// Decompiled from Application-x64.exe (read-only Ghidra recon).
//
// ENDGAME B.3.8 K1: a REAL class (CitizenDescription.h) with a compiler-generated vtable;
// the hand-built 11-slot table and its RegisterExeAddr(0x140162600) row are gone
// (nothing in the tree read that address back — B38_RULES D5).

#include "Component/CitizenDescription.h"
#include "Component/CitizenComponent.h"   // B.3.8 CB1a (TypeId)
#include "object/Internal.h"      // Object::detail::InternalMemAlloc / InternalMemFree
#include "io/Serializer.h"
#include "Object/factory_registration.h"
#include "object/External.h"
#include "object/TypeInfo.h"
#include <cstring>
#include <cstdint>
#include <new>
#include "system/MemoryAllocator.h"   // B3R: the allocator doors
#include "Game/exe_bodies.h"
#include "Game/Profession.h"   // B8 RT2: ::Object::SerializeExternal<Profession> (the downcast needs it complete)
#include "graphics/GraphicsMesh.h"   // B8 RT2: ::Object::SerializeExternalArray<Graphics::GraphicsMesh> (the downcast needs it complete)

namespace {
// The retail ctor stores raw IEEE bit patterns; this keeps them exact.
inline float FloatBits(uint32_t bits) { float f; std::memcpy(&f, &bits, sizeof f); return f; }
}  // namespace

// ctor (inlined into create 0x140030170 / createInPlace 0x1400301b0):
// `??0External@Object@@` (Description()), the stamps (the compiler's), then
// the defaults.
CitizenDescription::CitizenDescription()
    : Description(),
      maxAge(FloatBits(0x41700000)),
      maxAgeTolerance(FloatBits(0x40000000)),
      agingRate(FloatBits(0x3991a2b4)),
      adultAge(FloatBits(0x40000000)),
      marriageAge(FloatBits(0x40000000)),
      marriageRange(FloatBits(0x40400000)),
      minPregnancyAge(FloatBits(0x40000000)),
      maxPregnancyAge(FloatBits(0x41000000)),
      pregnancyTime(FloatBits(0x44610000)),
      postChildTerm(FloatBits(0x44e10000)),
      postChildTermTol(FloatBits(0x45a8c000)),
      childBirthHealthy(FloatBits(0x3c23d70a)),
      childBirthUnhealthy(FloatBits(0x3dcccccd)),
      health(FloatBits(0x40c00000)),
      maxHealth(FloatBits(0x41200000)),
      unhealthyLevel(FloatBits(0x41000000)),
      foodInterval(FloatBits(0x40400000)),
      diseaseChanceHealthyYears(FloatBits(0x3f800000)),
      diseaseChanceHealthyPopulation(FloatBits(0x3f800000)),
      diseaseChanceUnhealthyYears(FloatBits(0x3f800000)),
      diseaseChanceUnhealthyPopulation(FloatBits(0x3f800000)),
      diseaseChanceNomadTradeYears(FloatBits(0x3f800000)),
      diseaseChanceNomadTradePopulation(FloatBits(0x3f800000)),
      diseasePeriod(FloatBits(0x3f800000)),
      diseasePeriodTol(FloatBits(0x3f000000)),
      immunityLength(FloatBits(0x41400000)),
      happinessInterval(FloatBits(0x3e800000)),
      maxHappiness(FloatBits(0x41200000)),
      minFoodForHappiness(4),
      maxFoodForHappiness(12),
      workForHappiness(3),
      unhappyLevel(FloatBits(0x3f19999a)),
      depressedLevel(FloatBits(0x3e800000)),
      idleRange(20),
      hungerRate(FloatBits(0x42c80000)),
      foodLevel(FloatBits(0x42c80000)),
      hungerLevel(FloatBits(0x425c0000)),
      starvingLevel(FloatBits(0x42960000)),
      maxHunger(FloatBits(0x42c80000)),
      fuelLevel(FloatBits(0x41c80000)),
      retryInMonths(FloatBits(0x3dcccccd)),
      coldnessLevel(FloatBits(0x42480000)),
      freezingLevel(FloatBits(0x42960000)),
      maxColdness(FloatBits(0x42c80000)),
      warmTemperature(FloatBits(0x42400000)),
      maxWaterForFireSearch(60),
      minDistanceForWork(60),
      maxDistanceForWork(2000),
      timeUntilMaxDistanceForWorkInMonths(6),
      maxWalkInWinter(200),
      minTemperatureForWinterWalk(FloatBits(0x42340000)),
      toolQuality(100),
      toolPenalty(FloatBits(0x40a00000)),
      maxEducation(200),
      educationTimeLength(FloatBits(0x40800000)),
      educationWorkLength(FloatBits(0x40a00000)),
      medicateWorkLength(FloatBits(0x40a00000)),
      doctorChanceOfDeathScalar(FloatBits(0x40000000)),
      medicineEffectivenessInMonths(FloatBits(0x3c23d70a)),
      clothingLengthInMonths(FloatBits(0x42700000)) {
    _professions[0] = _professions[1] = _professions[2] = nullptr;
    coldRate[0] = coldRate[1] = coldRate[2] = FloatBits(0x41200000);
}

// dtor (slot 0 0x1400301f0 minus the stamps): the tree's slot 0 skipped ~External; the compiler chain now runs it (B38 D2 — this commit's behaviour change)
// The compiler then chains ~Description -> ~External.
// The body is empty: ~_tools frees the mesh table unconditionally and zeroes +0x138 /
// +0x140 (0x140030217..0x14003022d; the entries are non-owning, no element release).
CitizenDescription::~CitizenDescription() {
}

// [5] 0x140114900: stream order as retail.
bool CitizenDescription::Serialize(IO::Stream& stream) {
    IO::Serializer ser(stream, 0);
    ser.Serialize(adultAge, nullptr);
    ser.Serialize(health, nullptr);
    ser.Serialize(maxHealth, nullptr);
    ser.Serialize(unhealthyLevel, nullptr);
    ser.Serialize(foodInterval, nullptr);
    ser.Serialize(diseaseChanceHealthyYears, nullptr);
    ser.Serialize(diseaseChanceHealthyPopulation, nullptr);
    ser.Serialize(diseaseChanceUnhealthyYears, nullptr);
    ser.Serialize(diseaseChanceUnhealthyPopulation, nullptr);
    ser.Serialize(diseaseChanceNomadTradeYears, nullptr);
    ser.Serialize(diseaseChanceNomadTradePopulation, nullptr);
    ser.Serialize(maxAge, nullptr);
    ser.Serialize(maxAgeTolerance, nullptr);
    ser.Serialize(agingRate, nullptr);
    ser.Serialize(hungerRate, nullptr);
    ser.Serialize(retryInMonths, nullptr);
    ser.Serialize(3, coldRate, nullptr);   // float[3] bulk at +0xdc
    ser.Serialize(marriageAge, nullptr);
    ser.Serialize(marriageRange, nullptr);
    ser.Serialize(minPregnancyAge, nullptr);
    ser.Serialize(maxPregnancyAge, nullptr);
    ser.Serialize(pregnancyTime, nullptr);
    ser.Serialize(postChildTerm, nullptr);
    ser.Serialize(postChildTermTol, nullptr);
    ser.Serialize(foodLevel, nullptr);
    ser.Serialize(fuelLevel, nullptr);
    ser.Serialize(hungerLevel, nullptr);
    ser.Serialize(starvingLevel, nullptr);
    ser.Serialize(maxHunger, nullptr);
    ser.Serialize(coldnessLevel, nullptr);
    ser.Serialize(freezingLevel, nullptr);
    ser.Serialize(maxColdness, nullptr);
    ser.Serialize(warmTemperature, nullptr);
    ser.Serialize(toolQuality, nullptr);
    {
        // dynamic External* array (count at +0x140, data at +0x138)
        int32_t count = _tools._size;
        ser.Serialize(count, nullptr);   // local temp, as retail
        if (static_cast<int>(ser.GetDirection()) == 0) {   // loading
            // exe 0x140001bb0 IS Collection::Array<void*>::SetSize(n) (kSpec_PtrZero).
            _tools.SetSize(count);
        }
        // 0x14001bcc0 takes three arguments: its prologue spills R9 to the home slot
        // [RBP+0x58], which the body only reuses as a local after writing it, so the
        // old door's 4th nullptr was never read.
        ::Object::SerializeExternalArray<Graphics::GraphicsMesh>(ser, count, _tools._data);
    }
    ser.Serialize(toolPenalty, nullptr);
    ser.Serialize(maxEducation, nullptr);
    ser.Serialize(educationWorkLength, nullptr);
    ser.Serialize(educationTimeLength, nullptr);
    ser.Serialize(doctorChanceOfDeathScalar, nullptr);
    ser.Serialize(medicateWorkLength, nullptr);
    ser.Serialize(clothingLengthInMonths, nullptr);
    ser.Serialize(maxHappiness, nullptr);
    ser.Serialize(minFoodForHappiness, nullptr);
    ser.Serialize(maxFoodForHappiness, nullptr);
    ser.Serialize(happinessInterval, nullptr);
    ser.Serialize(workForHappiness, nullptr);
    ser.Serialize(unhappyLevel, nullptr);
    ser.Serialize(depressedLevel, nullptr);
    ser.Serialize(idleRange, nullptr);
    {
        // ExternalRef<Profession> x3 at +0x20/+0x28/+0x30 (0x140018020)
        ::Object::SerializeExternal<Profession>(ser, _professions[0]);
        ::Object::SerializeExternal<Profession>(ser, _professions[1]);
        ::Object::SerializeExternal<Profession>(ser, _professions[2]);
    }
    ser.Serialize(childBirthHealthy, nullptr);
    ser.Serialize(childBirthUnhealthy, nullptr);
    ser.Serialize(maxWaterForFireSearch, nullptr);
    ser.Serialize(minDistanceForWork, nullptr);
    ser.Serialize(maxDistanceForWork, nullptr);
    ser.Serialize(timeUntilMaxDistanceForWorkInMonths, nullptr);
    ser.Serialize(maxWalkInWinter, nullptr);
    ser.Serialize(minTemperatureForWinterWalk, nullptr);
    return true;
}

// [6] 0x1400301c0: `this->~T()` (retail: slot 0, flags 0 -- destroy in place), then the ctor again on
// `this`. The hand-built table's Cleanup trampoline already did this.
Object::External* CitizenDescription::Reconstruct() {
    this->~CitizenDescription();
    return new (static_cast<void*>(this)) CitizenDescription();
}

// [7] 0x140001640: retail's shared body, a virtual `this->~T()` (retail: slot 0, flags 0) (D9 #10).
void CitizenDescription::Destruct() { this->~CitizenDescription(); }

// [8] 0x1400301e0: CitizenComponent's type id through the hidden return slot.
Object::Type* CitizenDescription::GetComponentType(Object::Type* ret) const {
    ret->_type = CitizenComponent::TypeId();
    return ret;
}

// [9] 0x1400301b0: placement-new on `this`, the same body as createInPlace (D9 #10).
Object::External* CitizenDescription::Construct() {
    return new (static_cast<void*>(this)) CitizenDescription();
}

// create 0x140030170: MemAlloc(0x148, 8), then the (inlined) ctor.
::Object::Base* CitizenDescription::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(CitizenDescription), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) CitizenDescription());
}
// createInPlace 0x1400301b0.
::Object::Base* CitizenDescription::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) CitizenDescription());
}

DEFINE_EXE_TYPE(CitizenDescription, "Description")
