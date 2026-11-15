// ============================================================================
// src/engine/video/imagebuffer_vtable.cpp — Wave 18 lane VDATA
// The seven slots of ??_7ImageBuffer@Video@@6B@ (DLL 0x180154fc0) this file owns
// ============================================================================
//
// ENDGAME B.3.5 block C. `Video::ImageBuffer` is a REAL C++ class
// (src/engine/video/ImageBuffer.h) and the bodies below are members of it.
//
// WHAT THIS FILE IS FOR, IN ONE PARAGRAPH.
//
// `??_7ImageBuffer@Video@@6B@` has ten slots. Three have their bodies elsewhere
// (slot 0 the scalar deleting dtor, slot 4 `Object::External::Compile`, slot 9
// `ImageBuffer::Create`); this file holds the bodies of slots 3 and 5 to 8, and
// slots 1 and 2 are DECLARE_TYPE's.
//
// | slot | symbol                                              | retail VA   |
// |------|-----------------------------------------------------|-------------|
// |  0   | `??_EImageBuffer@Video@@UEAAPEAXI@Z` (del-dtor)      | 0x18001ed20 | ~ImageBuffer (buffers.cpp)
// |  1   | `?TypeName@ImageBuffer@Video@@UEBAPEBDXZ`            | 0x18001e900 | DECLARE_TYPE (ImageBuffer.h)
// |  2   | `?Type@ImageBuffer@Video@@UEBAIXZ`                   | 0x18001e8e0 | DECLARE_TYPE (ImageBuffer.h)
// |  3   | `?GetFileVersion@ImageBuffer@Video@@UEBAHXZ`         | 0x1800f26e0 | **this file**
// |  4   | `?Compile@External@Object@@UEAA?AW4…@Z`             | 0x1800066e0 | already ours (object/external_class.cpp)
// |  5   | `?Serialize@ImageBuffer@Video@@UEAA_NAEAVStream@IO@@@Z` | 0x1800f26f0 | **this file**
// |  6   | `?Reconstruct@ImageBuffer@Video@@UEAAPEAVExternal@Object@@XZ` | 0x18001ea80 | **this file**
// |  7   | `?Destruct@ImageBuffer@Video@@UEAAXXZ`               | 0x18001eab0 | **this file**
// |  8   | `?Construct@ImageBuffer@Video@@UEAAPEAVExternal@Object@@XZ` | 0x18001eaa0 | **this file**
// |  9   | `?Create@ImageBuffer@Video@@UEAA_N…@Z`              | 0x1800f2680 | already ours (buffers.cpp)
//
// ALL SEVEN ARE EXPORTED BY NAME. Checked with `tools/pe_exports.py` against
// the shipped `Runtime-x64.dll` before a line was written — every one resolves
// at offset 0 of its own export, and none of the `AEAA`-means-unexported
// confusion applies
// (`reference_aeaa_is_private_not_unexported.md`).
//
// ----------------------------------------------------------------------------
// READ FROM THE DISASSEMBLY, NOT THE DECOMPILED C
// ----------------------------------------------------------------------------
// WAVE18_KICKOFF §4.7. Every body below is transcribed from the listing at its
// retail VA. Two of them are exactly the shape Ghidra's C output mangles:
// `Destruct` is a TAIL JUMP through a vtable slot (`JMP qword ptr [RAX]`) whose
// second argument is set by `XOR EDX,EDX` in the caller and which the C output
// prints as an argument-less call, and `Construct` is a bare `TEST RCX,RCX /
// JNZ <ctor>` whose fallthrough returns zero in EAX.
//
// ----------------------------------------------------------------------------
// WHAT THE B.3.5 CLASS CONVERSION CHANGED
// ----------------------------------------------------------------------------
//   * `Serialize` uses the REAL `IO::Serializer` (B.3.4 block 3) and its
//     `Serialize(T&, const char*)` overloads, so this file no longer carries its
//     own copy of the encoding-factory prologue (`MakeCodec`), the codec
//     teardown (`DestroyCodec`) or the two `rw_vtbl::k*` byte offsets. The
//     Serializer local's SCOPE is what places construction and destruction at
//     retail's positions — the prologue before the first emission, the teardown
//     AFTER `PlatformSerialize` — because moving either relative to another
//     stream operation is a save-format change (Serializer.h's banner).
//   * `PlatformSerialize` is a member of the class now (its body is
//     `imagebuffer_platform_serialize.cpp`, also this block's), and
//     `??0ImageBuffer` (ImageBuffer.h) and `?Serialize@String@System@@` are
//     plain C++ calls.
//   * The two dead `using Fn = …` typedefs in `Type` and `GetFileVersion`, left
//     over from a differential oracle that no longer exists, are gone with the
//     two `-Wunused-local-typedefs` warnings they carried.
//
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>

#include "video/ImageBuffer.h"
#include "io/Serializer.h"
#include "io/Stream.h"
#include "system/String.h"

// The holder over `Video::ImageBuffer`'s protected surface. `?PlatformSerialize@`
// is PRIVATE in retail (`AEAA`) and `protected` on the class, yet it is exported;
// this file has to CALL it from `Serialize`, and neither an `extern "C"` function
// nor an anonymous-namespace helper can name a protected member. B34_BRIEFS A.4:
// friend the holder rather than widen the access, so the header keeps saying what
// retail says. It has no fields and is never instantiated.
struct ImageBufferDoorAccess {
    static void PlatformSerialize(Video::ImageBuffer* p, IO::Serializer& ser) {
        p->Video::ImageBuffer::PlatformSerialize(ser);
    }
};

namespace {

// ---------------------------------------------------------------------------
// `?_typeInfo@ImageBuffer@Video@@0VBaseTypeInfo@Object@@A` — retail 0x1801fa548
// ---------------------------------------------------------------------------
// Slots 1 and 2 are one instruction each and BOTH read this one static:
//     0x18001e8e0  MOV EAX,dword ptr [0x1801fa548]   ; Type()     -> +0x00 typeId
//     0x18001e900  MOV RAX,qword ptr [0x1801fa550]   ; TypeName() -> +0x08 name
// 0x1801fa550 is not a second symbol: `tools/pe_exports.py` resolves it to
// `?_typeInfo@ImageBuffer@Video@@…` **+8**, i.e. `BaseTypeInfo::name`.
//
// ENDGAME B.3.9 TI-REST: that static is the class's own `DEFINE_TYPE` record
// (video_type_records.cpp); slots 1 and 2 and TypeId() are DECLARE_TYPE's.

// `??0ImageBuffer@Video@@QEAA@XZ` — retail 0x1800f2580; its body is `ImageBuffer()`
// (ImageBuffer.h), which calls `ImageBuffer::InitFields` (`buffers.cpp`).
// ENDGAME B.3.9 VS: the door, the by-address lookup and its function-local
// cache are GONE. Construct / Reconstruct run the real constructor in place,
// `::new (this) ImageBuffer()` -- retail's `JMP 0x1800f2580`, which stores
// ImageBuffer's own table exactly as the compiler's constructor does.

// (B3X W0: `CallSlot0`, which read slot 0 through the object's vptr, is gone.
// Every live ImageBuffer wears the compiler's table since ENDGAME B.3.9 VS, so
// `~T()` (slot 0, flags 0) -- unqualified, VIRTUAL -- reaches a derived class's own
// slot 0 (`Graphics::ReflectionImageBuffer`, `FrameCopyImageBuffer`,
// `GeneratedImageBuffer` inherit `Destruct`/`Reconstruct`) exactly as it did.)

// One enum field through the UNSIGNED slot (+0x98), which is the slot retail
// picks for the eight enum fields (see Serialize below). B.4 (R-VIDEO): through a
// `uint32_t` local and back instead of a `uint32_t&` view of the enum --
// value-identical on both directions (the writer reads the local; the reader
// fills it and the store puts the same four bytes in the field; a reader that
// writes nothing leaves the field's own value).
template <class Enum>
void SerializeUnsigned(IO::Serializer& ser, Enum& field) {
    uint32_t v = static_cast<uint32_t>(field);
    ser.Serialize(v, nullptr);
    field = static_cast<Enum>(v);
}

}  // namespace

// ############################################################################
// The members
// ############################################################################

namespace Video {

// ============================================================================
// slot 2 — uint Video::ImageBuffer::Type() const
// ?Type@ImageBuffer@Video@@UEBAIXZ                        retail 0x18001e8e0
//
//     MOV EAX,dword ptr [0x1801fa548]
//     RET
//
// A 4-byte load at offset 0 of the BaseTypeInfo, not a pointer load — the same
// reading mainloop_video.cpp §4 pins for all eleven of these globals.
// ============================================================================
// (DECLARE_TYPE's `Type()` / `TypeId()`; retail never touches RCX.)

// ============================================================================
// slot 1 — const char* Video::ImageBuffer::TypeName() const
// ?TypeName@ImageBuffer@Video@@UEBAPEBDXZ                 retail 0x18001e900
//
//     MOV RAX,qword ptr [0x1801fa550]      ; _typeInfo + 8  ==  BaseTypeInfo::name
//     RET
// ============================================================================
// (DECLARE_TYPE's `TypeName()`. The two literals, COPIED from B35_SLOTS section
// 3 (0x18001e8c0 / 0x18001e8d0), are DEFINE_TYPE's in video_type_records.cpp.)

// ============================================================================
// slot 3 — int Video::ImageBuffer::GetFileVersion() const
// ?GetFileVersion@ImageBuffer@Video@@UEBAHXZ              retail 0x1800f26e0
//
//     MOV EAX,0x7
//     RET
//
// The compiled-external file format version for this class. It is a LITERAL in
// the shipped code, so the constant is the whole port and getting it wrong
// silently changes which branch a loader takes on every image in the game.
// ============================================================================
int32_t ImageBuffer::GetFileVersion() const { return 7; }

// ============================================================================
// slot 7 — void Video::ImageBuffer::Destruct()
// ?Destruct@ImageBuffer@Video@@UEAAXXZ                    retail 0x18001eab0
//
//     MOV RAX,qword ptr [RCX]      ; the object's OWN vtable
//     XOR EDX,EDX                  ; flags = 0  ->  destroy, DO NOT free
//     JMP qword ptr [RAX]          ; slot 0, the scalar deleting destructor
//
// THREE instructions, and Ghidra's C prints the tail jump as an argument-less
// call — the `XOR EDX,EDX` that selects "destroy without freeing" is exactly
// the dropped argument `reference_ghidra_c_output_vs_disassembly.md` is about.
// Passing 1 here instead would free an object the caller still owns.
//
// `this->~ImageBuffer()` is that slot-0 call: a virtual destructor call, which
// for an ImageBuffer runs `??1ImageBuffer@Video@@UEAA@XZ` (0x1800f2610,
// buffers.cpp).
// ============================================================================
void ImageBuffer::Destruct() { this->~ImageBuffer(); }

// ============================================================================
// slot 8 — Object::External* Video::ImageBuffer::Construct()
// ?Construct@ImageBuffer@Video@@UEAAPEAVExternal@Object@@XZ   retail 0x18001eaa0
//
//     TEST RCX,RCX
//     JNZ  0x1800f2580             ; tail call ??0ImageBuffer@Video@@QEAA@XZ
//     XOR  EAX,EAX
//     RET
//
// The null test on `this` is MSVC's placement-new codegen: the source is
// `return new (this) ImageBuffer();`, and `operator new(size_t, void*)` is
// specified to return its pointer unchanged, so the compiler emits the
// null check it emits for every placement new. It looks redundant for a
// virtual — `this` cannot be null at a virtual call — and it is not
// reproduced: `this == nullptr` inside a member is a comparison GCC folds and
// warns about.
//
// The return value is `this`: the ctor at 0x1800f2580 ends `MOV RAX,RBX; RET`
// (read at 0x1800f25ff), and this body TAIL JUMPS to it, so RAX at return is
// the constructed object. The C++ constructor returns nothing, so the body
// returns `this` itself.
// ============================================================================
Object::External* ImageBuffer::Construct() {
    ::new (this) ImageBuffer();
    return this;
}

// ============================================================================
// slot 6 — Object::External* Video::ImageBuffer::Reconstruct()
// ?Reconstruct@ImageBuffer@Video@@UEAAPEAVExternal@Object@@XZ  retail 0x18001ea80
//
//     PUSH RBX / SUB RSP,0x20
//     MOV  RAX,qword ptr [RCX]     ; the object's OWN vtable
//     XOR  EDX,EDX                 ; flags = 0  ->  destroy, DO NOT free
//     MOV  RBX,RCX
//     CALL qword ptr [RAX]         ; slot 0, scalar deleting destructor
//     MOV  RCX,RBX
//     ADD RSP,0x20 / POP RBX
//     JMP  0x1800f2580             ; tail call the ctor
//
// Destroy in place, then re-construct in place, on the SAME storage. This is
// the device-lost recovery primitive: `Object::External`'s protocol for
// throwing away a GPU-side resource and rebuilding it without moving the
// object, which is why the address is stable across the reload.
//
// It is NOT `Destruct(); Construct();` written out — it reaches `vtable[0]`
// itself and tail-jumps the ctor directly — but it is behaviourally that, and
// it is written below as the two explicit steps so each one's own comment
// applies. The observable difference is nil: same two calls, same order, same
// arguments, same return.
// ============================================================================
Object::External* ImageBuffer::Reconstruct() {
    this->~ImageBuffer();
    ::new (this) ImageBuffer();
    return this;
}

// ============================================================================
// slot 5 — bool Video::ImageBuffer::Serialize(IO::Stream&)
// ?Serialize@ImageBuffer@Video@@UEAA_NAEAVStream@IO@@@Z   retail 0x1800f26f0
//
// The field order is the save-file layout for every image in the game, so the
// ORDER and the TYPE of each emission is the whole port. Getting either wrong
// is the silent save-desync this project has paid for repeatedly. Transcribed
// call by call from 0x1800f2741 .. 0x1800f281a:
//
//   0x1800f2741  [+0xa0] &this+0x20   int    width
//   0x1800f2756  [+0xa0] &this+0x24   int    height
//   0x1800f276b  [+0x98] &this+0x28   uint   dimension
//   0x1800f2780  [+0x98] &this+0x2c   uint   kind
//   0x1800f2795  [+0x98] &this+0x30   uint   format
//   0x1800f27aa  [+0x98] &this+0x34   uint   flags
//   0x1800f27bf  [+0x98] &this+0x38   uint   addressU
//   0x1800f27d4  [+0x98] &this+0x3c   uint   addressV
//   0x1800f27e9  [+0x98] &this+0x40   uint   addressW
//   0x1800f27fe  [+0x98] &this+0x44   uint   filter
//   0x1800f280d  CALL 0x1800c9930  (this+0x48, &ser)   System::String::Serialize
//   0x1800f281a  CALL 0x1800f3130  (this,     &ser)    ImageBuffer::PlatformSerialize
//   0x1800f2834  MOV AL,1                              always true, no failure path
//
// The third argument of every emitter is the field NAME and retail passes
// NULL (`XOR R8D,R8D` before each one); a text encoding would write those
// names into the file, so inventing them would change the format.
//
// TWO IN THE ORDER MATTER MORE THAN THE REST: width and height go through the
// **signed** slot (+0xa0) and the other eight through the **unsigned** slot
// (+0x98). On a binary encoding both are four bytes and the distinction is
// invisible; on any other it is not, and the slot number is what the shipped
// bytes say. With the real `IO::Serializer` the distinction is carried by the
// OVERLOAD chosen — `Serialize(int&)` reaches +0xa0 and `Serialize(unsigned
// int&)` reaches +0x98 — which is why the seven enum fields go through
// `SerializeUnsigned` (a `uint32_t` round trip), exactly as retail picks the
// other slot for the same address.
// ============================================================================
bool ImageBuffer::Serialize(IO::Stream& stream) {
    // LEFT (B.4 R-VIDEO): the slot's parameter is `IO::Stream&` in retail, but
    // `Object::External::Serialize` is declared `bool(void*)` across the whole
    // External family (R-OBJ's declaration).
    IO::Stream& s = stream;
    {
        // Version 0: retail zeroes R8D at 0x1800f2714 for the `CMP [RDX+8],R8D`
        // and never reloads it before the factory call at 0x1800f2725/0x1800f272a.
        IO::Serializer ser(s, 0);

        ser.Serialize(_width, nullptr);                                     // +0x20
        ser.Serialize(_height, nullptr);                                    // +0x24
        SerializeUnsigned(ser, _dimension);                                 // +0x28
        SerializeUnsigned(ser, _kind);                                      // +0x2c
        SerializeUnsigned(ser, _format);                                    // +0x30
        ser.Serialize(_flags, nullptr);                                     // +0x34
        SerializeUnsigned(ser, _addressU);                                  // +0x38
        SerializeUnsigned(ser, _addressV);                                  // +0x3c
        SerializeUnsigned(ser, _addressW);                                  // +0x40
        SerializeUnsigned(ser, _filter);                                    // +0x44

        // +0x48, a System::String (0x1800c9930).
        _sourceName.Serialize(ser);
        ImageBufferDoorAccess::PlatformSerialize(this, ser);
    }   // ~Serializer: `if (rw) rw->slot0(rw, 1)` at 0x1800f2820 .. 0x1800f2832
    return true;
}

}  // namespace Video
