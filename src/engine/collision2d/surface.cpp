// ============================================================================
// src/engine/collision2d/surface.cpp — Collision2D::Surface, the whole class
// ============================================================================
//
// Vtable batch 8. ??_7Surface@Collision2D@@6B@ (retail), nine slots:
//
//   0  deleting dtor        0x180064ea0  Surface::DeletingDtor (Surface.h)
//   1  TypeName             0x180064a70  here
//   2  Type                 0x180064a50  here
//   3  GetFileVersion       0x18010ca30  here  (returns 1)
//   4  Compile              -- NOT a Surface slot: retail's table holds
//                              External::Compile (0x1800066e0) there
//   5  Serialize            0x18010ca40  here
//   6  Reconstruct          0x180064c70  here
//   7  Destruct             0x180064d40  here
//   8  Construct            0x180064ce0  here
//
// plus ~Surface (0x18010ca00) here.
//
// ENDGAME B.3.9 ENG (B39_RULES D1/D2): the file-local POD became the real class
// `Collision2D::Surface` (src/engine/collision2d/Surface.h). Its constructor
// holds the factory rows' stores, its DeletingDtor the retired RT_DELDTOR row,
// and the slot bodies are its members: every live Surface wears the COMPILER's
// table. The eight slot doors, the ??1 door, `ConstructBody` and `CallSlot0`
// went with the minted `??_7Surface@Collision2D@@6B@` row and the FACTREST
// pair (rt_factories_collision2d.{cpp,inc}).
// ============================================================================
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>

#include "io/Serializer.h"
#include "io/Stream.h"       // IO::Stream::_direction (B.4 R-SYS)
#include "collision2d/Surface.h"

namespace {

using rt::io::ReaderWriter;
using rt::io::Serializer;
using rt::io::Stream;

}  // namespace

namespace Collision2D {

// TypeId 0x180064a60 / [2] 0x180064a50 / [1] 0x180064a70: DECLARE_TYPE's
// loads over the class's own record (collision2d_type_records.cpp; ENDGAME
// B.3.9 TI-REST).
// [3] 0x18010ca30: `MOV EAX,1 ; RET`.
int32_t Surface::GetFileVersion() const { return 1; }

// [8] 0x180064ce0 -- the inlined constructor, i.e. placement construction.
// (Retail's null test sat in the retired door; a virtual call always has a
// `this`.) `::new`: the class declares its own placement new.
Object::External* Surface::Construct() {
    return ::new (this) Surface();
}
// [6] 0x180064c70: slot0(this, 0) dispatched VIRTUALLY, then the same stores.
Object::External* Surface::Reconstruct() {
    this->~Surface();
    return ::new (this) Surface();
}
// [7] 0x180064d40: `MOV RAX,[RCX] ; XOR EDX,EDX ; JMP [RAX]`.
void Surface::Destruct() { this->~Surface(); }

// ?CreateNewObject@Surface@Collision2D@@SAPEAVBase@Object@@XZ  0x180064b60
// The FACTREST row this replaces: MemAlloc of 0x30 bytes, align 8 (the locked
// `?MemAlloc@MemoryAllocator@System@@` Object::detail::InternalMemAlloc
// reaches); retail's null branch; then the inlined constructor.
Object::Base* Surface::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(Surface), 8);
    if (p == nullptr) return nullptr;
    return ::new (p) Surface();
}
// ?CreateInPlaceObject@Surface@Collision2D@@SAPEAVBase@Object@@PEAX@Z  0x180064c10
Object::Base* Surface::CreateInPlaceObject(void* place) {
    if (place == nullptr) return nullptr;
    return ::new (place) Surface();
}

// ??1Surface@Collision2D@@UEAA@XZ (0x18010ca00): restamp own vtable, chain to
// ~External. No owned resource of its own -- three plain uint32 fields. The
// restamp and the chain are the compiler's.
Surface::~Surface() {}

// ?Serialize@Surface@Collision2D@@UEAA_NAEAVStream@IO@@@Z (0x18010ca40)
bool Surface::Serialize(IO::Stream& stream) {
    // B.4 R-SYS: retail's `??0Serializer@IO@@` (0x1800b9960): the stream, a
    // null ReaderWriter, then the encoding's CreateReader (+0x18) / CreateWriter
    // (+0x20) by direction, version 0; `~Serializer` (0x1800b99b0) deletes the
    // ReaderWriter at scope exit, where the hand-written `delete` stood.
    IO::Serializer ser(stream, 0);

    // SerializeUInt is +0x98.
    IO::ReaderWriter* rwc = ser._readerWriter;
    rwc->SerializeUInt(_reportOnly, nullptr);
    rwc->SerializeUInt(_color, nullptr);
    rwc->SerializeUInt(_mask, nullptr);

    return true;   // ~Serializer deletes the ReaderWriter
}

}  // namespace Collision2D

// B3R (registry teardown, W1 RT-Core): every row this function registered had
// no reader by name or by address, so they went. It registers nothing and
// stays defined only because standalone/resolve_standalone.cpp's
// RuntimePortInit() still calls it (the coordinator removes the call).
