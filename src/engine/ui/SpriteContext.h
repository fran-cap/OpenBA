#pragma once
// ============================================================================
// src/engine/ui/SpriteContext.h — UI::SpriteContext (a real, concrete,
//                                     NON-polymorphic class) and UI::SpriteDraw
// ============================================================================
//
// Started by Wave 7 lane F as an evidence note behind a hold-back. Rewritten by
// WAVE 9 LANE UIF (2026-09-08), which ported three of the class's exports and
// two of its internals. ENDGAME B.3.6 block D made it the real class under
// retail's name: every one of retail's 23 exports is declared below, the ported
// bodies are its members, and the registered doors delegate to them.
//
// PORTABILITY: no <windows.h>, per PLAN §4.1 decision 1.
//
// Retail addresses (Runtime-x64.dll, image base 0x180000000). ALL of these are
// in the export table — including the `AEAA` (private) ones this banner used
// to call "internal, no export": `AddSpriteDraw`, `Flush`, `FlushDeferredFonts`
// and `SetSpriteSheet` are exported private members (B36_SLOTS.md section 6;
// reference_aeaa_is_private_not_unexported.md). Their doors are registered by
// ADDRESS in this tree; the names resolve through rt_export_addrs.inc.
//
// ---------------------------------------------------------------------------
// SIZE: EXACTLY 0x88 — PINNED BY ENDGAME B.3.6 block D (B36_RULES section 13
// item 2), from the RETAIL COPY CONSTRUCTOR.
// ---------------------------------------------------------------------------
// `??0SpriteContext@UI@@QEAA@AEBV01@@Z` (0x1800321c0) is the compiler-generated
// member-wise copy, and it copies EVERY member exactly once, in order:
//   +0x00 dword · +0x08 qword · +0x10 Array (0x180033d20) · +0x20..+0x2c four
//   dwords · +0x30 qword · +0x38 byte · +0x40 Array (0x180033d20) · +0x50 the
//   Font::Layout (its own copy ctor 0x180032280) · +0x78 qword · +0x80 qword
// and then `MOV RAX,RDI ; RET`. Its last store is `MOV [RDI+0x80],RAX`
// (0x18003225f): nothing past +0x87 is a member, and with an 8-byte alignment
// there is no tail padding to hide one in. So sizeof == 0x88. The two other
// witnesses bound it the same way: `UserInterface::Draw` keeps the object at
// [RBP-0x80] with the next local at RBP+0x14 (0x88 <= sizeof <= 0x94), and
// `Begin`'s store at +0x80 is the highest offset any body writes.
//
// The retail ctor `??0SpriteContext@UI@@QEAA@AEAVContext@Video@@@Z`
// (0x18011d1e0) stores through +0x70 and leaves +0x50, +0x78, +0x80 alone;
// the retail dtor `??1SpriteContext@UI@@QEAA@XZ` (0x18011d250) runs
// ~Array(+0x68), ~Array(+0x58) (the Layout's two arrays), ~Array(+0x40),
// ~Array(+0x10) — exactly the implicit destructor of the members below, in
// reverse declaration order.
//
// NOT TRIVIALLY COPYABLE, AND RETAIL SAYS SO. B36_RULES 4.5 asks for
// `is_trivially_copyable`; retail's own class has a user-visible destructor
// (0x18011d250) and a DEEP copy constructor (0x1800321c0 copies each Array
// through 0x180033d20), so it is not trivially copyable in retail's terms
// either, and the four `Collection::Array` members make that true here. What
// B6-4.5 is for — `saveload_draw.cpp:734` builds one on the STACK field by
// field — holds: the class is concrete with a PUBLIC defaulted default
// constructor, the fields that TU writes are public, and the implicit
// destructor frees exactly the four buffers retail's does.
//
// ---------------------------------------------------------------------------
// THE CLASS IS NOT POLYMORPHIC.
// ---------------------------------------------------------------------------
// Read out of the shipped bytes, not inferred from the absent `??_7`: Begin's
// first store to the object is `MOV [RCX+0x80],RBX` and its second is
// `MOV [RCX+0x78],R9`; the +0x00 slot is written later, and it is written with
// `MOVSS [RDI],XMM6` (0x18011d393) — a FLOAT, not a vtable pointer. Every
// export of the class is `QEAA`, none `UEAA`.
//
// ---------------------------------------------------------------------------
// FIELD EVIDENCE
// ---------------------------------------------------------------------------
//   off    field                proven by                              retail
//   0x00   _scale               MOVSS XMM2,[RCX] then DIVSS in          0x18011d06e
//                               SetScissor and MULSS on the way out;
//                               Begin writes it from the float
//                               argument (`MOVSS [RDI],XMM6`)           0x18011d393
//   0x08   _currentSheet        AddLine compares the incoming sprite's  0x18011cc14
//                               sheet against [RCX+8] and calls Flush
//                               when they differ; FlushDeferred does
//                               the same per deferred record           0x18011c6d4
//   0x10   _draws               AddSpriteDraw appends with
//                               `LEA RCX,[RDI+0x10]` and passes
//                               `[RDI+0x18] + 1` as the new size        0x18011c9a6
//   0x20   _scissorX            SetScissor compares/overwrites all      0x18011d0c1
//   0x24   _scissorY            four; AddSpriteDraw culls against       0x18011d165
//   0x28   _scissorW            them scaled by _scale                   0x18011c92f
//   0x2c   _scissorH
//   0x30   _context             SetScissor reads `*(*(this+0x30)+0x28)` 0x18011d10d
//                               for the render target's w/h; Draw
//                               stores its Video::Context& there        0x180121cc8
//   0x38   _deferring           FlushDeferred returns immediately when  0x18011c6a6
//                               it is 0
//   0x40   _deferred            FlushDeferred walks it with size at     0x18011c6ba
//                               +0x48 and clears it through
//                               Array<SpriteDraw>::SetSize(0)           0x18011c72a
//   0x78   _clipRegion          AddSpriteDraw reads four ints through   0x18011c8b0
//                               it as {x, y, w, h}; Begin stores the
//                               4th argument there, and Draw passes
//                               `&UserInterface::_captureViewport`      0x180121cf3
//   0x80   _spriteCapture       Begin stores the 3rd argument;          0x18011d2eb
//                               AddSpriteDraw and SetScissor both
//                               call into it only when non-null        0x18011c89a
//
// THREE MORE ARRAYS, deliberately left opaque. UserInterface::Draw's epilogue
// frees FOUR buffers belonging to this object — at +0x10, +0x40, +0x58 and
// +0x68 (0x180122242, 0x18012228f, 0x1801222dc, 0x180122329, each zeroing the
// pointer and the qword after it). The first two are `_draws` and `_deferred`
// above. The other two, and the plain qword at +0x50, are NOT modelled: no
// body this lane ported reads them, and inventing a layout we have not proven
// is how a shared struct corrupts silently (PLAN §4.3 risk 1).
//
// WAVE 14 LANE DRAW ADDS A SECOND, INDEPENDENT WITNESS for the same four, and
// one new fact about +0x50. `Save::SaveLoad::Draw` (0x1800c2eb0) builds a
// SpriteContext on its STACK with the constructor fully inlined, so its
// prologue is a field-by-field initialiser list and its epilogue is the
// destructor:
//   * it zeroes BOTH halves of the arrays at +0x10, +0x40, +0x58 and +0x68
//     (0x1800c37e5, 0x1800c3807, 0x1800c3820, 0x1800c382e and their +8 pairs),
//     and frees exactly those four pointers on the way out in the order
//     +0x68, +0x58, +0x40, +0x10 (0x1800c3acd .. 0x1800c3af8);
//   * it NEVER WRITES +0x50. It takes the address (`LEA RAX,[RBP+0xa0]` at
//     0x1800c3815) for the MSVC exception-state record and nothing else. So the
//     qword at +0x50 is left as whatever the storage held — uninitialised on a
//     stack instance. A port of any body that constructs one must NOT zero it,
//     and an oracle must not compare it.
// The two arrays at +0x58 and +0x68 are therefore Collection::Array<T> with the
// usual {_data, _size, _capacity} shape; only their ELEMENT type is still
// unknown, which is why they stay inside `_opaque50` rather than being named.
//
// ---------------------------------------------------------------------------
// WHAT IS STILL HELD BACK, AND WHY THE WAVE 7 ARGUMENT ONLY HALF SURVIVED
// ---------------------------------------------------------------------------
// Wave 7 lane F held the whole class back with one argument: these methods
// "submit into the LIVE Video::Context / GPU vertex buffer", so a scratch copy
// of the object does not isolate a replay — the copy's `_context` still points
// at the one live context, and calling the DLL's body on it would submit a
// second, real, visible draw.
//
// THAT ARGUMENT IS CORRECT, AND IT IS AN ARGUMENT ABOUT `Flush`, NOT ABOUT THE
// THREE EXPORTS. Re-checked instruction by instruction for Wave 9
// (reference_holdback_blockers_are_stale_check_them_first.md — three stated
// blockers dissolved on re-checking in one session):
//
//   * `Flush` (0x18011ba40) is the only body in the class that touches the GPU.
//     It maps the vertex buffer through `_context`'s device vtable
//     (`CALL [RAX+0x70]` / `[RAX+0x78]` at 0x18011bc06 / 0x18011c5e3), memmoves
//     transformed vertices into it, and advances the write cursor. It is NOT
//     ported and NOT replayable, and the Wave 7 sentence is about it.
//   * `AddLine`, both `SetScissor`s, `AddSpriteDraw` and `FlushDeferred` do NOT
//     touch the GPU. They mutate the object's own scissor ints and its own
//     `Array<SpriteDraw>` batch, and they CALL `Flush`. That makes them
//     unreplayable for a different and much narrower reason — a second call
//     would append the record twice — but it does not make them unportable,
//     and the batch they build is ordinary memory this port owns end to end.
//
// So the class is no longer held back; `Flush` and `Begin` are. See the lane
// doc for the door arithmetic that decision produces. (Both have been ported
// since: Flush in spritecontext_flush.cpp, Begin in spritecontext.cpp.)
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <type_traits>

#include "../collection/Collection.h"
#include "Font.h"   // RtUI::FontLayout — the Font::Layout at +0x50

// Classes other blocks own, taken by reference / pointer only.
namespace Object { template <class T> class ExternalPtr; }
namespace System { class String; }
namespace Video  { class Context; struct Viewport; }

namespace UI {

class Sprite;
class SpriteCapture;
class SpriteSheet;
struct SharpDraw;   // ui/SharpDraw.h (NOT RETAIL, ext.sharp_ui's engine end)
struct SpriteRecord;   // ui/SpriteRecord.h (NOT RETAIL, ext.render_pipe's replayed frames)
class Font;

// ============================================================
// UI::SpriteDraw — one queued sprite/line, stride 0x28. Retail's name is the
// nested `UI::SpriteContext::SpriteDraw` (`AEBUSpriteDraw@12@` in AddSpriteDraw's
// mangling); the class re-exports it under that spelling below.
//
// The stride is proven three times over: `ADD R15,0x28` in Flush's batch walk
// (0x18011c5a1) and in Draw's element walk (0x180121faf), and the `LEA
// RCX,[RAX+RAX*4] / SHL RCX,3` (== *0x28) index arithmetic in both
// Array<SpriteDraw> instantiations (0x180078573, 0x180078439).
//
// The FIELDS are read out of the two writers and the one reader agreeing:
//   * AddLine (0x18011cbe0) builds a record on its own stack at RSP+0x20 and
//     writes +0x00,+0x08,+0x0c,+0x10,+0x14,+0x18,+0x1c,+0x20,+0x24;
//   * UserInterface::Draw (0x180121890) builds the same record at RSP+0x28 for
//     the fullscreen fade and for the mouse cursor, writing the same offsets;
//   * AddSpriteDraw (0x18011c890) reads +0x0c/+0x14 as the X pair and
//     +0x10/+0x18 as the Y pair when it culls against the clip region, and
//     hands +0x00/+0x08 to SpriteCapture as a UI::Sprite.
//
// The x/y interleave is the one thing easy to get backwards, so it is nailed
// to a single instruction pair: 0x18011c8ac loads +0x0c and compares it
// against `clip.x`, and 0x18011c8c7 loads +0x10 and compares it against
// `clip.y`. +0x0c and +0x10 are therefore NOT a pair — they are the first
// coordinate of each axis.
// ============================================================
struct SpriteDraw {
    SpriteSheet* sheet;     // +0x00  ExternalPtr<SpriteSheet>, the batch key (B4 P2 H4: typed)
    int32_t  spriteIndex;   // +0x08  -1 is the empty sprite, never 0
    int32_t  x1;            // +0x0c
    int32_t  y1;            // +0x10
    int32_t  x2;            // +0x14
    int32_t  y2;            // +0x18
    float    rotation;      // +0x1c  0.0f selects the axis-aligned fast path
                            //        in both Flush and AddSpriteDraw
    uint32_t color;         // +0x20  ARGB, 0xffffffff is "untinted"
    uint8_t  isLine;        // +0x24  1 from AddLine, 0 from every other writer
    uint8_t  _pad25[3];     // +0x25  never written by the default-init arm
};
static_assert(sizeof(SpriteDraw) == 0x28, "SpriteDraw stride is 0x28");
static_assert(offsetof(SpriteDraw, _pad25) == 0x25, "SpriteDraw::_pad25");
static_assert(offsetof(SpriteDraw, sheet)       == 0x00, "sheet at +0x00");
static_assert(offsetof(SpriteDraw, spriteIndex) == 0x08, "spriteIndex at +0x08");
static_assert(offsetof(SpriteDraw, x1)          == 0x0c, "x1 at +0x0c");
static_assert(offsetof(SpriteDraw, y1)          == 0x10, "y1 at +0x10");
static_assert(offsetof(SpriteDraw, x2)          == 0x14, "x2 at +0x14");
static_assert(offsetof(SpriteDraw, y2)          == 0x18, "y2 at +0x18");
static_assert(offsetof(SpriteDraw, rotation)    == 0x1c, "rotation at +0x1c");
static_assert(offsetof(SpriteDraw, color)       == 0x20, "color at +0x20");
static_assert(offsetof(SpriteDraw, isLine)      == 0x24, "isLine at +0x24");

// ============================================================================
// UI::SpriteContext — 0x88 bytes, no vtable, CONCRETE (RULE B6-4.5).
// ============================================================================
// The clip region at +0x78 is a `Video::Viewport` ({x, y, width, height}):
// Begin's fourth parameter is `PEAVViewport@Video@@` in retail's own mangling,
// and AddSpriteDraw ADDS +0x08 to +0x00 and +0x0c to +0x04 to reach the far
// edges (0x18011c8bf, 0x18011c8de). The file-local `ClipRect` this header used
// to spell for it was the same four ints under other names; it is gone (one
// definition per layout).
class SpriteContext {
public:
    using SpriteDraw = ::UI::SpriteDraw;   // retail's nested spelling

    // B6-4.5: `saveload_draw.cpp:734` builds one on its stack and writes every
    // field itself (retail inlined the ctor there). Defaulted and PUBLIC; the
    // Array members zero themselves, +0x50's qword and +0x78/+0x80 are left as
    // the storage held, which is what retail's ctor does too.
    SpriteContext() = default;

    // ??0SpriteContext@UI@@QEAA@AEAVContext@Video@@@Z  0x18011d1e0 — retail's
    // stores, in address order: +0x00 = 1.0f; +0x08 = 0; +0x10 / +0x18 (the
    // _draws Array()); +0x20 / +0x28 (the four scissor ints); +0x30 =
    // &context; BYTE +0x38 = 0; +0x40 / +0x48 (_deferred); +0x58..+0x77 (the
    // two Array()s of _layout). +0x04, +0x39..+0x3f, +0x50 (_layout's
    // _width / _height: FontLayout has no constructor of its own), +0x78 and
    // +0x80 are NOT written. UserInterface::Draw (0x180121cad..0x180121cef)
    // inlines exactly these stores (B4 Y RT-UI).
    explicit SpriteContext(Video::Context& context)
        : _scale(1.0f), _currentSheet(nullptr), _draws(),
          _scissorX(0), _scissorY(0), _scissorW(0), _scissorH(0),
          _context(&context), _deferring(0), _deferred() {}
    // ??0SpriteContext@UI@@QEAA@AEBV01@@Z  0x1800321c0 — the deep member-wise
    // copy that pins sizeof (banner). NOT PORTED, no caller; declared only, so
    // no C++ caller can reach Collection::Array's SHALLOW defaulted copy.
    SpriteContext(const SpriteContext& other);
    SpriteContext& operator=(const SpriteContext&) = delete;   // retail exports none
    // ??1SpriteContext@UI@@QEAA@XZ  0x18011d250 — the implicit member-wise
    // destructor IS retail's body (banner).
    ~SpriteContext() = default;

    // ---- ported ------------------------------------------------------------
    // ?Begin@…@QEAAXMPEAVSpriteCapture@2@PEAVViewport@Video@@@Z  0x18011d2b0
    void Begin(float scale, SpriteCapture* capture, Video::Viewport* clipRegion);
    // ?AddLine@…@QEAAXAEBVSprite@2@HHHHI@Z  0x18011cbe0
    void AddLine(const Sprite& sprite, int32_t x1, int32_t y1,
                 int32_t x2, int32_t y2, uint32_t color);
    // The four AddSprite overloads — non-virtual C++ overloads, THE DOOR
    // DECIDES WHICH ONE A CALLER MEANT (the mangled parameter list).
    // ?AddSprite@…@QEAAXAEBVSprite@2@HHI@Z       0x18011cf00  point, natural size
    void AddSprite(const Sprite& sprite, int32_t x, int32_t y, uint32_t color);
    // ?AddSprite@…@QEAAXAEBVSprite@2@HHHHI@Z     0x18011d3c0  rect
    void AddSprite(const Sprite& sprite, int32_t x1, int32_t y1,
                   int32_t x2, int32_t y2, uint32_t color);
    // ?AddSprite@…@QEAAXAEBVSprite@2@HHHHIM@Z    0x18011cb10  rotated rect
    void AddSprite(const Sprite& sprite, int32_t x1, int32_t y1,
                   int32_t x2, int32_t y2, uint32_t color, float rotation);
    // ?AddSprite@…@QEAAXAEBVSprite@2@HHIM@Z      0x18011c780  NOT PORTED, no caller
    void AddSprite(const Sprite& sprite, int32_t x, int32_t y,
                   uint32_t color, float rotation);
    // ?AddFont@…@QEAAXV?$ExternalPtr@VFont@UI@@@Object@@AEBVString@System@@_NHW4Justify@Font@2@HHHI@Z
    //   0x18011ccb0. `layoutA`/`layoutB` are NOT named beyond their position
    //   (spritecontext_add.cpp); `justify` is the W4 enum, an int32.
    void AddFont(Object::ExternalPtr<Font> font, const System::String& text,
                 bool multiline, int32_t layoutA, int32_t justify, int32_t layoutB,
                 int32_t x, int32_t y, uint32_t color);
    // ?SetScissor@…@QEAA?AVViewport@Video@@AEBV34@@Z  0x18011d050 — RETSLOT in
    // retail; a plain by-value return here (not a vtable slot). Returns the
    // PREVIOUS scissor.
    Video::Viewport SetScissor(const Video::Viewport& scissor);
    // ?SetScissor@…@QEAA?AVViewport@Video@@HHHH@Z     0x18011d010 — RETSLOT.
    Video::Viewport SetScissor(int32_t x, int32_t y, int32_t width, int32_t height);

    // ---- exported, NOT PORTED, no caller: declared, deliberately undefined --
    void End();                                              // 0x18011d490
    void BeginDeferredFonts();                               // 0x18011c9d0
    void EndDeferredFonts();                                 // 0x18011c9e0
    Video::Viewport GetScissor() const;                      // 0x18011ca30  RETSLOT
    Video::Viewport GetScreenScissor() const;                // 0x18011ca00  RETSLOT
    void SetScreenScissor(const Video::Viewport& scissor);   // 0x18011caa0
    Video::Context& GetVideoContext();                       // 0x1800321b0

    // ---- retail `AEAA` (private) members, PUBLIC here ----------------------
    // Exported under private access; the doors and the not-yet-converted
    // builders (UserInterface::Draw, Save::SaveLoad::Draw) reach them, so they
    // stay public until those callers convert.
    // ?AddSpriteDraw@…@AEAAXAEBUSpriteDraw@12@@Z  0x18011c890
    void AddSpriteDraw(const SpriteDraw& draw);
    // ?Flush@SpriteContext@UI@@AEAAXXZ  0x18011ba40 — src/engine/ui/spritecontext_flush.cpp
    void Flush();
    // ?FlushDeferredFonts@SpriteContext@UI@@AEAAXXZ  0x18011c6a0
    void FlushDeferredFonts();
    // ?SetSpriteSheet@…@AEAAXV?$ExternalPtr@VSpriteSheet@UI@@@Object@@@Z
    //   0x18011c740 — NOT PORTED as a body: retail's batch-key switch is inlined
    //   into every caller the tree has ported (the `BindSheet` helper). Declared.
    void SetSpriteSheet(Object::ExternalPtr<SpriteSheet> sheet);

    // ---- the 0x88 bytes (PUBLIC: the two stack builders write them) --------
    float   _scale;                              // +0x00
    uint8_t _pad04[4];                           // +0x04
    SpriteSheet* _currentSheet;                  // +0x08  ExternalPtr<SpriteSheet> (B4 P2 H4: typed)

    // The batch being accumulated for `_currentSheet`. Flush drains it.
    Collection::Array<SpriteDraw> _draws;        // +0x10, 0x10 bytes

    // Device-space scissor. SetScissor divides the incoming (point-space)
    // rect by _scale on the way in and multiplies the outgoing one by _scale
    // on the way out — see the port for the ONE branch where it does not.
    int32_t _scissorX;                           // +0x20
    int32_t _scissorY;                           // +0x24
    int32_t _scissorW;                           // +0x28
    int32_t _scissorH;                           // +0x2c

    Video::Context* _context;                    // +0x30
    uint8_t _deferring;                          // +0x38
    uint8_t _pad39[7];                           // +0x39

    // Records queued while _deferring is set — retail's deferred font pass.
    Collection::Array<SpriteDraw> _deferred;     // +0x40, 0x10 bytes

    // UI::Font::Layout — AddFont hands `this + 0x50` to ComputeStringLayout as
    // its layout out-parameter, and the copy ctor copies +0x50 as ONE object
    // through the Layout's own copy ctor (0x180032280). Its qword at +0x50 is
    // never initialised by either builder (only ComputeStringLayout zeroes it).
    RtUI::FontLayout _layout;                    // +0x50 .. +0x77

    const Video::Viewport* _clipRegion;          // +0x78  Begin's 4th argument
    SpriteCapture*         _spriteCapture;       // +0x80  Begin's 3rd; may be null

    // NOT RETAIL (ext.sharp_ui, docs/extensions/DESIGN.md §3 rule 5): null by
    // default and in the faithful profile. The game side's Ext::SharpUI::Draw points
    // it at a SharpDraw (ui/SharpDraw.h) for the duration of one
    // UserInterface::Draw; Flush reads it once per flush and, when set, snaps
    // each axis-aligned quad's corners to whole device pixels and draws a
    // sheet that has a twin (a font variant, a 2x sheet) from the twin. A static: the
    // 0x88-byte layout above is untouched.
    static const SharpDraw* _extSharp;

    // NOT RETAIL (ext.render_pipe's replayed frames, docs/extensions/handins/SMOOTHREPLAY.md):
    // null by default and in the faithful profile. While the game side points it at a
    // SpriteRecord (ui/SpriteRecord.h), Flush appends each chunk it draws (its vertices and
    // the context state it drew them under); read once per flush. A static, as above.
    static SpriteRecord* _extRecord;
};

static_assert(sizeof(SpriteContext) == 0x88,
              "UI::SpriteContext is 0x88 bytes (the copy ctor 0x1800321c0's last "
              "member store is the qword at +0x80; B36_RULES section 13 item 2)");
static_assert(offsetof(SpriteContext, _pad04) == 0x4, "SpriteContext::_pad04");
static_assert(offsetof(SpriteContext, _pad39) == 0x39, "SpriteContext::_pad39");
static_assert(alignof(SpriteContext) == 8, "UI::SpriteContext aligns to 8");
static_assert(!std::is_polymorphic<SpriteContext>::value,
              "no vtable: Begin's first store to +0x00 is a MOVSS (0x18011d393)");
static_assert(offsetof(SpriteContext, _scale)         == 0x00, "_scale at +0x00");
static_assert(offsetof(SpriteContext, _currentSheet)  == 0x08, "_currentSheet at +0x08");
static_assert(offsetof(SpriteContext, _draws)         == 0x10, "_draws at +0x10");
static_assert(offsetof(SpriteContext, _scissorX)      == 0x20, "_scissorX at +0x20");
static_assert(offsetof(SpriteContext, _scissorY)      == 0x24, "_scissorY at +0x24");
static_assert(offsetof(SpriteContext, _scissorW)      == 0x28, "_scissorW at +0x28");
static_assert(offsetof(SpriteContext, _scissorH)      == 0x2c, "_scissorH at +0x2c");
static_assert(offsetof(SpriteContext, _context)       == 0x30, "_context at +0x30");
static_assert(offsetof(SpriteContext, _deferring)     == 0x38, "_deferring at +0x38");
static_assert(offsetof(SpriteContext, _deferred)      == 0x40, "_deferred at +0x40");
static_assert(offsetof(SpriteContext, _layout)        == 0x50, "_layout at +0x50");
static_assert(offsetof(SpriteContext, _layout) + offsetof(RtUI::FontLayout, _entries) == 0x58,
              "the glyph array AddFont walks is at +0x58 (size at +0x60)");
static_assert(offsetof(SpriteContext, _layout) + offsetof(RtUI::FontLayout, _lineWidths) == 0x68,
              "the line array the dtor frees first is at +0x68");
static_assert(offsetof(SpriteContext, _clipRegion)    == 0x78, "_clipRegion at +0x78");
static_assert(offsetof(SpriteContext, _spriteCapture) == 0x80, "_spriteCapture at +0x80");
static_assert(sizeof(Collection::Array<SpriteDraw>) == 0x10,
              "Array<T> is data/size/capacity = 0x10");

}  // namespace UI

// ============================================================================
// ENDGAME B.3.6 block D: THE SECOND `FontGlyph` IS GONE (RULE B6-4.2).
// This header used to declare a `UI::FontGlyph {spriteIndex, x, y, _unread0C}`
// for the 16-byte elements AddFont walks at +0x58. Those elements are
// `UI::Font::Layout`'s entries — the record ComputeLineLayout appends
// (0x180116d99..0x180116dc0) — i.e. `RtUI::FontLayoutEntry` in Font.h
// {_glyph, _x, _y, _line}, whose `_line` AddFont simply never reads. It was
// NOT `Font.h`'s `FontGlyph` (the glyph-metric row), which is why the two
// could not both keep the name. The `kSpriteContextLayout*` offsets that
// pointed into the old opaque blob are gone with it: `_layout` is typed.
// ============================================================================
