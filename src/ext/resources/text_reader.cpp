// ============================================================================
// src/ext/resources/text_reader.cpp -- ::Object::External objects from our .rsc
// text (docs/extensions/KITFREE.md, block RSCTEXT)
// ============================================================================
// NOT RETAIL. The kit's compiler (Tools-x64.exe, Runtime-x64-profile.dll)
// read a .rsc through a TEXT IO::ReaderWriter that answered each Serialize
// slot by the FIELD NAME the profile build passes; the shipped DLL stripped
// those names (every ported Serialize body passes `nullptr`, and the binary
// format carries none), so a name-driven reader cannot exist here. What the
// shipped bodies still guarantee is the ORDER of the slots, which is also what
// the compiled .crs bytes are. So this reader is POSITIONAL:
//
//   * a SCHEMA per description class lists, per class level in Serialize
//     order, the kit's field name and the slot kind (int / uint / bool / float
//     / String / ExternalPtr / array of ExternalPtr / array of records). The
//     order and the meaning of each slot were transcribed from the ported
//     bodies (src/engine/ui/*description.cpp, dialog_vtable.cpp,
//     stringtable_factory.cpp) and pinned against the kit's own compiled
//     output for our three files (tools/ext_resources.sh, decoded with
//     tools/ext_font_check.py's LZF reader) on 2026-10-18: e.g. the four
//     ElementDescription pads are serialized _leftPad, _rightPad, _topPad,
//     _bottomPad (EnhancedOptions.rsc:enhancedOptions, 6/2/4/4), `_color`
//     is the +0x3c uint the header calls `_id`, LabelDescription's two uints
//     are _textAlignment then _labelPosition, ButtonDescription's `_content`
//     is the fifth of its five ExternalPtrs, ContainerDescription's the third
//     of three, a Tab is {_button, _content}.
//   * the READER keeps a cursor over that flat list and, at each slot the
//     object's own Serialize asks for, checks the slot KIND against the schema
//     (a transcription slip fails loudly, it never writes a wrong member),
//     then answers the text's value when the text names the field and leaves
//     the reference untouched otherwise. A String is two slots (uint length
//     with bit 31, then the chars), an ExternalPtr is a bool and, when
//     present, a String (the factory path; the engine's own
//     ExternalPtr::Serialize then FindOrLoads it, so a reference to an object
//     of the same file re-enters the provider), an array is an int count and
//     then its elements.
//
// DERIVATION. `T name : "base"` is: create a fresh T through the type table,
// FindOrLoad the base, copy it with the ported BinaryWriter into a memory
// stream and the BinaryReader out of it (exactly a load: every ExternalPtr is
// re-resolved through the factory), then run the text over the copy. An
// inherited ARRAY is REPLACED by the derived one (EXTRSC.md section 1): the
// text runs TWICE, a first pass that answers 0 for every array the text names
// (the engine's SetSize(0) destroys the inherited elements) and the real pass
// after it, whose SetSize(n) builds zeroed fresh elements. Nothing here
// touches a container, a String or an ExternalPtr directly.
//
// ENUMS. The identifier tables are text_enums.cpp's, each row with where its
// value comes from (the engine's own readers, the retail accessors, the kit's
// name tables in the profile DLL, or the compiled output). An identifier
// without a known value is an error, never a guess.
// ============================================================================

#include "ext/resources/text_reader.h"
#include "ext/resources/text_enums.h"

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <vector>

#include "io/Serializer.h"
#include "io/Stream.h"
#include "object/External.h"
#include "object/TypeInfo.h"
#include "system/Filename.h"
#include "system/String.h"
#include "system/string_internal.h"   // RtSystem::RtMemAlloc / RtMemFree (the codec's storage)

namespace Ext {
namespace RscText {

namespace {

// ---------------------------------------------------------------------------
// The schema
// ---------------------------------------------------------------------------
enum class K { Int, UInt, Bool, Float, String, Ext, ExtArray, Records };

struct FieldDesc {
    const char*      name;   // the kit's field name ("" = unnamed / not settable from text)
    K                kind;
    Enum             en;     // for UInt: the identifier set
    const FieldDesc* sub;    // Records: the element's fields
    int              subCount;
};

#define F(name, kind)            { name, K::kind, kNoEnum, nullptr, 0 }
#define FE(name, en)             { name, K::UInt, en, nullptr, 0 }
#define FR(name, sub)            { name, K::Records, kNoEnum, sub, static_cast<int>(sizeof(sub) / sizeof(sub[0])) }

// UI::ElementDescription::Serialize (elementdescription_serialize.cpp), version 0.
const FieldDesc kElement[] = {
    F("_leftPad", Int), F("_rightPad", Int), F("_topPad", Int), F("_bottomPad", Int),
    F("_minWidth", Int), F("_minHeight", Int),
    FE("_alignment", kAlignment),        // +0x40, the ctor's 3 (MidLeft)
    FE("_flags", kElementFlags),         // +0x44, OR'd into Element::_flags by CreateInstance
    F("_color", UInt),                   // +0x3c: the header's `_id`, copied to Element::_color
    F("_tabOrder", Int),
    F("_border", Ext),                   // +0x48 _childDesc
    F("_toolTipDialog", Ext),            // +0x50 the tool-tip Dialog (Dialog/ToolTip.rsc)
    F("_toolTipStringTable", Ext),       // +0x58
    F("_toolTipText", String),           // +0x60
};
// UI::TextDescription::Serialize, version 1 (the +0x8c tail is always read by a text encoding).
// The five tail slots, in stream order (member names of TextDescription.h and TextElement.h):
//   stream  description  member         element member   meaning
//   1st     +0x84        _b84           _drawShadow      black +1/+1 pass
//   2nd     +0x85        _b85           _wrap            word wrap on
//   3rd     +0x88        _wordWrapSize  _wordWrapSize    the width a wrapped text breaks at (default 200)
//   4th     +0x80        _u80           _justify         UI::Font::Justify
//   5th     +0x8c        _i8c           _maxWidth        -1: none; NOT the wrap width (shrinks the box)
// Both Int slots are the same kind, so swapping them reads fine and writes the wrap width into _maxWidth.
const FieldDesc kText[] = {
    F("_font", Ext), F("_stringTable", Ext), F("_text", String),
    F("", Bool),                 // 1st: _drawShadow (not set from text)
    F("_wordWrap", Bool),        // 2nd: _wrap
    F("_wordWrapSize", Int),     // 3rd: _wordWrapSize
    F("", UInt),                 // 4th: _justify (not set from text)
    F("", Int),                  // 5th: _maxWidth (not set from text)
};
// UI::LabelDescription::Serialize: the sprite pair, then the six dwords.
const FieldDesc kLabel[] = {
    F("_spriteSheet", Ext), F("_spriteName", String),
    FE("_textAlignment", kAlignment), FE("_labelPosition", kLabelPosition),
    F("", Int), F("_spacing", Int), F("", Int), F("", Int),
};
// UI::ButtonDescription::Serialize: five ExternalPtrs (four borders, then the content), four colours.
const FieldDesc kButton[] = {
    F("", Ext), F("", Ext), F("", Ext), F("", Ext), F("_content", Ext),
    F("", UInt), F("", UInt), F("", UInt), F("", UInt),
};
const FieldDesc kCheck[] = { F("_check", Ext) };   // the element drawn over the button while it is checked
// UI::EditDescription::Serialize (editdescription.cpp): the font, the starting text, the typed-character
// limit, the password mask, and the characters it accepts (empty: any).
const FieldDesc kEdit[] = { F("_font", Ext), F("_text", String), F("_maxLength", Int), F("", Bool),
                           F("_filter", String) };
const FieldDesc kCombo[] = { F("", Ext), F("", Ext), F("_initialData", Ext) };
const FieldDesc kTab[]   = { F("_button", Ext), F("_content", Ext) };
const FieldDesc kTabDesc[] = {
    F("", Ext), F("", Ext), F("", Ext), F("", Ext), F("", String), F("", String), F("", String),
    F("", Int), FR("_tabs", kTab),
};
const FieldDesc kRibbon[] = { F("_vertical", Bool), F("", Bool), F("_cellPad", Int), F("_elements", ExtArray) };
const FieldDesc kGroup[]  = { F("_elements", ExtArray) };
const FieldDesc kContainer[] = {
    F("_width", Int), F("_height", Int), F("_scrollSpacing", Int),   // +0x68 / +0x6c requested size, +0x74 the scroll bar's gap
    F("", Ext), F("", Ext), F("_content", Ext), F("", Bool), F("", Bool),
};
const FieldDesc kLayoutItem[] = {
    F("_element", Ext), F("_name", String), F("_xAnchorTo", String), F("_yAnchorTo", String),
    FE("_xLayout", kLayoutH), FE("_yLayout", kLayoutV), F("_xOffset", Int), F("_yOffset", Int), F("", Bool),
};
const FieldDesc kLayout[] = { FR("_elements", kLayoutItem) };
const FieldDesc kTableItem[] = { F("_x", Int), F("_y", Int), F("_element", Ext) };
const FieldDesc kTable[] = { F("_width", Int), F("_height", Int), F("_spacing", Int), FR("_items", kTableItem) };
const FieldDesc kImage[] = { F("_spriteSheet", Ext), F("_spriteName", String), F("", Int), F("", Int) };
const FieldDesc kSlider[] = {
    F("_number", Ext), F("", Ext), F("", String), F("", String), F("", String), F("", String), F("", String),
    F("", Int), F("", Int), F("", Float), F("", Float), F("", Int),
};
const FieldDesc kOverride[] = { F("_element", Ext), F("_name", String) };
const FieldDesc kController[] = { F("", UInt), F("", Ext), F("", Ext), F("", String) };
// UI::Dialog::Serialize (dialog_vtable.cpp), version 0.
const FieldDesc kDialog[] = {
    FE("_flags", kDialogFlags), F("_rootElement", Ext), F("_spriteSheet", Ext), F("_spriteName", String),
    F("_backgroundColor", UInt), FR("_override", kOverride), F("_templates", ExtArray),
    FR("", kController), F("_references", ExtArray), F("", Int),
};
const FieldDesc kEntry[] = { F("_name", String), F("_text", String) };
const FieldDesc kStringTable[] = { FR("_strings", kEntry) };

struct Level {
    const char*      type;
    const char*      parent;   // null at the root
    const FieldDesc* fields;
    int              count;
};
#define L(type, parent, arr) { type, parent, arr, static_cast<int>(sizeof(arr) / sizeof(arr[0])) }
const Level kLevels[] = {
    L("ElementDescription",   nullptr,              kElement),
    L("TextDescription",      "ElementDescription", kText),
    L("LabelDescription",     "TextDescription",    kLabel),
    L("ButtonDescription",    "ElementDescription", kButton),
    L("CheckDescription",     "ButtonDescription",  kCheck),
    L("ComboDescription",     "ButtonDescription",  kCombo),
    L("EditDescription",      "ElementDescription", kEdit),
    L("TabDescription",       "ElementDescription", kTabDesc),
    L("RibbonDescription",    "ElementDescription", kRibbon),
    L("GroupDescription",     "ElementDescription", kGroup),
    L("ContainerDescription", "ElementDescription", kContainer),
    L("LayoutDescription",    "ElementDescription", kLayout),
    L("TableDescription",     "ElementDescription", kTable),
    L("ImageDescription",     "ElementDescription", kImage),
    L("SliderDescription",    "ElementDescription", kSlider),
    L("Dialog",               nullptr,              kDialog),
    L("StringTable",          nullptr,              kStringTable),
};
#undef L
#undef F
#undef FE
#undef FR

const Level* FindLevel(const char* type) {
    for (const Level& l : kLevels)
        if (std::strcmp(l.type, type) == 0) return &l;
    return nullptr;
}

// The flat slot list of a concrete type: base levels first, as every
// Serialize body calls its base before its own Serializer.
bool Flatten(const char* type, std::vector<FieldDesc>& out) {
    const Level* l = FindLevel(type);
    if (l == nullptr) return false;
    if (l->parent != nullptr && !Flatten(l->parent, out)) return false;
    out.insert(out.end(), l->fields, l->fields + l->count);
    return true;
}

void Log(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void Log(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    std::fputs("[ExtRes] ", stderr);
    std::vfprintf(stderr, fmt, ap);
    std::fputc('\n', stderr);
    va_end(ap);
}

// ---------------------------------------------------------------------------
// The positional builder: one per object pass.
// ---------------------------------------------------------------------------
enum class Prim { Int, UInt, Bool, Float, Chars };

// The kit's text escape: `~n` is a newline in a compiled string (the three
// load notices; the compiled .crs holds 0x0a where the source has ~n). No
// other escape occurs in our files; a `~` before anything else stays as is.
std::string Unescape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '~' && i + 1 < s.size() && s[i + 1] == 'n') { out += '\n'; ++i; }
        else out += s[i];
    }
    return out;
}

const char* PrimName(Prim p) {
    switch (p) {
    case Prim::Int: return "int"; case Prim::UInt: return "uint"; case Prim::Bool: return "bool";
    case Prim::Float: return "float"; default: return "chars";
    }
}

struct Frame {
    const FieldDesc*  fields;
    int               count;
    int               idx;
    int               phase;
    const Block*      ovr;        // the text block overriding this frame's fields (null: none)
    bool              isExtElem;  // an ExtArray element: `fields` is the one implicit Ext slot
    const Token*      extTok;     // ... and its override, or null
    int               elem;
    int               elemCount;
    bool              extPresent;
    std::vector<char> used;       // per ovr->fields: consumed by a schema slot
};

const FieldDesc kExtElem = { "", K::Ext, kNoEnum, nullptr, 0 };

class Builder {
public:
    Builder(const std::vector<FieldDesc>& schema, const Block& body, const char* dir,
            const char* fileName, const char* what, bool clearArrays)
        : _dir(dir), _file(fileName), _what(what), _clearArrays(clearArrays) {
        _stack.reserve(16);
        Frame root{};
        root.fields = schema.data();
        root.count  = static_cast<int>(schema.size());
        root.ovr    = &body;
        root.used.assign(body.fields.size(), 0);
        _stack.push_back(root);
    }

    bool Failed() const { return _failed; }

    // Called when the object's Serialize has returned: every slot must be consumed.
    bool Finish() {
        if (_failed) return false;
        while (!_stack.empty()) {
            Frame& f = _stack.back();
            if (f.idx < f.count) {
                Fail("Serialize ended before the schema did (next slot '%s' #%d)", f.fields[f.idx].name, f.idx);
                return false;
            }
            if (!CheckUnused(f)) return false;
            _stack.pop_back();
            if (!_stack.empty()) {
                Frame& p = _stack.back();
                if (++p.elem < p.elemCount) { Fail("Serialize ended inside the array '%s'", p.fields[p.idx].name); return false; }
                p.idx++; p.phase = 0;
            }
        }
        return true;
    }

    void OnInt(int32_t& v)             { Step(Prim::Int, &v, 0); }
    void OnUInt(uint32_t& v)           { Step(Prim::UInt, &v, 0); }
    void OnBool(bool& v)               { Step(Prim::Bool, &v, 0); }
    void OnFloat(float& v)             { Step(Prim::Float, &v, 0); }
    void OnChars(int n, char* buf)     { Step(Prim::Chars, buf, n); }
    void OnOther(const char* slot)     { Fail("unexpected Serialize slot %s", slot); }

private:
    void Fail(const char* fmt, ...) __attribute__((format(printf, 2, 3))) {
        if (_failed) return;
        _failed = true;
        char msg[512];
        va_list ap;
        va_start(ap, fmt);
        std::vsnprintf(msg, sizeof msg, fmt, ap);
        va_end(ap);
        Log("%s: %s", _what, msg);
        // The slots the body asked for, in order: what a transcription slip looks like.
        std::string trace;
        for (size_t i = 0; i < _trace.size(); ++i) {
            char t[48];
            std::snprintf(t, sizeof t, "%s%s@%d", i ? " " : "", PrimName(_trace[i].prim), _trace[i].slot);
            trace += t;
        }
        Log("%s: slots asked: %s", _what, trace.c_str());
    }

    struct Asked { Prim prim; int slot; };
    std::vector<Asked> _trace;

    bool CheckUnused(const Frame& f) {
        if (f.ovr == nullptr) return true;
        for (size_t i = 0; i < f.ovr->fields.size(); ++i) {
            if (!f.used[i]) {
                Fail("line %d: the field '%s' is not one this type serializes (or is not settable from text)",
                     f.ovr->fields[i].line, f.ovr->fields[i].name.c_str());
                return false;
            }
        }
        return true;
    }

    // The text field overriding slot `d` of frame `f`, marked consumed.
    const Field* Override(Frame& f, const FieldDesc& d) {
        if (f.ovr == nullptr || d.name[0] == '\0') return nullptr;
        for (size_t i = 0; i < f.ovr->fields.size(); ++i) {
            if (f.ovr->fields[i].name == d.name) {
                f.used[i] = 1;
                return &f.ovr->fields[i];
            }
        }
        return nullptr;
    }

    void PushElement(Frame& parent) {
        const FieldDesc& d = parent.fields[parent.idx];
        const Field* o = parent.ovr ? parent.ovr->Find(d.name) : nullptr;
        if (d.name[0] == '\0') o = nullptr;
        Frame c{};
        if (d.kind == K::ExtArray) {
            c.fields = &kExtElem; c.count = 1; c.isExtElem = true;
            c.extTok = (o != nullptr && parent.elem < static_cast<int>(o->tokens.size())) ? &o->tokens[parent.elem] : nullptr;
        } else {
            c.fields = d.sub; c.count = d.subCount;
            if (o != nullptr && parent.elem < static_cast<int>(o->blocks.size())) {
                c.ovr = &o->blocks[parent.elem];
                c.used.assign(c.ovr->fields.size(), 0);
            }
        }
        _stack.push_back(c);
    }

    bool Expect(Prim got, Prim want, const FieldDesc& d, int slot) {
        if (got == want) return true;
        Fail("slot #%d ('%s') is a %s in the Serialize body but the schema says %s", slot, d.name,
             PrimName(got), PrimName(want));
        return false;
    }

    // One scalar token of the override; null (and a failure) when the shape is wrong.
    const Token* Scalar(const Field* o, const FieldDesc& d, bool quoted) {
        if (o == nullptr) return nullptr;
        if (o->isArray || o->tokens.size() != 1 || o->tokens[0].quoted != quoted) {
            Fail("line %d: '%s' wants %s value", o->line, d.name, quoted ? "one quoted" : "one bare");
            return nullptr;
        }
        return &o->tokens[0];
    }

    bool ParseUInt(const Field* o, const FieldDesc& d, uint32_t& out) {
        if (o->isArray || o->tokens.empty()) { Fail("line %d: '%s' wants a value", o->line, d.name); return false; }
        uint32_t acc = 0;
        for (const Token& t : o->tokens) {
            if (t.quoted) { Fail("line %d: '%s' wants a bare value", o->line, d.name); return false; }
            uint32_t v;
            const char* s = t.text.c_str();
            if ((s[0] >= '0' && s[0] <= '9')) {
                char* end = nullptr;
                v = static_cast<uint32_t>(std::strtoul(s, &end, 0));
                if (end == nullptr || *end != '\0') { Fail("line %d: '%s': bad number %s", o->line, d.name, s); return false; }
            } else if (d.en == kNoEnum || !LookupEnum(d.en, t.text, v)) {
                Fail("line %d: '%s': unknown identifier %s", o->line, d.name, s);
                return false;
            }
            acc |= v;
        }
        out = acc;
        return true;
    }

    // The chars slot of a String / path: the pending override copied in (the
    // engine sized the buffer from the length answered), or nothing when the
    // keep answer (zero length) was given.
    bool TakeChars(const FieldDesc& d, void* v, int count) {
        if (_pendingSet) {
            if (count != static_cast<int>(_pending.size() + 1)) {
                Fail("'%s': the chars slot has %d bytes, not %d", d.name, count, static_cast<int>(_pending.size() + 1));
                return false;
            }
            std::memcpy(v, _pending.c_str(), _pending.size() + 1);
            _pendingSet = false;
        } else if (count != 0) {
            Fail("'%s': a kept string asked for %d chars", d.name, count);
            return false;
        }
        return true;
    }

    void Step(Prim p, void* v, int count) {
        if (_failed) return;
        if (_trace.size() < 512) _trace.push_back(Asked{p, _stack.empty() ? -1 : _stack.back().idx});
        for (;;) {
            if (_stack.empty()) { Fail("a Serialize slot after the schema's last field"); return; }
            Frame& f = _stack.back();
            if (f.idx >= f.count) {
                if (!CheckUnused(f)) return;
                _stack.pop_back();
                if (_stack.empty()) { Fail("a Serialize slot after the schema's last field"); return; }
                Frame& parent = _stack.back();
                if (++parent.elem < parent.elemCount) PushElement(parent);
                else { parent.idx++; parent.phase = 0; }
                continue;
            }
            const FieldDesc& d = f.fields[f.idx];
            const int slot = f.idx;
            switch (d.kind) {
            case K::Int: {
                if (!Expect(p, Prim::Int, d, slot)) return;
                const Field* o = Override(f, d);
                if (const Token* t = Scalar(o, d, false)) {
                    char* end = nullptr;
                    // strtoll clamped to int32: what strtol gives on Win64 (32-bit long), on every host.
                    long long n = std::strtoll(t->text.c_str(), &end, 0);
                    n = n < INT32_MIN ? INT32_MIN : (n > INT32_MAX ? INT32_MAX : n);
                    if (end == nullptr || *end != '\0') { Fail("line %d: '%s': bad int %s", o->line, d.name, t->text.c_str()); return; }
                    if (!_clearArrays) *static_cast<int32_t*>(v) = static_cast<int32_t>(n);
                }
                f.idx++;
                return;
            }
            case K::UInt: {
                if (!Expect(p, Prim::UInt, d, slot)) return;
                if (const Field* o = Override(f, d)) {
                    uint32_t u;
                    if (!ParseUInt(o, d, u)) return;
                    if (!_clearArrays) *static_cast<uint32_t*>(v) = u;
                }
                f.idx++;
                return;
            }
            case K::Bool: {
                if (!Expect(p, Prim::Bool, d, slot)) return;
                if (const Token* t = Scalar(Override(f, d), d, false)) {
                    if (t->text != "true" && t->text != "false") { Fail("'%s': a bool is true or false", d.name); return; }
                    if (!_clearArrays) *static_cast<bool*>(v) = (t->text == "true");
                }
                f.idx++;
                return;
            }
            case K::Float: {
                if (!Expect(p, Prim::Float, d, slot)) return;
                if (const Token* t = Scalar(Override(f, d), d, false)) {
                    if (!_clearArrays) *static_cast<float*>(v) = std::strtof(t->text.c_str(), nullptr);
                }
                f.idx++;
                return;
            }
            case K::String: {
                // A String is NEVER left untouched: System::String::Serialize
                // re-allocates its buffer on the length answer, before the chars
                // slot, so "the current length" would hand back freed memory. The
                // keep answer is a ZERO length (bit 31 set): the engine then
                // skips SetLength and asks for zero chars, and the string stays.
                if (f.phase == 0) {
                    if (!Expect(p, Prim::UInt, d, slot)) return;
                    _pending.clear(); _pendingSet = false;
                    if (const Token* t = Scalar(Override(f, d), d, true)) {
                        if (!_clearArrays) { _pending = Unescape(t->text); _pendingSet = true; }
                    }
                    *static_cast<uint32_t*>(v) = _pendingSet
                        ? (static_cast<uint32_t>(_pending.size() + 1) | 0x80000000u) : 0x80000000u;
                    f.phase = 1;
                    return;
                }
                if (!Expect(p, Prim::Chars, d, slot)) return;
                if (!TakeChars(d, v, count)) return;
                f.idx++; f.phase = 0;
                return;
            }
            case K::Ext: {
                // The override: the frame's element token, or the named text field.
                const Token* tok = nullptr;
                if (f.isExtElem) tok = f.extTok;
                else if (const Field* o = Override(f, d)) {
                    if (o->isArray || o->tokens.size() != 1 || !o->tokens[0].quoted) { Fail("line %d: '%s' wants one quoted reference", o->line, d.name); return; }
                    tok = &o->tokens[0];
                }
                if (_clearArrays) tok = nullptr;
                if (f.phase == 0) {
                    if (!Expect(p, Prim::Bool, d, slot)) return;
                    if (tok != nullptr) *static_cast<bool*>(v) = true;
                    f.extPresent = *static_cast<bool*>(v);
                    _pending.clear(); _pendingSet = false;
                    if (tok != nullptr) { _pending = ResolveReference(tok->text, _dir, _file); _pendingSet = true; }
                    // An absent pointer is the one bool: the engine asks nothing
                    // more for it, so the slot is complete now (not at the next
                    // call, which may never come when it is the last slot).
                    if (!f.extPresent) { f.idx++; f.phase = 0; }
                    else f.phase = 1;
                    return;
                }
                if (f.phase == 1) {
                    // The path String: the same keep rule as K::String (a zero
                    // length leaves `path = cur->GetPath()` as the engine built it).
                    if (!Expect(p, Prim::UInt, d, slot)) return;
                    *static_cast<uint32_t*>(v) = _pendingSet
                        ? (static_cast<uint32_t>(_pending.size() + 1) | 0x80000000u) : 0x80000000u;
                    f.phase = 2;
                    return;
                }
                if (!Expect(p, Prim::Chars, d, slot)) return;
                if (!TakeChars(d, v, count)) return;
                f.idx++; f.phase = 0;
                return;
            }
            case K::ExtArray:
            case K::Records: {
                if (!Expect(p, Prim::Int, d, slot)) return;
                const Field* o = Override(f, d);
                if (o != nullptr) {
                    if (!o->isArray) { Fail("line %d: '%s' is an array", o->line, d.name); return; }
                    const bool blocks = (d.kind == K::Records);
                    if (blocks ? !o->tokens.empty() : !o->blocks.empty()) { Fail("line %d: '%s' wants %s", o->line, d.name, blocks ? "{ } items" : "quoted items"); return; }
                    const int n = static_cast<int>(blocks ? o->blocks.size() : o->tokens.size());
                    *static_cast<int32_t*>(v) = _clearArrays ? 0 : n;
                }
                f.elemCount = *static_cast<int32_t*>(v);
                f.elem = 0;
                if (f.elemCount > 0) { f.phase = 1; PushElement(f); }
                else { f.idx++; f.phase = 0; }
                return;
            }
            }
        }
    }

    const char*        _dir;
    const char*        _file;
    const char*        _what;
    bool               _clearArrays;
    bool               _failed = false;
    std::vector<Frame> _stack;
    std::string        _pending;
    bool               _pendingSet = false;
};

// ---------------------------------------------------------------------------
// The IO side: a Stream whose Encoding hands out TextReader codecs over one
// Builder. Nothing is read from the stream itself.
// ---------------------------------------------------------------------------
class TextReader final : public IO::ReaderWriter {
public:
    TextReader(IO::Serializer& ser, int version, Builder* b) : ReaderWriter(ser), _version(version), _b(b) {}
    ~TextReader() override {}
    int  GetVersion() const override { return _version; }
    void Skip() override {}
    void SerializeCharArray  (int n, char* a, const char*) override { _b->OnChars(n, a); }
    void SerializeBoolArray  (int, bool*, const char*) override { _b->OnOther("bool[]"); }
    void SerializeFloatArray (int, float*, const char*) override { _b->OnOther("float[]"); }
    void SerializeUInt64Array(int, uint64_t*, const char*) override { _b->OnOther("uint64[]"); }
    void SerializeInt64Array (int, int64_t*, const char*) override { _b->OnOther("int64[]"); }
    void SerializeUIntArray  (int, uint32_t*, const char*) override { _b->OnOther("uint[]"); }
    void SerializeIntArray   (int, int32_t*, const char*) override { _b->OnOther("int[]"); }
    void SerializeUShortArray(int, uint16_t*, const char*) override { _b->OnOther("ushort[]"); }
    void SerializeShortArray (int, int16_t*, const char*) override { _b->OnOther("short[]"); }
    void SerializeUCharArray (int, unsigned char*, const char*) override { _b->OnOther("uchar[]"); }
    void SerializeSCharArray (int, signed char*, const char*) override { _b->OnOther("schar[]"); }
    void SerializeChar  (char&, const char*) override { _b->OnOther("char"); }
    void SerializeBool  (bool& v, const char*) override { _b->OnBool(v); }
    void SerializeFloat (float& v, const char*) override { _b->OnFloat(v); }
    void SerializeUInt64(uint64_t&, const char*) override { _b->OnOther("uint64"); }
    void SerializeInt64 (int64_t&, const char*) override { _b->OnOther("int64"); }
    void SerializeUInt  (uint32_t& v, const char*) override { _b->OnUInt(v); }
    void SerializeInt   (int32_t& v, const char*) override { _b->OnInt(v); }
    void SerializeUShort(uint16_t&, const char*) override { _b->OnOther("ushort"); }
    void SerializeShort (int16_t&, const char*) override { _b->OnOther("short"); }
    void SerializeUChar (unsigned char&, const char*) override { _b->OnOther("uchar"); }
    void SerializeSChar (signed char&, const char*) override { _b->OnOther("schar"); }
    void SerializeBytes(int, void*, const char*) override { _b->OnOther("bytes"); }

private:
    int      _version;
    Builder* _b;
};

// IsBinary false: TextDescription's conditional tail is then always read, as
// the kit's text reader read it (the compiled output always carries it).
// GetKind 1 (text): never 3, the reference-visit kind ExternalPtr::Serialize
// answers without touching the stream.
class TextEncoding final : public IO::Encoding {
public:
    explicit TextEncoding(Builder* b) : _b(b) {}
    ~TextEncoding() = default;
    bool IsBinary() const override { return false; }
    bool IsText() const override { return true; }
    int  GetKind() const override { return 1; }
    IO::ReaderWriter* CreateReader(IO::Serializer& ser, int version) override {
        // ~Serializer deletes it through ReaderWriter::operator delete (MemFree).
        void* p = RtSystem::RtMemAlloc(sizeof(TextReader), 8);
        return p ? ::new (p) TextReader(ser, version, _b) : nullptr;
    }
    IO::ReaderWriter* CreateWriter(IO::Serializer&, int) override { return nullptr; }
private:
    Builder* _b;
};

class TextStream final : public IO::Stream {
public:
    explicit TextStream(Builder* b) {
        // ~Stream frees the encoding with RtMemFree, so it is allocated there.
        void* p = RtSystem::RtMemAlloc(sizeof(TextEncoding), 8);
        _encoding = p ? ::new (p) TextEncoding(b) : nullptr;
        _direction = IO::kReading;
    }
    ~TextStream() override {}
    bool     Write(int, const void*) override { return false; }
    bool     Read(int, void*) override { return false; }
    bool     SetCurrentPos(int) override { return true; }
    uint32_t GetCurrentPos() const override { return 0; }
    uint32_t GetSize() const override { return 0; }
};

bool RunTextPass(::Object::External* obj, const std::vector<FieldDesc>& schema, const Block& body,
                 const char* dir, const char* fileName, const char* what, bool clearArrays) {
    Builder b(schema, body, dir, fileName, what, clearArrays);
    {
        TextStream ts(&b);
        if (ts._encoding == nullptr) { Log("%s: out of memory", what); return false; }
        obj->Serialize(ts);
    }
    return b.Finish();
}

// The base copy: the ported BinaryWriter into a memory stream, the BinaryReader
// out of it into the fresh object (a load, ExternalPtrs re-resolved).
bool CopyBase(::Object::External* fresh, const ::Object::External* base, const char* what) {
    constexpr int kCapacity = 1 << 20;
    IO::MemoryStream ms;
    if (!ms.Create(kCapacity) || ms._data == nullptr) { Log("%s: out of memory for the base copy", what); return false; }
    const_cast<::Object::External*>(base)->Serialize(ms);
    if (ms._cursor >= static_cast<uint32_t>(kCapacity - 16)) { Log("%s: the base is larger than the copy buffer", what); return false; }
    ms._size = ms._cursor;
    ms._cursor = 0;
    ms._direction = IO::kReading;
    fresh->Serialize(ms);
    if (ms._cursor != ms._size) {
        Log("%s: the base copy read %u of %u bytes (the two Serialize bodies disagree)", what, ms._cursor, ms._size);
        return false;
    }
    return true;
}

}  // namespace

bool KnowsType(const char* typeName) {
    return FindLevel(typeName) != nullptr;
}

::Object::External* BuildObject(const File& file, const Object& obj, const char* dir, const char* fileName) {
    (void)file;
    const std::string what = std::string(dir) + fileName + ":" + obj.name;

    std::vector<FieldDesc> schema;
    if (!Flatten(obj.type.c_str(), schema)) {
        Log("%s: no schema for the type %s", what.c_str(), obj.type.c_str());
        return nullptr;
    }
    ::Object::Factory* const factory = ::Object::Factory::_singleton;
    ::Object::ExternalFactory* const externals = ::Object::ExternalFactory::_singleton;
    if (factory == nullptr || externals == nullptr) { Log("%s: no factory yet", what.c_str()); return nullptr; }
    if (externals->_referenceModeEnabled) { Log("%s: asked while the factory is in reference mode", what.c_str()); return nullptr; }

    const uint32_t typeId = factory->GetTypeIdFromName(obj.type.c_str(), false);
    if (typeId == 0) { Log("%s: the type %s is not registered", what.c_str(), obj.type.c_str()); return nullptr; }

    ::Object::Base* const created = factory->CreateObject(typeId);
    if (!::Object::IsDerivedFromNullSafe(created, ::Object::External::TypeId())) {
        Log("%s: %s did not create an External", what.c_str(), obj.type.c_str());
        if (created) delete created;
        return nullptr;
    }
    ::Object::External* fresh = static_cast<::Object::External*>(created);

    bool ok = true;
    if (!obj.base.empty()) {
        const std::string basePath = ResolveReference(obj.base, dir, fileName);
        const ::Object::External* base = externals->FindOrLoadAs<::Object::External>(System::Filename(basePath.c_str()));
        if (base == nullptr) {
            Log("%s: the base %s did not load", what.c_str(), basePath.c_str());
            ok = false;
        } else if (base->Type() != typeId) {
            Log("%s: the base %s is a %s, not a %s", what.c_str(), basePath.c_str(), base->TypeName(), obj.type.c_str());
            ok = false;
        } else {
            ok = CopyBase(fresh, base, what.c_str());
        }
    }
    // The text, twice: the inherited arrays the text names are emptied first
    // (REPLACE, not append), then every named field is applied.
    if (ok) ok = RunTextPass(fresh, schema, obj.body, dir, fileName, what.c_str(), true);
    if (ok) ok = RunTextPass(fresh, schema, obj.body, dir, fileName, what.c_str(), false);
    if (!ok) {
        delete fresh;
        return nullptr;
    }
    fresh->_path = System::Filename(what.c_str());
    return fresh;
}

}  // namespace RscText
}  // namespace Ext
