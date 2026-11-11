// ============================================================================
// src/ext/map_overlays/overlay_views.cpp -- ext.map_overlays: the instant views, read from the live town
// ============================================================================
// NOT RETAIL. docs/extensions/handins/MAPOVERLAYS.md §2. BuildInstantTiles walks the entity list once per
// image rebuild (overlay_view.cpp: at most once every kRebuildFrames frames, never per frame). Read only:
// nothing here writes game state or draws from the game's Random.
//
// Every building is recognised by its components and descriptions, never by its name, so a mod's building
// with the same components shows in the same view. A tile is the game's cell (Map::cells index
// y * size + x): a building's MapComponent footprint {tileX0, tileY0, w = tileX1, h = tileY1}, its centre
// (w >> 1) + x0, (h >> 1) + y0 as Map::StampInfluence takes it; an entity without one (an animal) is at
// its model's translation, (int)(t + (size >> 1)) per axis as MoveTo's WorldToCell.
//
//   Stock     StorageComponent items (storage, stockpiles, markets, homes: a home's goods are its own
//             StorageComponent) and RawMaterialComponent piles on the ground, filtered by the selection's
//             resources; a building's count is spread over its footprint.
//   Services  a disk of RadiusDescription::_radius round the building's centre, +1 per building, of:
//               Market    MarketComponent
//               Well      WellComponent
//               Herbalist HealerComponent (Herbalist.rsc and DoctorHouse.rsc both carry HealerDescription)
//               Chapel    AttendanceComponent + HappinessComponent, no HealerComponent
//               School    AttendanceComponent without HappinessComponent / HealerComponent
//               Tavern    HappinessComponent whose description needs goods (_requireStorage), not a market
//               TownHall  ImmigrationComponent
//             A building with no RadiusDescription (school, town hall: they reach the whole town) covers
//             its own footprint only.
//   Citizens  per home, the residents' mean of one measure, in percent, over the home's footprint.
//   Natural   NaturalResourceComponent entities by the flags of the material they yield (Wood, Stone,
//             Iron); wildlife = WildAnimalComponent entities at their tile; fish: the game keeps no fish
//             stock, so the view shows the water the fishing docks reach (MapCell influence byte 8, the
//             count FishingComponent stamps and FishingWork samples).
//   Land      one LandClass per tile, by the placement tool's own cell test (land_class.h).
// ============================================================================
#include "ext/map_overlays/map_overlays.h"
#include "ext/map_overlays/land_class.h"
#include "ext/map_overlays/overlay_views_internal.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "Component/AttendanceComponent.h"
#include "Component/CitizenComponent.h"
#include "Component/CitizenDescription.h"
#include "Component/ComponentDescription.h"
#include "Component/HappinessComponent.h"
#include "Component/HappinessDescription.h"
#include "Component/HealerComponent.h"
#include "Component/ImmigrationComponent.h"
#include "Component/MapComponent.h"
#include "Component/MarketComponent.h"
#include "Component/ModelComponent.h"
#include "Component/NaturalResourceComponent.h"
#include "Component/NaturalResourceDescription.h"
#include "Component/RadiusDescription.h"
#include "Component/RawMaterialComponent.h"
#include "Component/RawMaterialDescription.h"
#include "Component/StorageComponent.h"
#include "Component/WellComponent.h"
#include "Component/WildAnimalComponent.h"
#include "Entity/CitizenBehavior.h"
#include "Entity/ComponentEntity.h"
#include "Entity/EntityList.h"
#include "Game/GameUpdateLoop.h"
#include "Game/Map.h"
#include "object/External.h"
#include "object/TypeInfo.h"
#include "system/Filename.h"

namespace Ext {
namespace MapOverlays {

namespace {

constexpr int kFishingCoverageByte = 8;   // MapCell::influenceCounts[8]: the docks' coverage (fishingcomponent_produce.cpp)
constexpr int kMaxRadius = 512;           // a radius past this is a broken description, not a service

// ---- materials -------------------------------------------------------------------------------------------

struct MaterialInfo {
    std::string path;   // lower case
    uint32_t flags = 0;
};

std::unordered_map<const RawMaterialDescription*, MaterialInfo> s_materials;   // per loaded material
std::unordered_map<std::string, uint32_t> s_flagsByPath;                       // lower-case path -> _flags
bool s_flagsWalked = false;
std::unordered_map<const ComponentDescription*, const RawMaterialDescription*> s_materialOf;   // template -> material

std::string Lower(const char* p) {
    std::string out;
    if (p == nullptr) {
        return out;
    }
    for (; *p != '\0'; ++p) {
        char c = *p;
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
        out.push_back(c);
    }
    return out;
}

std::string LowerPath(const ::Object::External* external) {
    const System::Filename path = external->GetPath();
    return Lower(path._data);
}

const MaterialInfo& InfoOf(const RawMaterialDescription* m) {
    auto it = s_materials.find(m);
    if (it != s_materials.end()) {
        return it->second;
    }
    MaterialInfo info;
    info.path = LowerPath(m);
    info.flags = m->_flags;
    return s_materials.emplace(m, info).first->second;
}

// The material a template is, through its RawMaterialDescription (null when it has none).
const RawMaterialDescription* MaterialOf(const ComponentDescription* d) {
    if (d == nullptr) {
        return nullptr;
    }
    auto it = s_materialOf.find(d);
    if (it != s_materialOf.end()) {
        return it->second;
    }
    const RawMaterialDescription* m = d->FindDescription<RawMaterialDescription>();
    s_materialOf.emplace(d, m);
    return m;
}

// Every loaded RawMaterialDescription's flags by path: the factory walk ext.restricted_foods uses
// (ExternalFactory::FindNext), once per town.
void WalkMaterials() {
    if (s_flagsWalked) {
        return;
    }
    s_flagsWalked = true;
    ::Object::ExternalFactory* factory = ::Object::ExternalFactory::_singleton;
    if (factory == nullptr) {
        return;
    }
    const uint32_t tid = RawMaterialDescription::TypeId();
    const ::Object::External* prev = nullptr;
    int guard = 0;
    while ((prev = factory->FindNext(tid, prev)) != nullptr && guard++ < 20000) {
        if (!::Object::IsDerivedFromNullSafe(prev, tid)) {
            continue;
        }
        const RawMaterialDescription* m = static_cast<const RawMaterialDescription*>(prev);
        const MaterialInfo& info = InfoOf(m);
        s_flagsByPath[info.path] = info.flags;
    }
}

// The selection's resource filter, resolved once per rebuild.
struct ResourceFilter {
    bool all = true;
    std::string path;     // lower case; wins over flags
    uint32_t flags = 0;

    explicit ResourceFilter(const Selection& s) {
        if (!s.resourcePath.empty()) {
            all = false;
            path = Lower(s.resourcePath.c_str());
        } else if (s.categoryFlags != 0) {
            all = false;
            flags = s.categoryFlags;
        }
    }

    bool Matches(const RawMaterialDescription* m) const {
        if (m == nullptr) {
            return false;
        }
        if (all) {
            return true;
        }
        const MaterialInfo& info = InfoOf(m);
        if (!path.empty()) {
            return info.path == path;
        }
        return (info.flags & flags) != 0;
    }
};

// ---- the town --------------------------------------------------------------------------------------------

struct Footprint {
    int x = 0;
    int y = 0;
    int w = 1;
    int h = 1;
};

// The entity's tiles: its MapComponent footprint when placed, else the 1x1 tile under its model. False when
// it has neither (not on the map).
bool FootprintOf(const ComponentEntity* e, int size, Footprint& out) {
    const MapComponent* mc = e->FindComponent<MapComponent>();
    if (mc != nullptr && mc->tileX0 >= 0 && mc->tileY0 >= 0) {
        out.x = mc->tileX0;
        out.y = mc->tileY0;
        out.w = mc->tileX1 > 0 ? mc->tileX1 : 1;
        out.h = mc->tileY1 > 0 ? mc->tileY1 : 1;
        return out.x < size && out.y < size;
    }
    const ModelComponent* model = e->FindComponent<ModelComponent>();
    if (model == nullptr) {
        return false;
    }
    const float half = static_cast<float>(size >> 1);
    const float fx = model->transform.translation.x + half;
    const float fy = model->transform.translation.y + half;
    if (!(fx >= 0.0f && fy >= 0.0f && fx < static_cast<float>(size) && fy < static_cast<float>(size))) {
        return false;
    }
    out.x = static_cast<int>(fx);
    out.y = static_cast<int>(fy);
    out.w = 1;
    out.h = 1;
    return true;
}

void CentreOf(const Footprint& f, int& cx, int& cy) {
    cx = (f.w >> 1) + f.x;
    cy = (f.h >> 1) + f.y;
}

// Adds `value` to each footprint tile inside the map.
void PaintFootprint(std::vector<float>& out, int size, const Footprint& f, float value) {
    for (int y = f.y; y < f.y + f.h; ++y) {
        if (y < 0 || y >= size) {
            continue;
        }
        for (int x = f.x; x < f.x + f.w; ++x) {
            if (x < 0 || x >= size) {
                continue;
            }
            out[static_cast<size_t>(y) * size + x] += value;
        }
    }
}

// Adds `value` over the disk Map::StampInfluence stamps: dx^2 + dy^2 <= r^2.
void PaintDisk(std::vector<float>& out, int size, int cx, int cy, int r, float value) {
    const int r2 = r * r;
    for (int y = cy - r; y <= cy + r; ++y) {
        if (y < 0 || y >= size) {
            continue;
        }
        const int dy = y - cy;
        for (int x = cx - r; x <= cx + r; ++x) {
            if (x < 0 || x >= size) {
                continue;
            }
            const int dx = x - cx;
            if (dx * dx + dy * dy <= r2) {
                out[static_cast<size_t>(y) * size + x] += value;
            }
        }
    }
}

// The components the views look at, found in one pass over the entity's component list.
struct Parts {
    StorageComponent* storage = nullptr;
    RawMaterialComponent* pile = nullptr;
    NaturalResourceComponent* natural = nullptr;
    WildAnimalComponent* animal = nullptr;
    MarketComponent* market = nullptr;
    WellComponent* well = nullptr;
    HealerComponent* healer = nullptr;
    AttendanceComponent* attendance = nullptr;
    HappinessComponent* happiness = nullptr;
    ImmigrationComponent* immigration = nullptr;
};

struct TypeIds {
    uint32_t storage = StorageComponent::TypeId();
    uint32_t pile = RawMaterialComponent::TypeId();
    uint32_t natural = NaturalResourceComponent::TypeId();
    uint32_t animal = WildAnimalComponent::TypeId();
    uint32_t market = MarketComponent::TypeId();
    uint32_t well = WellComponent::TypeId();
    uint32_t healer = HealerComponent::TypeId();
    uint32_t attendance = AttendanceComponent::TypeId();
    uint32_t happiness = HappinessComponent::TypeId();
    uint32_t immigration = ImmigrationComponent::TypeId();
};

template <class T>
void Take(Component* c, uint32_t id, uint32_t want, T*& slot) {
    if (id == want && slot == nullptr && c->IsDerivedFrom(want)) {
        slot = static_cast<T*>(c);
    }
}

void ReadParts(const ComponentEntity* e, const TypeIds& ids, Parts& p) {
    for (int32_t i = 0; i < e->_components._size; ++i) {
        Component* c = e->_components[i].Get();
        if (c == nullptr) {
            continue;
        }
        const uint32_t id = c->Type();
        Take(c, id, ids.storage, p.storage);
        Take(c, id, ids.pile, p.pile);
        Take(c, id, ids.natural, p.natural);
        Take(c, id, ids.animal, p.animal);
        Take(c, id, ids.market, p.market);
        Take(c, id, ids.well, p.well);
        Take(c, id, ids.healer, p.healer);
        Take(c, id, ids.attendance, p.attendance);
        Take(c, id, ids.happiness, p.happiness);
        Take(c, id, ids.immigration, p.immigration);
    }
}

template <class Fn>
void ForEachEntity(Fn fn) {
    GameUpdateLoop* g = GameUpdateLoop::_singleton;
    if (g == nullptr || g->m_entityList == nullptr) {
        return;
    }
    const MasterEntityArray& all = g->m_entityList->masterArray;
    for (int32_t i = 0; i < all._size; ++i) {
        ComponentEntity* e = all._data[i].Get();
        if (e != nullptr) {
            fn(e);
        }
    }
}

// ---- the views -------------------------------------------------------------------------------------------

void BuildStock(const Selection& s, int size, std::vector<float>& out) {
    const ResourceFilter filter(s);
    const TypeIds ids;
    ForEachEntity([&](ComponentEntity* e) {
        Parts p;
        ReadParts(e, ids, p);
        if (p.storage == nullptr && p.pile == nullptr) {
            return;
        }
        float count = 0.0f;
        if (p.storage != nullptr) {
            const StorageComponent::ItemArray& items = p.storage->storageSlots;
            for (int32_t i = 0; i < items._size; ++i) {
                const StorageComponent::Item& item = items._data[i];
                if (item._count > 0 && filter.Matches(MaterialOf(item._resource))) {
                    count += static_cast<float>(item._count);
                }
            }
        }
        if (p.pile != nullptr && p.pile->quantity > 0 && filter.Matches(MaterialOf(e->_description))) {
            count += static_cast<float>(p.pile->quantity);
        }
        Footprint f;
        if (count > 0.0f && FootprintOf(e, size, f)) {
            PaintFootprint(out, size, f, count / static_cast<float>(f.w * f.h));
        }
    });
}

bool IsServiceKind(const Parts& p, ServiceKind kind) {
    switch (kind) {
        case ServiceKind::Market:
            return p.market != nullptr;
        case ServiceKind::Well:
            return p.well != nullptr;
        case ServiceKind::Herbalist:
            return p.healer != nullptr;
        case ServiceKind::Chapel:
            return p.attendance != nullptr && p.happiness != nullptr && p.healer == nullptr;
        case ServiceKind::School:
            return p.attendance != nullptr && p.happiness == nullptr && p.healer == nullptr;
        case ServiceKind::Tavern: {
            if (p.happiness == nullptr || p.market != nullptr) {
                return false;
            }
            Description* d = p.happiness->_description;
            if (!::Object::IsDerivedFromNullSafe(d, HappinessDescription::TypeId())) {
                return false;
            }
            return static_cast<const HappinessDescription*>(d)->_needsGoods;
        }
        case ServiceKind::TownHall:
            return p.immigration != nullptr;
        default:
            return false;
    }
}

void BuildServices(const Selection& s, int size, std::vector<float>& out) {
    if (s.kind >= static_cast<uint8_t>(ServiceKind::Count)) {
        return;
    }
    const ServiceKind kind = static_cast<ServiceKind>(s.kind);
    const TypeIds ids;
    ForEachEntity([&](ComponentEntity* e) {
        Parts p;
        ReadParts(e, ids, p);
        if (!IsServiceKind(p, kind)) {
            return;
        }
        Footprint f;
        if (!FootprintOf(e, size, f)) {
            return;
        }
        const RadiusDescription* radius =
            e->_description != nullptr ? e->_description->FindDescription<RadiusDescription>() : nullptr;
        if (radius == nullptr || radius->_radius <= 0 || radius->_radius > kMaxRadius) {
            PaintFootprint(out, size, f, 1.0f);
            return;
        }
        int cx = 0;
        int cy = 0;
        CentreOf(f, cx, cy);
        PaintDisk(out, size, cx, cy, radius->_radius, 1.0f);
    });
}

// One citizen's measure in 0..1; false when the description gives no scale for it.
bool CitizenValue(const CitizenComponent* c, CitizenMeasure m, float& v) {
    const CitizenDescription* d = c->description;
    if (d == nullptr) {
        return false;
    }
    float num = 0.0f;
    float den = 0.0f;
    bool invert = false;
    switch (m) {
        case CitizenMeasure::Happiness:
            num = c->happiness;
            den = d->maxHappiness;
            break;
        case CitizenMeasure::Health:
            num = c->health;
            den = d->maxHealth;
            break;
        case CitizenMeasure::Food:
            num = c->hunger;
            den = d->maxHunger;
            invert = true;
            break;
        case CitizenMeasure::Warmth:
            num = c->coldness;
            den = d->maxColdness;
            invert = true;
            break;
        case CitizenMeasure::Age:
            num = c->age;
            den = c->maxAge;   // the share of the citizen's own life span
            break;
        default:
            return false;
    }
    if (!(den > 0.0f) || !(num == num)) {
        return false;
    }
    float t = num / den;
    if (t < 0.0f) {
        t = 0.0f;
    }
    if (t > 1.0f) {
        t = 1.0f;
    }
    v = invert ? 1.0f - t : t;
    return true;
}

void BuildCitizens(const Selection& s, int size, std::vector<float>& out) {
    if (s.kind >= static_cast<uint8_t>(CitizenMeasure::Count)) {
        return;
    }
    const CitizenMeasure measure = static_cast<CitizenMeasure>(s.kind);
    GameUpdateLoop* g = GameUpdateLoop::_singleton;
    if (g == nullptr || g->m_citizenList == nullptr) {
        return;
    }
    struct Mean {
        float sum = 0.0f;
        int count = 0;
    };
    std::unordered_map<ComponentEntity*, Mean> homes;
    const auto& list = g->m_citizenList->citizens;
    for (int32_t i = 0; i < list._size; ++i) {
        const CitizenComponent* c = list._data[i].Get();
        if (c == nullptr) {
            continue;
        }
        ComponentEntity* home = c->home.Get();
        float v = 0.0f;
        if (home == nullptr || !CitizenValue(c, measure, v)) {
            continue;
        }
        Mean& m = homes[home];
        m.sum += v;
        ++m.count;
    }
    for (const auto& kv : homes) {
        Footprint f;
        if (kv.second.count > 0 && FootprintOf(kv.first, size, f)) {
            // Percent; the smallest share still shows (0 would be "no home here").
            float pct = 100.0f * kv.second.sum / static_cast<float>(kv.second.count);
            if (pct < 0.5f) {
                pct = 0.5f;
            }
            PaintFootprint(out, size, f, pct);
        }
    }
}

void BuildNatural(const Selection& s, int size, std::vector<float>& out) {
    if (s.kind >= static_cast<uint8_t>(NaturalKind::Count)) {
        return;
    }
    const NaturalKind kind = static_cast<NaturalKind>(s.kind);
    if (kind == NaturalKind::Fish) {
        GameUpdateLoop* g = GameUpdateLoop::_singleton;
        const Map* map = g != nullptr ? g->m_game : nullptr;
        if (map == nullptr || map->size != size) {
            return;
        }
        const MapCell* cells = map->cells.GetData();
        if (cells == nullptr || !Map::CellsCoverSize(size, map->cells._size)) {
            return;
        }
        const size_t n = static_cast<size_t>(size) * size;
        for (size_t i = 0; i < n; ++i) {
            out[i] = static_cast<float>(cells[i].influenceCounts[kFishingCoverageByte]);
        }
        return;
    }
    uint32_t wantFlags = 0;
    if (kind == NaturalKind::Trees) {
        wantFlags = RawMaterialFlags::Wood;
    } else if (kind == NaturalKind::Stone) {
        wantFlags = RawMaterialFlags::Stone;
    } else if (kind == NaturalKind::Iron) {
        wantFlags = RawMaterialFlags::Iron;
    }
    const TypeIds ids;
    ForEachEntity([&](ComponentEntity* e) {
        Parts p;
        ReadParts(e, ids, p);
        if (kind == NaturalKind::Wildlife) {
            if (p.animal == nullptr) {
                return;
            }
        } else {
            if (p.natural == nullptr) {
                return;
            }
            Description* d = p.natural->_description;
            if (!::Object::IsDerivedFromNullSafe(d, NaturalResourceDescription::TypeId())) {
                return;
            }
            // NaturalResourceDescription +0x20 is the .rsc's _rawMaterial (the template the resource yields).
            const RawMaterialDescription* m = MaterialOf(static_cast<const NaturalResourceDescription*>(d)->_building);
            if (m == nullptr || (InfoOf(m).flags & wantFlags) == 0) {
                return;
            }
        }
        Footprint f;
        if (!FootprintOf(e, size, f)) {
            return;
        }
        int cx = 0;
        int cy = 0;
        CentreOf(f, cx, cy);
        if (cx >= 0 && cy >= 0 && cx < size && cy < size) {
            out[static_cast<size_t>(cy) * size + cx] += 1.0f;
        }
    });
}

// One LandClass per tile, from the cell array alone (no entity walk).
void BuildLand(int size, std::vector<float>& out) {
    GameUpdateLoop* g = GameUpdateLoop::_singleton;
    const Map* map = g != nullptr ? g->m_game : nullptr;
    if (map == nullptr || map->size != size) {
        return;
    }
    const MapCell* cells = map->cells.GetData();
    if (cells == nullptr || !Map::CellsCoverSize(size, map->cells._size)) {
        return;
    }
    const size_t n = static_cast<size_t>(size) * size;
    for (size_t i = 0; i < n; ++i) {
        const MapCell& cell = cells[i];
        const LandClass c = ClassifyLand(cell.terrainType, cell.blocker.Get() != nullptr, cell.footprintCount);
        out[i] = static_cast<float>(static_cast<uint8_t>(c));
    }
}

}  // namespace

void BuildResourceMask(const Selection& s, const std::vector<std::string>& resources, std::vector<uint8_t>& mask) {
    mask.clear();
    if (s.resourcePath.empty() && s.categoryFlags == 0) {
        return;
    }
    const std::string path = Lower(s.resourcePath.c_str());
    if (path.empty()) {
        WalkMaterials();
    }
    mask.resize(resources.size(), 0);
    for (size_t i = 0; i < resources.size(); ++i) {
        const std::string entry = Lower(resources[i].c_str());
        if (!path.empty()) {
            mask[i] = entry == path ? 1 : 0;
            continue;
        }
        const auto it = s_flagsByPath.find(entry);
        mask[i] = it != s_flagsByPath.end() && (it->second & s.categoryFlags) != 0 ? 1 : 0;
    }
    // A filter that matches nothing must not read as "every resource" (the empty mask).
    if (mask.empty()) {
        mask.push_back(0);
    }
}

void ForgetMaterials() {
    s_materials.clear();
    s_flagsByPath.clear();
    s_flagsWalked = false;
    s_materialOf.clear();
}

bool BuildInstantTiles(const Selection& s, int size, std::vector<float>& out) {
    if (size <= 0 || size > kMaxMapSize) {
        out.clear();
        return false;
    }
    GameUpdateLoop* g = GameUpdateLoop::_singleton;
    if (g == nullptr || g->m_game == nullptr) {
        out.clear();
        return false;
    }
    out.assign(static_cast<size_t>(size) * size, 0.0f);
    switch (s.view) {
        case View::Stock:
            BuildStock(s, size, out);
            return true;
        case View::Services:
            BuildServices(s, size, out);
            return true;
        case View::Citizens:
            BuildCitizens(s, size, out);
            return true;
        case View::Natural:
            BuildNatural(s, size, out);
            return true;
        case View::Land:
            BuildLand(size, out);
            return true;
        default:
            out.clear();
            return false;
    }
}

}  // namespace MapOverlays
}  // namespace Ext
