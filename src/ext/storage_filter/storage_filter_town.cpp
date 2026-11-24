// src/ext/storage_filter/storage_filter_town.cpp -- NOT RETAIL. ext.storage_filter (class C): the game side.
// The factory walk that finds the materials, the once-per-frame BeginFrame, and the seam's slow path. See
// storage_filter.h and docs/extensions/handins/STORAGEFILTER.md.
//
// SELECTION (DESIGN.md hook rule 4). The seam never touches a string or a map. BeginFrame resolves the saved
// paths once, when the list or the lock epoch changes, into `g_resolved`: per refusing building a short flat
// array of RawMaterialDescription pointers. The seam tests `g_active` once; only a call about a building that
// has an entry goes on to the (sorted) array.
#include "ext/storage_filter/storage_filter.h"

#include "Component/MapComponent.h"
#include "Component/RawMaterialDescription.h"
#include "Component/StorageComponent.h"
#include "Component/StorageLocationComponent.h"
#include "Entity/ComponentEntity.h"
#include "Entity/EntityList.h"
#include "Game/GameUpdateLoop.h"
#include "ext/registry.h"   // Ext::Ext().storage_filter (the game's locked value), Ext::LockEpoch
#include "object/External.h"
#include "object/TypeInfo.h"
#include "system/Filename.h"

#include "Component/ComponentDescription.h"

#include <algorithm>
#include <cctype>
#include <unordered_set>
#include <utility>

namespace Ext {
namespace StorageFilter {

bool g_active = false;

namespace {

struct Resolved {
    int32_t tileX = 0;
    int32_t tileY = 0;
    std::vector<const RawMaterialDescription*> refused;
    std::vector<std::pair<const RawMaterialDescription*, int32_t>> caps;   // item, most units it takes
};

std::vector<Resolved> g_resolved;   // sorted by corner tile
uint32_t g_seenRevision = 0;
uint32_t g_seenEpoch = 0;
bool g_haveSeen = false;

struct Candidate {
    const RawMaterialDescription* material = nullptr;
    std::string key;   // the path, lower case
};

std::string Fold(const std::string& s) {
    std::string out = s;
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

std::string TemplatePath(const void* external) {
    if (external == nullptr) {
        return std::string();
    }
    const System::Filename path = static_cast<const ::Object::External*>(external)->GetPath();
    return path._data != nullptr ? std::string(path._data) : std::string();
}

// Every loaded RawMaterialDescription with its path. The factory walk is the one the game uses to enumerate
// what a package loaded (ExternalFactory::FindNext).
std::vector<Candidate> AllMaterials() {
    std::vector<Candidate> out;
    ::Object::ExternalFactory* factory = ::Object::ExternalFactory::_singleton;
    if (factory == nullptr) {
        return out;
    }
    const uint32_t tid = RawMaterialDescription::TypeId();
    const ::Object::External* prev = nullptr;
    int guard = 0;
    while ((prev = factory->FindNext(tid, prev)) != nullptr && guard++ < 20000) {
        if (!::Object::IsDerivedFromNullSafe(prev, tid)) {
            continue;
        }
        Candidate c;
        c.material = static_cast<const RawMaterialDescription*>(prev);   // the pointer the seam compares
        c.key = Fold(TemplatePath(prev));
        if (!c.key.empty()) {
            out.push_back(c);
        }
    }
    return out;
}

uint64_t TileKey(int32_t x, int32_t y) {
    return (static_cast<uint64_t>(static_cast<uint32_t>(x)) << 32) | static_cast<uint32_t>(y);
}

// The footprint corner of every live storage building (an entity with a StorageLocationComponent, the kind
// the tab is for). False when that cannot be told (no game, or the entity list not filled yet): nothing is
// dropped then.
bool StorageTiles(std::unordered_set<uint64_t>& tiles) {
    GameUpdateLoop* game = GameUpdateLoop::_singleton;
    EntityList* list = game != nullptr ? game->m_entityList : nullptr;
    if (list == nullptr || list->masterArray._size <= 0) {
        return false;
    }
    for (int32_t i = 0; i < list->masterArray._size; ++i) {
        ComponentEntity* entity = list->masterArray._data[i].Get();
        if (entity == nullptr || entity->FindComponent<StorageLocationComponent>() == nullptr) {
            continue;
        }
        const MapComponent* footprint = entity->FindComponent<MapComponent>();
        if (footprint != nullptr) {
            tiles.insert(TileKey(footprint->tileX0, footprint->tileY0));
        }
    }
    return true;
}

void Resolve() {
    std::vector<Resolved> next;
    std::vector<std::pair<int32_t, int32_t>> gone;   // corner tiles whose entry is dropped below
    const std::vector<Entry>& entries = Entries();
    if (!entries.empty()) {
        const std::vector<Candidate> all = AllMaterials();
        std::unordered_set<uint64_t> tiles;
        const bool known = StorageTiles(tiles);
        for (const Entry& e : entries) {
            if (known && tiles.count(TileKey(e.tileX, e.tileY)) == 0) {
                gone.push_back(std::make_pair(e.tileX, e.tileY));
                continue;
            }
            Resolved r;
            r.tileX = e.tileX;
            r.tileY = e.tileY;
            for (const std::string& path : e.refused) {
                const std::string key = Fold(path);
                for (const Candidate& c : all) {
                    if (c.key == key) {
                        r.refused.push_back(c.material);
                        break;
                    }
                }
            }
            for (const Entry::Cap& cap : e.caps) {
                const std::string key = Fold(cap.path);
                for (const Candidate& c : all) {
                    if (c.key == key) {
                        r.caps.push_back(std::make_pair(c.material, cap.limit));
                        break;
                    }
                }
            }
            if (!r.refused.empty() || !r.caps.empty()) {
                next.push_back(r);
            }
        }
        std::sort(next.begin(), next.end(), [](const Resolved& a, const Resolved& b) {
            return a.tileX != b.tileX ? a.tileX < b.tileX : a.tileY < b.tileY;
        });
    }
    g_resolved.swap(next);
    // A demolished building's entry goes (a later building on its tile must not inherit it). That moves the
    // list's revision after BeginFrame has read it, so the next frame resolves once more and finds nothing
    // left to drop.
    for (const auto& tile : gone) {
        ClearBuilding(tile.first, tile.second);
    }
}

}  // namespace

void BeginFrame() {
    if (!Ext::Ext().storage_filter) {   // class C: the game's locked value
        if (g_active || !g_resolved.empty()) {
            g_resolved.clear();
            g_haveSeen = false;
        }
        g_active = false;
        return;
    }
    const uint32_t epoch = LockEpoch();
    const uint32_t revision = Revision();
    if (!g_haveSeen || epoch != g_seenEpoch || revision != g_seenRevision) {
        Resolve();
        g_seenEpoch = epoch;
        g_seenRevision = revision;
        g_haveSeen = true;
    }
    g_active = !g_resolved.empty();
}

// The resolved entry for this building, or null: none for its corner tile, or it is not a storage building
// (a house built on a demolished barn's tile must not inherit the barn's entry).
static const Resolved* EntryFor(const StorageComponent* storage) {
    ComponentEntity* entity = storage->_entity.Get();
    if (entity == nullptr || entity->FindComponent<StorageLocationComponent>() == nullptr) {
        return nullptr;
    }
    const MapComponent* footprint = entity->FindComponent<MapComponent>();
    if (footprint == nullptr) {
        return nullptr;
    }
    const int32_t x = footprint->tileX0;
    const int32_t y = footprint->tileY0;
    const auto it = std::lower_bound(g_resolved.begin(), g_resolved.end(), std::make_pair(x, y),
                                     [](const Resolved& r, const std::pair<int32_t, int32_t>& v) {
        return r.tileX != v.first ? r.tileX < v.first : r.tileY < v.second;
    });
    if (it == g_resolved.end() || it->tileX != x || it->tileY != y) {
        return nullptr;
    }
    return &*it;
}

bool OverCapSlow(const StorageComponent* storage, const RawMaterialDescription* material, int count) {
    const Resolved* entry = EntryFor(storage);
    if (entry == nullptr || material == nullptr) {
        return false;
    }
    for (const auto& cap : entry->caps) {
        if (cap.first != material) {
            continue;
        }
        // Units of this item inside now: the slot whose template carries this description.
        int64_t held = 0;
        for (int32_t i = 0; i < storage->storageSlots._size; ++i) {
            const ComponentDescription* res = storage->storageSlots[i]._resource;
            for (int32_t j = 0; res != nullptr && j < res->_subDescCount; ++j) {
                if (res->_subDescs[j] == material) {
                    held += storage->storageSlots[i]._count;
                    break;
                }
            }
        }
        return held + count > cap.second;
    }
    return false;
}

bool RefusesSlow(const StorageComponent* storage, const RawMaterialDescription* material) {
    const Resolved* entry = EntryFor(storage);
    if (entry == nullptr) {
        return false;
    }
    const Resolved* it = entry;
    return std::find(it->refused.begin(), it->refused.end(), material) != it->refused.end();
}

}  // namespace StorageFilter
}  // namespace Ext
