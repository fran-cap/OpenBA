// ============================================================================
// src/ext/better_trees/blossom_draw.cpp -- the per-draw blossom code (docs/extensions/BLOSSOM.md)
// ============================================================================
// NOT RETAIL. Graphics::MaterialInstance::_extAfterSetup while natural tree growth is on (Sync sets it):
// after every material Setup, gc.time.zw carries the draw's blossom code (blossom.h "THE SPECIES") when
// the material is a deciduous canopy, else 0 / 0 (Context::SetTime's own values, which no retail program
// reads). The global constants are marked dirty only when the pair changes, so a run of canopy draws of one
// species and a run of anything else cost one upload each.
//
// The species comes from the draw's names: the material's path (billboard, not evergreen), the material
// instance's own path and its textures' paths. The answer is kept per material instance, per thread (the
// render pipe draws on its own thread), with the material and first texture it was computed from, so a
// freed instance whose address is reused is recomputed. Each new name set is logged once to stderr.
// ============================================================================
#include "ext/better_trees/better_trees.h"

#include "better_trees/blossom.h"   // src/render/common/better_trees: the table, the packing (pure)
#include "graphics/Material.h"
#include "object/External.h"
#include "video/Context.h"
#include "video/ImageBuffer.h"

#include <cctype>
#include <cstdio>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace Ext {
namespace BetterTrees {

namespace {

struct Code {
    const void* material = nullptr;   // what the code was computed from
    const void* image    = nullptr;
    float z = 0.0f;
    float w = 0.0f;
};

std::string Lower(const char* s) {
    std::string r = s != nullptr ? s : "";
    for (char& c : r) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return r;
}

std::string PathOf(const Object::External* object) {
    if (object == nullptr) return std::string();
    const System::Filename path = object->GetPath();
    return Lower(path._data);
}

const void* FirstImage(const Graphics::MaterialInstance& mi) {
    return mi._textures._size > 0 ? mi._textures._data[0].image : nullptr;
}

void LogOnce(const std::string& names, const Blossom::Species* s, bool known) {
    static std::mutex mutex;
    static std::unordered_set<std::string> logged;
    std::lock_guard<std::mutex> lock(mutex);
    if (!logged.insert(names).second) return;
    if (s == nullptr) return;   // not a canopy: nothing worth a line
    std::fprintf(stderr, "[NaturalGrowth] blossom: %s (%s, peak %.2f, density %.2f%s) <- %s\n", s->name,
                 known ? "table" : "hashed", static_cast<double>(s->peak), static_cast<double>(s->density),
                 (s->flags & Blossom::kCatkin) != 0 ? ", catkins" : "", names.c_str());
}

Code Compute(const Graphics::MaterialInstance& mi) {
    Code c;
    c.material = mi._material.Get();
    c.image    = FirstImage(mi);
    const std::string material = PathOf(mi._material.Get());
    std::string names = PathOf(&mi);
    for (int32_t i = 0; i < mi._textures._size; ++i) {
        names += '|';
        names += PathOf(mi._textures._data[i].image);
    }
    Blossom::Species s;
    bool known = false;
    if (!Blossom::SpeciesFor(material, names, &s, &known)) {
        LogOnce(names, nullptr, false);
        return c;
    }
    Blossom::Pack(s, &c.z, &c.w);
    LogOnce(names, &s, known);
    return c;
}

void AfterSetup(const Graphics::MaterialInstance& mi, Video::Context& context) {
    thread_local std::unordered_map<const Graphics::MaterialInstance*, Code> codes;
    auto it = codes.find(&mi);
    if (it == codes.end() || it->second.material != mi._material.Get() || it->second.image != FirstImage(mi)) {
        it = codes.insert_or_assign(&mi, Compute(mi)).first;
    }
    const Code& c = it->second;
    if (context._time.z == c.z && context._time.w == c.w) return;
    context._time.z = c.z;
    context._time.w = c.w;
    context._dirty |= RtVideo::kDirtyGlobalConstants;
}

}  // namespace

void SyncBlossom(bool on) {
    Graphics::MaterialInstance::_extAfterSetup = on ? &AfterSetup : nullptr;
}

}  // namespace BetterTrees
}  // namespace Ext
