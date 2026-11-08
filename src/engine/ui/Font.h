// ============================================================================
// src/engine/ui/Font.h — UI::Font, 0x90 bytes, plus its two helper PODs
// ============================================================================
//
// `UI::Font` is an `Object::External` that owns a codepoint -> glyph table, a
// glyph metric table, and the sprite sheet the glyphs are cut from. It is the
// class behind every piece of text the engine draws: the three text-metric
// bodies ported in src/engine/ui/font.cpp (`ComputeStringLayout`,
// `ComputeLineLayout`, `ComputeCharacterPosition`) are the only readers of most
// of these fields.
//
// EVERY NUMBER HERE IS READ OUT OF THE SHIPPED RETAIL BYTES:
//
//   sizeof 0x90   `MOV EDX,0x90` at 0x18003214f — the eh_vector_destructor
//                 stride in the deleting destructor 0x180032130. Corroborated
//                 by the already-ported factory row (`"Font", 0x90ull, 8` in
//                 src/engine/ui/factory_ui1_tables.inc) whose last store is
//                 +0x88, and by the constructor 0x180031ce0, which zeroes
//                 +0x08 .. +0x88 and nothing past it.
//
//   +0x30 _charToGlyph   `MOV RAX,[RBP+0x30]` then `[RAX + cp*4]` at
//                        0x180116d16 — indexed by a DECODED CODEPOINT, and the
//                        bound tested one instruction earlier is +0x38, which
//                        is Array::_size. The element is int32 (`MOVSXD RDX,
//                        dword ptr [RAX+RCX*4]`).
//   +0x40 _glyphs        `SHL RBX,0x4 ; ADD RBX,[RBP+0x40]` at 0x180116d52 —
//                        16-byte stride, indexed by the GLYPH index the table
//                        above produced. Size at +0x48 (`MOV EAX,[RSI+0x48]`
//                        in Serialize at 0x1801178f2).
//   +0x50 _lineHeight    ComputeStringLayout adds it once per line
//                        (0x1801171a0) and hands it back as the height of a
//                        SINGLE line at 0x180117076.
//   +0x58 _ellipsisWidth Serialize computes it, for a file older than reader
//                        version 1, by measuring the literal "..."
//                        (0x180159138) — 0x180117a99 — and ComputeLineLayout
//                        reserves exactly it before deciding to elide
//                        (0x180116d6e).
//   +0x5c _spaceWidth    the same loop over the literal " " (0x1801567e0),
//                        stored at 0x180117b28. No body in this class READS
//                        it; the name comes from what is measured.
//   +0x60 _spriteSheet   serialized by 0x180004590, which our tree already
//                        names `ExternalPtr<UI::SpriteSheet>::Serialize`
//                        (src/engine/object/external_ptr_serialize.cpp). The
//                        metric bodies read a count at +0x28 of the pointee and
//                        an array of 0x38-byte sprite records at +0x20.
//   +0x68 _material      serialized by 0x180014520 =
//                        `ExternalPtr<Graphics::MaterialInstance>::Serialize`,
//                        same file.
//
// FIELDS DELIBERATELY LEFT UNNAMED. +0x20, +0x78, +0x80 and +0x88 are four
// `System::String`s (the destructor 0x1801175a0 releases all four through the
// refcount-at-data[-4] sequence, and Serialize hands all four to
// `String::Serialize` 0x1800c9930). Serialize fixes their ORDER but says
// nothing about their meaning, and one class is not enough to name a field.
// +0x28 is whatever 0x180076e90 serializes and +0x54/+0x70/+0x74 are three
// plain int32s; same rule.
//
// PORTABILITY: no <windows.h>, per PLAN.md §4.1 decision 1.
// ============================================================================
#pragma once

#include <cstdint>
#include <cstddef>

#include "../collection/Collection.h"
#include "../object/External.h"     // Object::External, ExternalPtr<T>
#include "../object/Internal.h"     // Object::detail::InternalMemFree
#include "../system/String.h"       // System::String (the four name fields)

namespace RtUI {

// One row of UI::Font::_glyphs, 16 bytes. Named from the three bodies that
// walk it, which agree field for field:
//   penX += _preAdvance + <carried kern>   ; the glyph is DRAWN at that penX
//   penX += _postAdvance
//   <carried kern> = _postKern             ; applied before the NEXT glyph
//   glyphY = baseY - _offsetY - <sprite top>
// (0x180116d7f..0x180116dd1 in ComputeLineLayout; 0x180116f8f..0x180116f9b in
// ComputeCharacterPosition; 0x180117318..0x180117335 in ComputeStringLayout.)
struct FontGlyph {
    int32_t _preAdvance;    // +0x00
    int32_t _postKern;      // +0x04
    int32_t _postAdvance;   // +0x08
    int32_t _offsetY;       // +0x0c
};
static_assert(sizeof(FontGlyph) == 0x10, "UI::Font glyph stride 0x10 (SHL RBX,4)");
static_assert(offsetof(FontGlyph, _preAdvance) == 0x0, "FontGlyph::_preAdvance");
static_assert(offsetof(FontGlyph, _postKern) == 0x4, "FontGlyph::_postKern");
static_assert(offsetof(FontGlyph, _postAdvance) == 0x8, "FontGlyph::_postAdvance");
static_assert(offsetof(FontGlyph, _offsetY) == 0xC, "FontGlyph::_offsetY");

// One placed glyph in a UI::Font::Layout, 16 bytes. Built on the stack at
// [RSP+0x58] in ComputeLineLayout (0x180116d99..0x180116dc0) and appended by
// 0x180076d10; the justify pass at the end of ComputeStringLayout re-reads
// `_line` (0x1801174be) and adjusts `_x` (0x1801174d2 / 0x1801174de).
struct FontLayoutEntry {
    int32_t _glyph;         // +0x00  the GLYPH (= sprite) index, NOT the
                            //        codepoint: ComputeLineLayout stores the
                            //        looked-up glyph (font.cpp: storing `cp`
                            //        here drew "n" for "New"), and AddFont
                            //        hands it to AddSprite as the sprite index
    int32_t _x;             // +0x04
    int32_t _y;             // +0x08
    int32_t _line;          // +0x0c  index into Layout::_lineWidths
};
static_assert(sizeof(FontLayoutEntry) == 0x10, "Layout entry stride 0x10");
static_assert(offsetof(FontLayoutEntry, _glyph) == 0x0, "FontLayoutEntry::_glyph");
static_assert(offsetof(FontLayoutEntry, _x) == 0x4, "FontLayoutEntry::_x");
static_assert(offsetof(FontLayoutEntry, _y) == 0x8, "FontLayoutEntry::_y");
static_assert(offsetof(FontLayoutEntry, _line) == 0xC, "FontLayoutEntry::_line");

// UI::Font::Layout, 0x28 bytes — the out-parameter of ComputeStringLayout.
// ComputeStringLayout's own prologue names every field: it clears +0x18 with
// `Array<int32>::SetSize(.,0)` (0x18002fea0 at 0x18011700b), clears +0x08 with
// 0x180076a40, and zeroes +0x00..+0x07 with ONE qword store (0x180117021),
// which is why _width and _height are adjacent int32s. It fills them back in
// at 0x1801174f0 / 0x1801174f5.
struct FontLayout {
    int32_t                              _width;        // +0x00
    int32_t                              _height;       // +0x04
    Collection::Array<FontLayoutEntry>   _entries;      // +0x08
    Collection::Array<int32_t>           _lineWidths;   // +0x18
};
static_assert(offsetof(FontLayout, _entries) == 0x08, "Layout::_entries @ +0x08");
static_assert(offsetof(FontLayout, _entries._size) == 0x10,
              "Layout::_entries._size @ +0x10 (MOV EDX,[RDI+0x10] at 0x18011718a)");
static_assert(offsetof(FontLayout, _lineWidths) == 0x18, "Layout::_lineWidths @ +0x18");
static_assert(offsetof(FontLayout, _lineWidths._size) == 0x20,
              "Layout::_lineWidths._size @ +0x20 (MOV EDX,[RDI+0x20] at 0x180117190)");
static_assert(sizeof(FontLayout) == 0x28, "sizeof(UI::Font::Layout) == 0x28");
static_assert(offsetof(FontLayout, _width) == 0x0, "FontLayout::_width");
static_assert(offsetof(FontLayout, _height) == 0x4, "FontLayout::_height");

// UI::Font::Justify, the W4 enum ComputeStringLayout's fifth parameter carries.
// Only two values are ever tested (0x1801174c5 and 0x1801174d8); 0 is every
// other value and is the do-nothing case, so it is named for what it does.
enum FontJustify : int32_t {
    kFontJustifyLeft   = 0,
    kFontJustifyCenter = 1,
    kFontJustifyRight  = 2,
};

}  // namespace RtUI

// ############################################################################
// UI::Font — the real class (ENDGAME B.3.6 block D). 0x90 bytes, NINE slots,
// `Object::External` (`?_ParentTypeName@Font@UI@@` 0x180031990 ->
// "Object::External"). It replaces the `RtUI::Font` POD this header carried
// (its one reader, spritecontext_add.cpp, is in the same block): ONE
// definition of the layout, and every offset above is re-asserted below.
//
// `??_7Font@UI@@6B@` @ 0x180155b50 (B36_SLOTS.md section 1):
//   [0] +0x00 0x180032130  (unexported) vector deleting dtor — the class's own (B.3.9)
//   [1] +0x08 0x1800319c0  ?TypeName@Font@UI@@UEBAPEBDXZ
//   [2] +0x10 0x1800319a0  ?Type@Font@UI@@UEBAIXZ
//   [3] +0x18 0x1801176b0  ?GetFileVersion@Font@UI@@UEBAHXZ  (MOV EAX,8)
//   [4] +0x20 0x1801176c0  ?Compile@Font@UI@@…  — OVERRIDDEN (RULE B6-1.5)
//   [5] +0x28 0x1801176d0  ?Serialize@Font@UI@@UEAA_NAEAVStream@IO@@@Z
//   [6] +0x30 0x180031c40  ?Reconstruct@Font@UI@@UEAAPEAVExternal@Object@@XZ
//   [7] +0x38 0x180031d80  ?Destruct@Font@UI@@UEAAXXZ
//   [8] +0x40 0x180031ce0  ?Construct@Font@UI@@UEAAPEAVExternal@Object@@XZ — B6-1.1
//
// THREE FIELDS NAMED THIS PASS FROM THE EXPORTED GETTERS, read as retail
// disassembly (they settle what this header left unnamed or named by use):
//   ?GetHeight@Font@UI@@QEBAHXZ      0x180031de0  MOV EAX,[RCX+0x50] — the
//                                    `_lineHeight` ComputeStringLayout adds per line
//   ?GetBaseLine@Font@UI@@QEBAHXZ    0x180031df0  MOV EAX,[RCX+0x54] — was `_field54`
//   ?GetSpaceWidth@Font@UI@@QEBAHXZ  0x180031e00  MOV EAX,[RCX+0x5c] — `_spaceWidth` confirmed
//   ?GetKern@Font@UI@@QEBAAEBUKerning@12@H@Z  0x180031dd0  `&_glyphs._data[i]`
//                                    (SHL 4 + [RCX+0x40]) — so the 16-byte glyph
//                                    row IS retail's `UI::Font::Kerning`
//   ?GetSpriteIndex@Font@UI@@QEBAHH@Z 0x180031da0  `cp` bounds-checked against
//                                    +0x38, `_charToGlyph[cp]`, 0xffff -> -1
//   ?GetSpriteSheet@Font@UI@@…       0x180031d90  MOV RAX,[RCX+0x60]; MOV [RDX],RAX
//                                    — a hidden RETSLOT
//
// ENDGAME B.3 step 9 (block UI-D, B39_RULES D1/D2): CONCRETE, the
// CharacterSet.h shape. The real public ctor is the inlined one (21 stores in
// Construct 0x180031ce0; +0x5c is NOT written), slot 0 the class's own vector
// deleting dtor, ~Font virtual, with the retired ??1 door's
// body. The factory pair is the class's own (font.cpp), replacing the FACTUI1
// row; the minted `??_7Font@UI@@6B@` and its RT_DELDTOR row are retired.
// ############################################################################

namespace System { class Filename; class String; }
namespace Graphics { class MaterialInstance; }

namespace UI {

class SpriteSheet;
class CharacterSet;

class Font : public Object::External {
public:
    // _charToGlyph entry of a codepoint the font has no glyph for
    // (GetSpriteIndex 0x180031da0 maps it to -1).
    static constexpr int32_t kNoGlyph = 0xffff;

    // Retail's nested names (`Kerning@Font@UI`, `Layout@Font@UI`,
    // `Justify@Font@UI` in the exported manglings).
    using Kerning = RtUI::FontGlyph;
    using Layout  = RtUI::FontLayout;
    using Justify = int32_t;   // W4 enum: EAX-sized; see RtUI::FontJustify

    // The REAL constructor (the 21 stores of 0x180031cf6..0x180031d67 minus the
    // three vptr stores): External() zeroes +0x08..+0x18; the two Arrays and the
    // ExternalPtr zero themselves (+0x30/+0x38, +0x40/+0x48, +0x60); +0x58 is a
    // DWORD store and +0x5c is NEVER written (retail — `_spaceWidth` is what
    // the allocator left until Serialize fills it).
    // The four Strings' null stores (180031d2a, 180031d55 / 59 / 60) are the
    // members' own constructors, run in declaration order before this body.
    Font() : Object::External() {
        _characterSet       = nullptr;                        // 180031d2e
        _lineHeight    = 0;                              // 180031d42 (qword: +0x50/+0x54)
        _baseLine      = 0;
        _ellipsisWidth = 0;                              // 180031d46 DWORD
        _material      = nullptr;                        // 180031d4d
        _sheetWidth       = 0;                              // 180031d51 (qword: +0x70/+0x74)
        _sheetHeight       = 0;
    }

    // [0] 0x180032130 — MSVC's VECTOR deleting destructor, both arms exactly as
    // the retired RT_DELDTOR row ran them (`MOV EDX,0x90`, the element size).
    // 0x1801175a0 — retail's sequence is the members' destructors in reverse
    // declaration order (font.cpp): the body is empty.
    ~Font() override;

    // ENDGAME B.3.9 TI-UI: ONE reflection block (B39 D6); `_typeInfo` is the
    // class's own record (DEFINE_TYPE in the class's TU), no longer minted.
    // retail: TypeName 0x1800319c0, Type 0x1800319a0, CreateNewObject 0x180031ab0, CreateInPlaceObject 0x180031ba0
    DECLARE_TYPE(Font)
public:
    int32_t     GetFileVersion() const override;        // [3] 0x1801176b0
    CompileResult Compile(const System::Filename& path) override;  // [4] 0x1801176c0 — OVERRIDE
    bool        Serialize(IO::Stream& stream) override;       // [5] 0x1801176d0
    Object::External* Reconstruct() override;           // [6] 0x180031c40
    void        Destruct() override;                    // [7] 0x180031d80
    virtual Object::External* Construct();              // [8] 0x180031ce0 — B6-1.1

    // ---- ported non-virtual members (src/engine/ui/font.cpp) --------------
    // ?ComputeLineLayout@Font@UI@@QEBAHPEBDHHHHHPEAULayout@12@@Z  0x180116c70
    int32_t ComputeLineLayout(const char* text, int32_t count, int32_t startX,
                              int32_t baseY, int32_t maxWidth, int32_t line,
                              Layout* layout) const;
    // ?ComputeCharacterPosition@Font@UI@@QEBAHAEBVString@System@@H@Z  0x180116ee0
    int32_t ComputeCharacterPosition(const System::String& text, int32_t count) const;
    // ?ComputeStringLayout@Font@UI@@QEBAXAEBVString@System@@_NHHW4Justify@12@AEAH3PEAULayout@12@@Z
    //   0x180116fd0 — `AEAH`/`AEAH` are by-REFERENCE outs, `X` returns void.
    void ComputeStringLayout(const System::String& text, bool multiline,
                             int32_t wrapWidth, int32_t maxWidth, Justify justify,
                             int32_t& outWidth, int32_t& outHeight,
                             Layout* layout) const;

    // ---- the getters (read this pass; see the banner) ----------------------
    int32_t GetHeight() const     { return _lineHeight; }  // 0x180031de0
    int32_t GetBaseLine() const   { return _baseLine; }    // 0x180031df0
    int32_t GetSpaceWidth() const { return _spaceWidth; }  // 0x180031e00
    const Kerning& GetKern(int32_t glyph) const { return _glyphs._data[glyph]; }  // 0x180031dd0
    int32_t GetSpriteIndex(int32_t codepoint) const {      // 0x180031da0
        if (codepoint < 0 || codepoint >= _charToGlyph._size) return -1;
        const int32_t g = _charToGlyph._data[codepoint];
        return (g == kNoGlyph) ? -1 : g;
    }
    // 0x180031d90 — RETSLOT in retail; a plain by-value return here.
    Object::ExternalPtr<SpriteSheet> GetSpriteSheet() const { return _spriteSheet; }

    // ---- exported, NOT PORTED, no caller: declared, deliberately undefined --
    Font(const Font& src);                              // 0x180031e10
    Font& operator=(const Font& src);                   // 0x180031f60
    // 0x1800319b0 — the minted record's type id (font.cpp).
    // Placement is spelt `::new (p) Font()` (B9-5): these stay undefined.
    static void* operator new(std::size_t bytes);              // 0x1800319d0
    static void* operator new(std::size_t bytes, void* place); // 0x180031a40
    static void  operator delete(void* p) { ::Object::detail::InternalMemFree(p); }                     // 0x180031a50
    // Retail's factory pair (B39_RULES D2), defined and registered in font.cpp.

public:
    // ---- the 0x70 bytes Font adds (public: the doors and AddFont read them) --
    System::String                     _fontName;      // +0x20  ctor 180031d2a, dtor 180117664
    CharacterSet*                      _characterSet;       // +0x28  ExternalPtr<CharacterSet>, serialized by 0x180076e90 (B4 H4: typed)
    Collection::Array<int32_t>         _charToGlyph;   // +0x30  codepoint -> glyph index
    Collection::Array<Kerning>         _glyphs;        // +0x40  GetKern
    int32_t                            _lineHeight;    // +0x50  GetHeight
    int32_t                            _baseLine;      // +0x54  GetBaseLine
    int32_t                            _ellipsisWidth; // +0x58
    int32_t                            _spaceWidth;    // +0x5c  GetSpaceWidth
    Object::ExternalPtr<SpriteSheet>   _spriteSheet;   // +0x60  GetSpriteSheet
    Graphics::MaterialInstance*        _material;      // +0x68  ExternalPtr<MaterialInstance> (B4 H4: typed)
    int32_t                            _sheetWidth;       // +0x70
    int32_t                            _sheetHeight;       // +0x74
    System::String                     _imageName;      // +0x78  ctor 180031d55, dtor 18011762a
    System::String                     _sheetName;      // +0x80  ctor 180031d59, dtor 1801175f8
    System::String                     _materialName;      // +0x88  ctor 180031d60, dtor 1801175c5
};

static_assert(sizeof(Font) == 0x90, "sizeof(UI::Font) == 0x90 (MOV EDX,0x90 at 0x18003214f)");
static_assert(alignof(Font) == 8, "UI::Font aligns to 8 (the vptr)");
static_assert(offsetof(Font, _fontName)      == 0x20, "Font::_fontName @ +0x20");
static_assert(offsetof(Font, _characterSet)       == 0x28, "Font::_characterSet @ +0x28");
static_assert(offsetof(Font, _charToGlyph)   == 0x30, "Font::_charToGlyph @ +0x30");
static_assert(offsetof(Font, _charToGlyph._size) == 0x38,
              "Font::_charToGlyph._size @ +0x38 (CMP EAX,[RBP+0x38] at 0x180116d0a)");
static_assert(offsetof(Font, _glyphs)        == 0x40, "Font::_glyphs @ +0x40");
static_assert(offsetof(Font, _glyphs._size)  == 0x48,
              "Font::_glyphs._size @ +0x48 (MOV EAX,[RSI+0x48] at 0x1801178f2)");
static_assert(offsetof(Font, _lineHeight)    == 0x50, "GetHeight 0x180031de0 reads +0x50");
static_assert(offsetof(Font, _baseLine)      == 0x54, "GetBaseLine 0x180031df0 reads +0x54");
static_assert(offsetof(Font, _ellipsisWidth) == 0x58, "Font::_ellipsisWidth @ +0x58");
static_assert(offsetof(Font, _spaceWidth)    == 0x5c, "GetSpaceWidth 0x180031e00 reads +0x5c");
static_assert(offsetof(Font, _spriteSheet)   == 0x60, "GetSpriteSheet 0x180031d90 reads +0x60");
static_assert(offsetof(Font, _material)      == 0x68, "Font::_material @ +0x68");
static_assert(offsetof(Font, _sheetWidth)       == 0x70, "Font::_sheetWidth @ +0x70");
static_assert(offsetof(Font, _sheetHeight)       == 0x74, "Font::_sheetHeight @ +0x74");
static_assert(offsetof(Font, _imageName)      == 0x78, "Font::_imageName @ +0x78");
static_assert(offsetof(Font, _sheetName)      == 0x80, "Font::_sheetName @ +0x80");
static_assert(offsetof(Font, _materialName)      == 0x88, "Font::_materialName @ +0x88");
static_assert(offsetof(Font, _fontName) == sizeof(Object::External),
              "Font's own bytes begin right after Object::External");

}  // namespace UI
