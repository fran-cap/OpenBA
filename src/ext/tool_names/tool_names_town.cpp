// ============================================================================
// src/ext/tool_names/tool_names_town.cpp -- ext.tool_names (class C): the game side
// ============================================================================
// docs/extensions/handins/TOOLNAMES.md. The pick-up seam hands RememberPickup the citizen and the item; the
// panel seam asks ToolFor for the item to name; BeginFrame (GameUpdateLoop::Update) drops the notes, and the
// records a load left waiting, of citizens who are gone. The note table and the codec are tool_names.cpp;
// this file is what needs the running game: a citizen's entity id, name and gender, an item's template path,
// and the factory walk that finds an item by that path.
// ============================================================================

#include "ext/tool_names/tool_names.h"

#include "Component/CitizenComponent.h"
#include "Component/RawMaterialDescription.h"
#include "Entity/CitizenBehavior.h"
#include "Entity/ComponentEntity.h"
#include "Game/GameUpdateLoop.h"
#include "ext/registry.h"
#include "object/External.h"
#include "object/TypeInfo.h"
#include "system/Filename.h"

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

namespace Ext {
namespace ToolNames {

namespace {

constexpr int kPruneFrames = 600;   // the notes of citizens who are gone are dropped this often

std::string TemplatePath(const void* external) {
    if (external == nullptr) return std::string();
    const System::Filename path = static_cast<const ::Object::External*>(external)->GetPath();
    return path._data != nullptr ? std::string(path._data) : std::string();
}

Citizen WhoIs(const CitizenComponent* c) {
    Citizen who;
    const ComponentEntity* e = c->_entity.Get();
    who.entity = e != nullptr ? e->_entityId : -1;
    who.name = c->name._data != nullptr ? std::string(c->name._data) : std::string();
    who.gender = c->gender == 0 ? 0 : 1;
    return who;
}

// The loaded RawMaterialDescription at `path`, by the factory walk the game itself uses to enumerate what its
// packages loaded; null when the game no longer has it.
const RawMaterialDescription* ItemAtPath(const std::string& path) {
    ::Object::ExternalFactory* factory = ::Object::ExternalFactory::_singleton;
    if (factory == nullptr) return nullptr;
    const uint32_t tid = RawMaterialDescription::TypeId();
    const ::Object::External* prev = nullptr;
    int guard = 0;
    while ((prev = factory->FindNext(tid, prev)) != nullptr && guard++ < 20000) {
        if (!::Object::IsDerivedFromNullSafe(prev, tid)) continue;
        if (TemplatePath(prev) == path) return static_cast<const RawMaterialDescription*>(prev);
    }
    return nullptr;
}

}  // namespace

void RememberPickup(const CitizenComponent* citizen, const RawMaterialDescription* item, int32_t uses) {
    if (citizen == nullptr || item == nullptr) return;
    const std::string path = TemplatePath(item);
    if (path.empty()) return;
    Remember(citizen, item, uses, WhoIs(citizen), path);
}

const RawMaterialDescription* ToolFor(const CitizenComponent* citizen, int32_t uses) {
    if (citizen == nullptr) return nullptr;
    if (const void* noted = Lookup(citizen, uses)) return static_cast<const RawMaterialDescription*>(noted);
    if (SavedCount() == 0) return nullptr;   // nothing waits from a save: the usual case
    const Citizen who = WhoIs(citizen);
    std::string path;
    if (!Claim(who, uses, path)) return nullptr;
    const RawMaterialDescription* item = ItemAtPath(path);
    // the game no longer has the item, or it is another tool now
    if (item == nullptr || item->_i54 != uses) return nullptr;
    Remember(citizen, item, uses, who, path);
    return item;
}

void BeginFrame() {
    static int countdown = kPruneFrames;
    const bool loaded = TakeLoadFlag();   // the first frame after a load looks at the waiting records at once
    if (!loaded && --countdown > 0) return;
    countdown = kPruneFrames;
    if (LiveCount() == 0 && SavedCount() == 0) return;
    GameUpdateLoop* g = GameUpdateLoop::_singleton;
    if (g == nullptr || g->m_citizenList == nullptr) return;
    std::unordered_set<const void*> alive;
    std::vector<Citizen> living;
    const auto& list = g->m_citizenList->citizens;
    for (int i = 0; i < list._size; ++i) {
        const CitizenComponent* c = list._data[i].Get();
        if (c == nullptr) continue;
        alive.insert(c);
        living.push_back(WhoIs(c));
    }
    Prune(alive);
    // a town with no citizens at all is a game that has not finished loading (or is over): keep the records
    if (!living.empty()) PruneSaved(living);
}

}  // namespace ToolNames
}  // namespace Ext
