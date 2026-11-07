// src/ext/recipe_skip/recipe_skip.cpp -- ext.recipe_skip (NOT RETAIL)
//
// docs/extensions/handins/RECIPESKIP.md. Reads the town's material table the way Work/consumeproduce.cpp's
// depletion test does: a recipe's material is at its limit when the town's count is at or above the limit
// the player set (ProfessionResourceEntry::currentCount against maxCount).
#include "ext/recipe_skip/recipe_skip.h"

#include "ext/recipe_skip/recipe_skip_math.h"

#include "Component/ConsumeProduceComponent.h"
#include "Component/ConsumeProduceDescription.h"
#include "Entity/CitizenBehavior.h"
#include "Game/GameUpdateLoop.h"
#include "object/TypeInfo.h"
#include "system/Value.h"   // System::Random

namespace Ext {
namespace RecipeSkip {

namespace {

// The recipes of the workshop and which of them are at their limit; false when the town is not readable or
// the workshop has more recipes than the arrays hold. The count comes back in `count`.
bool Read(const ConsumeProduceComponent* component, float* weights, char* atLimit, size_t& count) {
    GameUpdateLoop* game = GameUpdateLoop::_singleton;
    if (component == nullptr || game == nullptr || game->m_citizenList == nullptr) {
        return false;
    }
    const Description* base = component->_description;
    if (!::Object::IsDerivedFromNullSafe(base, ConsumeProduceDescription::TypeId())) {
        return false;
    }
    const ConsumeProduceDescription* description = static_cast<const ConsumeProduceDescription*>(base);
    const int recipes = description->_recipeCount;
    const ProfessionResourceEntry* materials = game->m_citizenList->professionResources._data;
    if (recipes <= 0 || !FitsRecipes(static_cast<size_t>(recipes)) || materials == nullptr ||
        component->consumeRates._size < recipes) {
        return false;
    }
    count = static_cast<size_t>(recipes);
    for (size_t i = 0; i < count; ++i) {
        weights[i] = component->consumeRates._data[i];
        const ProfessionResourceEntry& material = materials[description->_recipes[i].materialType];
        atLimit[i] = material.maxCount <= material.currentCount ? 1 : 0;
    }
    return true;
}

}  // namespace

bool AllAtLimit(const ConsumeProduceComponent* component, bool& all) {
    float weights[kMaxRecipes];
    char atLimit[kMaxRecipes];
    size_t count = 0;
    if (!Read(component, weights, atLimit, count)) {
        return false;
    }
    all = ::Ext::RecipeSkip::AllAtLimit(atLimit, count);
    return true;
}

int Pick(const ConsumeProduceComponent* component) {
    float weights[kMaxRecipes];
    char atLimit[kMaxRecipes];
    size_t count = 0;
    if (!Read(component, weights, atLimit, count)) {
        return -1;
    }
    const float r = GameUpdateLoop::_singleton->m_random->RandFloat();
    return PickRecipe(weights, atLimit, count, r);
}

}  // namespace RecipeSkip
}  // namespace Ext
