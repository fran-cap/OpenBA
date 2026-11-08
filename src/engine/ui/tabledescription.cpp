// ============================================================================
// src/engine/ui/tabledescription.cpp — UI::TableDescription, whole class
// ============================================================================
//
// Vtable batch 2. Derives straight from UI::ElementDescription.
//
// ??_7TableDescription@UI@@6B@ (retail 0x1801592c0), ten slots:
//   0  deleting dtor      0x180079210  deleting_dtor.cpp row (sizeof 0x98)
//   1  TypeName           0x180078ed0  here
//   2  Type               0x180078eb0  here
//   3  GetFileVersion     0x18011e6e0  here  (returns 0x204)
//   4  External::Compile  0x1800066e0  already ported
//   5  Serialize          0x18011e590  here
//   6  Reconstruct        0x180079050  here
//   7  Destruct           0x180079080  here
//   8  CreateInstance     0x18011e350  here
//   9  Construct          0x180079070  here
// plus ??0TableDescription@UI@@QEAA@XZ 0x18011e7a0 (out of line) and
// ??1TableDescription@UI@@UEAA@XZ 0x18011e6f0.
//
// LAYOUT, sizeof 0x98 (`MOV EDX,0x98` at 0x18007922f). ElementDescription
// (0x68) plus:
//     +0x68  int32   _gridA   ctor 1     kScalarInt; -> element +0xc4
//     +0x6c  int32   _gridB   ctor 1     kScalarInt; -> element +0xc0
//     +0x70  int32   _i70     ctor 0     kScalarInt; -> element +0xc8
//     +0x74  *** NEVER WRITTEN, NEVER READ ***
//     +0x78  Array<Cell>{int32 col, int32 row, ExternalPtr<ElementDescription>}
//     +0x80  _size, +0x84 _capacity
//     +0x88  (zeroed by the ctor; touched by nothing in this class)
//     +0x90  System::String — released by the destructor, and by nothing else
//                             here. It has no serializer and no reader in any
//                             of this class's ten slots, so all this file can
//                             say is that it is an owned String.
//
// THE CTOR'S +0x6c STORE IS AN UNALIGNED QWORD (`MOV qword [RCX+0x6c],1` at
// 0x18011e824), so it writes _gridB = 1 AND +0x70 = 0 in one instruction and
// stops at +0x73. +0x74 is the gap that leaves.
//
// TWO NEW COLLECTION HELPERS, appended to src/engine/collection/array.cpp:
//   0x1800799a0  Array<Cell>::SetSize -> kSpec_TableCell10, whose default tail
//                is {-1, -1, nullptr} and NOT zero (see that row's comment)
//   0x180079870  ~Array<Cell>, the canonical free-and-zero body
// ??0TableElement@UI@@ (0x18011e2f0) is already ported (ui_factory_wave28.cpp).
// ============================================================================
#include <new>

#include "io/Stream.h"
#include <cstddef>
#include <cstdint>

#include "ui/TableDescription.h"
#include "ui/TableElement.h"
#include "io/Serializer.h"
#include "ui/Dialog.h"
#include "ui/SpriteSheet.h"
#include "system/String.h"
#include "system/MemoryAllocator.h"
#include "collection/Collection.h"

// ENDGAME B.3.6 block C4: the layout moved to src/engine/ui/
// TableDescription.h (the real class). Every slot body below is a MEMBER; each
// registered door is a one-line QUALIFIED delegation. The ctor and dtor stay
// DOORS (B4b-2). RULE B6-5: every export the members reach is cached in a
// namespace-scope std::atomic slot, never in a function-local static.

namespace {





void ReleaseStringField(char** field) {
    char* s = *field;
    if (s == nullptr) return;
    if (--RtSystem::StringRefCount(s) == 0) {
        *field = s - 8;
        System::MemoryAllocator::MemFree(*field);
    } else {
        *field = nullptr;
    }
}

}  // namespace


// ============================================================================
// The member bodies (ENDGAME B.3.6 block C4)
// ============================================================================
namespace UI {

int32_t TableDescription::GetFileVersion() const { return 0x204; }

// 0x180079070 — the ctor, placement (the null test sat in the retired door).
Object::External* TableDescription::Construct() {
    return ::new (this) TableDescription();
}
// 0x180079050 — slot 0 (VIRTUAL) with flag 0, then the ctor.
Object::External* TableDescription::Reconstruct() {
    this->~TableDescription();
    return ::new (this) TableDescription();
}
// 0x180079080
void TableDescription::Destruct() { this->~TableDescription(); }

// ============================================================================
// ?Serialize@TableDescription@UI@@UEAA_NAEAVStream@IO@@@Z   0x18011e590
// ============================================================================
// Three ints, then the cell count into a LOCAL, the read-path SetSize, and a
// per-cell loop that emits {int, int, ExternalPtr}. The String at +0x90 is NOT
// serialized — the destructor is the only body in the class that touches it.
bool TableDescription::Serialize(IO::Stream& streamv) {
    IO::Stream* stream = &streamv;

    UI::ElementDescription::Serialize(*stream);                 // 18011e5aa (qualified)

    // The inlined ??0Serializer (0x1800b9960): CreateReader / CreateWriter
    // of the stream's encoding, version 0.
    IO::Serializer ser(*stream, 0);
    IO::ReaderWriter* const rw = ser._readerWriter;

    rw->SerializeInt(_columnCount, nullptr);
    rw->SerializeInt(_rowCount, nullptr);
    rw->SerializeInt(_spacing, nullptr);

    int32_t n = _cells._size;                                  // 18011e61e
    rw->SerializeInt(n, nullptr);

    if (ser._stream->_direction == 0) {                               // 18011e643 (re-read)
        rtcoll::RtColl_ArraySetSize_1800799a0(&_cells, n);         // 18011e651
    }

    Cell* cell = _cells._data;                                 // 18011e656 (once)
    for (int32_t i = 0; i < n; ++i, ++cell) {                  // 18011e65e / 18011e6ae
        rw->SerializeInt(cell->column, nullptr);
        rw->SerializeInt(cell->row, nullptr);
        Object::SerializeExternal<UI::ElementDescription>(ser, cell->desc);   // 18011e6a2
    }

    // The inlined ~Serializer deletes the ReaderWriter (18011e6b8).
    return true;
}

// ============================================================================
// ?CreateInstance@TableDescription@UI@@...   retail 0x18011e350
// ============================================================================
// TableElement is 0xf0 bytes (`MOV EDX,0xf0` at 0x18011e398); its ctor is CALLED
// out of line (0x18011e2f0, the tableelement.cpp door).
//
// This is a GRID BUILDER. For every (a, b) with a in [0, _columnCount) and b in
// [0, _rowCount) it picks ONE cell description out of the flat cell list and
// instantiates it at the flat index `_columnCount * b + a`.
//
// *** THE PICK IS A FOUR-TIER SEARCH AND THE TIERS ARE NOT INTERCHANGEABLE. ***
// One pass over the cells, tracking three candidates:
//   exact     cell.column == a && cell.row == b     -> taken IMMEDIATELY,
//                                                      the pass stops
//   wildcardA cell.column <  0 && cell.row == b     -> FIRST such cell only
//   wildcardB cell.row <  0 && cell.column == a     -> FIRST such cell only
//   wildcardAB cell.column < 0 && cell.row < 0      -> the LAST such cell
// and the fallback order is exact, wildcardA, wildcardB, wildcardAB; if none
// matched, THE CELL IS LEFT EMPTY (no element is created for it).
//
// The "first such" / "last such" asymmetry is retail's and is the kind of thing
// a tidy-up destroys: the two wildcard branches are guarded by
// `TEST R9D/R10D,R9D/R10D; JNS <skip>` (keep the first), while the both-wildcard
// branch is a bare `CMOVL R8D,EAX` with NO such guard (keep the last). Three
// lines apart in the shipped code, and deliberate.
//
// THE FLAT INDEX IS `_columnCount * b + a`, computed with the DESCRIPTION's
// count (0x18011e4f2), and then recomputed a second time from 64-bit copies of
// the same counters (0x18011e524) for the parent write. Retail computes it
// twice; so does this.
Object::InternalPtr<Element>* TableDescription::CreateInstance(
        Object::InternalPtr<Element>* ret, const Dialog* dialog) const {
    void* raw = System::MemoryAllocator::MemAlloc(0xf0, 8);   // 18011e398 (inlined)

    TableElement* elem = nullptr;
    if (raw != nullptr)                                        // 18011e3c3
        elem = ::new (raw) TableElement();                     // 18011e3c8 ??0 (out of line)

    Object::InternalPtr<Element> slot(elem);                                         // 18011e3d5
    UI::ElementDescription::CreateInstance(                   // 18011e3e2
        &slot);

    const int32_t columns = _columnCount;
    const int32_t rows    = _rowCount;

    elem->_columnCount = columns;                              // 18011e3ea  CROSSED:
    elem->_rowCount    = rows;                                 // 18011e3f4  +0x68->0xc4, +0x6c->0xc0

    rtcoll::RtColl_ArraySetSize_18002fea0(&elem->_rowSizes, rows);        // 18011e405  Array<int32>
    rtcoll::RtColl_ArraySetSize_18002fea0(&elem->_columnSizes, columns);  // 18011e414  Array<int32>

    elem->_spacing = _spacing;                                 // 18011e41c

    rtcoll::RtColl_ArraySetSize_1800764c0(&elem->_children, columns * rows);  // 18011e431

    for (int32_t a = 0; a < _columnCount; ++a) {
        for (int32_t b = 0; b < _rowCount; ++b) {
            int32_t wildA  = -1;                               // 18011e460  R9
            int32_t wildB  = -1;                               // 18011e464  R10
            int32_t wildAB = -1;                               // 18011e467  R8
            int32_t pick   = -1;

            const int32_t cells = _cells._size;
            bool exact = false;
            if (cells > 0) {                                   // 18011e473
                const Cell* c = _cells._data;
                for (int32_t i = 0; i < cells; ++i, ++c) {
                    const int32_t col = c->column;
                    const int32_t row = c->row;

                    if (a == col && b == row) {                // 18011e482 / 18011e486
                        pick = i; exact = true; break;         // 18011e4cb
                    }
                    if (wildA < 0 && col < 0 && b == row) {    // 18011e48b..18011e499
                        wildA = i;
                    } else if (wildB < 0 && row < 0 && a == col) {  // 18011e49e..18011e4ad
                        wildB = i;
                    } else if (col < 0 && row < 0) {           // 18011e4b2..18011e4ba
                        wildAB = i;    // NO "first only" guard — the LAST wins
                    }
                }
            }

            if (!exact) {                                      // 18011e4cf
                pick = wildA;
                if (pick < 0) pick = wildB;
                if (pick < 0) pick = wildAB;
                if (pick < 0) continue;                        // 18011e4e5 JS -> skip
            }

            const Cell* cell = _cells._data + pick;

            const int32_t flat = _columnCount * b + a;         // 18011e4f2

            Element** kids = elem->_children._data;            // 18011e4fd
            Element** dst  = kids + flat;                      // 18011e504

            // ?CreateElement@Dialog@UI@@ 0x180112ed0: RETSLOT, the member's prvalue
            // placed in the caller's slot.                    // 18011e519
            Object::InternalPtr<Element>* made = &slot;
            *made = dialog->CreateElement(
                Object::ExternalPtr<UI::ElementDescription>(cell->desc));

            *dst = made->Get();                                // 18011e521

            // The flat index RECOMPUTED, and the array data pointer RE-READ,
            // before the parent write (0x18011e524..0x18011e53a). Retail does
            // not reuse `dst`; neither does this.
            const int64_t flat2 = static_cast<int64_t>(_columnCount) *
                                  static_cast<int64_t>(b) + static_cast<int64_t>(a);
            Element* child = elem->_children._data[flat2];
            child->_parent = elem;                             // 18011e53a
        }
    }

    *ret = Object::InternalPtr<Element>(elem);                 // 18011e562 / 18011e567
    return ret;
}

// ??1TableDescription@UI@@UEAA@XZ  0x18011e6f0 — retail's body, moved from the
// retired door (ENDGAME B.3.9 UI-B): the String at +0x90 FIRST, then the cell
// Array through 0x180079870 (frees and zeroes the header; the compiler's
// `_cells.~Array()` afterwards is a no-op). The restamps, the base's
// _textName (+0x60) and the tail JMP into ~External are the compiler's chain.
TableDescription::~TableDescription() {
    ReleaseStringField(&_unreadOwnedString);                           // 18011e710
    rtcoll::RtColl_ArrayDtor_180079870(&_cells);              // 18011e74a
}

// ?CreateNewObject@TableDescription@UI@@SAPEAVBase@Object@@XZ 0x180078fc0 --
// `?MemAlloc@MemoryAllocator` (0x1800c7a50) of 0x98, align 8; retail's null
// branch; the ctor. Replaces the factory interpreter row (B39_RULES D2).
Object::Base* TableDescription::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(TableDescription), 8);
    if (p == nullptr) return nullptr;
    return ::new (p) TableDescription();
}
// ?CreateInPlaceObject@TableDescription@UI@@SAPEAVBase@Object@@PEAX@Z 0x180079040
Object::Base* TableDescription::CreateInPlaceObject(void* place) {
    if (place == nullptr) return nullptr;
    return ::new (place) TableDescription();
}

}  // namespace UI

// ============================================================================
// ENDGAME B.3.9 UI-B: the slot doors, the ctor / dtor doors and the
// factory-interpreter row are gone. Their only readers were the minted
// `??_7TableDescription@UI@@6B@`, its RT_DELDTOR row and FACTUI2: every live
// TableDescription now wears the compiler's table.
// ============================================================================

// ============================================================================
// ENDGAME B.3.9 TI-UI (B39 D6): UI::TableDescription's own type record, built at CRT init
// (head-insert into Object::BaseTypeInfo::_firstTypeInfo). Literals copied from
// the retired minted row ?_typeInfo@TableDescription@UI@@ (0x1801fae40): name "TableDescription", parent
// "ElementDescription" (retail ?_ParentTypeName@TableDescription@UI@@), no file extension; factories:
// the class's own.
// ============================================================================
DEFINE_TYPE(UI::TableDescription, "ElementDescription", nullptr)
