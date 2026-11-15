// src/game/Component/growthcomponent_serialize.cpp
// GrowthComponent::Serialize (vtable slot [3], +0x18, original 0x1401207c0 —
// Ghidra mislabels it GrowthComponent_Shutdown; the second parameter is a
// Stream*, and the body is a textbook stream serializer, not a dtor).
//
// This is the missing piece behind the save/load tree-regrowth bug: on load,
// GrowthComponent had NO stream serializer at all (vtable slot 3 stayed the
// base-Component no-op), so every plant's growth state came back zeroed and
// GrowthComponent::PostUpdate (growthcomponent.cpp) re-derives the model's visual
// scale from that zeroed progress — trees visibly shrink to nothing and
// regrow from scratch, in EntityList update-bucket batches as each bucket's
// components first tick post-load.
//
// Fields (verified against the retail decompile / disassembly at 0x1401207c0):
//   +0x28  InternalBasePtr  sibling natural-resource component (validated
//                           against type id DAT_1401c7b30 — see
//                           growthcomponent.cpp's GrowthComponent::Startup
//                           comment: "resource@+0x28 = first sibling
//                           component of the natural-resource type").
//   +0x30  InternalBasePtr  sibling ModelComponent (validated against type id
//                           DWORD_1401c7a50 — same Startup comment: "+0x30 =
//                           sibling of type DAT_1401c7a50").
//   +0x48  float            smoothed displayed visual scale (see
//                           GrowthComponent::PostUpdate, vtable slot 10).
//   +0x4c  float            growthScale — the randomized per-instance harvest
//                           threshold set once by GrowthComponent::Startup.
//   +0x40  Time (8 bytes)   sub-resource spawn timer (see GrowthComponent::Update).
//
// NOTE: the actual growth PROGRESS value that Update reads/writes every tick
// lives on the SIBLING resource component (resourceComp+0x28, where
// resourceComp = *(void**)(self+0x28) after this Serialize restores the IBP),
// not on GrowthComponent itself. This serializer restores GrowthComponent's
// own fields (including the +0x28 IBP that RE-LINKS to that sibling), which
// is everything retail's GrowthComponent::Serialize touches — the resource's
// own growth-progress field comes back through THAT component's own
// serializer (NaturalResourceComponent::Persist, naturalresourcecomponent.cpp).
//
// Serializer facts reused from animalwave_serialize.cpp (verified against
// this function's own disassembly, same dispatch slots):
//   * Serializer is {Stream* stream; int version; ...}; ctor(self, stream, v).
//   * After serializing an InternalBasePtr, retail validates the pointee with
//     IsDerivedFrom(ptr, typeId) and nulls the ref on mismatch.
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch A
#include <cstdint>
#include <new>
#include "object/Internal.h"   // ENDGAME B.3.4 caller batch G
#include "system/Value.h"   // ENDGAME B.3.4b System caller batch
#include "Component/GrowthComponent.h"
#include "Component/NaturalResourceComponent.h"   // ENDGAME B.3.9 C1: 0x1401c7b30 cell class
#include "Component/ModelComponent.h"             // ENDGAME B.3.9 C1: 0x1401c7a50 cell class
#include "Component/GrowthDescription.h"

// (The base serialize 0x1401189b0 is Component::Persist, called qualified.)
// Each IBP is InternalPtr<T>::Serialize(ser, typeId): InternalBasePtr::Serialize,
// then IsDerivedFrom (slot 0x140152a98) and the ref nulled on a mismatch.

// ============================================================
// GrowthComponent::Serialize (vtable slot [3], 0x1401207c0)
// ============================================================

void GrowthComponent::Persist(IO::Stream& stream) {
    Component::Persist(stream);   // retail's direct CALL 0x1401189b0

    bool loading;
    {
        IO::Serializer ser(stream, 0);

        resource.Serialize(ser, NaturalResourceComponent::TypeId());
        model.Serialize(ser, ModelComponent::TypeId());
        ser.Serialize(displayScale, nullptr);
        ser.Serialize(growthScale, nullptr);
        spawnTimer.Serialize(ser);

        // Everything above is retail 0x1401207c0 field-for-field. What follows is a
        // deliberate, load-only repair -- see below for why it cannot be avoided.
        loading = (static_cast<int>(ser.GetDirection()) == 0);   // (IO::Serializer::GetDirection, retail 0x180006210)
    }

    // config@+0x38 is NOT in the stream: retail's serialize (above) persists only
    // +0x28, +0x30, +0x48, +0x4c and +0x40. Its one writer is GrowthComponent's
    // Startup (retail 0x1401206f0), which is never dispatched for a stream-restored
    // component -- and which cannot simply be re-run here, because it also RE-ROLLS
    // growthScale@+0x4c from the RNG and would throw away the per-instance harvest
    // threshold this stream just restored (a silent load-fidelity bug: the plant
    // keeps its growth but changes when it becomes harvestable, differently on
    // every load).
    //
    // In Startup, +0x38 is only ever a copy of the description at +0x18 -- which
    // the base Component_Serialize above has just restored -- so fill it directly.
    //
    // This repair previously lived lazily inside GrowthComponent_Update, which left
    // an ordering hole: CropFieldComponent slot 15 (OnMessage, retail 0x14011ab50)
    // reads `*(float*)(growth[7] + 0x28)` unguarded, exactly as retail does, and a
    // crop field can message a restored plant BEFORE that plant's first Update tick.
    // That was a null deref a few hundred frames into a loaded tutorial 4. Doing it
    // here means no reader can ever observe +0x38 null on a loaded plant.
    Description* desc0 = _description;
    if (loading && config == nullptr)
        config = static_cast<GrowthDescription*>(desc0);
}
