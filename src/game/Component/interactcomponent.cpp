// src/game/Component/interactcomponent.cpp
// Clean reimplementation of InteractComponent lifecycle methods.
//
// InteractComponent (0x38 bytes) -- direct Component subclass.
// Manages interactive object selection (clickable entities).
// Holds a dynamic array of entity references at +0x28 (ptr) / +0x30 (count).
//
// Vtable at 0x1401638b0:
//   [3] Shutdown = 0x140123340 -- serialization with entity array + IsDerivedFrom
//   [6] Draw     = 0x140123110 -- 140-instruction rebuild of interact list from scene
//
// Decompiled from Application-x64.exe using Ghidra.

#include "Component/MultiObjectComponent.h"
#include "object/TypeInfo.h"
#include "system/MemoryAllocator.h"   // System::MemoryAllocator::MemAlloc / MemFree
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch A
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <new>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "Component/exe_bodies.h"
#include "Entity/ComponentEntity.h"   // ComponentEntity::FindComponent<T>

// Helpers, called by name (B3R): Object::Base::IsDerivedFrom (import slot 0x140152a98),
// InteractComponent_FindWorkplaceComponent (0x140004740, below). 0x1400032e0 was
// Collection::Array<void*>::SetSize(n) monomorphised into the exe (ENDGAME B.3.2 step 2)
// — converted at its call site below.

// ============================================================
// InteractComponent::Shutdown (vtable slot [3], 0x140123340)
// ============================================================
// Serializes Component base + dynamic array of InternalBasePtr elements.
//
// Original pseudocode:
//   Component_Serialize(self, stream)
//   Serializer local(stream, 0)
//   int count = self->field30
//   Serialize(&count, nullptr)
//   if (serializer loading): IBPArray_Resize(&self->field28, count)
//   for each element in array:
//     IBP::Serialize(element, serializer)
//     if element != null && !IsDerivedFrom(element, InteractTypeId):
//       element = 0
//   ~Serializer()

// ============================================================
// InteractComponent::Draw (vtable slot [6], 0x140123110)
// ============================================================
// Reimpl of FUN_140123110: rebuild the interact-use-location list from the
// owner's MultiObject children. For each child's MapComponent (DAT_1401c7900),
// scan its job hosts for an InteractComponent and append it to the +0x28 list.
// Faithful port of the Ghidra decompilation.

// ============================================================================
// ENDGAME B.3.8 VIS — InteractComponent as a REAL class (InteractComponent.h).
//
// Before: a 48-slot InteractComponent_InertVtable filled per create,
// Inert_ScalarDtor in slot 0 (only ~Internal).
// After: the compiler's 16-slot vtable and a real ctor/dtor chain
// (B38_RULES D1, D2); slot bodies are the ones the old table installed.
// ============================================================================
#include "Object/factory_registration.h"
#include "Component/InteractComponent.h"

// Slot 6 is the ON-ADDED broadcast: ComponentEntity_OnAddedPass (FUN_1400c2f60, the
// tail of EntityList::RegisterEntity 0x1400c48a0) calls it on every component whose
// capability flags (+0x20) have bit 0 — InteractComponent's 0x81 qualifies. The body
// rebuilds the owner's interact use-location list (+0x28 array / +0x30 count) from the
// owner's MultiObjectComponent children; FUN_140122b00 (CalcChildTargetPos) recurses
// over that list to find a tile a worker can stand on (bridge bank end-pieces, fenced
// pasture masters).

// 0x140123480: Component() is the CALL 0x1401188b0; the 0x1401638b0 stamp is
// the compiler's; +0x28 = +0x30 = 0 (the inlined Array ctor); flags |= 1 (0x81).
InteractComponent::InteractComponent() {
    OrFlagsInCtor(0x1);
}

// [3] 0x140123340 — the use-location IBP array (+0x28/+0x30) round-trips here.
void InteractComponent::Persist(IO::Stream& stream) {
    // Call base Component_Serialize
    Component::Persist(stream);   // retail's direct CALL 0x1401189b0

    IO::Serializer ser(stream, 0);

    // Serialize count
    int32_t count = useLocations._size;
    ser.Serialize(count, nullptr);

    // "Am I loading?" is the STREAM's direction (IO::Serializer::GetDirection, retail
    // 0x180006210), not the Serializer's own +8 (m_encoding): see
    // reference_serializer_plus8_is_encoding_not_loading_flag -- the same trap, found
    // here a second time. With the wrong field the resize below never ran on a real
    // load, so the array at +0x28 stayed NULL while `count` came back non-zero from
    // the stream, and the element loop dereferenced null.
    if (static_cast<int>(ser.GetDirection()) == 0) {
        // exe 0x1400032e0 IS Collection::Array<void*>::SetSize(n) (kSpec_PtrZero).
        useLocations.SetSize(count);
    }

    // Serialize each InternalBasePtr element and validate
    Object::InternalPtr<InteractComponent>* arrayBase = useLocations._data;
    for (int i = 0; i < count; i++) {
        arrayBase[i].InternalBasePtr::Serialize(ser);

        // Validate with IsDerivedFrom
        // NOT RETAIL (F-143): null and type tests on the held Internal*, before any
        // downcast to InteractComponent*; the answer is the same.
        if (!arrayBase[i].IsNull()) {
            bool valid = ::Object::IsDerivedFromNullSafe(arrayBase[i], InteractComponent::TypeId());   // slot 0x140152a98
            if (!valid) {
                arrayBase[i] = nullptr;
            }
        }
    }
}
// [6] 0x140123110
void InteractComponent::OnAdded() {
    MultiObjectComponent* multi = _entity->FindComponent<MultiObjectComponent>();   // 0x140004740
    if (multi == nullptr) return;

    // Clear the existing use-location array
    System::MemoryAllocator::MemFree(useLocations._data);
    useLocations._data = nullptr;
    useLocations._size = 0;
    useLocations._capacity = 0;

    int childCount = multi->children._size;
    for (int ci = 0; ci < childCount; ci++) {
        ComponentEntity* host = multi->children[ci].Get();

        // Find the InteractComponent (DAT_1401c7900) on this host
        InteractComponent* interact = host->FindComponent<InteractComponent>();

        if (interact != nullptr) {
            // Append interact to the use-location array at +0x28 (count +0x30, cap +0x34)
            uint32_t oldCount = (uint32_t)useLocations._size;
            uint32_t newCount = oldCount + 1;
            if (newCount != oldCount) {
                Object::InternalPtr<InteractComponent>* oldBuf = useLocations._data;
                useLocations._size = (int32_t)newCount;
                if (useLocations._capacity < (int)newCount) {
                    useLocations._capacity = (int32_t)newCount;
                    if (oldCount != 0) useLocations._capacity = (int32_t)(newCount * 3);
                    useLocations._data = static_cast<Object::InternalPtr<InteractComponent>*>(
                        System::MemoryAllocator::MemAlloc((uint64_t)((int64_t)useLocations._capacity << 3), 8));
                }
                int start = (oldBuf == useLocations._data) ? (int)oldCount : 0;
                Object::InternalPtr<InteractComponent>* nbArr = useLocations._data;
                Object::InternalPtr<InteractComponent>* oldArr = oldBuf;
                for (int i = start; i < (int)newCount; i++) {
                    Object::InternalPtr<InteractComponent>* dst = nbArr + i;
                    if (dst) {
                        *dst = nullptr;
                        *dst = (i < (int)oldCount) ? oldArr[i] : interact;
                    }
                }
                if (oldBuf != useLocations._data) System::MemoryAllocator::MemFree(oldBuf);
            }
        }
    }
}
// create 0x1400351a0: MemAlloc(0x38, 8), then the ctor.
::Object::Base* InteractComponent::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(InteractComponent), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) InteractComponent());
}
// Retail's createInPlace is `Internal::CreateInPlaceObject` (returns null); the
// tree's in-place entry built the object, and that is kept (B38 B8-13).
::Object::Base* InteractComponent::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) InteractComponent());
}

DEFINE_EXE_TYPE(InteractComponent, "Component")
