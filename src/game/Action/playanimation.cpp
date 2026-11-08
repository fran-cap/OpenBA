// src/game/Action/playanimation.cpp
// Clean C++ reimplementation of PlayAnimation action methods.
// Source: Game/Action/PlayAnimation.cpp (from profile build debug strings)
//
// PlayAnimation -- 0x40 bytes. Plays an animation on a citizen entity.
// Inherits: Object::Internal -> Action (0x20) -> PlayAnimation
// Vtable at 0x140158950 (10 slots).
//
// Decompiled from Application-x64.exe using Ghidra.

#include "PlayAnimation.h"
#include "object/TypeInfo.h"
#include "Action/exe_bodies.h"   // FindCharacterComponent, SetAnimation
#include "Game/exe_constants.h"  // kRdata_14017214c
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch C
#include <cstdint>
#include <new>
#include "object/Internal.h"   // ENDGAME B.3.4 caller batch G
#include "system/Value.h"      // ENDGAME B.3.4b System callers
#include "Entity/ComponentEntity.h"
#include "Component/CharacterComponent.h"   // CharacterComponent::SetAnimation

// ============================================================
// Helpers, called by name (B3R)
//   0x1400012d0 ActMv_FindComponentFamily_12d0 (Entity_FindComponent_Family)
//   0x14010a1f0 ActMv_SetAnimation_a1f0 -- set animation on family component
// DAT_14017214c (1.0f, the animation-setup float) is kRdata_14017214c (Game/exe_constants.h).
// ============================================================

// ============================================================
// PlayAnimation::Serialize (0x140042480)
// Serializes: status(+0x18), entity(+0x20) ComponentEntity, int(+0x28),
//   Time(+0x30), Time(+0x38)
// ============================================================
void PlayAnimation::Persist(IO::Stream& stream) {
    Action::Persist(stream);   // inlined in retail: Serializer(stream, 0), status +0x18

    IO::Serializer ser(stream, 0);
    _entity.InternalBasePtr::Serialize(ser);
    if (!_entity.IsNull()) {
        if (!::Object::IsDerivedFromNullSafe(_entity, ComponentEntity::TypeId())) {
            _entity = nullptr;
        }
    }
    ser.Serialize(animation, nullptr);   // +0x28 int
    _duration.Serialize(ser);             // +0x30 Time
    _elapsed.Serialize(ser);             // +0x38 Time
}

// ============================================================
// PlayAnimation::AbstractImpl / _purecall (0x140042420)
// Finds the entity's CharacterComponent, plays the animation.
// Returns 1 (action stays active, Execute checks completion).
// ============================================================
// 0x140042420, read whole: CheckPtr(+0x20) 0x140042431; CALL 0x1400012d0 (the target's
// CharacterComponent) 0x140042440; CheckPtr 0x14004244a; SetAnimation(character, +0x28,
// 1.0f, 1.0f) 0x140042463; MOV EAX,1 0x14004246d. No test of the target or the component.
// (The tree's null-target return, its readable-memory / vptr probe of the target and its
// null-CharacterComponent return are gone: their reason, "an object built without the
// real Object::Internal ctor is never table-registered, so CheckPtr cannot invalidate a
// weak ref to it", is retired since B.4 gave every class its ctor.)
int  PlayAnimation::AbstractImpl(ActionStack* actionStack) {
    (void)actionStack;
    CharacterComponent* character = _entity.Get()->FindComponent<CharacterComponent>();   // 0x1400012d0
    character->SetAnimation(static_cast<uint32_t>(animation), kRdata_14017214c, kRdata_14017214c);   // 0x14010a1f0
    return 1;
}

// ============================================================
// PlayAnimation::Execute (0x140042400)
// Accumulates elapsed time, returns true while still animating.
//   self->_elapsed += *time;
//   return (self->_elapsed < self->_duration);
// ============================================================
int  PlayAnimation::Execute(ActionStack* actionStack, const System::Time* time) {
    (void)actionStack;
    int64_t elapsed = time->_ticks;
    _elapsed._ticks += elapsed;
    return (_elapsed._ticks < _duration._ticks) ? 1 : 0;
}

// ============================================================
// PlayAnimation::GetTarget (0x1400423b0)
// Returns true if param matches entity at +0x20.
// ============================================================
bool PlayAnimation::GetTarget(ComponentEntity** param) {
    return _entity.Get() == *param;
}

// ============================================================
// class PlayAnimation (PlayAnimation.h) — ENDGAME B.3.8 ACT.
// The hand-built g_PlayAnimation_Vtable[10], its per-class type-id cache and the
// Action_Alloc / Action_InitInPlace factories are gone: the compiler emits
// the table, DECLARE_EXE_TYPE answers slots 1/2.
// ============================================================
#include "Object/factory_registration.h"

// create 0x1400011f0 (inlined ctor). The tree's factory zeroed every field above
// +0x10; kept (not a behaviour change of this step).
PlayAnimation::PlayAnimation()
    : _entity(nullptr),
      animation(0),
      _pad2C{},
      _duration{0},
      _elapsed{0} {}

// [3] 0x140042480
// [4] 0x140042420
// [5] 0x140042400
// [8] 0x1400423f0 — the leaf's own `RET 0` (same bytes as the base body)
void PlayAnimation::CheckEntity() {}
// [9] 0x1400423b0

::Object::Base* PlayAnimation::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(PlayAnimation), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) PlayAnimation());
}
::Object::Base* PlayAnimation::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) PlayAnimation());
}


DEFINE_EXE_TYPE(PlayAnimation, "Action")
