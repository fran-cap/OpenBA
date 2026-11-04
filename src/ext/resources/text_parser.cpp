// ============================================================================
// src/ext/resources/text_parser.cpp -- our .rsc text dialect (text_parser.h)
// ============================================================================
// NOT RETAIL. A hand-written recursive-descent reader over a tiny tokenizer:
// identifiers (letters, digits, '_', '.', ':' and a leading '-' / "0x" for
// numbers), quoted strings (no escapes: a backslash inside the quotes is a path
// delimiter, as in `"Font\FontSmall.rsc"`), the punctuation = ; : | { } [ ],
// and `//` comments. Anything else is a syntax error with its line number.
// ============================================================================

#include "ext/resources/text_parser.h"

#include <cstring>

namespace Ext {
namespace RscText {

const Field* Block::Find(const char* name) const {
    for (const Field& f : fields)
        if (f.name == name) return &f;
    return nullptr;
}

const Object* File::Find(const char* name) const {
    for (const Object& o : objects)
        if (o.name == name) return &o;
    return nullptr;
}

namespace {

enum class T { End, Ident, String, Punct };

struct Tok {
    T           kind;
    std::string text;
    int         line;
};

bool IsIdentStart(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' || c == '-' ||
           (c >= '0' && c <= '9');
}
bool IsIdentChar(char c) {
    return IsIdentStart(c) || c == '.';
}

class Lexer {
public:
    Lexer(const char* s, size_t n) : _s(s), _n(n) {}

    bool Next(Tok& t, std::string& err) {
        for (;;) {
            while (_i < _n && (_s[_i] == ' ' || _s[_i] == '\t' || _s[_i] == '\r' || _s[_i] == '\n')) {
                if (_s[_i] == '\n') ++_line;
                ++_i;
            }
            if (_i + 1 < _n && _s[_i] == '/' && _s[_i + 1] == '/') {
                while (_i < _n && _s[_i] != '\n') ++_i;
                continue;
            }
            break;
        }
        t.line = _line;
        if (_i >= _n) { t.kind = T::End; t.text.clear(); return true; }
        const char c = _s[_i];
        if (c == '"') {
            ++_i;
            const size_t start = _i;
            while (_i < _n && _s[_i] != '"' && _s[_i] != '\n') ++_i;
            if (_i >= _n || _s[_i] != '"') { err = Line() + "unterminated string"; return false; }
            t.kind = T::String;
            t.text.assign(_s + start, _i - start);
            ++_i;
            return true;
        }
        if (std::strchr("=;:|{}[]", c) != nullptr) {
            t.kind = T::Punct;
            t.text.assign(1, c);
            ++_i;
            return true;
        }
        if (IsIdentStart(c)) {
            const size_t start = _i;
            while (_i < _n && IsIdentChar(_s[_i])) ++_i;
            t.kind = T::Ident;
            t.text.assign(_s + start, _i - start);
            return true;
        }
        err = Line() + "unexpected character '" + std::string(1, c) + "'";
        return false;
    }

    std::string Line() const { return "line " + std::to_string(_line) + ": "; }

private:
    const char* _s;
    size_t      _n;
    size_t      _i = 0;
    int         _line = 1;
};

class Parser {
public:
    Parser(const char* s, size_t n) : _lex(s, n) {}

    bool Run(File& out, std::string& err) {
        if (!Advance(err)) return false;
        while (_tok.kind != T::End) {
            Object o;
            if (!ParseObject(o, err)) return false;
            out.objects.push_back(o);
        }
        return true;
    }

private:
    bool Advance(std::string& err) { return _lex.Next(_tok, err); }

    std::string Where() const { return "line " + std::to_string(_tok.line) + ": "; }

    bool Expect(T kind, const char* text, std::string& err) {
        if (_tok.kind != kind || (text != nullptr && _tok.text != text)) {
            err = Where() + "expected " + (text ? std::string("'") + text + "'" : "a word") +
                  ", got '" + _tok.text + "'";
            return false;
        }
        return Advance(err);
    }

    bool ParseObject(Object& o, std::string& err) {
        o.line = _tok.line;
        if (_tok.kind != T::Ident) { err = Where() + "expected an object type"; return false; }
        o.type = _tok.text;
        if (!Advance(err)) return false;
        if (_tok.kind != T::Ident) { err = Where() + "expected an object name after " + o.type; return false; }
        o.name = _tok.text;
        if (!Advance(err)) return false;
        if (_tok.kind == T::Punct && _tok.text == ":") {
            if (!Advance(err)) return false;
            if (_tok.kind != T::String) { err = Where() + "expected the base reference in quotes"; return false; }
            o.base = _tok.text;
            if (!Advance(err)) return false;
        }
        return ParseBlock(o.body, err);
    }

    // Hardening (review F-038): ParseBlock -> ParseField -> ParseBlock recurses once per
    // `T f[{` level, 5 bytes of text each, so unbounded nesting overflowed the stack.
    // The embedded files nest blocks at most 2 deep; anything past kMaxDepth is refused.
    static constexpr int kMaxDepth = 64;

    bool ParseBlock(Block& b, std::string& err) {
        if (_depth >= kMaxDepth) {
            err = Where() + "blocks nested too deep";
            return false;
        }
        if (!Expect(T::Punct, "{", err)) return false;
        ++_depth;
        while (!(_tok.kind == T::Punct && _tok.text == "}")) {
            if (_tok.kind == T::End) { err = Where() + "unexpected end of file inside a block"; return false; }
            Field f;
            if (!ParseField(f, err)) return false;
            b.fields.push_back(f);
        }
        --_depth;
        return Advance(err);   // over the '}'
    }

    bool ParseField(Field& f, std::string& err) {
        f.line = _tok.line;
        f.isArray = false;
        if (_tok.kind != T::Ident) { err = Where() + "expected a field type"; return false; }
        f.type = _tok.text;
        if (!Advance(err)) return false;
        if (_tok.kind != T::Ident) { err = Where() + "expected a field name after " + f.type; return false; }
        f.name = _tok.text;
        if (!Advance(err)) return false;

        if (_tok.kind == T::Punct && _tok.text == "=") {
            if (!Advance(err)) return false;
            // value: one string, or identifiers / numbers joined by '|'
            for (;;) {
                if (_tok.kind == T::String)      f.tokens.push_back(Token{_tok.text, true});
                else if (_tok.kind == T::Ident)  f.tokens.push_back(Token{_tok.text, false});
                else { err = Where() + "expected a value for " + f.name; return false; }
                if (!Advance(err)) return false;
                if (_tok.kind == T::Punct && _tok.text == "|") {
                    if (!Advance(err)) return false;
                    continue;
                }
                break;
            }
            return Expect(T::Punct, ";", err);
        }
        if (_tok.kind == T::Punct && _tok.text == "[") {
            f.isArray = true;
            if (!Advance(err)) return false;
            while (!(_tok.kind == T::Punct && _tok.text == "]")) {
                if (_tok.kind == T::String)      { f.tokens.push_back(Token{_tok.text, true});  if (!Advance(err)) return false; }
                else if (_tok.kind == T::Ident)  { f.tokens.push_back(Token{_tok.text, false}); if (!Advance(err)) return false; }
                else if (_tok.kind == T::Punct && _tok.text == "{") {
                    Block b;
                    if (!ParseBlock(b, err)) return false;
                    f.blocks.push_back(b);
                } else { err = Where() + "unexpected '" + _tok.text + "' in the array " + f.name; return false; }
            }
            if (!f.tokens.empty() && !f.blocks.empty()) {
                err = Where() + "the array " + f.name + " mixes values and blocks";
                return false;
            }
            return Advance(err);   // over the ']'
        }
        err = Where() + "expected '=' or '[' after " + f.name;
        return false;
    }

    Lexer _lex;
    Tok   _tok{T::End, "", 1};
    int   _depth = 0;   // blocks open (F-038)
};

}  // namespace

bool Parse(const char* text, size_t len, File& out, std::string& error) {
    error.clear();
    Parser p(text, len);
    return p.Run(out, error);
}

std::string ResolveReference(const std::string& ref, const char* dir, const char* fileName) {
    std::string file, name;
    const size_t colon = ref.find(':');
    if (colon != std::string::npos) { file = ref.substr(0, colon); name = ref.substr(colon + 1); }
    else                            { file = ref; name = "resource"; }
    if (file.find(".rsc") == std::string::npos) {   // a local object of this file
        name = ref;
        file = fileName;
    }
    for (char& c : file) if (c == '\\') c = '/';
    if (file.find('/') == std::string::npos) file = std::string(dir) + file;   // beside this file
    return file + ":" + name;
}

}  // namespace RscText
}  // namespace Ext
