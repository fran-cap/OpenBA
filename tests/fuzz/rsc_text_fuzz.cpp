// ============================================================================
// tests/fuzz/rsc_text_fuzz.cpp -- Ext::RscText::Parse + ResolveReference over arbitrary text
// ============================================================================
// P0_MAP surface rank 13 (the .rsc text dialect of src/ext/resources/text_parser.cpp). The parser
// only reads the files embedded in the exe (tools/ext_rsc_embed.py), so exposure is low; the target
// checks memory safety, the recursion depth of nested `[ { ... } ]` blocks, and that every parsed
// base reference resolves. libFuzzer hands the text in a heap buffer of exactly `size` bytes.
// ============================================================================
#include "ext/resources/text_parser.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace {

void Walk(const Ext::RscText::Block& b, size_t& n) {
    for (const auto& f : b.fields) {
        n += f.tokens.size();
        (void)b.Find(f.name.c_str());
        for (const auto& sub : f.blocks)
            Walk(sub, n);
    }
}

void Resolve(const std::string& ref) {
    (void)Ext::RscText::ResolveReference(ref, "Dialog/", "Dialog/Fuzz.rsc");
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size > 0x40000)
        return 0;
    Ext::RscText::File file;
    std::string err;
    Ext::RscText::Parse(reinterpret_cast<const char*>(data), size, file, err);
    size_t n = 0;
    for (const auto& o : file.objects) {
        (void)file.Find(o.name.c_str());
        Walk(o.body, n);
        if (!o.base.empty())
            Resolve(o.base);
        for (const auto& f : o.body.fields)
            for (const auto& t : f.tokens)
                if (t.quoted)
                    Resolve(t.text);
    }
    return 0;
}
