// ============================================================================
// src/ext/stranded_wade/stranded_wade.cpp -- fix.stranded_wade
// ============================================================================
// NOT RETAIL. docs/retail_bugs.md §19. The seams are MoveTo::AbstractImpl / Execute / Exit
// (src/game/Action/moveto.cpp) and CitizenAIComponent::TransitionState
// (src/game/Component/citizenaicomponent.cpp); the stranded rule is stranded_wade_rule.h.
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

const MapCell& CellAt(const Map* map, float x, float y) {
    return map->cells.GetData()[CellOf(map, x, y)];
}

const MapCell& CharacterCell(const Map* map, const CharacterComponent* character) {
    const Math::Vector3& pos = character->modelComponent->transform.translation;
    return CellAt(map, pos.x, pos.y);
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

// The interaction node (node 0) of `entity`, false when it has no InteractComponent.
bool NodeOf(ComponentEntity* entity, Math::Vector3* out) {
    InteractComponent* interact = entity ? entity->FindComponent<InteractComponent>() : nullptr;
    if (interact == nullptr) return false;
    interact->NodePosition(out, 0, 0, nullptr);
    return true;
}

// The citizen's anchor zone: his home's zone, or the nearest town hall's when he is homeless (0: none).
uint16_t AnchorZone(const Map* map, CitizenComponent* citizen, const CharacterComponent* character) {
    Math::Vector3 node;
    if (citizen->home != nullptr) {
        if (!NodeOf(citizen->home.Get(), &node)) return 0;
    } else {
        const Math::Vector3& pos = character->modelComponent->transform.translation;
        if (!NodeOf(NearestHall(map, CellOf(map, pos.x, pos.y)), &node)) return 0;
    }
    return CellAt(map, node.x, node.y).zone;
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

}  // namespace

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
    const uint16_t anchor = AnchorZone(map, citizen, character);
    Store(character, OnRefusal(Load(character), walk->passFlags, here.terrainType, here.zone,
                               there.terrainType, there.zone, anchor));
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
    if ((s.flags & kRefused) == 0) return false;   // the common case: one byte test

    Map* map = GameUpdateLoop::_singleton->m_game;
    const MapCell& here = CharacterCell(map, character);
    const uint16_t anchor = AnchorZone(map, citizen, character);
    s = Settle(s, here.terrainType, here.zone, anchor);
    if (!IsStranded(s, here.terrainType, here.zone, anchor)) {
        Store(character, s);
        return false;
    }

    const Math::Vector3& pos = character->modelComponent->transform.translation;
    Math::Vector3 homeNode;
    const bool hasHome = citizen->home != nullptr && NodeOf(citizen->home.Get(), &homeNode);
    Target target = RescueTarget(s, hasHome);
    Math::Vector3 dest;
    bool found = false;
    if (target == Target::Hall) {
        found = NodeOf(NearestHall(map, CellOf(map, pos.x, pos.y)), &dest);
        if (found) {
            const MapCell& hall = CellAt(map, dest.x, dest.y);
            if (!NeedsRescue(here.terrainType, here.zone, hall.terrainType, hall.zone)) found = false;
        }
        if (!found && hasHome) {
            // No town hall he is cut off from: home is the only place left to try.
            target = Target::Home;
        }
    }
    if (target == Target::Home) {
        dest = homeNode;
        found = true;
    }
    if (!found) {
        Store(character, OnNoTarget(s));
        return false;
    }
    PushRescueWalk(citizen, &dest.x);
    Store(character, OnRescuePushed(s, target));
    if (s_logged < kLogCap) {
        ++s_logged;
        fprintf(stderr, "[StrandedWade] citizen %p at (%.1f, %.1f) zone %u wades to his %s at (%.1f, %.1f) zone %u\n",
                static_cast<void*>(citizen), pos.x, pos.y, here.zone, target == Target::Home ? "home" : "town hall",
                dest.x, dest.y, CellAt(map, dest.x, dest.y).zone);
    }
    return true;
}

}  // namespace StrandedWade
}  // namespace Ext
