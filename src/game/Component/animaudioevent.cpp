// src/game/Component/animaudioevent.cpp
// AnimAudioEvent — a Graphics::AnimationEvent subclass (0x30 bytes) loaded as
// part of the Character animation closure when an entity template with a
// Character component is deserialized (Template\Citizen.rsc).
//
// Layout (from AnimAudioEvent_Factory 0x140016f00 + Serialize 0x140087ed0):
//   +0x00 vtable (0x14015e0c0)
//   +0x08 Graphics::AnimationEvent / Object::External base
//   +0x20 External*  audio resource reference (default 0)
//   +0x28 float      play chance (default 1.0)
//
// Serialize (slot 5) reads exactly the two subclass fields: the External ref at
// +0x20 (via ExternalPtr<SoundEffect>::Serialize FUN_1400019a0, called at
// 0x140087efa) and the float at +0x28. GetFileVersion (slot 3) = 2 (exe body
// `mov eax,2; ret`).
//
// ENDGAME B.3.8 VIS: a REAL class (AnimAudioEvent.h). Before: an 11-slot
// hand-built g_AAE_Vtable registered at 0x14015e0c0, the object built through
// the `??0AnimationEvent@Graphics@@` DOOR (lazily resolved by name) and then
// re-stamped. After: the compiler's 10-slot vtable; AnimationEvent() is the
// base's real protected ctor (block RB). Nothing reads 0x14015e0c0 back by
// address, so its RegisterExeAddr row is gone (B38 D5).

#include "Component/AnimAudioEvent.h"
#include "math/Math.h"
#include "object/External.h"
#include "object/TypeInfo.h"
#include "graphics/Camera.h"
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch A
#include "object/Internal.h"  // Object::detail::InternalMemAlloc / InternalMemFree
#include "Object/factory_registration.h"
#include <cstdint>
#include <new>
#include "Component/exe_bodies.h"
#include "audio/SoundEffect.h"   // B8 RT2: ::Object::SerializeExternal<Audio::SoundEffect> (the downcast needs it complete)
#include "audio/Audio.h"         // Audio::Scene::PlayEffect3D (the trigger)
#include "system/Value.h"        // System::Random::RandFloat, System::Time
#include "Game/GameUpdateLoop.h"  // GameUpdateLoop::_singleton (retail g_pGameWorldSingleton 0x1401c3dd8)

// 0x140016f00 (inlined): AnimationEvent() is `??0AnimationEvent@Graphics@@`
// (External's three zero stores); the 0x14015e0c0 stamp is the compiler's;
// +0x20 = 0, +0x28 = 1.0f.
AnimAudioEvent::AnimAudioEvent() : _sound(nullptr), _chance(1.0f), _pad2c{} {}

// [5] 0x140087ed0: Serializer(stream, 0); ExternalPtr +0x20; float +0x28;
// ~Serializer; return 1. (The hand-built slot was a `void` function, so its AL
// was whatever the Serializer dtor left there.)
bool AnimAudioEvent::Serialize(IO::Stream& stream) {
    IO::Serializer ser(stream, 0);
    // +0x20 the sound: ExternalPtr<SoundEffect>::Serialize (0x140087efa CALL 0x1400019a0)
    ::Object::SerializeExternal<Audio::SoundEffect>(ser, _sound);
    // +0x28 float
    ser.Serialize(_chance, nullptr);
    return true;
}

// [6] 0x140016fb0: retail destroys and rebuilds in place: a virtual
// `~T()` (slot 0, flags 0), then the ctor again on `this` (B38_RULES §2.2, D9 #10).
Object::External* AnimAudioEvent::Reconstruct() {
    this->~AnimAudioEvent();
    return new (static_cast<void*>(this)) AnimAudioEvent();
}
// [7] 0x140001640: retail's shared body, a virtual `this->~T()` (retail: slot 0, flags 0).
void AnimAudioEvent::Destruct() { this->~AnimAudioEvent(); }
// [8] 0x140087e40 — the audio trigger, fired by Graphics::Animation::ApplyEvents
// (Runtime 0x1800faa5e, `CALL [RAX+0x40]`) with RDX = the transform the animated
// MeshInstance's Update handed down (0x140087e54 MOV RBX,RDX). Retail:
//   roll  = System::Random::RandFloat([gameWorld+0xd8])      (0x140087e5e, [0,1))
//   COMISS chance(+0x28), roll ; JC skip   -> play when !(chance < roll)
//   Time(0.0f) (0x140087e76); pos = transform+0x10/+0x14/+0x18 (the translation)
//   Audio::Scene::PlayEffect3D([gameWorld+0x78], +0x20, SoundType 4, &time, &pos)
//     (0x140087eb5; RCX scene, RDX the bare ExternalPtr, R8D 4, R9 &time,
//      [RSP+0x20] &pos — no hidden return slot, the uint handle is dropped)
// The body returns nothing; ApplyEvents ignores RAX. Both game-world reads are
// unguarded in retail (the world exists whenever an entity's model ticks).
void AnimAudioEvent::Construct(const Math::Transform& transform) {
    GameUpdateLoop* const world = GameUpdateLoop::_singleton;
    const float roll = world->m_random->RandFloat();
    if (_chance < roll) return;                 // COMISS / JC: CF set skips (NaN too)
    const System::Time timeZero{0};             // System::Time(0.0f)
    const Math::Vector3 pos = transform.translation;
    world->m_audioScene->PlayEffect3D(Object::ExternalPtr<Audio::SoundEffect>(_sound),
                                      static_cast<Audio::Scene::SoundType>(4), timeZero, pos);
}
// [9] 0x140016f60 — placement construction on `this` (the createInPlace body).
Object::External* AnimAudioEvent::ConstructInPlace() {
    return new (static_cast<void*>(this)) AnimAudioEvent();
}

// create 0x140016f00: MemAlloc(0x30, 8), then the (inlined) ctor.
::Object::Base* AnimAudioEvent::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(AnimAudioEvent), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) AnimAudioEvent());
}
// createInPlace 0x140016f60: placement construction.
::Object::Base* AnimAudioEvent::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) AnimAudioEvent());
}

DEFINE_EXE_TYPE(AnimAudioEvent, "Graphics::AnimationEvent")
