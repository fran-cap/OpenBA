#pragma once
// ============================================================================
// src/ext/alloc_tiebreak/alloc_tiebreak.h -- fix.alloc_tiebreak (fix tier, default ON)
// ============================================================================
// NOT RETAIL (docs/retail_bugs.md §14, docs/extensions/handins/ALLOCTIEBREAK.md).
// Retail's employment allocator (PathGraph::AssignCitizens 0x1400d43b0) and housing
// re-balancer (PathGraph::ReassignWorkers 0x1400d62a0) order their scratch trees by
// raw pointer: the idle set and every need's (cost, citizen) tree by CitizenComponent*,
// the profession-count map by Profession*, the empty and occupied residence trees by
// ResidenceComponent*. Which citizen takes a job, which one Phase 5B strips first, and
// which empty house a family moves to among equals therefore follows heap addresses,
// and a save does not play out the same twice. With the row on, each key carries a
// stable id (tie_key.h), read here:
//
//   * a citizen or a residence: its entity's EntityList id (ComponentEntity::_entityId,
//     +0x18). EntityList::RegisterEntity (0x1400c48a0) stamps it with the master array's
//     count before the append, and RemoveEntity (0x1400c38e0) swaps the last entity into
//     the hole and re-stamps it, so `masterArray[e->_entityId] == e` always holds: the
//     ids are unique at any moment and set only by the order of registrations and
//     removals, which the sim decides. EntityList::Serialize (0x1400c44a0) writes every entity in
//     master-array order with its id and reads both back, so a load keeps them.
//   * a profession: its index in ProfessionList::professions (+0x20), built at Startup
//     (0x1400a90a0) from the factory's walk of the Profession externals (path order) and
//     sorted by +0x48, the same on every load of the same data. A profession outside
//     that list (a child's or student's title) ranks after it, by its place in the
//     same factory walk.
//
// Nothing here reads an address, an allocation or a clock. The seams read
// Ext::Fix().alloc_tiebreak once per call and pass 0 for every id when it is off.
// ============================================================================
#include <cstdint>
#include <vector>

class Component;
class Profession;
class ProfessionList;

namespace Ext {
namespace AllocTiebreak {

// The owning entity's EntityList id (+0x18) of a citizen or residence component.
int32_t EntityKey(const Component* component);

// Profession ranks for one allocator call.
class ProfessionOrder {
public:
    explicit ProfessionOrder(ProfessionList* list) : _list(list) {}

    // The index in the list's professions; past it, the place in the factory's
    // Profession walk; INT32_MAX for a null profession.
    int32_t Rank(const Profession* profession) const;

private:
    ProfessionList* _list;
    mutable std::vector<const Profession*> _all;   // the factory walk, built on the first miss
    mutable bool _allBuilt = false;
};

}  // namespace AllocTiebreak
}  // namespace Ext
