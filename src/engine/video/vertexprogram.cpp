// ============================================================================
// src/engine/video/vertexprogram.cpp — Video::VertexProgram, the whole class
// (original file: code\Runtime\Video\VertexProgram.h + ShaderProgram_win.cpp)
// ============================================================================
//
// ??_7VertexProgram@Video@@6B@ (retail 0x180157b10), nine slots:
//
//   0  deleting dtor        0x18004e810  ShaderProgram.h (inline, B.3.9)
//   1  TypeName             0x18004e560  here
//   2  Type                 0x18004e540  here
//   3  GetFileVersion       0x1800f6230  here  (returns 5)
//   4  External::Compile    0x1800066e0  already ported
//   5  Serialize            0x1800f6240  here
//   6  Reconstruct          0x18004e6e0  here
//   7  Destruct             0x18004e710  here
//   8  Construct            0x18004e700  here
//
// plus ??0VertexProgram@Video@@QEAA@XZ (0x1800f6100) — the ctor the cutover
// gate died on, called from the factory step at 0x18004e6af — ~VertexProgram
// (0x1800f61b0), the three private Platform* helpers and the non-virtual
// GetData / Bind.
//
// Layout: src/engine/video/ShaderProgram.h (ShaderProgram 0x50 + _data, 0x58).
//
// ---------------------------------------------------------------------------
// THE CTOR IS ShaderProgram's, INLINED — AND THE VTABLE ORDER MATTERS
// ---------------------------------------------------------------------------
// 0x1800f6100 stamps FIVE vtables in order: Base, External, CompiledExternal,
// **ShaderProgram** and only then VertexProgram. The fourth stamp is not
// decoration — an object that faults between the fourth and fifth store is
// destroyed as a ShaderProgram, and that is the shape retail's unwinder
// expects. Then PlatformConstruct: DeviceData slot +0x78.
//
// ---------------------------------------------------------------------------
// Serialize (0x1800f6240): THE BASE CALL, THEN A DEAD-ON-ARRIVAL v0 BLOCK
// ---------------------------------------------------------------------------
//     ShaderProgram::Serialize(this, stream)        0x1800f5110
//     rw = encoding->Create{Reader,Writer}(&ser, 1)
//     if (rw->GetVersion() == 0) { ...the legacy block... }
//     if (reading) _data->vtable[1](_data)          PlatformCreate
//     delete rw
//
// The legacy block reads FOUR temporary arrays and an ExternalPtr off the
// stream and then throws every one of them away without looking at a single
// field — it exists purely to advance a version-0 stream past data this build
// no longer stores on the object. It can only run on the READ path of a file
// written by a pre-v1 build: BinaryWriter stamps the version it was constructed
// with, which is the literal 1 above, so on the write path GetVersion() is 1
// and the block is skipped unconditionally.
//
// It is ported anyway, and it is ported as retail wrote it — including the
// ExternalPtr<VertexLayout> that is read and dropped. Five of the helpers it
// calls are unexported internals:
//     0x1800567d0  SetSize, stride 0x18 {String, String, uint, int}
//     0x180056960  ~Array of that element
//     0x180056af0  SetSize, the second legacy element type
//     0x180056db0  the per-element serialize loop for it
//     0x180056cb0  ~Array of it
//
// ============================================================================
// ENDGAME B.3.5 BLOCK B — THE BODIES MOVED INTO THE MEMBERS.
// ============================================================================
// `Video::VertexProgram` is a real C++ class deriving from the real
// `Video::ShaderProgram` (src/engine/video/ShaderProgram.h). Every body below
// is a member, defined in the `namespace Video` block at the bottom of this file
// (B34_BRIEFS A.2).
//
// THE DESTRUCTOR CHAIN IS RETAIL'S INLINED TEARDOWN, FOR FREE. ~VertexProgram
// (0x1800f61b0) is: stamp ??_7VertexProgram, PlatformDestruct, stamp
// ??_7ShaderProgram, the three array teardowns, stamp ??_7CompiledExternal,
// tail-JMP ~External. The member below calls PlatformDestruct; the compiler
// writes the stamps, and everything after PlatformDestruct is `~ShaderProgram()` →
// `~CompiledExternal()` → `~External()`, which is what the compiler emits for
// this inheritance chain. `RtVideo_ShaderProgram_DestroyArrays` — the cross-file
// `extern "C"` that used to run ShaderProgram's half by hand — IS GONE, and so
// is the `RT_EXPORT_ONCE(…"??1External@Object@@UEAA@XZ")` binding: two
// runtime-internal bindings retired, both inside this block's file set.
//
// ENDGAME B.3.9 block VID: THE CTOR IS REAL NOW (ShaderProgram.h). The ctor and
// dtor doors, ConstructBody and the seven slot doors retired with
// ??_7VertexProgram@Video@@6B@; the factory pair is the class's own.
//
// CORRECTION (cutover closure batch D1, 2026-09-12). This banner used to read
// "five ... UNPORTED internals ... with the DLL gone those five resolve to
// nothing, so a version-0 asset would call address zero". ALL FIVE ARE NOW
// PORTED — 0x1800567d0 / 0x180056af0 as `kSpec_VtxElement18` and
// `kSpec_VtxStream18` in src/engine/collection/array.cpp, 0x180056960 and
// 0x180056cb0 in array_destroy.cpp, and 0x180056db0 near the top of THIS file
// (RtVideo_VertexProgram_SerializeStreams). The five calls name those bodies
// directly. What remains true is
// that the block is unreachable for any shipped asset (every one is version 1).
// ============================================================================
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>

#include "video/Device.h"   // ENDGAME B.3.9 VID: Video::Device::_singleton
#include "video/CompanionData.h"   // the typed *Data companions
#include "system/String.h"
#include "io/Serializer.h"
#include "video/ShaderProgram.h"
#include "io/Stream.h"               // IO::Stream, IO::kReading
#include "collection/Collection.h"   // rtcoll:: the array instantiations
#include "object/External.h"         // Object::SerializeExternal<T>
#include "system/MemoryAllocator.h"
#include "video/VertexLayout.h"   // B.4 P2 X: T::GetTypeInfo() for Object::SerializeExternal<T>

namespace {

using Video::VertexProgram;

// B.4 (R-VIDEO): the version-0 temporaries, typed. Each array is an unrolled
// `Collection::Array<T>` triple `{data, size, capacity}` handed by address to
// its unexported template instance (rtcoll::, Collection.h), which is why the
// triples are plain structs here and not `Collection::Array<T>` (H5: an
// `Array` would run its own destructor on top of the explicit teardown below).
// B.4 (R-GFX): the two records are `Video::VertexProgram::Element` / `Stream`
// (ShaderProgram.h), where the collection bodies can name them too.
using V0Element      = VertexProgram::Element;
using V0ElementArray = VertexProgram::ElementArray;
using V0Stream       = VertexProgram::Stream;
struct V0StreamArray { V0Stream* data; int32_t size; int32_t capacity; };

struct StringArray { System::String* data; int32_t size; int32_t capacity; };

// Retail inlines EnterCriticalSection / Free / LeaveCriticalSection here; the
// exported ?MemFree@MemoryAllocator@System@@SAXPEAX@Z (0x1800c7ad0) is that
// sequence instruction for instruction, plus the null guard retail also does
// (src/engine/collection/array.cpp "THE ALLOCATOR BOUNDARY").
using System::MemoryAllocator;

// ============================================================================
// The legacy Stream element serialize loop   0x180056db0   (batch D1)
// ============================================================================
// SerializeLegacyV0 below calls this by name. The element is
// {Collection::Array<Element> @+0x00, System::String* @+0x10}, stride 0x18 —
// the layout array.cpp's kSpec_VtxStream18 and array_destroy.cpp's
// 0x180056cb0 row independently agree on. B.4 (R-VIDEO): its one caller is
// SerializeLegacyV0, so it is this file's own (typed) helper now.
//
//   180056de0  n = elem->inner._size                      (e+0x08)
//   180056df6  kScalarInt(&n)                             — a LOCAL, not the field
//   180056dfc  if (ser->stream->direction == reading)
//   180056e0c      0x1800567d0(&elem->inner, n)           SetSize
//   180056e15  p = elem->inner._data                      (e+0x00)
//   180056e22  per inner element (stride 0x18):
//                 String::Serialize(p+0x00) / (p+0x08)
//                 kScalarUInt(p+0x10) / kScalarInt(p+0x14)
//   180056e6e  String::Serialize(elem+0x10)
//
// The count that drives the inner loop is the LOCAL read back at 0x180056e11,
// not `elem->inner._size` re-read after the SetSize. On the write path they are
// the same number; on the read path of a truncated stream they are not, and
// retail uses the local.
void RtVideo_VertexProgram_SerializeStreams(IO::Serializer& ser, int count, V0Stream* e) {
    if (count <= 0) return;                                  // 180056db0 TEST/JLE

    for (int i = 0; i < count; ++i, ++e) {
        int32_t n = e->elements.size;                        // 180056de0
        ser._readerWriter->SerializeInt(n, nullptr);         // 180056df6

        if (ser._stream->GetDirection() == IO::kReading)     // 180056dfc
            rtcoll::RtColl_ArraySetSize_1800567d0(&e->elements, n);   // 180056e0c

        V0Element* p = e->elements.data;                     // 180056e15
        for (int32_t k = 0; k < n; ++k, ++p) {               // 180056e22
            p->name.Serialize(ser);
            p->semantic.Serialize(ser);
            ser._readerWriter->SerializeUInt(p->format, nullptr);
            ser._readerWriter->SerializeInt(p->index, nullptr);
        }

        e->name.Serialize(ser);                             // 180056e6e
    }
}

// ---- the version-0 legacy block -------------------------------------------
// A version-0 file stored the vertex declaration on the program itself. Nothing
// here is kept; every array is destroyed and freed before the block returns.
void SerializeLegacyV0(IO::Serializer& ser) {
    IO::ReaderWriter* rw = ser._readerWriter;
    const bool reading = (ser._stream->GetDirection() == IO::kReading);

    Video::VertexLayout* droppedLayout = nullptr;   // ExternalPtr<VertexLayout>, read and dropped
    V0ElementArray elements = { nullptr, 0, 0 };
    V0StreamArray  streams  = { nullptr, 0, 0 };   // handled entirely by 0x180056db0
    StringArray    namesA   = { nullptr, 0, 0 };   // Array<System::String>
    StringArray    namesB   = { nullptr, 0, 0 };   // Array<System::String>
    int32_t  n = 0;

    Object::SerializeExternal<Video::VertexLayout>(ser, droppedLayout);   // 0x180055090 ExternalPtr<VertexLayout>

    n = 0;
    rw->SerializeInt(n, nullptr);
    if (reading) rtcoll::RtColl_ArraySetSize_1800567d0(&elements, n);
    {
        V0Element* e = elements.data;
        for (int32_t i = 0; i < n; ++i, ++e) {
            e->name.Serialize(ser);
            e->semantic.Serialize(ser);
            rw->SerializeUInt(e->format, nullptr);
            rw->SerializeInt(e->index, nullptr);
        }
    }

    n = 0;
    rw->SerializeInt(n, nullptr);
    if (reading) rtcoll::RtColl_ArraySetSize_180056af0(&streams, n);
    RtVideo_VertexProgram_SerializeStreams(ser, n, streams.data);   // 0x180056db0

    n = 0;
    rw->SerializeInt(n, nullptr);
    // RtIo_SerializeStringArray (0x180029bb0): its 4th parameter is unused --
    // retail spills R9 to its home slot and overwrites that slot (0x180029d00)
    // before any read.
    if (reading) rtcoll::RtColl_ArraySetSize_180029a80(&namesA, n);   // Array<String>::SetSize
    RtIo_SerializeStringArray(&ser, n, namesA.data, nullptr);         // Array<String> serialize loop

    n = 0;
    rw->SerializeInt(n, nullptr);
    if (reading) rtcoll::RtColl_ArraySetSize_180029a80(&namesB, n);
    RtIo_SerializeStringArray(&ser, n, namesB.data, nullptr);

    bool flag = false;
    rw->SerializeBool(flag, nullptr);

    // Teardown, in retail's order: namesB, namesA, streams, elements.
    rtcoll::RtColl_DestroyStrings_180011010(nullptr, namesB.data, namesB.size);
    MemoryAllocator::MemFree(namesB.data);   namesB   = StringArray{ nullptr, 0, 0 };
    rtcoll::RtColl_DestroyStrings_180011010(nullptr, namesA.data, namesA.size);
    MemoryAllocator::MemFree(namesA.data);   namesA   = StringArray{ nullptr, 0, 0 };
    rtcoll::RtColl_DestroyVtxStream_180056cb0(nullptr, streams.data, streams.size);
    MemoryAllocator::MemFree(streams.data);  streams  = V0StreamArray{ nullptr, 0, 0 };
    rtcoll::RtColl_DestroyStrings_180056960(nullptr, elements.data, elements.size);
    MemoryAllocator::MemFree(elements.data); elements = V0ElementArray{ nullptr, 0, 0 };
    (void)namesA; (void)namesB; (void)streams; (void)elements; (void)droppedLayout;
}

}  // namespace

// ============================================================================
// THE MEMBER BODIES — `Video::VertexProgram::*`. At `namespace Video` scope and
// AFTER the anonymous namespace (B34_BRIEFS A.2); the helpers above stay
// reachable by unqualified name.
// ============================================================================
namespace Video {

// slots 1 / 2 and the two static reflection companions: DECLARE_TYPE /
// DEFINE_TYPE's over the class's own record (video_type_records.cpp; ENDGAME
// B.3.9 TI-REST). The parent literal "ShaderProgram" is NOT namespaced even
// though "Object::CompiledExternal" is on ShaderProgram itself — copied, not
// derived.
const char* VertexProgram::_FileExtension()  { return "srsl"; }

// slot 3 — 0x1800f6230 is `MOV EAX,5 ; RET`. A GetFileVersion that returns the
// wrong constant is what silently dropped every draw item once (B35_RULES 6.1).
int32_t VertexProgram::GetFileVersion() const { return 5; }

// slot 8 — ?Construct@: `TEST RCX,RCX / JZ` then the ctor (`this` is never null
// here). `virtual`, not `override`: this branch introduces the slot (RULE
// B5-16). `::new` because the class declares its own (unported) placement new.
Object::External* VertexProgram::Construct() {
    return ::new (this) VertexProgram();
}

// slot 6 — ?Reconstruct@: slot 0 with flags 0 (destroy in place, no free), then
// the ctor over the same storage.
Object::External* VertexProgram::Reconstruct() {
    this->~VertexProgram();
    return ::new (this) VertexProgram();
}

// slot 7 — ?Destruct@: `XOR EDX,EDX ; JMP [RAX]` — slot 0, flags 0.
void VertexProgram::Destruct() { this->~VertexProgram(); }

// ??1VertexProgram@Video@@UEAA@XZ — retail: stamp ??_7VertexProgram, PlatformDestruct, then
// ~ShaderProgram inlined and ~External. The stamps and everything after
// PlatformDestruct are the destructor chain's.
VertexProgram::~VertexProgram() {
    this->Video::VertexProgram::PlatformDestruct();
}

// ?CreateNewObject@VertexProgram@Video@@SAPEAVBase@Object@@XZ  0x18004e650:
// MemAlloc(0x58, 8), null branch, the ctor (B39_RULES D2).
Object::Base* VertexProgram::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(VertexProgram), 8);
    if (p == nullptr) return nullptr;
    return ::new (p) VertexProgram();
}
// ?CreateInPlaceObject@VertexProgram@Video@@SAPEAVBase@Object@@PEAX@Z  0x18004e6d0
Object::Base* VertexProgram::CreateInPlaceObject(void* storage) {
    return storage ? ::new (storage) VertexProgram() : nullptr;
}

// ?GetData@VertexProgram@Video@@QEBAPEBVVertexProgramData@2@XZ (0x18004e720) —
// `MOV RAX,[RCX+0x50]; RET`.
const void* VertexProgram::GetData() const { return _data; }

// ?Bind@VertexProgram@Video@@QEBAXAEAVContext@2@H@Z (0x1800f6660): both
// arguments pass straight through to the companion's Bind slot.
void VertexProgram::Bind(void* context, int32_t permutation) const {
    _data->Bind(context, permutation);
}

// ?PlatformConstruct@VertexProgram@Video@@AEAAXXZ (0x1800f65e0) — DeviceData
// slot 15 (+0x78), CreateVertexProgramData.
void VertexProgram::PlatformConstruct() {
    Device* device = Device::_singleton;
    DeviceData* dd = device ? device->deviceData : nullptr;
    if (dd == nullptr) { _data = nullptr; return; }
    _data = dd->CreateVertexProgramData(this);
}
// ?PlatformCreate@VertexProgram@Video@@AEAA_NXZ (0x1800f6640) — tail-jumps into
// the companion's slot 1, so it RETURNS whatever that returns.
bool VertexProgram::PlatformCreate() {
    return _data->PlatformCreate();
}
// ?PlatformDestruct@VertexProgram@Video@@AEAAXXZ (0x1800f6620): slot 0, flags 1.
// B5 RES (B5_RULES D10): retail's slot-0 call with flags 1 is `delete` -- the
// virtual destructor, then the base's class-scope operator delete (MemFree).
void VertexProgram::PlatformDestruct() {
    if (_data == nullptr) return;
    delete _data;
}

// slot 5 — ?Serialize@VertexProgram@Video@@UEAA_NAEAVStream@IO@@@Z (0x1800f6240)
bool VertexProgram::Serialize(IO::Stream& streamv) {
    // Retail CALLs 0x1800f5110 here. The QUALIFIED spelling is that call; an
    // unqualified `Serialize(stream)` would re-enter this function.
    this->ShaderProgram::Serialize(streamv);

    IO::Stream& stream = streamv;
    IO::Serializer ser(stream, 1);

    if (ser._readerWriter->GetVersion() == 0)
        SerializeLegacyV0(ser);

    if (stream.GetDirection() == IO::kReading)
        this->Video::VertexProgram::PlatformCreate();   // retail derefs _data unguarded

    return true;   // ~Serializer: the ReaderWriter's slot 0, flags 1
}

}  // namespace Video
