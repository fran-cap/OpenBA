// src/ext/renamed_mods/renamed_mods_math.h -- which root list a package really has (NOT RETAIL).
// Standard C++ only (unit-tested).
//
// docs/extensions/handins/RENAMEDMODS.md. The game finds a mod's content list by the NAME OF ITS .pkm
// FILE: for MegaMod.pkm it asks for `MegaModResources.rsc:resource`, which the package holds as the
// root-level entry `MegaModResources_resource.crs`. Rename the file and the name no longer matches,
// the list is never loaded and the mod's new content never appears, with no message (the package is
// still mounted, so its files that replace the game's own apply). A package has one such
// root-level list of its own (every package seen has exactly one), so a renamed mod can be loaded by
// the name the package carries.
#pragma once

#include <cctype>
#include <string>
#include <vector>

namespace Ext {
namespace RenamedMods {

enum class Kind {
    Named,       // the package holds the list the file name asks for: nothing to do
    Found,       // it does not, but it holds one root-level list of its own (or the .index names one)
    None,        // no root-level `...Resources_resource.crs` at all
    Ambiguous,   // several, and the package's .index does not pick one
};

struct Choice {
    Kind kind = Kind::None;
    // Found / Named: the name before "Resources_resource.crs" (`ChooChoo`); the list is
    // `<prefix>Resources.rsc:resource`.
    std::string prefix;
    int candidates = 0;      // how many root-level lists the package holds
};

namespace detail {

inline std::string Lower(const std::string& s) {
    std::string out = s;
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

inline bool EndsWith(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

inline bool IsRootLevel(const std::string& entry) {
    return entry.find('\\') == std::string::npos && entry.find('/') == std::string::npos;
}

}  // namespace detail

// `stem`: the .pkm file's name without its extension. `entries`: the package's entry names (any
// case; names with a directory are ignored).
inline Choice ChooseRoot(const std::string& stem, const std::vector<std::string>& entries) {
    static const std::string kList = "resources_resource.crs";
    static const std::string kIndex = ".index";
    Choice out;
    // the root-level lists as the part before "Resources_resource.crs", in the package's spelling
    std::vector<std::string> prefixes;
    // the package's own name(s), from its `<name>.index` entries
    std::vector<std::string> indexNames;
    for (const std::string& e : entries) {
        if (!detail::IsRootLevel(e)) {
            continue;
        }
        const std::string low = detail::Lower(e);
        if (detail::EndsWith(low, kList) && e.size() > kList.size()) {
            prefixes.push_back(e.substr(0, e.size() - kList.size()));
        } else if (detail::EndsWith(low, kIndex) && e.size() > kIndex.size()) {
            indexNames.push_back(e.substr(0, e.size() - kIndex.size()));
        }
    }
    out.candidates = static_cast<int>(prefixes.size());
    const std::string want = detail::Lower(stem);
    for (const std::string& p : prefixes) {
        if (detail::Lower(p) == want) {
            out.kind = Kind::Named;
            out.prefix = p;
            return out;
        }
    }
    if (prefixes.size() == 1) {
        out.kind = Kind::Found;
        out.prefix = prefixes[0];
        return out;
    }
    if (prefixes.empty()) {
        out.kind = Kind::None;
        return out;
    }
    // several root lists: the package's own name (its .index entry) picks the one that is its own
    if (indexNames.size() == 1) {
        const std::string own = detail::Lower(indexNames[0]);
        const std::string* hit = nullptr;
        int hits = 0;
        for (const std::string& p : prefixes) {
            if (detail::Lower(p) == own) {
                hit = &p;
                ++hits;
            }
        }
        if (hits == 1) {
            out.kind = Kind::Found;
            out.prefix = *hit;
            return out;
        }
    }
    out.kind = Kind::Ambiguous;
    return out;
}

}  // namespace RenamedMods
}  // namespace Ext
