// src/game/Game/builder_pool_driver.cpp
//
// Tick-driven Builder pool — the headless stand-in for the player-set Builder count.
//
// In this game construction is gated on a Builder count that the player sets via the
// town-hall Professions spinner (UI msg 0x15 -> CitizenComponent_impl_63c0 / dd10 ->
// FindAvailableBuilding + the cbf0/ca20/c5f0/c4b0 laborer-conversion cascade). There is
// NO player-free path in the engine that seats the FIRST builder at a fresh construction
// site: the periodic employment allocator (0x1400d43b0) only REBALANCES workplaces that
// already have >=1 worker (its Phase-2 need-table skips any workplace whose live roster
// count WPC+0x60 == 0 — Ghidra-verified). See reference_builder_profession_bst_flow.
//
// RE-VERIFIED 2026-08-09 from the decompile of 0x1400d43b0: the skip is RETAIL's, not ours.
// Phase 1 (the citizen sweep) only *evicts* — for a citizen whose site profession
// (WorkPlaceComponent+0x48) no longer matches its own (+0x1f0) it calls AssignHousing(null)
// and demotes it to laborer; it never seats. Phase 2 then walks the per-profession site
// lists out of townMgr+0x100 and builds its need table under a literal
// `if (*(int *)(workPlaceComponent + 0x60) != 0)` guard, so a roster-0 site is invisible to
// it. Nothing else in retail seats a first worker: the WorkPlaceUI workerCountUp click
// (0x1450 -> FUN_14008d100 + CitizenComponent_impl_c3d0) and the town-hall Professions
// spinner are the only first-seat paths, and both are ported + live in SA. So this stand-in
// exists because retail's UI is the allocator's missing half, not because our allocator
// diverges — do NOT "fix" 0x1400d43b0 by removing the roster==0 skip.
//
// WHAT THIS FILE STILL DOES: nothing. The registration-side diagnostics it used to
// carry were env-gated probes and went with the flags; the tick entry point is kept
// because CitizenBehavior_Update calls it.
//
// ── RETIRED 2026-08-15: BANISHED_BUILDER_AUTOPOOL ────────────────────────────────────
// This file used to carry a tick-driven auto-seat pool: each town tick it ran the retail
// seat cascade (FindAvailableBuilding + cbf0/ca20/c5f0/c4b0) DIRECTLY against every
// registered construction site, reaching around the UI. It existed because the widget
// that normally drives that cascade — the workplace panel's workerCountUp click — was
// unproven, and it was the documented "set this or nothing gets built" flag.
//
// The click is now measured on a construction site specifically: with AUTOPOOL off and
// only src/diag/panel_staffing_driver.cpp running, a site placed by
// BANISHED_TOOL_HARNESS=placement went roster 0 -> 1 -> 3 (its full WorkComponent
// capacity) through WorkPlaceUI slot 9. So the bypass has no remaining job and is gone:
// builders are seated the same way every other profession is, by the panel driver
// standing in for the player on unattended runs.
//
// Site REGISTRATION into townMgr+0x100 (WorkPlaceComponent's state-0 enter,
// EnterBuilderProfession 0x14012f520) is what makes a site visible to the finders
// behind the click.

#include <cstdint>


// Called from CitizenBehavior_Update (the town tick, 0x14008c2b0). Seating itself is
// not done here — see the RETIRED note at the top of the file.
// src/diag/panel_staffing_driver.cpp clicks workerCountUp on Builder-keyed
// construction sites exactly as it does on every other workplace.
extern "C" void BuilderPool_Tick() {
}
