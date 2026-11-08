// ============================================================================
// src/engine/collision2d/collisionmesh.cpp — Collision2D::CollisionMesh, whole class
// ============================================================================
//
// Vtable batch 8. ??_7CollisionMesh@Collision2D@@6B@ (retail), nine slots:
//
//   0  deleting dtor        0x1800656b0  deleting_dtor.cpp row (registers ~CollisionMesh here)
//   1  TypeName             0x180064f70  here
//   2  Type                 0x180064f50  here
//   3  GetFileVersion       0x180108cc0  here  (returns 3)
//   4  Compile              0x180108cb0  here  (returns 1)
//   5  Serialize            0x180108cd0  here
//   6  Reconstruct          0x1800651b0  here
//   7  Destruct             0x1800652c0  here
//   8  Construct            0x180065240  here
//
// plus ~CollisionMesh (0x180065400), registered here and appended to
// src/engine/object/deleting_dtor.cpp's kSpecs.
//
// Layout (sizeof 0x70; Reconstruct/Construct zero +0x20..+0x68 as ten qwords,
// factory step table src/engine/collision2d/rt_factories_collision2d.inc agrees):
//   +0x00  Object::External                (0x20)
//   +0x20  System::String              _name1
//   +0x28  System::String              _name2
//   +0x30  Array<Vec2>                 _ptsA    {data,size@0x38,cap@0x3c}  align4
//   +0x40  Array<Vec2>                 _ptsB    {data,size@0x48,cap@0x4c}  align8
//   +0x50  Array<Face>                 _faces   {data,size@0x58,cap@0x5c}  align8
//   +0x60  Array<ExternalPtr<Surface>> _surfaces{data,size@0x68,cap@0x6c}  align8
//
// Face (0x28, from FUN_1800665c0 0x1800665c0 disassembled at instruction level --
// Ghidra's decompile of this one drops several call arguments, a known project
// pitfall, so this shape came from the raw disassembly, not the pseudo-C):
//   +0x00  int32_t                surfaceIndex kScalarInt
//   +0x04  (pad)
//   +0x08  Array<Triple3>         _triples    {data,size@0x10,cap@0x14}  align2
//   +0x18  Array<Quad4>           _quads      {data,size@0x20,cap@0x24}  align2
// Serialize order per face: surfaceIndex, THEN _quads (count, resize, 4x kScalarUShort
// per element), THEN _triples (count, resize, kArrayUShort(n=3) per element).
// Grow: FUN_180066350 (Face array, element assign FUN_180065b70 deep-clones both
// nested arrays via FUN_180066de0/FUN_180066b30, shrink-tail destroy FUN_1800664c0
// frees both nested buffers). Nested array grows: FUN_180066e80 (_triples,
// stride 6) and FUN_180066bc0 (_quads, stride 8), both plain zero-fill-tail, no
// per-element dtor.
//
// _ptsA/_ptsB: Vec2 (2 floats), serialized as kArrayFloat(2, ptr) per element.
// Grows FUN_1800660f0 (_ptsA, align4) and FUN_1800666e0 (_ptsB, align8) are both
// plain zero-fill-tail, no per-element dtor -- align differs only in the MemAlloc
// call, transcribed for fidelity though it has no observable effect.
//
// _surfaces: ExternalPtr<Collision2D::Surface> (a bare, non-owning External*).
// Grow reuses FUN_1800666e0 (same stride 8/align 8 as _ptsB -- retail happens to
// share the body). Serialize is Object::ExternalPtr<T>::Serialize (Wave 10 lane
// EXTPTR, src/engine/object/external_ptr_serialize.cpp), confirmed by reading
// FUN_1800667f0's typeInfo global (0x1801fa820) against Surface::Type's own body
// (0x180064a50: `MOV EAX,[0x1801fa820]; RET`) -- same address, so T == Surface.
//
// ENDGAME B.3.9 ENG (B39_RULES D1/D2): the file-local POD became the real class
// `Collision2D::CollisionMesh` (src/engine/collision2d/CollisionMesh.h). Its
// constructor holds the factory rows' stores, its DeletingDtor the retired
// RT_DELDTOR row, and the slot bodies are its members: every live
// CollisionMesh wears the COMPILER's table. The eight slot doors, the ??1 door,
// `ConstructBody` and `CallSlot0` went with the minted
// `??_7CollisionMesh@Collision2D@@6B@` row and the FACTREST pair.
// ============================================================================
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>          // ENDGAME B.3.1 step 2: placement new into the object body

#include "collection/Collection.h"
#include "io/Serializer.h"
#include "io/Stream.h"       // IO::Stream::_direction (B.4 R-SYS)
#include "object/External.h"
#include "system/String.h"
#include "collision2d/CollisionMesh.h"
#include "collision2d/Surface.h"   // the _surfaces field's type record
#include "system/string_internal.h"

namespace {

using rt::io::Encoding;
using rt::io::ReaderWriter;
using rt::io::Serializer;
using rt::io::Stream;


using CollisionMesh = Collision2D::CollisionMesh;
using Vec2    = CollisionMesh::Vec2;
using Triple3 = CollisionMesh::Triple3;
using Quad4   = CollisionMesh::Quad4;
using Face    = CollisionMesh::Face;


// ---- the Collection::Array<T> instantiations, by ADDRESS -------------------
//
// CUTOVER CLOSURE BATCH D2 "helpers-high" (2026-09-12). This file used to carry
// a private `GenericArraySetSize` template plus hand-written Clone/Free/Face
// resize helpers, written before the Array family was parameterised. THEY WERE
// NOT BYTE-FAITHFUL, and the divergences were not cosmetic:
//
//   * `GenericArraySetSize` zero-filled the new tail for every element type.
//     Retail's `_ptsA`/`_ptsB` grows DO zero it, but through a single qword
//     store, and the `Face::quads`/`Face::triples` grows have their own arms;
//     a template cannot carry "which arm exists" per instantiation.
//   * `CloneTriples` / `CloneQuads` allocated `n` elements and set capacity to
//     `n` unconditionally. Retail's `operator=` reuses the existing buffer when
//     `_capacity >= _size`, and applies the `* 3` treble rule when it grows.
//   * The align immediates were guessed from the element type rather than read
//     out of each body's own `MOV EDX,<align>`: retail passes 4 for `_ptsA`,
//     8 for `_ptsB`/`_surfaces` and 2 for BOTH of Face's inner arrays.
//
// Those copies are gone. Each of the seven bodies is now ported ONCE, in
// src/engine/collection/array{,_destroy}.cpp with the rest of its family, and
// called here by name (B3R: they were reached by retail address before).
// The bodies are declared in src/engine/collection/Collection.h.

// 0x1800660f0  Array<Vec2>::SetSize   (_ptsA,   stride 8, align 4, zero tail)
void PtsASetSize(Collection::Array<Vec2>* a, int32_t n) {
    rtcoll::RtColl_ArraySetSize_1800660f0(a, n);
}
// 0x1800666e0  Array<Vec2>::SetSize   (_ptsB AND _surfaces: retail shares one
//              body for both, stride 8, align 8, zero tail)
template <typename T>
void PtsBSetSize(Collection::Array<T>* a, int32_t n) {
    rtcoll::RtColl_ArraySetSize_1800666e0(a, n);
}
// 0x180066bc0  Array<Quad4>::SetSize   (Face::quads,   stride 8, align 2)
void QuadArraySetSize(Collection::Array<Quad4>* a, int32_t n) {
    rtcoll::RtColl_ArraySetSize_180066bc0(a, n);
}
// 0x180066e80  Array<Triple3>::SetSize (Face::triples, stride 6, align 2)
void TripleArraySetSize(Collection::Array<Triple3>* a, int32_t n) {
    rtcoll::RtColl_ArraySetSize_180066e80(a, n);
}
// 0x180066350  Array<Face>::SetSize    (element copy ctor 0x180065b70, shrink /
//              realloc destroy 0x1800664c0)
void FaceArraySetSize(Collection::Array<Face>* a, int32_t n) {
    rtcoll::RtColl_ArraySetSize_180066350(a, n);
}

// ---- the two Serialize helpers --------------------------------------------
//
// Retail splits CollisionMesh::Serialize's face walk into two out-of-line
// functions. They are ported with retail's own signatures (the Serializer is
// RCX in both) so they can be registered at their addresses, and Serialize
// calls them exactly where retail CALLs them.
//
// 0x180066d50(Serializer* ser, int count, Quad4* base) -- FOUR kScalarUShort
// per element at +0x00, +0x02, +0x04, +0x06, stride 8. Read from the raw
// disassembly, not the pseudo-C: Ghidra drops the third argument on three of
// the four calls here (the known indirect-call pitfall).
void SerializeQuadsBody(IO::Serializer* ser, int32_t count, Quad4* base) {
    if (count <= 0) return;
    IO::ReaderWriter* rw = ser->_readerWriter;
    for (int32_t i = 0; i < count; ++i) {                  // SerializeUShort, +0xa8
        rw->SerializeUShort(base[i].a, nullptr);
        rw->SerializeUShort(base[i].b, nullptr);
        rw->SerializeUShort(base[i].c, nullptr);
        rw->SerializeUShort(base[i].d, nullptr);
    }
}

// 0x1800665c0(Serializer* ser, int count, Face* base) -- the per-face walk.
// Order, from the disassembly: surfaceIndex, THEN quads (count, grow when reading,
// then 0x180066d50), THEN triples (count, grow when reading, then one
// kArrayUShort(n=3) per element). The `if (reading)` test retail writes is
// `*(int*)(ser->stream + 8) == 0`, i.e. Stream::direction.
void SerializeFacesBody(IO::Serializer* ser, int32_t count, Face* base) {
    if (count <= 0) return;
    IO::ReaderWriter* rw = ser->_readerWriter;
    const bool reading = (ser->_stream->_direction == rt::io::kDirectionReading);
    for (int32_t i = 0; i < count; ++i) {
        Face* f = &base[i];
        rw->SerializeInt(f->surfaceIndex, nullptr);              // +0xa0

        int32_t quadCount = f->quads._size;
        rw->SerializeInt(quadCount, nullptr);
        if (reading) QuadArraySetSize(&f->quads, quadCount);
        SerializeQuadsBody(ser, quadCount, f->quads._data);

        int32_t tripleCount = f->triples._size;
        rw->SerializeInt(tripleCount, nullptr);
        if (reading) TripleArraySetSize(&f->triples, tripleCount);
        for (int32_t t = 0; t < tripleCount; ++t)          // SerializeUShortArray, +0x50
            rw->SerializeUShortArray(3, &f->triples._data[t].a, nullptr);
    }
}

}  // namespace

namespace Collision2D {

// TypeId 0x180064f60 / [2] 0x180064f50 / [1] 0x180064f70: DECLARE_TYPE's
// loads over the class's own record (collision2d_type_records.cpp; ENDGAME
// B.3.9 TI-REST).
// [3] 0x180108cc0: `MOV EAX,3 ; RET`.
int32_t CollisionMesh::GetFileVersion() const { return 3; }
// [4] 0x180108cb0: returns 1.
Object::External::CompileResult CollisionMesh::Compile(const System::Filename&) { return 1; }

// [8] 0x180065240 -- the inlined constructor, i.e. placement construction.
// (Retail's null test sat in the retired door; a virtual call always has a
// `this`.) `::new`: the class declares its own placement new.
Object::External* CollisionMesh::Construct() {
    return ::new (this) CollisionMesh();
}
// [6] 0x1800651b0: slot0(this, 0) dispatched VIRTUALLY, then the same stores.
Object::External* CollisionMesh::Reconstruct() {
    this->~CollisionMesh();
    return ::new (this) CollisionMesh();
}
// [7] 0x1800652c0: `MOV RAX,[RCX] ; XOR EDX,EDX ; JMP [RAX]`.
void CollisionMesh::Destruct() { this->~CollisionMesh(); }

// ?CreateNewObject@CollisionMesh@Collision2D@@SAPEAVBase@Object@@XZ  0x180065060
// The FACTREST row this replaces: MemAlloc of 0x70 bytes, align 8 (the locked
// `?MemAlloc@MemoryAllocator@System@@` Object::detail::InternalMemAlloc
// reaches); retail's null branch; then the inlined constructor.
Object::Base* CollisionMesh::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(CollisionMesh), 8);
    if (p == nullptr) return nullptr;
    return ::new (p) CollisionMesh();
}
// ?CreateInPlaceObject@CollisionMesh@Collision2D@@SAPEAVBase@Object@@PEAX@Z  0x180065130
Object::Base* CollisionMesh::CreateInPlaceObject(void* place) {
    if (place == nullptr) return nullptr;
    return ::new (place) CollisionMesh();
}

// ??1CollisionMesh@Collision2D@@UEAA@XZ (0x180065400) -- retail's body, moved
// here from the retired `RtCollision2D_CollisionMesh_Dtor` door.
CollisionMesh::~CollisionMesh() {
    // The four `~Array` calls retail makes, through the ported bodies rather
    // than inlined. The pairing is read from retail's own CALLs at 0x18006541a
    // / 0x180065424 / 0x18006542e / 0x180065438, NOT guessed from the member
    // order: _surfaces (+0x60) goes through 0x180065e80, _faces (+0x50)
    // through the element-destroy pair 0x180065d40, and BOTH _ptsB (+0x40) and
    // _ptsA (+0x30) through the single body 0x180065bf0. Each frees and ZEROES
    // its header, so the compiler-emitted member `~Array`s that follow are
    // no-ops (MemFree of null). The compiler then releases `_name2` and
    // `_name1` (`~String` 0x180006c80) and chains ~External: retail's order.
    rtcoll::RtColl_ArrayDtor_180065e80(&_surfaces);
    rtcoll::RtColl_ArrayDtorElem_180065d40(&_faces);   // the Face range destroy, then ~Array
    rtcoll::RtColl_ArrayDtor_180065bf0(&_ptsB);
    rtcoll::RtColl_ArrayDtor_180065bf0(&_ptsA);
}

// ?Serialize@CollisionMesh@Collision2D@@UEAA_NAEAVStream@IO@@@Z (0x180108cd0)
bool CollisionMesh::Serialize(IO::Stream& stream) {
    CollisionMesh* self = this;
    // B.4 R-SYS: retail's `??0Serializer@IO@@` (0x1800b9960): the stream, a
    // null ReaderWriter, then the encoding's CreateReader (+0x18) / CreateWriter
    // (+0x20) by direction, version 0; `~Serializer` (0x1800b99b0) deletes the
    // ReaderWriter at scope exit, where the hand-written `delete` stood.
    IO::Serializer ser(stream, 0);

    // ENDGAME B.3.1 step 2: `?Serialize@String@System@@QEAAXAEAVSerializer@IO@@@Z`
    // (0x1800c9930) is a member now, so the name door is gone. The registry row
    // at that name registers the shim, and the shim is one line over this member.
    self->_name1.Serialize(ser);
    self->_name2.Serialize(ser);

    // SerializeInt is +0xa0, SerializeFloatArray +0x28.
    IO::ReaderWriter* rwc = ser._readerWriter;
    const bool reading = (stream._direction == rt::io::kDirectionReading);

    // _ptsA
    int32_t nA = self->_ptsA._size;
    rwc->SerializeInt(nA, nullptr);
    if (reading) PtsASetSize(&self->_ptsA, nA);
    for (int32_t i = 0; i < nA; ++i)
        rwc->SerializeFloatArray(2, &self->_ptsA._data[i].x, nullptr);

    // _ptsB
    int32_t nB = self->_ptsB._size;
    rwc->SerializeInt(nB, nullptr);
    if (reading) PtsBSetSize(&self->_ptsB, nB);
    for (int32_t i = 0; i < nB; ++i)
        rwc->SerializeFloatArray(2, &self->_ptsB._data[i].x, nullptr);

    // _faces
    int32_t nF = self->_faces._size;
    rwc->SerializeInt(nF, nullptr);
    if (reading) FaceArraySetSize(&self->_faces, nF);
    // Retail CALLs 0x1800665c0 here rather than inlining the walk; so does
    // this, through the ported body, so the registered function is the one
    // that runs (batch D2, 2026-09-12).
    SerializeFacesBody(&ser, nF, self->_faces._data);

    // _surfaces (ExternalPtr<Collision2D::Surface>)
    int32_t nS = self->_surfaces._size;
    rwc->SerializeInt(nS, nullptr);
    if (reading) PtsBSetSize(&self->_surfaces, nS);
    // ExternalPtr<Surface>::Serialize per element; T's record is retail's
    // [0x1801fa820] (`?_typeInfo@Surface@Collision2D@@`).
    for (int32_t i = 0; i < nS; ++i) {
        Object::SerializeExternal<Collision2D::Surface>(ser, self->_surfaces._data[i]);
    }

    return true;   // ~Serializer deletes the ReaderWriter
}

}  // namespace Collision2D

// ---- CUTOVER CLOSURE BATCH D2: the two registered Serialize halves --------

// B3R (registry teardown, W1 RT-Core): every row this function registered had
// no reader by name or by address, so they went. It registers nothing and
// stays defined only because standalone/resolve_standalone.cpp's
// RuntimePortInit() still calls it (the coordinator removes the call).
