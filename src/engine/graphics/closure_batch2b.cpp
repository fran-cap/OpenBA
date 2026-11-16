// ============================================================================
// src/engine/graphics/closure_batch2b.cpp — cutover closure batch 2b
// ============================================================================
//
//   0x180015b60  ??0Geometry@Graphics@@QEAA@AEBV01@@Z    393 B  (ordinal 204)
//   0x18001be60  Graphics_DrawGroup_CopyCtor             145 B  (unexported)
//   0x180014850  Graphics_EdgeMesh_SerializeTriangles    177 B  (unexported)
//
// All three are pure transcriptions with no new struct layout of their own: the
// Geometry copy constructor writes into the layout src/engine/graphics/
// Geometry.h already pins, and the other two walk arrays whose stride is in
// their own index arithmetic.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <cstring>

#include "graphics/Geometry.h"
#include "io/Serializer.h"   // IO::ReaderWriter (B3X W0)
#include "graphics/GraphicsMesh.h"   // B.4: GraphicsMesh::TriangleList
#include "graphics/Mesh.h"           // B.4: RtGraphics_EdgeMesh_SerializeTriangleListArray
#include "graphics/Camera.h"          // rtgfxd::RtGraphics_Camera_CopyCtor
#include "collection/Collection.h"    // rtcoll:: the array instantiations

namespace {

// `Array<Geometry*>::operator=` — retail 0x18001b1a0, the CALL at 0x180015b8b.
// Batch 2 ported it in src/engine/collection/array_destroy.cpp; called by name.
//
// ENDGAME B.3.2 step 2 (callers) — NOT converted to `dst->_attached =
// src->_attached`, though `Geometry::_attached` is a real `Collection::Array<
// Geometry*>` whose `operator=` is the deep `Assign` (Collection.h).
void ArrayGeometryPtrAssign(void* dst, const void* src) {
    rtcoll::RtColl_ArrayAssign_18001b1a0(dst, src);
}

// `Graphics::Camera::Camera(const Camera&)` — retail 0x180017a70, read from
// 0x18001be7e / 0x18001be91: rtgfxd::RtGraphics_Camera_CopyCtor, called by name
// in the draw-group copy below.

// The per-element copy constructor the eh_vector iterator is handed at
// 0x18001bece: `Collection::Array<T>::Array(const Array&)`, retail 0x18001c620
// (`dst[0]=0; dst[1]=0; operator=(dst, src)` — 0x18001cd30).
//
// ENDGAME B.3.2 step 2 (callers) — NOT converted: `Collection::Array<T>`'s
// copy CONSTRUCTOR is `=default` (shallow), so there is no member spelling yet that
// reproduces this deep zero-then-assign. `T` here is `RendererDrawItem`
// (Camera.h, 0x38 bytes — array_destroy.cpp's "Array<blob0x38>" names the
// element WIDTH, not an anonymous type: this is
// `Collection::Array<RendererDrawItem>::Array(const Array&)`, the copy ctor
// of `RendererDrawGroup::items[i]`), a real, named C++ type — the missing
// piece is only the class's copy constructor, not the element type.
void InnerArrayCopyCtor(void* dst, const void* src) {
    rtcoll::RtColl_ArrayCopyCtor_18001c620(dst, src);
}

}  // namespace

// ============================================================================
// 0x180015b60  ??0Geometry@Graphics@@QEAA@AEBV01@@Z
//
// The copy constructor. Stores the BASE vtable (not the source's — retail's
// `LEA RAX,[0x180154e60]`, so copying a derived Geometry through this ctor
// produces a base Geometry, which is what slicing means and is faithful), then:
//
//   +0x00  vtable   = &??_7Geometry@Graphics@@6B@
//   +0x08  qword    copied           (_attachParent)
//   +0x10  dword    copied           (_attachNode)
//   +0x18  qword    ZEROED  }  the Array<Geometry*> header, zeroed and THEN
//   +0x20  qword    ZEROED  }  assigned through 0x18001b1a0 — the same
//                              zero-then-operator= order every nested Array
//                              copy in this DLL uses, because operator= starts
//                              by destroying what the destination holds.
//   +0x28 .. +0x78  21 dwords copied
//   +0x7c  QWORD    copied  (one unaligned 8-byte move, not two dwords —
//                            `MOV RAX,[RDX+0x7c]` at 0x180015bf1)
//   +0x84 .. +0xb8  14 dwords copied
//   +0xbc .. +0xbf  NEVER WRITTEN. sizeof(Geometry) is 0xC0 and the last store
//                   ends at 0xbc; the trailing dword is left holding whatever
//                   the caller's storage held. Recorded, not zeroed.
// ============================================================================

// ENDGAME B.3.9 GFX: this is the MEMBER now. The vptr store (retail's
// `LEA RAX,[0x180154e60]`) is the compiler's; the ??0 copy door
// (`RtGraphics_Geometry_CopyCtor`, which stamped the minted table), its
// registrations (name + 0x180015b60) and `GeometryVtable()` are retired.
// `_attached` is default-initialised first (Array()'s three zero stores), which
// the body then repeats before the deep assign, so the final bytes are retail's.
//
// B.4: on the named fields, in retail's store order; the same bytes are written
// (+0x14 and +0xbc stay unwritten).
namespace Graphics {
Geometry::Geometry(const Geometry& src) {
    _attachParent = src._attachParent;             // +0x08 qword
    _attachNode   = src._attachNode;               // +0x10 dword

    _attached._data     = nullptr;                 // +0x18 \ two zero qwords
    _attached._size     = 0;                       // +0x20 /
    _attached._capacity = 0;
    ArrayGeometryPtrAssign(&_attached, &src._attached);

    // +0x28 .. +0x7b, twenty-one dwords.
    _detailId          = src._detailId;
    _dirtyFlags        = src._dirtyFlags;
    _materialTypeFlags = src._materialTypeFlags;
    _localBounds       = src._localBounds;
    _worldBounds       = src._worldBounds;
    _prevWorldBounds   = src._prevWorldBounds;

    // +0x7c / +0x80: retail moves the two together as ONE unaligned qword.
    _opacity     = src._opacity;
    _opacityPrev = src._opacityPrev;

    // +0x84 .. +0xbb, fourteen dwords.
    _transform._current     = src._transform._current;
    _transform._previous = src._transform._previous;
}
}  // namespace Graphics

// ############################################################################
// ENDGAME B.3 step 5 (block E) — the real `Graphics::Geometry`'s C++ surface
// ############################################################################
//
// `src/engine/graphics/Geometry.h` declares the real class. This file carries
// its copy constructor (above), which RULE B5-10 requires be explicit. The
// pure slots (1, 2, 3) resolve to `__cxa_pure_virtual`, exactly as retail's
// resolve to `_purecall`; the destructor (slot 0) and the five non-pure slots
// (4..8) are DEFINED IN `scene.cpp`.
//
// ############################################################################

namespace Graphics {

// RULE B5-10 — `??0Geometry@Graphics@@QEAA@AEBV01@@Z`, 0x180015b60: declared
// explicitly in Geometry.h and NEVER `= default` (the deep Array assign above
// is the reason). Its body is the member defined above (ENDGAME B.3.9 GFX).

// Slots 4, 5, 6, 7 and 8 — `UpdateAttached`, `GetSubTransform`,
// `GetInstanceTransform`, `SetSharedInstanceData` and `Clean` — are the members
// in `src/engine/graphics/scene.cpp`.

// `??1Geometry@Graphics@@UEAA@XZ`, 0x1800a41c0: VIRTUAL, slot 0 (Geometry.h).
// `~Geometry()` is the member in scene.cpp.

}  // namespace Graphics

// ============================================================================
// 0x18001be60  the 0x1f0-byte "draw group" copy constructor
//
// Unexported and unnamed. Its owner is identified from the OTHER end: its
// element destructor, 0x18001c8c0, is array_destroy.cpp's J2b row (three inner
// `Collection::Array<T>` at +0x1c0/+0x1d0/+0x1e0, stride 0x1f0), and the array
// whose elements these are is grown by 0x1800638e0, whose only caller is
// `BeginDrawGroup` (0x180104fd0). So: a per-draw-group record holding TWO
// `Graphics::Camera` (0xD8 each, at +0x08 and +0xE0 — the second offset is what
// pins the Camera size) and three Arrays.
//
//   +0x000  qword  copied verbatim
//   +0x008  Camera copy-constructed  (CALL 0x180017a70)
//   +0x0e0  Camera copy-constructed  (CALL 0x180017a70)
//   +0x1b8  dword  copied
//   +0x1bc  dword  copied
//   +0x1c0  3 x 0x10 copy-constructed ASCENDING through MSVC's
//           `eh vector copy constructor iterator` (0x18007ed60) with element
//           ctor 0x18001c620. The iterator walks FORWARD (unlike the destructor
//           iterator, which walks backward), so the order here is +0x1c0,
//           +0x1d0, +0x1e0 — the reverse of the destroy order.
//   +0x1d0..+0x1ef are inside that range; nothing else in the 0x1f0 bytes is
//   written, so +0x0dc..? etc. are whatever the two Camera ctors leave.
//
// The CRT iterator is not reproduced as a call: three fixed elements with a
// known ctor is the whole of what it does here, and inlining it removes a CRT
// dependency at cutover (array_destroy.cpp's J2b row made the same call for the
// destroy side).
// ============================================================================
// B.4: the two element pointers (the element op's `void*` signature, declared in
// Scene.h and handed raw element storage by the array row) are each viewed
// once as the `Graphics::RendererDrawGroup` they are.
extern "C" void* RtGraphics_DrawGroup_CopyCtor(void* dst, const void* src) {
    Graphics::RendererDrawGroup* const d = static_cast<Graphics::RendererDrawGroup*>(dst);   // element op of a collection spec row takes void* (the type-erased collection core: one spec-row signature serves every element type)  // CAST-OK: elem_stride
    const Graphics::RendererDrawGroup* const s = static_cast<const Graphics::RendererDrawGroup*>(src);   // element op of a collection spec row takes void* (the type-erased collection core: one spec-row signature serves every element type)  // CAST-OK: elem_stride

    d->_renderBuffer = s->_renderBuffer;                               // +0x000
    rtgfxd::RtGraphics_Camera_CopyCtor(&d->_camera, &s->_camera);          // +0x008
    rtgfxd::RtGraphics_Camera_CopyCtor(&d->_cullCamera, &s->_cullCamera);  // +0x0e0
    d->_shader            = s->_shader;                                // +0x1b8
    d->_materialTypeFlags = s->_materialTypeFlags;                     // +0x1bc
    for (int k = 0; k < 3; ++k)                       // ASCENDING
        InnerArrayCopyCtor(&d->_items[k], &s->_items[k]);
    return dst;
}

// ============================================================================
// 0x180014850  SerializeTriangleListArray(SerializeCtx*, int count, void* data)
//
// Called by name from src/engine/graphics/edgemesh_vtable.cpp,
// graphicsmesh_vtable.cpp and batchc_gfx_leaves.cpp.
//
// Per element (stride 0x18) it makes SIX identical virtual calls through the
// encoding's slot +0xa0, one per dword field, re-loading `ctx->encoding`
// (`[ctx+8]`) before each. All six pass (encoding, &field, 0) — the third
// argument is `XOR R8D,R8D` at every one of the six sites, checked in the
// disassembly rather than taken from Ghidra's C, which prints it on the first
// call and drops it on the other five.
//
// `TEST EDX,EDX / JLE` — a count of zero does nothing at all, including no
// virtual call. `MOV ESI,EDX` then `DEC RSI`: the counter is the zero-extended
// 32-bit argument, so a negative count is impossible rather than huge.
// ============================================================================
// `ser._readerWriter` is [ctx+8] and +0xa0 is `SerializeInt` (Serializer.h).
// B.4: typed -- the six DWORDs are the TriangleList's six fields, stride 0x18.
void RtGraphics_EdgeMesh_SerializeTriangleListArray(IO::Serializer& ser, int count,
                                                    Graphics::GraphicsMesh::TriangleList* data) {
    if (count <= 0) return;                                  // TEST/JLE
    Graphics::GraphicsMesh::TriangleList* e = data;
    uint64_t n = static_cast<uint32_t>(count);               // MOV ESI,EDX
    do {
        // Retail re-loads `ser._readerWriter` and the slot before EVERY call; a
        // hoisted load would be one less indirection than retail performs and
        // would miss a reader/writer swapped mid-element. `ser` is a reference,
        // so each `ser._readerWriter` below is a fresh load.
        ser._readerWriter->SerializeInt(e->_baseVertex, nullptr);
        ser._readerWriter->SerializeInt(e->_vertexCount, nullptr);
        ser._readerWriter->SerializeInt(e->_startIndex, nullptr);
        ser._readerWriter->SerializeInt(e->_indexCount, nullptr);
        ser._readerWriter->SerializeInt(e->_materialIndex, nullptr);
        ser._readerWriter->SerializeInt(e->_node, nullptr);
        ++e;
    } while (--n);
}

