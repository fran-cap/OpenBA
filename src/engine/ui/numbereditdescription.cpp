// ============================================================================
// src/engine/ui/numbereditdescription.cpp — UI::NumberEditDescription
// ============================================================================
//
// Vtable batch 2. Derives straight from UI::ElementDescription (unlike Radio
// and Page, which go through GroupDescription).
//
// ??_7NumberEditDescription@UI@@6B@ (retail 0x180156fe0), ten slots:
//   0  deleting dtor      0x180046190  NumberEditDescription.h (sizeof 0x88)
//   1  TypeName           0x180045e90  here
//   2  Type               0x180045e70  here
//   3  GetFileVersion     0x1800d4fd0  here  (returns 0x1004)
//   4  External::Compile  0x1800066e0  already ported
//   5  Serialize          0x1800d4ee0  here
//   6  Reconstruct        0x180046010  here
//   7  Destruct           0x180046040  here
//   8  CreateInstance     0x1800d4cc0  here
//   9  Construct          0x180046030  here
// plus ??0NumberEditDescription@UI@@QEAA@XZ 0x1800d5050 (out of line; the real
// ctor in the header since ENDGAME B.3.9 UI-A) and
// ??1NumberEditDescription@UI@@UEAA@XZ 0x1800d4fe0.
//
// LAYOUT, sizeof 0x88 (`MOV EDX,0x88` at 0x1800461af). ElementDescription
// (0x68) plus:
//     +0x68  ExternalPtr<UI::Font>   ctor 0, serialized by 0x18003f1e0
//     +0x70  int32   _initialValue   *** NEVER WRITTEN BY THE CONSTRUCTOR *** — the
//                                    ctor's qword store at +0x68 stops at +0x6f and the
//                                    next store is the dword at +0x74. It IS serialized
//                                    (kScalarInt) and it IS read by CreateInstance
//                                    (formatted into the element's text with "%i"), so
//                                    a fresh, never-deserialized description formats
//                                    whatever the allocator left there. That is
//                                    retail's; zeroing it here would be a divergence
//                                    and would hide the fact if it ever matters.
//     +0x74  int32   _maxLength      ctor 6
//     +0x78  int32   _minValue       ctor 0xffff8001  (-32767)
//     +0x7c  int32   _maxValue       ctor 0x7fff
//     +0x80  bool    _allowNegative  ctor 1 — selects the character set (see below)
//     +0x81..+0x87 never written
//
// NOT PORTED HERE, DELIBERATELY: ??0EditElement@UI@@QEAA@XZ (0x1800d0f10), the
// base-element constructor CreateInstance calls. The *Element constructors
// belong to the Element files; this one is reached through ResolveAddr, which
// is registry-first, so it picks up their port the moment it lands.
//
// ENDGAME B.3.6 block C3: every slot body is a `UI::NumberEditDescription`
// member (src/engine/ui/NumberEditDescription.h, the layout above moved out
// of this file — RULE B6-8). The doors delegate with a QUALIFIED call; the ctor
// and dtor stay DOOR bodies. CreateInstance writes a real
// `UI::NumberEditElement` (EditElement's 0xf8 + `_min`/`_max`).
// ============================================================================

#include "element_common.h"   // rtuielem::AtomicExport (RULE B6-5), ResolveAddr
#include "ui/NumberEditDescription.h"
#include "ui/NumberEditElement.h"
#include "io/Serializer.h"
#include "io/Stream.h"
#include "description_common.h"
#include "system/MemoryAllocator.h"
#include "system/String.h"
#include "ui/Font.h"   // B.4 P2 X: T::GetTypeInfo() for Object::SerializeExternal<T>
#include <new>

namespace {

// (B4: the file-local IO::Stream / IO::Serializer shadows are gone -- H10;
// the Serialize body builds a real IO::Serializer.)

void MemFree(void* p) { System::MemoryAllocator::MemFree(p); }
int32_t& RefCount(char* s) { return RtSystem::StringRefCount(s); }

// `System::String::operator=` INLINED, as retail has it at both of this file's
// two assignment sites (0x1800d4d8f and 0x1800d4e37). The `field = nullptr` arm
// on a still-shared string is the asymmetry the sibling *Description
// destructors carry too.
void StringAssignInline(char** field, char* value) {
    if (*field == value) return;
    if (*field != nullptr) {
        if (--RefCount(*field) == 0) {
            *field = *field - 8;
            MemFree(*field);
        } else {
            *field = nullptr;
        }
    }
    *field = value;
    if (value != nullptr) ++RefCount(value);
}

// The matching release of the caller's temporary, also inlined by retail.
void StringReleaseInline(char** slot) {
    char* s = *slot;
    if (s == nullptr) return;
    if (--RefCount(s) == 0) {
        s = s - 8;
        *slot = s;
        MemFree(s);
    } else {
        *slot = nullptr;
    }
}

}  // namespace

namespace UI {

int32_t NumberEditDescription::GetFileVersion() const { return 0x1004; }

// 0x180046030: null test (it sat in the retired door), then the ctor.
Object::External* NumberEditDescription::Construct() {
    return ::new (this) NumberEditDescription();
}
Object::External* NumberEditDescription::Reconstruct() {
    this->~NumberEditDescription();
    return ::new (this) NumberEditDescription();
}
void NumberEditDescription::Destruct() { this->~NumberEditDescription(); }

// ============================================================================
// ?Serialize@NumberEditDescription@UI@@UEAA_NAEAVStream@IO@@@Z   0x1800d4ee0
// ============================================================================
//   ElementDescription::Serialize                        0x180116b00 (qualified)
//   ExternalPtr<UI::Font>::Serialize(&_font, &pair)      0x18003f1e0
//   kScalarInt  +0x70, +0x78, +0x7c, +0x74     <- NOT in offset order
//   kScalarBool +0x80
//
// THE FOUR INTS ARE EMITTED 0x70, 0x78, 0x7c, 0x74. The order is the format.
bool NumberEditDescription::Serialize(IO::Stream& streamv) {
    IO::Stream* stream = &streamv;

    UI::ElementDescription::Serialize(*stream);                 // 1800d4efa

    IO::Serializer ser(*stream, 0);  // ??0Serializer (0x1800b9960) inlined

    IO::ReaderWriter* rw = ser._readerWriter;                                     // 1800d4f2a

    Object::SerializeExternal<UI::Font>(ser, _font);          // 1800d4f38

    rw->SerializeInt(_initialValue, nullptr);
    rw->SerializeInt(_minValue, nullptr);
    rw->SerializeInt(_maxValue, nullptr);
    rw->SerializeInt(_maxLength, nullptr);
    rw->SerializeBool(_allowNegative, nullptr);

    // ~Serializer (0x1800b99b0) inlined here: rw's scalar deleting dtor                                       // 1800d4faf
    return true;
}

// ============================================================================
// ?CreateInstance@NumberEditDescription@UI@@...   retail 0x1800d4cc0  (slot 8)
// ============================================================================
// RETSLOT (B6-2). R8 (the Dialog) is never read — this class creates no
// children.
//
// NumberEditElement is 0x100 bytes (`MOV EDX,0x100` at 0x1800d4cff). Its
// construction (the real ctor, NumberEditElement.h) CALLS ??0EditElement
// (0x1800d0f10), stamps the NumberEditElement vtable and writes the two dwords
// that the tail then
// OVERWRITES with the description's own values. Retail does both; the pair at
// 0x1800d4d41/0x1800d4d4b is not dead, because the null-allocation path skips
// them and the tail's stores then run against a null pointer anyway.
//
// TWO STRING FIELDS ARE BUILT:
//   _text   String::Format("%i", _initialValue)                the initial text
//   _filter _allowNegative ? "0123456789-" : "0123456789"   the accepted character set
// (both literals read out of .rdata at 0x180156f50 / 0x180156f60, and the
// format string out of 0x180154848).
//
// NO NULL GUARD ON `elem`. When the allocation fails retail sets RDI to zero
// and falls straight through into `MOV [RDI+0xc0],RAX`. That deref is retail's.
Object::InternalPtr<Element>* NumberEditDescription::CreateInstance(
        Object::InternalPtr<Element>* ret, const Dialog* /*dialog*/) const {
    void* raw = System::MemoryAllocator::MemAlloc(0x100, 8);   // 1800d4cff (inlined)

    NumberEditElement* elem = nullptr;   // retail: RDI = the null allocation
    if (raw != nullptr)                                        // 1800d4d2c
        elem = ::new (raw) NumberEditElement();                // 1800d4d31..1800d4d4b inlined ctor

    Object::InternalPtr<Element> slot(elem);                                         // 1800d4d5a
    // The non-virtual ElementDescription::CreateInstance(InternalPtr<Element>)
    // (0x180115ad0) — a QUALIFIED member call since fragment pass F2.
    UI::ElementDescription::CreateInstance(                   // 1800d4d67
        &slot);

    elem->_font = _font;                                       // 1800d4d70

    // --- the initial text ---------------------------------------------------
    // `?Format@String@System@@SA?AV12@PEBDZZ` returns by memory; retail points
    // the hidden return slot at [RSP+0x50], the SAME local that was holding
    // `elem` a moment ago, so the temporary and that slot are one object.
    {
        char* tmp = nullptr;                                   // [RSP+0x50]
        new (static_cast<void*>(&tmp)) System::String(                     // 1800d4d87
            System::String::Format("%i", _initialValue));                   // 0x1800c9730, retslot = &tmp
        char** made = &tmp;

        StringAssignInline(&elem->_text._data, *made);               // 1800d4d8f
        StringReleaseInline(&tmp);                             // 1800d4dd8
    }

    elem->_maxLength = _maxLength;                                   // 1800d4e05

    // --- the accepted character set -----------------------------------------
    {
        // The MINUS SIGN is in the set only when _allowNegative is set, which is what
        // makes that field an "allow negative" switch.
        const char* set = _allowNegative != 0 ? "0123456789-" : "0123456789";  // 1800d4e1c

        System::String tmp;                                    // 1800d4e27
        tmp.Set(set);                                          // 1800d4e31  0x1800c95d0

        StringAssignInline(&elem->_filter._data, tmp._data);         // 1800d4e37

        // `tmp`'s ~String at the closing brace is the release (0x1800d4e82..
        // 0x1800d4e9f): retail works off the value it kept in RBX across the
        // assignment, which is the value `tmp` still holds -- the assignment
        // never writes `tmp`.
    }

    elem->_min = _minValue;                                         // 1800d4ea7
    elem->_max = _maxValue;                                         // 1800d4eb0

    *ret = Object::InternalPtr<Element>(elem);                     // 1800d4eb6
    return ret;
}

}  // namespace UI

// ============================================================================
// ??1NumberEditDescription@UI@@UEAA@XZ   0x1800d4fe0
// ============================================================================
// ONE String release — ElementDescription's _textName at +0x60, under the BASE
// vtable — then the tail JMP to ??1External. ENDGAME B.3.9 UI-A: the body is
// EMPTY and the compiler chains ~ElementDescription and ~External. The
// ExternalPtr<Font> at +0x68 is not released (same asymmetry as
// ~ImageDescription).
namespace UI {
NumberEditDescription::~NumberEditDescription() {}

// ?CreateNewObject@NumberEditDescription@UI@@SAPEAVBase@Object@@XZ  0x180045f80
// The FACTUI2 row this replaces: MemAlloc(0x88, 8), retail's null branch, then
// the out-of-line ctor ??0NumberEditDescription (0x1800d5050).
Object::Base* NumberEditDescription::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(NumberEditDescription), 8);
    if (p == nullptr) return nullptr;
    return ::new (p) NumberEditDescription();
}
// ?CreateInPlaceObject@NumberEditDescription@UI@@SAPEAVBase@Object@@PEAX@Z  0x180046000
Object::Base* NumberEditDescription::CreateInPlaceObject(void* place) {
    if (place == nullptr) return nullptr;
    return ::new (place) NumberEditDescription();
}
}  // namespace UI

// ============================================================================
// ENDGAME B.3.9 TI-UI (B39 D6): UI::NumberEditDescription's own type record, built at CRT init
// (head-insert into Object::BaseTypeInfo::_firstTypeInfo). Literals copied from
// the retired minted row ?_typeInfo@NumberEditDescription@UI@@ (0x1801fa158): name "NumberEditDescription", parent
// "ElementDescription" (retail ?_ParentTypeName@NumberEditDescription@UI@@), no file extension; factories:
// the class's own.
// ============================================================================
DEFINE_TYPE(UI::NumberEditDescription, "ElementDescription", nullptr)
