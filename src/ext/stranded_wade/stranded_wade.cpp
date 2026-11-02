// ============================================================================
// src/ext/stranded_wade/stranded_wade.cpp -- fix.stranded_wade
// ============================================================================
// NOT RETAIL. docs/retail_bugs.md §19. The seams are MoveTo::AbstractImpl / Execute / Exit
// (src/game/Action/moveto.cpp), CitizenComponent::SetHome (observed only,
// src/game/Component/citizen_assignworkplace.cpp) and CitizenAIComponent::TransitionState
// (src/game/Component/citizenaicomponent.cpp); the stranded rule is stranded_wade_rule.h.
// ============================================================================
#include "ext/stranded_wade/stranded_wade.h"
#include "ext/stranded_wade/stranded_wade_rule.h"

#include "Game/GameUpdateLoop.h"
#include "Game/Map.h"
#include "Game/exe_constants.h"   // kRdata_140172280 (the MoveTo speed sentinel)
#include "Entity/CitizenBehavior.h"   // the town's storage lists (+0x00 / +0x10)
#include "Entity/ComponentEntity.h"
#include "Entity/EntityList.h"
#include "Action/Action.h"
#include "Action/MoveTo.h"
#include "Action/SendComponentMessage.h"
#include "Component/CharacterComponent.h"
#include "Component/CitizenAIComponent.h"
#include "Component/CitizenComponent.h"
#include "Component/ImmigrationComponent.h"
#include "Component/InteractComponent.h"
#include "Component/ModelComponent.h"
#include "Component/ResidenceComponent.h"
#include "object/Internal.h"
#include "system/MemoryAllocator.h"

#include <cstdio>
#include <cstring>
#include <new>

namespace Ext {
namespace StrandedWade {

namespace {

// One stderr line per rescue, for the first few only (AGENTS.md §3).
int s_logged = 0;
constexpr int kLogCap = 16;

// The state lives in CharacterComponent's padding at +0x44 (zeroed by the ctor, never read by retail,
// never saved): byte 0 the flags, bytes 2..3 the zone of the recorded refusal.
State Load(const CharacterComponent* character) {
    State s;
    s.flags = character->_pad44[0];
    std::memcpy(&s.zone, &character->_pad44[2], sizeof s.zone);
    return s;
}

void Store(CharacterComponent* character, State s) {
    character->_pad44[0] = s.flags;
    std::memcpy(&character->_pad44[2], &s.zone, sizeof s.zone);
}

// The lost home's cell + 1 (0: none) lives in CitizenComponent's padding at +0x244 (zeroed by the
// ctor, never read by retail, never saved).
uint32_t LoadLost(const CitizenComponent* citizen) {
    uint32_t lost = 0;
    std::memcpy(&lost, citizen->_pad244, sizeof lost);
    return lost;
}

void StoreLost(CitizenComponent* citizen, uint32_t lost) {
    std::memcpy(citizen->_pad244, &lost, sizeof lost);
}

// A world position (x, y) to its clamped cell, as MoveTo::PathSetup 0x1400c8c10 computes it.
int CellOf(const Map* map, float x, float y) {
    const int width = map->size;
    const float center = static_cast<float>(width >> 1);
    int cx = static_cast<int>(x + center);
    int cy = static_cast<int>(y + center);
    if (cx < 0) cx = 0;
    if (cx >= width) cx = width - 1;
    if (cy < 0) cy = 0;
    if (cy >= width) cy = width - 1;
    return width * cy + cx;
}

// A cell's centre in world space.
Math::Vector3 CellCentre(const Map* map, int cell) {
    const int width = map->size;
    const float center = static_cast<float>(width >> 1);
    return Math::Vector3(static_cast<float>(cell % width) - center + 0.5f,
                         static_cast<float>(cell / width) - center + 0.5f, 0.0f);
}

const MapCell& CellAt(const Map* map, float x, float y) {
    return map->cells.GetData()[CellOf(map, x, y)];
}

const MapCell& CharacterCell(const Map* map, const CharacterComponent* character) {
    const Math::Vector3& pos = character->modelComponent->transform.translation;
    return CellAt(map, pos.x, pos.y);
}

// The interaction node (node 0) of `entity`, false when it has no InteractComponent.
bool NodeOf(ComponentEntity* entity, Math::Vector3* out) {
    InteractComponent* interact = entity ? entity->FindComponent<InteractComponent>() : nullptr;
    if (interact == nullptr) return false;
    interact->NodePosition(out, 0, 0, nullptr);
    return true;
}

// The squared cell distance from `cell` to `entity`'s interaction node, -1 when it has none or (with
// zone != 0) the node is not in that zone.
long long NodeDistance(const Map* map, ComponentEntity* entity, int cell, uint16_t zone, Math::Vector3* node) {
    if (entity == nullptr || ((entity->_entityFlags >> 10) & 1) != 0) return -1;   // marked for removal
    if (!NodeOf(entity, node)) return -1;
    const int nodeCell = CellOf(map, node->x, node->y);
    if (zone != 0 && map->cells.GetData()[nodeCell].zone != zone) return -1;
    const int width = map->size;
    const long long dx = nodeCell % width - cell % width;
    const long long dy = nodeCell / width - cell / width;
    return dx * dx + dy * dy;
}

enum class Pick : uint8_t {
    Hall,       // an entity with an ImmigrationComponent
    Residence,  // an entity with a ResidenceComponent
    Building,   // any entity with an interaction node that is not a character
};

// The nearest entity of `pick` to `cell` (in `zone` when non-zero), its node in `node`; null if none.
ComponentEntity* NearestEntity(const Map* map, int cell, uint16_t zone, Pick pick, Math::Vector3* node) {
    EntityList* entities = GameUpdateLoop::_singleton->m_entityList;
    ComponentEntity* best = nullptr;
    long long bestDist = 0;
    for (int i = 0; i < entities->masterArray._size; ++i) {
        ComponentEntity* entity = entities->masterArray._data[i].Get();
        if (entity == nullptr) continue;
        if (pick == Pick::Hall && entity->FindComponent<ImmigrationComponent>() == nullptr) continue;
        if (pick == Pick::Residence && entity->FindComponent<ResidenceComponent>() == nullptr) continue;
        if (pick == Pick::Building && entity->FindComponent<CharacterComponent>() != nullptr) continue;
        Math::Vector3 at;
        const long long dist = NodeDistance(map, entity, cell, zone, &at);
        if (dist < 0) continue;
        if (best == nullptr || dist < bestDist) {
            best = entity;
            bestDist = dist;
            *node = at;
        }
    }
    return best;
}

// The nearest storage building to `cell` (in `zone` when non-zero): the town's storage lists that
// CitizenBehavior::PickBestDropTarget (0x14008d590) walks for a citizen's load (+0x00 and +0x10),
// nearest by distance only (no material, capacity or reachability test: the goal may be across water).
ComponentEntity* NearestStorage(const Map* map, int cell, uint16_t zone, Math::Vector3* node) {
    CitizenBehavior* town = GameUpdateLoop::_singleton->m_citizenList;
    ComponentEntity* best = nullptr;
    long long bestDist = 0;
    for (int pass = 0; pass < 2; ++pass) {
        const CitizenIbpList<ComponentEntity>& list = pass == 0 ? town->field_0x00 : town->field_0x10;
        for (int i = 0; i < list._size; ++i) {
            ComponentEntity* entity = list._data[i].Get();
            Math::Vector3 at;
            const long long dist = NodeDistance(map, entity, cell, zone, &at);
            if (dist < 0) continue;
            if (best == nullptr || dist < bestDist) {
                best = entity;
                bestDist = dist;
                *node = at;
            }
        }
    }
    return best;
}

// The anchor: its kind, zone and the rescue walk's destination.
struct Anchor {
    AnchorKind kind = AnchorKind::None;
    uint16_t zone = 0;
    Math::Vector3 dest;
};

// The ruled order: home; the lost home (the building if it still stands, else the nearest storage,
// else any building, in its zone, else its cell); the nearest town hall; the nearest storage.
Anchor ResolveAnchor(const Map* map, CitizenComponent* citizen, int here) {
    Anchor a;
    Math::Vector3 node;
    const bool hasHome = citizen->home != nullptr && NodeOf(citizen->home.Get(), &node);
    const uint32_t lost = LoadLost(citizen);
    Math::Vector3 hallNode;
    ComponentEntity* hall = nullptr;
    Math::Vector3 storageNode;
    ComponentEntity* storage = nullptr;
    if (!hasHome && lost == 0) {
        hall = NearestEntity(map, here, 0, Pick::Hall, &hallNode);
        if (hall == nullptr) storage = NearestStorage(map, here, 0, &storageNode);
    }
    a.kind = ChooseAnchor(hasHome, lost != 0, hall != nullptr, storage != nullptr);
    switch (a.kind) {
    case AnchorKind::Home:
        a.dest = node;
        break;
    case AnchorKind::LostHome: {
        const int lostCell = static_cast<int>(lost - 1);
        a.zone = map->cells.GetData()[lostCell].zone;
        // The lost home still stands when a residence's node is on the remembered cell.
        ComponentEntity* building = NearestEntity(map, lostCell, a.zone, Pick::Residence, &node);
        if (building != nullptr && CellOf(map, node.x, node.y) == lostCell) {
            a.dest = node;
        } else if (NearestStorage(map, lostCell, a.zone, &node) != nullptr ||
                   NearestEntity(map, lostCell, a.zone, Pick::Building, &node) != nullptr) {
            a.dest = node;
        } else {
            a.dest = CellCentre(map, lostCell);
        }
        return a;
    }
    case AnchorKind::Hall:
        a.dest = hallNode;
        break;
    case AnchorKind::Storage:
        a.dest = storageNode;
        break;
    case AnchorKind::None:
        return a;
    }
    a.zone = CellAt(map, a.dest.x, a.dest.y).zone;
    return a;
}

// True while the action on top of the character's stack is a walk with the rescue pass flags.
bool RescueWalkRunning(CharacterComponent* character) {
    ActionStack& stack = character->actionStack;
    if (stack.current < 0) return false;
    Action* top = stack.actions[stack.current].Get();
    if (top == nullptr || top->Type() != MoveTo::TypeId()) return false;
    return static_cast<MoveTo*>(top)->passFlags == kRescueFlags;
}

// Clear the citizen's stack and push one walk to `dest` with the rescue pass flags, led by the
// "decide again" message the AI's own builders install (citizenai_actionctors.cpp
// InstallLeadMessage: SendComponentMessage to CitizenAIComponent in ActionStack +0x10).
void PushRescueWalk(CitizenComponent* citizen, float* dest) {
    CharacterComponent* character = citizen->character.Get();
    ActionStack* stack = &character->actionStack;
    stack->Clear();   // 0x1400c5400

    void* leadMem = System::MemoryAllocator::MemAlloc(0x40, 8);
    Action* lead = nullptr;
    if (leadMem) {
        ComponentEntity* destIBP = nullptr;
        uint32_t msgId = CitizenAIComponent::TypeId();
        ComponentEntity* entityIBP = citizen->_entity.Get();
        lead = new (leadMem) SendComponentMessage(&entityIBP, &msgId, 0, &destIBP, 0);   // 0x1400cb1d0
    }
    if (stack->field_0x10 != nullptr) stack->field_0x10.DeletePtr();
    stack->field_0x10 = lead;

    void* walkMem = System::MemoryAllocator::MemAlloc(0x480, 8);
    Action* walk = nullptr;
    if (walkMem) {
        CharacterComponent* mover = character;
        walk = new (walkMem) MoveTo(&mover, 1 /*Slow*/, 0, dest, kRescueFlags, 0, kRdata_140172280);   // 0x1400c9170
    }
    ::Object::InternalPtr<Action> ibp(walk);
    ActionStackPush(stack, &ibp);   // 0x1400c54e0
}

const char* KindName(AnchorKind kind) {
    switch (kind) {
    case AnchorKind::Home:
        return "home";
    case AnchorKind::LostHome:
        return "lost home";
    case AnchorKind::Hall:
        return "town hall";
    case AnchorKind::Storage:
        return "storage";
    case AnchorKind::None:
        break;
    }
    return "none";
}

}  // namespace

void OnHomeChange(CitizenComponent* citizen, ComponentEntity* oldHome, ComponentEntity* newHome) {
    // The home's interaction node cell: a house's footprint cells are not land (type 5, no zone),
    // the node in front of its door is.
    int oldCell = -1;
    Math::Vector3 node;
    if (oldHome != nullptr && newHome == nullptr && NodeOf(oldHome, &node)) {
        oldCell = CellOf(GameUpdateLoop::_singleton->m_game, node.x, node.y);
    }
    StoreLost(citizen, Ext::StrandedWade::OnHomeChange(LoadLost(citizen), oldHome != nullptr, oldCell,
                                                       newHome != nullptr));
}

void OnRefused(MoveTo* walk) {
    CharacterComponent* character = walk->character.Get();
    if (character == nullptr) return;
    Map* map = GameUpdateLoop::_singleton->m_game;
    const MapCell& here = CharacterCell(map, character);
    const MapCell& there = CellAt(map, walk->destination.x, walk->destination.y);
    if (!IsZoneRefusal(walk->passFlags, here.terrainType, here.zone, there.terrainType, there.zone)) return;
    // Only a citizen has an anchor (a nomad's or an animal's walk is not recorded).
    CitizenComponent* citizen = character->_entity->FindComponent<CitizenComponent>();
    if (citizen == nullptr) return;
    const Math::Vector3& pos = character->modelComponent->transform.translation;
    const Anchor anchor = ResolveAnchor(map, citizen, CellOf(map, pos.x, pos.y));
    Store(character, OnRefusal(Load(character), walk->passFlags, here.terrainType, here.zone,
                               there.terrainType, there.zone, anchor.zone));
}

void OnEmptyPath(MoveTo* walk) {
    CharacterComponent* character = walk->character.Get();
    if (character == nullptr) return;
    Store(character, OnWalkEnd(Load(character), false, walk->passFlags == kRescueFlags));
}

void OnExit(MoveTo* walk) {
    CharacterComponent* character = walk->character.Get();
    if (character == nullptr) return;
    Map* map = GameUpdateLoop::_singleton->m_game;
    const Math::Vector3& pos = character->modelComponent->transform.translation;
    if (CellOf(map, pos.x, pos.y) != CellOf(map, walk->destination.x, walk->destination.y)) return;
    State s = OnWalkEnd(Load(character), true, walk->passFlags == kRescueFlags);
    // A record made in another zone than the one he arrived in is stale ("since he entered").
    const MapCell& here = CharacterCell(map, character);
    if (IsZonedLand(here.terrainType, here.zone) && here.zone != s.zone) {
        s.flags = static_cast<uint8_t>(s.flags & ~kRefused);
        s.zone = 0;
    }
    Store(character, s);
}

bool Rescue(CitizenAIComponent* ai) {
    CitizenComponent* citizen = ai->citizen.Get();
    CharacterComponent* character = citizen->character.Get();
    State s = Load(character);
    // The rescue walk is still out: no state pick may replace it (the cold / hunger / housing
    // transitions of CitizenAIComponent::Decide call TransitionState(1) while an action runs, and
    // every state's builder clears the stack first).
    if ((s.flags & kPending) != 0 && RescueWalkRunning(character)) return true;
    const uint32_t lost = LoadLost(citizen);
    if ((s.flags & kRefused) == 0 && lost == 0) return false;   // the common case: two loads

    Map* map = GameUpdateLoop::_singleton->m_game;
    const Math::Vector3& pos = character->modelComponent->transform.translation;
    const int cell = CellOf(map, pos.x, pos.y);
    const MapCell& here = map->cells.GetData()[cell];
    // The lost home is forgotten once he stands in its zone again.
    if (lost != 0) {
        StoreLost(citizen, SettleLostHome(lost, here.terrainType, here.zone,
                                          map->cells.GetData()[lost - 1].zone));
    }
    if ((s.flags & kRefused) == 0) return false;

    const Anchor anchor = ResolveAnchor(map, citizen, cell);
    s = Settle(s, here.terrainType, here.zone, anchor.zone);
    if (!IsStranded(s, here.terrainType, here.zone, anchor.zone)) {
        Store(character, s);
        return false;
    }

    Target target = RescueTarget(s);
    Math::Vector3 dest = anchor.dest;
    AnchorKind goal = anchor.kind;
    if (target == Target::Fallback) {
        // The wading walk to the anchor failed: the nearest town hall, else storage, in another zone.
        Math::Vector3 node;
        if (NearestEntity(map, cell, 0, Pick::Hall, &node) != nullptr) {
            goal = AnchorKind::Hall;
        } else if (NearestStorage(map, cell, 0, &node) != nullptr) {
            goal = AnchorKind::Storage;
        } else {
            goal = AnchorKind::None;
        }
        const MapCell& there = CellAt(map, node.x, node.y);
        if (goal == AnchorKind::None || !NeedsRescue(here.terrainType, here.zone, there.terrainType, there.zone)) {
            // No fallback he is cut off from: the anchor is the only place left to try.
            target = Target::Anchor;
            goal = anchor.kind;
        } else {
            dest = node;
        }
    }
    PushRescueWalk(citizen, &dest.x);
    Store(character, OnRescuePushed(s, target));
    if (s_logged < kLogCap) {
        ++s_logged;
        fprintf(stderr, "[StrandedWade] citizen %p at (%.1f, %.1f) zone %u wades to his %s at (%.1f, %.1f) zone %u\n",
                static_cast<void*>(citizen), pos.x, pos.y, here.zone, KindName(goal), dest.x, dest.y,
                CellAt(map, dest.x, dest.y).zone);
    }
    return true;
}

}  // namespace StrandedWade
}  // namespace Ext
