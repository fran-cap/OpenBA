#pragma once
// ============================================================================
// src/engine/video/ImageBuffer.h — `Video::ImageBuffer` as a real C++ class,
// plus the `RtVideo::ImageBuffer` POD its unconverted callers still use.
//
// ENDGAME B.3.5 block C. RULES B5-2.1 (this header is what `src/render/dx11/`'s deleted
// `ImageBufferOwner` view now points at), B5-15 (the two type-name literals are
// COPIED from B35_SLOTS, not derived from the C++ base name), B5-16 (`Construct`
// is introduced by this branch at slot 8).
// ============================================================================
//
// Runtime-x64.dll class, 0x68 bytes, `??_7ImageBuffer@Video@@6B@` @ 0x180154fc0,
// TEN slots (B35_SLOTS.md), `sizeof` confirmed by the `MOV EDX,0x68` in the
// slot-0 vector deleting destructor at 0x18001ed20.
//
// Inheritance: Object::Base -> Object::External -> Object::CompiledExternal ->
// Video::ImageBuffer. `?_ParentTypeName@ImageBuffer@Video@@` (0x18001e8d0)
// returns "Object::CompiledExternal", read byte-wise.
//
// Decompiled from (Wave 6 lane C):
//   Ctor:            0x1800f2580 (??0ImageBuffer@Video@@QEAA@XZ)
//   Copy ctor:       0x18001eb80 (??0ImageBuffer@Video@@QEAA@AEBV01@@Z)
//   Dtor:            0x1800f2610 (??1ImageBuffer@Video@@UEAA@XZ)
//   Width:           0x1800f2530 (?Width@ImageBuffer@Video@@QEBAHXZ)
//   Height:          0x1800f24e0 (?Height@ImageBuffer@Video@@QEBAHXZ)
//   GetLevelCount:   0x1800f2420 (?GetLevelCount@ImageBuffer@Video@@QEBAHXZ)
//   IsCompressed:    0x1800f2480 (?IsCompressed@ImageBuffer@Video@@QEBA_NXZ)
//   IsDepthBuffer:   0x1800f24c0 (?IsDepthBuffer@ImageBuffer@Video@@QEBA_NXZ)
//   IsDynamic:       0x1800f24d0 (?IsDynamic@ImageBuffer@Video@@QEBA_NXZ)
//   IsGenerated:     0x1800f24a0 (?IsGenerated@ImageBuffer@Video@@QEBA_NXZ)
//   IsRenderBuffer:  0x1800f2460 (?IsRenderBuffer@ImageBuffer@Video@@QEBA_NXZ)
//   IsRendered:      0x1800f2490 (?IsRendered@ImageBuffer@Video@@QEBA_NXZ)
//   CopyToTexture:   0x1800f3380 (?CopyToTexture@ImageBuffer@Video@@QEAA_NPEAXHH@Z)
//   CopyFromTexture: 0x1800f33a0 (?CopyFromTexture@ImageBuffer@Video@@QEAA_NPEAXHH@Z)
//
// STALE-COMMENT CORRECTION (Wave 6 lane C): the +0x28/+0x2C fields were
// previously "unknown". The disassembly of the ctor (0x1800f25d8-0x1800f25db,
// one dword store then one qword store covering both) and the Is*() family
// (all of which read *(int*)(this+0x2c)) show they are `dimension` (2D vs
// cube) and `kind` (an enum selecting bind flags: 0/4/5 = render target,
// 1 = depth, 2 = default/none, 3 = dynamic). This matches
// src/render/dx11/imagebufferdata.cpp's independently-recovered field map exactly
// (that file reads the same offsets from the DX11 side). +0x38/+0x3C/+0x40
// were also "unknown"; src/render/dx11/imagebufferdata.cpp's sampler code (and the
// ctor's default value 0) identify them as the address-mode triple
// (U/V/W), and +0x44 (init value 1) as the filter index.
//
// Width()/Height() were PREVIOUSLY WRONG as inlines: the old `return width;` /
// `return height;` only reproduced the common case. The real accessors
// (0x1800f2530 / 0x1800f24e0) special-case two flag bits that make the
// buffer track the live device resolution (bit 8 = full device size, bit 9 =
// half device size) and a third (bit 6) that right-shifts the stored value
// by a device-configured downsample amount.
// ============================================================================

#include <cstdint>
#include <cstddef>

// ENDGAME B.3.5 block A — `Video::ImageBufferData` and its POD view live here
// now, with the other eleven companion bases (RULE B5-1 / B4b-1).
#include "CompanionData.h"
// `Object::CompiledExternal`, the proved parent of the real class below.
#include "../object/External.h"
#include "../object/Internal.h"   // ENDGAME B.3.8 RB: InternalMemFree (the concrete slot 0)

// `src/engine/video/`'s door helper over `Video::ImageBuffer`'s protected
// surface. Four of its members are PRIVATE in retail (`AEAA` — the three
// platform halves and `PlatformSerialize`) and `~ImageBuffer` is non-virtual and
// protected, yet all five are exported by name and keep registered `extern "C"`
// doors, which cannot name a protected member; `Serialize` also has to call
// `PlatformSerialize`. B34_BRIEFS A.4's answer is to FRIEND the holder rather
// than widen the access, so the header keeps saying what retail says. A DERIVED
// helper class would not work: deriving forces GCC to reference
// `vtable for Video::ImageBuffer`, whose slot 0 (`DeletingDtor`) is a table row
// in `src/engine/object/deleting_dtor.cpp` and is defined in no translation
// unit, so the link fails (seen on `Video::RenderState`, not predicted).
struct ImageBufferDoorAccess;

// `Collection::Array<T>` (three template parameters, two defaulted) and
// `System::Filename` both arrive with `../object/External.h` above, which
// includes them; they are NOT forward-declared here, because a hand-written
// `template <class T> class Array;` would be a different template.
namespace IO { class Serializer; }

// ############################################################################
// THE POD, MOVED INTO `RtVideo` BY B4b-1.
// ############################################################################
//
// The retail name now belongs to the real class `Video::ImageBuffer` below, so
// the POD moves into the `Rt*` namespace — and it STAYS, because it still has
// five unconverted users, none of them this block's:
//
//   src/engine/video/buffers.cpp          (13 members; block D2)
//   src/engine/video/dynamic_resources.cpp (2;          block D2)
//   src/engine/video/video_resources.cpp   (1;          block D2)
//   src/engine/save/saveload_draw.cpp / saveload_saveinfo.cpp (the owner's)
//   src/engine/graphics/wave23_lane_gfxbody.cpp, through the two
//     `RtGraphics::*ImageBuffer` PODs that embed it (block H1)
//
// A global-scope `using` is left behind so not one of those files has to move
// in this block; they all spell it `ImageBuffer` with no namespace, which is
// the fourth namespace spelling B35_FILEMAP (d) records for this header.
// ############################################################################


// ============================================================
// RtVideo::ImageBuffer (0x68 bytes) — the POD view
// ============================================================



// The global spelling the five unconverted consumers use. See the banner above.
// And `Video::ImageBufferData`'s POD, whose global alias ENDGAME B.3.5 block A
// left here when it moved the declaration into `CompanionData.h`:
// `src/engine/video/buffers.cpp` writes `pd->vtable` by hand in three
// trampolines and in the ported `??1ImageBufferData` door. Block D2's when
// `buffers.cpp` converts.

// ############################################################################
// THE REAL CLASS.
// ############################################################################

namespace Video {

// ============================================================
// Video::ImageBuffer — one image/texture resource. 0x68 bytes,
// `Object::CompiledExternal` base (which adds no bytes over `External`'s 0x20).
//
// ENDGAME B.3.9 VS: every live `ImageBuffer` is built by the real public
// constructor (`::new (p) Video::ImageBuffer()` at every builder, Runtime and
// exe, and by the class's own `CreateNewObject` / `CreateInPlaceObject`) and
// wears the compiler's table. The `??0` / `??1` doors, the FACTREST row, the
// `RT_DELDTOR` row and the minted `??_7ImageBuffer@Video@@6B@` are retired.
//
// THE DESTRUCTOR IS VIRTUAL (B3X W1): retail slot 0 is
// the deleting destructor. `~ImageBuffer()` is D1 + D0 here.
// ============================================================
class ImageBuffer : public Object::CompiledExternal {
public:
    // The five 4-byte enums `?Create@` names, all NESTED in this class
    // (`W4ImageType@12@`, `W4Usage@12@`, `W4Format@12@`, `W4FilterMode@12@`,
    // `W4AddressMode@12@`). THE ENUMERATOR NAMES ARE NOT RECOVERABLE from the
    // binary — only the values the ported bodies and the DX11 backend read — so,
    // exactly as `Object::External::CompileResult` and
    // `Video::VertexLayout::Usage` do for the same reason, they are spelled as
    // the 32-bit integers they are rather than as enums with invented names.
    //
    // What IS known, from the bodies:
    //   ImageType    +0x28 `_dimension`: 0 = 2D, 1 = cube
    //   Usage        +0x2c `_kind`: 1 = depth, 2 = default, 3 = dynamic,
    //                4 = generated, 5 = rendered, and {0,1,4,5} = render buffer
    //   Format       +0x30, an index into an 8-entry DXGI table; < 3 is
    //                block-compressed
    //   FilterMode   +0x44: 0 = POINT, 1 = LINEAR, 2 = ANISOTROPIC
    //   AddressMode  +0x38/+0x3c/+0x40: 0 = WRAP, 1 = CLAMP
    using ImageType   = int32_t;
    using Usage       = int32_t;
    using Format      = int32_t;
    using FilterMode  = int32_t;
    using AddressMode = int32_t;

    // The value counts of the serialized enum fields, and the table indexes the
    // backends derive from them. All five fields are read from a .rsc with
    // `SerializeUnsigned` and never range-checked in retail.
    static constexpr uint32_t kFormatCount      = 8;   // the DX11 8-entry DXGI / bytes tables
    static constexpr uint32_t kAddressModeCount = 2;   // WRAP, CLAMP
    static constexpr uint32_t kFilterModeCount  = 3;   // POINT, LINEAR, ANISOTROPIC

    // `kind == 4 || kind == 5` (IsGenerated() || IsRendered()) on a raw `_kind`:
    // retail's `LEA EAX,[R8-4] / CMP EAX,1 / JBE` (0x1800f3154), a wrapping
    // 32-bit subtraction and an unsigned compare.
    // NOT RETAIL (UB, signed overflow): `kind - 4` in int overflows for a
    // serialized kind of 0x80000000; the subtraction is made unsigned, which is
    // the wrap retail's LEA performs. Same answer for every kind.
    static constexpr bool KindIsGeneratedOrRendered(Usage kind) {
        return static_cast<uint32_t>(kind) - 4u <= 1u;
    }

    // NOT RETAIL (UB, out-of-bounds index): retail indexes the DX11 format tables
    // with the raw `_format` and has no check. True for every retail format 0..7.
    static constexpr bool FormatInRange(Format format) {
        return static_cast<uint32_t>(format) < kFormatCount;
    }

    // NOT RETAIL (UB, out-of-bounds index): retail indexes the sampler's 2-entry
    // address table with the raw field and has no check. 0 and 1 index
    // themselves; any other value takes its low bit, as the Vulkan backend does.
    static constexpr int32_t AddressTableIndex(AddressMode mode) {
        return static_cast<int32_t>(static_cast<uint32_t>(mode) & 1u);
    }

    // NOT RETAIL (UB, out-of-bounds index): retail indexes the sampler's 3-entry
    // filter table with the raw field and has no check. 0..2 index themselves;
    // any other value is clamped into 0..2, as the Vulkan backend does.
    static constexpr int32_t FilterTableIndex(FilterMode filter) {
        if (filter < 0)
            return 0;
        if (filter > 2)
            return 2;
        return filter;
    }

    // The `Collection::Array<Collection::Array<uint8_t>>` of per-mip-level pixel
    // blobs that `Create` and `PlatformCreate` take
    // (`PEAV?$Array@V?$Array@E@Collection@@@Collection@@` in the mangling).
    using LevelArray = Collection::Array<Collection::Array<uint8_t> >;

    // ---- the ten slots, in measured order -------------------------------
    // Slot 4 is `Object::External::Compile`, INHERITED (retail's slot 4 is
    // 0x1800066e0, External's own body), so it is not redeclared here.
    //
    // SLOT 0 IS CONCRETE IN RETAIL (0x18001ed20) AND IS DECLARED `= 0` HERE,
    // which is B.3.4's settled shape (B34_BRIEFS A.4) and the same call
    // `CompanionData.h` makes for the twelve `*Data` bases: our slot 0 is a table
    // ROW in `src/engine/object/deleting_dtor.cpp`, registered by address, and
    // is defined in no translation unit. Leaving it non-pure makes GCC demand
    // `vtable for Video::ImageBuffer` from whichever translation unit defines a
    // constructor or destructor of the class, which then fails to link. Pure
    // makes every entry known (`__cxa_pure_virtual`) and the vtable emittable —
    // and no live object ever wears it, because the ctor door stamps the minted
    // address. The class is therefore ABSTRACT in C++ while being "concrete" in
    // B35_RULES 2.2's sense (retail instantiates it); the two statements are
    // about different things.
    //
    // ENDGAME B.3.8 block RB (B38_RULES D1/D2): the exe instantiates the class
    // ITSELF (its table copy 0x14015c8c0; ModDialog, NoiseUI, TradeWork); its
    // slot 0 is the exe copy's 0x1400129d0, the scalar deleting dtor
    // (`~ImageBuffer`; MemFree on flags & 1) -- the DLL table's is the VECTOR
    // one 0x18001ed20. Since B3X W1 that slot is the virtual destructor (D1 +
    // D0; `delete` frees through this class's `operator delete`, MemFree).
    // ??1ImageBuffer@Video@@UEAA@XZ (0x1800f2610): reached by the deleting
    // destructor (so `Destruct` / `Reconstruct` too) and by the three Graphics
    // subclasses' destructor chains. Its door is gone (ENDGAME B.3.9 VS).
    ~ImageBuffer() override;
    // [1] 0x18001e900 TypeName / [2] 0x18001e8e0 Type, TypeId 0x18001e8f0, `_TypeName` / `_ParentTypeName` and the factory pair:
    // retail's macro (ENDGAME B.3.9 TI-REST, B39_RULES D6); the record is
    // `DEFINE_TYPE`d in src/engine/video/video_type_records.cpp.
    DECLARE_TYPE(ImageBuffer)
public:
    int32_t           GetFileVersion() const override;        // [3] 0x1800f26e0 — `MOV EAX,7`
    bool              Serialize(IO::Stream& stream) override;       // [5] 0x1800f26f0
    Object::External* Reconstruct() override;                 // [6] 0x18001ea80
    void              Destruct() override;                    // [7] 0x18001eab0
    // [8] 0x18001eaa0. `virtual`, NOT `override`: `Object::External`'s and
    // `CompiledExternal`'s retail tables are eight entries, so this branch
    // introduces `Construct` itself (RULE B5-16).
    virtual Object::External* Construct();
    // [9] 0x1800f2680. Introduced by this branch too. `Graphics::
    // GeneratedImageBuffer` INHERITS it, `Graphics::FrameCopyImageBuffer`
    // OVERRIDES it, and `Graphics::ReflectionImageBuffer` inherits it again
    // (B35_RULES 2.3 item 5, read from the three slot lists).
    //
    // NO LONGER PURE — ENDGAME B.3.5 block VC (the fragment pass). Block C wrote
    // `= 0` here for one reason only: the body was the registered door
    // `Video_ImageBuffer_Create` in `src/engine/video/buffers.cpp` and was
    // defined in no translation unit AS A MEMBER, so a non-pure declaration put
    // an undefined symbol in this class's emitted vtable and the exe did not
    // link. `buffers.cpp` has now moved the body onto this member, so the symbol
    // exists and the `= 0` comes off exactly as that banner said it would.
    //
    // SLOT 0 STAYS PURE and so the class stays ABSTRACT in C++: its body is a
    // table row in `src/engine/object/deleting_dtor.cpp`, which B35_RULES 11
    // keeps for the whole of step 5. Nothing is therefore instantiable by
    // accident, and no live object wears GCC's table — the ctor door stamps the
    // minted `??_7ImageBuffer@Video@@6B@`.
    virtual bool Create(ImageType type, Usage usage, Format format, uint32_t flags,
                        int32_t width, int32_t height, FilterMode filter,
                        AddressMode addressU, AddressMode addressV,
                        AddressMode addressW, LevelArray* levels);

    // ---- the exported non-virtual members --------------------------------
    // Every `?…@ImageBuffer@Video@@` in the shipped export table is declared
    // here, ported or not, so no later block has to edit this header
    // (BLOCK_BRIEF "the shape").
    //
    // ENDGAME B.3.5 block VC (the fragment pass) MOVED THE PORTED BODIES ONTO
    // THESE MEMBERS. Block C had to leave them declared-undefined because the
    // bodies lived in `src/engine/video/buffers.cpp`, `dynamic_resources.cpp`
    // and `video_resources.cpp` — block D2's file set. Those three files are
    // converted now and the doors are one-line QUALIFIED delegations. The
    // members marked `not ported` below still have no body anywhere in the tree
    // and nothing calls them; that is unchanged.
    int32_t Width() const;                 // 0x1800f2530 — buffers.cpp; live device size for bits 8/9
    int32_t Height() const;                // 0x1800f24e0 — buffers.cpp; ditto
    int32_t GetLevelCount() const;         // 0x1800f2420 — buffers.cpp
    // ========================================================================
    // ADDRESS CORRECTION, ENDGAME B.3.5 block VC — 23 of the addresses in this
    // section and below were WRONG, and the instrument that found them is the
    // shipped export directory itself (`tools/pe_exports.py` over
    // `C:/GOG Games/Banished/Runtime-x64.dll`, every `*ImageBuffer@Video@@`
    // symbol dumped with its RVA and re-based to 0x180000000). Every address
    // that was wrong sat in the 0x1800f23e0..0x1800f2570 band or the
    // 0x1800f2ba0..0x1800f3100 band, and NOT ONE address in either band is an
    // ImageBuffer export at all — so the old comments named nothing. The eleven
    // that were right are exactly the eleven that a registration in
    // `buffers.cpp` / `dynamic_resources.cpp` / `video_resources.cpp` also
    // spells, i.e. the ones a pefile check had already been run against
    // (`reference_resolveexport_phantom_names.md` is the rule those three files
    // follow and this header did not).
    //
    // NOTHING BEHAVIOURAL CHANGED with the fix: every one of the corrected rows
    // is an unported member, so no door and no registration named the bad
    // address. It is a documentation defect, and the reason it is worth this
    // banner is that the next block to port one of these would have registered
    // `RegisterRuntimeAddr` on a number out of a comment.
    // ========================================================================
    Format  GetFormat() const;             // 0x18001eb50 — `?AW4`, an ENUM by value: EAX, NOT a retslot
    bool    HasMipMaps() const;            // 0x18001eac0 — buffers.cpp (GetLevelCount's gate)
    bool    Is2D() const;                  // 0x18001eb30 — not ported
    bool    IsCube() const;                // 0x18001eb40 — not ported
    bool    IsCompressed() const;          // 0x1800f2480 — buffers.cpp
    bool    IsColorBuffer() const;         // 0x1800f24b0 — not ported
    bool    IsDepthBuffer() const;         // 0x1800f24c0 — buffers.cpp
    bool    IsDynamic() const;             // 0x1800f24d0 — buffers.cpp
    bool    IsGenerated() const;           // 0x1800f24a0 — buffers.cpp
    bool    IsRenderBuffer() const;        // 0x1800f2460 — buffers.cpp
    bool    IsRendered() const;            // 0x1800f2490 — buffers.cpp
    bool    IsShadowMap() const;           // 0x18001eb20 — not ported
    bool    IsAliasedColor() const;        // 0x18001eb00 — not ported
    bool    IsAliasedDepth() const;        // 0x18001eb10 — not ported
    bool    CanDownsample() const;         // 0x18001ead0 — not ported
    bool    ClearEdgeAlpha() const;        // 0x18001eae0 — not ported
    bool    ClearEdgeColor() const;        // 0x18001eaf0 — not ported
    void    AliasDefaultColorBuffer();     // 0x1800f3470 — not ported
    void    AliasDefaultDepthBuffer();     // 0x1800f3340 — not ported
    void    Bind(void* context, int32_t slot) const;   // 0x1800f3360 — not ported
    bool    CopyToTexture(void* dst, int32_t width, int32_t height);   // 0x1800f3380 — buffers.cpp
    bool    CopyFromTexture(void* src, int32_t width, int32_t height); // 0x1800f33a0 — buffers.cpp
    void    Recreate();                    // 0x1800f33f0 — video_resources.cpp
    void    Release();                     // 0x1800f33c0 — video_resources.cpp
    // `?GetData@ImageBuffer@Video@@` IS NOT IN THE SHIPPED EXPORT TABLE, under
    // either mangling, and the two addresses previously claimed here
    // (0x1800f2560 / 0x1800f2570) are not exports either. The pair is kept
    // DECLARED because `src/render/dx11/` reads `_data` at +0x60 and a later block may want
    // the accessor, but the "exported member" claim is withdrawn: they are
    // either inlined everywhere or they do not exist.
    ImageBufferData*       GetData();      // not exported — the non-const overload
    const ImageBufferData* GetData() const;// not exported — `QEBA`, the const one

    // ?SaveImageToFile@ImageBuffer@Video@@SA_NAEBVFilename@System@@HHPEAI@Z
    //   0x1800f2840. STATIC — it takes no `this`; the body is
    //   `src/engine/video/save_image_to_file.cpp`.
    static bool SaveImageToFile(const System::Filename& path, int32_t width,
                                int32_t height, uint32_t* pixels);
    // ?ReleaseDynamic@ / ?RecreateDynamic@ — STATIC, they walk the
    // `?_firstSurface@ImageBuffer@Video@@` intrusive list. Bodies in
    // `dynamic_resources.cpp`, moved onto these members by block VC.
    // Addresses corrected — see the banner above; `dynamic_resources.cpp` had
    // the right pair all along (ordinals 3537 / 3523).
    static void ReleaseDynamic();          // 0x1800f2e80
    static void RecreateDynamic();         // 0x1800f2d90

    // ?_firstSurface@ImageBuffer@Video@@0PEAV12@EA (0x1801fe780) — the head of
    // the live-surface list PlatformConstruct / PlatformDestruct link into and the
    // two sweeps walk (device-lost recovery). The class static since ENDGAME
    // B.3.9 VID (B39_RULES D7), defined in dynamic_resources.cpp; private in
    // retail, PUBLIC here because the walkers are free functions.
    static ImageBuffer* _firstSurface;

    // EXT SEAM (NOT RETAIL, docs/extensions/handins/CREATEONDEMAND.md, DESIGN.md hook rule 5):
    // ext.create_on_demand's level reader. Null by default and under the faithful profile.
    // PlatformSerialize's READ arm, with the outer level array sized, asks it before reading the
    // `count` level blobs: true = it consumed them (each level's size read as retail reads it, its
    // bytes either skipped -- left for the renderer to create on first use -- or read into `levels`
    // exactly as retail reads them), false = nothing consumed, retail's read follows.
    using ExtLevelSourceFn = bool (*)(IO::Serializer& serializer, ImageBuffer& image, int32_t count,
                                      LevelArray& levels);
    static ExtLevelSourceFn _extLevelSource;

    // `??0ImageBuffer@Video@@QEAA@AEBV01@@Z` (0x18001eb80) and
    // `??4ImageBuffer@Video@@QEAAAEAV01@AEBV01@@Z` (0x18001ec50) are REAL,
    // EXPORTED bodies, not compiler defaults — the situation RULE B5-10 records
    // for `Graphics::Geometry`. Declared, and deliberately left UNDEFINED: a
    // `= default` here would silently substitute a bitwise copy (which would
    // alias `_data`, a singly-owned `ImageBufferData*`, and share `_sourceName`
    // without bumping its refcount) for whatever those two do.
    ImageBuffer(const ImageBuffer& other);              // 0x18001eb80 — not ported
    ImageBuffer& operator=(const ImageBuffer& other);   // 0x18001ec30 — not ported
                                                        //   (was 0x18001ec50; the
                                                        //   export table says ec30)

    // The static reflection companions retail emits are DECLARE_TYPE's (the
    // factory pair 0x18001e9f0 / 0x18001ea70 is in buffers.cpp).
    // ??2ImageBuffer@Video@@SAPEAX_K@Z 0x18001e910 — the body moved onto this
    // member by block VC; the door is `src/engine/video/context_ctor.cpp`'s
    // `RtVideo_ImageBuffer_OperatorNew`, which keeps its name and both rows.
    static void*         operator new(size_t size);
    // ??2ImageBuffer@Video@@SAPEAX_KPEAX@Z 0x18001e980 — not ported.
    static void*         operator new(size_t size, void* storage);
    // ??3ImageBuffer@Video@@SAXPEAX@Z 0x18001e990 — read in retail (B3X W1):
    // `if (p) { Enter(lock); MemoryAllocator::Free(p); Leave(lock); }`, i.e.
    // MemFree verbatim. Defined inline since the normal-virtual cutover: the
    // deleting destructor of every ImageBuffer class calls it. (0x180006a10 —
    // the address `context_lifecycle.cpp` registers — is NOT this: it is the
    // shared unexported guarded free that IO, Save, Input, Object and Engine
    // all reach by address.)
    static void          operator delete(void* p) { ::Object::detail::InternalMemFree(p); }

    // ---- PUBLIC BY NECESSITY, ENDGAME B.3.5 block VC ----------------------
    // `~ImageBuffer` is non-virtual and PROTECTED in retail, and the two platform
    // halves are PRIVATE (`AEAA`) — yet all three are exported by name and keep
    // registered `extern "C"`-style doors in `src/engine/video/buffers.cpp`, and
    // a door is a free function that cannot name a protected member. They are
    // therefore public, exactly as `Video::VertexLayout` made the same three
    // public in b35-B and for the same reason. The CONSTRUCTOR stays protected.
    //
    void PlatformConstruct();                  // 0x1800f2f40 (was 0x1800f2ba0) — buffers.cpp
    void PlatformDestruct();                   // 0x1800f3020 (was 0x1800f2c40) — buffers.cpp

    // ??0ImageBuffer@Video@@QEAA@XZ 0x1800f2580 — the REAL constructor
    // (ENDGAME B.3.8 block RB), PUBLIC: the class is concrete. CompiledExternal()
    // and the stamp are the compiler's; the field stores and `PlatformConstruct`
    // are `InitFields`. ENDGAME B.3.9 VS: the ctor door is gone.
    ImageBuffer() { InitFields(); }

private:
    // The ctor / dtor bodies minus the compiler's stores, out of line (buffers.cpp)
    // only so the unit tests can link recording stand-ins (no Video::Device there).
    void InitFields();      // 0x1800f2580 minus the External stores + stamp
    void ReleaseFields();   // 0x1800f2610 minus the stamps + ~External

protected:
    // The registered doors over the protected surface. See the banner on the
    // forward declaration above `namespace RtVideo`.
    friend struct ::ImageBufferDoorAccess;

    // ?PlatformCreate@ (private in retail, `AEAA`) and ?PlatformSerialize@ stay
    // behind the friend holder: `PlatformSerialize` has a registered door and one
    // in-class caller, both of which reach it through `ImageBufferDoorAccess`
    // (B34_BRIEFS A.4 — friend the holder rather than widen the access, so the
    // header keeps saying what retail says).
    // Addresses corrected from the export table — see the banner above.
    bool PlatformCreate(LevelArray* levels);   // 0x1800f3110 (was 0x1800f2c90) — not ported
    void PlatformSerialize(IO::Serializer& serializer);   // 0x1800f3130

    // RULE B5-15: both literals are COPIED from B35_SLOTS section 3 into the
    // DEFINE_TYPE (video_type_records.cpp):
    //   0x18001e8c0  ?_TypeName@ImageBuffer@Video@@       -> "ImageBuffer"
    //   0x18001e8d0  ?_ParentTypeName@ImageBuffer@Video@@ -> "Object::CompiledExternal"

public:
    // ---- the 0x48 bytes ImageBuffer adds ---------------------------------
    // PUBLIC for the reason `Object::External`'s `_path`/`_refs` are: the ported
    // bodies, the three `Graphics::*ImageBuffer` subclasses and `src/render/dx11/` read
    // these at exactly these offsets. Field for field the same bytes as
    // `RtVideo::ImageBuffer` above, cross-asserted below.
    int32_t     _width;        // +0x20  raw; `Width()` is NOT this field
    int32_t     _height;       // +0x24  raw; `Height()` is NOT this field
    ImageType   _dimension;    // +0x28  0 = 2D, 1 = cube
    Usage       _kind;         // +0x2C  bind-flag selector; init 2
    Format      _format;       // +0x30
    uint32_t    _flags;        // +0x34  see the POD's comment for the bit map
    AddressMode _addressU;     // +0x38
    AddressMode _addressV;     // +0x3C
    AddressMode _addressW;     // +0x40
    FilterMode  _filter;       // +0x44  init 1
    // +0x48 is a `System::String`, NOT a pixel buffer — proved by
    // `?Serialize@ImageBuffer@Video@@` handing `this+0x48` to
    // `?Serialize@String@System@@`. B4 X-RH: a `System::String` member. Retail's
    // ctor 0x1800f2580 zeroes it before PlatformConstruct; retail's dtor
    // 0x1800f2610 runs PlatformDestruct (0x1800f2630), THEN the inlined ~String
    // on +0x48 (0x1800f2636..0x1800f2658), then ~External (0x1800f2672). The
    // member's ctor runs before InitFields and its dtor after ReleaseFields:
    // that order, with ONE release (the compiler's; buffers.cpp no longer
    // releases it by hand). The POD calls the same eight bytes `pixelData`,
    // which is the name Wave 18 proved wrong; the real class uses the proved one.
    System::String _sourceName;   // +0x48  ctor 0x1800f2580, dtor 0x1800f2636
    // +0x50 / +0x58 are the links of the `_firstSurface` circular list that
    // PlatformConstruct (0x1800f2f40) splices `this` into and the device-lost
    // sweeps walk (+0x50 is written with the head, +0x58 with the head's old
    // tail). B.4 (R-VIDEO), H4: typed as the ImageBuffer pointers they hold; the
    // guess names stay (tests/unit/video_imagebuffer_abi_test.cpp pins them).
    ImageBuffer* _field50;     // +0x50  list link (next)
    ImageBuffer* _field58;     // +0x58  list link (prev; the one the sweeps step)
    // The `Video::ImageBufferData*` the Device factory hands back (`DeviceData`
    // slot 10, +0x50 — `Dx11_CreateImageBufferData`). THIS is the field
    // `src/render/dx11/dx11_backend_access.h`'s deleted `ImageBufferOwner` view pinned
    // at +0x60 (RULE B5-2.1).
    ImageBufferData* _data;    // +0x60
};

static_assert(sizeof(ImageBuffer) == 0x68,
              "Video::ImageBuffer is 0x68 bytes (MOV EDX,0x68 in the slot-0 "
              "vector deleting dtor at 0x18001ed20)");
static_assert(offsetof(ImageBuffer, _width) == 0x20, "ImageBuffer::_width");
static_assert(offsetof(ImageBuffer, _height) == 0x24, "ImageBuffer::_height");
static_assert(offsetof(ImageBuffer, _dimension) == 0x28, "ImageBuffer::_dimension");
static_assert(offsetof(ImageBuffer, _kind) == 0x2C, "ImageBuffer::_kind");
static_assert(offsetof(ImageBuffer, _format) == 0x30, "ImageBuffer::_format");
static_assert(offsetof(ImageBuffer, _flags) == 0x34, "ImageBuffer::_flags");
static_assert(offsetof(ImageBuffer, _addressU) == 0x38, "ImageBuffer::_addressU");
static_assert(offsetof(ImageBuffer, _addressV) == 0x3C, "ImageBuffer::_addressV");
static_assert(offsetof(ImageBuffer, _addressW) == 0x40, "ImageBuffer::_addressW");
static_assert(offsetof(ImageBuffer, _filter) == 0x44, "ImageBuffer::_filter");
static_assert(offsetof(ImageBuffer, _sourceName) == 0x48, "ImageBuffer::_sourceName");
static_assert(alignof(ImageBuffer) == 8, "Video::ImageBuffer is qword-aligned");

#define RT_VIDEO_IMAGEBUFFER_PIN(MEMBER, POD_MEMBER, OFFSET)                   \
    static_assert(offsetof(ImageBuffer, MEMBER) == (OFFSET),                   \
                  "Video::ImageBuffer::" #MEMBER " @ +" #OFFSET)

RT_VIDEO_IMAGEBUFFER_PIN(_width,      width,        0x20);
RT_VIDEO_IMAGEBUFFER_PIN(_height,     height,       0x24);
RT_VIDEO_IMAGEBUFFER_PIN(_dimension,  dimension,    0x28);
RT_VIDEO_IMAGEBUFFER_PIN(_kind,       kind,         0x2C);
RT_VIDEO_IMAGEBUFFER_PIN(_format,     format,       0x30);
RT_VIDEO_IMAGEBUFFER_PIN(_flags,      flags,        0x34);
RT_VIDEO_IMAGEBUFFER_PIN(_addressU,   addressU,     0x38);
RT_VIDEO_IMAGEBUFFER_PIN(_addressV,   addressV,     0x3C);
RT_VIDEO_IMAGEBUFFER_PIN(_addressW,   addressW,     0x40);
RT_VIDEO_IMAGEBUFFER_PIN(_filter,     filter,       0x44);
RT_VIDEO_IMAGEBUFFER_PIN(_sourceName, pixelData,    0x48);
RT_VIDEO_IMAGEBUFFER_PIN(_field50,    field50,      0x50);
RT_VIDEO_IMAGEBUFFER_PIN(_field58,    field58,      0x58);
RT_VIDEO_IMAGEBUFFER_PIN(_data,       platformData, 0x60);

#undef RT_VIDEO_IMAGEBUFFER_PIN

}  // namespace Video

// src/engine/video/closure_batch2b.cpp — 0x180054080, the shared
// `Array<Array<uint8>>` blob serializer (unexported; three retail callers, one
// of them ImageBuffer::PlatformSerialize). B.4 (R-VIDEO): typed.
void RtVideo_SerializeBlobArray(IO::Serializer& ser, int count,
                                Collection::Array<uint8_t>* data);
