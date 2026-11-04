// src/game/Work/work_rules.h
//
// The small decisions several Work::Execute bodies share, pulled out of the bodies so the
// unit tests can pin them without a live world (tests/unit/work_rules_test.cpp). Each one
// is written from the retail instructions quoted beside it.
#pragma once

#include <cstdint>

#include "system/Value.h"   // System::Random

namespace WorkRules {

// The worker cap of HuntWork / PlantArea / PlantOrchard / TendArea::Execute. Retail
// compares the count with WorkComponent+0x30 and takes the work when it is not greater:
//   1400f54ed: CMP R14D,dword ptr [RBX + 0x30]   (TendArea; Hunt 0x1400ef196,
//   1400f54f1: JLE 0x1400f54fd                    PlantArea 0x1400f407e, PlantOrchard 0x1400f4b70)
//   1400f54f3: MOV EAX,0x1
// so a count equal to the max still takes the work.
inline bool OverWorkerCap(int count, int maxWorkers) {
    return count > maxWorkers;
}

// The retry test of a random sample around a work cell (FishingWork 0x1400ea39a,
// ForesterWork 0x1400eb2be, HerbalistWork 0x1400ee755). Retail's X term is dead:
//   TEST x,x ; JNS skip ; CMP x,size ; JGE retry   -> retry only if x < 0 && x >= size
// so only Y is tested and an X off the map reads the wrapped-row cell size*y + x.
inline bool SampleOffMap(int x, int y, int size) {
    (void)x;
    return y < 0 || y >= size;
}

// NOT RETAIL: the wrapped-row cell size*y + x of a sample whose X is off the map lies
// before cell 0 on the first row and past the last cell on the last row. Retail reads it
// anyway, an out-of-bounds read (undefined behaviour); the callers reject that one sample
// as a failed attempt instead. Every other wrapped cell is read as retail reads it.
inline bool SampleCellOutsideMap(int x, int y, int size) {
    int64_t cell = (int64_t)size * y + x;
    return cell < 0 || cell >= (int64_t)size * size;
}

// PlantArea::Execute: the second slot index a planting carries (packedCells[1] and the
// high word of the packed pair). Retail packs the slot COUNT when there is no pair:
//   1400f41e0: XOR SIL,SIL
//   1400f41e3: MOV EBX,R8D        ; R8D = [this+0x60], the slot count
// and OnCancel 0x1400f3ea0 / OnComplete 0x1400f3f60 act on it only when it is < count.
inline int PlantAreaPairedSlot(int slot, int slotCount, bool paired) {
    if (paired) {
        return slot + 1;
    }
    return slotCount;
}

// PastureWork::Execute's grazing fallback: a random cell of the pasture's MapComponent
// rect (x0, y0, w, h). Retail draws the bounded Random::Rand(uint), h first:
//   1400f299d: CALL qword ptr [0x1401512e8]   ; Rand(h)
//   1400f29aa: CALL qword ptr [0x1401512e8]   ; Rand(w)
//   1400f29c0: ADD EBX,ECX                    ; (y0 + r1) * size + r2 + x0
inline int PastureGrazeCell(System::Random& rng, int x0, int y0, int w, int h, int mapSize) {
    uint32_t dy = rng.Rand((uint32_t)h);
    uint32_t dx = rng.Rand((uint32_t)w);
    return ((int)y0 + dy) * mapSize + dx + x0;
}

}  // namespace WorkRules
