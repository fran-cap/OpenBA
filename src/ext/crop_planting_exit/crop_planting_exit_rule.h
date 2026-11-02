// ============================================================================
// src/ext/crop_planting_exit/crop_planting_exit_rule.h -- fix.crop_planting_exit: the pure rules
// ============================================================================
// NOT RETAIL. docs/retail_bugs.md §11. Standard C++ only, so
// tests/unit/ext_crop_planting_exit_test.cpp links it without the world. The state-9 half of
// the row is a gated request in CropFieldComponent::State9PerFrame and needs no rule here.
//
// A crop field plants in state 9 (CropFieldComponent::State9PerFrame, retail 0x14011a1e0) and
// leaves it two ways, both of which retail can miss:
//
// 1. Every slot done (no crop left growing): the handler requests 21 (the re-clear), but the
//    populator (0x14011b150) gives state 9 the transitions {3, 10} only, so the request is
//    dropped and the field stays in state 9. The row lets the request through.
//
// 2. A slot no worker can reach: PlantArea::Execute (0x1400f4010) offers every slot still to
//    plant, skips one whose access cell the citizen cannot reach (Map::FindCell 0x14009ccf0 /
//    Map::ValidateCell 0x1400a00f0), and returns "nothing assigned" before its end-of-planting
//    branch (CropFieldComponent::MarkPlantSlots 0x14011b390, which turns the empty slots done).
//    So that slot stays empty, growing + done never reaches the slot count, and the field
//    never leaves state 9. With the row on, when every slot still to plant is out of the
//    citizen's reach, no plant is in flight and some slot of the field has been planted, the
//    end-of-planting branch runs: the unreachable slots count as done.
// ============================================================================
#pragma once

namespace Ext {
namespace CropPlantingExit {

// PlantArea::Execute found slots still to plant and could assign none of them to this
// citizen (each failed the reach test). True when the end-of-planting branch should run:
// the row is on, no plant is in flight and some slot already holds a crop or is done (a
// worker reached the field, so the unreached slots are not merely this citizen's).
inline bool EndPlantingOnUnreachable(bool fixOn, int inFlight, int settledSlots) {
    return fixOn && inFlight == 0 && settledSlots > 0;
}

}  // namespace CropPlantingExit
}  // namespace Ext
