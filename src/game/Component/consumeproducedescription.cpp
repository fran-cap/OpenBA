// src/game/Component/consumeproducedescription.cpp
// ConsumeProduceDescription (entity description block) — production-building recipe.
// Extends Object::External (0x20 base). Vtable 0x140162b30. Parent "Description".
// Size 0x58. GetVersion=12 (slot +0x18 @ 0x140119200: mov eax,0xC; ret).
//
// Ctor defaults (ConsumeProduceDescription_sub_9270):
//   ExternalPtr  @ +0x20 = 0
//   array        @ +0x28 = {ptr=0, count/cap=0 @ +0x30}   (recipe entries)
//   Type         @ +0x38 = 0   (tool typeId; serialized as a name)
//   int          @ +0x3c = 0
//   int/enum     @ +0x40 = 1
//   float        @ +0x44 = 4.0 (0x40800000)
//   int          @ +0x48 = -1  (0xffffffff)
//   int          @ +0x4c = 0
//   int/enum     @ +0x50 = 0xb (11)
// Serialize (vtable +0x28, 0x1401190e0), stream order (verified disasm CALLs):
//   ExternalPtr @ +0x20  (FUN_14000bf40 -> Reimpl_ExternalPtr_Serialize_StringTable)
//   int count   @ +0x30 -> resize +0x28 outer array (stride 0x28), per outer element:
//        inner int count @+0x08 -> resize inner array @+0x00 (stride 0x10):
//             per inner element: ExternalPtr @+0x00 (FUN_140002340), int @+0x08
//        ExternalPtr @+0x10 (FUN_140002340)
//        enum @+0x18 (encoding vcall +0x98)
//        String @+0x20
//   int   @ +0x40
//   enum  @ +0x50 (encoding vcall +0x98)
//   Type  @ +0x38 (Object::Type::Serialize, dispatch slot 0x1401516b0)
//   int   @ +0x3c
//   float @ +0x44
//   int   @ +0x48
//   int   @ +0x4c
// ToolKit fields: inputs / outputs / work time / tool.
// Decompiled READ-ONLY from Application-x64.exe.
//
// NOTE for orchestrator: the Type field @ +0x38 uses Object::Type::Serialize via
// IO::Serializer dispatch slot 0x1401516b0. We resolve that slot inline below; it
// is filled from Runtime-x64.dll at startup (same mechanism as g_fn_String_Serialize
// = slot 0x140151238). If it is unfilled in standalone the Type read is skipped,
// which would desync the stream — register/wire slot 0x1401516b0 if needed.
//
// ENDGAME B.3.8 K1: a REAL class (ConsumeProduceDescription.h) with a compiler-generated vtable;
// the hand-built 11-slot table and its RegisterExeAddr(0x140162b30) row are gone
// (nothing in the tree read that address back — B38_RULES D5).

#include "Component/ConsumeProduceDescription.h"
#include "Component/ConsumeProduceComponent.h"   // B.3.8 CB1a (TypeId)
#include "object/Internal.h"      // Object::detail::InternalMemAlloc / InternalMemFree
#include "io/Serializer.h"
#include "Object/factory_registration.h"
#include "object/External.h"
#include "object/TypeInfo.h"
#include "system/String.h"
#include <cstdint>
#include <new>
#include "Game/exe_bodies.h"
#include "system/MemoryAllocator.h"
#include "object/Type.h"

#include <cstring>
#include "ui/StringTable.h"   // B8 RT2: ::Object::SerializeExternal<UI::StringTable> (the downcast needs it complete)
#include "Component/ComponentDescription.h"   // B8 RT2: ::Object::SerializeExternal<ComponentDescription> (the downcast needs it complete)

namespace {
// retail's `*(int*)(*(serializer) + 8) == 0` is the stream's direction word —
// IO::Serializer::GetDirection (0x180006210) — 0 = reading. The tree's check before
// it allocates an array.
inline bool Reading(IO::Serializer& ser) {
    return static_cast<int>(ser.GetDirection()) == 0;
}
}  // namespace

// ctor (inlined into create 0x140032360 / createInPlace 0x1400323a0):
// `??0External@Object@@` (Description()), the stamps (the compiler's), then
// the defaults.
ConsumeProduceDescription::ConsumeProduceDescription()
    : Description(),
      _stringTable(nullptr),
      _recipes(nullptr),
      _recipeCount(0),
      _recipeCap(0),
      _messageType(0),
      _messageId(0),
      _workRequired(1),
      _workTime(4.0f),
      _maximumProduction(-1),
      _defaultRecipe(0),
      _toolType(0xb) {}

// dtor (slot 0 0x1400323e0 minus the stamps): frees each recipe's inner array and String, then the recipe array.
// The compiler then chains ~Description -> ~External.
ConsumeProduceDescription::~ConsumeProduceDescription() {
    // free inner arrays then the outer recipe array (the tree walks the CAPACITY)
    int outerN = _recipeCap;
    if (_recipes) {
        for (int i = 0; i < outerN; ++i) {
            ConsumeProduceRecipe& e = _recipes[i];
            if (e.inputs) ::System::MemoryAllocator::MemFree(e.inputs);
            e.name.~String();
        }
        ::System::MemoryAllocator::MemFree(_recipes);
    }
    _recipes = nullptr;
}

// [5] 0x1401190e0: stream order as retail.
bool ConsumeProduceDescription::Serialize(IO::Stream& stream) {
    if (!::System::MemoryAllocator::_singleton) return true;   // standalone: unwired doors
    IO::Serializer ser(stream, 0);
    ::Object::SerializeExternal<UI::StringTable>(ser, _stringTable);
    // outer recipe array @ +0x28 (count @ +0x30, element stride 0x28)
    ser.Serialize(_recipeCount, nullptr);
    int outerN = _recipeCount;
    if (Reading(ser) && outerN > 0) {
        _recipes = static_cast<ConsumeProduceRecipe*>(::System::MemoryAllocator::MemAlloc((uint64_t)outerN * sizeof(ConsumeProduceRecipe), 8));
        _recipeCap = outerN;
        if (_recipes) std::memset(static_cast<void*>(_recipes), 0, (size_t)outerN * sizeof(ConsumeProduceRecipe));
    }
    if (outerN > 0 && _recipes) {
        for (int i = 0; i < outerN; ++i) {
            ConsumeProduceRecipe& e = _recipes[i];
            // inner array (count, element stride 0x10)
            ser.Serialize(e.inputCount, nullptr);
            int innerN = e.inputCount;
            if (Reading(ser) && innerN > 0) {
                e.inputs = static_cast<ConsumeProduceInput*>(::System::MemoryAllocator::MemAlloc((uint64_t)innerN * sizeof(ConsumeProduceInput), 8));
                if (e.inputs) std::memset(static_cast<void*>(e.inputs), 0, (size_t)innerN * sizeof(ConsumeProduceInput));
            }
            if (e.inputs) {
                for (int j = 0; j < innerN; ++j) {
                    ::Object::SerializeExternal<ComponentDescription>(ser, e.inputs[j].material); // ExternalPtr
                    ser.Serialize(e.inputs[j].count, nullptr);
                }
            }
            ::Object::SerializeExternal<ComponentDescription>(ser, e.product);      // ExternalPtr +0x10
            ser.Serialize(e.materialType, nullptr);                          // enum +0x18
            e.name.Serialize(ser);                                          // String +0x20
        }
    }
    ser.Serialize(_workRequired, nullptr);
    ser.Serialize(_toolType, nullptr);
    // Type @ +0x38: Object::Type::Serialize (import slot 0x1401516b0; body
    // RtObject_Type_Serialize, src/engine/object/Type.h).
    RtObject::RtObject_Type_Serialize(&_messageType, &ser);
    ser.Serialize(_messageId, nullptr);
    ser.Serialize(_workTime, nullptr);
    ser.Serialize(_maximumProduction, nullptr);
    ser.Serialize(_defaultRecipe, nullptr);
    return true;
}

// [6] 0x1400323b0: retail destroys and rebuilds in place: a virtual `this->~T()` (retail: slot 0, flags 0),
// then the ctor again on `this` (D9 #10).
Object::External* ConsumeProduceDescription::Reconstruct() {
    this->~ConsumeProduceDescription();
    return new (static_cast<void*>(this)) ConsumeProduceDescription();
}

// [7] 0x140001640: retail's shared body, a virtual `this->~T()` (retail: slot 0, flags 0) (D9 #10).
void ConsumeProduceDescription::Destruct() { this->~ConsumeProduceDescription(); }

// [8] 0x1400323d0: ConsumeProduceComponent's type id through the hidden return slot.
Object::Type* ConsumeProduceDescription::GetComponentType(Object::Type* ret) const {
    ret->_type = ConsumeProduceComponent::TypeId();
    return ret;
}

// [9] 0x1400323a0: placement-new on `this` (folded with createInPlace). The old
// table held the same construction in this slot.
Object::External* ConsumeProduceDescription::Construct() {
    return new (static_cast<void*>(this)) ConsumeProduceDescription();
}

// create 0x140032360: MemAlloc(0x58, 8), then the (inlined) ctor.
::Object::Base* ConsumeProduceDescription::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(ConsumeProduceDescription), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) ConsumeProduceDescription());
}
// createInPlace 0x1400323a0.
::Object::Base* ConsumeProduceDescription::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) ConsumeProduceDescription());
}

DEFINE_EXE_TYPE(ConsumeProduceDescription, "Description")
