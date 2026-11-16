#pragma once
// UI::TextElement -- offsets used by Wave 7 lane E.
//
// Evidence (Runtime-x64.dll addresses):
//   SetParameter(i,s)  0x18011e9b0  guards -1<i<*(int*)(this+0xd0); writes
//                                    System::String::operator=(
//                                      *(String**)(this+0xc8) + i*8, s)
//                                    i.e. an Array<System::String> whose DATA
//                                    pointer is at this+0xc8 and whose element
//                                    COUNT is at this+0xd0 -- matching
//                                    Collection::Array<T> {data,size,cap} laid
//                                    out from base +0xc8 (data +0xc8, size
//                                    +0xd0, cap +0xd4). Then dirty-chain walk,
//                                    then CacheText(this).
//   SetText(String)    0x180042070  this+0xd8 = param[0]; this+0xe0 = param[1]
//                                    (the raw UI::String value, 16 bytes);
//                                    dirty-chain walk; CacheText(this).
//   CacheText (private, but exported: ?CacheText@TextElement@UI@@AEAAXXZ,
//              0x18011ec10) reads/writes this+0xd0 (param count), converts
//              the UI::String at this+0xd8 to a System::String, and manages a
//              cached-formatted-text pointer at this+0xf8. Ported as
//              TextElement::CacheText (textelement_cachetext.cpp).
//   SetParameterCount (private, exported: 0x18011e8e0) likewise ported
//              there.
//
// this+0xc0..0xc8 (8 bytes) is read by neither SetParameter nor SetText
// (LabelElement.h's old table guessed "font ptr" for it; the note below
// confirms it).
//
// *** +0xC0 IS THE FONT, AND THE OLD GUESS WAS RIGHT. 2026-09-12, elem_B. ***
// `?ComputeSize@TextElement@UI@@UEAAXXZ` (0x18011F060) loads +0xC0 into RCX and
// calls `?ComputeStringLayout@Font@UI@@` on it; `?Draw@` (0x18011F280) loads
// the same qword, reads its +0x50/+0x54 line metrics and passes it to
// `?AddFont@SpriteContext@UI@@` as an ExternalPtr<UI::Font>. The rest of the
// object, from the same two bodies and from ~TextElement (0x18011EDA0):
//
//   +0xE8  _drawShadow  bool   Draw's first, black, +1/+1 pass
//   +0xE9  _wrap        bool   ComputeStringLayout / AddFont argument 3
//   +0xEC  _wordWrapSize int32  argument 4 (the width a wrapped text breaks at; retail's Mod.rsc: 192)
//   +0xF0  _maxWidth    int32  argument 5, and ComputeSize's width clamp when >0
//   +0xF4  _justify     UI::Font::Justify
//
// sizeof(TextElement) == 0x100, from the vector deleting destructor's
// `MOV EDX,0x100` at 0x18004230F. ~TextElement releases +0xF8 and then destroys
// the +0xC8 parameter array through 0x180010AB0 — the element-destroying
// `~Array` variant, which is the third independent witness that +0xC8 really
// is an Array<System::String> and not the single owning pointer a
// NumberElement has at its own +0xD0. The two-class contradiction the note
// below warns about is therefore CLOSED: the two classes are unrelated and
// both readings were right. See src/engine/ui/textelement.cpp.

#include <type_traits>
#include <cstdint>
#include <cstddef>
#include "Element.h"
#include "String.h"
#include "../object/Internal.h"   // Object::detail::InternalMemFree

namespace UI { namespace TextElementOffsets {

// *** kParamCount CONTRADICTED BY MEASUREMENT on a NumberElement. Wave 9 lane M. ***
// Probing UI::NumberElement::SetValueInt on the tutorial routes shows the eight
// bytes at +0xD0 holding an OWNING, REFCOUNTED System::String data pointer (a
// heap address with a {length, refcount} header at [-8], payload = the formatted
// number), not an int32 size followed by an int32 capacity. Both readings cannot
// be right for the same offset. This header's offsets are the ones derived from
// call sites (0x18011e9b0) and are LEFT AS THEY ARE -- a NumberElement is not
// necessarily laid out like the TextElement this table was read from, and
// nothing here has been re-derived. NumberElement.h carries the same note and
// the evidence is in docs/history/runtime_port/WAVE9_LANE_M.md Finding 1. The banner
// above records why both readings are right.
constexpr int kParamArrayData = 0xC8;  // Collection::Array<System::String>.data -- TextElement's own field
constexpr int kParamCount     = 0xD0;  // Collection::Array<System::String>.size (int32_t) -- TextElement's own field
constexpr int kParamCapacity  = 0xD4;  // Collection::Array<System::String>.capacity (int32_t) -- inferred from the Array<T> shape, not read by either ported function
constexpr int kTextString     = 0xD8;  // UI::String, 16 bytes (0xD8, 0xE0) -- TextElement's own field
constexpr int kCachedText     = 0xF8;  // refcounted System::String data ptr, owned by CacheText -- TextElement's own field
constexpr int kDirtyFlags     = offsetof(UI::Element, _flags);  // 0x34, confirmed by Element.h (lane A)
constexpr int kParent         = offsetof(UI::Element, _parent); // 0x98, confirmed by Element.h (lane A)
constexpr uint32_t kDirtyBit  = UI::kElementFlagNeedsSizeCalc;

} } // namespace UI::TextElementOffsets

// ############################################################################
// ENDGAME B.3 step 6 (block C3) — `UI::TextElement`, the REAL C++ class
// ############################################################################
//
// PARENT `UI::Element` — READ (B36_SLOTS.md section 3), RECORDED, NOT USED:
//     ?_TypeName@TextElement@UI@@CAPEBDXZ       @0x180041eb0 -> "TextElement"
//     ?_ParentTypeName@TextElement@UI@@CAPEBDXZ @0x180041ec0 -> "Element"
//
// `??_7TextElement@UI@@6B@` @ 0x180156b80, TWELVE slots:
//   [0] 0x1800422f0 vector deleting dtor
//   [1] 0x180041ef0 TypeName · [2] 0x180041ed0 Type · [3] Element::Persist
//   [4] 0x18011f060 ComputeSize · [5] 0x18011f120 ComputeLayout
//   [6] 0x18011f280 Draw · [7..11] Element's, inherited
//
// SIZE 0x100 (`MOV EDX,0x100` at 0x18004230f). THE LAYOUT, every field a store
// of `??0TextElement@UI@@QEAA@XZ` (0x18011eb80, read as DISASSEMBLY by block C3):
//     +0xc0 qword 0      _font        ExternalPtr<UI::Font> (ComputeSize/Draw)
//     +0xc8 qword 0      _params      Array<System::String> {data +0xc8,
//     +0xd0 qword 0                   size +0xd0, capacity +0xd4} (SetParameter)
//     +0xd8 qword 0      _text        UI::String {table +0xd8,
//     +0xe0 dword -1                  index +0xe0}; +0xe4 NOT written
//     +0xe8 WORD  0      _drawShadow +0xe8, _wrap +0xe9 (one store)
//     +0xea ..+0xeb      NOT written
//     +0xec dword 100    _wordWrapSize
//     +0xf0 dword -1     _maxWidth
//     +0xf4 dword 0      _justify     UI::Font::Justify
//     +0xf8 qword 0      _cachedText  System::String, owned by CacheText
// then `CALL 0x18011ec10` (CacheText). The names are the ones this header's
// banner and textelement.cpp already carried; `TextElementOffsets` above is now
// asserted against these fields.
//
// (Until ENDGAME B.3.9:) ABSTRACT in C++ (slot 0 `= 0`); no real ctor: live TextElements are built by
// the `??0TextElement` door (element_bodies_b5.cpp) and wear the MINTED table,
// so every door delegates with a QUALIFIED call.
//
// ENDGAME B.3 step 9 (block UI-A, B39_RULES D1/D2): CONCRETE. The real public
// ctor is retail's `??0TextElement@UI@@QEAA@XZ` (0x18011eb80) minus its vptr stores;
// slot 0 is the class's own vector deleting dtor; ~TextElement is public and
// virtual. The factory pair is the class's own; the minted
// `??_7TextElement@UI@@6B@`, its RT_DELDTOR row, the factory-interpreter row and
// the doors are retired.
// ############################################################################
namespace UI {

class Font;
class Dialog;

class TextElement : public Element {
public:
    // ??0TextElement@UI@@QEAA@XZ 0x18011eb80 — the REAL constructor: CALL
    // ??0Element (Element()), then the stores the banner lists at retail's
    // widths and in retail's order (`_params` is Array()'s zero triple, `_text`
    // UI::String's own default ctor {nullptr, -1}, `_cachedText` String()'s
    // null; +0xe4 and +0xea..+0xeb NOT written), all in the init list so the
    // two typed members keep their place in the sequence (B4 X7-RT), then
    // `CALL 0x18011ec10` — CacheText.
    TextElement()
        : Element(), _font(nullptr), _params(), _text(),
          _drawShadow(0), _wrap(0), _wordWrapSize(0x64), _maxWidth(-1),
          _justify(0), _cachedText() {
        CacheText();                                             // 18011ebf9
    }

    // ---- the twelve slots (own ones declared) ------------------------------
    // [0] 0x1800422f0 — MSVC's VECTOR deleting destructor, both arms exactly as
    // the retired RT_DELDTOR row ran them (`MOV EDX,0x100`, the element size).
    // `??1TextElement@UI@@UEAA@XZ` (0x18011eda0). VIRTUAL (B3X W1)
    // (textelement.cpp): releases the cached text at +0xf8, destroys the
    // parameter Strings through retail's element-destroying Array dtor; the
    // compiler chains ~Element.
    ~TextElement() override;
    // ENDGAME B.3.9 TI-UI: ONE reflection block (B39 D6); `_typeInfo` is the
    // class's own record (DEFINE_TYPE in the class's TU), no longer minted.
    // retail: TypeName 0x180041ef0, Type 0x180041ed0, TypeId 0x180041ee0, _TypeName 0x180041eb0
    //         _ParentTypeName 0x180041ec0, CreateNewObject 0x180041fe0, CreateInPlaceObject 0x180042060
    DECLARE_TYPE(TextElement)
public:
    void ComputeSize() override;                                 // [4] 0x18011f060
    void ComputeLayout(int32_t x, int32_t y,
                       int32_t w, int32_t h) override;           // [5] 0x18011f120
    void Draw(SpriteContext& context, uint8_t alpha) const override; // [6] 0x18011f280

    // ---- ported non-virtual members (textelement_cachetext.cpp) -------------
    // Both are `AEAA` (PRIVATE) in retail; PUBLIC here because CacheText is called
    // from outside the class (comboelement.cpp, widgets_input.cpp,
    // element_bodies_b5.cpp); access is not part of the GCC ABI.
    void CacheText();                                            // 0x18011ec10
    void SetParameterCount();                                    // 0x18011e8e0

    // ---- exported; DEFINED in widgets_layout.cpp (fragment pass) ----------
    // ?SetParameter@TextElement@UI@@QEAAXHAEBVString@System@@@Z  0x18011e9b0
    //   (`AEBV` — a const reference.)
    void SetParameter(int32_t index, const System::String& value);
    // ?SetText@TextElement@UI@@QEAAXVString@2@@Z  0x180042070. `VString@2@` is
    //   the 0x10-byte UI::String BY VALUE, which MSVC passes by HIDDEN POINTER
    //   (the RULE B6-11 shape): the retail register holds the ADDRESS of the
    //   caller's copy, so the member takes the pointer, in retail order.
    void SetText(const String* text);

    // ?GetText@TextElement@UI@@QEBA?AVString@2@XZ  0x1800420b0 — RETSLOT in
    // retail (`MOV RAX,[RCX+0xd8] / MOV [RDX],RAX / MOV RAX,[RCX+0xe0] /
    // MOV [RDX+8],RAX / MOV RAX,RDX`): the whole body, so inline and by value.
    String GetText() const { return _text; }

    TextElement(const TextElement& src);                         // 0x1800420d0  not ported
    TextElement& operator=(const TextElement& src);              // 0x1800421c0  not ported
    static void* operator new(std::size_t bytes);                // 0x180041f00  not ported
    static void* operator new(std::size_t bytes, void* place);   // 0x180041f70  not ported
    static void  operator delete(void* p) { ::Object::detail::InternalMemFree(p); }                       // 0x180041f80

private:

public:
    // ---- the 0x40 bytes TextElement adds ------------------------------------
    // B4 X7-RT (`behaviour:`): `_params` and `_cachedText` are typed members.
    // Retail's ~TextElement 0x18011eda0 releases +0xf8 first, then calls
    // ~Array<String> 0x180010ab0 on +0xc8, then ~Element: the compiler's
    // reverse-declaration member destruction after the (empty) body. `_text`
    // (UI::String, a table pointer and an index) has no destructor.
    Font*      _font;          // +0xc0  ExternalPtr<UI::Font>
    Collection::Array<System::String> _params;   // +0xc8  {data, size +0xd0, capacity +0xd4}
    String     _text;          // +0xd8  UI::String
    uint8_t    _drawShadow;    // +0xe8
    uint8_t    _wrap;          // +0xe9
    uint8_t    _padEA[2];      // +0xea  never written
    int32_t    _wordWrapSize;  // +0xec  ctor 100
    int32_t    _maxWidth;      // +0xf0  ctor -1
    int32_t    _justify;       // +0xf4  UI::Font::Justify
    System::String _cachedText;   // +0xf8  owned by CacheText
};

static_assert(sizeof(TextElement) == 0x100,
              "UI::TextElement is 0x100 (MOV EDX,0x100 at 0x18004230f)");
static_assert(offsetof(TextElement, _padEA) == 0xEA, "TextElement::_padEA");
static_assert(alignof(TextElement) == 8, "UI::TextElement aligns to 8");
static_assert(std::has_virtual_destructor<TextElement>::value, "B3X W1: the normal-virtual cutover");
static_assert(!std::is_abstract<TextElement>::value, "B39 D1: concrete");
static_assert(offsetof(TextElement, _font) == sizeof(Element),
              "TextElement's own fields begin at the end of Element");
static_assert(offsetof(TextElement, _font)        == 0xc0, "");
static_assert(offsetof(TextElement, _params)      == TextElementOffsets::kParamArrayData, "");
static_assert(offsetof(TextElement, _params) + offsetof(Collection::Array<System::String>, _size)
              == TextElementOffsets::kParamCount, "");
static_assert(offsetof(TextElement, _params) + offsetof(Collection::Array<System::String>, _capacity)
              == TextElementOffsets::kParamCapacity, "");
static_assert(offsetof(TextElement, _text)        == TextElementOffsets::kTextString, "");
static_assert(offsetof(TextElement, _drawShadow)  == 0xe8, "");
static_assert(offsetof(TextElement, _wrap)        == 0xe9, "");
static_assert(offsetof(TextElement, _wordWrapSize) == 0xec, "");
static_assert(offsetof(TextElement, _maxWidth)    == 0xf0, "");
static_assert(offsetof(TextElement, _justify)     == 0xf4, "");
static_assert(offsetof(TextElement, _cachedText)  == TextElementOffsets::kCachedText, "");

}  // namespace UI
