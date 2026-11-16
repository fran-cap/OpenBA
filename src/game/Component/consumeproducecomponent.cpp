// src/game/Component/consumeproducecomponent.cpp
// Clean reimplementation of ConsumeProduceComponent lifecycle methods from Application-x64.exe.
//
// ConsumeProduceComponent extends ResourceProduceComponent (0x70 bytes).
// Two non-trivial vtable overrides:
//   Persist (0x140118e00) — parent Persist + versioned serialization of consume fields
//   Startup (0x140118ef0) — init consume data from building template
//
// Decompiled from Application-x64.exe using Ghidra.

#include "Component/ConsumeProduceComponent.h"
#include <cstdint>
#include "object/Internal.h"
#include "object/TypeInfo.h"
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch A
#include "io/Stream.h"      // ENDGAME B.3.4 callers batch H: IO::Stream::GetDirection
#include "Component/ConsumeProduceDescription.h"   // ENDGAME B.3.9 C1: 0x1401c7388 cell class
#include "Component/ComponentDescription.h"
#include "Entity/ComponentEntity.h"
#include <cstdio>
#include <new>

// ============================================================
// Resolved helpers
// ============================================================

// FUN_140023a00 IS Collection::Array<float>::SetSize (ENDGAME B.3.2 step 2), but
// with a derived row that DEVIATES from the header's zero-tail assumption: the
// retail body has NO else-arm write for the new tail at all — it leaves the grown
// slots holding whatever MemAlloc returned. That is exactly SpecFor<float>'s
// behaviour for a trivially default-constructible T (init == nullptr), so the member is
// a Collection::Array<float> (a bare scalar, not a zeroing element). (The free ConsumeProduceComponent_ResizeArray copy and its
// reader-less 0x140023a00 row went at the B.3.8 close.)

// The IsDerivedFrom import slot 0x140152a98 is Object::Base::IsDerivedFrom.

// ============================================================
// ConsumeProduceComponent::Persist (vtable slot [3], 0x140118e00)
// ============================================================
// Calls parent ResourceProduceComponent::Persist, then serializes:
//   - consumeResourceId (+0x50)
//   - consumeRates._size (+0x60) via local copy, with array resize on load
//   - float array at consumeRates (+0x58)
//   - consumeState (+0x68)
//   - Version-0 migration: if consumeResourceId < 0 on load, reset to 0

// ============================================================
// ConsumeProduceComponent::Startup (vtable slot [4], 0x140118ef0)
// ============================================================
// Looks up the entity's description (+0x28) via owner (+0x10), iterates its child
// list to find one matching ConsumeProduceDescription type ID (0x1401c7388),
// then copies consume slot info (count at +0x30, resourceId at +0x4c),
// and initializes the consumeRates float array to 1.0f per slot.
//
// Original decompilation:
//   CheckPtr(self+0x10)
//   entity = *(self+0x10)
//   compList = entity->+0x28 (the description's child list: +0x28=array, +0x30=count)
//   for i in range(compList.count):
//     comp = compList[i]
//     typeId = comp->vtable[+0x10]()  // GetTypeId virtual
//     if typeId == DWORD_1401c7388:
//       desc = compList[i]
//       if IsDerivedFrom(desc, DWORD_1401c7388): desc is valid
//       break
//   ResizeConsumeArray(self+0x58, *(desc+0x30))   // count from description
//   self->consumeResourceId = *(desc+0x4c)        // resource ID from description
//   for i in range(self->consumeRates._size):
//     consumeRates[i] = 1.0f

// ============================================================================
// ENDGAME B.3.8 MI-B — ConsumeProduceComponent as a REAL class
// (ConsumeProduceComponent.h).
//
// Before: CONTROLLER_LIVE_FACTORY_STATES (48-slot table filled per create, slots
// 3/4/13/14/18 written from InstallLive, Inert_ScalarDtor in slot 0, the shared RP
// dispatcher stamped at +0x28). After: the compiler's 22-slot vtable (13/14 — the
// panel "work" checkbox pair — 16/17 and 19-21 inherited from
// ResourceProduceComponent, which is what retail's table holds), a real ctor/dtor
// chain, the controller the base's member (B38_RULES D1, D2, D6).
// ============================================================================
#include "Object/factory_registration.h"

// Slot 18 (+0x90) CreateWork -- retail 0x140118d00, defined in
// resourceproduce_creatework.cpp alongside its five siblings.

// 0x140119050: the RP ctor (CALL 0x14012b3d0), the compiler's stamp, then
// +0x50 = -1, +0x58 = +0x60 = 0 (qwords), +0x68 = 0, `_flags |= 0x80` (already
// set by Component()). +0x50 = -1 is "no recipe selected" until Startup (slot 4)
// reads the description's +0x4c; the tree's factory zero-filled it (recipe 0).
ConsumeProduceComponent::ConsumeProduceComponent()
    : ResourceProduceComponent(), consumeResourceId(-1), _pad54{},
      consumeState(0), _pad6C{} {
    OrFlagsInCtor(0x80);
}

// 0x140118fd0: the body is empty. ~consumeRates is MemFree(+0x58), +0x58 = +0x60 = 0
// (0x140118ff0..0x140119000), then the bases' restamps and ~Internal (the compiler
// chain).
ConsumeProduceComponent::~ConsumeProduceComponent() {
}

// create 0x140032300: MemAlloc(0x70, 8), then the ctor.
::Object::Base* ConsumeProduceComponent::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(ConsumeProduceComponent), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) ConsumeProduceComponent());
}
// Retail's createInPlace is `Internal::CreateInPlaceObject` (returns null); the
// tree's in-place entry built the object, and that is kept (B38 B8-13).
::Object::Base* ConsumeProduceComponent::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) ConsumeProduceComponent());
}

// [3] 0x140118e00 (serialize) and [4] 0x140118ef0 (Startup: init the consume config
// from the building's ConsumeProduceDescription).
void       ConsumeProduceComponent::Persist(IO::Stream& stream) {
    // Base ResourceProduceComponent::Persist (0x14012b4a0), a direct call in retail.
    ResourceProduceComponent::Persist(stream);

    // Create versioned serializer (version 1)
    IO::Serializer ser(stream, 1);

    // Serialize consumeResourceId at +0x50
    ser.Serialize(consumeResourceId, nullptr);

    // Copy consume count to local (original uses local_res8, NOT direct field access).
    // On load, the resize function reads +0x60 internally as old count to decide
    // whether reallocation is needed. Writing directly to +0x60 before resize would
    // make old == new, causing resize to skip reallocation.
    int localCount = consumeRates._size;
    ser.Serialize(localCount, nullptr);

    // On load (the stream's direction word, IO::Serializer::GetDirection == 0): resize.
    if (static_cast<int>(ser.GetDirection()) == 0) {
        // exe 0x140023a00 IS Collection::Array<float>::SetSize(n)
        // (bare float — see the derived-row note under "Resolved helpers" above).
        consumeRates.SetSize(localCount);
    }

    // Serialize float array using local count and rates pointer
    ser.Serialize(localCount, consumeRates._data, nullptr);

    // Serialize consumeState at +0x68
    ser.Serialize(consumeState, nullptr);

    // Version-0 migration: on load, if consumeResourceId < 0, reset to 0
    if (ser.GetVersion() == 0 && consumeResourceId < 0) {
        // Check if we're loading (stream direction == 0)
        if (static_cast<int>(stream.GetDirection()) == 0) {   // (IO::Stream::GetDirection, retail 0x180006200)
            consumeResourceId = 0;
        }
    }
}
void       ConsumeProduceComponent::Startup() {
    ComponentEntity* entity = _entity.Get();
    if (!entity) return;

    // Walk the entity DESCRIPTION's child list, not the component array.
    //
    // ComponentEntity keeps its description at +0x28 and its COMPONENT array at +0x30
    // (count +0x38); a description keeps its CHILD list at +0x28 with the count at
    // +0x30. What is being searched for here is ConsumeProduce*Description*, so retail
    // costs one extra deref: 0x140118ef0 does `RBP = *(entity+0x28)` and only then
    // reads [RBP+0x30] as the count and [RBP+0x28] as the array. Its slot-18 sibling
    // (0x140118d00) does exactly the same.
    const ComponentDescription* descRoot = entity->_description;
    if (!descRoot) return;
    Description* const* compArray = descRoot->_subDescs;
    int compCount = descRoot->_subDescCount;

    // Find ConsumeProduceDescription in the description's child list
    ConsumeProduceDescription* desc = nullptr;
    uint32_t targetTypeId = ConsumeProduceDescription::TypeId();

    for (int i = 0; i < compCount; i++) {
        Description* comp = compArray[i];
        if (!comp) continue;

        if (comp->Type() == targetTypeId) {
            // Verify with IsDerivedFrom
            if (comp->IsDerivedFrom(targetTypeId)) {
                desc = static_cast<ConsumeProduceDescription*>(comp);
            }
            break;
        }
    }

    if (!desc) return;

    // Resize consume array from the description's recipe count
    int descCount = desc->_recipeCount;
    // exe 0x140023a00 IS Collection::Array<float>::SetSize(n).
    consumeRates.SetSize(descCount);

    // The recipe the description selects by default
    consumeResourceId = desc->_defaultRecipe;

    // Initialize all consume rates to 1.0f
    float* rates = consumeRates._data;
    if (rates && consumeRates._size > 0) {
        for (int i = 0; i < consumeRates._size; i++) {
            rates[i] = 1.0f;
        }
    }
}

DEFINE_EXE_TYPE(ConsumeProduceComponent, "ResourceProduceComponent")
