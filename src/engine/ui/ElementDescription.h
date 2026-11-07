#pragma once
// ============================================================================
// src/engine/ui/ElementDescription.h — UI::ElementDescription layout
// ============================================================================
//
// Wave 7 lane B (docs/history/runtime_port/WAVE7_LANE_B.md). `UI::ElementDescription`
// is the SERIALIZED, resource-side description of one UI element: the thing a
// `.rsc` file parses into, and the thing `CreateInstance` stamps onto a live
// `UI::Element`. It is an `Object::External` (a resource with a path), not an
// `Object::Internal`.
//
// Inheritance, read out of the ctor's three successive vtable stores
// (0x180115a40, disassembled — the decompiler prints them as three writes to
// `*(void**)this` in a row, which is exactly what a base-chain ctor looks like):
//
//     Object::Base            ??_7Base@Object@@6B@             0x180154788
//       -> Object::External   ??_7External@Object@@6B@         0x1801547a0
//         -> UI::ElementDescription
//                             ??_7ElementDescription@UI@@6B@   0x1801561c0
//
// ALL THREE VTABLES ARE EXPORTED. That is the `??_7Geometry@Graphics@@6B@`
// lever from Wave 6 (WAVE7.md fact 2): a ported ctor stamps the DLL's own
// vtable byte for byte instead of minting one, so a half-ported class never
// puts two vtables in play. `src/engine/ui/dialog.cpp`'s ported ctor resolves
// all three by name and stamps them; it does not synthesise a table.
//
// SIZE: 0x68, and it is PROVEN by the constructor rather than inferred — the
// ctor's last store is a qword at +0x60, and `??1ElementDescription`
// (0x180116100) releases exactly one String, the one at +0x60, before chaining
// to `Object::External::~External`. No member is ever touched above +0x67.
//
// ---------------------------------------------------------------------------
// EVIDENCE FOR EVERY OFFSET
// ---------------------------------------------------------------------------
// Three functions pin this layout between them, and they agree field for field:
//
//   ??0ElementDescription@UI@@QEAA@XZ   0x180115a40  ord  173  — initialiser
//   ?Serialize@ElementDescription@UI@@UEAA_NAEAVStream@IO@@@Z
//                                       0x180116b00  ord 3687  — field TYPES
//   ?CreateInstance@ElementDescription@UI@@QEBAXV?$InternalPtr@VElement@UI@@@Object@@@Z
//                                       0x180115ad0  ord 2004  — field MEANINGS
//
// `Serialize` is the strongest of the three, because the Encoding vtable slot
// it uses names the field's type outright (retail numbers, and note that the
// PROFILE build's Encoding vtable puts these somewhere else — see
// src/engine/object/Internal.h's warning):
//
//     +0x98  Serialize(uint&, const char*)      rt::io::rw_vtbl::kScalarUInt
//     +0xa0  Serialize(int&,  const char*)      rt::io::rw_vtbl::kScalarInt
//
// ENDGAME B.3.4a closing: those two rows used to be spelled
// `RtObject::kEncSerializeUint` / `kEncSerializeInt`, constants that lived on
// `RtObject::SerializerLite` -- a FOURTH declaration of IO::Serializer's 16
// bytes. `IO::Serializer` is a real class since B.3.4 block 3, and
// B34_RULES 3.3 retired `SerializerLite` and its three `kEnc*` constants in
// the commit that landed it. THE OFFSETS ARE UNCHANGED -- a `static_assert`
// tied the retired constants to the `rw_vtbl::kScalar*` ones precisely so
// this substitution could never drift -- so the evidence this banner rests on
// is exactly as strong as it was; only the spelling that names it still exists.
//
//   off   field           type      proven by
//   ----  --------------  --------  --------------------------------------------
//   0x00  Object::External base     ctor (3 vtable stores + zeroes at 8/0x10/0x18)
//   0x20  _x              int32     Serialize +0xa0; CreateInstance -> elem+0x18
//   0x24  _y              int32     Serialize +0xa0; CreateInstance -> elem+0x1c
//   0x28  _width          int32     Serialize +0xa0; CreateInstance -> elem+0x20
//   0x2c  _height         int32     Serialize +0xa0; CreateInstance -> elem+0x24
//   0x30  _minWidth       int32     Serialize +0xa0; CreateInstance -> elem+0x28
//   0x34  _minHeight      int32     Serialize +0xa0; CreateInstance -> elem+0x2c
//   0x38  _tabOrder       int32     Serialize +0xa0; CreateInstance -> elem+0x3c
//   0x3c  _id             uint32    ctor sets 0xffffffff; Serialize +0x98 (uint);
//                                   CreateInstance -> elem+0x38
//   0x40  _elementType    uint32    ctor sets 3; Serialize +0x98;
//                                   CreateInstance -> elem+0x30
//   0x44  _flags          uint32    Serialize +0x98; CreateInstance ORs into
//                                   elem+0x34 (never assigns — see below)
//   0x48  _childDesc      External* CreateInstance calls its vtable +0x40 and
//                                   stores the result in elem+0xb0
//   0x50  _userData       ExternalPtr<UI::Dialog>  -- NOT a uint64; see the
//                                   note on the field below. CreateInstance's
//                                   plain qword copy to elem+0x40 still looks
//                                   right because an ExternalPtr is 8 bytes.
//   0x58  _stringTable    StringTable*  CreateInstance: null -> index -1, else
//                                   StringTable::GetIndexFromName(_stringTable,
//                                   _textName); stored to elem+0x48
//   0x60  _textName       String    CreateInstance passes &this[0x60] to
//                                   GetIndexFromName; ~ElementDescription
//                                   releases exactly this one String
//
// The names for +0x20..+0x34 are inferred from what CreateInstance copies them
// INTO on the Element, not from a debug string. They are labelled as such here
// rather than presented as recovered: lane A owns `UI::Element` and its header
// is the authority on what elem+0x18..+0x2c mean. If lane A's names disagree,
// lane A wins and these should be renamed to match.
//
// TWO THINGS THE CTOR DOES NOT DO, both of which matter to a port:
//
//   * `_flags` (+0x44) is left ZERO by the ctor and CreateInstance ORs it into
//     the element rather than assigning, so a description with no flags cannot
//     clear a flag the element already has. That is retail behaviour, not a
//     transcription slip.
//   * The ctor writes EVERY byte of 0x00..0x67 — there is no padding hole. That
//     is what makes the replay oracle in dialog.cpp a full oracle for it rather
//     than a spot check (reference_oracle_must_not_compare_unwritten_bytes.md:
//     an oracle that compares bytes neither side writes fails 100% of calls).
//
// PORTABILITY: no <windows.h>, per PLAN.md §4.1 decision 1.
// ============================================================================

#include <cstdint>
#include <cstddef>

#include "../object/External.h"
#include "../object/Internal.h"   // Object::InternalPtr<T> (CreateInstance's retslot)

// (B.4 Z MIRROR: the `RtUI::ElementDescription` POD mirror and its
// `kElementDescriptionSize` had no user left; the class is UI::ElementDescription
// below.)

// ############################################################################
// ENDGAME B.3 step 6 (block A) — `UI::ElementDescription`, the REAL C++ class
// ############################################################################
//
// PARENT `Object::External` — READ, not inferred:
//     ?_ParentTypeName@ElementDescription@UI@@CAPEBDXZ @0x1800350c0 -> "Object::External"
// and the ctor's three stamps (Base, External, ElementDescription) above agree.
//
// RULE B5-15 / B6-7 — THE TWO NAME LITERALS, RECORDED, NOT USED (no
// `DEFINE_TYPE` in step 6; slots 1/2 keep reading the minted
// `?_typeInfo@ElementDescription@UI@@0VBaseTypeInfo@Object@@A`, 0x1801faaf8):
//     ?_TypeName@ElementDescription@UI@@CAPEBDXZ       @0x1800350b0 -> "ElementDescription"
//     ?_ParentTypeName@ElementDescription@UI@@CAPEBDXZ @0x1800350c0 -> "Object::External"
//
// `??_7ElementDescription@UI@@6B@` @ 0x1801561c0, NINE slots (B36_SLOTS.md
// section 1, `.rdata` qwords; sizeof from `MOV EDX,0x68` in slot 0):
//
//   [0] +0x00  0x180035350  (unexported) vector deleting dtor
//   [1] +0x08  0x1800350f0  ?TypeName@ElementDescription@UI@@UEBAPEBDXZ
//   [2] +0x10  0x1800350d0  ?Type@ElementDescription@UI@@UEBAIXZ
//   [3] +0x18  0x180116af0  ?GetFileVersion@…  (`MOV EAX,4 ; RET`)
//   [4] +0x20  0x1800066e0  ?Compile@External@Object@@  INHERITED — NOT declared (B6-1.5)
//   [5] +0x28  0x180116b00  ?Serialize@…  — REAL; every leaf's Serialize calls it
//   [6] +0x30  _purecall    Reconstruct — pure, inherited from External, NOT declared
//   [7] +0x38  _purecall    Destruct    — pure, inherited from External, NOT declared
//   [8] +0x40  _purecall    CreateInstance(const Dialog*) -> InternalPtr<Element>
//                           BY VALUE — INTRODUCED HERE, pure, RETSLOT (RULE B6-2)
//
// There is NO `Construct` on this class: the 27 leaves introduce it at slot 9,
// BEHIND `CreateInstance` (RULE B6-1.1).
//
// ABSTRACT (slots 6, 7, 8 pure). Every live one is a leaf description built
// by its real constructor and wearing its COMPILER table (ENDGAME B.3.9 UI-D
// retired the minted `??_7ElementDescription@UI@@6B@`, its RT_DELDTOR row,
// the ??0 / ??1 doors and the slot doors). The REAL protected ctor and
// non-virtual dtor (B.3.8 block RB) have the private `InitFields` /
// `ReleaseFields` as bodies. Slot 0 is the class's own vector deleting dtor.
//
// WHERE THE BODIES ARE. `Serialize` moved into the member
// (elementdescription_serialize.cpp). `TypeName`, `Type`, `GetFileVersion`,
// `InitFields`/`ReleaseFields` and the non-virtual `CreateInstance(InternalPtr<Element>*)`
// are members defined in `src/engine/ui/dialog.cpp` (fragment pass F2).
// Defining `TypeName` (the key function) emits this class's vtable in
// dialog.cpp.
//
// A NAME-HIDING TRAP FOR THE LEAF BLOCKS. Retail has TWO `CreateInstance`s on
// this class: the pure slot-8 virtual and the non-virtual
// `?CreateInstance@ElementDescription@UI@@QEBAXV?$InternalPtr@VElement@UI@@@Object@@@Z`
// (0x180115ad0, "stamp this description onto that element"). A leaf that
// overrides slot 8 HIDES the non-virtual one; call it from a leaf as
// `ElementDescription::CreateInstance(&element)` (it has a different arity, so
// the mistake is a compile error, never a silent re-dispatch).
// ############################################################################

namespace UI {

class Dialog;    // block B
class Element;   // Element.h (this block)
class StringTable;

class ElementDescription : public Object::External {
public:
    // The alignment enum `?GetAlignedPosition@Element@UI@@SAXW4Alignment@
    // ElementDescription@2@…` and `?SetAlignment@Element@UI@@QEAAXW4Alignment@
    // ElementDescription@2@@Z` name. A `W4` enum: four bytes, passed in a
    // register. Its VALUES are the row-major 3x3 src/engine/ui/element.cpp reads
    // off the jump table at 0x180115a14 (0 top-left .. 8 bottom-right, anything
    // else behaves as top-left); its NAMES are retail's, from the profile build's
    // {value, name} table (Runtime-x64-profile.dll 0x1802544e0), which are also
    // the .rsc identifiers the kit's text files use.
    enum Alignment : int32_t {
        TopLeft      = 0,   // left column, top row (the default arm of both tests)
        TopCenter    = 1,   // centre column, top row
        TopRight     = 2,   // right column, top row
        MidLeft      = 3,   // left column, middle row: the ctor's default
        MidCenter    = 4,   // centre column, middle row
        MidRight     = 5,   // right column, middle row
        BottomLeft   = 6,   // left column, bottom row
        BottomCenter = 7,   // centre column, bottom row
        BottomRight  = 8,   // right column, bottom row
    };

    // ---- the nine slots, in measured order ---------------------------------
    // [0] 0x180035350 — MSVC's VECTOR deleting destructor (retail; here
    // the virtual destructor, B3X W1) (`MOV EDX,0x68`).
    // `??1ElementDescription@UI@@UEAA@XZ` (0x180116100). VIRTUAL (B3X W1); the
    // compiler restamps on entry and chains ~External (retail's tail JMP).
    ~ElementDescription() override {}   // +0x60: the member `_textName`'s ~String
    // ENDGAME B.3.9 TI-UI: ONE reflection block (B39 D6); `_typeInfo` is the
    // class's own record (DEFINE_TYPE in the class's TU), no longer minted.
    // retail: TypeName 0x1800350f0, Type 0x1800350d0, TypeId 0x1800350e0, _TypeName 0x1800350b0
    //         _ParentTypeName 0x1800350c0, CreateNewObject 0x180035100, CreateInPlaceObject 0x180035110
    DECLARE_TYPE(ElementDescription)
public:
    int32_t     GetFileVersion() const override;                  // [3] 0x180116af0  dialog.cpp
    // [4] Compile — Object::External's own body, inherited unchanged.
    bool        Serialize(IO::Stream& stream) override;                 // [5] 0x180116b00  (IO::Stream&)
    // [6] Reconstruct, [7] Destruct — pure in Object::External, still pure.
    virtual Object::InternalPtr<Element>* CreateInstance(
        Object::InternalPtr<Element>* ret,
        const Dialog* dialog) const = 0;                          // [8] _purecall — RETSLOT, B6-2

    // ---- non-virtual members ----------------------------------------------
    // 0x180115ad0 — defined in dialog.cpp (fragment pass F2); the door
    // `RtUiElementDescription_CreateInstance` delegates to it. Retail passes
    // the handle by ADDRESS (MSVC: non-trivial copy ctor; RULE B6-11), so the
    // member takes `InternalPtr<Element>*` in retail order and every leaf's
    // applier calls it as `ElementDescription::CreateInstance(&slot)`.
    void CreateInstance(Object::InternalPtr<Element>* element) const;

    ElementDescription(const ElementDescription& src);            // 0x180035120  not ported
    ElementDescription& operator=(const ElementDescription& src); // 0x180035210  not ported

    // Reflection statics. `_typeInfo` is the MINTED record TypeId reads.
    // Abstract: retail's pair is `xor eax,eax; ret` (dialog.cpp).

protected:
    // `??0ElementDescription@UI@@QEAA@XZ` (0x180115a40) — the REAL constructor
    // (ENDGAME B.3.8 block RB, RULE B8-1): the compiler stamps Base ->
    // External -> ElementDescription where retail's three inlined stores are
    // and runs External() (retail inlines its three zero stores); the body is
    // `InitFields`.
    ElementDescription() { InitFields(); }

private:
    void InitFields();      // +0x20..+0x67 stores of 0x180115a40 (dialog.cpp)


public:
    // ---- the 0x48 bytes after Object::External — RtUI::ElementDescription's
    // names and types, unchanged (the per-field evidence is the banner at the
    // top of this file). PUBLIC for the reason External's are.
    //
    // ONE MEANING CORRECTION, recorded and NOT applied as a rename (call sites
    // use the name): `_elementType` (+0x40) is the element's ALIGNMENT. The
    // ctor seeds it with 3, `CreateInstance` (0x180115ad0) copies it to
    // `Element::_alignment` (+0x30), and `Element::Element` seeds that field
    // with the same 3.
    int32_t  _x;                       // +0x20
    int32_t  _y;                       // +0x24
    int32_t  _width;                   // +0x28
    int32_t  _height;                  // +0x2c
    int32_t  _minWidth;                // +0x30
    int32_t  _minHeight;               // +0x34
    int32_t  _tabOrder;                // +0x38
    uint32_t _id;                      // +0x3c  ctor: 0xffffffff; serialized by the uint slot, copied to Element::_color
    uint32_t _elementType;             // +0x40  ctor: 3 (MidLeft) — the Alignment, see above
    uint32_t _flags;                   // +0x44
    ElementDescription* _childDesc;    // +0x48  ExternalPtr<UI::ElementDescription> (B4 H4)
    // +0x50: ExternalPtr<UI::Dialog> (Serialize 0x180076610). B4 H3: renamed from
    // `_userData` by what retail does with it — CreateInstance (0x180115ad0)
    // copies it to Element::_toolTipDialog (+0x40).
    Dialog*  _toolTipDialog;           // +0x50
    StringTable* _stringTable;         // +0x58  ExternalPtr<UI::StringTable> (B4 H4)
    // B4 Y RT-UI: a real member. Retail ctor 0x180115a40 zeroes +0x60 (its
    // String()); ~ElementDescription 0x180116100's one release is its ~String,
    // run after the (empty) body and before ~External.
    System::String _textName;          // +0x60
};

static_assert(sizeof(ElementDescription) == 0x68,
              "UI::ElementDescription is 0x68 bytes (MOV EDX,0x68 in its slot-0 "
              "vector deleting destructor 0x180035350)");
static_assert(alignof(ElementDescription) == 8, "UI::ElementDescription aligns to 8");
static_assert(sizeof(ElementDescription::Alignment) == 4, "a W4 enum is four bytes");
static_assert(offsetof(ElementDescription, _x) == sizeof(Object::External),
              "ElementDescription's own fields begin at the end of the External base");
static_assert(offsetof(ElementDescription, _x)           == 0x20, "");
static_assert(offsetof(ElementDescription, _y)           == 0x24, "");
static_assert(offsetof(ElementDescription, _width)       == 0x28, "");
static_assert(offsetof(ElementDescription, _height)      == 0x2c, "");
static_assert(offsetof(ElementDescription, _minWidth)    == 0x30, "");
static_assert(offsetof(ElementDescription, _minHeight)   == 0x34, "");
static_assert(offsetof(ElementDescription, _tabOrder)    == 0x38, "");
static_assert(offsetof(ElementDescription, _id)          == 0x3c, "");
static_assert(offsetof(ElementDescription, _elementType) == 0x40, "");
static_assert(offsetof(ElementDescription, _flags)       == 0x44, "");
static_assert(offsetof(ElementDescription, _childDesc)   == 0x48, "");
static_assert(offsetof(ElementDescription, _toolTipDialog) == 0x50, "");
static_assert(offsetof(ElementDescription, _stringTable) == 0x58, "");
static_assert(offsetof(ElementDescription, _textName)    == 0x60, "");

}  // namespace UI
