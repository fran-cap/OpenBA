// ============================================================================
// tests/unit/work_rules_test.cpp -- src/game/Work/work_rules.h against retail
// ============================================================================
// The Work::Execute decisions the review of 2026-11-03 found drifted from retail
// (F-199..F-206), pinned without a live world:
//   * the worker cap is CMP count,[WorkComponent+0x30]; JLE (HuntWork 0x1400ef196,
//     PlantArea 0x1400f407e, PlantOrchard 0x1400f4b70, TendArea 0x1400f54ed):
//     count == max still takes the work;
//   * a random sample retries on Y only: retail's X term (TEST x; JNS; CMP x,size;
//     JGE) needs x < 0 && x >= size and is never true (FishingWork 0x1400ea39a,
//     ForesterWork 0x1400eb2be, HerbalistWork 0x1400ee755), so an X off the map is
//     the wrapped-row cell size*y + x;
//   * that wrapped cell leaves the map's cells only on the first and last row (the
//     port-only guard of retail's out-of-bounds read);
//   * PlantArea's unpaired planting packs the slot COUNT (0x1400f41e3 MOV EBX,R8D);
//   * PastureWork's grazing cell draws Random::Rand(h) then Rand(w) (import
//     0x1401512e8 at 0x1400f299d / 0x1400f29aa), inside the rect.
// ============================================================================
#include "unit.h"

#include "Work/work_rules.h"
#include "system/Value.h"

using WorkRules::OverWorkerCap;
using WorkRules::PastureGrazeCell;
using WorkRules::PlantAreaPairedSlot;
using WorkRules::SampleCellOutsideMap;
using WorkRules::SampleOffMap;

UNIT_TEST(work_rules_worker_cap_admits_count_equal_to_max) {
    CHECK(!OverWorkerCap(0, 3));
    CHECK(!OverWorkerCap(3, 3));
    CHECK(OverWorkerCap(4, 3));
}

UNIT_TEST(work_rules_sample_retries_on_y_only) {
    CHECK(!SampleOffMap(-1, 5, 10));
    CHECK(!SampleOffMap(10, 5, 10));
    CHECK(!SampleOffMap(0, 0, 10));
    CHECK(SampleOffMap(5, -1, 10));
    CHECK(SampleOffMap(5, 10, 10));
}

UNIT_TEST(work_rules_wrapped_cell_leaves_the_map_only_on_the_edge_rows) {
    CHECK(!SampleCellOutsideMap(-1, 5, 10));   // row 4, last column
    CHECK(!SampleCellOutsideMap(10, 5, 10));   // row 6, first column
    CHECK(SampleCellOutsideMap(-1, 0, 10));    // before cell 0
    CHECK(SampleCellOutsideMap(10, 9, 10));    // past cell 99
    CHECK(!SampleCellOutsideMap(9, 9, 10));
}

UNIT_TEST(work_rules_plant_area_unpaired_slot_packs_the_slot_count) {
    CHECK_EQ(PlantAreaPairedSlot(2, 8, true), 3);
    CHECK_EQ(PlantAreaPairedSlot(2, 8, false), 8);
    CHECK_EQ(PlantAreaPairedSlot(7, 8, false), 8);
}

UNIT_TEST(work_rules_pasture_graze_cell_is_rand_h_then_rand_w_inside_the_rect) {
    const int x0 = 10;
    const int y0 = 20;
    const int w = 4;
    const int h = 6;
    const int size = 64;
    System::Random rng(1234u);
    System::Random ref(1234u);
    int notRetail = 0;
    int outsideRect = 0;
    for (int n = 0; n < 1000; n++) {
        int cell = PastureGrazeCell(rng, x0, y0, w, h, size);
        uint32_t dy = ref.Rand((uint32_t)h);
        uint32_t dx = ref.Rand((uint32_t)w);
        if (cell != (int)((y0 + dy) * size + dx + x0)) {
            notRetail++;
        }
        if (cell < 0 || cell / size < y0 || cell / size >= y0 + h || cell % size < x0 ||
            cell % size >= x0 + w) {
            outsideRect++;
        }
    }
    CHECK_EQ(notRetail, 0);
    CHECK_EQ(outsideRect, 0);
}
