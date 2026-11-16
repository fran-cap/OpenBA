// src/game/Component/ambientemitterdescription.cpp
// Clean reimplementation of AmbientEmitterDescription (entity sub-description).
//
// AmbientEmitterDescription (0x38 bytes) — extends Object::External (0x20 base).
// Parent type name: "Description". Vtable @ 0x140158ae0.  GetVersion = 2.
//
// Field layout (from Factory + Serialize FUN_1400431b0 + dtor FUN_140001660):
//   +0x20: bool   _enabled            (default 1)
//   +0x28: void*  _emitters[]         ExternalPtr array buffer (default null)
//   +0x30: int32  _emitterCount       (default 0)  array {ptr@0x28,count@0x30,cap@0x34}
//   +0x34: int32  _emitterCap         (default 0)
//
// Serialize (FUN_1400431b0) stream order:
//   1. int   count                              -> +0x30
//   2. (load) resize ptr-array +0x28 to count   (FUN_140001bb0 stride 8)
//   3. per element: ExternalPtr serialize        (FUN_140001c70 loop)
//   4. bool  _enabled                            -> +0x20
//
// Decompiled from Application-x64.exe (READ-ONLY Ghidra).
//
// ENDGAME B.3.8 K1: a REAL class (AmbientEmitterDescription.h) with a compiler-generated vtable;
// the hand-built 11-slot table and its RegisterExeAddr(0x140158ae0) row are gone
// (nothing in the tree read that address back — B38_RULES D5).

#include "Component/AmbientEmitterDescription.h"
#include "Component/AmbientEmitterComponent.h"   // B.3.8 CB1a (TypeId)
#include "object/Internal.h"      // Object::detail::InternalMemAlloc / InternalMemFree
#include "io/Serializer.h"
#include "Object/factory_registration.h"
#include "object/External.h"
#include "object/TypeInfo.h"
#include <cstdint>
#include <new>
#include "Game/exe_bodies.h"
#include "system/MemoryAllocator.h"
#include "audio/SoundEffect.h"   // B8 RT2: ::Object::SerializeExternal<Audio::SoundEffect> (the downcast needs it complete)

// ctor (inlined into create 0x140001510 / createInPlace 0x140001580):
// `??0External@Object@@` (Description()), the stamps (the compiler's), then
// the defaults.
AmbientEmitterDescription::AmbientEmitterDescription()
    : Description(),
      _enabled(true),
      _emitters(nullptr),
      _emitterCount(0),
      _emitterCap(0) {}

// dtor (slot 0 0x140001660 minus the stamps): frees the ptr-array buffer (FUN_140001660).
// The compiler then chains ~Description -> ~External.
AmbientEmitterDescription::~AmbientEmitterDescription() {
    ::System::MemoryAllocator::MemFree(_emitters);   // free ptr-array buffer
    _emitters = nullptr;
    _emitterCount = 0;
    _emitterCap = 0;
}

// [5] 0x1400431b0: stream order as retail.
bool AmbientEmitterDescription::Serialize(IO::Stream& stream) {
    IO::Serializer ser(stream, 0);
    // 1. the count, through a STACK LOCAL (retail 0x1400431c6: `local = [this+0x30]`,
    //    Serialize(&local)). Serializing +0x30 in place made the on-load SetSize below
    //    a no-op (old count == new count -> early return, no allocation): every
    //    AmbientEmitterDescription loaded with count N and a NULL array, the per-slot
    //    ExternalPtr records were never consumed, and no emitter sound was ever bound
    //    (the river ambience was silent; known_port_issues row 1286).
    int count = _emitterCount;
    ser.Serialize(count, nullptr);
    
    // 2. on LOAD, resize ptr-array (+0x28) to count.
    //
    // ENDGAME B.3.4a closing — THIS READ WAS ONE FIELD OFF AND `loading` WAS
    // STUCK TRUE. It read `*(int*)(*(void**)(serializer + 8) + 8)`: serializer
    // +0x08 is the READERWRITER, and ReaderWriter+0x08 is `_allowCustomSerialize`
    // (B34_RULES §1.2.4), which is always 0 — so the test was `0 == 0`, i.e.
    // TRUE on the save leg too, and the array was resized while saving.
    //
    // Retail reads the DIRECTION, off serializer +0x00 (the Stream), not +0x08.
    // AmbientEmitterDescription::Serialize, Application-x64.exe 0x1400431b0,
    // with the local Serializer at [RSP+0x28]:
    //     1400431f0  MOV RAX,qword ptr [RSP + 0x28]   ; serializer+0x00 = Stream*
    //     1400431f5  CMP dword ptr [RAX + 0x8],0x0    ; stream+0x08 = direction
    //     1400431f9  JNZ 0x140043208                  ; nonzero -> SKIP the resize
    //     1400431fb  MOV EDX,dword ptr [RSP + 0x50]   ; the count
    //     1400431ff  LEA RCX,[RDI + 0x28]
    //     140043203  CALL 0x140001bb0                 ; Array<T*>::SetSize
    // So the resize runs only when direction == 0, and that is `loading`.
    // `IO::Serializer::GetDirection()` is those same two loads (Serializer.h:906).
    bool loading = (static_cast<int>(ser.GetDirection()) == 0);
    if (loading) {
        Reimpl_Array_Resize_Stride08(&_emitters, count);   // sets +0x30 from the local, as retail
    }
    
    // 3. per-element ExternalPtr serialize (each slot is an External*)
    Audio::SoundEffect** buf = _emitters;
    if (buf) {
        for (int i = 0; i < count; ++i) {
            ::Object::SerializeExternal<Audio::SoundEffect>(ser, buf[i]);
        }
    }
    ser.Serialize(_enabled, nullptr);
    return true;
}

// [6] 0x1400015e0: retail destroys and rebuilds in place: a virtual `this->~T()` (retail: slot 0, flags 0),
// then the ctor again on `this` (D9 #10).
Object::External* AmbientEmitterDescription::Reconstruct() {
    this->~AmbientEmitterDescription();
    return new (static_cast<void*>(this)) AmbientEmitterDescription();
}

// [7] 0x140001640: retail's shared body, a virtual `this->~T()` (retail: slot 0, flags 0) (D9 #10).
void AmbientEmitterDescription::Destruct() { this->~AmbientEmitterDescription(); }

// [8] 0x140001650: AmbientEmitterComponent's type id through the hidden return slot.
Object::Type* AmbientEmitterDescription::GetComponentType(Object::Type* ret) const {
    ret->_type = AmbientEmitterComponent::TypeId();
    return ret;
}

// [9] 0x140001580: placement-new on `this`, the same body as createInPlace (D9 #10).
Object::External* AmbientEmitterDescription::Construct() {
    return new (static_cast<void*>(this)) AmbientEmitterDescription();
}

// create 0x140001510: MemAlloc(0x38, 8), then the (inlined) ctor.
::Object::Base* AmbientEmitterDescription::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(AmbientEmitterDescription), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) AmbientEmitterDescription());
}
// createInPlace 0x140001580.
::Object::Base* AmbientEmitterDescription::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) AmbientEmitterDescription());
}

DEFINE_EXE_TYPE(AmbientEmitterDescription, "Description")
