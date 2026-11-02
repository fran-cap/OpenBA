// ============================================================================
// tests/unit/ext_input_fetch_room_test.cpp -- fix.input_fetch_room's fetch count
// ============================================================================
// src/ext/input_fetch_room/input_fetch_room_rule.h without the world. Off, the count is
// retail's -1 (a full carry). On, the load fits the workplace: Deposits() is
// DropOffRawMaterial::Deposit's test (0x1400c7620 -> StorageComponent::CanAdd 0x14012cd80:
// the whole load or nothing) applied to what PickupStoredRawMaterial (0x1400ca520) would load.
// ============================================================================
#include "unit.h"
#include "ext/input_fetch_room/input_fetch_room_rule.h"

using namespace Ext::InputFetchRoom;

namespace {

// Units the Pickup ctor loads for `count` (count < 1: the carry room), clamped to the store.
int Loaded(int count, int carryCapacity, int carryStored, int weight, int available) {
    int n = count < 1 ? (carryCapacity - carryStored) / weight : count;
    return n < available ? n : available;
}

// The drop-off's all-or-nothing test at the workplace.
bool Deposits(int units, int destCapacity, int destStored, int weight) {
    return units * weight + destStored <= destCapacity;
}

}  // namespace

UNIT_TEST(ext_input_fetch_room_off_is_retail) {
    CHECK_EQ(FetchCount(false, 300, 250, 100, 0, 1), kRetailCount);
    CHECK_EQ(FetchCount(false, 300, 0, 100, 0, 1), kRetailCount);
}

UNIT_TEST(ext_input_fetch_room_retail_load_bounces) {
    // A tavern-like workplace with 50 of 300 free; the worker carries up to 100 of weight 1.
    const int loaded = Loaded(kRetailCount, 100, 0, 1, 500);
    CHECK_EQ(loaded, 100);
    CHECK(!Deposits(loaded, 300, 250, 1));   // refused whole: carried off elsewhere
}

UNIT_TEST(ext_input_fetch_room_on_load_fits) {
    const int count = FetchCount(true, 300, 250, 100, 0, 1);
    CHECK_EQ(count, 50);
    const int loaded = Loaded(count, 100, 0, 1, 500);
    CHECK(Deposits(loaded, 300, 250, 1));
    // Weight 2: 25 units of room, 50 of carry.
    CHECK_EQ(FetchCount(true, 300, 250, 100, 0, 2), 25);
    // The carry is the tighter bound when the workplace is nearly empty.
    CHECK_EQ(FetchCount(true, 300, 0, 100, 0, 1), 100);
    // A store holding less than that: the Pickup clamps, the deposit still fits.
    CHECK(Deposits(Loaded(FetchCount(true, 300, 250, 100, 0, 1), 100, 0, 1, 7), 300, 250, 1));
}

UNIT_TEST(ext_input_fetch_room_on_no_room_skips) {
    CHECK_EQ(FetchCount(true, 300, 300, 100, 0, 1), 0);
    CHECK_EQ(FetchCount(true, 300, 299, 100, 0, 2), 0);
    CHECK_EQ(FetchCount(true, 300, 310, 100, 0, 1), 0);   // over-full (AddItem checks nothing)
}

UNIT_TEST(ext_input_fetch_room_zero_weight_keeps_retail) {
    CHECK_EQ(FetchCount(true, 300, 0, 100, 0, 0), kRetailCount);
}
