// src/game/Game/town_workplace_registry.cpp
//
// Phase-1 economy foundation: the TownManager per-profession workplace registry
// (townMgr+0x100 AVL BST). Faithful 1:1 port of the exe's lazy find-or-create
// node insert. This is the piece that makes spawned workplaces FINDABLE by the
// citizen employment finders (FindAvailableBuilding 0x14008d100 / FindWorkplace
// core sub_1930 0x140111930), which is the gate for citizens to get jobs and stop
// random-wandering. See docs/history/plans/ECONOMY_PLAN.md + reference_town_registry_node_create.
//
// The AVL insert cluster it depends on is ALREADY ported+registered (Keystone A):
//   0x140017230  wrapper (ResidenceComponent_impl_7230, keystone_a_restree.cpp)
//   0x1400172f0  find-or-create   0x140017460 node-alloc   0x140014b40 rebalance
//   0x140003110  growable-array 8-byte value append (ActHunt_DynArraySetCount_3110)
// TownManager (= Ghidra-mislabeled TradeComponent) = *(gameWorld+0xf0); ctor
// 0x140090a30 (already Load step 10). Node = 0x38 bytes: +0x00 key(profession id) /
// +0x08 array.data / +0x10 count / +0x14 cap / +0x18 parent / +0x20 left /
// +0x28 right / +0x30 balance.

#include "Action/exe_bodies.h"    // ActHunt_DynArraySetCount_3110
#include "Component/exe_bodies.h" // ResidenceComponent_impl_7230, AttendanceComponent_GetJobHostComponent
#include "Tutorial/exe_bodies.h"  // (WorkComponent_CalcMaxWorkers went in B6: the member is called)
#include <new>
#include "Entity/exe_bodies.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Game/GameUpdateLoop.h"   // caller batch CB6: the world's named fields
#include "object/TypeInfo.h"                 // caller batch CB6: Object::Base::Type()
#include "Component/WorkPlaceComponent.h"   // caller batch CB6: TypeId()
#include "Component/WorkComponent.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "system/Value.h"      // ENDGAME B.3.4b System caller batch (Random/Time/Color)
#include "Game/exe_bodies.h"
#include "Entity/CitizenBehavior.h"   // the town manager (GameUpdateLoop +0xF0)
#include "Entity/ComponentEntity.h"

// Called by name:
//   0x140017230 ResidenceComponent_impl_7230 -- Keystone-A insert wrapper; returns param_2
//               (&out), *out = the found/created node (Component/exe_bodies.h)
//   0x140003110 ActHunt_DynArraySetCount_3110 -- growable 8-byte-element array: set count
//               to newCount + append *elem (Action/exe_bodies.h)
//   0x140089470 TownRegistry_ShuffleProfessionList -- per-profession building-list
//               shuffle; FindAvailableBuilding calls it unconditionally, as retail
//               0x14008d11b (B8 RES, row 1061: the BANISHED_LIVE_SEAT_SHUFFLE gate is gone)
//   0x14012e5c0 WorkComponent_CalcMaxWorkers -- max worker slots (Tutorial/exe_bodies.h;
//               the registered, live one of the two bodies of this address)
//   0x140002e10 AttendanceComponent_GetJobHostComponent -- find the job host into an
//               IBP (Component/exe_bodies.h)

namespace {

// CitizenBehavior's +0x100 profession tree is a Collection::Set of
// CitizenBucket<Profession> (Entity/CitizenBehavior.h, B.4 X5-COMP): the node's
// key is the profession, compared as an unsigned qword (a pointer compare), its
// value the Array<InternalPtr<ComponentEntity>> of workplace buildings.
ProfessionBucket* TreeRoot(const CitizenBehavior* town) {
    return town->workplacesByProfession._root;
}
} // namespace

// ============================================================
// 0x1400895a0 — ResidenceComponent_impl_95a0 = AddWorkplaceToProfessionTree.
//   param_1 = townMgr (*(gameWorld+0xf0))
//   param_2 = InternalBasePtr* holding the workplace entity value (mirrors
//             WorkPlaceComponent_OnCreate's local copy of *(WPC+0x10))
//   param_3 = the profession (WorkPlaceComponent+0x48); the tree key is its pointer
// Walk the +0x100 BST by key; find-or-create the profession node; dedup the
// workplace against the node's entity array; append if absent. Faithful 1:1.
// ============================================================
void CitizenBehavior::RegisterWorkplace(ComponentEntity** param_2, const Profession* profession) {
    // BST search for the key (key<node -> left[+0x20]; key>node -> right[+0x28]; == -> found).
    // The key is the profession's address, compared unsigned (a pointer compare).
    ProfessionBucket* node = TreeRoot(this);                           // root
    ProfessionBucket* found = nullptr;                                 // puVar3
    while (node != nullptr) {
        if (profession < node->element.key) {          // *node = key at +0x00
            node = node->left;                                         // left (+0x20)
        } else if (profession <= node->element.key) {
            found = node;                                              // exact match
            break;
        } else {
            node = node->right;                                        // right (+0x28)
        }
    }

    // Key absent -> create the profession node (empty entity array).
    if (found == nullptr) {
        ProfessionBucket* out = nullptr;
        CitizenBucketEntities emptyRec = {};   // array record: +0x00 data / +0x08 count+cap
        InsertCitizenBucket(&workplacesByProfession, &out, &profession, &emptyRec);   // 0x140017230
        found = out;     // *out = the new node
        if (found == nullptr) return;
    }

    // Dedup the workplace ref against the node's entity array (base +0x08, count +0x10).
    // param_2 points at the caller's InternalPtr<ComponentEntity> local.
    int count = found->element.entities._size;        // +0x10 count
    Object::InternalPtr<ComponentEntity>* arrBase = found->element.entities._data;   // +0x08 array data
    for (int i = 0; i < count; ++i) {
        if (arrBase[i] == *param_2) {
            return;   // already registered — no duplicate
        }
    }

    // Append: 003110(&node.array (node+0x08), count+1, workplaceRef).
    found->element.entities.SetSizeFill(count + 1, param_2);   // 0x140003110
}

// ------------------------------------------------------------
// Convenience for the standalone spawn-time registration wiring: given a live
// WorkPlaceComponent, register its owner building into the profession BST. Mirrors
// WorkPlaceComponent_OnCreate's copy of *(WPC+0x10) into a stack local before insert.
// Skips key==0 (residence-roster WorkPlaceComponents, e.g. houses — not employers).
// ------------------------------------------------------------
extern "C" void TownRegistry_RegisterWorkplace(CitizenBehavior* townMgr, const WorkPlaceComponent* wpc) {
    if (townMgr == nullptr || wpc == nullptr) return;
    const Profession* key = wpc->profession;
    if (key == nullptr) return;   // no profession -> not an employer workplace
    ComponentEntity* refLocal = wpc->_entity.Get();
    townMgr->RegisterWorkplace(&refLocal, key);
}

// ============================================================
// 0x140089470 — TownRegistry_ShuffleProfessionList. Fisher-Yates shuffle of ONE
// profession node's building array, in place. FindAvailableBuilding calls it as its
// step 1, which is what makes retail's "which building gets the next worker" pick
// round-robin instead of always-the-first-registered.
//   param_1 = townMgr   param_2 = professionKey
// Walks the +0x100 BST for the key; bails if absent or count <= 1; otherwise swaps
// each slot i with System::Random::Rand(*(gw+0xd8), count). Faithful 1:1 — note the
// original's redundant `count == 1` / `count < 2` / `count < 1` early-outs, all of
// which collapse to "nothing to shuffle".
//
// NOT the same function as FUN_1400896a0 (Shuffle896a0 in citizen_employ_cascade.cpp)
// — that one shuffles the town HOUSE list for strategy 3. Different array, different
// caller.
//
// Called on every FindAvailableBuilding, as retail (B8 RES, row 1061). Until then it ran
// only under the opt-in BANISHED_LIVE_SEAT_SHUFFLE flag, so seat order was deterministic
// (always the first registered building with room); it is retail's random order now,
// drawing from the game Random (+0xd8) on every seat decision.
// ============================================================
void CitizenBehavior::ShuffleProfessionList(const Profession* professionKey) {
    if (GameUpdateLoop::_singleton == nullptr) return;

    // BST search for the key (same walk as the reader below).
    ProfessionBucket* node = TreeRoot(this);
    ProfessionBucket* found = nullptr;
    while (node != nullptr) {
        if (professionKey < node->element.key)       node = node->left;   // left
        else if (professionKey <= node->element.key) { found = node; break; }
        else                                 node = node->right;  // right
    }
    if (found == nullptr) return;

    uint32_t count = static_cast<uint32_t>(found->element.entities._size);   // +0x10 count
    if (static_cast<int>(count) < 2) return;                 // 0/1 entries -> nothing to do

    System::Random* rng = GameUpdateLoop::_singleton->m_random;
    if (!rng) return;

    for (int64_t i = 0; i < static_cast<int>(count); ++i) {
        uint32_t j = rng->Rand(static_cast<uint32_t>(count));
        if (static_cast<uint32_t>(i) == j) continue;
        Object::InternalPtr<ComponentEntity>* base = found->element.entities._data;   // +0x08 array data
        ComponentEntity* tmp = base[i].Get();
        base[i] = base[static_cast<int>(j)];
        base[static_cast<int>(j)] = tmp;
    }
}

// ============================================================
// 0x14008d100 — FindAvailableBuilding = the READER of the townMgr+0x100 profession
//   BST. Given a professionKey, returns (in *out) an available workplace building
//   entity for that profession — one that (a) has a WorkPlaceComponent whose live
//   worker count (+0x60) is below its WorkComponent's capacity (+0x30), and
//   (b) is currently reachable. If none qualify (or the key is absent / BST empty)
//   it writes an empty InternalBasePtr into *out. Faithful 1:1 port.
//     param_1 = townMgr (*(gameWorld+0xf0))
//     param_2 = out    (InternalBasePtr* the caller receives; also the return value)
//     param_3 = professionKey
// ============================================================
void* CitizenBehavior::FindAvailableBuilding(void* out, const Profession* professionKey) {

    // Step 1: shuffle the profession node's building list, unconditionally (0x14008d11b).
    ShuffleProfessionList(professionKey);   // 0x140089470

    // Step 2: walk the +0x100 profession BST for the key (key<node -> left[+0x20];
    // key>node -> right[+0x28]; == -> found).
    ProfessionBucket* node = TreeRoot(this);   // root
    ProfessionBucket* found = nullptr;
    while (node != nullptr) {
        if (professionKey < node->element.key) {          // *node = key at +0x00
            node = node->left;                            // left  (+0x20)
        } else if (professionKey <= node->element.key) {
            found = node;                                 // exact match
            break;
        } else {
            node = node->right;                           // right (+0x28)
        }
    }


    // Key absent -> empty result.
    if (found == nullptr) {
        new (out) ::Object::InternalBasePtr(nullptr);
        return out;
    }

    // Step 3: scan the profession node's building array (base +0x08, count +0x10).
    int count = found->element.entities._size;             // +0x10 count
    for (int i = 0; i < count; ++i) {
        // building entity ref (InternalBasePtr value) at node.arr[i].
        // Row 99 (B7 RES): retail 0x14008d100 tests none of the building, the
        // WorkPlaceComponent, its owner or the WorkComponent: the two inline scans are
        // FindComponent<T>'s (0x14008d1e0-0x14008d202 / 0x14008d255-0x14008d277, `[[slot]+0x10]`
        // with no null-slot test), and +0x60 / +0x10 / +0x30 are read through the results.
        ComponentEntity* building = found->element.entities._data[i].Get();

        // 3a: find its WorkPlaceComponent; wrap in an IBP (matches original).
        ::Object::InternalPtr<WorkPlaceComponent> wpcIBP(building->FindComponent<WorkPlaceComponent>());
        WorkPlaceComponent* wpcPtr = wpcIBP.Get();

        int workerCount = wpcPtr->workers._size;            // WPC+0x60 = live workers

        // owning entity is the embedded IBP at WPC+0x10 — CheckPtr then deref.
        ComponentEntity* owner = wpcPtr->_entity.Get();

        // 3b: find the WorkComponent on the owning entity; wrap in an IBP.
        ::Object::InternalPtr<WorkComponent> wcIBP(owner->FindComponent<WorkComponent>());
        WorkComponent* wcPtr = wcIBP.Get();

        // capacity = WorkComponent+0x30; lazily compute+cache via CalcMaxWorkers.
        int capRaw = wcPtr->workState;
        int capacity = capRaw;
        if (capacity == 0) {
            // The B4 FORWARD (its two guards before the member's body stay: known_port_issues row 80).
            capacity = wcPtr->CalcMaxWorkers();   // 0x14012e5c0
            wcPtr->workState = capacity;
        }

        // 3c: room available -> reachability probe; if reachable, this is our pick.
        char accepting = -1;
        bool reachOk = false;
        if (workerCount < capacity) {
            ::Object::InternalPtr<WorkPlaceComponent> host;   // the 16-byte scratch (matches local_50)
            {
                // 0x140002e10 = FindComponent<WorkPlaceComponent> into its return buffer.
                host = building->FindComponent<WorkPlaceComponent>();
                accepting = host->isAccepting;
                reachOk = (accepting != 0);
                if (reachOk) {
                    new (out) ::Object::InternalPtr<ComponentEntity>(building);   // *out = building entity ptr
                    return out;
                }
            }
        }
    }

    // No qualifying building -> empty result.
    new (out) ::Object::InternalBasePtr(nullptr);
    return out;
}
