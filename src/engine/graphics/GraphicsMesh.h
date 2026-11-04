#pragma once
// ============================================================================
// src/engine/graphics/GraphicsMesh.h — ENDGAME B.3 step 5, block G
//   the REAL `Graphics::GraphicsMesh`, with its two nested value types
// ============================================================================
//
// *** B35_RULES section 2.2 LISTS THIS LAYOUT AS UNDOCUMENTED ("hdr" column
// `—`) AND B35_FILEMAP.md:380 SAYS SO TWICE: "no `struct GraphicsMesh` exists
// anywhere; `Mesh.h` only has `GraphicsMeshView` (a partial/opaque view) and
// `graphicsmesh_vtable.cpp` uses offset comments instead of a struct". SO THIS
// HEADER IS AUTHORED, NOT MOVED *** — the second and last place step 5 has to
// author a layout (B5-2.2 authored `Video::VertexLayout.h`, block A).
//
// Two descriptions already covered PARTS of these bytes and BOTH ARE KEPT and
// cross-asserted below, because both have live users outside block G's file set:
//
//   * `RtGraphics::GraphicsMeshView` (`Mesh.h`) — a read-only overlay of the
//     four offsets Wave 6 lane E needed (+0x50, +0x60, +0x70, +0x80). Its own
//     banner says "do not `sizeof` it".
//   * `src/engine/graphics/graphicsmesh_vtable.cpp`'s eleven `constexpr
//     uint32_t k*` offsets, read from the constructor's stores
//     (0x1800ff181..0x1800ff256) and cross-checked against the destructor and
//     `Serialize`. Every one of them is reproduced here as a named field.
//
// WHAT THIS PASS ADDED, all read from RETAIL DISASSEMBLY this pass (block G,
// 2026-09-19) rather than from either of those:
//
//   GraphicsMesh, one instruction each — so there is nothing to misread:
//     +0x20  `_name`            ?GetMeshName@  @0x180012730 (RETSLOT: this in
//                               RCX, the caller's String slot in RDX, refcount
//                               bumped at `data-4`, RDX returned in RAX)
//     +0x44  `_scale`           ?GetScale@             @0x180012720 MOVSS +0x44
//     +0x48  `_maxDepthDistance` ?GetMaxDepthDistance@ @0x180012680 MOVSS +0x48
//     +0x58  detail count       ?GetDetailLevelCount@  @0x1800125b0 MOV EAX,+0x58
//                               — i.e. the `_size` of an Array based at +0x50
//     +0x60  `_nodes`           ?GetNodes@             @0x180012690 LEA RAX,+0x60
//                               — LEA, so +0x60 IS the Array, not a pointer to one
//     +0x70  `_skinMaterials`   ?GetMaterialList@…XZ   @0x1800126b0 MOV RAX,[+0x70]
//                               — MOV, so it returns the array's _data, i.e.
//                               element 0; and ?GetMaterialList@…H@Z @0x1800126c0
//                               is the same with `+ index*0x10`
//     +0x78  skin count         ?GetSkinCount@         @0x1800126a0 MOV EAX,+0x78
//                               — the `_size` of THAT array. So "one material
//                               list per skin" is what ties the two names
//                               together, and it is why +0x78 is not a field of
//                               its own.
//     +0x80  `_aabb`            ?GetAABB@              @0x1800126d0 LEA RAX,+0x80
//
//   `GraphicsMesh::DetailLevel`, the 0xb0-byte element of the +0x50 array. The
//   stride is the `IMUL R8,R8,0xb0` that appears in all eight of its accessors,
//   and each field is the `ADD RAX,<n>` beside it:
//     +0x00  `_name`          ?GetSubMeshName@ @0x180012770 reads
//                             `*(*(this+0x50))` — level 0's first qword — as a
//                             refcounted String, RETSLOT
//     +0x08  `_image`         ?GetImage@ @0x1800126e0: `MOV RCX,[base+0x8]`,
//                             `MOV [RDX],RCX`, `MOV RAX,RDX` — an
//                             ExternalPtr<Video::ImageBuffer> through a RETSLOT
//     +0x10  `_distance`      ?GetDetailLevelDistance@ @0x180012660 MOVSS +0x10
//     +0x18  `_vertexBuffer`  ?GetVertexBuffer@     @0x1800125e0 ADD RAX,0x18
//     +0x38  `_indexBuffer`   ?GetIndexBuffer@      @0x1800125c0 ADD RAX,0x38
//     +0x58  `_skinVertexBuffer` ?GetSkinVertexBuffer@ @0x180012620 ADD RAX,0x58
//     +0x78  `_skinIndexBuffer`  ?GetSkinIndexBuffer@  @0x180012600 ADD RAX,0x78
//     +0x98  `_triangleLists` ?GetTriangleLists@…H@Z @0x180012640 ADD RAX,0x98,
//                             and the mangled return type names the element
//                             `UTriangleList@GraphicsMesh@Graphics@@`
//     +0xa8  `_maxInstances`  ?GetMaxInstances@ @0x180012700 MOV EAX,[base+0xa8]
//
//   *** THE FOUR BUFFERS AND THE FOUR SUB-OBJECTS graphicsmesh_vtable.cpp's
//   `TriangleListPass` VISITS ARE THE SAME FOUR THINGS, AND THAT IS THE
//   CROSS-CHECK. *** That function walks `+0x40, +0x20, +0x80, +0x60` of the
//   0xb0 record, "each a POINTER, each called through ITS OWN vtable slot
//   +0x30". Those are exactly `0x18+8`, `0x38+8`, `0x58+8`, `0x78+8` — the
//   `_deviceData` qword `Video::{Vertex,Index}Buffer::PlatformConstruct` writes
//   at `VideoBufferHeader+0x08` (`Mesh.h`'s banner pins the same idiom on
//   `EdgeMesh`'s eight buffers). Two independent readings of one layout agree,
//   which is what licenses naming the four.
//
//   `GraphicsMesh::TriangleList`, 0x18 bytes, SIX DWORDS. Pinned twice:
//   `?Serialize@TriangleList@GraphicsMesh@Graphics@@` @0x1800ff300 emits
//   `this+0x00, +0x04, +0x08, +0x0c, +0x10, +0x14`, each through the
//   ReaderWriter's DWORD scalar slot (+0xa0), and `edgemesh_vtable.cpp:225-228`
//   independently describes the array helper at 0x180014850 as "six DWORDs per
//   element at +0,+4,+8,+0xc,+0x10,+0x14, stride 0x18". WHAT THE SIX MEAN IS
//   NOT ESTABLISHED by any body this project has read, so they are named by
//   offset rather than guessed — the six are a triangle range and a material
//   index in some order, and inventing those names would put a guess where the
//   whole contract of this header is that there is none.
//
//   `GraphicsMesh::Node`, 0x48 bytes, pinned twice and with an UNINITIALISED
//   FIELD. `??0Node@GraphicsMesh@Graphics@@QEAA@XZ` @0x180012550 writes +0x00
//   (qword 0), then +0x0c/+0x14/+0x18 and +0x20/+0x28 as an IDENTITY
//   `Math::Transform` at +0x0c (quaternion 0,0,0,1 then translation 0,0,0), then
//   +0x30/+0x34/+0x3c as a SECOND identity Transform at +0x28 — and it NEVER
//   WRITES +0x08. `?Serialize@Node@GraphicsMesh@Graphics@@` @0x1800ff0d0 settles
//   both the gap and the two Transforms: String::Serialize(this+0x00), then one
//   DWORD scalar at this+0x08, then float arrays of 4 at +0x0c, 3 at +0x1c, 4 at
//   +0x28 and 3 at +0x38. So +0x08 is the parent index the ctor leaves holding
//   whatever the allocator returned, exactly as `EdgeMeshInstance::_followed`
//   does (`Mesh.h`'s banner), and the port MUST NOT zero it.
// ----------------------------------------------------------------------------
// PARENT: `Object::CompiledExternal`, and it is the ONE class of block G's four
// that is not a direct `Object::External`. Read, not inferred:
//   ?_ParentTypeName@GraphicsMesh@Graphics@@CAPEBDXZ @0x180012360
//       -> "Object::CompiledExternal"     (B35_SLOTS.md section 3)
// and corroborated by the constructor, which stamps FOUR tables in sequence —
// Base (0x180154788), External (0x1801547a0), CompiledExternal (0x180154a30),
// GraphicsMesh (0x180154cc0) — where `Material` and `EdgeMesh` stamp only three.
// Converting it on `Material`'s pattern would give it an `External` vtable stamp
// it must not have, which is the exact mistake B35_RULES section 2.1 warns about
// for `Video::VideoProgram`.
//
// RULE B5-15 — the two literals, COPIED from B35_SLOTS.md and recorded, not
// used. No `_typeInfo` row is retired and no `DEFINE_TYPE` is introduced
// (B35_RULES section 11); slots 1/2 keep reading
// `?_typeInfo@GraphicsMesh@Graphics@@0VBaseTypeInfo@Object@@A` (0x1801fa708).
//     ?_TypeName@GraphicsMesh@Graphics@@       -> "GraphicsMesh"
//     ?_ParentTypeName@GraphicsMesh@Graphics@@ -> "Object::CompiledExternal"
//
// ABSTRACT (B34_BRIEFS A.4 as amended): nothing in the tree holds a
// `GraphicsMesh` BY VALUE as a C++ member, and slot 0's body (0x180012bd0) is a
// row in `src/engine/object/deleting_dtor.cpp` that B35_RULES section 11
// forbids retiring in step 5, so slot 0 stays `= 0`.
//
// NO REAL CONSTRUCTOR (B4b-2): `??0GraphicsMesh@Graphics@@QEAA@XZ` @0x1800ff170
// OPEN-CODES the whole base chain as four vptr stamps plus three zero stores and
// does NOT call a base constructor, and the stamps name the MINTED tables, which
// is right for the objects that reach it. A member constructor there would stamp
// GCC's tables and would additionally run `??0CompiledExternal@Object@@`'s own
// stores, which are not these.
//
// PORTABILITY: no <windows.h>, per PLAN.md section 4.1 decision 1.
// ============================================================================

#include <cstdint>
#include <cstddef>

#include "../global_types.h"  // ::Vector3
#include "../object/External.h"            // Object::CompiledExternal, ExternalPtr<T>
#include "../object/Internal.h"            // B.3.9: InternalMemFree (the concrete slot 0)
#include "../collection/Collection.h"      // Collection::Array
#include "../math/Math.h"                  // ::Transform
#include "../volume/Volume.h"              // ::Box
#include "../video/Buffers.h"              // B.4: Video::VertexBuffer / IndexBuffer (DetailLevel)

// This header does not include `Mesh.h` (it once could not: `Mesh.h` and
// `AnimationState.h` each defined a POD `InterpolatedTransform`, both gone
// since B.4 Z MIRROR2). It takes its own value types, and the CROSS-ASSERTS against
// `RtGraphics::GraphicsMeshView` and `RtGraphics::MeshNode` live in
// `tests/unit/graphics_material_abi_test.cpp`, which can include both safely.
// NEITHER `Mesh.h` NOR `AnimationState.h` IS EDITED HERE: `AnimationState.h` is
// block H1's file and the collapse is one name for the closing pass, not a
// four-line change a concurrent block should make behind two other blocks' backs.

namespace Video { class ImageBuffer; class IndexBuffer; class VertexBuffer; }
namespace IO    { class Serializer; }

// B.4 Y RT: the hand-derived row of `Array<GraphicsMesh::Node>`
// (src/engine/collection/array_graphics_anim.cpp): SetSize 0x18005aa30's
// identity-Transform init, the copy ctor 0x1800127f0, and the element destroy
// 0x180014a40 that ~Array 0x180014090 runs (it releases each node's name).
// Declared here so `GraphicsMesh::NodeArray` can name it.
namespace rtcoll { namespace gfxanim {
extern const ArraySpec kSpec_MeshNode;
} }

namespace Graphics {

class MaterialInstance;   // src/engine/graphics/Material.h

// ============================================================================
// Graphics::GraphicsMesh — 0x98 bytes, NINE slots, `Object::CompiledExternal`.
//
// `??_7GraphicsMesh@Graphics@@6B@` @ 0x180154cc0, read as `.rdata` qwords
// (B35_SLOTS.md:113-122):
//
//   [0] +0x00  0x180012bd0  (unexported) the vector deleting dtor
//   [1] +0x08  0x180012390  ?TypeName@…
//   [2] +0x10  0x180012370  ?Type@…
//   [3] +0x18  0x1800ff3a0  ?GetFileVersion@…  (`MOV EAX,0x3415 ; RET`)
//   [4] +0x20  0x1800066e0  ?Compile@External@Object@@  INHERITED — NOT declared
//   [5] +0x28  0x1800ff3b0  ?Serialize@…
//   [6] +0x30  0x180012510  ?Reconstruct@…
//   [7] +0x38  0x180012540  ?Destruct@…
//   [8] +0x40  0x180012530  ?Construct@…  — INTRODUCED HERE (RULE B5-16)
//
// sizeof 0x98 from the `MOV EDX,0x98` in slot 0's vector deleting destructor,
// and independently from the constructor's last store ending at +0x97.
// ============================================================================
class GraphicsMesh : public Object::CompiledExternal {
public:
    // ========================================================================
    // GraphicsMesh::TriangleList — 0x18 bytes, six dwords.
    //
    // Exported as a `U` (struct) in every mangled name that mentions it, e.g.
    // `?GetTriangleLists@GraphicsMesh@Graphics@@QEBAAEBV?$Array@
    //  UTriangleList@GraphicsMesh@Graphics@@@Collection@@H@Z`, so it is a struct
    // and its element stride is what `Array<TriangleList>` walks.
    //
    // NAMED BY OFFSET ON PURPOSE — see the file banner. Nothing this project has
    // read gives the six dwords meanings, and `?Serialize@TriangleList@` treats
    // them as six interchangeable 4-byte scalars in ascending order.
    // ========================================================================
    struct TriangleList {
        int32_t _f00;   // +0x00
        int32_t _f04;   // +0x04
        int32_t _f08;   // +0x08
        int32_t _f0c;   // +0x0c
        int32_t _f10;   // +0x10
        int32_t _f14;   // +0x14

        // ??0TriangleList@GraphicsMesh@Graphics@@QEAA@XZ  0x1800125a0 —
        // `MOV RAX,RCX ; RET`, i.e. it writes NOTHING. A default-constructed
        // TriangleList holds whatever the allocator returned, and the port must
        // not zero it. That is why there is no member initialiser here and why
        // the type stays trivially default-constructible.
        //
        // ??4TriangleList@…QEAAAEAU012@AEBU012@@Z  0x180012940 — not ported
        // ?Serialize@TriangleList@…QEAAXAEAVSerializer@IO@@@Z 0x1800ff300 —
        //   not ported. NOT the same function as the unexported 0x1800a4670 that
        //   `graphicsmesh_vtable.cpp` registers: that one is the WHOLE
        //   DETAIL LEVEL's inline-geometry pass, and this one is one 0x18-byte
        //   element of the array inside it. The helper at 0x180014850 loops this
        //   body over `n` elements.
        void Serialize(IO::Serializer& serializer);      // 0x1800ff300  not ported
    };

    // ========================================================================
    // GraphicsMesh::Node — 0x48 bytes, TWO Transforms and an UNINITIALISED
    // parent index. See the file banner for the two readings behind every field.
    //
    // THE SECOND TRANSFORM IS THE BIND POSE, AND THE TREE ALREADY SAID SO.
    // `src/engine/graphics/animationstate.cpp:306` describes the element of
    // `Array<GraphicsMesh::Node>::Serialize` as
    // "{String name, int parent, Transform local, Transform bind}, stride 0x48"
    // — written by the batch that ported that element loop, independently of this
    // pass, and it agrees offset for offset with the two readings below. A FOURTH
    // reading, `?Serialize@Node@GraphicsMesh@
    // Graphics@@` @0x1800ff0d0, emits exactly the same
    // four runs. `MeshInstance::UpdateHierarchy` (0x1800ff910) reads ONLY +0x0c
    // (`LEA R8,[RCX+0xc]` into `Quaternion::operator*`), which is the separate
    // reason +0x0c is the one called `_local`.
    // ========================================================================
    struct Node {
        // B.4 Y RT (`behaviour:`): the real `System::String`. Nothing destroys a
        // Node through C++ but `Array<Node>`'s own row (`NodeArray` below:
        // ReleaseStringsCore over each name, retail 0x180014a40), and ??1Node
        // 0x1800127b0 (the String release) stays undefined, so the name is
        // released once.
        System::String _name;         // +0x00  strcmp'd directly by GetNodeIndexFromName
        // *** LEFT UNINITIALISED BY ??0Node@ (0x180012550). *** The ctor writes
        // the qword at +0x00 and then the qword at +0x0c, so +0x08..+0x0b keeps
        // whatever the allocator returned. `?Serialize@Node@` emits it as a
        // DWORD scalar and `MeshInstance::UpdateHierarchy` indexes `_nodes` with
        // it. NOT RETAIL (review F-049): the array's init row (MeshNodeInit,
        // array_graphics_anim.cpp) zeroes it, since serializing or copying an
        // indeterminate int is undefined behaviour; every reader sets it first.
        int32_t   _parent;            // +0x08
        ::Transform _local;           // +0x0c .. +0x27  ctor: identity
        ::Transform _bind;     // +0x28 .. +0x43  ctor: identity; see above
        uint32_t  _pad44;             // +0x44  past the ctor's last store

        // ??4Node@GraphicsMesh@Graphics@@QEAAAEAU012@AEBU012@@Z 0x180012880 — not ported
        // ?Serialize@Node@…QEAAXAEAVSerializer@IO@@@Z       0x1800ff0d0 — not ported
        // ??0Node@GraphicsMesh@Graphics@@QEAA@XZ            0x180012550 — not ported
        // ??0Node@GraphicsMesh@Graphics@@QEAA@AEBU012@@Z    0x1800127f0 — PORTED,
        //   registered in `src/engine/graphics/animationstate.cpp` (the one
        //   `GraphicsMesh` fragment in that file, B35_FILEMAP.md:97)
        // ??1Node@GraphicsMesh@Graphics@@QEAA@XZ            0x1800127b0 — not ported
        //
        // B.4: the default ctor stays UNDECLARED -- the unit test pins
        // `!is_default_constructible<Node>` -- so `Array<Node>` cannot take the
        // trait-derived row (its element init names `Node()`); it takes
        // retail's hand-derived row instead (`NodeArray` below).
        Node(const Node& src);
        Node& operator=(const Node& src);
        ~Node();
        void Serialize(IO::Serializer& serializer);
    };

    // `Collection::Array<GraphicsMesh::Node>` with retail's element lifecycle:
    // SetSize 0x18005aa30, operator= 0x180013960, ~Array 0x180014090 are the
    // `kSpec_MeshNode` row (B.4 Y RT).
    using NodeArray = Collection::Array<Node, Collection::DefaultAllocator,
                                        Collection::ExplicitSpec<&rtcoll::gfxanim::kSpec_MeshNode>>;

    // ========================================================================
    // GraphicsMesh::DetailLevel — the 0xb0-byte element of `_detailLevels`.
    //
    // NOT in B35_RULES section 2.4's list of nested value types, and it has no
    // exported constructor of its own: every accessor of it is a member of
    // `GraphicsMesh` that indexes `[level]` by the `IMUL …,0xb0` above. It is
    // named here because `_detailLevels` needs an element and because eight
    // exported getters name eight of its fields.
    //
    // `DetailLevel` IS A NAME THIS PROJECT CHOSE, not a retail one: the getters
    // say "DetailLevel" (`?GetDetailLevelCount@`, `?GetDetailLevelDistance@`)
    // and `Mesh.h`'s `GraphicsMeshView` already calls the array `_detailLevels`,
    // so the name follows the exports. Said plainly so nobody mistakes it for a
    // read symbol.
    //
    // B.4 (R-GFX, known_port_issues row 102): THIS IS THE 0xb0 ELEMENT
    // `src/engine/collection/array_graphics_mesh.cpp` calls "TriangleList".
    // Re-read from Runtime-x64.dll: the element ctor 0x1800fefc0 stores
    // `[+0x00] = 0` (the String), `[+0x08] = 0` (the ExternalPtr),
    // `dword [+0x10] = 0x461c4000` (10000.0f, `_distance`), the four buffer
    // headers at +0x18 / +0x38 / +0x58 / +0x78 (dword 0, byte 0, then
    // PlatformConstruct: Vertex, Index, Vertex, Index), `[+0x98] = [+0xa0] = 0`
    // (the Array<TriangleList> header) and `dword [+0xa8] = 1`
    // (`_maxInstances`), and returns `this`: field for field the layout below.
    // `Array<DetailLevel>`'s SetSize is 0x18005d840 (stride 0xb0), its copy
    // 0x180013c50, its element destroy 0x180014910 and ~Array 0x180013fb0. The
    // 0x18-byte `TriangleList` above is the element of `_triangleLists` at
    // +0x98, a different struct; the collection file's names are the misnomer.
    // ========================================================================
    struct DetailLevel {
        // B.4 Y RT (`behaviour:`): the real `System::String`. A DetailLevel is
        // built, copied and destroyed only by `Array<DetailLevel>`'s registered
        // row (array_graphics_mesh.cpp: 0x1800fefc0 / 0x180013c50 / 0x180014910,
        // the last releasing this name), never through a C++ ctor / dtor.
        System::String _name;            // +0x00  ?GetSubMeshName@ reads level 0's
        Object::ExternalPtr<Video::ImageBuffer> _image;  // +0x08  ?GetImage@
        float    _distance;              // +0x10  ?GetDetailLevelDistance@
        uint32_t _pad14;                 // +0x14  no body read touches it
        // The four buffers: the REAL 0x20-byte `Video::VertexBuffer` /
        // `Video::IndexBuffer` (Buffers.h), embedded. Their `platformData` at
        // +0x08 is what `TriangleListPass` and `PlatformSerialize` dispatch
        // through — see the file banner.
        //
        // B.4 (R-GFX): the class's own element ctor 0x1800fefc0 writes
        // `dword [buf] = 0; byte [buf+4] = 0` and then CALLs
        // `?PlatformConstruct@VertexBuffer@Video@@` 0x1800f57e0 on +0x18 / +0x58
        // and `?PlatformConstruct@IndexBuffer@Video@@` 0x1800f3680 on +0x38 /
        // +0x78, i.e. each sub-object IS that class (the `size` / `dynamic`
        // pair, then the companion). Both classes are trivially copyable and
        // have no C++ ctor or dtor (Buffers.h asserts it), so declaring them
        // here makes nothing run that did not run before. They replace a local
        // field-identical `Buffer` struct; `Mesh.h`'s `RtGraphics::
        // VideoBufferHeader` is the same 0x20 bytes (cross-asserted in the unit
        // test).
        Video::VertexBuffer _vertexBuffer;      // +0x18  ?GetVertexBuffer@     0x1800125e0
        Video::IndexBuffer  _indexBuffer;       // +0x38  ?GetIndexBuffer@      0x1800125c0
        Video::VertexBuffer _skinVertexBuffer;  // +0x58  ?GetSkinVertexBuffer@ 0x180012620
        Video::IndexBuffer  _skinIndexBuffer;   // +0x78  ?GetSkinIndexBuffer@  0x180012600
        // Array<TriangleList>. A RAW 16-byte header, for `AnimationGroup.h`'s
        // reason: element construction, copy and destruction run through the
        // REGISTERED `kSpec_*` rows in `src/engine/collection/array.cpp` at
        // retail's own addresses (`SetSize` 0x1800136f0, `~Array` through
        // `array_graphics_mesh.cpp`), and the typed form would make the compiler
        // emit a SECOND element lifecycle beside the ported one.
        // B.4: typed. TriangleList is trivially destructible, so the implicit
        // member dtor is the same Clear-then-J1 as the raw header's.
        Collection::Array<TriangleList> _triangleLists;   // +0x98  ?GetTriangleLists@
        int32_t  _maxInstances;          // +0xa8  ?GetMaxInstances@
        uint32_t _padAC;                 // +0xac  no body read touches it
    };

    // ---- ENDGAME B.3 step 9 (block GFX, B39_RULES D1/D2): CONCRETE ----------
    // A real public ctor (retail 0x1800ff170), its own vector slot 0, a
    // protected non-virtual ~GraphicsMesh holding the retired ??1 door's body,
    // the factory pair as the class's own statics; Construct / Reconstruct
    // placement-new the class. The minted `??_7GraphicsMesh@Graphics@@6B@`, its
    // RT_DELDTOR row 0x180012bd0, the FACTREST row and the ctor / dtor / slot
    // doors are retired (tools/rt_mint_retired/gfx.txt).
    //
    // ??0GraphicsMesh@Graphics@@QEAA@XZ 0x1800ff170 -- the REAL ctor (retail
    // also calls it from both factories and tail-jumps it from Construct).
    // Retail open-codes External() and the four stamps (the compiler's here),
    // then: +0x20 name null; +0x28 (1,1,1); +0x34 (-1,-1,-1); +0x40 = 1; +0x44
    // 1.0f; +0x48 10000.0f (0x461c4000); the three Array headers zero (their own
    // ctors); +0x80 (1e8 x3, 0x4cbebc20) and +0x8c (-1e8 x3). +0x4c is written by
    // nothing. Every float is exact, so the decimal spellings carry retail's bits.
    // B.4 Y RT: `_name`'s null store is its own ctor, first, as retail's
    // 0x1800ff1bf is the first store after the stamps.
    GraphicsMesh() : CompiledExternal(), _name() {
        _vector28.x = 1.0f;  _vector28.y = 1.0f;  _vector28.z = 1.0f;
        _vector34.x = -1.0f; _vector34.y = -1.0f; _vector34.z = -1.0f;
        _field40          = 1;
        _scale            = 1.0f;
        _maxDepthDistance = 10000.0f;
        _aabb.min.x = 1e8f;  _aabb.min.y = 1e8f;  _aabb.min.z = 1e8f;
        _aabb.max.x = -1e8f; _aabb.max.y = -1e8f; _aabb.max.z = -1e8f;
    }

    // ---- the nine slots, in measured retail order --------------------------
    // [0] 0x180012bd0 -- retail's VECTOR deleting destructor (`MOV EDX,0x98` =
    // the element size); here the virtual destructor (B3X W1: D1 + D0, so
    // retail slot N >= 1 is compiler slot N + 1).
    // `??1GraphicsMesh@Graphics@@UEAA@XZ` @ 0x1800ff270. VIRTUAL (B3X
    // W1). ENDGAME B.3.9 GFX: the retired door's body (graphicsmesh_vtable.cpp)
    // -- the three arrays in retail's REVERSE order through their own
    // out-of-line `~Array`s (each zeroes its header, so the members' implicit
    // ~Array is a no-op), then `_name`'s own ~String (B.4 Y RT); the
    // restamps and ~CompiledExternal are the compiler's chain.
    ~GraphicsMesh() override;

    // [1] 0x180012390 TypeName / [2] 0x180012370 Type, TypeId 0x180012380, `_TypeName` / `_ParentTypeName`
    // and the factory pair: retail's macro (ENDGAME B.3.9 TI-REST, B39_RULES
    // D6); the record is `DEFINE_TYPE`d in
    // src/engine/graphics/graphics_type_records.cpp.
    DECLARE_TYPE(GraphicsMesh)
public:
    int32_t     GetFileVersion() const override;           // [3] 0x1800ff3a0
    bool        Serialize(IO::Stream& stream) override;           // [5] 0x1800ff3b0
    Object::External* Reconstruct() override;               // [6] 0x180012510
    void        Destruct() override;                        // [7] 0x180012540
    virtual Object::External* Construct();                  // [8] 0x180012530 — B5-16
    // slot 4 is `Object::External::Compile`'s own body, inherited unchanged.

    // ---- PORTED non-virtual members ---------------------------------------
    // `?GetMaterialList@GraphicsMesh@Graphics@@QEBAAEBV?$Array@V?$ExternalPtr@
    //  VMaterialInstance@Graphics@@@Object@@@Collection@@H@Z` 0x1800126c0 —
    // body in `src/engine/graphics/mesh.cpp`. `*(this+0x70) + index*0x10`: the
    // +0x70 array holds one `Array<ExternalPtr<MaterialInstance>>` PER SKIN, so
    // this indexes by the 0x10-byte Array header, not by an element.
    const Collection::Array<Object::ExternalPtr<MaterialInstance> >&
        GetMaterialList(int32_t skin) const;               // 0x1800126c0  mesh.cpp

    // `?GetNodeIndexFromName@…QEBAHPEBD@Z` 0x1800fee90 — body in mesh.cpp.
    int32_t GetNodeIndexFromName(const char* name) const;  // 0x1800fee90  mesh.cpp

    // `?PlatformSerialize@…QEAAXAEAVSerializer@IO@@@Z` 0x1800a48e0 — body in
    // `src/engine/graphics/batchc_gfx_leaves.cpp`.
    //
    // *** RULE B5-11 LIVES ON THIS MEMBER. *** Its registration was once
    // stripped from the PERF build because the `Rt*Register()` call that made it
    // sat inside a `printf` argument, whose whole expression `log_strip.h`
    // compiles to `((void)0)`; boot then died on a VCALL-of-0 (defect 3, fixed
    // `1a4200c3`). The perf build is a gate item for every block G commit.
    void PlatformSerialize(IO::Serializer& serializer);    // 0x1800a48e0  batchc

    // ---- exported, NOT PORTED, no caller: declared, deliberately undefined --
    // Declared so no later block or caller batch has to edit this header. Each
    // is one to six instructions over offsets this header already pins, and the
    // instruction is quoted in the file banner where it named a field.
    int32_t GetDetailLevelCount() const;                   // 0x1800125b0
    const Video::IndexBuffer&  GetIndexBuffer(int32_t level) const;      // 0x1800125c0
    const Video::VertexBuffer& GetVertexBuffer(int32_t level) const;     // 0x1800125e0
    const Video::IndexBuffer&  GetSkinIndexBuffer(int32_t level) const;  // 0x180012600
    const Video::VertexBuffer& GetSkinVertexBuffer(int32_t level) const; // 0x180012620
    // B.4: retail's `const Array<TriangleList>&` (`?GetTriangleLists@…QEBAAEBV?$
    // Array@UTriangleList@…@H@Z`). Declared, not ported.
    const Collection::Array<TriangleList>& GetTriangleLists(int32_t level) const; // 0x180012640
    float   GetDetailLevelDistance(int32_t level) const;   // 0x180012660
    // 0x180012690 `LEA RAX,[RCX+0x60]; RET`, retail's `const Array<Node>&`.
    // B.4: PORTED, inline.
    const NodeArray& GetNodes() const { return _nodes; }
    float   GetMaxDepthDistance() const;                   // 0x180012680
    int32_t GetSkinCount() const;                          // 0x1800126a0
    const Collection::Array<Object::ExternalPtr<MaterialInstance> >&
        GetMaterialList() const;                           // 0x1800126b0
    const ::Box& GetAABB() const;                          // 0x1800126d0
    // RETSLOT in retail (0x1800126e0: value through RDX, RDX back in RAX); a
    // plain by-value return here, per the COORDINATOR AMENDMENT to B5-12 —
    // it is not a vtable slot, so both ends of a C++ call are this same GCC.
    Object::ExternalPtr<Video::ImageBuffer> GetImage(int32_t level) const;  // 0x1800126e0
    int32_t GetMaxInstances(int32_t level) const;          // 0x180012700
    float   GetScale() const;                              // 0x180012720
    // RETSLOT in retail (`System::String` by value, refcount bumped at data-4).
    System::String GetMeshName() const;                  // 0x180012730
    System::String GetSubMeshName() const;               // 0x180012770
    void    PlatformVersion();                             // 0x1800a4cd0

    // The two PRIVATE (`AEAA`) builders. This DLL exports its private members
    // (reference_aeaa_is_private_not_unexported.md), so both are in the export
    // directory; they are declared `private` below rather than here.

    GraphicsMesh(const GraphicsMesh& src);                 // 0x180012970  not ported
    GraphicsMesh& operator=(const GraphicsMesh& src);      // 0x180012a90  not ported

    // The three operators stay declared and undefined (placement is `::new`);
    // the factory pair is the class's own (B39_RULES D2, graphicsmesh_vtable.cpp).
    static void* operator new(std::size_t bytes);           // 0x1800123a0
    static void* operator new(std::size_t bytes, void* place);// 0x180012410
    static void  operator delete(void* p) { ::Object::detail::InternalMemFree(p); }                  // 0x180012420
    // CreateNewObject 0x180012480 / CreateInPlaceObject 0x180012500: declared
    // by DECLARE_TYPE above.

private:
    // `?Set@GraphicsMesh@Graphics@@AEAA_NHPEBV?$ExternalPtr@VMaterialInstance@
    //  Graphics@@@Object@@HPEBUNode@12@AEBVBox@Volume@@@Z`  0x1800feb80
    bool Set(int32_t skinCount,
             const Object::ExternalPtr<MaterialInstance>* materials,
             int32_t nodeCount, const Node* nodes, const ::Box& aabb);

    // `?AddDetailLevel@GraphicsMesh@Graphics@@AEAA_NHHPEBUTriangleList@12@H
    //  PEAGHPEAXHH1H2HH@Z`  0x1800fecd0. FOURTEEN arguments; the `1` and `2`
    // backreferences in the mangled name are repeats of `PEAG`
    // (`unsigned short*`) and `PEAX` (`void*`), which is how the two index /
    // vertex blob pairs are spelled.
    bool AddDetailLevel(int32_t a, int32_t b, const TriangleList* lists, int32_t c,
                        unsigned short* indices, int32_t indexCount,
                        void* vertices, int32_t vertexCount, int32_t d,
                        unsigned short* skinIndices, int32_t skinIndexCount,
                        void* skinVertices, int32_t skinVertexCount, int32_t e);

public:
    // ---- the 0x78 bytes GraphicsMesh adds after Object::CompiledExternal ----
    // PUBLIC for the reason `Object::External`'s are: unconverted bodies reach
    // them at these offsets through a cast.
    //
    // B.4 Y RT (`behaviour:`): `_name` is the real `System::String`. Retail's
    // ctor 0x1800ff170 stores it null first (0x1800ff1bf) and its dtor
    // 0x1800ff270 releases it (0x1800ff2ae..0x1800ff2d0, the `str-8` arm) after
    // the three ~Arrays and before ~CompiledExternal: the member's own ~String
    // (the same `Release`) after the dtor body, the arrays' implicit ~Arrays
    // being no-ops on the headers the body's explicit calls zeroed.
    System::String _name;             // +0x20  ?GetMeshName@ 0x180012730
    ::Vector3 _vector28;               // +0x28  ctor ( 1, 1, 1) — role not established
    ::Vector3 _vector34;               // +0x34  ctor (-1,-1,-1) — role not established
    int32_t  _field40;                // +0x40  ctor 1; Serialize emits it as a dword
    float    _scale;                  // +0x44  ctor 1.0f;      ?GetScale@
    float    _maxDepthDistance;       // +0x48  ctor 10000.0f;  ?GetMaxDepthDistance@
    uint32_t _pad4c;                  // +0x4c  no body read touches it. FOUR bytes,
                                      //        not eight: a `uint64_t` here would
                                      //        align to +0x50 and silently push the
                                      //        three arrays out by 8 (it did, and the
                                      //        size assert caught it).
    // The three arrays, all RAW 16-byte headers for `AnimationGroup.h`'s reason
    // (see `DetailLevel::_triangleLists`). The true element types and the
    // registered `SetSize` addresses are named beside each.
    //
    // B.4 (R-GFX, H5 `behaviour:` commit): `_detailLevels` and `_skinMaterials`
    // carry their element types. What that makes run, against retail:
    //  * ctor 0x1800ff170 zeroes the three headers with six qword stores
    //    (0x1800ff202..0x1800ff216); `Collection::Array()`'s three zero stores
    //    are the same bytes, for the typed and the raw member alike.
    //  * dtor 0x1800ff270 CALLs the three out-of-line ~Arrays itself, in reverse
    //    order: 0x180014160 (+0x70), 0x180014090 (+0x60), 0x180013fb0 (+0x50),
    //    then releases `_name`, then restamps and JMPs ~CompiledExternal. The
    //    tree's ~GraphicsMesh makes the same three calls (the registered
    //    element rows), and each frees AND zeroes its header (ArrayDtorImpl), so
    //    the implicit member ~Array the compiler adds after the body sees
    //    `_size == 0` and destroys no element: MemFree(nullptr), as the raw
    //    member's did. Nothing else ever constructs, copies or destroys a
    //    GraphicsMesh's arrays (the copy ctor / operator= are declared and
    //    undefined).
    //  * SIZING STAYS ON THE REGISTERED ROWS: `Rt_SetSize_18005d840` /
    //    `Rt_SetSize_18005cda0` (array_graphics_mesh.cpp, retail's element ctor
    //    0x1800fefc0 and copy 0x180013c50). Never call the members' own
    //    `SetSize` / `operator=`: those take the trait-derived row, which is not
    //    retail's element lifecycle.
    // B.4 Y RT: `_nodes` is `NodeArray`, the Node array under retail's own row
    // (`Node` has no default ctor, so not the trait-derived one); the body's
    // explicit 0x180014090 zeroes it first, so its implicit ~Array is a no-op.
    Collection::Array<DetailLevel> _detailLevels;   // +0x50  stride 0xb0,
                                                    //        SetSize 0x18005d840, ~Array 0x180013fb0
    NodeArray _nodes;                           // +0x60  Array<Node>, stride 0x48,
                                                //        SetSize 0x18005aa30, ~Array 0x180014090
    Collection::Array<Collection::Array<Object::ExternalPtr<MaterialInstance> > >
        _skinMaterials;                         // +0x70  stride 0x10,
                                                //        SetSize 0x18005cda0, ~Array 0x180014160
    ::Box _aabb;                      // +0x80 .. +0x97  ctor (+1e8 x3, -1e8 x3); ?GetAABB@
};

static_assert(sizeof(GraphicsMesh) == 0x98,
              "Graphics::GraphicsMesh is 0x98 bytes (the MOV EDX,0x98 in its own "
              "slot-0 vector deleting destructor 0x180012bd0)");
static_assert(offsetof(GraphicsMesh, _pad4c) == 0x4C, "GraphicsMesh::_pad4c");
static_assert(alignof(GraphicsMesh) == 8, "Graphics::GraphicsMesh aligns to 8 (the vptr)");

static_assert(offsetof(GraphicsMesh, _name)            == 0x20, "GraphicsMesh::_name @ +0x20 (GetMeshName 0x180012730)");
static_assert(offsetof(GraphicsMesh, _vector28)        == 0x28, "GraphicsMesh::_vector28 @ +0x28 (ctor 1,1,1)");
static_assert(offsetof(GraphicsMesh, _vector34)        == 0x34, "GraphicsMesh::_vector34 @ +0x34 (ctor -1,-1,-1)");
static_assert(offsetof(GraphicsMesh, _field40)         == 0x40, "GraphicsMesh::_field40 @ +0x40 (ctor 1)");
static_assert(offsetof(GraphicsMesh, _scale)           == 0x44, "GraphicsMesh::_scale @ +0x44 (GetScale 0x180012720)");
static_assert(offsetof(GraphicsMesh, _maxDepthDistance)== 0x48, "GraphicsMesh::_maxDepthDistance @ +0x48 (0x180012680)");
static_assert(offsetof(GraphicsMesh, _detailLevels)    == 0x50, "GraphicsMesh::_detailLevels @ +0x50 (GetDetailLevelCount reads +0x58)");
static_assert(offsetof(GraphicsMesh, _nodes)           == 0x60, "GraphicsMesh::_nodes @ +0x60 (GetNodes 0x180012690 LEAs it)");
static_assert(offsetof(GraphicsMesh, _skinMaterials)   == 0x70, "GraphicsMesh::_skinMaterials @ +0x70 (GetSkinCount reads +0x78)");
static_assert(offsetof(GraphicsMesh, _aabb)            == 0x80, "GraphicsMesh::_aabb @ +0x80 (GetAABB 0x1800126d0 LEAs it)");

// The base subobject must start at zero and the derived fields at exactly
// sizeof(Object::CompiledExternal) — no tail-padding reuse, no vptr duplication.
static_assert(offsetof(GraphicsMesh, _name) == sizeof(Object::CompiledExternal),
              "GraphicsMesh's own fields begin at the end of the CompiledExternal base");

// THE CROSS-ASSERTS AGAINST `RtGraphics::GraphicsMeshView` AND
// `RtGraphics::MeshNode` (both in `Mesh.h`) ARE IN
// `tests/unit/graphics_material_abi_test.cpp`, not here — this header cannot
// include `Mesh.h` (see the include note at the top). They matter: the view is
// lane E's partial overlay of these same bytes and it has live users outside
// block G's file set.
//
// THE TWO DESCRIPTIONS DISAGREE ON ONE INTERPRETATION AND AGREE ON EVERY BYTE:
// the view calls +0x70 `Collection::Array<ExternalPtr>* _materialLists`, i.e. a
// POINTER TO an array, where this class calls it the `_data` field of an Array
// header based at +0x70. `?GetMaterialList@…XZ` (0x1800126b0) is
// `MOV RAX,[RCX+0x70]` and `?GetSkinCount@` (0x1800126a0) is
// `MOV EAX,[RCX+0x78]` — a `_data`/`_size` pair — so the header reading is the
// one with two instructions behind it, and the view's is the same qword named
// for what lane E did with it. The OFFSET is identical either way.

// ---- the nested value types ------------------------------------------------
static_assert(sizeof(GraphicsMesh::TriangleList) == 0x18,
              "GraphicsMesh::TriangleList is 0x18 bytes (six dwords; "
              "?Serialize@TriangleList@ 0x1800ff300 emits +0x00..+0x14, and the "
              "array helper 0x180014850 strides 0x18)");
static_assert(offsetof(GraphicsMesh::TriangleList, _f00) == 0x0, "TriangleList::_f00");
static_assert(offsetof(GraphicsMesh::TriangleList, _f04) == 0x4, "TriangleList::_f04");
static_assert(offsetof(GraphicsMesh::TriangleList, _f08) == 0x8, "TriangleList::_f08");
static_assert(offsetof(GraphicsMesh::TriangleList, _f0c) == 0xC, "TriangleList::_f0c");
static_assert(offsetof(GraphicsMesh::TriangleList, _f10) == 0x10, "TriangleList::_f10");
static_assert(offsetof(GraphicsMesh::TriangleList, _f14) == 0x14, "TriangleList::_f14");
static_assert(alignof(GraphicsMesh::TriangleList) == 4, "six dwords, dword aligned");

static_assert(sizeof(GraphicsMesh::Node) == 0x48,
              "GraphicsMesh::Node is 0x48 bytes (GetNodeIndexFromName 0x1800fee90 "
              "advances nine qwords per node, MeshInstance::UpdateHierarchy "
              "0x1800ff910 does ADD RBP,0x48, and animationstate.cpp's element "
              "loop strides 0x48)");
static_assert(alignof(GraphicsMesh::Node) == 8, "GraphicsMesh::Node aligns to 8 (the String)");
static_assert(offsetof(GraphicsMesh::Node, _name)   == 0x00, "Node::_name @ +0x00");
static_assert(offsetof(GraphicsMesh::Node, _parent) == 0x08, "Node::_parent @ +0x08 (UNINITIALISED by ??0Node@ 0x180012550)");
static_assert(offsetof(GraphicsMesh::Node, _local)  == 0x0c, "Node::_local @ +0x0c (?Serialize@Node@ emits 4 then 3 floats)");
static_assert(offsetof(GraphicsMesh::Node, _bind)   == 0x28, "Node::_bind @ +0x28 (ditto, and animationstate.cpp:306 names it)");
static_assert(offsetof(GraphicsMesh::Node, _pad44)  == 0x44, "Node::+0x44 is past the ctor's last store");
// `Mesh.h`'s `RtGraphics::MeshNode` is lane E's overlay of the same bytes and
// pins three of them; the cross-asserts are in the unit test, for the include
// reason at the top of this file.

static_assert(sizeof(GraphicsMesh::DetailLevel) == 0xb0,
              "GraphicsMesh::DetailLevel is 0xb0 bytes (the IMUL R8,R8,0xb0 in "
              "all eight of its accessors)");
static_assert(offsetof(GraphicsMesh::DetailLevel, _name)             == 0x00, "DetailLevel::_name @ +0x00 (GetSubMeshName 0x180012770)");
static_assert(offsetof(GraphicsMesh::DetailLevel, _image)            == 0x08, "DetailLevel::_image @ +0x08 (GetImage 0x1800126e0)");
static_assert(offsetof(GraphicsMesh::DetailLevel, _distance)         == 0x10, "DetailLevel::_distance @ +0x10 (0x180012660)");
static_assert(offsetof(GraphicsMesh::DetailLevel, _vertexBuffer)     == 0x18, "DetailLevel::_vertexBuffer @ +0x18 (0x1800125e0)");
static_assert(offsetof(GraphicsMesh::DetailLevel, _indexBuffer)      == 0x38, "DetailLevel::_indexBuffer @ +0x38 (0x1800125c0)");
static_assert(offsetof(GraphicsMesh::DetailLevel, _skinVertexBuffer) == 0x58, "DetailLevel::_skinVertexBuffer @ +0x58 (0x180012620)");
static_assert(offsetof(GraphicsMesh::DetailLevel, _skinIndexBuffer)  == 0x78, "DetailLevel::_skinIndexBuffer @ +0x78 (0x180012600)");
static_assert(offsetof(GraphicsMesh::DetailLevel, _triangleLists)    == 0x98, "DetailLevel::_triangleLists @ +0x98 (0x180012640)");
static_assert(offsetof(GraphicsMesh::DetailLevel, _maxInstances)     == 0xa8, "DetailLevel::_maxInstances @ +0xa8 (0x180012700)");
static_assert(offsetof(GraphicsMesh::DetailLevel, _vertexBuffer) +
              offsetof(Video::VertexBuffer, platformData) == 0x20 &&
              offsetof(GraphicsMesh::DetailLevel, _indexBuffer) +
              offsetof(Video::IndexBuffer, platformData) == 0x40,
              "the companions TriangleListPass dispatches through are the "
              "record's +0x20 / +0x40 / +0x60 / +0x80 (0x1800a4670)");
static_assert(offsetof(GraphicsMesh::DetailLevel, _name)   == 0x00 &&
              offsetof(GraphicsMesh::DetailLevel, _pad14)  == 0x14 &&
              offsetof(GraphicsMesh::DetailLevel, _padAC)  == 0xac,
              "DetailLevel +0x00 / +0x14 / +0xac");

}  // namespace Graphics

// ==== B3R RT-VG: the graphics-mesh Collection::Array instantiations ====
// Defined in src/engine/collection/array_graphics_mesh.cpp (given external
// linkage for their direct readers in src/engine/graphics/).
namespace rtcoll {
void* Rt_ArrayDtor_180013fb0(void* a);  // 0x180013fb0 ~Array<TriangleList>
void* Rt_ArrayDtor_180014160(void* a);  // 0x180014160 ~Array<Array<T>>
void Rt_SetSize_18005d840(void* a, int n);  // 0x18005d840 Array<TriangleList>
void Rt_SetSize_18005cda0(void* a, int n);  // 0x18005cda0 Array<Array<T>>
void* Rt_TriangleListCtor_1800fefc0(void* p);  // 0x1800fefc0
void* Rt_TriangleListCopyCtor_180013c50(void* d, const void* s);  // 0x180013c50
}  // namespace rtcoll
// ==== end B3R RT-VG ====
