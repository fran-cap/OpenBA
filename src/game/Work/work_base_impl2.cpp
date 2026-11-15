// src/game/Work/work_base_impl2.cpp
// Wave 2 (Work) — impl/geometry/math sub-batch.
#include "Work/exe_bodies.h"
//
// Contains the math helper:
//   0x140132010  FUN_140132010  — sqrtf (NaN/Inf-aware)
//
// The other helpers drafted for this sub-batch (the draft is
// docs/history/notes/wave2_drafts/work_base_impl2_full.cpp.draft) are ported elsewhere:
// 0x140087fb0 EntityAnchorCell and 0x140122e20 InteractComponent::NodePosition
// (Action/action_pf_road.cpp), 0x1400f6ef0 Work::StepProgress and the 0x140122b00 /
// 0x140122be0 walkable-position search (work_base_walkpos.cpp).
//

#include <cmath>

// ============================================================
// 0x140132010 — sqrtf with NaN/Inf handling
// ============================================================
// The original routes Inf/NaN/negative through the CRT sqrtf error path
// (CharacterComponent_helper_3e1c) and SQRT()s finite non-negative inputs. A faithful,
// clean reimpl is simply sqrtf, which already produces NaN for negative and +Inf for +Inf.
float WorkImpl_Sqrtf_132010(float param_1) {
    return sqrtf(param_1);
}
