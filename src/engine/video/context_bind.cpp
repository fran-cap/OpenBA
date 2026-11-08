// ============================================================================
// src/engine/video/context_bind.cpp — Wave 9 lane CTX
//   ?BindResources@Context@Video@@AEAAXXZ   retail 0x1800ef060, ordinal 1664
//   ?BindTextures@Context@Video@@AEAAXXZ    retail 0x1800ef330, ordinal 1667
//
// These are the two `Video::Context` methods the FRAME LOOP actually calls, and
// until this file they were the last unported ones on that path.
// ============================================================================
//
// HOW "THE FRAME LOOP HITS THIS FIRST" WAS ESTABLISHED — from disassembly and
// from our own src/render/dx11/, not from a guess
// ---------------------------------------------------------------------------
// Lane VID2 predicted the next wall as "?PlatformStartup@Context@Video@@ or the
// first Context method the frame loop calls". **There is no
// ?PlatformStartup@Context@Video@@** — the shipped export directory has
// ?PlatformConstruct@ (0x1800f21f0) and ?PlatformDestruct@ (0x1800f2240) and
// nothing else with that shape. The second half of the prediction is the live
// one, and the answer is not in Engine::MainLoop::Draw at all:
//
//   ?Draw@MainLoop@Engine@@QEAAXM@Z  (0x1800dcd40) reaches exactly two Context
//   methods — `?Reset@` on the failure path (tail JMP 0x1800dcd82) and
//   `?ClearResources@` at 0x1800dcd9f — and BOTH are already ported (Wave 6
//   lane A and Wave 9 lane VID2 respectively). Everything else it does is a
//   virtual call into `Context+0x2160`, which is OUR src/render/dx11/ ContextData.
//
// So the frame's Context traffic comes from the OTHER side: our own DX11
// backend calls back INTO Video::Context, by name, on the hot path.
// src/render/dx11/contextdata.cpp resolves `?BindResources@` and `?BindTextures@`
// through `Dx11ResolveRuntimeImport` and calls them at four sites:
//
//   contextdata.cpp:776   BindTextures  — slot 5, ContextData_BindRenderBuffer
//   contextdata.cpp:953   BindResources — the indexed/instanced draw
//   contextdata.cpp:1092  BindResources — the dynamic-vertex draw
//   contextdata.cpp:1253  BindResources — the dynamic indexed draw
//
// `BindResources` therefore runs ONCE PER DRAW CALL. It is the most-executed
// `Video::Context` method in the process, and before this file every one of
// those calls left our own renderer and entered the shipped DLL.
//
// It is also, unusually, a method with NO Runtime-internal caller at all.
// get_xrefs_to(0x1800ef060) names four DATA references (the export tables) and
// nothing else; get_xrefs_to(0x1800ef330) adds exactly one CALL, from
// BindResources itself at 0x1800ef093. Everything that calls either of these
// lives in the video plugin — which is us. **There is no split-ownership
// hazard here at all**: after this registration our backend is the only caller
// in the process and it reaches only our body.
//
// TRANSITIVE CLOSURE, COUNTED BEFORE ANYTHING WAS WRITTEN
// -------------------------------------------------------
// BindResources contains exactly one direct CALL (`CALL 0x1800ef330`, which is
// BindTextures); every other call it makes is `CALL qword ptr [reg+imm]`, i.e.
// a virtual dispatch through a pointer the Context already holds. BindTextures
// contains no direct call at all. **Closure = these two functions, and ZERO new
// resolve doors** — the port adds no name to `unsatisfied_export_scan.py`'s
// denominator, which is the property lane PICK's five-rows-cost-fourteen note
// asks to be measured up front rather than discovered afterwards.
//
// THE ORACLE, CHOSEN BEFORE THE FIRST BODY, AND WHAT WAS REJECTED
// ---------------------------------------------------------------
// Neither function can be REPLAYED on a live Context the way Wave 6 lane A
// replayed the setters. A setter is a pure state transformation, so running it
// twice on two copies is free; these two ISSUE COMMANDS — every arm ends in a
// virtual call into the backend, and running them twice would bind every
// resource to D3D twice and (via the dirty-bit clears) make the second pass see
// a different input from the first.
//
// REJECTED — a live `RT_VIDCTXBIND` A/B. It can only report "did it
// boot and did the picture look right", which is exactly the
// executed-but-undiffed state reference_uncalled_port_is_not_verified.md warns
// about one door over; it cannot tell me that arm 0x1000 uploaded 0x20 bytes
// from +0x3f8 rather than 0x40 bytes from +0x208.
//
// REJECTED — comparing only the Context bytes afterwards. Both bodies write
// just three fields (+0x10, +0x18, +0x1c and the qword at +0x20), so a
// byte-only comparison is blind to the entire point of the function: WHICH
// backend entry points were called, in WHICH ORDER, with WHICH ARGUMENTS. That
// is the "lossy transform" trap — the comparison point cannot see the work.
//
// CHOSEN — a synthetic subject plus a CALL LOG. Two `.bss` Contexts, one set of
// fake sub-objects SHARED by both sides (so every recorded pointer argument
// except the Context itself compares directly), and a recorder on every vtable
// slot. Ours runs on one copy, the shipped body on the other, and the probe
// compares the full 0x2168 bytes AND the two call sequences element by element
// — tag, slot index, and all five arguments, with pointer arguments that land
// inside the subject normalised to an OFFSET from that side's own base.
//
// That is lockstep, not end-state-only: a defect that sets a wrong dirty bit
// and then clears it compares equal at the end and differs in the log.
//
// THE FAKE TABLES ARE 64 SLOTS WIDE AND EVERY ENTRY IS IMPLEMENTED, and the
// probe PRINTS THE OBSERVED SET rather than the predicted one. Lane VID2 guessed
// a slot set three times and was wrong three times; the derivation below says
// slots 4,5,6,7,8,9,10,11 and 1 and 2, but the report is what was actually
// called, on both sides, as a bitmask.
//
// NOTHING IS EXCLUDED FROM THE BYTE COMPARISON. Both subjects are memcpy'd from
// one master image before each case, so every byte either side reads was
// written by the probe; there is no 0xCD region for the two to disagree about
// and therefore no tolerance to state.
//
// ---------------------------------------------------------------------------
// THE DIRTY MASK, COMPLETED
// ---------------------------------------------------------------------------
// BindResources is the only function that consumes the whole of Context+0x10,
// so reading it fills four gaps src/engine/video/Context.h left open, and
// corrects one name. Derived from the disassembly, arm by arm, in RETAIL ORDER
// (which is NOT bit order — note 0x8 before 0x4):
//
//   bit      arm at        does
//   0x00001  0x1800ef079   companion slot 5  (BindRenderBuffer)
//   0x00002  0x1800ef08a   CALL BindTextures (0x1800ef330)
//   0x00040  0x1800ef098   vertexProgram(+0x1f0)->+0x50 ->vt[2](that, ctx, detail)
//   0x00080  0x1800ef0b6   pixelProgram (+0x1f8)->+0x50 ->vt[2](that, ctx, detail)
//   0x00100  0x1800ef0d4   videoProgram (+0x200)->+0x40 ->vt[2](that, ctx, detail)
//   0x04000  0x1800ef0f5   companion slot 8  (BindTransforms)  ** see below **
//   0x00008  0x1800ef119   the sixteen vertex streams
//   0x00004  0x1800ef1b2   vertexLayout (+0xb0)->+0x30 ->vt[1](that, ctx)
//   0x00010  0x1800ef1cc   indexBuffer  (+0x1b8)->+0x08 ->vt[5](that, ctx)
//                          or companion slot 10 (BindIndexNone) when null
//   0x00020  0x1800ef1fa   renderState  (+0x1c8)->+0x78 ->vt[2](that, ctx)
//   0x00200  0x1800ef214   companion slot 6  (BindViewport)
//   0x00400  0x1800ef22a   companion slot 7  (BindScissor)
//   0x00800  0x1800ef240   globalConstants  (+0x1f78) upload +0x328  len 0xd0
//   0x01000  0x1800ef26b   instanceConstants(+0x1fc0) upload +0x3f8  len 0x20
//   0x02000  0x1800ef296   lightConstants   (+0x2098) upload +0x1dd8 len 0x1a0
//   0x08000  0x1800ef2c1   materialConstants(+0x0298) upload +0x208  len 0x40
//   0x10000  0x1800ef2ec   pixelConstants   (+0x02e0) upload +0x248  len 0x50
//
// FOUR CORRECTIONS TO Context.h, each derived rather than assumed:
//
//  * **+0x200 IS A POINTER, NOT STAGING.** Context.h models 0x200..0x297 as one
//    `_pixelConstantStaging[0x98]` blob. Arm 0x100 loads `[RBX+0x200]` and
//    dereferences it twice. `?SetVideoProgram@Context@Video@@` (0x18001a040) is
//    four instructions and settles it: it compares `[RCX+0x200]` against its
//    argument, ORs 0x100 into the dirty mask and stores the pointer. So +0x200
//    is `_videoProgram` and bit 0x100 is `kDirtyVideoProgram`. The staging blob
//    is 0x208..0x297, which is also exactly what `?Reset@` memsets (0x208 len
//    0x40, then 0x248 len 0x50) and exactly what arms 0x8000 and 0x10000 upload.
//  * bit **0x2000** is the LIGHT constants (upload of +0x1dd8, the block
//    `?SetLight@` writes), not "unknown".
//  * bit **0x8000** is the MATERIAL constants (+0x208 -> the buffer at +0x298).
//  * bit **0x1000** uploads +0x3f8 len 0x20 — `_inverseScale` and `_color` —
//    into the buffer at +0x1fc0, which Context.h calls `_instanceConstants`.
//    Context.h's enum calls this bit `kDirtyInstanceConstants` (formerly the
//    misnamed `kDirtyMaterialConstants`, which is now 0x8000).
//
// ** The 0x4000 arm is NOT a plain bit test. ** 0x1800ef0f5 reads
//
//     if ((dirty & 0x4000) || transformCount > 0 || _boneCount > 0)
//
// — a bit OR two SIGNED comparisons on the qword at +0x20, and the second
// comparison is on a field Context.h calls `_boneCount`. That is why the
// function's final store is `MOV qword ptr [RBX+0x20],RBP`: a QWORD write
// clearing BOTH +0x20 and +0x24 at once. Clearing only the dword at +0x20
// would leave a live transform binding that never re-binds again, and it is one
// of this file's negative controls.
//
// AND THE WHOLE FUNCTION IS WRAPPED IN `if (dirty != 0) { ... }`. When the mask
// is zero nothing happens — not the transform check, not the two clears. A
// Context with `transformCount > 0` and `dirty == 0` binds NOTHING. Faithful.
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "video/Context.h"
// ENDGAME B.3.5 block J: the eight real Video::*Data abstract bases (block A),
// whose slots the seventeen backend dispatches below go through.
#include "video/CompanionData.h"
// B.4 (R-VIDEO): the owners whose companions the arms read, by name.
#include "video/Buffers.h"
#include "video/ImageBuffer.h"
#include "video/RenderState.h"
#include "video/ShaderProgram.h"
#include "video/VertexLayout.h"
#include "video/VideoProgram.h"

// ENDGAME B.3.5 block D1. Both bodies MOVED onto `Video::Context`; the two
// registered doors kept their names and became one-line QUALIFIED non-virtual
// delegations.
//
// B.4 (R-VIDEO): every read is a named field of a typed receiver — the owners'
// companions (`ImageBuffer::_data` +0x60, `VertexProgram` / `PixelProgram::_data`
// +0x50, `VideoProgram::_data` +0x40, `VertexLayout::_data` +0x30,
// `Index/VertexBuffer::platformData` +0x08, `RenderState::_data` +0x78,
// `ConstantBuffer::_platformData` +0x40) and the Context's own fields. What is
// LEFT, each with its reason at the line:
//   * `_imageBuffers[i]` is still `void*` in Context.h (outside writers store
//     untyped pointers into it), so the image arm views it as the ImageBuffer it
//     is;
//   * +0x200 is `_videoProgram` living inside `_pixelConstantStaging`, which
//     Context.h keeps undivided because code outside this block indexes the
//     array by its own offsets;
// and two things are spelled from the member they start in:
//   * the QWORD clear over +0x20..+0x27 (0x1800ef317), `_transformCount` AND
//     `_boneCount`, written as the two named fields in address order (H7);
//   * the five constant-buffer STAGING sources at +0x208 / +0x248 / +0x328 /
//     +0x3f8 / +0x1dd8, two of which start mid-array.
//
// The negative controls and the in-process replay probe that used to follow
// the two bodies are GONE (B.4 R-VIDEO): nothing called the probe
// (`RunCase` / `InstallFakes` were unreferenced — the compiler's
// -Wunused-function said so), and the defect switch it set was therefore
// always 0.
using Video::Context;

namespace {

// ---- Upload sizes, as retail's immediates ----------------------------------
enum : int32_t {
    kGlobalBytes   = 0xd0,
    kInstanceBytes = 0x20,
    kLightBytes    = 0x1a0,
    kMaterialBytes = 0x40,
    kPixelBytes    = 0x50,
};

// THE SLOT DECIDES. Every virtual call below is on the companion class whose
// slot retail's call site dispatches through; the indexes were checked against
// the class's declaration order in CompanionData.h AND against dx11's slot
// (B.3.5 block J: the `DX11_REGISTER_SLOT` row; since B5 the `override`
// declaration in src/render/dx11/VideoDX11Data.h):
//   ContextData  BindRenderBuffer 5, BindViewport 6, BindScissor 7,
//                BindTransforms 8, BindTextureNone 9, BindIndexNone 10,
//                BindVertexNone 11
//   ImageBufferData::Bind 4 · Vertex/Pixel/VideoProgramData::Bind 2 ·
//   VertexBufferData::Bind 5 · VertexLayoutData::Bind 1 · IndexBufferData::Bind 5
//   · RenderStateData::Bind 2 · ConstantBufferData::Bind 2

}  // namespace

// ============================================================================
// ?BindTextures@Context@Video@@AEAAXXZ                      retail 0x1800ef330
// ============================================================================
// Sixteen slots. `EBX` is a rotating one-bit mask (`ROL EBX,1` at 0x1800ef381)
// tested against the WORD IN MEMORY at +0x18 on every iteration — re-read, not
// cached, which is why clearing +0x18 before the loop instead of after would
// make the whole loop bind nothing. `EDI` is the index and `R14` walks the
// pointer array. The mask and the index are two separate registers even though
// one could be derived from the other; kept separate here for the same reason
// lane VID2 kept ClearResources' two masks separate.
void Video::Context::BindTextures() {

    uint32_t mask = 1;
    for (int32_t i = 0; i < 16; ++i, mask = (mask << 1) | (mask >> 31)) {
        if ((_imageBufferDirty & mask) == 0) continue;

        const ImageBuffer* img = _imageBuffers[i];
        if (img) {
            // 0x1800ef358: RCX = img->+0x60, RDX = ctx, R8D = i, vt[+0x20].
            img->_data->Bind(this, static_cast<unsigned>(i));
        } else {
            // 0x1800ef372: companion->BindTextureNone(i).
            _contextData->BindTextureNone(i);
        }
    }
    // 0x1800ef398 — AFTER the loop, unconditionally, even when no bit was set.
    _imageBufferDirty = 0;
}

// ============================================================================
// ?BindResources@Context@Video@@AEAAXXZ                     retail 0x1800ef060
// ============================================================================
// The arms are in RETAIL ORDER, which is not bit order: the sixteen vertex
// streams (0x8) are bound BEFORE the vertex layout (0x4), and the transform
// check sits between the shader programs and the streams. Reordering them is
// invisible in the final Context bytes and visible to any backend that cares
// what was set when.
void Video::Context::BindResources() {

    // 0x1800ef066: the whole body is inside `if (dirty != 0)`.
    if (_dirty == 0) return;

    ContextData* cd = _contextData;

    // --- 0x00001 render buffer ---------------------------------------------
    // TWO DELIBERATE DEPARTURES FROM THE LITERAL BYTES, both stated rather than
    // hidden:
    //
    //   (a) 0x1800ef079 tests `AL` of the value loaded ONCE at entry
    //       (0x1800ef066); every later arm re-reads `[RBX+0x10]`. This port
    //       re-reads for the 0x1 arm too. The only instruction between the
    //       entry load and that test is a register spill, so nothing can change
    //       +0x10 in between and the two spellings cannot differ.
    //   (b) retail reloads `[RBX+0x2160]` at each companion arm; `cd` is loaded
    //       once here. No arm writes +0x2160 — the field is set by the ctor and
    //       by ?PlatformConstruct@, neither of which this function reaches.
    if (_dirty & RtVideo::kDirtyRenderBuffer)
        cd->BindRenderBuffer();

    // --- 0x00002 textures ---------------------------------------------------
    // A direct member call: retail's `CALL 0x1800ef330` at 0x1800ef08a is a near
    // call to its own sibling body.
    if (_dirty & RtVideo::kDirtyImageBuffers)
        this->Video::Context::BindTextures();

    const int32_t detail = _shaderDetail;

    // --- 0x00040 vertex program --------------------------------------------
    if (_dirty & RtVideo::kDirtyVertexProgram)
        _vertexProgram->_data->Bind(this, detail);

    // --- 0x00080 pixel program ---------------------------------------------
    if (_dirty & RtVideo::kDirtyPixelProgram)
        _pixelProgram->_data->Bind(this, detail);

    // --- 0x00100 video program ---------------------------------------------
    // NOTE THE COMPANION OFFSET: +0x40, not +0x50. A VideoProgram's backend
    // companion sits one qword earlier than a Vertex/PixelProgram's; the typed
    // `VideoProgram::_data` says so.
    if (_dirty & RtVideo::kDirtyVideoProgram) {
        // +0x200 is `_videoProgram`, the first qword of `_pixelConstantStaging`.
        const VideoProgram* p = _pixelConstantStaging.videoProgram;
        p->_data->Bind(this, detail);
    }

    // --- 0x04000 transforms, OR a live transform count ----------------------
    if ((_dirty & RtVideo::kDirtyTransformConstants) || _transformCount > 0 || _boneCount > 0)
        cd->BindTransforms();

    // --- 0x00008 the sixteen vertex streams ---------------------------------
    if (_dirty & RtVideo::kDirtyVertexBuffers) {
        uint32_t mask = 1;
        for (int32_t i = 0; i < 16; ++i, mask = (mask << 1) | (mask >> 31)) {
            if ((_vertexBufferDirty & mask) == 0) continue;

            const VertexBuffer* vb = _vertexBuffers[i];
            if (vb) {
                // 0x1800ef15d: RCX = vb->+0x8, RDX = ctx, R8D = i,
                // R9D = offsets[i] (+0x138), [RSP+0x20] = strides[i] (+0x178,
                // reached as R15+0x40 off the offsets cursor).
                vb->platformData->Bind(this,
                                       static_cast<uint32_t>(i),
                                       static_cast<uint32_t>(_vertexOffsets[i]),
                                       static_cast<uint32_t>(_vertexStrides[i]));
            } else {
                cd->BindVertexNone(i);
            }
        }
        // 0x1800ef1af — inside the 0x8 arm only. A stream marked dirty while
        // bit 0x8 is clear stays marked.
        _vertexBufferDirty = 0;
    }

    // --- 0x00004 vertex layout ----------------------------------------------
    // Retail binds the layout AFTER the streams.
    if (_dirty & RtVideo::kDirtyVertexLayout)
        _vertexLayout->_data->Bind(this);

    // --- 0x00010 index buffer -----------------------------------------------
    if (_dirty & RtVideo::kDirtyIndexBuffer) {
        const IndexBuffer* ib = _indexBuffer;
        if (ib) {
            // The Context argument retail loads into RDX at 0x1800ef1e2 and
            // VideoDX11's body never reads. The SLOT takes it.
            ib->platformData->Bind(this);
        } else {
            cd->BindIndexNone();
        }
    }

    // --- 0x00020 render state -----------------------------------------------
    if (_dirty & RtVideo::kDirtyRenderState)
        _renderState->_data->Bind(this);

    // --- 0x00200 viewport / 0x00400 scissor ---------------------------------
    if (_dirty & RtVideo::kDirtyViewport)
        cd->BindViewport();
    if (_dirty & RtVideo::kDirtyScissor)
        cd->BindScissor();

    // --- the five constant-buffer uploads -----------------------------------
    // Each is `buf->_platformData->vt[+0x10](platformData, ctx, staging, 0,
    // len)`. The fourth argument is an `XOR R9D,R9D` in all five; the fifth is
    // the size immediate. The STAGING sources are spelled from the member they
    // start in — +0x208 and +0x248 start inside `_pixelConstantStaging` (+0x200),
    // +0x328 is `_viewProjection`, +0x3f8 `_inverseScale`, +0x1dd8 `_lightStaging`.
    struct Upload { uint32_t bit; Video::ConstantBuffer* buf; const void* src; int32_t len; };
    const Upload kUploads[5] = {
        { RtVideo::kDirtyGlobalConstants,   &_globalConstants,   &_viewProjection,                kGlobalBytes   },
        { RtVideo::kDirtyInstanceConstants, &_instanceConstants, &_inverseScale,                  kInstanceBytes },
        { RtVideo::kDirtyLightConstants,    &_lightConstants,    &_lightStaging,                  kLightBytes    },
        { RtVideo::kDirtyMaterialConstants, &_materialConstants, _pixelConstantStaging.material, kMaterialBytes },
        { RtVideo::kDirtyPixelConstants,    &_pixelConstants,    _pixelConstantStaging.pixel,    kPixelBytes    },
    };
    for (const Upload& u : kUploads) {
        if ((_dirty & u.bit) == 0) continue;
        // The fourth argument is retail's `XOR R9D,R9D` — a DWORD zero.
        u.buf->_platformData->Bind(this, u.src, 0, u.len);
    }

    // 0x1800ef317 / 0x1800ef31b — a QWORD zero over +0x20..+0x27, then the
    // dirty mask. That single store covers `_transformCount` AND `_boneCount`;
    // both are cleared (clearing only the dword would leave a live transform
    // binding that never re-binds again).
    _transformCount = 0;
    _boneCount    = 0;
    _dirty = 0;
}

// ---- ?SetVertexBuffer@Context@Video@@QEAAXPEBVVertexBuffer@2@HHH@Z  0x180019f20
// B.4 (R-VIDEO): MOVED here from context.cpp (unchanged), beside BindResources,
// the one reader of the slots it fills.
void Context::SetVertexBuffer(const VertexBuffer* vb, int32_t slot,
                              int32_t offset, int32_t stride) {

    if (vb != _vertexBuffers[slot] || offset != _vertexOffsets[slot] ||
        stride != _vertexStrides[slot]) {
        _dirty |= RtVideo::kDirtyVertexBuffers;
        _vertexBufferDirty |= 1u << (static_cast<unsigned>(slot) & 0x1fu);
        _vertexBuffers[slot] = vb;
        _vertexOffsets[slot] = offset;
        _vertexStrides[slot] = stride;
    }

}
