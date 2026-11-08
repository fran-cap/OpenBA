// ============================================================================
// src/engine/ui/editdescription.cpp — UI::EditDescription, the whole class
// ============================================================================
//
// ??_7EditDescription@UI@@6B@ (retail 0x180156880), ten slots:
//   0  deleting dtor      0x18003fa20  EditDescription.h (the class's own)
//   1  TypeName           0x18003f6d0  here
//   2  Type               0x18003f6b0  here
//   3  GetFileVersion     0x1800d11e0  here  (returns 0x204)
//   4  External::Compile  0x1800066e0  already ported
//   5  Serialize          0x1800d1110  here
//   6  Reconstruct        0x18003f850  here
//   7  Destruct           0x18003f880  here
//   8  CreateInstance     0x1800d0f90  here
//   9  Construct          0x18003f870  here
// plus ??1EditDescription@UI@@UEAA@XZ (0x1800d11f0).
//
// ENDGAME B.3.6 block C3: every slot body is a `UI::EditDescription` member
// (src/engine/ui/EditDescription.h) and the doors delegate with a QUALIFIED
// call. CreateInstance writes a real `UI::EditElement` now (EditElement.h, the
// 0xf8 layout).
//
// The constructor is OUT OF LINE (`??0EditDescription@UI@@QEAA@XZ`,
// 0x1800d12c0). ENDGAME B.3.9 UI-A: it is the real ctor (EditDescription.h);
// the class is concrete, its factory pair its own, the doors retired.
//
// THIS CLASS HAS NO SEPARATE APPLIER. Its CreateInstance is the only one in the
// batch that open-codes TWO refcounted String ASSIGNMENTS (not releases) — the
// increment side of the protocol, which no other member of this batch does.
// ============================================================================

#include "description_common.h"
#include "ui/EditDescription.h"
#include "ui/EditElement.h"
#include "ui/Font.h"   // B.4 P2 X: T::GetTypeInfo() for Object::SerializeExternal<T>

namespace {

using namespace rtdesc;

// ?MemFree@MemoryAllocator@System@@SAXPEAX@Z (0x1800c7ad0), called directly.
void FreeStringBlock(void* p) {
    System::MemoryAllocator::MemFree(p);
}

// The refcounted System::String RELEASE (retail's asymmetry: the freeing
// branch leaves the field at `data - 8`, the sharing branch nulls it) is
// rtdesc::ReleaseStringField (description_common.h).

// The refcounted System::String ASSIGNMENT retail open-codes twice in
// CreateInstance (0x1800d102a and 0x1800d1082). Not a release: the old value is
// released with the usual asymmetry, the new one is stored and its refcount
// INCREMENTED — and the whole thing is skipped when the two pointers are
// already equal (`CMP RAX,[RDI+0x70] ; JZ`), which is what makes a self-assign
// not drop the last reference.
void AssignStringField(char*& d, char* const s) {
    if (d == s) return;                                      // 1800d1035
    if (d != nullptr) {                                      // 1800d103a
        --RtSystem::StringRefCount(d);
        if (RtSystem::StringRefCount(d) == 0) {
            d = d - 8;                                       // 1800d1045
            FreeStringBlock(d);
        } else {
            d = nullptr;                                     // 1800d105b
        }
    }
    d = s;                                                   // 1800d106a
    if (d != nullptr) ++RtSystem::StringRefCount(d);         // 1800d1076
}

}  // namespace

namespace UI {

int32_t EditDescription::GetFileVersion() const { return 0x204; }

// 0x18003f870: null test (it sat in the retired door), then a JMP to the ctor.
Object::External* EditDescription::Construct() {
    return ::new (this) EditDescription();                   // 18003f879 JMP
}
Object::External* EditDescription::Reconstruct() {
    this->~EditDescription();                                         // 18003f85e
    return ::new (this) EditDescription();                   // 18003f868 JMP
}
void EditDescription::Destruct() { this->~EditDescription(); }

// ============================================================================
// ?Serialize@EditDescription@UI@@UEAA_NAEAVStream@IO@@@Z   0x1800d1110
// ============================================================================
// +0x68 goes through 0x18003f1e0, a DIFFERENT ExternalPtr serializer from the
// two this batch otherwise uses — which is how its pointee type (UI::Font) is
// distinguished.
bool EditDescription::Serialize(IO::Stream& streamv) {
    IO::Stream* stream = &streamv;

    UI::ElementDescription::Serialize(*stream);               // 1800d112a

    IO::Serializer ser(*stream, 0);  // ??0Serializer (0x1800b9960) inlined
    IO::ReaderWriter* rw = ser._readerWriter;

    Object::SerializeExternal<UI::Font>(ser, _font);       // 1800d1168
    _initialText.Serialize(ser);             // 1800d1176

    rw->SerializeInt(_maxLength, nullptr);                      // 1800d118a
    rw->SerializeBool(_password, nullptr);                     // 1800d119f

    _allowableCharacters.Serialize(ser);             // 1800d11ae

    // ~Serializer (0x1800b99b0) inlined here: rw's scalar deleting dtor                  // 1800d11c6
    return true;
}

// ============================================================================
// ?CreateInstance@EditDescription@UI@@...   retail 0x1800d0f90  (slot 8)
// ============================================================================
// RETSLOT (B6-2). R8 (the Dialog) is never read — no children.
//
// EditElement is 0xf8 and its constructor is OUT OF LINE
// (`??0EditElement@UI@@QEAA@XZ`, 0x1800d0f10; the real ctor since B.3.9);
// retail takes its RETURN value as the element. The base applier is the
// non-virtual `ElementDescription::CreateInstance(InternalPtr<Element>)`
// (0x180115ad0) — a QUALIFIED member call since fragment pass F2.
//
// The field copies are a QWORD, two refcounted String assignments, a DWORD and
// a BYTE — and the BYTE copy is LAST, after both strings (0x1800d10d7).
Object::InternalPtr<Element>* EditDescription::CreateInstance(
        Object::InternalPtr<Element>* ret, const Dialog* /*dialog*/) const {
    void* raw = MemAlloc(0xf8, 8);                           // 1800d0fd1

    EditElement* elem = nullptr;
    if (raw != nullptr)                                      // 1800d0ffc
        elem = ::new (raw) EditElement();                    // 1800d1001 ??0EditElement

    Object::InternalPtr<Element> slot(elem);                                       // 1800d100d
    UI::ElementDescription::CreateInstance(                   // 1800d101a
        &slot);

    elem->_font = _font;                                    // 1800d1023 QWORD
    AssignStringField(elem->_text._data, _initialText._data);               // 1800d102a
    elem->_maxLength = _maxLength;                              // 1800d107c DWORD
    AssignStringField(elem->_filter._data, _allowableCharacters._data);             // 1800d1082
    elem->_password = _password;                               // 1800d10d7 BYTE

    *ret = Object::InternalPtr<Element>(elem);                   // 1800d10e1
    return ret;
}

}  // namespace UI

// ============================================================================
// ??1EditDescription@UI@@UEAA@XZ   0x1800d11f0 — retail's body, moved here
// from the retired door (ENDGAME B.3.9 UI-A)
// ============================================================================
// TWO String releases (+0x80 then +0x70). The restamps are the compiler's, and
// so is the chain into ~ElementDescription (the base's _textName) and
// ~External (retail's tail JMP at 0x1800d12ac). The +0x68 external is NOT
// released.
namespace UI {
// B4 Y RT-UI: the two releases (1800d1210 +0x80, 1800d1246 +0x70) are the
// String members' destructors, run after this empty body in reverse
// declaration order: retail's order.
EditDescription::~EditDescription() {
}

// ?CreateNewObject@EditDescription@UI@@SAPEAVBase@Object@@XZ  0x18003f7c0
// The FACTUI1 row this replaces: MemAlloc(0x88, 8), retail's null branch,
// then the out-of-line ctor.
Object::Base* EditDescription::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(EditDescription), 8);
    if (p == nullptr) return nullptr;
    return ::new (p) EditDescription();
}
// ?CreateInPlaceObject@EditDescription@UI@@SAPEAVBase@Object@@PEAX@Z  0x18003f840
Object::Base* EditDescription::CreateInPlaceObject(void* place) {
    if (place == nullptr) return nullptr;
    return ::new (place) EditDescription();
}
}  // namespace UI

// ============================================================================
// ENDGAME B.3.9 TI-UI (B39 D6): UI::EditDescription's own type record, built at CRT init
// (head-insert into Object::BaseTypeInfo::_firstTypeInfo). Literals copied from
// the retired minted row ?_typeInfo@EditDescription@UI@@ (0x1801f9f28): name "EditDescription", parent
// "ElementDescription" (retail ?_ParentTypeName@EditDescription@UI@@), no file extension; factories:
// the class's own.
// ============================================================================
DEFINE_TYPE(UI::EditDescription, "ElementDescription", nullptr)
