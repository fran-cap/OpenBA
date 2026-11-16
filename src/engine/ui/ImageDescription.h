#pragma once
// ============================================================================
// src/engine/ui/ImageDescription.h — UI::ImageDescription, the REAL C++ class
// ============================================================================
//
// ENDGAME B.3 step 6, block C2. Before this header the layout lived only as
// raw `+0x..` reads in src/engine/ui/imagedescription.cpp (RULE B6-8); they
// are MOVED here under the class.
//
// PARENT `UI::ElementDescription` — READ, not inferred (B36_SLOTS.md section 3):
//     ?_ParentTypeName@ImageDescription@UI@@CAPEBDXZ  @0x180035ca0 -> "ElementDescription"
// RULE B5-15 / B6-7 — the two literals, RECORDED, NOT USED:
//     ?_TypeName@ImageDescription@UI@@CAPEBDXZ        @0x180035c90 -> "ImageDescription"
//     ?_ParentTypeName@ImageDescription@UI@@CAPEBDXZ  @0x180035ca0 -> "ElementDescription"
//
// `??_7ImageDescription@UI@@6B@` @ 0x180156110, TEN slots:
//   [0] 0x1800361f0  (unexported) vector deleting dtor — the class's own (B.3.9)
//   [1] 0x180035cd0  TypeName
//   [2] 0x180035cb0  Type
//   [3] 0x1801184a0  GetFileVersion  (`return 0x204`)
//   [4] 0x1800066e0  Object::External::Compile  INHERITED — NOT declared (B6-1.5)
//   [5] 0x1801184b0  Serialize
//   [6] 0x180035f60  Reconstruct
//   [7] 0x1800360c0  Destruct
//   [8] 0x180118910  CreateInstance(const Dialog*) -> InternalPtr<Element>  RETSLOT (B6-2)
//   [9] 0x180036010  Construct — INTRODUCED here (`virtual`, B6-1.1)
// ONE SUBCLASS (RULE B6-9): `AnimatedImageDescription` ("ImageDescription").
//
// THE NON-VIRTUAL `CreateInstance` IS EXPORTED. The tree called 0x180118570
// "UNEXPORTED, address-only"; B36_SLOTS.md section 6 lists it as
// `?CreateInstance@ImageDescription@UI@@QEBAXV?$InternalPtr@VImageElement@UI@@@Object@@@Z`.
// It is "stamp this description onto that image": both this class's and
// AnimatedImageDescription's slot 8 call it, and both slot-8 overrides HIDE it
// (qualify it from the subclass). The handle is by value in the source, so
// retail passes the ADDRESS of the caller's copy (`MOV RAX,[RDX]` at
// 0x18011857f, RULE B6-11) — declared as that pointer.
//
// ---------------------------------------------------------------------------
// LAYOUT — 0x80 bytes (`MOV EDX,0x80` at 0x18003620f), ElementDescription
// (0x68) plus four fields. The inlined ctor (0x18003608c..0x18003609e; the
// same stores sit in ??0AnimatedImageDescription at 0x1800cb763..0x1800cb775,
// read this pass) writes qword 0 at +0x68, qword 0 at +0x70 and ONE qword -1
// at +0x78 covering +0x78 AND +0x7c.
//   +0x68  _spriteSheet  ExternalPtr<UI::SpriteSheet> — serialized by
//                        0x180004590; the applier hands it to
//                        `SpriteSheet::GetSpriteIndexFromName` and copies it
//                        to `ImageElement::_sprite`.
//   +0x70  _spriteName   System::String — serialized by 0x1800c9930; the name
//                        the applier looks up; ~ImageDescription releases it.
//   +0x78  _width        one kScalarInt; the applier copies it to
//                        `ImageElement::_width` (+0xd4) — that member's name.
//   +0x7c  _height       one kScalarInt; copied to `ImageElement::_height`.
//
// (Until ENDGAME B.3.9:) ABSTRACT (slot 0 `= 0`); NO out-of-line default ctor in retail
// (`??0ImageDescription` 0x1801182d0 is the factory's door; Construct and
// Reconstruct inline the stores).
//
// ENDGAME B.3 step 9 (block UI-A, B39_RULES D1/D2): CONCRETE. The real public
// ctor is retail's `??0ImageDescription@UI@@QEAA@XZ` (0x1801182d0) minus its vptr stores;
// slot 0 is the class's own vector deleting dtor; ~ImageDescription is public and
// virtual. The factory pair is the class's own; the minted
// `??_7ImageDescription@UI@@6B@`, its RT_DELDTOR row, the factory-interpreter row and
// the doors are retired.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <type_traits>

#include "ElementDescription.h"
#include "../object/Internal.h"   // Object::detail::InternalMemFree

namespace UI {

class Dialog;
class Element;
class ImageElement;

class SpriteSheet;
class StringTable;
class Font;
class ElementDescription;

class ImageDescription : public ElementDescription {
public:
    // ??0ImageDescription@UI@@QEAA@XZ 0x1801182d0 — the REAL constructor, the
    // same stores Construct / Reconstruct / both factories inline
    // (0x18003602a..0x18003609e): ElementDescription()'s, then +0x68 and +0x70
    // QWORD zero and ONE QWORD -1 at +0x78 (covering +0x78 and +0x7c).
    ImageDescription() : ElementDescription() {
        _spriteSheet = nullptr;
        _width  = -1;
        _height = -1;
    }

    // ---- the ten slots, in measured order ---------------------------------
    // [0] 0x1800361f0 — MSVC's VECTOR deleting destructor, both arms exactly as
    // the retired RT_DELDTOR row ran them (`MOV EDX,0x80`, the element size).
    // `??1ImageDescription@UI@@UEAA@XZ` (0x180118370). VIRTUAL (B3X W1)
    // (imagedescription.cpp): releases the sprite name at +0x70; the compiler
    // chains ~ElementDescription (+0x60) and ~External. +0x68 is NOT released.
    ~ImageDescription() override;
    // ENDGAME B.3.9 TI-UI: ONE reflection block (B39 D6); `_typeInfo` is the
    // class's own record (DEFINE_TYPE in the class's TU), no longer minted.
    // retail: TypeName 0x180035cd0, Type 0x180035cb0, TypeId 0x180035cc0, _TypeName 0x180035c90
    //         _ParentTypeName 0x180035ca0, CreateNewObject 0x180035dc0, CreateInPlaceObject 0x180035eb0
    DECLARE_TYPE(ImageDescription)
public:
    int32_t     GetFileVersion() const override;                  // [3] 0x1801184a0
    // [4] Compile — Object::External's, inherited.
    bool        Serialize(IO::Stream& stream) override;                 // [5] 0x1801184b0  (IO::Stream&)
    Object::External* Reconstruct() override;                     // [6] 0x180035f60
    void        Destruct() override;                              // [7] 0x1800360c0
    Object::InternalPtr<Element>* CreateInstance(
        Object::InternalPtr<Element>* ret,
        const Dialog* dialog) const override;                     // [8] 0x180118910  RETSLOT
    virtual Object::External* Construct();                        // [9] 0x180036010  INTRODUCED

    // ---- non-virtual ------------------------------------------------------
    // 0x180118570 — the applier (see the banner). Ported (imagedescription.cpp).
    void CreateInstance(Object::InternalPtr<ImageElement>* element) const;

    // ---- exported, NOT PORTED as members ----------------------------------
    ImageDescription(const ImageDescription& src);                // 0x1800360d0  not ported
    ImageDescription& operator=(const ImageDescription& src);     // 0x180036140  not ported

private:

public:
    SpriteSheet* _spriteSheet; // +0x68 (B4 H4: typed)  ExternalPtr<UI::SpriteSheet> (one raw pointer)
    System::String _spriteName;  // +0x70  ctor 0x1801182d0, dtor 0x180118370 (B4 Y RT-UI: a real member)
    int32_t  _width;          // +0x78  ctor -1 (one qword store with +0x7c)
    int32_t  _height;         // +0x7c  ctor -1
};

static_assert(sizeof(ImageDescription) == 0x80,
              "UI::ImageDescription is 0x80 bytes (MOV EDX,0x80 at 0x18003620f)");
static_assert(alignof(ImageDescription) == 8, "UI::ImageDescription aligns to 8");
static_assert(std::has_virtual_destructor<ImageDescription>::value, "B3X W1: the normal-virtual cutover");
static_assert(!std::is_abstract<ImageDescription>::value, "B39 D1: concrete");
static_assert(offsetof(ImageDescription, _spriteSheet) == sizeof(ElementDescription),
              "ImageDescription's own fields begin at the end of ElementDescription");
static_assert(offsetof(ImageDescription, _spriteSheet) == 0x68, "");
static_assert(offsetof(ImageDescription, _spriteName)  == 0x70, "");
static_assert(offsetof(ImageDescription, _width)       == 0x78, "");
static_assert(offsetof(ImageDescription, _height)      == 0x7c, "");

}  // namespace UI
