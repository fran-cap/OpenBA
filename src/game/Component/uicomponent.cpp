// src/game/Component/uicomponent.cpp
// UIComponent (0x38 bytes), vtable at 0x1401649f0. Slot [3] (Persist) lives in
// uicomponent_serialize.cpp; slots [5]-[15] are Component's shared no-op bodies
// (inherited, B.3.8 CB1b deleted the unused UIComponent_SlotN copies).

// ============================================================================
// ENDGAME B.3.8 VIS — UIComponent as a REAL class (UIComponent.h).
//
// Before: COMPONENT_INERT_FACTORY_SER — a 48-slot table filled per create,
// Inert_ScalarDtor in slot 0 (only ~Internal).
// After: the compiler's 16-slot vtable and a real ctor/dtor chain
// (B38_RULES D1, D2); slot bodies are the ones the old table installed.
// ============================================================================
#include "Object/factory_registration.h"
#include "Component/UIComponent.h"
#include "Component/UIDescription.h"   // UIDescription::TypeId() (cell 0x1401c7f90)
#include <cstring>                     // std::memset (0x14003f010)

// 0x14012de00: Component() is the CALL 0x1401188b0; the 0x1401649f0 stamp is
// the compiler's; +0x28 = +0x30 = 0 (the inlined Array ctor); flags |= 0x80.
UIComponent::UIComponent() {
    OrFlagsInCtor(0x80);
}

// [3] 0x14012dd00 — UIComponent::Persist lives in uicomponent_serialize.cpp.

// [4] 0x14012dca0 — the description through IsDerivedFrom UIDescription (cell
// 0x1401c7f90; CMOVNZ, null on a miss, then read unguarded as retail reads it),
// `data` sized to its +0x20 count (0x14000a590, Array<uint8_t>::SetSize) and
// zeroed (the tail JMP 0x14003f010 = memset(data, 0, count)).
void UIComponent::Startup() {
    Description* obj = _description;
    UIDescription* desc = obj->IsDerivedFrom(UIDescription::TypeId())   // import 0x140152a98
                              ? static_cast<UIDescription*>(obj) : nullptr;
    data.SetSize(desc->_settingsBufferSize);
    std::memset(data._data, 0, static_cast<size_t>(desc->_settingsBufferSize));
}

// create 0x140039550: MemAlloc(0x38, 8), then the ctor.
::Object::Base* UIComponent::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(UIComponent), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) UIComponent());
}
// Retail's createInPlace is `Internal::CreateInPlaceObject` (returns null); the
// tree's in-place entry built the object, and that is kept (B38 B8-13).
::Object::Base* UIComponent::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) UIComponent());
}

DEFINE_EXE_TYPE(UIComponent, "Component")
