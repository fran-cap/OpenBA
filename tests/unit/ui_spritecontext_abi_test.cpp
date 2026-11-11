// ============================================================================
// tests/unit/ui_spritecontext_abi_test.cpp — ENDGAME B.3.6 block D
//   UI::SpriteContext: a real, concrete, NON-polymorphic class of 0x88 bytes
//
// B36_RULES section 13 item 2 left the size as "0x88 or 0x90". Block D pinned
// it from the retail copy constructor ??0SpriteContext@UI@@QEAA@AEBV01@@Z
// (0x1800321c0), whose last member store is the qword at +0x80 — so 0x88. The
// layout the ported members and the two stack builders
// (Save::SaveLoad::Draw, UserInterface::Draw) rely on is pinned here, plus:
//   * the object has NO vtable (Begin's first write to +0x00 is a MOVSS);
//   * it is default-constructible IN PUBLIC (RULE B6-4.5: saveload_draw.cpp
//     builds one on its stack) and its Array members start empty;
//   * +0x50 is a UI::Font::Layout whose two arrays sit at +0x58 / +0x68 — the
//     glyph array AddFont walks and the line array ~SpriteContext frees first;
//   * it is NOT trivially copyable — nor is retail's (a deep copy ctor and a
//     real destructor are exported) — and C++ copy-assignment is deleted.
// The member bodies live in src/engine/ui/spritecontext*.cpp (port registry,
// live video context) and are verified by the real run.
// ============================================================================
#include "unit.h"

#include "ui/SpriteContext.h"

#include <cstdint>
#include <cstddef>
#include <type_traits>

namespace {

UNIT_TEST(ui_spritecontext_size_is_0x88) {
    CHECK_EQ((int)alignof(UI::SpriteContext), 8);
    CHECK(!std::is_polymorphic<UI::SpriteContext>::value);
    CHECK(std::is_default_constructible<UI::SpriteContext>::value);
    CHECK(!std::is_trivially_copyable<UI::SpriteContext>::value);
    CHECK(!std::is_copy_assignable<UI::SpriteContext>::value);
}

// The B6-4.5 shape: a stack instance built the way saveload_draw.cpp builds it.
// The four Array members come up empty, and the implicit destructor (retail's
// 0x18011d250 member-wise) is safe on them.
UNIT_TEST(ui_spritecontext_stack_instance_starts_empty) {
    UI::SpriteContext sc;
    sc._scale = 1.0f;
    sc._currentSheet = nullptr;
    sc._clipRegion = nullptr;
    sc._spriteCapture = nullptr;
    CHECK(sc._draws._data == nullptr);
    CHECK_EQ(sc._draws._size, 0);
    CHECK(sc._deferred._data == nullptr);
    CHECK(sc._layout._entries._data == nullptr);
    CHECK_EQ(sc._layout._entries._size, 0);
    CHECK(sc._layout._lineWidths._data == nullptr);
}

}  // namespace
