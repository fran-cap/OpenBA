// ============================================================================
// src/engine/graphics/edgemesh_vtable.cpp — WAVE 27 lane GFXMESH
//
// THE EIGHT NON-SLOT-0 BODIES OF `??_7EdgeMesh@Graphics@@6B@` (DLL 0x180154c70).
//
//
// This file holds the bodies for slots 1..8. SLOT 0 IS NOT HERE: it is the
// vector deleting destructor at 0x1800134b0, which the compiler emits for the
// virtual `~EdgeMesh()` (declared in Mesh.h, defined in batchc_gfx_leaves.cpp).
// The per-slot table is in docs/history/runtime_port/WAVE27_LANE_GFXMESH.md §3.
//
// EVERY BODY BELOW WAS READ FROM THE DISASSEMBLY of the shipped RETAIL
// Runtime-x64.dll, never from Ghidra's decompiled C, which drops dereferences,
// prints MOVSXD as a zero extension and drops whole arguments at a mistyped
// vtable call (reference_ghidra_c_output_vs_disassembly.md).
//
// EVERY ADDRESS WAS CONFIRMED AGAINST THE EXPORT DIRECTORY BY VA, with pefile
// against `C:\GOG Games\Banished\Runtime-x64.dll` — not by name pattern, and
// not against the profile build, whose vtable offsets are known to differ
// (reference_encoding_vtable_offsets_differ_between_builds.md):
//
//     0x180012c90  4382  ?TypeName@EdgeMesh@Graphics@@UEBAPEBDXZ
//     0x180012c70  4175  ?Type@EdgeMesh@Graphics@@UEBAIXZ
//     0x1800a23d0  2622  ?GetFileVersion@EdgeMesh@Graphics@@UEBAHXZ
//     0x1800a21c0  1741  ?Compile@EdgeMesh@Graphics@@UEAA?AW4CompileResult@...
//     0x1800a21d0  3685  ?Serialize@EdgeMesh@Graphics@@UEAA_NAEAVStream@IO@@@Z
//     0x180012e10  3477  ?Reconstruct@EdgeMesh@Graphics@@UEAAPEAVExternal@Object@@XZ
//     0x180012e40  2172  ?Destruct@EdgeMesh@Graphics@@UEAAXXZ
//     0x180012e30  1817  ?Construct@EdgeMesh@Graphics@@UEAAPEAVExternal@Object@@XZ
//
// All eight are `UEBA`/`UEAA` — PUBLIC virtual.
//
//
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>

#include "graphics/Mesh.h"   // the real Graphics::EdgeMesh (block G)
#include "io/Serializer.h"    // IO::Serializer, IO::ReaderWriter (B3X W0)
#include "io/Stream.h"        // B.4: IO::Stream (the slot's parameter)
#include "video/CompanionData.h"  // B.4: Video::{Index,Vertex}BufferData::Serialize
#include "object/External.h"  // Object::SerializeExternal<T>
#include "graphics/Material.h"   // B.4 P2 X: T::GetTypeInfo() for Object::SerializeExternal<T>
#include "graphics/GraphicsMesh.h"   // B.4 P2 X: T::GetTypeInfo() for Object::SerializeExternal<T>
#include "../collection/array_internal.h"

namespace {

// ---------------------------------------------------------------------------
// SLOTS 2 and 1 — the BaseTypeInfo node readers.
//
//   0x180012c70  8b 05 ..   MOV EAX,[0x1801f99b0]   RET   -> node + 0x00, DWORD
//   0x180012c90  48 8b 05.. MOV RAX,[0x1801f99b8]   RET   -> node + 0x08, QWORD
//
// Neither instruction has an RCX operand: `this` is not read by either body.
// That is not an inference from a decompile, it is what the encodings contain.
//
// 0x1801f99b0 is EdgeMesh's own type record (retail `?_typeInfo@EdgeMesh@
// Graphics@@`). ENDGAME B.3.9 TI-REST: it is the class's `DEFINE_TYPE` static
// (graphics_type_records.cpp) and both slots are DECLARE_TYPE's bodies; the
// RT_DATA_SLOT node readers that stood here are gone.
// ---------------------------------------------------------------------------

// (ENDGAME B.3.9 GFX: `EdgeMeshCtor`, the ??0EdgeMesh name door Construct /
// Reconstruct called, is gone with the door; both placement-new the class.)

// ---------------------------------------------------------------------------
// THE SERIALIZE CONTEXT IS AN `IO::Serializer`, read out of the shipped frame.
//
// ?Serialize@EdgeMesh@ builds a TWO-QWORD local at [RBP-0x18] and passes its
// ADDRESS (`LEA RDX,[RBP-0x18]`) to every helper:
//     [RBP-0x18] = RDX          the incoming IO::Stream&
//     [RBP-0x10] = 0, then the ReaderWriter the stream's Encoding factory
//                  hands back (`CMP [stream+8],0` -> +0x18 reader, else +0x20
//                  writer; version 0 in R8D)
// and the frame ends with `if (rw) rw->vt[0](rw, 1)`. B.4: that is
// `IO::Serializer`'s ctor (0x1800b9960) and dtor (0x1800b99b0) inlined, and
// the local is spelled as the class: `IO::Serializer ser(stream, 0)`. The
// stream's mode is `ser._stream->_direction` (IO::kReading == 0 selects the
// READ arm, the one that RESIZES the two arrays before streaming them).
//
// The two ReaderWriter slots this body calls, READ FROM RETAIL: `CALL
// [RAX+0xa0]` is `SerializeInt` and `CALL [RAX+0x28]` is
// `SerializeFloatArray` (count, float*, 0) -- by name on `IO::ReaderWriter`.
// ---------------------------------------------------------------------------

// The eight Video-buffer calls: `MOV RCX,[this+X] ; MOV RAX,[RCX] ;
// CALL [RAX+0x30]` -- the companion's slot 6, `Serialize`, on the buffer's
// `platformData` (header+0x08, what PlatformConstruct wrote). That is
// `?Serialize@IndexBuffer/VertexBuffer@Video@@` (0x1800f5a20) inlined.
// B.4 (R-GFX): `platformData` is typed in Buffers.h.
inline void SerializeBuffer(Video::IndexBuffer& buf, IO::Serializer& ser) {
    buf.platformData->Serialize(&ser);   // faithful: unguarded
}
inline void SerializeBuffer(Video::VertexBuffer& buf, IO::Serializer& ser) {
    buf.platformData->Serialize(&ser);   // faithful: unguarded
}

// ---------------------------------------------------------------------------
// THE FOUR UNEXPORTED HELPERS THIS BODY CALLS.
//
// SYMBOL:      none. All four are in-DLL internals with no export-directory
//              entry — checked by VA against the export table with pefile, not
//              inferred from an `AEAA` in a mangled name
//              (reference_aeaa_is_private_not_unexported.md), because these
//              have no mangled name at all.
// CALLED:      by name, see the B3R note below the list. The four are the
//              ones docs/history/runtime_port/WAVE27_LANE_GFXMESH.md §6 lists.
//
//   0x180014230  SerializeExternalPtrBase(ExternalPtr*, SerializeCtx*)
//                the +0x20 slot. Its type check reads the typeId at
//                0x1801fa708 — `?_typeInfo@GraphicsMesh@Graphics@@`.
//   0x180014520  SerializeExternalPtrMaterial(ExternalPtr*, SerializeCtx*)
//                the four material slots. Identical body except its typeId is
//                0x1801fa778 — `?_typeInfo@MaterialInstance@Graphics@@`. That
//                ONE difference is why retail emits two helpers and why this
//                file must not collapse them into one.
//   0x180014850  SerializeTriangleListArray(SerializeCtx*, int n, void* data)
//                six DWORDs per element at +0,+4,+8,+0xc,+0x10,+0x14, stride
//                0x18, through the encoding's +0xa0 slot. `TEST EDX,EDX ; JLE`
//                — a count of zero does nothing, including no virtual call.
//   0x1800136f0  the TriangleList Array<>::SetSize.
// ---------------------------------------------------------------------------
// The first three are ported and called by name: 0x180014230 and 0x180014520 are
// `Object::SerializeExternal<T>` / the ExternalPtr<T> member (External.h), and
// 0x180014850 is RtGraphics_EdgeMesh_SerializeTriangleListArray
// (graphics/closure_batch2b.cpp, declared in graphics/Mesh.h). The +0x20 slot is
// a GraphicsMesh*.
inline void ExtBase(Graphics::GraphicsMesh*& slot, IO::Serializer& ser) {
    Object::SerializeExternal<Graphics::GraphicsMesh>(ser, slot);
}
// 0x1800136f0, the TriangleList `Array<>::SetSize`, is called by its body
// name `rtcoll::RtColl_ArraySetSize_1800136f0` (Collection.h), on the typed
// `Array<GraphicsMesh::TriangleList>` members (B.4), as mesh.cpp's `Set` and
// graphicsmesh_vtable.cpp do.

}  // namespace

// ############################################################################
// ENDGAME B.3.5 step 5, block G — `Graphics::EdgeMesh`'s member bodies. Their
// own `namespace Graphics { }` block, after the anonymous namespace above has
// closed, because a member definition has to appear in a namespace ENCLOSING the
// class and `Graphics` / `RtGraphics` are siblings (B34_BRIEFS A.2). The
// anonymous namespace's helpers are reachable unqualified from here.
//
// `this` IS UNUSED IN FOUR OF THE SLOTS: slots 1, 2, 3 and 4 read neither `this`
// nor their arguments in retail (the encodings have no RCX and no RDX operand),
// and as members that is simply a body that mentions no field.
// ############################################################################

namespace Graphics {

// ---- slot 2  ?Type@EdgeMesh@Graphics@@UEBAIXZ  0x180012c70 -----------------
//   0x180012c70  8b 05 ..   MOV EAX,[0x1801f99b0]   RET   -> node + 0x00, DWORD
// ENDGAME B.3.9 TI-REST: Type / TypeName / TypeId are DECLARE_TYPE's bodies over the class's own
// record (graphics_type_records.cpp).

// ---- slot 1  ?TypeName@EdgeMesh@Graphics@@UEBAPEBDXZ  0x180012c90 ----------
//
// DECLARE_TYPE's body over the class's own record (graphics_type_records.cpp);
// `BaseTypeInfo::name` is content-only, never compared by pointer.

// ---- slot 3  ?GetFileVersion@EdgeMesh@Graphics@@UEBAHXZ  0x1800a23d0 -------
//
//   b8 02 00 00 00 c3     MOV EAX,2 ; RET
//
// A LITERAL, not a read: the on-disk serialised format version of a
// Graphics::EdgeMesh. Faithful means returning 2 and NOT deriving it from
// anything in our tree — a derived spelling would silently track OUR format if
// EdgeMesh::Serialize below is ever revised, and retail's does not. It is also
// NOT the same constant as MaterialInstance's 4 or Particle's 0x1000, which is
// why each of the three is written as its own literal in its own file.
int32_t EdgeMesh::GetFileVersion() const {
    return 2;
}

// ---- slot 4  ?Compile@EdgeMesh@Graphics@@UEAA...  0x1800a21c0 --------------
//
//   b8 01 00 00 00 c3     MOV EAX,1 ; RET
//
// Six bytes. It reads NEITHER `this` NOR the Filename& — the encoding has no
// RCX and no RDX operand — so `Object::External::CompileResult` = 1 is what
// every EdgeMesh compile returns, unconditionally. Whatever 1 means in that
// enum, this body does not decide it and neither does the port: the constant is
// transcribed, not interpreted.
//
// The Filename& parameter is kept in the signature even though the body never
// touches it, because the vtable slot is still CALLED with it in RDX and a
// narrower signature would be a different ABI on any future by-pointer use.
// *** THIS IS EdgeMesh'S OWN SLOT 4, NOT `Object::External`'s. *** Material,
// MaterialInstance and GraphicsMesh all INHERIT `?Compile@External@Object@@`
// (0x1800066e0) at this index; EdgeMesh overrides it with its own six bytes.
// Both bodies return 1, so getting that backwards is SILENT — which is why
// `tests/unit/graphics_material_abi_test.cpp` compares the slot-4 POINTER
// against a bare `Object::External` leaf's and requires this one to DIFFER.
//
// The `Filename&` parameter is kept because the vtable slot is still CALLED with
// it in RDX, and it is genuinely unread: the encoding has no RDX operand.
EdgeMesh::CompileResult EdgeMesh::Compile(const System::Filename& /*path*/) {
    return 1u;
}

// ---- slot 5  ?Serialize@EdgeMesh@Graphics@@UEAA_NAEAVStream@IO@@@Z ---------
//                                                            0x1800a21d0
//
// WHAT MAKES PORTING IT SAFE ANYWAY, and it is a narrow claim: this body
// performs NO ARITHMETIC OF ITS OWN. Every line below is a transcription of a
// call — same helper, same arguments, same order, same vtable byte offsets read
// from the same retail image. The two places it does branch are both a test of
// `stream->mode`, and both are reproduced from the shipped compare rather than
// reasoned about. There is nothing here for a misunderstanding of the format to
// hide in; the format lives entirely in the helpers it calls.
//
// THE SHIPPED ORDER, transcribed line for line from 0x1800a21d0:
//   1. ctx = { stream, nullptr }
//   2. factory = stream->+0x10;
//      ctx.encoding = factory->vt[ stream->mode ? +0x20 : +0x18 ](factory, &ctx, 0)
//   3. ExtPtrBase    (&this->+0x20, &ctx)
//   4. ExtPtrMaterial(&this->_material0 .. _material3, &ctx)   << in slot order
//   5. n = this->_triangleLists._size;
//      enc->vt[0xa0](enc, &n, 0);
//      if (stream->mode == 0) Array_SetSize(&this->_triangleLists, n);
//      SerializeTriangleListArray(&ctx, n, this->_triangleLists._data)
//   6. the same five lines for _edgeLists   (+0x160 size, +0x158 data)
//   7. eight Video-buffer virtual calls, `buf->vt[0x30](buf, &ctx)`, in
//      retail's order: +0x70, +0x50, +0xb0, +0x90, +0xf0, +0xd0, +0x130, +0x110
//      — INDEX buffer first in each pair, and the pointer is at header+0x08
//      (what PlatformConstruct wrote), which is why these are 8 past the
//      header offsets in the type header.
//   8. enc->vt[0x28](enc, 3, &this->_bounds.min, 0)  and the same for +0x174
//   9. if (enc) enc->vt[0](enc, 1)      <- destroy AND free
//  10. return true.   `MOV AL,1` — this slot has no failure path at all.
//
// STEP 5 IS WRITTEN IN THE SHIPPED ORDER AND THAT ORDER MATTERS: the count is
// serialised through a LOCAL (`MOV EAX,[RBX+0x150] ; MOV [RBP+0x10],EAX`), the
// resize is driven from THAT LOCAL, and the element walk is driven from it too.
// On the read arm the encoding OVERWRITES the local, so the resize and the walk
// both use the value that came off the stream, not the value the array had.
// Reading `_size` again after the resize would be the same number by accident
// on the write arm and a silent truncation on the read arm.
bool EdgeMesh::Serialize(IO::Stream& stream) {
    IO::Serializer ser(stream, 0);

    // The +0x20 qword no getter names, then the four materials IN FIELD ORDER
    // (mask, skinMask, edge, skinEdge — see Mesh.h's note; the FIELD order and
    // the SetMaterials ARGUMENT order are two groupings of the same four).
    ExtBase(_reserved20,      ser);
    _maskMaterial.Serialize(ser);
    _skinMaskMaterial.Serialize(ser);
    _edgeMaterial.Serialize(ser);
    _skinEdgeMaterial.Serialize(ser);

    IO::ReaderWriter* const rw = ser._readerWriter;

    {   // step 5 — the triangle lists, +0x148 data / +0x150 size
        int32_t n = _triangleLists._size;
        rw->SerializeInt(n, nullptr);
        if (ser._stream->_direction == IO::kReading)
            // retail 0x1800136f0 — Array<GraphicsMesh::TriangleList>::SetSize(n).
            rtcoll::RtColl_ArraySetSize_1800136f0(&_triangleLists, n);
        RtGraphics_EdgeMesh_SerializeTriangleListArray(ser, n, _triangleLists._data);
    }
    {   // step 6 — the edge lists, +0x158 data / +0x160 size
        int32_t n = _edgeLists._size;
        rw->SerializeInt(n, nullptr);
        if (ser._stream->_direction == IO::kReading)
            // Same SetSize: the edge-list array shares 0x1800136f0.
            rtcoll::RtColl_ArraySetSize_1800136f0(&_edgeLists, n);
        RtGraphics_EdgeMesh_SerializeTriangleListArray(ser, n, _edgeLists._data);
    }

    // step 7 — eight Video buffers, INDEX BEFORE VERTEX in each pair, retail's
    // order (+0x70, +0x50, +0xb0, +0x90, +0xf0, +0xd0, +0x130, +0x110: each
    // pair's `platformData`).
    SerializeBuffer(_indexBuffer,          ser);
    SerializeBuffer(_vertexBuffer,         ser);
    SerializeBuffer(_skinIndexBuffer,      ser);
    SerializeBuffer(_skinVertexBuffer,     ser);
    SerializeBuffer(_edgeIndexBuffer,      ser);
    SerializeBuffer(_edgeVertexBuffer,     ser);
    SerializeBuffer(_edgeSkinIndexBuffer,  ser);
    SerializeBuffer(_edgeSkinVertexBuffer, ser);

    // step 8 — the bounds box, as two runs of THREE floats. Retail passes the
    // literal 3 in EDX (`LEA EDX,[R9+0x3]` with R9 already zeroed), and the two
    // runs are +0x168 and +0x174, not one run of six: the Box is {min, max} and
    // the encoding is told so.
    rw->SerializeFloatArray(3, &_aabb.min.x, nullptr);
    rw->SerializeFloatArray(3, &_aabb.max.x, nullptr);

    // step 9 — `if (rw) rw->vt[0](rw, 1)` is `~Serializer` at scope exit.
    return true;   // MOV AL,1 — no failure path exists in the shipped body.
}

// ---- slot 7  ?Destruct@EdgeMesh@Graphics@@UEAAXXZ  0x180012e40 -------------
//
//   180012e40  MOV RAX,[RCX] ; XOR EDX,EDX ; JMP [RAX]
//
// SEVEN BYTES, then padding. Same shape as ?Destruct@MaterialInstance@
// (wave24_lane_gfxmat.cpp).
//
// No null test. Retail has none; destructing a null faults here exactly as it
// faults in the DLL (CLAUDE.md, "Don't fix a faithful crash with a null check").
void EdgeMesh::Destruct() {
    this->~EdgeMesh();
}

// ---- slot 8  ?Construct@EdgeMesh@Graphics@@UEAAPEAVExternal@Object@@XZ -----
//                                                            0x180012e30
//
//   180012e30  TEST RCX,RCX ; JNZ 0x1800a2830 ; XOR EAX,EAX ; RET
//
// TWELVE BYTES. Unlike MaterialInstance::Construct — which INLINES its whole
// constructor — EdgeMesh::Construct is a guarded TAIL JUMP to the real ctor at
// 0x1800a2830. The null test is retail's own and returns null.
//
// ENDGAME B.3.9 GFX: placement new of the class -- the ctor is the real
// `EdgeMesh()` (mesh.cpp); a virtual is never entered with null.
Object::External* EdgeMesh::Construct() {
    return ::new (this) EdgeMesh();
}

// ---- slot 6  ?Reconstruct@EdgeMesh@Graphics@@UEAAPEAVExternal@Object@@XZ ---
//                                                            0x180012e10
//
//   180012e10  PUSH RBX ; SUB RSP,0x20
//              MOV RAX,[RCX] ; XOR EDX,EDX ; MOV RBX,RCX ; CALL [RAX]
//              MOV RCX,RBX ; ADD RSP,0x20 ; POP RBX ; JMP 0x1800a2830
//
// Destroy in place through vtable slot 0 with flag 0 — NOT flag 1: the storage
// is reused, not freed — then re-run the constructor on the same storage and
// return what the ctor returns, which is `this`.
//
// NO NULL TEST, unlike retail's Construct one slot over: reconstructing a null
// faults here exactly as it faults in the DLL. MaterialInstance's Reconstruct
// (wave24_lane_gfxmat.cpp) has no null test either.
Object::External* EdgeMesh::Reconstruct() {
    this->~EdgeMesh();
    return ::new (this) EdgeMesh();
}

// ??1EdgeMesh@Graphics@@UEAA@XZ  0x1800a2780 -- ENDGAME B.3.9 GFX: defined in
// batchc_gfx_leaves.cpp, which holds the retired door's body.

// ?TypeId@EdgeMesh@Graphics@@SAIXZ 0x180012c80 -- DECLARE_TYPE's TypeId over
// the class's own record (graphics_type_records.cpp).

// ?CreateNewObject@EdgeMesh@Graphics@@SAPEAVBase@Object@@XZ 0x180012d80 --
// `??2EdgeMesh` (MemAlloc 0x180, align 8), the null branch, the ctor.
// Replaces the FACTREST row (B39_RULES D2).
Object::Base* EdgeMesh::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(EdgeMesh), 8);
    if (p == nullptr) return nullptr;
    return ::new (p) EdgeMesh();
}
// ?CreateInPlaceObject@EdgeMesh@Graphics@@SAPEAVBase@Object@@PEAX@Z 0x180012e00
Object::Base* EdgeMesh::CreateInPlaceObject(void* place) {
    if (place == nullptr) return nullptr;
    return ::new (place) EdgeMesh();
}

}  // namespace Graphics
