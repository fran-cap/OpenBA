# RECIPESKIP hand-in (2026-11-06)

Branch `ext-recipe-skip`, on `main` e70c566b. The task (the user's): a workshop whose recipes have different limits should skip a recipe that has reached its limit and work on the next, instead of stopping. **Result: `ext.recipe_skip`, class B, off by default.**

## 1. Why
A workshop description with a default recipe of -1 ("every recipe") draws a recipe at random each time it works. Retail stops the whole building as soon as ONE recipe's material is at its limit (the limit the player sets with the building's spinners). A mod author who wants one building to make, say, honey, beeswax and royal jelly therefore cannot give them different limits: MegaMod's Village Bee Shelter gates royal jelly by Herbs, honey and beeswax by Food, and its third limit spinner is left unused on purpose, because a different limit on one recipe would stop the honey too.

## 2. What retail does (read from the port, not run)
`ConsumeProduceComponent::consumeResourceId` starts as the description's default recipe (`ConsumeProduceDescription::_defaultRecipe`, +0x4c); -1 is the every-recipe mode. In a template's `.rsc` that field is `int _initialProduct` (MegaMod's `DSSVApiaryBeeShelter1.rsc` sets `_initialProduct = -1`; the vanilla workshops leave it out, so the constructor's default of 0, the first recipe, applies). The mapping is confirmed by the game test in section 5: the extension only acts when `consumeResourceId` is -1, and it changed that building's behavior.

* `ConsumeProduceComponent::PickConsumeRecipe` (`Action/action_child_ctors2.cpp`, GOG `0x140119470`) is a weighted random draw over `consumeRates` (all 1.0 at start: uniform). One `RandFloat` per call, redrawn on every call; it is not a rotation.
* `Work/consumeproduce.cpp`'s Execute draws once, then, in the every-recipe mode, loops over ALL recipes and sets "depleted" when any recipe's material has `currentCount >= maxCount` in the town's material table (`CitizenBehavior::professionResources`, indexed by the recipe's `materialType`). Depleted shows the status icon and the building makes nothing.
* `Work/work_act_consume.cpp` draws again, separately, to choose whose inputs the citizen takes. Retail can already disagree with itself between the two draws; the extension does not change that.

## 3. What the extension does
With the row on, in the every-recipe mode only:

* `PickConsumeRecipe` does the same weighted draw (one random number) over the recipes whose material is below its limit. When every recipe is at its limit the draw is retail's, over all of them.
* the depletion test counts the building as depleted only when every recipe is at its limit.

A building set to one recipe, and any building with the row off, is untouched. The weights are the building's own.

| file | role |
|---|---|
| `src/ext/recipe_skip/recipe_skip_math.h` | the draw and the all-at-limit test (pure, unit tested) |
| `src/ext/recipe_skip/recipe_skip.h/.cpp` | reads the building's recipes and the town's material table |
| `tests/unit/ext_recipe_skip_test.cpp` | the draw |

Seams: one in `ConsumeProduceComponent::PickConsumeRecipe` (`action_child_ctors2.cpp`) and one in the depletion test of `Work/consumeproduce.cpp`. Registration: `registry.cpp/.h`, the row's icon (`tools/ext_icons/recipeskip.py`, `ExtensionsSprites.rsc`, the baked sheet).

## 4. Class
B: nothing new is saved (the every-recipe pick is not persisted), but with the row on a town produces differently from retail, so the row is locked for the game and the save records the extension. Off by default.

## 5. Not done / not looked at
* **Looked at in a game by the author** (Windows, DX11, release build): MegaMod's Village Bee Shelter with the same setup on two builds. With the row on it ran a full game year without stopping, and its last season shows honey, beeswax and comb honey made and no royal jelly (the one at its limit); on a build without the row the same setup stopped almost at once. A building set to one recipe, with the row on, still reaches its own limit as before.
* The panel's product combo (`consumeproduceui.cpp:190`) calls `PickConsumeRecipe` for every building. So the combo of an every-recipe building shows a random recipe (retail does the same), and with the row on it never shows one that is at its limit.
* When the extension cannot read the recipes (none, no material table, or more than 32 of them: the pick keeps fixed arrays, no allocation per call), `Pick` returns -1 without drawing and `AllAtLimit` returns false, and the seams go on into retail's own draw and retail's own depletion test.
* The Windows gate and the resave check (the maintainer's); Vulkan, Metal, Linux, Android, macOS.
