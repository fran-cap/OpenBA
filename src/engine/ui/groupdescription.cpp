// ============================================================================
// src/engine/ui/groupdescription.cpp — UI::GroupDescription, the whole class
// ============================================================================
//
// WHY THIS CLASS, AND WHY NOW. The 2026-09-12 cutover boot (make cutover, DLL
// absent) stopped here:
//
//   [RtMint] FATAL: call through a NOT-YET-PORTED minted vtable slot
//   [RtMint]   vtable : ??_7GroupDescription@UI@@6B@
//   [RtMint]   slot   : 2
//   [RtMint]   target : ?Type@GroupDescription@UI@@UEBAIXZ
//
// It is the first UI ElementDescription subclass the dialog load path
// dispatches on, and slot 2 is only where it happened to land first — the whole
// table was traps. So the unit ported here is the CLASS, not the slot.
//
// ??_7GroupDescription@UI@@6B@ (retail 0x180157190), ten slots:
//
//   0  deleting dtor       0x180046af0  GroupDescription.h (the class's own)
//   1  TypeName            0x180046660  GroupDescription.h (DECLARE_TYPE)
//   2  Type                0x180046640  GroupDescription.h (DECLARE_TYPE)
//   3  GetFileVersion      0x180117cf0  here  (returns 0x104)
//   4  External::Compile   0x1800066e0  already ported
//   5  Serialize           0x180117d00  here
//   6  Reconstruct         0x1800468e0  here
//   7  Destruct            0x180046a20  here
//   8  CreateInstance      0x180117dc0  here
//   9  Construct           0x180046980  here
//
// plus ??1GroupDescription@UI@@UEAA@XZ (0x180117c00) and the factory pair
// `?CreateNewObject@` (0x180046750) / `?CreateInPlaceObject@` (0x180046840),
// the class's own since ENDGAME B.3.9 (the FACTUI1 row is retired).
//
// Layout: src/engine/ui/GroupDescription.h. `ElementDescription` + one
// `Array<ExternalPtr<ElementDescription>>` at +0x68; sizeof 0x78.
//
// ENDGAME B.3.6 block C2: every slot body is a MEMBER of `UI::GroupDescription`.
//
// ENDGAME B.3.9 block UI-A (B39_RULES D1/D2): the class is CONCRETE and every
// live GroupDescription wears the COMPILER's table; the slot doors and the ??1
// door, read only by the retired minted table and its RT_DELDTOR row, are gone.
//
// ----------------------------------------------------------------------------
// THE CONSTRUCTOR IS INLINED EVERYWHERE AND THERE IS NO OUT-OF-LINE ??0.
// ----------------------------------------------------------------------------
// `Construct` (0x180046980), `Reconstruct` (0x1800468e0) and both factory steps
// each open-code the same 19 stores; the DLL exports no `??0GroupDescription`.
// It is the class's real ctor (GroupDescription.h) since B.3.9; the stores,
// transcribed from 0x180046996..0x180046a06. Two details Ghidra's C loses:
//   * `+0x40` is written as ONE QWORD holding 3 (`MOV qword [RCX+0x40],3` at
//     0x1800469e0), so it covers `_elementType` AND `_flags` (+0x44).
//   * `+0x38` and `+0x3c` are two SEPARATE DWORDS (0 and -1), because the
//     values differ. Not one qword.
// The three dead base-vtable stamps (Base, then External over it unread, then
// ElementDescription) are reproduced: they are four instructions in the shipped
// bytes and collapsing them would be a divergence for no gain.
// ============================================================================
#include <cstddef>
#include <cstdint>
#include <new>

#include "description_common.h"
#include "ui/GroupDescription.h"
#include "ui/GroupElement.h"

namespace {

using namespace rtdesc;

}  // namespace

// ============================================================================
// The member bodies
// ============================================================================
namespace UI {

// 0x180117cf0: `MOV EAX,0x104 ; RET`. 0x104 == 260, the dialog resource file
// version this class understands.
int32_t GroupDescription::GetFileVersion() const { return 0x104; }

// ============================================================================
// Construct / Reconstruct / Destruct
// ============================================================================
// `Construct` is the ctor with a null test (the test sat in the retired door —
// a virtual call always has a `this`); `Reconstruct` is `slot0(this, 0)` — the
// DELETING DESTRUCTOR with flag 0, i.e. destroy WITHOUT freeing, through the
// object's own table — and then the ctor again, with NO null test (retail
// derefs `this` to reach the vtable before it could test it).
Object::External* GroupDescription::Construct() {
    return ::new (this) GroupDescription();                    // 180046996..180046a06
}
Object::External* GroupDescription::Reconstruct() {
    this->~GroupDescription();                                           // 1800468f7
    return ::new (this) GroupDescription();
}
// 0x180046a20 is a TAIL JUMP into slot 0 with flag 0 (`JMP [RAX]`).
void GroupDescription::Destruct() { this->~GroupDescription(); }

// ============================================================================
// ?Serialize@GroupDescription@UI@@UEAA_NAEAVStream@IO@@@Z   0x180117d00
// ============================================================================
//   ElementDescription::Serialize(this, stream)            0x180116b00
//   pair = { stream, nullptr }
//   rw   = stream->mode == 0 ? factory->vt[0x18](factory,&pair,0)
//                            : factory->vt[0x20](factory,&pair,0)
//   n    = _children._size
//   rw->vt[0xa0](rw, &n, 0)
//   if (pair.stream->mode == 0) SetSize(&_children, n)      0x1800492d0, ported
//   ElementDescriptionPtrArray_Serialize(&pair, n, _children._data)
//                                                          0x1800493d0, ported
//   if (rw) rw->vt[0](rw, 1)
//   return true
//
// The base call is QUALIFIED: this object wears the leaf's table, whose slot 5
// is this very function.
//
// THE MODE IS RE-READ FROM THE PAIR'S COPY, not from the argument: the
// disassembly is `MOV RAX,[RSP+0x28] ; CMP dword [RAX+8],0` at
// 0x180117d6c..0x180117d71, one deref deeper than the first test at
// 0x180117d3c. Transcribed rather than folded.
bool GroupDescription::Serialize(IO::Stream& streamv) {
    IO::Stream* stream = &streamv;

    ElementDescription::Serialize(*stream);                     // 180117d1a

    IO::Serializer ser(*stream, 0);  // ??0Serializer (0x1800b9960) inlined              // [RSP+0x28], [RSP+0x30]
    IO::ReaderWriter* rw = ser._readerWriter; // 180117d4a

    int32_t n = _children._size;                               // 180117d4f
    rw->SerializeInt(n, nullptr);                              // 180117d66

    if (ser._stream->_direction == 0) {                               // 180117d6c (re-read)
        rtcoll::RtColl_ArraySetSize_1800492d0(&_children, n);   // 180117d7f
    }

    Object::SerializeExternalArray<UI::ElementDescription>(ser, n, _children._data);

    // ~Serializer (0x1800b99b0) inlined here: rw's scalar deleting dtor                    // 180117d9c
    return true;                                               // MOV AL,1
}

// ============================================================================
// ?CreateInstance@GroupDescription@UI@@UEBA?AV?$InternalPtr@VElement@UI@@@Object@@PEBVDialog@2@@Z
// retail 0x180117dc0   (slot 8, RULE B6-2)
// ============================================================================
// RCX = this, RDX = the hidden return slot, R8 = the Dialog*. Ghidra prints
// `CreateInstance(GroupDescription*, Dialog*)` and DROPS the third register;
// the disassembly is unambiguous: `MOV R15,R8` (the Dialog, used only as
// CreateElement's `this`), `MOV R14,RDX` (written through at 0x180117ed1),
// `MOV RSI,RCX`.
//
// ONE STACK SLOT DOES TWO JOBS, and it must stay one variable here. Retail's
// [RSP+0x50] holds the new GroupElement when it is handed to
// `ElementDescription::CreateInstance` (which applies the description to it),
// and is then REUSED as `Dialog::CreateElement`'s return slot inside the loop.
//
// `ElementDescription::CreateInstance` (0x180115ad0) is a QUALIFIED member
// call since fragment pass F2 defined it (dialog.cpp); the inlined
// GroupElement ctor is the real one since B.3.9. `Dialog::CreateElement` is
// a plain member call.
//
// The parent-chain walk at the end of each iteration is retail's: set
// `kElementFlagNeedsSizeCalc` on the group and on every ancestor through
// `Element::_parent`, until the chain ends.
Object::InternalPtr<Element>* GroupDescription::CreateInstance(
        Object::InternalPtr<Element>* ret, const Dialog* dialog) const {
    // The locked wrapper, which is the Enter/Allocate/Leave triple retail
    // inlines at 0x180117deb..0x180117e25.
    void* raw = rtdesc::MemAlloc(0xc0, 8);

    GroupElement* elem = nullptr;
    if (raw != nullptr)                                        // 180117e32
        elem = ::new (raw) GroupElement();                     // 180117e3a..180117e47 inlined ctor

    Object::InternalPtr<Element> slot(elem);                                         // 180117e4f [RSP+0x50]
    UI::ElementDescription::CreateInstance(                   // 180117e5c
        &slot);

    const int32_t count = _children._size;
    for (int32_t i = 0; i < count; ++i) {                      // 180117e61 / 180117ec9
        ElementDescription* desc = _children._data[i];         // 180117e77

        // ?CreateElement@Dialog@UI@@ (0x180112ed0), its RETSLOT the same local.
        Object::InternalPtr<Element>* made = &slot;
        *made = dialog->CreateElement(Object::ExternalPtr<ElementDescription>(desc));

        Element* child = made->Get();                          // 180117e88
        if (child != nullptr)
            child->_parent = elem;                             // 180117e90  Element::_parent

        // Array<Element*>::SetSize(&elem->_children, size + 1, made) — the
        // three-argument (fill) form, ported as array.cpp's kSpec_Fill8Z.
        rtcoll::RtColl_ArraySetSizeFill_180076900(&elem->_children,
                                               elem->_children._size + 1,
                                               made);          // 180117ea9

        for (Element* n = elem; n != nullptr; n = n->_parent)  // 180117eb1..180117ec5
            n->_flags |= kElementFlagNeedsSizeCalc;
    }

    new (static_cast<void*>(ret)) Object::InternalPtr<Element>(elem);   // 180117ed1
    return ret;                                                // MOV RAX,R14
}

}  // namespace UI

// ============================================================================
// ??1GroupDescription@UI@@UEAA@XZ   0x180117c00
// ============================================================================
// Retail: re-stamp own vtable, destroy the child array (J1 `Array::~Array`,
// 0x180039440 -- the ExternalPtrs are NOT released), re-stamp the BASE vtable,
// release `ElementDescription::_textName` at +0x60 and TAIL-JUMP to
// `??1External@Object@@` (0x180117c66). ENDGAME B.3.9 UI-A: EMPTY; every step
// is the compiler's -- the member `_children.~Array()` is the J1 body for a
// trivially destructible element (Collection.h), then ~ElementDescription and
// ~External.
namespace UI {
GroupDescription::~GroupDescription() {}
}  // namespace UI

namespace UI {
// ?CreateNewObject@GroupDescription@UI@@SAPEAVBase@Object@@XZ  0x180046750
// The factory-interpreter row this replaces: MemAlloc(0x78, 8), retail's
// null branch, then the inlined ctor (19 stores).
Object::Base* GroupDescription::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(GroupDescription), 8);
    if (p == nullptr) return nullptr;
    return ::new (p) GroupDescription();
}
// ?CreateInPlaceObject@GroupDescription@UI@@SAPEAVBase@Object@@PEAX@Z  0x180046840
Object::Base* GroupDescription::CreateInPlaceObject(void* place) {
    if (place == nullptr) return nullptr;
    return ::new (place) GroupDescription();
}
}  // namespace UI

// ============================================================================
// ENDGAME B.3.9 TI-UI (B39 D6): UI::GroupDescription's own type record, built at CRT init
// (head-insert into Object::BaseTypeInfo::_firstTypeInfo). Literals copied from
// the retired minted row ?_typeInfo@GroupDescription@UI@@ (0x1801faba0): name "GroupDescription", parent
// "ElementDescription" (retail ?_ParentTypeName@GroupDescription@UI@@), no file extension; factories:
// the class's own.
// ============================================================================
DEFINE_TYPE(UI::GroupDescription, "ElementDescription", nullptr)
