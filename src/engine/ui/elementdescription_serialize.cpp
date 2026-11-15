// ============================================================================
// src/engine/ui/elementdescription_serialize.cpp — Wave 10, lane EXTPTR
//   `?Serialize@ElementDescription@UI@@UEAA_NAEAVStream@IO@@@Z`
//   retail 0x180116b00, ordinal 3687, 57 instructions.
// ============================================================================
//
// THE ROW THIS WAVE EXISTS TO CLOSE. Wave 7 lane B declined it, Wave 9 lane
// UIW2 declined it twice, and `docs/history/runtime_port/WAVE10_EXTPTR_SERIALIZE.md`
// answered the last blocker: its three unported callees at +0x48/+0x50/+0x58
// are ONE body. That body is now
// `src/engine/object/external_ptr_serialize.cpp`, and this file is what it
// unblocks.
//
//
// ---------------------------------------------------------------------------
// SHAPE
// ---------------------------------------------------------------------------
// VIRTUAL (`UEAA`). RCX = this, RDX = an `IO::Stream&` — NOT a Serializer. The
// body builds a 16-byte `IO::Serializer` in its own frame ([RSP+0x28] = the
// Stream*, [RSP+0x30] = the codec) and asks the stream's `Encoding` for a
// reader or a writer depending on `Stream+0x08`:
//
//     Serializer ser{ &stream, nullptr };
//     Encoding* enc = stream.encoding;                  // [stream+0x10]
//     ser.readerWriter = stream.direction == 0          // [stream+0x08]
//         ? enc->vtable[+0x18](enc, &ser, 0)            // CreateReader
//         : enc->vtable[+0x20](enc, &ser, 0);           // CreateWriter
//
// The third argument is the save-format VERSION and it is ZERO: `XOR R8D,R8D`
// at 0x180116b24 is what the direction test compares against, and R8D is never
// reloaded before the factory call. Serializer.h records that this argument is
// NOT ignored — BinaryWriter writes it into the file — so it is worth spelling
// out. The epilogue is `if (rw) rw->vtable[0](rw, 1)`, the scalar DELETING
// destructor; skipping it leaks 0x30 bytes per call.
//
// It always returns `true` (`MOV AL,1`); there is no failure path.
//
// ---------------------------------------------------------------------------
// THE FOURTEEN RECORDS, IN RETAIL'S ORDER — WHICH IS NOT DECLARATION ORDER
// ---------------------------------------------------------------------------
// Getting the ORDER or the COUNT wrong here is the silent save-desync this
// project has paid for repeatedly, so the sequence is transcribed field by
// field from the listing and not from the header's field order:
//
//   +0x20 _x            int    slot +0xA0
//   +0x24 _y            int    slot +0xA0
//   +0x28 _width        int    slot +0xA0
//   +0x2c _height       int    slot +0xA0
//   +0x30 _minWidth     int    slot +0xA0
//   +0x34 _minHeight    int    slot +0xA0
//   +0x40 _elementType  uint   slot +0x98
//   +0x44 _flags        uint   slot +0x98
//   +0x3c _id           uint   slot +0x98   <-- declared int32_t; retail
//                                               serialises it through the
//                                               UNSIGNED slot. Transcribed as
//                                               retail has it: the two slots
//                                               are different vtable entries
//                                               and a reader picks by slot.
//   +0x38 _tabOrder     int    slot +0xA0   <-- and it comes AFTER +0x3c/+0x40
//   +0x48 ExternalPtr<UI::ElementDescription>   CALL 0x180038f90
//   +0x50 ExternalPtr<UI::Dialog>               CALL 0x180076610
//   +0x58 ExternalPtr<UI::StringTable>          CALL 0x1800042a0
//   +0x60 System::String                        CALL String::Serialize
//
// The +0x98 / +0xA0 slot numbers are RETAIL's. In Runtime-x64-profile.dll the
// same two live at +0xb0 and +0xb8, and copying them off a profile decompile
// would emit the wrong record type into every save
// ([[reference_encoding_vtable_offsets_differ_between_builds]]). They come from
// `rt::io::rw_vtbl`, which Wave 4 pinned twice against the shipped bytes.
//
// The third argument at every slot call is the record's NAME and retail passes
// NULL (`XOR R8D,R8D` before each one).
//
// ---------------------------------------------------------------------------
// BLAST RADIUS
// ---------------------------------------------------------------------------
// This is not a leaf in a save file. It is the `UIDescription` parse path of
// EVERY `.rsc` in the game, so a full boot is what actually drives it.
// ============================================================================

#include <cstdint>
#include <cstdio>

#include "io/Serializer.h"
#include "ui/ElementDescription.h"
#include "object/External.h"
#include "system/String.h"
#include "ui/Dialog.h"   // B.4 P2 X: T::GetTypeInfo() for Object::SerializeExternal<T>
#include "ui/StringTable.h"   // B.4 P2 X: T::GetTypeInfo() for Object::SerializeExternal<T>

// The three ExternalPtr<T>::Serialize instantiations this body dispatches are
// `Object::SerializeExternal<T>` (src/engine/object/External.h, the one body
// in src/engine/object/external_ptr_serialize.cpp).

// ============================================================================
// bool UI::ElementDescription::Serialize(IO::Stream&) — the MEMBER
// ============================================================================
// ENDGAME B.3.6 block A: the body MOVED here from the door (`self->x` became
// `x`). Slot 5 takes the `IO::Stream&`, as `Object::External` declares it.
namespace UI {

bool ElementDescription::Serialize(IO::Stream& streamArg) {
    // The prologue and epilogue, 0x180116b20 and 0x180116c4d: the inlined
    // ??0Serializer@IO@@ 0x1800b9960 (version 0) / ??1Serializer@IO@@ 0x1800b99b0.
    IO::Serializer serializer(streamArg, 0);
    IO::ReaderWriter* const rw = serializer._readerWriter;
    IO::Serializer& ser = serializer;

    rw->SerializeInt(_x, nullptr); // +0x20
    rw->SerializeInt(_y, nullptr); // +0x24
    rw->SerializeInt(_width, nullptr); // +0x28
    rw->SerializeInt(_height, nullptr); // +0x2c
    rw->SerializeInt(_minWidth, nullptr); // +0x30
    rw->SerializeInt(_minHeight, nullptr); // +0x34

    rw->SerializeUInt(_elementType, nullptr); // +0x40
    rw->SerializeUInt(_flags, nullptr); // +0x44
    rw->SerializeUInt(_id, nullptr); // +0x3c — the uint slot

    rw->SerializeInt(_tabOrder, nullptr); // +0x38

    Object::SerializeExternal<UI::ElementDescription>(ser, _childDesc);   // +0x48 0x180038f90
    Object::SerializeExternal<UI::Dialog>(ser, _toolTipDialog);           // +0x50 0x180076610
    Object::SerializeExternal<UI::StringTable>(ser, _stringTable);        // +0x58 0x1800042a0
    _textName.Serialize(ser);                                             // +0x60 0x1800c9930

    return true;                    // ~Serializer: the ReaderWriter's slot 0, flags 1
}

// (`~ElementDescription` is inline and empty in ElementDescription.h; its one
// release, +0x60, is the member `_textName`'s ~String.)

}  // namespace UI

// (ENDGAME B.3.9 UI-D: the slot door `RtUi_ElementDescription_Serialize` is
// retired with the minted `??_7ElementDescription@UI@@6B@`; every leaf's
// Serialize calls the member QUALIFIED, and the exe import slot that once
// bound the name is gone.)
