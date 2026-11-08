#pragma once
// UI::DocumentTextElement -- offsets used by Wave 7 lane E.
//
// Evidence (Runtime-x64.dll addresses):
//   SetText(UI::String)        0x18003bfd0  this+0xc0=param[0]; this+0xc8=param[1]
//                                (raw UI::String value, 16 bytes); dirty-chain walk.
//   SetText(System::String&)   0x18003c010  resets this+0xc0/0xc8 to {0,-1}
//                                (a String-index reset, same shape as
//                                UI::Sprite's {ptr,-1}); manually refcounts a
//                                SEPARATE plain System::String at this+0x168
//                                (release old, take new, refcount at ptr-4);
//                                dirty-chain walk.
//   GetLink(index)             0x18003c0c0  this+0x158 = link array data ptr,
//                                stride 0x18 (0x18-byte entries, String at
//                                entry+0x00); returns entry with the ref
//                                bumped, or an empty String if the slot is
//                                null. No bounds check observed on `index`
//                                (matches SpriteSheet::GetSpriteNameFromIndex's
//                                documented "no bounds checking" pattern).

#include <cstdint>
#include "Element.h"

namespace UI { namespace DocumentTextElementOffsets {

constexpr int kUiStringValue = 0xC0;  // UI::String, 16 bytes (0xC0, 0xC8) -- the by-value overload's storage, DocumentTextElement's own field
constexpr int kContentString = 0x168; // refcounted System::String* (plain, not UI::String) -- DocumentTextElement's own field
constexpr int kLinkArrayData = 0x158; // pointer to an array of 0x18-byte entries; entry+0x00 = System::String -- DocumentTextElement's own field
constexpr int kLinkEntryStride = 0x18;
constexpr int kDirtyFlags    = offsetof(UI::Element, _flags);  // 0x34, confirmed by Element.h (lane A)
constexpr int kParent        = offsetof(UI::Element, _parent); // 0x98, confirmed by Element.h (lane A)
constexpr uint32_t kDirtyBit = UI::kElementFlagNeedsSizeCalc;

} } // namespace UI::DocumentTextElementOffsets

// ============================================================================
// The PRIVATE TEXT PIPELINE's fields, added for src/engine/ui/documenttext.cpp.
// ============================================================================
// Every constant below is read straight out of one of these seven retail bodies
// and nothing is inferred from a name:
//   GetNextItem        0x1800cdf40   GenerateItems      0x1800cf060
//   LayoutRow          0x1800cdbc0   FindNextWhiteSpace 0x1800cefd0
//   BeginLink          0x1800cdeb0   EndLink            0x1800cde00
//   DocumentTextDescription::CreateInstance 0x1800cf520
//
// The parser is a `^`-escape machine over a flat System::String. It appends
// fixed-size DocumentItems to the item array, flushes a row when the pen passes
// the right edge, and LayoutRow then gives every item of the finished row its x
// and y. The escapes it understands, in the order retail tests them:
//   ^n ^p ^t ^s ^f<d> ^c<d> ^j{l,r,c} ^v{t,c,b} ^m{b,c,e,l,r} ^l{b,e,i,o}
//   ^a{b,e} ^d<name> ^i<d><name>

namespace UI { namespace DocumentTextElementOffsets {

// ---- geometry the element publishes back to UI::Element --------------------
constexpr int kElementHeight   = 0x64;  // GenerateItems' last store (0x1800cf2fb)

// ---- description-supplied resources ---------------------------------------
constexpr int kSpriteSheet     = 0xD0;  // UI::SpriteSheet* (CreateInstance)
constexpr int kCaptureRefSheet = 0xD8;  // {SpriteSheet*, int} pair, 16 bytes
constexpr int kCaptureRefIndex = 0xE0;
constexpr int kFrameTable      = 0xE8;  // Array<{SpriteSheet*, int}>, stride 0x10
constexpr int kInt0F8          = 0xF8;
constexpr int kInt0FC          = 0xFC;
constexpr int kInt100          = 0x100;
constexpr int kTabWidth        = 0x104; // ^t advance and the ^lb / ^le indent step
constexpr int kWrapWidth       = 0x108; // the default right edge
constexpr int kFontArray       = 0x110; // ptr to Font*[] ; Font +0x50 = line
                                        // height, +0x54 = baseline
constexpr int kSheetArray      = 0x120; // ptr to SpriteSheet*[] (^i<digit>)
constexpr int kColorArray      = 0x130; // ptr to int[] (^c<digit> picks one)
constexpr int kResourceList    = 0x140; // the object list ^d<name> searches;
                                        // +0x20 data, +0x28 count

// ---- the generated item array ---------------------------------------------
constexpr int kItemArrayData   = 0x148;
constexpr int kItemCount       = 0x150;
constexpr int kItemCapacity    = 0x154;
constexpr int kItemStride      = 0x24;

// ---- the parse cursor ------------------------------------------------------
constexpr int kTextLength      = 0x170; // cached length of the +0x168 string
constexpr int kTextPos         = 0x174;

// ---- the pen ---------------------------------------------------------------
constexpr int kPenX            = 0x178; // row origin / left margin (^ml sets it)
constexpr int kHangIndent      = 0x17C; // folded into kPenX at the next row flush
constexpr int kRightEdge       = 0x180; // ^mr sets it; defaults to kWrapWidth
constexpr int kInt184          = 0x184; // saved and restored across ^lo only
constexpr int kRowPenX         = 0x188; // running x inside the row being built
constexpr int kPenY            = 0x18C;
constexpr int kRowFirstItem    = 0x190; // index of the first item of this row

// ---- the link being accumulated (^ab ... ^ae) ------------------------------
// One 0x18-byte Link: the String at +0x198 plus the item range [+0x1a0, +0x1a4],
// pushed into the +0x158 array by EndLink. kLinkEntryStride above is its size.
constexpr int kLinkText        = 0x198;
constexpr int kLinkFirstItem   = 0x1A0; // < 0 means "no link open"
constexpr int kLinkLastItem    = 0x1A4;
constexpr int kLinkByte1A8     = 0x1A8;

// ---- the style state machine ----------------------------------------------
constexpr int kFontIndex       = 0x1B0; // ^f<digit>, also the ^li frame slot
constexpr int kColorIndex      = 0x1B4; // ^c<digit>
constexpr int kHAlign          = 0x1B8; // ^j : 0 left, 1 right, 2 centre
constexpr int kVAlign          = 0x1BC; // ^v : 0 top, 1 centre, 2 baseline
constexpr int kColumnTop       = 0x1C0; // ^mb records, ^mc rewinds to
constexpr int kColumnMaxY      = 0x1C4; // ^mc / ^me high-water mark

// ---- UI::Font and UI::SpriteSheet fields these bodies reach into -----------
constexpr int kFontLineHeight  = 0x50;
constexpr int kFontBaseline    = 0x54;
constexpr int kSheetSpriteData = 0x20;  // SpriteSheet's sprite array, stride 0x38
constexpr int kSpriteStride    = 0x38;
constexpr int kSpriteWidth     = 0x1C;
constexpr int kSpriteHeight    = 0x20;
// UI::SpriteCapture, what ^d<name> resolves to, reports its size directly.
constexpr int kCaptureWidth    = 0x50;
constexpr int kCaptureHeight   = 0x54;

} } // namespace UI::DocumentTextElementOffsets

// ############################################################################
// ENDGAME B.3 step 6 (block C5) — `UI::DocumentTextElement`, the REAL C++ class
// ############################################################################
//
// PARENT, read (B36_SLOTS.md section 3), recorded not used (RULE B6-7):
//     ?_TypeName@DocumentTextElement@UI@@CAPEBDXZ       @0x18003be10 -> "DocumentTextElement"
//     ?_ParentTypeName@DocumentTextElement@UI@@CAPEBDXZ @0x18003be20 -> "Element"
//
// `??_7DocumentTextElement@UI@@6B@` @ 0x1801566f0, TWELVE slots; sizeof 0x1c8
// (`MOV EDX,0x1c8` in the slot-0 vector deleting dtor 0x18003c650):
//   [0] 0x18003c650 deleting dtor (RT_DELDTOR row)  [1] 0x18003be50 TypeName
//   [2] 0x18003be30 Type        [3] Element::Persist (inherited)
//   [4] 0x1800cdb60 ComputeSize [5] 0x1800cda00 ComputeLayout
//   [6] 0x1800cd7a0 Draw        [7] 0x1800cd360 Update — OVERRIDDEN
//   [8..11] Element's (inherited)
//
// THE FIELDS are the offsets above (DocumentTextElementOffsets, every one read
// out of a retail body) under names, cross-asserted below, plus what the ctor
// (0x1800cf410, factory_ui1_tables.inc kDocumentTextElement), the copy ctor
// (0x18003c140) and the dtor (0x1800cf320) — documenttextelement.cpp's doors —
// store: the ctor writes +0xc0..+0x168 and +0x198..+0x1a8 and NOTHING in
// +0x170..+0x197 or +0x1b0..+0x1c7 (GenerateItems resets those before use); the
// copy ctor copies +0x170..+0x193 and +0x1b0..+0x1c7, so +0x10c, +0x194 and
// +0x1a9..+0x1af are written by nothing.
//
// DocumentItem (0x24) and Link (0x18) are retail's NESTED value types
// (`AEAUDocumentItem@12@` in GetNextItem's mangling). `UI::DocumentItem`
// remains as an alias for the transition.
//
// ENDGAME B.3 step 9 (block UI-B, B39_RULES D1/D2): CONCRETE. A real public
// ctor (retail's 0x1800cf410 stores), its own slot 0, the factory pair as the
// class's own statics; ~DocumentTextElement holds the retired ??1 door's body.
// The minted `??_7DocumentTextElement@UI@@6B@`, its RT_DELDTOR row 0x18003c650
// and the ctor / copy / dtor / slot doors are retired.
// ############################################################################
#include <cstddef>
#include "Sprite.h"

namespace rtcoll { extern const ArraySpec kSpec_Link18; }  // 0x18003e1a0 (array.cpp)

namespace UI {

class Font;
class SpriteSheet;

class DocumentTextElement : public Element {
public:
    // ---- the nested value types -------------------------------------------
    // The unit GetNextItem produces, GenerateItems appends and LayoutRow
    // positions. The stride is fixed by LayoutRow's `LEA RDX,[0x3 + RBX*0x8] ;
    // ADD RDX,RBX` addressing (9 dwords) and by the 0x24 step at 0x1800cdd81.
    struct DocumentItem {
        int32_t x;          // +0x00  written only by LayoutRow
        int32_t y;          // +0x04  written only by LayoutRow
        int32_t width;      // +0x08
        int32_t height;     // +0x0c
        int32_t first;      // +0x10  text: first char; sprite: sprite index
        int32_t last;       // +0x14  text: last char, inclusive
        int32_t color;      // +0x18  resolved from _colors at emit time
        int32_t index;      // +0x1c  text: font slot; sprite: sheet slot
        uint8_t kind;       // +0x20  0 text, 1 sprite, 2 capture, 3 blank/tab
        uint8_t underline;     // +0x21  Update's underline: 0 none, 1 full, 2 trimmed
        uint8_t pad22[2];   // +0x22
    };
    // One clickable span. Its compiler-generated operator= (0x18003c760,
    // unexported) is ported in documenttext.cpp.
    struct Link {
        // B4 Y RT-UI: typed. Array elements are built / copied / destroyed
        // only by kSpec_Link18's arms (raw `text._data`); `_openLink` and
        // EndLink's local are the C++ objects whose ~String runs.
        System::String text; // +0x00  the target
        int32_t firstItem;  // +0x08  < 0 means "no link open"
        int32_t lastItem;   // +0x0c  one past the last item
        uint8_t armed;      // +0x10  pressed on this link
        uint8_t pad11[7];   // +0x11
        Link& operator=(const Link& src);                         // 0x18003c760
    };

    // ??0DocumentTextElement@UI@@QEAA@XZ 0x1800cf410 -- the REAL ctor:
    // `CALL ??0Element`, then +0xc0..+0x168 (the UI::String and UI::Sprite
    // members' own ctors write {0, -1}) and +0x198..+0x1a8; NOTHING in
    // +0x170..+0x197 or +0x1b0..+0x1c7 (GenerateItems resets those).
    DocumentTextElement() : Element() {
        _spriteSheet        = nullptr;
        _linkColor          = 0xffeeee00u;
        _linkHoverColor     = 0xffffff00u;
        _linkPressedColor   = 0xffcccc00u;
        _tabWidth           = 4;
        _wrapWidth          = 0x12c;
        // B4 Y RT-UI: the six Array headers (+0xe8, +0x110, +0x120, +0x130,
        // +0x148, +0x158), `_sourceText` (+0x168) and `_openLink.text`
        // (+0x198) are zeroed by their own Array() / String() as members.
        _resourceList       = nullptr;
        _openLink.firstItem = -1;                // one QWORD -1 store with lastItem
        _openLink.lastItem  = -1;
        _openLink.armed     = 0;
    }

    // ---- the twelve slots, in retail order --------------------------------
    // [0] 0x18003c650 -- MSVC's VECTOR deleting destructor (retail; here the
    // virtual destructor, B3X W1) (`MOV EDX,0x1c8` = the element size).
    // ??1 0x1800cf320 -- VIRTUAL (B3X W1); retail's body (documenttextelement.cpp),
    // the compiler chains ~Element (retail's tail JMP).
    ~DocumentTextElement() override;
    // ENDGAME B.3.9 TI-UI: ONE reflection block (B39 D6); `_typeInfo` is the
    // class's own record (DEFINE_TYPE in the class's TU), no longer minted.
    // retail: TypeName 0x18003be50, Type 0x18003be30, _TypeName 0x18003be10, _ParentTypeName 0x18003be20
    //         CreateNewObject 0x18003bf40, CreateInPlaceObject 0x18003bfc0
    DECLARE_TYPE(DocumentTextElement)
public:
    void ComputeSize() override;                                  // [4] 0x1800cdb60
    void ComputeLayout(int32_t x, int32_t y,
                       int32_t w, int32_t h) override;            // [5] 0x1800cda00
    void Draw(SpriteContext& context, uint8_t alpha) const override; // [6] 0x1800cd7a0
    // [7] 0x1800cd360 — the handle arrives as the ADDRESS of the caller's
    // temporary (Element.h's banner), hence the pointer.
    void Update(const System::Time& time, UserInterface* ui,
                Object::InternalPtr<DialogInstance>* dialog,
                const UpdateState& state) override;

    // ---- non-virtual public members ---------------------------------------
    // DEFINED in src/engine/ui/widgets_layout.cpp (fragment pass).
    System::String GetLink(int32_t index) const;                  // 0x18003c0c0 RETSLOT
    void SetText(const System::String& text);                     // 0x18003c010
    // `?SetText@DocumentTextElement@UI@@QEAAXVString@2@@Z` — the 0x10-byte
    // UI::String BY VALUE arrives by HIDDEN POINTER (RULE B6-11), so the member
    // takes the pointer (fragment pass F1; was declared by value).
    void SetText(const UI::String* text);                         // 0x18003bfd0

    DocumentTextElement(const DocumentTextElement& src);          // 0x18003c140  not ported (no caller)
    DocumentTextElement& operator=(const DocumentTextElement& src); // 0x18003c3d0 not ported

    // ---- the private text pipeline (`AEAA` in retail) ----------------------
    // Public here only because each keeps a registered door that reaches it.
    // Bodies in src/engine/ui/documenttext.cpp.
    void    GenerateItems();                                      // 0x1800cf060
    bool    GetNextItem(DocumentItem& item);                      // 0x1800cdf40
    int32_t LayoutRow();                                          // 0x1800cdbc0
    int32_t FindNextWhiteSpace();                                 // 0x1800cefd0
    void    BeginLink(const System::String& target);              // 0x1800cdeb0
    void    EndLink();                                            // 0x1800cde00
    void    EndRow();                                             // 0x1800cddb0  not ported
    bool    IsWhiteSpace(char c) const;                           // 0x1800cf030  not ported

private:

public:
    ::UIString _text;             // +0xc0   UI::String {table, index}: SetText(UI::String), the applier
    void*    _spriteSheet;        // +0xd0   ExternalPtr<UI::SpriteSheet>
    ::Sprite _underlineSprite;    // +0xd8   the 1px link underline (Draw)
    // +0xe8 Array<UI::Sprite>, one per font slot (^li's bullet). Raw header:
    // copy/destroy/resize are addressed helpers (0x180037020 / 0x180037310 /
    // 0x1800370c0), like every array below.
    // B4 Y RT-UI: the six arrays and the two Strings are real members. Retail
    // ~DocumentTextElement 0x1800cf320 releases +0x198, +0x168, then ~Array
    // 0x18003e310 (+0x158), 0x18003e0c0 (+0x148), 0x18003db40 (+0x130),
    // 0x18003da00 (+0x120), 0x18003d8c0 (+0x110), 0x180037310 (+0xe8): exact
    // reverse declaration order, so the compiler's member destructors are it.
    Collection::Array<::Sprite> _frameTable; // +0xe8
    uint32_t _linkColor;          // +0xf8   ctor 0xffeeee00
    uint32_t _linkHoverColor;     // +0xfc   ctor 0xffffff00
    uint32_t _linkPressedColor;   // +0x100  ctor 0xffcccc00
    int32_t  _tabWidth;           // +0x104  ctor 4; ^t advance, ^lb/^le indent step
    int32_t  _wrapWidth;          // +0x108  ctor 300; the default right edge
    uint32_t _pad10c;             // +0x10c  written by nothing
    Collection::Array<Font*> _fonts;         // +0x110  Font +0x50 line height, +0x54 baseline
    Collection::Array<SpriteSheet*> _sheets; // +0x120  (^i<digit>, sprite items)
    Collection::Array<int32_t> _colors;      // +0x130  (^c<digit>)
    Object::ExternalList* _resourceList; // +0x140 ExternalPtr<Object::ExternalList> (B4 H4)  object whose +0x20/+0x28 array ^d searches
    Collection::Array<DocumentItem> _items;  // +0x148  stride 0x24
    // +0x158 retail's own row (SetSize 0x18003e1a0 / 0x18003d430, ~Array 0x18003e310).
    Collection::Array<Link, Collection::DefaultAllocator,
                      Collection::ExplicitSpec<&rtcoll::kSpec_Link18>> _links;
    System::String _sourceText;              // +0x168  the flat text parsed
    // ---- the parse state (GenerateItems resets it; the ctor does not) ----
    int32_t  _textLength;         // +0x170  cached length of _sourceText
    int32_t  _textPos;            // +0x174
    int32_t  _penX;               // +0x178  row origin / left margin (^ml)
    int32_t  _hangIndent;         // +0x17c  folded into _penX at the next flush
    int32_t  _rightEdge;          // +0x180  ^mr; defaults to _wrapWidth
    int32_t  _unwrittenPenState;         // +0x184  saved/restored across ^lo only
    int32_t  _rowPenX;            // +0x188  running x inside the row being built
    int32_t  _penY;               // +0x18c
    int32_t  _rowFirstItem;       // +0x190
    uint32_t _pad194;             // +0x194  written by nothing
    Link     _openLink;           // +0x198  the link being accumulated (^ab ... ^ae)
    int32_t  _fontIndex;          // +0x1b0  ^f<digit>
    int32_t  _colorIndex;         // +0x1b4  ^c<digit>
    int32_t  _hAlign;             // +0x1b8  ^j : 0 left, 1 right, 2 centre
    int32_t  _vAlign;             // +0x1bc  ^v : 0 top, 1 centre, 2 baseline
    int32_t  _columnTop;          // +0x1c0  ^mb records, ^mc rewinds to
    int32_t  _columnMaxY;         // +0x1c4  ^mc / ^me high-water mark
};

// The transition alias documenttext.cpp's `using UI::DocumentItem;` names.
using DocumentItem = DocumentTextElement::DocumentItem;

static_assert(sizeof(DocumentTextElement) == 0x1c8,
              "UI::DocumentTextElement is 0x1c8 (MOV EDX,0x1c8 in 0x18003c650)");
static_assert(offsetof(DocumentTextElement, _text) == 0xC0, "DocumentTextElement::_text");
static_assert(offsetof(DocumentTextElement, _spriteSheet) == 0xD0, "DocumentTextElement::_spriteSheet");
static_assert(offsetof(DocumentTextElement, _frameTable) == 0xE8, "DocumentTextElement::_frameTable");
static_assert(offsetof(DocumentTextElement, _linkColor) == 0xF8, "DocumentTextElement::_linkColor");
static_assert(offsetof(DocumentTextElement, _linkHoverColor) == 0xFC, "DocumentTextElement::_linkHoverColor");
static_assert(offsetof(DocumentTextElement, _linkPressedColor) == 0x100, "DocumentTextElement::_linkPressedColor");
static_assert(offsetof(DocumentTextElement, _tabWidth) == 0x104, "DocumentTextElement::_tabWidth");
static_assert(offsetof(DocumentTextElement, _wrapWidth) == 0x108, "DocumentTextElement::_wrapWidth");
static_assert(offsetof(DocumentTextElement, _pad10c) == 0x10C, "DocumentTextElement::_pad10c");
static_assert(offsetof(DocumentTextElement, _fonts) == 0x110, "DocumentTextElement::_fonts");
static_assert(offsetof(DocumentTextElement, _sheets) == 0x120, "DocumentTextElement::_sheets");
static_assert(offsetof(DocumentTextElement, _colors) == 0x130, "DocumentTextElement::_colors");
static_assert(offsetof(DocumentTextElement, _resourceList) == 0x140, "DocumentTextElement::_resourceList");
static_assert(offsetof(DocumentTextElement, _items) == 0x148, "DocumentTextElement::_items");
static_assert(offsetof(DocumentTextElement, _links) == 0x158, "DocumentTextElement::_links");
static_assert(offsetof(DocumentTextElement, _sourceText) == 0x168, "DocumentTextElement::_sourceText");
static_assert(offsetof(DocumentTextElement, _textLength) == 0x170, "DocumentTextElement::_textLength");
static_assert(offsetof(DocumentTextElement, _textPos) == 0x174, "DocumentTextElement::_textPos");
static_assert(offsetof(DocumentTextElement, _penX) == 0x178, "DocumentTextElement::_penX");
static_assert(offsetof(DocumentTextElement, _hangIndent) == 0x17C, "DocumentTextElement::_hangIndent");
static_assert(offsetof(DocumentTextElement, _rightEdge) == 0x180, "DocumentTextElement::_rightEdge");
static_assert(offsetof(DocumentTextElement, _unwrittenPenState) == 0x184, "DocumentTextElement::_unwrittenPenState");
static_assert(offsetof(DocumentTextElement, _rowPenX) == 0x188, "DocumentTextElement::_rowPenX");
static_assert(offsetof(DocumentTextElement, _penY) == 0x18C, "DocumentTextElement::_penY");
static_assert(offsetof(DocumentTextElement, _rowFirstItem) == 0x190, "DocumentTextElement::_rowFirstItem");
static_assert(offsetof(DocumentTextElement, _pad194) == 0x194, "DocumentTextElement::_pad194");
static_assert(offsetof(DocumentTextElement, _openLink) == 0x198, "DocumentTextElement::_openLink");
static_assert(offsetof(DocumentTextElement, _fontIndex) == 0x1B0, "DocumentTextElement::_fontIndex");
static_assert(offsetof(DocumentTextElement, _colorIndex) == 0x1B4, "DocumentTextElement::_colorIndex");
static_assert(offsetof(DocumentTextElement, _hAlign) == 0x1B8, "DocumentTextElement::_hAlign");
static_assert(offsetof(DocumentTextElement, _vAlign) == 0x1BC, "DocumentTextElement::_vAlign");
static_assert(offsetof(DocumentTextElement, _columnTop) == 0x1C0, "DocumentTextElement::_columnTop");
static_assert(offsetof(DocumentTextElement, _columnMaxY) == 0x1C4, "DocumentTextElement::_columnMaxY");
static_assert(alignof(DocumentTextElement) == 8, "UI::DocumentTextElement aligns to 8");
static_assert(sizeof(DocumentTextElement::DocumentItem) == 0x24, "DocumentItem is 0x24 bytes");
static_assert(offsetof(DocumentTextElement::DocumentItem, x) == 0x0, "DocumentItem::x");
static_assert(offsetof(DocumentTextElement::DocumentItem, y) == 0x4, "DocumentItem::y");
static_assert(offsetof(DocumentTextElement::DocumentItem, pad22) == 0x22, "DocumentItem::pad22");
static_assert(offsetof(DocumentTextElement::DocumentItem, width)  == 0x08, "");
static_assert(offsetof(DocumentTextElement::DocumentItem, height) == 0x0c, "");
static_assert(offsetof(DocumentTextElement::DocumentItem, first)  == 0x10, "");
static_assert(offsetof(DocumentTextElement::DocumentItem, last)   == 0x14, "");
static_assert(offsetof(DocumentTextElement::DocumentItem, color)  == 0x18, "");
static_assert(offsetof(DocumentTextElement::DocumentItem, index)  == 0x1c, "");
static_assert(offsetof(DocumentTextElement::DocumentItem, kind)   == 0x20, "");
static_assert(offsetof(DocumentTextElement::DocumentItem, underline) == 0x21, "");
static_assert(sizeof(DocumentTextElement::Link) == DocumentTextElementOffsets::kLinkEntryStride, "");
static_assert(offsetof(DocumentTextElement::Link, text) == 0x0, "Link::text");
static_assert(offsetof(DocumentTextElement::Link, pad11) == 0x11, "Link::pad11");
static_assert(offsetof(DocumentTextElement::Link, firstItem) == 0x08, "");
static_assert(offsetof(DocumentTextElement::Link, lastItem)  == 0x0c, "");
static_assert(offsetof(DocumentTextElement::Link, armed)     == 0x10, "");

namespace DocumentTextElementOffsets {
using C = DocumentTextElement;
static_assert(offsetof(C, _text)            == kUiStringValue, "");
static_assert(offsetof(C, _spriteSheet)     == kSpriteSheet, "");
static_assert(offsetof(C, _underlineSprite) == kCaptureRefSheet, "");
static_assert(offsetof(C, _frameTable)      == kFrameTable, "");
static_assert(offsetof(C, _linkColor)       == kInt0F8, "");
static_assert(offsetof(C, _linkHoverColor)  == kInt0FC, "");
static_assert(offsetof(C, _linkPressedColor)== kInt100, "");
static_assert(offsetof(C, _tabWidth)        == kTabWidth, "");
static_assert(offsetof(C, _wrapWidth)       == kWrapWidth, "");
static_assert(offsetof(C, _fonts)           == kFontArray, "");
static_assert(offsetof(C, _sheets)          == kSheetArray, "");
static_assert(offsetof(C, _colors)          == kColorArray, "");
static_assert(offsetof(C, _resourceList)    == kResourceList, "");
static_assert(offsetof(C, _items)           == kItemArrayData, "");
static_assert(offsetof(C, _items) + offsetof(Collection::Array<DocumentItem>, _size) == kItemCount, "");
static_assert(offsetof(C, _items) + offsetof(Collection::Array<DocumentItem>, _capacity) == kItemCapacity, "");
static_assert(offsetof(C, _links)           == kLinkArrayData, "");
static_assert(offsetof(C, _sourceText)      == kContentString, "");
static_assert(offsetof(C, _textLength)      == kTextLength, "");
static_assert(offsetof(C, _textPos)         == kTextPos, "");
static_assert(offsetof(C, _penX)            == kPenX, "");
static_assert(offsetof(C, _hangIndent)      == kHangIndent, "");
static_assert(offsetof(C, _rightEdge)       == kRightEdge, "");
static_assert(offsetof(C, _unwrittenPenState)      == kInt184, "");
static_assert(offsetof(C, _rowPenX)         == kRowPenX, "");
static_assert(offsetof(C, _penY)            == kPenY, "");
static_assert(offsetof(C, _rowFirstItem)    == kRowFirstItem, "");
static_assert(offsetof(C, _openLink)        == kLinkText, "");
static_assert(offsetof(C, _openLink) + offsetof(DocumentTextElement::Link, firstItem) == kLinkFirstItem, "");
static_assert(offsetof(C, _openLink) + offsetof(DocumentTextElement::Link, lastItem)  == kLinkLastItem, "");
static_assert(offsetof(C, _openLink) + offsetof(DocumentTextElement::Link, armed)     == kLinkByte1A8, "");
static_assert(offsetof(C, _fontIndex)       == kFontIndex, "");
static_assert(offsetof(C, _colorIndex)      == kColorIndex, "");
static_assert(offsetof(C, _hAlign)          == kHAlign, "");
static_assert(offsetof(C, _vAlign)          == kVAlign, "");
static_assert(offsetof(C, _columnTop)       == kColumnTop, "");
static_assert(offsetof(C, _columnMaxY)      == kColumnMaxY, "");
static_assert(offsetof(Element, _layoutHeight) == kElementHeight, "");
}  // namespace DocumentTextElementOffsets

} // namespace UI
