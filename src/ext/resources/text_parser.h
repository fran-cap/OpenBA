#pragma once
// ============================================================================
// src/ext/resources/text_parser.h -- our .rsc text dialect, parsed into a tree
// ============================================================================
// NOT RETAIL (docs/extensions/KITFREE.md, block RSCTEXT). The files under
// src/ext/ui/Dialog/ (embedded into the exe by tools/ext_rsc_embed.py) use
// exactly this much of the kit's resource syntax, and nothing more:
//
//   Type name                     { fields }        a top-level object
//   Type name : "File.rsc:obj"    { fields }        derived from another file's object
//   Type name : "File.rsc"        { fields }        derived from that file's unnamed object
//   Type name : "localObj"        { fields }        derived from an object of this file
//   Type _field = value;                            a scalar: int, hex, float, bool,
//                                                   "string", Ident, Ident | Ident
//   Type _field [ item item ... ]                   an array of "strings" / idents, or of
//                                                   { fields } blocks
//   // comment                                      to the end of the line
//
// The parser knows nothing about the engine: it keeps every value as text (the
// builder in text_reader.cpp gives it a type from the class's Serialize order).
// Pure, host-testable (tests/unit/ext_rsc_text_test.cpp).
// ============================================================================

#include <cstddef>
#include <string>
#include <vector>

namespace Ext {
namespace RscText {

struct Token {
    std::string text;      // without the quotes when `quoted`
    bool        quoted;    // "..." vs a bare identifier / number
};

struct Block;

struct Field {
    std::string        type;      // the declared type word ("int", "String", "ElementDescription", ...)
    std::string        name;      // "_minWidth"
    bool               isArray;   // `name [ ... ]` rather than `name = ...;`
    std::vector<Token> tokens;    // scalar: the value's tokens (an enum OR has several);
                                  // array of scalars: one per item
    std::vector<Block> blocks;    // array of { ... } blocks: one per item
    int                line;      // the source line, for messages
};

struct Block {
    std::vector<Field> fields;

    // The field named `name`, or null.
    const Field* Find(const char* name) const;
};

struct Object {
    std::string type;    // "LabelDescription"
    std::string name;    // "labelTitle" ("resource" for the unnamed top-level object)
    std::string base;    // the quoted base reference, "" when none
    Block       body;
    int         line;
};

struct File {
    std::vector<Object> objects;

    // The object named `name`, or null (top-level objects only; names are unique).
    const Object* Find(const char* name) const;
};

// Parses `len` bytes of text. False = a syntax error, described in `error`
// ("line N: ..."); `out` then holds what was parsed before it.
bool Parse(const char* text, size_t len, File& out, std::string& error);

// Turns a reference as written in our text ("SharedElements.rsc:dialogBorder",
// "localObj", "Font/FontSmall.rsc", "Font\FontSmall.rsc",
// "EnhancedOptions.rsc:aoStrings") into the full "Dir/File.rsc:name" path the
// factory keys by: a reference without ".rsc" names an object of this file, a
// file without a directory sits beside this file (`dir`, e.g. "Dialog/"), a
// reference without ":name" is the file's unnamed object, "resource".
std::string ResolveReference(const std::string& ref, const char* dir, const char* fileName);

}  // namespace RscText
}  // namespace Ext
