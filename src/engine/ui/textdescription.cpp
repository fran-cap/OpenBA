// ============================================================================
// src/engine/ui/textdescription.cpp — UI::TextDescription, the whole class
// ============================================================================
//
// The cutover boot's wall after the UI::*Description first half landed.
//
// ??_7TextDescription@UI@@6B@ (retail 0x180156be0), ten slots:
//   0  deleting dtor      0x180041e30  TextDescription.h (the class's own)
//   1  TypeName           0x180041ad0  here
//   2  Type               0x180041ab0  here
//   3  GetFileVersion     0x18011ee20  here  (returns 0x304 — per class)
//   4  External::Compile  0x1800066e0  already ported
//   5  Serialize          0x18011ee30  here
//   6  Reconstruct        0x180041c50  here
//   7  Destruct           0x180041c80  here
//   8  CreateInstance     0x18011f3d0  here
//   9  Construct          0x180041c70  here
// plus ??1TextDescription@UI@@UEAA@XZ 0x18011eae0 and the applier
// ?CreateInstance@TextDescription@UI@@ (0x18011ef70), which the shipped binary
// keeps as its own function — exported, so it is registered by NAME as well as
// by address.
//
// ENDGAME B.3.6 block C3: every slot body is a `UI::TextDescription` member
// (src/engine/ui/TextDescription.h, which now carries the layout this file
// used to spell as raw offsets — RULE B6-8). The registered doors keep their
// names and addresses and delegate with a QUALIFIED call: a live
// TextDescription wears the MINTED table. The ctor and dtor stay door bodies
// (no real ctor on a polymorphic class, B4b-2).
//
// The applier 0x18011ef70 is element_bodies_b5.cpp's (a non-slot member body
// and its door), reached here by address.
//
// THE CONSTRUCTOR IS OUT OF LINE IN RETAIL. Unlike Group/Ribbon/Image, whose
// ctor bodies are inlined into Construct/Reconstruct, TextDescription's is a
// real function at 0x18011ea20 that both Construct (`JNZ 0x18011ea20`, a TAIL
// jump) and Reconstruct (`JMP 0x18011ea20`) reach.
//
// ENDGAME B.3.9 block UI-A (B39_RULES D1/D2): the class is CONCRETE. That ctor
// is the real one (TextDescription.h); the factory pair is defined and
// registered here, replacing the ui_factory_wave28.cpp row. The ??0 / ??1
// doors and the slot doors, read only by the retired minted table, its
// RT_DELDTOR row and the interpreter rows, are gone; so is element_bodies_b5.cpp's
// ??0TextElement door (TextElement's real ctor is spelled below).
// ============================================================================
#include <cstddef>
#include <cstdint>

#include "element_common.h"   // rtuielem::AtomicExport (RULE B6-5), ResolveAddr
#include "ui/TextDescription.h"
#include "ui/TextElement.h"
#include "system/String.h"
#include "io/Serializer.h"
#include "io/Stream.h"
#include "system/MemoryAllocator.h"
#include "ui/Font.h"   // B.4 P2 X: T::GetTypeInfo() for Object::SerializeExternal<T>
#include "ui/StringTable.h"   // B.4 P2 X: T::GetTypeInfo() for Object::SerializeExternal<T>

namespace {


}  // namespace

namespace UI {

// 0x180041ab0 / 0x180041ad0: the first dword and the +8 qword of the minted
// BaseTypeInfo. TypeName is the KEY FUNCTION, so GCC emits this class's table
// here.
// 0x18011ee20: `MOV EAX,0x304 ; RET`. 0x304, not the 0x204 the Image/Ribbon
// siblings return — the file version is per class and must not be shared.
int32_t TextDescription::GetFileVersion() const { return 0x304; }

// 0x180041c70: `TEST RCX,RCX ; JNZ ctor ; XOR EAX,EAX ; RET` — a TAIL jump, so
// the ctor's return value (this) is Construct's. The null test sat in the
// retired door.
Object::External* TextDescription::Construct() {
    return ::new (this) TextDescription();
}
// 0x180041c50: slot 0 with flag 0, then a TAIL jump to the ctor.
Object::External* TextDescription::Reconstruct() {
    this->~TextDescription();                                           // 180041c5e
    return ::new (this) TextDescription();                     // 180041c68
}
// 0x180041c80 is three instructions: `MOV RAX,[RCX] ; XOR EDX,EDX ; JMP [RAX]`.
void TextDescription::Destruct() { this->~TextDescription(); }

// ??1TextDescription@UI@@UEAA@XZ   0x18011eae0 — retail's body, moved here
// from the retired door (ENDGAME B.3.9 UI-A). TWO String releases in retail,
// both with its asymmetry (field moved to `data-8` and freed on the last
// reference, NULLED otherwise): this class's own name at +0x78 under its own
// vtable -- here, through System::String's own Release -- then
// ElementDescription's _textName at +0x60 under the base vtable, which is the
// compiler's chain into ~ElementDescription, as is the tail JMP to ~External
// (0x18011eb66). THE TWO ExternalPtrs AT +0x68 AND +0x70 ARE NOT RELEASED --
// retail's, the same asymmetry ~ImageDescription has.
//
// B4 Y: the +0x78 release (18011eb04..18011eb22) is the member `_name`'s
// ~String, run after this (empty) body and before ~ElementDescription.
TextDescription::~TextDescription() {
}

// ============================================================================
// ?Serialize@TextDescription@UI@@UEAA_NAEAVStream@IO@@@Z   0x18011ee30
// ============================================================================
//   ElementDescription::Serialize(this, stream)      0x180116b00 (qualified)
//   rw = encoding factory, read side or write side by stream->mode
//   ExternalPtr<UI::Font>::Serialize(&_font, &pair)        0x18003f1e0
//   ExternalPtr<UI::StringTable>::Serialize(&_tbl, &pair)  0x1800042a0
//   System::String::Serialize(&_name, &pair)               0x1800c9930
//   rw->vt[0x78](rw, &_dropShadow, 0)   kScalarBool
//   rw->vt[0x78](rw, &_wordWrap, 0)     kScalarBool
//   rw->vt[0xa0](rw, &_wordWrapSize, 0)   kScalarInt
//   rw->vt[0x98](rw, &_justify, 0)      kScalarUInt   <- NOT in offset order, and
//                                                        NOT the same slot as _wordWrapSize
//   if (rw->GetVersion() >= 1 || !encoding->vt[0]())
//       rw->vt[0xa0](rw, &_maxWidth, 0)   kScalarInt
//   if (rw) rw->vt[0](rw, 1)
//
// *** THIS BODY PASSES 1, NOT 0, AS THE VERSION TO THE ENCODING FACTORY. ***
// `MOV R8D,0x1` at 0x18011ee61, where Group/Ribbon/Image all have `XOR R8D,R8D`.
// R8 is the `int version` the Serializer ctor forwards to
// ??0Binary{Reader,Writer}@IO@@, and BinaryWriter WRITES it into the stream.
//
// THE LAST FIELD IS CONDITIONAL. `_maxWidth` is serialized only when the reader's
// own version is >= 1, OR when the encoding's vtable slot 0 predicate says no.
// The two are read in THAT order and the `||` short-circuits.
bool TextDescription::Serialize(IO::Stream& streamv) {
    IO::Stream* stream = &streamv;

    UI::ElementDescription::Serialize(*stream);                 // 18011ee4a

    // The inlined ??0Serializer (0x1800b9960): CreateReader / CreateWriter
    // of the stream's encoding, version 1.
    IO::Serializer ser(*stream, 1);                      // [RSP+0x28], [RSP+0x30]
    IO::ReaderWriter* const rw = ser._readerWriter;

    Object::SerializeExternal<UI::Font>(ser, _font);          // 18011ee8b ExternalPtr<Font>
    Object::SerializeExternal<UI::StringTable>(ser, _stringTable2);  // 18011ee99 ExternalPtr<StringTable>
    _name.Serialize(ser);                                     // 18011eea7 String 0x1800c9930

    rw->SerializeBool(_dropShadow, nullptr);
    rw->SerializeBool(_wordWrap, nullptr);
    rw->SerializeInt(_wordWrapSize, nullptr);
    rw->SerializeUInt(_justify, nullptr);

    const int32_t rwVersion = rw->GetVersion();                // 18011ef0e

    bool emitTail = (rwVersion >= 1);                          // 18011ef11 CMP EAX,1 / JGE
    if (!emitTail) {
        emitTail = !stream->_encoding->IsBinary();             // 18011ef16, slot 0
    }
    if (emitTail) {
        rw->SerializeInt(_maxWidth, nullptr);
    }

    // The inlined ~Serializer deletes the ReaderWriter (18011ef41).
    return true;
}

// ============================================================================
// ?CreateInstance@TextDescription@UI@@...   retail 0x18011f3d0  (vtable slot 8)
// ============================================================================
// RETSLOT (RULE B6-2): `ret` is the caller's InternalPtr<Element>, written with
// the raw element pointer and handed back.
//
// The allocation is the MemoryAllocator::Alloc wrapper (0x1800c7a50) INLINED —
// the shipped body enters the same critical section at `g_heap+0x640`, calls
// the same 0x1800c6e60 with (0x100, 8), and leaves it. Calling the wrapper is
// the same sequence and keeps the one allocator door this tree already has.
//
// The applier 0x18011ef70 is the non-virtual `CreateInstance(InternalPtr<
// TextElement>, const Dialog*)`, whose body is element_bodies_b5.cpp's door:
// reached by address (registry-first) with the handle by ADDRESS (B6-11).
Object::InternalPtr<Element>* TextDescription::CreateInstance(
        Object::InternalPtr<Element>* ret, const Dialog* dialog) const {
    void* raw = System::MemoryAllocator::MemAlloc(0x100, 8);   // 18011f412 (inlined)

    TextElement* elem = nullptr;
    if (raw != nullptr)                                        // 18011f43d
        elem = ::new (raw) TextElement();                      // ??0TextElement 0x18011eb80

    Object::InternalPtr<TextElement> slot(elem);               // 18011f44e
    TextDescription::CreateInstance(&slot, dialog);            // 18011f45e  0x18011ef70

    *ret = Object::InternalPtr<Element>(elem);                 // 18011f463
    return ret;
}

}  // namespace UI

namespace UI {
// ?CreateNewObject@TextDescription@UI@@SAPEAVBase@Object@@XZ  0x180041bc0
// The factory-interpreter row this replaces: MemAlloc(0x90, 8), retail's
// null branch, then the out-of-line ctor ??0TextDescription (0x18011ea20).
Object::Base* TextDescription::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(TextDescription), 8);
    if (p == nullptr) return nullptr;
    return ::new (p) TextDescription();
}
// ?CreateInPlaceObject@TextDescription@UI@@SAPEAVBase@Object@@PEAX@Z  0x180041c40
Object::Base* TextDescription::CreateInPlaceObject(void* place) {
    if (place == nullptr) return nullptr;
    return ::new (place) TextDescription();
}
}  // namespace UI

// ============================================================================
// ENDGAME B.3.9 TI-UI (B39 D6): UI::TextDescription's own type record, built at CRT init
// (head-insert into Object::BaseTypeInfo::_firstTypeInfo). Literals copied from
// the retired minted row ?_typeInfo@TextDescription@UI@@ (0x1801faeb0): name "TextDescription", parent
// "ElementDescription" (retail ?_ParentTypeName@TextDescription@UI@@), no file extension; factories:
// the class's own.
// ============================================================================
DEFINE_TYPE(UI::TextDescription, "ElementDescription", nullptr)
