// src/ext/item_limits/item_limits_town.cpp -- NOT RETAIL. ext.item_limits (class C): the game side. The
// factory walk that finds the item templates, the path an item is listed by, the once-per-frame BeginFrame,
// the per-tick count of the limited items and the workshop's test. See item_limits.h and
// docs/extensions/handins/ITEMLIMITS.md.
#include "ext/item_limits/item_limits.h"

#include "Component/ComponentDescription.h"
#include "Component/AnimalComponent.h"
#include "Component/AnimalDescription.h"
#include "Component/CitizenComponent.h"
#include "Component/ConsumeProduceDescription.h"
#include "Component/HerbalistDescription.h"
#include "Component/NaturalResourceDescription.h"
#include "Component/LivestockComponent.h"
#include "Component/PastureComponent.h"
#include "Component/RawMaterialDescription.h"
#include "Component/StorageComponent.h"
#include "Entity/CitizenBehavior.h"
#include "Entity/ComponentEntity.h"
#include "ext/registry.h"   // Ext::Ext().item_limits (the game's locked value), Ext::LockEpoch
#include "object/External.h"
#include "object/TypeInfo.h"
#include "system/Filename.h"

#include <cstdio>

namespace Ext {
namespace ItemLimits {

std::string TemplatePath(const void* external) {
    if (external == nullptr) {
        return std::string();
    }
    const System::Filename path = static_cast<const ::Object::External*>(external)->GetPath();
    return path._data != nullptr ? std::string(path._data) : std::string();
}

namespace {

// Every loaded item template (a ComponentDescription carrying a RawMaterialDescription) with the path of its
// RawMaterialDescription. Walked only when the list or the lock epoch changes.
std::vector<Candidate> AllItems() {
    std::vector<Candidate> out;
    ::Object::ExternalFactory* factory = ::Object::ExternalFactory::_singleton;
    if (factory == nullptr) {
        return out;
    }
    const uint32_t tid = ComponentDescription::TypeId();
    const ::Object::External* prev = nullptr;
    int guard = 0;
    while ((prev = factory->FindNext(tid, prev)) != nullptr && guard++ < 100000) {
        if (!::Object::IsDerivedFromNullSafe(prev, tid)) {
            continue;
        }
        const ComponentDescription* item = static_cast<const ComponentDescription*>(prev);
        const RawMaterialDescription* material = item->FindDescription<RawMaterialDescription>();
        if (material == nullptr) {
            continue;
        }
        Candidate c;
        c.item = item;   // the pointer StorageComponent::Item::_resource and ConsumeProduceRecipe::product hold
        c.path = TemplatePath(material);
        if (!c.path.empty()) {
            out.push_back(c);
        }
    }
    return out;
}

// The limited items of one storage list of the town (CountResources' two lists).
void CountList(const CitizenIbpList<ComponentEntity>& list) {
    const int64_t n = static_cast<int64_t>(list._size);
    for (int64_t i = 0; i < n; ++i) {
        ComponentEntity* entity = list._data[i].Get();
        StorageComponent* storage = entity != nullptr ? entity->FindComponent<StorageComponent>() : nullptr;
        if (storage == nullptr) {
            continue;
        }
        const int32_t rows = storage->storageSlots._size;
        for (int32_t j = 0; j < rows; ++j) {
            const StorageComponent::Item& row = storage->storageSlots[j];
            Tally(row._resource, row._count);
        }
    }
}

}  // namespace

void BeginFrame() {
    const uint32_t epoch = LockEpoch();
    const bool rowOn = Ext::Ext().item_limits;   // class C: the game's locked value
    if (NeedsWalk(epoch)) {
        Resolve(epoch, Limits().empty() ? std::vector<Candidate>() : AllItems());
    } else if (NeedsResolve(epoch)) {
        RefreshLimits();   // only values changed: no walk
    }
    if (g_rowOn && !rowOn) {
        PanelsForget();   // a panel that closes after the row went off would leave its entry behind
    }
    SetRowActive(rowOn);
}

void CountTownSlow(CitizenBehavior* town) {
    BeginCount();
    CountList(town->storageLocations);
    CountList(town->marketLocations);
}

bool RecipeAtLimitSlow(const ConsumeProduceDescription* desc, bool allRecipes, int active) {
    const ConsumeProduceRecipe* recipes = desc->_recipes;
    if (!allRecipes) {
        return AtLimit(recipes[active].product);
    }
    const int n = desc->_recipeCount;
    for (int i = 0; i < n; ++i) {
        if (AtLimit(recipes[i].product)) {
            return true;
        }
    }
    return false;
}

const AnimalDescription* SlaughterCandidate(const PastureComponent* pasture) {
    // PastureWork::Execute's branch A, up to the description it hands RemoveAnimalActions.
    if (pasture == nullptr || pasture->capacity >= pasture->animals._size) {
        return nullptr;
    }
    const int slot = pasture->FindOldestLivestock();
    if (slot < 0) {
        return nullptr;
    }
    LivestockComponent* livestock = pasture->animals[slot].Get();
    AnimalComponent* animal = livestock != nullptr ? livestock->_entity->FindComponent<AnimalComponent>() : nullptr;
    if (animal == nullptr || ((animal->_flags >> 0x14) & 1) != 0) {
        return nullptr;
    }
    Description* desc = animal->_description;
    if (!::Object::IsDerivedFromNullSafe(desc, AnimalDescription::TypeId())) {
        return nullptr;
    }
    return static_cast<const AnimalDescription*>(desc);
}

bool SlaughterAtLimitSlow(const AnimalDescription* animal) {
    if (animal == nullptr) {
        return false;
    }
    return AllAtLimit(animal->_rawMaterialCount, [animal](int32_t i) {
        return static_cast<const void*>(animal->_rawMaterials[i]);
    });
}

bool SlaughterSkipsYieldSlow(const CitizenComponent* citizen, const void* item) {
    if (citizen == nullptr || !AtLimit(item)) {
        return false;
    }
    ComponentEntity* workplace = citizen->workplace.Get();
    return SkipsYield(workplace != nullptr && workplace->FindComponent<PastureComponent>() != nullptr, item);
}

const ComponentDescription* HerbItem(const ComponentDescription* herb) {
    const NaturalResourceDescription* plant =
        herb != nullptr ? herb->FindDescription<NaturalResourceDescription>() : nullptr;
    return plant != nullptr ? plant->_building : nullptr;
}

bool HerbAtLimitSlow(const ComponentDescription* herb) {
    return AtLimit(HerbItem(herb));   // false for a plant that names no item
}

bool HerbsAtLimitSlow(const HerbalistDescription* desc) {
    if (desc == nullptr) {
        return false;
    }
    return AllAtLimit(desc->_herbCount, [desc](int32_t i) {
        return static_cast<const void*>(HerbItem(desc->_herbs[i]));
    });
}

}  // namespace ItemLimits
}  // namespace Ext
