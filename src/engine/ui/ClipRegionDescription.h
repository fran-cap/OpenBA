#pragma once
// ============================================================================
// src/engine/ui/ClipRegionDescription.h — UI::ClipRegionDescription, the REAL class
// ============================================================================
//
// ENDGAME B.3 step 6, block C1. `RtUI::ClipRegionDescription` in DescriptionsBatch1.h
// is an alias of this class; every offset is asserted below.
//
// PARENT `UI::ElementDescription` — READ:
//     ?_ParentTypeName@ClipRegionDescription@UI@@CAPEBDXZ  @0x180037960 -> "ElementDescription"
// RULE B5-15 / B6-7 — DEFINE_TYPE (clipregiondescription.cpp) defines both, and the class's
// own `_typeInfo` record (retail `?_typeInfo@ClipRegionDescription@UI@@…` 0x1801f9d30):
//     ?_TypeName@ClipRegionDescription@UI@@CAPEBDXZ        @0x180037950 -> "ClipRegionDescription"
//     ?_ParentTypeName@ClipRegionDescription@UI@@CAPEBDXZ  @0x180037960 -> "ElementDescription"
//
// `??_7ClipRegionDescription@UI@@6B@` @ 0x180156350, TEN slots (sizeof from
// `MOV EDX,0x78` in slot 0 0x180037eb0):
//
//   [0] 0x180037eb0  (unexported) vector deleting dtor — the class's own (B.3.9)
//   [1] 0x180037990  TypeName
//   [2] 0x180037970  Type
//   [3] 0x1800cc0c0  GetFileVersion  (`return 0x104`)
//   [4] 0x1800066e0  Object::External::Compile  INHERITED — NOT declared
//   [5] 0x1800cc010  Serialize
//   [6] 0x180037c30  Reconstruct
//   [7] 0x180037d90  Destruct
//   [8] 0x1800cbec0  CreateInstance(const Dialog*)  RETSLOT (B6-2)
//   [9] 0x180037ce0  Construct — INTRODUCED here (`virtual`, B6-1.1)
//
// `??0ClipRegionDescription@UI@@QEAA@XZ` IS EXPORTED (0x1800cc140,
// B36_SLOTS.md sections 6 and 7); clipregiondescription.cpp's old banner said
// the DLL exported none. Nothing registers it; `Construct`/`Reconstruct`
// open-code the same store sequence.
//
// ENDGAME B.3 step 9 (block UI-A, B39_RULES D1/D2): CONCRETE. The real public
// ctor is retail's `??0ClipRegionDescription@UI@@QEAA@XZ` (0x1800cc140) minus its vptr stores;
// slot 0 is the class's own vector deleting dtor; ~ClipRegionDescription is public and
// virtual. The factory pair is the class's own; the minted
// `??_7ClipRegionDescription@UI@@6B@`, its RT_DELDTOR row, the factory-interpreter row and
// the doors are retired.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <type_traits>

#include "ElementDescription.h"
#include "DescriptionsBatch1.h"   // RtUI::ClipRegionDescription aliases this class
#include "../object/Internal.h"   // Object::detail::InternalMemFree

namespace UI {

class SpriteSheet;
class StringTable;
class Font;
class ElementDescription;

class ClipRegionDescription : public ElementDescription {
public:
    // ??0ClipRegionDescription@UI@@QEAA@XZ 0x1800cc140 — the REAL constructor,
    // the store sequence Construct/Reconstruct and both factory steps inline
    // (0x180037cfa..0x180037d78): ElementDescription()'s stores, then the two
    // extents 0x64 (DWORDs) and the child ExternalPtr zero.
    ClipRegionDescription() : ElementDescription() {
        _width     = 0x64;                                     // 180037d66
        _height    = 0x64;                                     // 180037d6d
        _childDesc = nullptr;                                  // 180037d74
    }

    // ---- the ten slots, in measured order ---------------------------------
    // [0] 0x180037eb0 — MSVC's VECTOR deleting destructor, both arms exactly as
    // the retired RT_DELDTOR row ran them (`MOV EDX,0x78`, the element size).
    // `??1ClipRegionDescription@UI@@UEAA@XZ` (0x1800cc0d0). EMPTY (clipregiondescription.cpp): retail releases nothing of its own;
    // the compiler chains ~ElementDescription (+0x60) and ~External.
    ~ClipRegionDescription() override;
    // ENDGAME B.3.9 TI-UI: ONE reflection block (B39 D6); `_typeInfo` is the
    // class's own record (DEFINE_TYPE in the class's TU), no longer minted.
    // retail: TypeName 0x180037990, Type 0x180037970, TypeId 0x180037980, _TypeName 0x180037950
    //         _ParentTypeName 0x180037960, CreateNewObject 0x180037a80, CreateInPlaceObject 0x180037b80
    DECLARE_TYPE(ClipRegionDescription)
public:
    int32_t     GetFileVersion() const override;                  // [3] 0x1800cc0c0
    bool        Serialize(IO::Stream& stream) override;                 // [5] 0x1800cc010
    Object::External* Reconstruct() override;                     // [6] 0x180037c30
    void        Destruct() override;                              // [7] 0x180037d90
    Object::InternalPtr<Element>* CreateInstance(
        Object::InternalPtr<Element>* ret,
        const Dialog* dialog) const override;                     // [8] 0x1800cbec0  RETSLOT
    virtual Object::External* Construct();                        // [9] 0x180037ce0  INTRODUCED

    ClipRegionDescription(const ClipRegionDescription& src);            // 0x180037da0  not ported
    ClipRegionDescription& operator=(const ClipRegionDescription& src); // 0x180037df0  not ported


private:

public:
    // RtUI::ClipRegionDescription's names, unchanged.
    //
    // NAME-HIDING NOTE — ALL THREE: `_width` (+0x68) hides
    // `ElementDescription::_width` (+0x28), `_height` (+0x6c) hides
    // `ElementDescription::_height` (+0x2c), and `_childDesc` (+0x70) hides
    // `ElementDescription::_childDesc` (+0x48). Every body in this class means
    // its own three; spell the base's `ElementDescription::_width` etc.
    int32_t _width;                // +0x68  ctor 0x64; kScalarInt @ 0x1800cc067; to elem +0xc0
    int32_t _height;               // +0x6c  ctor 0x64; kScalarInt @ 0x1800cc07c; to elem +0xc4
    ElementDescription* _childDesc; // +0x70 (B4 H4: typed)  ExternalPtr<ElementDescription> (0x180038f90 @ 0x1800cc089)
};

static_assert(sizeof(ClipRegionDescription) == 0x78,
              "UI::ClipRegionDescription is 0x78 bytes (MOV EDX,0x78 in 0x180037eb0)");
static_assert(alignof(ClipRegionDescription) == 8, "UI::ClipRegionDescription aligns to 8");
static_assert(std::has_virtual_destructor<ClipRegionDescription>::value, "B3X W1: the normal-virtual cutover");
static_assert(!std::is_abstract<ClipRegionDescription>::value, "B39 D1: concrete");
static_assert(offsetof(ClipRegionDescription, _width) == sizeof(ElementDescription), "");
static_assert(offsetof(ClipRegionDescription, _width)     == 0x68, "");
static_assert(offsetof(ClipRegionDescription, _height)    == 0x6c, "");
static_assert(offsetof(ClipRegionDescription, _childDesc) == 0x70, "");

}  // namespace UI
