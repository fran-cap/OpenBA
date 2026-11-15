// ============================================================================
// src/engine/video/videoprogram.cpp — Video::VideoProgram, the whole class
// ============================================================================
//
// ??_7VideoProgram@Video@@6B@ (retail 0x180157a70), nine slots:
//
//   0  deleting dtor        0x18004efb0  VideoProgram.h (inline, B.3.9)
//   1  TypeName             0x18004ec50  here
//   2  Type                 0x18004ec30  here
//   3  GetFileVersion       0x1800f46a0  here  (returns 1)
//   4  External::Compile    0x1800066e0  already ported
//   5  Serialize            0x1800f46b0  here
//   6  Reconstruct          0x18004edd0  here
//   7  Destruct             0x18004ee00  here
//   8  Construct            0x18004edf0  here
//
// plus the default ctor ??0VideoProgram@Video@@QEAA@XZ (0x1800f45b0, the real
// constructor in VideoProgram.h since ENDGAME B.3.9), ~VideoProgram (0x1800f4650),
// the factory pair and the three private Platform* helpers.
//
// ENDGAME B.3.9 block VID: the class is CONCRETE and wears the compiler's table.
// The ctor door, the dtor door and the seven slot doors below this banner's
// description are GONE (their callers were the minted table, the FACTREST
// interpreter and the RT_DELDTOR row, all retired with ??_7VideoProgram@Video@@6B@).
//
// ============================================================================
// ENDGAME B.3.5 BLOCK B — THE BODIES MOVED INTO THE MEMBERS, AND THIS FILE'S
// LOCAL `struct VideoProgram` IS GONE
// ============================================================================
// `Video::VideoProgram` is a real C++ class in the NEW
// src/engine/video/VideoProgram.h, which this block had to AUTHOR: B35_RULES
// 13.3 listed this layout as one where the size was verified and the FIELDS were
// not. The header carries the retail evidence for all five offsets — the four
// exported getters, the ctor's five stores, SetupShaders's context/dirty-bit
// pairing and Serialize's four per-T instantiations, four independent agreements.
//
// *** ITS PARENT IS `Object::External` DIRECTLY — NOT CompiledExternal, NOT
// ShaderProgram. *** That is B35_RULES 2.1's headline trap and the reason
// VideoProgram converts LAST and on its own (section 10 move 4). The ctor at
// 0x1800f45b0 stamps Base, External and VideoProgram and NEVER
// ??_7CompiledExternal@Object@@6B@, which ??0ShaderProgram and ??0VertexLayout
// both do; `?_ParentTypeName@VideoProgram@Video@@` says "Object::External".
//
// Each body below is a member, defined in the `namespace Video` block at the
// bottom (B34_BRIEFS A.2).
//
// One runtime-internal binding retires:
// `RT_EXPORT_ONCE(ExtDtorFn, "??1External@Object@@UEAA@XZ")(self)` — the base
// destructor is the destructor CHAIN now, which is what retail's tail-JMP to
// 0x1800bc950 at the end of 0x1800f4650 is (spelling 3).
//
// Serialize: build the {stream, readerWriter} pair, serialize the four
// ExternalPtrs (four 748-byte instantiations, object/external_ptr_serialize.cpp),
// then if any of them is non-null and we are reading, PlatformCreate =
// `_data->vtable[1](_data)`; finally delete the reader/writer.
// ============================================================================
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>

#include "video/Device.h"   // ENDGAME B.3.9 VID: Video::Device::_singleton
#include "video/CompanionData.h"   // the typed *Data companions

#include "io/Serializer.h"
#include "io/Stream.h"      // IO::Stream, IO::kReading
#include "object/External.h"
#include "video/VideoProgram.h"
#include "video/VertexLayout.h"   // B.4 P2 X: T::GetTypeInfo() for Object::SerializeExternal<T>
#include "video/ShaderProgram.h"   // B.4 P2 X: T::GetTypeInfo() for Object::SerializeExternal<T>
#include "video/RenderState.h"   // B.4 P2 X: T::GetTypeInfo() for Object::SerializeExternal<T>

namespace {

using Video::VideoProgram;

}  // namespace

// ============================================================================
// THE MEMBER BODIES — `Video::VideoProgram::*`. At `namespace Video` scope and
// AFTER the anonymous namespace (B34_BRIEFS A.2).
// ============================================================================
namespace Video {

// slot 1, TypeId 0x18004ec40, slot 2 0x18004ec30: DECLARE_TYPE's over the
// class's own record (video_type_records.cpp; ENDGAME B.3.9 TI-REST).
// slot 3 — 0x1800f46a0 is `MOV EAX,1 ; RET`.
int32_t VideoProgram::GetFileVersion() const { return 1; }

// slot 8 — 0x18004edf0: `TEST RCX,RCX / JZ` then the ctor. `this` is never null
// here (the factory's null test is CreateInPlaceObject's). `virtual`, not
// `override`: this branch introduces the slot (B5-16). `::new` because the class
// declares its own (unported) placement operator new.
Object::External* VideoProgram::Construct() {
    return ::new (this) VideoProgram();
}

// slot 6 — 0x18004edd0: slot 0 with flags 0 (destroy in place), then the ctor.
Object::External* VideoProgram::Reconstruct() {
    this->~VideoProgram();
    return ::new (this) VideoProgram();
}
// slot 7 — 0x18004ee00: `XOR EDX,EDX ; JMP [RAX]` — slot 0, flags 0.
void VideoProgram::Destruct() { this->~VideoProgram(); }

// ??1VideoProgram@Video@@UEAA@XZ (0x1800f4650), read as disassembly:
//   1800f4666  LEA RAX,[0x180157a70] / MOV [RCX],RAX     the vtable stamp
//   1800f4670  MOV RCX,[RCX+0x40] / TEST / JE            PlatformDestruct, inlined
//   1800f4681  CALL [RAX]  with EDX=1                      _data slot 0, flags=1
//   ...        tail-JMP ~External (0x1800bc950)
// The stamp is the compiler's (entry to this destructor), the tail-JMP the
// destructor chain. The four fields are trivially destructible `ExternalPtr<T>`s
// and retail does not release them here either.
VideoProgram::~VideoProgram() {
    this->Video::VideoProgram::PlatformDestruct();
}

// ?CreateNewObject@VideoProgram@Video@@SAPEAVBase@Object@@XZ  0x18004ed40:
// MemAlloc(0x48, 8), null branch, the ctor (B39_RULES D2).
Object::Base* VideoProgram::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(VideoProgram), 8);
    if (p == nullptr) return nullptr;
    return ::new (p) VideoProgram();
}
// ?CreateInPlaceObject@VideoProgram@Video@@SAPEAVBase@Object@@PEAX@Z  0x18004edc0
Object::Base* VideoProgram::CreateInPlaceObject(void* storage) {
    return storage ? ::new (storage) VideoProgram() : nullptr;
}

// The four getters, 0x18004ee10 .. 0x18004ee40, each `MOV RAX,[RCX+off]` into the
// caller's return slot. On why none of them has a door, and what a door would have
// to do about the hidden slot, see the header.
Object::ExternalPtr<VertexLayout>  VideoProgram::GetVertexLayout() const  { return _vertexLayout; }
Object::ExternalPtr<VertexProgram> VideoProgram::GetVertexProgram() const { return _vertexProgram; }
Object::ExternalPtr<PixelProgram>  VideoProgram::GetPixelProgram() const  { return _pixelProgram; }
Object::ExternalPtr<RenderState>   VideoProgram::GetRenderState() const   { return _renderState; }
// 0x18004ee50 — `MOV RAX,[RCX+0x40]; RET`, no return slot.
const void* VideoProgram::GetData() const { return _data; }

// ?PlatformConstruct@VideoProgram@Video@@AEAAXXZ (0x1800f47a0), also the tail of
// the ctor and of Construct. DeviceData slot 18 (+0x90).
void VideoProgram::PlatformConstruct() {
    Device* device = Device::_singleton;
    DeviceData* dd = device ? device->deviceData : nullptr;
    if (dd == nullptr) { _data = nullptr; return; }
    _data = dd->CreateVideoProgramData(this);
}
// ?PlatformCreate@VideoProgram@Video@@AEAAXXZ (0x1800f4810): the companion's slot 1.
// Its bool result is dropped, as retail's tail call drops it.
void VideoProgram::PlatformCreate() {
    _data->Slot1();
}
// ?PlatformDestruct@VideoProgram@Video@@AEAAXXZ (0x1800f47f0): slot 0, flags 1.
// B5 PILOT (B5_RULES D10): retail's slot-0 call with flags 1 is `delete` -- the
// virtual destructor, then the base's class-scope operator delete (MemFree).
void VideoProgram::PlatformDestruct() {
    if (_data == nullptr) return;
    delete _data;
}

// slot 5 — ?Serialize@VideoProgram@Video@@UEAA_NAEAVStream@IO@@@Z (0x1800f46b0).
//
// The four per-T `ExternalPtr<T>::Serialize` instantiations are reached by NAME;
// each is its own 748-byte body in retail and ours are ported in
// object/external_ptr_serialize.cpp. The ORDER is the field order and the four
// are NOT interchangeable: each one loads its own `_typeInfo`, so swapping two
// would look up a VertexProgram resource under VertexLayout's type id.
bool VideoProgram::Serialize(IO::Stream& streamv) {
    IO::Stream& stream = streamv;
    IO::Serializer ser(stream, 0);

    // The four ExternalPtr<T>::Serialize instantiations, as members (B.4 P2 X).
    _vertexLayout.Serialize(ser);    // 0x180055090
    _vertexProgram.Serialize(ser);   // 0x1800542a0
    _pixelProgram.Serialize(ser);    // 0x180055380
    _renderState.Serialize(ser);     // 0x180055670

    if ((_vertexLayout.Get() || _vertexProgram.Get() || _pixelProgram.Get() || _renderState.Get())
        && stream.GetDirection() == IO::kReading) {
        this->Video::VideoProgram::PlatformCreate();   // retail derefs _data unguarded
    }
    return true;   // ~Serializer: the ReaderWriter's slot 0, flags 1
}

}  // namespace Video
