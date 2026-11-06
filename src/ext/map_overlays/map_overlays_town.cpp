// ============================================================================
// src/ext/map_overlays/map_overlays_town.cpp -- ext.map_overlays (class C): the game side of the history
// ============================================================================
// NOT RETAIL. docs/extensions/handins/MAPOVERLAYS.md §3 / §4. g_on, BeginFrame (the lock, the map size, the
// month stamp), the flow seams' bodies (world position -> tile, material -> table index) and OnStep.
//
// WORLD -> TILE. The game's own mapping (DropOffRawMaterial::DropOnGround, action_pf_roots_drop.cpp, and
// EntityAnchorCell, action_pf_road.cpp): translation.x and translation.y are the two horizontal axes, and
// tile = (int)(w + (size >> 1)), clamped to the map edge as retail clamps it.
// An entity's site (OnProducedAt / OnConsumedAt): a citizen with a workplace counts at that building; then
// the entity's MapComponent footprint centre, tileX0 + (tileX1 >> 1), tileY0 + (tileY1 >> 1) (the footprint
// is x, y, w, h: EntityAnchorCell's fallback arm); else its translation (a pile's ModelComponent, a walking
// citizen's CharacterComponent::modelComponent, as DropOnGround reads it).
//
// THE MONTH STAMP is ext.family_tree's (FamilyTree::NowStamp: year * 12 + EventLog::SeasonString's month).
// ============================================================================
#include "ext/map_overlays/map_overlays.h"

#include <string>
#include <unordered_map>

#include "Component/CharacterComponent.h"
#include "Component/CitizenComponent.h"
#include "Component/ComponentDescription.h"
#include "Component/MapComponent.h"
#include "Component/ModelComponent.h"
#include "Component/RawMaterialDescription.h"
#include "Entity/ComponentEntity.h"
#include "Game/GameUpdateLoop.h"
#include "Game/Map.h"
#include "ext/family_tree/family_tree.h"   // FamilyTree::NowStamp, the month stamp
#include "ext/registry.h"                  // Ext::Ext().map_overlays (the game's locked value), Ext::LockEpoch
#include "object/External.h"
#include "system/Filename.h"

namespace Ext {
namespace MapOverlays {

bool g_on = false;

namespace {

// material -> table index, for the lock epoch it was built in (a new game or a load starts a new cache)
std::unordered_map<const RawMaterialDescription*, uint16_t> s_index;
uint32_t s_epoch = ~0u;

std::string TemplatePath(const RawMaterialDescription* material) {
    const System::Filename path = static_cast<const ::Object::External*>(material)->GetPath();
    return path._data != nullptr ? std::string(path._data) : std::string();
}

uint16_t IndexOf(const RawMaterialDescription* material) {
    const auto it = s_index.find(material);
    if (it != s_index.end()) {
        return it->second;
    }
    const uint16_t index = Town().ResourceIndex(TemplatePath(material));
    s_index.emplace(material, index);
    return index;
}

void AddAtTile(Flow flow, const RawMaterialDescription* material, int tileX, int tileZ, int count) {
    if (material == nullptr || count <= 0) {
        return;
    }
    History& h = Town();
    if (tileX < 0 || tileZ < 0 || tileX >= h.MapSize() || tileZ >= h.MapSize()) {
        return;
    }
    const uint16_t resource = IndexOf(material);
    if (resource == History::kNoResource) {
        return;
    }
    SiteKey site;
    site.x = static_cast<uint16_t>(tileX);
    site.z = static_cast<uint16_t>(tileZ);
    h.Add(flow, site, resource, static_cast<uint32_t>(count));
}

// One world axis as a tile: retail's (int) truncation clamped to [0, size) (DropOnGround), the clamp done
// on the float before the cast (a huge float would be UB). NaN is -1: not counted.
int TileOf(float world, int size) {
    const float t = world + static_cast<float>(size >> 1);
    if (t != t || size <= 0) {
        return -1;
    }
    if (t <= 0.0f) {
        return 0;
    }
    if (t >= static_cast<float>(size)) {
        return size - 1;
    }
    return static_cast<int>(t);
}

void AddAtWorld(Flow flow, const RawMaterialDescription* material, float worldX, float worldZ, int count) {
    const int size = Town().MapSize();
    AddAtTile(flow, material, TileOf(worldX, size), TileOf(worldZ, size), count);
}

void AddAtEntity(Flow flow, const ComponentDescription* item, const ComponentEntity* site, int count) {
    if (item == nullptr || site == nullptr || count <= 0) {
        return;
    }
    const RawMaterialDescription* material = item->FindDescription<RawMaterialDescription>();
    if (material == nullptr) {
        return;
    }
    // a citizen counts at its workplace (the harvester at its lodge), a citizen with none where it stands
    const ComponentEntity* place = site;
    const ModelComponent* model = nullptr;
    const CitizenComponent* citizen = site->FindComponent<CitizenComponent>();
    if (citizen != nullptr) {
        const ComponentEntity* work = citizen->workplace.Get();
        if (work != nullptr) {
            place = work;
        } else if (citizen->character.Get() != nullptr) {
            model = citizen->character.Get()->modelComponent.Get();
        }
    }
    const MapComponent* footprint = place->FindComponent<MapComponent>();
    if (footprint != nullptr && footprint->tileX0 >= 0 && footprint->tileY0 >= 0) {
        const int x = footprint->tileX0 + (static_cast<int>(footprint->tileX1) >> 1);
        const int z = footprint->tileY0 + (static_cast<int>(footprint->tileY1) >> 1);
        AddAtTile(flow, material, x, z, count);
        return;
    }
    if (model == nullptr) {
        model = place->FindComponent<ModelComponent>();
    }
    if (model != nullptr) {
        AddAtWorld(flow, material, model->transform.translation.x, model->transform.translation.y, count);
    }
}

}  // namespace

void BeginFrame() {
    g_on = ::Ext::Ext().map_overlays;   // class C: the game's locked value
    const uint32_t epoch = LockEpoch();
    if (epoch != s_epoch) {
        s_epoch = epoch;
        s_index.clear();
    }
    if (!g_on) {
        return;
    }
    GameUpdateLoop* g = GameUpdateLoop::_singleton;
    if (g != nullptr && g->m_game != nullptr) {
        Town().SetMapSize(g->m_game->size);
    }
    Town().AdvanceTo(FamilyTree::NowStamp());   // kNoStamp (no calendar) is ignored
    RenderFrame();
}

void OnProduced(const RawMaterialDescription* material, float worldX, float worldZ, int count) {
    AddAtWorld(Flow::Produced, material, worldX, worldZ, count);
}

void OnConsumed(const RawMaterialDescription* material, float worldX, float worldZ, int count) {
    AddAtWorld(Flow::Consumed, material, worldX, worldZ, count);
}

void OnProducedAt(const ComponentDescription* item, const ComponentEntity* site, int count) {
    AddAtEntity(Flow::Produced, item, site, count);
}

void OnConsumedAt(const ComponentDescription* item, const ComponentEntity* site, int count) {
    AddAtEntity(Flow::Consumed, item, site, count);
}

void OnStep(int tileX, int tileZ) {
    Town().AddStep(tileX, tileZ);
}

}  // namespace MapOverlays
}  // namespace Ext
