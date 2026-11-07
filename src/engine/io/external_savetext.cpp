// ============================================================================
// src/engine/io/external_savetext.cpp — Wave 11 follow-up 2, lane IOSTREAM
//   `Object::External::SaveText`                             retail 0x1800bb9d0
// ============================================================================
//
//   ?SaveText@External@Object@@QEAA_NAEBVFilename@System@@0@Z
//
// Write this object to `path` as TEXT: build an `IO::TextFileStream` tagged
// with the object's registered TYPE NAME and the caller's `name`, create the
// file, emit a byte-order mark if the encoding calls for one, then let the
// object serialize itself into the stream through its own vtable.
//
// ---------------------------------------------------------------------------
// THE ONE DIRECT OS CALL (this file was `external_savetext_win.cpp` until Phase D.4)
// ---------------------------------------------------------------------------
// Retail writes the byte-order mark with a DIRECT `WriteFile` through the
// FileInfo's OS handle rather than through the stream's own `Write` virtual
// (0x1800bbafc, an indirect call through Runtime's import thunk at
// 0x18014b108). Since THREADS E.5 that call is Platform::FileWrite
// (platform_file.h).
//
// THAT BRANCH IS DEAD IN THIS BODY AND IS TRANSCRIBED ANYWAY. Retail sets
// `_activeEncoding` to 2 eleven instructions earlier (0x1800bba90) and nothing
// between there and the test changes it, and 2 is one of the two values that
// selects NO mark. So the WriteFile is unreachable from this entry point as
// shipped. It is written out in full because "unreachable today" is a claim
// about the CALLER, not about the function: `_activeEncoding` is a plain field
// and `?Open@TextFileStream@` (vtable +0x40) takes a `Text::Format` argument
// that lands in it. Deleting the branch would make this port silently wrong for
// the first caller that sets a different format. See also the negative-control
// note at the bottom: this dead branch is exactly why control 5 in
// file_stream_probe.cpp cannot be caught.
//
// ---------------------------------------------------------------------------
// EVERY OFFSET THIS BODY TOUCHES IS PINNED TWICE
// ---------------------------------------------------------------------------
// src/engine/io/FileStream.h derived the TextFileStream layout from the two
// constructors and from retail's own accessors. This function is an entirely
// INDEPENDENT second source for the same offsets, because its inlined
// destructor epilogue reaches all of them from a frame pointer the layout had
// nothing to do with (rbp = rsp-0x37, so every offset below is odd):
//
//     [rbp-0x59] the object          +0x00   vptr, stamped three times
//     [rbp-0x49] +0x10  encoding     deleted through 0x180006a10
//     [rbp-0x41] +0x18  filename     String release idiom
//     [rbp-0x39] +0x20  file.handle  compared with -1 before CloseFile
//     [rbp-0x2d] +0x2c  file.cursor  `ADD [rbp-0x2d],EAX` after WriteFile
//     [rbp-0x21] +0x38  file.winHandle  loaded into WriteFile's RCX
//     [rbp-0x01] +0x58  objectName   String release idiom
//     [rbp+0x07] +0x60  objectType   String release idiom
//     [rbp+0x0f] +0x68  requestedFormat  = 2
//     [rbp+0x13] +0x6c  activeEncoding   = 2, and the BOM selector
//     [rbp+0x17] +0x70  buffer       BARE MemFree, not the String idiom
//
// That is the whole of TextFileStream, reached by a body that never saw a
// constructor. It also re-confirms `FileStream+0x20 IS a FileInfo` from a third
// direction: +0x2c and +0x38 are FileInfo::cursor and FileInfo::winHandle at
// exactly the offsets src/engine/io/FileSystem.h already asserts.
//
// ---------------------------------------------------------------------------
// ~FileInfo IS TRANSCRIBED, NOT CALLED
// ---------------------------------------------------------------------------
// Retail's epilogue calls 0x18000c180 (`~FileInfo`), which is one of the very
// few callees here that is NOT exported — package_file.cpp names it as an
// unported blocker. Calling it by ADDRESS would add a door to the cutover
// surface for a body that is two String releases
// (reference_porting_can_grow_the_cutover_surface.md). Its entire content is:
//
//     18000c19f  release the String at this+0x28   (FileInfo::_name1)
//     18000c1ce  release the String at this+0x20   (FileInfo::_name0)
//
// both through the same inlined idiom `RtString_ReleaseInline` already is, in
// that order. So it is written out here, the way this lane's `Add` writes out
// the inlined `~VisitorStream`.
//
// ---------------------------------------------------------------------------
// ORACLE
// ---------------------------------------------------------------------------
// This is a WRITE-PATH serializer, which is the shape
// reference_write_path_serialize_is_replayable.md says is replayable: the
// output is a file, so ours and theirs can each be asked to write the same
// object to two different paths and the BYTES compared. That is
// external_savetext_probe.cpp. Attendance and branch counters are below, and
// they are what distinguishes "ran and agreed" from "never ran".
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <new>

#include "platform_file.h"   // Platform::FileWrite

#include "system/string_internal.h"
// ORDER IS LOAD-BEARING, and it is a trap in the tree rather than a style
// choice. `External.h` does `#include "TypeInfo.h"` at its line 322 -- INSIDE
// its own `namespace RtObject { ... }`. A translation unit that includes
// External.h FIRST therefore gets `RtObject::RtObject::Factory`, and a plain
// `Factory` inside `namespace RtObject` does not name a type. Including
// TypeInfo.h first puts the declarations where their own file says they go and
// makes External.h's copy a `#pragma once` no-op; External.h's own uses spell
// it `RtObject::TypeNode`, which resolves either way.
#include "object/TypeInfo.h"
#include "object/External.h"
#include "io/FileStream.h"
#include "io/FileSystem.h"

namespace RtObject {


namespace {

// ---------------------------------------------------------------------------
// Doors. Every one is a NAME door except the Encoding delete, which has no
// export in either build (AEAA/IEAA would NOT have been a reason to reach for
// an address here — this DLL exports its private and protected members, and
// `?Create@FileStream@IO@@IEAA_...` below is a PROTECTED member reached by
// name; reference_aeaa_is_private_not_unexported.md).
// ---------------------------------------------------------------------------


// External's virtuals, by BYTE offset into the vtable, as External.h names them.
//   +0x10  GetTypeId() -> uint32
//   +0x28  Serialize(IO::Stream&) -> bool
//
// SLOT +0x28 RETURNS A BOOL, and this is the body that says so: 0x1800bb115 is
// `MOVZX EDI,AL` on the result, and that byte is what SaveText returns.
// external_add.cpp drives the same slot and DISCARDS the result, which is why
// its local typedef spells the return as void — a difference in the call site,
// not a disagreement about the slot.
// B3X W0: both by name -- `Object::Base::Type()` (slot 2, +0x10) and
// `Object::External::Serialize` (slot 5, +0x28).
uint32_t ExternalGetTypeId(::Object::External* self) {
    return self->Type();
}

bool ExternalSerialize(::Object::External* self, IO::Stream& stream) {
    return self->Serialize(stream);
}

// `if (fi->handle != -1) FileSystem::CloseFile(_singleton, fi);`
// Retail does this TWICE in the epilogue — once at the top of the inlined
// ~TextFileStream (0x1800bbb18) and once inside the inlined ~FileStream
// (0x1800bbba6). Both are transcribed rather than collapsed: CloseFile is
// idempotent on a closed FileInfo, but the second call is a different
// destructor's statement and a reader who saw only one would not know that.
void CloseIfOpen(RtIO::FileInfo* fi) {
    if (fi->handle == RtIO::FileInfo::kHandleNone) return;
    // ENDGAME B.3.9 SS1 (B39_RULES D7): the class static now, read fresh.
    ::IO::FileSystem::_singleton->CloseFile(*fi);                 // 0x1800accc0
}

}  // namespace

// ---------------------------------------------------------------------------
// EFFECTIVENESS COUNTERS.
//
// `calls` alone cannot tell a body that ran from one that did anything
// (reference_calls_zero_can_mean_always_took_the_uncovered_branch.md). The two
// that matter here are `createFailed` — the early-out that skips Serialize
// entirely — and `bomWritten`, which is expected to stay at ZERO and is
// reported so that a future caller which does reach it is visible immediately
// rather than silently.
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// NEGATIVE-CONTROL PERTURBATIONS, read by the body below and set only by
// external_savetext_probe.cpp.
//
// They live HERE, in the body, rather than in the probe, because a control that
// perturbs a copy of the code instead of the code under test catches in a
// screen it cannot reach (WAVE11_LANE_OBJEXT.md §10.3/§10.5). Each one is a
// single parameter consumed at exactly one site, each site increments
// `g_stAtt`, and each produces a COMPARABLE DIVERGENCE -- a different byte
// string in the output file -- never undefined behaviour.
//
//   1  swap the ctor's `type` and `name` arguments
//   2  pass objectVersion 1 instead of retail's 0
//   3  force _activeEncoding to 3, which is the ONLY way to reach the
//      byte-order-mark branch: no caller in either shipped binary sets it to
//      anything but 2, so without this control that transcription would never
//      execute and would be verified by inspection alone.
// ---------------------------------------------------------------------------
int      g_stCtl = 0;
uint64_t g_stAtt = 0;

struct SaveTextCounters {
    uint64_t calls;
    uint64_t typeFound;     // the type id was in Factory's tree
    uint64_t typeMissing;   // it was not -> the stream gets a null type name
    uint64_t createFailed;  // FileStream::Create said no -> no Serialize
    uint64_t serialized;    // Serialize was dispatched
    uint64_t serializeTrue; // ... and it returned true
    uint64_t bomWritten;    // the WriteFile branch, expected to stay 0
};
SaveTextCounters g_st{};

// ============================================================================
// Object::External::SaveText — the body.                 retail 0x1800bb9d0
// ============================================================================
extern "C" bool RtObject_External_SaveText(::Object::External* self, const System::Filename* path,
                                           const System::Filename* name) {
    ++g_st.calls;

    // ---- 1800bb9fd..1800bba3f -------------------------------------------
    // Look the object's own type id up in `Object::Factory`'s AVL tree and take
    // the registered class NAME out of the BaseTypeInfo it points at. Not found
    // means a NULL name, not a fault: retail loads r15 (zero) into RDX and
    // hands that to String::Set. That is the one lookup in this family that
    // does NOT deref a missing node — Factory::CreateObject and
    // Base::IsDerivedFrom both fault, deliberately (type_system.cpp's "NO ADDED
    // NULL GUARDS" note), and this one does not.
    const uint32_t typeId = ExternalGetTypeId(self);

    const ::Object::Factory* factory =
        ::Object::Factory::_singleton;   // class static (B.3.9 OBJ, D7)
    const ::Object::Factory::TypeSet::Node* node = factory->_types._root;
    while (node != nullptr) {
        const uint32_t key = node->element.typeId;
        if (typeId < key)      node = node->left;    // 1800bba2a, +0x38
        else if (key < typeId) node = node->right;   // 1800bba24, +0x40
        else                   break;                // 1800bba22
    }

    const char* typeName = nullptr;
    if (node != nullptr) {
        ++g_st.typeFound;
        typeName = node->element.record.info->name;  // 1800bba35/39: [n+0x10]->[+0x08]
    } else {
        ++g_st.typeMissing;
    }

    // ---- 1800bba50..1800bba85 -------------------------------------------
    // `IO::TextFileStream ts(typeStr, *name, 0);`
    //
    // ENDGAME B.3.9 IO: 0x80 bytes of stack and the REAL constructor (retail's
    // ??0TextFileStream 0x1800ba340). It writes every field this body reads;
    // TextFileStream+0x54 is one it never touches, as in retail.
    alignas(8) unsigned char tsMem[sizeof(IO::TextFileStream)];
    IO::TextFileStream* ts;
    {
        // ENDGAME B.3.1 step 2: 1800bba4a is a null-init followed by
        // `String::Set`, which is the `const char*` constructor (0x18000b630)
        // spelled out. The scope ends where retail's 1800bba64 release is, so
        // the destructor lands on the same instruction the release did.
        System::String typeStr(typeName);            // 1800bba4a

        const System::String* ctorType = &typeStr;
        const System::String* ctorName = &name->AsString();   // retail's Filename IS a String
        int                   ctorVer  = 0;
        if (g_stCtl == 1) { ++g_stAtt; const System::String* t = ctorType; ctorType = ctorName; ctorName = t; }
        if (g_stCtl == 2) { ++g_stAtt; ctorVer = 1; }
        ts = ::new (tsMem) IO::TextFileStream(*ctorType, *ctorName, ctorVer);
    }                                                // 1800bba64

    // ---- 1800bba85..1800bba9f -------------------------------------------
    bool result = false;                             // 1800bba85: XOR DIL,DIL
    int32_t bomLen = 2;                              // 1800bba88: MOV EBX,2
    ts->_requestedFormat = IO::Text::Format::Utf8;   // 1800bba8d: [+0x68] = 2
    ts->_activeEncoding  = IO::Text::Format::Utf8;   // 1800bba90: [+0x6c] = 2
    if (g_stCtl == 3) { ++g_stAtt; ts->_activeEncoding = IO::Text::Format::Utf8Bom; }  // see the control note

    if (ts->IO::FileStream::Create(*path)) {   // B.3.9 IO: the member

        // ---- 1800bbaa3..1800bbb05 ---------------------------------------
        // The byte-order mark. `_activeEncoding` ALONE selects it — not
        // `_requestedFormat`, which retail sets to the same value here and
        // never reads again:
        //     0 or 2 -> none      3 -> EF BB BF      4 -> FF FE      5 -> FE FF
        // Retail's chain is a `sub 2` followed by three `dec`s, so any value
        // outside {0,2,3,4,5} also falls through to "none".
        uint8_t bom[3];
        bom[0] = 0; bom[1] = 0; bom[2] = 0;
        bool haveBom = false;

        switch (ts->_activeEncoding) {
            case IO::Text::Format::Utf8Bom:   // 1800bbaca
                bom[0] = 0xEF;
                bom[1] = 0xBB;
                bom[2] = 0xBF;
                bomLen = 3;
                haveBom = true;
                break;
            case IO::Text::Format::Utf16Le:   // 1800bbac2
                bom[0] = 0xFF;
                bom[1] = 0xFE;
                haveBom = true;
                break;
            case IO::Text::Format::Utf16Be:   // 1800bbaba
                bom[0] = 0xFE;
                bom[1] = 0xFF;
                haveBom = true;
                break;
            default:                          // 0, 2, other
                break;
        }

        if (haveBom) {
            // 1800bbae8..1800bbb05. Straight to Win32 through the FileInfo's
            // handle, bypassing the stream's own Write virtual, and the byte
            // count is added to FileInfo::cursor by hand.
            ++g_st.bomWritten;
            uint32_t written = 0;
            Platform::FileWrite(ts->_file.winHandle,             // WriteFile
                                bom, static_cast<uint32_t>(bomLen), &written);
            ts->_file.cursor += static_cast<int32_t>(written);
        }

        // ---- 1800bbb08..1800bbb15 ---------------------------------------
        // The object writes itself into the stream, and ITS answer is
        // SaveText's answer.
        ++g_st.serialized;
        result = ExternalSerialize(self, *ts);
        if (result) ++g_st.serializeTrue;
    } else {
        ++g_st.createFailed;
    }

    // ---- 1800bbb18..1800bbc09 -------------------------------------------
    // The inlined ~TextFileStream -> ~FileStream -> ~Stream chain. Retail's
    // inlined copy opens with a close of the file (1800bbb18); the rest --
    // bare MemFree of +0x70, release +0x60 then +0x58, ~FileStream's close,
    // ~FileInfo, release +0x18, free the encoding -- is the class's destructor
    // chain, run by a non-virtual slot 0 with flags 0 at the same position.
    CloseIfOpen(&ts->_file);                                             // 1800bbb18
    ts->IO::TextFileStream::~TextFileStream();                             // 1800bbb2f..c04

    return result;                                                       // 1800bbc09
}

void DumpSaveTextCounters() {
    printf("[ExtSaveText] calls=%llu typeFound=%llu typeMissing=%llu "
                "createFailed=%llu serialized=%llu serializeTrue=%llu bomWritten=%llu\n",
                (unsigned long long)g_st.calls,
                (unsigned long long)g_st.typeFound,
                (unsigned long long)g_st.typeMissing,
                (unsigned long long)g_st.createFailed,
                (unsigned long long)g_st.serialized,
                (unsigned long long)g_st.serializeTrue,
                (unsigned long long)g_st.bomWritten);
}

}  // namespace RtObject
