// ============================================================================
// tests/unit/ext_recipe_skip_test.cpp -- ext.recipe_skip's pick
// ============================================================================
// src/ext/recipe_skip/recipe_skip_math.h without the game.
// ============================================================================
#include "unit.h"
#include "ext/recipe_skip/recipe_skip_math.h"

#include <vector>

using namespace Ext::RecipeSkip;

namespace {

// Retail's draw (Action/action_child_ctors2.cpp), written out again: the first recipe where the running sum
// of the weights passes r * total, else 0.
int RetailPick(const std::vector<float>& weights, float r) {
    float total = 0.0f;
    for (float w : weights) {
        total += w;
    }
    float running = 0.0f;
    for (size_t i = 0; i < weights.size(); ++i) {
        running += weights[i];
        if (r * total < running) {
            return static_cast<int>(i);
        }
    }
    return 0;
}

}  // namespace

UNIT_TEST(ext_recipe_skip_with_none_at_limit_is_retails_draw) {
    const std::vector<float> weights = {1.0f, 1.0f, 1.0f};
    const std::vector<char> atLimit = {0, 0, 0};
    for (int step = 0; step < 100; ++step) {
        const float r = static_cast<float>(step) / 100.0f;
        CHECK_EQ(PickRecipe(weights.data(), atLimit.data(), weights.size(), r), RetailPick(weights, r));
    }
}

UNIT_TEST(ext_recipe_skip_never_picks_a_recipe_at_its_limit) {
    // royal jelly (0) is at its limit; honey (1) and beeswax (2) are not
    const std::vector<float> weights = {1.0f, 1.0f, 1.0f};
    const std::vector<char> atLimit = {1, 0, 0};
    int seen[3] = {0, 0, 0};
    for (int step = 0; step < 100; ++step) {
        const float r = static_cast<float>(step) / 100.0f;
        ++seen[PickRecipe(weights.data(), atLimit.data(), weights.size(), r)];
    }
    CHECK_EQ(seen[0], 0);
    CHECK_EQ(seen[1], 50);
    CHECK_EQ(seen[2], 50);
}

UNIT_TEST(ext_recipe_skip_keeps_the_weights_of_the_recipes_left) {
    // weights 1 : 3 among the two left
    const std::vector<float> weights = {5.0f, 1.0f, 3.0f};
    const std::vector<char> atLimit = {1, 0, 0};
    int seen[3] = {0, 0, 0};
    for (int step = 0; step < 100; ++step) {
        const float r = static_cast<float>(step) / 100.0f;
        ++seen[PickRecipe(weights.data(), atLimit.data(), weights.size(), r)];
    }
    CHECK_EQ(seen[0], 0);
    CHECK_EQ(seen[1], 25);
    CHECK_EQ(seen[2], 75);
}

UNIT_TEST(ext_recipe_skip_every_recipe_at_its_limit_is_retails_draw_and_a_stop) {
    const std::vector<float> weights = {1.0f, 2.0f};
    const std::vector<char> atLimit = {1, 1};
    CHECK(AllAtLimit(atLimit.data(), atLimit.size()));
    for (int step = 0; step < 100; ++step) {
        const float r = static_cast<float>(step) / 100.0f;
        CHECK_EQ(PickRecipe(weights.data(), atLimit.data(), weights.size(), r), RetailPick(weights, r));
    }
}

UNIT_TEST(ext_recipe_skip_all_at_limit_needs_every_recipe) {
    const char none[1] = {0};
    const char some[2] = {1, 0};
    const char zeros[2] = {0, 0};
    const char one[1] = {1};
    CHECK(!AllAtLimit(none, 0));
    CHECK(!AllAtLimit(some, 2));
    CHECK(!AllAtLimit(zeros, 2));
    CHECK(AllAtLimit(one, 1));
}

UNIT_TEST(ext_recipe_skip_more_recipes_than_the_cap_are_left_to_retail) {
    CHECK(!FitsRecipes(0));
    CHECK(FitsRecipes(1));
    CHECK(FitsRecipes(kMaxRecipes));
    CHECK(!FitsRecipes(kMaxRecipes + 1));
}

UNIT_TEST(ext_recipe_skip_the_last_recipe_is_found_at_the_top_of_the_range) {
    const std::vector<float> weights = {1.0f, 1.0f, 1.0f};
    const std::vector<char> atLimit = {0, 1, 0};
    CHECK_EQ(PickRecipe(weights.data(), atLimit.data(), weights.size(), 0.999999f), 2);
    CHECK_EQ(PickRecipe(weights.data(), atLimit.data(), weights.size(), 0.0f), 0);
}
