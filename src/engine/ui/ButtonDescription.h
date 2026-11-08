#pragma once
// ============================================================================
// src/engine/ui/ButtonDescription.h — UI::ButtonDescription, the REAL class
// ============================================================================
//
// ENDGAME B.3 step 6, block C1. The layout is `RtUI::ButtonDescription` in
// DescriptionsBatch1.h (the ten-class POD header four clusters share and
// nobody edits, RULE B6-4.4); this header declares the class under retail's
// name with the SAME fields and cross-asserts every offset against that POD.
//
// PARENT `UI::ElementDescription` — READ, not inferred:
//     ?_ParentTypeName@ButtonDescription@UI@@CAPEBDXZ  @0x180047e00 -> "ElementDescription"
// RULE B5-15 / B6-7 — the two literals, RECORDED, NOT USED (slots 1/2 keep
// reading the minted `?_typeInfo@ButtonDescription@UI@@…`, 0x1801fa900):
//     ?_TypeName@ButtonDescription@UI@@CAPEBDXZ        @0x180047df0 -> "ButtonDescription"
//     ?_ParentTypeName@ButtonDescription@UI@@CAPEBDXZ  @0x180047e00 -> "ElementDescription"
//
// `??_7ButtonDescription@UI@@6B@` @ 0x180157370, TEN slots (B36_SLOTS.md
// section 1; sizeof from `MOV EDX,0xa0` in slot 0 0x1800481a0):
//
//   [0] 0x1800481a0  (unexported) vector deleting dtor — the class's own (B.3.9)
//   [1] 0x180047e30  TypeName
//   [2] 0x180047e10  Type
//   [3] 0x1801113d0  GetFileVersion  (`return 0x104`)
//   [4] 0x1800066e0  Object::External::Compile  INHERITED — NOT declared (B6-1.5)
//   [5] 0x1801113e0  Serialize
//   [6] 0x180047fb0  Reconstruct
//   [7] 0x180047fe0  Destruct
//   [8] 0x180111510  CreateInstance(const Dialog*) -> InternalPtr<Element>  RETSLOT (B6-2)
//   [9] 0x180047fd0  Construct — INTRODUCED here (`virtual`, B6-1.1)
//
// TWO SUBCLASSES (RULE B6-9): `CheckDescription` and `ComboDescription`, both
// "ButtonDescription". Both call this class's NON-VIRTUAL
// `CreateInstance(InternalPtr<ButtonElement>, const Dialog*)` (0x1801108d0)
// and its `Serialize` first; both are name-hidden by their own slot-8
// override, so both call sites are QUALIFIED.
//
// THE KEY FUNCTION. `TypeName` is this class's key function, so GCC emits
// `vtable for UI::ButtonDescription` in buttondescription.cpp. Its typeinfo
// names `typeinfo for UI::ElementDescription`, whose key function
// (`ElementDescription::TypeName`) is still a dialog.cpp door — that does NOT
// break the link: mingw-w64 GCC emits every typeinfo it needs as a COMDAT in
// the using TU (measured with `nm` on a two-class probe, 2026-09-25). The
// emitted table is never stamped on anything; live objects wear the minted one.
// The DESTRUCTOR is the one member that stays undefined: a body would chain to
// `~ElementDescription`, which is not defined in the real build.
//
// (Until ENDGAME B.3.9:) ABSTRACT: slot 0 `= 0`; no real ctor (`??0ButtonDescription` 0x180110b10 is
// the factory_ui1.cpp door that `Construct`/`Reconstruct` tail-jump into).
//
// ENDGAME B.3 step 9 (block UI-A, B39_RULES D1/D2): CONCRETE. The real public
// ctor is retail's `??0ButtonDescription@UI@@QEAA@XZ` (0x180110b10) minus its vptr stores;
// slot 0 is the class's own vector deleting dtor; ~ButtonDescription is public and
// virtual. The factory pair is the class's own; the minted
// `??_7ButtonDescription@UI@@6B@`, its RT_DELDTOR row, the factory-interpreter row and
// the doors are retired.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <type_traits>

#include "ElementDescription.h"
#include "DescriptionsBatch1.h"   // kVtbl* names; RtUI::ButtonDescription aliases this class
#include "../object/Internal.h"   // Object::detail::InternalMemFree

namespace UI {

class Dialog;          // block B
class Element;
class ButtonElement;   // ButtonElement.h

class ButtonDescription : public ElementDescription {
public:
    // ??0ButtonDescription@UI@@QEAA@XZ 0x180110b10 — the REAL constructor:
    // ElementDescription()'s stores, then +0x68..+0x98 as seven QWORD zeros
    // (0x180110b8d..0x180110bae): the five child handles and the four colours.
    ButtonDescription() : ElementDescription() {
        for (int i = 0; i < 5; ++i) _childDesc[i] = nullptr;
        _enabledColor = 0;
        _disabledColor = 0;
        _highlightedColor = 0;
        _pressedColor = 0;
    }

    // ---- the ten slots, in measured order ---------------------------------
    // [0] 0x1800481a0 — MSVC's VECTOR deleting destructor, both arms exactly as
    // the retired RT_DELDTOR row ran them (`MOV EDX,0xa0`, the element size).
    // `??1ButtonDescription@UI@@UEAA@XZ` (0x180110bd0). EMPTY
    // (buttondescription.cpp): nothing of its own is released; the compiler
    // chains ~ElementDescription (+0x60) and ~External.
    ~ButtonDescription() override;
    // ENDGAME B.3.9 TI-UI: ONE reflection block (B39 D6); `_typeInfo` is the
    // class's own record (DEFINE_TYPE in the class's TU), no longer minted.
    // retail: TypeName 0x180047e30, Type 0x180047e10, TypeId 0x180047e20, _TypeName 0x180047df0
    //         _ParentTypeName 0x180047e00, CreateNewObject 0x180047f20, CreateInPlaceObject 0x180047fa0
    DECLARE_TYPE(ButtonDescription)
public:
    int32_t     GetFileVersion() const override;                  // [3] 0x1801113d0
    // [4] Compile — Object::External's, inherited.
    bool        Serialize(IO::Stream& stream) override;                 // [5] 0x1801113e0  (IO::Stream&)
    Object::External* Reconstruct() override;                     // [6] 0x180047fb0
    void        Destruct() override;                              // [7] 0x180047fe0
    Object::InternalPtr<Element>* CreateInstance(
        Object::InternalPtr<Element>* ret,
        const Dialog* dialog) const override;                     // [8] 0x180111510  RETSLOT
    virtual Object::External* Construct();                        // [9] 0x180047fd0  INTRODUCED

    // ---- non-virtual ------------------------------------------------------
    // `?CreateInstance@ButtonDescription@UI@@QEBAXV?$InternalPtr@VButtonElement@UI@@@Object@@PEBVDialog@2@@Z`
    // (0x1801108d0) — "stamp this description onto that button": the base
    // applier, the five child elements, the label colour and the four state
    // colours. EXPORTED (B36_SLOTS.md section 6), although the tree registered
    // it by address only and called it "unexported". The handle is taken BY
    // VALUE in the source, so retail passes the ADDRESS of the caller's copy
    // (`MOV RAX,[RDX]` at entry) — declared as the pointer the register holds.
    void CreateInstance(Object::InternalPtr<ButtonElement>* element,
                        const Dialog* dialog) const;              // 0x1801108d0

    ButtonDescription(const ButtonDescription& src);              // 0x180047ff0  not ported
    ButtonDescription& operator=(const ButtonDescription& src);   // 0x180048090  not ported


private:

public:
    // ---- the 0x38 bytes after ElementDescription — RtUI::ButtonDescription's
    // names, unchanged (the evidence per field is on the POD).
    //
    // NAME-HIDING NOTE: `_childDesc` here (+0x68, five of them) HIDES
    // `ElementDescription::_childDesc` (+0x48) inside this class and its two
    // subclasses. Every body in this class means the five; spell the base's
    // one `ElementDescription::_childDesc` if it is ever needed.
    ElementDescription* _childDesc[5]; // +0x68 .. +0x88 (B4 H4: typed)  ExternalPtr<ElementDescription>:
                                   //   [0..3] the four state backgrounds, [4] the label
    uint32_t _enabledColor;              // +0x90  state-0 colour; also the label's slot-10 arg (B4: kScalarUInt, retail's type)
    uint32_t _disabledColor;              // +0x94  state-1 colour
    uint32_t _highlightedColor;              // +0x98  state-2 colour
    uint32_t _pressedColor;              // +0x9c  state-3 colour
};

static_assert(sizeof(ButtonDescription) == 0xa0,
              "UI::ButtonDescription is 0xa0 bytes (MOV EDX,0xa0 in 0x1800481a0)");
static_assert(alignof(ButtonDescription) == 8, "UI::ButtonDescription aligns to 8");
static_assert(std::has_virtual_destructor<ButtonDescription>::value, "B3X W1: the normal-virtual cutover");
static_assert(!std::is_abstract<ButtonDescription>::value, "B39 D1: concrete");
static_assert(offsetof(ButtonDescription, _childDesc) == sizeof(ElementDescription), "");
// The POD offsets, absolute (RtUI::ButtonDescription is an ALIAS since fragment pass F2).
static_assert(offsetof(ButtonDescription, _childDesc) == 0x68, "");
static_assert(offsetof(ButtonDescription, _enabledColor)    == 0x90, "");
static_assert(offsetof(ButtonDescription, _disabledColor)    == 0x94, "");
static_assert(offsetof(ButtonDescription, _highlightedColor)    == 0x98, "");
static_assert(offsetof(ButtonDescription, _pressedColor)    == 0x9c, "");

}  // namespace UI
