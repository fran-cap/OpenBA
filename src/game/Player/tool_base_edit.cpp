// src/game/Player/tool_base_edit.cpp
// Player registration wave (batch "edit") — shared exe sub-helpers resolved from
// src/game/Player/edittool.cpp and fencedtool.cpp via ResolveAddr(Module::Exe, 0xADDR).
//
// These resolve to 0 (the guarded call site is silently skipped) unless registered.
// This file provides faithful 1:1 reimplementations; registration lives in
// standalone/reg_player_edit.cpp (RegisterPlayerEditBatch).
//
// Each body is a literal port of the decompiled exe helper — no "improvements".

#include <new>
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Game/exe_constants.h"   // kRdata_* (B3R Leftovers: the .rdata constants)
#include "Game/exe_bodies.h"
#include <cstdint>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "Player/exe_bodies.h"
#include "Player/FencedTool.h"
#include "Player/RotationSetting.h"
#include "Component/CreateFencedDescription.h"
#include "Game/Map.h"
#include "Entity/ComponentEntity.h"   // B4 S4 RT2: InternalPtr<ComponentEntity>::Get() needs the class complete

// ============================================================
// FUN_1400a0280 — EntityAtCell: get the entity occupying a world cell.
// ============================================================
// Closure: only fn_InternalBasePtr_Ctor (Runtime export). No exe sub-callees.
//
// Reads the Map's cell [cellIndex] (+0x80, stride 0x30). The blocker (+0x00) is the
// primary occupant, the occupant (+0x08) the secondary; the first non-null is stored
// into the out InternalBasePtr (written raw, the slot zeroed first) and returned. If
// both are null, the empty IBP (the ctor's null store).
ComponentEntity** Map::EntityAtCell(ComponentEntity** retIBP, int cellIndex) const {
    const MapCell* cell = &cells.GetData()[cellIndex];
    if (cell->blocker != nullptr) {
        *retIBP = nullptr;
        *retIBP = cell->blocker.Get();
        return retIBP;
    }
    if (cell->occupant != nullptr) {
        *retIBP = nullptr;
        *retIBP = cell->occupant.Get();
        return retIBP;
    }
    *retIBP = nullptr;   // InternalBasePtr(nullptr) (0x140152a90)
    return retIBP;
}

// ============================================================
// FUN_140101730 — CharacterComponent_impl_1730: average terrain height over a
// rectangular cell range (helper of FencedTool anchor calc).
// ============================================================
// Closure: exe sub-callee 0x14009f3e0 (terrain height query — ALREADY registered)
// + Math::Vector3::operator+= (inlined component-wise add here) + g_fUVScale +
// DAT_140172114 (= 0.5f). No unregistered sub-callees.
//
// Iterates every cell (col in [p2,p4], row in [p3,p5]) of the world grid (width at
// *(g_pGameWorldSingleton+0xf8)), queries terrain height per cell via f3e0, scales
// each sample by uvScale = g_fUVScale / cellCount, accumulates into a Vector3, and
// returns the accumulated .x (the mean height). Faithful to the disasm: the
// accumulator's other lanes are computed but only .x (RSP+0x38 / accum[0]) is
// returned; if the row range is empty the result is 0.
//
// Caller signature (via 0x1400dbf90): float fn(void* gameWorld, int p2,p3,p4,p5)
// (0x14009f3e0 = Reimpl_MapGetTerrainPos, Game/exe_bodies.h, called by name.)

float PlayerEdit_AvgTerrainHeight_101730(void* /*param_1*/, int p2, int p3, int p4, int p5) {
    // The original ignores its param_1 and reads g_pGameWorldSingleton directly.
    Map* mapGrid = GameUpdateLoop::_singleton->m_game;

    const float halfCell = 0.5f;                 // DAT_140172114 (XMM7)
    const float uvScaleConst = kRdata_14017214c;  // DAT_14017214c (XMM6)

    float accum[3] = {0.0f, 0.0f, 0.0f};

    // uvScale = uvScaleConst / cellCount; cellCount = (p4-p2+1)*(p5-p3+1).
    // Computed up front, exactly as the asm (DIVSS XMM6, cellCount) — before the guard.
    int cellCount = ((p4 - p2) + 1) * ((p5 - p3) + 1);
    float uvScale = uvScaleConst / static_cast<float>(cellCount);

    // Outer guard: empty row range -> return 0 (asm CMP R8D(param_3),R15D(param_5);
    // JG 0x14010189d -> MOVAPS XMM0,XMM1(=0); RET).
    if (p3 > p5) {
        return 0.0f;
    }

    // Verified from disasm 0x140101730: OUTER = EBX = rows (param_3..param_5),
    // INNER = EDI = cols (param_2..param_4). idx = width*row + col
    // (IMUL EAX,EBX[row]; ADD EAX,EDI[col]). So x=column lane, y=row lane.
    for (int row = p3; row <= p5; ++row) {       // EBX loop (param_3..param_5)
        for (int col = p2; col <= p4; ++col) {   // EDI loop (param_2..param_4)
            int width = mapGrid->size;            // R8D = mapGrid[0]
            int idx = width * row + col;
            float halfW = static_cast<float>(width >> 1) - halfCell;
            float inPos[2];
            inPos[0] = static_cast<float>(idx % width) - halfW;  // x = column
            inPos[1] = static_cast<float>(idx / width) - halfW;  // y = row

            float outPos[3] = {0.0f, 0.0f, 0.0f};
            mapGrid->GetTerrainPos(outPos, inPos, '\0', nullptr);

            // scaled = outPos * uvScale ; accum += scaled  (Vector3::operator+= inlined)
            accum[0] += outPos[0] * uvScale;
            accum[1] += outPos[1] * uvScale;
            accum[2] += outPos[2] * uvScale;
        }
    }

    // Return the accumulated HEIGHT lane: the asm returns [RSP+0x38] = accum.z
    // (the Vector3 accumulator is at [RSP+0x30]; +8 = z), NOT accum.x.
    return accum[2];
}

// ============================================================
// FUN_1400dbf90 — FencedTool anchor/rotation rectangle calc.
// ============================================================
// Closure: exe sub-callees 0x140101730 (registered in THIS batch) + 0x14009f3e0
// (registered earlier) + InternalBasePtr_CheckPtr (Runtime export) + g_fUVScale +
// DAT_140172114 (= 0.5f) + g_pGameWorldSingleton. No unregistered sub-callees.
//
// Converts the tool's two stored cell indices (self+0x2c start, self+0x30 end) to
// (col,row) pairs in the world grid, normalizes them so p2<=p4 and p3<=p5, rejects
// regions smaller than the structure footprint (config at self+0x38, +0x60 width,
// +0x64 depth), then computes the orientation offset (param_7) and snaps the
// rectangle to the structure's anchoring rule depending on the rotation field
// (self+0x50 IBP, +0x10 = rotation 0..3) and the centered flag (config +0x68).
// Finally computes a world-space anchor position (param_6 Vector3) via two f3e0
// terrain queries and writes the averaged height (from 0x140101730) into param_6[2].
//
// Returns 1 if a valid rectangle was produced, 0 if too small.
//
// Callers (fencedtool.cpp, fencedtool_calcpath.cpp): the tool, the four rect corners,
// the world anchor out (Vector3), and the yaw out (param_7: retail stores the float's
// bits as a dword constant).
// (0x140101730 = PlayerEdit_AvgTerrainHeight_101730, above, called by name.)

uint64_t FencedTool::CalcAnchor(uint32_t* param_2, uint32_t* param_3,
                                uint32_t* param_4, uint32_t* param_5,
                                float* param_6, uint32_t* param_7) const {
    const float halfCell = 0.5f;  // DAT_140172114

    int startCell = this->startCell;   // DragTool +0x2c
    int endCell   = this->endCell;     // DragTool +0x30
    Map* mapGrid  = GameUpdateLoop::_singleton->m_game;
    int width     = mapGrid->size;

    *param_2 = static_cast<uint32_t>(startCell % width);
    *param_3 = static_cast<uint32_t>(startCell / width);
    *param_4 = static_cast<uint32_t>(endCell % width);
    *param_5 = static_cast<uint32_t>(endCell / width);

    // Normalize so p2<=p4, p3<=p5.
    if (static_cast<int>(*param_4) < static_cast<int>(*param_2)) {
        uint32_t t = *param_2; *param_2 = *param_4; *param_4 = t;
    }
    if (static_cast<int>(*param_5) < static_cast<int>(*param_3)) {
        uint32_t t = *param_3; *param_3 = *param_5; *param_5 = t;
    }

    // Footprint reject: width must be >= config width (+0x60), depth >= config depth (+0x64).
    const CreateFencedDescription* config = fencedDesc;
    uint32_t spanW = (*param_4 - *param_2) + 1;
    if (static_cast<int>(spanW) < config->_minWidth) {
        return (static_cast<uint64_t>(spanW) >> 8) << 8;  // low byte 0 == false
    }
    uint32_t spanD = (*param_5 - *param_3) + 1;
    if (static_cast<int>(spanD) < config->_minHeight) {
        return (static_cast<uint64_t>(spanD) >> 8) << 8;
    }

    // Orientation offset param_7 from the rotation setting (+0x50, rotation 0..3).
    int rot = rotationSetting->rotation;
    if (rot == 0)      *param_7 = 0;
    else if (rot == 1) *param_7 = 0xc2b40000u;   // -90.0f
    else if (rot == 2) *param_7 = 0x43340000u;   //  180.0f
    else if (rot == 3) *param_7 = 0x42b40000u;   //   90.0f

    // Footprint dimensions, swapped for rotations 1/3.
    int rotA = rotationSetting->rotation;
    int fw, fd;
    if (rotA == 1 || rotA == 3) {
        fw = static_cast<int>(footprintDepth);
        fd = static_cast<int>(footprintWidth);
    } else {
        fw = static_cast<int>(footprintWidth);
        fd = static_cast<int>(footprintDepth);
    }

    // Snap the rectangle: branch on the "centered" flag (config +0x68).
    if (!config->_centerBuilding) {
        int r = rotationSetting->rotation;
        if (r == 0) {
            *param_2 = (*param_4 - fw) + 1;
            *param_3 = (*param_5 - fd) + 1;
        } else if (r == 1) {
            *param_2 = (*param_4 - fw) + 1;
            *param_5 = (*param_3 - 1) + fd;
        } else if (r == 2) {
            *param_4 = (*param_2 - 1) + fw;
            *param_5 = (*param_3 - 1) + fd;
        } else if (r == 3) {
            *param_3 = (*param_5 - fd) + 1;
            *param_4 = (*param_2 - 1) + fw;
        }
    } else {
        int r = rotationSetting->rotation;
        if (r == 0) {
            *param_5 = *param_3;
            uint32_t v = (static_cast<int>(*param_2 + *param_4) >> 1) - (fw >> 1);
            *param_2 = v;
            *param_4 = fw - 1 + v;
        } else if (r == 1) {
            *param_4 = *param_2;
            uint32_t v = (static_cast<int>(*param_3 + *param_5) >> 1) - (fd >> 1);
            *param_3 = v;
            *param_5 = fd - 1 + v;
        } else if (r == 2) {
            *param_3 = *param_5;
            uint32_t v = (static_cast<int>(*param_2 + *param_4) >> 1) - (fw >> 1);
            *param_2 = v;
            *param_4 = fw - 1 + v;
        } else if (r == 3) {
            *param_2 = *param_4;
            uint32_t v = (static_cast<int>(*param_3 + *param_5) >> 1) - (fd >> 1);
            *param_3 = v;
            *param_5 = fd - 1 + v;
        }
    }

    // World-space anchor: average of the two corner cells' terrain positions, with
    // the mean height from the rect-average helper (0x140101730).
    mapGrid = GameUpdateLoop::_singleton->m_game;
    float avgHeight = PlayerEdit_AvgTerrainHeight_101730(GameUpdateLoop::_singleton,
                                       static_cast<int>(*param_2), static_cast<int>(*param_3),
                                       static_cast<int>(*param_4), static_cast<int>(*param_5));

    int w0 = mapGrid->size;
    int idx0 = static_cast<int>(*param_3) * w0 + static_cast<int>(*param_2);
    float half0 = static_cast<float>(w0 >> 1) - halfCell;
    float in0[3] = {static_cast<float>(idx0 % w0) - half0,
                    static_cast<float>(idx0 / w0) - half0, 0.0f};
    float out0[3] = {0.0f, 0.0f, 0.0f};
    mapGrid->GetTerrainPos(out0, in0, '\0', nullptr);

    int w1 = mapGrid->size;
    int idx1 = w1 * static_cast<int>(*param_5) + static_cast<int>(*param_4);
    float half1 = static_cast<float>(w1 >> 1) - halfCell;
    float in1[3] = {static_cast<float>(idx1 % w1) - half1,
                    static_cast<float>(idx1 / w1) - half1, 0.0f};
    float out1[3] = {0.0f, 0.0f, 0.0f};
    mapGrid->GetTerrainPos(out1, in1, '\0', nullptr);

    // param_6 = (out1 + out0) * 0.5 ; then param_6.z is overwritten with avgHeight.
    param_6[0] = (out1[0] + out0[0]) * halfCell;
    param_6[1] = (out1[1] + out0[1]) * halfCell;
    param_6[2] = (out1[2] + out0[2]) * halfCell;
    param_6[0] = param_6[0];
    param_6[1] = param_6[1];
    param_6[2] = avgHeight;

    return 1;
}
