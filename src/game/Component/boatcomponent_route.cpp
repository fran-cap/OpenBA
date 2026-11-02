// src/game/Component/boatcomponent_route.cpp
// The merchant-boat ROUTE BUILDER — retail BoatComponent::SetDestination (0x1400456e0)
// and the leaves it needs. This is layer 2 of the TradeComponent controller wave: layer 1
// (tradecomponent_states.cpp) made a trading post arm its merchant timer; this is what
// the timer's expiry can finally call.
//
// ── What a "route" is ────────────────────────────────────────────────────────────
// The world holds a set of FEATURE POLYLINES at worldGeom+0x1e0 (worldGeom = gameWorld
// +0x110), filled by worldgen.cpp step 8. Retail's boat route uses POLYLINE 0 only — the
// record at the array base, with no index arithmetic (`**(longlong**)(wg+0x1e0)` for the
// points, `*(*(wg+0x1e0)+8)` for the count). Those points run from OUTSIDE the map, in
// across the world, and back out again.
//
// The finished route (BoatComponent +0x60 array / +0x68 count / +0x6c capacity, stride
// 0xc Vector3) is three pieces spliced together:
//
//   1. every polyline point BEFORE the polyline first enters the map — the approach lane
//      the boat sails in along, taken straight from the polyline with z = 0;
//   2. two A* legs through the water grid — map-entry -> dock, and dock -> map-exit —
//      each cell converted to a world position and height-sampled;
//   3. every polyline point from the first OUT-of-map point onward — the departure lane.
//
// If EITHER A* leg comes back empty, retail appends NEITHER (the `0 < a && 0 < b` gate at
// 140045e60) and the route degenerates to the polyline alone. That is retail behaviour,
// not an error path: do not "fix" it into appending the leg that did succeed.
//
// ── The decompile of 0x1400456e0 cannot be read literally ────────────────────────
// The function has two 4096-int stack maps and heavy register/stack-slot aliasing, so
// Ghidra folds several distinct variables onto one name — `local_res8` is by turns a
// point, a loop index and a Vector3-ctor argument slot — and it types the polyline's
// int32 point COUNT as a float. Everything below is read from the DISASSEMBLY:
//   140045840  MOV EBX,[RAX+0x8]        the count is an int32, not a float
//   1400458df  the in-bounds test that BREAKS loop 1 (first point inside the map)
//   140046268  a7310(&comp[0x50], &comp[0x40]) — nearest route point to the dock
//   14004636b  the unary Vector3 minus that flips the dock approach heading
//
// ── Deliberate, behaviour-preserving deviations ──────────────────────────────────
//  * Retail's forward scan (loop 2) is unrolled x4 by the compiler. It is written here as
//    one plain loop; the unrolled form's `local_80f8` shuffling exists only to keep "last
//    in-map point" correct across the 4-wide body, which the plain loop gets for free.
//    Checked branch-by-branch against 140045a18..140045cd5.
//  * Vector3 construction, subtraction, dot and unary minus are inlined as float
//    arithmetic rather than dispatched through the Math slots. A Vector3 is exactly three
//    floats (the route array's 0xc stride and the 3x MOV copy at 1400460b8 prove it) and
//    these are pure arithmetic, so the result is identical. Normalize / Heading /
//    FromYawPitch / the Transform and Quaternion ops are NOT inlined — those carry angle
//    and handedness conventions worth taking from the engine.
//
// GATED: reached only through BANISHED_LIVE_TRADE_CTRL (the state-2 merchant spawn) and
// the controller's shared teardown. Nothing else in the build calls it.

#include <new>
#include "object/TypeInfo.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "system/String.h"
#include "math/Math.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "Component/InteractDescription.h"   // ENDGAME B.3.9 C1: 0x1401c7938 cell class
#include "Component/ModelComponent.h"        // ENDGAME B.3.9 C1: 0x1401c7a50 cell class
#include "Component/BoatComponent.h"         // ENDGAME B.3.9 C1: 0x1401c4020 cell class
#include "Component/InteractComponent.h"
#include "Component/StateMachineComponent.h"
#include "Entity/ComponentEntity.h"
#include "Game/WorldGeometry.h"
#include "Game/Map.h"
#include "Game/PathfinderThread.h"
#include "pathfinding/PointList.h"
#include "Action/exe_bodies.h"
#include "Game/exe_bodies.h"
#include "Game/exe_constants.h"
#include "Player/exe_bodies.h"
#include "Work/exe_bodies.h"
#include "system/MemoryAllocator.h"
#include "ext/registry.h"                          // Ext::Fix().boat_reach_notice
#include "ext/boat_reach_notice/boat_reach_notice.h"

// ── Leaves, called directly ──────────────────────────────────────────────────────
//   0x140003ea0 PlayerPath_FindComponent_3ea0      0x140002ed0 WorkBase_FindComponent_2ed0
//   0x14009f3e0 Reimpl_MapGetTerrainPos            0x1400a6a30 Pf_EnqueueCmd1_a6a30 (set cost)
//   0x1400a68b0 Pf_EnqueueCmd4_a68b0 (flags)       0x1400a6e30 RoadTool_helper_6e30 (run)
//   0x140172114 kRdata_140172114 == 0.5f (the half-tile bias)

// ============================================================
// 0x140123880 — Entity::FindAttachPoint(name, outPos, outYaw)
// ============================================================
// Looks up a NAMED attach point ("boat" for a trading post's dock) on a component's
// description and resolves it to a WORLD position and heading through the owning entity's
// ModelComponent transform. Recurses into child components when the name is not on this
// one; returns false when no child has it either.
//
// Attach-point table: comp(+0x18) = description -> +0x20 -> { +0x30 array, +0x38 count },
// stride 0x18 per entry:
//     +0x00  System::String  name       (8 bytes)
//     +0x08  Vector3         localPos
//     +0x14  float           localYaw
// The stride and both field offsets come from 1401239d7 (`LEA RDI,[RAX+RAX*2]` = idx*3,
// then `[RBX+8+RDI*8]`) and 140123a3c (`[RCX+RDI*8+0x14]`) — not guessed.
extern "C" bool BoatRoute_FindAttachPoint(InteractComponent* comp, const System::String* nameStr,
                                          float* outPos, float* outYaw) {
    if (!comp) return false;

    // The description, IsDerivedFrom-gated exactly as retail. A failed cast yields NULL
    // and retail then derefs it — faithful: it relies on this component always carrying
    // an InteractDescription. The null-check below is on OUR side of that only to keep
    // the recursion honest, and it is the same branch retail takes when the table is
    // empty (count 0 -> no name matched -> recurse).
    Description* descRaw = comp->_description;
    InteractDescription* desc = nullptr;
    if (::Object::IsDerivedFromNullSafe(descRaw, InteractDescription::TypeId())) {
        desc = static_cast<InteractDescription*>(descRaw);
    }

    int hit = -1;
    const Pathfinding::PointList* table = nullptr;
    if (desc) {
        table = desc->_points;
        const int n = table ? table->_points._size : 0;
        for (int i = 0; i < n; ++i) {
            const Pathfinding::PointList::Point& entry = table->_points._data[i];
            if (entry.name ==
                *nameStr) {
                hit = i;
                break;
            }
        }
    }

    if (hit < 0) {
        // Not on this component — try each child (comp+0x28 array of IBPs, +0x30 count).
        const int childCount = comp->useLocations._size;
        for (int i = 0; i < childCount; ++i) {
            InteractComponent* child = comp->useLocations[i].Get();
            if (BoatRoute_FindAttachPoint(child, nameStr, outPos, outYaw)) return true;
        }
        return false;
    }

    // Found the name. Its position is relative to the owning entity's ModelComponent.
    ComponentEntity* const entity = comp->_entity.Get();
    const uint32_t wantedTid = ModelComponent::TypeId();
    ModelComponent* model = nullptr;
    const int compCount = entity ? entity->_components._size : 0;
    for (int i = 0; i < compCount; ++i) {
        Object::InternalPtr<Component>* const slot = &entity->_components[i];
        Component* const obj = slot->Get();
        if (!obj) continue;
        if (obj->Type() == wantedTid) {
            Component* cand = slot->Get();
            model = (::Object::IsDerivedFromNullSafe(cand, wantedTid)) ? static_cast<ModelComponent*>(cand) : nullptr;
            break;
        }
    }

    const Object::InternalPtr<ModelComponent> modelIBP(model);
    const ModelComponent* const m = modelIBP.Get();
    if (!m) return false;   // no ModelComponent: retail would fault on m+0x148

    const Pathfinding::PointList::Point& entry = table->_points._data[hit];

    // worldPos = modelTransform * entry.localPos   (the transform lives at model+0x148)
    // Transform copy-ctor (slot 0x140151418), Transform::operator*(Vector3) (slot 0x140151470).
    const Math::Transform xform = m->transform;
    const Math::Vector3 worldPos =
        xform * Math::Vector3(entry.pos.x, entry.pos.y, entry.pos.z);
    outPos[0] = worldPos.x;
    outPos[1] = worldPos.y;
    outPos[2] = worldPos.z;

    // worldYaw = Heading(modelRotation * FromYawPitch(entry.localYaw, 0)).
    // Retail copies model+0x148..+0x160 (7 floats: quaternion + translation) and hands
    // Quaternion::Rotate the head of that copy, so the rotate sees the quaternion only.
    // Vector3::FromYawPitch slot 0x1401512a0, Quaternion::Rotate slot 0x1401525f0,
    // Vector3::Heading slot 0x1401512d8.
    // The yaw is PointList::Point +0x14 (the runtime header names it `radius`).
    const float localYaw  = entry.radius;
    const float zeroPitch = 0.0f;
    const Math::Transform rot = m->transform;
    const Math::Vector3 yawVec = Math::Vector3::FromYawPitch(localYaw, zeroPitch);
    const Math::Vector3 rotated = rot.rotation.Rotate(yawVec);
    *outYaw = rotated.Heading();
    return true;
}

// ============================================================
// 0x1400a7310 — index of the route point nearest a position
// ============================================================
// Retail's receiver is BoatComponent+0x50, the head of the route sub-object it shares
// with TornadoComponent: +0x10 from there is the element pointer (comp+0x60) and +0x18
// the count (comp+0x68). Here it takes the boat and reads the same two fields by name.
// Retail seeds the running minimum from element 0 and then re-tests element 0 in the
// loop; kept, because it makes the empty-array case return 0 rather than -1 and the
// caller indexes with the result unguarded.
extern "C" int BoatRoute_NearestPointIndex(const BoatComponent* boat, const float* pt) {
    const Math::Vector3* base = boat->route._data;
    if (!base) return 0;

    float dx = pt[0] - base[0].x, dy = pt[1] - base[0].y, dz = pt[2] - base[0].z;
    float best = dx * dx + dy * dy + dz * dz;
    int bestIdx = 0;

    const int n = boat->route._size;
    for (int i = 0; i < n; ++i) {
        const Math::Vector3* e = boat->route._data + i;
        dx = pt[0] - e->x; dy = pt[1] - e->y; dz = pt[2] - e->z;
        const float d = dx * dx + dy * dy + dz * dz;
        if (d < best) { best = d; bestIdx = i; }
    }
    return bestIdx;
}

// ============================================================
// 0x14009ffc0 — run one WATER A* leg
// ============================================================
// Loads the boat movement-cost profile into the shared pathfinder worker, then runs it.
// The eleven costs and the 0x380 mode word are retail's, verified against
// 1400a0000..1400a00a8. Returns the number of cells written to `outBuf` (capacity 0x1000
// ints, which is why 56e0's two stack maps are int[4096]).
//
// ⚠ RETURN VALUE: Ghidra types both this and 0x1400a6e30 `void`, but 6e30 TAIL-CALLS
// 0x1400a6d50 (at 1400a6edf) whose EAX is the copied path length, and 56e0 uses that
// length to decide whether either leg found anything. keystone_a_zone.cpp's port of 6e30
// was declared void for the same reason and is widened to int alongside this file — its
// own caller discards the value, so nothing else changes.
// See reference_discarded_return_reenables_gated_branch.
extern "C" int BoatRoute_FindWaterPath(Map* map, void* /*unused2*/, uint32_t fromCell,
                                       uint64_t toCell, void* /*unused5*/, int* outBuf) {
    PathfinderThread* pf = map->pathfinder;
    if (!pf) {
        // A missing worker and "no water route" both end as 0 cells at the caller, and
        // they are completely different bugs. Say which one this was.
        std::fprintf(stderr, "[BoatRoute] A* SKIPPED: pf=%p\n", pf);
        std::fflush(stderr);
        return 0;
    }

    static const int kCost[11] = {10, 10, 10, 10, 10, 10, 10, 5, 1, 5, 10};
    for (int i = 0; i < 11; ++i)   // 0x1400a6a30: the cost rides in R8 (read as an int), the ctx in R9
        pf->SetStepCost(i, kCost[i]);
    pf->SetSearchFlags(1, 0, 0);   // 0x1400a68b0 (1, 0, 0)
    const int n = pf->QueryPath(static_cast<int>(fromCell), static_cast<int>(toCell & 0xffffffffu),
                                       0x380, outBuf, 0x1000);   // 0x1400a6e30
    return n;
}

// ============================================================
// 0x1400456e0 — BoatComponent::SetDestination(entity)
// ============================================================
namespace {

// The route array's append, inlined by retail at four sites (140045909, 140045fa9,
// 140046184, and the tail of the second A* loop). All four are the same body:
//   if (count == 0) { comp+0x50 = pt; comp+0x70 = 0; }        // first point + cursor
//   grow to count+1 (capacity = count+1, or (count+1)*3 when growing from non-zero)
//   copy the surviving elements, write pt into the new tail
// The `if (dst)` inside retail's copy loop is a NULL-BASE test (JZ on base+offset), kept.
void AppendRoutePoint(BoatComponent* comp, const float pt[3]) {
    if (comp->route._size == 0) {
        comp->waypoint.x = pt[0];
        comp->waypoint.y = pt[1];
        comp->waypoint.z = pt[2];
        comp->routeIndex = 0;
    }

    const int oldCount = comp->route._size;
    const int newCount = oldCount + 1;
    Math::Vector3* oldBase = comp->route._data;
    comp->route._size = newCount;

    if (newCount > comp->route._capacity) {
        comp->route._capacity = newCount;
        if (oldCount != 0) comp->route._capacity = newCount * 3;
        comp->route._data = static_cast<Math::Vector3*>(::System::MemoryAllocator::MemAlloc(
            static_cast<uint64_t>(comp->route._capacity) * 0xc, 4));
    }

    Math::Vector3* newBase = comp->route._data;
    int i = (oldBase == newBase) ? oldCount : 0;
    for (; i < newCount; ++i) {
        if (!newBase) continue;          // retail's JZ on base+offset: skip, do not break
        Math::Vector3* dst = &newBase[i];
        const float* src = (i < oldCount) ? &oldBase[i].x : pt;
        dst->x = src[0];
        dst->y = src[1];
        dst->z = src[2];
    }
    if (oldBase != newBase) ::System::MemoryAllocator::MemFree(oldBase);   // no null test (retail)
}

// (int)(coord + mapSize/2) clamped into [0, mapSize-1] — retail's tile conversion,
// repeated verbatim at every cell computation in 56e0.
inline int ToTileClamped(float coord, int mapSize) {
    int t = static_cast<int>(coord + static_cast<float>(mapSize >> 1));
    if (t < 0) t = 0;
    if (t >= mapSize) t = mapSize - 1;
    return t;
}

inline bool TileInMap(float x, float y, int mapSize) {
    const float half = static_cast<float>(mapSize >> 1);
    const int ix = static_cast<int>(x + half);
    const int iy = static_cast<int>(y + half);
    return ix >= 0 && ix < mapSize && iy >= 0 && iy < mapSize;
}

}  // namespace

// Row 350 (B7 RES): 0x1400456e0 tests only the destination (0x1400456ff `lVar20 == 0`);
// the argument, the release path's StateMachineComponent and owner node, the world, the
// terrain, the WorldGeometry, its contour record and the route array are read straight.
void BoatComponent::SetDestination(const Object::InternalPtr<ComponentEntity>* destEntityIBP) {
    ComponentEntity* dest = destEntityIBP->Get();
    destination = dest;

    // ── Null destination: RELEASE the boat ───────────────────────────────────────
    // The controller's teardown (0x14005b160) calls in with a null entity to hand the
    // merchant back. Retail finds a component on the boat's own entity and dispatches
    // its +0x48 owner through vtable slot 2 (Controller::PostEvent), passing the byte at
    // +0x51 (StateMachine::activeState). The tree's old null test on the slot read from
    // the table is gone with the table read: a compiler vtable slot is never null.
    if (dest == nullptr) {
        StateMachineComponent* state = _entity->FindComponent<StateMachineComponent>();   // 0x140003ea0
        StateMachine* ownerSlot = &state->stateMachine;
        ControllerComponent::Controller* owner = ownerSlot->current;
        owner->PostEvent(ownerSlot, static_cast<int>(state->stateMachine.activeState), 1);
        return;
    }

    // ── Where the boat is going: the destination's "boat" attach point ───────────
    InteractComponent* interact = destination.Get()->FindComponent<InteractComponent>();   // 0x140002ed0

    System::String boatName("boat");
    float* const destPos = &dockPosition.x;   // Vector3
    float* const destYaw = &dockHeading;
    BoatRoute_FindAttachPoint(interact, &boatName, destPos, destYaw);

    // The dock's facing, as a direction vector. Flipped at the end if the route
    // approaches from the opposite side. (Vector3::FromYawPitch, slot 0x1401512a0.)
    const float zeroPitch = 0.0f;
    Math::Vector3 dockForward = Math::Vector3::FromYawPitch(*destYaw, zeroPitch);

    GameUpdateLoop* gw = GameUpdateLoop::_singleton;
    // WorldGeometry::_contourOutlines (+0x1E0): its record 0 is the polyline.
    Map* terrain   = gw->m_game;
    WorldGeometry* wgeo = gw->m_worldGeometry;

    const RawArray<Math::Vector2>* poly = wgeo->_contourOutlines.data;   // record 0 — retail
    const int   polyCount = poly->size;   // int32 (140045840), [[wgeo+0x1e0]+8]
    const Math::Vector2* polyPts = poly->data;

    // ── 1. the approach lane: every polyline point before the map ────────────────
    int consumed = 0;      // points examined, including the first in-map one
    int resumeIdx = 0;     // where piece 3 restarts
    float entryX = 0.0f, entryY = 0.0f;
    for (int i = 0; i < polyCount; ++i) {
        const float px = polyPts[i].x;
        const float py = polyPts[i].y;
        ++consumed;
        entryX = px;
        entryY = py;
        if (TileInMap(px, py, terrain->size)) { resumeIdx = i; break; }
        const float pt[3] = {px, py, 0.0f};
        AppendRoutePoint(this, pt);
        entryX = 0.0f;
        entryY = 0.0f;
        resumeIdx = 0;
    }

    // ── 2a. the exit: scan forward for the first point back OUTSIDE the map ──────
    // Retail resumes piece 3 at (index of that point) + 1 — the transition point itself
    // is skipped. That is retail's arithmetic (uVar26 is pre-incremented at 140045c9e
    // before the point is read), not an off-by-one here.
    float exitX = 0.0f, exitY = 0.0f;
    for (int i = consumed; i < polyCount; ++i) {
        const float px = polyPts[i].x;
        const float py = polyPts[i].y;
        resumeIdx = i + 1;
        if (!TileInMap(px, py, terrain->size)) break;
        exitX = px;
        exitY = py;
    }

    // ── 2b. the two A* legs ─────────────────────────────────────────────────────
    static int s_cellsIn[4096];
    static int s_cellsOut[4096];

    int mapSize = terrain->size;
    const int destTx  = ToTileClamped(destPos[0], mapSize);
    const int destTy  = ToTileClamped(destPos[1], mapSize);
    const int entryTx = ToTileClamped(entryX, mapSize);
    const int entryTy = ToTileClamped(entryY, mapSize);

    const int nIn = BoatRoute_FindWaterPath(terrain, nullptr,
                                            static_cast<uint32_t>(mapSize * entryTy + entryTx),
                                            static_cast<uint64_t>(static_cast<uint32_t>(mapSize * destTy + destTx)),
                                            nullptr, s_cellsIn);

    mapSize = terrain->size;                                   // retail re-reads it
    const int exitTx  = ToTileClamped(exitX, mapSize);
    const int exitTy  = ToTileClamped(exitY, mapSize);
    const int destTx2 = ToTileClamped(destPos[0], mapSize);
    const int destTy2 = ToTileClamped(destPos[1], mapSize);
    const int nOut = BoatRoute_FindWaterPath(terrain, nullptr,
                                             static_cast<uint32_t>(mapSize * destTy2 + destTx2),
                                             static_cast<uint64_t>(static_cast<uint32_t>(mapSize * exitTy + exitTx)),
                                             nullptr, s_cellsOut);

    // CONTROL PROBE (BANISHED_BOAT_TRACE only, no behaviour): both real legs end at the
    // dock, which sits on LAND by construction — so "0 cells" there is ambiguous between
    // "this dock is unreachable" and "water pathing does not work in standalone at all".
    // Query between two points that are unambiguously ON the polyline centreline instead.
    // A non-zero result here means the A* is fine and the dock is the problem; a zero
    // means the water profile or the nav grid is, and the route builder is being blamed
    // for someone else's gap.

    // BOTH legs or NEITHER — retail's gate at 140045e60.
    if (nIn > 0 && nOut > 0) {
        const float bias = static_cast<float>(terrain->size >> 1) - kRdata_140172114;
        for (int pass = 0; pass < 2; ++pass) {
            const int* cells = pass == 0 ? s_cellsIn : s_cellsOut;
            const int  n     = pass == 0 ? nIn : nOut;
            for (int i = 0; i < n; ++i) {
                float cellPos[3] = {
                    static_cast<float>(cells[i] % terrain->size) - bias,
                    static_cast<float>(cells[i] / terrain->size) - bias,
                    0.0f
                };
                float snapped[3] = {0.0f, 0.0f, 0.0f};
                terrain->GetTerrainPos(snapped, cellPos, '\0', nullptr);   // 0x14009f3e0
                AppendRoutePoint(this, snapped);
            }
        }
    } else if (Ext::Fix().boat_reach_notice) {
        // NOT RETAIL (fix.boat_reach_notice, docs/retail_bugs.md §30): retail drops the failed
        // legs without a word. The event log says so; the route below is retail's.
        Ext::BoatReachNotice::OnLegsFailed(dest);
    }

    // ── 3. the departure lane: the rest of the polyline ─────────────────────────
    for (int i = resumeIdx; i < polyCount; ++i) {
        const float pt[3] = {polyPts[i].x, polyPts[i].y, 0.0f};
        AppendRoutePoint(this, pt);
    }

    // ── the dock approach heading ───────────────────────────────────────────────
    // Take the route direction near the dock (nearest point minus the one two back) and
    // flip the dock's own facing if the boat arrives against it.
    // ⚠ `nearest - 2` is retail's index arithmetic (LEA EAX,[RBX-0x2] at 1400462bc) and it
    // is NOT bounds-checked there either: retail relies on the dock never being the first
    // two points of a route. Left faithful — a negative index here means the route is
    // degenerate (an empty polyline, or both A* legs failed), and the fix belongs upstream
    // at whichever of those produced it, not in a guard that silently yields a wrong yaw.
    const int nearest = BoatRoute_NearestPointIndex(this, destPos);
    const Math::Vector3* pts = route._data;   // [this+0x60], no test
    {
        const Math::Vector3& a = pts[nearest];
        const Math::Vector3& b = pts[nearest - 2];
        const Math::Vector3 dir(a.x - b.x, a.y - b.y, 0.0f);
        const Math::Vector3 normalized = dir.Normalize();   // slot 0x1401512c0
        const float dot = normalized.x * dockForward.x +
                          normalized.y * dockForward.y +
                          normalized.z * dockForward.z;
        if (dot < 0.0f) {
            dockForward.x = -dockForward.x;
            dockForward.y = -dockForward.y;
            dockForward.z = -dockForward.z;
        }
        *destYaw = dockForward.Heading();   // slot 0x1401512d8
    }

}
