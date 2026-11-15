// ============================================================================
// src/engine/video/closure_batch2b.cpp — cutover closure batch 2b
// ============================================================================
//
//   0x180054080  RtVideo_SerializeBlobArray               127 B  (unexported)
//   0x1800f8fb0  Volume_Frustum_SelectBoxSupportVertices  467 B  (unexported)
// ============================================================================
//
// ATTRIBUTION CORRECTED, ENDGAME B.3.5 block VC. 0x180054080 was called
// `Video_ShaderProgram_SerializeBlobArray` and described as the blob half of
// `Video::*Program::PlatformSerialize` with "its caller 0x180053ea0". IT HAS
// THREE CALLERS, read with Ghidra `getReferencesTo` on the retail DLL (15290
// functions) this pass:
//
//   0x180053d90  in FUN_180053d40   (unexported; only .rdata 0x180207840 names it)
//   0x180053eeb  in FUN_180053ea0   (unexported; only .rdata 0x180207858 names it)
//   0x1800f31d0  in ?PlatformSerialize@ImageBuffer@Video@@   <-- a DIFFERENT class
//
// plus one DATA reference at 0x180207870. Three adjacent `.rdata` qwords naming
// three adjacent bodies is a dispatch table, not a private helper. So this is the
// SHARED `Array<Array<uint8>>` blob serializer — one template instantiation two
// unrelated classes reach — and it is renamed accordingly.
//
// AND IT WAS DEFINED TWICE IN THIS TREE.
// `src/engine/video/imagebuffer_platform_serialize.cpp` carried its own
// `SerializeLevelBlobs`, with a banner saying 0x180054080 "is this function's own
// helper, nothing else in the tree resolves it" and "is deliberately NOT
// registered". Both halves were false: this file defines it too, and
// `ImageBuffer::PlatformSerialize` is only one of its three callers. Two bodies of
// one address that agreed by luck is the drift this tree keeps warning about, so
// that copy is gone and that file CALLS this one — which is what retail's
// `CALL 0x180054080` at 0x1800f31d0 is.

#include <cstdint>

#include "collection/Collection.h"  // ENDGAME B.3.2 step 2: real Array<uint8_t>
// ENDGAME B.3.5 block VC: the real `IO::Serializer` / `IO::ReaderWriter`, which
// replace this file's two hand-inlined ReaderWriter slot offsets.
#include "io/Serializer.h"
#include "video/ImageBuffer.h"   // RtVideo_SerializeBlobArray

// ============================================================================
// 0x180054080 — the SHARED blob half of an `Array<Array<uint8>>` serialize.
//
// Each of its three callers (see the file banner) is the same four-line pattern:
// serialize the outer count, resize the outer `Array<Array<uint8>>` if reading,
// then hand this function (ctx, count, data) to read or write every blob's bytes.
//
// Per element (stride 0x10 — `{uint8* data, int size, int capacity}`, i.e. a
// `Collection::Array<uint8>` header):
//
//   1. copy `element->_size` into a STACK dword and serialize THAT through the
//      ReaderWriter's +0xa0 slot with (rw, &tmp, 0). The stack copy matters:
//      on a read the slot writes the incoming size into `tmp`, NOT into the
//      array header, so the array's own `_size` is untouched until step 2.
//   2. if `*(int*)((char*)ctx->0 + 8) == 0` — the "reading" test, `CMP [RAX+8],0
//      / JNZ` at 0x1800540bf — call 0x180029630(element, tmp), the
//      `Array<uint8>::SetSize` that allocates the buffer.
//   3. serialize the bytes through slot +0x60 with (rw, tmp,
//      element->_data, 0).
//
// A NAME CORRECTION, ENDGAME B.3.5 block VC. `ctx + 8` WAS CALLED "the encoding"
// here and IT IS THE ReaderWriter. The two slot offsets say so on their own:
// `rt::io::rw_vtbl::kScalarInt == 0xA0` and `kArrayUChar == 0x60`
// (src/engine/io/Serializer.h), against `encoding_vtbl`'s five slots which stop
// at +0x20. So `ctx` is an `IO::Serializer` -- `{Stream*, ReaderWriter*}`, +0x00
// and +0x08 -- and `*(int*)(stream + 8)` is `Stream::direction`, which is exactly
// what `IO::Serializer::GetDirection()` (retail 0x180006210) returns. All three
// steps are now real calls on the real classes, so the two byte offsets and the
// two local function-pointer typedefs are gone with the mis-naming.
//
// (This also corrects b35-C's claim that imagebuffer_platform_serialize.cpp "was
// the last caller in the tree spelling either ReaderWriter offset". It was not:
// this file spelled both, and now nothing does.)
//
// NOT MADE A MEMBER OF ANY CLASS, on purpose and now with a second reason.
// 0x180054080 is UNEXPORTED -- no mangled name in the shipped export directory --
// so there is no evidence it is a member rather than a free template helper, and
// B35_RULES' shape rule is that a header declares the EXPORTED members. The
// three-caller read above settles it the other way round: two of its callers are
// unexported table entries and the third is `Video::ImageBuffer`'s, so it belongs
// to no one class. (`Video::ShaderProgram::PlatformSerialize` inlines the same
// logic as `PlatformSerializeArm` in src/engine/video/shaderprogram.cpp, which is
// where retail inlines it for that class.)
//
// `TEST EDX,EDX / JLE` at entry: a count of zero does nothing. `MOVSXD RSI,EDX`
// — the counter IS sign-extended here (unlike 0x180014850's zero-extension),
// but the JLE above has already excluded negatives.
//
// ============================================================================
// ENDGAME B.3.2 step 2 — `ArrayU8SetSize` (0x180029630) is GONE: each element
// `e` walked below IS a `Collection::Array<uint8_t>` header (the stride-0x10
// `{data,size,capacity}` triple the banner above names), and `kSpec_Bool`
// (0x180029630's row, array.cpp) is `SpecFor<uint8_t>()` field for field —
// trait-derived, so a direct `.SetSize()` on the reinterpreted element is that
// exact body.

// B.4 (R-VIDEO): typed (ImageBuffer.h); its callers in this tree are
// imagebuffer_platform_serialize.cpp, src/ext/create_on_demand/level_source.cpp
// and src/ext/texture_quality/texture_quality.cpp.
void RtVideo_SerializeBlobArray(IO::Serializer& serRef, int count,
                                Collection::Array<uint8_t>* e) {
    if (count <= 0) return;                                   // TEST EDX,EDX / JLE
    IO::Serializer* const ser = &serRef;
    int64_t n = count;                                        // MOVSXD RSI,EDX
    do {
        // The STACK copy is load-bearing: on a read the slot writes the incoming
        // size into `tmp` and not into the array header, so `_size` is untouched
        // until `SetSize` below. `Serialize(int&)` is retail 0x1800061d0, which
        // forwards to `rw_vtbl::kScalarInt` (+0xa0) — the offset this file used
        // to spell by hand.
        int32_t tmp = e->_size;                               // MOV EAX,[RBX+8]
        ser->Serialize(tmp, nullptr);

        // `CMP [RAX+8],0 / JNZ` at 0x1800540bf, i.e. Stream::direction == 0.
        if (ser->GetDirection() == IO::kReading)
            e->SetSize(tmp);                                  // retail 0x180029630

        // `Serialize(int, unsigned char*, const char*)` is retail 0x180054160,
        // which forwards to `rw_vtbl::kArrayUChar` (+0x60).
        ser->Serialize(tmp, e->_data, nullptr);

        ++e;                                                  // ADD RBX,0x10
    } while (--n);
}

// ============================================================================
// 0x1800f8fb0 — the support-vertex selector inside `Volume::Frustum::Contains`
// (its only caller, 0x1800f8e80).
//
// Given a plane normal and an axis-aligned box {min[3], max[3]}, it produces the
// box's two SUPPORT VERTICES along that normal: the corner furthest along -n
// (written to `outNeg`) and the corner furthest along +n (`outPos`). That pair
// is the standard "n-vertex / p-vertex" frustum-culling trick, and it is what
// lets Contains classify a box against a plane with two dot products.
//
// Per component i: `outNeg[i] = (n[i] < 0) ? box.max[i] : box.min[i]` and
// `outPos[i]` is the other one. Retail evaluates the three components in the
// order Z, Y, X (it loads n[0], n[2], n[1] and branches on z first) and then
// stores X, Y, Z; the order is preserved because these are four distinct
// buffers a caller may alias, and an aliasing caller can see it.
//
// COMPARISON IS `< 0.0f` (COMISS against a zeroed register), not a sign-bit
// test. -0.0f therefore takes the `min` branch, and a NaN component takes it
// too (COMISS sets CF on unordered, and the branch is JB). Reproduced with the
// same C comparison, which compiles to the same predicate.
// ============================================================================
extern "C" void RtVolume_Frustum_SelectBoxSupportVertices(const float* n,
                                                           const float* box,
                                                           float* outNeg,
                                                           float* outPos) {
    const float nx = n[0];
    const float nz = n[2];
    const float ny = n[1];

    const float a2 = (nz < 0.0f) ? box[5] : box[2];
    const float a1 = (ny < 0.0f) ? box[4] : box[1];
    const float a0 = (nx < 0.0f) ? box[3] : box[0];
    outNeg[0] = a0;
    outNeg[1] = a1;
    outNeg[2] = a2;

    const float b2 = (nz < 0.0f) ? box[2] : box[5];
    const float b1 = (ny < 0.0f) ? box[1] : box[4];
    const float b0 = (nx < 0.0f) ? box[0] : box[3];
    outPos[0] = b0;
    outPos[1] = b1;
    outPos[2] = b2;
}

