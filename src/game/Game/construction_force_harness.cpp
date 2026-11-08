// src/game/Game/construction_force_harness.cpp
// Organic-construction test driver (env BANISHED_FORCE_CONSTRUCT, default OFF).
//
// Built on the generic ForceWork core (force_work.h), the same way forester_force_harness.cpp
// drives ForesterWork. Instead of the town's auto-assignment (empty registry / freezes at the
// pathfinder frontier), we point ONE citizen at the demo construction foundation and drive its
// construction Work directly, mirroring what the site's inert controller would do organically:
//
//   PHASE GATHER (BuildComponent state 1): construct a GatherRawMaterials bound to the site
//     (GatherRawMaterials_ConstructForSite → the faithful site ctor seeds per-material slots
//     from the site's BuildDescription + already-in-storage counts) and drive its Execute when
//     the citizen's action stack is idle. Execute finds a town StorageComponent with the needed
//     material, reserves it, pathfinds, and pushes a pickup/carry/drop action chain onto the
//     citizen's stack; MoveTick's executor runs it. As materials physically arrive in the site
//     storage the state-1 update (already ported) transitions the BuildComponent 1 → 8.
//
//   PHASE BUILD (state 8, all materials delivered): construct a BuildStructure bound to the
//     site (BuildStructure_ConstructForSite → the faithful full ctor sizes the build-position
//     byte array from the site footprint) and drive its Execute when idle. Execute assigns the
//     citizen a build position and enqueues the timed-labor action chain. A labor-progress
//     bridge (BANISHED_BUILD_SYNC_LABOR, default on) credits BuildComp+0x48 toward target each
//     tick — standing in for the PerformWork→component-message the SA executor never reaches —
//     so State8Update advances the mesh frames and requests 8 → 2. The phase switches on the
//     sub_8630 "all materials in site storage" predicate (HaveAllMaterials).
//
// DELIVERED-SYNC: GatherRawMaterials credits delivered (field_48) from its OnComplete, which
// fires from the citizen's DropOff→CompleteTask action — a node the standalone CitizenAI
// executor doesn't yet reach. So we force BANISHED_BUILD_SYNC_DELIVERED on (gatherrawmaterials.cpp
// re-syncs field_48 from the live site storage each Execute), which lets the gather advance
// through materials as they are physically delivered without the deep executor completion.
//
// CRASH SAFETY: single observable subject; refuse to drive until deps resolve; all foreign
// reads go through ForceWork's VirtualQuery-backed probes.


#include "options.h"
#include "clamped_atoi.h"     // review F-087: atoi on BANISHED_* text
#include <new>
#include "object/TypeInfo.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "system/MemoryAllocator.h"   // System::MemoryAllocator::MemAlloc / MemFree
#include "Component/BuildComponent.h"   // caller batch CB6: TypeId()
#include "Component/BuildDescription.h"   // the build cost +0x30 (B4 P2 GAME-1)
#include "Entity/ComponentEntity.h"
#include "force_work.h"
#include "Work/BuildStructure.h"   // BuildStructure::field_58 (the build-position count)
#include "Work/GatherRawMaterials.h"   // s_gather is driven as a Work
#include "Work/exe_bodies.h"       // BuildStructure_ConstructForSite
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B


// The demo foundation's OWNER entity (starttown.cpp, set alongside g_pConstructionDemoSite).
extern "C" ComponentEntity* g_pConstructionDemoEntity;

// PHASE GATHER — GatherRawMaterials Work wrappers (gatherrawmaterials.cpp).
extern "C" GatherRawMaterials* GatherRawMaterials_ConstructForSite(ComponentEntity* const* buildEntityIBP);
extern "C" int   GatherRawMaterials_ForceDrive(Work* self, CitizenComponent** citizenIBP);
extern "C" bool  GatherRawMaterials_DepsReady();

// PHASE BUILD — BuildStructure Work wrappers (buildstructure.cpp).
extern "C" int   BuildStructure_ForceDrive(Work* self, CitizenComponent** citizenIBP);
extern "C" bool  BuildStructure_DepsReady();

static bool ForceConstructEnvOn() {
    return Opt().forceConstruct;
}

// Build-labor progress bridge (runs whenever FORCE_CONSTRUCT drives the site).
// In retail, BuildComp+0x48 (labor accumulator) is raised by a component message posted when
// a citizen's timed PerformWork action fires — the same executor node the standalone CitizenAI
// executor never reaches (the gather half hit the identical gap at DropOff→CompleteTask). So
// once the site is in state 8 and we are force-driving the REAL BuildStructure::Execute (which
// assigns build positions + enqueues the labor action chain), we also credit BuildComp+0x48
// toward its target each tick, standing in for the labor-complete message. State8Update
// (buildcomponent_state8.cpp) reads +0x48 vs target = matComp[0x30]·bc[0x4c], advances the mesh
// frames, and requests 8 -> 2 once progress >= target. Ramp paced over BANISHED_BUILD_DEMO_FRAMES
// (default 600 ≈ 10s) so the rise is watchable.
static void CreditBuildProgress(BuildComponent* bc) {
    if (!bc || !ForceWork::Readable(bc, 0x50)) return;
    // target = base build effort (matComp = bc+0x18, +0x30) * build multiplier (bc+0x4c,
    // clamp to retail default 1). Same formula BC8_Target uses. Probe-guarded; fall back to a
    // nominal 60 if the description read looks bogus so a garbage target can't 1-shot complete.
    // matComp is the component's description (Component +0x18), a BuildDescription.
    const Description* matComp = bc->_description;
    int base = ForceWork::Readable(matComp, 0x34)
                   ? static_cast<const BuildDescription*>(matComp)->_workRequired : 0;   // +0x30
    int mult = bc->costMultiplier;   // +0x4c
    if (mult <= 0) mult = 1;
    int target = base * mult;
    if (target <= 0 || target > 1000000) target = 60;

    static int rampFrames = -1;
    if (rampFrames < 0) {
        const char* e = Opt().buildDemoFrames;
        rampFrames = (e && e[0]) ? ClampedAtoi(e) : 600;
        if (rampFrames < 1) rampFrames = 600;
    }
    int inc = target / rampFrames;
    if (inc < 1) inc = 1;
    int* prog = &bc->laborProgress;   // +0x48
    int before = *prog;
    int p = before + inc;
    if (p > target) p = target;
    *prog = p;
}

// Driver. Called from the CitizenAI UpdateAdapter for ONE force-worked citizen (same hook as
// StartTown_ForcePushForesterWork). Returns true if it "owns" the citizen this tick (caller
// skips the competing AI state machine so the construction work keeps the idle stack).
extern "C" bool StartTown_ForcePushConstructionWork(CitizenComponent* citizen) {
    if (!ForceConstructEnvOn()) return false;
    ComponentEntity* site = g_pConstructionDemoEntity;
    if (!site || !citizen) return false;
    if (GameUpdateLoop::_singleton == nullptr) return false;
    if (!System::MemoryAllocator::_singleton) return false;

    // Bind to the FIRST force-worked citizen only (single, observable subject).
    static void* s_owner = nullptr;
    if (!s_owner) s_owner = citizen;
    if (citizen != s_owner) return false;


    // ============================================================================
    // PHASE SWITCH — gather (state 1) until all raw materials are in the site storage,
    // then build (state 8). HaveAllMaterials is the same sub_8630 predicate the real state
    // machine transitions 1 -> 8 on. Latch the build phase once entered so State8Finalize's
    // storage reconfigure (which flips HaveAllMaterials back to false on completion) can't
    // bounce us back to gather on a finished building.
    // ============================================================================
    static BuildComponent* s_buildComp = nullptr;
    if (!s_buildComp) {
        const uint32_t tidBuild = BuildComponent::TypeId();   // caller batch CB6: was a lazy by-name cache
        Component* found = ForceWork::FindComp(site, tidBuild);
        s_buildComp = static_cast<BuildComponent*>(found);   // matched by type id

    }
    static int s_buildPhase = 0;
    if (!s_buildPhase && s_buildComp) {
        // Only trust HaveAllMaterials once State1Enter has initialized the build multiplier
        // (bc+0x4c > 0). Before State1Enter runs, bc+0x4c is 0, so sub_8630 computes
        // required = costQty·0 = 0 and returns true TRIVIALLY — which would switch to build
        // on the very first tick, before the gather ever hauls (and before the site storage/
        // workplace is even configured). Gating on mult>0 makes HaveAllMaterials reflect the
        // real per-material requirement, so the switch fires only after the gather delivers.
        int mult = ForceWork::Readable(s_buildComp, 0x50)
                       ? s_buildComp->costMultiplier : 0;   // +0x4c

        // BuildComponent::HaveAllMaterials, sub_8630 (was its BuildComponent_HaveAllMaterials
        // forwarder, whose null test this arm's s_buildComp test already makes)
        if (mult > 0 && s_buildComp->HaveAllMaterials()) {
            s_buildPhase = 1;
        }
    }

    if (!s_buildPhase) {
        // ---- PHASE GATHER (state 1, BuildingState::GatherMaterials) ----
        // One-time readiness gate: refuse to drive until every GatherRawMaterials::Execute
        // dependency resolves, else Execute would null-deref a sub-fn / type-id.
        static ForceWork::DepsCheck s_verified = ForceWork::DepsCheck::Unchecked;
        if (static_cast<int>(s_verified) < 0) {   // DepsCheck::Unchecked
            bool ok = GatherRawMaterials_DepsReady();
            s_verified = ok ? ForceWork::DepsCheck::Usable : ForceWork::DepsCheck::Unusable;
            fprintf(stderr, "[ForceConstruct] site=%p depsReady=%d -> %s\n", site, (int)ok,
                    s_verified == ForceWork::DepsCheck::Usable ? "will drive" : "report-only (deps missing)");
            fflush(stderr);
        }
        if (s_verified != ForceWork::DepsCheck::Usable) return true;

        // Construct the site GatherRawMaterials ONCE and keep it (its per-material slot arrays
        // @+0x48/+0x58 must survive across drives). Faithful site ctor seeds the slots.
        static GatherRawMaterials* s_gather = nullptr;
        if (!s_gather) {
            ComponentEntity* siteIBP = nullptr;
            new (&siteIBP) ::Object::InternalBasePtr(site);   // ComponentEntity is-an Object::Internal
            s_gather = GatherRawMaterials_ConstructForSite(&siteIBP);
            if (!s_gather) return true;   // own it, retry construction next tick
            fprintf(stderr, "[ForceConstruct] constructed GatherRawMaterials=%p for site=%p\n",
                    s_gather, site);
            fflush(stderr);
        }

        // Drive the gather Execute when the citizen's action stack is idle (mirrors the site
        // controller's Work-vtable call with the worker IBP). Own the citizen every tick.
        static int s_driveCount = 0;
        ForceWork::DriveWhenIdle(citizen, s_gather, &GatherRawMaterials_ForceDrive,
                                 "ForceConstruct", &s_driveCount, 12);
        return true;
    }

    // ---- PHASE BUILD (state 8, BuildingState::Construct) ----
    // One-time readiness gate for BuildStructure::Execute (shares the engine_funcs globals).
    static ForceWork::DepsCheck s_buildVerified = ForceWork::DepsCheck::Unchecked;
    if (static_cast<int>(s_buildVerified) < 0) {   // DepsCheck::Unchecked
        bool ok = BuildStructure_DepsReady();
        s_buildVerified = ok ? ForceWork::DepsCheck::Usable : ForceWork::DepsCheck::Unusable;
        fprintf(stderr, "[ForceBuild] site=%p depsReady=%d -> %s\n", site, (int)ok,
                s_buildVerified == ForceWork::DepsCheck::Usable ? "will drive" : "report-only (deps missing)");
        fflush(stderr);
    }
    if (s_buildVerified != ForceWork::DepsCheck::Usable) return true;

    // Construct the site BuildStructure ONCE (its build-position byte array @+0x50/+0x58 must
    // survive across drives). The faithful full ctor sizes it from the site footprint.
    static BuildStructure* s_build = nullptr;
    if (!s_build) {
        ComponentEntity* siteIBP = site;   // the InternalBasePtr ctor is one store
        s_build = BuildStructure_ConstructForSite(&siteIBP);
        if (!s_build) return true;   // own it, retry construction next tick
        fprintf(stderr, "[ForceBuild] constructed BuildStructure=%p for site=%p (buildPositions=%d)\n",
                s_build, site, s_build->_slots._size);
        fflush(stderr);
    }

    // Drive the build Execute when the citizen's action stack is idle (assigns a build
    // position + enqueues the timed-labor action chain).
    static int s_buildDriveCount = 0;
    ForceWork::DriveWhenIdle(citizen, s_build, &BuildStructure_ForceDrive,
                             "ForceBuild", &s_buildDriveCount, 12);

    // Labor-progress bridge: credit BuildComp+0x48 toward target each tick (executor gap —
    // the PerformWork->message that raises +0x48 organically never fires in SA). Run every
    // tick (not only when idle) so completion proceeds even while the citizen is mid-chain.
    CreditBuildProgress(s_buildComp);
    return true;
}

