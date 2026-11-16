// ============================================================================
// tests/unit/ui_value_types_abi_test.cpp — ENDGAME B.3.6 block D
//   the two UI VALUE TYPES (RULE B6-10): UI::Sprite and UI::String
//
// Both are 0x10-byte handle+index records that the tree holds BY VALUE
// (Element::_toolTip, ImageElement, LabelElement, ProgressElement,
// SliderElement, Save::SaveLoad) and that retail returns through hidden
// retslots. The conversion rests on four claims, each pinned here:
//
//   1. LAYOUT — 0x10 bytes, 8-aligned, the handle at +0x00, the index at +0x08.
//   2. TRIVIAL COPY — so every by-value holder and every memcpy'd retslot keeps
//      working, and the implicit `operator=` IS retail's `??4` (two qword
//      copies, 0x180030cb0 / 0x180035090) byte for byte, pad0C included.
//   3. THE DEFAULT CTOR IS RETAIL'S — {null, -1} (0x18011ba10 / 0x18011da40)
//      and it does NOT write pad0C: a door placement-news it into caller
//      storage, and a byte the retail ctor never stores must survive.
//   4. The inline members answer as retail's bodies do: IsValid's two
//      short-circuit orders, Index, Sprite's == / != (sheet then index, pad0C
//      ignored).
//
// The out-of-line constructors and Width/Height live in src/engine/ui/
// sprite.cpp, which the unit binary links (UNIT_TREE_SRCS in
// tests/unit/sources.cmake).
// ============================================================================
#include "unit.h"

#include "ui/Sprite.h"
#include "ui/String.h"

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <new>
#include <type_traits>

namespace {

UNIT_TEST(ui_sprite_layout_and_triviality) {
    CHECK_EQ((int)alignof(UI::Sprite), 8);
    CHECK(std::is_trivially_copyable<UI::Sprite>::value);
    CHECK(std::is_trivially_destructible<UI::Sprite>::value);
    CHECK(!std::is_polymorphic<UI::Sprite>::value);
    // The transition alias names the same type (RULE B6-4.2).
    CHECK((std::is_same<::Sprite, UI::Sprite>::value));
}

UNIT_TEST(ui_string_layout_and_triviality) {
    CHECK_EQ((int)alignof(UI::String), 8);
    CHECK(std::is_trivially_copyable<UI::String>::value);
    CHECK(std::is_trivially_destructible<UI::String>::value);
    CHECK(!std::is_polymorphic<UI::String>::value);
    CHECK((std::is_same<::UIString, UI::String>::value));
}

// Claim 3: the door shape. Poison the caller's storage, placement-new the
// default ctor into it, and check that only +0x00 and +0x08 changed.
UNIT_TEST(ui_sprite_default_ctor_writes_only_what_retail_writes) {
    alignas(8) uint8_t storage[0x10];
    std::memset(storage, 0xcd, sizeof storage);
    UI::Sprite* s = new (storage) UI::Sprite();
    CHECK(s->spriteSheet == nullptr);
    CHECK_EQ(s->spriteIndex, -1);
    CHECK_EQ(*reinterpret_cast<const uint32_t*>(storage + 0x0c), 0xcdcdcdcdu);
    CHECK(!s->IsValid());
    CHECK_EQ(s->Index(), -1);
}

UNIT_TEST(ui_string_default_ctor_writes_only_what_retail_writes) {
    alignas(8) uint8_t storage[0x10];
    std::memset(storage, 0xcd, sizeof storage);
    UI::String* s = new (storage) UI::String();
    CHECK(s->table == nullptr);
    CHECK_EQ(s->index, -1);
    CHECK_EQ(*reinterpret_cast<const uint32_t*>(storage + 0x0c), 0xcdcdcdcdu);
    CHECK(!s->IsValid());
    CHECK_EQ(s->Index(), -1);
}

// Claim 2: `operator=` copies all sixteen bytes, as retail's two qword moves do.
UNIT_TEST(ui_value_types_assignment_is_retails_two_qword_copy) {
    UI::Sprite a;
    a.spriteSheet = reinterpret_cast<UI::SpriteSheet*>(uintptr_t(0x1000));
    a.spriteIndex = 7;
    a.pad0C = 0x11223344;
    UI::Sprite b;
    b = a;
    CHECK(std::memcmp(&a, &b, sizeof a) == 0);

    UI::String c;
    c.table = reinterpret_cast<UI::StringTable*>(uintptr_t(0x2000));
    c.index = 3;
    c.pad0C = 0x55667788;
    UI::String d;
    d = c;
    CHECK(std::memcmp(&c, &d, sizeof c) == 0);
}

// Claim 4.
UNIT_TEST(ui_sprite_compare_and_validity_are_retails) {
    int dummy = 0;
    UI::Sprite a;
    a.spriteSheet = reinterpret_cast<UI::SpriteSheet*>(&dummy);
    a.spriteIndex = 2;
    a.pad0C = 1;
    UI::Sprite b = a;
    b.pad0C = 99;                          // 0x180030c60 never reads +0x0c
    CHECK(a == b);
    CHECK(!(a != b));
    b.spriteIndex = 3;
    CHECK(!(a == b));
    CHECK(a != b);

    CHECK(a.IsValid());
    a.spriteIndex = -1;                    // index first (0x180030c40)
    CHECK(!a.IsValid());
    a.spriteIndex = 0;
    a.spriteSheet = nullptr;
    CHECK(!a.IsValid());

    UI::String s;
    s.table = reinterpret_cast<UI::StringTable*>(&dummy);
    s.index = 0;
    CHECK(s.IsValid());                    // table first (0x180035060)
    s.index = -5;
    CHECK(!s.IsValid());
    CHECK_EQ(s.Index(), -5);
}

}  // namespace
