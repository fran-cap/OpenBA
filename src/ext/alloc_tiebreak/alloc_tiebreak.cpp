// ============================================================================
// src/ext/alloc_tiebreak/alloc_tiebreak.cpp -- fix.alloc_tiebreak: the stable ids
// ============================================================================
// NOT RETAIL. alloc_tiebreak.h has the contract and the proof the ids are stable;
// docs/extensions/handins/ALLOCTIEBREAK.md the evidence.
// ============================================================================
#include "ext/alloc_tiebreak/alloc_tiebreak.h"
#include "Component/Component.h"
#include "Entity/ComponentEntity.h"
#include "Game/Profession.h"
#include "Game/ProfessionList.h"
#include "object/External.h"
#include "object/TypeInfo.h"   // Object::IsDerivedFromNullSafe

#include <climits>

namespace Ext {
namespace AllocTiebreak {

int32_t EntityKey(const Component* component) {
    return component->_entity.Get()->_entityId;   // ComponentEntity +0x18
}

int32_t ProfessionOrder::Rank(const Profession* profession) const {
    if (profession == nullptr) return INT32_MAX;
    Profession* const* listed = _list->professions.GetData();
    const int count = _list->professions.GetSize();
    for (int i = 0; i < count; ++i) {
        if (listed[i] == profession) return i;
    }
    // Outside the selectable list: the factory walks its resources in path order
    // (ProfessionList::Startup's own walk, before its filter and sort).
    if (!_allBuilt) {
        const uint32_t tid = Profession::TypeId();
        const ::Object::ExternalFactory* factory = ::Object::ExternalFactory::_singleton;
        const ::Object::External* found = factory->FindNext(tid, nullptr);
        for (; found; found = factory->FindNext(tid, found)) {
            if (::Object::IsDerivedFromNullSafe(found, tid))
                _all.push_back(static_cast<const Profession*>(found));
        }
        _allBuilt = true;
    }
    for (size_t i = 0; i < _all.size(); ++i) {
        if (_all[i] == profession) return count + static_cast<int32_t>(i);
    }
    return INT32_MAX;
}

}  // namespace AllocTiebreak
}  // namespace Ext
