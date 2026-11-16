// ============================================================================
// src/engine/video/shaderprogram.cpp — Video::ShaderProgram, the whole class
// (original file: code\Runtime\Video\ShaderProgram_win.cpp)
// ============================================================================
//
// ??_7ShaderProgram@Video@@6B@ (retail 0x180157b60) — EIGHT slots, not nine.
// ShaderProgram is ABSTRACT: three of its slots are `_purecall`
// (0x18007ec28), and it has no Construct slot at all.
//
//   0  deleting dtor        0x18004e360  ShaderProgram.h (inline, B.3.9)
//   1  TypeName             0x18004e0c0  here
//   2  Type                 0x18004e0a0  here
//   3  _purecall            0x18007ec28  (GetFileVersion — subclass supplies it)
//   4  External::Compile    0x1800066e0  already ported
//   5  Serialize            0x1800f5110  here
//   6  _purecall            0x18007ec28  (Reconstruct)
//   7  _purecall            0x18007ec28  (Destruct)
//
// plus ??0ShaderProgram@Video@@QEAA@XZ (0x1800f5030), ~ShaderProgram
// (0x1800f50b0), ?PlatformSerialize@ShaderProgram@Video@@ (0x1800f52b0) and the
// nine non-virtual accessors the export table names.
//
// Layout: src/engine/video/ShaderProgram.h (sizeof 0x50).
//
// ENDGAME B.3.9 block VID: the ctor door (and the ConstructBody the two
// subclasses' doors reused), the dtor door and the Type / TypeName / Serialize
// slot doors are GONE with ??_7ShaderProgram@Video@@6B@. The real constructor is
// protected in the header; the exported non-virtual members are plain members.
//
// ============================================================================
// ENDGAME B.3.5 BLOCK B — THE BODIES MOVED INTO THE MEMBERS.
// ============================================================================
// `Video::ShaderProgram` is a real C++ class (src/engine/video/ShaderProgram.h)
// deriving from the real `Object::CompiledExternal`. Every body below that was a
// `RtVideo_ShaderProgram_*` function is now a member of that class, defined in
// the `namespace Video` block at the bottom of this file (B34_BRIEFS A.2: a
// member of `Video::X` cannot be defined inside a sibling namespace).
//
// TWO BINDINGS RETIRED, both runtime-internal and both inside this block's file
// set (BLOCK_BRIEF "runtime-internal callers"):
//   * `RT_EXPORT_ONCE(ExtDtorFn, "??1External@Object@@UEAA@XZ")(self)` — the
//     base-destructor call is now the destructor CHAIN's, which is what retail's
//     tail-JMP to 0x1800bc950 is. Spelling 3 of the nine.
//   * `RtVideo_ShaderProgram_DestroyArrays` — a cross-file `extern "C"` that
//     vertexprogram.cpp and pixelprogram.cpp called to run ~ShaderProgram's
//     array teardown by hand. The teardown is inside `~ShaderProgram()` now and
//     the subclass destructors reach it as their base-destructor step, exactly
//     as retail's inlined copies do. The symbol is GONE.
//
// WHAT THE DESTRUCTOR DOES AND DOES NOT WRITE BY HAND. Retail's ~ShaderProgram
// is: stamp ??_7ShaderProgram, destroy the three arrays newest-first, stamp
// ??_7CompiledExternal, tail-JMP ~External. Only the three array teardowns are
// written out below. Both stamps and the tail are the destructor chain's —
// `~CompiledExternal()` and `~External()` are real members
// (src/engine/object/external_class.cpp) and emit their own stores, which name
// the compiler's tables. The object is mid-teardown at that point and nothing
// dispatches on it.
//
// THE THREE ARRAY TEARDOWNS RUN EXACTLY ONCE EACH, and that needs saying because
// `_textures`/`_constants`/`_byteCode` are real `Collection::Array<T>` members
// with real destructors now. The bodies below call the ported retail array
// destructors (0x18004fb00 / 0x18004fbd0), which release each element's String /
// free each inner array AND THEN zero `_data`/`_size`/`_capacity`
// (`ArrayDtorImpl`, src/engine/collection/Collection.h). The compiler-emitted
// `~Array<T>` that runs after this body therefore sees an empty header and frees
// nothing. That is the same decomposition `Object::External::~External` uses for
// `_refs`, with each half running once.
//
// ---------------------------------------------------------------------------
// Serialize (0x1800f5110) — CONSTANTS FIRST, TEXTURES SECOND
// ---------------------------------------------------------------------------
// The order is not the field order and is not guessable: the body serializes
// the array at +0x30 (`_constants`, the one GetConstantCount reads) BEFORE the
// array at +0x20 (`_textures`). Getting it backwards reads every name into the
// wrong array and shows up only as a shader that binds the wrong sampler.
//
// Each array is: `int n = arr._size; rw->Int(&n); if (reading) SetSize(&arr,n)`
// (0x180055db0 — src/engine/collection/array.cpp `kSpec_ShaderParam`), then a
// per-element loop of
//     String::Serialize(&e->name, &ser)          0x1800c9930
//     rw->Int(&e->dest)
//     if (rw->GetVersion() >= 1) rw->Bool(&e->internal)
// The version test is INSIDE the loop in retail, re-asking the reader/writer
// once per element. Kept that way.
//
// ---------------------------------------------------------------------------
// PlatformSerialize (0x1800f52b0) — THREE ARMS, ONE PER GRAPHICS INTERFACE
// ---------------------------------------------------------------------------
// Byte code is stored once per backend and only the ACTIVE backend's copy is
// read. The body opens with `if (!stream->encoding->vtable[0](encoding)) return;`
// — the encoding policy object's "does this stream carry platform data at all"
// slot — and then runs the same block three times, for
// `Video::Device::_singleton->activeInterface` == 0, == 1 and == 2. The third
// arm is additionally gated on the OUTER reader/writer's GetVersion() >= 1.
//
// Each arm builds ITS OWN reader/writer over the same stream, so the three
// blocks are independently skippable: when the arm's interface is not the
// active one the body calls `rw->Skip()` (slot +0x10) instead of reading, which
// advances the stream past that backend's blob. That is the whole mechanism —
// three sub-streams, read one, skip two — and it is why the arms cannot be
// collapsed into a loop over a single reader/writer.
//
// Inside an arm:
//     int n = _byteCode._size; rw->Int(&n);
//     if (reading) SetSize(&_byteCode, n)        0x180053f00  kSpec_InnerArrayU8
//     for each inner Array<uint8>:
//         int m = inner->_size; rw->Int(&m);
//         if (reading) SetSize(inner, m)         0x180029630
//         rw->UCharArray(m, inner->_data)
// ============================================================================
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>

#include "system/String.h"
#include "io/Serializer.h"
#include "io/Stream.h"      // IO::Stream, IO::kReading
#include "video/ShaderProgram.h"
#include "video/Device.h"   // ENDGAME B.3.9 VID: Video::Device::_singleton
#include "collection/Collection.h"   // rtcoll:: the array instantiations

namespace {

using Collection::Array;
using ShaderParameter = Video::ShaderProgram::Parameter;

// `Video::Device::_singleton->activeInterface` (+0xF8).
int32_t ActiveInterface() {
    Video::Device* device = Video::Device::_singleton;
    // Retail derefs the singleton unguarded here (MOV RAX,[0x1801fe7a8] then
    // CMP dword [RAX+0xF8]); the null test exists only because RT_DATA_SLOT could
    // hand back a slot that the mint had not filled yet, which retail's own
    // static could not be.
    if (!device) return -1;
    return device->activeInterface;
}

// One `Array<Parameter>` of the two, in the order retail does them. Both are
// resized by 0x180055db0, `Array<Parameter>::SetSize`.
void SerializeParameters(IO::Serializer& ser, decltype(Video::ShaderProgram::_textures)* arr) {
    IO::ReaderWriter* rw = ser._readerWriter;
    int32_t n = arr->_size;
    rw->SerializeInt(n, nullptr);
    if (ser._stream->GetDirection() == IO::kReading)
        rtcoll::RtColl_ArraySetSize_180055db0(arr, n);

    ShaderParameter* e = arr->_data;
    for (int32_t i = 0; i < n; ++i, ++e) {
        e->name.Serialize(ser);
        rw->SerializeInt(e->dest, nullptr);
        if (rw->GetVersion() >= 1) rw->SerializeBool(e->internal, nullptr);
    }
}

// One arm of PlatformSerialize. `wanted` is the interface index this arm is
// written for; when it is not the active one the arm SKIPS instead of reading.
// Each arm is its own `IO::Serializer` over the outer one's stream, version 0.
void PlatformSerializeArm(Video::ShaderProgram* self, IO::Serializer& outer, int32_t wanted) {
    IO::Serializer sub(*outer._stream, 0);
    IO::ReaderWriter* rw = sub._readerWriter;

    if (ActiveInterface() == wanted) {
        int32_t n = self->_byteCode._size;
        rw->SerializeInt(n, nullptr);
        if (sub._stream->GetDirection() == IO::kReading)
            rtcoll::RtColl_ArraySetSize_180053f00(&self->_byteCode, n);   // Array<Array<uint8>>::SetSize

        Array<uint8_t>* blob = self->_byteCode._data;
        for (int32_t i = 0; i < n; ++i, ++blob) {
            int32_t m = blob->_size;
            rw->SerializeInt(m, nullptr);
            if (sub._stream->GetDirection() == IO::kReading)
                rtcoll::RtColl_ArraySetSize_180029630(blob, m);           // Array<uint8>::SetSize
            rw->SerializeUCharArray(m, blob->_data, nullptr);
        }
    } else {
        rw->Skip();
    }
}   // ~Serializer: the sub reader/writer's slot 0, flags 1

}  // namespace

// ---- the nine non-virtual accessors ---------------------------------------

// ?GetTextureName@ / ?GetConstantName@ are NOT here. Both return System::String
// BY VALUE through a hidden RDX return slot, and both were already ported —
// with a byte-level sweep against the shipped exports — in
// src/engine/video/shader_program_names.cpp, which is also where their MEMBER
// bodies now live. Writing a second copy here would have been the duplicate-body
// drift this tree keeps warning about, and the linker caught it. Read that file
// for the hidden-slot analysis.

// ============================================================================
// THE MEMBER BODIES — `Video::ShaderProgram::*`.
//
// At `namespace Video` scope and AFTER the anonymous namespace, per B34_BRIEFS
// A.2. The helpers above stay reachable by unqualified name because an
// anonymous namespace's members are members of the global namespace's
// declarative region.
// ============================================================================
namespace Video {

// slot 1 (0x18004e0c0), slot 2 (0x18004e0a0), TypeId (0x18004e0b0) and the two
// literals: DECLARE_TYPE / DEFINE_TYPE's over the class's own record
// (video_type_records.cpp; ENDGAME B.3.9 TI-REST).

// ??1ShaderProgram@Video@@UEAA@XZ (0x1800f50b0). Retail: stamp ??_7ShaderProgram,
// the three arrays newest-first, stamp ??_7CompiledExternal, tail-JMP ~External.
// Both stamps and the tail are the destructor chain's (the compiler's tables).
ShaderProgram::~ShaderProgram() {
    // The three Array teardowns, in retail's order (+0x40 first, then +0x30,
    // then +0x20). Each ported body ends in `ArrayDtorImpl`, which zeroes
    // the header, so the compiler-emitted `~Array` that follows frees nothing.
    rtcoll::RtColl_ArrayDtorElem_18004fbd0(&_byteCode);    // ~Array<Array<uint8>>
    rtcoll::RtColl_ArrayDtorElem_18004fb00(&_constants);   // ~Array<Parameter>
    rtcoll::RtColl_ArrayDtorElem_18004fb00(&_textures);    // ~Array<Parameter>
}

// ?CreateNewObject@ / ?CreateInPlaceObject@ (0x18004e0d0 / 0x18004e0e0): the
// abstract class's factory pair, `XOR EAX,EAX ; RET` both (B39_RULES D2).
Object::Base* ShaderProgram::CreateNewObject() { return nullptr; }
Object::Base* ShaderProgram::CreateInPlaceObject(void*) { return nullptr; }

// ?IsValid@ShaderProgram@Video@@QEBA_NXZ  0x18004e0f0 — `+0x48 > 0`.
bool ShaderProgram::IsValid() const { return _byteCode._size > 0; }

// ?GetByteCode@ShaderProgram@Video@@QEBAAEBV?$Array@E@Collection@@H@Z
// 0x18004e100: `(Array<uint8>*)_byteCode._data + index`. No bounds check in
// retail and none added here.
const Collection::Array<uint8_t>& ShaderProgram::GetByteCode(int32_t index) const {
    return _byteCode._data[index];
}

int32_t ShaderProgram::GetTextureCount() const  { return _textures._size; }    // 0x18004e410
int32_t ShaderProgram::GetConstantCount() const { return _constants._size; }   // 0x18004e4a0

int32_t ShaderProgram::GetTextureDest(int32_t index) const {                   // 0x18004e470
    return _textures._data[index].dest;
}
int32_t ShaderProgram::GetConstantDest(int32_t index) const {                  // 0x18004e500
    return _constants._data[index].dest;
}
bool ShaderProgram::IsTextureInternal(int32_t index) const {                   // 0x18004e480
    return _textures._data[index].internal != 0;
}

// ?PlatformSerialize@ShaderProgram@Video@@QEAAXAEAVSerializer@IO@@@Z 0x1800f52b0
// The opening test is the stream's encoding policy slot 0, `IsBinary`: a text
// stream carries no byte code.
void ShaderProgram::PlatformSerialize(IO::Serializer& ser) {
    if (!ser._stream->_encoding->IsBinary()) return;

    PlatformSerializeArm(this, ser, 0);
    PlatformSerializeArm(this, ser, 1);
    // The third backend arrived in file version 1; older files have only two
    // sub-streams and must not be advanced past a third.
    if (ser._readerWriter->GetVersion() < 1) return;
    PlatformSerializeArm(this, ser, 2);
}

// slot 5 — ?Serialize@ShaderProgram@Video@@UEAA_NAEAVStream@IO@@@Z 0x1800f5110
bool ShaderProgram::Serialize(IO::Stream& streamv) {
    IO::Serializer ser(streamv, 1);

    SerializeParameters(ser, &_constants);   // +0x30 FIRST
    SerializeParameters(ser, &_textures);    // +0x20 second
    // Retail CALLs 0x1800f52b0 here; the qualified spelling is that call.
    this->ShaderProgram::PlatformSerialize(ser);

    return true;   // ~Serializer: the ReaderWriter's slot 0, flags 1
}

}  // namespace Video
