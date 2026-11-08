// src/game/Component/uidescription.cpp
// Clean reimplementation of UIDescription (entity sub-description).
//
// UIDescription (0x48 bytes) — extends Object::External (0x20 base).
// Parent type name: "Description". Vtable @ 0x140164a70.  GetVersion = 4.
//
// NOTE: exe-defined type parented to "Description" (NOT Runtime UI::*). It
// registers its own factory (UIDescription_Register @ 0x14014fd10).
//
// Field layout (from Factory inline ctor + Serialize FUN_14012de80 + dtor
// FUN_140039700 / element-array cleanup FUN_1400397d0):
//   +0x20: int32  _settingsBufferSize (serialized LAST)             (default 0)
//   +0x28: void*  _descRef        ExternalPtr (Description)         (default 0)
//   +0x30: void*  _entries[]      element-array buffer (stride 0x28)(default 0)
//   +0x38: int32  _entryCount                                       (default 0)
//   +0x3c: int32  _entryCap                                         (default 0)
//   +0x40: int32  _displayPage    (serialized 2nd)                  (default 0)
//
// Per-element layout (stride 0x28):
//   +0x00: Object::Type  (4B, serialized via Object::Type::Serialize, import slot 0x1401516b0)
//   +0x08: ExternalPtr   (FUN_140027ad0 — identical stream)
//   +0x10: ExternalPtr   (FUN_140039840 — identical stream)
//   +0x18: ExternalPtr   (FUN_140027ad0 — identical stream)
//   +0x20: String
//
// Serialize (FUN_14012de80) stream order:
//   1. ExternalPtr (+0x28)         FUN_140028650
//   2. int (+0x40)
//   3. int count (+0x38)
//   4. (load) resize element array (+0x30) to count   FUN_140039a50
//   5. loop count: Type::Serialize, ExternalPtr x3, String::Serialize
//   6. int (+0x20)
//
// Decompiled from Application-x64.exe (READ-ONLY Ghidra).
//
// ENDGAME B.3.8 K1: a REAL class (UIDescription.h) with a compiler-generated vtable;
// the hand-built 11-slot table and its RegisterExeAddr(0x140164a70) row are gone
// (nothing in the tree read that address back — B38_RULES D5).

#include "Component/UIDescription.h"
#include "Component/UIComponent.h"
#include "object/Internal.h"      // Object::detail::InternalMemAlloc / InternalMemFree
#include "io/Serializer.h"
#include "Object/factory_registration.h"
#include "object/External.h"
#include "object/TypeInfo.h"
#include "Game/exe_bodies.h"
#include "options.h"
#include "system/String.h"
#include "system/MemoryAllocator.h"
#include "object/Type.h"          // RtObject::RtObject_Type_Serialize (import slot 0x1401516b0)
#include <cstdio>
#include <cstdint>
#include <new>
#include "ui/DialogControllerConfig.h"   // B8 RT2: ::Object::SerializeExternal<UI::DialogControllerConfig> (the downcast needs it complete)
#include "ui/ElementDescription.h"   // B8 RT2: ::Object::SerializeExternal<UI::ElementDescription> (the downcast needs it complete)
#include "ui/Dialog.h"   // B8 RT2: ::Object::SerializeExternal<UI::Dialog> (the downcast needs it complete)

// ---------------------------------------------------------------------------
// [8] exe 0x1400396e0 — UIDescription::GetComponentType, defined below
//
//   83 79 20 00     CMP  dword [RCX+0x20], 0
//   7E 0C           JLE  zero
//   8B 05 dc e8 18  MOV  EAX, [0x1401c7fc8]      ; g_typeId_UIComponent
//   89 02           MOV  [RDX], EAX
//   48 8B C2 C3     MOV  RAX, RDX ; RET
//   zero: C7 02 0 / MOV dword [RDX], 0 ; MOV RAX,RDX ; RET
//
// This slot is how ComponentEntity's post-load "create missing components from
// description" pass (0x1400c3140) recognises a component the save already
// restored. It was a ReturnNull stub, so the caller's out-param kept its 0 seed
// and the pass treated the entity's UIComponent as absent. That does not merely
// lose one component: the pass walks the description and the restored roster in
// LOCKSTEP, so once the first entry fails to match, every following description
// entry is compared against the same survivor and ALL of them are inserted. The
// trading post came out of the tutorial-4 save with 43 components — 21 freshly
// built empty ones in front of the 22 real restored ones — and every
// FindComponent-by-type on that entity (the finders at 0x140003f60 /
// 0x140004680) then returned the EMPTY duplicate: a ModelComponent with an
// identity transform and a MapComponent with a zeroed footprint. The building
// still drew (its real ModelComponent is the one in the Scene), but the tutorial
// arrow and the selection outline both placed themselves at the world origin.

// ctor (inlined into create 0x1400395b0 / createInPlace 0x140039620):
// `??0External@Object@@` (Description()), the stamps (the compiler's), then
// the defaults.
UIDescription::UIDescription()
    : Description(),
      _settingsBufferSize(0),
      _dialog(nullptr),
      _entries(nullptr),
      _entryCount(0),
      _entryCap(0),
      _displayPage(0) {}

// dtor (slot 0 0x140039700 minus the stamps): FUN_140039700 / element cleanup FUN_1400397d0.
// The compiler then chains ~Description -> ~External.
UIDescription::~UIDescription() {
    // Destroys each element's String (+0x20), frees the element-array buffer.
    UIDescriptionEntry* buf = _entries;
    int n = _entryCount;
    if (buf) {
        UIDescriptionEntry* e = buf;
        for (int i = 0; i < n; ++i, ++e) e->name.~String();
    }
    System::MemoryAllocator::MemFree(buf);
    _entries = nullptr;
    _entryCount = 0;
    _entryCap = 0;
}

// [5] 0x14012de80: stream order as retail.
bool UIDescription::Serialize(IO::Stream& stream) {
    IO::Serializer ser(stream, 0);

    // 1. _descRef ExternalPtr (+0x28). FUN_140028650 — identical stream layout
    //    to the registered Serialize2 (typeId trusted in standalone).
    ::Object::SerializeExternal<UI::Dialog>(ser, _dialog);
    // 2. int (+0x40)
    ser.Serialize(_displayPage, nullptr);
    // 3. count (+0x38)
    ser.Serialize(_entryCount, nullptr);
    int count = _entryCount;
    
    // 4. Ensure the element array (+0x30, stride 0x28) has room for `count` elements.
    // The exe calls a resize helper (FUN_140039a50) that is load/save-agnostic:
    // on LOAD `count` was just read from the stream and the array is empty (fresh
    // object) so it allocates; on SAVE the array already holds `count` elements so
    // it is a no-op. We reproduce that directly rather than sniff a load/save flag
    // — the previous `*(serObj+8)==0` heuristic was WRONG (that slot holds stream
    // payload bytes, never a clean 0/1), so it was ALWAYS false and the array was
    // NEVER allocated → every UIDescription dialog lost its sub-controllers.
    int cap = _entryCap;
    UIDescriptionEntry* arr = _entries;
    if (count > 0 && (arr == nullptr || cap < count)) {
        UIDescriptionEntry* buf = static_cast<UIDescriptionEntry*>(
            System::MemoryAllocator::MemAlloc((uint64_t)count * sizeof(UIDescriptionEntry), 8));
        _entries = buf;
        _entryCap = buf ? count : 0;
        if (buf) {
            UIDescriptionEntry* e = buf;
            for (int i = 0; i < count; ++i, ++e) {
                e->type._type = 0;             // the tree zeroes the whole first qword
                e->_pad04[0] = e->_pad04[1] = e->_pad04[2] = e->_pad04[3] = 0;
                e->element = nullptr;
                e->config = nullptr;
                e->tabButton = nullptr;
                ::new (&e->name) System::String();   // _data = null (0x180006760)
            }
        }
    }
    _entryCount = count;
    if (Opt().mapTrace)
        printf("[UIDescSer] self=%p count=%d entries=%p cap->%d\n",
               static_cast<void*>(this), count, static_cast<void*>(_entries), _entryCap);
    
    // 5. per-element serialize
    if (UIDescriptionEntry* e = _entries) {
        for (int i = 0; i < count; ++i, ++e) {
            RtObject::RtObject_Type_Serialize(&e->type._type, &ser);   // import slot 0x1401516b0
            ::Object::SerializeExternal<UI::ElementDescription>(ser, e->element);
            ::Object::SerializeExternal<UI::DialogControllerConfig>(ser, e->config);
            ::Object::SerializeExternal<UI::ElementDescription>(ser, e->tabButton);
            e->name.Serialize(ser);
        }
    }
    ser.Serialize(_settingsBufferSize, nullptr);
    return true;
}

// [6] 0x140039680: retail destroys and rebuilds in place: a virtual `this->~T()` (retail: slot 0, flags 0),
// then the ctor again on `this` (D9 #10).
Object::External* UIDescription::Reconstruct() {
    this->~UIDescription();
    return new (static_cast<void*>(this)) UIDescription();
}

// [7] 0x140001640: retail's shared body, a virtual `this->~T()` (retail: slot 0, flags 0) (D9 #10).
void UIDescription::Destruct() { this->~UIDescription(); }

// [8] 0x1400396e0: UIComponent's type id when _settingsBufferSize > 0, else 0 (see above).
// The tree read the id through a first-call cache over the exe cell 0x1401c7fc8;
// the by-name lookup is the same registered id, without the lazy static (B6-5).
Object::Type* UIDescription::GetComponentType(Object::Type* ret) const {
    ret->_type = (_settingsBufferSize > 0) ? UIComponent::TypeId() : 0u;
    return ret;
}

// [9] 0x140039620: placement-new on `this`, the same body as createInPlace (D9 #10).
Object::External* UIDescription::Construct() {
    return new (static_cast<void*>(this)) UIDescription();
}

// create 0x1400395b0: MemAlloc(0x48, 8), then the (inlined) ctor.
::Object::Base* UIDescription::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(UIDescription), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) UIDescription());
}
// createInPlace 0x140039620.
::Object::Base* UIDescription::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) UIDescription());
}

DEFINE_EXE_TYPE(UIDescription, "Description")
