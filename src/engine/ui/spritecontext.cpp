// ============================================================================
// src/engine/ui/spritecontext.cpp — WAVE 9 LANE UIF — UI::SpriteContext
// ============================================================================
//
// Ports the three exported `UI::SpriteContext` methods Wave 7 lane F held back,
// plus the two unexported internals they cannot work without. The layout and
// the evidence for every offset are in src/engine/ui/SpriteContext.h; the
// door arithmetic, the oracle design and everything this lane DECLINED are in
// docs/history/runtime_port/WAVE9_LANE_UIF2.md.
//
//   | Function                       | Retail      | Ported | Oracle          |
//   |--------------------------------|-------------|--------|-----------------|
//   | SetScissor(Viewport const&)     | 0x18011d050 | yes    | replay (early)  |
//   | SetScissor(int,int,int,int)     | 0x18011d010 | yes    | replay (early)  |
//   | AddLine                         | 0x18011cbe0 | yes    | replay (early)  |
//   | AddSpriteDraw   (internal)      | 0x18011c890 | yes    | replay (cull)   |
//   | FlushDeferred   (internal)      | 0x18011c6a0 | yes    | replay (no-op)  |
//   | Flush           (internal)      | 0x18011ba40 | yes    | —               |
//   | Begin           (internal)      | 0x18011d2b0 | yes    | —               |
//
// The two `Array<UI::SpriteDraw>` instantiations these bodies call —
// 0x180078520 (append) and 0x1800783d0 (clear) — are the `kSpec_SpriteDraw` row,
// which lives in src/engine/collection/array.cpp with the rest of that family
// rather than here.
//
// ---------------------------------------------------------------------------
// THE HIDDEN RETURN SLOT — the one defect this file was most likely to have
// ---------------------------------------------------------------------------
// BOTH `SetScissor` overloads return `Video::Viewport` BY VALUE. Under the
// MSVC x64 ABI that is not a register return: the caller passes a pointer to a
// caller-owned slot, and for a member function that pointer is the SECOND
// argument, in RDX, after `this` in RCX. An `fn_*` typedef that omits it is a
// WILD WRITE that still returns the right answer — the callee writes through
// whatever was left in RDX and hands the same address back in RAX, so a caller
// that only reads the result looks fine while 16 bytes of unrelated memory are
// gone. That exact defect was live on every menu scene load for a whole wave
// (`fn_Math_Transform3_Identity`).
//
// It is settled here from the SHIPPED BYTES, not from the mangled name alone:
//
//   0x18011d010  MOV RBX,RDX          ; save arg2 …
//   0x18011d03d  MOV RAX,RBX          ; … and return it. RDX is the slot.
//   0x18011d050  MOV RDI,RDX          ; same, in the other overload …
//   0x18011d1c6  MOV RAX,RDI          ; … returned in RAX.
//
// and the four-int overload's stack arguments start at RSP+0x60 for a frame of
// `PUSH RBX; SUB RSP,0x30` — i.e. caller-frame +0x28, which is the FIFTH
// argument slot. Four register arguments precede it: this, slot, x, y. If the
// slot were not an argument, `w` would sit at +0x20, not +0x28.
//
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>

#include "ui/Sprite.h"
#include "ui/SpriteContext.h"
#include "ui/SpriteCapture.h"
#include "math/Math.h"
#include "video/Context.h"
#include "../collection/array_internal.h" // ENDGAME B.3.2 step 2 — kSpec_SpriteIndexZ etc.

namespace {

using UI::SpriteContext;
using UI::SpriteDraw;

// ---- the float helpers, kept honest -----------------------------------------
// Retail converts int -> float (CVTDQ2PS), divides or multiplies in SINGLE
// precision (DIVSS / MULSS), and converts back with CVTTSS2SI, which truncates
// TOWARD ZERO. `static_cast<int>` on a float does exactly that; the `float`
// casts are what keep the arithmetic out of double precision.
inline int ScaleDown(int v, float scale) {
    return static_cast<int>(static_cast<float>(v) / scale);
}
inline int ScaleUp(int v, float scale) {
    return static_cast<int>(static_cast<float>(v) * scale);
}

}  // namespace

// UI::SpriteCapture's recorders are reached only when `_spriteCapture` is
// non-null — which it is not unless a screenshot/capture UI is active. Argument
// orders read off the two call sites, NOT guessed from the callee names:
//   AddScissor  0x1800d8ef0  <- 0x18011d126..0x18011d146
//   AddSprite   0x1800d8b20  <- 0x18011c8e4..0x18011c916
// ENDGAME B.3.6 block D: these were two `ResolveAddr` static caches
// (`TheCaptureAddScissor` / `TheCaptureAddSprite`, RULE B6-5 removes them);
// they are UI::SpriteCapture member calls now.

// ============================================================================
// The ports — UI::SpriteContext's members (ENDGAME B.3.6 block D)
// ============================================================================
// Every body is byte-faithful to the retail DISASSEMBLY, not to Ghidra's
// decompiled C (reference_ghidra_c_output_vs_disassembly.md). No null guard
// retail does not have.
namespace UI {

// ?Begin@SpriteContext@UI@@QEAAXMPEAVSpriteCapture@2@PEAVViewport@Video@@@Z
// Retail 0x18011d2b0, ord 1632. Read the scissor from the context's CURRENT
// render target (the same read src/engine/save/saveload_draw.cpp's own
// SpriteContext construction makes, through `RtVideo::RenderBufferPrefix`);
// store the capture object and the clip viewport; if a capture object was
// handed in, clear its `_entries` and `_sprites` arrays to size 0
// (0x18004ba90 / 0x1800370c0, array.cpp's kSpec_CaptureEntryZ /
// kSpec_SpriteIndexZ) and zero its +0x50 (`SpriteCapture::BeginAddingSprites`'
// own clear, SpriteCapture.h); push an identity Matrix through
// `Video::Context::SetTransform`; and finally store `_scale`.
void SpriteContext::Begin(float scale, SpriteCapture* capture, Video::Viewport* clipRegion) {
    Video::Context* const ctx = _context;
    const Video::RenderBuffer* rb = ctx->_renderBuffer;
    const int32_t w = rb->width;
    const int32_t h = rb->height;

    _spriteCapture = capture;
    _clipRegion = clipRegion;

    if (capture) {
        // Retail INLINES SpriteCapture::BeginAddingSprites (0x1800d9010) here:
        // zero +0x50 (width and height), SetSize(_sprites, 0) through
        // kSpec_SpriteIndexZ (0x1800370c0), SetSize(_entries, 0) through
        // kSpec_CaptureEntryZ (0x18004ba90) — the member does exactly those
        // three, in that order.
        capture->BeginAddingSprites();
    }

    _currentSheet = nullptr;
    _scissorX = 0;
    _scissorY = 0;
    _scissorW = w;
    _scissorH = h;

    // Math::Matrix's default ctor (retail 0x18001d770) zeroes all sixteen cells.
    Matrix identity;
    identity.m[0] = 1.0f; identity.m[5] = 1.0f; identity.m[10] = 1.0f; identity.m[15] = 1.0f;
    // THE OVERLOAD IS `?SetTransform@…QEAAXHAEBVMatrix@Math@@@Z`
    // (0x18001a6b0) — the (slot, Matrix&) pair, not `SetTransformScale`.
    ctx->SetTransform(0, identity);

    _scale = scale;
}

// ---- 0x18011c890  SpriteContext::AddSpriteDraw (private) -------------------
//
// Two independent halves, and retail runs them in this order:
//   (a) if a capture is active AND the record lies wholly inside the clip
//       region, hand a copy to the capture;
//   (b) if the record is axis-aligned, cull it against the scissor rect
//       scaled by `_scale`; whatever survives is appended to `_draws`.
//
// Half (b)'s guard is `rotation == 0.0f`, and it is written as an equality
// test on purpose. Retail's `UCOMISS / JP / JNZ` (0x18011c928) branches to the
// APPEND on unordered as well as on not-equal, so a NaN rotation skips the
// cull — and `rot == 0.0f` is false for NaN in C++ too, which reproduces that
// without a special case.
void SpriteContext::AddSpriteDraw(const SpriteDraw& draw) {
    const SpriteDraw* d = &draw;

    // --- (a) the capture branch, 0x18011c89a..0x18011c916 -------------------
    if (SpriteCapture* capture = _spriteCapture) {
        const Video::Viewport* clip = _clipRegion;
        if (d->x1 >= clip->x && d->x2 <= clip->x + clip->width &&
            d->y1 >= clip->y && d->y2 <= clip->y + clip->height) {
            // Retail rebuilds a bare {sheet, index} UI::Sprite on its own stack
            // (0x18011c8f1 / 0x18011c8fc) rather than passing the record.
            Sprite s;
            s.spriteSheet = d->sheet;
            s.spriteIndex = d->spriteIndex;
            capture->AddSprite(s, d->x1, d->y1, d->x2, d->y2, d->rotation, d->color);
        }
    }

    // --- (b) the scissor cull, 0x18011c920..0x18011c9a1 ---------------------
    bool append = true;
    if (d->rotation == 0.0f) {
        const float sc = _scale;
        const int   sx = _scissorX;
        const int   sy = _scissorY;
        if      (static_cast<float>(sx) * sc > static_cast<float>(d->x2))              append = false;
        else if (static_cast<float>(d->x1) > static_cast<float>(sx + _scissorW) * sc)  append = false;
        else if (static_cast<float>(sy) * sc > static_cast<float>(d->y2))              append = false;
        else if (static_cast<float>(d->y1) > static_cast<float>(sy + _scissorH) * sc)  append = false;
    }

    if (append) {
        // retail 0x180078520 — kSpec_SpriteDraw, NON-TRIVIAL (a partial-write
        // assign), so `_draws` is bridged through the hand-derived row rather
        // than its own trait-derived member functions.
        rtcoll::AsArray<&rtcoll::kSpec_SpriteDraw>(&_draws)
            ->SetSizeFill(_draws._size + 1, d);
    }
}

// ---- 0x18011c6a0  ?FlushDeferredFonts@SpriteContext@UI@@AEAAXXZ -------------
//
// Replays the deferred batch through AddSpriteDraw, re-binding `_currentSheet`
// (and flushing) whenever a record's sheet differs from the cursor, then
// flushes once more and clears the deferred array. A pure no-op when
// `_deferring` is clear (`CMP byte [RCX+0x38],0 / JZ epilogue`, 0x18011c6a6).
//
// Both `_deferred._data` and `_deferred._size` are RE-READ every iteration
// (0x18011c6d0 and 0x18011c70f), not hoisted. That is kept faithful rather
// than tidied: AddSpriteDraw can reallocate `_draws`, and while it does not
// touch `_deferred` today, hoisting would bake that in as an assumption.
void SpriteContext::FlushDeferredFonts() {
    if (_deferring == 0) return;

    for (int i = 0; i < _deferred.GetSize(); ++i) {
        SpriteDraw* rec   = &_deferred.GetData()[i];
        SpriteSheet* sheet = rec->sheet;
        if (_currentSheet != sheet) {
            if (_currentSheet != nullptr) Flush();
            _currentSheet = sheet;
        }
        AddSpriteDraw(*rec);
    }

    Flush();
    // retail 0x1800783d0 — kSpec_SpriteDraw's SetSize(0) form; same
    // NON-TRIVIAL row as the append above, bridged the same way.
    rtcoll::AsArray<&rtcoll::kSpec_SpriteDraw>(&_deferred)->SetSize(0);
}

// ---- 0x18011cbe0  ?AddLine@SpriteContext@UI@@QEAAXAEBVSprite@2@HHHHI@Z -----
//
// Queues one line quad. The sheet cursor is rebound (flushing the previous
// batch first, unless there was none) whenever the incoming sprite's sheet
// differs, which is what groups draws into per-sheet batches.
void SpriteContext::AddLine(const Sprite& sprite, int32_t x1, int32_t y1,
                            int32_t x2, int32_t y2, uint32_t color) {
    // 0x18011cc09 / 0x18011cc0e: two independent early returns. Note the index
    // test is `< 0`, not `== -1` — `JL`, a SIGNED branch.
    SpriteSheet* sheet = sprite.spriteSheet;
    if (sheet == nullptr || sprite.spriteIndex < 0) return;

    if (_currentSheet != sheet) {
        if (_currentSheet != nullptr) Flush();
        _currentSheet = sheet;
    }

    // The record retail builds at RSP+0x20 (0x18011cc31..0x18011cc7c).
    //
    // ONE DELIBERATE DIVERGENCE: retail leaves +0x25..+0x27 as whatever was on
    // its stack. `SpriteDraw rec{}` zeroes them. Nothing reads those three
    // bytes — Flush's record walk reads +0x00, +0x08, +0x0c, +0x10, +0x14,
    // +0x18, +0x1c, +0x20 and the +0x24 BYTE and nothing else — so the
    // difference is unobservable, and reading indeterminate bytes would be UB.
    SpriteDraw rec{};
    rec.sheet       = sheet;
    rec.spriteIndex = sprite.spriteIndex;
    rec.x1          = x1;
    rec.y1          = y1;
    rec.x2          = x2;
    rec.y2          = y2;
    rec.rotation    = 0.0f;   // 0x18011cc47, XORPS then MOVSS
    rec.color       = color;
    rec.isLine      = 1;      // 0x18011cc77, the literal 1

    AddSpriteDraw(rec);
}

// ---- 0x18011d050  ?SetScissor@…@QEAA?AVViewport@Video@@AEBV34@@Z -----------
//
// Sets the scissor rect and RETURNS THE PREVIOUS ONE, so callers can restore
// it. The incoming rect is in point space and is divided by `_scale` to reach
// the device space the four fields hold; the returned rect is multiplied back
// up — EXCEPT on the unchanged branch. See the note there.
Video::Viewport SpriteContext::SetScissor(const Video::Viewport& v) {
    Video::Viewport ret;

    const float scale = _scale;
    const int nx = ScaleDown(v.x, scale);
    const int ny = ScaleDown(v.y, scale);
    const int nw = ScaleDown(v.width, scale);
    const int nh = ScaleDown(v.height, scale);

    const bool changed = !(nx == _scissorX && ny == _scissorY &&
                           nw == _scissorW && nh == _scissorH);

    if (!changed) {
        // 0x18011d0d8..0x18011d0ef. *** RETAIL RETURNS THE RAW FIELDS HERE,
        // NOT `field * scale`. *** The other exit multiplies by `_scale`; this
        // one copies the device-space ints straight out. With `_scale != 1`
        // the two exits therefore return the same rect in DIFFERENT SPACES.
        //
        // That is retail's behaviour, it is almost certainly a bug in retail,
        // and it is reproduced rather than corrected: a caller that saves the
        // result and restores it later feeds it back through this same
        // function, and "fixing" the asymmetry would change what the restore
        // computes (CLAUDE.md — a faithful port does not repair retail).
        ret.x      = _scissorX;
        ret.y      = _scissorY;
        ret.width  = _scissorW;
        ret.height = _scissorH;
        return ret;
    }

    Flush();
    FlushDeferredFonts();

    // 0x18011d101..0x18011d146. `_context` -> +0x28 is the bound render target;
    // its +0x10/+0x14 are the target's width and height, and the flag says the
    // new scissor covers the whole target.
    if (SpriteCapture* capture = _spriteCapture) {
        const Video::RenderBuffer* target = _context->_renderBuffer;
        const int tw = target->width;
        const int th = target->height;
        const bool fullScreen = (nw == tw && nh == th);
        capture->AddScissor(v.x, v.y, v.width + v.x, v.height + v.y, fullScreen);
    }

    // 0x18011d14b..0x18011d1b4. The OLD values are read before the new ones
    // are stored; the return is the old rect scaled back up to point space.
    const int ox = _scissorX;
    const int oy = _scissorY;
    const int ow = _scissorW;
    const int oh = _scissorH;

    _scissorX = nx;
    _scissorY = ny;
    _scissorW = nw;
    _scissorH = nh;

    ret.x      = ScaleUp(ox, scale);
    ret.y      = ScaleUp(oy, scale);
    ret.width  = ScaleUp(ow, scale);
    ret.height = ScaleUp(oh, scale);
    return ret;
}

// ---- 0x18011d010  ?SetScissor@…@QEAA?AVViewport@Video@@HHHH@Z --------------
//
// A forwarder: packs the four ints into a Viewport on its own stack and calls
// the overload above with the SAME return slot. The field order of the packed
// rect is read off the four stores at 0x18011d01a..0x18011d033, which are
// emitted OUT OF ORDER (+0x20, +0x28, +0x2c, +0x24) and are easy to misread
// from the decompiler: R8D -> +0x00, R9D -> +0x04, arg5 -> +0x08, arg6 -> +0x0c.
Video::Viewport SpriteContext::SetScissor(int32_t x, int32_t y,
                                          int32_t width, int32_t height) {
    Video::Viewport v;
    v.x = x;
    v.y = y;
    v.width = width;
    v.height = height;
    return SetScissor(v);
}

}  // namespace UI
