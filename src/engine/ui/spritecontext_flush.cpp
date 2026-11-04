// ============================================================================
// src/engine/ui/spritecontext_flush.cpp — WAVE 9 LANE FLUSH
//                                          UI::SpriteContext::Flush 0x18011ba40
// ============================================================================
//
// Lane UIF closed three exported `SpriteContext` methods and, in doing so,
// opened exactly one per-frame ADDRESS door: `Flush`. Its write-up
// (docs/history/runtime_port/WAVE9_LANE_UIF2.md §1, §11.4) says so plainly, and says
// the same object is also why ~11,400 live executions of the class carry no
// differential evidence — `SetScissor`'s changed branch, `AddLine`'s
// sheet-rebind and `FlushDeferred`'s drain all call `Flush`, and no replay
// survives a call that maps the GPU vertex buffer and submits a draw.
//
// This file closes that door. The lane doc is docs/history/runtime_port/WAVE9_LANE_FLUSH.md.
//
// ---------------------------------------------------------------------------
// WHAT THE FUNCTION IS
// ---------------------------------------------------------------------------
// 591 instructions, retail 0x18011ba40..0x18011c698. It drains `_draws` — one
// batch of same-sheet records — into the Video::Context's dynamic vertex
// buffer, six vertices per sprite (two triangles) or two per line, then clears
// the batch and the sheet cursor. In order:
//
//   1. Ask the video backend for the half-pixel offset:
//        `[[0x1801fe7a8]+0x100]->vt[0x28]()`  (Device::_singleton -> DeviceData,
//        vtable slot 5). ALWAYS called, even when there is nothing to draw —
//        it is the first instruction pair of the function, before the
//        `_currentSheet == 0` test at 0x18011ba73.
//   2. If `_currentSheet` is null: skip to step 8.
//   3. Push `_scissorX..H` into `_context+0x1e0..+0x1ec` if any of the four
//      differs, and OR 0x400 into the context dirty mask.
//   4. `isLine = _draws._size > 0 ? _draws[0].isLine : 0` — the WHOLE batch is
//      typed by its FIRST record.
//   5. `MaterialInstance::Setup(_currentSheet->+0x30, _context, 0)`.
//   6. Walk the batch in chunks of `(isLine ? 2 : 6) << 12` records. Per chunk:
//      `BeginVertices(records*vertsPer, 32)` through the ContextData vtable
//      (+0x2160, slot 0x70); if it FAILS the chunk's records are SKIPPED and
//      `EndVertices` is NOT called (`JMP 0x18011c5e6` at 0x18011bc0f); if it
//      succeeds, every record is expanded and `EndVertices(isLine ? 2 : 0)`
//      (slot 0x78) submits it.
//   7. Each record becomes 6 (or 2) 32-byte vertices memcpy'd one at a time
//      into `_context->_dynamicVertexCursor`, advancing by
//      `_context->_dynamicVertexStride`. Both fields are RE-READ from
//      `_context` before every single vertex; that is kept.
//   8. `Array<SpriteDraw>::SetSize(0)` on `_draws`, then `_currentSheet = 0`.
//
// ---------------------------------------------------------------------------
// THE HALF-PIXEL OFFSET IS 0.0f IN THIS PROCESS, AND THAT IS AN HONEST LIMIT
// ---------------------------------------------------------------------------
// DeviceData vtable slot 5 is `src/render/dx11/devicedata_stubs.cpp`'s
// `DeviceData_Stub5_ZeroFloat` — the shipped DX11 backend returns 0.0f (a DX9
// backend would return 0.5f). So every `- halfPixel` below subtracts zero on
// every live call. See the lane doc's HONEST LIMITS.
//
// ---------------------------------------------------------------------------
// FLOAT FAITHFULNESS: THE TERMS ARE NOT REORDERED
// ---------------------------------------------------------------------------
// The rotated path builds `T(-centre) * R(angle) * T(+centre)` as three real
// `Math::Transform2`s and two real 2x3 multiplies, with 1.0f/0.0f stored as
// data — retail does not fold the identities, and neither does this port.
// Folding is algebraically identical and NOT bit-identical
// (reference_moved_term_hides_where_terms_cancel.md): `0.0f * -sin` is `-0.0f`,
// and `-0.0f + x` differs from `x` when `x` is `+0.0f`. Every ADDSS in
// `Transform2Mul` and the two point helpers keeps retail's operand order; MULSS
// operands are commutative and are written in whichever order reads best.
//
// The constants are read out of the shipped .rdata, not retyped
// (reference_crt_coefficients_must_be_lifted_not_retyped.md):
//   0x18016167c = 0x3c8efa35  degrees -> radians
//   0x180161694 = 0x3f000000  +0.5f
//   0x180161754 = 0xbf000000  -0.5f
//   0x18016169c = 0x3f800000  +1.0f
//   0x1801617c0 = 0x80000000  the sign flip that makes -sin
//
// `sinf`/`cosf` (0x180127d90 / 0x180126d10) go through `RtLibm::Sin/Cos`,
// which are the PORTED MSVC CRT bodies (Wave 9 lane L) — so this file opens no
// door for them.
//
// ---------------------------------------------------------------------------
// THE ORACLE — AND WHY THIS ONE ACTUALLY WORKS
// ---------------------------------------------------------------------------
// Wave 7 lane F's hold-back, restated by lane UIF §2, was: `Flush` "submits
// into the LIVE Video::Context / GPU vertex buffer ... a scratch copy of the
// SpriteContext object does not isolate a replay", because the copy's
// `_context` still points at the one live context. That is true, and this file
// does not dispute it. It builds a scratch **Context** as well.
//
// The reason a scratch Context is possible at all — and the reason nobody had
// checked — is that the only two things retail's `Flush` does to the context
// that are not plain field writes are:
//
//   * `MaterialInstance::Setup` (0x180102c90), and
//   * the two ContextData virtual calls at +0x2160 slots 0x70 / 0x78.
//
// `Setup` was READ rather than assumed
// (reference_holdback_blockers_are_stale_check_them_first.md). Every one of its
// writes — and every write of `VideoProgram::SetupShaders` (0x1800f4520), the
// one function it calls — is guarded by "if the context's field differs from
// the material's value, store it and set a dirty bit". There is no allocation,
// no refcount, and no backend call anywhere in either body. So `Setup` on a
// BYTE-COPY of the live context, taken before our body ran, writes nothing at
// all: our body already ran the same `Setup` on the original, so every compare
// finds the values equal. That makes the replay observable-free.
//
// The two virtual calls are removed by pointing the scratch context's +0x2160
// at a fake ContextData whose vtable this file owns: slot 0x70 hands back a
// private arena and returns true, slot 0x78 records the primitive type. So the
// shipped body writes its vertices into OUR memory, and our body tees a copy of
// its own vertices into a second arena. The verdict is a byte compare of the
// two arenas plus the Begin/EndVertices argument log plus the four scissor
// ints, the dirty mask, `_draws._size` and `_currentSheet`.
//
// FOUR THINGS THIS ORACLE DELIBERATELY DOES NOT COMPARE:
//   * the whole 0x2168-byte context. Comparing bytes neither side writes is
//     how an oracle reports 100% divergence on unrelated noise
//     (reference_oracle_must_not_compare_unwritten_bytes.md). The context is
//     compared only at +0x10 and +0x1e0..+0x1ec, the only fields `Flush`
//     itself writes.
//   * `_context->_dynamicVertexCursor`, because the two sides run against
//     different buffers by construction. The ADVANCE is compared instead, as
//     the arena byte count.
//   * anything at all when the sheet is null or the batch is empty — there is
//     nothing to compare, and the site declares those calls `worked=0`.
//   * the record buffer. The scratch SpriteContext gets its OWN copy of the
//     records, so the shipped `Array<SpriteDraw>::SetSize(0)` at the end
//     cannot reach the live batch even if lane UIF's §6 proof that it never
//     frees were wrong. The oracle checks that the data pointer survived and
//     reports a HARNESS finding, not a divergence, if it did not.
//
// ---------------------------------------------------------------------------
// THE DOOR ARITHMETIC THIS FILE PRODUCES
// ---------------------------------------------------------------------------
// CLOSES one address door: 0x18011ba40, lane UIF's only opened one.
// OPENS none. Every callee is already satisfied:
//   0x180102c90  MaterialInstance::Setup  — reached BY EXPORTED NAME
//                (`?Setup@MaterialInstance@Graphics@@QEBAXAEAVContext@Video@@H@Z`),
//                which src/game/Player/tool_base_tilerender.cpp:100 already
//                opens, so the unsatisfied-EXPORT set does not grow either.
//   0x180127d90 / 0x180126d10  sinf/cosf  — RtLibm, ported (lane L).
//   0x1800783d0  Array<SpriteDraw>::SetSize(0) — ported (lane UIF), and this
//                file reaches it through the same literal `ResolveAddr`
//                spelling that file uses, so the audit sees one door, not two.
//   0x18008a290  the DLL's `memcpy` — `std::memcpy`, no door.
//   0x1801fe7a8  Device::_singleton — a DATA address, already minted.
//   the ContextData vtable at +0x2160 — a virtual call, not a door.
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "rt_libm.h"
#include "ui/SpriteContext.h"
#include "ui/SpriteSheet.h"
#include "video/Context.h"
#include "video/CompanionData.h"  // ENDGAME B.3.5: Video::{Context,Device}Data
#include "video/Device.h"   // ENDGAME B.3.9 close: Video::Device::_singleton
#include "graphics/Material.h"    // ENDGAME B.3.5: Graphics::MaterialInstance
#include "ui/SharpDraw.h"         // NOT RETAIL: ext.sharp_ui's engine end (SpriteContext::_extSharp)
#include "../collection/array_internal.h" // ENDGAME B.3.2 step 2 — kSpec_SpriteDraw

// Same forward declarations, same spelling, as spritecontext.cpp — the linker
// checks them. See that file's header for why resolve.h is not included.
namespace {

using UI::SpriteContext;
using UI::SpriteDraw;

// ---------------------------------------------------------------------------
// The constants, lifted from the shipped .rdata rather than retyped.
// ---------------------------------------------------------------------------
inline float FromBits(uint32_t b) {
    float f;
    std::memcpy(&f, &b, sizeof(f));
    return f;
}
const float kDegToRad = FromBits(0x3c8efa35);   // 0x18016167c
const float kHalf     = FromBits(0x3f000000);   // 0x180161694
const float kNegHalf  = FromBits(0xbf000000);   // 0x180161754
const float kOne      = FromBits(0x3f800000);   // 0x18016169c
const float kZero     = FromBits(0x00000000);

// ---------------------------------------------------------------------------
// The vertex retail builds on its stack: 32 bytes, four of them per record.
//
// The layout is read off the stores, not guessed. Retail keeps four slots at
// RSP+0x28 (A), RSP+0x48 (B), RBP-0x70 (C) and RBP-0x50 (D) — with RBP = RSP +
// 0x100 for this frame (`LEA RBP,[RSP-0x1d8]` then `SUB RSP,0x2d8`) — and the
// field offsets within a slot are identical in all four:
//
//   +0x00 x    +0x04 y    +0x08 z (always 0)   +0x0c color (raw ARGB dword)
//   +0x10 u    +0x14 v    (the sprite's sheet UV, from sprite+0x24..+0x30)
//   +0x18 cu   +0x1c cv   (the CORNER uv: A(0,0) B(0,1) C(1,1) D(1,0))
//
// `color` is moved through an integer stack slot and reloaded with MOVSS
// (0x18011c472/0x18011c476) — a bit reinterpret, not a conversion, so it is a
// uint32_t here.
// ---------------------------------------------------------------------------
struct FlushVertex {
    float    x, y, z;
    uint32_t color;
    float    u, v;
    float    cu, cv;
};
static_assert(sizeof(FlushVertex) == 0x20, "the dynamic vertex is 32 bytes");
static_assert(offsetof(FlushVertex, color) == 0x0c, "color at +0x0c");
static_assert(offsetof(FlushVertex, u)     == 0x10, "sheet u at +0x10");
static_assert(offsetof(FlushVertex, cu)    == 0x18, "corner u at +0x18");

// ---------------------------------------------------------------------------
// Math::Transform2 — a 2x2 linear part plus a translation, row-vector
// convention. Retail builds three of these on the stack at RBP+0x50 (T1),
// RBP+0x30 (R) and RBP+0x70 (T2) and multiplies them pairwise.
// ---------------------------------------------------------------------------
struct FlushTransform2 {
    float m00, m01;
    float m10, m11;
    float tx,  ty;
};

// r = a * b. THE ADDITION ORDER IS RETAIL'S, term for term:
//
//   0x18011c03e  MULSS XMM0, a.m10*b.m00 ; 0x18011c04d MULSS XMM1, a.m11*b.m10
//   0x18011c057  ADDSS XMM1,XMM0                      -> r.m10
//   0x18011c003  XMM0 = b.m00*a.tx ; XMM1 = b.m10*a.ty
//   0x18011c011  ADDSS XMM1,XMM0 ; 0x18011c018 ADDSS XMM1,b.tx  -> r.tx
//
// i.e. every element is `(a[i][1] * b[1][j]) + (a[i][0] * b[0][j])`, the ROW-1
// term FIRST, and the translation adds `b.t` last. The second multiply
// (0x18011c100 onwards) has the identical shape, which is why one helper
// serves both.
inline FlushTransform2 Transform2Mul(const FlushTransform2& a, const FlushTransform2& b) {
    FlushTransform2 r;
    r.m00 = (a.m01 * b.m10) + (a.m00 * b.m00);
    r.m01 = (a.m01 * b.m11) + (a.m00 * b.m01);
    r.m10 = (a.m11 * b.m10) + (a.m10 * b.m00);
    r.m11 = (a.m11 * b.m11) + (a.m10 * b.m01);
    r.tx  = ((b.m10 * a.ty) + (b.m00 * a.tx)) + b.tx;
    r.ty  = ((b.m11 * a.ty) + (b.m01 * a.tx)) + b.ty;
    return r;
}

// One point through the transform, same ADDSS order (0x18011c216..0x18011c2ae):
// the y term first, then the x term, then the translation.
inline float TransformX(const FlushTransform2& m, float x, float y) {
    return ((m.m10 * y) + (m.m00 * x)) + m.tx;
}
inline float TransformY(const FlushTransform2& m, float x, float y) {
    return ((m.m11 * y) + (m.m01 * x)) + m.ty;
}

// ---------------------------------------------------------------------------
// Dependencies
// ---------------------------------------------------------------------------
// ENDGAME B.3.5 — `?Setup@MaterialInstance@Graphics@@QEBAXAEAVContext@Video@@H@Z`
// 0x180102c90 was an RT_EXPORT_ONCE door here, with a `MaterialSetupFn` typedef
// and a `kMaterialSetup` literal, because the member was only DECLARED. The body
// is `Graphics::MaterialInstance::Setup` (src/engine/graphics/wave21_lane_gfxbody.cpp)
// now, so step 6 below calls the member and the typedef/literal are gone with it.
// The door stays registered for callers outside this file.

// ENDGAME B.3.2 step 2 — the 0x1800783d0 forwarder that used to sit here is
// GONE. `self->_draws` is a real `Collection::Array<SpriteDraw>` member, and
// 0x1800783d0 is `kSpec_SpriteDraw`'s `SetSize(0)` form (NON-TRIVIAL — the
// same row spritecontext.cpp's AddSpriteDraw/FlushDeferred bridge through),
// so both call sites below call it directly.

// The fields read here, by name: Video::Context `_dirty` (+0x10), `_scissor`
// (+0x1e0), `_dynamicVertexCursor` (+0x20e0), `_dynamicVertexStride`
// (+0x20e8), `_contextData` (+0x2160); UI::SpriteSheet `m_sprites` (+0x20,
// stride 0x38, u0/v0/u1/v1 at +0x24..+0x30) and `m_field30` (+0x30, the
// Graphics::MaterialInstance).

// ENDGAME B.3.5: the two ContextData byte offsets are gone. +0x70 and +0x78 are
// slots 14 (`BeginVertices(int,int)`) and 15 (`EndVertices(int)`) of the real
// abstract `Video::ContextData`, declared in retail order in CompanionData.h and
// pinned there against dx11's own slot table (src/render/dx11/contextdata.cpp:37-38).
// The receiver is PROVED: it is `Video::Context::_contextData` at +0x2160. The
// CONCRETE `*Data` subclass and the 84-slot seam are a declared leave for step 5
// (B35_RULES section 11); only the dispatch converts.

// The dirty bit `Flush` ORs in when the scissor changed (0x18011badf).
const uint32_t kDirtyScissor = 0x400;

// ---------------------------------------------------------------------------
// The half-pixel offset. `Video::Device::_singleton` (0x1801fe7a8) -> +0x100
// (the DeviceData) -> vtable slot 5.
//
// The POINTER is re-read on every call, which is what retail does
// (`MOV RAX,[0x1801fe7a8]` inside the body). ENDGAME B.3.9 close (D7): the
// cell is the class static; the by-address door and its cached slot are gone.
// ---------------------------------------------------------------------------
inline ::Video::DeviceData* VideoDeviceData() {
    ::Video::Device* device = ::Video::Device::_singleton;
    if (!device) return nullptr;
    return device->deviceData;                                   // +0x100
}

// ENDGAME B.3.5: slot 5 of the real abstract `Video::DeviceData`. The retail
// name is not recoverable, so CompanionData.h calls it `Slot5()` and records
// what dx11's override returns; the receiver is PROVED (it is `Device+0x100`,
// the DeviceData the singleton owns).
inline float DeviceHalfPixel(Video::DeviceData* deviceData) {
    return deviceData->Slot5();
}

// One vertex into the live buffer.
// `_context` is re-read for EVERY vertex and so are both fields, exactly as
// retail does (six `MOV RBX,[R13+0x30]` in the shared tail, 0x18011c46e..).
inline void Emit(SpriteContext* self, const FlushVertex& v) {
    Video::Context* ctx    = self->_context;
    uint8_t*        cursor = ctx->_dynamicVertexCursor;
    const int32_t   stride = ctx->_dynamicVertexStride;
    std::memcpy(cursor, &v, static_cast<size_t>(stride));
    ctx->_dynamicVertexCursor = cursor + stride;
}

// ---- effectiveness counters, printed by the report hook --------------------
uint64_t g_calls = 0;
uint64_t g_records = 0, g_lines = 0, g_rotated = 0, g_mapFail = 0;

}  // namespace

// ============================================================================
// 0x18011ba40  ?Flush@SpriteContext@UI@@AEAAXXZ — UI::SpriteContext::Flush
// ============================================================================
// ENDGAME B.3.6 block D: the body the door and its `FlushBody` helper carried
// is the member now; the door below delegates.
namespace UI {

// NOT RETAIL: ext.sharp_ui's engine-side seam (SpriteContext.h), null by default.
const SharpDraw* SpriteContext::_extSharp = nullptr;

void SpriteContext::Flush() {
    // 0x18011ba53..0x18011ba70, BEFORE the `_currentSheet` test. Retail derefs
    // the device global UNGUARDED and so does this port: the call below faults
    // exactly where retail's does if the singleton is not up yet.
    Video::DeviceData* const deviceData = VideoDeviceData();
    const float halfPixel  = DeviceHalfPixel(deviceData);

    ++g_calls;

    if (_currentSheet == nullptr) {
        rtcoll::AsArray<&rtcoll::kSpec_SpriteDraw>(&_draws)->SetSize(0);  // 0x1800783d0
        _currentSheet = nullptr;
        return;
    }

    Video::Context* const ctx = _context;
    const SpriteSheet* const current = _currentSheet;

    // --- the scissor push, 0x18011bab3..0x18011bb08 -------------------------
    // Four independent compares that all fall through to the SAME store block:
    // one difference rewrites all four fields and sets the bit.
    Video::Viewport& ctxScissor = ctx->_scissor;
    if (ctxScissor.x != _scissorX || ctxScissor.y != _scissorY ||
        ctxScissor.width != _scissorW || ctxScissor.height != _scissorH) {
        ctx->_dirty |= kDirtyScissor;
        ctxScissor.x      = _scissorX;
        ctxScissor.y      = _scissorY;
        ctxScissor.width  = _scissorW;
        ctxScissor.height = _scissorH;
    }

    // --- the batch type, 0x18011bb0e..0x18011bb20 ---------------------------
    // The FIRST record types the whole batch. An empty batch is `isLine = 0`.
    int32_t count = _draws._size;
    const uint8_t isLine = (count > 0) ? _draws._data[0].isLine : 0;

    // NOT RETAIL (ext.sharp_ui): the engine-side seam, read ONCE per flush and
    // hoisted out of the record walk. Null in the faithful profile. `swap` is
    // this batch's twin sheet (a font variant / a 2x sheet), or null.
    const SharpDraw* const sharp = _extSharp;
    const SharpSheetSwap* const swap = (sharp != nullptr) ? FindSwap(*sharp, current) : nullptr;

    // --- 0x18011bb37 -------------------------------------------------------
    // `m_field30` is spelt uint64_t (saveload_draw.cpp stores it as an integer).
    if (swap == nullptr)
        current->m_field30
            ->Setup(*ctx, 0);
    else if (swap->material != nullptr)
        swap->material->Setup(*ctx, 0);   // EXT SEAM (NOT RETAIL, docs/extensions/handins/SHARP_SPRITES.md): ext.sharp_ui, the scaled sheet's material
    else
        swap->twin->m_field30->Setup(*ctx, 0);   // EXT SEAM (NOT RETAIL, docs/extensions/handins/SHARPUI.md): ext.sharp_ui, the twin's material

    if (sharp != nullptr && sharp->point) {
        // EXT SEAM (NOT RETAIL, docs/extensions/handins/SHARPUI.md): ext.sharp_ui, this batch's sampler, chosen before the
        // first EndVertices binds it (ui/SharpDraw.h). POINT only when every
        // axis-aligned quad is pixel-exact; a rotated quad keeps filtering.
        bool point = true;
        for (int32_t i = 0; i < count && point; ++i) {
            const SpriteDraw& r = _draws._data[i];
            if (r.isLine != 0 || r.spriteIndex < 0) continue;
            if (r.rotation != 0.0f) { point = false; break; }
            const SpriteInfo& info = current->m_sprites._data[r.spriteIndex];
            point = BatchQuadIsPixelExact(*sharp, r.x1, r.y1, r.x2, r.y2, info.width, info.height);
        }
        if (point != Video::ImageBufferData::_extPointSampling) {
            // Context::BindTextures binds only dirty slots: force the rebind.
            Video::ImageBufferData::_extPointSampling = point;
            ctx->_imageBufferDirty |= 0xffffu;
            ctx->_dirty |= RtVideo::kDirtyImageBuffers;
        }
    }

    // --- the chunked walk, 0x18011bb3c..0x18011c5ef ------------------------
    const int32_t vertsPer = isLine ? 2 : 6;
    const int32_t chunkCap = vertsPer << 12;      // `SHL EAX,0xc` at 0x18011bb57
    int32_t       base     = 0;                   // read cursor into _draws

    // NOT RETAIL (ext.sharp_ui): the word run the placed glyphs of this batch
    // continue (ui/SharpDraw.h, SharpRun). Untouched while `sharp` is null.
    SharpRun run;

    while (count > 0) {
        const int32_t n = (count < chunkCap) ? count : chunkCap;
        count -= n;

        Video::ContextData* cd = _context->_contextData;
        const bool ok = cd->BeginVertices(n * vertsPer, 0x20);

        if (!ok) {
            // 0x18011bc0d/0x18011bc0f: the chunk's records are SKIPPED and
            // EndVertices is NOT called. Faithful, including the skip.
            base += n;
            ++g_mapFail;
            continue;
        }

        for (int32_t i = 0; i < n; ++i) {
            // Both are re-read every iteration (0x18011bc40 / 0x18011bc44);
            // `_currentSheet` does not change inside the walk.
            const SpriteDraw* d = &_draws._data[base + i];
            const SpriteSheet* sheet = current;

            // --- the sheet UV, 0x18011bc4b..0x18011bc89 --------------------
            // `MOVSXD RAX,[rec+8]` then `TEST EAX,EAX / JNS`: a SIGNED test on
            // the 32-bit index; a negative index yields four zeros, not a read.
            float u0 = kZero, v0 = kZero, u1 = kZero, v1 = kZero;
            const int32_t idx = d->spriteIndex;
            if (idx >= 0) {
                const SpriteInfo& info = sheet->m_sprites._data[idx];
                u0 = info.u0; v0 = info.v0; u1 = info.u1; v1 = info.v1;
            }
            if (swap != nullptr && idx >= 0 && swap->twinSprite != nullptr) {
                // EXT SEAM (NOT RETAIL, docs/extensions/handins/SHARPUI.md): ext.sharp_ui, the UVs of the twin's
                // sprite; one with no twin draws nothing (zero UVs). A scaled sheet (no table) keeps the base UVs.
                const int32_t t = idx < swap->count ? swap->twinSprite[idx] : -1;
                u0 = v0 = u1 = v1 = kZero;
                if (t >= 0) {
                    const SpriteInfo& info = swap->twin->m_sprites._data[t];
                    u0 = info.u0; v0 = info.v0; u1 = info.u1; v1 = info.v1;
                }
            }

            const uint32_t color = d->color;
            ++g_records;

            if (d->isLine != 0) {
                // --- the LINE path, 0x18011bcb3..0x18011bd9c ---------------
                // NOTE: no half-pixel subtraction here. The line path converts
                // the four ints and uses them raw (0x18011bceb/0x18011bcee is
                // CVTDQ2PS with no SUBSS). That asymmetry is retail's.
                ++g_lines;
                FlushVertex a, b;
                a.x = static_cast<float>(d->x1); a.y = static_cast<float>(d->y1);
                a.z = kZero; a.color = color;
                a.u = u0; a.v = v0; a.cu = kZero; a.cv = kZero;
                b.x = static_cast<float>(d->x2); b.y = static_cast<float>(d->y2);
                b.z = kZero; b.color = color;
                b.u = u1; b.v = v1; b.cu = kOne;  b.cv = kOne;
                Emit(this, a);
                Emit(this, b);
                continue;
            }

            // --- the SPRITE path: four corners, then six vertices ----------
            float ax, ay, bx, by, cx, cy, dx, dy;
            if (d->rotation == 0.0f) {
                // 0x18011bdb7..0x18011bea3. `UCOMISS / JP / JNZ` sends a NaN
                // rotation to the ROTATED path, and `rot == 0.0f` is false for
                // a NaN in C++ too, so no special case is needed.
                ax = static_cast<float>(d->x1) - halfPixel;
                ay = static_cast<float>(d->y1) - halfPixel;
                by = static_cast<float>(d->y2) - halfPixel;
                cx = static_cast<float>(d->x2) - halfPixel;
                bx = ax;  cy = by;  dx = cx;  dy = ay;
                if (sharp != nullptr) {
                    // EXT SEAM (NOT RETAIL, docs/extensions/handins/SHARPUI.md): ext.sharp_ui, the corners on whole
                    // device pixels, rounded against the dialog's origin (ui/SharpDraw.h). `sharp` is read once per
                    // flush; null = retail's four lines above, untouched.
                    ax = SnapToPixel(static_cast<float>(d->x1), sharp->pxPerUnitX, sharp->unitsPerPxX, sharp->originX) - halfPixel;
                    ay = SnapToPixel(static_cast<float>(d->y1), sharp->pxPerUnitY, sharp->unitsPerPxY, sharp->originY) - halfPixel;
                    by = SnapToPixel(static_cast<float>(d->y2), sharp->pxPerUnitY, sharp->unitsPerPxY, sharp->originY) - halfPixel;
                    cx = SnapToPixel(static_cast<float>(d->x2), sharp->pxPerUnitX, sharp->unitsPerPxX, sharp->originX) - halfPixel;
                    bx = ax;  cy = by;  dx = cx;  dy = ay;
                    if (swap != nullptr && swap->place != nullptr && idx >= 0 && idx < swap->count) {
                        // A font variant's glyph at its own device-pixel size
                        // on the base glyph's baseline, its pen continuing the
                        // word's run (SharpRunPen) or snapped (PlaceGlyphAt).
                        const SharpGlyphPlace& place = swap->place[idx];
                        const float devPen = SharpRunPen(run, place, d->x1, d->y2,
                                                         sharp->originX, sharp->pxPerUnitX);
                        const SharpQuad q = PlaceGlyphAt(place, devPen, d->y2, sharp->pxPerUnitY,
                                                         sharp->unitsPerPxX, sharp->unitsPerPxY,
                                                         sharp->originY);
                        ax = q.x1 - halfPixel;  ay = q.y1 - halfPixel;
                        cx = q.x2 - halfPixel;  by = q.y2 - halfPixel;
                        bx = ax;  cy = by;  dx = cx;  dy = ay;
                    } else {
                        run.live = false;   // not a placed glyph: the run ends
                    }
                }
            } else {
                if (sharp != nullptr) run.live = false;   // NOT RETAIL: a rotated quad ends the run
                // 0x18011bea8..0x18011c46e. T(-centre) * R(angle) * T(+centre),
                // built as three real Transform2s. Not folded — see the header.
                ++g_rotated;
                const float ang = d->rotation * kDegToRad;

                // `LEA EAX,[R14+RDI]` — a 32-bit INT sum, then CVTDQ2PS.
                const float sumX = static_cast<float>(d->x2 + d->x1);
                const float sumY = static_cast<float>(d->y2 + d->y1);

                // sinf FIRST (0x18011bf89), then cosf (0x18011bf94).
                const float s = RtLibm::Sin(ang);
                const float c = RtLibm::Cos(ang);
                // `XORPS XMM1,[0x1801617c0]` — a SIGN FLIP, so sin == +0.0f
                // becomes -0.0f, which unary minus reproduces exactly.
                const float ns = -s;

                const FlushTransform2 t1 = { kOne, kZero, kZero, kOne,
                                        sumX * kNegHalf, sumY * kNegHalf };
                FlushTransform2       rot = { c, s, ns, c, kZero, kZero };
                const FlushTransform2 t2 = { kOne, kZero, kZero, kOne,
                                        sumX * kHalf, sumY * kHalf };

                const FlushTransform2 m1 = Transform2Mul(t1, rot);
                const FlushTransform2 m  = Transform2Mul(m1, t2);

                // Each coordinate is converted ONCE (0x18011c18e / 0x18011c1bc
                // / 0x18011c245 / 0x18011c2a6) and reused across the corners.
                const float fx1 = static_cast<float>(d->x1);
                const float fy1 = static_cast<float>(d->y1);
                const float fx2 = static_cast<float>(d->x2);
                const float fy2 = static_cast<float>(d->y2);

                ax = TransformX(m, fx1, fy1) - halfPixel;
                ay = TransformY(m, fx1, fy1) - halfPixel;
                bx = TransformX(m, fx1, fy2) - halfPixel;
                by = TransformY(m, fx1, fy2) - halfPixel;
                cx = TransformX(m, fx2, fy2) - halfPixel;
                cy = TransformY(m, fx2, fy2) - halfPixel;
                dx = TransformX(m, fx2, fy1) - halfPixel;
                dy = TransformY(m, fx2, fy1) - halfPixel;
            }

            // The four corners and their two UV pairs. The CORNER uv is what
            // distinguishes them beyond position: A(0,0) B(0,1) C(1,1) D(1,0).
            FlushVertex A, B, C, D;
            A.x = ax; A.y = ay; A.z = kZero; A.color = color;
            A.u = u0; A.v = v0; A.cu = kZero; A.cv = kZero;
            B.x = bx; B.y = by; B.z = kZero; B.color = color;
            B.u = u0; B.v = v1; B.cu = kZero; B.cv = kOne;
            C.x = cx; C.y = cy; C.z = kZero; C.color = color;
            C.u = u1; C.v = v1; C.cu = kOne;  C.cv = kOne;
            D.x = dx; D.y = dy; D.z = kZero; D.color = color;
            D.u = u1; D.v = v0; D.cu = kOne;  D.cv = kZero;

            // 0x18011c4ae..0x18011c58d: A, D, C, A, C, B — two triangles,
            // (TL,TR,BR) and (TL,BR,BL).
            Emit(this, A); Emit(this, D); Emit(this, C);
            Emit(this, A); Emit(this, C); Emit(this, B);
        }
        base += n;

        // 0x18011c5c5..0x18011c5e3. `_context` and `+0x2160` are re-read.
        Video::ContextData* cd2 = _context->_contextData;
        const int32_t prim = isLine ? 2 : 0;
        cd2->EndVertices(prim);
    }

    // --- 0x18011c674..0x18011c685 ------------------------------------------
    rtcoll::AsArray<&rtcoll::kSpec_SpriteDraw>(&_draws)->SetSize(0);  // 0x1800783d0
    _currentSheet = nullptr;
}

}  // namespace UI

// ============================================================================
// The per-run counters.
// ============================================================================
void RtUiSpriteContextFlushReport() {
    std::fprintf(stderr,
        "[Flush] calls=%llu | records=%llu (lines=%llu rotated=%llu) mapFail=%llu\n",
        (unsigned long long)g_calls,
        (unsigned long long)g_records, (unsigned long long)g_lines,
        (unsigned long long)g_rotated, (unsigned long long)g_mapFail);
}
