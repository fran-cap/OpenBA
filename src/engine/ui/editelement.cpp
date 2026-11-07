// ============================================================================
// src/engine/ui/editelement.cpp — UI::EditElement, the whole class
// ============================================================================
//
// 2026-09-12, vtable batch 3. `??_7EditElement@UI@@6B@`.
//
//   slot  method                retail       where
//   ----  --------------------  -----------  ---------------------------------
//    0    vector deleting dtor  0x18003fed0  EditElement.h (the class's own)
//    1    TypeName              0x18003fae0  here
//    2    Type                  0x18003fac0  here
//    4    ComputeSize           0x1800d0c10  here
//    5    ComputeLayout         0x1800d0ab0  here
//    6    Draw                  0x1800d0820  here
//    7    Update                0x1800d0220  here
//   12    FinalizeText          0x1800d0da0  here
//
// plus ??1EditElement@UI@@UEAA@XZ 0x1800d0e50 and the factory pair (ENDGAME
// B.3.9 UI-A: the class is concrete; its real ctor ??0 0x1800d0f10 is in the
// header, and the copy-ctor door, which had no caller, is retired).
//
// Slots 4/5/6/7 are ALSO NumberEditElement's (it inherits them); registered
// once, here.
//
// *** THE VTABLE IS THIRTEEN SLOTS, NOT TWELVE. *** `?FinalizeText@` is slot 12
// (+0x60), INTRODUCED by this class. Update calls it through `[vtable + 0x60]`
// at 0x1800d0396; `?FinalizeText@NumberEditElement@` (0x1800d4a60) overrides it.
//
// ENDGAME B.3.6 block C3: every slot body is a `UI::EditElement` member
// (src/engine/ui/EditElement.h — the 0xf8 layout, pinned from the ctor
// 0x1800d0f10; the old header's "size not asserted" and NumberEditElement.h's
// 0xE8 are gone). The registered doors delegate with a QUALIFIED call. The
// base `Element::Update` call is the qualified member; slot 12 is a C++
// virtual call (it reads the MINTED table, exactly as retail's
// `CALL [RAX+0x60]`); Font / SpriteContext calls are direct member calls.
//
// NOT HERE: `?SetText@EditElement@UI@@` (widgets_input.cpp's door) and
// `?SelectCharacter@` (element_bodies_b5.cpp's door, reached by address); the
// constructor 0x1800d0f10 (EditElement.h since B.3.9).
// ============================================================================
#include <cstring>

#include "element_common.h"
#include "ui/EditElement.h"
#include "ui/Font.h"
#include "ui/SpriteContext.h"
#include "input/Device.h"   // ENDGAME B.3.9 SS1: Input::Device::_singleton
#include "input/Input.h"    // ENDGAME B.3.9 SS1: Input::Configuration::_singleton
#include "system/String.h"
#include "system/Timer.h"   // ENDGAME B.3.9 SS1: Timer::_secondsPerTick
#include "system/MemoryAllocator.h"
#include "collection/Collection.h"

using namespace rtuielem;

namespace {

// Input::Device::_keyboard — the per-key State records (+0x6c0, stride 0x18,
// 0x69 entries: the loop bound at 0x1800d07f6). Two bytes per key are tested
// together everywhere: `pressed` (+0x06) and `repeat` (+0x08). The retail
// displacements are +0x6c6 + key * 0x18 (and +2): Escape 0xc66, Enter 0xf7e,
// Left 0xea6, Right 0xed6, Home 0xe16, End 0xe5e, Back 0xba6, Delete 0xe46.
constexpr int kKeyCount  = ::Input::Device::kKeyboardInputCount;
constexpr int kKeyEscape = 60;
constexpr int kKeyEnter  = 93;
constexpr int kKeyLeft   = 84;
constexpr int kKeyRight  = 86;
constexpr int kKeyHome   = 78;
constexpr int kKeyEnd    = 81;
constexpr int kKeyBack   = 52;
constexpr int kKeyDelete = 80;

// ENDGAME B.3.9 SS1 (B39_RULES D7): both class statics, read at each use.
// The text-input latch is `Configuration::_singleton->_keyMap->_suspendCount`.
double SecondsPerTick() {
    // ENDGAME B.3.9 SS1 (B39_RULES D7): the class static now, no lazy cache.
    return ::System::Timer::_secondsPerTick;
}


void MemFree(void* p) { System::MemoryAllocator::MemFree(p); }

struct Event {
    void*    dialog;
    void*    element;
    UI::EventId kind;   // UI::EventId::ValueChanged: the edit was committed
    int32_t  elementId;
    int32_t  x;
    int32_t  y;
    int32_t  index;
    int32_t  pad24;
    uint64_t tail;
};
static_assert(sizeof(Event) == 0x30, "UI event record stride");

// `System::String`'s length, the way every body in this file reads it: the
// dword at [data - 8], and ZERO when the handle is null.
int StrLen(const char* s) {
    if (s == nullptr) return 0;
    return RtSystem::StringLength(s);
}

// The inlined `System::String::operator=` the three edit paths share:
// `if (dst != src) { release dst; dst = src; addref }`. The pointer COMPARE
// comes first, so assigning a String to itself does not touch the refcount.
void AssignStringInline(char** dst, char* const* src) {
    if (*dst == *src) return;
    if (*dst != nullptr) {
        int32_t* rc = &RtSystem::StringRefCount(*dst);
        --*rc;
        if (*rc == 0) {
            *dst = *dst - 8;
            MemFree(*dst);
        } else {
            *dst = nullptr;
        }
    }
    *dst = *src;
    if (*dst != nullptr) ++RtSystem::StringRefCount(*dst);
}

// Retail's password mask: append '*' to a fresh String once per character of
// the real text. `System::String::operator+=` (0x1800c89e0) takes a HIDDEN
// TEMPORARY SLOT as its second argument and leaves a String in it that the
// caller must release — which is why the loop has a release in its body: the
// temporary's ~String at the end of each iteration (B4 Y RT-UI: the mask and
// the temporary are System::String locals; the mask is handed to the caller
// by value, its one reference moving with it).
System::String BuildMask(int length) {
    System::String masked;
    for (int i = 0; i < length; ++i) {
        System::String tmp = (masked += '*');                  // 0x1800c89e0, retslot = &tmp
    }
    return masked;
}

}  // namespace

namespace UI {

// ??1EditElement@UI@@UEAA@XZ  0x1800d0e50 — retail's body, moved here from the
// retired door (ENDGAME B.3.9 UI-A): DROP the text-input latch if this element
// still holds it, release the filter and the text; the restamp is the
// compiler's and so is the chain into ~Element. The latch decrement is the
// destructor's half of Update's pairing; destroying a focused edit box without
// it deafens the game.
EditElement::~EditElement() {
    if (_active != 0) {
        --::Input::Configuration::_singleton->_keyMap->_suspendCount;
    }
    // `_filter` (+0xd8) then `_text` (+0xc8) are released by their own
    // destructors, in reverse declaration order: retail's 0x1800d0e85 /
    // 0x1800d0ebb order.
}

// ============================================================================
// ?ComputeSize@EditElement@UI@@UEAAXXZ   0x1800d0c10
// ============================================================================
// One line of text, measured with `UI::Font::ComputeLineLayout` (0x180116c70),
// floored by the element's minimum WIDTH only — the height is the font's line
// height with no floor at all.
//
// In password mode the mask is built FIRST and measured instead, so a password
// box is as wide as its asterisks, not as its secret.
void EditElement::ComputeSize() {
    if ((_flags & kElementFlagNeedsSizeCalc) == 0) return;

    Font* font = _font;

    if (_password == 0) {
        char* text = _text._data;
        _layoutWidth = 0; _layoutHeight = 0;                 // one qword store
        _layoutWidth  = font->ComputeLineLayout(text, StrLen(text), 0, 0, -1, 0, nullptr);
        _layoutHeight = font->_lineHeight;
    } else {
        System::String masked = BuildMask(StrLen(_text._data));
        _layoutWidth = 0; _layoutHeight = 0;                 // one qword store
        _layoutWidth  = font->ComputeLineLayout(masked._data,
                                                StrLen(masked._data), 0, 0, -1, 0, nullptr);
        _layoutHeight = font->_lineHeight;
        // `masked`'s ~String: the release that ended this arm.
    }

    if (_layoutWidth < _minWidth) _layoutWidth = _minWidth;
    _flags &= ~kElementFlagNeedsSizeCalc;
}

// ============================================================================
// ?ComputeLayout@EditElement@UI@@UEAAXHHHH@Z   0x1800d0ab0 — the standard
// Element alignment body (element_common.h), which dispatches slots 5/4.
// ============================================================================
void EditElement::ComputeLayout(int32_t x, int32_t y, int32_t w, int32_t h) {
    ElementComputeAlignedRect(this, x, y, w, h);
}

// ============================================================================
// ?Draw@EditElement@UI@@UEBAXAEAVSpriteContext@2@E@Z   0x1800d0820
// ============================================================================
// *** THE DISABLED TEST IS NOT THE USUAL ONE. *** This one returns early when
// EITHER disabled bit is set UNLESS bit 24 (system-disabled) is ALSO set — i.e.
// a user-disabled edit box draws nothing, but a system-disabled one still
// draws. Read off 0x1800d085d..0x1800d086e.
//
// The text is drawn at the element's left edge and a baseline of
// `top - font.baseline + font.lineHeight`, and the caret — a literal "|" —
// one pixel LEFT of the character position the caret index resolves to.
void EditElement::Draw(SpriteContext& ctx, uint8_t alpha) const {
    if (Element* border = _border)
        border->Draw(ctx, alpha);                            // virtual, slot 6

    const uint32_t flags = _flags;
    if ((flags & UI::kElementFlagAnyDisabled) != 0 && (flags & kElementFlagSystemDisabled) == 0) return;

    const uint32_t baseColor = _color;
    if ((flags & kElementFlagDimColor) != 0) alpha = static_cast<uint8_t>(alpha >> 1);
    const uint32_t colour = rtuielem::ComputeColor(baseColor, alpha);

    // The string actually drawn: the mask in password mode, otherwise a
    // REFERENCE to the real text (retail bumps its refcount and releases it at
    // the end, so the field itself is untouched).
    // B4 Y RT-UI: `shown` and `caret` are System::String locals; each one's
    // ~String is the release that ended its scope.
    System::String shown;
    if (_password != 0) {
        shown = BuildMask(StrLen(_text._data));              // a pointer steal onto null
    } else if (_text._data != nullptr) {
        shown._data = _text._data;
        ++RtSystem::StringRefCount(shown._data);
    }

    Font* font = _font;
    const int baseline = _layoutTop - font->_baseLine + font->_lineHeight;
    ctx.AddFont(Object::ExternalPtr<Font>(font), shown, false, 0, 0, -1,
                _layoutLeft, baseline, colour);

    if (_blinkOn != 0 && _active != 0) {
        const int cx = _font->ComputeCharacterPosition(shown, _caret) - 1;

        // THE CARET'S STRING IS BUILT FROM A C LITERAL. Retail passes the DLL's
        // own `"|"` at 0x180156804; our own literal is the same bytes and
        // `System::String::Set` copies them either way.
        System::String caret;
        static const char kCaretText[] = "|";
        caret.Set(kCaretText);                                          // 0x1800c95d0

        Font* f = _font;
        const int y = _layoutTop - f->_baseLine + f->_lineHeight;
        ctx.AddFont(Object::ExternalPtr<Font>(_font), caret, false, 0, 0, -1,
                    _layoutLeft + cx, y, colour);
    }
}

// ============================================================================
// ?Update@EditElement@UI@@   0x1800d0220
// ============================================================================
// Focus, caret movement, editing and character insertion, all driven straight
// off `Input::Device::_singleton`'s key table rather than through the update
// state — the element reads the keyboard itself.
//
// FOCUS is taken on a click inside (and the caret placed at the clicked
// character, found by walking `ComputeCharacterPosition` until it passes the
// cursor), and dropped on a click OUTSIDE, on Escape, or on Enter. Taking and
// dropping it INCREMENTS and DECREMENTS a counter inside
// `Input::Configuration::_singleton` — the engine's "text input is active,
// stop treating keys as game bindings" latch.
//
// DROPPING focus calls virtual slot 12 (`FinalizeText`) and raises event 0x15.
void EditElement::Update(const System::Time& time, UserInterface* ui,
                         Object::InternalPtr<DialogInstance>* dlg,
                         const UpdateState& st) {
    {
        // The base body gets its OWN copy of the handle, by address (B6-11).
        Object::InternalPtr<DialogInstance> dlgCopy = *dlg;
        UI::Element::Update(time, ui, &dlgCopy, st);
    }

    const ::Input::Device* dev = ::Input::Device::_singleton;
    auto Down = [dev](int key) {                               // pressed or repeat
        return dev->_keyboard[key].pressed != 0 || dev->_keyboard[key].repeat != 0;
    };

    if ((_flags & UI::kElementFlagAnyDisabled) != 0) return;

    // ---- take focus -------------------------------------------------------
    if (st.buttonPressed != 0 && (_flags & kElementFlagHovered) != 0) {
        if (_active == 0) {
            ++::Input::Configuration::_singleton->_keyMap->_suspendCount;
        }
        _active = 1;
        _blinkOn = 1;
        _blinkAccumulator = 0;
        _caret = StrLen(_text._data);

        const int relX = st.mouseX - _layoutLeft;
        for (int i = 0; i < StrLen(_text._data); ++i) {
            const int cx = _font->ComputeCharacterPosition(_text, i + 1);
            if (relX >= 0 && relX <= cx) { _caret = i; break; }
        }
    }

    if (_active == 0) return;

    // ---- drop focus -------------------------------------------------------
    const bool clickedAway = st.buttonPressed != 0 && (_flags & kElementFlagHovered) == 0;
    if (clickedAway || dev->_keyboard[0].pressed != 0 ||
        dev->_keyboard[kKeyEscape].pressed != 0 || dev->_keyboard[kKeyEnter].pressed != 0) {
        _active = 0;
        --::Input::Configuration::_singleton->_keyMap->_suspendCount;

        FinalizeText();                                      // virtual, slot 12
        SetNeedsSizeCalc();

        ::Event ev;
        ev.dialog    = dlg->Get();
        ev.element   = this;
        ev.kind      = EventId::ValueChanged;
        ev.elementId = _elementId;
        ev.x         = st.mouseX;
        ev.y         = st.mouseY;
        ev.index     = 0;
        ev.tail      = 0;
        rtcoll::RtColl_ArraySetSizeFill_180033690(&ui->_events, ui->_events._size + 1, &ev);
    }

    if (_active == 0) return;

    // ---- blink ------------------------------------------------------------
    _blinkAccumulator += time._ticks;
    if (_blinkAccumulator > static_cast<int64_t>(0.5 / SecondsPerTick())) {
        _blinkOn = (_blinkOn == 0) ? 1 : 0;
        _blinkAccumulator = 0;
    }

    // ---- caret movement ---------------------------------------------------
    if (Down(kKeyLeft)) {
        _caret -= 1;
        if (_caret < 0) _caret = 0;
    }
    if (Down(kKeyRight)) {
        _caret += 1;
        const int len = StrLen(_text._data);
        if (_caret > len) _caret = len;
    }
    if (dev->_keyboard[kKeyHome].pressed != 0) _caret = 0;
    if (dev->_keyboard[kKeyEnd].pressed  != 0) _caret = StrLen(_text._data);

    // ---- backspace / delete ----------------------------------------------
    // System::String::RemoveAt (0x1800c86b0, retslot) and operator= (0x1800c9810).
    if (Down(kKeyBack) && _caret > 0) {
        {
            System::String tmp = _text.RemoveAt(_caret - 1);
            _text = tmp;
        }                                                    // tmp's release
        _caret -= 1;
        SetNeedsSizeCalc();
    }

    if (Down(kKeyDelete)) {
        if (_caret < StrLen(_text._data)) {
            {
                System::String tmp = _text.RemoveAt(_caret);
                _text = tmp;
            }                                                // tmp's release
            SetNeedsSizeCalc();
        }
    }

    // ---- character insertion ----------------------------------------------
    // `?SelectCharacter@` (0x1800cfd40) maps a key index to the character it
    // produces under the current modifiers, or 0 for "not a character key".
    for (int k = 0; k < kKeyCount; ++k) {
        if (dev->_keyboard[k].pressed == 0 && dev->_keyboard[k].repeat == 0) continue;

        const uint8_t ch = static_cast<uint8_t>(SelectCharacter(k));
        if (ch == 0) continue;

        if (StrLen(_text._data) >= _maxLength) continue;

        // The filter: when it is a non-empty String, the character must appear
        // in it. When it is null or empty, anything goes.
        char* filter = _filter._data;
        const int filterLen = StrLen(filter);
        if (filter != nullptr && filterLen > 0) {
            bool found = false;
            for (int m = 0; m < filterLen; ++m) {
                if (static_cast<uint8_t>(_filter._data[m]) == ch) { found = true; break; }
            }
            if (!found) continue;
        }

        // Build a one-character System::String. Retail does this with a
        // 2-byte stack buffer, two hand-rolled strlen loops and a memcpy of
        // `min(strlen + 1, length + 1)`; transcribed rather than replaced by a
        // one-liner so the length arithmetic stays visible.
        char buf[2];
        buf[0] = static_cast<char>(ch);
        buf[1] = '\0';

        // Both counts are DO-WHILE loops in retail (0x1800d06c0 and
        // 0x1800d06e5): they step once before testing.
        int n = 0;
        int64_t idx = 0;
        do { ++n; ++idx; } while (buf[idx] != 0);

        // B4 Y RT-UI: `one` and `ins` are System::String locals of this block;
        // their destructors at its closing brace (ins, then one: reverse
        // declaration order) are the two releases that stood there.
        {
            System::String one;
            System::StringPrivateAccess::SetLength(one, n);           // 0x1800c8220

            const int cap = n + 1;
            int srcLen = 0;
            {
                const char* p = buf;
                do { ++srcLen; ++p; } while (*p != 0);
            }
            ++srcLen;
            const int copy = (cap < srcLen) ? cap : srcLen;
            std::memmove(one._data, buf, static_cast<size_t>(copy));  // 0x18008a290 is memmove
            if (srcLen > cap) one._data[idx] = 0;

            System::String ins = _text.InsertAt(_caret, one);         // 0x1800c8510, retslot = &ins
            char** ret = &ins._data;
            AssignStringInline(&_text._data, ret);
        }

        _caret += 1;
        SetNeedsSizeCalc();
    }
}

// ============================================================================
// ?FinalizeText@EditElement@UI@@UEAAXXZ   0x1800d0da0 — slot 12, INTRODUCED
// ============================================================================
// A bare `RET`. It exists so `UI::NumberEditElement` can override it
// (0x1800d4a60) and clamp its value when the box loses focus. Ported as the
// empty body it is — see reference_retail_ships_empty_bodies.md.
void EditElement::FinalizeText() {}

}  // namespace UI

// ============================================================================
// The factory pair (B39_RULES D2), replacing the FACTUI1 row
// ============================================================================
namespace UI {
// ?CreateNewObject@EditElement@UI@@SAPEAVBase@Object@@XZ  0x18003fbd0 —
// MemAlloc(0xf8, 8), retail's null branch, then the out-of-line ctor.
Object::Base* EditElement::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(EditElement), 8);
    if (p == nullptr) return nullptr;
    return ::new (p) EditElement();
}
// ?CreateInPlaceObject@EditElement@UI@@SAPEAVBase@Object@@PEAX@Z  0x18003fc50
Object::Base* EditElement::CreateInPlaceObject(void* place) {
    if (place == nullptr) return nullptr;
    return ::new (place) EditElement();
}
}  // namespace UI

// ============================================================================
// ENDGAME B.3.9 TI-UI (B39 D6): UI::EditElement's own type record, built at CRT init
// (head-insert into Object::BaseTypeInfo::_firstTypeInfo). Literals copied from
// the retired minted row ?_typeInfo@EditElement@UI@@ (0x1801f9f60): name "EditElement", parent
// "Element" (retail ?_ParentTypeName@EditElement@UI@@), no file extension; factories:
// the class's own.
// ============================================================================
DEFINE_TYPE(UI::EditElement, "Element", nullptr)
