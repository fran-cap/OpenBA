// src/game/Component/naturalresourcedescription.cpp
// Clean reimplementation of NaturalResourceDescription (Object::External "Description").
//
// NaturalResourceDescription (0x88 bytes) — extends Object::External (0x20 base).
// Vtable at 0x140163e90.  GetVersion = 4.
//
// Field layout (from ctor NaturalResourceDescription_sub_6010 @ 0x140126010 +
// Serialize FUN_140125e10 @ 0x140125e10):
//   +0x20 ExternalPtr<BuildingDescription>  (FUN_140002340 serialize)
//   +0x28 float (1.0)   +0x2c float (4.0)   +0x30 float (1.0)  (+0x34 float, high dword of +0x30 write = 0)
//   +0x38 float (0.5)   (+0x3c float, high dword of +0x38 write = 0)
//   +0x40 float (16.0)  +0x44 float (0.95)
//   +0x48 bool (0)
//   +0x4c float (1.0)   +0x50 float (1.0)   +0x54 float (1.0)  (+0x58 float, high dword of +0x54 write = 0)
//   +0x5c int (1)       (+0x60 float, high dword of +0x5c write = 0)
//   +0x64 bool (0)
//   +0x68 Time (1.0)    +0x70 Time (1.0)
//   +0x78 ExternalRef<Profession>  (FUN_140018020 serialize)
//   +0x80 int/enum (0xb = 11)
//
// Serialize stream order (EXACT, from FUN_140125e10):
//   ExternalPtr(+0x20); float +0x28,+0x2c,+0x30,+0x34,+0x38; bool +0x48;
//   float +0x4c,+0x50,+0x54,+0x58; int +0x5c; float +0x3c,+0x40,+0x44; bool +0x64;
//   Time +0x68; Time +0x70; enum(serializer vtbl +0x98) +0x80; ExternalRef +0x78;
//   float +0x60.
//
// Decompiled from Application-x64.exe (read-only Ghidra recon).
//
// ENDGAME B.3.8 K1: a REAL class (NaturalResourceDescription.h) with a compiler-generated vtable;
// the hand-built 11-slot table and its RegisterExeAddr(0x140163e90) row are gone
// (nothing in the tree read that address back — B38_RULES D5).

#include "Component/NaturalResourceDescription.h"
#include "Component/NaturalResourceComponent.h"
#include "object/Internal.h"      // Object::detail::InternalMemAlloc / InternalMemFree
#include "io/Serializer.h"
#include "Object/factory_registration.h"
#include "object/External.h"
#include "object/TypeInfo.h"
#include "Game/exe_bodies.h"   // Reimpl_ExternalPtr_Serialize_StringTable (0x140002340, 0x140018020)
#include "system/Value.h"      // RtSystem_Time_Ctor
#include <cstring>
#include <cstdint>
#include <new>
#include "Game/Profession.h"   // B8 RT2: ::Object::SerializeExternal<Profession> (the downcast needs it complete)
#include "Component/ComponentDescription.h"   // B8 RT2: ::Object::SerializeExternal<ComponentDescription> (the downcast needs it complete)

namespace {
// The retail ctor stores raw IEEE bit patterns; this keeps them exact.
inline float FloatBits(uint32_t bits) { float f; std::memcpy(&f, &bits, sizeof f); return f; }
}  // namespace

// ctor (inlined into create 0x1400365d0 / createInPlace 0x140036610):
// `??0External@Object@@` (Description()), the stamps (the compiler's), then
// the defaults.
NaturalResourceDescription::NaturalResourceDescription()
    : Description(),
      _building(nullptr),
      _noisePersistance(1.0f),
      _noiseOctaves(4.0f),
      _noiseScale(1.0f),
      _noiseCutoff(0.0f),
      _spawnChance(0.5f),
      _minHeight(0.0f),
      _maxHeight(16.0f),
      _maxAngle(FloatBits(0x3f733333)),
      _groupModels(false),
      _groupPersistance(1.0f),
      _groupOctaves(1.0f),
      _groupScale(1.0f),
      _groupCutOff(0.0f),
      _groupRandom(1),
      _positionTolerance(0.0f),
      _animated(false),
      _profession(nullptr),
      _toolType(0xb) {
    RtSystem_Time_Ctor(&_preAnimateTime, 1.0f);   // ??0Time@System@@QEAA@M@Z
    RtSystem_Time_Ctor(&_postAnimateTime, 1.0f);
}

// [5] 0x140125e10: stream order as retail.
bool NaturalResourceDescription::Serialize(IO::Stream& stream) {
    IO::Serializer ser(stream, 0);
    ::Object::SerializeExternal<ComponentDescription>(ser, _building);   // +0x20 ExternalPtr<BuildingDescription>
    ser.Serialize(_noisePersistance, nullptr);
    ser.Serialize(_noiseOctaves, nullptr);
    ser.Serialize(_noiseScale, nullptr);
    ser.Serialize(_noiseCutoff, nullptr);
    ser.Serialize(_spawnChance, nullptr);
    ser.Serialize(_groupModels, nullptr);
    ser.Serialize(_groupPersistance, nullptr);
    ser.Serialize(_groupOctaves, nullptr);
    ser.Serialize(_groupScale, nullptr);
    ser.Serialize(_groupCutOff, nullptr);
    ser.Serialize(_groupRandom, nullptr);
    ser.Serialize(_minHeight, nullptr);
    ser.Serialize(_maxHeight, nullptr);
    ser.Serialize(_maxAngle, nullptr);
    ser.Serialize(_animated, nullptr);
    _preAnimateTime.Serialize(ser);
    _postAnimateTime.Serialize(ser);
    ser.Serialize(_toolType, nullptr);   // enum through the UINT door
    ::Object::SerializeExternal<Profession>(ser, _profession);   // +0x78 ExternalRef<Profession> (0x140018020)
    ser.Serialize(_positionTolerance, nullptr);
    return true;
}

// [6] 0x140036620: `this->~T()` (retail: slot 0, flags 0 -- destroy in place), then the ctor again on
// `this`. The hand-built table's Cleanup trampoline already did this.
Object::External* NaturalResourceDescription::Reconstruct() {
    this->~NaturalResourceDescription();
    return new (static_cast<void*>(this)) NaturalResourceDescription();
}

// [7] 0x140001640: retail's shared body, a virtual `this->~T()` (retail: slot 0, flags 0) (D9 #10).
void NaturalResourceDescription::Destruct() { this->~NaturalResourceDescription(); }

// [8] 0x140036640: NaturalResourceComponent's type id through the hidden return slot.
Object::Type* NaturalResourceDescription::GetComponentType(Object::Type* ret) const {
    ret->_type = NaturalResourceComponent::TypeId();
    return ret;
}

// [9] 0x140036610: placement-new on `this`, the same body as createInPlace (D9 #10).
Object::External* NaturalResourceDescription::Construct() {
    return new (static_cast<void*>(this)) NaturalResourceDescription();
}

// create 0x1400365d0: MemAlloc(0x88, 8), then the (inlined) ctor.
::Object::Base* NaturalResourceDescription::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(NaturalResourceDescription), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) NaturalResourceDescription());
}
// createInPlace 0x140036610.
::Object::Base* NaturalResourceDescription::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) NaturalResourceDescription());
}

DEFINE_EXE_TYPE(NaturalResourceDescription, "Description")
