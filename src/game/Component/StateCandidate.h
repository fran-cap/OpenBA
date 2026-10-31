#pragma once
// src/game/Component/StateCandidate.h -- the CitizenAI candidate record, on its own so the
// sort over it (Component/citizenai_sortbypriority.cpp) builds without the component.
#include <cstddef>
#include <cstdint>

// The candidate list CitizenAIComponent::BuildCandidateStateList (0x14010dca0) fills: a
// {data, count, capacity} array of 12-byte {state id, priority, unused} records. Layout
// from the tree's port of 0x14010dca0 (the append allocates capacity * 0xc with alignment
// 4 and copies three dwords per record; the shuffle swaps the three dwords;
// TransitionState 0x14010db90 reads id at +0 with stride 3 ints). (Moved from
// citizenai_candidatelist.cpp: TransitionState reads it too.)
struct StateCandidate {
    int32_t id;         // +0x00 CitizenAI state id
    int32_t priority;   // +0x04 sort key (descending)
    int32_t unused;     // +0x08 always 0
};
static_assert(sizeof(StateCandidate) == 0xc, "StateCandidate is 12 bytes");
static_assert(offsetof(StateCandidate, id) == 0x0, "");
static_assert(offsetof(StateCandidate, priority) == 0x4, "");
static_assert(offsetof(StateCandidate, unused) == 0x8, "");
