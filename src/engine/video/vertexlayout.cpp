// ============================================================================
// src/engine/video/vertexlayout.cpp — Video::VertexLayout, the whole class
// ============================================================================
//
// ??_7VertexLayout@Video@@6B@ (retail 0x180157ba0), nine slots:
//
//   0  deleting dtor        0x18004e000  VertexLayout.h (inline, B.3.9)
//   1  TypeName             0x18004dcd0  here
//   2  Type                 0x18004dcb0  here
//   3  GetFileVersion       0x1800f5e60  here  (returns 3)
//   4  External::Compile    0x1800066e0  already ported
//   5  Serialize            0x1800f5e70  here
//   6  Reconstruct          0x18004de50  here
//   7  Destruct             0x18004de80  here
//   8  Construct            0x18004de70  here
//
// plus ??0VertexLayout@Video@@QEAA@XZ (0x1800f5d50), ~VertexLayout (0x1800f5df0)
// and the element serializer loop 0x1800563c0. GetElementCount / GetElement /
// GetStreamCount / Add / BeginAdd / EndAdd are non-virtual and not on the boot
// path; they are declared in the header and not ported here.
//
// ENDGAME B.3.9 block VID: the class is CONCRETE and wears the compiler's table.
// The ctor door, the dtor door and the seven slot doors are GONE (their callers
// were the minted table, the FACTREST interpreter and the RT_DELDTOR row, all
// retired with ??_7VertexLayout@Video@@6B@); the factory pair is the class's own.
//
// ============================================================================
// ENDGAME B.3.5 BLOCK B — THE BODIES MOVED INTO THE MEMBERS, AND THIS FILE'S
// LOCAL STRUCTS ARE GONE
// ============================================================================
// `Video::VertexLayout` and its nested `Element` are a real C++ class, authored
// by block A in `src/engine/video/VertexLayout.h` from retail bytes, deriving
// from the real `Object::CompiledExternal`. This file's anonymous-namespace
// `struct VertexLayout` / `struct VertexElement` — one of the two partial,
// private views block A's header replaced — ARE DELETED. Each body below is a
// member, defined in the `namespace Video` block at the bottom (B34_BRIEFS A.2).
//
// *** THE FIELD NAMES CHANGED AND THE WIRE DID NOT. ***
// Block A PROVED from retail that this file's `VertexElement` map was wrong in
// NAMING: `?Add@VertexLayout@Video@@` (0x1800f5a90) stores its five parameters at
// +0x08/+0x0c/+0x10/+0x14/+0x18 in declaration order, so the map is
//     +0x08 stream   +0x0c offset   +0x10 usage   +0x14 usageIndex   +0x18 format
// where this file used to read +0x0c as `usage`, +0x10 as `usageIndex`, +0x14 as
// `format` and +0x18 as `offset`. `?GetStreamCount@` (0x1800f5a30) and VideoDX11's
// `PlatformCreate` (0x180003ff0) agree with block A independently.
//
// The mistake was INERT because retail's element loop (0x1800563c0) writes the
// five fields POSITIONALLY, in address order, through a fixed sequence of
// reader/writer slots — Int, Int, UInt, Int, UInt — and the old permutation
// happened to preserve that sequence. So the rename must NOT reorder the loop:
// the loop below walks the same five offsets in the same order through the same
// five slots, and only the names moved. The pairing is written out field by field
// at the loop so a future edit cannot "tidy" `usage` into the Int slot because its
// C++ type is `int32_t`.
//
// `_platform` is now `_signature`, block A's proved name: VideoDX11 hands those
// bytes to `CreateInputLayout` as the vertex-shader INPUT SIGNATURE and aborts the
// whole of `PlatformCreate` when its `_size` is 0.
//
// Two runtime-internal bindings retire, both inside this block's file set:
//   * `RT_EXPORT_ONCE(ExtDtorFn, "??1External@Object@@UEAA@XZ")(self)` — the base
//     destructor is the destructor CHAIN's now, which is what retail's tail-JMP to
//     0x1800bc950 is (spelling 3);
//   * `RtAddr<ElemSerFn>(0x1800563c0)(...)` — Serialize reached its OWN registered
//     element loop by going out to the registry and back; it calls it directly
//     (spelling 4).
//
// Layout (sizeof 0x48) and every offset's evidence: src/engine/video/VertexLayout.h.
//
// Serialize (0x1800f5e70): build the {stream, readerWriter} pair;
//   int n = _elements.size; rw->Int(&n); if reading SetSize(_elements, n) (0x180056240)
//   0x1800563c0(&ser, n, _elements.data)                 element loop, below
//   PlatformSerialize inlined: int m = _signature.size; rw->Int(&m);
//     if reading SetSize(_signature, m) (0x180029630); rw->UCharArray(m, _signature.data)
//   if reading: _data->vtable[2](_data)                  PlatformCreate
//   delete rw
// ============================================================================
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>

#include "video/Device.h"   // ENDGAME B.3.9 VID: Video::Device::_singleton
#include "video/CompanionData.h"   // the typed *Data companions
#include "system/String.h"
#include "collection/Collection.h"
#include "io/Serializer.h"
#include "io/Stream.h"      // IO::Stream, IO::kReading
#include "object/External.h"
#include "video/VertexLayout.h"

namespace {

using Video::VertexLayout;

// ============================================================================
// 0x1800563c0 — the per-element loop: (Serializer*, int count, Element* data)
//
// UNEXPORTED in retail, so it is not a member of
// anything: it stays a file-scope function, typed with the real
// `Video::VertexLayout::Element`. `Serialize` below calls it DIRECTLY. B.4
// (R-VIDEO): its one caller is this file, so it has internal linkage now.
//
// THE FIVE FIELDS AND THEIR READER/WRITER SLOTS, PAIRED EXPLICITLY. Retail walks
// the element in ADDRESS order through a fixed slot sequence, and the slot is NOT
// derivable from the field's C++ type — `usage` and `format` are `int32_t` and go
// through the UNSIGNED slot, `usageIndex` is `int32_t` and goes through the signed
// one. Getting any pair wrong re-encodes every `MeshLayout\*.rsc` and every save.
//
//     +0x00  name        String::Serialize
//     +0x08  stream      kScalarInt
//     +0x0c  offset      kScalarInt     (this file used to call this one `usage`)
//     +0x10  usage       kScalarUInt    (…and this one `usageIndex`)
//     +0x14  usageIndex  kScalarInt     (…and this one `format`)
//     +0x18  format      kScalarUInt    (…and this one `offset`)
//
// B.4 (R-VIDEO): the two unsigned-slot fields go through a `uint32_t` local and back
// (value-identical on both directions: the writer reads the local, the reader
// fills it and the store puts the same four bytes in the field).
// ============================================================================
void RtVideo_VertexLayout_SerializeElements(IO::Serializer& ser, int count,
                                            VertexLayout::Element* e) {
    IO::ReaderWriter* rw = ser._readerWriter;
    for (int i = 0; i < count; ++i, ++e) {
        e->name.Serialize(ser);
        rw->SerializeInt(e->stream, nullptr);
        rw->SerializeInt(e->offset, nullptr);
        uint32_t usage = static_cast<uint32_t>(e->usage);
        rw->SerializeUInt(usage, nullptr);
        e->usage = static_cast<VertexLayout::Usage>(usage);
        rw->SerializeInt(e->usageIndex, nullptr);
        uint32_t format = static_cast<uint32_t>(e->format);
        rw->SerializeUInt(format, nullptr);
        e->format = static_cast<VertexLayout::Format>(format);
    }
}

}  // namespace

// ============================================================================
// THE MEMBER BODIES — `Video::VertexLayout::*`. At `namespace Video` scope and
// AFTER the anonymous namespace (B34_BRIEFS A.2).
// ============================================================================
namespace Video {

// slot 1, TypeId 0x18004dcc0, slot 2 0x18004dcb0: DECLARE_TYPE's over the
// class's own record (video_type_records.cpp; ENDGAME B.3.9 TI-REST).

// slot 3 — 0x1800f5e60 is `MOV EAX,3 ; RET`. This port said 1 until the
// 2026-09-12 cutover walk: DLL-present the DLL's own vtable answered, so nothing
// noticed; DLL-absent every MeshLayout\*.rsc load failed External::Load's version
// check (badVer=65), every VideoProgram lost its VertexLayout, and
// Renderer::AddDrawItem silently dropped every draw item -- the black window.
int32_t VertexLayout::GetFileVersion() const { return 3; }

// slot 8 — ?Construct@VertexLayout@Video@@UEAAPEAVExternal@Object@@XZ 0x18004de70:
// `TEST RCX,RCX / JZ` then the ctor. `virtual`, not `override`: this branch
// introduces the slot (RULE B5-16). `::new` because the class declares its own
// (unported) placement operator new.
Object::External* VertexLayout::Construct() {
    return ::new (this) VertexLayout();
}

// slot 6 — 0x18004de50: slot 0 with flags 0 (destroy in place), then the ctor.
Object::External* VertexLayout::Reconstruct() {
    this->~VertexLayout();
    return ::new (this) VertexLayout();
}

// slot 7 — ?Destruct@VertexLayout@Video@@UEAAXXZ 0x18004de80: slot 0, flags 0.
void VertexLayout::Destruct() { this->~VertexLayout(); }

// ??1VertexLayout@Video@@UEAA@XZ (0x1800f5df0)
//
// Retail's order: stamp ??_7VertexLayout, PlatformDestruct, ~Array<uint8> over
// `_signature`, ~Array<Element> over `_elements` (which releases the names), stamp
// ??_7CompiledExternal, tail-JMP ~External. The stamps are the compiler's (entry
// to each destructor of the chain) and the tail-JMP is the chain itself.
//
// The two array teardowns call the ported retail bodies, which end in
// `ArrayDtorImpl` and zero `_data`/`_size`/`_capacity`
// (src/engine/collection/Collection.h), so the compiler-emitted `~Array<T>` that
// runs after this body sees an empty header and frees nothing. Each half runs
// exactly once — the same decomposition `Object::External::~External` uses for
// `_refs`.
VertexLayout::~VertexLayout() {
    this->Video::VertexLayout::PlatformDestruct();
    _signature.~Array();                                 // 0x1800112a0 Array<uint8>::~Array
    rtcoll::RtColl_ArrayDtorElem_18004fa30(&_elements);  // 0x18004fa30 ~Array<Element> (releases names)
}

// ?CreateNewObject@VertexLayout@Video@@SAPEAVBase@Object@@XZ  0x18004ddc0:
// MemAlloc(0x48, 8), null branch, the ctor (B39_RULES D2).
Object::Base* VertexLayout::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(VertexLayout), 8);
    if (p == nullptr) return nullptr;
    return ::new (p) VertexLayout();
}
// ?CreateInPlaceObject@VertexLayout@Video@@SAPEAVBase@Object@@PEAX@Z  0x18004de40
Object::Base* VertexLayout::CreateInPlaceObject(void* storage) {
    return storage ? ::new (storage) VertexLayout() : nullptr;
}

// ?PlatformConstruct@VertexLayout@Video@@AEAAXXZ (0x1800f5f90) — DeviceData slot
// 14 (+0x70), CreateVertexLayoutData.
void VertexLayout::PlatformConstruct() {
    Device* device = Device::_singleton;
    DeviceData* dd = device ? device->deviceData : nullptr;
    if (dd == nullptr) { _data = nullptr; return; }
    _data = dd->CreateVertexLayoutData(this);
}
// ?PlatformCreate@VertexLayout@Video@@AEAAXXZ (0x1800f6060): the companion's slot 2.
// VOID, unlike VertexProgram's and PixelProgram's bool — VideoDX11's 0x180003ff0
// falls off its end with no return value set.
void VertexLayout::PlatformCreate() {
    _data->PlatformCreate();
}
// ?PlatformDestruct@VertexLayout@Video@@AEAAXXZ (0x1800f5fd0): slot 0, flags 1.
// B5 RES (B5_RULES D10): retail's slot-0 call with flags 1 is `delete` -- the
// virtual destructor, then the base's class-scope operator delete (MemFree).
void VertexLayout::PlatformDestruct() {
    if (_data == nullptr) return;
    delete _data;
}

// slot 5 — ?Serialize@VertexLayout@Video@@UEAA_NAEAVStream@IO@@@Z (0x1800f5e70)
bool VertexLayout::Serialize(IO::Stream& streamv) {
    IO::Stream& stream = streamv;
    IO::Serializer ser(stream, 0);
    IO::ReaderWriter* rw = ser._readerWriter;

    int32_t n = _elements._size;
    rw->SerializeInt(n, nullptr);
    if (ser._stream->GetDirection() == IO::kReading)
        rtcoll::RtColl_ArraySetSize_180056240(&_elements, n);
    // Retail CALLs 0x1800563c0; this IS that function, in this file.
    RtVideo_VertexLayout_SerializeElements(ser, n, _elements._data);

    // PlatformSerialize (0x1800f5ff0), inlined by retail and inlined here.
    int32_t m = _signature._size;
    rw->SerializeInt(m, nullptr);
    if (ser._stream->GetDirection() == IO::kReading)
        rtcoll::RtColl_ArraySetSize_180029630(&_signature, m);
    rw->SerializeUCharArray(m, _signature._data, nullptr);

    if (stream.GetDirection() == IO::kReading)
        this->Video::VertexLayout::PlatformCreate();   // retail derefs _data unguarded
    return true;   // ~Serializer: the ReaderWriter's slot 0, flags 1
}

}  // namespace Video
