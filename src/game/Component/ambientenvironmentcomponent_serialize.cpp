// src/game/Component/ambientenvironmentcomponent_serialize.cpp
// Slot-3 (+0x18) stream Serialize body for AmbientEnvironmentComponent
// (retail 0x1400437e0 — Ghidra mislabels it AmbientEnvironmentComponent_Shutdown;
// the fn it CALLS is Component_Serialize, confirming this is really Serialize).
//
// Retail body (verified by decompile + disassembly of 0x1400437e0):
//   Component_Serialize(self, stream)
//   Serializer ser(stream, /*version*/0)
//   Vector3::Serialize(self+0x38, ser)   // region-center world position
//   Serialize(ser, self+0x28, null)      // gridX   (int)
//   Serialize(ser, self+0x2c, null)      // gridY   (int)
//   Serialize(ser, self+0x30, null)      // rw      (int)
//   Serialize(ser, self+0x34, null)      // rh      (int)
//   ~Serializer
//
// These are exactly the fields AmbientEnvironmentComponent::InitRegion (in
// ambientenvironmentcomponent.cpp) already writes at spawn time (gridX/gridY/
// rw/rh at +0x28.. +0x34, and the Vector3 region-center position at +0x38),
// so this body round-trips real per-instance state through save/load, not
// filler.
//
#include "math/Math.h"
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch A
#include <cstdint>
#include <new>
#include "Component/AmbientEnvironmentComponent.h"

// ============================================================
// AmbientEnvironmentComponent::Serialize (vtable slot [3], 0x1400437e0)
// ============================================================

// [3] 0x1400437e0.
void AmbientEnvironmentComponent::Persist(IO::Stream& stream) {
    Component::Persist(stream);   // retail's direct CALL 0x1401189b0

    IO::Serializer ser(stream, 0);

    position.Serialize(ser);
    ser.Serialize(regionX, nullptr);
    ser.Serialize(regionY, nullptr);
    ser.Serialize(regionWidth, nullptr);
    ser.Serialize(regionHeight, nullptr);
}
