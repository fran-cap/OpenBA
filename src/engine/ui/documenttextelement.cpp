// ============================================================================
// src/engine/ui/documenttextelement.cpp — UI::DocumentTextElement
// ============================================================================
//
// 2026-09-12, vtable batch 3. `??_7DocumentTextElement@UI@@6B@` (retail
// 0x1801566f0). The rich-text block: a laid-out run of text ITEMS, some of
// which are inline sprites or sprite captures, with clickable LINK spans over
// them.
//
//   slot  method                retail       where
//   ----  --------------------  -----------  ---------------------------------
//    0    vector deleting dtor  0x18003c650  deleting_dtor.cpp row
//    1    TypeName              0x18003be50  here
//    2    Type                  0x18003be30  here
//    4    ComputeSize           0x1800cdb60  here
//    5    ComputeLayout         0x1800cda00  here
//    6    Draw                  0x1800cd7a0  here
//    7    Update                0x1800cd360  here
//
// plus ??0DocumentTextElement@UI@@QEAA@XZ         0x1800cf410
//      ??0DocumentTextElement@UI@@QEAA@AEBV01@@Z  0x18003c140
//      ??1DocumentTextElement@UI@@UEAA@XZ         0x1800cf320
//
// `?SetText@` (both overloads) and `?GetLink@` are ALREADY PORTED in
// src/engine/ui/widgets_layout.cpp; `?GenerateItems@` (0x1800cf060), the
// layout pass ComputeSize calls, is a member in src/engine/ui/documenttext.cpp.
//
// B.3.6 block C5: slots 1, 2, 4-7 are members of UI::DocumentTextElement
// (src/engine/ui/DocumentTextElement.h); the registered names are one-line
// qualified doors.
//
// ---------------------------------------------------------------------------
// LAYOUT — 0x1c8 bytes (`MOV EDX,0x1c8` at 0x18003c66f). Enumerated from the
// constructor at 0x1800cf410 and the destructor at 0x1800cf320; the three
// offsets src/engine/ui/DocumentTextElement.h already pins are marked.
//
//   +0x0c0 UI::String   the by-value text (header: kUiStringValue)
//   +0x0d8 UI::Sprite   the 1px UNDERLINE sprite (sheet +0x0d8, index +0x0e0)
//   +0x0e8 Array<UI::Sprite>       inline sprite table
//   +0x0f8 uint32       link colour, normal   (ctor 0xffeeee00)
//   +0x0fc uint32       link colour, hovered  (ctor 0xffffff00)
//   +0x100 uint32       link colour, pressed  (ctor 0xffcccc00)
//   +0x104 int32        ctor 4
//   +0x108 int32        the WRAP WIDTH, ctor 300 — ComputeSize floors
//                       `_layoutWidth` by it
//   +0x110 Array<UI::Font*>        the font table items index into
//   +0x120 Array<UI::SpriteSheet*> the sheet table inline images index into
//   +0x130 Array<...>              a third table, destroyed by 0x18003db40
//   +0x140 ExternalPtr             an object whose +0x20 is the array inline
//                                  CAPTURES index into
//   +0x148 Array<Item>  stride 0x24 — the laid-out run (see below)
//   +0x158 Array<Link>  stride 0x18 (header: kLinkArrayData)
//   +0x168 System::String  the source text (header: kContentString)
//   +0x170 .. +0x197  NOT WRITTEN BY THE CONSTRUCTOR; copied by the copy ctor
//   +0x198 System::String
//   +0x1a0 int32 -1, +0x1a4 int32 -1, +0x1a8 uint8 0
//   +0x1b0 .. +0x1c7  six more dwords, copied by the copy ctor
//
// ITEM (stride 0x24), from Draw and Update:
//   +0x00 int32 x       +0x04 int32 y      +0x08 int32 width   +0x0c int32 height
//   +0x10 int32 substring START (kind 0) / sprite INDEX (kind 1)
//   +0x14 int32 substring LENGTH
//   +0x18 uint32 colour  +0x1c int32 table index
//   +0x20 uint8 kind: 0 text, 1 sprite, 2 capture
//   +0x21 uint8 underline: 0 none, 1 full width, 2 width minus the font's +0x5c
//
// LINK (stride 0x18):
//   +0x08 int32 first item   +0x0c int32 one past last   +0x10 uint8 armed
//
// `??0DocumentTextElement@UI@@QEAA@XZ` (0x1800cf410) is NOT here: it is already
// ported and registered, table-driven, by src/engine/ui/factory_ui1.cpp
// (the factory roster). One address, one owner.
// ============================================================================
#include <new>

#include "element_common.h"
#include "ui/DocumentTextElement.h"
#include "ui/SpriteCapture.h"   // B.3.9 TI-UI: SpriteCapture::TypeId()
#include "system/Timer.h"   // ENDGAME B.3.9 SS1: Timer::_secondsPerTick
#include "ui/SpriteContext.h"
#include "ui/Font.h"
#include "audio/Audio.h"   // B4: Audio::Scene::Channel (the inlined Play)
#include "system/String.h"
#include "system/MemoryAllocator.h"
#include "collection/Collection.h"

using namespace rtuielem;

namespace {

constexpr int kUnderlineSprite = 0x0d8;
constexpr int kLinks           = UI::DocumentTextElementOffsets::kLinkArrayData;  // 0x158
constexpr int kSourceText      = UI::DocumentTextElementOffsets::kContentString;  // 0x168

static_assert(kLinks == 0x158 && kSourceText == 0x168,
              "DocumentTextElement.h must still pin the link array and the source string");
static_assert(kUnderlineSprite == offsetof(UI::DocumentTextElement, _underlineSprite), "");

using Item = UI::DocumentTextElement::DocumentItem;
using Link = UI::DocumentTextElement::Link;

// UserInterface::_sounds[4] (+0xb8) and [5] (+0xc0): the hover and click sounds.
constexpr int kUiHoverSound  = 4;
constexpr int kUiClickSound  = 5;

double SecondsPerTick() {
    // ENDGAME B.3.9 SS1 (B39_RULES D7): the class static now, no lazy cache.
    return ::System::Timer::_secondsPerTick;
}


struct Event {
    void*    dialog;
    void*    element;
    UI::EventId kind;
    int32_t  elementId;
    int32_t  x;
    int32_t  y;
    int32_t  index;
    int32_t  pad24;
    uint64_t tail;
};
static_assert(sizeof(Event) == 0x30, "UI event record stride");

// `UI::UserInterface::Play`, INLINED by this body (ButtonElement calls the
// out-of-line 0x1801223d0 instead). Walk the audio scene's 256 voice slots for
// one whose +0x68 is zero, take it, and stamp a generation counter into it.
//
// `(int64)(0.0 / _secondsPerTick)` is retail's start time. It is zero, and it
// is computed the way retail computes it rather than written as 0, because the
// division is what the bytes do — and because `_secondsPerTick` being zero
// would make it a NaN there too.
void PlayInline(UI::UserInterface* ui, int soundIndex) {
    ::Audio::SoundEffect* sound = ui->_sounds[soundIndex];
    if (sound == nullptr) return;
    // LEFT: UserInterface::_audioScene is void* (UserInterface.h, R-UI-b).
    ::Audio::Scene* scene = static_cast<::Audio::Scene*>(ui->_audioScene);
    if (scene == nullptr) return;

    const int64_t start = static_cast<int64_t>(0.0 / SecondsPerTick());
    ::Audio::Scene::Channel* voices = scene->channels._data;
    for (int64_t i = 0; i < 0x100; ++i) {
        ::Audio::Scene::Channel& v = voices[i];
        if (v.handleTag != 0) continue;
        v.soundEffectRef = sound;
        v.startTime._ticks = start;
        v.soundType = 1;
        // Retail's INC / SHL wrap at 32 bits: unsigned here (an int32
        // overflow or shift is UB), as UserInterface::Play.
        const uint32_t counter = static_cast<uint32_t>(scene->playGeneration) + 1;
        scene->playGeneration = static_cast<int32_t>(counter);
        v.handleTag = (static_cast<uint32_t>(i) * 2u) | (counter << 9);
        return;
    }
}

}  // namespace

namespace UI {

// ============================================================================
// [4] ?ComputeSize@DocumentTextElement@UI@@UEAAXXZ   0x1800cdb60
// ============================================================================
// Throw away the previous item and link lists, floor the width at the wrap
// width, zero the height, and let `GenerateItems` (0x1800cf060) re-lay the text
// and fill both lists — it is what actually writes `_layoutHeight`.
//
// NOTE THE ASYMMETRY: the WIDTH is a floor (`if (_layoutWidth < _wrapWidth)`),
// so a document inside a wider box keeps the box's width, while the HEIGHT is
// unconditionally reset to 0 before the layout pass.
void DocumentTextElement::ComputeSize() {
    if ((_flags & kElementFlagNeedsSizeCalc) == 0) return;

    rtcoll::RtColl_ArraySetSize_18003d1c0(&_items);
    rtcoll::RtColl_ArraySetSize_18003d430(&_links);

    if (_layoutWidth < _wrapWidth)
        _layoutWidth = _wrapWidth;
    _layoutHeight = 0;

    GenerateItems();   // 0x1800cf060 — was by address; the member (documenttext.cpp)

    _flags &= ~UI::kElementFlagNeedsSizeCalc;
}

// ============================================================================
// [5] ?ComputeLayout@DocumentTextElement@UI@@UEAAXHHHH@Z   0x1800cda00
// ============================================================================
void DocumentTextElement::ComputeLayout(int32_t x, int32_t y, int32_t w, int32_t h) {
    ElementComputeAlignedRect(this, x, y, w, h);
}

// ============================================================================
// [6] ?Draw@DocumentTextElement@UI@@UEBAXAEAVSpriteContext@2@E@Z   0x1800cd7a0
// ============================================================================
// One pass over the item list. There is NO alpha modulation anywhere in this
// body: each item carries its own colour (Update computes it), and the `alpha`
// parameter is loaded, halved by the dim flag and then never used — the halving
// is dead code in the shipped function and is reproduced only in that it costs
// nothing.
//
// The three item kinds:
//   0  TEXT     — a SUBSTRING of `_sourceText` (start +0x10, length +0x14),
//                 drawn with `_fonts[+0x1c]`, then optionally an underline bar
//                 one pixel below the baseline. The temporary String is
//                 released before the next item.
//   1  SPRITE   — a UI::Sprite built ON THE STACK from `_sheets[+0x1c]` and
//                 the item's +0x10, drawn at natural size in OPAQUE WHITE.
//   2  CAPTURE  — a `UI::SpriteCapture` fetched from `_resourceList->[+0x20]`,
//                 type-tested, and given its own Draw. A failed type test
//                 yields null and retail calls through it.
void DocumentTextElement::Draw(SpriteContext& context, uint8_t alpha) const {
    if (_border != nullptr)
        _border->Draw(context, alpha);   // slot 6

    Font* const* const fonts = _fonts._data;
    const int32_t count = _items._size;
    for (int32_t i = 0; i < count; ++i) {
        const Item* item = _items._data + i;
        const uint8_t kind = item->kind;
        const int L = _layoutLeft;
        const int T = _layoutTop;

        if (kind == 0) {
            // The temporary is a System::String local of this arm: the
            // retslot of SubString (0x1800c9230) is its storage, and its
            // ~String at the end of the arm is the release that closed it.
            System::String tmp = _sourceText.SubString(item->first, item->last);

            Font* font = _fonts._data[item->index];
            context.AddFont(Object::ExternalPtr<UI::Font>(font),          // 0x18011ccb0
                            tmp, false, 0, 0, -1,
                            item->x + L, item->y + T, static_cast<uint32_t>(item->color));

            const uint8_t underline = item->byte21;
            if (underline == 1 || underline == 2) {
                int width = item->width;
                if (underline == 2) {
                    const Font* f = fonts[item->index];
                    width -= f->_spaceWidth;                          // Font +0x5c
                }
                const int x = item->x + L;
                const int y = item->y + T;
                context.AddSprite(_underlineSprite, x, y + 1, width + x, y + 2,   // 0x18011d3c0
                                  static_cast<uint32_t>(item->color));
            }
            // `tmp`'s ~String: the release that stood here.
        } else if (kind == 1) {
            UI::Sprite sprite;
            sprite.spriteSheet = _sheets._data[item->index];
            sprite.spriteIndex = item->first;
            context.AddSprite(sprite,                                       // 0x18011cf00
                              L + item->x, T + item->y, System::kColorWhite);
        } else if (kind == 2) {
            Object::External* obj = _resourceList->_resources[item->index];
            const UI::SpriteCapture* capture = obj->IsDerivedFrom(UI::SpriteCapture::TypeId())
                                ? static_cast<const UI::SpriteCapture*>(obj) : nullptr;
            // ?Draw@SpriteCapture@UI@@ (0x1800d9230).
            capture->Draw(context, L + item->x, T + item->y);
        }
    }
}

// ============================================================================
// [7] ?Update@DocumentTextElement@UI@@   0x1800cd360
// ============================================================================
// Link hit testing and colouring, one link span at a time.
//
// For each link: walk its items looking for one under the cursor. A TEXT item's
// vertical extent is shifted by `font.baseline - font.lineHeight` because its
// stored Y is a baseline, not a top edge; sprite and capture items are not.
// Then colour every TEXT item in the span, arm the underline on them, and raise
// the hover / click sounds and the activation event.
//
// *** THE UNDERLINE FLAG ON THE LAST ITEM OF A PRESSED LINK IS 2, NOT 1. ***
// `if (j == last - 1 && pressed) item[0x21] = 2 else item[0x21] = pressed`
// (0x1800cd5xx). Draw reads that 2 as "full width minus the font's +0x5c", i.e.
// the trailing space is not underlined. A port that writes `pressed` uniformly
// draws one glyph too much of rule on every link.
//
// `UI::UserInterface::Play` is INLINED here, twice — see PlayInline above.
void DocumentTextElement::Update(const System::Time& time, UserInterface* ui,
                                 Object::InternalPtr<DialogInstance>* dialog,
                                 const UpdateState& state) {
    {
        // The base Update, QUALIFIED (this object's slot 7 is this very body),
        // with the handle copied into a temporary whose ADDRESS is passed.
        Object::InternalPtr<DialogInstance> dlgCopy = *dialog;
        Element::Update(time, ui, &dlgCopy, state);
    }

    if ((_flags & UI::kElementFlagHidden) != 0) return;

    const int32_t linkCount = _links._size;
    for (int32_t li = 0; li < linkCount; ++li) {
        Link* link = _links._data + li;

        uint32_t colour = _linkColor;
        bool hit = false;
        bool pressed = false;

        if ((_flags & kElementFlagHovered) != 0) {
            int32_t j = link->firstItem;
            const int32_t end = link->lastItem;
            for (; j < end; ++j) {
                const Item* item = _items._data + j;
                int y0 = item->y + _layoutTop;
                int y1 = item->height + y0;
                if (item->kind == 0) {
                    const Font* f = _fonts._data[item->index];
                    const int adj = f->_baseLine - f->_lineHeight;
                    y0 += adj;
                    y1 += adj;
                }
                const int x0 = item->x + _layoutLeft;
                const int x1 = item->width + x0;
                if (x0 <= state.mouseX && state.mouseX < x1 &&
                    y0 <= state.mouseY && state.mouseY < y1) {
                    hit = true;
                    break;
                }
            }
        }

        if (state.buttonPressed != 0 && hit) link->armed = 1;

        if ((_flags & UI::kElementFlagAnyDisabled) == 0) {
            if (hit) {
                colour = (link->armed != 0 && state.leftDown != 0)
                             ? _linkPressedColor : _linkHoverColor;
                pressed = true;
            } else {
                link->armed = 0;
            }
        } else {
            colour = _linkColor;
            if (!hit) link->armed = 0;
        }

        for (int32_t j = link->firstItem; j < link->lastItem; ++j) {
            Item* item = _items._data + j;
            if (item->kind != 0) continue;
            item->color = static_cast<int32_t>(colour);
            if (j == link->lastItem - 1 && pressed) item->byte21 = 2;
            else                                    item->byte21 = pressed ? 1 : 0;
        }

        if (state.buttonPressed != 0 && hit && link->armed != 0)
            PlayInline(ui, kUiHoverSound);

        if (state.b04 != 0 && hit && link->armed != 0) {
            ::Event ev;
            ev.dialog    = dialog->Get();
            ev.element   = this;
            ev.kind      = EventId::Activated;
            ev.elementId = _elementId;
            ev.x         = state.mouseX;
            ev.y         = state.mouseY;
            ev.index     = li;
            ev.tail      = 0;
            rtcoll::RtColl_ArraySetSizeFill_180033690(&ui->_events,
                                                      ui->_events._size + 1, &ev);
            PlayInline(ui, kUiClickSound);
        }
    }
}

// ??1DocumentTextElement@UI@@UEAA@XZ  0x1800cf320 — retail's body, moved from
// the retired door (ENDGAME B.3.9 UI-B): both Strings released, six arrays
// destroyed in retail's order. The entry restamp and the TAIL JUMP to ~Element
// are the compiler's.
DocumentTextElement::~DocumentTextElement() {
    // B4 Y RT-UI: empty. +0x198 (`_openLink`'s String), +0x168 (`_sourceText`),
    // then ~Array +0x158 (0x18003e310, kSpec_Link18's destroy 0x18003e3e0 then
    // J1), +0x148 (0x18003e0c0), +0x130 (0x18003db40), +0x120 (0x18003da00),
    // +0x110 (0x18003d8c0), +0xe8 (0x180037310) are the member destructors,
    // run after this body in reverse declaration order: retail's order.
}

// ?CreateNewObject@DocumentTextElement@UI@@SAPEAVBase@Object@@XZ 0x18003bf40 --
// `?MemAlloc@MemoryAllocator` (0x1800c7a50) of 0x1c8, align 8; retail's null
// branch; the ctor. Replaces the factory interpreter row (B39_RULES D2).
Object::Base* DocumentTextElement::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(DocumentTextElement), 8);
    if (p == nullptr) return nullptr;
    return ::new (p) DocumentTextElement();
}
// ?CreateInPlaceObject@DocumentTextElement@UI@@SAPEAVBase@Object@@PEAX@Z 0x18003bfc0
Object::Base* DocumentTextElement::CreateInPlaceObject(void* place) {
    if (place == nullptr) return nullptr;
    return ::new (place) DocumentTextElement();
}

}  // namespace UI

// ============================================================================
// ENDGAME B.3.9 UI-B: the slot doors (Type .. Update), the copy-ctor door and
// the ??1 door are gone. Their only readers were the minted
// `??_7DocumentTextElement@UI@@6B@`, its RT_DELDTOR row and the FACTUI1 row.
// ============================================================================

// ============================================================================
// ENDGAME B.3.9 TI-UI (B39 D6): UI::DocumentTextElement's own type record, built at CRT init
// (head-insert into Object::BaseTypeInfo::_firstTypeInfo). Literals copied from
// the retired minted row ?_typeInfo@DocumentTextElement@UI@@ (0x1801f9ef0): name "DocumentTextElement", parent
// "Element" (retail ?_ParentTypeName@DocumentTextElement@UI@@), no file extension; factories:
// the class's own.
// ============================================================================
DEFINE_TYPE(UI::DocumentTextElement, "Element", nullptr)
