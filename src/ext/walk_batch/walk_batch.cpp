// src/ext/walk_batch/walk_batch.cpp -- see walk_batch.h. NOT RETAIL: ext.walk_batch (class A,
// docs/extensions/handins/WALKBATCH.md) and ext.walk_batch_grid (class B, handins/WALKGRID.md).
//
// graphics/*.h FIRST (as citizenai_executor.cpp): Volume.h's global `struct Rectangle` and
// <wingdi.h>'s `Rectangle()`.
#include "graphics/Mesh.h"                    // MeshInstance::_rootOnlyUpdates, RootAnimationOffset
#include "ext/walk_batch/walk_batch.h"
#include "ext/walk_batch/walk_batch_rules.h"

#include <cstdint>
#include <cstring>
#include <vector>

#include "math/Math.h"
#include "object/Internal.h"
#include "object/class_vptr.h"   // Object::ClassVptr (the vptr hint)
#include "Action/Action.h"
#include "Action/MoveTo.h"
#include "Component/CharacterComponent.h"
#include "Component/CharacterDescription.h"
#include "Component/CharacterGrid.h"
#include "Component/ModelComponent.h"
#include "Game/GameUpdateLoop.h"               // GameUpdateLoop::_singleton (m_game, m_characterGrid)
#include "Game/Map.h"
#include "Game/terrain_sidecar.h"              // the terrain byte MoveTo::Execute reads (same mirror)
#include "ext/registry.h"                      // Ext::Ext().walk_batch (A: live) / .walk_batch_grid (B: locked)
#include "options.h"                           // Opt().movetickTrace (read once)
#include "stress_harness.h"                    // g_verifyOn / StressHarness_Verify (the shadows)

namespace Ext {
namespace WalkBatch {

long long g_batchedTicks = 0;
long long g_profTicks = 0;
long long g_profCalls = 0;

namespace {

int s_traceOn = -1;            // BANISHED_MOVETICK_TRACE, read once (as the move tick does)
unsigned s_frameSerial = 0;    // bumped at the first batched iteration of a frame (the shadow's key)

bool TraceOn() {
    if (s_traceOn < 0) {
        const char* e = Opt().movetickTrace;
        s_traceOn = (e && e[0] && e[0] != '0') ? 1 : 0;
    }
    return s_traceOn == 1;
}

// The fields one batched tick writes, as a value (the COMPPROF=4 shadow saves them, runs the
// batched step, restores them and runs the real tick on the live object).
struct Fields {
    Math::Vector3 pos;   // model +0x158
    float yaw;           // model +0x168
    float char38;        // CharacterComponent +0x38 (the speed Execute stores)
    bool  char28;        // +0x28 (UpdateFacing clears it)
    Math::Vector3 dir;   // +0x2C (UpdateFacing)
    float char3C;        // +0x3C (UpdateFacing)
    int32_t pathIndex;   // MoveTo +0x60
};

Fields Read(const CharacterComponent* self, const ModelComponent* model, const MoveTo* mv) {
    Fields f;
    f.pos = model->transform.translation;
    f.yaw = model->yaw;
    f.char38 = self->field_0x38;
    f.char28 = self->field_0x28;
    f.dir = self->field_0x2C;
    f.char3C = self->field_0x3C;
    f.pathIndex = mv->pathIndex;
    return f;
}

void Write(CharacterComponent* self, ModelComponent* model, MoveTo* mv, const Fields& f) {
    model->transform.translation = f.pos;
    model->yaw = f.yaw;
    self->field_0x38 = f.char38;
    self->field_0x28 = f.char28;
    self->field_0x2C = f.dir;
    self->field_0x3C = f.char3C;
    mv->pathIndex = f.pathIndex;
}

inline uint32_t Bits(float v) { uint32_t b; std::memcpy(&b, &v, sizeof b); return b; }

// The decision. Returns the MoveTo whose walking branch this tick is, or null (the real tick
// runs). Every test reads what the executor / Execute would read first, in their order, and
// nothing a declined tick would not read.
MoveTo* Eligible(CharacterComponent* self, int* curCellOut, int* destCellOut) {
    // The move tick's head: the executor runs only for a non-empty stack, and the tail only
    // for a placed character. Neither an idle character nor an unplaced one is batched.
    const int32_t cur = self->actionStack.current;
    if (cur < 0) return nullptr;
    if (self->characterIndex < 0) return nullptr;
    // The executor's top entry (ActionStack::Execute): a null slot pops, a node not yet
    // entered runs its precheck, a chain runs every node -- all real ticks. Batched: one
    // entered MoveTo, alone, that moves this character.
    Action* top = self->actionStack.actions[cur].Get();
    if (top == nullptr) return nullptr;
    if (*reinterpret_cast<const void* const*>(top) != Object::ClassVptr<MoveTo>::Get()) return nullptr;  // CAST-OK: vslot (MoveTo is final: the vptr names the class)
    MoveTo* mv = static_cast<MoveTo*>(top);
    if (!mv->_status) return nullptr;
    if (mv->_next.Get() != nullptr) return nullptr;
    if (mv->character.Get() != self) return nullptr;
    // MoveTo::Execute's own branch tests (walk_batch_rules.h DecideWalk).
    const int32_t pathIndex = mv->pathIndex;
    if (pathIndex < 0) return nullptr;   // kPathWait (read before the map, as Execute)
    const Map* map = GameUpdateLoop::_singleton->m_game;
    const Math::Vector3& at = self->modelComponent->transform.translation;
    const int mapSize = map->size;
    const int destCell = WorldToCell(mapSize, mv->destination.x, mv->destination.y);
    const int curCell = WorldToCell(mapSize, at.x, at.y);
    if (DecideWalk(pathIndex, mv->pathCount, curCell, destCell, mv->field_0x474) != kBatch)
        return nullptr;
    *curCellOut = curCell;
    *destCellOut = destCell;
    return mv;
}

// One batched tick: MoveTo::Execute's walking branch, then the move tick's tail with the
// root-only flag set, minus the trig / rotation store and the Geometry push. kPlace
// (ext.walk_batch) ends with the faithful tail's Place; without it (ext.walk_batch_grid) the
// grid keeps the walker's cell of its last real tick.
template <bool kPlace>
void Step(CharacterComponent* self, MoveTo* mv, int curCell) {
    Map* map = GameUpdateLoop::_singleton->m_game;
    // ---- MoveTo::Execute 0x1400c8570, case 2, not in the destination cell -------------
    const uint8_t terrainType = TerrainSidecar::Get(map->cells.GetData(), map->cells.GetSize(), curCell);
    const CharacterDescription* mdesc = mv->description;   // MoveTo +0x30
    const float cellSpeed = TerrainSpeed(terrainType, mdesc->_f34, mdesc->_f38);
    self->field_0x38 = cellSpeed * mv->speedMultiplier;    // character->+0x38 (character == self)
    const int32_t pathIndex = mv->pathIndex;
    if (curCell == mv->path[pathIndex]) {
        mv->pathIndex = pathIndex + 1;
        mv->UpdateFacing();                                // 0x1400c8d20, the real body
    }
    // ---- the tail, rootOnly set (citizenai_executor.cpp:314-375, 436-493) -------------
    ModelComponent* model = self->modelComponent.Get();
    const float fx = model->transform.translation.x;
    const float fy = model->transform.translation.y;
    const float fz = model->transform.translation.z;
    const CharacterDescription* desc = self->characterDescription;
    float fVar14 = Heading(self->field_0x3C, model->yaw, desc->_f24, desc->_slowSpeed);
    const Math::Vector3 root = model->meshInstance.RootAnimationOffset();
    const float speed = RootSpeed(root.x, root.y, root.z, self->field_0x38);
    const Pos p = StepPos(fx, fy, fz, speed, self->field_0x2C.x, self->field_0x2C.y,
                          self->field_0x2C.z, desc->_b3c, desc->_f40);
    fVar14 = fVar14 + desc->_f24;
    model->transform.translation.x = p.x;
    model->transform.translation.y = p.y;
    model->transform.translation.z = p.z;
    model->yaw = fVar14;
    // kPlace: the faithful tail's last call, same index, same model state (the tail's Geometry
    // push in between does not touch what Place reads). Without it: the class B grid lag.
    if constexpr (kPlace)
        GameUpdateLoop::_singleton->m_characterGrid->Place(self->characterIndex);
}

// ---- the shadows (BANISHED_STRESS_COMPPROF=4) -------------------------------------------
// "The grid cell the batch would have left this character in": the cell of its last real
// Place (the frame's first batched tick takes the cell the grid holds, which the last real
// tick placed), per grid index, keyed with the frame.
struct GridMemo { unsigned serial; int32_t cell; const CharacterComponent* who; };
std::vector<GridMemo> s_gridMemo;

int32_t GridCell(int idx) {
    const CharacterGrid* grid = GameUpdateLoop::_singleton->m_characterGrid;
    if (idx < 0 || idx >= grid->slots._size) return -1;
    return grid->slots[idx].cell;
}

GridMemo& MemoFor(const CharacterComponent* self) {
    const size_t i = static_cast<size_t>(self->characterIndex);
    if (i >= s_gridMemo.size()) s_gridMemo.resize(i + 256, GridMemo{ 0, -1, nullptr });
    GridMemo& m = s_gridMemo[i];
    if (m.serial != s_frameSerial || m.who != self)
        m = GridMemo{ s_frameSerial, GridCell(self->characterIndex), self };
    return m;
}

// The tick under the shadow. Every tick is the real one (the sim stays the faithful sim);
// a tick the batch would take is first computed by the batched step on the live object,
// read back and undone, then the real tick runs and the two results are compared.
//   WalkBatchSim   detail bit 0  a position component differs by more than 1e-4
//                         bit 1  the position bits differ at all
//                         bit 2  yaw, +0x38, +0x2C, +0x3C, +0x28 or the path index bits differ
//                  expected 0 mismatches: the batched step is retail's arithmetic.
//   WalkBatchSpan  a batched tick whose real tick did something else (the stack changed, an
//                  arrival, a pop): expected 0, that is what the decline rule is for.
//   WalkBatchGrid  the real tick moved the character to another grid cell than the one the
//                  batch would have left it in. kPlace (ext.walk_batch): the batch places it
//                  too, expected 0. Without it (ext.walk_batch_grid): the reference is retail's
//                  Place (the real tick's) against the cell of the walker's last real Place,
//                  expected nonzero -- the class B evidence.
template <bool kPlace>
bool TickShadow(CharacterComponent* self, const System::Time* time) {
    int curCell = -1, destCell = -1;
    MoveTo* mv = Eligible(self, &curCell, &destCell);
    if (mv == nullptr) {
        self->Tick(time);   // the real tick; the batch's grid cell is now the real one
        if (self->characterIndex >= 0) {
            GridMemo& m = MemoFor(self);
            m.cell = GridCell(self->characterIndex);
        }
        return true;
    }
    ++g_batchedTicks;
    ModelComponent* model = self->modelComponent.Get();
    GridMemo& memo = MemoFor(self);
    int32_t batchCell = memo.cell;
    const Fields before = Read(self, model, mv);
    Step<kPlace>(self, mv, curCell);
    // kPlace: Step placed the character itself; the real tick's Place below then sees the same
    // rect (memo hit) unless the positions differ (WalkBatchSim bit 1). Without it the batch
    // left the grid alone: batchCell stays the last real Place's cell.
    if constexpr (kPlace) batchCell = GridCell(self->characterIndex);
    const Fields batched = Read(self, model, mv);
    Write(self, model, mv, before);
    const int32_t cur0 = self->actionStack.current;
    self->Tick(time);
    // The real tick may have popped the MoveTo (it must not have: WalkBatchSpan).
    const bool sameTop = self->actionStack.current == cur0 &&
                         self->actionStack.actions[cur0].Get() == mv;
    StressHarness_Verify("WalkBatchSpan", sameTop, sameTop ? 0 : 1);
    if (!sameTop) return true;
    const Fields real = Read(self, model, mv);
    long long detail = 0;
    const float dx = real.pos.x - batched.pos.x, dy = real.pos.y - batched.pos.y,
                dz = real.pos.z - batched.pos.z;
    const float tol = 1e-4f;
    if (!(dx <= tol && dx >= -tol) || !(dy <= tol && dy >= -tol) || !(dz <= tol && dz >= -tol))
        detail |= 1;
    if (std::memcmp(&real.pos, &batched.pos, sizeof real.pos) != 0) detail |= 2;
    if (Bits(real.yaw) != Bits(batched.yaw) || Bits(real.char38) != Bits(batched.char38) ||
        std::memcmp(&real.dir, &batched.dir, sizeof real.dir) != 0 ||
        Bits(real.char3C) != Bits(batched.char3C) || real.char28 != batched.char28 ||
        real.pathIndex != batched.pathIndex)
        detail |= 4;
    StressHarness_Verify("WalkBatchSim", detail == 0, detail);
    const int32_t realCell = GridCell(self->characterIndex);
    StressHarness_Verify("WalkBatchGrid", realCell == batchCell, realCell == batchCell ? 0 : 1);
    // kPlace: the batch's own Place is the new reference cell. Without it memo.cell stays (the
    // batch would not have placed the walker).
    if constexpr (kPlace) memo.cell = batchCell;
    return true;
}

// Tick / TickGrid's body.
template <bool kPlace>
[[gnu::always_inline]] inline bool TickImpl(CharacterComponent* self, const System::Time* time) {
    if (g_verifyOn) return TickShadow<kPlace>(self, time);
    int curCell = -1, destCell = -1;
    MoveTo* mv = Eligible(self, &curCell, &destCell);
    if (mv == nullptr) return false;
    ++g_batchedTicks;
    Step<kPlace>(self, mv, curCell);
    return true;
}

// TickCharacter / TickCharacterGrid's body: it calls the public Tick / TickGrid, as
// TickCharacter always called Tick.
template <bool kPlace>
[[gnu::always_inline]] inline bool TickOf(CharacterComponent* self, const System::Time* time) {
    if constexpr (kPlace) return Tick(self, time);
    else return TickGrid(self, time);
}

template <bool kPlace>
[[gnu::always_inline]] inline void TickCharacterImpl(CharacterComponent* self, const System::Time* time) {
    if (g_compProfOn >= 2) {
        // As entitylist.cpp's TickOneComponent profiling arm (same brackets, same order).
        const int slot = CompProf_BeginKind(self, 1);
        const int charTok = CharProf_Begin(self, slot);
        const long long q0 = CompProf_Now();
        const bool batched = TickOf<kPlace>(self, time);
        if (!batched) self->Tick(time);
        const long long dt = CompProf_Now() - q0;
        CharProf_End(charTok, dt);
        CompProf_End(slot, dt);
        if (batched) { g_profTicks += dt; ++g_profCalls; }
        return;
    }
    if (!TickOf<kPlace>(self, time)) self->Tick(time);
}

}  // namespace

int g_frameWalk = kWalkFaithful;

void BeginFrame() {
    // Both rows, once per frame. ext.walk_batch_grid's value is the game's (class B: the
    // registry keeps the value the game was locked with); it implies the batch on its own.
    const ::Ext::Extensions& x = ::Ext::Ext();
    g_frameWalk = TraceOn() ? kWalkFaithful
                : x.walk_batch_grid ? kWalkNoPlace
                : x.walk_batch ? kWalkPlace
                : kWalkFaithful;
    ++s_frameSerial;   // the shadow's frame key (a frame's batched ticks share it)
}

bool Tick(CharacterComponent* self, const System::Time* time) { return TickImpl<true>(self, time); }

bool TickGrid(CharacterComponent* self, const System::Time* time) { return TickImpl<false>(self, time); }

void TickCharacter(CharacterComponent* self, const System::Time* time) {
    TickCharacterImpl<true>(self, time);
}

void TickCharacterGrid(CharacterComponent* self, const System::Time* time) {
    TickCharacterImpl<false>(self, time);
}

}  // namespace WalkBatch
}  // namespace Ext
