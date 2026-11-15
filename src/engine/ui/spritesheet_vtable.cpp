// ============================================================================
// src/engine/ui/spritesheet_vtable.cpp — WAVE 10 LANE UI4
//   the SpriteSheet virtual table's slot bodies
// ============================================================================
//
// ---------------------------------------------------------------------------
// SLOT 5 IS NOW PORTED — and the recorded blocker was WRONG IN TWO WAYS
// ---------------------------------------------------------------------------
// The paragraph that used to sit here said `?Serialize@SpriteSheet@UI@@` was
// held back because it "drives four calls through the ENCODING vtable at +0xa0
// and one at +0x78", offsets that could not be named from the profile build.
// Wave 10B lane SS re-derived that from the shipped disassembly rather than
// inheriting it, and BOTH halves of the claim are false:
//
//   1. THOSE ARE NOT ENCODING SLOTS. Read the `this` register at each call:
//      `MOV RCX,qword ptr [RSP + 0x30]` (0x18011d793, 0x18011d7a8, 0x18011d7bd,
//      0x18011d7d6) loads the local at RSP+0x30, which is the SERIALIZER's
//      +0x08 field — the `IO::ReaderWriter*` the encoding's factory slot just
//      returned. The Encoding itself is touched ONCE in this whole body, at
//      0x18011d755, to pick +0x18 or +0x20. +0x78 and +0xa0 are ReaderWriter
//      slots, not Encoding slots.
//
//   2. THE MAP ALREADY EXISTED. `rt::io::rw_vtbl` in
//      src/engine/io/Serializer.h has carried all 26 RETAIL ReaderWriter
//      slot offsets since Wave 4, pinned twice against the shipped bytes
//      (each Serializer forwarder's own tail-jump displacement, AND the
//      contents of ??_7BinaryReader@IO@@6B@ / ??_7BinaryWriter@IO@@6B@). It
//      names +0x78 `kScalarBool` and +0xa0 `kScalarInt`, and its banner
//      already carries the standing warning against the profile build's
//      +0x90/+0xb8. Nothing had to be produced to expire this holdback.
//
// So the holdback was a MISATTRIBUTION (ReaderWriter slots called Encoding
// slots) on top of a search that never reached the file holding the answer.
// Recorded here rather than deleted, per PLAN §6 step 8, so the wrong claim is
// recognisable if it is met again — the same paragraph is repeated in
// WAVE10_LANE_UI4.md §6 and WAVE10_UI_CENSUS.md §6, which are another lane's
// documents and were not edited here.
//
// A THIRD independent confirmation of the same offsets fell out of the port:
// the per-entry helper at 0x180078ad0 drives each SpriteInfo through +0xa0 for
// its five ints and +0x80 for its four floats — and +0x80 is
// `rw_vtbl::kScalarFloat`, so a slot this body never uses itself still lands
// exactly where Wave 4's map puts it.
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>

#include "io/Serializer.h"
#include "io/Stream.h"
#include "ui/SpriteSheet.h"
#include "object/External.h"
#include "system/String.h"
#include "graphics/Material.h"   // B.4 P2 X: T::GetTypeInfo() for Object::SerializeExternal<T>
#include "../collection/array_internal.h" // ENDGAME B.3.2 step 2 — kSpec_Sprite38Z


// ############################################################################
// UI::SpriteSheet's slot bodies (ENDGAME B.3.6 block D), inside `namespace UI`
// after the anonymous namespace (B34_BRIEFS A.2). ENDGAME B.3.9 UI-D: a live
// SpriteSheet wears its COMPILER table; the minted table and the slot doors
// are retired.
// ############################################################################
namespace UI {

// ---- slots 1/2 and the static TypeId (0x180002e50 / 0x180002e30 /
// 0x180002e40, `MOV RAX,[0x1801fadd8]` / `MOV EAX,[0x1801fadd0]`) are
// DECLARE_TYPE's, over the class's own record (ENDGAME B.3.9 TI-UI).

// ---- slot 3  ?GetFileVersion@SpriteSheet@UI@@UEBAHXZ  0x18011d710 ----------
//   MOV EAX,6 / RET. The sheet resource's on-disk format version. A LITERAL.
int32_t SpriteSheet::GetFileVersion() const { return 6; }

// ---- slot 4  ?Compile@SpriteSheet@UI@@UEAA…  0x18011d720 — OVERRIDE ----------
//   MOV EAX,1 / RET. It IGNORES its Filename argument entirely — the return is
//   an Object::External::CompileResult and 1 is the constant answer. Not a
//   stub of ours: those two instructions are the whole shipped body. It is a
//   DIFFERENT body from Object::External's own slot 4 (0x1800066e0) even though
//   both answer 1 (RULE B6-1.5).
Object::External::CompileResult SpriteSheet::Compile(const System::Filename&) { return 1; }

// ---- slot 7  ?Destruct@SpriteSheet@UI@@UEAAXXZ  0x180003180 ----------------
//   MOV RAX,[RCX] / XOR EDX,EDX / JMP qword ptr [RAX]
// A tail jump into SLOT 0 of the object's own vtable — the vector deleting
// destructor — with flag 0, i.e. "destroy, do not free". Reading the vtable
// from the OBJECT and not from a resolved constant is load-bearing: a derived
// SpriteSheet would dispatch to its own slot 0.
void SpriteSheet::Destruct() { this->~SpriteSheet(); }

// ---- slot 8  ?Construct@SpriteSheet@UI@@UEAAPEAVExternal@Object@@XZ  0x180003100
//   if (this) { <the constructor, inlined>; return this; } else return nullptr.
//   A member is never called on null; the constructor is the class's own.
Object::External* SpriteSheet::Construct() {
    return ::new (static_cast<void*>(this)) SpriteSheet();
}

// ---- slot 6  ?Reconstruct@SpriteSheet@UI@@UEAAPEAVExternal@Object@@XZ  0x180003080
//   Destruct (through slot 0, virtually) then construct on the SAME storage;
//   no null test on this path (0x180003092 dereferences RCX immediately).
Object::External* SpriteSheet::Reconstruct() {
    this->~SpriteSheet();
    return ::new (static_cast<void*>(this)) SpriteSheet();
}

// ---- the factory pair (ENDGAME B.3.9 UI-D: the class's own statics) --------
// ?CreateNewObject@SpriteSheet@UI@@SAPEAVBase@Object@@XZ  0x180002f40
// The retired FACTUI2 row: the locked MemAlloc of 0x58 bytes, align 8
// (`mov EDX,0x58 / lea R8D,[RDX-0x50]` at 0x180002f71); retail's null branch;
// then ??0SpriteSheet inlined.
Object::Base* SpriteSheet::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(SpriteSheet), 8);
    if (p == nullptr) return nullptr;
    return ::new (p) SpriteSheet();
}
// ?CreateInPlaceObject@SpriteSheet@UI@@SAPEAVBase@Object@@PEAX@Z  0x180003000
Object::Base* SpriteSheet::CreateInPlaceObject(void* place) {
    if (place == nullptr) return nullptr;
    return ::new (place) SpriteSheet();
}

}  // namespace UI

// ============================================================================
// slot 5  ?Serialize@SpriteSheet@UI@@UEAA_NAEAVStream@IO@@@Z   0x18011d730
// ============================================================================
//
// TRANSCRIPTION, instruction by instruction (retail 0x18011d730..0x18011d845):
//
//   18011d744  MOV RBX,RCX                  this
//   18011d747  MOV [RSP+0x28],RDX           IO::Serializer local: .stream  = &stream
//   18011d74c  MOV [RSP+0x30],0                                   .readerWriter = null
//   18011d755  MOV RCX,[RDX+0x10]           encoding = stream->encoding
//   18011d75f  CMP [RDX+0x8],R8D            direction == 0 ?
//   18011d76a  CALL [RAX+0x18]  / +0x20     CreateReader / CreateWriter (enc, &ser, 0)
//   18011d772  MOV [RSP+0x30],RAX           ser.readerWriter = rw
//     ^ this is the IO::Serializer CONSTRUCTOR, inlined, with version = 0.
//       It is NOT a call to ??0Serializer@IO@@ — MSVC inlined it here.
//       Constructing it MOVES THE STREAM CURSOR by 12 bytes
//       (src/engine/io/Serializer.h).
//
//   18011d780  CALL 0x1800c9930  (RBX+0x40) String::Serialize  m_groupName    <- FIRST
//   18011d78e  CALL 0x1800c9930  (RBX+0x38) String::Serialize  m_textureName  <- SECOND
//     ^ the ORDER IS +0x40 THEN +0x38, i.e. the two strings come off the wire
//       in the opposite order to their addresses. Getting this backwards would
//       not crash: it would swap two String records of usually-different length
//       and desync every subsequent record. Transcribed from the LEA operands,
//       not from field order.
//
//   18011d7a2  CALL [RAX+0xa0]   (RBX+0x48, null) int   m_imageWidth
//   18011d7b7  CALL [RAX+0xa0]   (RBX+0x4c, null) int   m_imageHeight
//   18011d7cc  CALL [RAX+0x78]   (RBX+0x50, null) bool  m_padForFiltering
//     ^ RCX for all three is reloaded from [RSP+0x30] each time — the
//       ReaderWriter, not the Encoding. See the banner.
//
//   18011d7cf  MOV EAX,[RBX+0x28] / MOV [RSP+0x50],EAX   count = m_sprites._size
//   18011d7e6  CALL [RAX+0xa0]   (&count, null)          int count
//     ^ a STACK COPY is serialised, never the field. On the read path the
//       count arrives here and m_sprites._size is updated by SetSize below, not
//       by this record.
//
//   18011d7ec  MOV RAX,[RSP+0x28] / CMP [RAX+0x8],0 / JNZ
//   18011d7ff  CALL 0x180078950  (&m_sprites, count)   Array<SpriteInfo>::SetSize
//     ^ READ PATH ONLY. Third argument is absent; retail passes two.
//
//   18011d804  MOV R8,[RBX+0x20]                        RE-READ after SetSize
//   18011d811  CALL 0x180078ad0  (&ser, count, data)    per-entry serialize
//     ^ the data pointer is loaded AFTER SetSize, which reallocates. Hoisting
//       that load above the branch would hand the helper a freed block on the
//       read path.
//
//   18011d81f  CALL 0x180014520  (RBX+0x30, &ser)       ExternalPtr<T>::Serialize
//   18011d825  if (rw) rw->vtable[0](rw, 1)             inlined Serializer dtor
//   18011d839  MOV AL,1                                 unconditional `true`
//
// ---------------------------------------------------------------------------
// THE FOUR CALLEES
// ---------------------------------------------------------------------------
//   0x1800c9930  ?Serialize@String@System@@QEAAXAEAVSerializer@IO@@@Z — the member
//                System::String::Serialize (src/engine/system/string.cpp).
//
//   0x180078950  Array<UI::SpriteSheet::SpriteInfo>::SetSize, stride 0x38 — the
//                kSpec_Sprite38Z row in src/engine/collection/array.cpp.
//
//   0x180078ad0  the per-SpriteInfo record walker (name, group, x, y, width,
//                height, reserved, u0, v0, u1, v1) — RtUi_SpriteSheet_SerializeEntries
//                below.
//
//   0x180014520  ExternalPtr<Graphics::MaterialInstance>::Serialize (tabulated in
//                docs/history/runtime_port/WAVE10_LANE_EXTPTR.md) —
//                Object::SerializeExternal<Graphics::MaterialInstance>, through
//                MaterialPtrSerialize below. THIS ALSO NAMES SpriteSheet+0x30: it is
//                not an anonymous qword, it is the sheet's MaterialInstance handle.
//
// ============================================================================

namespace {

// ExternalPtr<MaterialInstance>::Serialize (0x180014520), by name. `m_materialInstance`
// is the sheet's Graphics::MaterialInstance*, and the body takes the IO::Serializer.
void MaterialPtrSerialize(Graphics::MaterialInstance** field, IO::Serializer* ser) {
    Object::SerializeExternal<Graphics::MaterialInstance>(*ser, *field);
}

}  // namespace

// ============================================================================
// WAVE 29 LANE WALL — `0x180078ad0`, the per-SpriteInfo record walker.
//
//
// ---------------------------------------------------------------------------
// EXTENT, DERIVED FROM THE INSTRUCTION STREAM (not `.pdata` — warning 16)
// ---------------------------------------------------------------------------
// 0x180078ad0 .. 0x180078bd8, i.e. through the `REPZ RET` at 0x180078bd7,
// followed by seven INT3 and then an unrelated `XOR EAX,EAX / MOV [RCX],RAX`
// body at 0x180078be0. 0x109 bytes, one loop, no SEH, and no allocation of
// its own beyond whatever `String::Serialize` does.
//
// ---------------------------------------------------------------------------
// SIGNATURE AND BODY — objdump of the shipped Runtime-x64.dll
// ---------------------------------------------------------------------------
//   (RCX = Serializer*, EDX = count, R8 = SpriteInfo* entries)
//
//   TEST EDX,EDX ; JLE  -> a count of zero or less does NOTHING, including no
//                          virtual call. `count` is read SIGNED; the parameter
//                          is `int32_t`.
//   RBX = entries + 0x10, and the loop is a DO-WHILE on `DEC RSI`, stride 0x38.
//
//   per entry:
//     CALL 0x1800c9930   (RBX-0x10, ser)   String::Serialize  name   +0x00
//     CALL 0x1800c9930   (RBX-0x08, ser)   String::Serialize  group  +0x08
//     vtable +0xa0 (kScalarInt)   RBX+0x00   -> x           +0x10
//     vtable +0xa0                RBX+0x04   -> y           +0x14
//     vtable +0xa0                RBX+0x0c   -> width       +0x1c
//     vtable +0xa0                RBX+0x10   -> height      +0x20
//     vtable +0xa0                RBX+0x08   -> _reserved18 +0x18   <-- LAST
//     vtable +0x80 (kScalarFloat) RBX+0x14   -> u0          +0x24
//     vtable +0x80                RBX+0x18   -> v0          +0x28
//     vtable +0x80                RBX+0x1c   -> u1          +0x2c
//     vtable +0x80                RBX+0x20   -> v1          +0x30
//
// THE FIELD AT +0x18 IS SERIALIZED FIFTH OF THE FIVE INTS, NOT THIRD. The
// record is walked in AddSpriteInfo's PARAMETER order — x, y, width, height —
// with the always-zero `_reserved18` appended at the end, which is why the
// stream order is not the declaration order. A loop written "in field order"
// emits the same eleven values in the wrong SEQUENCE and desynchronises every
// following record in the file: the failure is not a crash, it is a corrupt
// sprite atlas, and nothing in this tree would report it. The order here is
// transcribed from the displacement on each LEA, one at a time.
//
// It agrees with the field list this file's own door table at the top already
// recorded — "name, group, x, y, width, height, reserved, u0, v0, u1, v1" —
// an annotation that HELD when it was finally checked. Recorded because the
// standing expectation in this project is the opposite
// ([[reference_pdata_understates_by_24x_and_annotations_lie]]) and a warning
// that is always right is a warning nobody re-checks.
//
// `+0x34` IS NOT SERIALIZED. There is no eleventh scalar call. That agrees
// with SpriteSheet.h's Wave 7 correction, which proved from the element
// copy-construct helper that +0x34 is never written by either implementation.
//
// RETAIL RELOADS `ser->readerWriter` AND ITS VTABLE POINTER BEFORE EVERY ONE
// of the nine scalar calls (`MOV RCX,[RDI+8] ; MOV RAX,[RCX]`), rather than
// hoisting either out of the loop. `IO::Serializer::Serialize` is exactly that
// reload, because `String::Serialize` runs between the reload and the first
// scalar call and a hoist would be a silent behavioural change if it ever
// replaced the codec.
// ---------------------------------------------------------------------------
extern "C" void RtUi_SpriteSheet_SerializeEntries(IO::Serializer* ser,
                                                  int32_t     count,
                                                  SpriteInfo* entries) {
    if (count <= 0) return;                 // 0x180078ad0  TEST EDX,EDX ; JLE

    SpriteInfo* e = entries;
    do {
        e->name.Serialize(*ser);          // +0x00  CALL 0x1800c9930
        e->group.Serialize(*ser);         // +0x08  CALL 0x1800c9930

        ser->Serialize(e->x, nullptr);            // +0x10
        ser->Serialize(e->y, nullptr);            // +0x14
        ser->Serialize(e->width, nullptr);        // +0x1c
        ser->Serialize(e->height, nullptr);       // +0x20
        ser->Serialize(e->_reserved18, nullptr);  // +0x18  FIFTH — see the banner

        ser->Serialize(e->u0, nullptr);           // +0x24
        ser->Serialize(e->v0, nullptr);           // +0x28
        ser->Serialize(e->u1, nullptr);           // +0x2c
        ser->Serialize(e->v1, nullptr);           // +0x30

        ++e;                                // ADD RBX,0x38
    } while (--count != 0);                 // DEC RSI ; JNE
}

namespace UI {

bool SpriteSheet::Serialize(IO::Stream& streamv) {
    // The inlined ??0Serializer (0x18011d747..0x18011d772): the stream, then
    // CreateReader / CreateWriter of its encoding, version 0.
    IO::Serializer ser(streamv, 0);

    m_groupName.Serialize(ser);     // +0x40 FIRST — see banner
    m_textureName.Serialize(ser);   // +0x38 second

    ser.Serialize(m_imageWidth, nullptr);       // +0x48
    ser.Serialize(m_imageHeight, nullptr);       // +0x4c
    ser.Serialize(m_padForFiltering, nullptr);       // +0x50, Serialize(bool&)

    int32_t count = m_sprites._size;   // +0x28, into the RSP+0x50 temporary
    ser.Serialize(count, nullptr);

    if (ser._stream->_direction == 0)
        rtcoll::AsArray<&rtcoll::kSpec_Sprite38Z>(&m_sprites)
            ->SetSize(static_cast<int>(count));            // retail 0x180078950

    // Re-read AFTER SetSize: it reallocates.
    RtUi_SpriteSheet_SerializeEntries(&ser, count, m_sprites._data);   // 0x180078ad0

    MaterialPtrSerialize(&m_materialInstance, &ser);  // +0x30

    // EXT SEAM (NOT RETAIL): ext.create_on_demand's prefetch (SpriteSheet.h has the contract).
    if (ser._stream->_direction == 0 && _extLoaded != nullptr) _extLoaded(*this);

    // The inlined ~Serializer (0x18011d825) deletes the ReaderWriter.
    return true;
}

void (*SpriteSheet::_extLoaded)(const SpriteSheet& sheet) = nullptr;

}  // namespace UI

// ============================================================================
// ENDGAME B.3.9 TI-UI (B39 D6): UI::SpriteSheet's own type record, built at CRT init
// (head-insert into Object::BaseTypeInfo::_firstTypeInfo). Literals copied from
// the retired minted row ?_typeInfo@SpriteSheet@UI@@ (0x1801fadd0): name "SpriteSheet", parent
// "Object::External" (retail ?_ParentTypeName@SpriteSheet@UI@@), no file extension; factories:
// the class's own.
// ============================================================================
DEFINE_TYPE(UI::SpriteSheet, "Object::External", nullptr)
