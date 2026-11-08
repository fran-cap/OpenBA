// src/game/Component/animaldescription.cpp
// AnimalDescription (entity description block) — animal/wildlife parameters.
// Extends Object::External. Vtable 0x140162190. Parent "Description". Size 0x58.
// GetVersion=2. (Transitively pulled by PopulationDeer via the deer entity.)
//
// Ctor defaults (FUN_1401071e0): int@0x20=5, int@0x24=4, float@0x28=1.0,
//   float@0x2c=0, float@0x30=1.0, float@0x34=0.5, float@0x38=0.5, bool@0x3c=1,
//   float@0x40=4.0, External*-array@0x48 (count@0x50, cap@0x54).
// Serialize (vtable +0x28, 0x140107050), stream order:
//   int count@0x50 -> resize +0x48 array -> per-element External FindOrLoad
//   (FUN_1400049c0 == the shared Desc_LoadExternalArray loop), then
//   int@0x20, int@0x24, float@0x28, float@0x2c, float@0x30, float@0x34,
//   float@0x38, bool@0x3c.
// Decompiled READ-ONLY from Application-x64.exe.
//
// ENDGAME B.3.8 K1: a REAL class (AnimalDescription.h) with a compiler-generated vtable;
// the hand-built 11-slot table and its RegisterExeAddr(0x140162190) row are gone
// (nothing in the tree read that address back — B38_RULES D5).

#include "Component/AnimalDescription.h"
#include "Component/AnimalComponent.h"   // B.3.8 CB1a (TypeId)
#include "object/Internal.h"      // Object::detail::InternalMemAlloc / InternalMemFree
#include "io/Serializer.h"
#include "Object/factory_registration.h"
#include "object/External.h"
#include "Component/ComponentDescription.h"   // the SerializeExternalArray<ComponentDescription> instantiation
#include "object/TypeInfo.h"
#include <cstdint>
#include <new>
#include "system/MemoryAllocator.h"


// ctor (inlined into create 0x14002f080 / createInPlace 0x14002f0c0):
// `??0External@Object@@` (Description()), the stamps (the compiler's), then
// the defaults.
AnimalDescription::AnimalDescription()
    : Description(),
      _roamSize(5),
      _requiredArea(4),
      _maxAge(1.0f),
      _maxAgeTolerance(0.0f),
      _ageForOffspring(1.0f),
      _offspringTerm(0.5f),
      _offspringTermTolerance(0.5f),
      _randomIdleHeading(true),
      _idleTime(4.0f),
      _rawMaterials(nullptr),
      _rawMaterialCount(0),
      _rawMaterialCap(0) {}

// dtor (slot 0 0x14002f100 minus the stamps): frees the External* array buffer.
// The compiler then chains ~Description -> ~External.
AnimalDescription::~AnimalDescription() {
    if (_rawMaterials) ::System::MemoryAllocator::MemFree(_rawMaterials);
    _rawMaterials = nullptr;
}

// [5] 0x140107050: stream order as retail.
bool AnimalDescription::Serialize(IO::Stream& stream) {
    IO::Serializer ser(stream, 0);
    // External*-array @ +0x48 (count @ +0x50, stride 8)
    ser.Serialize(_rawMaterialCount, nullptr);
    int n = _rawMaterialCount;
    // IO::Serializer::GetDirection (retail 0x180006210: the stream's +0x08 word), 0 = reading.
    if (static_cast<int>(ser.GetDirection()) == 0 && n > 0 && ::System::MemoryAllocator::_singleton) {
        ComponentDescription** buf = static_cast<ComponentDescription**>(::System::MemoryAllocator::MemAlloc((uint64_t)n * 8, 8));
        _rawMaterials = buf;
        _rawMaterialCap = n;
        if (buf) for (int i = 0; i < n; ++i) buf[i] = nullptr;
    }
    // 0x1401070b5 CALL 0x1400049c0: the ComponentDescription instantiation (cell 0x1401c7318).
    Object::SerializeExternalArray<ComponentDescription>(ser, n, _rawMaterials);
    ser.Serialize(_roamSize, nullptr);
    ser.Serialize(_requiredArea, nullptr);
    ser.Serialize(_maxAge, nullptr);
    ser.Serialize(_maxAgeTolerance, nullptr);
    ser.Serialize(_ageForOffspring, nullptr);
    ser.Serialize(_offspringTerm, nullptr);
    ser.Serialize(_offspringTermTolerance, nullptr);
    ser.Serialize(_randomIdleHeading, nullptr);
    return true;
}

// [6] 0x14002f0d0: retail destroys and rebuilds in place: a virtual `this->~T()` (retail: slot 0, flags 0),
// then the ctor again on `this` (D9 #10).
Object::External* AnimalDescription::Reconstruct() {
    this->~AnimalDescription();
    return new (static_cast<void*>(this)) AnimalDescription();
}

// [7] 0x140001640: retail's shared body, a virtual `this->~T()` (retail: slot 0, flags 0) (D9 #10).
void AnimalDescription::Destruct() { this->~AnimalDescription(); }

// [8] 0x14002f0f0: AnimalComponent's type id through the hidden return slot.
Object::Type* AnimalDescription::GetComponentType(Object::Type* ret) const {
    ret->_type = AnimalComponent::TypeId();
    return ret;
}

// [9] 0x14002f0c0: placement-new on `this` (folded with createInPlace). The old
// table held the same construction in this slot.
Object::External* AnimalDescription::Construct() {
    return new (static_cast<void*>(this)) AnimalDescription();
}

// create 0x14002f080: MemAlloc(0x58, 8), then the (inlined) ctor.
::Object::Base* AnimalDescription::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(AnimalDescription), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) AnimalDescription());
}
// createInPlace 0x14002f0c0.
::Object::Base* AnimalDescription::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) AnimalDescription());
}

DEFINE_EXE_TYPE(AnimalDescription, "Description")
