// ============================================================================
// src/engine/ui/spritecontext_add.cpp — UI::SpriteContext's four public "add"
//                                        bodies (the per-frame menu/HUD path)
// ============================================================================
//
// src/engine/ui/spritecontext.cpp already owns this class's scissor/line half
// and its two internals; src/engine/ui/spritecontext_flush.cpp owns the batch
// drain. What was still missing is the half that every dialog, every label and
// every HUD panel actually calls:
//
//   | Retail      | Mangled name                                              |
//   |-------------|-----------------------------------------------------------|
//   | 0x18011cf00 | ?AddSprite@SpriteContext@UI@@QEAAXAEBVSprite@2@HHI@Z       |
//   | 0x18011d3c0 | ?AddSprite@SpriteContext@UI@@QEAAXAEBVSprite@2@HHHHI@Z     |
//   | 0x18011cb10 | ?AddSprite@SpriteContext@UI@@QEAAXAEBVSprite@2@HHHHIM@Z    |
//   | 0x18011ccb0 | ?AddFont@SpriteContext@UI@@QEAAXV?$ExternalPtr@VFont@UI@@@Object@@AEBVString@System@@_NHW4Justify@Font@2@HHHI@Z |
//
// Every fact below is read out of `disassemble_function` on retail
// Runtime-x64.dll, not out of Ghidra's decompiled C. The three AddSprite bodies are
// byte-for-byte the same shape and differ only in where the record's four
// coordinates and its rotation come from, which is why they are written here as
// three separate bodies rather than three wrappers: retail does not share them
// either, and a shared helper would invent a call retail does not make.
//
// ---------------------------------------------------------------------------
// NO HIDDEN RETURN SLOT ON ANY OF THE FOUR
// ---------------------------------------------------------------------------
// The `X` in each mangling says void, and — because the mangling alone is not
// the proof — the shipped bytes agree:
// all three AddSprite bodies begin `MOV RBX,[RDX]` and immediately treat RDX as
// the `UI::Sprite const&`, and AddFont's first instruction is
// `MOV [RSP+0x10],RDX` — spilling RDX into the home slot of ARGUMENT 1, which it
// later reloads as the `ExternalPtr<Font>` (0x18011cd69). If RDX were a return
// slot, argument 1 would be in R8. None of the four writes RAX before RET.
//
// ---------------------------------------------------------------------------
// THE ARGUMENT HOME SLOTS, SPELLED OUT
// ---------------------------------------------------------------------------
// At the CALLEE's entry [RSP] is the return address, [RSP+0x08] is argument 0's
// home (`this`), and the first STACK argument — argument 4 — is at [RSP+0x28],
// then +0x30, +0x38, ... Each prologue then shifts that window:
//
//   AddSprite x3   4 home stores, PUSH R14, SUB RSP,0x50   -> window at +0x80
//   AddFont        PUSH RSI, PUSH R14, SUB RSP,0xa8        -> window at +0xe0
//
// so AddSprite's argument 4 is `[RSP+0x80]` (0x18011cfc3, 0x18011d431,
// 0x18011cb87) and AddFont's is `[RSP+0xe0]` (0x18011ccfb). That arithmetic is
// what pins every parameter list in the table above; nothing here is inferred
// from a name.
//
// THE FLOAT PARAMETER IN `...IM@Z` IS A STACK ARGUMENT, NOT AN XMM ONE. It is
// parameter 8 by position, and the Microsoft x64 ABI only registerises the first
// four — so the shipped body reads it with `MOVSS XMM0,[RSP+0x98]`
// (0x18011cb64), the eighth slot, and a C++ declaration that lists it eighth
// puts it in exactly that place.
//
// ---------------------------------------------------------------------------
// THE SHEET REBIND IS THE SAME FIVE INSTRUCTIONS IN ALL THREE AddSprites
// ---------------------------------------------------------------------------
//   MOV RAX,[RCX+0x8] ; CMP RAX,RBX ; JZ done
//   TEST RAX,RAX ; JNZ flush ; MOV [RCX+0x8],RBX ; JMP done
//   flush: CALL 0x18011ba40 ; MOV [RDI+0x8],RBX
// i.e. a batch is flushed only when a DIFFERENT non-null sheet was already
// bound. It is identical to the one src/engine/ui/spritecontext.cpp's
// `AddLine` already ports, and identical to the early-outs in front of it: the
// sprite index test is `CMP dword [RDX+8],0 / JL` — a SIGNED `< 0`, not
// `== -1`.
//
// ---------------------------------------------------------------------------
// WHAT AddFont ACTUALLY DOES, AND THE ONE NEW LAYOUT FACT IT PINS
// ---------------------------------------------------------------------------
// AddFont does not rasterise anything. It asks the font to lay the string out,
// then replays the resulting glyph list through `AddSprite` (the point overload
// ported in this same file) or, while the context is deferring, straight into
// `_deferred`.
//
// The layout call is `ComputeStringLayout` (0x180116fd0, unexported in retail;
// the name is the profile build's), and its NINTH argument is `this + 0x50`.
// That is the region `src/engine/ui/SpriteContext.h` types as `_layout`
// (`RtUI::FontLayout`), and ComputeStringLayout's own prologue names its shape:
//
//   0x180117005  LEA RCX,[RDI+0x18] ; CALL 0x18002fea0   -> clear array @ +0x68
//   0x180117010  LEA RCX,[RDI+0x08] ; CALL 0x180076a40   -> clear array @ +0x58
//   0x180117021  MOV [RDI],RBX                           -> zero  qword @ +0x50
//
// which is exactly the three members SpriteContext.h deduced from
// `Save::SaveLoad::Draw`'s inlined constructor, seen from the other side. So
// `_layout` is one `RtUI::FontLayout` object, and AddFont then walks the array
// at +0x58 with `_size` at +0x60 and a stride of 0x10 (`ADD RDI,0x10` at
// 0x18011ceba), reading three ints out of each element: the sprite index at
// +0x00, an x at +0x04 and a y at +0x08 — `RtUI::FontLayoutEntry`'s `_glyph`,
// `_x` and `_y` (Font.h).
//
// TWO EARLY-OUTS BRACKET THE LAYOUT CALL, and they are vertical scissor culls:
//
//   before (0x18011ccd7):  (y - font[+0x50])                > (scissorY + scissorH) * scale
//   after  (0x18011cd78):  (scissorY * scale)               > (y + outAscent - font[+0x50])
//
// Both are `COMISS` + `JA`, so an unordered compare falls THROUGH rather than
// returning — and `a > b` is false for a NaN in C++ too, so the plain `>` below
// reproduces that without a special case. The font pointer is dereferenced
// before either test with no null check; that is retail's, and it stays
// (CLAUDE.md — do not "fix" a faithful crash with a null guard).
//
// The two `int*` out-parameters handed to ComputeStringLayout are written into
// AddFont's OWN home-slot area (`[RSP+0xc0]`, the low half of `this`'s home
// slot, and `[RSP+0x100]`, argument 8's home slot after its value has already
// been copied into R14D). They are scratch, they are pre-zeroed by retail
// before the call, and only the second is read afterwards. Two plain locals
// reproduce that exactly.
//
// ---------------------------------------------------------------------------
// THE CALLEES
// ---------------------------------------------------------------------------
//   0x180116fd0  Font::ComputeStringLayout    — font.cpp
//   0x18011ba40  SpriteContext::Flush         — spritecontext_flush.cpp
//   0x18011c890  SpriteContext::AddSpriteDraw — spritecontext.cpp
//   0x180078520  Array<SpriteDraw> append     — collection/array.cpp
//   0x18011cf00  AddSprite (point overload)   — HERE, a direct C++ call, exactly
//                as spritecontext.cpp's FlushDeferredFonts calls AddSpriteDraw.
// ============================================================================

#include <cstddef>
#include <cstdint>

#include "collection/Collection.h"
#include "ui/Sprite.h"
#include "ui/SpriteContext.h"
#include "ui/SpriteSheet.h"
#include "object/External.h"
#include "system/String.h"
#include "../collection/array_internal.h" // ENDGAME B.3.2 step 2 — kSpec_SpriteDraw

namespace {

using UI::SpriteContext;
using UI::SpriteDraw;

// The sheet rebind retail performs at the head of all three AddSprite bodies.
// 0x18011cf3c..0x18011cf59 (and the identical blocks at 0x18011d3f4,
// 0x18011cb44). Flushing only happens when a DIFFERENT non-null sheet is bound.
// (`SetSpriteSheet` 0x18011c740 is retail's out-of-line copy; these bodies
// inline it.)
inline void BindSheet(SpriteContext* self, SpriteSheet* sheet) {
    if (self->_currentSheet == sheet) return;
    if (self->_currentSheet == nullptr) {
        self->_currentSheet = sheet;
        return;
    }
    self->Flush();
    self->_currentSheet = sheet;
}

// One entry of the bound sheet's sprite table, or {0,0} for a negative index.
// Retail inlines this twice — once in the point overload (0x18011cf59..0x18011cf95)
// and once in AddFont's deferred arm (0x18011ce1e..0x18011ce54) — and both
// copies test the index with `TEST EAX,EAX / JNS`, a SIGNED test on the 32-bit
// value, reading the table only when it is non-negative.
struct SpriteExtent { int32_t width; int32_t height; };

inline SpriteExtent ExtentOf(SpriteSheet* sheet, int32_t spriteIndex) {
    SpriteExtent e = { 0, 0 };
    if (spriteIndex < 0) return e;
    const SpriteInfo* table = sheet->m_sprites._data;
    const SpriteInfo& info = table[static_cast<int64_t>(spriteIndex)];
    e.width  = info.width;    // entry +0x1c
    e.height = info.height;   // entry +0x20
    return e;
}

}  // namespace

// Font::ComputeStringLayout (0x180116fd0) is a UI::Font member (font.cpp).
// ENDGAME B.3.6 block D: this was a `ResolveAddr` static cache
// (`TheComputeStringLayout`, RULE B6-5 removes it). Argument order is read off
// the nine stores at 0x18011ccfb..0x18011cd61: the `Justify` and the sixth
// AddFont parameter arrive at the callee in the OPPOSITE order to the one
// AddFont declares them in.

// ============================================================================
// The ports — UI::SpriteContext's AddSprite/AddFont members
// ============================================================================
namespace UI {

// ---- 0x18011cf00  ?AddSprite@SpriteContext@UI@@QEAAXAEBVSprite@2@HHI@Z -----
//
// The POINT overload: place the sprite at (x, y) at its natural size. The far
// corner is the sheet entry's width/height added to the near corner, which is
// the only thing that distinguishes this body from the rect overload below.
//
// The record retail builds at RSP+0x20 (0x18011cf95..0x18011cfd8) is emitted in
// a scrambled order — +0x00, rotation, x2, spriteIndex, y2, x1, y1, isLine,
// color — which is easy to misread from the decompiler. The offsets, not the
// order, are what is reproduced here.
void SpriteContext::AddSprite(const Sprite& sprite, int32_t x, int32_t y, uint32_t color) {
    // 0x18011cf29 / 0x18011cf32: two independent early returns, the second a
    // SIGNED `< 0` on the index.
    SpriteSheet* sheet = sprite.spriteSheet;
    if (sheet == nullptr) return;
    if (sprite.spriteIndex < 0) return;

    BindSheet(this, sheet);

    const int32_t      idx = sprite.spriteIndex;
    const SpriteExtent ext = ExtentOf(sheet, idx);

    SpriteDraw rec{};
    rec.sheet       = sheet;
    rec.spriteIndex = idx;
    rec.x1          = x;
    rec.y1          = y;
    rec.x2          = ext.width  + x;   // `LEA EAX,[R9 + R14*0x1]` 0x18011cfa5
    rec.y2          = ext.height + y;   // `LEA EAX,[R10 + RBP*0x1]` 0x18011cfb6
    rec.rotation    = 0.0f;             // XORPS then MOVSS, 0x18011cf98
    rec.color       = color;
    rec.isLine      = 0;                // the literal 0 byte, 0x18011cfd3

    AddSpriteDraw(rec);
}

// ---- 0x18011d3c0  ?AddSprite@…@QEAAXAEBVSprite@2@HHHHI@Z -------------------
//
// The RECT overload: both corners are given, so no sheet-table read happens at
// all. Identical to the point overload in every other respect.
void SpriteContext::AddSprite(const Sprite& sprite, int32_t x1, int32_t y1,
                              int32_t x2, int32_t y2, uint32_t color) {
    SpriteSheet* sheet = sprite.spriteSheet;
    if (sheet == nullptr) return;
    if (sprite.spriteIndex < 0) return;

    BindSheet(this, sheet);

    SpriteDraw rec{};
    rec.sheet       = sheet;
    rec.spriteIndex = sprite.spriteIndex;
    rec.x1          = x1;
    rec.y1          = y1;
    rec.x2          = x2;
    rec.y2          = y2;
    rec.rotation    = 0.0f;
    rec.color       = color;
    rec.isLine      = 0;

    AddSpriteDraw(rec);
}

// ---- 0x18011cb10  ?AddSprite@…@QEAAXAEBVSprite@2@HHHHIM@Z ------------------
//
// The ROTATED rect overload — the rect overload with the record's `rotation`
// taken from the eighth parameter instead of being zeroed. `rotation` is in
// DEGREES: SpriteContext::Flush multiplies it by 0x3c8efa35 (pi/180) before the
// sin/cos pair (see spritecontext_flush.cpp).
//
// The float arrives on the stack, at `[RSP+0x98]` — the eighth argument slot —
// and is copied straight through with MOVSS. It is NOT converted, NOT compared,
// and NOT normalised here, so a NaN or an infinity reaches Flush intact; that is
// what selects Flush's rotated path for a NaN.
void SpriteContext::AddSprite(const Sprite& sprite, int32_t x1, int32_t y1,
                              int32_t x2, int32_t y2, uint32_t color, float rotation) {
    SpriteSheet* sheet = sprite.spriteSheet;
    if (sheet == nullptr) return;
    if (sprite.spriteIndex < 0) return;

    BindSheet(this, sheet);

    SpriteDraw rec{};
    rec.sheet       = sheet;
    rec.spriteIndex = sprite.spriteIndex;
    rec.x1          = x1;
    rec.y1          = y1;
    rec.x2          = x2;
    rec.y2          = y2;
    rec.rotation    = rotation;   // MOVSS from [RSP+0x98], 0x18011cb64/0x18011cb7d
    rec.color       = color;
    rec.isLine      = 0;

    AddSpriteDraw(rec);
}

// ---- 0x18011ccb0  ?AddFont@SpriteContext@UI@@QEAAX… ------------------------
//
// Lay a string out through the font, then submit one sprite per glyph. See the
// file banner for the two scissor culls, the layout object at `this + 0x50` and
// the argument-order swap on the way into ComputeStringLayout.
//
// `layoutA` and `layoutB` are deliberately NOT named beyond their position.
// They are AddFont's fourth and sixth parameters, they are forwarded to
// ComputeStringLayout as its fourth and fifth, and nothing in THIS body says
// what they mean.
//
// The two Font fields read here are `Font.h`'s +0x50 (`_lineHeight`, which this
// body subtracts from the incoming y in both culls) and +0x60 (`_spriteSheet`,
// handed straight to AddSprite as every glyph's sheet).
void SpriteContext::AddFont(Object::ExternalPtr<Font> font, const System::String& text,
                            bool multiline, int32_t layoutA, int32_t justify,
                            int32_t layoutB, int32_t x, int32_t y, uint32_t color) {
    const Font* f = font.Get();

    // --- the first vertical cull, 0x18011ccd7..0x18011ccf5 ------------------
    // `font` is dereferenced here with no null test. Faithful.
    const int32_t top = y - f->_lineHeight;
    const float   scissorBottom =
        static_cast<float>(_scissorY + _scissorH) * _scale;
    if (static_cast<float>(top) > scissorBottom) return;

    // --- the layout, 0x18011ccfb..0x18011cd64 -------------------------------
    // Retail zeroes both out-ints before the call even though the callee's own
    // first act is to zero them (0x180117034/0x180117037). Kept.
    int32_t outWidth  = 0;
    int32_t outAscent = 0;
    f->ComputeStringLayout(text, multiline, layoutA, layoutB, justify,
                           outWidth, outAscent, &_layout);

    // --- the second vertical cull, 0x18011cd69..0x18011cd98 -----------------
    // The font pointer is reloaded from its home slot; the sheet is read out of
    // it BEFORE the compare and is what every glyph below is drawn from.
    SpriteSheet*  sheet  = f->_spriteSheet.Get();
    const int32_t bottom = (outAscent - f->_lineHeight) + y;
    const float   scissorTop = static_cast<float>(_scissorY) * _scale;
    if (scissorTop > static_cast<float>(bottom)) return;

    // --- the glyph walk, 0x18011cda8..0x18011cec1 ---------------------------
    // `_deferring`, the glyph array's data pointer and its size are ALL re-read
    // on every iteration (0x18011cde0, 0x18011cde4, 0x18011cebe). That is kept
    // rather than hoisted: both arms below can reallocate an array, and hoisting
    // would bake in an assumption retail does not make.
    for (int32_t i = 0; i < _layout._entries._size; ++i) {
        const RtUI::FontLayoutEntry& g = _layout._entries._data[i];

        if (_deferring == 0) {
            // 0x18011cdea..0x18011ce14. A bare {sheet, index} UI::Sprite on the
            // stack — +0x0c is never written, exactly as Sprite.h records — then
            // the point overload above, which does its own sheet rebind and its
            // own sheet-table read.
            Sprite s;
            s.spriteSheet = sheet;
            s.spriteIndex = g._glyph;
            AddSprite(s, g._x + x, g._y + y, color);
            continue;
        }

        // 0x18011ce1e..0x18011ceb3. The deferred arm builds the SpriteDraw
        // itself and appends straight to `_deferred`, bypassing both the sheet
        // rebind and AddSpriteDraw's scissor cull — FlushDeferredFonts replays
        // it through AddSpriteDraw later. Note it does NOT re-check the index
        // for the record: a negative index yields a zero extent and is still
        // queued.
        const SpriteExtent ext = ExtentOf(sheet, g._glyph);

        SpriteDraw rec{};
        rec.sheet       = sheet;
        rec.spriteIndex = g._glyph;
        rec.x1          = g._x + x;
        rec.y1          = g._y + y;
        rec.x2          = (g._x + ext.width)  + x;   // LEA / ADD, 0x18011ce84
        rec.y2          = (g._y + ext.height) + y;   // LEA / ADD, 0x18011cea3
        rec.rotation    = 0.0f;
        rec.color       = color;
        rec.isLine      = 0;

        // retail 0x180078520 — kSpec_SpriteDraw, NON-TRIVIAL (partial-write
        // assign), same row spritecontext.cpp's AddSpriteDraw bridges through.
        rtcoll::AsArray<&rtcoll::kSpec_SpriteDraw>(&_deferred)
            ->SetSizeFill(_deferred._size + 1, &rec);
    }
}

}  // namespace UI
