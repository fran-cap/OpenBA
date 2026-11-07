// src/game/Game/force_work.h
// Generic "force one citizen to run a Work" testing framework (standalone only).
//
// The town-manager building registry is empty / auto-assignment (FindWorkplace +
// AssignHousing) scales badly in standalone, so no citizen organically acquires a Work
// — and enabling the full auto-assignment combo freezes at the MoveTo/pathfinder
// frontier. To exercise a Work's real Execute chain deterministically we instead point
// ONE citizen at a work-area building and drive that Work's Execute directly, mirroring
// what the building's (inert) controller would do organically.
//
// This header factors the reusable mechanics out of the original forester harness
// (forester_force_harness.cpp) so any module test can reuse them: crash-safe memory
// probes, the entity component scan, the Work_Ctor25 work-area center-cell formula, and
// the "drive Work::Execute when the citizen's action stack is idle" step. Each concrete
// harness (forester, construction, ...) supplies its own work construction + phase logic
// and calls these helpers.
//
// A Work is driven with the WORK-DRIVER ABI — (self, citizenIBP) — NOT the action-stack
// executor ABI. The building's controller calls the Work's vtable slot directly with the
// worker's IBP; Execute then pushes an action sequence (MoveTo/PerformWork/...) onto the
// citizen's OWN CharacterComponent action stack, which MoveTick's executor (FUN_1400c4c50,
// env BANISHED_LIVE_CHARACTER_MOVETICK) runs. Driving only when the stack is idle lets the
// previously-pushed sequence finish before the next Execute.
#pragma once

#include <cstdint>
#include <cstddef>

namespace Object { class Base; }   // object/Base.h
class ComponentEntity;   // Entity/ComponentEntity.h
class Component;         // Component/Component.h
class CitizenComponent;  // Component/CitizenComponent.h
class Work;              // Work/Work.h

namespace ForceWork {

// Entity component-IBP array layout (matches starttown.cpp ENT_COMP_ARR/CNT).
constexpr int kEntCompArr = 0x30;   // ptr to component-IBP array
constexpr int kEntCompCnt = 0x38;   // int32 component count

// CharacterComponent (citizen+0x278) action stack: top-index at +0x60 (<0 == idle).
constexpr int kCharCompOff  = 0x278;
constexpr int kActionTopOff = 0x60;

// A harness's one-time dependency check (a latch: it never returns to Unchecked). Until it
// reads Usable the harness stays report-only and never drives its Work.
enum class DepsCheck : int {
    Unchecked = -1,   // the check has not run yet
    Unusable  = 0,    // a dependency (sub-fn / type id / component) is missing: report only
    Usable    = 1,    // every dependency resolved: the harness drives
};

// ---- crash-safe memory probes (VirtualQuery-backed) -------------------------
bool Readable(const void* p, size_t n);   // LEFT: void* is a raw address (diagnostic probe), not an object: VirtualQuery readable-range check
bool Executable(const void* p);   // LEFT: void* is a raw address (diagnostic probe), not an object: VirtualQuery executable check

// Drop Readable()'s cached readable regions. Call once at the start of a pass that
// probes many objects; entries stay valid only until the next reset, so the cache
// can never outlive a single pass (see the note in force_work.cpp).
void ResetReadableCache();

// GetTypeId via vtable[+0x10], fully probe-guarded (0 on any bad pointer).
uint32_t SafeGetTypeId(::Object::Base* obj);

// Find a component by exe type-id on an entity's IBP component list (crash-safe, null on miss).
Component* FindComp(ComponentEntity* entity, uint32_t tid);

// The work-area CENTER cell exactly as Work_Ctor25 computes it (+0x28 of the Work):
//   ((mc.fpH>>1)+mc.tileY)*mapDim + ((mc.fpW>>1)+mc.tileX)
// from the area's MapComponent (+0x28 tileX/+0x2a tileY/+0x2c fpW/+0x2e fpH). Returns -1
// if the area has no MapComponent or the world grid is unavailable.
int WorkAreaCenterCell(ComponentEntity* area);

// True when the citizen's CharacterComponent action stack is idle (ready for a new drive).
// Returns false (not idle / not ready) if charComp or its fields aren't live yet.
bool CitizenStackIdle(const CitizenComponent* citizen);

// Drive one Work's Execute on a citizen when its action stack is idle.
//   driveFn ABI: int (*)(Work* work, CitizenComponent** citizenIBP)  (the Work-driver ABI)
// Constructs the citizen IBP, calls driveFn, and traces the first `traceMax` drives under
// `tag`. Returns the Execute return code, or INT_MIN if the stack wasn't idle / not ready.
int DriveWhenIdle(CitizenComponent* citizen, Work* work, int (*driveFn)(Work* work, CitizenComponent** citizenIBP),
                  const char* tag, int* driveCount, int traceMax);

}  // namespace ForceWork

