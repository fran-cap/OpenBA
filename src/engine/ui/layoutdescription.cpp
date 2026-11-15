// ============================================================================
// src/engine/ui/layoutdescription.cpp — UI::LayoutDescription, whole class
// ============================================================================
//
// Vtable batch 2, the last class in it and the one with the most machinery
// underneath. Derives straight from UI::ElementDescription.
//
// ??_7LayoutDescription@UI@@6B@ (retail 0x180156cc0), ten slots:
//   0  deleting dtor      0x1800431d0  LayoutDescription.h (the class's own; sizeof 0x78)
//   1  TypeName           0x180042d40  here
//   2  Type               0x180042d20  here
//   3  GetFileVersion     0x1800d3a00  here  (returns 0x304)
//   4  External::Compile  0x1800066e0  already ported
//   5  Serialize          0x1800d3940  here
//   6  Reconstruct        0x180042fc0  here
//   7  Destruct           0x180043100  here
//   8  CreateInstance     0x1800d33a0  here
//   9  Construct          0x180043060  here
// plus ??1LayoutDescription@UI@@UEAA@XZ 0x1800d3a10 and the entry-array
// serializer 0x180043f50 (unexported).
//
// THE CONSTRUCTOR IS INLINED, twice (Construct 0x180043076.. and Reconstruct
// 0x180042fde..). Both copies were compared instruction by instruction for this
// port and agree.
//
// LAYOUT, sizeof 0x78 (`MOV EDX,0x78` at 0x1800431ef). ElementDescription
// (0x68) plus ONE array:
//     +0x68  Array<Entry>.data, +0x70 _size, +0x74 _capacity
//
// Entry is 0x38 bytes and is the thing this class is about
// (src/engine/ui/LayoutDescription.h names it field by field):
//     +0x00  ExternalPtr<UI::ElementDescription>   the cell's content
//     +0x08  System::String      this entry's OWN name — what siblings look up
//     +0x10  System::String   \  the names of the two entries this one is
//     +0x18  System::String   /  positioned RELATIVE TO (X, then Y)
//     +0x20  uint32  +0x24 uint32  +0x28 int32  +0x2c int32  +0x30 bool
//     +0x31..+0x37 never written by anything
// (B.3.6 block C4 correction: this banner used to put the OWN name at +0x18
// and the two relative names at +0x08/+0x10. CreateInstance's own lookup —
// `LEA R11,[R14+0x8]` at 0x1800d3645 for the candidates, `MOV RBX,[R14+R8+0x10]`
// at 0x1800d361d for the wanted name, gated by `CMP dword [R14+R8+0x20],0` —
// and the body below always said +0x08 / +0x10 / +0x18 as above.)
//
// FOUR COLLECTION HELPERS, all appended to the collection files rather than
// written here (that is where the family lives, and the specs there carry the
// unwritten-byte windows an oracle must not compare):
//   src/engine/collection/array.cpp
//     kSpec_LayoutEntry38 + J3_ENTRY2 for 0x180043d80 (Array<Entry>::SetSize)
//     kSpec_LayoutInfo44  + J3_ENTRY3 for 0x180043b40 (the ELEMENT's 0x44-byte
//                                                      per-cell info array)
//   src/engine/collection/array_destroy.cpp
//     J2_ENTRY for 0x180043a00 (~Array<Entry>) and the single-element
//     destructor 0x180044050, plus the element copy-assign 0x180043980
//
// ??0LayoutElement@UI@@ is NOT called here: CreateInstance inlines the element
// construction (??0Element, the LayoutElement vtable, two zeroed qwords).
//
// ENDGAME B.3.6 block C4: the layout moved to src/engine/ui/
// LayoutDescription.h / LayoutElement.h (the real classes). Every slot body
// below is a MEMBER, and so is the dtor.
// ============================================================================
#include <new>
#include <cstddef>
#include <cstdint>

#include "ui/LayoutDescription.h"
#include "ui/LayoutElement.h"
#include "io/Serializer.h"
#include "io/Stream.h"
#include "description_common.h"
#include "ui/Dialog.h"
#include "system/String.h"
#include "system/MemoryAllocator.h"
#include "collection/Collection.h"

namespace {

// (B4: the file-local IO::Stream / IO::Serializer shadows are gone -- H10;
// the Serialize body builds a real IO::Serializer.)

// Retail's String comparison, inlined at FOUR places in CreateInstance and
// written once here. It is NOT strcmp: the two cached LENGTHS at `data[-8]` are
// compared first, and only then the bytes, and the byte walk stops at the first
// NUL on EITHER side. Returns true when the two compare equal.
//
// A zero-length table entry short-circuits to "equal" the moment the lengths
// match, which is what makes an empty name match another empty name.
bool StringEquals(const char* have, const char* want) {
    const int32_t haveLen = have ? RtSystem::StringLength(have) : 0;
    const int32_t wantLen = want ? RtSystem::StringLength(want) : 0;
    if (haveLen != wantLen) return false;
    if (have == nullptr || RtSystem::StringLength(have) == 0) return true;
    if (want == nullptr) return false;

    int64_t k = 0;
    char c = have[0];
    if (c == want[0]) {
        const char* w = want;
        while (c != '\0' && *w != '\0') {
            ++k;
            ++w;
            c = have[w - want];
            if (c != *w) break;
        }
    }
    return have[k] == want[k];
}

bool NamesNothing(const char* s) {
    return s == nullptr || RtSystem::StringLength(s) == 0;
}

}  // namespace

// ============================================================================
// The entry-array serializer, retail 0x180043f50 — UNEXPORTED, address-only.
// Not a member: it is the `Array<Entry>` element serializer, not a
// LayoutDescription method.
// ============================================================================
// `void SerializeEntries(Serializer* ser, int count, Entry* data)`
// RCX = the {stream, readerWriter} PAIR, EDX = count, R8 = the elements.
//
// Per entry, in this order:
//   +0x00  ExternalPtr<ElementDescription>::Serialize(&e, pair)  0x180038f90
//   +0x08  System::String::Serialize(&e, pair)                   0x1800c9930
//   +0x10  System::String::Serialize
//   +0x18  System::String::Serialize
//   +0x20  kScalarUInt   +0x24 kScalarUInt   (vt+0x98, NOT the int slot)
//   +0x28  kScalarInt    +0x2c kScalarInt    (vt+0xa0)
//   +0x30  kScalarBool
// The READER is read out of the pair (`MOV RCX,[RDI+8]`) ONCE PER FIELD, not
// hoisted, and the body returns immediately when count <= 0 (`TEST EDX,EDX;
// JLE` is the first instruction, before the prologue).
extern "C" void RtUi_LayoutDescription_SerializeEntries(IO::Serializer* ser, int32_t count,
                                                        UI::LayoutDescription::Entry* e) {
    if (count <= 0) return;                                    // 180043f50

    // The ReaderWriter is re-read from the Serializer for every field, as retail does.
    auto rw = [ser]() { return ser->_readerWriter; };

    for (int32_t i = 0; i < count; ++i, ++e) {
        Object::SerializeExternal<UI::ElementDescription>(*ser, e->desc);   // 180043f77
        e->name.Serialize(*ser);                               // 180043f83 0x1800c9930
        e->relativeToX.Serialize(*ser);                        // 180043f8e
        e->relativeToY.Serialize(*ser);                        // 180043f9a

        rw()->SerializeUInt(reinterpret_cast<uint32_t&>(e->horizontal), nullptr);          // +0x98  // retail serializes the int32 enum through the uint slot  // CAST-OK: byte_io
        rw()->SerializeUInt(reinterpret_cast<uint32_t&>(e->vertical), nullptr);            // +0x98  // retail serializes the int32 enum through the uint slot  // CAST-OK: byte_io
        rw()->SerializeInt(e->offsetX, nullptr);              // +0xa0
        rw()->SerializeInt(e->offsetY, nullptr);              // +0xa0
        rw()->SerializeBool(e->force, nullptr);               // +0x78
    }
}

// ============================================================================
// The member bodies (ENDGAME B.3.6 block C4)
// ============================================================================
namespace UI {

int32_t LayoutDescription::GetFileVersion() const { return 0x304; }

// 0x180043060 — the ctor is INLINED here in retail: placement-new the class
// (the null test sat in the retired door; a virtual call has a `this`).
Object::External* LayoutDescription::Construct() {
    return ::new (this) LayoutDescription();                   // 180043076 .. 1800430ea
}
// 0x180042fc0 — slot 0 (VIRTUAL) with flag 0, then the inlined ctor.
Object::External* LayoutDescription::Reconstruct() {
    this->~LayoutDescription();                                           // 180042fd7
    return ::new (this) LayoutDescription();                   // .. 180043052
}
// 0x180043100
void LayoutDescription::Destruct() { this->~LayoutDescription(); }

// ============================================================================
// ?Serialize@LayoutDescription@UI@@UEAA_NAEAVStream@IO@@@Z   0x1800d3940
// ============================================================================
bool LayoutDescription::Serialize(IO::Stream& streamv) {
    IO::Stream* stream = &streamv;

    UI::ElementDescription::Serialize(*stream);                 // 1800d395a (qualified)

    IO::Serializer ser(*stream, 0);  // ??0Serializer (0x1800b9960) inlined

    IO::ReaderWriter* rw = ser._readerWriter;                                     // 1800d398a

    int32_t n = _entries._size;                                // 1800d398f
    rw->SerializeInt(n, nullptr);

    if (ser._stream->_direction == 0) {                               // 1800d39b1 (re-read)
        _entries.SetSize(n);                                   // 1800d39bf 0x180043d80
    }

    RtUi_LayoutDescription_SerializeEntries(&ser, n, _entries._data);   // 1800d39d1 0x180043f50

    // ~Serializer (0x1800b99b0) inlined here: rw's scalar deleting dtor                                       // 1800d39df
    return true;
}

// ============================================================================
// ?CreateInstance@LayoutDescription@UI@@...   retail 0x1800d33a0
// ============================================================================
// RCX = this, RDX = the hidden InternalPtr<Element> return slot, R8 = Dialog*.
//
// LayoutElement is 0xd0 bytes (`MOV EDX,0xd0` at 0x1800d33ee); its construction
// is INLINED (??0Element, the LayoutElement vtable, then +0xc0 and +0xc8
// zeroed).
//
// *** THIS BODY IS A DEPENDENCY RESOLVER, AND THAT IS WHY IT IS SHAPED LIKE
//     THIS. *** Every entry may name up to two OTHER entries it is positioned
// relative to (`relativeToX` +0x10 and `relativeToY` +0x18; the entry's own
// name is `name`, +0x08). An entry can only be instantiated once the entries it
// names already have a record in the element, so the body runs:
//
//   `order` = Array<uint32> of one slot per entry, all initialised to -1
//             (0x180023830, the three-argument fill form == kSpec_Fill4)
//
//   PASS 1 — every entry with NO dependency (both gates zero, or both
//            relative-name Strings empty) is placed immediately, in index order.
//
//   PASS 2 — repeat over all still-unplaced entries; an entry is placed when
//            BOTH of its named dependencies resolve to an already-placed entry
//            (or it names none). OUTER LOOP repeats while `placed < count`.
//
// *** THE OUTER LOOP HAS NO PROGRESS CHECK. *** If a cycle or a dangling name
// makes a pass place nothing, retail loops forever. That is retail's, it is not
// "fixed" here, and it is the reason a malformed layout .rsc hangs rather than
// drawing wrong.
//
// `order[i]` doubles as the placed flag (-1 == not placed) and as the VALUE
// stored into the resolved entry's record: it receives the element's current
// record count at the moment the entry is placed.
//
// THE LOOKUPS COMPARE STRINGS AGAINST EVERY *PLACED* ENTRY'S OWN NAME (`name`,
// +0x08) and skip the entry being resolved (`uVar19 != uVar17`). The sentinel
// handling is layered:
//   -1  the field named nothing (its gate at +0x20 / +0x24 is zero, or the name
//       String is empty)   -> not a blocker
//   -2  the field named something that is not placed yet
//       -> `-2 < x` is the test that lets -1 through and stops -2, which is why
//          both sentinels exist and why they are not interchangeable.
Object::InternalPtr<Element>* LayoutDescription::CreateInstance(
        Object::InternalPtr<Element>* ret, const Dialog* dialog) const {
    void* raw = System::MemoryAllocator::MemAlloc(0xd0, 8);    // 1800d33ee (inlined)

    // Retail inlines ??0LayoutElement (0x1800d3421 .. 0x1800d343b): the real ctor.
    LayoutElement* elem = nullptr;
    if (raw != nullptr)                                        // 1800d341c
        elem = ::new (raw) LayoutElement();                    // 1800d3421 ??0Element + stores

    Object::InternalPtr<Element> slot(elem);                                         // 1800d344e
    UI::ElementDescription::CreateInstance(                   // 1800d3459
        &slot);

    // The order table: one uint32 per entry, all -1.
    struct { int32_t* data; int32_t size; int32_t capacity; } order = { nullptr, 0, 0 };
    {
        const int32_t minusOne = -1;                           // 1800d346a
        rtcoll::RtColl_ArraySetSizeFill_180023830(&order, _entries._size, &minusOne);  // 1800d347d
    }

    const Entry* const entriesBase = _entries._data;
    int32_t placed = 0;                                        // [RBP+0x7f]

    // ---- one entry's placement, shared by both passes ----------------------
    // (0x1800d34e9..0x1800d359f and 0x1800d37cb..0x1800d3883 are the same
    // sequence with a different record source; written once.)
    auto place = [&](int32_t index, const Entry* entry, int32_t relA, int32_t relB) {
        order.data[index] = elem->_childRecords._size;          // 1800d34e9
        ++placed;                                              // 1800d34f4

        ElementDescription* childDesc = entriesBase[index].desc;   // 1800d3500
        // ?CreateElement@Dialog@UI@@ 0x180112ed0: RETSLOT, the member's prvalue
        // placed in the caller's slot.
        Object::InternalPtr<Element>* made = &slot;
        *made = dialog->CreateElement(Object::ExternalPtr<ElementDescription>(childDesc));

        Element* child = made->Get();                          // 1800d3514
        if (child != nullptr) child->_parent = elem;           // 1800d351c

        rtcoll::RtColl_ArraySetSizeFill_180076900(&elem->_children,
                                       elem->_children.GetSize() + 1, made);   // 1800d3533

        for (Element* n = elem; n != nullptr; n = n->_parent)  // 1800d3540..1800d3554
            n->_flags |= kElementFlagNeedsSizeCalc;

        // The 0x44-byte record. RETAIL WRITES ONLY ITS FIRST 0x24 BYTES
        // (0x1800d3568..0x1800d3581) and the array assign copies all 0x44, so
        // bytes 0x24..0x43 of every appended record are retail's stack residue
        // (as are +0x19..+0x1b). Reproduced with an uninitialised local: what is
        // faithful is the WIDTH of the copy, not our residue.
        LayoutElement::ChildRecord info;                       // [RBP-0x49]
        info.alignX  = entry->horizontal;                      // 1800d3568
        info.alignY  = entry->vertical;                        // 1800d356b
        info.refX    = relA;                                   // 1800d356e / 1800d385a
        info.refY    = relB;                                   //            / 1800d385d
        info.offsetX = entry->offsetX;                         // 1800d3576
        info.offsetY = entry->offsetY;                         // 1800d3579
        info.force   = entry->force;                           // 1800d357d
        const uint64_t zero = 0;
        __builtin_memcpy(&info.sizeX, &zero, sizeof zero);     // 1800d3581  UNALIGNED QWORD

        rtcoll::RtColl_ArraySetSizeFill_180043b40(&elem->_childRecords,
                                           elem->_childRecords._size + 1,
                                           &info);             // 1800d359a
    };

    // ---- PASS 1: the entries that depend on nothing ------------------------
    {
        const int32_t count = _entries._size;
        for (int32_t i = 0; i < count; ++i) {                  // 1800d348b / 1800d35b0
            const Entry* e = _entries._data + i;               // 1800d34b0

            const bool noFlags = (e->horizontal == 0 && e->vertical == 0);    // 1800d34b7
            const bool noNames = NamesNothing(e->relativeToX._data) && NamesNothing(e->relativeToY._data);
            if (!(noFlags || noNames)) continue;               // 1800d35a2

            // -1, -1: this entry is positioned relative to nothing.
            place(i, e, -1, -1);                               // 1800d356e MOV qword -1
        }
    }

    // ---- PASS 2: repeat until everything is placed -------------------------
    // NO PROGRESS CHECK — see the banner.
    while (placed < _entries._size) {                          // 1800d35c5 / 1800d38be
        const int32_t count = _entries._size;
        for (int32_t i = 0; i < count; ++i) {                  // 1800d35d7 / 1800d38ad
            if (order.data[i] >= 0) continue;                  // 1800d35f4  already placed

            const Entry* const entries = _entries._data;
            const Entry* e = entries + i;

            // --- the FIRST named dependency -------------------------------
            int32_t relA = -2;                                 // 1800d3603
            {
                const char* want = e->relativeToX._data;
                if (e->horizontal == 0 || NamesNothing(want)) {
                    relA = -1;                                 // 1800d36d3  names nothing
                } else {
                    for (int32_t j = 0; j < count; ++j) {      // 1800d3650 / 1800d36c3
                        if (order.data[j] < 0) continue;       // not placed yet
                        if (j == i) continue;                  // 1800d3657
                        if (StringEquals(entries[j].name._data, want)) {   // 1800d366d..1800d36b8
                            relA = order.data[j];              // 1800d36ca
                            break;
                        }
                    }
                }
            }

            // --- the SECOND named dependency ------------------------------
            int32_t relB;
            {
                const char* want = e->relativeToY._data;
                if (e->vertical == 0 || NamesNothing(want)) {
                    relB = -1;                                 // 1800d37a6  names nothing
                } else {
                    relB = -2;                                 // 1800d37af (the local's
                                                               //            initial -2)
                    for (int32_t j = 0; j < count; ++j) {      // 1800d3720 / 1800d3795
                        if (order.data[j] < 0) continue;
                        if (j == i) continue;                  // 1800d3727
                        if (StringEquals(entries[j].name._data, want)) {   // 1800d373d..1800d3788
                            relB = order.data[j];              // 1800d379c
                            break;
                        }
                    }
                }
            }

            // `-2 < x` — lets -1 (names nothing) through and stops -2 (names
            // something not yet placed). The two sentinels are not the same.
            if (relA < -1 || relB < -1) continue;              // 1800d37b9 / 1800d37c2

            place(i, e, relA, relB);                           // 1800d37cb..1800d3883
        }
    }

    *ret = Object::InternalPtr<Element>(elem);                     // 1800d38cf

    // The order table's buffer is freed through MemoryAllocator::Free under the
    // allocator's own critical section (0x1800d38d7..0x1800d3906), the same
    // sequence ?MemFree@MemoryAllocator@System@@ is.
    if (order.data != nullptr) System::MemoryAllocator::MemFree(order.data);   // 1800d38d2
    order.data = nullptr;                                      // 1800d390c
    order.size = 0;                                            // 1800d3910
    order.capacity = 0;

    return ret;
}

// ??1LayoutDescription@UI@@UEAA@XZ   0x1800d3a10 — EMPTY body (B4 Y RT-UI).
// Retail: restamp, `_entries`' ~Array 0x180043a00 (1800d3a34: 0x180044050 per
// element, ascending, then the J1 free and the header zeroed), restamp, the
// base's +0x60 _textName release, tail JMP into ~External. Every one of those
// is now a member destructor, run in that order after this body.
LayoutDescription::~LayoutDescription() {
}

// ?CreateNewObject@LayoutDescription@UI@@SAPEAVBase@Object@@XZ 0x180042e30 --
// `?MemAlloc@MemoryAllocator` (0x1800c7a50) of 0x78, align 8; retail's null
// branch; the ctor. Replaces the factory interpreter row (B39_RULES D2).
Object::Base* LayoutDescription::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(LayoutDescription), 8);
    if (p == nullptr) return nullptr;
    return ::new (p) LayoutDescription();
}
// ?CreateInPlaceObject@LayoutDescription@UI@@SAPEAVBase@Object@@PEAX@Z 0x180042f20
Object::Base* LayoutDescription::CreateInPlaceObject(void* place) {
    if (place == nullptr) return nullptr;
    return ::new (place) LayoutDescription();
}

}  // namespace UI

// ============================================================================
// ENDGAME B.3.9 UI-B: the slot doors, the ctor / dtor doors and the
// factory-interpreter row are gone. Their only readers were the minted
// `??_7LayoutDescription@UI@@6B@`, its RT_DELDTOR row and FACTUI2: every live
// LayoutDescription now wears the compiler's table.
// ============================================================================

// ============================================================================
// ENDGAME B.3.9 TI-UI (B39 D6): UI::LayoutDescription's own type record, built at CRT init
// (head-insert into Object::BaseTypeInfo::_firstTypeInfo). Literals copied from
// the retired minted row ?_typeInfo@LayoutDescription@UI@@ (0x1801fa078): name "LayoutDescription", parent
// "ElementDescription" (retail ?_ParentTypeName@LayoutDescription@UI@@), no file extension; factories:
// the class's own.
// ============================================================================
DEFINE_TYPE(UI::LayoutDescription, "ElementDescription", nullptr)
