// ============================================================================
// src/engine/video/pixelprogram.cpp — Video::PixelProgram, the whole class
// (original file: code\Runtime\Video\PixelProgram.h + ShaderProgram_win.cpp)
// ============================================================================
//
// ??_7PixelProgram@Video@@6B@ (retail 0x180157ac0), nine slots:
//
//   0  deleting dtor        0x18004eb90  ShaderProgram.h (inline, B.3.9)
//   1  TypeName             0x18004e8e0  here
//   2  Type                 0x18004e8c0  here
//   3  GetFileVersion       0x1800f3a80  here  (returns 5)
//   4  External::Compile    0x1800066e0  already ported
//   5  Serialize            0x1800f3a90  here
//   6  Reconstruct          0x18004ea60  here
//   7  Destruct             0x18004ea90  here
//   8  Construct            0x18004ea80  here
//
// plus ??0PixelProgram@Video@@QEAA@XZ (0x1800f3950, the factory step at
// 0x18004ea2f), ~PixelProgram (0x1800f3a00), the three Platform* helpers and
// GetData / Bind.
//
// PixelProgram is VertexProgram's twin: same 0x58 layout, same five-vtable
// ctor, same "base Serialize then a dead v0 block" shape. The THREE things
// that differ, and they are the three that would be silently wrong if this file
// were written by copying vertexprogram.cpp:
//
//   1. the DeviceData slot is +0x80 (CreatePixelProgramData), not +0x78;
//   2. the ExternalPtr the v0 block reads and drops is
//      ExternalPtr<VertexProgram> (0x1800542a0), not ExternalPtr<VertexLayout>;
//   3. the v0 block's four temporaries are different types in a different
//      order — see SerializeLegacyV0 below.
//
// Everything said in vertexprogram.cpp about the v0 block applies here too:
// it can only run when reading a pre-v1 file and it keeps nothing.
//
// ============================================================================
// ENDGAME B.3.5 BLOCK B — THE BODIES MOVED INTO THE MEMBERS.
// ============================================================================
// Read vertexprogram.cpp's banner of the same name: `Video::PixelProgram` is the
// twin conversion, member for member. The bodies are members of the real class
// (src/engine/video/ShaderProgram.h), defined in the `namespace Video` block
// at the bottom of this file; ~PixelProgram runs only PlatformDestruct, and
// ~ShaderProgram / ~CompiledExternal / ~External are the destructor CHAIN, which
// is exactly retail's inlined teardown at 0x1800f3a00. ENDGAME B.3.9 VID: the
// ctor is REAL (ShaderProgram.h) and the factory pair is the class's own.
//
// CORRECTION (cutover closure batch D1, 2026-09-12). This banner used to end
// "...are reached through ResolveAddr and RESOLVE TO NOTHING ONCE THE DLL IS
// GONE". All four are now ported and registered: 0x1800547e0 and 0x180054a20
// as `kSpec_StringRefZ_PP` / `kSpec_PpSampler10` in
// src/engine/collection/array.cpp, 0x180054920 and 0x180054ba0 as the
// `kD_Str_08` / `kD_Str_10` Body-B rows in array_destroy.cpp; B3R: the four
// calls below name those bodies (rtcoll::, Collection.h) directly.
// ============================================================================
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>

#include "video/Device.h"   // ENDGAME B.3.9 VID: Video::Device::_singleton
#include "video/CompanionData.h"   // the typed *Data companions
#include "system/String.h"
#include "io/Serializer.h"
#include "io/Stream.h"               // IO::Stream, IO::kReading
#include "video/ShaderProgram.h"
#include "collection/Collection.h"   // rtcoll:: the array instantiations
#include "object/External.h"         // Object::SerializeExternal<T>
#include "system/MemoryAllocator.h"

namespace {

using Video::PixelProgram;

// B.4 (R-VIDEO): the version-0 temporaries, typed. Each array is an unrolled
// `Collection::Array<T>` triple `{data, size, capacity}` handed by address to
// its unexported template instance (rtcoll::, Collection.h); see
// vertexprogram.cpp for why they are not `Collection::Array<T>`.
struct StringArray { System::String* data; int32_t size; int32_t capacity; };

using V0Sampler = Video::PixelProgram::Sampler;   // stride 0x10 (ShaderProgram.h)
struct V0SamplerArray { V0Sampler* data; int32_t size; int32_t capacity; };

using System::MemoryAllocator;   // MemFree 0x1800c7ad0

// ---- the version-0 legacy block -------------------------------------------
void SerializeLegacyV0(IO::Serializer& ser) {
    IO::ReaderWriter* rw = ser._readerWriter;
    const bool reading = (ser._stream->GetDirection() == IO::kReading);

    Video::VertexProgram* droppedVertexProgram = nullptr;   // ExternalPtr<VertexProgram>
    StringArray    strings8 = { nullptr, 0, 0 };   // stride 8, one String each
    StringArray    namesA   = { nullptr, 0, 0 };   // Array<System::String>
    StringArray    namesB   = { nullptr, 0, 0 };   // Array<System::String>
    V0SamplerArray samplers = { nullptr, 0, 0 };   // stride 0x10 {String, int, 3 bools}
    int32_t  n = 0;

    Object::SerializeExternal<Video::VertexProgram>(ser, droppedVertexProgram);   // 0x1800542a0

    n = 0;
    rw->SerializeInt(n, nullptr);
    if (reading) rtcoll::RtColl_ArraySetSize_1800547e0(&strings8, n);
    for (int32_t i = 0; i < n; ++i)
        strings8.data[i].Serialize(ser);

    n = 0;
    rw->SerializeInt(n, nullptr);
    // RtIo_SerializeStringArray (0x180029bb0): its 4th parameter is unused --
    // retail spills R9 to its home slot and overwrites that slot (0x180029d00)
    // before any read.
    if (reading) rtcoll::RtColl_ArraySetSize_180029a80(&namesA, n);
    RtIo_SerializeStringArray(&ser, n, namesA.data, nullptr);

    n = 0;
    rw->SerializeInt(n, nullptr);
    if (reading) rtcoll::RtColl_ArraySetSize_180029a80(&namesB, n);
    RtIo_SerializeStringArray(&ser, n, namesB.data, nullptr);

    n = 0;
    rw->SerializeInt(n, nullptr);
    if (reading) rtcoll::RtColl_ArraySetSize_180054a20(&samplers, n);
    {
        V0Sampler* e = samplers.data;
        for (int32_t i = 0; i < n; ++i, ++e) {
            e->name.Serialize(ser);
            rw->SerializeInt (e->unit,  nullptr);
            rw->SerializeBool(e->flagA, nullptr);
            rw->SerializeBool(e->flagB, nullptr);
            rw->SerializeBool(e->flagC, nullptr);
        }
    }

    // Teardown, in retail's order: samplers, namesB, namesA, strings8.
    rtcoll::RtColl_DestroyStrings_180054ba0(nullptr, samplers.data, samplers.size);
    MemoryAllocator::MemFree(samplers.data); samplers = V0SamplerArray{ nullptr, 0, 0 };
    rtcoll::RtColl_DestroyStrings_180011010(nullptr, namesB.data, namesB.size);
    MemoryAllocator::MemFree(namesB.data);   namesB   = StringArray{ nullptr, 0, 0 };
    rtcoll::RtColl_DestroyStrings_180011010(nullptr, namesA.data, namesA.size);
    MemoryAllocator::MemFree(namesA.data);   namesA   = StringArray{ nullptr, 0, 0 };
    rtcoll::RtColl_DestroyStrings_180054920(nullptr, strings8.data, strings8.size);
    MemoryAllocator::MemFree(strings8.data); strings8 = StringArray{ nullptr, 0, 0 };
    (void)samplers; (void)namesA; (void)namesB; (void)strings8; (void)droppedVertexProgram;
}

}  // namespace

// ============================================================================
// THE MEMBER BODIES — `Video::PixelProgram::*`. At `namespace Video` scope and
// AFTER the anonymous namespace (B34_BRIEFS A.2).
// ============================================================================
namespace Video {

// TypeName / TypeId / Type / `_TypeName` / `_ParentTypeName`: DECLARE_TYPE /
// DEFINE_TYPE's (video_type_records.cpp; ENDGAME B.3.9 TI-REST).
// RULE B5-15: read out of .rdata, not derived from the C++ base name.
const char* PixelProgram::_FileExtension()  { return "srsl"; }

// slot 3 — 0x1800f3a80 is `MOV EAX,5 ; RET`.
int32_t PixelProgram::GetFileVersion() const { return 5; }

// slot 8 — ?Construct@: `TEST RCX,RCX / JZ` then the ctor (`this` is never null
// here). `virtual`, not `override`: this branch introduces the slot (RULE
// B5-16). `::new` because the class declares its own (unported) placement new.
Object::External* PixelProgram::Construct() {
    return ::new (this) PixelProgram();
}

// slot 6 — ?Reconstruct@: slot 0 with flags 0 (destroy in place, no free), then
// the ctor over the same storage.
Object::External* PixelProgram::Reconstruct() {
    this->~PixelProgram();
    return ::new (this) PixelProgram();
}

// slot 7 — ?Destruct@: `XOR EDX,EDX ; JMP [RAX]` — slot 0, flags 0.
void PixelProgram::Destruct() { this->~PixelProgram(); }

// ??1PixelProgram@Video@@UEAA@XZ — retail: stamp ??_7PixelProgram, PlatformDestruct, then
// ~ShaderProgram inlined and ~External. The stamps and everything after
// PlatformDestruct are the destructor chain's.
PixelProgram::~PixelProgram() {
    this->Video::PixelProgram::PlatformDestruct();
}

// ?CreateNewObject@PixelProgram@Video@@SAPEAVBase@Object@@XZ  0x18004e9d0:
// MemAlloc(0x58, 8), null branch, the ctor (B39_RULES D2).
Object::Base* PixelProgram::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(PixelProgram), 8);
    if (p == nullptr) return nullptr;
    return ::new (p) PixelProgram();
}
// ?CreateInPlaceObject@PixelProgram@Video@@SAPEAVBase@Object@@PEAX@Z  0x18004ea50
Object::Base* PixelProgram::CreateInPlaceObject(void* storage) {
    return storage ? ::new (storage) PixelProgram() : nullptr;
}

// ?GetData@PixelProgram@Video@@QEBAPEBVPixelProgramData@2@XZ (0x18004eaa0)
const void* PixelProgram::GetData() const { return _data; }

// ?Bind@PixelProgram@Video@@QEBAXAEAVContext@2@H@Z (0x1800f3ed0)
void PixelProgram::Bind(void* context, int32_t permutation) const {
    _data->Bind(context, permutation);
}

// ?PlatformConstruct@PixelProgram@Video@@AEAAXXZ (0x1800f3e40). THE SLOT IS
// +0x80, NOT +0x78: CreatePixelProgramData, not CreateVertexProgramData. Swapping
// the two produces a PixelProgramData behind a VertexProgram's vtable, which
// fails only on the first Bind.
void PixelProgram::PlatformConstruct() {
    Device* device = Device::_singleton;
    DeviceData* dd = device ? device->deviceData : nullptr;
    if (dd == nullptr) { _data = nullptr; return; }
    _data = dd->CreatePixelProgramData(this);
}
// ?PlatformCreate@PixelProgram@Video@@AEAA_NXZ (0x1800f3eb0)
bool PixelProgram::PlatformCreate() {
    return _data->PlatformCreate();
}
// ?PlatformDestruct@PixelProgram@Video@@AEAAXXZ (0x1800f3e90): slot 0, flags 1.
// B5 RES (B5_RULES D10): retail's slot-0 call with flags 1 is `delete` -- the
// virtual destructor, then the base's class-scope operator delete (MemFree).
void PixelProgram::PlatformDestruct() {
    if (_data == nullptr) return;
    delete _data;
}

// slot 5 — ?Serialize@PixelProgram@Video@@UEAA_NAEAVStream@IO@@@Z (0x1800f3a90)
bool PixelProgram::Serialize(IO::Stream& streamv) {
    // Retail CALLs 0x1800f5110 here; the QUALIFIED spelling is that call.
    this->ShaderProgram::Serialize(streamv);

    IO::Stream& stream = streamv;
    IO::Serializer ser(stream, 1);

    if (ser._readerWriter->GetVersion() == 0)
        SerializeLegacyV0(ser);

    if (stream.GetDirection() == IO::kReading)
        this->Video::PixelProgram::PlatformCreate();   // retail derefs _data unguarded

    return true;   // ~Serializer: the ReaderWriter's slot 0, flags 1
}

}  // namespace Video
