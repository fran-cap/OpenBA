// ============================================================================
// src/engine/ui/combodescription.cpp — UI::ComboDescription, the whole class
// ============================================================================
//
// ??_7ComboDescription@UI@@6B@ (retail 0x180159090), ten slots:
//   0  deleting dtor      0x180074e70  ComboDescription.h (the class's own)
//   1  TypeName           0x180074ae0  here
//   2  Type               0x180074ac0  here
//   3  GetFileVersion     0x1801129b0  here  (returns 0x2104)
//   4  External::Compile  0x1800066e0  already ported
//   5  Serialize          0x1801129c0  here
//   6  Reconstruct        0x180074cc0  here
//   7  Destruct           0x180074d70  here
//   8  CreateInstance     0x180112a70  here
//   9  Construct          0x180074d20  here
// plus ??1ComboDescription@UI@@UEAA@XZ (0x180112370).
//
// ENDGAME B.3.6 block C1: the bodies are MEMBERS of `UI::ComboDescription`
// (src/engine/ui/ComboDescription.h).
//
// DERIVES FROM UI::ButtonDescription, exactly as CheckDescription does, and
// reaches the same three places in src/engine/ui/buttondescription.cpp:
// ??0ButtonDescription (0x180110b10), ButtonDescription::Serialize
// (0x1801113e0) and the shared applier (0x1801108d0) — the last two as
// QUALIFIED member calls. `??0ComboDescription@UI@@QEAA@XZ` (0x180112320) IS
// exported; the four stores it adds are the real ctor in ComboDescription.h,
// which Construct and Reconstruct run.
//
// ----------------------------------------------------------------------------
// WHAT IS **NOT** PORTED HERE, AND WHY
// ----------------------------------------------------------------------------
// `CreateInstance` calls six functions that belong to other classes, and all
// six are called by name rather than copied:
//   0x180035020  UI::String::operator System::String   PORTED (ui/sprite.cpp)
//   0x180034860  Array<System::String>::SetSize(n,fill) PORTED (collection/array.cpp
//                kSpec_StringRef)
//   0x18004bfc0  Array<{ptr,int}>::SetSize(n,fill)      PORTED (collection/array.cpp)
//   0x180024fe0  Array<void*>::SetSize(n,fill)          PORTED (collection/array.cpp)
//   0x180120f20  UI::UserInterface::CloseDialog         PORTED (ui/userinterface_close.cpp)
//   0x180112180  ComboElement::UpdateText               PORTED (ui/widgets_input.cpp)
//
// Layout: src/engine/ui/ComboDescription.h over DescriptionsBatch1.h.
// sizeof 0xb8.
// ============================================================================
#include <new>

#include "description_common.h"
#include "ui/ComboDescription.h"
#include "ui/ComboElement.h"
#include "ui/String.h"
#include "ui/StringTable.h"   // B4: StringTable::_entries._size
#include "ui/UserInterface.h"
#include "ui/DialogInstance.h"
#include "ui/ComboDialog.h"     // ComboElement::_openDialog

namespace {

using namespace rtdesc;

}  // namespace

namespace UI {

int32_t ComboDescription::GetFileVersion() const { return 0x2104; }

// The four stores ComboDescription adds on top of its base's constructor,
// transcribed from 0x180074d42..0x180074d63 (Construct) and identically inside
// Reconstruct.
// ENDGAME B.3.9 UI-A: that sequence is the real ctor (ComboDescription.h).
Object::External* ComboDescription::Construct() {
    return ::new (this) ComboDescription();                    // 180074d3c..180074d63
}
Object::External* ComboDescription::Reconstruct() {
    this->~ComboDescription();                                           // 180074cd7
    return ::new (this) ComboDescription();                    // 180074ce1..180074d08
}
void ComboDescription::Destruct() { this->~ComboDescription(); }

// ============================================================================
// ?Serialize@ComboDescription@UI@@UEAA_NAEAVStream@IO@@@Z   0x1801129c0
// ============================================================================
// Base is ButtonDescription::Serialize (QUALIFIED). Two ExternalPtr<Element-
// Description> then ONE ExternalPtr<StringTable> — a DIFFERENT serializer
// (0x1800042a0, not 0x180038f90), which is how the +0xb0 field's type is known.
bool ComboDescription::Serialize(IO::Stream& streamv) {
    IO::Stream* stream = &streamv;

    ButtonDescription::Serialize(*stream);                   // 1801129da

    IO::Serializer ser(*stream, 0);  // ??0Serializer (0x1800b9960) inlined

    Object::SerializeExternal<UI::ElementDescription>(ser, _listDesc);    // 180112a1b
    Object::SerializeExternal<UI::ElementDescription>(ser, _itemDesc);    // 180112a2c
    Object::SerializeExternal<UI::StringTable>(ser, _stringTable); // 180112a3d

    // ~Serializer (0x1800b99b0) inlined here: rw's scalar deleting dtor                 // 180112a55
    return true;
}

// ============================================================================
// ?CreateInstance@ComboDescription@UI@@...   retail 0x180112a70   (slot 8)
// ============================================================================
// RCX = this (RBP), RDX = the hidden return slot (RSI), R8 = Dialog* (R14).
//
// ComboElement is 0x128 and its constructor is OUT OF LINE
// (`??0ComboElement@UI@@QEAA@XZ`, 0x1801123f0, the real ctor since B.3.9);
// retail takes its RETURN value as the element.
//
// After the shared ButtonDescription applier, the body POPULATES THE COMBO FROM
// ITS STRING TABLE: for every entry i of `_stringTable` it builds a
// `UI::String{table, i}` ON THE STACK, converts it to a System::String, and
// pushes one entry into each of THREE parallel element arrays:
//     _strings  (+0xe8)   Array<System::String>   the label          (0x180034860)
//     _sprites  (+0xf8)   Array<{ptr,int}>        the sprite, {0,-1} (0x18004bfc0)
//     _userData (+0x108)  Array<void*>            the user data, 0   (0x180024fe0)
// and then releases the temporary string with retail's usual asymmetry.
//
// THREE TRANSCRIPTION FACTS, none of them inferable from the C:
//   1. `_stringTable` is RE-READ at the BOTTOM of every iteration to recompute
//      the loop bound (`MOV RAX,[RBP+0xb0]` at 0x180112c08). Not hoisted.
//   2. THE WHOLE TAIL — the CloseDialog and the +0x118 reset — IS INSIDE the
//      `if (_stringTable != nullptr)` arm. The `JZ 0x180112c5c` at 0x180112b37
//      jumps past all of it, straight to `*ret = elem`.
//   3. The three fills are three DIFFERENT stack objects and one of them, the
//      {0, -1} pair at [RSP+0x38], is built INSIDE the loop even though it is
//      loop-invariant.
//
// ONE STACK SLOT ([RSP+0x90]) serves as the applier's handle, the converted
// System::String and the CloseDialog argument in retail; here they are three
// typed locals.
Object::InternalPtr<Element>* ComboDescription::CreateInstance(
        Object::InternalPtr<Element>* ret, const Dialog* dialog) const {
    void* raw = MemAlloc(0x128, 8);                         // 180112ab3

    ComboElement* elem = nullptr;                           // R12 == 0
    if (raw != nullptr)                                     // 180112ae4
        elem = ::new (raw) ComboElement();                  // 180112ae9 ??0ComboElement

    // Retail keeps ONE stack slot ([RSP+0x90]) for the applier's handle, the
    // converted System::String and the CloseDialog argument; nothing reads one
    // use's value in another, so here they are three typed locals.
    Object::InternalPtr<ButtonElement> slot(elem);          // 180112af6  [RSP+0x90]
    ButtonDescription::CreateInstance(&slot, dialog);       // 180112b0c

    elem->_listDesc = _listDesc;                            // 180112b18 QWORD
    elem->_itemDesc = _itemDesc;                            // 180112b26 QWORD

    StringTable* table = _stringTable;
    if (table != nullptr) {                                 // 180112b37
        int32_t i = 0;
        if (table->_entries._size > 0) {                    // 180112b40
            void* userDataFill = nullptr;                   // 180112b4a  [RSP+0x98]
            do {
                // UI::String{ StringTable*, index } built in place, then
                // converted into the slot.
                UI::String key;                             // [RSP+0x28]/[RSP+0x30]
                key.table = table;
                key.index = i;
                key.pad0C = 0;
                // LEFT (H5): the String is held as raw char* storage.
                char* str = nullptr;
                new (static_cast<void*>(&str)) System::String(key.operator System::String());   // 180112b68 retslot

                UI::Sprite spriteFill;                      // 180112b6e / 180112b73: {0, -1}
                spriteFill.pad0C = 0;

                rtcoll::RtColl_ArraySetSizeFill_180034860(
                    &elem->_strings, elem->_strings._size + 1, &str);           // 180112b92
                rtcoll::RtColl_ArraySetSizeFill_18004bfc0(
                    &elem->_sprites, elem->_sprites._size + 1, &spriteFill);    // 180112bab
                rtcoll::RtColl_ArraySetSizeFill_180024fe0(
                    &elem->_userData, elem->_userData._size + 1, &userDataFill); // 180112bc7

                // The temporary System::String, released in place.
                ReleaseStringField(str);                    // 180112bd5 .. 180112bfe

                ++i;                                        // 180112c06
                table = _stringTable;                       // 180112c08
            } while (i < table->_entries._size);
        }

        // 180112c18: close any dialog the element is still showing.
        DialogInstance* const open = elem->_openDialog;
        if (open != nullptr) {                              // 180112c1f
            Object::InternalPtr<DialogInstance> dlgSlot(open);   // 180112c24
            open->_ui->CloseDialog(&dlgSlot);               // 180112c38
            elem->_openDialog = nullptr;                    // 180112c3d
        }

        if (elem->_currentItem != 0) {                      // 180112c44
            elem->_currentItem = 0;                         // 180112c4d
            // ComboElement::UpdateText (0x180112180).
            elem->UpdateText();                             // 180112c57
        }
    }

    new (static_cast<void*>(ret)) Object::InternalPtr<Element>(elem);   // 180112c5c
    return ret;
}

}  // namespace UI

// ??1ComboDescription@UI@@UEAA@XZ   0x180112370 -- three restamps, the base's
// _textName, the tail JMP into ~External. ENDGAME B.3.9 UI-A: EMPTY; all of it
// is the compiler's chain.
namespace UI {
ComboDescription::~ComboDescription() {}
}  // namespace UI

namespace UI {
// ?CreateNewObject@ComboDescription@UI@@SAPEAVBase@Object@@XZ  0x180074bd0
// The factory-interpreter row this replaces: MemAlloc(0xb8, 8), retail's
// null branch, then ??0ButtonDescription and the three stores.
Object::Base* ComboDescription::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(ComboDescription), 8);
    if (p == nullptr) return nullptr;
    return ::new (p) ComboDescription();
}
// ?CreateInPlaceObject@ComboDescription@UI@@SAPEAVBase@Object@@PEAX@Z  0x180074c70
Object::Base* ComboDescription::CreateInPlaceObject(void* place) {
    if (place == nullptr) return nullptr;
    return ::new (place) ComboDescription();
}
}  // namespace UI

// ============================================================================
// ENDGAME B.3.9 TI-UI (B39 D6): UI::ComboDescription's own type record, built at CRT init
// (head-insert into Object::BaseTypeInfo::_firstTypeInfo). Literals copied from
// the retired minted row ?_typeInfo@ComboDescription@UI@@ (0x1801faa18): name "ComboDescription", parent
// "ButtonDescription" (retail ?_ParentTypeName@ComboDescription@UI@@), no file extension; factories:
// the class's own.
// ============================================================================
DEFINE_TYPE(UI::ComboDescription, "ButtonDescription", nullptr)
