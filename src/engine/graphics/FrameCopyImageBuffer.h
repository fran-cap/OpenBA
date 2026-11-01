#pragma once
// ============================================================================
// src/engine/graphics/FrameCopyImageBuffer.h — Graphics::FrameCopyImageBuffer
// ============================================================================
//
// 0x88 bytes. Derived from Video::ImageBuffer (0x68) — the ctor calls
// `??0ImageBuffer@Video@@QEAA@XZ` on `this` before touching anything of its
// own, and the dtor tail-jumps to `??1ImageBuffer@Video@@UEAA@XZ`.
//
// The SIZE is confirmed three independent ways, none of which is this header:
//   * `src/engine/save/saveload_draw.cpp:451` allocates exactly 0x88 for one
//     through `??2FrameCopyImageBuffer@Graphics@@SAPEAX_K@Z`;
//   * `src/game/Game/w2_takescreenshot.cpp:229` MemAllocs 0x88 for one,
//     transcribed from exe 0x140095da0;
//   * the ctor (Runtime 0x1800a3d10) writes +0x68, +0x70, +0x78 and +0x80 and
//     nothing past it.
//
// Every offset below was read from the DISASSEMBLY of the four bodies that
// touch them (ctor 0x1800a3d10, dtor 0x1800a3c80, Create 0x1800a3b70,
// Generate 0x1800a3b20), not from Ghidra's C — the decompiler's rendering of
// the ctor invents an `extraout_RAX` for the inlined `operator new` result and
// would have sent a reader looking for a field that is not there. See
// docs/history/runtime_port/WAVE23_LANE_GFXBODY.md §3.
//
// WHAT THE THREE OWNED POINTERS ARE FOR (from the four bodies, in order):
//   +0x70  a SECOND, plain Video::ImageBuffer (0x68, ctor-constructed). The
//          ctor immediately calls slot 1 of its platform companion, which
//          src/render/dx11/imagebufferdata.cpp:11 names "alias the DeviceData color
//          buffer, build an RTV" — i.e. this member IS the back buffer.
//   +0x78  a Video::RenderBuffer whose colour attachment is +0x70 (the ctor's
//          last act: `RenderBuffer::Create(+0x78, +0x70, nullptr)`).
//   +0x80  a Video::RenderBuffer whose colour attachment is `this` — created
//          by `Create` once the texture behind `this` exists.
// `Generate` then asks +0x80's platform companion to resolve/blit FROM +0x78,
// which is what makes this class "a copy of the frame".
//
// +0x68 is a plain int LATCH, not a flag: Generate compares it with its own
// `int` argument and only does the copy when they differ, then stores the
// argument. The ctor zeroes it. Both live callers pass 1
// (saveload_draw.cpp:479, w2_takescreenshot.cpp), so on those routes it fires
// once per object.
// ============================================================================

#include <cstdint>
#include <cstddef>

#include "../video/ImageBuffer.h"




// ----------------------------------------------------------------------------
// The two vtables the ctor and dtor stamp, as RETAIL addresses.
//
// `??_7FrameCopyImageBuffer@Graphics@@6B@` is 0x180154f00 — corroborated
// independently at src/engine/save/saveload_draw.cpp:83, which walked its
// slot 0x50 and found 0x1800a3b20 (= ?Generate@FrameCopyImageBuffer@).
// `??_7GeneratedImageBuffer@Graphics@@6B@` is 0x180154f60, the address the
// ctor stamps before its own and the dtor restores on the way out (the MSVC
// "hand the object back to my base class" idiom).
//
// CORRECTION (2026-09-12, vtable batch 6): that sentence is right about the
// mechanism and understates what the other table IS. `Graphics::
// GeneratedImageBuffer` is a REAL INTERMEDIATE CLASS with its own 0x70 layout,
// its own constructor (0x1800a3e70), destructor (0x1800a3e40), `?_typeInfo@`
// node and five vtable slots — all of them now in
// src/engine/graphics/GeneratedImageBuffer.h and
// src/engine/graphics/imagebuffer_derived.cpp. The field this header calls
// `_generation` at +0x68 is ITS member, inherited here, not FrameCopy's own,
// which is why `Graphics::ReflectionImageBuffer` has a field at the same offset
// doing the same job. The hierarchy is
// Video::ImageBuffer -> GeneratedImageBuffer -> {FrameCopy, Reflection}.
// ----------------------------------------------------------------------------
// ENDGAME B.3.9 block GFX: `kVtFrameCopyImageBuffer` (0x180154f00) and
// `kVtGeneratedImageBuffer` (0x180154f60) are gone with the minted tables they
// named; the real classes' ctors stamp the compiler's.


// ############################################################################
// THE REAL CLASS — `Graphics::FrameCopyImageBuffer`.
// ENDGAME B.3.5 block C. The POD above STAYS: `src/engine/graphics/
// wave23_lane_gfxbody.cpp` (block H1's) holds this class's ctor, dtor, `Create`
// and `Generate` and still spells it `RtGraphics::FrameCopyImageBuffer`.
// ############################################################################

#include "GeneratedImageBuffer.h"

namespace Graphics {

// ============================================================
// Graphics::FrameCopyImageBuffer — 0x88 bytes, TWELVE slots
// (`??_7FrameCopyImageBuffer@Graphics@@6B@` @ 0x180154f00, B35_SLOTS.md).
//
// Parent `Graphics::GeneratedImageBuffer`, from
// `?_ParentTypeName@FrameCopyImageBuffer@Graphics@@` (0x18001ef80) -> the
// literal "GeneratedImageBuffer", NO namespace — which is RULE B5-15's point:
// this branch's three `_TypeName`/`_ParentTypeName` strings are
// "FrameCopyImageBuffer", "GeneratedImageBuffer" and (on the parent)
// "Video::ImageBuffer", i.e. namespaced iff the parent is in another namespace,
// and a `#Parent` stringisation of the C++ base name would produce
// "Graphics::GeneratedImageBuffer" here and change the type-id CRC.
//
// *** WHICH SLOTS THIS CLASS OVERRIDES — AND B35_RULES 2.3 ITEM 5 IS WRONG
// *** ABOUT ONE OF THEM. The rules sheet says FrameCopy and Reflection "override
// 6,7,8,10,11 but INHERIT slot 9 (?Create@ImageBuffer@Video@@)". That is true of
// `ReflectionImageBuffer` and FALSE of this class: B35_SLOTS.md's own byte read
// puts `?Create@FrameCopyImageBuffer@Graphics@@…` (0x1800a3b70) at slot 9 here,
// and the shipped export table carries that symbol. `?Create@
// ReflectionImageBuffer@` does not exist. So this class overrides
// 1,2,3,5,6,7,8,9,10,11 and inherits only slot 4; Reflection overrides
// 1,2,3,5,6,7,8,10,11 and inherits 4 AND 9. Copied from B35_SLOTS, per the
// brief's "COPY from it; do not reconstruct".
//
// *** NOT CONSTRUCTIBLE IN B.3.5 *** and (until B3X W1) NO VIRTUAL DESTRUCTOR — the same two
// statements `Video::ImageBuffer` carries, for the same two reasons.
// ============================================================
class FrameCopyImageBuffer : public GeneratedImageBuffer {
public:
    // Slot 0's body is a `src/engine/object/deleting_dtor.cpp` table row
    // (0x18001eee0's sibling for this class) and is defined in no translation
    // unit, so it stays PURE — see `Video::ImageBuffer`'s slot-0 banner for why
    // a non-pure declaration does not link.
    //
    // ENDGAME B.3.8 block RB (B38_RULES D1/D2): the exe instantiates the class
    // itself (its table copy 0x14015e370, ModDialog's 0x140095da0), so slot 0
    // is CONCRETE: the exe copy's scalar deleting dtor 0x140019d50
    // (~FrameCopyImageBuffer; MemFree on flags & 1). Inline, so the key
    // function does not move. ENDGAME B.3.9 GFX: the minted table and its
    // RT_DELDTOR row 0x18001f260 are retired; the class keeps the exe's SCALAR
    // arm (B39_RULES D1.2 -- the two differ only on flags & 2, never passed).
    // ??1… 0x1800a3c80: release the two RenderBuffers and the back-buffer image
    // (wave23_lane_gfxbody.cpp); the restamps and ~GeneratedImageBuffer /
    // ~ImageBuffer are the compiler's chain. Virtual (B3X W1).
    ~FrameCopyImageBuffer() override;
    // [ 1] 0x18001efb0 TypeName / [ 2] 0x18001ef90 Type, TypeId, `_TypeName` / `_ParentTypeName`
    // and the factory pair: retail's macro (ENDGAME B.3.9 TI-REST, B39_RULES
    // D6); the record is `DEFINE_TYPE`d in
    // src/engine/graphics/graphics_type_records.cpp.
    DECLARE_TYPE(FrameCopyImageBuffer)
public:
    int32_t           GetFileVersion() const override;             // [ 3] 0x1800a3c70 — 0x1007
    bool              Serialize(IO::Stream& stream) override;            // [ 5] 0x1800a3c40
    Object::External* Reconstruct() override;                      // [ 6] 0x18001f130
    void              Destruct() override;                         // [ 7] 0x18001f160
    Object::External* Construct() override;                        // [ 8] 0x18001f150
    // [ 9] 0x1800a3b70 — ITS OWN, see the banner. DEFINED AS A MEMBER in
    // `wave23_lane_gfxbody.cpp` since ENDGAME B.3.5 block GC, so the `= 0` that
    // stood in for "the body is not a member yet" is GONE. That does NOT make the
    // class constructible: slot 0 (`DeletingDtor`) is still pure, for the reason
    // its own banner gives, so `FrameCopyImageBuffer` stays abstract.
    bool Create(ImageType type, Usage usage, Format format, uint32_t flags,
                int32_t width, int32_t height, FilterMode filter,
                AddressMode addressU, AddressMode addressV,
                AddressMode addressW, LevelArray* levels) override;
    // [10] 0x1800a3b20 — likewise a member in `wave23_lane_gfxbody.cpp` now.
    void Generate(void* context, int32_t generation) const override;
    // [11] 0x1800a3b10 — `RET`. One byte in the shipped build, ported as an
    // empty body (reference_retail_ships_empty_bodies.md).
    void Render(Renderer& renderer, const Scene& scene, const Camera& camera,
                int32_t generation) const override;

    // ??0FrameCopyImageBuffer@Graphics@@QEAA@XZ 0x1800a3d10 — the REAL
    // constructor, PUBLIC: the class is concrete. ImageBuffer(), the Generated
    // stamp + `_generation = 0` (GeneratedImageBuffer()) and the FrameCopy stamp
    // are the chain; the three nulls, the back-buffer alias and the RenderBuffer
    // are the body (wave23_lane_gfxbody.cpp). ENDGAME B.3.9 GFX: the ??0 / ??1
    // doors and `DoorBody` are retired; every builder spells `::new (p) X()`.
    FrameCopyImageBuffer();

    // EXT SEAM (NOT RETAIL, docs/extensions/handins/BETTERWATER.md section 8): ext.better_water's
    // scene depth copy, called by Generate right after retail's colour resolve, on the frame the copy
    // is made. Null by default and under the faithful profile (DESIGN.md hook rule 5): Generate pays
    // one null test. A static: the 0x88-byte layout is unchanged.
    using ExtAfterCopy = void (*)(void* context);
    static ExtAfterCopy _extAfterCopy;

protected:

    // RULE B5-15, both COPIED from B35_SLOTS section 3 into the DEFINE_TYPE:
    //   0x18001ef70  ?_TypeName@       -> "FrameCopyImageBuffer"
    //   0x18001ef80  ?_ParentTypeName@ -> "GeneratedImageBuffer"   (NO namespace)

public:
    // TypeId and the factory pair 0x18001f0a0 / 0x18001f120: DECLARE_TYPE's.
    static void*         operator new(size_t size);             // ??2 — reached by name
    static void*         operator new(size_t size, void* storage);
    static void          operator delete(void* p) { ::Object::detail::InternalMemFree(p); }              // ??3
    FrameCopyImageBuffer(const FrameCopyImageBuffer& other);            // ??0…AEBV01 — not ported
    FrameCopyImageBuffer& operator=(const FrameCopyImageBuffer& other); // ??4…      — not ported

    // ---- the 0x18 bytes FrameCopyImageBuffer adds -------------------------
    // WHAT THE THREE OWNED POINTERS ARE FOR: the POD's banner at the top of this
    // file, which is the byte read. `_backBufferImage` is typed
    // `Video::ImageBuffer*` now, which is what lets its teardown be
    // `delete _backBufferImage` instead of a hand-indexed slot 0. B.4: the two
    // render buffers are the real `Video::RenderBuffer` (Device.h) -- pointers,
    // so the bytes and the ctor's stores are unchanged.
    Video::ImageBuffer*  _backBufferImage;  // +0x70  owned; deleted via ITS slot 0, flags 1
    Video::RenderBuffer* _sourceTarget;     // +0x78  owned; colour = _backBufferImage
    Video::RenderBuffer* _destTarget;       // +0x80  owned; colour = this
};

static_assert(sizeof(FrameCopyImageBuffer) == 0x88,
              "Graphics::FrameCopyImageBuffer is 0x88 bytes (saveload_draw.cpp:451 "
              "allocates 0x88; ctor 0x1800a3d10 writes nothing past +0x80)");
static_assert(alignof(FrameCopyImageBuffer) == 8, "qword-aligned");
static_assert(offsetof(FrameCopyImageBuffer, _backBufferImage) == 0x70, "_backBufferImage @ +0x70");
static_assert(offsetof(FrameCopyImageBuffer, _sourceTarget)    == 0x78, "_sourceTarget @ +0x78");
static_assert(offsetof(FrameCopyImageBuffer, _destTarget)      == 0x80, "_destTarget @ +0x80");

}  // namespace Graphics
