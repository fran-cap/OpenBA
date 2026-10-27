// src/game/Component/boatcomponent_states.cpp
// BoatComponent's OWN state machine — layer 4 of the trade wave, and the thing that
// makes the merchant boat actually sail.
//
// ── WHY THIS EXISTS ──────────────────────────────────────────────────────────────
// After layer 2/3 the trading post spawned a merchant, built it a 993-point route and
// parked in state 0x12 forever. The note left behind said "BoatComponent's own Update is
// the base no-op, so some other system drives it". It does not: BoatComponent drives
// ITSELF, through the same embedded-state-node mechanism TradeComponent uses. It was
// invisible because BoatComponent's per-frame handlers all live in the trans30 column,
// and nothing in our build had ever dispatched that column.
//
// The geometry is read off the vtable, not guessed. BoatComponent vtable 0x140158f40:
//   slot 16 = 0x140003730  (`XOR EAX,EAX; RET`)   => default state 0
//   slot 17 = 0x140003720  (`LEA RAX,[RCX+0x78]`) => node at +0x78, back-pointer +0x90
// and the ctor 0x140046800 writes node vtable 0x140158ef0 at +0x78 with +0x90 = this,
// exactly TradeComponent's shape one offset family over.
//
// ── THE TABLE (BoatDescription_Setup -> 0x1400468a0) ─────────────────────────────
// Built with BoatComponent's own three primitives — 0x1400039d0 register-state,
// 0x140003ae0 add-transition, 0x140003b90 add-event — into the table at 0x1401c9150
// (count 0x1401ca0e0). The argument mapping is read off 0x1400039d0's DISASSEMBLY,
// where the temporary StateEntry is assembled at RSP+0x30:
//     [RSP+0x30] = ECX  -> entry+0x00  id
//     [RSP+0x38] = RDX  -> entry+0x08  enter
//     [RSP+0x40] = R9   -> entry+0x10  trans30      <-- NOT R8
//     [RSP+0x48] = 0    -> entry+0x18  trans38      <-- ALWAYS NULL for this table
//     [RSP+0x50] = R8   -> entry+0x20  update
// which differs from TradeComponent's 0x14000dd70 (whose trans38 arrives on the stack).
// Getting this backwards silently swaps the arrival test with the exit predicate.
//
//   state 0  OUTBOUND — follow the route to the dock
//     enter   53f0  latch ModelComponent/CharacterComponent, snap to the nearest route
//                   point, face along it, play animation 1
//     update  5190  retail dead code (see the note on the port) — kept faithful
//     trans30 5250  per-frame: steer (63d0); if the destination died, delete the boat
//                   and post event 0xb to the post; on arrival -> state 1
//     event 1 5670  -> state 4
//   state 1  FINAL APPROACH — drive straight at the dock position
//     enter   5150  play animation 2
//     trans30 4ff0  per-frame: move toward +0x40; within range -> state 2
//     event 1 5670  -> state 4
//   state 2  DOCKED
//     enter   4f70  stop the mover AND ***POST EVENT 9 TO THE TRADING POST*** — this
//                   single call is what unblocks layer 3
//     event 0 4f00  -> state 3
//     event 1 5670  -> state 4
//   state 3  DEPARTING — retrace the route outward
//     enter   4ec0  play animation 3
//     trans30 4d10  per-frame: steer toward +0x50; within 0x140172180 of it -> state 4
//   state 4  GONE
//     enter   4cd0  play animation 1
//     trans30 4c40  per-frame: once 63d0 runs out of route, delete the boat entity
//
// ── WHAT ACTUALLY MOVES THE HULL ─────────────────────────────────────────────────
// Nothing here does. These handlers only write STEERING into the boat's own
// CharacterComponent (+0x38): +0x28 moving flag, +0x2c..+0x34 direction, +0x38 speed,
// +0x3c heading. The CharacterComponent move tick (already live) integrates it. That is
// why the boat looked inert from every angle: the mover was live, the route was built,
// and the one thing missing was the state machine that points the mover at it.
//
// boat+0x30 = ModelComponent   (finder 0x140003f60, type-id global 0x1401c7a50)
// boat+0x38 = CharacterComponent (finder 0x1400012d0, type-id global 0x1401c7078)
// boat+0x40 = the dock position   boat+0x50 = the CURRENT waypoint
// boat+0x60/+0x68/+0x6c = route array/count/capacity   boat+0x70 = waypoint index

// graphics/Geometry.h FIRST: it pulls src/engine/volume/Volume.h, whose
// global-scope `struct Rectangle` is hidden by <wingdi.h>'s `Rectangle()` once
// <windows.h> has been seen through resolve.h.  ENDGAME B.3.5 caller batch I2.
#include "graphics/Geometry.h"
#include "Component/controller_state_table.h"
#include <new>
#include "math/Math.h"
#include "system/Value.h"
#include "graphics/Camera.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// ENDGAME B.3.8 MI-A: the tag lives with the class (its controller member is
// ControllerLive::TableController<BoatCtrlTag>, BoatComponent.h).
#include "Component/BoatComponent.h"
#include "Action/exe_bodies.h"
#include "Player/exe_bodies.h"
#include "Game/exe_constants.h"
using BoatDispatch = ::ControllerLive::Dispatcher<BoatCtrlTag>;

#include "graphics/Mesh.h"
#include "Component/CharacterComponent.h"
#include "Component/ModelComponent.h"
#include "Component/StateMachineComponent.h"
#include "Entity/ComponentEntity.h"
#include "Entity/EntityList.h"

namespace {

// ── Leaves, called directly (B.3R) ───────────────────────────────────────────────
//   0x140003f60 FindComponent<ModelComponent>
//   0x1400012d0 FindComponent<CharacterComponent>
//   0x140003ea0 FindComponent<StateMachineComponent>
//   0x1400a9660 StateMachine::Tick            0x1400c2ea0 ActHunt_MarkEntityRemoval_c2ea0
//   the Math import slots (Vector3 ctors 0x140152a30 / 0x140151268, Length 0x140151290,
//   Dot 0x140151298, Normalize 0x1401512c0 / 0x1401512b8, Heading 0x1401512d8,
//   Transform::FromYawPitchOffset 0x140151288, Transform::operator= 0x140151280) and
//   Time(float) 0x140152b00 -> the Math members / RtSystem_Time_Ctor.
// 0x140003110 was Collection::Array<void*>::SetSize(n, fill) monomorphised
// into the exe (ENDGAME B.3.2 step 2) — converted at its call site below.
//
// Float constants, by address (retail reads them from .rdata, so do we; exe_constants.h):
inline float BitsF(uint32_t bits) { float f; std::memcpy(&f, &bits, sizeof f); return f; }
const float kArrive     = kRdata_140172180;           // waypoint arrival radius
const float kPointSpace = kRdata_14017219c;           // min spacing to the NEXT waypoint
const float kSpeedBase  = kRdata_14017214c;           // ( == g_fUVScale, 1.0f )
const float kSpeedScale = kRdata_1401720ec;
const float kSpeedMin   = BitsF(kRdata_14017210c);    // 0.33
const float kDockRange  = kRdata_1401720fc;           // state 1 -> 2 range
const float kApproach   = kRdata_1401721f4;           // state 0 -> 1 range (squared)

// The boat's animation ids, as retail passes them to 0x14010a1f0.
// (0x14010a1f0 is already ported as ActMv_SetAnimation_a1f0 in action_base_move.cpp.)

// ── Small shared shapes ──────────────────────────────────────────────────────────

// Defined after the 0x1400a70c0 port below; Steer needs it first.
int NextRoutePoint(BoatComponent* boat, const Math::Vector3* ref);

// The model's world position: the translation of its transform (+0x148 + 0x10).
inline const Math::Vector3* ModelPos(ModelComponent* model) {
    if (!model) return nullptr;                     // see the guard note in Steer()
    return &model->transform.translation;
}

// The request-transition idiom every boat handler ends with, byte-for-byte:
//     if (sm[9] < 0) { Time t(0.0f); StateMachine::Tick(sm, &t, 0, 0); }
//     i = node->slot08(node, (int8)sm[9], &desired);
//     if (i >= 0) { sm[0xb] = i; sm[0xa] = (uint8)desired; }
// ControllerLive::RequestStateTransition already models the second half; the nested
// flush in the first half is retail's way of forcing a valid current index before
// asking, and it matters here because a boat handler can fire on the very tick the
// machine entered its state.
// The state machine is the one the handler received (controller_state_table.h).
void RequestTransition(StateMachine* sm, int desiredStateId) {
    if (!sm) return;
    if (sm->activeState < 0) {
        System::Time zeroTime{0};
        RtSystem_Time_Ctor(&zeroTime, 0.0f);
        // The time argument is the ADDRESS of the Time (retail passes it by reference).
        sm->Tick(&zeroTime, 0, 0);   // 0x1400a9660
    }
    ::ControllerLive::RequestStateTransition(sm, desiredStateId);
}

// Post an event to the DESTINATION entity's StateMachineComponent — the boat's only
// channel back to the trading post. Retail (0x140044fda, 0x140045311):
//     smc  = FindComponent<StateMachineComponent>(*(boat+0x28))
//     node = *(smc+0x48)                       ; the SM's current node
//     node->slot2(node, sm = smc+0x48, curIdx = (int8)smc[0x51], eventId)
// smc+0x48 IS the StateMachine struct (its +0x00 is the node), and smc+0x51 is that
// machine's current node-local index — the same +0x09 byte the node ABI uses one field
// earlier in the struct.
// The node's slot 2 is Controller::PostEvent, called by name. The tree's old null tests
// on the table and the slot it read are gone with the table read (a compiler vtable and
// its slots are never null; known_port_issues row 352).
// Row 352 (B7 RES): 0x140044fa7-0x140044fda and 0x1400452d4-0x140045311 test nothing --
// the destination, its StateMachineComponent and the current node are read straight; the
// tree's three null returns are gone.
void PostEventToDestination(BoatComponent* boat, int eventId) {
    ComponentEntity* destEntity = boat->destination.Get();

    StateMachineComponent* smc = destEntity->FindComponent<StateMachineComponent>();

    StateMachine* sm   = &smc->stateMachine;
    ControllerComponent::Controller* node = smc->stateMachine.current;
    const int curIdx = smc->stateMachine.activeState;
    node->PostEvent(sm, curIdx, eventId);
}

// Retail's "the boat is finished — delete it" tail, shared verbatim by 5250 and 4c40
// (0x140045272.. and 0x140044c52..): mark the boat's own entity for removal and push it
// onto the entity list's removal array at gw+0xE8 -> +0xd0/+0xd8.
void DeleteBoatEntity(BoatComponent* boat) {
    // 0x140045272-0x14004528c: the entity list is read before the entity's test, with
    // no test of its own; the entity pointer is retail's one test (row 352, B7 RES).
    ComponentEntity* entIBP = boat->_entity.Get();
    EntityList* const entityList = GameUpdateLoop::_singleton->m_entityList;
    if (!entIBP) return;

    if ((entIBP->_entityFlags >> 10) & 1) return;   // already going
    entIBP->MarkForRemoval();
    {
        // exe 0x140003110 IS Collection::Array<void*>::SetSize(n, fill) (kSpec_PtrZero).
        const int32_t newCount = entityList->removalList._size + 1;
        entityList->removalList.SetSizeFill(newCount, &entIBP);
    }
}

}  // namespace

// ── 0x1400463d0 — the steering core ──────────────────────────────────────────────
// Ported from DISASSEMBLY. Ghidra's decompile of this one loses the XMM plumbing badly
// (it renders the model position as three separate half-live locals and drops the
// hidden out-params of Length/Normalize entirely).
//
//   v = boat.waypoint(+0x50) - model.pos, FLATTENED to z = 0
//   if (|v| < 0x140172180) {                       // reached the current waypoint
//       i = NextRoutePoint(boat+0x50, model.pos, 0x14017219c)
//       if (i < 0) return 0;                        // route exhausted -> caller deletes
//       boat+0x70 = i;  boat+0x50 = route[i] (z = 0)
//   }
//   dir = Normalize(boat.waypoint - model.pos, &len)
//   character+0x28 = 0                              // NOT under mover control
//   character+0x2c..0x34 = Normalize(dir.xy, 0)
//   character+0x3c = Heading(dir)
//   return 1
//
// ⚠ +0x28 = 0 here but 0x14010a0e0 (used by state 1) writes 1. That is not a
// transcription slip: state 0 steers by direction only, state 1 hands the mover an
// explicit target and speed.
char BoatComponent::Steer() {
    const Math::Vector3* mp = ModelPos(model.Get());
    if (!mp) return 0;
    const Math::Vector3 modelPos = *mp;
    const Math::Vector3* wp = &waypoint;

    char ok = 1;
    {
        Math::Vector3 v(wp->x - modelPos.x, wp->y - modelPos.y, wp->z - modelPos.z);
        v.z = 0.0f;                                   // the flatten at 0x140046473
        const float len = v.Length().v;
        if (len < kArrive) {
            const int idx = NextRoutePoint(this, &modelPos);
            if (idx < 0) return 0;
            routeIndex = idx;
            const Math::Vector3& pt = route[idx];
            const Math::Scalar px(pt.x), py(pt.y), pz(0.0f);
            waypoint = Math::Vector3(px, py, pz);
        }
    }

    // dir = Normalize(waypoint - modelPos), with the length dropped on the floor here
    // (retail passes an out-param it does not read in this function).
    const Math::Vector3 toWp(wp->x - modelPos.x, wp->y - modelPos.y, wp->z - modelPos.z);
    Math::Scalar dropLen;
    const Math::Vector3 dir = toWp.Normalize(dropLen);

    CharacterComponent* chr = character.Get();
    if (!chr) return ok;
    chr->field_0x28 = false;

    const Math::Vector3 flat(dir.x, dir.y, 0.0f);
    const Math::Vector3 nrm = flat.Normalize();
    chr->field_0x2C.x = nrm.x;
    chr->field_0x2C.y = nrm.y;
    chr->field_0x2C.z = nrm.z;
    chr->field_0x3C = dir.Heading();
    return ok;
}

// ── 0x1400a70c0 — the next route point at least `spacing` away ───────────────────
// Walks FORWARD from the current index (boat+0x70) through the route array
// (base +0x60, count +0x68) and returns the first index whose point is at least
// `spacing` from `ref`; -1 once the route runs out. The caller treats -1 as "the boat
// has finished this leg" — in state 4 that is what deletes it.
//
// Note the argument: retail passes boat+0x50, i.e. the routine reads count/base/index
// at +0x18/+0x10/+0x20 RELATIVE TO THE WAYPOINT FIELD, which lands on the boat's
// +0x68/+0x60/+0x70. Same three fields, addressed from a different anchor: here the
// receiver is the boat and the three are read by name (TornadoComponent::Path is the
// same sub-object's class on the tornado; the boat's header keeps the fields flat).
extern "C" int BoatCtrl_NextRoutePoint(const BoatComponent* boat, const float* ref, float spacing) {
    const int   count = boat->route._size;
    const Math::Vector3* base = boat->route._data;
    int i = boat->routeIndex;
    if (!base) return -1;
    const float r2 = spacing * spacing;
    for (;;) {
        ++i;
        if (i >= count) return -1;
        const float dx = ref[0] - base[i].x;
        const float dy = ref[1] - base[i].y;
        const float dz = ref[2] - 0.0f;
        if (dx * dx + dy * dy + dz * dz >= r2) return i;
    }
}

namespace {
int NextRoutePoint(BoatComponent* boat, const Math::Vector3* ref) {
    const float spacing = kPointSpace;
    return BoatCtrl_NextRoutePoint(boat, &ref->x, spacing);
}
}  // namespace

// ── 0x14010a0e0 — point the mover at a world target ──────────────────────────────
//     character+0x28 = 1
//     dir = Normalize(target - character.model(+0x68).pos)
//     character+0x2c..0x34 = dir
//     character+0x3c = heading      (passed in — retail hands it boat+0x4c)
//     character+0x38 = character.model(+0x13c) * speed
void CharacterComponent::DriveToward(const Math::Vector3* target, float heading, float speed) {
    field_0x28 = true;

    ModelComponent* model = modelComponent.Get();
    if (!model) return;
    const Math::Vector3* mp = ModelPos(model);

    const Math::Vector3 v(target->x - mp->x, target->y - mp->y, target->z - mp->z);
    const Math::Vector3 nrm = v.Normalize();
    field_0x2C.x = nrm.x;
    field_0x2C.y = nrm.y;
    field_0x2C.z = nrm.z;
    ModelComponent* model2 = modelComponent.Get();
    field_0x3C = heading;
    // +0x13c is the MeshInstance's _color[2], the visual scale AnimalComponent writes.
    field_0x38 =
        model2 ? model2->meshInstance._color[2] * speed : 0.0f;
}

// 0x14010a1f0 SetAnimation, already ported for the citizen mover (ActMv_SetAnimation_a1f0,
// Action/exe_bodies.h).

namespace {
void SetAnim(BoatComponent* boat, uint32_t animId) {
    CharacterComponent* chr = boat->character.Get();
    if (!chr) return;
    const float f = kSpeedBase;
    chr->SetAnimation(animId, f, f);
}
}  // namespace

// ============================================================
// state 0 ENTER — 0x1400453f0
// ============================================================
// Latches the two sibling components the rest of the machine steers through, snaps the
// current waypoint to the first route point far enough away, faces the hull along the
// route and starts animation 1.
char BoatComponent::State0Enter() {
    model = _entity->FindComponent<ModelComponent>();           // 0x140003f60
    character = _entity->FindComponent<CharacterComponent>();   // 0x1400012d0

    const Math::Vector3* pts = route._data;
    if (!pts) {
        // Retail derefs this unguarded — it relies on the route builder having run. If we
        // are here with no route the fault is upstream (layer 2), and a silent no-op would
        // hide it, so say so once and bail rather than fault.
        std::fprintf(stderr, "[BoatCtrl] state0 enter with NO ROUTE (boat+0x60 null) — "
                             "the route builder did not run for this merchant\n");
        std::fflush(stderr);
        return 1;
    }

    // Snap to the nearest usable route point when we are already close to route[0].
    {
        const Math::Vector3 p0(pts[0].x, pts[0].y, 0.0f);
        const Math::Vector3* wp = &waypoint;
        const Math::Vector3 v(wp->x - p0.x, wp->y - p0.y, wp->z - 0.0f);
        const float len = v.Length().v;
        if (len < kArrive) {
            const int idx = NextRoutePoint(this, &p0);
            if (idx >= 0) {
                routeIndex = idx;
                const Math::Vector3& pt = pts[idx];
                const Math::Scalar px(pt.x), py(pt.y), pz(0.0f);
                waypoint = Math::Vector3(px, py, pz);
            }
        }
    }

    // Face along (waypoint - route[0]) and push the transform into the model + geometry.
    {
        const Math::Vector3* wp = &waypoint;
        const Math::Vector3 dir(wp->x - pts[0].x, wp->y - pts[0].y, wp->z - 0.0f);
        ModelComponent* m = model.Get();
        if (m) {
            const float yaw   = dir.Heading();
            const float pitch = 0.0f;
            // ⚠ The OFFSET is route[0], not the origin (R9 = [RBP-0x21] at 0x1400455db,
            // the Vector3 built from route[0] at 0x140045595). This transform is what
            // puts the newly spawned hull at the start of its route; passing a zero
            // offset would park every merchant at the map origin.
            const Math::Vector3 offset(pts[0].x, pts[0].y, 0.0f);
            const Math::Transform xform = Math::Transform::FromYawPitchOffset(
                Math::Scalar(yaw), Math::Scalar(pitch), offset);
            Math::Transform* modelTransform = &m->transform;
            *modelTransform = xform;
            m->yaw = yaw;
            m->meshInstance.SetTransform(*modelTransform);
        }
    }

    SetAnim(this, 1);
    return 1;
}

// ============================================================
// state 0 UPDATE — 0x140045190
// ============================================================
// ⚠ THIS IS DEAD CODE IN RETAIL, AND IT IS PORTED THAT WAY DELIBERATELY.
// The disassembly builds its test vector from the waypoint MINUS ITSELF:
//     MOVSS XMM3,XMM1 / SUBSS XMM3,XMM1     -> x component = 0
//     MOVSS XMM0,XMM2 / SUBSS XMM0,XMM2     -> y component = 0
//     MOVSS [..],XMM6 (XORPS'd)             -> z component = 0
// so it takes Length((0,0,0)) and enters the re-pick branch only when that is < 0.
// It never is. Whatever the source line meant to subtract, the shipped binary does
// nothing here, and the real waypoint advance lives in 0x1400463d0 (Steer) which
// the per-frame trans30 handler calls. Reproducing the fold rather than "fixing" it
// keeps the port honest — and it matters, because a repaired version would advance the
// waypoint twice per frame.
char BoatComponent::State0Update() {
    const Math::Vector3 zero(0.0f, 0.0f, 0.0f);
    const float len = zero.Length().v;
    if (len < 0.0f) {                     // never taken; kept for shape
        const int idx = NextRoutePoint(this, &waypoint);
        if (idx >= 0) {
            routeIndex = idx;
            const Math::Vector3& pt = route[idx];
            waypoint.x = pt.x;
            waypoint.y = pt.y;
            waypoint.z = 0.0f;
        }
    }
    return 1;
}

// ============================================================
// state 0 TRANS30 (per-frame) — 0x140045250
// ============================================================
// Steer; if the route ran out, scrap the boat and tell the post its merchant is gone
// (event 0xb — the handler layer 1 already ported as TradeCtrl_Event0B_MerchantLost).
// Then, unconditionally, test arrival at the dock and hand over to state 1.
//
// The arrival test is Dot(d, d) — the SQUARED distance — against 0x1401721f4, which is
// therefore a squared radius. Retail calls Vector3::Dot with the same vector for both
// operands (RCX and R8 are the same address at 0x14004532c).
char BoatComponent::State0Tick(StateMachine* smV) {
    if (!Steer()) {
        DeleteBoatEntity(this);
        PostEventToDestination(this, 0xb);
    }

    const Math::Vector3* mp = ModelPos(model.Get());
    if (!mp) return 1;
    const Math::Vector3* dockPos = &dockPosition;
    const Math::Vector3 d(mp->x - dockPos->x, mp->y - dockPos->y, mp->z - dockPos->z);
    const float dist2 = d.Dot(d).v;

    if (kApproach > dist2) {
        RequestTransition(smV, 1);
    }
    return 1;
}

// ============================================================
// state 1 ENTER — 0x140045150
// ============================================================
char BoatComponent::State1Enter() {
    SetAnim(this, 2);
    return 1;
}

// ============================================================
// state 1 TRANS30 (per-frame) — 0x140044ff0
// ============================================================
// Drive straight at the dock position and, when close enough, dock.
//
//   d      = boat+0x40 - model.pos
//   dir    = Normalize(d, &dist)
//   speed  = clamp(dist * 0x1401720ec, 0x14017210c, 0x14017214c)
//   DriveToward(character, boat+0x40, heading = boat+0x4c, speed)
//   if (0x1401720fc > dist) -> state 2
//
// The clamp is read off the branch pair at 0x14005008d/0x14005009a: `COMISS XMM6,max;
// JA take-max` then `COMISS min,XMM6; JBE keep`, i.e. high clamp to 0x14017214c and low
// clamp to 0x14017210c. Ghidra renders this as nonsense involving `DAT_1401720ec * 0.0`.
char BoatComponent::State1Tick(StateMachine* smV) {
    const Math::Vector3* mp = ModelPos(model.Get());
    if (!mp) return 1;
    const Math::Vector3* dockPos = &dockPosition;
    const Math::Vector3 d(dockPos->x - mp->x, dockPos->y - mp->y, dockPos->z - mp->z);
    Math::Scalar distS;
    const Math::Vector3 dir = d.Normalize(distS);
    (void)dir;
    const float dist = distS.v;

    float speed = dist * kSpeedScale;
    const float hi = kSpeedBase;
    const float lo = kSpeedMin;
    // 0x14005008d: COMISS speed,hi / JA take-XMM0 (XMM0 == hi)
    // 0x14005009a: COMISS lo,speed  / JBE keep      (else XMM0 == lo)
    if (speed > hi) speed = hi;
    else if (lo > speed) speed = lo;

    CharacterComponent* chr = character.Get();
    if (chr)
        chr->DriveToward(&dockPosition, dockHeading, speed);

    if (kDockRange > dist) {
        RequestTransition(smV, 2);
    }
    return 1;
}

// ============================================================
// state 2 ENTER — 0x140044f70   ***THE EVENT-9 CALL SITE***
// ============================================================
// Stop the mover (SetAnimation 0 at speed 0/0) and post event 9 to the trading post.
// This is the single instruction sequence the whole of layer 3 was waiting on: the trade
// machine's state 0x12 has no per-frame handler at all, only events 9 and 0xb, so until
// something raised 9 it could never reach 0x13 and run the manifest / buy-and-pay code.
char BoatComponent::State2Enter() {
    // 0x140044f8d/f95: XMM2 = 0x14017214c, XMM3 = 0 (XORPS), EDX = 0.
    if (CharacterComponent* chr = character.Get())
        chr->SetAnimation(0, kSpeedBase, 0.0f);

    PostEventToDestination(this, 9);
    return 1;
}

// ============================================================
// state 2 EVENT 0 — 0x140044f00      (the post releases the merchant)
// ============================================================
// Reads nothing of its receiver: stays free (B1).
extern "C" char BoatCtrl_State2_Event0(BoatComponent* /*self*/, StateMachine* smV) {
    RequestTransition(smV, 3);
    return 1;
}

// ============================================================
// EVENT 1 on states 0/1/2 — 0x140045670   (abort: go straight to GONE)
// ============================================================
// Reads nothing of its receiver: stays free (B1).
extern "C" char BoatCtrl_Event1_Abort(BoatComponent* /*self*/, StateMachine* smV) {
    RequestTransition(smV, 4);
    return 1;
}

// ============================================================
// state 3 ENTER — 0x140044ec0
// ============================================================
char BoatComponent::State3Enter() {
    if (CharacterComponent* chr = character.Get()) {
        const float f = kSpeedBase;
        chr->SetAnimation(3, f, f);
    }
    return 1;
}

// ============================================================
// state 3 TRANS30 (per-frame) — 0x140044d10
// ============================================================
//   Length(boat+0x40 - boat+0x50, &len)            len at [RBP+0x20] (+0x36 LEA RDX,[RBP+0x20])
//   dir = Normalize(boat+0x50 - model.pos, &len)   the same [RBP+0x20] (+0x94 LEA R8,[RBP+0x20])
//   character+0x28 = 0
//   character+0x2c..0x34 = Normalize(dir.xy, 0)
//   character+0x3c = Heading(dir)
//   if (0x140172180 > [RBP+0x20]) -> state 4                         (+0x12a COMISS)
// The second call overwrites the first length, so the gate is the boat -> waypoint distance.
// Offsets are from the function's start.
char BoatComponent::State3Tick(StateMachine* smV) {
    const Math::Vector3* mp = ModelPos(model.Get());
    if (!mp) return 1;
    const Math::Vector3 modelPos = *mp;
    const Math::Vector3* dockPos = &dockPosition;
    const Math::Vector3* wp      = &waypoint;

    const Math::Vector3 fromDock(dockPos->x - wp->x, dockPos->y - wp->y, dockPos->z - wp->z);
    Math::Scalar gateLen = fromDock.Length();

    const Math::Vector3 toWp(wp->x - modelPos.x, wp->y - modelPos.y, wp->z - modelPos.z);
    const Math::Vector3 dir = toWp.Normalize(gateLen);
    const float gate = gateLen.v;

    if (CharacterComponent* chr = character.Get()) {
        chr->field_0x28 = false;
        const Math::Vector3 flat(dir.x, dir.y, 0.0f);
        const Math::Vector3 nrm = flat.Normalize();
        chr->field_0x2C.x = nrm.x;
        chr->field_0x2C.y = nrm.y;
        chr->field_0x2C.z = nrm.z;
        chr->field_0x3C = dir.Heading();
    }

    if (kArrive > gate) {
        RequestTransition(smV, 4);
    }
    return 1;
}

// ============================================================
// state 4 ENTER — 0x140044cd0
// ============================================================
char BoatComponent::State4Enter() {
    SetAnim(this, 1);
    return 1;
}

// ============================================================
// state 4 TRANS30 (per-frame) — 0x140044c40
// ============================================================
// Same steer call, same "route exhausted" test — but no event to the post, because by
// state 4 the post has already released this merchant. Just delete the entity.
char BoatComponent::State4Tick() {
    if (!Steer()) DeleteBoatEntity(this);
    return 1;
}

// ============================================================
// Table build — replicates 0x1400468a0
// ============================================================
// The table (controller_state_table.h) holds plain handler addresses called as
// handler([node+0x18], sm, time): [node+0x18] is the controller's back-pointer,
// this component, so each entry is a one-line adapter that calls the member.
namespace {
char BoatState0Enter(BoatComponent* b, StateMachine* /*sm*/)               { return b->State0Enter(); }    // 53f0
char BoatState0Update(BoatComponent* b, StateMachine* /*sm*/)              { return b->State0Update(); }   // 5190
char BoatState0Tick(BoatComponent* b, StateMachine* sm, const System::Time* /*time*/)      { return b->State0Tick(sm); }   // 5250
char BoatState1Enter(BoatComponent* b, StateMachine* /*sm*/)               { return b->State1Enter(); }    // 5150
char BoatState1Tick(BoatComponent* b, StateMachine* sm, const System::Time* /*time*/)      { return b->State1Tick(sm); }   // 4ff0
char BoatState2Enter(BoatComponent* b, StateMachine* /*sm*/)               { return b->State2Enter(); }    // 4f70
char BoatState3Enter(BoatComponent* b, StateMachine* /*sm*/)               { return b->State3Enter(); }    // 4ec0
char BoatState3Tick(BoatComponent* b, StateMachine* sm, const System::Time* /*time*/)      { return b->State3Tick(sm); }   // 4d10
char BoatState4Enter(BoatComponent* b, StateMachine* /*sm*/)               { return b->State4Enter(); }    // 4cd0
char BoatState4Tick(BoatComponent* b, StateMachine* /*sm*/, const System::Time* /*time*/)                { return b->State4Tick(); }     // 4c40
}  // namespace

// Idempotent: the installer runs once per component instance.
extern "C" void BoatCtrl_PopulateTable() {
    auto& t = BoatDispatch::table;
    if (t.count != 0) return;
    using namespace ::ControllerLive;

    // TableRegisterState(t, id, enter, trans30, trans38, update)
    // state 0 — outbound along the route
    TableRegisterState(t, 0,
        &BoatState0Enter,    // 53f0
        &BoatState0Tick,     // 5250  (R9)
        nullptr,                                      // retail: always 0
        &BoatState0Update);  // 5190  (R8)
    TableAddTransition(t, 1, nullptr);
    TableAddTransition(t, 4, nullptr);
    TableAddEvent(t, 1, &BoatCtrl_Event1_Abort);   // 5670

    // state 1 — final approach
    TableRegisterState(t, 1,
        &BoatState1Enter,    // 5150
        &BoatState1Tick,     // 4ff0
        nullptr, nullptr);
    TableAddTransition(t, 2, nullptr);
    TableAddTransition(t, 4, nullptr);
    TableAddEvent(t, 1, &BoatCtrl_Event1_Abort);

    // state 2 — docked (posts event 9 to the post on entry)
    TableRegisterState(t, 2,
        &BoatState2Enter,    // 4f70
        nullptr, nullptr, nullptr);
    TableAddTransition(t, 3, nullptr);
    TableAddTransition(t, 4, nullptr);
    TableAddEvent(t, 0, &BoatCtrl_State2_Event0);  // 4f00
    TableAddEvent(t, 1, &BoatCtrl_Event1_Abort);

    // state 3 — departing
    TableRegisterState(t, 3,
        &BoatState3Enter,    // 4ec0
        &BoatState3Tick,     // 4d10
        nullptr, nullptr);
    TableAddTransition(t, 4, nullptr);

    // state 4 — gone
    TableRegisterState(t, 4,
        &BoatState4Enter,    // 4cd0
        &BoatState4Tick,     // 4c40
        nullptr, nullptr);

    std::fprintf(stderr, "[BoatCtrl] table built: %d states (0..4)\n", t.count);
    std::fflush(stderr);
}
