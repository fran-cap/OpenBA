#pragma once
// ============================================================================
// src/engine/ui/LayoutDescription.h — UI::LayoutDescription, the REAL C++
// class (ENDGAME B.3 step 6, block C4)
// ============================================================================
//
// PARENT `UI::ElementDescription` — READ, not inferred:
//     ?_ParentTypeName@LayoutDescription@UI@@CAPEBDXZ @0x180042d10 -> "ElementDescription"
//
// RULE B5-15 / B6-7 — THE TWO NAME LITERALS (retail's `?_typeInfo@LayoutDescription@UI@@…`,
// 0x1801fa078); slots 1/2 read the class's own DEFINE_TYPE record, which carries them:
//     ?_TypeName@LayoutDescription@UI@@CAPEBDXZ       @0x180042d00 -> "LayoutDescription"
//     ?_ParentTypeName@LayoutDescription@UI@@CAPEBDXZ @0x180042d10 -> "ElementDescription"
//
// `??_7LayoutDescription@UI@@6B@` @ 0x180156cc0, TEN slots (B36_SLOTS.md
// section 1; sizeof from `MOV EDX,0x78` in slot 0 0x1800431d0):
//   [0] 0x1800431d0 deleting dtor (row)  [1] 0x180042d40 TypeName
//   [2] 0x180042d20 Type                 [3] 0x1800d3a00 GetFileVersion
//   [4] 0x1800066e0 External::Compile INHERITED
//   [5] 0x1800d3940 Serialize            [6] 0x180042fc0 Reconstruct
//   [7] 0x180043100 Destruct             [8] 0x1800d33a0 CreateInstance RETSLOT
//   [9] 0x180043060 Construct            INTRODUCED by the leaf (RULE B6-1.1)
//
// ENDGAME B.3 step 9 (block UI-B, B39_RULES D1/D2) -- the paragraph below is
// HISTORY: the class is now CONCRETE. A real public ctor (retail's stores), its
// own slot 0 (the vector deleting dtor), a virtual ~LayoutDescription holding
// the retired ??1 door's body, and the factory pair as the class's own statics;
// every live one wears the COMPILER's table. The minted `??_7LayoutDescription@UI@@6B@`,
// its RT_DELDTOR row 0x1800431d0 and the ctor / dtor / slot doors are retired.
// ABSTRACT (slot 0 pure), no real ctor. `??0LayoutDescription@UI@@QEAA@XZ`
// (0x1800d3a90) is ui_factory_wave28.cpp's door; Construct and Reconstruct
// INLINE the same stores (0x180043076.., 0x180042fde..).
//
// LAYOUT (RULE B6-8) — MOVED here from layoutdescription.cpp's raw offsets.
// ElementDescription (0x68) plus ONE array, `Array<Entry>` at +0x68 (the inlined
// ctor: `MOV qword [p+0x68],0 ; MOV qword [p+0x70],0` at 0x1800430e2/e6).
// Entry is 0x38 bytes; its fields are named from the entry serializer
// (0x180043f50: ExternalPtr 0x180038f90 over +0x00, System::String 0x1800c9930
// over +0x08/+0x10/+0x18, kScalarUInt +0x20/+0x24, kScalarInt +0x28/+0x2c,
// kScalarBool +0x30) and from what CreateInstance (0x1800d33a0) does with each:
// +0x08 is the name OTHER entries look up; +0x10/+0x18 are the names this entry
// is positioned relative to, gated by +0x20/+0x24 being non-zero; +0x20..+0x30
// are copied into LayoutElement's per-child record (src/engine/ui/
// LayoutElement.h), where GetChildLayout (0x1800d2dc0) reads +0x00 as the X
// alignment and +0x04 as the Y alignment — and the exported
// `?GetChildLayoutX@LayoutElement@UI@@AEBAHW4LayoutHorizontal@LayoutDescription@2@HHHH@Z`
// / `…GetChildLayoutY@…W4LayoutVertical@…` name those two as the enums
// `LayoutDescription::LayoutHorizontal` / `LayoutVertical`. Their enumerator
// NAMES are not recoverable; no names are invented. +0x31..+0x37 are written by
// nothing.
// ============================================================================

#include <cstddef>
#include <cstdint>

#include "ElementDescription.h"
#include "../collection/Collection.h"
#include "../system/String.h"

// B4 Y RT-UI: the hand-derived row `_entries` is built and destroyed through
// (src/engine/collection/array.cpp, retail SetSize 0x180043d80), named here
// so the typed member can carry it as its spec.
namespace rtcoll {
extern const ArraySpec kSpec_LayoutEntry38;   // 0x180043d80  Array<LayoutDescription::Entry>
}  // namespace rtcoll

namespace UI {

class Dialog;
class Element;

class LayoutDescription : public ElementDescription {
public:
    // `W4` enums: four bytes, passed in a register. Values 1..5 are the five
    // alignment arms GetChildLayout's AlignAxis switches on; names unknown.
    enum LayoutHorizontal : int32_t {};
    enum LayoutVertical   : int32_t {};

    // One child entry, `Array<Entry>` stride 0x38 (0x180043d80's spec).
    // B4 Y RT-UI: the three names are System::String. An entry is only ever
    // built, copied and destroyed by kSpec_LayoutEntry38's arms (init: the
    // new-tail arm of 0x180043d80; assign 0x180043980; destroy 0x180044050,
    // +0x18 then +0x10 then +0x08), which store `_data` raw; no C++
    // construction or destruction of an Entry runs, so each name is released
    // once.
    struct Entry {
        ElementDescription* desc;      // +0x00  ExternalPtr<UI::ElementDescription> (B4 H4)
        System::String   name;         // +0x08  what siblings look up
        System::String   relativeToX;  // +0x10  gated by `horizontal`
        System::String   relativeToY;  // +0x18  gated by `vertical`
        LayoutHorizontal horizontal;   // +0x20  kScalarUInt; -> record +0x00
        LayoutVertical   vertical;     // +0x24  kScalarUInt; -> record +0x04
        int32_t          offsetX;      // +0x28  kScalarInt;  -> record +0x10
        int32_t          offsetY;      // +0x2c  kScalarInt;  -> record +0x14
        bool             force;        // +0x30  kScalarBool (B4: retail's type); -> record +0x18 (laid out even when hidden)
        uint8_t          _pad31[7];    // +0x31  written by nothing
    };

    // ---- the ten slots, in measured order ----------------------------------
    // ??0LayoutDescription@UI@@QEAA@XZ 0x1800d3a90 -- the REAL ctor (retail
    // also inlines it into Construct / Reconstruct): ElementDescription's
    // stores, then _entries' three zero stores (the Array's own ctor).
    LayoutDescription() : ElementDescription() {}

    // [0] 0x1800431d0 -- MSVC's VECTOR deleting destructor (retail; here the
    // virtual destructor, B3X W1) (`MOV EDX,0x78` = the element size).
    // `??1LayoutDescription@UI@@UEAA@XZ` (0x1800d3a10): VIRTUAL (the normal-virtual
    // cutover, B3X W1). Its body is empty: `_entries`' ~Array (0x180043a00),
    // then the base's _textName and ~External, all compiler-chained.
    ~LayoutDescription() override;
    // ENDGAME B.3.9 TI-UI: ONE reflection block (B39 D6); `_typeInfo` is the
    // class's own record (DEFINE_TYPE in the class's TU), no longer minted.
    // retail: TypeName 0x180042d40, Type 0x180042d20, _TypeName 0x180042d00, _ParentTypeName 0x180042d10
    DECLARE_TYPE(LayoutDescription)
public:
    int32_t       GetFileVersion() const override;                // [3] 0x1800d3a00
    bool          Serialize(IO::Stream& stream) override;               // [5] 0x1800d3940
    Object::External* Reconstruct() override;                     // [6] 0x180042fc0
    void          Destruct() override;                            // [7] 0x180043100
    Object::InternalPtr<Element>* CreateInstance(
        Object::InternalPtr<Element>* ret,
        const Dialog* dialog) const override;                     // [8] 0x1800d33a0 RETSLOT
    virtual Object::External* Construct();                        // [9] 0x180043060

    // ---- exported, not ported: declared, deliberately undefined ------------
    LayoutDescription(const LayoutDescription& src);              // 0x180043110
    LayoutDescription& operator=(const LayoutDescription& src);   // 0x180043170

private:

public:
    // ---- the 0x10 bytes after ElementDescription ---------------------------
    // B4 Y RT-UI: on retail's own row (SetSize 0x180043d80). Its ~Array is the
    // row's destroy (0x180044050 per element, ascending) over the live range,
    // then the J1 free and the header zeroed: retail's 0x180043a00, which
    // ~LayoutDescription 0x1800d3a10 calls first, before the +0x60 _textName
    // release -- the order the compiler runs the member destructors in.
    using EntryArray = Collection::Array<Entry, Collection::DefaultAllocator,
                                         Collection::ExplicitSpec<&rtcoll::kSpec_LayoutEntry38>>;
    EntryArray _entries;   // +0x68 .. +0x77
};

static_assert(sizeof(LayoutDescription::LayoutHorizontal) == 4, "a W4 enum is four bytes");
static_assert(sizeof(LayoutDescription::Entry) == 0x38, "Array<Entry> stride (0x180043d80)");
static_assert(offsetof(LayoutDescription::Entry, desc) == 0x0, "Entry::desc");
static_assert(offsetof(LayoutDescription::Entry, _pad31) == 0x31, "Entry::_pad31");
static_assert(offsetof(LayoutDescription::Entry, name)        == 0x08, "");
static_assert(offsetof(LayoutDescription::Entry, relativeToX) == 0x10, "");
static_assert(offsetof(LayoutDescription::Entry, relativeToY) == 0x18, "");
static_assert(offsetof(LayoutDescription::Entry, horizontal)  == 0x20, "");
static_assert(offsetof(LayoutDescription::Entry, vertical)    == 0x24, "");
static_assert(offsetof(LayoutDescription::Entry, offsetX)     == 0x28, "");
static_assert(offsetof(LayoutDescription::Entry, offsetY)     == 0x2c, "");
static_assert(offsetof(LayoutDescription::Entry, force)       == 0x30, "");
static_assert(sizeof(LayoutDescription) == 0x78,
              "UI::LayoutDescription is 0x78 bytes (MOV EDX,0x78 in its slot-0 "
              "vector deleting destructor 0x1800431d0)");
static_assert(alignof(LayoutDescription) == 8, "UI::LayoutDescription aligns to 8");
static_assert(offsetof(LayoutDescription, _entries) == sizeof(ElementDescription), "");
static_assert(offsetof(LayoutDescription, _entries) == 0x68, "");

}  // namespace UI

// ==== B3R W0-P0c: Runtime bodies declared for callers by name ====
// ENDGAME B.3R (B3R_RULES R5): each body below carries its retail address in its
// trailing comment. It is declared here, copied from its definition (linkage,
// namespace, types), so a caller can call it by name; layoutdescription.cpp
// defines it and LayoutDescription::Serialize calls it.

// ---- ui/layoutdescription.cpp
namespace IO { class Serializer; }
extern "C" void RtUi_LayoutDescription_SerializeEntries(IO::Serializer* ser, int32_t count, UI::LayoutDescription::Entry* e);  // 0x180043f50
// ==== end B3R W0-P0c ====
