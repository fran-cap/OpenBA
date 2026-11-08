#pragma once
// ============================================================================
// src/engine/video/RenderState.h — `Video::RenderState` as a real C++ class,
// with its three nested value types `AlphaBlend` / `Depth` / `Stencil`.
//
// ENDGAME B.3.5 block C. RULES B5-15 (the two type-name literals are COPIED
// from B35_SLOTS, not derived from the C++ base name), B5-16 (`Construct` is
// introduced by this branch at slot 8, so `virtual` and not `override`).
// ============================================================================
//
// Offsets read out of RETAIL Runtime-x64.dll `objdump -d`, 2026-09-12; the
// vtable, the slot list, `sizeof` and the two `_TypeName` literals re-read from
// bytes by block 0 and tabulated in `B35_SLOTS.md` (`Video::RenderState`,
// vtable 0x180157720, 9 slots, sizeof 0x80).
//
// *** RenderState DERIVES FROM Object::External DIRECTLY, NOT FROM
//     Object::CompiledExternal. *** Every other Video external in this tree
//     (ImageBuffer, VertexLayout, ShaderProgram and its two subclasses) stamps
//     ??_7CompiledExternal@Object@@6B@ (0x180154a30) between External's vtable
//     and its own. ??0RenderState@Video@@QEAA@XZ (0x1800f4b60) does NOT: it
//     goes Base (0x180154788) -> External (0x1801547a0) -> RenderState
//     (0x180157720), and ??1RenderState (0x1800f4c30) restores only the
//     RenderState vtable before tail-jumping straight to ??1External
//     (0x1800bc950). ?_ParentTypeName@RenderState@Video@@ (0x18004b540)
//     returns the literal "Object::External", where ShaderProgram's
//     (0x18004e090) returns "Object::CompiledExternal" — the export table says
//     the same thing the ctor does. B35_RULES 2.1 lists this as the one
//     direct-`External` Video class with a documented header, and the class
//     declaration below is what finally makes the distinction structural
//     instead of a comment.
//
// THE FIELD OFFSETS ARE PINNED BY ?Initialize@RenderState@Video@@ (0x1800f4840),
// which is nothing but a field-by-field copy of the three sub-structs into
// this object, so it states each sub-struct's size and position at once:
//
//   RDX -> +0x20, five dwords         AlphaBlend  (0x14 bytes)
//   R8  -> +0x34, four dwords         Depth       (0x10 bytes)
//   R9  -> +0x44, nine dwords         Stencil     (0x24 bytes)
//   [RSP+0x28] -> +0x68               cullMode
//   [RSP+0x30] -> +0x6c               fillMode   (IsWireframe 0x18004b730 is
//                                                 `CMP dword [RCX+0x6c],1`)
//   [RSP+0x38] -> +0x70               a bool
//   then `_data->vtable[1](_data)`    PlatformCreate
//
// and the ctor's defaults agree field for field:
//   +0x20 = 0            +0x24 = 1   +0x28 = 0   +0x2c = 0   +0x30 = 0xf
//   +0x34 = 1 (byte)     +0x35 = 1   +0x38 = 4   +0x3c = 0   +0x40 = 0
//   +0x44 .. +0x67 = 0
//   +0x68 = 1            +0x6c = 0   +0x70 = 0
//   +0x78 = Device::_singleton->deviceData ? dd->vtable[0x88](dd, this) : null
//
// sizeof is 0x80: the vector deleting destructor at 0x18004b9d0 carries
// `MOV EDX,0x80`, and rt_factories_video.inc's CreateNewObject step asks the
// allocator for 0x80 with alignment 8.
//
// ----------------------------------------------------------------------------
// THE `rt::video::RenderState` POD IS GONE, NOT MOVED (B4b-1)
// ----------------------------------------------------------------------------
// B4b-1 says a POD occupying the retail name moves into an `Rt*` namespace and
// lives there until its LAST user converts. `rt::video::{RenderState,
// AlphaBlend, Depth, Stencil}` had exactly one user in the whole tree —
// `src/engine/video/renderstate.cpp`, verified by grep over `code/ engine/
// runtime/ standalone/ src/render/dx11/` — and that file converts in this same commit. So
// there is no second spelling to keep alive and nothing outside this header to
// update: the classes below are the ONE definition of these bytes. (Contrast
// `RtVideo::ImageBuffer` in ImageBuffer.h, whose POD has five unconverted users
// and therefore stays.)
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "../object/External.h"
#include "../object/Internal.h"                // Object::detail::InternalMemFree
// `_data` is a real `Video::RenderStateData*` (ENDGAME B.3.5 block A, RULE
// B5-1), which is what lets `renderstate.cpp` call `Create()` / `Bind()` /
// `DeletingDtor()` on it as virtuals instead of hand-indexing +0x08/+0x10/+0x00,
// and `Video::DeviceData::CreateRenderStateData` as slot 17 by name.
#include "CompanionData.h"

namespace IO { class Serializer; }

// `src/engine/video/renderstate.cpp`'s door helper. `?PlatformConstruct@` /
// `?PlatformCreate@` / `?PlatformDestruct@` are PRIVATE in retail (`AEAA`) and
// `~RenderState` is non-virtual and protected, yet all four are exported by name
// and keep registered `extern "C"` doors — which cannot name a protected member.
// B34_BRIEFS A.4's answer is to FRIEND the holder rather than widen the access,
// so the header keeps saying what retail says. A derived helper class would have
// worked too and does NOT work here: deriving forces GCC to reference
// `vtable for Video::RenderState`, whose slot 0 (`DeletingDtor`) is a table row
// in `src/engine/object/deleting_dtor.cpp` and is defined in no translation
// unit, so the link fails (seen, not predicted).
struct RenderStateDoorAccess;

namespace Video {

// ============================================================
// Video::RenderState — one compiled render-state resource. 0x80 bytes,
// `Object::External` base (see the banner).
//
// ENDGAME B.3 step 9, block VID (B39_RULES.md D1/D2): CONCRETE. Retail's real
// public constructor (??0RenderState 0x1800f4b60), its own slot 0, and the
// factory pair as its own statics: every live RenderState wears the COMPILER's
// table. The minted row, its RT_DELDTOR row 0x18004b9d0, the ??0 / ??1 doors and
// the seven slot doors are retired (tools/rt_mint_retired/vid.txt).
//
// THE DESTRUCTOR IS VIRTUAL (B3X W1): retail slot 0 is
// the deleting destructor. `~RenderState()` is D1 + D0 here.
// ============================================================
class RenderState : public Object::External {
public:
    // The two 4-byte enums `?Initialize@` names (`W4CullMode@12@`,
    // `W4FillMode@12@`, i.e. enums NESTED in `Video::RenderState`). THE
    // ENUMERATOR NAMES ARE NOT RECOVERABLE from the binary — only the ctor
    // defaults (cull 1, fill 0) and `IsWireframe`'s `fill == 1` — so, exactly
    // as `Object::External::CompileResult` and `Video::VertexLayout::Usage` do
    // for the same reason, they are spelled as the 32-bit integers they are
    // rather than as enums with invented names.
    using CullMode = uint32_t;
    using FillMode = uint32_t;

    // The number of values each serialized field may take: the entries of the
    // backend's mapping table (VideoDX11 0x1800037c0.. builds them on its stack).
    static constexpr uint32_t kBlendFactorCount = 10;
    static constexpr uint32_t kBlendOpCount     = 3;
    static constexpr uint32_t kCompareFuncCount = 8;
    static constexpr uint32_t kStencilOpCount   = 8;
    static constexpr uint32_t kCullModeCount    = 3;
    static constexpr uint32_t kFillModeCount    = 2;

    // NOT RETAIL (UB, out-of-bounds index; review S-042): retail indexes those
    // tables with the raw fields of the .rsc and has no check, so a corrupt or mod
    // value reads past a table. In range, a value indexes itself; out of range it
    // takes `fallback` (the field's constructor default).
    static constexpr uint32_t TableIndex(uint32_t value, uint32_t count, uint32_t fallback) {
        return value < count ? value : fallback;
    }

    // ========================================================
    // Video::RenderState::AlphaBlend — 0x14 bytes. A `struct`: the mangled
    // `?Initialize@` spells `AEBUAlphaBlend@12@`, `U` and not `V`.
    //
    // ?Serialize@AlphaBlend@RenderState@Video@@ (0x1800f4aa0):
    //   Bool(+0x00), UInt(+0x04), UInt(+0x08), UInt(+0x0c), UInt(+0x10)
    // ??0AlphaBlend@RenderState@Video@@ (0x1800f4b30): +0x00=0, +0x04=1,
    //   +0x08=0, +0x0c=0, +0x10=0xf.
    // ========================================================
    struct AlphaBlend {
        // ??0AlphaBlend@RenderState@Video@@QEAA@XZ  0x1800f4b30. A REAL
        // exported constructor, and unlike the polymorphic classes in this file
        // it may be one: the struct has no vtable, so converting its ctor
        // cannot change which table a live object wears (the same licence
        // `IO::Serializer` is granted in src/engine/io/Serializer.h).
        // Defined inline since ENDGAME B.3.9 (RenderState's real ctor runs it).
        AlphaBlend()
            : enabled(0), _pad01{}, srcBlend(1), dstBlend(0), blendOp(0), writeMask(0xf) {}
        // ?Serialize@AlphaBlend@RenderState@Video@@QEAAXAEAVSerializer@IO@@@Z
        //   0x1800f4aa0
        void Serialize(IO::Serializer& ser);

        bool     enabled;    // +0x00  (B.4 R-VIDEO: bool, H4 — the Bool slot)
        uint8_t  _pad01[3];  // +0x01  never written (the ctor stores a BYTE at +0)
        uint32_t srcBlend;   // +0x04  default 1
        uint32_t dstBlend;   // +0x08  default 0
        uint32_t blendOp;    // +0x0c  default 0
        uint32_t writeMask;  // +0x10  default 0xf (all four channels)
    };

    // ========================================================
    // Video::RenderState::Depth — 0x10 bytes.
    //
    // ?Serialize@Depth@RenderState@Video@@ (0x1800f4a00):
    //   Bool(+0x00), Bool(+0x01), UInt(+0x04), Float(+0x08), Float(+0x0c)
    // ??0Depth@RenderState@Video@@ (0x1800f4a80): word at +0x00 = 0x0101 (so
    //   both bools are 1), qword at +0x04 = 4 (func = 4, +0x08 = 0), +0x0c = 0.
    // ========================================================
    struct Depth {
        Depth()                              // 0x1800f4a80
            : test(1), write(1), _pad02{}, func(4), bias(0.0f), slopeBias(0.0f) {}
        void Serialize(IO::Serializer& ser); // 0x1800f4a00

        bool     test;       // +0x00  default 1 (B.4 R-VIDEO: bool, H4)
        bool     write;      // +0x01  default 1 (B.4 R-VIDEO: bool, H4)
        uint8_t  _pad02[2];  // +0x02
        uint32_t func;       // +0x04  default 4
        float    bias;       // +0x08  default 0
        float    slopeBias;  // +0x0c  default 0
    };

    // ========================================================
    // Video::RenderState::Stencil — 0x24 bytes.
    //
    // ?Serialize@Stencil@RenderState@Video@@ (0x1800f48e0):
    //   Bool(+0x00), Bool(+0x01), UInt(+0x04), UChar(+0x08),
    //   UInt(+0x0c), UInt(+0x10), UInt(+0x14), UInt(+0x18), UInt(+0x1c),
    //   UInt(+0x20)
    // ??0Stencil@RenderState@Video@@ (0x1800f49d0) zeroes the whole 0x24.
    //
    // `reference` IS SERIALIZED AS A UChar among eight UInts. A UInt there
    // would move the next eight fields by three bytes in every saved file, so
    // the asymmetry is pinned by its own assert below and must not be tidied.
    //
    // B5 RES, NAMES CORRECTED FROM THE ONE CONSUMER. The serializer shape above
    // gives widths, not meanings; the earlier names (readMask / writeMask /
    // func / failOp / depthFailOp / passOp / backFunc / reference at +0x04..
    // +0x20) were guesses from that order. VideoDX11's RenderStateData::Create
    // (0x180003760) and ::Bind (0x180003a90) are what READ the fields, and they
    // say (src/render/dx11/renderstatedata.cpp; the deleted `VideoRenderState` mirror
    // there carried the same map):
    //   +0x04 -> D3D11 StencilFunc of the FRONT face AND, two-sided, the BACK
    //            face (0x180003a10 re-reads it): the one compare function
    //   +0x08 -> OMSetDepthStencilState's StencilRef (`MOVZX R8D,byte [+0x4c]`
    //            in Bind) -- the byte field, hence the UChar
    //   +0x0c/+0x10/+0x14 -> FrontFace Stencil{Fail,DepthFail,Pass}Op
    //   +0x18/+0x1c/+0x20 -> BackFace  Stencil{Fail,DepthFail,Pass}Op (two-sided)
    // The read/write masks are NOT stored here: Create hard-codes both to 0xff
    // (`MOV word [RSP+0x30],0xffff` @ 0x180003986). Offsets, widths and the
    // serialize order are unchanged; only the spellings moved.
    // ========================================================
    struct Stencil {
        Stencil()                            // 0x1800f49d0 — zeroes the whole 0x24
            : enabled(0), twoSided(0), _pad02{}, func(0), reference(0), _pad09{},
              failOp(0), depthFailOp(0), passOp(0),
              backFailOp(0), backDepthFailOp(0), backPassOp(0) {}
        void Serialize(IO::Serializer& ser); // 0x1800f48e0

        bool     enabled;          // +0x00  (B.4 R-VIDEO: bool, H4)
        bool     twoSided;         // +0x01  (B.4 R-VIDEO: bool, H4)
        uint8_t  _pad02[2];        // +0x02
        uint32_t func;             // +0x04  compare function, both faces
        uint8_t  reference;        // +0x08  stencil ref; serialized as UChar, not UInt
        uint8_t  _pad09[3];        // +0x09
        uint32_t failOp;           // +0x0c  front face
        uint32_t depthFailOp;      // +0x10  front face
        uint32_t passOp;           // +0x14  front face
        uint32_t backFailOp;       // +0x18  back face (two-sided only)
        uint32_t backDepthFailOp;  // +0x1c  back face (two-sided only)
        uint32_t backPassOp;       // +0x20  back face (two-sided only)
    };

    // ??0RenderState@Video@@QEAA@XZ 0x1800f4b60 -- the REAL constructor.
    // External() (Base's and External's vptrs, +0x08..+0x1f zeroed), NO
    // CompiledExternal, RenderState's vptr (the compiler's), the three nested
    // value types' ctors inlined in field order, cull 1 / fill 0 / flag 0, then
    // PlatformConstruct fills +0x78. +0x71..+0x77 are never written.
    RenderState() : Object::External(), _alphaBlend(), _depth(), _stencil() {
        _cullMode = 1;         // +0x68
        _fillMode = 0;         // +0x6c
        _scissorEnable   = 0;         // +0x70
        PlatformConstruct();   // +0x78
    }

    // ---- the nine slots, in measured order ------------------------------
    // Slot 4 is `Object::External::Compile`, INHERITED (retail's slot 4 is
    // 0x1800066e0, External's own body), so it is not redeclared here.
    //
    // [0] 0x18004b9d0 -- MSVC's VECTOR deleting destructor (`MOV EDX,0x80`, the
    // eh-vector dtor over ~RenderState 0x1800f4c30), both arms as the retired
    // RT_DELDTOR row ran them. Nothing `new[]`s one.
    // ??1RenderState@Video@@UEAA@XZ (0x1800f4c30). VIRTUAL
    // (B3X W1).
    ~RenderState() override;
    // [1] 0x18004b570 TypeName / [2] 0x18004b550 Type, TypeId 0x18004b560, `_TypeName` / `_ParentTypeName` and the factory pair:
    // retail's macro (ENDGAME B.3.9 TI-REST, B39_RULES D6); the record is
    // `DEFINE_TYPE`d in src/engine/video/video_type_records.cpp.
    DECLARE_TYPE(RenderState)
public:
    int32_t           GetFileVersion() const override;        // [3] 0x1800f4c80 — `MOV EAX,3`
    bool              Serialize(IO::Stream& stream) override;       // [5] 0x1800f4c90
    Object::External* Reconstruct() override;                 // [6] 0x18004b6f0
    void              Destruct() override;                    // [7] 0x18004b720
    // [8] 0x18004b710. `virtual`, NOT `override`: `Object::External`'s retail
    // table is eight entries, so this branch introduces `Construct` itself
    // (RULE B5-16).
    virtual Object::External* Construct();

    // ---- the exported non-virtual members --------------------------------
    // ?Initialize@RenderState@Video@@QEAAXAEBUAlphaBlend@12@AEBUDepth@12@
    //             AEBUStencil@12@W4CullMode@12@W4FillMode@12@_N@Z  0x1800f4840
    void Initialize(const AlphaBlend& alphaBlend, const Depth& depth,
                    const Stencil& stencil, CullMode cullMode,
                    FillMode fillMode, bool flag);
    bool IsWireframe() const;              // ?IsWireframe@ 0x18004b730
    void Bind(void* context) const;        // ?Bind@        0x1800f4ec0

    // `??0RenderState@Video@@QEAA@AEBV01@@Z` (0x18004b640) and
    // `??4RenderState@Video@@QEAAAEAV01@AEBV01@@Z` (0x18004b750) are REAL,
    // EXPORTED bodies, not compiler defaults — the situation RULE B5-10
    // records for `Graphics::Geometry`. Declared, and deliberately left
    // UNDEFINED in this block: nothing in the tree calls either, and a
    // `= default` here would silently substitute a bitwise copy (which would
    // alias `_data`, a singly-owned `RenderStateData*`) for whatever they do.
    RenderState(const RenderState& other);              // 0x18004b640 — not ported
    RenderState& operator=(const RenderState& other);   // 0x18004b750 — not ported

    // The static reflection companions retail emits are DECLARE_TYPE's. The
    // factory pair 0x18004b660 / 0x18004b6e0 is the class's own (B39_RULES D2),
    // defined in renderstate.cpp; the class declares its own placement
    // `operator new` (not ported), so placement spells `::new`.
    static void*         operator new(size_t size);             // ??2 — not ported
    static void*         operator new(size_t size, void* storage);
    static void          operator delete(void* p) { ::Object::detail::InternalMemFree(p); }              // ??3 — MemFree

protected:
    // The four registered doors over the protected surface. See the banner on
    // the forward declaration above this namespace.
    friend struct ::RenderStateDoorAccess;


    // The three private platform halves. `PlatformCreate` returns void here,
    // unlike the shader programs' `_N` (bool) form — the mangling says so and
    // so does the tail-jump's discarded result.
    void PlatformConstruct();   // ?PlatformConstruct@ 0x1800f4e30
    void PlatformCreate();      // ?PlatformCreate@    0x1800f4ea0
    void PlatformDestruct();    // ?PlatformDestruct@  0x1800f4e80

    // RULE B5-15: both literals are COPIED from B35_SLOTS section 3 into the
    // DEFINE_TYPE (video_type_records.cpp):
    //   0x18004b530  ?_TypeName@RenderState@Video@@        -> "RenderState"
    //   0x18004b540  ?_ParentTypeName@RenderState@Video@@  -> "Object::External"

public:
    // ---- the 0x60 bytes RenderState adds ---------------------------------
    // PUBLIC for the reason `Object::External`'s `_path`/`_refs` are: the
    // ported bodies in `src/engine/video/` and the unconverted callers reach
    // these at exactly these offsets.
    AlphaBlend       _alphaBlend; // +0x20
    Depth            _depth;      // +0x34
    Stencil          _stencil;    // +0x44
    CullMode         _cullMode;   // +0x68  default 1
    FillMode         _fillMode;   // +0x6c  default 0; ==1 means wireframe
    bool             _scissorEnable;     // +0x70  (B.4 R-VIDEO: bool, H4 — Initialize's `_N` flag)
    uint8_t          _pad71[7];   // +0x71  never written
    RenderStateData* _data;       // +0x78  from DeviceData slot 17
};

static_assert(sizeof(RenderState) == 0x80,
              "Video::RenderState is 0x80 (MOV EDX,0x80 at 0x18004ba02)");
static_assert(offsetof(RenderState, _pad71) == 0x71, "RenderState::_pad71");
static_assert(alignof(RenderState) == 8, "Video::RenderState is qword-aligned");
static_assert(offsetof(RenderState, _alphaBlend) == 0x20, "RenderState::_alphaBlend @ +0x20");
static_assert(offsetof(RenderState, _depth)      == 0x34, "RenderState::_depth @ +0x34");
static_assert(offsetof(RenderState, _stencil)    == 0x44, "RenderState::_stencil @ +0x44");
static_assert(offsetof(RenderState, _cullMode)   == 0x68, "RenderState::_cullMode @ +0x68");
static_assert(offsetof(RenderState, _fillMode)   == 0x6c, "RenderState::_fillMode @ +0x6c");
static_assert(offsetof(RenderState, _scissorEnable)     == 0x70, "RenderState::_scissorEnable @ +0x70");
static_assert(offsetof(RenderState, _data)       == 0x78, "RenderState::_data @ +0x78");

static_assert(sizeof(RenderState::AlphaBlend) == 0x14,
              "RenderState::AlphaBlend is 0x14 (5 dwords copied by Initialize)");
static_assert(offsetof(RenderState::AlphaBlend, enabled) == 0x0, "AlphaBlend::enabled");
static_assert(offsetof(RenderState::AlphaBlend, _pad01) == 0x1, "AlphaBlend::_pad01");
static_assert(offsetof(RenderState::AlphaBlend, srcBlend) == 0x4, "AlphaBlend::srcBlend");
static_assert(offsetof(RenderState::AlphaBlend, dstBlend) == 0x8, "AlphaBlend::dstBlend");
static_assert(offsetof(RenderState::AlphaBlend, blendOp) == 0xC, "AlphaBlend::blendOp");
static_assert(sizeof(RenderState::Depth) == 0x10,
              "RenderState::Depth is 0x10 (4 dwords copied by Initialize)");
static_assert(offsetof(RenderState::Depth, test) == 0x0, "Depth::test");
static_assert(offsetof(RenderState::Depth, write) == 0x1, "Depth::write");
static_assert(offsetof(RenderState::Depth, _pad02) == 0x2, "Depth::_pad02");
static_assert(offsetof(RenderState::Depth, bias) == 0x8, "Depth::bias");
static_assert(offsetof(RenderState::Depth, slopeBias) == 0xC, "Depth::slopeBias");
static_assert(sizeof(RenderState::Stencil) == 0x24,
              "RenderState::Stencil is 0x24 (9 dwords copied by Initialize)");
static_assert(offsetof(RenderState::Stencil, enabled) == 0x0, "Stencil::enabled");
static_assert(offsetof(RenderState::Stencil, twoSided) == 0x1, "Stencil::twoSided");
static_assert(offsetof(RenderState::Stencil, _pad02) == 0x2, "Stencil::_pad02");
static_assert(offsetof(RenderState::Stencil, func) == 0x4, "Stencil::func");
static_assert(offsetof(RenderState::Stencil, _pad09) == 0x9, "Stencil::_pad09");
static_assert(offsetof(RenderState::Stencil, failOp) == 0xC, "Stencil::failOp");
static_assert(offsetof(RenderState::Stencil, depthFailOp) == 0x10, "Stencil::depthFailOp");
static_assert(offsetof(RenderState::Stencil, passOp) == 0x14, "Stencil::passOp");
static_assert(offsetof(RenderState::Stencil, backFailOp) == 0x18, "Stencil::backFailOp");
static_assert(offsetof(RenderState::Stencil, backDepthFailOp) == 0x1C, "Stencil::backDepthFailOp");
static_assert(alignof(RenderState::AlphaBlend) == 4, "AlphaBlend is dword-aligned");
static_assert(alignof(RenderState::Depth) == 4, "Depth is dword-aligned");
static_assert(alignof(RenderState::Stencil) == 4, "Stencil is dword-aligned");
static_assert(offsetof(RenderState::AlphaBlend, writeMask) == 0x10, "AlphaBlend::writeMask @ +0x10");
static_assert(offsetof(RenderState::Depth, func) == 0x04, "Depth::func @ +0x04");
static_assert(offsetof(RenderState::Stencil, reference) == 0x08,
              "Stencil::reference @ +0x08 — serialized as UChar (0x1800f48e0)");
static_assert(sizeof(RenderState::Stencil::reference) == 1,
              "Stencil::reference is the byte the UChar serializer and dx11's "
              "MOVZX byte [+0x4c] read");
static_assert(offsetof(RenderState::Stencil, backPassOp) == 0x20, "Stencil::backPassOp @ +0x20");

// `?Initialize@` copies each sub-struct dword by dword, INCLUDING the padding
// bytes after `AlphaBlend::enabled` and `Depth::test`/`write`. The member
// bodies express that as a struct assignment, which is only the same bytes
// while all three stay trivially copyable — asserted rather than assumed. A
// user-declared default constructor (above) does not affect this.
static_assert(std::is_trivially_copyable<RenderState::AlphaBlend>::value,
              "Initialize copies AlphaBlend as bytes");
static_assert(std::is_trivially_copyable<RenderState::Depth>::value,
              "Initialize copies Depth as bytes");
static_assert(std::is_trivially_copyable<RenderState::Stencil>::value,
              "Initialize copies Stencil as bytes");

}  // namespace Video
