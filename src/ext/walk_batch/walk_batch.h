#pragma once
// ============================================================================
// src/ext/walk_batch/walk_batch.h -- NOT RETAIL. Two extension rows share this body:
//   ext.walk_batch       class A, a live toggle, no save impact (WALKBATCH.md)
//   ext.walk_batch_grid  class B, locked for the game, pseudo-mod ext:walk_batch_grid
//                        (WALKGRID.md); its own header, src/ext/walk_batch_grid/walk_batch_grid.h,
//                        is the seam's view of the TickGrid / TickCharacterGrid bodies below
// OFFSCREEN design D2, the walk batch at speed N.
// docs/extensions/handins/WALKBATCH.md is the design and the proof; WALKGRID.md the class B row.
//
// At speed N retail runs N sim iterations per Update and draws only the last one. On the
// N-1 intermediate iterations (MeshInstance::_rootOnlyUpdates set), a character whose move
// tick is exactly MoveTo::Execute's walking branch followed by the tail's step gets a short
// tick instead of CharacterComponent::Tick: the same Execute arithmetic (the speed of the
// cell, the waypoint advance with the real MoveTo::UpdateFacing), the same root-motion step
// and heading recurrence, written straight into the live fields. Skipped: the executor's
// loop and virtual dispatch, the trig, the rotation store and the Geometry push.
// ext.walk_batch: CharacterGrid::Place is still made, at the point the faithful tail makes it.
// ext.walk_batch_grid: Place is skipped too. The last iteration always runs the faithful walk,
// which writes the rotation of the caught-up yaw (and places the walker).
//
// Why ext.walk_batch is class A: position, yaw, speed, facing, path index and the grid are
// retail's every tick; only the walker's model rotation (and its render transform) lags inside
// the frame, and no sim read of either was found (WALKBATCH.md sections 11 and 13). Between
// frames the state is retail's, so the toggle is live.
// Why ext.walk_batch_grid is class B: the character grid holds a walker's cell from its last
// real tick, up to N-1 ticks late, and CharacterGrid::QueryNearestCharacter (MoveTo::Enter's
// follow flag, the hunts, FindBestTarget) reads it: the game diverges from retail
// (WALKBATCH.md section 3, WALKGRID.md).
//
// Selection (DESIGN.md hook rules 2, 3 and 4): GameUpdateLoop::Update picks the EntityList
// update once per iteration from Select() -- the rows read once per frame (the class B row's
// per-game locked value) and the iteration's root-only flag. The faithful walk is the existing
// body, with no test in its loop; each batched walk is its own instantiation.
// ============================================================================
#include <cstdint>

class CharacterComponent;                // Component/CharacterComponent.h
namespace System { class Time; }         // src/engine/system/Value.h

namespace Ext {
namespace WalkBatch {

// The walk an EntityList update runs (EntityList::UpdateImpl's template argument).
enum Walk : int {
    kWalkFaithful = 0,   // EntityList::Update: retail's walk
    kWalkPlace    = 1,   // EntityList::UpdateWalkBatch: ext.walk_batch (class A, Place every tick)
    kWalkNoPlace  = 2,   // EntityList::UpdateWalkBatchGrid: ext.walk_batch_grid (class B, no Place)
};

// Batched ticks taken (or, under BANISHED_STRESS_COMPPROF=4, the ticks the shadow checked).
// Main thread only; nothing reads it (the /char walkbatch row reads g_profCalls / g_profTicks).
extern long long g_batchedTicks;
// COMPPROF=2: the RDTSC ticks the batched ticks charged (TickCharacter's bracket, as
// entitylist.cpp's TickOneComponent) and their count; reset with the report window.
extern long long g_profTicks;
extern long long g_profCalls;

// COMPPROF=2: the decline census. One count per exit of the batched decision (Eligible in
// walk_batch.cpp; the last five are walk_batch_rules.h's Decline, in its order) for every
// intermediate character tick the batched walk saw, read before the tick. Printed and reset
// with the [COMPPROF] /char walkbatch row.
enum Census : int {
    kCensusBatch = 0,    // batched
    kCensusIdle,         // the stack is empty
    kCensusUnplaced,     // no grid index
    kCensusNullTop,      // a null top slot (the executor pops it)
    kCensusNotMoveTo,    // the top node is not a MoveTo
    kCensusNotEntered,   // a MoveTo not entered yet (its precheck runs)
    kCensusChained,      // a MoveTo with a next node
    kCensusOtherChar,    // a MoveTo that moves another character
    kCensusPathWait,     // Decline kPathWait
    kCensusInDestCell,   // Decline kInDestCell
    kCensusShortPath,    // Decline kShortPath
    kCensusPathDone,     // Decline kPathDone
    kCensusFollow,       // Decline kFollow
    kCensusCount
};
const char* CensusName(int why);
// The node type behind a kCensusNotMoveTo (the top), kCensusNotEntered (the MoveTo) or
// kCensusChained (the MoveTo's next node) decline; walkChain = of which the top chain holds a
// traversing MoveTo (char_prof.h's walk class).
struct CensusTypeRow {
    uint32_t typeId;
    int why;
    long long ticks;
    long long walkChain;
};
constexpr int kCensusTypeMax = 48;
extern long long g_census[kCensusCount];
extern CensusTypeRow g_censusTypes[kCensusTypeMax];
extern int g_censusTypeCount;
extern long long g_censusTypeOverflow;
void CensusReset();

// GameUpdateLoop::Update, once per frame beside Ext::RenderCulling::BeginFrame: g_frameWalk = the walk the
// frame's intermediate iterations take. kWalkNoPlace when ext.walk_batch_grid is on (its
// per-game value under the class B lock; it implies the batch whether or not ext.walk_batch is
// on), else kWalkPlace when ext.walk_batch is on (live, class A), else kWalkFaithful; always
// kWalkFaithful while the BANISHED_MOVETICK_TRACE diagnostic is on (its log counts every
// tail). The only reader of either row.
void BeginFrame();
extern int g_frameWalk;

// GameUpdateLoop::Update, once per sim iteration, before the EntityList update: the walk to
// run. Plain loads (no call), so the faithful EntityList::Update keeps its code: kWalkFaithful
// on the last iteration of a frame (and at speed 1, where every iteration is the last) and
// while both rows are off.
inline int Select(bool rootOnlyIteration) { return rootOnlyIteration ? g_frameWalk : kWalkFaithful; }

// The batched decision and step for one CharacterComponent tick. Returns true when the tick
// was taken here (the batched step, or under COMPPROF=4 the shadow, which runs the real
// tick itself); false = the caller runs CharacterComponent::Tick. Tick places a batched
// walker (ext.walk_batch); TickGrid does not (ext.walk_batch_grid).
bool Tick(CharacterComponent* self, const System::Time* time);
bool TickGrid(CharacterComponent* self, const System::Time* time);

// The batched walk's slot-8 tick of a CharacterComponent: Tick (EntityList::UpdateWalkBatch)
// or TickGrid (EntityList::UpdateWalkBatchGrid), else CharacterComponent::Tick. At
// BANISHED_STRESS_COMPPROF=2 it brackets the tick as the faithful walk's TickOneComponent
// does (the /tree and /char rows) and charges a batched tick to g_profTicks / g_profCalls.
// Defined here, out of line, so the walk's translation unit compiles its faithful
// instantiation as before.
void TickCharacter(CharacterComponent* self, const System::Time* time);
void TickCharacterGrid(CharacterComponent* self, const System::Time* time);

}  // namespace WalkBatch
}  // namespace Ext
