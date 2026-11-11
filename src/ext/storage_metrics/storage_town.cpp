// ============================================================================
// src/ext/storage_metrics/storage_town.cpp -- ext.storage_metrics: the town's storage buildings, summed
// ============================================================================
// NOT RETAIL. docs/extensions/handins/STORAGEMETRICS.md. Read only: nothing here writes game state.
//
// The buildings are the town's own two lists: storageLocations (a StorageLocationComponent adds its
// building when it is finished: stockpiles, barns, a mod's stores) and tradePosts (a TradeComponent adds
// its building when it is placed). A building that is not standing finished (a trading post still being
// built, a store being taken down) is left out: its space is not the town's to use.
// So is a staffed workplace (a WorkPlaceComponent with a profession) that is a storage location for its
// own goods.
// ============================================================================
#include "ext/storage_metrics/storage_metrics.h"

#include "ui/StringTable.h"
#include "ui/String.h"
#include "object/External.h"
#include "object/Internal.h"
#include "object/TypeInfo.h"
#include "system/String.h"

#include "Component/ComponentDescription.h"
#include "Component/StateMachineComponent.h"
#include "Component/StorageComponent.h"
#include "Component/StorageDescription.h"
#include "Component/ToolbarDescription.h"
#include "Component/TradeComponent.h"
#include "Component/WorkPlaceComponent.h"
#include "Entity/CitizenBehavior.h"
#include "Entity/ComponentEntity.h"
#include "Game/GameUpdateLoop.h"

#include <cstdint>
#include <string>

namespace Ext {
namespace StorageMetrics {

namespace {

// A toolbar string: its table's text, or the key itself when the table does not have it.
std::string TextOf(UI::StringTable* table, const System::String& key) {
    const std::string raw = key._data != nullptr ? std::string(key._data) : std::string();
    if (table == nullptr || raw.empty()) {
        return raw;
    }
    const UI::String s(Object::ExternalPtr<UI::StringTable>(table), key);
    if (!s.IsValid()) {
        return raw;
    }
    const System::String text = s;
    return text._data != nullptr ? std::string(text._data) : raw;
}

// The building type's title, as the toolbar and the building's own panel show it; empty when the template
// has none (the tab then names the line by its kind).
std::string TitleOf(const ComponentDescription* tmpl) {
    const ToolbarDescription* toolbar = tmpl != nullptr ? tmpl->FindDescription<ToolbarDescription>() : nullptr;
    if (toolbar == nullptr) {
        return std::string();
    }
    return TextOf(toolbar->_stringTable, toolbar->_str1);
}

// Standing finished (storage_math.h: Standing), by the state its state machine was last asked for; a
// building without a state machine has no building site and counts.
bool Finished(ComponentEntity* entity) {
    StateMachineComponent* state = entity->FindComponent<StateMachineComponent>();
    if (state == nullptr) {
        return true;
    }
    return Standing(state->stateMachine.currentInput);
}

void AddBuilding(Snapshot& out, ComponentEntity* entity) {
    if (entity == nullptr) {
        return;
    }
    StorageComponent* storage = entity->FindComponent<StorageComponent>();
    if (storage == nullptr || !Finished(entity)) {
        return;
    }
    // A staffed workplace that is a storage location for its own goods (the game's tavern; with some mods
    // the hunting cabin, the herbalist, the forester) is not room the town can store goods in. A barn's
    // workplace has no profession.
    const bool hasTrade = entity->FindComponent<TradeComponent>() != nullptr;
    const WorkPlaceComponent* workplace = entity->FindComponent<WorkPlaceComponent>();
    if (!hasTrade && workplace != nullptr && workplace->profession != nullptr) {
        return;
    }
    bool areaBased = false;
    if (::Object::IsDerivedFromNullSafe(storage->_description, StorageDescription::TypeId())) {
        // +0x20, the kit's _areaBasedLimit: StorageComponent::OnAdded multiplies the limit by the footprint
        areaBased = static_cast<const StorageDescription*>(storage->_description)->_flagA;
    }
    const ComponentDescription* tmpl = entity->_description;
    out.Add(tmpl, TitleOf(tmpl), Classify(hasTrade, areaBased), storage->QueryCount(), storage->capacity);
}

}  // namespace

Snapshot Gather() {
    Snapshot out;
    if (GameUpdateLoop::_singleton == nullptr || GameUpdateLoop::_singleton->m_citizenList == nullptr) {
        return out;
    }
    CitizenBehavior* town = GameUpdateLoop::_singleton->m_citizenList;
    for (int i = 0; i < town->storageLocations._size; ++i) {
        ComponentEntity* entity = town->storageLocations._data[i].Get();
        // a trading post that is a storage location too (a mod's) is counted once, from the trade list
        if (entity != nullptr && entity->FindComponent<TradeComponent>() != nullptr) {
            continue;
        }
        AddBuilding(out, entity);
    }
    for (int i = 0; i < town->tradePosts._size; ++i) {
        AddBuilding(out, town->tradePosts._data[i].Get());
    }
    return out;
}

}  // namespace StorageMetrics
}  // namespace Ext
