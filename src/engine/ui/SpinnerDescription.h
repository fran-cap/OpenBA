#pragma once
// ============================================================================
// src/engine/ui/SpinnerDescription.h — UI::SpinnerDescription, the REAL class
// ============================================================================
//
// ENDGAME B.3 step 6, block C5. RULE B6-8: this class had NO header; its layout
// lived as raw offsets in src/engine/ui/spinnerdescription.cpp, MOVED here.
// Evidence, all in that file (read from the retail bytes when it was ported):
//   * `??0SpinnerDescription@UI@@QEAA@XZ` 0x1800d89c0 — +0x68/+0x70/+0x78 = 0
//     (qwords), +0x80 = 1 (a DWORD; +0x84 is never written);
//   * `?Serialize@` 0x1800d8880 — three ExternalPtr<ElementDescription>
//     (0x180038f90) then one kScalarInt;
//   * `?CreateInstance@` 0x1800d86b0 — the three descriptions become children
//     0, 1, 2 of the SpinnerElement (the NumberEdit, the up and the down
//     button — SpinnerElement.h), and +0x80 overwrites the element's
//     `_increment`;
//   * sizeof 0x88 — `MOV EDX,0x88` in the slot-0 vector deleting dtor
//     0x18004b050 (B36_SLOTS.md section 1).
//
// PARENT, read (B36_SLOTS.md section 3), recorded not used (RULE B6-7):
//     ?_TypeName@SpinnerDescription@UI@@CAPEBDXZ       @0x18004ad20 -> "SpinnerDescription"
//     ?_ParentTypeName@SpinnerDescription@UI@@CAPEBDXZ @0x18004ad30 -> "ElementDescription"
//
// `??_7SpinnerDescription@UI@@6B@` @ 0x1801576c0, TEN slots:
//   [0] 0x18004b050 deleting dtor                   [1] 0x18004ad60 TypeName
//   [2] 0x18004ad40 Type            [3] 0x1800d8940 GetFileVersion (0x204)
//   [4] External::Compile (inherited, NOT declared — B6-1.5)
//   [5] 0x1800d8880 Serialize       [6] 0x18004aee0 Reconstruct
//   [7] 0x18004af10 Destruct        [8] 0x1800d86b0 CreateInstance RETSLOT (B6-2)
//   [9] 0x18004af00 Construct — INTRODUCED here (B6-1.1)
//
// ENDGAME B.3 step 9 (block UI-B, B39_RULES D1/D2) -- the paragraph below is
// HISTORY: the class is now CONCRETE. A real public ctor (retail's stores), its
// own slot 0 (the vector deleting dtor), a virtual ~SpinnerDescription holding
// the retired ??1 door's body, and the factory pair as the class's own statics;
// every live one wears the COMPILER's table. The minted `??_7SpinnerDescription@UI@@6B@`,
// its RT_DELDTOR row 0x18004b050 and the ctor / dtor / slot doors are retired.
// ABSTRACT (slot 0 `= 0`), NO real ctor. The destructor is declared only:
// `??1` (0x1800d8950) is the registered door.
// ============================================================================

#include <cstddef>
#include <cstdint>

#include "ElementDescription.h"

namespace UI {

class SpinnerDescription : public ElementDescription {
public:
    // ??0SpinnerDescription@UI@@QEAA@XZ 0x1800d89c0 -- the REAL ctor:
    // ElementDescription's stores (inlined in retail), the three part
    // descriptions zero and +0x80 = 1. +0x84 is written by nothing.
    SpinnerDescription() : ElementDescription() {
        _editDescription = nullptr;
        _upDescription   = nullptr;
        _downDescription = nullptr;
        _increment       = 1;
    }

    // [0] 0x18004b050 -- MSVC's VECTOR deleting destructor (retail; here the
    // virtual destructor, B3X W1) (`MOV EDX,0x88` = the element size).
    // `??1SpinnerDescription@UI@@UEAA@XZ` (0x1800d8950): VIRTUAL (the normal-virtual
    // cutover, B3X W1). Retail's body is in spinnerdescription.cpp;
    // the compiler chains the base's.
    ~SpinnerDescription() override;
    // ENDGAME B.3.9 TI-UI: ONE reflection block (B39 D6); `_typeInfo` is the
    // class's own record (DEFINE_TYPE in the class's TU), no longer minted.
    // retail: TypeName 0x18004ad60, Type 0x18004ad40, _TypeName 0x18004ad20, _ParentTypeName 0x18004ad30
    DECLARE_TYPE(SpinnerDescription)
public:
    int32_t     GetFileVersion() const override;                  // [3] 0x1800d8940
    bool        Serialize(IO::Stream& stream) override;                 // [5] 0x1800d8880 (IO::Stream&)
    Object::External* Reconstruct() override;                     // [6] 0x18004aee0
    void        Destruct() override;                              // [7] 0x18004af10
    Object::InternalPtr<Element>* CreateInstance(
        Object::InternalPtr<Element>* ret,
        const Dialog* dialog) const override;                     // [8] 0x1800d86b0 RETSLOT
    virtual Object::External* Construct();                        // [9] 0x18004af00

    SpinnerDescription(const SpinnerDescription& src);            // 0x18004af20  not ported
    SpinnerDescription& operator=(const SpinnerDescription& src); // 0x18004af80  not ported

private:

public:
    ElementDescription*    _editDescription;   // +0x68  ExternalPtr<UI::ElementDescription> -> children[0]
    ElementDescription*    _upDescription;     // +0x70  ExternalPtr<UI::ElementDescription> -> children[1]
    ElementDescription*    _downDescription;   // +0x78  ExternalPtr<UI::ElementDescription> -> children[2]
    int32_t  _increment;         // +0x80  ctor 1 -> SpinnerElement::_increment; kScalarInt
    uint32_t _pad84;             // +0x84  written by nothing
};

static_assert(sizeof(SpinnerDescription) == 0x88,
              "UI::SpinnerDescription is 0x88 (MOV EDX,0x88 in 0x18004b050)");
static_assert(offsetof(SpinnerDescription, _pad84) == 0x84, "SpinnerDescription::_pad84");
static_assert(alignof(SpinnerDescription) == 8, "UI::SpinnerDescription aligns to 8");
static_assert(offsetof(SpinnerDescription, _editDescription) == sizeof(ElementDescription), "");
static_assert(offsetof(SpinnerDescription, _editDescription) == 0x68, "");
static_assert(offsetof(SpinnerDescription, _upDescription)   == 0x70, "");
static_assert(offsetof(SpinnerDescription, _downDescription) == 0x78, "");
static_assert(offsetof(SpinnerDescription, _increment)       == 0x80, "");

}  // namespace UI
