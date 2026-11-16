// ============================================================================
// tests/unit/filename_adjust_test.cpp -- block SAVELOAD's exactness proof for
//   Filename::AdjustDelimiters (and the header-inline String::GetLength)
// ============================================================================
// `OldAdjust` below is the pre-SAVELOAD body of AdjustDelimiters, verbatim but
// for spelling `this` as `f` (the length re-read per character, no fast exit).
// It lives here only, never in src/.
// Each test feeds the same raw characters to both and compares the result:
//   * every string of length 0..8 over { a / \ . : } (488,281 strings), which
//     covers every arrangement of delimiters, "..", splices at every position,
//     the "\.." tail case and the stale-`lastDelim` re-scan;
//   * the game's resource names (data/resource_names.inc), raw, under a data
//     directory, with "../" segments spliced in, and as compiled ".crs" paths.
// Also: AdjustDelimiters is idempotent on its own output (the memo's argument,
// src/engine/object/expand_memo.h, relies on it), on the same corpus.
// ============================================================================
#include "unit.h"
#include "system/Filename.h"

#include <string>
#include <vector>

using System::Filename;
using System::String;

namespace {

// The pre-SAVELOAD AdjustDelimiters (src/engine/system/filename.cpp at b55d48a65).
void OldAdjust(Filename& f) {
    {
        int i = 0;
        int64_t k = 0;
        int len = f.AsString().GetLength();
        if (len > 0) {
            do {
                if (f._data[k] == '/')
                    f._data[k] = '\\';
                ++i;
                ++k;
                len = f.AsString().GetLength();
            } while (i < len);
        }
    }

    int lastDelim = -1;

    for (;;) {
        int i = 0;
        int64_t k = 0;
        if (f.AsString().GetLength() - 2 <= 0)
            return;

        const char* base = f._data;
        bool collapse = false;

        for (;;) {
            bool isDelim = false;
            if (base[k] == '\\') {
                if (base[k + 1] == '.' && base[k + 2] == '.' && lastDelim >= 1) {
                    if (i + 3 < f.AsString().GetLength() - 1) {
                        collapse = true;
                        break;
                    }
                }
                isDelim = true;
            }
            if (isDelim)
                lastDelim = i;
            ++i;
            ++k;
            if (i >= f.AsString().GetLength() - 2)
                return;
        }
        if (!collapse)
            return;

        String tail    = f.AsString().SubString(i + 3, f.AsString().GetLength() - 1);
        String head    = f.AsString().SubString(0, lastDelim - 1);
        String spliced = head + tail;
        f.AsString() = spliced;
        OldAdjust(f);
    }
}

// A Filename holding `raw` exactly, NOT normalised: String::operator= adopts
// without AdjustDelimiters.
Filename Raw(const std::string& raw) {
    Filename f;
    f.AsString() = String(raw.c_str());
    return f;
}

std::string Text(const Filename& f) {
    return f._data ? std::string(f._data, static_cast<size_t>(f.GetLength())) : std::string("(null)");
}

// Old vs new on one input; returns false (and reports the first few) on a difference.
int g_reported = 0;
bool Same(const std::string& raw) {
    Filename a = Raw(raw);
    Filename b = Raw(raw);
    OldAdjust(a);
    System::FilenamePrivateAccess::AdjustDelimiters(b);
    const bool same = (Text(a) == Text(b)) && (a.GetLength() == b.GetLength());
    if (!same && g_reported++ < 5)
        std::printf("    old \"%s\" vs new \"%s\" for \"%s\"\n", Text(a).c_str(), Text(b).c_str(),
                    raw.c_str());
    return same;
}

bool Idempotent(const std::string& raw) {
    Filename a = Raw(raw);
    System::FilenamePrivateAccess::AdjustDelimiters(a);
    const std::string once = Text(a);
    System::FilenamePrivateAccess::AdjustDelimiters(a);
    const bool same = (Text(a) == once);
    if (!same && g_reported++ < 5)
        std::printf("    not idempotent: \"%s\" -> \"%s\" -> \"%s\"\n", raw.c_str(), once.c_str(),
                    Text(a).c_str());
    return same;
}

template <class Fn>
int ForEachShortString(int maxLen, Fn fn) {
    static const char kAlphabet[] = { 'a', '/', '\\', '.', ':' };
    int failures = 0;
    std::string s;
    for (int len = 0; len <= maxLen; ++len) {
        std::vector<int> digit(static_cast<size_t>(len), 0);
        for (;;) {
            s.assign(static_cast<size_t>(len), 'a');
            for (int i = 0; i < len; ++i) s[static_cast<size_t>(i)] = kAlphabet[digit[static_cast<size_t>(i)]];
            if (!fn(s)) ++failures;
            int p = 0;
            while (p < len && ++digit[static_cast<size_t>(p)] == 5) { digit[static_cast<size_t>(p)] = 0; ++p; }
            if (p == len) break;
        }
    }
    return failures;
}

const char* const kNames[] = {
#include "data/resource_names.inc"
};

// The names as the loader meets them: raw, absolute under a data directory,
// with ".." segments, and as the compiled path FindOrLoad probes.
std::vector<std::string> NameCorpus() {
    std::vector<std::string> out;
    for (const char* n : kNames) {
        const std::string s(n);
        out.push_back(s);
        out.push_back("C:\\GOG Games\\Banished\\WinData\\" + s);
        out.push_back("C:/GOG Games/Banished/WinData/" + s);
        out.push_back("C:\\GOG Games\\Banished\\WinData\\Game\\..\\" + s);
        out.push_back("Template/../" + s);
        out.push_back("a\\b\\..\\..\\" + s);
        out.push_back(s + "\\..");
        out.push_back(s + "\\..\\x");
        const size_t colon = s.find(':');
        const std::string stem = s.substr(0, s.rfind('.', colon));
        out.push_back("C:\\GOG Games\\Banished\\WinData\\" + stem + "_resource.crs");
    }
    return out;
}

}  // namespace

UNIT_TEST(filename_adjust_matches_old_body_on_every_short_string) {
    g_reported = 0;
    CHECK_EQ(ForEachShortString(8, Same), 0);
}

UNIT_TEST(filename_adjust_matches_old_body_on_resource_names) {
    g_reported = 0;
    int failures = 0;
    for (const std::string& s : NameCorpus())
        if (!Same(s)) ++failures;
    CHECK_EQ(failures, 0);
    CHECK(sizeof(kNames) / sizeof(kNames[0]) > 100);
}

UNIT_TEST(filename_adjust_is_idempotent) {
    g_reported = 0;
    CHECK_EQ(ForEachShortString(8, Idempotent), 0);
    int failures = 0;
    for (const std::string& s : NameCorpus())
        if (!Idempotent(s)) ++failures;
    CHECK_EQ(failures, 0);
}

UNIT_TEST(filename_adjust_writes_through_the_shared_buffer) {
    // Phase 1 still rewrites the SHARED characters in place (retail's
    // behaviour, filename_test.cpp's from_string test), and a path with nothing
    // to do keeps its buffer.
    String s("Template/Tree.rsc:description");
    Filename f(s);
    CHECK(f._data == s._data);
    CHECK_STREQ(s._data, "Template\\Tree.rsc:description");
    char* const before = f._data;
    System::FilenamePrivateAccess::AdjustDelimiters(f);
    CHECK(f._data == before);
}

UNIT_TEST(string_getlength_inline_reads_the_header) {
    String n;
    CHECK_EQ(n.GetLength(), 0);
    String e("");
    CHECK_EQ(e.GetLength(), 0);
    String s("Material/Tree.rsc");
    CHECK_EQ(s.GetLength(), 17);
    CHECK_EQ(s.GetLength(), RtSystem::StringLength(s._data));
}
