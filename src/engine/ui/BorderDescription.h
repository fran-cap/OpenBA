#pragma once
// ============================================================================
// src/engine/ui/BorderDescription.h — UI::BorderDescription, the REAL class
// ============================================================================
//
// ENDGAME B.3 step 6, block C1. `RtUI::BorderDescription` in DescriptionsBatch1.h
// is an alias of this class.
//
// PARENT `UI::ElementDescription` — READ:
//     ?_ParentTypeName@BorderDescription@UI@@CAPEBDXZ  @0x1800409b0 -> "ElementDescription"
// RULE B5-15 / B6-7 — RECORDED, NOT USED (`?_typeInfo@BorderDescription@UI@@…`
// 0x1801fa890; the class's own record is DEFINE_TYPE's):
//     ?_TypeName@BorderDescription@UI@@CAPEBDXZ        @0x1800409a0 -> "BorderDescription"
//     ?_ParentTypeName@BorderDescription@UI@@CAPEBDXZ  @0x1800409b0 -> "ElementDescription"
//
// `??_7BorderDescription@UI@@6B@` @ 0x180156980, TEN slots (sizeof from
// `MOV EDX,0xb8` in slot 0 0x180040ff0):
//
//   [0] 0x180040ff0  (unexported) vector deleting dtor — the class's own (B.3.9)
//   [1] 0x1800409e0  TypeName
//   [2] 0x1800409c0  Type
//   [3] 0x18010fe10  GetFileVersion  (`return 0x104`)
//   [4] 0x1800066e0  Object::External::Compile  INHERITED — NOT declared
//   [5] 0x18010fe20  Serialize
//   [6] 0x180040b60  Reconstruct
//   [7] 0x180040b90  Destruct
//   [8] 0x18010ff40  CreateInstance(const Dialog*)  RETSLOT (B6-2)
//   [9] 0x180040b80  Construct — INTRODUCED here (`virtual`, B6-1.1)
//
// ENDGAME B.3 step 9 (block UI-A, B39_RULES D1/D2): CONCRETE. The real public
// ctor is retail's `??0BorderDescription@UI@@QEAA@XZ` (0x18010fa50) minus its
// vptr stores; slot 0 is the class's own vector deleting dtor; ~BorderDescription
// is public, virtual and holds the retired ??1 door's body. The factory
// pair is the class's own (borderdescription.cpp), replacing the FACTUI1 row;
// the minted `??_7BorderDescription@UI@@6B@` and its RT_DELDTOR row are retired.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <type_traits>

#include "ElementDescription.h"
#include "DescriptionsBatch1.h"   // RtUI::BorderDescription aliases this class
#include "../object/Internal.h"   // Object::detail::InternalMemFree

namespace UI {

class SpriteSheet;
class StringTable;
class Font;
class ElementDescription;

class BorderDescription : public ElementDescription {
public:
    // ??0BorderDescription@UI@@QEAA@XZ 0x18010fa50 — the REAL constructor. The
    // base's stores are ElementDescription(); its own are +0x68..+0xb0 zero
    // (0x18010facd..0x18010fb03: the ExternalPtr and the nine String bodies).
    BorderDescription() : ElementDescription() {
        _spriteSheet = nullptr;
        // +0x70..+0xb0 `_spriteName[9]`: each String() (the zero store) runs
        // as a member, [0] first.
    }

    // ---- the ten slots, in measured order ---------------------------------
    // [0] 0x180040ff0 — MSVC's VECTOR deleting destructor, both arms exactly as
    // the retired RT_DELDTOR row ran them (`MOV EDX,0xb8`, the element size).
    // `??1BorderDescription@UI@@UEAA@XZ` (0x18010fb20). VIRTUAL (B3X W1);
    // borderdescription.cpp holds retail's body (the nine names, descending);
    // the compiler chains ~ElementDescription (+0x60) and ~External.
    ~BorderDescription() override;
    // ENDGAME B.3.9 TI-UI: ONE reflection block (B39 D6); `_typeInfo` is the
    // class's own record (DEFINE_TYPE in the class's TU), no longer minted.
    // retail: TypeName 0x1800409e0, Type 0x1800409c0, _TypeName 0x1800409a0, _ParentTypeName 0x1800409b0
    //         CreateNewObject 0x180040ad0, CreateInPlaceObject 0x180040b50
    DECLARE_TYPE(BorderDescription)
public:
    int32_t     GetFileVersion() const override;                  // [3] 0x18010fe10
    bool        Serialize(IO::Stream& stream) override;                 // [5] 0x18010fe20
    Object::External* Reconstruct() override;                     // [6] 0x180040b60
    void        Destruct() override;                              // [7] 0x180040b90
    Object::InternalPtr<Element>* CreateInstance(
        Object::InternalPtr<Element>* ret,
        const Dialog* dialog) const override;                     // [8] 0x18010ff40  RETSLOT
    virtual Object::External* Construct();                        // [9] 0x180040b80  INTRODUCED

    BorderDescription(const BorderDescription& src);              // 0x180040ba0  not ported
    BorderDescription& operator=(const BorderDescription& src);   // 0x180040ce0  not ported

    // 0x1800409d0 — the type id. Retail's factory pair (B39_RULES D2):
    // MemAlloc(0xb8, 8) + the ctor, and the null-tested in-place ctor; both are
    // declared by DECLARE_TYPE above and defined in borderdescription.cpp.

private:

public:
    // RtUI::BorderDescription's names, unchanged: one ExternalPtr<SpriteSheet>
    // and NINE System::String sprite names, the pieces of a nine-slice border,
    // in BorderElement's row-major order.
    SpriteSheet* _spriteSheet;     // +0x68 (B4 H4: typed)  LEA RCX,[RDI+0x68] @ 0x18010fe6f
    System::String _spriteName[9]; // +0x70 .. +0xb0  @ 0x18010fe7d .. 0x18010feff; dtor releases [8]..[0]
};

static_assert(sizeof(BorderDescription) == 0xb8,
              "UI::BorderDescription is 0xb8 bytes (MOV EDX,0xb8 in 0x180040ff0)");
static_assert(alignof(BorderDescription) == 8, "UI::BorderDescription aligns to 8");
static_assert(std::has_virtual_destructor<BorderDescription>::value, "B3X W1: the normal-virtual cutover");
static_assert(!std::is_abstract<BorderDescription>::value, "B39 D1: concrete");
static_assert(offsetof(BorderDescription, _spriteSheet) == sizeof(ElementDescription), "");
static_assert(offsetof(BorderDescription, _spriteSheet) == 0x68, "");
static_assert(offsetof(BorderDescription, _spriteName)  == 0x70, "");

}  // namespace UI
