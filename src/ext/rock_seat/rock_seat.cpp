// ============================================================================
// src/ext/rock_seat/rock_seat.cpp -- fix.rock_seat: the glue
// ============================================================================
// NOT RETAIL. rock_seat.h has the contract; docs/extensions/handins/ROCKSEAT.md
// the readers checked and the render-only argument.
// ============================================================================
#include "graphics/Geometry.h"
#include "graphics/Mesh.h"
#include "ext/rock_seat/rock_seat.h"
#include "ext/rock_seat/rock_seat_math.h"
#include "ext/registry.h"
#include "Component/ModelComponent.h"
#include "Component/NaturalResourceComponent.h"
#include "Component/NaturalResourceDescription.h"
#include "Entity/ComponentEntity.h"
#include "Entity/EntityList.h"
#include "Game/GameUpdateLoop.h"
#include "Game/Map.h"
#include "Game/WorldGeometry.h"
#include "object/TypeInfo.h"
#include "system/Value.h"   // RtSystem_Time_Ctor
#include <cstdio>

namespace Ext {
namespace RockSeat {

namespace {

// The terrain under world (x, y): the heightmap query PlaceAtCell itself uses
// (Map::GetTerrainPos), without the floor refine (no rock stands on a deck).
float TerrainHeight(void* ctx, float x, float y) {
    Map* map = static_cast<Map*>(ctx);
    const float in[2] = { x, y };
    float out[3] = { 0.0f, 0.0f, 0.0f };
    map->GetTerrainPos(out, in, '\1', nullptr);
    return out[2];
}

// The map, when its terrain is loaded (on a load the entities are read before
// the WorldGeometry, so OnAdded runs with no heightmap yet).
Map* LoadedMap() {
    GameUpdateLoop* loop = GameUpdateLoop::_singleton;
    if (loop == nullptr || loop->m_game == nullptr) return nullptr;
    const WorldGeometry* wg = loop->m_worldGeometry;
    if (wg == nullptr || wg->_heights.data == nullptr || wg->_heightRowStride <= 0) return nullptr;
    return loop->m_game;
}

bool IsRock(const ModelComponent* model) {
    ComponentEntity* entity = model->_entity.Get();
    if (entity == nullptr) return false;
    const NaturalResourceComponent* nr = entity->FindComponent<NaturalResourceComponent>();
    if (nr == nullptr) return false;
    Description* desc = nr->_description;
    if (!::Object::IsDerivedFromNullSafe(desc, NaturalResourceDescription::TypeId())) return false;
    const NaturalResourceDescription* d = static_cast<const NaturalResourceDescription*>(desc);
    return !d->_animated && d->_toolType == kToolPickAxe;
}

}  // namespace

void SetMeshTransform(ModelComponent* model) {
    Graphics::MeshInstance* mesh = &model->meshInstance;
    Map* map = LoadedMap();
    if (map == nullptr || model->meshIndex < 0 || !IsRock(model)) {
        mesh->SetTransform(model->transform);   // retail
        return;
    }
    const ::Box& b = mesh->_localBounds;
    const float scale = mesh->_color[0] > 0.0f ? mesh->_color[0] : 1.0f;
    const Footprint f = FootprintFromBounds(b.min, b.max, scale);
    const Math::Transform seated = Seat(model->transform, f, Val().rock_seat_mode, &TerrainHeight, map);
    mesh->SetTransform(seated);
}

void SeatAll(EntityList* list) {
    if (list == nullptr || LoadedMap() == nullptr) return;
    int seated = 0;
    const int count = list->masterArray._size;
    for (int i = 0; i < count; ++i) {
        ComponentEntity* entity = list->masterArray._data[i].Get();
        if (entity == nullptr) continue;
        ModelComponent* model = entity->FindComponent<ModelComponent>();
        if (model == nullptr || model->modelId < 0 || model->meshIndex < 0 || !IsRock(model)) continue;
        SetMeshTransform(model);
        // The model is already in the scene (ModelComponent::OnAdded ran inside its Persist), and
        // its node pose was built there from the unseated transform; a rock never ticks, so the
        // pose is rebuilt here the way OnAdded builds it (MeshInstance::Update with Time(0)).
        System::Time t{0};
        RtSystem_Time_Ctor(&t, 0.0f);
        model->meshInstance.Update(t);
        ++seated;
    }
    std::fprintf(stderr, "[RockSeat] seated %d rock(s) after the load\n", seated);
}

}  // namespace RockSeat
}  // namespace Ext
