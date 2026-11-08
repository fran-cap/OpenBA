// src/game/Game/toolbar.cpp
// Clean reimplementation of Toolbar game logic.
//
// Toolbar (0x68 bytes) -- toolbar UI container.
// Extends Object::External (0x20 base).
// Vtable at 0x140160120.
//
// Contains toolbar items, layout configuration, selection state,
// and references to building descriptions.
//
// Required so the in-game resource preload (UpdateLoop::LoadResources) can
// instantiate a Toolbar when External::Load reads Game\Toolbar.rsc out of the
// resource tree. Without the reflected type the loader hits a NullFactory ->
// IsDerivedFrom(NULL) -> crash.
//
// ENDGAME B.3.8 block LOOP: a REAL class (src/game/Game/Toolbar.h, field layout
// and retail slot map there) with a compiler-generated vtable. Gone: the
// hand-built 9-slot `g_Toolbar_Vtable` (three null slots: TypeName, Compile,
// Destruct), the lazy vtable resolves in the old Init/Slot8, the lazily-resolved
// typed-ExternalPtr door (a non-atomic first-call cache, B6-5) and the C entry
// points.
//
// Decompiled from Application-x64.exe.

#include "Game/Toolbar.h"
#include "object/External.h"
#include "object/TypeInfo.h"
#include "Game/exe_bodies.h"
#include "system/String.h"
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch B
#include "object/Internal.h" // Object::detail::InternalMemAlloc / InternalMemFree
#include <cstdint>
#include <new>
#include "ui/SpriteSheet.h"   // B8 RT2: ::Object::SerializeExternal<UI::SpriteSheet> (the downcast needs it complete)
#include "Component/ComponentDescription.h"   // B8 RT2: ::Object::SerializeExternal<ComponentDescription> (the downcast needs it complete)
#include "ui/StringTable.h"   // B8 RT2: ::Object::SerializeExternal<UI::StringTable> (the downcast needs it complete)

// FUN_140024c70 — Object::ExternalPtr<Toolbar>::Serialize (serialize_externals.cpp;
// registered at that address in reg_game_leaf.cpp). Called directly: the old
// lazily-resolved pointer resolved to it.

// ============================================================
// Toolbar::Toolbar (0x1400d2130)
// ============================================================
// External() and the 0x140160120 stamp are the compiler's; then the defaults.
Toolbar::Toolbar() {
    textTable      = nullptr;  // +0x20: ExternalPtr
    spriteSheet      = nullptr;  // +0x28: ExternalPtr
    // +0x30 / +0x38: the two String members' ctors (retail inlines them as the zero stores)
    shortcutKey      = 8;        // +0x40: default 8
    autoShortcut      = true;     // +0x44: default true (0x101 as uint16 = both bytes set)
    visible      = true;     // +0x45: default true
    actionType      = 0;        // +0x48: zeroed
    selectedIndex = -1;       // +0x4C: -1 sentinel (no selection)
    componentDescription      = nullptr;  // +0x58: ExternalPtr
    parentToolbar      = nullptr;  // +0x60: ExternalPtr
}

// ============================================================
// Toolbar::~Toolbar / DeletingDtor (vtable +0x00)
// ============================================================
// Original: 0x140024c10
// Destroys two String fields (+0x38, +0x30) in reverse order; External::
// ~External is the compiler's chain; MemFree if flags & 1. The two String
// members' dtors are that order (reverse declaration).
Toolbar::~Toolbar() {
}

// ============================================================
// Toolbar::Serialize (vtable +0x28, Slot5) -- Serialize fields
// ============================================================
// Original: 0x1400d2010
bool Toolbar::Serialize(IO::Stream& stream) {
    // The serializer on the stack, version 0; destroyed at the return (retail's order).
    IO::Serializer ser(stream, 0);
    IO::Serializer* serializer = &ser;

    // ExternalPtr fields
    ::Object::SerializeExternal<UI::StringTable>(*serializer, textTable);
    ::Object::SerializeExternal<UI::SpriteSheet>(*serializer, spriteSheet);

    // String fields
    textKey.Serialize(*serializer);
    spriteName.Serialize(*serializer);

    // Encoding vcall 0x98 (the uint door) for +0x48
    serializer->Serialize(actionType, nullptr);

    // Typed ExternalPtr serialize for Toolbar at +0x60
    ::Object::SerializeExternal<Toolbar>(*serializer, parentToolbar);

    // ExternalPtr<BuildingDescription> at +0x58
    ::Object::SerializeExternal<ComponentDescription>(*serializer, componentDescription);

    // Encoding vcall 0x98 (the uint door) for +0x40
    serializer->Serialize(shortcutKey, nullptr);

    // Bool fields
    serializer->Serialize(visible, nullptr);
    serializer->Serialize(autoShortcut, nullptr);

    // Int fields
    serializer->Serialize(selectedIndex, nullptr);
    serializer->Serialize(sortPriority, nullptr);

    return true;   // ~Serializer
}

// ============================================================
// Toolbar::Reconstruct (vtable +0x30, Slot6) -- Reset to defaults
// ============================================================
// Original: 0x140024bf0. Destroys (slot 0, flags 0) then re-constructs in place.
Object::External* Toolbar::Reconstruct() {
    this->~Toolbar();
    return ::new (static_cast<void*>(this)) Toolbar();
}

// [7] 0x140001640 — the shared body `MOV RAX,[RCX]; XOR EDX,EDX; JMP [RAX]`:
// slot 0 with flags 0 (destroy, no free).
void Toolbar::Destruct() { this->~Toolbar(); }

// ============================================================
// Toolbar::Construct (vtable +0x40)
// ============================================================
// Original: 0x140024be0 — placement-new of the class on `this` (the same
// defaults as the ctor; MSVC's placement new null-checks `this`).
Object::External* Toolbar::Construct() {
    return ::new (static_cast<void*>(this)) Toolbar();
}

// ============================================================
// Reflection: create / createInPlace, the registration
// ============================================================
// Original registration (Toolbar_RegisterType @0x14014d410):
//   BaseTypeInfo(&TypeInfo_Toolbar, "Toolbar", "Object::External",
//                null, Toolbar_Factory(0x140024ba0), Toolbar_Cleanup).
::Object::Base* Toolbar::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(Toolbar), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(::new (p) Toolbar());
}
::Object::Base* Toolbar::CreateInPlaceObject(void* memory) {
    if (!memory) return nullptr;
    return static_cast<Object::Base*>(::new (memory) Toolbar());
}


DEFINE_EXE_TYPE(Toolbar, "Object::External")
