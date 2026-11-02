// ============================================================================
// src/ext/stranded_wade/stranded_wade.h -- fix.stranded_wade (fix tier, default ON)
// ============================================================================
// NOT RETAIL. docs/retail_bugs.md §19 (stranded citizens).
//
// A citizen who reaches land cut off from the town (over water or a mountain, a builder left on a
// finished dock or bridge piece, a zone split under him) asks for walks that Map::RequestPath
// (0x14009cfb0) refuses on the zone test, and stands there until he starves or freezes ("going
// somewhere to get warm": State_ElderTransition 0x14010d2c0 pushes the refused walk home at every
// pick).
//
// With the row on, every MoveTo reports its outcome (the three seams in Action/moveto.cpp) into a
// byte of its character's padding, and when the last two walks were refused on a land-to-land zone
// mismatch, the AI's next state pick (CitizenAIComponent::TransitionState 0x14010db90) pushes one
// walk with the pass flags 0xe1f | 0x1e0 instead: retail's own wade / climb bypass in RequestPath,
// to his home, or to the town hall when he has no home or the wading walk home failed. The rule is
// stranded_wade_rule.h. No rescue when the goal is land in his own zone (he is not the one cut off),
// and while the rescue walk is out no state pick replaces it. Nothing is saved.
// ============================================================================
#pragma once

class MoveTo;
class CitizenAIComponent;

namespace Ext {
namespace StrandedWade {

// MoveTo::AbstractImpl 0x1400c8ab0: RequestPath refused the walk (ticket {-1, 0}, answer 2).
void OnRefused(MoveTo* walk);
// MoveTo::Execute 0x1400c8570: the search came back with an empty path (answer 2).
void OnEmptyPath(MoveTo* walk);
// MoveTo::Exit 0x1400c8520: the executor popped the walk; an arrival when it stands on its
// destination cell.
void OnExit(MoveTo* walk);

// CitizenAIComponent::TransitionState 0x14010db90, before retail's pick: true when the citizen was
// stranded and a rescue walk was pushed (the pick is skipped).
bool Rescue(CitizenAIComponent* ai);

}  // namespace StrandedWade
}  // namespace Ext
