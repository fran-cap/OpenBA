// ============================================================================
// src/ext/radius_all/radius_all.cpp -- ext.radius_all
// ============================================================================
// The rules are in radius_all.h, the pure parts in radius_all_math.h.
// ============================================================================

#include "ext/radius_all/radius_all.h"
#include "ext/radius_all/radius_all_math.h"
#include "ext/background_save/background_save.h"   // Busy: the save worker reads the decal list
#include "ext/registry.h"

#include "input/Input.h"
#include "ui/UserInterface.h"
#include "Component/ComponentDescription.h"
#include "Component/MapComponent.h"
#include "Component/RadiusDescription.h"
#include "Entity/CitizenBehavior.h"
#include "Entity/ComponentEntity.h"
#include "Game/GameUpdateLoop.h"
#include "Game/inputbindings.h"   // GameKey::PathMethod: retail's Shift
#include "Game/Map.h"
#include "Game/WorldGeometry.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace Ext {
namespace RadiusAll {

namespace {

std::vector<const void*> g_kinds;   // the templates whose circles are shown, in the order they were added
std::vector<Circle> g_circles;      // the decals this row added
uint64_t g_digest = 0;              // of the buildings g_circles was last built from
bool g_dirty = false;               // rebuild at the next Frame whatever the digest says
uint32_t g_epoch = 0;               // Ext::LockEpoch() of the game g_kinds / g_circles belong to
double g_armedUntil = -1.0;         // a long press's click counts until then

double Now() {
    using Clock = std::chrono::steady_clock;
    static const Clock::time_point start = Clock::now();
    return std::chrono::duration<double>(Clock::now() - start).count();
}

// Another game than the one the state was made in (a new game, a load, back to the menu): its terrain and
// its decals are gone with it, so the state is dropped and nothing is removed.
void ForgetIfAnotherGame() {
    const uint32_t epoch = LockEpoch();
    if (epoch != g_epoch) {
        g_kinds.clear();
        g_circles.clear();
        g_digest = 0;
        g_dirty = false;
        g_epoch = epoch;
    }
}

void RemoveCircle(WorldGeometry& world, const Circle& c) {
    if (c.decal >= 0 && c.decal < world._decals._size) {
        world.RemoveDecal(c.decal);   // 0x1400b0d00: frees the slot when it still holds this decal
    }
}

void RemoveAll(WorldGeometry& world) {
    for (const Circle& c : g_circles) {
        RemoveCircle(world, c);
    }
    g_circles.clear();
}

// The town's entities of a template: CitizenBehavior +0x160, the tree the toolbar counts buildings in
// (Toolbar_BuiltCount, toolbardialog.cpp).
const TemplateBucket* BucketOf(const CitizenBehavior& town, const void* kind) {
    const TemplateBucket* node = town.entitiesByTemplate._root;
    while (node != nullptr) {
        const void* key = node->element.key;
        if (key > kind) {
            node = node->left;
        } else if (key < kind) {
            node = node->right;
        } else {
            return node;
        }
    }
    return nullptr;
}

}  // namespace

bool Click(const ComponentEntity* entity, const Input::KeyMap* keyMap) {
    const bool armed = Now() < g_armedUntil;
    g_armedUntil = -1.0;
    const bool shift = keyMap != nullptr && keyMap->GetValue(0, ToUnderlying(GameKey::PathMethod)) != 0;
    if (!armed && !shift) {
        return false;
    }
    const ComponentDescription* kind = entity->_description;
    if (kind == nullptr) {
        return false;
    }
    const RadiusDescription* radius = kind->FindDescription<RadiusDescription>();
    if (radius == nullptr || radius->_material == nullptr) {
        return false;   // no circle to show: the click selects, as retail's
    }
    ForgetIfAnotherGame();
    const bool shown = ToggleKind(g_kinds, kind);
    g_dirty = true;
    const GameUpdateLoop* game = GameUpdateLoop::_singleton;
    if (game != nullptr && game->m_userInterface != nullptr) {
        game->m_userInterface->Play(5);   // a game button's click
    }
    std::fprintf(stderr, "[RadiusAll] %s the circles of kind %p (radius %d): %d kind(s) shown\n",
                 shown ? "show" : "hide", static_cast<const void*>(kind), radius->_radius,
                 static_cast<int32_t>(g_kinds.size()));
    return true;
}

void ArmLongPress() {
    g_armedUntil = Now() + kArmedSeconds;
}

void Frame() {
    if (g_kinds.empty() && g_circles.empty()) {
        return;
    }
    ForgetIfAnotherGame();
    if (g_kinds.empty() && g_circles.empty()) {
        return;
    }
    const GameUpdateLoop* game = GameUpdateLoop::_singleton;
    if (game == nullptr || game->m_worldGeometry == nullptr || game->m_citizenList == nullptr ||
        game->m_game == nullptr) {
        return;
    }
    if (BackgroundSave::Busy()) {
        return;   // the save worker is reading the decal list: nothing is added until it is done
    }
    WorldGeometry& world = *game->m_worldGeometry;
    if (!Ext().radius_all) {
        RemoveAll(world);
        g_kinds.clear();
        return;
    }

    // The buildings of the kinds shown; nothing to do while they are the ones the circles were made from.
    const CitizenBehavior& town = *game->m_citizenList;
    uint64_t digest = kDigestSeed;
    for (const void* kind : g_kinds) {
        digest = DigestAdd(digest, reinterpret_cast<uintptr_t>(kind));   // CAST-OK: ptr_int (an identity)
        const TemplateBucket* bucket = BucketOf(town, kind);
        const int32_t count = bucket != nullptr ? bucket->element.entities._size : 0;
        for (int32_t i = 0; i < count; ++i) {
            digest = DigestAdd(digest, reinterpret_cast<uintptr_t>(bucket->element.entities._data[i].Get()));   // CAST-OK: ptr_int (an identity)
        }
    }
    if (digest == g_digest && !g_dirty) {
        return;
    }
    g_digest = digest;
    g_dirty = false;

    const int32_t size = game->m_game->size;
    std::vector<Wanted> want;
    std::vector<const RadiusDescription*> wantRadius;
    for (const void* kind : g_kinds) {
        const TemplateBucket* bucket = BucketOf(town, kind);
        if (bucket == nullptr) {
            continue;
        }
        const RadiusDescription* radius = bucket->element.key->FindDescription<RadiusDescription>();
        if (radius == nullptr) {
            continue;
        }
        for (int32_t i = 0; i < bucket->element.entities._size; ++i) {
            ComponentEntity* entity = bucket->element.entities._data[i].Get();
            if (entity == nullptr) {
                continue;
            }
            const MapComponent* map = entity->FindComponent<MapComponent>();
            if (map == nullptr) {
                continue;
            }
            Wanted w;
            w.entity = entity;
            w.kind = kind;
            w.cell = CentreCell(map->tileX0, map->tileY0, map->tileX1, map->tileY1, size);
            if (w.cell < 0) {
                continue;
            }
            want.push_back(w);
            wantRadius.push_back(radius);
        }
    }

    std::vector<size_t> drop;
    std::vector<size_t> add;
    Diff(g_circles, want, drop, add);
    for (size_t k = drop.size(); k-- > 0;) {
        RemoveCircle(world, g_circles[drop[k]]);
        g_circles.erase(g_circles.begin() + static_cast<std::ptrdiff_t>(drop[k]));
    }
    for (const size_t j : add) {
        Circle c;
        c.entity = want[j].entity;
        c.kind = want[j].kind;
        c.cell = want[j].cell;
        c.decal = wantRadius[j]->AddHighlight(c.cell);   // 0x140055430
        g_circles.push_back(c);
    }
    std::fprintf(stderr, "[RadiusAll] %d circle(s) for %d kind(s): %d added, %d removed\n",
                 static_cast<int32_t>(g_circles.size()), static_cast<int32_t>(g_kinds.size()),
                 static_cast<int32_t>(add.size()), static_cast<int32_t>(drop.size()));
}

void BeforeSave() {
    if (g_circles.empty()) {
        return;
    }
    ForgetIfAnotherGame();
    const GameUpdateLoop* game = GameUpdateLoop::_singleton;
    if (g_circles.empty() || game == nullptr || game->m_worldGeometry == nullptr) {
        return;
    }
    RemoveAll(*game->m_worldGeometry);
    g_dirty = true;
}

}  // namespace RadiusAll
}  // namespace Ext
