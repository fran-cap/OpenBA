// src/ext/recipe_skip/recipe_skip.h -- ext.recipe_skip's entry points (NOT RETAIL)
//
// docs/extensions/handins/RECIPESKIP.md. Class B, off by default: a workshop set to "every recipe" skips a
// recipe whose material is at its limit instead of stopping. Two seams: ConsumeProduceComponent::
// PickConsumeRecipe (the draw) and the work's depletion test (Work/consumeproduce.cpp).
#pragma once

class ConsumeProduceComponent;

namespace Ext {
namespace RecipeSkip {

// For a workshop in the every-recipe mode: sets `all` to whether every recipe's material is at its limit.
// Returns false, leaving `all` alone, when it cannot read the recipes (none, or more than kMaxRecipes, or no
// material table): the caller then does retail's own test.
bool AllAtLimit(const ConsumeProduceComponent* component, bool& all);

// For a workshop in the every-recipe mode: retail's weighted draw (one random number) over the recipes that
// are not at their limit. Returns -1, without drawing, when it cannot read the recipes: the caller then does
// retail's own draw.
int Pick(const ConsumeProduceComponent* component);

}  // namespace RecipeSkip
}  // namespace Ext
