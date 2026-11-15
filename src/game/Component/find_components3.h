// src/game/Component/find_components3.h -- three FindComponent<T> scans of one entity in one walk (perf,
// identical result)
//
// Retail runs ComponentEntity::FindComponent<T> (Entity/ComponentEntity.h) once per component class it needs:
// each walk stops at the first component whose Type() (slot 2) is T's id and narrows it with IsDerivedFrom.
// Type() and IsDerivedFrom read the type info only and the walk writes nothing, so one walk that keeps the
// FIRST match of each id returns the same three pointers. It reads the same components the three scans read
// together: up to the last of the three matches, or every component when one class is missing.
//
// Callers: CitizenBehavior::PickBestDropTarget (Action/action_pf_roots_route.cpp, per candidate storage) and
// ClearStorage::Execute (Work/clearstorage.cpp, the job's building). Each runs retail's three scans beside it
// under the COMPPROF=4 shadow ("FindComponents3").
#pragma once

#include <cstdint>
#include "Entity/ComponentEntity.h"
#include "stress_harness.h"   // StressHarness_Verify (the COMPPROF=4 shadow)

template <class A, class B, class C>
struct FoundComponents3 {
    A* a = nullptr;
    B* b = nullptr;
    C* c = nullptr;
};

template <class A, class B, class C>
FoundComponents3<A, B, C> FindComponents3(const ComponentEntity* entity) {
    const uint32_t idA = A::TypeId();
    const uint32_t idB = B::TypeId();
    const uint32_t idC = C::TypeId();
    FoundComponents3<A, B, C> out;
    bool haveA = false;
    bool haveB = false;
    bool haveC = false;
    for (int32_t i = 0; i < entity->_components._size; ++i) {
        Component* comp = entity->_components[i].Get();
        const uint32_t type = comp->Type();
        if (!haveA && type == idA) {
            haveA = true;
            out.a = comp->IsDerivedFrom(idA) ? static_cast<A*>(comp) : nullptr;
        }
        if (!haveB && type == idB) {
            haveB = true;
            out.b = comp->IsDerivedFrom(idB) ? static_cast<B*>(comp) : nullptr;
        }
        if (!haveC && type == idC) {
            haveC = true;
            out.c = comp->IsDerivedFrom(idC) ? static_cast<C*>(comp) : nullptr;
        }
        if (haveA && haveB && haveC) {
            break;
        }
    }
    return out;
}

// The shadow: retail's three scans against the one walk.
template <class A, class B, class C>
__attribute__((noinline)) void VerifyComponents3(const ComponentEntity* entity, const FoundComponents3<A, B, C>& found) {
    const bool ok = entity->FindComponent<A>() == found.a && entity->FindComponent<B>() == found.b &&
                    entity->FindComponent<C>() == found.c;
    StressHarness_Verify("FindComponents3", ok, entity->_components._size);
}
