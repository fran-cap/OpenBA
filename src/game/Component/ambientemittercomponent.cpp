// src/game/Component/ambientemittercomponent.cpp
// Clean reimplementation of AmbientEmitterComponent lifecycle methods.
//
// AmbientEmitterComponent (0x48 bytes) manages ambient sound emitters for entities.
// Fields: +0x28=emitter data ptr, +0x30=count, +0x38=emitter array ptr, +0x40=count2
//
// Vtable at 0x140158a60. Non-trivial overrides:
//   Shutdown (0x140042f30) -- Complex serialization with Vector3, emitter entries
//   Draw     (0x140042ba0) -- Resizes emitter list, calls audio setup
//
// Decompiled from Application-x64.exe using Ghidra.

#include "Component/AmbientEmitterDescription.h"   // B.3.8 CB1a (TypeId)
#include "system/Value.h"
#include "object/Internal.h"
#include "object/TypeInfo.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "math/Math.h"
#include "Object/factory_registration.h"   // Object::GetTypeIdByName
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch A
#include "audio/Audio.h"     // ENDGAME B.3.7 callers (K4)
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <new>
#include "Component/exe_bodies.h"
#include "system/MemoryAllocator.h"
#include "collection/Collection.h"   // Collection::Array (the typed view of the raw triple)

#include "Component/AmbientEmitterComponent.h"
#include "audio/SoundEffect.h"

// ============================================================
// The component's AmbientEmitterDescription: every body below opens with
// `IsDerivedFrom([this+0x18], [0x1401c3e98])` (the member, import 0x140152a98) and a
// CMOVNZ over zero, then reads through the result with no test (row 1246).
// ============================================================
static AmbientEmitterDescription* EmitterDescription(AmbientEmitterComponent* self) {
    Description* d = self->_description;
    return d->IsDerivedFrom(AmbientEmitterDescription::TypeId())
           ? static_cast<AmbientEmitterDescription*>(d) : nullptr;
}

// ============================================================
// Reimpl of FUN_140001770 (grow/shrink the 0x10-byte-element emitter array)
// ============================================================
// Resizes the array {ptr@arr+0, count@arr+8, cap@arr+0xc} (stride 0x10). On grow
// past capacity: cap = newCount (or newCount*3 when growing by exactly one from a
// non-empty array), MemAlloc(cap*0x10, 8), copy existing elements (2 qwords each),
// zero-init the new tail with element layout [u32 0][f32 1.0][u64 0]. The exe body
// is hand-unrolled x4; this is the equivalent scalar 1:1 port.
// Retail 0x140001770 is Array<AmbientEmitterSlot>::SetSize exactly (unconditional MemAlloc,
// treble rule, copy-then-free, no element destroy). B6 row 570 dropped the one step retail
// lacked (MemAlloc skipped on a null System::MemoryAllocator::_singleton; retail calls it at
// 0x1400017c0 with no test). B8 (RT): the member itself, an explicit specialization of Array<T>::SetSize.
template <> void Collection::Array<AmbientEmitterSlot>::SetSize(int newCount) {   // 0x140001770
    int oldCount = _size;
    if (newCount == oldCount) return;

    AmbientEmitterSlot* oldPtr = _data;
    _size = newCount;
    if (_capacity < newCount) {
        _capacity = newCount;
        if (newCount == oldCount + 1 && oldCount != 0) {
            _capacity = newCount * 3;
        }
        _data = static_cast<AmbientEmitterSlot*>(
            ::System::MemoryAllocator::MemAlloc((uint64_t)_capacity << 4, 8));
    }

    int start = 0;
    if (oldPtr == _data) start = oldCount;  // grown in place

    AmbientEmitterSlot* base = _data;
    for (int i = start; i < newCount; ++i) {
        AmbientEmitterSlot* dst = base + i;
        if (!dst) continue;
        if (i < oldCount) {
            *dst = oldPtr[i];                     // the two qwords
        } else {
            dst->handle = 0;
            dst->volume = 1.0f;                   // 0x3f800000
            dst->sound  = nullptr;
        }
    }

    if (oldPtr != _data && oldPtr) {
        ::System::MemoryAllocator::MemFree(oldPtr);
    }
}

// ============================================================
// AmbientEmitterComponent::StartEmitters -- 0x140042e10 (read 2026-10-11, row 1246).
// ============================================================
// `if (+0x40 > 0)`, then for i = 0 while i < +0x40 (re-read each pass) PlaySlot's body
// inlined at slot i: the description cast, the scene ([0x1401c3dd8]+0x78) and the slot
// array (+0x38) are re-read every pass, and nothing is tested.
void AmbientEmitterComponent::StartEmitters() {
    for (int i = 0; i < slots._size; ++i)
        PlaySlot(i);
}

// ============================================================
// AmbientEmitterComponent::PlaySlot -- 0x140042c60 (read 2026-10-11, row 1246).
// ============================================================
// 0x140042c74-0x140042d36: desc = the description cast (CMOVNZ over 0); scene =
// [[0x1401c3dd8]+0x78]; entry = +0x38 + slot*0x10. No test on any of them: when the
// slot has no sound yet it takes desc->_emitters[slot] (desc+0x28). FindEffect3D
// reuses a playing effect; else PlayEffect3D(sound, SoundType 3 = ambient, Time(0),
// origin); then tail AddEmitter(handle, position (+0x28), volume).
void AmbientEmitterComponent::PlaySlot(int slot) {
    AmbientEmitterDescription* desc = EmitterDescription(this);
    Audio::Scene* scene = GameUpdateLoop::_singleton->m_audioScene;
    AmbientEmitterSlot* entry = &slots._data[slot];

    if (entry->sound == nullptr)
        entry->sound = desc->_emitters[slot];

    uint32_t handle = scene->FindEffect3D(entry->sound);
    entry->handle = handle;
    if (handle == 0) {
        const System::Time timeZero{0};
        const Math::Vector3 posZero(0.0f, 0.0f, 0.0f);
        handle = scene->PlayEffect3D(entry->sound, static_cast<Audio::Scene::SoundType>(3),
                                     timeZero, posZero);
        entry->handle = handle;
    }
    scene->AddEmitter(handle, position, entry->volume);
}

// ============================================================
// AmbientEmitterComponent::SetSlot -- 0x140042a10 (read 2026-10-11, row 1246).
// ============================================================
// desc = the description cast; `if (+0x40 != desc->_emitterCount) SetSize` (desc read
// with no test); entry = +0x38 + slot*0x10. Then:
//   handle == 0      -> record sound, volume; return. The slot's handle is seeded by
//                       OnAdded (StartEmitters) at registration; SetSlot never starts
//                       a slot itself (ce6fba40d).
//   same sound       -> volume unchanged (UCOMISS, NaN = changed): return; else store
//                       it and SetEmitterVolume(handle, position, vol).
//   different sound  -> RemoveEmitter(handle, position), StopEffect3D when the effect
//                       has no emitter left, handle = 0, volume, sound, PlaySlot(slot).
// The scene is [[0x1401c3dd8]+0x78], read with no test in both arms.
void AmbientEmitterComponent::SetSlot(int slot, Audio::SoundEffect* sound, float vol) {
    AmbientEmitterDescription* desc = EmitterDescription(this);
    int wantCount = desc->_emitterCount;
    if (slots._size != wantCount)
        slots.SetSize(wantCount);   // 0x140001770

    AmbientEmitterSlot* entry = &slots._data[slot];
    uint32_t handle = entry->handle;

    if (handle == 0) {
        entry->sound = sound;
        entry->volume = vol;
        return;
    }

    if (entry->sound != sound) {
        Audio::Scene* scene = GameUpdateLoop::_singleton->m_audioScene;
        scene->RemoveEmitter(handle, position);
        if (scene->GetEmitterCount(entry->handle) == 0)
            scene->StopEffect3D(entry->handle);
        entry->handle = 0;
        entry->volume = vol;
        entry->sound = sound;
        PlaySlot(slot);
        return;
    }

    if (entry->volume == vol) return;
    entry->volume = vol;
    GameUpdateLoop::_singleton->m_audioScene->SetEmitterVolume(handle, position, vol);
}

// ============================================================================
// ENDGAME B.3.8 CIT — AmbientEmitterComponent as a REAL class
// (AmbientEmitterComponent.h).
//
// Before: COMPONENT_LIVE_FACTORY_ONADDED + COMPONENT_INSTALL_SERIALIZE — a
// 48-slot hand-filled table (slot 3 the serializer, slot 6 OnAdded) with
// Inert_ScalarDtor in slot 0. After: the compiler's 16-slot vtable and a real
// ctor/dtor chain (B38_RULES D1, D2).
// ============================================================================
#include "Object/factory_registration.h"

// 0x1400430c0. Component() is the CALL 0x1401188b0; the 0x140158a60 stamp is
// the compiler's; +0x28, +0x30 (dword, 0x1400430f1), +0x38, +0x40 = 0; flags |= 0x43
// (0xc3). +0x34 is never written.
AmbientEmitterComponent::AmbientEmitterComponent()
    : position(), slots() {
    OrFlagsInCtor(0x43);
}

// [3] 0x140042f30
// ============================================================
// Serializes:
//   1. Base Component_Serialize
//   2. Serializer(version=1):
//      - Vector3 at +0x28 (position)
//      - count from +0x40, serialize it
//      - If loading: resize array at +0x38
//      - per slot: the sound ExternalPtr (+0x08), the handle uint (+0x00), the volume (+0x04)
//      - If version==0: resize from building data +0x30
//   3. ~Serializer
void AmbientEmitterComponent::Persist(IO::Stream& stream) {
    Component::Persist(stream);   // retail's direct CALL 0x1401189b0
    IO::Serializer serializer(stream, 1);

    position.Serialize(serializer);

    int localCount = slots._size;
    serializer.Serialize(localCount, nullptr);

    // If loading (stream mode == 0), resize emitter array
    if (static_cast<int>(serializer.GetDirection()) == 0) {   // IO::Serializer::GetDirection, retail 0x180006210
        slots.SetSize(localCount);   // 0x140001770
    }

    if (localCount > 0) {
        AmbientEmitterSlot* arrayBase = slots._data;
        for (int i = 0; i < localCount; i++) {
            ::Object::SerializeExternal<Audio::SoundEffect>(serializer, arrayBase[i].sound);   // 0x1400019a0
            serializer.Serialize(arrayBase[i].handle, nullptr);   // retail's int overload (0x1800061d0)
            serializer.Serialize(arrayBase[i].volume, nullptr);
        }
    }

    // Version-gated: if version==0, resize from building data
    int version = serializer.GetVersion();
    if (version == 0) {
        slots.SetSize(EmitterDescription(this)->_emitterCount);   // 0x140001770, desc read with no test
    }
}

// [6] 0x140042ba0 — dispatched by ComponentEntity_OnAddedPass at register time
// (flags bit 0): size the emitter array and start the building's ambient sounds.
// 0x140042baf-0x140042be2: the description cast (CMOVNZ over 0), SetSize(desc+0x30)
// with no test, then StartEmitters when desc+0x20 (_enabled) is set.
void AmbientEmitterComponent::OnAdded() {
    AmbientEmitterDescription* building = EmitterDescription(this);
    slots.SetSize(building->_emitterCount);

    if (building->_enabled) {
        StartEmitters();
    }
}

// [7] 0x140042b90 (`JMP 0x140042d70`), reached through MarkForRemoval's slot-7
// broadcast (0x1400c2ea0; the ctor's flags 0x43 carry bit 1). For every slot:
// RemoveEmitter at the cached position, StopEffect3D when the effect has no
// emitter left, handle = 0. Retail reads [0x1401c3dd8]+0x78 unguarded (an entity
// is only ever marked while the world is up); so does this.
void AmbientEmitterComponent::OnRemove() {
    const int count = slots._size;
    if (count <= 0) return;
    Audio::Scene* scene = GameUpdateLoop::_singleton->m_audioScene;
    for (int i = 0; i < count; ++i) {
        AmbientEmitterSlot* slot = &slots._data[i];
        const uint32_t id = slot->handle;
        scene->RemoveEmitter(id, position);
        if (scene->GetEmitterCount(id) == 0)
            scene->StopEffect3D(id);
        slot->handle = 0;
    }
}

// create 0x1400014b0: MemAlloc(0x48, 8), then the ctor.
::Object::Base* AmbientEmitterComponent::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(AmbientEmitterComponent), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) AmbientEmitterComponent());
}
// Retail's createInPlace is `Internal::CreateInPlaceObject` (returns null); the
// tree's in-place entry built the object, and that is kept (B38 B8-13).
::Object::Base* AmbientEmitterComponent::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) AmbientEmitterComponent());
}

DEFINE_EXE_TYPE(AmbientEmitterComponent, "Component")
