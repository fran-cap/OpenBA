// ============================================================================
// src/engine/ui/font.cpp — UI::Font, the whole class plus its text metrics
// ============================================================================
//
// ??_7Font@UI@@6B@ (retail 0x180155b50), NINE slots — one fewer than the
// ElementDescription family, because UI::Font is a direct Object::External
// subclass and has no `CreateInstance`:
//
//   0  deleting dtor      0x180032130  Font.h (the class's own, B.3.9). Its
//                                      array arm encodes ~Font 0x1801175a0 and
//                                      sizeof 0x90 (`LEA R9,[0x1801175a0]` and
//                                      `MOV EDX,0x90` at 0x180032148/0x18003214f).
//   1  TypeName           0x1800319c0  here
//   2  Type               0x1800319a0  here
//   3  GetFileVersion     0x1801176b0  here  (returns 8)
//   4  Compile            0x1801176c0  here  (returns 1 — NOT the base's
//                                      External::Compile; this class overrides
//                                      it with its own two-instruction body)
//   5  Serialize          0x1801176d0  here
//   6  Reconstruct        0x180031c40  here
//   7  Destruct           0x180031d80  here
//   8  Construct          0x180031ce0  here
//
// plus, off the vtable:
//   ??1Font@UI@@UEAA@XZ                          0x1801175a0  here
//   ?ComputeStringLayout@Font@UI@@                0x180116fd0  here
//   ?ComputeLineLayout@Font@UI@@                  0x180116c70  here
//   ?ComputeCharacterPosition@Font@UI@@           0x180116ee0  here
//
// and the factory pair `?CreateNewObject@Font@UI@@` (0x180031ab0) /
// `?CreateInPlaceObject@Font@UI@@` (0x180031ba0) — ENDGAME B.3.9 block UI-D
// (B39_RULES D2): the class's own statics, replacing the FACTUI1 row.
// `??3Font@UI@@SAXPEAX@Z` (0x180031a50), the class operator delete, is ported
// in src/engine/ui/ui_operator_delete.cpp.
//
// Layout: src/engine/ui/Font.h (sizeof 0x90).
//
// ----------------------------------------------------------------------------
// THE CONSTRUCTOR IS INLINED AND THERE IS NO OUT-OF-LINE ??0Font@UI@@.
// ----------------------------------------------------------------------------
// `Construct` (0x180031ce0), `Reconstruct` (0x180031c40) and both already-
// ported factory steps each open-code the same 21 stores; the DLL exports no
// `??0Font@UI@@`. ENDGAME B.3.9 block UI-D: that sequence (0x180031cf6..
// 0x180031d67) is the class's REAL constructor, in Font.h, and every live Font
// wears the COMPILER's table. Two details worth keeping visible:
//   * `+0x00` is written THREE TIMES (Object::Base's vtable, then
//     Object::External's, then Font's). Those are the two inlined base
//     constructors followed by this class's own stamp; all three stores run.
//   * `+0x58` is a DWORD store and `+0x5c` is never written at all
//     (`MOV dword ptr [RCX+0x58],EDX` at 0x180031d46 is followed by a QWORD
//     store to +0x60). So a fresh Font's `_spaceWidth` is whatever the
//     allocator handed back, and Serialize only ever writes it on the
//     reader-version-0 path. That is retail's behaviour, not an omission here.
//
// ----------------------------------------------------------------------------
// NO HIDDEN RETURN SLOT ANYWHERE IN THIS FILE. Checked against every body:
//   * `?Compile@...UEAA?AW4CompileResult@External@Object@@...` LOOKS like the
//     `?A` by-value-return shape that costs a hidden RDX pointer, and it is
//     NOT: `W4` is an enum, MSVC returns it in EAX, and the whole body is
//     `MOV EAX,0x1 ; RET` (0x1801176c0) with RDX never touched. The Filename&
//     stays in RDX.
//   * `ComputeStringLayout`'s `AEAH`/`AEAH`/`PEAULayout` are by-REFERENCE out
//     parameters on the stack (read at [RSP+0xe0], [RSP+0xe8], [RSP+0xf0]),
//     and the function returns `X` (void). No slot.
//   * `ComputeLineLayout` and `ComputeCharacterPosition` both return `H` (int)
//     in EAX.
// ============================================================================
#include <cstddef>
#include <cstdint>

#include "io/Serializer.h"
#include "ui/Font.h"
#include "ui/SpriteSheet.h"   // B4: the glyph sheet's sprite count / records
#include "system/String.h"
#include "io/Stream.h"
#include "object/External.h"
#include "system/MemoryAllocator.h"
#include "collection/Collection.h"
#include "ui/CharacterSet.h"   // B.4 P2 X: T::GetTypeInfo() for Object::SerializeExternal<T>
#include "ui/SpriteSheet.h"   // B.4 P2 X: T::GetTypeInfo() for Object::SerializeExternal<T>
#include "graphics/Material.h"   // B.4 P2 X: T::GetTypeInfo() for Object::SerializeExternal<T>
#include <new>

namespace {

// ---- the dependencies these bodies call -------------------------------------
//
// B3R RT-UI: every one by name -- System::String::Serialize (0x1800c9930), the
// ExternalPtr<>::Serialize bodies (src/engine/object/External.h), IO's
// DecodeOne (0x1800c9fc0), the rtcoll Array bodies (Collection.h) and
// MemoryAllocator::MemFree (0x1800c7ad0).

void StringSerialize(System::String& str, IO::Serializer* ser) {
    str.Serialize(*ser);
}

// ?MemFree@MemoryAllocator@System@@SAXPEAX@Z — 0x1800c7ad0. Retail inlines the
// EnterCriticalSection(alloc+0x640) / Free / LeaveCriticalSection triple at
// each site; the exported function IS that triple with an extra `if (p)` in
// front of it, so calling it from inside retail's own `if (p)` is instruction
// for instruction the same work. src/engine/collection/array.cpp:171 records
// the same equivalence for the same reason.
void MemFree(void* p) { System::MemoryAllocator::MemFree(p); }

// IO::TextCodec::DecodeOne, 0x1800c9fc0. Every caller in this file passes
// format 2 (UTF-8) in BOTH the ECX and the arg-5 position, exactly as retail
// does (`MOV ECX,0x2` + `MOV dword ptr [RSP+0x20],0x2`).
int32_t DecodeUtf8(const void* src, const void* end, int32_t* cp) {
    return RtIo_TextCodec_DecodeOne(IO::Text::Format::Utf8, src, end, cp, IO::Text::Format::Utf8);
}

// The two literals Serialize measures when it meets a file written before
// reader version 1, and the one ComputeLineLayout elides with. Retail points
// at its own .rdata — 0x180159138 is "..." (three bytes, and the loop bound is
// literally 3) and 0x1801567e0 is " " — and both byte strings were read out of
// the shipped image to get them right.
//
// THEY ARE OUR OWN STORAGE HERE, NOT THOSE ADDRESSES. A raw 0x180159138 would
// be a Ghidra address, not a runtime one: the DLL is relocated by ASLR today
// and is not mapped at all after cutover. What the bodies consume is the
// CONTENT, and the content is three dots and a space.
const char kEllipsisText[] = "...";
const char kSpaceText[]    = " ";

// ---- the reader/writer's scalar slot (+0xa0), by name -----------------------
void ScalarInt(IO::ReaderWriter* rw, int32_t& value) {
    rw->SerializeInt(value, nullptr);
}

// The per-codepoint advance walk, shared by both of Serialize's measuring
// loops (0x180117a20..0x180117a93 and 0x180117ab0..0x180117b22 — the two are
// instruction for instruction the same, over different literals and bounds).
// It is written once here because retail's two copies are one inlined helper.
int32_t MeasureLiteral(const UI::Font& font, const char* text, int32_t bytes) {
    int32_t total = 0;   // R15D / EDI
    int32_t kern  = 0;   // R13D / R15D
    int32_t idx   = 0;   // EBX

    do {
        int32_t cp = 0;
        idx += DecodeUtf8(text + idx, text + bytes, &cp);            // 180117a3e

        if (cp >= 0 && cp < font._charToGlyph._size) {
            const int32_t glyph = font._charToGlyph._data[cp];
            if (glyph != UI::Font::kNoGlyph && glyph != -1 && glyph >= 0) {  // 180117a5c..a6f
                const UI::SpriteSheet* sheet = font._spriteSheet.Get();
                // NOT RETAIL: retail bounds the glyph by the sheet only; `_glyphs` comes from
                // the font resource and may be shorter (heap out-of-bounds read).
                if (glyph < sheet->m_sprites._size && glyph < font._glyphs._size) {
                    const UI::Font::Kerning& e = font._glyphs._data[glyph];
                    total += e._postAdvance + e._preAdvance + kern;  // 180117a81..a89
                    kern = e._postKern;                              // 180117a8c
                }
            }
        }
    } while (idx < bytes);                                           // 180117a90

    return total + kern;                                             // 180117a95
}

}  // namespace


// ============================================================================
// The four one-liners
// ============================================================================
// 0x1800319a0: `MOV EAX,[0x1801fab68] ; RET` — the first dword of the class's
// own BaseTypeInfo node. 0x1801fab68 IS `?_typeInfo@Font@UI@@0VBaseTypeInfo@
// Object@@A`, so this reads it through the name rather than the address.
// ENDGAME B.3.6 block D: every body below is a UI::Font member; the doors
// that keep the registered names/addresses follow the members and delegate
// with a QUALIFIED call (a live Font wears the MINTED table).
namespace UI {

// 0x1800319c0: `MOV RAX,[0x1801fab70] ; RET` — the same node at +8.
// 0x1801176b0: `MOV EAX,0x8 ; RET`. Eight — a plain integer, unlike the
// *Description family's packed 0x104 / 0x204 / 0x10204.
int32_t Font::GetFileVersion() const { return 8; }
// 0x1801176c0: `MOV EAX,0x1 ; RET`. The Filename& in RDX is never read. See
// the banner: W4CompileResult comes back in EAX, there is no hidden slot.
Object::External::CompileResult Font::Compile(const System::Filename&) { return 1; }

// ============================================================================
// Construct / Reconstruct / Destruct
// ============================================================================
// 0x180031ce0: `TEST RCX,RCX ; JZ ret0`, then the inlined constructor, then
// `MOV RAX,RCX`. (The null test sat in the retired door; a virtual call always
// has a `this`.)
Object::External* Font::Construct() {
    return ::new (this) Font();                                      // 180031cf6..180031d67
}
// 0x180031c40: slot0(this, 0) — the deleting destructor with the free bit
// CLEAR, i.e. destroy in place, VIRTUALLY — and then the same inlined
// constructor. NOT a tail jump: there is no out-of-line ctor to jump to.
Object::External* Font::Reconstruct() {
    this->~Font();                                                 // 180031c57
    return ::new (this) Font();                                      // 180031c5e..180031ccf
}
// 0x180031d80: `MOV RAX,[RCX] ; XOR EDX,EDX ; JMP [RAX]`.
void Font::Destruct() { this->~Font(); }

// ?CreateNewObject@Font@UI@@SAPEAVBase@Object@@XZ  0x180031ab0
// The FACTUI1 row this replaces: the locked MemAlloc of 0x90 bytes, align 8;
// retail's null branch; then the inlined constructor.
Object::Base* Font::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(Font), 8);
    if (p == nullptr) return nullptr;
    return ::new (p) Font();
}
// ?CreateInPlaceObject@Font@UI@@SAPEAVBase@Object@@PEAX@Z  0x180031ba0
Object::Base* Font::CreateInPlaceObject(void* place) {
    if (place == nullptr) return nullptr;
    return ::new (place) Font();
}

// ============================================================================
// ??1Font@UI@@UEAA@XZ   0x1801175a0 — retail's body, moved here from the
// retired `RtUi_Font_Dtor` door (ENDGAME B.3.9 UI-D). Retail's sequence:
//     release String +0x88, +0x80, +0x78                 ; 1801175c5..18011764c
//     Array<FontGlyph>::~Array(this + 0x40)   0x180033c50 ; 180117654
//     Array<int32>::~Array   (this + 0x30)    0x18001c750 ; 18011765e
//     release String +0x20                                ; 180117664
//     JMP ~External                           0x1800bc950 ; 180117697
// That is exactly the members' destructors in reverse declaration order
// (the four Strings are System::String members since B4 X-R3, the two Arrays
// Collection::Array members: free `_data`, `_capacity` left alone — the same
// body both retail copies compile to), which the compiler runs after this
// empty body, then ~External. The +0x20 release follows the two array frees,
// as in retail.
Font::~Font() {
}

}  // namespace UI

// ============================================================================
// ?Serialize@Font@UI@@UEAA_NAEAVStream@IO@@@Z   0x1801176d0
// ============================================================================
// The reader/writer is built with THIRD ARGUMENT 2, not 0 (`LEA R8D,[RDI+0x2]`
// at 0x180117708, RDI being the zero register here). Every other Serialize in
// this family passes 0; this one does not, and the 2 is transcribed as-is.
//
// The field order below is the file format and is NOT the declaration order:
// +0x20, +0x50, +0x80, +0x70, +0x74, +0x88, +0x78, +0x28, +0x60, +0x68, then
// the two tables, then +0x54, then the pair +0x58/+0x5c.
//
// TWO VERSION GATES, both asking the READER/WRITER for its version
// (vt[0x08]), not this class's GetFileVersion:
//   * < 2  — the codepoint table was written as UNSIGNED SHORTS. Read them
//            into a scratch Array<uint16>, then widen each into the int32
//            table. The scratch buffer is freed by hand.
//   * >= 2 — the table is int32 and goes through vt[0x48] (kArrayInt) directly.
//   * < 1  — +0x58 and +0x5c were not in the file at all, so they are
//            RECOMPUTED by measuring "..." and " " with the font's own glyph
//            metrics. That is what gives ComputeLineLayout its ellipsis
//            reserve for an old file.
namespace UI {

// The body reads its own fields at the file format's offsets (+0x20, +0x50,
// +0x80 ...) through `p`, in FORMAT order, which is not declaration order;
// every offset is asserted against a named member in Font.h.
bool Font::Serialize(IO::Stream& streamv) {
    // LEFT: the Serialize slot's parameter is `void*` in Object::External
    // (block R-IO); retail's is IO::Stream&.
    IO::Stream* const stream = &streamv;

    // 0x180117704..0x180117720: `??0Serializer@IO@@` (0x1800b9960) INLINED —
    // the stream, a null reader/writer, then the encoding's CreateReader /
    // CreateWriter slot with THIRD ARGUMENT 2 (`LEA R8D,[RDI+0x2]` at
    // 0x180117708), stored back. Every other Serialize in this family passes 0.
    IO::Serializer ser(*stream, 2);                                  // [RBP-0x28], [RBP-0x20]
    IO::ReaderWriter* const rw = ser._readerWriter;

    StringSerialize(_fontName, &ser);                                // 18011772c
    ScalarInt(rw, _lineHeight);                                      // 18011773f
    StringSerialize(_sheetName, &ser);                                // 180117750
    ScalarInt(rw, _sheetWidth);                                         // 180117763
    ScalarInt(rw, _sheetHeight);                                         // 180117777
    StringSerialize(_materialName, &ser);                                // 180117788
    StringSerialize(_imageName, &ser);                                // 180117795

    // LEFT: the ExternalPtr<T>::Serialize thunks take RtObject::External** and
    // the rt::io::Serializer POD (External.h, block R-IO).
    Object::SerializeExternal<UI::CharacterSet>(ser, _characterSet);                  // 1801177a2
    _spriteSheet.Serialize(ser);                  // 1801177af  ExternalPtr<SpriteSheet>
    Object::SerializeExternal<Graphics::MaterialInstance>(ser, _material);                  // 1801177bc  ExternalPtr<MaterialInstance>

    const int32_t version = rw->GetVersion();                        // 1801177c8

    if (version < 2) {                                               // 1801177ce
        // ---- legacy uint16 codepoint table -----------------------
        struct { uint16_t* _data; int32_t _size; int32_t _capacity; } tmp =
            { nullptr, 0, 0 };                                       // [RBP-0x18]
        int32_t n = 0;                                               // [RBP+0x30]

        ScalarInt(rw, n);                                            // 1801177f4

        if (ser._stream->_direction == 0) {                          // 1801177fe (re-read)
            rtcoll::RtColl_ArraySetSize_1800774d0(&tmp, n);           // 18011780b
        }

        rw->SerializeUShortArray(n, tmp._data, nullptr);             // 180117827 (+0x50)

        rtcoll::RtColl_ArraySetSize_18002fea0(&_charToGlyph, tmp._size);   // 180117833

        for (int32_t i = 0; i < tmp._size; ++i) {                    // 180117850
            // `MOV RAX,qword ptr [R14]` sits INSIDE the loop: the destination
            // pointer is re-read every iteration, not hoisted.
            _charToGlyph._data[i] = tmp._data[i];
        }

        if (tmp._data != nullptr) MemFree(tmp._data);                // 180117867
        tmp._data = nullptr;                                         // 1801178a1
        tmp._size = 0;
        tmp._capacity = 0;                                           // 1801178a5, one qword
    } else {
        // ---- int32 codepoint table -------------------------------
        int32_t n = _charToGlyph._size;                              // 1801178af
        ScalarInt(rw, n);                                            // 1801178c4

        if (ser._stream->_direction == 0) {                          // 1801178ce
            rtcoll::RtColl_ArraySetSize_18002fea0(&_charToGlyph, n);  // 1801178da
        }

        rw->SerializeIntArray(n, _charToGlyph._data, nullptr);       // 1801178ef (+0x48)
    }

    // ---- the glyph table: four separate int32 scalars per row ----
    // NOT one kArrayInt call over 4*n ints — retail emits four kScalarInt
    // calls per glyph, which is what a NAMED text encoding needs even though
    // every name here is null.
    int32_t glyphs = _glyphs._size;                                  // 1801178f2
    ScalarInt(rw, glyphs);                                           // 180117906

    if (ser._stream->_direction == 0) {                              // 180117910
        rtcoll::RtColl_ArraySetSize_180077330(&_glyphs, glyphs);    // 18011791d
    }

    Kerning* row = _glyphs._data;                                    // 180117922
    for (int32_t i = 0; i < glyphs; ++i) {                           // 180117940
        ScalarInt(rw, row->_preAdvance);
        ScalarInt(rw, row->_postKern);
        ScalarInt(rw, row->_postAdvance);
        ScalarInt(rw, row->_offsetY);
        ++row;                                                       // 18011798f
    }

    ScalarInt(rw, _baseLine);                                        // 1801179a6

    const int32_t version2 = rw->GetVersion();                       // 1801179b3

    if (version2 >= 1) {                                             // 1801179b6
        ScalarInt(rw, _ellipsisWidth);                               // 1801179c9
        ScalarInt(rw, _spaceWidth);                                  // 1801179dd
    } else if (ser._stream->_direction == 0) {                       // 1801179ec
        // The encoding's slot 0 (IsBinary) is a bool: only when it says yes
        // are the two widths recomputed. (`MOV RCX,[RCX+0x10] ; MOV RAX,[RCX] ;
        // CALL [RAX] ; TEST AL,AL`.)
        if (ser._stream->_encoding->IsBinary()) {                    // 1801179fd
            _ellipsisWidth = MeasureLiteral(*this, kEllipsisText, 3); // 180117a99
            _spaceWidth    = MeasureLiteral(*this, kSpaceText, 1);   // 180117b28
        }
    }

    // 180117b32: `??1Serializer@IO@@` (0x1800b99b0) INLINED — the reader/writer's
    // scalar deleting destructor when it is non-null — at the end of `ser`'s scope.
    return true;                                                     // 180117b3e MOV AL,1
}

}  // namespace UI

// ============================================================================
// ?ComputeLineLayout@Font@UI@@QEBAHPEBDHHHHHPEAULayout@12@@Z   0x180116c70
// ============================================================================
//   int ComputeLineLayout(const char* text, int count, int startX, int baseY,
//                         int maxWidth, int line, Layout* layout) const
//
// The parameter names come from the call sites, which pin every one of them:
// ComputeStringLayout passes its accumulated y in the `baseY` slot and its
// running line counter in `line`, and passes -1 in `maxWidth` on every wrapped
// segment (0x180117164, 0x18011720a, 0x180117360, 0x180117448) so the ellipsis
// test cannot fire; the single-line path passes ComputeStringLayout's own
// fourth parameter there instead (0x180117060).
//
// Returns the ADVANCE of the run, `penX + kern - startX`, which is why both
// exits recompute it from the saved copy of startX rather than from EBX (EBX
// is reused as the glyph-row pointer inside the loop, 0x180116d4f).
//
// THE ELLIPSIS ARM RECURSES INTO THIS SAME FUNCTION over the literal "...",
// and DISCARDS the recursive return value (0x180116e2f) — the value it hands
// back is recomputed from the pen at 0x180116e34. Not an oversight to
// "simplify": the recursive call is there for its SIDE EFFECT on `layout`.
namespace UI {

int32_t Font::ComputeLineLayout(const char* text, int32_t count, int32_t startX,
                                int32_t baseY, int32_t maxWidth, int32_t line,
                                Layout* layout) const {
    if (count == 0) return 0;                                        // 180116c8f

    int32_t       penX       = startX;   // EDI
    int32_t       kern       = 0;        // ESI
    const int32_t startSaved = startX;   // [RSP+0x44]
    int32_t       byteIdx    = 0;        // R8D

    if (count > 0) {                                                 // 180116cb8
        const char* const end = text + count;                        // [RSP+0x50]
        for (;;) {
            int32_t cp = 0;
            byteIdx += DecodeUtf8(text + byteIdx, end, &cp);         // 180116ceb

            if (cp >= 0 && cp < _charToGlyph._size) {
                const int32_t glyph = _charToGlyph._data[cp];
                if (glyph != kNoGlyph && glyph != -1 && glyph >= 0) {  // 180116d1e..d35
                    const SpriteSheet* sheet = _spriteSheet.Get();
                    // NOT RETAIL: retail bounds the glyph by the sheet only; `_glyphs` comes from
                    // the font resource and may be shorter (heap out-of-bounds read).
                    if (glyph < sheet->m_sprites._size && glyph < _glyphs._size) {
                        const SpriteInfo* sprites = sheet->m_sprites._data;
                        const Kerning& e = _glyphs._data[glyph];     // 180116d52
                        // The sprite record's +0x20 (SpriteInfo::height).
                        const int32_t spriteTop = sprites[glyph].height;

                        if (maxWidth > 0) {                          // 180116d6c
                            const int32_t reach =
                                _ellipsisWidth -
                                startSaved + e._preAdvance +
                                kern + penX;                         // 180116d6e..d79
                            if (reach >= maxWidth) {                 // 180116d7d
                                // 180116df0 — elide: lay out "..." from here.
                                ComputeLineLayout(kEllipsisText, 3,
                                                  kern + penX, baseY, -1,
                                                  line, layout);
                                return _ellipsisWidth -
                                       startSaved + kern + penX;     // 180116e34
                            }
                        }

                        penX += e._preAdvance + kern;                // 180116d8b

                        if (layout != nullptr) {                     // 180116d8d
                            RtUI::FontLayoutEntry entry;             // [RSP+0x58]
                            // GLYPH index, not the codepoint: EDX still holds
                            // the MOVSXD from the codepoint table (0x180116d1a)
                            // and nothing rewrites it before this store. This
                            // is what SpriteContext::AddFont draws by, and the
                            // sheet has only as many sprites as there are
                            // glyphs (storing `cp` here drew "n" for "New").
                            entry._glyph = glyph;                    // 180116d99
                            entry._x     = penX;                     // 180116db1
                            entry._y     = baseY - e._offsetY - spriteTop;  // 180116da0..daf
                            entry._line  = line;                     // 180116dc0

                            rtcoll::RtColl_ArraySetSizeFill_180076d10(
                                &layout->_entries,
                                layout->_entries._size + 1,
                                &entry);                             // 180116dc4
                        }

                        penX += e._postAdvance;                      // 180116dce
                        kern  = e._postKern;                         // 180116dd1
                    }
                }
            }

            if (byteIdx >= count) break;                             // 180116dd4
        }
    }

    return penX + (kern - startSaved);                               // 180116e45
}

}  // namespace UI

// ============================================================================
// ?ComputeCharacterPosition@Font@UI@@QEBAHAEBVString@System@@H@Z  0x180116ee0
// ============================================================================
//   int ComputeCharacterPosition(const String& s, int byteCount) const
//
// The pen x after `byteCount` BYTES of `s` — the caret position for an edit
// field. Identical accumulation to ComputeLineLayout's, minus the layout
// emission, the ellipsis and the start offset.
//
// The String's data pointer is read ONCE, before the loop (`MOV R15,[RDX]` at
// 0x180116eea), unlike ComputeStringLayout which re-reads it every iteration.
namespace UI {

int32_t Font::ComputeCharacterPosition(const System::String& text, int32_t count) const {
    const char* const data = text._data;                              // 180116eea

    if (count == 0) return 0;                                        // 180116ef8

    int32_t total   = 0;   // EDI
    int32_t kern    = 0;   // R14D
    int32_t byteIdx = 0;   // ESI

    if (count > 0) {                                                 // 180116f18
        const char* const end = data + count;                        // R12
        do {
            int32_t cp = 0;
            byteIdx += DecodeUtf8(data + byteIdx, end, &cp);         // 180116f4b

            if (cp >= 0 && cp < _charToGlyph._size) {
                const int32_t glyph = _charToGlyph._data[cp];
                if (glyph != kNoGlyph && glyph != -1 && glyph >= 0) {  // 180116f6b..f79
                    const SpriteSheet* sheet = _spriteSheet.Get();
                    // NOT RETAIL: retail bounds the glyph by the sheet only; `_glyphs` comes from
                    // the font resource and may be shorter (heap out-of-bounds read).
                    if (glyph < sheet->m_sprites._size && glyph < _glyphs._size) {
                        const Kerning& e = _glyphs._data[glyph];     // 180116f87
                        total += e._postAdvance + e._preAdvance + kern;
                        kern = e._postKern;
                    }
                }
            }
        } while (byteIdx < count);                                   // 180116f9d
    }

    return kern + total;                                             // 180116fab
}

}  // namespace UI

// ============================================================================
// ?ComputeStringLayout@Font@UI@@   0x180116fd0
// ============================================================================
//   void ComputeStringLayout(const String& s, bool multiline, int wrapWidth,
//                            int maxWidth, Justify justify,
//                            int& outWidth, int& outHeight,
//                            Layout* layout) const
//
// THE MANGLED NAME'S `3` IS `AEAH`, NOT A THIRD `H`. MSVC's argument
// back-reference table records every encoding longer than one character, and
// `_N` (bool) counts: the table here is 0 = `AEBVString@System@@`, 1 = `_N`,
// 2 = `W4Justify@12@`, 3 = `AEAH`. Reading `3` as anything else would shift
// every later parameter. Confirmed against the bytes: [RSP+0xe0] and
// [RSP+0xe8] are both DEREFERENCED as `int*` (0x180117034, 0x180117037), so
// they are two int references, and the Layout* is the ninth slot at [RSP+0xf0].
//
// NOT MULTILINE is one straight call into ComputeLineLayout with maxWidth in
// the ellipsis slot, and it RETURNS EARLY (0x18011708b) — so it never reaches
// the justify pass and never appends a line width. Only the multiline path
// fills `Layout::_lineWidths`, which is exactly why the justify pass is safe
// to index by `_line`.
//
// THREE STACK SLOTS ARE REUSED AS SCRATCH after their parameter is consumed —
// [RSP+0xc0] (the bool's home) holds the current line's start byte AND, for
// one instruction each time, the width just measured; [RSP+0xe0] and
// [RSP+0xf0] (the two out-parameter homes) hold the pen x and the carried
// kern once R15/RDI have the real pointers. They are separate locals here.
namespace UI {

void Font::ComputeStringLayout(const System::String& text, bool multiline,
                               int32_t wrapWidth, int32_t maxWidth, Justify justify,
                               int32_t& outWidthRef, int32_t& outHeightRef,
                               Layout* layout) const {
    // Retail keeps the String's data pointer's HOME and re-reads it every
    // iteration (0x1801170f0); `strRef` is that home. The two out-references
    // are written through pointers exactly as retail does.
    const char* const* strRef = &text._data;
    int32_t* outWidth  = &outWidthRef;
    int32_t* outHeight = &outHeightRef;

    if (layout != nullptr) {                                         // 180117003
        rtcoll::RtColl_ArraySetSize_18002fea0(&layout->_lineWidths, 0);   // 18011700b
        rtcoll::RtColl_ArraySetSize_180076a40(&layout->_entries);          // 180117014
        layout->_width  = 0;                                         // 180117021, one qword
        layout->_height = 0;
    }

    *outWidth  = 0;                                                  // 180117034
    *outHeight = 0;                                                  // 180117037

    if (!multiline) {                                                // 18011703d
        const char* data = *strRef;                                  // 18011703f
        const int32_t len = (data != nullptr) ? RtSystem::StringLength(data) : 0;
        *outWidth = ComputeLineLayout(data, len, /*startX*/ 0,
                                      /*baseY*/ 0, maxWidth,
                                      /*line*/ 0, layout);           // 18011706e
        *outHeight = _lineHeight;  // 180117076
        return;
    }

    // ---- multiline ------------------------------------------------
    const char* data0 = *strRef;                                     // 18011708c
    const int32_t len = (data0 != nullptr) ? RtSystem::StringLength(data0) : 0;
    const int32_t lineHeight = _lineHeight;

    int32_t pendingKern = 0;    // [RSP+0xf0]
    int32_t lineStart   = 0;    // [RSP+0xc0]
    int32_t lastSpace   = -1;   // [RSP+0x40]
    int32_t curX        = 0;    // [RSP+0xe0]
    int32_t tabX        = 0;    // EBP
    int32_t y           = 0;    // R14D
    int32_t lineIndex   = 0;    // R13D
    int32_t byteIdx     = 0;    // ESI

    auto appendLineWidth = [&](int32_t w) {
        if (layout == nullptr) return;                               // 180117188
        int32_t v = w;
        rtcoll::RtColl_ArraySetSizeFill_180023830(
            &layout->_lineWidths, layout->_lineWidths._size + 1, &v);
    };

    while (byteIdx < len) {                                          // 1801170e1 / 18011740d
        const char* data = *strRef;                                  // 1801170f0, RE-READ
        int32_t cp = 0;
        const int32_t used = DecodeUtf8(data + byteIdx, data + len, &cp);  // 18011710f

        if (cp < 0) {                                                // 180117120
            byteIdx += used;                                         // 180117122
            continue;
        }

        if (cp == 0x0a) {                                            // 18011712d  '\n'
            const int32_t w = ComputeLineLayout(*strRef + lineStart, byteIdx - lineStart, tabX,
                y, -1, lineIndex, layout);                           // 180117171
            if (w > *outWidth) *outWidth = w;                        // 18011717d
            appendLineWidth(w);                                      // 18011719b

            y += lineHeight;                                         // 1801171a0
            pendingKern = 0;                                         // 1801171ac
            curX        = 0;                                         // 1801171b6
            lineStart   = byteIdx + 1;                               // 1801171bd
            tabX        = 0;                                         // 1801171c5
            ++lineIndex;                                             // 1801171c7
        } else if (cp == 0x09) {                                     // 1801171d3  '\t'
            const int32_t w = ComputeLineLayout(*strRef + lineStart, byteIdx - lineStart, tabX,
                y, -1, lineIndex, layout);                           // 180117217
            if (w > *outWidth) *outWidth = w;                        // 180117223
            appendLineWidth(w);                                      // 180117241

            // The next 32-pixel tab stop. Retail spells the round-down with
            // `CDQ ; AND EDX,0x1f ; LEA EBP,[RDX+RAX] ; AND EBP,~0x1f`, which
            // is round-TOWARD-ZERO for a negative total, not floor.
            const int32_t raw = curX + 0x20 + pendingKern;           // 180117246..18011725f
            lineStart   = byteIdx + 1;                               // 180117257
            pendingKern = 0;                                         // 180117266
            const int32_t bias = (raw < 0) ? 0x1f : 0;               // 18011726d CDQ/AND
            tabX = (bias + raw) & ~0x1f;                             // 180117271

            if (tabX > wrapWidth) {                                  // 180117277
                y += lineHeight;                                     // 180117280
                curX = 0;                                            // 180117288
                ++lineIndex;                                         // 18011728f
                tabX = 0;                                            // 180117292
            } else {
                curX = tabX;                                         // 180117299
            }
        } else {
            if (cp == 0x20) lastSpace = byteIdx;                     // 1801172c2 CMOVZ
        }

        // ---- 1801172c9: the glyph accumulate, reached by ALL THREE arms.
        // A newline or a tab runs it too; they simply have no glyph, so the
        // codepoint table lookup normally falls through. Faithful.
        if (cp < _charToGlyph._size) {   // 1801172d1
            const int32_t glyph = _charToGlyph._data[cp];
            if (glyph != kNoGlyph && glyph != -1 && glyph >= 0) {    // 1801172e3..fa
                const SpriteSheet* sheet = _spriteSheet.Get();
                // NOT RETAIL: retail bounds the glyph by the sheet only; `_glyphs` comes from
                // the font resource and may be shorter (heap out-of-bounds read).
                if (glyph < sheet->m_sprites._size && glyph < _glyphs._size) {
                    const Kerning& e = _glyphs._data[glyph];         // 180117310
                    const int32_t advance =
                        e._postAdvance + e._preAdvance + pendingKern;
                    const int32_t nextKern = e._postKern;

                    pendingKern = nextKern;                          // 180117323
                    curX       += advance;                           // 18011732a

                    if (curX + nextKern >= wrapWidth) {              // 180117339
                        // ---- wrap 180117346 ----------------------
                        int32_t w;
                        if (lastSpace >= 0) {                        // 180117379
                            w = ComputeLineLayout(*strRef + lineStart, lastSpace - lineStart,
                                tabX, y, -1, lineIndex, layout);     // 180117381
                            byteIdx   = lastSpace + 1;               // 180117386
                            lineStart = byteIdx;                     // 18011738b
                        } else {
                            w = ComputeLineLayout(*strRef + lineStart, byteIdx - lineStart - 1,
                                tabX, y, -1, lineIndex, layout);     // 18011739d
                            lineStart = byteIdx - 1;                 // 1801173a8
                            byteIdx   = byteIdx - 2;                 // 1801173a5
                        }

                        if (w > *outWidth) *outWidth = w;            // 1801173b6
                        appendLineWidth(w);                          // 1801173d4

                        y += lineHeight;                             // 1801173d9
                        lastSpace   = -1;                            // 1801173de
                        pendingKern = 0;                             // 1801173e6
                        ++lineIndex;                                 // 1801173ed
                        curX        = 0;                             // 1801173f0
                        tabX        = 0;                             // 1801173f7
                    }
                }
            }
        }

        byteIdx += used;                                             // 1801173f9
    }

    // ---- the trailing partial line, 180117425 ---------------------
    if (len - lineStart > 0) {                                       // 18011742d
        const int32_t w = ComputeLineLayout(*strRef + lineStart, len - lineStart, /*startX*/ 0,
            y, -1, lineIndex, layout);                               // 180117455
        if (w > *outWidth) *outWidth = w;                            // 180117461
        appendLineWidth(w);                                          // 18011747f
        y += lineHeight;                                             // 180117484
    }

    *outHeight = y;                                                  // 180117496

    if (layout == nullptr) return;                                   // 18011749c

    // ---- justify, 18011749e --------------------------------------
    // The loop bound is RE-READ every iteration (`CMP EBX,[RDI+0x10]`), and
    // `*outWidth` is the widest line, so `d` is this line's slack.
    for (int32_t i = 0; i < layout->_entries._size; ++i) {
        RtUI::FontLayoutEntry& e = layout->_entries._data[i];        // 1801174b0
        const int32_t* widths = layout->_lineWidths._data;
        const int32_t d = *outWidth - widths[e._line];

        if (justify == 1) {                                          // 1801174c5
            e._x += d / 2;                                           // 1801174cb, trunc
        } else if (justify == 2) {                                   // 1801174dc
            e._x += d;                                               // 1801174de
        }
    }

    layout->_width  = *outWidth;                                     // 1801174f0
    layout->_height = *outHeight;                                    // 1801174f5
}

}  // namespace UI

// ============================================================================
// ENDGAME B.3.9 TI-UI (B39 D6): UI::Font's own type record, built at CRT init
// (head-insert into Object::BaseTypeInfo::_firstTypeInfo). Literals copied from
// the retired minted row ?_typeInfo@Font@UI@@ (0x1801fab68): name "Font", parent
// "Object::External" (retail ?_ParentTypeName@Font@UI@@), no file extension; factories:
// the class's own.
// ============================================================================
DEFINE_TYPE(UI::Font, "Object::External", nullptr)
