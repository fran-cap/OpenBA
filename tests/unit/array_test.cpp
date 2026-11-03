// ============================================================================
// tests/unit/array_test.cpp — ENDGAME B.3.2 step 1, `Collection::Array<T>`
// ============================================================================
//
// The cases are B32_RULES's list, and every one of them is a rule read out of
// a shipped body rather than a preference:
//
//   grow from empty            capacity 1, NOT 3 (the treble is gated on
//                              oldSize != 0 — array.cpp's J3 banner)
//   append to non-empty        capacity 3 * newSize, not 3 * oldCapacity
//   exact-fit bulk SetSize(n)  capacity exactly n
//   shrink                     destroy on the TRUNCATED TAIL only, capacity and
//                              buffer kept
//   Clear()                    == SetSize(0): buffer retained
//   the three trait shapes     Array<int32_t>; a copy-ctor element
//                              (System::Filename, the kSpec_StringRef /
//                              kSpec_VideoResource shape); a destructor element
//                              (the kSpec_StringExt / kSpec_ExtArrayElem shape,
//                              modelled by a counting type so the exact
//                              ctor/dtor SEQUENCE on a realloc is asserted)
//   kSpec_PkgEntry shrink arm  0x1800298f0's shrink branch, which a live boot
//                              never takes (COLLECTION_INVENTORY.md §2)
//
// These drive the REAL `rtcoll::SetSizeCore` and the REAL spec rows through the
// template, which is the point: the template is a wrapper, so a test that
// re-implemented either would be testing a copy of the port.
// ============================================================================

#include "unit.h"

#include <cstdint>
#include <cstring>
#include <new>
#include <vector>

#include "collection/Collection.h"
#include "system/Filename.h"
#include "collection/array_internal.h"

namespace {

// ---------------------------------------------------------------------------
// The counting element — the `kSpec_StringExt` / `kSpec_ExtArrayElem` shape
// ---------------------------------------------------------------------------
// Non-trivially copyable AND non-trivially destructible, so `SpecFor<Counted>`
// produces byteCopy=false, a real `assign` and a real `destroy` — the same
// three fields those hand-derived rows carry.
struct Event {
    enum Kind { kDefaultCtor, kCopyCtor, kDtor } kind;
    int value;
};
std::vector<Event>* g_log = nullptr;

struct Counted {
    int value;
    Counted() : value(0) { if (g_log) g_log->push_back({Event::kDefaultCtor, 0}); }
    Counted(int v) : value(v) {}                       // seeding, deliberately unlogged
    Counted(const Counted& o) : value(o.value) {
        if (g_log) g_log->push_back({Event::kCopyCtor, o.value});
    }
    Counted& operator=(const Counted& o) { value = o.value; return *this; }
    ~Counted() { if (g_log) g_log->push_back({Event::kDtor, value}); }
};

int CountOf(const std::vector<Event>& log, Event::Kind k) {
    int n = 0;
    for (const Event& e : log) if (e.kind == k) ++n;
    return n;
}

}  // namespace

// ============================================================================
// Growth policy — the four capacity rules
// ============================================================================

UNIT_TEST(array_grow_from_empty_allocates_capacity_one) {
    Collection::Array<int32_t> a;
    CHECK(a.GetData() == nullptr);
    CHECK_EQ(a.GetSize(), 0);
    CHECK_EQ(a.GetCapacity(), 0);

    a.SetSize(1);
    CHECK_EQ(a.GetSize(), 1);
    CHECK_EQ(a.GetCapacity(), 1);      // the `* 3` is gated on oldSize != 0
    CHECK(a.GetData() != nullptr);
}

UNIT_TEST(array_append_to_nonempty_trebles_to_three_times_newsize) {
    Collection::Array<int32_t> a;
    a.SetSize(1);
    CHECK_EQ(a.GetCapacity(), 1);

    a.SetSize(2);                      // newSize == oldSize + 1, oldSize != 0
    CHECK_EQ(a.GetSize(), 2);
    CHECK_EQ(a.GetCapacity(), 6);      // 3 * newSize, not 3 * oldCapacity

    for (int n = 3; n <= 6; ++n) {     // still inside the capacity: no realloc
        a.SetSize(n);
        CHECK_EQ(a.GetCapacity(), 6);
    }

    // The `+ 1` is measured against _SIZE, not against capacity: the treble
    // fires again only on the append that actually outgrows the buffer.
    a.SetSize(7);
    CHECK_EQ(a.GetCapacity(), 21);     // the 1, 6, 21, 66, 201 walk
}

UNIT_TEST(array_bulk_setsize_allocates_exactly_n) {
    Collection::Array<int32_t> a;
    a.SetSize(10);                     // 10 != 0 + 1, so no treble
    CHECK_EQ(a.GetSize(), 10);
    CHECK_EQ(a.GetCapacity(), 10);
}

UNIT_TEST(array_setsize_to_same_size_is_a_noop) {
    Collection::Array<int32_t> a;
    a.SetSize(4);
    int32_t* before = a.GetData();
    for (int i = 0; i < 4; ++i) a[i] = i + 100;

    a.SetSize(4);                      // the exact-equal early return
    CHECK(a.GetData() == before);
    CHECK_EQ(a[3], 103);
}

UNIT_TEST(array_clear_retains_the_buffer) {
    Collection::Array<int32_t> a;
    a.SetSize(5);
    int32_t* buf = a.GetData();

    a.Clear();                         // == SetSize(0)
    CHECK_EQ(a.GetSize(), 0);
    CHECK_EQ(a.GetCapacity(), 5);      // capacity NEVER shrinks
    CHECK(a.GetData() == buf);         // and the buffer is kept
}

UNIT_TEST(array_setsize_with_fill_copies_the_fill_into_the_new_tail) {
    Collection::Array<int32_t> a;
    const int32_t fill = 0x5A5A5A5A;
    a.SetSize(3, fill);
    CHECK_EQ(a.GetSize(), 3);
    for (int i = 0; i < 3; ++i) CHECK_EQ(a[i], fill);
}

// ============================================================================
// Trait instantiation 1 — Array<int32_t> (kSpec_I32, 0x18002fea0)
// ============================================================================

UNIT_TEST(array_int32_spec_is_the_kspec_i32_row) {
    const rtcoll::ArraySpec* s = Collection::Array<int32_t>::Spec();
    CHECK_EQ(s->stride, 4u);
    CHECK_EQ(s->align, 4);
    CHECK(s->byteCopy);                // is_trivially_copyable<int32_t>
    CHECK(s->destroy == nullptr);      // is_trivially_destructible<int32_t>
    CHECK(s->init == nullptr);         // tail UNINITIALISED, retail's empty else
    CHECK(s->assign != nullptr);
}

UNIT_TEST(array_int32_realloc_keeps_the_old_contents) {
    Collection::Array<int32_t> a;
    a.SetSize(3);
    for (int i = 0; i < 3; ++i) a[i] = i * 7;

    a.SetSize(4);                      // grows and reallocates
    for (int i = 0; i < 3; ++i) CHECK_EQ(a[i], i * 7);
    CHECK_EQ(a.GetCapacity(), 12);
}

// ============================================================================
// Trait instantiation 2 — a copy-ctor element (System::Filename)
// The kSpec_StringRef (0x180034860) / kSpec_VideoResource (0x18004f620) shape:
// `assign` is a CALL to Filename's copy ctor, not a memcpy.
// ============================================================================

UNIT_TEST(array_filename_spec_is_the_copy_ctor_shape) {
    const rtcoll::ArraySpec* s = Collection::Array<System::Filename>::Spec();
    CHECK_EQ(s->stride, 8u);
    CHECK(!s->byteCopy);               // a copy ctor, so NOT a byte copy
    CHECK(s->destroy != nullptr);      // Filename releases its block
    CHECK(s->init != nullptr);         // Filename() is not trivial
}

UNIT_TEST(array_filename_realloc_bumps_and_releases_the_refcount) {
    Collection::Array<System::Filename> a;
    a.SetSize(1);
    a[0] = System::Filename("data/models/house.rsc");
    const char* block = a[0]._data;
    CHECK(block != nullptr);
    CHECK_EQ(RtSystem::StringRefCount(block), 1);

    // The realloc copy-CONSTRUCTS the survivor into the new buffer and then
    // destroys the old one: the refcount goes 1 -> 2 -> 1, and the block lives.
    a.SetSize(2);
    CHECK(a[0]._data == block);
    CHECK_EQ(RtSystem::StringRefCount(block), 1);
    CHECK_STREQ(a[0]._data, "data\\models\\house.rsc");   // AdjustDelimiters

    // The new tail was default-CONSTRUCTED, not left uninitialised, because
    // Filename() is not trivial.
    CHECK(a[1]._data == nullptr);
}

// ============================================================================
// Trait instantiation 3 — a destructor element, and the exact realloc sequence
// ============================================================================

UNIT_TEST(array_counted_realloc_is_copy_then_destroy_then_free) {
    std::vector<Event> log;
    g_log = &log;

    Collection::Array<Counted> a;
    a.SetSize(2);                      // fresh buffer: two default ctors
    CHECK_EQ(CountOf(log, Event::kDefaultCtor), 2);
    CHECK_EQ(CountOf(log, Event::kCopyCtor), 0);
    CHECK_EQ(CountOf(log, Event::kDtor), 0);
    a[0].value = 10;
    a[1].value = 11;

    log.clear();
    a.SetSize(3);                      // reallocates

    // Retail's order, and the reason a move would be wrong: the two survivors
    // are COPY-constructed into the new buffer, the new element is
    // default-constructed, and only THEN are the old two destroyed.
    CHECK_EQ(static_cast<int>(log.size()), 5);
    if (log.size() == 5) {
        CHECK(log[0].kind == Event::kCopyCtor);     CHECK_EQ(log[0].value, 10);
        CHECK(log[1].kind == Event::kCopyCtor);     CHECK_EQ(log[1].value, 11);
        CHECK(log[2].kind == Event::kDefaultCtor);
        CHECK(log[3].kind == Event::kDtor);         CHECK_EQ(log[3].value, 10);
        CHECK(log[4].kind == Event::kDtor);         CHECK_EQ(log[4].value, 11);
    }
    CHECK_EQ(a[0].value, 10);
    CHECK_EQ(a[1].value, 11);

    log.clear();
    a.Clear();                          // SetSize(0) — the shrink arm
    CHECK_EQ(CountOf(log, Event::kDtor), 3);
    CHECK_EQ(a.GetCapacity(), 9);       // 3 * 3 from the append above

    log.clear();
    g_log = nullptr;
}

UNIT_TEST(array_shrink_destroys_only_the_truncated_tail) {
    std::vector<Event> log;
    g_log = &log;

    Collection::Array<Counted> a;
    a.SetSize(4);
    for (int i = 0; i < 4; ++i) a[i].value = i;
    Counted* buf = a.GetData();

    log.clear();
    a.SetSize(2);                       // no realloc: 2 < capacity 4

    CHECK_EQ(static_cast<int>(log.size()), 2);   // ONLY the two that fell off
    if (log.size() == 2) {
        CHECK(log[0].kind == Event::kDtor);  CHECK_EQ(log[0].value, 2);
        CHECK(log[1].kind == Event::kDtor);  CHECK_EQ(log[1].value, 3);
    }
    CHECK_EQ(a.GetCapacity(), 4);       // capacity is NOT reduced
    CHECK(a.GetData() == buf);          // and the buffer is not reallocated
    CHECK_EQ(a[0].value, 0);
    CHECK_EQ(a[1].value, 1);

    log.clear();
    g_log = nullptr;
}

UNIT_TEST(array_destructor_destroys_the_live_elements_then_frees) {
    std::vector<Event> log;
    g_log = &log;
    {
        Collection::Array<Counted> a;
        a.SetSize(3);
        log.clear();
    }                                   // ~Array here
    CHECK_EQ(CountOf(log, Event::kDtor), 3);
    g_log = nullptr;
}

// ============================================================================
// kSpec_PkgEntry (0x1800298f0) — the shrink arm with zero live coverage
// ============================================================================
//
// COLLECTION_INVENTORY.md §2: a live game session only ever GROWS the packfile
// entry directory, so `SetSizeCore`'s shrink-tail branch is the one arm of this
// row nothing exercises. It is driven here through the same explicit-spec
// instantiation the registered shim uses.
//
// The row's two element operations are the Filename copy ctor (0x1800067d0)
// and the string-release range helper (0x180026b50), and B3R RT-Core calls
// both BY NAME, so this case runs the REAL bodies (filename.cpp and
// array_destroy.cpp are linked here). It used to install doubles for the two
// addresses through the stub resolver; it now observes the same two facts
// through each element's System::String reference count instead:
//   * the shrink arm releases exactly the truncated TAIL, in place (buffer and
//     capacity kept, the survivors untouched);
//   * the realloc arm copy-constructs the survivors (one reference taken) and
//     then releases the OLD buffer's elements (one reference dropped), so every
//     survivor's count is unchanged and its string is shared, not duplicated.

namespace {

// System::String's reference count lives at `_data - 4` (String.h).
int32_t RefCount(const System::Filename& f) {
    return *reinterpret_cast<const int32_t*>(f._data - 4);
}

}  // namespace

UNIT_TEST(array_pkgentry_shrink_arm_destroys_the_tail_in_place) {
    // Four names, no path separators (so the copy ctor's AdjustDelimiters has
    // nothing to rewrite and the copy shares the buffer). Each holds one
    // reference of its own.
    System::Filename names[4] = { System::Filename("one"), System::Filename("two"),
                                  System::Filename("three"), System::Filename("four") };

    {
        // The instantiation the registered 0x1800298f0 shim uses.
        rtcoll::ArrayWithSpec<&rtcoll::kSpec_PkgEntry> a;
        a.SetSize(4);                       // grow: exact fit, capacity 4
        CHECK_EQ(a.GetCapacity(), 4);
        uint8_t* buf = a.GetData();
        CHECK(buf != nullptr);
        std::memset(buf, 0, 4 * 0x10);      // the row's init is null
        for (int i = 0; i < 4; ++i) {       // each element takes a reference
            new (buf + i * 0x10) System::Filename(names[i]);
            CHECK_EQ(RefCount(names[i]), 2);
        }

        a.SetSize(2);                       // THE SHRINK ARM

        CHECK_EQ(a.GetSize(), 2);
        CHECK_EQ(a.GetCapacity(), 4);       // no reallocation, no free
        CHECK(a.GetData() == buf);
        CHECK_EQ(RefCount(names[0]), 2);    // survivors untouched
        CHECK_EQ(RefCount(names[1]), 2);
        CHECK_EQ(RefCount(names[2]), 1);    // the tail, released in place
        CHECK_EQ(RefCount(names[3]), 1);

        // And the realloc arm of the same row, for the contrast: it copies the
        // two survivors into the new buffer and releases the OLD buffer's two.
        a.SetSize(5);
        CHECK(a.GetCapacity() >= 5);
        uint8_t* nbuf = a.GetData();
        CHECK(nbuf != buf);
        CHECK_EQ(RefCount(names[0]), 2);    // +1 copy, -1 old element
        CHECK_EQ(RefCount(names[1]), 2);
        CHECK(*reinterpret_cast<char**>(nbuf + 0x00) == names[0]._data);   // shared
        CHECK(*reinterpret_cast<char**>(nbuf + 0x10) == names[1]._data);
        // The grown tail (elements 2..4) is NOT initialised -- the row has no
        // init arm -- so null it before the final shrink releases all five.
        std::memset(nbuf + 2 * 0x10, 0, 3 * 0x10);

        a.SetSize(0);                       // releases the two survivors
        CHECK_EQ(RefCount(names[0]), 1);
        CHECK_EQ(RefCount(names[1]), 1);
    }
}

// ============================================================================
// ENDGAME B.3.2 closing pass, item 3 — the DEEP `Array<T>::operator=`
// ============================================================================
//
// `operator=` is now retail's 0x18001cdf0 family, i.e. `rtcoll::ArrayAssignCore`
// (array_destroy.cpp) with `Array<T>::ClearCallback` as the clear. The shape,
// read out of 0x180014c70 and confirmed at 0x180005770 and 0x180033f30:
//
//     if (this == &src) return this;
//     clear(this);                       // element destroy, then the buffer free
//     _size = src._size;
//     if (_size > _capacity) { grow; MemAlloc(cap * stride, align); }
//     for (i = 0; i < _size; ++i) assign(data + i*stride, src.data + i*stride);
//     return this;                       // and NO free after the copy
//
// The cases below are the three trait shapes B32_RULES names, plus the
// self-assign early-out and the "the clear already freed it" property that the
// missing tail free is really about.

UNIT_TEST(array_assign_deep_copies_a_trivial_element) {
    Collection::Array<int32_t> src;
    src.SetSize(3);
    src[0] = 7; src[1] = 8; src[2] = 9;

    Collection::Array<int32_t> dst;
    dst.SetSize(1);
    dst[0] = 100;
    const int32_t* dstBuffer = dst.GetData();

    dst = src;

    CHECK_EQ(dst.GetSize(), 3);
    CHECK_EQ(dst[0], 7);
    CHECK_EQ(dst[1], 8);
    CHECK_EQ(dst[2], 9);
    // The clear freed dst's one-element buffer and zeroed the capacity, so the
    // copy allocated a NEW one — this is a deep copy, not a header steal.
    CHECK(dst.GetData() != src.GetData());
#if defined(_WIN32)
    // The new buffer's ADDRESS differs only because the Windows heap does not hand a
    // block straight back to the next request of a similar size. glibc's tcache does
    // (the 4-byte block freed by the clear is the 12-byte block the copy gets), so off
    // Windows the reallocation is proven by the capacity below instead (Phase D.4
    // POSIX, the Linux dry run).
    CHECK(dst.GetData() != dstBuffer);
#else
    (void)dstBuffer;
#endif
    CHECK_EQ(dst.GetCapacity(), 3);    // exactly n: 3 != 0 + 1, so no treble

    // The source is untouched — it is a COPY, never a move.
    CHECK_EQ(src.GetSize(), 3);
    CHECK_EQ(src[0], 7);
}

UNIT_TEST(array_self_assign_returns_without_clearing) {
    Collection::Array<int32_t> a;
    a.SetSize(2);
    a[0] = 4; a[1] = 5;
    const int32_t* buffer = a.GetData();

    a.Assign(a);                       // `CMP RCX,RDX / JZ` — the early-out

    CHECK(a.GetData() == buffer);      // not freed and not reallocated
    CHECK_EQ(a.GetSize(), 2);
    CHECK_EQ(a[0], 4);
    CHECK_EQ(a[1], 5);
}

UNIT_TEST(array_assign_copy_ctor_element_bumps_the_refcount) {
    Collection::Array<System::Filename> src;
    src.SetSize(1);
    src[0] = System::Filename("data/models/house.rsc");
    const char* block = src[0]._data;
    CHECK_EQ(RtSystem::StringRefCount(block), 1);

    Collection::Array<System::Filename> dst;
    dst.SetSize(1);
    dst[0] = System::Filename("data/models/barn.rsc");
    CHECK_EQ(RtSystem::StringRefCount(dst[0]._data), 1);

    dst = src;

    // The clear ran `~Filename` over dst's live element — its block is gone —
    // and the per-element assign is `Filename`'s COPY CONSTRUCTOR, so the two
    // arrays now share one block at refcount 2.
    CHECK_EQ(dst.GetSize(), 1);
    CHECK(dst[0]._data == block);
    CHECK_EQ(RtSystem::StringRefCount(block), 2);
    CHECK_STREQ(dst[0]._data, "data\\models\\house.rsc");
}

UNIT_TEST(array_assign_is_clear_then_copy_never_a_move) {
    std::vector<Event> log;

    Collection::Array<Counted> src;
    src.SetSize(2);
    src[0].value = 10;
    src[1].value = 11;

    Collection::Array<Counted> dst;
    dst.SetSize(1);
    dst[0].value = 99;

    g_log = &log;
    dst = src;
    g_log = nullptr;

    // Retail's order: the CLEAR destroys dst's live element and frees its
    // buffer FIRST, then the two survivors are COPY-constructed out of src.
    // A move-assign would show no copy ctor at all and would empty `src`.
    CHECK_EQ(static_cast<int>(log.size()), 3);
    if (log.size() == 3) {
        CHECK(log[0].kind == Event::kDtor);      CHECK_EQ(log[0].value, 99);
        CHECK(log[1].kind == Event::kCopyCtor);  CHECK_EQ(log[1].value, 10);
        CHECK(log[2].kind == Event::kCopyCtor);  CHECK_EQ(log[2].value, 11);
    }
    CHECK_EQ(dst.GetSize(), 2);
    CHECK_EQ(dst[0].value, 10);
    CHECK_EQ(dst[1].value, 11);
    CHECK_EQ(src.GetSize(), 2);        // the source still owns its elements
    CHECK_EQ(src[0].value, 10);

    // The two buffers are independent afterwards — which is what "NO free after
    // the copy" has to mean: the clear freed dst's old buffer exactly once, and
    // dst's new one is not src's.
    CHECK(dst.GetData() != src.GetData());
    log.clear();
    dst.Clear();
    src.Clear();
}

UNIT_TEST(array_assign_from_empty_leaves_an_empty_array) {
    Collection::Array<int32_t> src;    // never sized: null buffer
    Collection::Array<int32_t> dst;
    dst.SetSize(4);
    CHECK(dst.GetData() != nullptr);

    dst = src;

    // The clear freed the buffer and zeroed all three fields; the copy loop
    // runs zero times and allocates nothing, because `_size > _capacity` is
    // `0 > 0`.
    CHECK_EQ(dst.GetSize(), 0);
    CHECK_EQ(dst.GetCapacity(), 0);
    CHECK(dst.GetData() == nullptr);
}

// ============================================================================
// Review finding F-012 — a negative serialized count
// ============================================================================
//
// Every array Serialize reads its count with `SerializeInt(n)` and then calls
// `SetSize(n)` with no sign check (PackageFile, SoundEffect, GraphicsMesh
// nodes, AnimationGroup keys). The fuzz input below is the 25-byte SoundEffect
// .rsc body that reached it; its last four bytes are the array count, -1.
// Retail's 0x1800298f0 then takes the shrink arm with a negative newSize and
// destroys `oldSize - newSize` elements starting at `old + newSize*stride`,
// i.e. BELOW the buffer, and leaves `_size` negative.

namespace {

const uint8_t kSoundEffectNegCount[] = {
    0x03, 0xaa, 0xaa, 0xaa, 0xaa, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x80, 0x3f, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff,
};

int32_t NegCountFromInput() {
    int32_t n = 0;
    std::memcpy(&n, kSoundEffectNegCount + sizeof(kSoundEffectNegCount) - 4, 4);
    return n;
}

}  // namespace

UNIT_TEST(array_negcount_setsize_on_empty_array_stays_empty) {
    const int32_t n = NegCountFromInput();
    CHECK_EQ(n, -1);

    Collection::Array<int32_t> a;
    a.SetSize(n);
    CHECK_EQ(a.GetSize(), 0);
    CHECK_EQ(a.GetCapacity(), 0);
    CHECK(a.GetData() == nullptr);
}

UNIT_TEST(array_negcount_setsize_destroys_only_the_live_elements) {
    std::vector<Event> log;
    g_log = &log;
    {
        Collection::Array<Counted> a;
        a.SetSize(3);
        for (int i = 0; i < 3; ++i) {
            a[i].value = 10 + i;
        }
        Counted* buf = a.GetData();
        log.clear();

        a.SetSize(NegCountFromInput());

        // Exactly the three live elements are destroyed, none below the buffer.
        CHECK_EQ(CountOf(log, Event::kDtor), 3);
        bool allLive = true;
        for (const Event& e : log) {
            if (e.value < 10 || e.value > 12) {
                allLive = false;
            }
        }
        CHECK(allLive);
        CHECK_EQ(a.GetSize(), 0);
        CHECK(a.GetData() == buf);
        log.clear();
    }
    CHECK_EQ(CountOf(log, Event::kDtor), 0);   // size 0: ~Array destroys nothing more
    g_log = nullptr;
}
