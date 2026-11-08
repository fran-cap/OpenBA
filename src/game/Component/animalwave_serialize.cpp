// src/game/Component/animalwave_serialize.cpp
// Slot-3 (+0x18) stream Serialize bodies for the animal spawn wave:
//
//   HerdComponent_Serialize        0x140121f90
//   AnimalComponent_Serialize      0x140106e40
//   LivestockComponent_Serialize   0x140123e10
//   PopulationComponent_Serialize  0x1400544a0  (Ghidra: PopulationComponent_Shutdown;
//                                                the fn Ghidra CALLS *_Serialize at
//                                                0x140054220 is really the Startup)
//
// PastureComponent's body (0x140127f40) already lives in pasturecomponent.cpp
// (PastureComponent_Shutdown — Ghidra's shifted name) and WildAnimalComponent's
// (0x14012e300) in wildanimalcomponent.cpp; reg_animal_serialize.cpp registers
// all six retail addresses together.
//
// ⚠ NOT WIRED, BY DESIGN. Standalone has no save/load driver yet
// (GameUpdateLoop::SaveRestore 0x140095a90 is a no-op stub and nothing calls
// Save::SaveLoad::SetOperation for a game save), and a save stream is
// all-or-nothing: filling vtable slot 3 for SOME live components while others
// keep the inert no-op would desync every byte after the first missing
// component on load. Slot-3 wiring therefore lands as one repo-wide pass in
// the future save/load wave; these bodies bring the animal family to parity
// with the ~99 other ported-but-unwired component serializers until then.
//
// Serializer facts used below (verified against the retail decompiles):
//   * Serializer is {Stream* stream; int version; ...}; ctor(self, stream, v).
//   * The load-vs-save test retail emits before every load-side array resize
//     is *(int*)(stream + 8) == 0, read THROUGH the serializer's first field
//     (`*(int*)(*(int64*)ser + 8)`), not serializer+8.
//   * After serializing an InternalBasePtr, retail validates the pointee with
//     IsDerivedFrom(ptr, typeId) and nulls the ref on mismatch. With an
//     unregistered typeId storage (ResolveAddr -> null / id 0) we SKIP the
//     validation rather than null every ref: on the only path that matters
//     (load), skipping matches retail-with-valid-data behaviour.
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch A
#include <cstdint>
#include <new>
#include "object/Internal.h"   // ENDGAME B.3.4 caller batch G
#include "system/Value.h"   // ENDGAME B.3.4b System caller batch
#include "Component/AnimalComponent.h"
#include "Component/HerdComponent.h"
#include "Component/LivestockComponent.h"
#include "Component/WildAnimalComponent.h"     // ENDGAME B.3.9 C1: 0x1401c8000 cell class
#include "Component/PopulationComponent.h"     // ENDGAME B.3.9 C1: 0x1401c4790 cell class
#include "Component/PastureComponent.h"        // ENDGAME B.3.9 C1: 0x1401c7ba0 cell class
#include "Entity/ComponentEntity.h"            // ENDGAME B.3.9 C1: 0x1401c5d70 cell class
#include "Component/PopulationDescription.h"   // ENDGAME B.3.9 C1: 0x1401c47c8 cell class

namespace {

// ---- engine helpers (shared by all four bodies) ----------------------------
// 0x1401189b0 is Component::Persist; the import slot 0x140152a98 is
// Object::Base::IsDerivedFrom. Both are called directly.
// 0x1400032e0 was Collection::Array<void*>::SetSize(n) monomorphised into the
// exe (stride 8, align 8, zero tail: kSpec_PtrZero) — the resolved pointer is gone, see the call sites below.

// True when the serializer is READING (retail: *(int*)(stream + 8) == 0,
// reached through the serializer's first field).
inline bool Loading(IO::Serializer& ser) {
    return static_cast<int>(ser.GetDirection()) == 0;   // IO::Serializer::GetDirection, retail 0x180006210
}

// Serialize one InternalBasePtr slot and apply retail's post-load type validation
// (null the ref when the pointee is not derived from tid). The slots are the
// Herd/Livestock/Population headers' InternalPtr<T> members and array elements.
inline void SerializeValidatedIBP(Object::InternalBasePtr* p, IO::Serializer& ser, uint32_t tid) {
    p->Serialize(ser);
    ::Object::Internal* obj = p->Get();
    if (obj && tid && !::Object::IsDerivedFromNullSafe(obj, tid))
        *p = Object::InternalBasePtr();
}

} // namespace

// ============================================================
// HerdComponent::Serialize (vtable slot [3], 0x140121f90)
// Stream: base, member count(+0x40) with load-side resize of the member IBP
// array(+0x38, validated WildAnimalComponent), phase bool(+0x48), graze
// Time(+0x50), spawn Time(+0x58), grazing cell(+0x4c), PopulationComponent
// backref IBP(+0x28), habitability cell(+0x30). Serializer version 0.
// ============================================================

// ============================================================
// AnimalComponent::Serialize (vtable slot [3], 0x140106e40)
// Stream (version 1): base, wander Time(+0x30), habitat cell(+0x28),
// age(+0x44), deathAge(+0x48), wanders bool(+0x40), graze Time(+0x50),
// wander box shorts(+0x38/+0x3a/+0x3c/+0x3e). Version 0 additionally read a
// legacy IBP that is discarded after an IsDerivedFrom probe.
// ============================================================

// ============================================================
// LivestockComponent::Serialize (vtable slot [3], 0x140123e10)
// Stream (version 0): base, assigned-pasture backref IBP(+0x28, validated
// PastureComponent), production Time(+0x30).
// ============================================================

// ============================================================
// PopulationComponent::Serialize (vtable slot [3], 0x1400544a0 — Ghidra
// mislabels it PopulationComponent_Shutdown)
// Stream (version 0): base, PopulationDescription external ref(+0x28,
// FUN_14000b1b0 = the ExternalPtr<T>::Serialize template), habitability
// dim(+0x30), herd grid dim(+0x48), density-cell count(+0x40) with load-side
// 12-byte-cell resize(+0x38) and per-cell field order 0/8/4, herd-entity
// count(+0x58) with load-side IBP resize(+0x50, validated ComponentEntity),
// smooth Time(+0x60), ints +0x68/+0x6c.
// The member PopulationComponent::Persist (populationcomponent.cpp) calls this body;
// it reads the fields by name.
// ============================================================
extern "C" void PopulationComponent_Serialize_Real(PopulationComponent* self, IO::Stream& stream) {
    if (!self) return;
    self->Component::Persist(stream);   // 0x1401189b0

    IO::Serializer ser(stream, 0);

    ::Object::SerializeExternal<PopulationDescription>(ser, self->description);   // 0x14000b1b0 (B8 RT2)
    ser.Serialize(self->habitatDim, nullptr);
    ser.Serialize(self->herdDim, nullptr);

    int cells = self->cells._size;
    ser.Serialize(cells, nullptr);
    if (Loading(ser))
        self->SetCellCount(cells);   // 0x14000b0d0
    PopulationComponent::Cell* cell = self->cells._data;
    for (int i = 0; i < cells && cell; ++i, ++cell) {
        ser.Serialize(cell->density, nullptr);
        ser.Serialize(cell->minimum, nullptr);
        ser.Serialize(cell->smoothed, nullptr);
    }

    int herds = self->herds._size;
    ser.Serialize(herds, nullptr);
    // exe 0x1400032e0 IS Collection::Array<void*>::SetSize(n) (kSpec_PtrZero).
    if (Loading(ser)) self->herds.SetSize(herds);
    Object::InternalPtr<ComponentEntity>* herd = self->herds._data;
    for (int i = 0; i < herds && herd; ++i, ++herd)
        SerializeValidatedIBP(herd, ser, ComponentEntity::TypeId());

    self->smoothTimer.Serialize(ser);
    ser.Serialize(self->scanIndex, nullptr);
    ser.Serialize(self->unusedSerializedInt, nullptr);
}

void AnimalComponent::Persist(IO::Stream& stream) {
    Component::Persist(stream);   // retail's direct CALL 0x1401189b0

    IO::Serializer ser(stream, 1);

    moveTimer.Serialize(ser);
    ser.Serialize(habitatCell, nullptr);
    ser.Serialize(age, nullptr);
    ser.Serialize(deathAge, nullptr);
    ser.Serialize(canGraze, nullptr);
    grazeTimer.Serialize(ser);
    ser.Serialize(wanderX, nullptr);
    ser.Serialize(wanderY, nullptr);
    ser.Serialize(wanderWidth, nullptr);
    ser.Serialize(wanderHeight, nullptr);
    if (ser.GetVersion() == 0) {
        // v0 compatibility: read-and-discard one IBP (retail probes the type
        // and drops the value; the local never leaves the frame).
        Object::InternalBasePtr legacy;
        legacy.Serialize(ser);
        if (legacy.Get())
            ::Object::IsDerivedFromNullSafe(legacy, PopulationComponent::TypeId());
    }
}

void HerdComponent::Persist(IO::Stream& stream) {
    Component::Persist(stream);   // retail's direct CALL 0x1401189b0

    IO::Serializer ser(stream, 0);

    int count = members._size;
    ser.Serialize(count, nullptr);
    // exe 0x1400032e0 IS Collection::Array<void*>::SetSize(n) (kSpec_PtrZero).
    if (Loading(ser)) members.SetSize(count);

    Object::InternalPtr<WildAnimalComponent>* member = members._data;
    for (int i = 0; i < count && member; ++i, ++member)
        SerializeValidatedIBP(member, ser, WildAnimalComponent::TypeId());

    ser.Serialize(migrating, nullptr);
    grazeTimer.Serialize(ser);
    spawnTimer.Serialize(ser);
    ser.Serialize(tile, nullptr);
    SerializeValidatedIBP(&population, ser, PopulationComponent::TypeId());
    ser.Serialize(populationCell, nullptr);
}

void LivestockComponent::Persist(IO::Stream& stream) {
    Component::Persist(stream);   // retail's direct CALL 0x1401189b0

    IO::Serializer ser(stream, 0);
    SerializeValidatedIBP(&pasture, ser, PastureComponent::TypeId());
    productionTimer.Serialize(ser);
}
