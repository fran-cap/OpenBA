// ============================================================================
// tests/unit/ext_walk_batch_test.cpp -- ext.walk_batch (class A) and ext.walk_batch_grid
// (class B): the batched walk at speed N
// ============================================================================
// src/ext/walk_batch/walk_batch_rules.h holds the arithmetic and the decline rule the batched
// tick runs (walk_batch.cpp composes them around the game types). These tests check:
//   * the registry rows: walk_batch (class A, formatVersion 0, default off, live under a game's
//     lock, never a pseudo-mod); walk_batch_grid's row (class B) is ext_walk_batch_grid_test.cpp;
//   * each rule against a literal copy of its retail body (terrain speed, the cell, the decline
//     rule's truth table);
//   * two copies of one scripted world at speeds 1..20: the REFERENCE runs every tick in full
//     (literal copies of MoveTo::Execute and of the move tick's tail, citizenai_executor.cpp,
//     including the trig, the last-iteration terrain z and the grid placement -- both
//     production regions sit between [COPY-PINNED] markers hashed by copy_drift_test.cpp,
//     so a change there fails until these copies are re-synced); the BATCHED
//     world runs, on the intermediate iterations, the walk_batch.cpp composition of the rules
//     for every tick DecideWalk accepts and the full tick otherwise, and the full tick on the
//     last iteration (the faithful walk). After EVERY iteration position, yaw, the speed
//     +0x38, the facing +0x2C / +0x3C, the path index and the stack must be bit-identical
//     (the batch writes them through); after the LAST iteration the rotation and the grid
//     cell must be too (the catch-up). With Place every tick (ext.walk_batch) the grid cell
//     must match on every tick too. Without it (ext.walk_batch_grid) the grid cell may differ
//     on an intermediate iteration -- the class B lag -- and must then be exactly the
//     reference's cell of k ticks before, 1 <= k <= N-1 (the walker's last real tick); the
//     lag is counted and required to be nonzero.
// The script applies, between iterations, what the game can do to a walker mid-frame: the
// follow flag (MoveTo::Enter at bucket rate), a Clear and a new path from where the citizen
// stands (Work::Cleanup / AbortForEntity / a new work), a teleport (spawn / tornado writers).
// ============================================================================
#include "unit.h"
#include "ext/walk_batch/walk_batch_rules.h"
#include "ext/registry.h"
#include "Game/exe_constants.h"
#include "crt/msvc_libm.h"   // the tail's trig, the ported CRT sinf / cosf

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

float FromBits(uint32_t b) { float f; std::memcpy(&f, &b, 4); return f; }
uint32_t Bits(float f) { uint32_t b; std::memcpy(&b, &f, 4); return b; }

const Ext::Entry& E(const char* id) {
    const Ext::Entry* e = Ext::FindEntry(id);
    CHECK(e != nullptr);
    static Ext::Entry none{};
    return e ? *e : none;
}

struct Lcg {
    uint64_t s;
    uint32_t Next() { s = s * 6364136223846793005ULL + 1442695040888963407ULL; return static_cast<uint32_t>(s >> 33); }
    float Unit() { return static_cast<float>(Next() & 0xffffff) / 16777216.0f; }
    bool Chance(int outOf, int n) { return static_cast<int>(Next() % static_cast<uint32_t>(outOf)) < n; }
};

// ---- the scripted world -----------------------------------------------------------------
struct Desc {   // CharacterDescription (+0x24 .. +0x44); the MoveTo's +0x30 is the same object
    float f24, slow28, f30, f34, f38; bool b3c; float f40, f44;
};
struct Walker {
    // CharacterComponent
    float c38 = 0, c3C = 0; float dir[3] = { 0, 0, 0 }; bool c28 = true;
    int index = 0;
    // ModelComponent (+0x148 rotation, +0x158 translation, +0x168 yaw)
    float rot[4] = { 0, 0, 0, 1 }; float pos[3] = { 0, 0, 0 }; float yaw = 0;
    // the MoveTo on top of the stack (active = the stack holds it)
    bool active = false;
    int pathIndex = -1, pathCount = 0; int path[256] = {};
    float dest[3] = { 0, 0, 0 }; bool follow = false; float mult = 1.0f;
    // the model's clock (its own tick runs after the character tick, every iteration)
    int clock = 0;
    // CharacterGrid::Place's result (a stand-in grid: the map cell)
    int gridCell = -1;
    const Desc* desc = nullptr;
};
struct World {
    int size = 32;
    std::vector<uint8_t> terrain;
    std::vector<Walker> w;
};

// The instanced clip's root offset per tick, a function of the model's clock (both worlds
// read it at the same tick, as the batched tick calls the real RootAnimationOffset).
// Full-mantissa values (an integer hash of the clock), so a reordered sum rounds differently.
float HashUnit(uint32_t h) {
    h ^= h >> 16; h *= 0x7feb352dU; h ^= h >> 15; h *= 0x846ca68bU; h ^= h >> 16;
    return static_cast<float>(h & 0xffffff) / 16777216.0f;
}
void RootOffset(const Walker& k, float* r) {
    const uint32_t c = static_cast<uint32_t>(k.clock) * 3u + static_cast<uint32_t>(k.index) * 7919u;
    r[0] = 0.015f + 0.02f * HashUnit(c);
    r[1] = 0.01f * HashUnit(c + 1u) - 0.005f;
    r[2] = 0.003f * HashUnit(c + 2u);
}
// GetTerrainPos's height stand-in (the last iteration's z), a pure function of (x, y).
float Height(float x, float y) { return 0.25f * std::sin(x * 0.3f) + 0.125f * std::cos(y * 0.2f); }

// MoveTo::UpdateFacing (0x1400c8d20) stand-in: both worlds call this one function (the batched
// tick calls the real body). Pure in the walker's position and the waypoint cell.
void UpdateFacing(const World& wd, Walker& k) {
    const int target = k.path[k.pathIndex];
    const float center = static_cast<float>(wd.size >> 1) - 0.5f;
    const float tx = static_cast<float>(target % wd.size) - center;
    const float ty = static_cast<float>(target / wd.size) - center;
    float dx = tx - k.pos[0], dy = ty - k.pos[1];
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len > 0.0f) { dx = dx / len; dy = dy / len; }
    k.c28 = false;
    k.dir[0] = dx; k.dir[1] = dy; k.dir[2] = 0.0f;
    k.c3C = std::atan2(dy, dx) * 57.29578f;
}

// ---- the reference: literal copies of the retail bodies --------------------------------
int CellRef(int mapSize, float x, float y) {   // moveto.cpp WorldToCell
    float halfMap = (float)(mapSize >> 1);
    int cx = (int)(x + halfMap), cy = (int)(y + halfMap);
    if (cx < 0) cx = 0;
    if (cx >= mapSize) cx = mapSize - 1;
    if (cy < 0) cy = 0;
    if (cy >= mapSize) cy = mapSize - 1;
    return mapSize * cy + cx;
}

// MoveTo::Execute (0x1400c8570), case 2, and the executor's pop on a 0.
void ExecuteRef(const World& wd, Walker& k) {
    if (!k.active) return;                  // empty stack: the executor does not run
    if (k.pathIndex == -1) return;          // case 1 (the poll; the script never answers it)
    const float curX = k.pos[0], curY = k.pos[1], curZ = k.pos[2];
    int destCell = CellRef(wd.size, k.dest[0], k.dest[1]);
    int curCell = CellRef(wd.size, curX, curY);
    if (curCell == destCell) {
        float dx2 = curX - k.dest[0], dy2 = curY - k.dest[1];
        float dist = std::sqrt(dx2 * dx2 + dy2 * dy2);
        if (dist < 0.05f) { k.active = false; return; }   // 0: popped
        float rx = k.dest[0] - curX, ry = k.dest[1] - curY;
        (void)curZ;
        const float len = std::sqrt(rx * rx + ry * ry);
        rx = rx / len; ry = ry / len;
        k.c28 = false;
        k.dir[0] = rx; k.dir[1] = ry; k.dir[2] = 0.0f;
        k.c3C = std::atan2(ry, rx) * 57.29578f;
        return;
    }
    int32_t nodeCount = k.pathCount;
    if (nodeCount < 2) return;
    if (k.pathIndex >= nodeCount) { k.pathIndex = -1; return; }   // the stop animation
    uint8_t terrainType = wd.terrain[(size_t)curCell];
    float speed;
    if (k.follow) speed = k.desc->f30;
    else if (((terrainType - 1) & 0xFFFFFFF7) == 0) speed = k.desc->f34;
    else if (terrainType == 0) speed = k.desc->f38;
    else speed = kRdata_14017214c;
    k.c38 = speed * k.mult;
    if (curCell != k.path[k.pathIndex]) return;
    k.pathIndex = k.pathIndex + 1;
    UpdateFacing(wd, k);
}

// The move tick's tail (citizenai_executor.cpp:314-573), the StillPath block left out (it only
// skips stores of identical bits; STILLPATH proves it), the pushes left out (presentation).
void TailRef(const World& wd, Walker& k, bool rootOnly) {
    float fx = k.pos[0], fy = k.pos[1], fz = k.pos[2];
    float pos[3] = { fx, fy, fz };
    float fVar14 = k.c3C;
    float fVar10 = k.yaw - k.desc->f24;
    float fVar13 = fVar14 - fVar10;
    float c172220 = kRdata_140172220;
    float c172238 = FromBits(kRdata_140172238);
    float c1720e4 = FromBits(kRdata_1401720e4);
    float c1722a0 = FromBits(kRdata_1401722a0);
    float uvScale = kRdata_14017214c;
    if (c172220 <= (fVar13 < 0.0f ? -fVar13 : fVar13)) {
        if (fVar13 <= 0.0f) fVar13 = fVar13 + c172238;
        else fVar13 = -(c172238 - fVar13);
    }
    if (c1720e4 < (fVar13 < 0.0f ? -fVar13 : fVar13)) {
        float t = k.desc->slow28;
        fVar14 = (fVar13 + fVar10) * (uvScale - t) + t * fVar10;
        if (fVar14 <= c172220) {
            if (fVar14 < c1722a0) fVar14 = fVar14 + c172238;
        } else {
            fVar14 = fVar14 - c172238;
        }
    }
    float r[4] = { 0, 0, 0, 0 };
    RootOffset(k, r);
    float dotOut = r[1] * r[1] + r[0] * r[0] + r[2] * r[2];
    float speed = std::sqrt(dotOut);
    speed = speed * k.c38;
    if (0.0f < speed) {
        float ax = speed * k.dir[0], ay = speed * k.dir[1], az = speed * k.dir[2];
        pos[0] = fx + ax; pos[1] = fy + ay; pos[2] = fz + az;
        fx = pos[0]; fy = pos[1];
    }
    bool blocked = false;
    if (!rootOnly) {
        int dim = wd.size;
        int ix = (int)(fx + (float)(dim >> 1)), iy = (int)(fy + (float)(dim >> 1));
        if (ix < 0) ix = 0;
        if (dim <= ix) ix = dim - 1;
        if (iy < 0) iy = 0;
        if (dim <= iy) iy = dim - 1;
        if (wd.terrain[(size_t)(dim * iy + ix)] == 0x0b) { pos[2] = 0.0f; pos[0] = fx; pos[1] = fy; blocked = true; }
    }
    if (!blocked) {
        if (!k.desc->b3c) {
            if (!rootOnly) {
                float h = Height(pos[0], pos[1]);
                pos[2] = (k.desc->f44 < h) ? h : k.desc->f44;
            }
        } else {
            pos[2] = k.desc->f40;
        }
    }
    fVar14 = fVar14 + k.desc->f24;
    float ang = fVar14 * FromBits(kRdata_1401720cc) * kRdata_140172114;
    float s = 0, c = 0;
    s = MsvcLibm::Sinf(ang);
    c = MsvcLibm::Cosf(ang);
    k.rot[0] = 0.0f; k.rot[1] = 0.0f; k.rot[2] = -s; k.rot[3] = c;
    k.pos[0] = pos[0]; k.pos[1] = pos[1]; k.pos[2] = pos[2];
    k.yaw = fVar14;
    k.gridCell = CellRef(wd.size, k.pos[0], k.pos[1]);   // CharacterGrid::Place
}

void FullTick(const World& wd, Walker& k, bool rootOnly) {
    ExecuteRef(wd, k);
    TailRef(wd, k, rootOnly);
}

// ---- the batched tick: walk_batch.cpp's Eligible + Step, over the rules ------------------
struct BatchStats { long long batched = 0, waypoints = 0, inDestBatched = 0; };

// placeEveryTick: ext.walk_batch's Step<true> (the faithful tail's Place after the write);
// false: ext.walk_batch_grid's Step<false>.
bool BatchTick(const World& wd, Walker& k, BatchStats& st, bool placeEveryTick) {
    if (!k.active) return false;                               // no stack: the real tick
    if (k.pathIndex < 0) return false;
    const int destCell = Ext::WalkBatch::WorldToCell(wd.size, k.dest[0], k.dest[1]);
    const int curCell = Ext::WalkBatch::WorldToCell(wd.size, k.pos[0], k.pos[1]);
    if (Ext::WalkBatch::DecideWalk(k.pathIndex, k.pathCount, curCell, destCell, k.follow) != Ext::WalkBatch::kBatch)
        return false;
    if (curCell == destCell) ++st.inDestBatched;              // never: DecideWalk
    ++st.batched;
    // Execute's walking branch
    const float cellSpeed = Ext::WalkBatch::TerrainSpeed(wd.terrain[(size_t)curCell], k.desc->f34, k.desc->f38);
    k.c38 = cellSpeed * k.mult;
    if (curCell == k.path[k.pathIndex]) { k.pathIndex = k.pathIndex + 1; UpdateFacing(wd, k); ++st.waypoints; }
    // the tail, rootOnly set, no trig / rotation / push (and no Place without placeEveryTick)
    float fVar14 = Ext::WalkBatch::Heading(k.c3C, k.yaw, k.desc->f24, k.desc->slow28);
    float r[4] = { 0, 0, 0, 0 };
    RootOffset(k, r);
    const float speed = Ext::WalkBatch::RootSpeed(r[0], r[1], r[2], k.c38);
    const Ext::WalkBatch::Pos p = Ext::WalkBatch::StepPos(k.pos[0], k.pos[1], k.pos[2], speed, k.dir[0], k.dir[1],
                                                k.dir[2], k.desc->b3c, k.desc->f40);
    fVar14 = fVar14 + k.desc->f24;
    k.pos[0] = p.x; k.pos[1] = p.y; k.pos[2] = p.z;
    k.yaw = fVar14;
    if (placeEveryTick) k.gridCell = CellRef(wd.size, k.pos[0], k.pos[1]);   // CharacterGrid::Place
    return true;
}

// ---- the script --------------------------------------------------------------------------
void CellCenter(const World& wd, int cell, float* x, float* y) {
    const float center = static_cast<float>(wd.size >> 1) - 0.5f;
    *x = static_cast<float>(cell % wd.size) - center;
    *y = static_cast<float>(cell / wd.size) - center;
}

// A fresh MoveTo from the walker's current cell to `destCell`: the cells of an L-shaped route
// (x first, then y), traversal from index 1 (TakePath), facing set (UpdateFacing).
void NewPath(const World& wd, Walker& k, int destCell) {
    const int from = CellRef(wd.size, k.pos[0], k.pos[1]);
    int x = from % wd.size, y = from / wd.size;
    const int tx = destCell % wd.size, ty = destCell / wd.size;
    k.pathCount = 0;
    k.path[k.pathCount++] = from;
    while (x != tx && k.pathCount < 255) { x += (tx > x) ? 1 : -1; k.path[k.pathCount++] = y * wd.size + x; }
    while (y != ty && k.pathCount < 255) { y += (ty > y) ? 1 : -1; k.path[k.pathCount++] = y * wd.size + x; }
    CellCenter(wd, k.path[k.pathCount - 1], &k.dest[0], &k.dest[1]);
    k.dest[2] = 0.0f;
    k.active = true;
    k.follow = false;
    if (k.pathCount < 2) { k.pathIndex = -1; return; }   // a short "path": the poll stand-in
    k.pathIndex = 1;
    UpdateFacing(wd, k);
}

World MakeWorld(uint64_t seed, const std::vector<Desc>& descs) {
    World wd;
    Lcg g{ seed };
    wd.terrain.resize((size_t)(wd.size * wd.size));
    const uint8_t kinds[] = { 0, 0, 0, 1, 9, 2, 0x0b, 8 };
    for (uint8_t& t : wd.terrain) t = kinds[g.Next() % 8];
    for (int i = 0; i < 24; ++i) {
        Walker k;
        k.index = i;
        k.desc = &descs[(size_t)(i % (int)descs.size())];
        k.mult = 0.9f + 0.2f * g.Unit();
        const int start = (int)(g.Next() % (uint32_t)(wd.size * wd.size));
        CellCenter(wd, start, &k.pos[0], &k.pos[1]);
        k.pos[0] += 0.3f * g.Unit() - 0.15f;
        k.pos[1] += 0.3f * g.Unit() - 0.15f;
        k.pos[2] = 0.0f;
        k.yaw = 360.0f * g.Unit() - 180.0f;
        k.clock = (int)(g.Next() % 97);
        NewPath(wd, k, (int)(g.Next() % (uint32_t)(wd.size * wd.size)));
        wd.w.push_back(k);
    }
    return wd;
}

// What must match after every iteration (written through by the batch).
bool SameLive(const Walker& a, const Walker& b) {
    return std::memcmp(a.pos, b.pos, sizeof a.pos) == 0 && Bits(a.yaw) == Bits(b.yaw) &&
           Bits(a.c38) == Bits(b.c38) && Bits(a.c3C) == Bits(b.c3C) &&
           std::memcmp(a.dir, b.dir, sizeof a.dir) == 0 && a.c28 == b.c28 &&
           a.pathIndex == b.pathIndex && a.active == b.active && a.clock == b.clock;
}
// And after the last iteration: the rotation (one trig of the caught-up yaw) and the grid.
bool SameFrameEnd(const Walker& a, const Walker& b) {
    return SameLive(a, b) && std::memcmp(a.rot, b.rot, sizeof a.rot) == 0 && a.gridCell == b.gridCell;
}

struct RunResult {
    long long ticks = 0, liveMismatch = 0, endMismatch = 0, gridLag = 0, rotLag = 0;
    // The grid lag in ticks: the longest run of batched ticks since a walker's last real one
    // (bounded by speed - 1), and the ticks whose batched grid cell is NOT the reference's
    // cell of that many ticks before (expected 0: the grid holds retail's cell, late).
    long long maxLagTicks = 0, lagWrongCell = 0;
    BatchStats st;
};

// Both worlds, the same script; `speed` sim iterations per frame.
RunResult RunTwoWorlds(uint64_t seed, int speed, int frames, bool placeEveryTick) {
    const std::vector<Desc> descs = {
        { 0.0f, 0.85f, 0.6f, 1.4f, 1.0f, false, 0.0f, -1.0f },
        { 90.0f, 0.7f, 0.5f, 1.25f, 0.8f, false, 0.0f, 0.05f },
        { -45.0f, 0.9f, 0.7f, 1.5f, 1.1f, true, 0.375f, 0.0f },
    };
    World ref = MakeWorld(seed, descs);
    World bat = MakeWorld(seed, descs);
    Lcg script{ seed ^ 0x9e3779b97f4a7c15ULL };
    RunResult rr;
    // Per walker: the reference's grid cell after each of the last kHist iterations, and the
    // batched ticks since the batched world's last real tick.
    constexpr int kHist = 64;
    std::vector<std::vector<int>> refHist(ref.w.size(), std::vector<int>(kHist, -1));
    std::vector<int> since(ref.w.size(), 0);
    long long iter = 0;
    for (int f = 0; f < frames; ++f) {
        for (int it = 0; it < speed; ++it, ++iter) {
            const bool rootOnly = it < speed - 1;
            for (size_t i = 0; i < ref.w.size(); ++i) {
                Walker& a = ref.w[i];
                Walker& b = bat.w[i];
                FullTick(ref, a, rootOnly);
                const bool batched = rootOnly && BatchTick(bat, b, rr.st, placeEveryTick);
                if (!batched) FullTick(bat, b, rootOnly);
                ++a.clock; ++b.clock;   // the model tick
                ++rr.ticks;
                if (!SameLive(a, b)) ++rr.liveMismatch;
                if (rootOnly && a.gridCell != b.gridCell) ++rr.gridLag;
                refHist[i][(size_t)(iter % kHist)] = a.gridCell;
                since[i] = (batched && !placeEveryTick) ? since[i] + 1 : 0;
                if (since[i] > rr.maxLagTicks) rr.maxLagTicks = since[i];
                // (From the walker's first real tick on: before it the grid has never placed it.)
                if (since[i] < kHist && iter - since[i] >= 0 &&
                    b.gridCell != refHist[i][(size_t)((iter - since[i]) % kHist)])
                    ++rr.lagWrongCell;
                if (rootOnly && std::memcmp(a.rot, b.rot, sizeof a.rot) != 0) ++rr.rotLag;
            }
            // Between iterations (bucket phase, other entities): the same events in both worlds.
            for (size_t i = 0; i < ref.w.size(); ++i) {
                Walker& a = ref.w[i];
                Walker& b = bat.w[i];
                const uint32_t ev = script.Next() % 1000;
                if (ev < 6) {                       // MoveTo::Enter sets / clears the follow flag
                    a.follow = b.follow = !a.follow;
                } else if (ev < 9) {                // Clear + a new path from where it stands
                    const int to = (int)(script.Next() % (uint32_t)(ref.size * ref.size));
                    NewPath(ref, a, to);
                    NewPath(bat, b, to);
                } else if (ev < 10) {               // a teleport (an external transform write)
                    const float dx = script.Unit() - 0.5f, dy = script.Unit() - 0.5f;
                    a.pos[0] += dx; a.pos[1] += dy; b.pos[0] += dx; b.pos[1] += dy;
                } else if (!a.active && ev < 60) {  // an idle walker gets new work
                    const int to = (int)(script.Next() % (uint32_t)(ref.size * ref.size));
                    NewPath(ref, a, to);
                    NewPath(bat, b, to);
                }
            }
        }
        for (size_t i = 0; i < ref.w.size(); ++i)
            if (!SameFrameEnd(ref.w[i], bat.w[i])) ++rr.endMismatch;
    }
    return rr;
}

}  // namespace

UNIT_TEST(ext_walk_batch_registry_row) {
    const Ext::Entry& e = E("walk_batch");
    CHECK(e.tier == Ext::Tier::Extension);
    CHECK(e.cls == Ext::Class::A);          // the grid never lags (WALKBATCH.md section 13)
    CHECK(e.defaultOn);                     // class A ships ON
    CHECK(e.formatVersion == 0);            // class A: no save record
    CHECK(e.displayName != nullptr && e.description != nullptr);
    CHECK(std::strstr(e.description, "diverges") == nullptr);
    CHECK(Ext::SettingsKey(e) == "ext.walk_batch");

    Ext::State s{};
    CHECK(!s.ext.walk_batch);
    Ext::State on{};
    e.slot(on) = true;
    Ext::Install(on, Ext::Profile::PerEntry, false);
    CHECK(Ext::Ext().walk_batch);
    int owner = 0;
    Ext::LockForGame(&owner);
    CHECK(!Ext::EntryLocked(e));             // a game's lock does not hold it
    CHECK(Ext::SetRequested(e, false));      // live: takes effect at once
    CHECK(!Ext::Ext().walk_batch);
    CHECK(Ext::SetRequested(e, true));
    CHECK(Ext::Ext().walk_batch);
    // Never a pseudo-mod in the save, and a manifest naming it is refused (not B / C).
    std::vector<Ext::PseudoMod> mods((size_t)Ext::EntryCount());
    const int n = Ext::PseudoModsForSave(mods.data(), (int)mods.size());
    for (int k = 0; k < n && k < (int)mods.size(); ++k)
        CHECK(mods[(size_t)k].path != "ext:walk_batch");
    CHECK(Ext::PseudoModLoadStatus("ext:walk_batch", 1) == 1);
    Ext::UnlockForGame(&owner);
    std::vector<std::string> ids = { "walk_batch" };
    CHECK(!Ext::LockForGameWithSet(&owner, ids));
    Ext::Install(Ext::State{}, Ext::Profile::PerEntry, false);
}

UNIT_TEST(ext_walk_batch_rules_match_retail) {
    // The cell: every quadrant, the clamps, the half-map offset.
    const int sizes[] = { 2, 31, 32, 256 };
    Lcg g{ 7 };
    for (int sz : sizes)
        for (int i = 0; i < 2000; ++i) {
            const float x = (g.Unit() - 0.5f) * (float)sz * 1.2f, y = (g.Unit() - 0.5f) * (float)sz * 1.2f;
            CHECK_EQ(Ext::WalkBatch::WorldToCell(sz, x, y), CellRef(sz, x, y));
        }
    // The terrain speed (follow clear): road bytes 1 and 9, normal 0, the rest 1.0.
    for (int t = 0; t < 256; ++t) {
        float want;
        const uint8_t tt = (uint8_t)t;
        if (((tt - 1) & 0xFFFFFFF7) == 0) want = 1.5f;
        else if (tt == 0) want = 0.75f;
        else want = kRdata_14017214c;
        CHECK_EQ(Bits(Ext::WalkBatch::TerrainSpeed(tt, 1.5f, 0.75f)), Bits(want));
    }
    CHECK_EQ(Bits(Ext::WalkBatch::TerrainSpeed(1, 2.0f, 3.0f)), Bits(2.0f));
    CHECK_EQ(Bits(Ext::WalkBatch::TerrainSpeed(9, 2.0f, 3.0f)), Bits(2.0f));
    CHECK_EQ(Bits(Ext::WalkBatch::TerrainSpeed(0, 2.0f, 3.0f)), Bits(3.0f));
    CHECK_EQ(Bits(Ext::WalkBatch::TerrainSpeed(2, 2.0f, 3.0f)), Bits(1.0f));
    CHECK_EQ(Bits(Ext::WalkBatch::TerrainSpeed(8, 2.0f, 3.0f)), Bits(1.0f));
    // The step: no step at speed <= 0 or NaN; b3c pins z; -0.0 z becomes +0 only on a step.
    Ext::WalkBatch::Pos p = Ext::WalkBatch::StepPos(1.0f, 2.0f, -0.0f, 0.0f, 1.0f, 0.0f, 0.0f, false, 9.0f);
    CHECK(Bits(p.x) == Bits(1.0f) && Bits(p.z) == Bits(-0.0f));
    p = Ext::WalkBatch::StepPos(1.0f, 2.0f, -0.0f, 0.5f, 1.0f, 0.0f, 0.0f, false, 9.0f);
    CHECK(Bits(p.x) == Bits(1.5f) && Bits(p.z) == Bits(0.0f));
    p = Ext::WalkBatch::StepPos(1.0f, 2.0f, 3.0f, std::nanf(""), 1.0f, 0.0f, 0.0f, true, 9.0f);
    CHECK(Bits(p.x) == Bits(1.0f) && Bits(p.z) == Bits(9.0f));
}

UNIT_TEST(ext_walk_batch_decline_rules) {
    using namespace Ext::WalkBatch;
    CHECK(DecideWalk(3, 10, 5, 9, false) == kBatch);
    CHECK(DecideWalk(-1, 10, 5, 9, false) == kPathWait);
    CHECK(DecideWalk(3, 10, 9, 9, false) == kInDestCell);
    CHECK(DecideWalk(1, 1, 5, 9, false) == kShortPath);
    CHECK(DecideWalk(10, 10, 5, 9, false) == kPathDone);
    CHECK(DecideWalk(3, 10, 5, 9, true) == kFollow);
    // Execute's order: the poll first, then the destination cell, then the path tests.
    CHECK(DecideWalk(-1, 0, 9, 9, true) == kPathWait);
    CHECK(DecideWalk(12, 1, 9, 9, true) == kInDestCell);
    CHECK(DecideWalk(12, 1, 5, 9, true) == kShortPath);
    CHECK(DecideWalk(12, 10, 5, 9, true) == kPathDone);
}

namespace {

// placeEveryTick: ext.walk_batch (true) or ext.walk_batch_grid (false).
void TwoWorlds(bool placeEveryTick) {
    const int speeds[] = { 1, 3, 5, 10, 20 };
    long long batchedAll = 0, waypointsAll = 0, lagAll = 0;
    std::printf("    %s\n", placeEveryTick ? "walk_batch (Place every tick)" : "walk_batch_grid (no Place)");
    for (int sp : speeds) {
        const RunResult r = RunTwoWorlds(0x5eed0000ULL + (uint64_t)sp, sp, 6000 / sp, placeEveryTick);
        CHECK_EQ(r.liveMismatch, 0LL);
        CHECK_EQ(r.endMismatch, 0LL);
        CHECK_EQ(r.st.inDestBatched, 0LL);
        CHECK_EQ(r.lagWrongCell, 0LL);              // a lagging cell is retail's cell, k ticks late
        CHECK(r.maxLagTicks <= (long long)(sp - 1)); // and k <= N-1
        if (placeEveryTick) CHECK_EQ(r.maxLagTicks, 0LL);
        if (sp == 1) CHECK_EQ(r.st.batched, 0LL);   // speed 1: every iteration is the last
        batchedAll += r.st.batched; waypointsAll += r.st.waypoints; lagAll += r.gridLag;
        std::printf("    speed %2d: %lld ticks, %lld batched (%lld at a waypoint), grid lag %lld (max %lld ticks), rotation lag %lld\n",
                    sp, r.ticks, r.st.batched, r.st.waypoints, r.gridLag, r.maxLagTicks, r.rotLag);
    }
    CHECK(batchedAll > 10000);   // the script walks enough to mean something
    CHECK(waypointsAll > 100);   // waypoints are crossed inside batched spans
    if (placeEveryTick) CHECK_EQ(lagAll, 0LL);   // the grid cell matches on EVERY tick
    else CHECK(lagAll > 0);      // the class B row: the grid lags on intermediate iterations
}

}  // namespace

// ext.walk_batch_grid: position / path / speed / facing bit-identical every tick, the grid
// lagging (nonzero, bounded by N-1, always retail's cell of an earlier tick).
UNIT_TEST(ext_walk_batch_two_worlds_bit_identical) {
    TwoWorlds(false);
}

// ext.walk_batch alone: grid lag exactly 0.
UNIT_TEST(ext_walk_batch_two_worlds_place_every_tick) {
    TwoWorlds(true);
}
