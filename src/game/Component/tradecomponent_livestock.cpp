// src/game/Component/tradecomponent_livestock.cpp
// Trade wave — the last unported function in the trade path.
//
//   0x14005ace0  TradeComponent::SpawnPurchasedLivestock
//                (Ghidra's Building_AddBuyToStorage)
//
// This is the LIVESTOCK arm of the trade checkout. When a player buys animals at a
// trading post, the two other arms of 0x140081420 only register the purchase as
// available to the town; this one is what actually puts the animals in the world.
// Until now it was the single null dependency that kept the whole checkout opt-in
// (see the header of src/game/Dialog/tradeui_checkout.cpp).
//
// Ghidra's name for the fn-ptr, "Building_AddBuyToStorage", is a mislabel of the same
// family as the two in tradeui_checkout.cpp: nothing here touches a StorageComponent.
// It clones `qty` entities from the purchased template and scatters them across the
// trading post's own footprint.
//
// ── Shape ────────────────────────────────────────────────────────────────────────
//   param_1 = the TradeComponent (its entity IBP is at +0x10)
//   param_2 = the purchased entity template/description
//   param_3 = how many to spawn (unsigned; the loop is guarded by a SIGNED test)
//
//   1. Find the post's component at +0x10 via 0x140002ed0 and read TWO footprint
//      corners out of it with 0x140122e20(comp, &out, 2, 0|1, NULL).
//   2. Convert both corners to map cells and normalise them into a min/max rect.
//   3. Seed an alternating sex from ONE Rand(2) before the loop.
//   4. Per animal: clone the template, find its AnimalComponent, place it at a random
//      point inside the corner-to-corner box, stamp the roam rect, randomise its
//      behaviour phase, and register the entity with the world.
//
// ── Notes taken from the DISASSEMBLY, not the decompile ──────────────────────────
// This cluster's decompiles render hidden out-params and float constants as noise,
// so every float step
// below was read off the instruction stream at 0x14005af6c-0x14005b0d7:
//
//  * The two RandFloat draws are applied CROSSED — the FIRST scales the delta's Y,
//    the SECOND scales its X (14005afc8 MULSS XMM2,XMM7 / 14005afd2 MULSS XMM1,XMM6).
//    This is the same crossing already documented in pasturecomponent_spawn.cpp. The
//    world RNG at gw+0xd8 is shared, so drawing these in the wrong order desynchronises
//    every later roll in the game.
//  * The delta's Z is multiplied by XMM9, and XMM9 is set by `XORPS XMM9,XMM9`
//    (14005ae8e) — a hard 0.0. Every animal therefore lands at exactly corner-0's Z.
//  * +0x44 (behaviour phase) is RandFloat * animal[+0x48] * *0x140172114 — NOT the bare
//    RandFloat its pasture sibling writes.
//  * The cell round-trip (`idx = size*y + x` then `x = idx % size, y = idx / size`)
//    is retail's, and is a no-op by construction. Kept as-is rather than "simplified",
//    so the port diffs cleanly against the disassembly.
//
// ⚠ THE 0x1400c3470 CALL IS DEAD CODE IN RETAIL, and is ported that way deliberately.
// The guard is:
//        MOV ECX,[RDI+0x20] / MOV EAX,ECX / BTR EAX,0x16 / MOV [RDI+0x20],EAX
//        XOR ECX,EAX / TEST CX,CX / JZ ...
// BTR clears bit 22, so ECX^EAX is either 0 or 0x00400000 — and `TEST CX,CX` looks at
// the LOW 16 BITS ONLY, which are always zero either way. The branch can never be
// taken. This is the same species of retail dead code as 0x140045190 in the boat state
// machine. The flag clear itself is real and load-bearing; only the call is unreachable.
// "Repairing" it to TEST ECX,ECX would fire a visual-refresh call retail never makes.

#include "Component/AnimalComponent.h"
#include "Component/InteractComponent.h"
#include "Component/TradeComponent.h"
#include "Component/ComponentDescription.h"
#include "Entity/ComponentEntity.h"   // ComponentEntity::FindComponent<T>
#include <new>
#include "object/TypeInfo.h"
#include "Game/exe_bodies.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Game/Map.h"              // Map::size
#include <cstdint>
#include <cstdio>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "system/Value.h"   // ENDGAME B.3.4b System caller batch
#include "Component/exe_bodies.h"
#include "Entity/ComponentEntity.h"   // ComponentEntity(ComponentDescription*) (0x1400c3720)
#include "Entity/EntityList.h"        // EntityList::RegisterEntity (0x1400c48a0)
#include "Action/exe_bodies.h"                 // (Pf_EntityResolvePos_2e20 / NotifyEntityChanged went in B6: the typed calls)
#include "Game/exe_constants.h"                // kRdata_140172220 / kRdata_140172114
#include "system/MemoryAllocator.h"

namespace {

// Leaves, by retail address:
//   0x140002ed0 FindComponent<InteractComponent> (footprint)   0x140122e20 Pf_EntityResolvePos_2e20
//   0x1400c3720 ComponentEntity_CtorFromDescription           0x1400c48a0 EntityList_RegisterEntity_Impl
//   0x140107400 AnimalComponent::Place                      0x140107320 AnimalComponent::UpdateBehavior
//   0x1400c3470 ActHunt_NotifyEntityChanged_c3470 (model refresh)
//   .rdata 0x140172220 (yaw spread) / 0x140172114 (phase scale) -> kRdata_ constants.

// Retail's world-coordinate -> cell clamp, repeated verbatim for both corners.
inline int ToCell(float world, float half, int mapSize) {
    int c = static_cast<int>(world + half);      // CVTTSS2SI: truncate toward zero
    if (c < 0) c = 0;
    if (c >= mapSize) c = mapSize - 1;
    return c;
}

}  // namespace

// ============================================================
// 0x14005ace0 — spawn purchased livestock across the post's footprint
// ============================================================
void TradeComponent::SpawnPurchasedLivestock(ComponentDescription* tmpl, unsigned int qty) {
    // ── the post's footprint corners ─────────────────────────────────────────────
    InteractComponent* footprint = _entity->FindComponent<InteractComponent>();   // 0x140002ed0

    // Corner 0 and corner 1 of the footprint. Three floats each: 0x140122e20 writes a
    // full Vector3 through its out-param, and a 2-float array here would let it scribble
    // over whatever local sits next on the stack.
    Math::Vector3 a;
    Math::Vector3 b;
    footprint->NodePosition(&a, 2, 0, nullptr);
    footprint->NodePosition(&b, 2, 1, nullptr);

    GameUpdateLoop* const w = GameUpdateLoop::_singleton;
    if (!w) return;
    const int mapSize = w->m_game->size;   // Map +0x00
    const float half  = static_cast<float>(mapSize >> 1);

    // Retail derives each cell, packs it into a linear index, then immediately unpacks
    // it again. The round-trip is an identity; it is kept so this reads against the
    // disassembly one instruction at a time.
    int minX = ToCell(a.x, half, mapSize);
    int minY = ToCell(a.y, half, mapSize);
    {
        const int idx = mapSize * minY + minX;
        minX = idx % mapSize;
        minY = idx / mapSize;
    }
    int maxX = ToCell(b.x, half, mapSize);
    int maxY = ToCell(b.y, half, mapSize);
    {
        const int idx = mapSize * maxY + maxX;
        maxX = idx % mapSize;
        maxY = idx / mapSize;
    }
    if (maxX < minX) { const int t = minX; minX = maxX; maxX = t; }
    if (maxY < minY) { const int t = minY; minY = maxY; maxY = t; }

    System::Random* rng = w->m_random;

    // ONE Rand(2) before the loop, flipped every iteration, so a purchase of 8 comes
    // out 4/4 rather than 8 independent coin flips.
    bool female = rng->Rand(2u) == 0u;

    if (static_cast<int>(qty) <= 0) return;

    const int rectW = maxX - minX + 1;
    const int rectH = maxY - minY + 1;
    const float yawSpread  = kRdata_140172220;
    const float phaseScale = kRdata_140172114;

    int spawned = 0;
    for (unsigned int i = 0; i < qty; ++i) {
        ComponentEntity* mem = static_cast<ComponentEntity*>(System::MemoryAllocator::MemAlloc(0x48, 8));
        ComponentEntity* ent = mem ? ::new (mem) ComponentEntity(tmpl) : nullptr;
        Object::InternalPtr<ComponentEntity> entIBP(ent);
        if (!ent) continue;

        // Inline AnimalComponent scan. Retail compares GetTypeId (vtable +0x10) against
        // DAT_1401c6f98 itself rather than calling the 0x140005500 finder: the
        // FindComponent<T> shape (first id match, then IsDerivedFrom). The null tests on
        // the component array and a slot are the tree's.
        AnimalComponent* animal = nullptr;
        {
            const int n = ent->_components._size;
            Object::InternalPtr<Component>* list = ent->_components._data;
            for (int c = 0; c < n && list; ++c) {
                Component* cc = list[c].Get();
                if (!cc) continue;
                if (cc->Type() != AnimalComponent::TypeId()) continue;
                if (Object::IsDerivedFromNullSafe(cc, AnimalComponent::TypeId()))
                    animal = static_cast<AnimalComponent*>(cc);
                break;
            }
        }

        // CROSSED, per the disassembly: r1 scales Y, r2 scales X. Both are drawn even
        // when there is no AnimalComponent to place — the shared world RNG must advance
        // exactly as often as retail advances it.
        const float r1 = rng->RandFloat();
        const float r2 = rng->RandFloat();
        const Math::Vector3 pos(
            a.x + (b.x - a.x) * r2,
            a.y + (b.y - a.y) * r1,
            a.z + (b.z - a.z) * 0.0f);    // XORPS XMM9,XMM9 — always corner-0's Z

        const float yaw = rng->RandFloatTol() * yawSpread;
        if (animal)
            animal->Place(&pos, yaw, female);   // 0x140107400
        female = !female;   // alternate AFTER placing, as retail does

        if (animal) {
            // Clear bit 22. The refresh call behind it is unreachable in retail — see
            // the DEAD CODE note in this file's header — but the flag clear is real.
            const uint32_t before = animal->_flags;
            const uint32_t after  = before & ~0x00400000u;
            animal->_flags = after;
            if (static_cast<uint16_t>(before) != static_cast<uint16_t>(after) &&
                animal->_entity != nullptr) {
                animal->_entity.Get()->RefreshEntityFlags();   // 0x1400c3470
            }

            // The roam rect: where this animal is allowed to wander.
            animal->wanderX      = static_cast<int16_t>(minX);
            animal->wanderY      = static_cast<int16_t>(minY);
            animal->wanderWidth  = static_cast<int16_t>(rectW);
            animal->wanderHeight = static_cast<int16_t>(rectH);

            const float span = animal->deathAge;
            animal->age = rng->RandFloat() * span * phaseScale;
            animal->UpdateBehavior();   // 0x140107320
        }

        Object::InternalPtr<ComponentEntity> regIBP = entIBP;
        w->m_entityList->RegisterEntity(&regIBP,
                                       static_cast<int>(tmpl->_category));
        ++spawned;
    }

    // OUTCOME, not intent: "the spawn ran" and "animals exist" are separate claims.
    std::fprintf(stderr, "[TradeLivestock] spawned %d/%u animal(s) from template %p "
                 "into rect cell(%d,%d) %dx%d\n",
                 spawned, qty, static_cast<void*>(tmpl), minX, minY, rectW, rectH);
    std::fflush(stderr);
}

// 1 outside caller: Dialog/tradeui_checkout.cpp (the checkout's livestock arm).
