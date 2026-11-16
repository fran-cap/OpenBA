// ============================================================================
// tests/unit/slot_check_test.cpp — slot_check.h itself (B3X W0 T0)
// ============================================================================
// The suite states RETAIL slots and lets the type decide the compiler's
// (B3X_RULES X5). This proves the helper shifts: the same retail numbers pass
// on a class on a slot-0 `DeletingDtor` member (compiler == retail) and
// on a class with `virtual ~T()` (GCC's D1 + D0 take slots 0 and 1, so retail
// n >= 1 is compiler n + 1 — every flipped class).
// ============================================================================
#include "unit.h"
#include "slot_check.h"

#include <type_traits>

namespace {

// The shape after the flip: retail [0] dtor, [1] First, [2] Second.
struct WithVirtualDtor {
    virtual ~WithVirtualDtor() {}
    virtual int First() { return 1; }
    virtual int Second() { return 2; }
};

// The slot-0 member shape: the same retail
// table, slot 0 declared as a member.
struct WithDeletingDtor {
    virtual WithDeletingDtor* DeletingDtor(unsigned) { return this; }
    virtual int First() { return 1; }
    virtual int Second() { return 2; }
protected:
    ~WithDeletingDtor() = default;
};
struct DeletingDtorLeaf final : WithDeletingDtor {};

// Neither: a table with no destructor slot (the Thread / Controller shape).
struct NoDtorSlot {
    virtual int First() { return 1; }
    virtual int Second() { return 2; }
protected:
    ~NoDtorSlot() = default;
};

}  // namespace

UNIT_TEST(slot_check_the_same_retail_slots_pass_before_and_after_the_flip) {
    static_assert(std::has_virtual_destructor<WithVirtualDtor>::value, "the flipped shape");
    static_assert(!std::has_virtual_destructor<WithDeletingDtor>::value, "the exempt *Data shape");

    // The compiler's own numbering: the first virtual after the destructor.
    CHECK_EQ(CompilerSlot(&WithVirtualDtor::First), 2L);
    CHECK_EQ(CompilerSlot(&WithDeletingDtor::First), 1L);
    CHECK_EQ(CompilerSlot(&WithVirtualDtor::Second), 3L);
    CHECK_EQ(CompilerSlot(&WithDeletingDtor::Second), 2L);

    // One set of RETAIL numbers for both.
    CHECK_DTOR_SLOT(WithVirtualDtor);
    CHECK_SLOT(WithVirtualDtor, First, 1);
    CHECK_SLOT(WithVirtualDtor, Second, 2);
    CHECK_DTOR_SLOT(WithDeletingDtor);
    CHECK_SLOT(WithDeletingDtor, First, 1);
    CHECK_SLOT(WithDeletingDtor, Second, 2);
    CHECK_DTOR_SLOT(DeletingDtorLeaf);                 // an inherited member is found
    CHECK_SLOT(DeletingDtorLeaf, Second, 2);

    // A table with no destructor slot is never shifted, and has no dtor slot.
    CHECK_SLOT(NoDtorSlot, First, 0);
    CHECK_SLOT(NoDtorSlot, Second, 1);
    CHECK(!slot_check::DtorSlotOk<NoDtorSlot>());

    // The guards: retail slot 0 of a flipped class is the destructor pair, not
    // a member slot; a wrong number fails.
    CHECK(!slot_check::SlotOk(&WithVirtualDtor::First, 0));
    CHECK(!slot_check::SlotOk(&WithVirtualDtor::Second, 1));
    CHECK(!slot_check::SlotOk(&WithDeletingDtor::First, 2));
    CHECK(slot_check::SlotOk(&WithDeletingDtor::DeletingDtor, 0));
}

UNIT_TEST(slot_check_raw_reads_take_retail_slots) {
    CHECK_EQ(RetailSlot<WithVirtualDtor>(0), 0L);
    CHECK_EQ(RetailSlot<WithVirtualDtor>(1), 2L);
    CHECK_EQ(RetailSlot<WithDeletingDtor>(1), 1L);
    CHECK_EQ(RetailByteOffset<WithVirtualDtor>(0x10), (uintptr_t)0x18);
    CHECK_EQ(RetailByteOffset<NoDtorSlot>(0x08), (uintptr_t)0x08);

    using IntFn = int (*)(void*);
    WithVirtualDtor v;
    DeletingDtorLeaf d;
    CHECK_EQ((RetailSlotFn<IntFn, WithVirtualDtor>(&v, 1)(&v)), 1);
    CHECK_EQ((RetailSlotFn<IntFn, WithVirtualDtor>(&v, 2)(&v)), 2);
    CHECK_EQ((RetailSlotFn<IntFn, DeletingDtorLeaf>(&d, 1)(&d)), 1);
    CHECK_EQ((RetailSlotFn<IntFn, DeletingDtorLeaf>(&d, 2)(&d)), 2);
    CHECK(RetailEntry<WithVirtualDtor>(&v, 1) != RetailEntry<WithVirtualDtor>(&v, 2));
}
