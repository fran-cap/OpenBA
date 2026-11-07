// src/game/Action/action_child_ctors2.cpp
// FUN_140119470: the weighted-random product index (no class yet; B.4 inventory A
// attributes it, low confidence, to ConsumeProduceUI).
//
// B.4 R0-EXE: the rest of this file moved, text unchanged, to the classes' files:
//   CompleteRoad door 0x1400c59d0                          -> completeroad.cpp
//   HuntAnimal ctor + door 0x1400c8400                     -> huntanimal.cpp
//   DropOffRawMaterial ctor + door 0x1400c7cd0             -> dropoffrawmaterial.cpp
//   ConsumeRawMaterial ctors + doors 0x1400c63b0 / 0x1400c6440 and its
//     consume-list append 0x1400c64b0                     -> consumerawmaterial.cpp
//   PickupStoredRawMaterial ctor + door 0x1400ca520 and its
//     carrier-list transfer 0x1400ca230                   -> pickupstoredrawmaterial.cpp
//   the StateMachineComponent find 0x14000b6f0 (ComponentEntity FindComponent<T>)
//                                                          -> Entity/entity_findcomponent.cpp
#include "Action/exe_bodies.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include <cstdint>
#include "system/Value.h"      // ENDGAME B.3.4b System callers
#include "Component/ConsumeProduceComponent.h"
#include "ext/registry.h"                 // EXT SEAM (NOT RETAIL, RECIPESKIP.md): Ext::Ext()
#include "ext/recipe_skip/recipe_skip.h"   // EXT SEAM (NOT RETAIL, RECIPESKIP.md): the draw

// ============================================================
// FUN_140119470 — weighted-random index over a float-weight array.
// If *(int*)(param_1+0x50) == -1: sum the weights[ +0x58 ] (count +0x60), draw
// r = RandFloat(world.random), walk the weights again returning the first index where
// the running sum exceeds r*total. Otherwise (or no weights) returns 0.
// (Returns ulonglong index; non-negative *(int*)(+0x50) returns that value.)
// Row 443 (B7 RES): the ONE port of 0x140119470 (Dialog's fn_GetCurrentProductIndex is
// gone, consumeproduceui.cpp calls this). 0x140119470 reads [[gw]+0xd8] and calls RandFloat
// on it with no test; the tree's null-game / null-Random 0.0 draw is gone.
// ============================================================
uint64_t ConsumeProduceComponent::PickConsumeRecipe() const {
    uint32_t sel = static_cast<uint32_t>(consumeResourceId);
    uint64_t result = sel;
    if ((int)sel < 0) {
        if (sel == 0xffffffff) {
            // EXT SEAM (NOT RETAIL, docs/extensions/handins/RECIPESKIP.md): with the row on, the same
            // weighted draw (one random number) leaves out the recipes whose material is at its limit.
            if (::Ext::Ext().recipe_skip) {
                const int pick = ::Ext::RecipeSkip::Pick(this);
                if (pick >= 0) {
                    return static_cast<uint64_t>(pick);
                }
            }
            int n = consumeRates._size;
            float total = 0.0f;
            if (0 < n) {
                const float* w = consumeRates._data;
                for (int k = 0; k < n; ++k) total += w[k];
            }
            System::Random* random = GameUpdateLoop::_singleton->m_random;
            float r = random->RandFloat();
            result = 0;
            if (0 < n) {
                const float* w = consumeRates._data;
                float running = 0.0f;
                for (int k = 0; k < n; ++k) {
                    running += w[k];
                    if (r * total < running) return (uint64_t)k;
                }
            }
        }
        result = 0;
    }
    return result;
}

