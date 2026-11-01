// ============================================================================
// src/ext/resources/resources.cpp -- the resource providers (Resources.h) and
// the provider for our embedded .rsc text (text_provider.h)
// ============================================================================
// NOT RETAIL (docs/extensions/KITFREE.md section 2). The engine side is ONE
// seam: ExternalFactory::FindOrLoad's miss branch (src/engine/object/
// external_compiled.cpp) calls `RtObject::g_extProvideSeam`, a null-by-default
// function pointer, after External::Load found neither a loose .crs nor a
// package entry. The first Register() points it at Provide(); the faithful
// profile registers nothing, so the pointer stays null there and the engine
// pays one null test per load miss. A provided object is registered by
// FindOrLoad exactly as a loaded one (`_path = expanded`, ExternalFactory::Add).
//
// Provide() receives the factory's EXPANDED path ("Dialog\Extensions.rsc:
// buttonExtensions": backslashes, always a ":ident") and hands each provider
// the same path with forward slashes, matched against its prefix in
// registration order.
// ============================================================================

#include "ext/resources/Resources.h"
#include "ext/resources/text_provider.h"
#include "ext/resources/text_parser.h"
#include "ext/resources/text_reader.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "object/External.h"
#include "object/external_compiled.h"   // RtObject::g_extProvideSeam, the one engine seam
#include "system/Filename.h"

#include "ext_rsc_text.h"   // generated: Ext::RscText::kEmbedded (CMake target ext-rsc-text)

namespace Ext {
namespace Resources {

namespace {

struct Entry {
    const char* prefix;
    ProviderFn  fn;
    void*       user;
};
// The registered providers, in registration order. A vector: it was a fixed table of 8 that the providers were
// about to outgrow (docs/extensions/handins/PROVIDERTABLE.md). Never destroyed: the seam points at Provide()
// and a loader thread can still call it after main returns, where the old array had no destructor to run.
std::vector<Entry>& g_table = *new std::vector<Entry>;
int   g_provided = 0;

}  // namespace

bool Register(const char* prefix, ProviderFn fn, void* user) {
    if (prefix == nullptr || fn == nullptr) return false;
    // The same (prefix, fn, user) again is already registered: a second Init call changes nothing.
    for (const Entry& e : g_table) {
        if (e.fn == fn && e.user == user && std::strcmp(e.prefix, prefix) == 0) return true;
    }
    if (g_table.empty()) RtObject::g_extProvideSeam = &Provide;   // the engine seam, armed once
    g_table.push_back(Entry{prefix, fn, user});
    return true;
}

bool HasProvider(const char* path) {
    if (g_table.empty() || path == nullptr || path[0] == '\0') return false;
    std::string norm(path);
    for (char& c : norm) if (c == '\\') c = '/';
    for (const Entry& e : g_table) {
        if (std::strncmp(norm.c_str(), e.prefix, std::strlen(e.prefix)) == 0) return true;
    }
    return false;
}

namespace {

// THE LIFETIME OF A PROVIDED OBJECT: the process. The factory evicts an entry
// when every UpdateLoop whose usage bit it carries is gone (ExternalFactory::
// Clean), and a cache hit re-stamps the current loop's bit only while a loader
// runs (FindOrLoad, `_loadEnabled`). Retail's in-game dialogs are safe because
// the loop that opens them preloaded their string tables and sprites; ours are
// loaded lazily, so a dialog first opened in game carried the game's bit while
// the ExtensionsStringTable / ExtensionsSprites it points at carried only the
// boot menu's, and the in-game reload's Clean freed them under it (the
// Extensions dialog reopened after an in-game B / C change read a freed sprite
// sheet). Our objects are the exe's own data, not the install's: each is
// stamped with a bit of the provider's own that no loop ever cleans, through
// retail's own Add, whose first-touch reference visit (with loading enabled)
// stamps everything the object points at too. Only the whole-cache flush
// (`_cleanBitIndex < 0`) and ForceUnload still evict them.
int32_t g_pinBit = -2;   // -2: not reserved yet; -1: no free bit (nothing pinned)

void Pin(Object::External* obj, const char* expandedPath) {
    Object::ExternalFactory* const f = Object::ExternalFactory::_singleton;
    if (f == nullptr || obj == nullptr) return;
    if (g_pinBit == -2) {
        g_pinBit = -1;
        for (int32_t bit = 0; bit < 32; ++bit) {
            if ((f->_cleanBitMask & (1u << bit)) == 0) {
                f->_cleanBitMask |= (1u << bit);   // reserved for good, as an UpdateLoop's for its life
                g_pinBit = bit;
                break;
            }
        }
        std::fprintf(stderr, "[ExtRes] provided objects pinned with usage bit %d\n", g_pinBit);
    }
    if (g_pinBit < 0) return;

    // The factory's own insert first (the key is the expanded path, as
    // FindOrLoad sets it), then the pin as a second touch so the reference
    // visit runs; FindOrLoad's own Add after this is a hit that changes nothing.
    // The insert must not carry the pin bit: Add visits a row only on the first
    // touch of a bit, so when the published index IS the pin bit (the bit was
    // reserved right after a Clean released it, and no loop has published since)
    // the second touch would skip the visit, and what the object points at
    // (Dialog/Extensions.rsc:buttonExtensions -> Dialog/MainMenu.rsc:border) would
    // keep only the loop's bit and be freed under it (known_port_issues row 1333).
    // The insert then takes the next bit instead, on this object's row only.
    // EXT SEAM (NOT RETAIL, docs/extensions/handins/VISITREPLAY.md): a `_path` write.
    Object::NotifyResourceEvent(Object::ResourceEvent::PathWrite, nullptr, obj, expandedPath);
    obj->_path = System::Filename(expandedPath);
    const int32_t savedBit = f->_cleanBitIndex;
    if (savedBit == g_pinBit)
        f->_cleanBitIndex = (g_pinBit + 1) & 31;
    f->Add(obj);
    const bool savedLoad = f->_loadEnabled;
    f->_cleanBitIndex = g_pinBit;
    f->_loadEnabled = true;
    f->Add(obj);
    f->_cleanBitIndex = savedBit;
    f->_loadEnabled = savedLoad;
}

}  // namespace

Object::External* Provide(const char* path) {
    if (g_table.empty() || path == nullptr || path[0] == '\0') return nullptr;
    std::string norm(path);
    for (char& c : norm) if (c == '\\') c = '/';
    // The entries there are now: one a provider registers during this walk waits for the next miss.
    const size_t n = g_table.size();
    for (size_t i = 0; i < n; ++i) {
        const Entry e = g_table[i];   // a copy: a provider that registers another would reallocate the vector
        if (std::strncmp(norm.c_str(), e.prefix, std::strlen(e.prefix)) != 0) continue;
        Object::External* obj = e.fn(norm.c_str(), e.user);
        if (obj != nullptr) {
            ++g_provided;
            std::fprintf(stderr, "[ExtRes] provided %s (%s)\n", norm.c_str(), obj->TypeName());
            Pin(obj, path);
            return obj;
        }
    }
    return nullptr;
}

void SetPath(Object::External* obj, const char* path) {
    if (obj == nullptr || path == nullptr) return;
    // EXT SEAM (NOT RETAIL, docs/extensions/handins/VISITREPLAY.md): a `_path` write.
    Object::NotifyResourceEvent(Object::ResourceEvent::PathWrite, nullptr, obj, path);
    obj->_path = System::Filename(path);
}

Object::External* Publish(Object::External* obj) {
    if (obj == nullptr) return nullptr;
    Object::ExternalFactory* const f = Object::ExternalFactory::_singleton;
    if (f == nullptr) return nullptr;
    f->Add(obj);
    return obj;
}

int ProvidedCount() { return g_provided; }

// ---------------------------------------------------------------------------
// The text provider: the three embedded files, parsed once at boot.
// ---------------------------------------------------------------------------
namespace {

struct TextFile {
    std::string   key;       // "Dialog/Extensions.rsc"
    std::string   dir;       // "Dialog/"
    std::string   fileName;  // "Extensions.rsc"
    RscText::File parsed;
};
std::vector<TextFile>    g_files;
std::vector<std::string> g_building;   // paths under construction: a cycle is an error, not a hang

Object::External* ProvideText(const char* path, void* /*user*/) {
    const char* colon = std::strchr(path, ':');
    if (colon == nullptr) return nullptr;
    const std::string key(path, static_cast<size_t>(colon - path));
    const std::string name(colon + 1);
    for (TextFile& tf : g_files) {
        if (tf.key != key) continue;   // exact file: "Dialog/ExtensionsSprites.rsc" is another block's
        const RscText::Object* obj = tf.parsed.Find(name.c_str());
        if (obj == nullptr) {
            std::fprintf(stderr, "[ExtRes] %s: no object '%s' in the embedded text\n", key.c_str(), name.c_str());
            return nullptr;
        }
        for (const std::string& b : g_building) {
            if (b == path) {
                std::fprintf(stderr, "[ExtRes] %s: a reference cycle through this object\n", path);
                return nullptr;
            }
        }
        g_building.push_back(path);
        Object::External* built = RscText::BuildObject(tf.parsed, *obj, tf.dir.c_str(), tf.fileName.c_str());
        g_building.pop_back();
        return built;
    }
    return nullptr;
}

}  // namespace

void InitText() {
    // Once per run: the ext boot calls it under every profile but the faithful
    // one, and SaveLoadDialog::DoLoad's notice seam calls it again (under ANY
    // profile) before it shows an `ext:` entry's notice (text_provider.h).
    static bool s_done = false;
    if (s_done) return;
    s_done = true;
    for (int i = 0; i < RscText::kEmbeddedCount; ++i) {
        const RscText::EmbeddedFile& e = RscText::kEmbedded[i];
        TextFile tf;
        tf.key = e.path;
        const size_t slash = tf.key.rfind('/');
        tf.dir = (slash == std::string::npos) ? "" : tf.key.substr(0, slash + 1);
        tf.fileName = (slash == std::string::npos) ? tf.key : tf.key.substr(slash + 1);
        std::string err;
        if (!RscText::Parse(e.text, e.size, tf.parsed, err)) {
            std::fprintf(stderr, "[ExtRes] %s: %s -- its objects are not provided\n", e.path, err.c_str());
            continue;
        }
        int unknown = 0;
        for (const RscText::Object& o : tf.parsed.objects) {
            if (!RscText::KnowsType(o.type.c_str())) {
                std::fprintf(stderr, "[ExtRes] %s:%s: no schema for the type %s\n", e.path, o.name.c_str(), o.type.c_str());
                ++unknown;
            }
        }
        std::fprintf(stderr, "[ExtRes] %s: %d object(s) embedded%s\n", e.path,
                     static_cast<int>(tf.parsed.objects.size()),
                     unknown ? " (some of an unknown type)" : "");
        g_files.push_back(tf);
    }
    Register("Dialog/Extensions", &ProvideText, nullptr);
    Register("Dialog/EnhancedOptions", &ProvideText, nullptr);
    Register("Dialog/ExtensionsWindow", &ProvideText, nullptr);
    Register("Dialog/RestrictedFoodsPage", &ProvideText, nullptr);
    Register("Dialog/ItemPriorityPage", &ProvideText, nullptr);
    Register("Dialog/TerrainEditPage", &ProvideText, nullptr);
    Register("Dialog/TouchControls", &ProvideText, nullptr);
    Register("Dialog/TouchPointer", &ProvideText, nullptr);
    Register("Dialog/PhoneScroll", &ProvideText, nullptr);
    Register("Dialog/TownScreenshotPage", &ProvideText, nullptr);
    Register("Dialog/FamilyTreePage", &ProvideText, nullptr);
    Register("Dialog/FamilyTreeWindow", &ProvideText, nullptr);
    Register("Dialog/FamilyTreeSummary", &ProvideText, nullptr);
}

int TextObjectCount() {
    int n = 0;
    for (const TextFile& tf : g_files) n += static_cast<int>(tf.parsed.objects.size());
    return n;
}

bool TextObjectAt(int index, std::string& path) {
    for (const TextFile& tf : g_files) {
        const int n = static_cast<int>(tf.parsed.objects.size());
        if (index < n) { path = tf.key + ":" + tf.parsed.objects[static_cast<size_t>(index)].name; return true; }
        index -= n;
    }
    return false;
}

}  // namespace Resources
}  // namespace Ext
