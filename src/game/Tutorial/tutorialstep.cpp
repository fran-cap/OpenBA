// src/game/Tutorial/tutorialstep.cpp
// Clean reimplementation of TutorialStep vtable methods.
//
// TutorialStep (0x70 bytes) -- tutorial step definition.
// Vtable: 0x140160e90, extends Object::External (0x20 bytes base).
//
// Fields (from Ghidra decompilation):
//   +0x20: bool     completed      -- step completion flag
//   +0x28: void*    stringTable    -- ExternalPtr<StringTable>
//   +0x30: String   titleStr
//   +0x38: String   descStr
//   +0x40: Array<ExternalPtr<TutorialImage>> images
//   +0x50: Array<ExternalPtr<TutorialSetup>> setups
//   +0x60: Array<ExternalPtr<TutorialGoal>>  goals
//
// Decompiled from Application-x64.exe using Ghidra.

#include "Tutorial/TutorialStep.h"
#include "object/External.h"
#include "object/TypeInfo.h"
#include "Game/exe_bodies.h"
#include "Tutorial/exe_bodies.h"
#include "system/String.h"
#include "collection/Collection.h"
#include "object/Internal.h"      // Object::detail::InternalMemAlloc / InternalMemFree
#include "Object/factory_registration.h"
#include <cstdint>
#include <cstdio>
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch D
#include <new>
#include "ui/StringTable.h"   // B8 RT2: ::Object::SerializeExternal<UI::StringTable> (the downcast needs it complete)
#include "Tutorial/TutorialImage.h"   // B8 RT2: ::Object::SerializeExternalArray<T> (the downcast needs it complete)
#include "Tutorial/TutorialSetup.h"   // B8 RT2: ::Object::SerializeExternalArray<T> (the downcast needs it complete)
#include "Tutorial/TutorialGoal.h"   // B8 RT2: ::Object::SerializeExternalArray<T> (the downcast needs it complete)

// ============================================================
// Exe helpers, called by name: 0x14000bf40 ExternalPtr<StringTable>::Serialize
// (Object::SerializeExternal<UI::StringTable>) and the three ExternalPtrArray
// serializers (Object::SerializeExternalArray<T>), object/External.h; 0x140001bb0
// the DynArray resize (Reimpl_Array_Resize_Stride08), Game/exe_bodies.h.
// ============================================================

// ============================================================
// Construction / destruction (ENDGAME B.3.8 block GOAL)
// ============================================================
// ctor 0x1400e3480: External() (the base's real ctor), the stamp (the
// compiler's), then +0x20 .. +0x68 zeroed: the byte at +0x20, the qwords +0x28
// .. +0x68. The two Strings' default ctor and the three Arrays' default ctor
// are those zero stores (String() stores the null data pointer out of line).
TutorialStep::TutorialStep()
    : completed(false), _pad21{}, stringTable(nullptr) {}

// The dtor body 0x1400e33f0 is empty after the stamp: retail then frees the
// goals (+0x60), setups (+0x50) and images (+0x40) arrays (MemFree the buffer,
// zero _data, zero the _size/_capacity qword; no element destroy for the 8-byte
// ExternalPtr<T> entries) and runs ~String on the description (+0x38) and the
// title (+0x30), then JMPs to ~External: the compiler's member destruction in
// reverse declaration order.
TutorialStep::~TutorialStep() {}

// ============================================================
// TutorialStep::Reconstruct [6] 0x140028ca0 / Destruct [7] / Construct [8]
// ============================================================
// Reconstruct: `~T()` (slot 0, flags 0) (the tree called the dtor body directly — the
// same effect), then the ctor 0x1400e3480 on `this`.
Object::External* TutorialStep::Reconstruct() {
    this->~TutorialStep();
    return ::new (static_cast<void*>(this)) TutorialStep();
}
void TutorialStep::Destruct() { this->~TutorialStep(); }
Object::External* TutorialStep::Construct() {                     // 0x140028c90
    return ::new (static_cast<void*>(this)) TutorialStep();
}

// ============================================================
// TutorialStep::Serialize (vtable +0x28, 0x1400e32a0)
// ============================================================
// Fields serialized:
//   1. bool at +0x20 (completed)
//   2. ExternalPtr<StringTable> at +0x28
//   3. String at +0x30 (title)
//   4. String at +0x38 (description)
//   5. int count at +0x48, then DynArray resize +0x40, then array serialize (images)
//   6. int count at +0x58, then DynArray resize +0x50, then array serialize (setups)
//   7. int count at +0x68, then DynArray resize +0x60, then array serialize (goals)
bool TutorialStep::Serialize(IO::Stream& stream) {
    // The serializer.
    IO::Serializer serializer(stream, 0);

    // 1. Serialize bool at +0x20 (completed flag)
    serializer.Serialize(completed, nullptr);

    // 2. Serialize ExternalPtr<StringTable> at +0x28
    ::Object::SerializeExternal<UI::StringTable>(serializer, stringTable);   // 0x14000bf40

    // 3. / 4. Serialize the title (+0x30) and description (+0x38) Strings
    titleStr.Serialize(serializer);
    descStr.Serialize(serializer);

    // 5. / 6. / 7. The three ExternalPtr arrays: count, resize on load, elements.
    auto leg = [&serializer](auto& array, int32_t count) {
        int32_t localCount = count;
        serializer.Serialize(localCount, nullptr);
        // On load (encoding mode 0): resize the array
        if (static_cast<int>(serializer.GetDirection()) == 0) {   // IO::Serializer::GetDirection, retail 0x180006210
            Reimpl_Array_Resize_Stride08(&array, localCount);   // 0x140001bb0
        }
        return localCount;
    };
    int32_t n = leg(images, images.GetSize());
    ::Object::SerializeExternalArray<TutorialImage>(serializer, n, images.GetData());   // images  0x140028cf0
    n = leg(setups, setups.GetSize());
    ::Object::SerializeExternalArray<TutorialSetup>(serializer, n, setups.GetData());   // setups  0x140028f40
    n = leg(goals, goals.GetSize());
    ::Object::SerializeExternalArray<TutorialGoal>(serializer, n, goals.GetData());     // goals   0x140029190
    return true;
}

::Object::Base* TutorialStep::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(TutorialStep), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(::new (p) TutorialStep());
}
// createInPlace 0x140028c90.
::Object::Base* TutorialStep::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(::new (storage) TutorialStep());
}

DEFINE_EXE_TYPE(TutorialStep, "Object::External")
