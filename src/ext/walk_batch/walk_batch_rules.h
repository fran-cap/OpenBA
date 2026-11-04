#pragma once
// ============================================================================
// src/ext/walk_batch/walk_batch_rules.h -- NOT RETAIL. The pure arithmetic and the decline
// rule of ext.walk_batch (walk_batch.h), header-only and standard C++ so the host unit tests
// (tests/unit/ext_walk_batch_test.cpp) check the same code the batched tick runs.
// docs/extensions/handins/WALKBATCH.md
//
// Every function here is a literal copy of one piece of a retail body, in retail's operation
// order (x86-64 SSE, no FMA in any build flavour: the same expression gives the same bits):
//   WorldToCell   MoveTo::Execute's position -> clamped cell (0x1400c8570; moveto.cpp WorldToCell)
//   TerrainSpeed  MoveTo::Execute's per-cell speed, the follow flag clear (moveto.cpp:436-452)
//   Heading       the move tick's heading recurrence fVar14 (0x1401094c0; citizenai_executor.cpp:323-352)
//   RootSpeed     |RootAnimationOffset()| x +0x38 with Vector3::Dot's (y, x, z) order (:356-367)
//   StepPos       the step and the intermediate-iteration z (:368-375, :436-456 with rootOnly set)
//   DecideWalk    the decline rule: the walking branch of MoveTo::Execute case 2, and nothing else
// ============================================================================
#include <cstdint>
#include <cstring>

#include "Game/exe_constants.h"   // kRdata_* (the exe .rdata floats the bodies read)
#include "Action/saved_bounds.h"   // ActionBounds::WorldToCellAxis (moveto.cpp's cell, review F-160)

namespace Ext {
namespace WalkBatch {

inline float RdataBits(uint32_t bits) {
    float f;
    std::memcpy(&f, &bits, sizeof f);
    return f;
}

// MoveTo::Execute's WorldToCell (moveto.cpp:115-129): clamp each axis to [0, size-1].
// NOT RETAIL (review F-251, F-252): each axis goes through ActionBounds::WorldToCellAxis, as
// moveto.cpp's does since F-160: a saved destination or position outside int's range, or NaN, is
// not converted (undefined in C++) and gives cell 0 on that axis, CVTTSS2SI's answer after the
// clamp. Every in-range value converts as before.
inline int WorldToCell(int mapSize, float worldX, float worldY) {
    const float halfMap = static_cast<float>(mapSize >> 1);
    const int cellX = ActionBounds::WorldToCellAxis(worldX + halfMap, mapSize);
    const int cellY = ActionBounds::WorldToCellAxis(worldY + halfMap, mapSize);
    return mapSize * cellY + cellX;
}

// MoveTo::Execute's speed for the cell the character stands in, follow flag (+0x474) CLEAR
// (a set flag is declined, DecideWalk): a road / path byte (type & ~8 == 0 after -1, i.e.
// 1 or 9) -> desc +0x34; 0 -> desc +0x38; anything else -> 1.0 (0x14017214c). The caller
// multiplies by MoveTo +0x478 and stores it at CharacterComponent +0x38.
inline float TerrainSpeed(uint8_t terrainType, float descRoad34, float descNormal38) {
    if (((terrainType - 1) & 0xFFFFFFF7) == 0) return descRoad34;
    if (terrainType == 0) return descNormal38;
    return kRdata_14017214c;
}

// The move tick's heading recurrence (citizenai_executor.cpp:323-352): fVar14 from the target
// heading (+0x3C), the live yaw, desc +0x24 and desc +0x28. The tail then writes
// yaw = fVar14 + desc +0x24.
inline float Heading(float target3C, float yaw, float descF24, float descSlow28) {
    float fVar14 = target3C;
    const float fVar10 = yaw - descF24;
    float fVar13 = fVar14 - fVar10;
    const float c180 = kRdata_140172220;
    const float c360 = RdataBits(kRdata_140172238);
    const float c01 = RdataBits(kRdata_1401720e4);
    const float cm180 = RdataBits(kRdata_1401722a0);
    const float uv = kRdata_14017214c;
    const float a13 = fVar13 < 0.0f ? -fVar13 : fVar13;
    if (c180 <= a13) {
        if (fVar13 <= 0.0f) fVar13 = fVar13 + c360;
        else fVar13 = -(c360 - fVar13);
    }
    const float b13 = fVar13 < 0.0f ? -fVar13 : fVar13;
    if (c01 < b13) {
        const float t = descSlow28;
        fVar14 = (fVar13 + fVar10) * (uv - t) + t * fVar10;
        if (fVar14 <= c180) {
            if (fVar14 < cm180) fVar14 = fVar14 + c360;
        } else {
            fVar14 = fVar14 - c360;
        }
    }
    return fVar14;
}

// speed = sqrt(v.Dot(v)) * +0x38, Dot evaluated (y, x, z) as Vector3::Dot / InlV3Dot do.
inline float RootSpeed(float rx, float ry, float rz, float char38) {
    const float dot = ry * ry + rx * rx + rz * rz;
    return __builtin_sqrtf(dot) * char38;
}

struct Pos { float x, y, z; };

// The tail on an intermediate iteration (rootOnly set): step along +0x2C when speed > 0
// (pos = f + speed * dir, componentwise), then z = desc +0x40 when desc +0x3c is set, else the
// z the step left (the terrain and the 0x0b test run on the last iteration only).
inline Pos StepPos(float fx, float fy, float fz, float speed, float dx, float dy, float dz,
                   bool descB3c, float descF40) {
    Pos p{ fx, fy, fz };
    if (0.0f < speed) {
        const float ax = speed * dx;
        const float ay = speed * dy;
        const float az = speed * dz;
        p.x = fx + ax; p.y = fy + ay; p.z = fz + az;
    }
    if (descB3c) p.z = descF40;
    return p;
}

// Why a tick runs in full (the reasons the batched tick checks before DecideWalk are in
// walk_batch.cpp: no stack, no grid index, a top node that is not one entered MoveTo of this
// character alone). kBatch = the tick is MoveTo::Execute's walking branch.
enum Decline : uint8_t {
    kBatch = 0,
    kPathWait,     // pathIndex < 0: case 1 (the pathfinder poll / AbstractImpl)
    kInDestCell,   // the character stands in the destination cell: the arrival branch
    kShortPath,    // pathCount < 2: Execute returns 1 without a speed write
    kPathDone,     // pathIndex >= pathCount: the stop animation, pathIndex = -1
    kFollow,       // the follow flag +0x474 (set by MoveTo::Enter at bucket rate)
    kDeclineCount
};

// The walking branch of MoveTo::Execute case 2, in its own test order. Only this branch
// is batched: every other outcome of Execute (arrival, a pop, the stop, the poll) happens
// in the real tick, at its own tick.
inline Decline DecideWalk(int32_t pathIndex, int32_t pathCount, int curCell, int destCell,
                          bool followFlag) {
    if (pathIndex < 0) return kPathWait;
    if (curCell == destCell) return kInDestCell;
    if (pathCount < 2) return kShortPath;
    if (pathIndex >= pathCount) return kPathDone;
    if (followFlag) return kFollow;
    return kBatch;
}

}  // namespace WalkBatch
}  // namespace Ext
