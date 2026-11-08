// src/game/Component/builddescription.cpp
// Clean reimplementation of BuildDescription (entity description block).
//
// BuildDescription (0x38 bytes) — construction-cost parameters for a building.
// Extends Object::External (0x20 base). Vtable at 0x1401622f0.
// Parent type "Description".  (ToolKit: BuildDescription = construction costs.)
//
// Factory  : 0x14002f320  (MemAlloc 0x38 + base init)  fields:
//   void* @ +0x20 = 0   (dynamic array base: {External* @0, int @8} elements)
//   int   @ +0x28 = 0   (array count, also doubles as capacity slot @+0x2c)
//   int   @ +0x30 = 0
//   bool  @ +0x34 = 0
// CreateInPlace: 0x14002f390
// Serialize (vtable +0x28, 0x140109140):
//   int  count           @ +0x28   (also resizes array to `count` on load)
//   for each element (stride 0x10 at base +0x20):
//     int          @ elem+0x08     (cost quantity)
//     ExternalRef  @ elem+0x00     (FUN_140002340 — resource/material description ref)
//   int  @ +0x30
//   bool @ +0x34
// GetVersion (vtable +0x18, 0x140109220): mov eax,2; ret -> file version 2.
// Dtor (vtable +0x00, 0x14002f460): vtable reset + MemFree(array @+0x20) +
//   External::~External + free.
//
// STANDALONE notes:
//  - Fresh-load assumption (Init zeroed the array header): the exe resize helper
//    FUN_14000da70 collapses to "alloc count*0x10 + zero each element". Mirrors
//    MapGenerator's MG_AllocArray.
//  - The per-element ExternalRef serialize (FUN_140002340) is not mapped; we
//    reuse the proven generic ExternalPtr serialize reimpl
//    (Reimpl_ExternalPtr_Serialize_StringTable). The typeId only drives an
//    IsDerivedFrom validation that Object::IsDerivedFromOrTrust no-ops in
//    standalone, so the body is stream-identical.
//
// Decompiled from Application-x64.exe (READ-ONLY recon).
//
// ENDGAME B.3.8 K1: a REAL class (BuildDescription.h) with a compiler-generated vtable;
// the hand-built 11-slot table and its RegisterExeAddr(0x1401622f0) row are gone
// (nothing in the tree read that address back — B38_RULES D5).

#include "Component/BuildDescription.h"
#include "Component/BuildComponent.h"   // B.3.8 CB1a (TypeId)
#include "object/Internal.h"      // Object::detail::InternalMemAlloc / InternalMemFree
#include "io/Serializer.h"
#include "Object/factory_registration.h"
#include "object/External.h"
#include "object/TypeInfo.h"
#include "Game/exe_bodies.h"
#include <cstdint>
#include <new>
#include "system/MemoryAllocator.h"
#include "Component/ComponentDescription.h"   // B8 RT2: ::Object::SerializeExternal<ComponentDescription> (the downcast needs it complete)


// ctor (inlined into create 0x14002f320 / createInPlace 0x14002f390):
// `??0External@Object@@` (Description()), the stamps (the compiler's), then
// the defaults.
BuildDescription::BuildDescription()
    : Description(),
      _costs(nullptr),
      _workRequired(0),
      _scaledWithSize(false) {}

// dtor (slot 0 0x14002f460 minus the stamps): frees the cost array buffer.
// The compiler then chains ~Description -> ~External.
BuildDescription::~BuildDescription() {
    // Free the element array (+0x20). Elements hold weak External* refs (no
    // per-element dtor in the original) — only the buffer itself is freed.
    if (_costs) ::System::MemoryAllocator::MemFree(_costs);
    _costs = nullptr;
    _costCount = 0;
    _costCap = 0;
}

// [5] 0x140109140: stream order as retail.
bool BuildDescription::Serialize(IO::Stream& stream) {
    if (!::System::MemoryAllocator::_singleton) return true;   // standalone: the allocator is unwired
    IO::Serializer ser(stream, 0);
    // Preserve any already-populated requirement array across re-loads. The engine
    // re-invokes this Serialize for an already-loaded resource.
    //
    // [CORRECTED 2026-09-06, runtime port Wave 3 lane C — this comment used to say
    // "on EVERY FindOrLoad", and the mechanism is narrower than that.]
    // ExternalFactory::FindOrLoad (Runtime 0x1800bf320) does NOT reload or re-parse a
    // cached resource: on a Find hit it returns the cached External, and it calls
    // ExternalFactory::Add (0x1800be570) again only when EnableLoad (factory+0x41) is
    // set. Add is what re-invokes Serialize — vtable slot +0x28, driven with an
    // IO::VisitorStream, i.e. a reference-VISIT pass over the object rather than a
    // re-read of the file — and it does so only when `1 << factory->_cleanBitIndex`
    // was NOT already set in the entry's usage mask (entry+0x10). So the re-invoke is
    // once per resource per clean generation, not once per FindOrLoad, and the stream
    // it runs against is a visitor, which is exactly why the re-passes below carry no
    // data and yield empty elements.
    // The 6× count measured for the Pasture is left as recorded; nobody has re-measured
    // it against this mechanism, and a re-measurement is worth doing before trusting
    // either number. See docs/history/runtime_port/WAVE3_LANE_C.md.
    //
    // The first (path-mode) load resolves each _rawMaterial correctly; later
    // (reference-mode) re-loads read the ref through the standalone's incomplete
    // reference table and yield an empty element {material=0, count=0}. Without a
    // guard the last (empty) write wins. Verified for the CreateFenced Pasture,
    // whose `:build` sub-resource is FindOrLoad'd 6×: the first pass populated
    // {RawMaterialWood,1}; the 5 re-passes clobbered it to {0,0}. Building
    // descriptions are immutable templates, so a re-read can only legitimately
    // reproduce identical data — therefore an empty re-load must never overwrite a
    // populated array. (Non-fenced buildings are loaded once and were unaffected,
    // which is why only the Pasture lost its icon.)
    BuildCost* oldArr   = _costs;
    int32_t    oldCount = _costCount;
    bool oldPopulated = (oldArr && oldCount > 0 && oldArr[0].material != nullptr);

    // count @ +0x28 (read from stream)
    ser.Serialize(_costCount, nullptr);
    int count = _costCount;

    // Fresh-load: alloc count * 0x10 element buffer. Populate a fresh buffer; commit
    // to +0x20 only after confirming it isn't an empty reference-mode re-load (below).
    BuildCost* arr = nullptr;
    if (count > 0) {
        arr = static_cast<BuildCost*>(
            ::System::MemoryAllocator::MemAlloc((uint64_t)count * 0x10, 8));
    }

    for (int i = 0; i < count; ++i) {
        BuildCost* e = &arr[i];
        e->material = nullptr;                                     // External* (resolved ref)
        e->quantity = 0;                                           // int cost qty
        ser.Serialize(e->quantity, nullptr);                       // int @ elem+0x08
        ::Object::SerializeExternal<ComponentDescription>(ser, e->material);   // ref  @ elem+0x00
    }

    ser.Serialize(_workRequired, nullptr);   // int +0x30
    ser.Serialize(_scaledWithSize, nullptr);   // bool +0x34

    // Commit, keeping a populated array over an empty re-load. (The tree ran this
    // after ~Serializer; it touches only this object's memory, so ending the
    // Serializer scope at the return instead changes nothing.)
    bool newPopulated = (count > 0 && arr && arr[0].material != nullptr);
    if (oldPopulated && !newPopulated) {
        if (arr) ::System::MemoryAllocator::MemFree(arr);  // discard empty re-load
        arr = oldArr;
        count = oldCount;
    } else if (oldArr && oldArr != arr) {
        ::System::MemoryAllocator::MemFree(oldArr);                            // replaced -> free prior
    }
    _costs     = arr;
    _costCount = count;
    _costCap   = count;
    return true;
}

// [6] 0x14002f3f0: retail destroys and rebuilds in place: a virtual `this->~T()` (retail: slot 0, flags 0),
// then the ctor again on `this` (D9 #10).
Object::External* BuildDescription::Reconstruct() {
    this->~BuildDescription();
    return new (static_cast<void*>(this)) BuildDescription();
}

// [7] 0x140001640: retail's shared body, a virtual `this->~T()` (retail: slot 0, flags 0) (D9 #10).
void BuildDescription::Destruct() { this->~BuildDescription(); }

// [8] 0x14002f450: BuildComponent's type id through the hidden return slot.
Object::Type* BuildDescription::GetComponentType(Object::Type* ret) const {
    ret->_type = BuildComponent::TypeId();
    return ret;
}

// [9] 0x14002f390: placement-new on `this` (folded with createInPlace). The old
// table held the same construction in this slot.
Object::External* BuildDescription::Construct() {
    return new (static_cast<void*>(this)) BuildDescription();
}

// create 0x14002f320: MemAlloc(0x38, 8), then the (inlined) ctor.
::Object::Base* BuildDescription::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(BuildDescription), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) BuildDescription());
}
// createInPlace 0x14002f390.
::Object::Base* BuildDescription::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) BuildDescription());
}

DEFINE_EXE_TYPE(BuildDescription, "Description")
