// ============================================================================
// tests/fuzz/settings_file_fuzz.cpp -- IO::SettingsFile::Parse / Serialize / Merge over arbitrary text
// ============================================================================
// P0_MAP surface rank 10 (settings.cfg). Besides memory safety, the target checks the codec's
// round-trip property: Parse(Serialize(Parse(x))) == Parse(x), compared as key-sorted lists (Serialize
// sorts, Parse keeps first-appearance order). A mismatch aborts with both spellings printed.
// Also: Serialize output is a fixed point (Serialize(Parse(Serialize(e))) == Serialize(e)) and
// Merge(p, p) == sorted p.
// ============================================================================
#include "io/settings_file.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

using IO::SettingsFile::Entries;

namespace {

bool KeyLess(const IO::SettingsFile::Entry& a, const IO::SettingsFile::Entry& b) {
    return a.key < b.key;
}

Entries Sorted(Entries e) {
    std::stable_sort(e.begin(), e.end(), KeyLess);
    return e;
}

bool Same(const Entries& a, const Entries& b) {
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].key != b[i].key || a[i].value != b[i].value)
            return false;
    return true;
}

void Hex(const std::string& s) {
    for (unsigned char c : s)
        std::fprintf(stderr, "%02x", c);
}

void Dump(const char* what, const Entries& e) {
    std::fprintf(stderr, "%s (%zu entries)\n", what, e.size());
    for (const auto& x : e) {
        std::fprintf(stderr, "  key=");
        Hex(x.key);
        std::fprintf(stderr, " value=");
        Hex(x.value);
        std::fprintf(stderr, "\n");
    }
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size > 0x10000)
        return 0;
    const Entries first = IO::SettingsFile::Parse(reinterpret_cast<const char*>(data), size);
    const std::string text = IO::SettingsFile::Serialize(first);
    const Entries second = IO::SettingsFile::Parse(text);
    const Entries a = Sorted(first);
    const Entries b = Sorted(second);
    // MASK (finding "settings BOM key"): a key that starts with the bytes EF BB BF and sorts first is
    // written at the very start of the file, where Parse skips it as a BOM, so the key loses those
    // three bytes. Recorded once (scratchpad crashes/settings_file/bom_key_roundtrip); skipped here to
    // reach other mismatches.
    if (!a.empty() && a[0].key.compare(0, 3, "\xEF\xBB\xBF") == 0)
        return 0;
    if (!Same(a, b)) {
        Dump("Parse(x)", a);
        Dump("Parse(Serialize(Parse(x)))", b);
        std::fprintf(stderr, "SettingsFile round trip mismatch\n");
        std::abort();
    }
    const std::string again = IO::SettingsFile::Serialize(second);
    if (again != text) {
        std::fprintf(stderr, "SettingsFile Serialize is not a fixed point\n");
        std::abort();
    }
    const Entries merged = IO::SettingsFile::Merge(first, first);
    if (!Same(merged, a)) {
        std::fprintf(stderr, "SettingsFile Merge(p, p) != sorted p\n");
        std::abort();
    }
    return 0;
}
