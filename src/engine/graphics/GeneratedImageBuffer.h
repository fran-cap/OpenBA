#pragma once
// ============================================================================
// src/engine/graphics/GeneratedImageBuffer.h
// Graphics::GeneratedImageBuffer and Graphics::ReflectionImageBuffer
// ============================================================================
//
// The third member of this family, Graphics::FrameCopyImageBuffer, already has
// its own header (FrameCopyImageBuffer.h) and is NOT restated here. All three
// derive from Video::ImageBuffer (0x68), and GeneratedImageBuffer is the
// intermediate base both others stamp on the way in and out:
//
//     Video::ImageBuffer (0x68)
//       └─ Graphics::GeneratedImageBuffer (0x70)   ??_7 0x180154f60
//            ├─ Graphics::FrameCopyImageBuffer   (0x88)  FrameCopyImageBuffer.h
//            └─ Graphics::ReflectionImageBuffer  (0x90)  here, ??_7 0x180155190
//
// EVERY OFFSET BELOW WAS READ FROM THE DISASSEMBLY of the bodies that write it,
// never from Ghidra's C:
//   GeneratedImageBuffer   ctor 0x1800a3e70, dtor 0x1800a3e40
//   ReflectionImageBuffer  ctor 0x1800a7130, dtor 0x1800a70b0,
//                          Reconstruct 0x1800223e0, Construct 0x180022450,
//                          Render 0x1800a6b80, Serialize 0x1800a6f00
//
// THE TWO SIZES, each confirmed by its own deleting destructor's `MOV EDX,imm`
// (the stride the `eh vector destructor iterator` is handed):
//   0x18001eee0  MOV EDX,0x70   GeneratedImageBuffer
//   0x180022600  MOV EDX,0x90   ReflectionImageBuffer
// ============================================================================

#include <cstdint>
#include <cstddef>

#include "../video/ImageBuffer.h"

namespace Video { class RenderBuffer; }   // B.4: ReflectionImageBuffer::_target (Device.h)
namespace Graphics { class Renderer; class Scene; class Camera; }   // slot [11] Render's references


// ---------------------------------------------------------------------------
// Graphics::GeneratedImageBuffer — 0x70
// ---------------------------------------------------------------------------
// Adds exactly ONE field to Video::ImageBuffer: the int latch at +0x68 that
// every "generated" image compares against the caller's generation counter.
// The ctor zeroes it (`MOV dword ptr [RBX+0x68],0x0` at 0x1800a3e96) and the
// dtor touches nothing of its own — it restamps the vtable and tail-jumps
// ~ImageBuffer. `_pad6c` is NEVER WRITTEN by either body; it is the four bytes
// MSVC leaves before the 8-aligned tail of the derived classes. Recorded, not
// zeroed (FrameCopyImageBuffer.h says the same of its own +0x6c).


// ---------------------------------------------------------------------------
// Graphics::ReflectionImageBuffer — 0x90
// ---------------------------------------------------------------------------
// WHAT THE FIVE FIELDS ARE, read off the four bodies that touch them:
//
//   +0x70  Video::ImageBuffer*   OWNED, and it is the DEPTH buffer, not the
//          colour one. `Serialize` builds it — `??2ImageBuffer(0x68)`, then
//          `??0ImageBuffer`, then the object's own vtable slot 9 `Create` with
//          **usage = 1** (Video::ImageBuffer::IsDepthBuffer() is `kind == 1`)
//          and format 7, at this image's own width/height/flags. So the
//          reflection's depth target is created on LOAD, not on construction:
//          the constructor leaves +0x70 null. The destructor destroys it
//          through ITS OWN vtable slot 0 with flags = 1, the deleting form.
//   +0x78  Video::RenderBuffer*  OWNED. Also built by `Serialize`:
//          `??2RenderBuffer(0x30)`, three zero qwords, `PlatformConstruct`,
//          then `RenderBuffer::Create(rb, /*colour=*/this, /*depth=*/+0x70)`.
//          THIS object is the colour attachment — that is what makes the class
//          a reflection *texture*. Destroyed the long way (`PlatformDestruct`
//          then `??3RenderBuffer`), because RenderBuffer has no virtual
//          destructor; the same asymmetry FrameCopyImageBuffer.h records.
//   +0x80..+0x8c  the REFLECTION PLANE, four floats. `Render` (0x1800a6c01
//          onwards) reads them as normal (x, y, z) at +0x80/+0x84/+0x88 and
//          distance at +0x8c, and builds the Householder matrix
//          `I - 2*n*nᵀ` with a `2*d*n` translation row out of them.
//          The constructor initialises the plane to (0, 0, 1, 0) with two
//          qword stores: `[+0x80] = 0` covers x and y, and
//          `MOV qword ptr [RBX+0x88],0x3f800000` covers z = 1.0f and d = 0.0f
//          in ONE instruction. Two qwords, four floats — the field widths
//          below are what `Render` reads, and the store widths are what the
//          ctor writes; both readings describe the same sixteen bytes.


// ENDGAME B.3.9 block GFX: `kVtReflectionImageBuffer` (0x180155190) is gone
// with the minted table it named; the real class's ctor stamps the compiler's.


// ############################################################################
// THE REAL CLASSES — `Graphics::GeneratedImageBuffer` and
// `Graphics::ReflectionImageBuffer`. ENDGAME B.3.5 block C.
//
// The two PODs above STAY: `GeneratedImageBuffer`'s because it is the layout
// `RtGraphics::ReflectionImageBuffer` and `RtGraphics::FrameCopyImageBuffer`
// mirror by embedding `::ImageBuffer base`, and `FrameCopyImageBuffer`'s because
// `src/engine/graphics/wave23_lane_gfxbody.cpp` (block H1's) still uses it.
// ############################################################################

namespace Graphics {

// ============================================================
// Graphics::GeneratedImageBuffer — 0x70 bytes, TWELVE slots
// (`??_7GeneratedImageBuffer@Graphics@@6B@` @ 0x180154f60, B35_SLOTS.md).
//
// *** ABSTRACT, and in retail too: slots 10 and 11 are `_purecall`
// (0x18007ec28). That is what makes it the intermediate base of the branch
// rather than a usable image type — it defines "an image something regenerates
// per frame" and leaves BOTH the regeneration and the draw to a subclass.
//
// It inherits slots 6, 7, 8 (`Reconstruct`/`Destruct`/`Construct`) AND slot 9
// (`Create`) from `Video::ImageBuffer` — its `??_7` holds 0x18001ea80 /
// 0x18001eab0 / 0x18001eaa0 / 0x1800f2680, the base's own bodies — so none of
// the four is redeclared here. It overrides 1, 2, 3 and 5 and adds 10 and 11.
//
// RULE B5-15, both literals COPIED from B35_SLOTS section 3 and both read
// byte-wise: `?_TypeName@` (0x18001eda0) -> "GeneratedImageBuffer", NO
// namespace, while `?_ParentTypeName@` (0x18001edb0) -> "Video::ImageBuffer",
// WITH one. This class is the reason the rule is stated the way it is: the two
// strings for ONE class disagree about namespacing, so neither can be derived
// from the other or from the C++ base name.
//
// *** NOT CONSTRUCTIBLE IN B.3.5 *** and (until B3X W1) NO VIRTUAL DESTRUCTOR — the same two
// statements `Video::ImageBuffer` carries, for the same two reasons. Its ctor
// (0x1800a3e70) and dtor (0x1800a3e40) stay registered DOORS in
// `src/engine/graphics/imagebuffer_derived.cpp`: the ctor has to stamp the
// MINTED `??_7GeneratedImageBuffer` address, and the dtor has to reach
// `??1ImageBuffer@Video@@` which is still a door in block D2's `buffers.cpp`.
// ============================================================
//
// ENDGAME B.3 step 9 (block GFX, B39_RULES D1/D2): slot 0 is the class's OWN
// vector deleting dtor; the class stays ABSTRACT (slots 10 / 11 `_purecall`, as
// retail). The ctor / dtor doors, the four slot doors, the FACTREST row, the
// RT_DELDTOR row 0x18001eee0 and the minted `??_7GeneratedImageBuffer@Graphics@@6B@`
// are retired (tools/rt_mint_retired/gfx.txt); the "stay registered DOORS"
// paragraph above is history. The factory pair is retail's abstract one.
class GeneratedImageBuffer : public Video::ImageBuffer {
public:
    // [0] 0x18001eee0 -- MSVC's VECTOR deleting destructor, both arms exactly as
    // the retired RT_DELDTOR row ran them (`MOV EDX,0x70` = the element size).
    // Both subclasses declare their own (B9-4).
    // ??1… 0x1800a3e40 is `restamp; JMP ~ImageBuffer` — both the compiler's
    // chain, so the body is empty.
    ~GeneratedImageBuffer() override {}
    // [ 1] 0x18001ede0 TypeName / [ 2] 0x18001edc0 Type, TypeId, `_TypeName` / `_ParentTypeName`
    // and the factory pair: retail's macro (ENDGAME B.3.9 TI-REST, B39_RULES
    // D6); the record is `DEFINE_TYPE`d in
    // src/engine/graphics/graphics_type_records.cpp.
    DECLARE_TYPE(GeneratedImageBuffer)
public:
    int32_t       GetFileVersion() const override;            // [ 3] 0x1800a3eb0 — `MOV EAX,0x7`
    bool          Serialize(IO::Stream& stream) override;           // [ 5] 0x1800a3ec0

    // [10] and [11], `_purecall` in retail. `virtual`, not `override`: this
    // branch INTRODUCES both (RULE B5-16's shape one level down from
    // `Construct`). Their names come from the two concrete override sets, which
    // is the only place a pure slot's name can be read from.
    //
    // BOTH ARE `const` (`UEBA`) AND BOTH SUBCLASSES WRITE `_generation` THROUGH
    // THEM. Retail's own const-ness is transcribed, so `_generation` is declared
    // `mutable` (B.4 const_slot ruling): the latch the caller's generation
    // counter is compared against is written by retail's const slots
    // (0x1800a6e91 Render; FrameCopyImageBuffer's Generate).
    virtual void Generate(void* context, int32_t generation) const = 0;   // [10]
    virtual void Render(Renderer& renderer, const Scene& scene, const Camera& camera,
                        int32_t generation) const = 0;                    // [11]

protected:
    // ??0GeneratedImageBuffer@Graphics@@QEAA@XZ 0x1800a3e70 — the REAL
    // constructor, protected (abstract): ImageBuffer() and the stamp are the
    // compiler's; `MOV dword [RBX+0x68],0` is the body. +0x6c is never written.
    // (ENDGAME B.3.9 GFX: the door and `DoorBody` are retired.)
    GeneratedImageBuffer() { _generation = 0; }

protected:

    // `IEBA` = PROTECTED const, not private. Neither is ported: nothing in the
    // tree calls either, and they exist only as rows in
    // `standalone/rt_export_addrs.inc`. They are the latch protocol the two
    // subclasses' `Generate` arms would use.
    bool BeginGeneration(int32_t generation) const;   // ?BeginGeneration@ — not ported
    void EndGeneration(int32_t generation) const;     // ?EndGeneration@   — not ported

    // RULE B5-15 — see the class banner (the literals are DEFINE_TYPE's).

public:
    // TypeId and the factory pair: DECLARE_TYPE's.
    GeneratedImageBuffer(const GeneratedImageBuffer& other);            // ??0…AEBV01 — not ported
    GeneratedImageBuffer& operator=(const GeneratedImageBuffer& other); // ??4…      — not ported

    // ---- the 8 bytes GeneratedImageBuffer adds ----------------------------
    // The ctor zeroes `_generation` (`MOV dword ptr [RBX+0x68],0x0` at
    // 0x1800a3e96) and the dtor touches nothing of its own. `_pad6c` is NEVER
    // WRITTEN by either body; it is the four bytes MSVC leaves before the
    // 8-aligned tail of the derived classes. Recorded, not zeroed.
    mutable int32_t _generation;   // +0x68  written by the const slots [10] / [11]
    uint32_t _pad6c;        // +0x6c  never written
};

static_assert(sizeof(GeneratedImageBuffer) == 0x70,
              "Graphics::GeneratedImageBuffer is 0x70 (0x18001eee0's MOV EDX,0x70)");
static_assert(offsetof(GeneratedImageBuffer, _pad6c) == 0x6C, "GeneratedImageBuffer::_pad6c");
static_assert(alignof(GeneratedImageBuffer) == 8, "qword-aligned");
static_assert(offsetof(GeneratedImageBuffer, _generation) == 0x68, "_generation @ +0x68");

// ============================================================
// Graphics::ReflectionImageBuffer — 0x90 bytes, TWELVE slots
// (`??_7ReflectionImageBuffer@Graphics@@6B@` @ 0x180155190, B35_SLOTS.md).
//
// Overrides 1, 2, 3, 5, 6, 7, 8, 10, 11 and INHERITS 4 (`External::Compile`) and
// 9 (`?Create@ImageBuffer@Video@@`, whose address its slot 9 carries verbatim).
// The inherited slot 9 is what B35_RULES 2.3 item 5 says of BOTH concretes; it
// is true here and false of `FrameCopyImageBuffer`, which has its own `Create`
// (see FrameCopyImageBuffer.h's banner for the byte evidence).
//
// `?_TypeName@` (0x180022190) -> "ReflectionImageBuffer", `?_ParentTypeName@`
// (0x1800221a0) -> "GeneratedImageBuffer", NO namespace. RULE B5-15.
//
// ENDGAME B.3 step 9 (block GFX, B39_RULES D1/D2): CONCRETE. A real public
// ctor (retail 0x1800a7130), its own vector slot 0, a virtual
// ~ReflectionImageBuffer holding the retired ??1 door's body, the factory pair
// as the class's own statics; Construct / Reconstruct placement-new the class.
// The minted `??_7ReflectionImageBuffer@Graphics@@6B@`, its RT_DELDTOR row
// 0x180022600, the FACTREST row and the ctor / ??1 / slot doors are retired
// (rt_mint_retired/gfx.txt).
// ============================================================
class ReflectionImageBuffer : public GeneratedImageBuffer {
public:
    // [0] 0x180022600 -- MSVC's VECTOR deleting destructor, both arms exactly as
    // the retired RT_DELDTOR row ran them (`MOV EDX,0x90` = the element size).
    // ??1… 0x1800a70b0. VIRTUAL (B3X W1). ENDGAME B.3.9 GFX: the retired door's body
    // (imagebuffer_derived.cpp) -- the two owned GPU objects; the restamps and
    // ~GeneratedImageBuffer / ~ImageBuffer are the compiler's chain.
    ~ReflectionImageBuffer() override;

    // ??0ReflectionImageBuffer@Graphics@@QEAA@XZ 0x1800a7130 -- the REAL ctor
    // (retail also inlines it into both factories, Construct and Reconstruct).
    // ImageBuffer(), the Generated stamp + `_generation = 0`
    // (GeneratedImageBuffer()) and the Reflection stamp are the chain; then the
    // two owned pointers null and the plane (0, 0, 1, 0) as retail's two qword
    // stores `[+0x80] = 0` and `[+0x88] = 0x3f800000`.
    ReflectionImageBuffer() {
        _depth  = nullptr;
        _target = nullptr;
        _planeX = 0.0f;
        _planeY = 0.0f;
        _planeZ = 1.0f;
        _planeD = 0.0f;
    }
    // [ 1] 0x1800221d0 TypeName / [ 2] 0x1800221b0 Type, TypeId, `_TypeName` / `_ParentTypeName`
    // and the factory pair: retail's macro (ENDGAME B.3.9 TI-REST, B39_RULES
    // D6); the record is `DEFINE_TYPE`d in
    // src/engine/graphics/graphics_type_records.cpp.
    DECLARE_TYPE(ReflectionImageBuffer)
public:
    int32_t           GetFileVersion() const override;             // [ 3] 0x1800a70a0 — 0x1007
    bool              Serialize(IO::Stream& stream) override;            // [ 5] 0x1800a6f00
    Object::External* Reconstruct() override;                       // [ 6] 0x1800223e0
    void              Destruct() override;                          // [ 7] 0x1800224c0
    Object::External* Construct() override;                         // [ 8] 0x180022450
    // [10] 0x1800a6ef0 — `RET`. Empty in the shipped build: a reflection image is
    // produced by `Render` feeding a draw group, not by a blit
    // (reference_retail_ships_empty_bodies.md).
    void Generate(void* context, int32_t generation) const override;
    // [11] 0x1800a6b80 — the one body in this family that does real work.
    void Render(Renderer& renderer, const Scene& scene, const Camera& camera,
                int32_t generation) const override;

    // EXT SEAM (NOT RETAIL, issue #253): the engine end of ext.staggered_passes' reflection hold
    // (graphics/stagger_hold.h). Null by default and while the row is off (retail: the image is
    // rendered every frame); otherwise src/ext/staggered_passes/ points it at
    // ext.staggered_passes_reflection (1, 2, 4 or 8). While it points at N > 1, Render may skip
    // appending its scene entry for up to N - 1 frames on a still view, and the water samples the
    // image the last real render left. A data pointer; the hold state lives in
    // imagebuffer_derived.cpp, outside the pinned layout. Main thread.
    static const int32_t* _extStaggerEvery;

public:
    // TypeId, the factory pair and the two literals: DECLARE_TYPE / DEFINE_TYPE's.
    static void*         operator new(size_t size);             // ??2 — reached by name
    static void*         operator new(size_t size, void* storage);
    static void          operator delete(void* p) { ::Object::detail::InternalMemFree(p); }              // ??3
    ReflectionImageBuffer(const ReflectionImageBuffer& other);            // not ported
    ReflectionImageBuffer& operator=(const ReflectionImageBuffer& other); // not ported

    // ---- the 0x20 bytes ReflectionImageBuffer adds ------------------------
    // What the five fields are: the POD's banner above, which is the byte read.
    // `_depth` is typed `Video::ImageBuffer*` now, which is what lets its
    // teardown be `delete _depth` instead of a hand-indexed slot 0.
    // B.4: `_target` is the real `Video::RenderBuffer` (Device.h) -- a pointer,
    // so the bytes and the ctor's store are unchanged.
    Video::ImageBuffer*  _depth;    // +0x70  owned, usage = 1 (a DEPTH buffer)
    Video::RenderBuffer* _target;   // +0x78  owned, colour = this
    float               _planeX;   // +0x80  ctor: 0 (lower half of one qword store)
    float               _planeY;   // +0x84  ctor: 0 (upper half of it)
    float               _planeZ;   // +0x88  ctor: 1.0f
    float               _planeD;   // +0x8c  ctor: 0.0f
};

static_assert(sizeof(ReflectionImageBuffer) == 0x90,
              "Graphics::ReflectionImageBuffer is 0x90 (0x180022600's MOV EDX,0x90)");
static_assert(alignof(ReflectionImageBuffer) == 8, "qword-aligned");
static_assert(offsetof(ReflectionImageBuffer, _depth)  == 0x70, "_depth @ +0x70");
static_assert(offsetof(ReflectionImageBuffer, _target) == 0x78, "_target @ +0x78");
static_assert(offsetof(ReflectionImageBuffer, _planeX) == 0x80, "_planeX @ +0x80");
static_assert(offsetof(ReflectionImageBuffer, _planeY) == 0x84, "_planeY @ +0x84");
static_assert(offsetof(ReflectionImageBuffer, _planeZ) == 0x88, "_planeZ @ +0x88");
static_assert(offsetof(ReflectionImageBuffer, _planeD) == 0x8c, "_planeD @ +0x8c");

}  // namespace Graphics
