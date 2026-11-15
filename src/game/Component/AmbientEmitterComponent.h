#pragma once
// ============================================================================
// src/game/Component/AmbientEmitterComponent.h — ENDGAME B.3.8 block CIT
//
// `AmbientEmitterComponent` as a real C++ class with a compiler-generated
// vtable (B38_RULES.md D1-D4).
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §6):
//   reflected "AmbientEmitterComponent", parent "Component", ti 0x1401c3e60
//   create 0x1400014b0: MemAlloc(0x48, 8) + ctor 0x1400430c0
//   ctor   0x1400430c0: Component ctor, stamp 0x140158a60, +0x28 = +0x30(dword)
//                       = +0x38 = +0x40 = 0, flags |= 0x43
//   dtor   0x1400016d0 (scalar): MemFree(+0x38); +0x38 = +0x40 = 0 (the emitter
//                       array), stamp 0x140162920, ~Internal, MemFree on &1
//   table  0x140158a60, 16 slots:
//     [0] 0x1400016d0 scalar deleting dtor   [1]/[2] TypeName/Type
//     [3] 0x140042f30 Persist                [6] 0x140042ba0 OnAdded (emitters)
//     [7] 0x140042b90 OnRemove (JMP 0x140042d70)  every other slot = Component's
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Component/Component.h"
#include "Object/exe_type.h"
#include "math/Math.h"
#include "collection/Collection.h"   // Collection::Array (the +0x38 slots)

namespace Audio { class SoundEffect; }

// One emitter slot (0x10; Persist 0x140042f30, resize 0x140001770 default
// {0, 1.0f, null}).
struct AmbientEmitterSlot {
    int32_t             handle;   // +0x00 the Audio::Scene effect id (0 = not playing); int: Persist
                                  //       0x140042f30 serializes it through Serialize(int&) 0x1800061d0
    float               volume;   // +0x04
    Audio::SoundEffect* sound;    // +0x08 ExternalPtr<SoundEffect>
};
static_assert(sizeof(AmbientEmitterSlot) == 0x10, "AmbientEmitterSlot stride 0x10 (0x140001770)");
static_assert(offsetof(AmbientEmitterSlot, handle) == 0x0, "AmbientEmitterSlot::handle");
static_assert(offsetof(AmbientEmitterSlot, volume) == 0x04, "AmbientEmitterSlot::volume @ +0x04");
static_assert(offsetof(AmbientEmitterSlot, sound) == 0x08, "AmbientEmitterSlot::sound @ +0x08");

class AmbientEmitterComponent final : public Component {
    DECLARE_EXE_TYPE(AmbientEmitterComponent)

public:
    AmbientEmitterComponent();                                    // 0x1400430c0

    // 0x1400016d0: MemFree(+0x38), +0x38 = +0x40 = 0 (qwords), restamp, ~Internal --
    // the member destruction of `slots` (~Array, trivially destructible element).
    ~AmbientEmitterComponent() override = default;                // [0] 0x1400016d0
    void Persist(IO::Stream& stream) override;                          // [3] 0x140042f30
    void OnAdded() override;                                      // [6] 0x140042ba0
    void OnRemove() override;                                     // [7] 0x140042b90 (JMP 0x140042d70)

    // ---- non-virtual members (retail addresses) ----
    // 0x140042e10: start (or join) the 3D ambient effect of every emitter slot.
    void StartEmitters();
    // 0x140042a10: set slot `slot` to `sound` at `volume` (AmbientEnvironmentComponent).
    void SetSlot(int slot, Audio::SoundEffect* sound, float volume);
    // 0x140042c60: (re)play slot `slot`'s effect and add this emitter to it.
    void PlaySlot(int slot);
    // 0x140042960: move every playing slot's emitter to `newPos`, then cache it
    // in `position` (defined in tornadocomponent.cpp, COMP-e).
    void MoveTo(const Math::Vector3& newPos);

    // PUBLIC while the ported bodies read them (B38 decision 1). Persist
    // 0x140042f30: Vector3::Serialize, then the count and per slot the
    // ExternalPtr<SoundEffect> (0x1400019a0), the int and the float.
    Math::Vector3       position;      // +0x28 where the emitter sounds
    uint8_t             _pad34[4];     // +0x34
    // +0x38 Array<AmbientEmitterSlot> {data, count +0x40, capacity +0x44} (H5, typed: the
    // ctor 0x1400430c0 zeroes +0x38 / +0x40 as qwords, the dtor 0x1400016d0 MemFrees +0x38
    // and zeroes both qwords before ~Component; no element destroy). Resized by
    // 0x140001770 (Collection::Array<AmbientEmitterSlot>::SetSize).
    Collection::Array<AmbientEmitterSlot> slots;
};

static_assert(sizeof(AmbientEmitterComponent) == 0x48, "AmbientEmitterComponent is 0x48 (create 0x1400014b0)");
static_assert(offsetof(AmbientEmitterComponent, _pad34) == 0x34, "AmbientEmitterComponent::_pad34");
static_assert(alignof(AmbientEmitterComponent) == 8, "AmbientEmitterComponent alignment");
static_assert(offsetof(AmbientEmitterComponent, position) == 0x28, "AmbientEmitterComponent::position @ +0x28 (Vector3::Serialize)");
static_assert(offsetof(AmbientEmitterComponent, slots) == 0x38, "AmbientEmitterComponent::slots @ +0x38");
static_assert(offsetof(AmbientEmitterComponent, slots._size) == 0x40, "AmbientEmitterComponent::slots count @ +0x40");
static_assert(offsetof(AmbientEmitterComponent, slots._capacity) == 0x44, "AmbientEmitterComponent::slots capacity @ +0x44");
