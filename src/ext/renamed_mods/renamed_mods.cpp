// ============================================================================
// src/ext/renamed_mods/renamed_mods.cpp -- NOT RETAIL. ext.renamed_mods
// ============================================================================
// Class B (docs/extensions/handins/RENAMEDMODS.md); renamed_mods_math.h has the choice and its
// tests. The hook runs on the LOADER THREAD, inside Engine::UpdateLoop::LoadResources, once per
// enabled mod whose list the file name does not find. It reads the mod's path set (filled by the
// mod scan, never changed while a loop loads). It does NOT read the row: Sync() reads it ONCE at boot
// and installs the hook only when the row is on, so a toggle (or the lock a loaded save takes) never
// changes which mods load their list after the boot.
// ============================================================================

#include "ext/renamed_mods/renamed_mods.h"

#include "ext/registry.h"
#include "ext/renamed_mods/renamed_mods_math.h"
#include "engine/UpdateLoop.h"
#include "io/Modification.h"
#include "system/Filename.h"
#include "system/String.h"

#include <cstdio>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace Ext {
namespace RenamedMods {

namespace {

// One log line per mod and outcome for the whole run (LoadResources runs once per loop that loads).
bool FirstTime(const std::string& key) {
    static std::mutex m;
    static std::set<std::string> seen;
    std::lock_guard<std::mutex> lock(m);
    return seen.insert(key).second;
}

// The package's entry names, from the mod's path set (an AVL tree walked in order).
std::vector<std::string> EntryNames(const RtIO::Mod& mod) {
    std::vector<std::string> out;
    std::vector<const RtIO::ModFilenameSet::Node*> stack;
    const RtIO::ModFilenameSet::Node* node = mod.set._root;
    while (node != nullptr || !stack.empty()) {
        while (node != nullptr) {
            stack.push_back(node);
            node = node->left;
        }
        node = stack.back();
        stack.pop_back();
        if (node->element._data != nullptr) {
            out.emplace_back(node->element._data);
        }
        node = node->right;
    }
    return out;
}

bool ModRoot(const RtIO::Mod& mod, System::String& spec) {
    const System::Filename leaf(mod.file.GetPathFilename(true));
    const std::string stem = leaf._data != nullptr ? leaf._data : "";
    if (stem.empty()) {
        return false;
    }
    const std::string file = stem + ".pkm";
    const Choice c = ChooseRoot(stem, EntryNames(mod));
    switch (c.kind) {
        case Kind::Found:
            if (FirstTime(stem + "|found")) {
                std::fprintf(stderr,
                             "[RenamedMods] %s: no %sResources.rsc in the package; loading its own list "
                             "%sResources.rsc\n",
                             file.c_str(), stem.c_str(), c.prefix.c_str());
            }
            spec = System::String::Format("%sResources.rsc:resource", c.prefix.c_str());
            return true;
        case Kind::None:
            if (FirstTime(stem + "|none")) {
                std::fprintf(stderr,
                             "[RenamedMods] %s: no %sResources.rsc and no content list of its own at the "
                             "root of the package; nothing to load (a mod of replacement files has none)\n",
                             file.c_str(), stem.c_str());
            }
            return false;
        case Kind::Ambiguous:
            if (FirstTime(stem + "|ambiguous")) {
                std::fprintf(stderr,
                             "[RenamedMods] %s: no %sResources.rsc, and the package holds %d lists of its "
                             "own that its .index does not choose between: its new content is not loaded\n",
                             file.c_str(), stem.c_str(), c.candidates);
            }
            return false;
        case Kind::Named:
            break;   // the list is there; the engine's own test missed it for another reason
    }
    return false;
}

}  // namespace

void Sync() {
    const bool faithful = CurrentProfile() == Profile::Faithful;
    // The boot value, for the whole run: the seam is written here, before any loader thread starts, and
    // never again.
    Engine::UpdateLoop::_extModRoot = (!faithful && Ext().renamed_mods) ? &ModRoot : nullptr;
}

}  // namespace RenamedMods
}  // namespace Ext
