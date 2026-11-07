// src/ext/recipe_skip/recipe_skip_math.h -- ext.recipe_skip's pick (NOT RETAIL)
//
// Standard C++ with no game types, so the unit tests build it (tests/unit/ext_recipe_skip_test.cpp).
// docs/extensions/handins/RECIPESKIP.md.
//
// A workshop that is set to "every recipe" (the description's default recipe is -1) draws a recipe at random
// each time it works, weighted by the building's per-recipe rates. Retail stops the whole building when ANY
// recipe's material is at its limit. With the extension on a recipe at its limit is left out of the draw, and
// the building only stops when every recipe is at its limit.
#pragma once

#include <cstddef>

namespace Ext {
namespace RecipeSkip {

// The most recipes the pick handles (the callers keep fixed arrays of this size: no allocation per call). A
// workshop with more is left to retail's draw.
constexpr size_t kMaxRecipes = 32;

// True when `count` recipes can be handled: at least one, and no more than kMaxRecipes.
inline bool FitsRecipes(size_t count) {
    return count > 0 && count <= kMaxRecipes;
}

// True when there is at least one recipe and every one of them is at its limit.
inline bool AllAtLimit(const char* atLimit, size_t count) {
    if (count == 0) {
        return false;
    }
    for (size_t i = 0; i < count; ++i) {
        if (atLimit[i] == 0) {
            return false;
        }
    }
    return true;
}

// Retail's weighted draw (the first recipe where the running sum of the weights passes r * total, r in
// [0, 1)), over the recipes that are not at their limit. When every recipe is at its limit the draw is
// retail's: over all of them.
inline int PickRecipe(const float* weights, const char* atLimit, size_t count, float r) {
    if (count == 0) {
        return 0;
    }
    const bool skipping = !AllAtLimit(atLimit, count);
    float total = 0.0f;
    for (size_t i = 0; i < count; ++i) {
        const bool left = skipping && atLimit[i] != 0;
        if (!left) {
            total += weights[i];
        }
    }
    float running = 0.0f;
    int last = 0;
    for (size_t i = 0; i < count; ++i) {
        const bool left = skipping && atLimit[i] != 0;
        if (left) {
            continue;
        }
        running += weights[i];
        last = static_cast<int>(i);
        if (r * total < running) {
            return last;
        }
    }
    return last;
}

}  // namespace RecipeSkip
}  // namespace Ext
