// src/game/Component/tornadocomponent.cpp
// Clean reimplementation of the TornadoComponent lane from Application-x64.exe.
//
// TornadoComponent (0x68 bytes, vtable 0x14015aa80) carries ALL tornado gameplay:
// the path across the map, the movement integration, and the destruction. The
// `Tornado` type in src/game/Game/tornado.cpp is the *particle* (Graphics::Particle)
// and is purely cosmetic — it holds no gameplay code whatsoever.
//
// SLOT IDENTIFICATION — from the vtable BYTES at 0x14015aa80, not Ghidra's names.
// Ghidra labels these three "_Shutdown", "_Serialize" and "_OnRemove"; all three
// are off by one. Only these three are overridden, the other 13 are base-Component
// no-ops in 0x140118xxx (e.g. 0x140118a60 is a bare `ret`):
//   slot 3 (+0x18) 0x140058b80  Serialize  — save/load
//   slot 4 (+0x20) 0x1400585a0  Startup    — pick endpoints, build the path, post the event
//   slot 8 (+0x40) 0x140057cd0  Update     — per-frame movement + damage
//
// WHY SLOT 8 AND NOT SLOT 9. The house rule (charactercomponent.cpp) is that slot 8
// (+0x40) is the full-rate EntityList update-TREE walk (component flag bit 2) and
// slot 9 (+0x48) is the throttled 5-bucket round-robin (flag bit 3). The ctor ORs
// 0x84 = bit 2 | bit 7, so the tornado is a bit-2 component and ticks at full rate
// through slot 8. Wiring the movement at slot 9 would run it at the bucket rate.
//
// LIFETIME IS THE PARTICLE'S, NOT THE PATH'S. Update's first act is to fetch the
// entity's ParticleComponent (0x140005200) and test `count(+0x40) > 0 &&
// firstInstance.field8 >= 0`; when that fails the tornado queues ITSELF for
// destruction. Path exhaustion does not kill it directly — it calls 0x140053720,
// which flips the particle's playback rate NEGATIVE so the funnel winds down, and
// the gate trips a few frames later. (The type-id at 0x1401c4758 that 0x140053720
// checks is ParticleDescription, which is what identifies the component.)
//
// SPAWNING is debug-only in retail: "Template/Tornado.rsc" has exactly one xref,
// inside Game_SpawnTornado (0x14005fd20), whose only caller is the debug panel.
// That function is already ported at src/game/Dialog/debugui.cpp (command 329).
//
// GATE: BANISHED_LIVE_TORNADO — DEFAULT-ON since 2026-08-22 (the default-on table); the
// env var is now a kill-switch (=0). Trace tag [Tornado] on stderr.
//
// The particle half is done too: Tornado vtable slots 1/2/3/5/6/7/8/9 and
// TornadoInstance::Update (0x1400d2470) are all ported in src/game/Game/tornado.cpp,
// so the lifetime gate above is now driven by a real particle system finishing rather
// than by a dormant one.

// graphics/Mesh.h FIRST: it pulls src/engine/volume/Volume.h, whose
// global-scope `struct Rectangle` is hidden by <wingdi.h>'s `Rectangle()` once
// <windows.h> has been seen through resolve.h.  ENDGAME B.3.5 caller batch I2.
#include "graphics/Mesh.h"
#include "Component/tornadocomponent_funcs.h"
#include "Entity/EntityList.h"   // EntityList::removalList
#include "Game/Map.h"   // Map, MapCell
#include "options.h"
#include "object/TypeInfo.h"
#include "graphics/Camera.h"
#include "Game/exe_bodies.h"   // B3R STORAGE: g_gameSettings
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "system/MemoryAllocator.h"
#include "math/Math.h"
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch A residual
#include "io/Stream.h"      // ENDGAME B.3.4 callers batch H: IO::Stream::GetDirection
#include "Component/exe_bodies.h"      // the Component finders / leaves below
#include "Component/firecomponent_funcs.h"   // the fire lane's three bodies
#include "Action/exe_bodies.h"         // ActCNR_FindComponent_c460/_3f60, ActHunt_*
#include "Work/exe_bodies.h"           // WorkBase_FindComponent_52c0
#include "Player/exe_bodies.h"
#include "Dialog/dialog_exe_helpers.h" // Dialog_FindComponent_d6c0
#include <new>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "Object/factory_registration.h"
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "system/Value.h"   // ENDGAME B.3.4b System caller batch
#include "audio/Audio.h"     // ENDGAME B.3.7 callers (K4)
#include "Component/TornadoDescription.h"
#include "Component/ParticleDescription.h"
#include "Component/ParticleComponent.h"
#include "graphics/ParticleInstance.h"   // ParticleEmitterSlot::instance is-a Graphics::Geometry
#include "Component/AmbientEmitterComponent.h"
#include "Component/FireComponent.h"
#include "Component/BuildComponent.h"
#include "Component/GrowthComponent.h"
#include "Component/NaturalResourceComponent.h"
#include "Component/CitizenComponent.h"
#include "Component/AnimalComponent.h"
#include "Component/CharacterComponent.h"
#include "Action/PathSearch.h"   // RawArray<T> (the radius query's result)
#include "Component/ModelComponent.h"
#include "Component/DestroyComponent.h"
#include "Entity/ComponentEntity.h"
#include "Game/PlayerToolManager.h"
#include <atomic>
#include "Component/CharacterGrid.h"   // CharacterGrid::RadiusQuery
#include "Game/EventLog.h"   // EventLog::AddEvent (0x140095580)
#include "crt/msvc_libm.h"   // MsvcLibm::Sinf / Cosf (0x140131c90 / 0x140131090)

// "[Tornado] STRUCK a building" trace budget (was a function-local static; B6-5).
static std::atomic<int> s_tornadoStruckTraced{0};

// EventLog::AddEvent (0x140095580) — already ported in src/game/Game/eventlog.cpp
// (declared in Game/exe_bodies.h). Reused rather than re-inlined: retail's Startup
// builds the same 0x28-byte record by hand, and its literals (colour 0xffff6060,
// icon 4, popup -1) are exactly what that port's severity/icon/popup tables produce
// for type 0x1e / severity 3. Porting the same retail body a second time here is the
// trap that reference_one_retail_fn_ported_twice_diverges records.

// 0x140131c90 = sinf, 0x140131090 = cosf: the exe's MSVC CRT bodies, ported as
// MsvcLibm::Sinf / Cosf (crt/msvc_libm.h).

// ============================================================
// .rdata constants (read from the binary, values in the comments)
// ============================================================
namespace {

constexpr float kHalf            = 0.5f;          // DAT_140172114
constexpr float kPathJitter      = 0.25f;         // DAT_140172100
constexpr float kArriveRadius    = 3.0f;          // DAT_140172180
constexpr float kSkipRadius      = 10.0f;         // DAT_1401721b0
constexpr float kHalfCircleDeg   = 180.0f;        // DAT_140172220
constexpr float kDegToRad        = 0.017453292f;  // DAT_1401720cc

// The path is generated over indices [0 .. kPathLast]; Startup then pushes
// kPathLast + 1 waypoints. Both numbers are literals in 0x1400585a0.
constexpr int kPathLast  = 0x80;
constexpr int kPathNodes = kPathLast + 1;   // 129

// ============================================================
// Helpers, by retail address
// ============================================================
// Entity component finders. All take (entity, InternalBasePtr* out) and write the
// component pointer (or null) into *out.
//   0x140005200 NaturalResourceComponent_FindComponentByType (ParticleComponent)
//   0x140002280 FindComponent<AmbientEmitterComponent> (Tick; D.4 AUDIOCOMP)
//   0x1400052c0 WorkBase_FindComponent_52c0 (NaturalResourceComponent)
//   0x14000c460 ActCNR_FindComponent_c460  (GrowthComponent)
//   0x14000d6c0 Dialog_FindComponent_d6c0  (BuildComponent)
//   0x140005840 Entity_FindComponent_Fire_5840
//   0x140005780 PlayerPath_FindComponent_5780 (the fire owner)
//   0x140009790 Entity_FindComponent_Citizen_9790
//   0x140005500 ActHunt_FindComponent_5500 (AnimalComponent)
//   0x140003f60 ActCNR_FindComponent_3f60  (the mesh, "clear path queue")
// Leaves:
//   0x140053720 Tornado_ParticleReverse_53720   0x140042960 Tornado_AudioMoveEmitters_42960
//   0x14009f3e0 Reimpl_MapGetTerrainPos (terrain clamp)
//   0x1400c2ea0 ActHunt_MarkEntityRemoval_c2ea0 (0x1400c49d0 EntityList::QueueDestroy is inlined)
//   0x140126780 GrowthComponent_ResetGrowthState (knockdown)
//   0x14011e690 / 0x14011e7b0 / 0x14011ea10 the fire lane's IsBeingDemolished /
//               BeginBurning / StopBurning
//   0x140116dd0 CitizenComponent_HandleLifeEvent   0x140092990 Map_RadiusQuery_92990

// ---- tracing -------------------------------------------------------------
// The lane is opt-in and the volume is a handful of lines per tornado (spawn,
// one per damage tick, one per 16 waypoints, exhaustion, expiry), so tracing is on
// by default whenever the feature is. BANISHED_TORNADO_TRACE=0 silences it.
int g_trace = -1;
bool TraceOn() {
    if (g_trace < 0) {
        const char* v = Opt().tornadoTrace;
        g_trace = (v && (std::strcmp(v, "0") == 0 || std::strcmp(v, "false") == 0)) ? 0 : 1;
    }
    return g_trace != 0;
}
#define TORNADO_TRACE(...)                                       \
    do {                                                         \
        if (TraceOn()) {                                         \
            std::fprintf(stderr, "[Tornado] " __VA_ARGS__);      \
            std::fflush(stderr);                                 \
        }                                                        \
    } while (0)

GameUpdateLoop* GameWorld() {
    return GameUpdateLoop::_singleton;
}

// The TornadoDescription behind this component. Retail:
//     obj = *(void**)(self + 0x18);
//     desc = IsDerivedFrom(obj, TypeId_TornadoDescription) ? obj : nullptr;
// and then dereferences `desc` unguarded. The type-id global (0x1401c4aa0) has no
// storage in standalone, so the id comes from the runtime type registry instead —
// the standard substitution here (see tornadodescription.cpp).
TornadoDescription* TornadoDesc(TornadoComponent* self) {
    Description* obj = self->_description;
    // GetTypeIdByName returns 0xFFFFFFFF before Object::Factory::Startup has run, so
    // only latch a real id — otherwise the sentinel gets cached forever. (The
    // unchecked answer on an unregistered id is the tree's.)
    const uint32_t typeId = TornadoDescription::TypeId();
    if (typeId == 0 || typeId == 0xFFFFFFFFu) return static_cast<TornadoDescription*>(obj);
    return Object::IsDerivedFromNullSafe(obj, typeId) ? static_cast<TornadoDescription*>(obj) : nullptr;
}


}  // namespace

// ============================================================
// The arm-viability report
// ============================================================
// Each damage arm needs EVERY leaf on its own path; a missing one makes the arm
// silently unreachable and its counter reads 0 exactly like "the funnel never crossed
// one". Printed ONCE, unconditionally, on the first call into the class. Every leaf is
// a direct call now, so the verdicts are fixed; the audio arm is LIVE since
// Entity_FindAudioComponent (0x140002280) is FindComponent<AmbientEmitterComponent>.
static void Tornado_ReportArmsOnce() {
    static bool s_reported = false;
    if (s_reported) return;
    s_reported = true;
    {
        auto arm = [](const char* name, bool ok, const char* missing) {
            std::fprintf(stderr, "[Tornado] arm %-9s %s%s%s\n", name,
                         ok ? "LIVE" : "DEAD", ok ? "" : " — unresolved: ",
                         ok ? "" : missing);
        };
        arm("tree",     true, "findNaturalResource/findGrowth/growthKnockdown");
        arm("building", true,
            "findBuild(d6c0)/findFire(5840)/findFireOwner(5780)/ignite(e7b0)/finish(ea10)");
        arm("animal",   true, "findAnimal(5500)/markForRemoval");
        arm("citizen",  true, "findCitizen(9790)/citizenLifeEvent(116dd0)");
        arm("audio",    true,  "findAudio(2280)/audioMoveEmitters");
        arm("path",     true, "terrainClamp(9f3e0) - endpoints are NOT terrain-clamped");
        std::fflush(stderr);
    }

    TORNADO_TRACE("init: fire lane %s — the building arm is "
                  "%s until the fire lane lands\n",
                  "RESOLVED",
                  "live");
}


// ============================================================
// TornadoComponent::BuildPathFractal — 0x140058980
// ============================================================
// Recursive midpoint displacement between path[lo] and path[hi]:
//     mid       = (a + b) * 0.5
//     dir,len   = normalize(b - a)                 (Normalize with an out-length)
//     perp      = normalize(-dir.y, dir.x, 0)      (XY perpendicular)
//     path[m]   = mid + perp * len * 0.25 * RandFloat()
// then recurse on both halves while the span is wider than one index.
//
// THE `* 0.0` IN THE DECOMPILATION IS AN ARTIFACT — the path really is fractal.
// Ghidra renders the displacement terms as `local_6X * 0.0 * DAT_140172100`, which
// would collapse the whole thing into a straight line. The disassembly at
// 0x140058ad8..0x140058b00 is `MULSS XMM4,[0x140172100]` (0.25) then `MULSS XMM4,XMM0`
// (the RandFloat result) then `ADDSS XMM4,XMM6` (the midpoint) — no zero anywhere.
// The decompiler invented it by mis-modelling Normalize's out-parameters.
// The one genuine zero is the Z term: perp.z is 0 by construction, so mid.z is the
// plain average of the endpoints. That one is real and is kept.
void TornadoComponent::BuildPathFractal(float* points, int lo, int hi) {
    const float* a = points + static_cast<ptrdiff_t>(lo) * 3;
    const float* b = points + static_cast<ptrdiff_t>(hi) * 3;

    Math::Vector3 seg(b[0] - a[0], b[1] - a[1], b[2] - a[2]);
    Math::Scalar len(0.0f);
    Math::Vector3 dir = seg.Normalize(len);

    const float midX = (a[0] + b[0]) * kHalf;
    const float midY = (a[1] + b[1]) * kHalf;
    const float midZ = (a[2] + b[2]) * kHalf;

    Math::Vector3 perpIn(-dir.y, dir.x, 0.0f);
    Math::Vector3 perp = perpIn.Normalize();

    GameUpdateLoop* gw = GameWorld();
    System::Random* rng = gw ? gw->m_random : nullptr;
    const float r = rng ? rng->RandFloat() : 0.0f;

    const int mid = (lo + hi) >> 1;
    float* m = points + static_cast<ptrdiff_t>(mid) * 3;
    m[0] = perp.x * len.v * kPathJitter * r + midX;
    m[1] = perp.y * len.v * kPathJitter * r + midY;
    m[2] = perp.z * len.v * kPathJitter * r + midZ;

    if (mid - lo > 1) BuildPathFractal(points, lo, mid);
    if (hi - mid > 1) BuildPathFractal(points, mid, hi);
}

// ============================================================
// TornadoComponent::Path::FindNext — 0x1400a70c0
// ============================================================
// Scan forward from the current index for the first waypoint whose squared XY
// distance from `pos` is at least `radius * radius`. Returns -1 past the end.
// The Z term is `pos.z - 0.0` — retail ignores the waypoint's Z here.
int TornadoComponent::Path::FindNext(const float* pos, float radius) const {
    int i = index;
    for (;;) {
        ++i;
        if (i >= points._size) return -1;
        const Math::Vector3& wp = points[i];
        const float dx = pos[0] - wp.x;
        const float dy = pos[1] - wp.y;
        const float dz = pos[2] - 0.0f;
        // Math::Vector3::Dot, inlined: a pure leaf with no side effects.
        const float d2 = dx * dx + dy * dy + dz * dz;
        if (d2 >= radius * radius) return i;
    }
}

// ============================================================
// TornadoComponent::Path::Advance — 0x1400a71e0
// ============================================================
// If the current waypoint is closer than `arriveRadius`, pick the next waypoint
// more than `skipRadius` away and latch it into `target` (self+0x30..0x38, with
// Z forced to 0). Returns false only when the path is exhausted.
bool TornadoComponent::Path::Advance(const float* pos, float skipRadius, float arriveRadius) {
    Math::Vector3 d(target.x - pos[0],
                    target.y - pos[1],
                    0.0f);
    const float len = d.Length().v;

    if (len < arriveRadius) {
        const int next = FindNext(pos, skipRadius);
        if (next < 0) return false;
        index = next;
        const Math::Vector3& wp = points[next];
        target.x = wp.x;
        target.y = wp.y;
        target.z = 0.0f;
    }
    return true;
}

// ============================================================
// Tornado_StrikeEntity — 0x1400583c0
// ============================================================
// The per-entity effect. Finds six components off the entity and picks one arm.
//
// FAITHFUL ODDITY: `unusedFirstArg` is dead in retail — the prologue stores it to a
// local that the very next block overwrites, and one of the three call sites passes
// outright junk (`cellIndex * 6`). It is kept in the signature, and the junk is
// still passed, so the port matches the binary rather than tidying it. (The body
// reads nothing of its first argument: it stays free, B1.)
//
// The building arm ignites via the FIRE lane's 0x14011e7b0 / 0x14011ea10
// (DestroyComponent's members). The "NOKNOCK" / "NOFIRE" counters counted the
// unresolved-leaf arms, which cannot happen now that the leaves are direct calls;
// they stay in the trace line and read 0.
static void Tornado_StrikeEntityCounted(int64_t unusedFirstArg, ComponentEntity** entityIbp,
                                 int* hitTree, int* hitTreeNoKnock,
                                 int* hitBuilding, int* hitBuildingNoFire,
                                 int* hitAnimal, int* hitCitizen) {
    (void)unusedFirstArg;
    (void)hitTreeNoKnock;
    (void)hitBuildingNoFire;

    ComponentEntity* entity = *entityIbp;

    // The six finders (0x140005840 / 0x14000d6c0 / 0x14000c460 / 0x1400052c0 /
    // 0x140009790 / 0x140005500).
    FireComponent* fireComp = entity->FindComponent<FireComponent>();
    BuildComponent* buildComp = entity->FindComponent<BuildComponent>();
    GrowthComponent* growthComp = entity->FindComponent<GrowthComponent>();
    NaturalResourceComponent* natResComp = entity->FindComponent<NaturalResourceComponent>();
    CitizenComponent* citizenComp = entity->FindComponent<CitizenComponent>();
    AnimalComponent* animalComp = entity->FindComponent<AnimalComponent>();

    // The first few buildings the funnel touches, and which way the arm went.
    // Unconditional and one-shot: if this never appears, no tornado has swept a
    // building's cell and the arm is merely untested — a fact `building=0` cannot
    // convey on its own. See the arm-viability report, Tornado_ReportArmsOnce.
    if (buildComp) {
        if (s_tornadoStruckTraced.fetch_add(1, std::memory_order_relaxed) < 4) {
            std::fprintf(stderr, "[Tornado] STRUCK a building: entity=%p build=%p fire=%p"
                                 " -> arm %s\n",
                         static_cast<void*>(entity), static_cast<void*>(buildComp),
                         static_cast<void*>(fireComp),
                         fireComp ? "TAKEN (igniting)"
                                  : "SKIPPED - entity carries no FireComponent");
            std::fflush(stderr);
        }
    }

    if (natResComp != nullptr && growthComp != nullptr) {
        // --- tree / crop: stop it walking its growth queue and knock it down ---
        ModelComponent* model = entity->FindComponent<ModelComponent>();   // 0x140003f60
        const bool animating = model ? model->meshInstance.IsAnimating() : false;
        if (!animating) {
            natResComp->resourceScale = 0.0f;   // retail's dword 0 store
            // 0x140126780 is what actually FELLS the tree: it sets flag bit 0x200000,
            // notifies, and starts fall animation mode 2. Zeroing +0x28 alone only empties
            // the resource.
            natResComp->ResetGrowthState();
            if (hitTree) ++*hitTree;
        }
        return;
    }

    if (buildComp != nullptr && fireComp != nullptr) {
        // --- building: set it alight (the tornado does NOT demolish) ---
        DestroyComponent* fireOwner = entity->FindComponent<DestroyComponent>();   // 0x140005780
        const bool alreadyBurning = fireOwner ? fireOwner->IsBeingDemolished() : false;
        if (!alreadyBurning) {
            System::Time zero{0};
            RtSystem_Time_Ctor(&zero, 0.0f);
            // The null test is the tree's (the forwarder it called had it; retail has none).
            const bool lit = fireOwner ? fireOwner->BeginBurning(&zero) : false;
            if (lit) {
                fireOwner->StopBurning();
                if (hitBuilding) ++*hitBuilding;
            }
        }
        return;
    }

    if (animalComp != nullptr) {
        // --- ANIMAL (livestock or wild): remove it from the world ---
        // 0x140005500 matches type id 0x1401c6f98 = AnimalComponent (verified: the
        // finder compares against DAT_1401c6f98, and factory_registrations.csv maps
        // that global to AnimalComponent_RegisterType @ 0x14014e510). Its other
        // readers agree — HerdComponent_StartMigration, PopulationComponent_helper_0f50,
        // and 0x140047d70 (the crop lane's KillOneAnimal); reg_action_hunt.cpp:16
        // registers it as ActHunt_FindComponent_5500.
        // This arm was originally named "loose entity" here, which was wrong and would
        // have mis-read the soak: a tornado crossing a pasture reports its kills here,
        // and filing livestock losses under "debris" hides a herd wipe.
        GameUpdateLoop* gw = GameWorld();
        EntityList* entityMgr = gw ? gw->m_entityList : nullptr;
        ComponentEntity* ibp = entity;
        // Inlined 0x1400c49d0 (EntityList::QueueDestroy): skip entities already
        // flagged (bit 10 of entity+0x40), else MarkForRemoval + push onto the
        // pending-destroy array at entityList+0xd0 / count +0xd8.
        if (entityMgr && ibp != nullptr) {
            const uint32_t f = ibp->_entityFlags;
            if (((f >> 10) & 1) == 0) {
                ibp->MarkForRemoval();
                entityMgr->removalList
                    .SetSize(entityMgr->removalList._size + 1, ibp);
                if (hitAnimal) ++*hitAnimal;
            }
        }
        return;
    }

    if (citizenComp != nullptr) {
        // --- citizen: life event 5 (killed) ---
        citizenComp->HandleLifeEvent(5, nullptr);   // 0x140116dd0
        if (hitCitizen) ++*hitCitizen;
    }
}

// ============================================================
// TornadoComponent::DamageTick — 0x140058040
// ============================================================
// Runs down a countdown; on each expiry it strikes three sets of entities:
//   1. one random cell within desc->_scatterRadius of the funnel, but ONLY if that
//      cell's entity owns a NaturalResourceComponent (retail's own extra guard);
//   2. everything the quadtree radius query returns within desc->_damageRadius;
//   3. every occupied cell of the (2R+1)^2 square around the funnel.
// The cell entity raster is map->cells[idx].+8 (stride 0x30) — the cell+8 raster,
// not the cell+0 zone raster.
void TornadoComponent::DamageTick(const System::Time* dt) {
    TornadoDescription* desc = TornadoDesc(this);

    damageCountdown._ticks -= dt->_ticks;
    if (damageCountdown._ticks > 0) return;
    damageCountdown._ticks += desc->_damageInterval._ticks;

    GameUpdateLoop* gw = GameWorld();
    if (!gw) return;
    System::Random* rng = gw->m_random;
    Map* map = gw->m_game;

    // Random point within the scatter radius: angle in [-pi, pi], radius scaled by
    // RandFloat. Z is a genuine hard zero here (`XORPS XMM4,XMM4` at 0x140058110).
    const float angle = rng->RandFloatTol() * kHalfCircleDeg * kDegToRad;
    const float frac  = rng->RandFloat();
    const float scatter = static_cast<float>(desc->_scatterRadius);

    // The Vector3 ctor argument order (XMM1/XMM2/XMM3 at 0x140058121) puts sin on X
    // and cos on Y.
    const float ox = MsvcLibm::Sinf(angle) * scatter * frac;
    const float oy = MsvcLibm::Cosf(angle) * scatter * frac;

    const float px = ox + position.x;
    const float py = oy + position.y;
    const float pz = 0.0f + position.z;

    int hitTree = 0, hitTreeNoKnock = 0, hitBuilding = 0, hitBuildingNoFire = 0;
    int hitAnimal = 0, hitCitizen = 0;

    const int N = map->size;
    MapCell* cells = map->cells.GetData();

    auto clampCell = [N](float v) {
        int i = static_cast<int>(v + static_cast<float>(N >> 1));
        if (i < 0) i = 0;
        if (i >= N) i = N - 1;
        return i;
    };

    // --- 1. the scattered single strike (natural resources only) ---
    {
        const int ix = clampCell(px);
        const int iy = clampCell(py);
        ComponentEntity* cellEntity = cells[iy * N + ix].occupant.Get();
        if (cellEntity != nullptr) {
            NaturalResourceComponent* probe = cellEntity->FindComponent<NaturalResourceComponent>();   // 0x1400052c0
            if (probe != nullptr) {
                ComponentEntity* ibp = cellEntity;
                Tornado_StrikeEntityCounted(reinterpret_cast<int64_t>(cellEntity), &ibp, &hitTree, &hitTreeNoKnock,   // LEFT: B6 register retail leaves unset: none of retail's three calls of 0x1400583c0 loads RCX (0x140058220 / 0x140058291 / 0x14005834f set only RDX); the leftover is the entity then clobbered by 0x1400052c0 here, the slot pointer (RBX, 0x140058275, CheckPtr is a bare RET) at :536, the cell index * 6 (0x14005832f..0x140058333) at :559, so no one declared type carries all three without a cast  // CAST-OK: ptr_int (S4 ruling)
                                     &hitBuilding, &hitBuildingNoFire, &hitAnimal, &hitCitizen);
            }
        }
    }

    // --- 2. the quadtree radius query ---
    RawArray<CharacterComponent*> found = { nullptr, 0, 0 };
    const char* queryState = "ran";
    {
        float radius = static_cast<float>(desc->_damageRadius);
        float pos[3] = { px, py, pz };
        void* scratch = nullptr;
        // The quadtree this query walks is the CHARACTER SPATIAL-HASH MANAGER at
        // GameWorld+0x88, NOT the map grid. Retail 0x14005825c loads 0x88(g_GameWorld)
        // into rcx immediately before the call; gw+0xf8 (r14, loaded at 0x140058188) is
        // a separate live value in the same function, used for the cell raster above --
        // which is exactly why passing it here looked plausible. It is not a quadtree,
        // so 0x140092990 read its +0x50 node-count table as garbage and faulted on null.
        // citizen_updatehealth.cpp:687 was already calling this correctly.
        //
        // The null test is a DELIBERATE standalone-only deviation, not a papered-over
        // fault: retail always has this manager, but ours is built by CharManager_Seed
        // (charmanager_seed.cpp), which early-returns unless MoveTickEnabled(). With
        // that gate off gw+0x88 is legitimately null here, so skip the query rather
        // than fault. Every other deref in this function stays unguarded.
        CharacterGrid* quadtree = gw->m_characterGrid;
        if (quadtree)
            quadtree->RadiusQuery(&scratch, pos, &radius, 0, &found);
        else
            queryState = "SKIPPED(GameWorld+0x88 quadtree is null - "
                         "CharManager_Seed gated off by MoveTickEnabled)";

        for (int i = 0; i < found.size; i++) {
            CharacterComponent** slot = &found.data[i];
            ComponentEntity* ownerEntity = (*slot)->_entity.Get();
            Tornado_StrikeEntityCounted(reinterpret_cast<int64_t>(slot), &ownerEntity,   // LEFT: B6 register retail leaves unset (0x140058291 loads only RDX; RCX is the slot pointer left by 0x140058275): pointer here and at :500, integer (off * 6) at :559; no one declared type lets all three pass without a cast  // CAST-OK: ptr_int (S4 ruling)
                                 &hitTree, &hitTreeNoKnock, &hitBuilding, &hitBuildingNoFire,
                                 &hitAnimal, &hitCitizen);
        }
    }

    // --- 3. the (2R+1)^2 square sweep around the funnel itself ---
    {
        const int ix = clampCell(position.x);
        const int iy = clampCell(position.y);
        const int idx = N * iy + ix;
        const int row = idx / N;
        const int col = idx % N;
        const int R = desc->_damageRadius;
        for (int dy = -R; dy <= R; dy++) {
            for (int dx = -R; dx <= R; dx++) {
                const int cx = col + dx;
                const int cy = row + dy;
                if (cx < 0 || cy < 0 || cx >= N || cy >= N) continue;
                const int64_t off = static_cast<int64_t>(N) * cy + cx;
                ComponentEntity* cellEntity = cells[off].occupant.Get();
                if (cellEntity == nullptr) continue;
                // Retail passes `off * 6` here — junk into a dead parameter. Kept.
                Tornado_StrikeEntityCounted(off * 6, &cellEntity, &hitTree, &hitTreeNoKnock,
                                     &hitBuilding, &hitBuildingNoFire, &hitAnimal, &hitCitizen);
            }
        }
    }

    if (found.data) System::MemoryAllocator::MemFree(found.data);

    TORNADO_TRACE("damage at (%.1f,%.1f) radius=%d scatter=%d: tree=%d tree-NOKNOCK=%d "
                  "building=%d building-NOFIRE=%d animal=%d citizen=%d "
                  "(quadtree query %s, returned %d)\n",
                  position.x, position.y,
                  desc->_damageRadius, desc->_scatterRadius,
                  hitTree, hitTreeNoKnock, hitBuilding, hitBuildingNoFire, hitAnimal,
                  hitCitizen, queryState, found.size);
}


// ============================================================
// Ported leaves that belong to other classes
// ============================================================

// ParticleComponent::FadeOutEmitter (0x140053720): flip emitter `index`'s fade rate
// negative so the effect winds down. rate = -(1.0 / desc->emitters[index].fadeTime).
//
// CROSS-LANE: this body and 0x140042960 below are LOAD-BEARING FOR THE FIRE LANE TOO.
// Both lanes independently ported these two (my address grep was clean when I ran it —
// the fire lane's files did not exist yet), which is how
// reference_one_retail_fn_ported_twice_diverges lands. The fire lane deleted its copies
// and now resolves these addresses, so this is the single surviving implementation.
// The two copies differed: theirs guarded `duration == 0` before the divide. This one
// divides unguarded, matching retail (`g_fUVScale / AsSeconds(...)` at 0x14005375e with
// no test), per the project rule against papering over a faithful crash. If a
// divide-by-zero ever appears here, a zero-duration particle entry is the upstream bug
// to find — do not add the guard.
// Description array stride is 0x28 (fadeTime at +0x20); the component's emitter
// array stride is 0x18 (fadeRate at +0x10). The mismatch is retail's, not a typo.
void ParticleComponent::FadeOutEmitter(int index) {
    Description* obj = _description;
    const uint32_t typeId = ParticleDescription::TypeId();
    Description* descObj = obj;
    if (typeId != 0 && typeId != 0xFFFFFFFFu)
        descObj = Object::IsDerivedFromNullSafe(obj, typeId) ? obj : nullptr;
    ParticleDescription* desc = static_cast<ParticleDescription*>(descObj);

    const float duration = desc->_emitters[index].fadeTime.AsSeconds();
    const float rate = 1.0f / duration;
    emitters[index].fadeRate = -rate;   // DAT_1401722c0 sign flip (XORPS)
}

// B4 FORWARD: 1 outside caller (Component/firecomponent_ignite.cpp).

// AmbientEmitterComponent::MoveTo (0x140042960): move every live emitter to `newPos`,
// then latch it into the component's cached position at +0x28. No-op when the position
// is unchanged (`Vector3::operator==`, import 0x140152a70). The audio scene
// (GameUpdateLoop +0x78) is read per live slot, unguarded (0x1400429ab), and the
// count (+0x40) re-read each round.
void AmbientEmitterComponent::MoveTo(const Math::Vector3& newPos) {
    if (newPos == position)
        return;

    for (int i = 0; i < slots._size; i++) {
        const uint32_t id = slots._data[i].handle;
        if (id != 0)
            GameUpdateLoop::_singleton->m_audioScene->MoveEmitter(id, position, newPos);   // import 0x140152a00
    }
    position = newPos;
}

// ============================================================
// ENDGAME B.3.8 CIT — TornadoComponent as a REAL class (TornadoComponent.h)
// ============================================================
// Before: TWO tables — the 48-slot g_TornadoComponent_LiveVtable this file
// filled at every create, and a 16-slot copy in standalone/reg_tornado.cpp
// registered at the retail address 0x14015aa80 (the same three bodies at the
// same indices; only the Ghidra-derived NAMES differed). After: ONE compiler
// 16-slot vtable and a real ctor/dtor chain (B38_RULES D1, D2); the retail
// address had no by-address reader, so its registration went too (B38 D5).
// Slot 8 — NOT 9 — is the per-frame tick for a flag-0x84 (bit 2) component.
#include "Object/factory_registration.h"
#include "Component/TornadoComponent.h"

// 0x140058cf0. Component() is the CALL 0x1401188b0; the 0x14015aa80 stamp is
// the compiler's; +0x28 = 0, the path block {+0x30 = 0, +0x38 (dword) = 0,
// +0x40 = +0x48 = 0, +0x50 = -1}, +0x58 = 0, +0x60 (dword) = 0; flags |= 0x84.
TornadoComponent::TornadoComponent()
    : damageCountdown{0},
      path{Math::Vector3(0.0f, 0.0f, 0.0f), {}, {}, -1, {}},
      position(0.0f, 0.0f, 0.0f), _pad64{} {
    OrFlagsInCtor(0x84);
}

// ============================================================
// TornadoComponent::Persist — vtable slot 3 (+0x18), 0x140058b80
// ============================================================
// Stream order: Vector3 target(+0x30), int index(+0x50), int count(+0x48),
// [on load: resize the Vector3 array], each waypoint, Time(+0x28), Vector3 position(+0x58).
void TornadoComponent::Persist(IO::Stream& stream) {
    Tornado_ReportArmsOnce();

    Component::Persist(stream);   // retail: CALL 0x1401189b0 (qualified)

    IO::Stream* st = &stream;
    IO::Serializer ser(*st, 0);

    path.target.Serialize(ser);
    ser.Serialize(path.index, nullptr);

    int pathCount = path.points._size;
    ser.Serialize(pathCount, nullptr);

    // stream+8 == 0 means "loading": grow the array before reading the waypoints.
    // FUN_14000d030 is Collection::Array<Vector3>::SetSize(n)
    if (static_cast<int>(st->GetDirection()) == 0)   // IO::Stream::GetDirection, retail 0x180006200
        path.points.SetSize(pathCount);

    Math::Vector3* pVec = path.points._data;
    if (pathCount > 0 && pVec) {
        for (int i = 0; i < pathCount; i++) {
            pVec->Serialize(ser);
            ++pVec;   // sizeof(Math::Vector3) == 0xC
        }
    }

    damageCountdown.Serialize(ser);
    position.Serialize(ser);
}
// ============================================================
// TornadoComponent::Startup — vtable slot 4 (+0x20), 0x1400585a0
// ============================================================
void TornadoComponent::Startup() {
    Tornado_ReportArmsOnce();
    TornadoDescription* desc = TornadoDesc(this);

    GameUpdateLoop* gw = GameWorld();
    if (!gw) return;
    System::Random* rng = gw->m_random;
    Map* map = gw->m_game;

    // --- pick two cells at least desc->_minSeparation apart (at most 100 tries) ---
    const int minSep = desc->_minSeparation;
    uint32_t ax = 0, ay = 0, bx = 0, by = 0;
    int tries = 0;
    do {
        ax = rng->Rand(static_cast<uint32_t>(map->size));
        ay = rng->Rand(static_cast<uint32_t>(map->size));
        bx = rng->Rand(static_cast<uint32_t>(map->size));
        by = rng->Rand(static_cast<uint32_t>(map->size));
        if (tries > 99) break;
        ++tries;
    } while (static_cast<int>((bx - ax) * (bx - ax) + (by - ay) * (by - ay)) < minSep * minSep);

    // --- terrain-clamp both endpoints and fractal-fill between them ---
    float pathBuf[kPathNodes * 3];
    std::memset(pathBuf, 0, sizeof(pathBuf));

    const float half = static_cast<float>(map->size >> 1) - kHalf;

    auto seedEndpoint = [&](uint32_t cx, uint32_t cy, float* out) {
        const int n = map->size;
        const int lin = static_cast<int>(n * cy + cx);
        float in[3] = { static_cast<float>(lin % n) - half,
                        static_cast<float>(lin / n) - half,
                        0.0f };
        float clamped[3] = { in[0], in[1], 0.0f };
        {
            float* r = map->GetTerrainPos(clamped, in, 0, nullptr);   // 0x14009f3e0
            if (r) { clamped[0] = r[0]; clamped[1] = r[1]; clamped[2] = r[2]; }
        }
        out[0] = clamped[0]; out[1] = clamped[1]; out[2] = clamped[2];
    };

    seedEndpoint(ax, ay, &pathBuf[0]);

    float endB[3];
    seedEndpoint(bx, by, endB);
    // The tornado STARTS at the first endpoint: retail copies it into +0x58 before
    // overwriting the scratch with the second endpoint.
    position.x = pathBuf[0];
    position.y = pathBuf[1];
    position.z = pathBuf[2];
    pathBuf[2] = 0.0f;
    pathBuf[kPathLast * 3 + 0] = endB[0];
    pathBuf[kPathLast * 3 + 1] = endB[1];
    pathBuf[kPathLast * 3 + 2] = 0.0f;

    BuildPathFractal(pathBuf, 0, kPathLast);

    // --- push the waypoints; the first one seeds the current target ---
    for (int i = 0; i < kPathNodes; i++) {
        float* p = &pathBuf[i * 3];
        if (path.points._size == 0) {
            path.target.x = p[0];
            path.target.y = p[1];
            path.target.z = p[2];
            path.index = 0;
        }
        // FUN_14000cf40 is Collection::Array<Vector3>::SetSize(n, fill)
        path.points.SetSizeFill(path.points._size + 1, p);
    }

    // --- post the warning to the event log, and pause if the setting says so ---
    EventLog* eventLog = gw->m_eventNotifier;
    Object::InternalBasePtr nullIbp(nullptr);
    eventLog->AddEvent(0x1e, 3, &nullIbp);

    if (g_gameSettings != nullptr && g_gameSettings->autoSlow != 0) {
        PlayerToolManager* speedObj = gw->m_toolManager;
        if (speedObj) speedObj->field_0x34 = 1;
    }

    // Diagnostic only: whether the fractal actually bent the path. Straight would
    // mean the `* 0.0` reading had been right after all.
    float maxDev = 0.0f;
    {
        const float sx = pathBuf[0], sy = pathBuf[1];
        const float ex = pathBuf[kPathLast * 3], ey = pathBuf[kPathLast * 3 + 1];
        const float vx = ex - sx, vy = ey - sy;
        const float vlen = vx * vx + vy * vy;
        for (int i = 1; i < kPathLast; i++) {
            const float wx = pathBuf[i * 3] - sx, wy = pathBuf[i * 3 + 1] - sy;
            const float cross = (vlen > 0.0f) ? (wx * vy - wy * vx) : 0.0f;
            const float dev = (vlen > 0.0f) ? (cross < 0 ? -cross : cross) : 0.0f;
            if (dev > maxDev) maxDev = dev;
        }
        if (vlen > 0.0f) maxDev /= (vlen > 0.0f ? __builtin_sqrtf(vlen) : 1.0f);
    }

    ParticleComponent* particle = _entity->FindComponent<ParticleComponent>();   // 0x140005200

    TORNADO_TRACE("spawn: start=(%.1f,%.1f) end=(%.1f,%.1f) waypoints=%d "
                  "endpointTries=%d path=%s (maxLateralDeviation=%.2f) "
                  "speed=%.2f damageRadius=%d scatterRadius=%d particleComponent=%s\n",
                  pathBuf[0], pathBuf[1], pathBuf[kPathLast * 3], pathBuf[kPathLast * 3 + 1],
                  path.points._size, tries,
                  (maxDev > 0.01f) ? "FRACTAL" : "STRAIGHT", maxDev,
                  desc->_speed,
                  desc->_damageRadius,
                  desc->_scatterRadius,
                  particle ? "found" : "MISSING (Update will fault on it, as retail would)");
}
// ============================================================
// TornadoComponent::Tick — vtable slot 8 (+0x40), 0x140057cd0
// ============================================================
// Every frame: check the particle is still alive (else self-destruct), advance
// along the path, integrate the position, damage, then push the new transform and
// audio position out.
void TornadoComponent::Tick(const System::Time* time) {
    Tornado_ReportArmsOnce();
    const System::Time* dt = time;
    TornadoDescription* desc = TornadoDesc(this);

    GameUpdateLoop* gw = GameWorld();
    if (!gw) return;

    ParticleComponent* pc = _entity->FindComponent<ParticleComponent>();   // 0x140005200
    Map* map = gw->m_game;

    // FAITHFUL: retail dereferences the ParticleComponent unguarded — Tornado.rsc
    // always has one, so a null here is an upstream problem, not something to paper
    // over. Startup already logged whether the finder found it.
    const bool particleAlive =
        (pc->emitters._size > 0) &&
        (pc->emitters[0].sceneId >= 0);

    if (!particleAlive) {
        // --- the funnel is gone: queue the tornado entity for destruction ---
        ComponentEntity* entity = _entity.Get();
        EntityList* entityMgr = gw->m_entityList;
        if (entity != nullptr) {
            const uint32_t f = entity->_entityFlags;
            if (((f >> 10) & 1) == 0) {
                entity->MarkForRemoval();
                entityMgr->removalList
                    .SetSize(entityMgr->removalList._size + 1, entity);
                TORNADO_TRACE("expired: particle finished, entity queued for destruction "
                              "(pathIndex=%d of %d)\n",
                              path.index, path.points._size);
            }
        }
        return;
    }

    // --- advance along the path ---
    float pos[3] = { position.x, position.y, 0.0f };
    const int idxBefore = path.index;
    const bool stillOnPath = path.Advance(pos, kSkipRadius, kArriveRadius);
    const int idxAfter = path.index;

    Math::Vector3 dir(path.target.x - position.x,
                      path.target.y - position.y,
                      path.target.z - position.z);
    Math::Vector3 unit = dir.Normalize();

    if (!stillOnPath) {
        // Path exhausted: reverse the particle's playback so the funnel winds down.
        pc->FadeOutEmitter(0);   // 0x140053720
        TORNADO_TRACE("path exhausted at waypoint %d of %d — funnel winding down\n",
                      idxAfter, path.points._size);
    } else {
        DamageTick(dt);
        if (idxAfter != idxBefore && (idxAfter % 16) == 0)
            TORNADO_TRACE("waypoint %d of %d at (%.1f,%.1f)\n",
                          idxAfter, path.points._size, position.x, position.y);
    }

    // --- integrate the position ---
    const float secs = dt->AsSeconds();
    const float step = secs * desc->_speed;
    // Math::Vector3::operator+= (slot 0x1401514b8), inlined: a componentwise add
    // with no side effects.
    position.x += unit.x * step;
    position.y += unit.y * step;
    position.z += unit.z * step;

    {
        float out[3] = { 0.0f, 0.0f, 0.0f };
        float* r = map->GetTerrainPos(out, &position.x, 1, nullptr);   // 0x14009f3e0
        if (r) {
            position.x = r[0];
            position.y = r[1];
            position.z = r[2];
        }
    }

    // --- push the transform to the particle geometry ---
    {
        // Transform = { Quaternion(0,0,0,1), Vector3 position }.
        Math::Transform transform;
        transform.rotation = Math::Quaternion(0.0f, 0.0f, 0.0f, 1.0f);
        transform.translation.x = position.x;
        transform.translation.y = position.y;
        transform.translation.z = position.z;
        // Retail calls the geometry's SetTransform (the instance is-a Graphics::Geometry).
        pc->emitters[0].instance->SetTransform(transform);
    }

    // --- move the audio emitters and refresh the volume ---
    {
        // 0x140057f09 / 0x140057f52: FindComponent<AmbientEmitterComponent> (0x140002280),
        // twice, each result read UNGUARDED (Tornado.rsc carries an `ambientemitter`
        // with Audio/Effects/TornadoEffect.rsc, started by OnAdded at the origin): the
        // tornado's loop follows the funnel and takes the particle's opacity as volume.
        AmbientEmitterComponent* audio = _entity->FindComponent<AmbientEmitterComponent>();
        audio->MoveTo(position);   // 0x140042960

        // 0x140057f30-0x140057f3f: `MOV RCX,[RAX+0x38]; MOV RAX,[RCX]; MOVSS XMM6,[RAX+0x7c]` --
        // emitters[0].instance's Geometry opacity (row 430 fixed in B6: the tree read +0x7c of
        // the emitter array). It is the volume of the audio branch below.
        const float volume = pc->emitters._data[0].instance->_opacity;

        AmbientEmitterComponent* audio2 = _entity->FindComponent<AmbientEmitterComponent>();
        {
            // 0x140057f5d-0x140057f8c: slot [0] of the emitter array, read unguarded.
            AmbientEmitterSlot* emitter = audio2->slots._data;
            if (emitter->handle == 0) {
                emitter->volume = volume;
            } else if (emitter->volume != volume) {
                emitter->volume = volume;
                gw->m_audioScene->SetEmitterVolume(
                    emitter->handle, audio2->position, volume);
            }
        }
    }
}

// create 0x14000d500: MemAlloc(0x68, 8), then the ctor.
::Object::Base* TornadoComponent::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(TornadoComponent), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) TornadoComponent());
}
// Retail's createInPlace is `Internal::CreateInPlaceObject` (returns null); the
// tree's in-place entry built the object, and that is kept (B38 B8-13).
::Object::Base* TornadoComponent::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) TornadoComponent());
}

DEFINE_EXE_TYPE(TornadoComponent, "Component")
