// ============================================================================
// src/ext/stranded_wade/stranded_wade.cpp -- fix.stranded_wade
// ============================================================================
// NOT RETAIL. docs/retail_bugs.md §19. The seams are MoveTo::AbstractImpl / Execute / Exit
// (src/game/Action/moveto.cpp) and CitizenAIComponent::TransitionState
// (src/game/Component/citizenaicomponent.cpp); the counting rule is stranded_wade_rule.h.
// ============================================================================
#include "ext/stranded_wade/stranded_wade.h"
#include "ext/stranded_wade/stranded_wade_rule.h"

#include "Game/GameUpdateLoop.h"
#include "Game/Map.h"
#include "Game/exe_constants.h"   // kRdata_140172280 (the MoveTo speed sentinel)
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
#include "object/Internal.h"
#include "system/MemoryAllocator.h"

#include <cstdio>
#include <new>

namespace Ext {
namespace StrandedWade {

namespace {

// One stderr line per rescue, for the first few only (AGENTS.md §3).
int s_logged = 0;
constexpr int kLogCap = 16;

// The state byte: the first byte of CharacterComponent's padding at +0x44 (zeroed by the ctor,
// never read by retail, never saved).
uint8_t& State(CharacterComponent* character) {
    return character->_pad44[0];
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

void Record(MoveTo* walk, Outcome outcome) {
    CharacterComponent* character = walk->character.Get();
    if (character == nullptr) return;
    uint8_t& state = State(character);
    state = OnOutcome(state, outcome, walk->passFlags == kRescueFlags);
}

// The nearest town hall (an entity with an ImmigrationComponent) to `cell`, or null.
ComponentEntity* NearestHall(const Map* map, int cell) {
    EntityList* entities = GameUpdateLoop::_singleton->m_entityList;
    const int width = map->size;
    const int cx = cell % width;
    const int cy = cell / width;
    ComponentEntity* best = nullptr;
    long long bestDist = 0;
    for (int i = 0; i < entities->masterArray._size; ++i) {
        ComponentEntity* entity = entities->masterArray._data[i].Get();
        if (entity == nullptr) continue;
        if (((entity->_entityFlags >> 10) & 1) != 0) continue;   // marked for removal
        if (entity->FindComponent<ImmigrationComponent>() == nullptr) continue;
        InteractComponent* interact = entity->FindComponent<InteractComponent>();
        if (interact == nullptr) continue;
        Math::Vector3 node;
        interact->NodePosition(&node, 0, 0, nullptr);
        const int hallCell = CellOf(map, node.x, node.y);
        const long long dx = hallCell % width - cx;
        const long long dy = hallCell / width - cy;
        const long long dist = dx * dx + dy * dy;
        if (best == nullptr || dist < bestDist) {
            best = entity;
            bestDist = dist;
        }
    }
    return best;
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

}  // namespace

void OnRefused(MoveTo* walk) {
    CharacterComponent* character = walk->character.Get();
    if (character == nullptr) return;
    Map* map = GameUpdateLoop::_singleton->m_game;
    const Math::Vector3& pos = character->modelComponent->transform.translation;
    const MapCell* cells = map->cells.GetData();
    const MapCell& here = cells[CellOf(map, pos.x, pos.y)];
    const MapCell& there = cells[CellOf(map, walk->destination.x, walk->destination.y)];
    Record(walk, ClassifyRefusal(walk->passFlags, here.terrainType, here.zone, there.terrainType, there.zone));
}

void OnEmptyPath(MoveTo* walk) {
    Record(walk, Outcome::OtherFailed);
}

void OnExit(MoveTo* walk) {
    CharacterComponent* character = walk->character.Get();
    if (character == nullptr) return;
    Map* map = GameUpdateLoop::_singleton->m_game;
    const Math::Vector3& pos = character->modelComponent->transform.translation;
    if (CellOf(map, pos.x, pos.y) != CellOf(map, walk->destination.x, walk->destination.y)) return;
    Record(walk, Outcome::Arrived);
}

bool Rescue(CitizenAIComponent* ai) {
    CitizenComponent* citizen = ai->citizen.Get();
    CharacterComponent* character = citizen->character.Get();
    uint8_t& state = State(character);
    if (!IsStranded(state)) return false;

    Map* map = GameUpdateLoop::_singleton->m_game;
    const Math::Vector3& pos = character->modelComponent->transform.translation;
    const int cell = CellOf(map, pos.x, pos.y);

    ComponentEntity* home = citizen->home.Get();
    InteractComponent* homeNode = home ? home->FindComponent<InteractComponent>() : nullptr;
    Target target = RescueTarget(state, homeNode != nullptr);
    ComponentEntity* goal = home;
    InteractComponent* goalNode = homeNode;
    if (target == Target::Hall) {
        goal = NearestHall(map, cell);
        goalNode = goal ? goal->FindComponent<InteractComponent>() : nullptr;
        if (goalNode == nullptr && homeNode != nullptr) {
            // No town hall: home is the only place left to try.
            target = Target::Home;
            goal = home;
            goalNode = homeNode;
        }
    }
    if (goalNode == nullptr) {
        state = OnNoTarget(state);
        return false;
    }

    Math::Vector3 dest;
    goalNode->NodePosition(&dest, 0, 0, nullptr);
    PushRescueWalk(citizen, &dest.x);
    state = OnRescuePushed(state, target);
    if (s_logged < kLogCap) {
        ++s_logged;
        fprintf(stderr, "[StrandedWade] citizen %p at (%.1f, %.1f) wades to his %s at (%.1f, %.1f)\n",
                static_cast<void*>(citizen), pos.x, pos.y, target == Target::Home ? "home" : "town hall",
                dest.x, dest.y);
    }
    return true;
}

}  // namespace StrandedWade
}  // namespace Ext
