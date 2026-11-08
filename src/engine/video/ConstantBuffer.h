#pragma once
// ============================================================================
// src/engine/video/ConstantBuffer.h — `Video::ConstantBuffer`, the real class
// ENDGAME B.3.5 block D1. RULE B5-4, B35_RULES section 13 item 4.
// ============================================================================
//
// WHY THIS FILE EXISTS. Until B.3.5 there were FOUR partial views of this one
// 0x48-byte class, in four files, and none of them was a class:
//
//   src/engine/video/Context.h:203   `RtVideo::ConstantBufferOpaque` — 0x48
//                                        undifferentiated bytes, held by value
//                                        SEVEN TIMES inside `Video::Context`
//   src/engine/video/CompanionData.h:70  `RtVideo::ConstantBuffer` — head[0x38]
//                                        + device@+0x38 + platformData@+0x40
//   src/engine/video/constant_buffer.cpp   ten `constexpr size_t k*` offsets
//   src/render/dx11/buffers.cpp:230            `ConstantBufferOwner` — the ONLY view
//                                        with names for +0x14..+0x20, read from
//                                        the shipped VideoDX11 plugin's side
//
// This is the ONE definition (B4b-1 / B35_RULES 1.3). `CompanionData.h` now
// includes this header and keeps a `RtVideo::ConstantBuffer` alias so
// `src/engine/video/companion_data.cpp` — block A's file — compiles unchanged;
// `Context.h` keeps a `RtVideo::ConstantBufferOpaque` alias for the same reason.
// dx11's `ConstantBufferOwner` is block D2's file (`buffers.cpp`) and is left
// alone; it is listed for the closing pass.
//
// ---------------------------------------------------------------------------
// THE INTERIOR, WHICH WAS "DELIBERATELY OPAQUE" AND NOW MOSTLY IS NOT
// ---------------------------------------------------------------------------
// B35_RULES section 13 item 4 records this class's interior as the block's gap
// to close: "`ConstantBufferOpaque` is `uint8_t[0x48]`; `CompanionData.h:70`
// names only +0x38 and +0x40 … anything between +0x41 and +0x47 is unproven."
//
// Closed this pass from RETAIL BYTES. Every field below carries the instrument
// that names it, and TWO INDEPENDENT SOURCES agree on +0x14 .. +0x38: retail's
// own one-line accessors (read as disassembly this pass, addresses in the
// comments) and dx11's `ConstantBufferOwner`, which was recovered from the
// opposite side of the seam in an earlier wave and never compared with this one
// until now. They agree on every field they share.
//
//   off   field            proven by                                  retail
//   ----  ---------------  -----------------------------------------  ----------
//   0x00  _prefix          ?GetPrefix@ returns `this`                 0x1800182e0
//                          (`MOV RAX,RCX / RET` — so the String is at +0x00)
//   0x08  _typeName        ?GetTypeName@ returns `this + 8`           0x1800182f0
//   0x10  _ordinalIndex    ?GetOrdinalIndex@ `MOV EAX,[RCX+0x10]`     0x1800182d0
//   0x14  _uploadIndex     ?GetUploadIndex@ `MOV EAX,[RCX+0x14]`      0x1800182c0
//                          == dx11's `slotIndex`, the D3D11 StartSlot
//   0x18  _bindToVertex    dx11 `ConstantBufferOwner::bindToVertex`; the
//                          ctor writes it as a BYTE (`MOV [RCX+0x18],AL`)
//   0x19  _banked          dx11 `ConstantBufferOwner::banked`; written by
//                          ?BeginConstants@ (0x1800eedba) and by nothing else
//   0x1a  _pad1a[2]        NEVER WRITTEN by any body read this pass
//   0x1c  _registerCount   ?AddConstant@ ADDS the declaration size into it
//                          (0x1800eecf0); dx11: `<<4` is the total byte size
//   0x20  _bankStride      dx11 `ConstantBufferOwner::bankStride`; the only
//                          non-zero value the engine passes is 0x40, for tc_
//   0x24  _pad24[4]        NOT written by the ctor (its `MOV qword
//                          [RCX+0x1c],RAX` stops at +0x23) and by nothing else
//   0x28  _constants       `Collection::Array<ConstantInfo>` — data +0x28,
//                          _size +0x30, _capacity +0x34. ?GetConstant@
//                          (0x1800182b0) loads +0x28 and indexes `i*0x18`;
//                          ?GetConstantCount@ (0x1800182a0) reads +0x30;
//                          0x180053510 (the resize) writes +0x34.
//   0x38  _device          the ctor's second parameter (0x1800eee26)
//   0x40  _platformData    `Video::ConstantBufferData*` from the DeviceData
//                          vtable slot 11 / +0x58 (0x1800eee3c) — and it is a
//                          QWORD, so it runs to +0x47 and the object ENDS there
//
// THE SIZE, AND A CORRECTION TO B35_RULES SECTION 13 ITEM 4. 0x48 is not read off
// an allocation — nothing allocates a bare `ConstantBuffer`; all seven live
// instances are embedded in `Video::Context`. It is the STRIDE between those
// seven: `Context.h`'s pair at +0x298/+0x2e0 and the five at +0x1f78 / +0x1fc0 /
// +0x2008 / +0x2050 / +0x2098 are 0x48 apart, and `?AssignConstants@`
// (0x1800ef3d0) walks exactly those offsets. So 0x48 IS pinned — by the holder,
// not by the class.
//
// Section 13 item 4 says "the ctor's last write is +0x40, so anything between
// +0x41 and +0x47 is unproven". That reads as seven unaccounted bytes and there
// are none: the ctor's last write is `MOV [RBX+0x40],RAX`, a QWORD, so it covers
// +0x40..+0x47 and the 0x48 stride is exactly consumed. The bytes that really are
// unproven are +0x1a..+0x1b and +0x24..+0x27, and each is spelled as explicit
// named padding above so a later reader can see which is which.
//
// ---------------------------------------------------------------------------
// TRIVIALLY COPYABLE AND TRIVIALLY DEFAULT-CONSTRUCTIBLE — RULE B5-4
// ---------------------------------------------------------------------------
// `Video::Context` holds seven of these BY VALUE and has NO real constructor
// (B4b-2: `??0Context` takes a `Device*`; the ctor doors stay). If this class
// gained a user-provided default constructor, destructor, copy constructor or
// copy assignment, `Context` would acquire the matching implicit member and
// stop being the flat block every `memcpy`, `memset` and `reinterpret_cast` in
// `src/engine/video/` and `src/render/dx11/` treats it as. Both properties are
// asserted at the bottom of this file, next to the size — which is what RULE
// B5-4 asks for.
//
// That is also why `_prefix` and `_typeName` are `void*` and not
// `System::String`, and `_constants` is a hand-rolled header and not
// `Collection::Array<ConstantInfo>`: BOTH of those real classes have
// user-provided destructors (`String.h:148`, and `Array<T>`'s), so either one
// as a member would make this class non-trivially-copyable and take `Context`
// with it. The fields are the same bytes; the comment on each says what the
// real type is. Collapsing them is owed to the class-static/container pass at
// the END of B.3, not to step 5.
//
// The two live bodies that touch these as real types are
// `src/engine/video/constant_buffer.cpp` (which reproduces retail's INLINED
// String assign/adopt/release, exactly as it did before this conversion) and
// `~ConstantBuffer` in `src/engine/video/context_lifecycle.cpp`, which is a
// registered DOOR and NOT a real destructor for the reason above.
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace System { class String; }

namespace Video {

class Context;
class Device;              // B.4 R-VIDEO: the typed _device / _platformData
class ConstantBufferData;

class ConstantBuffer {
public:
    // ------------------------------------------------------------------
    // `ConstantBuffer::ConstantInfo` — one declared shader constant.
    //
    // The name is RETAIL's, not ours: `?GetConstant@ConstantBuffer@Video@@
    // QEBAAEBUConstantInfo@12@H@Z` spells the return type `AEBU`, so
    // `ConstantInfo` is a nested STRUCT of `ConstantBuffer`. The layout is
    // pinned three times over — `?AddConstant@` writes +0x00/+0x08/+0x10 and
    // zeroes +0x14, `?PlatformCreate@` walks +0x10/+0x14, and
    // `~ConstantBuffer` destroys the two Strings with stride 0x18 — and a
    // fourth time by `?GetConstant@`'s `LEA RDX,[RAX+RAX*2] / LEA RAX,[RAX +
    // RDX*8]`, i.e. index * 0x18 (0x1800182b0, read this pass).
    //
    // `src/engine/video/constant_buffer.cpp` called this `Entry`; that local
    // struct is gone and this is the one definition.
    // ------------------------------------------------------------------
    struct ConstantInfo {
        void*   name;      // +0x00  System::String data pointer ("texelSize")
        void*   typeName;  // +0x08  System::String data pointer ("float2")
        int32_t size;      // +0x10  size in float4 REGISTERS — a float4x4 is 4
        int32_t offset;    // +0x14  assigned by EndConstants / PlatformCreate
    };

    // ---- the ported members -------------------------------------------
    // Bodies in `src/engine/video/constant_buffer.cpp`; the registered doors
    // there are one-line QUALIFIED non-virtual delegations onto these.
    // B.4 (R-VIDEO), B6: the two String parameters are the
    // `const System::String&` the mangled names spell (`AEBVString@System@@0…`).
    void BeginConstants(const System::String& typeName, const System::String& prefix,
                        int32_t ordinalIndex, int32_t uploadIndex,
                        bool bindToVertex, bool banked,
                        int32_t bankStride);                  // 0x1800eed10
    void AddConstant(const System::String& name, const System::String& typeName,
                     int32_t size);                           // 0x1800eec40
    void EndConstants(int32_t unread);                        // 0x1800eebf0
    bool PlatformCreate();                                    // 0x1800ef010
    // 0x1800eef90. Ported this pass: `?AssignConstants@Context@Video@@` builds
    // the whole seven-buffer slot chain out of it, and until B.3.5 it existed
    // in the tree only as `constant_buffer.cpp`'s file-local `NextSlotAfter`,
    // unregistered under any door.
    int32_t GetNextUploadIndex() const;

    // ---- exported, NOT ported -----------------------------------------
    // Declared so no later block edits this header (BLOCK_BRIEF "the shape").
    // Nothing in the tree calls any of these AS A MEMBER; `Bind`'s registered
    // door has a live body, in block A's `companion_data.cpp`, and is the one
    // row here that is owed a move rather than a port (see below).
    //
    // `?Bind@ConstantBuffer@Video@@QEAAXAEAVContext@2@PEAXHH@Z` (0x1800eef70)
    // is three instructions — `MOV RCX,[RCX+0x40] / MOV R10,[RCX] / JMP
    // [R10+0x10]`, i.e. `_platformData->Bind(...)` through
    // `Video::ConstantBufferData` slot 2. Its body lives in
    // `src/engine/video/companion_data.cpp` as `Video_ConstantBuffer_Bind`,
    // which is block A's file and OUTSIDE this block's file set, so the door
    // stays exactly where it is and this declaration is left undefined. Moving
    // it is one line for the closing pass.
    void Bind(Context& context, void* data, int32_t a, int32_t b);  // 0x1800eef70 — door in companion_data.cpp

    int32_t             GetConstantCount() const;             // 0x1800182a0 — not ported
    const ConstantInfo& GetConstant(int32_t index) const;     // 0x1800182b0 — not ported
    int32_t             GetUploadIndex() const;               // 0x1800182c0 — not ported
    int32_t             GetOrdinalIndex() const;              // 0x1800182d0 — not ported
    const void*         GetPrefix() const;                    // 0x1800182e0 — not ported
    const void*         GetTypeName() const;                  // 0x1800182f0 — not ported
    void                PlatformConstruct();                  // 0x1800eefb0 — not ported (AEAA, private in retail)
    void                PlatformDestruct();                   // 0x1800eeff0 — not ported (AEAA, private in retail)

    // `??0ConstantBuffer@Video@@QEAA@PEAVDevice@1@@Z` (0x1800eedf0),
    // `??0…@AEBV01@@Z` (0x180018300), `??1…` (0x1800eeee0) and `??4…`
    // (0x1800183a0) are REAL EXPORTED BODIES and they stay registered DOORS —
    // the ctor because its FIRST act is to write fields a constructor cannot
    // write before the object exists as a `ConstantBuffer` (it is run on raw
    // MemAlloc'd bytes, seven times inlined inside the Context ctor), and the
    // other three because a user-provided copy ctor / copy assign / destructor
    // would destroy `std::is_trivially_copyable` and take `Video::Context` with
    // it (RULE B5-4, see the banner). Declaring them here without defining them
    // would ALSO destroy triviality, so they are named in this comment and
    // NOT declared. Their doors:
    //   ??0(Device*)  src/engine/video/context_ctor.cpp   (inlined, x7)
    //   ??1           src/engine/video/context_lifecycle.cpp
    //   ??0(copy) / ??4   unported, no caller in the tree

protected:
    // RULE B5-4 / B4b-3: concrete in the non-polymorphic sense, so that
    // `Video::Context` can hold seven by value, but not freely constructible.
    // `= default` on the FIRST declaration, so the default constructor stays
    // TRIVIAL — `Video::Context` must remain trivially default-constructible
    // and trivially copyable, and both are asserted below.
    ConstantBuffer() = default;
    friend class Context;

public:
    // ---- the 0x48 bytes ------------------------------------------------
    // Public because `constant_buffer.cpp`'s ported bodies and the two
    // registered lifecycle doors in `context_lifecycle.cpp` read and write
    // them, and because `src/render/dx11/` reads +0x14..+0x38 from the other side of the
    // seam. Same posture `Object::External` takes with `_path` / `_refs`.
    void*    _prefix;          // +0x00  System::String data ptr — "pc_"
    void*    _typeName;        // +0x08  System::String data ptr — "PixelConstants"
    int32_t  _ordinalIndex;    // +0x10  ctor: -1
    int32_t  _uploadIndex;     // +0x14  ctor: -1 (the D3D11 StartSlot)
    uint8_t  _bindToVertex;    // +0x18  0 -> PSSetConstantBuffers, else VSSet…
    uint8_t  _banked;          // +0x19  0 -> one buffer, else a growing ladder
    uint8_t  _pad1a[2];        // +0x1a  UNPROVEN — written by nothing
    int32_t  _registerCount;   // +0x1c  running total, in float4 registers
    int32_t  _bankStride;      // +0x20  bytes per bank; 0x40 for tc_ only
    uint8_t  _pad24[4];    // +0x24  UNPROVEN — the ctor's qword stops at +0x23

    // `Collection::Array<ConstantInfo>`, spelled out for the triviality reason
    // in the banner. +0x28 / +0x30 / +0x34.
    ConstantInfo* _constantData;      // +0x28
    int32_t       _constantCount;     // +0x30
    int32_t       _constantCapacity;  // +0x34

    Device*             _device;        // +0x38  (B.4 R-VIDEO, H4)
    ConstantBufferData* _platformData;  // +0x40  ends AT 0x48
};

static_assert(sizeof(ConstantBuffer) == 0x48,
              "Video::ConstantBuffer is 0x48 bytes — the STRIDE between the "
              "seven instances embedded in Video::Context (0x2e0 - 0x298, and "
              "the five at 0x1f78/0x1fc0/0x2008/0x2050/0x2098)");
static_assert(offsetof(ConstantBuffer, _pad1a) == 0x1A, "ConstantBuffer::_pad1a");
static_assert(offsetof(ConstantBuffer, _pad24) == 0x24, "ConstantBuffer::_pad24");
static_assert(alignof(ConstantBuffer) == 8, "Video::ConstantBuffer is qword-aligned");

// RULE B5-4, the properties `Video::Context` depends on.
static_assert(std::is_trivially_copyable<ConstantBuffer>::value,
              "RULE B5-4: Video::Context holds seven ConstantBuffers by value "
              "and is memcpy'd and memset as a flat block");
static_assert(std::is_trivially_destructible<ConstantBuffer>::value,
              "RULE B5-4: ??1ConstantBuffer@Video@@ stays a registered DOOR "
              "(context_lifecycle.cpp); a real destructor here would make "
              "Video::Context non-trivially-destructible too");
static_assert(std::is_standard_layout<ConstantBuffer>::value,
              "every offsetof below, and dx11's reads of +0x14..+0x38, depend "
              "on standard layout");
// `std::is_trivially_default_constructible` is NOT asserted here, and the reason
// is a trait subtlety worth recording rather than working around: the default
// constructor is `= default` (so it IS trivial) but `protected` (B4b-3), and the
// trait also requires the constructor to be ACCESSIBLE from where the trait is
// evaluated — at namespace scope it is not, so the trait reads false for a class
// that is in fact trivially default-constructible. `Video::Context`, the one
// holder, reaches it as a `friend` and gets the trivial construction. Making the
// constructor public to satisfy a `static_assert` would be the tail wagging the
// dog.

static_assert(offsetof(ConstantBuffer, _prefix)          == 0x00, "_prefix @ +0x00 (?GetPrefix@ 0x1800182e0)");
static_assert(offsetof(ConstantBuffer, _typeName)        == 0x08, "_typeName @ +0x08 (?GetTypeName@ 0x1800182f0)");
static_assert(offsetof(ConstantBuffer, _ordinalIndex)    == 0x10, "_ordinalIndex @ +0x10 (?GetOrdinalIndex@ 0x1800182d0)");
static_assert(offsetof(ConstantBuffer, _uploadIndex)     == 0x14, "_uploadIndex @ +0x14 (?GetUploadIndex@ 0x1800182c0)");
static_assert(offsetof(ConstantBuffer, _bindToVertex)    == 0x18, "_bindToVertex @ +0x18 (ctor 0x1800eee17, byte)");
static_assert(offsetof(ConstantBuffer, _banked)          == 0x19, "_banked @ +0x19 (?BeginConstants@ 0x1800eedba)");
static_assert(offsetof(ConstantBuffer, _registerCount)   == 0x1c, "_registerCount @ +0x1c (?AddConstant@ 0x1800eecf0)");
static_assert(offsetof(ConstantBuffer, _bankStride)      == 0x20, "_bankStride @ +0x20 (?BeginConstants@ stack arg 4)");
static_assert(offsetof(ConstantBuffer, _constantData)    == 0x28, "_constantData @ +0x28 (?GetConstant@ 0x1800182b7)");
static_assert(offsetof(ConstantBuffer, _constantCount)   == 0x30, "_constantCount @ +0x30 (?GetConstantCount@ 0x1800182a0)");
static_assert(offsetof(ConstantBuffer, _constantCapacity)== 0x34, "_constantCapacity @ +0x34 (the resize 0x180053510)");
static_assert(offsetof(ConstantBuffer, _device)          == 0x38, "_device @ +0x38 (ctor 0x1800eee26)");
static_assert(offsetof(ConstantBuffer, _platformData)    == 0x40, "_platformData @ +0x40 (ctor 0x1800eee3f, DeviceData +0x58)");

static_assert(sizeof(ConstantBuffer::ConstantInfo) == 0x18,
              "ConstantInfo is 0x18 bytes (?GetConstant@'s index * 0x18)");
static_assert(offsetof(ConstantBuffer::ConstantInfo, name)     == 0x00, "ConstantInfo::name @ +0x00");
static_assert(offsetof(ConstantBuffer::ConstantInfo, typeName) == 0x08, "ConstantInfo::typeName @ +0x08");
static_assert(offsetof(ConstantBuffer::ConstantInfo, size)     == 0x10, "ConstantInfo::size @ +0x10");
static_assert(offsetof(ConstantBuffer::ConstantInfo, offset)   == 0x14, "ConstantInfo::offset @ +0x14");

}  // namespace Video

// ---- the Collection::Array<ConstantInfo> instantiations and ~ConstantBuffer ----
// B.4 (R-VIDEO): the array is `ConstantBuffer`'s three named fields
// `{_constantData, _constantCount, _constantCapacity}`, by reference. They are
// template instances (unexported, reached by address in retail) and become
// `Collection::Array<ConstantInfo>` members when the triple becomes one (H5:
// an `Array` member would give the class a destructor).
// src/engine/video/context_lifecycle.cpp
void RtVideo_ConstEntryArrayDestroy(Video::ConstantBuffer::ConstantInfo* data,
                                    int32_t count);                         // 0x18001cfc0
void RtVideo_ConstEntryArrayDtor(Video::ConstantBuffer::ConstantInfo*& data,
                                 int32_t& size, int32_t& capacity);        // 0x18001cab0
// ??1ConstantBuffer@Video@@QEAA@XZ — a door, not `~ConstantBuffer()` (RULE B5-4).
void Video_ConstantBuffer_Dtor(Video::ConstantBuffer* self);                // 0x1800eeee0
// src/engine/video/constant_buffer.cpp
void RtVideo_ConstEntryArraySetSize(Video::ConstantBuffer::ConstantInfo*& data,
                                    int32_t& size, int32_t& capacity, int32_t newSize,
                                    const Video::ConstantBuffer::ConstantInfo& fill);  // 0x180053510
