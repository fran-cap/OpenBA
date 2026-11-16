#pragma once
// ============================================================================
// src/game/Component/UIComponent.h — ENDGAME B.3.8 block VIS
//
// `UIComponent` as a real C++ class with a compiler-generated vtable
// (B38_RULES.md D1-D4).
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §6):
//   reflected "UIComponent", parent "Component", ti 0x1401c7fc8
//   create 0x140039550: MemAlloc(0x38, 8) + ctor 0x14012de00
//   ctor   0x14012de00: Component ctor, stamp 0x1401649f0, +0x28 = +0x30 = 0,
//                       flags |= 0x80 (no change over Component's)
//   dtor   0x14012dda0: MemFree(+0x28), +0x28 = +0x30 = 0 (~data, the inlined
//                       ~Array), restamp, ~Internal
//   table  0x1401649f0, 16 slots:
//     [0] 0x140039760 scalar deleting dtor   [1]/[2] TypeName/Type
//     [3] 0x14012dd00 Persist                [4] 0x14012dca0 Startup (size + zero data)
//     every other slot = Component's
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Component/Component.h"
#include "Object/exe_type.h"
#include "collection/Collection.h"   // Collection::Array (data)

class UIComponent final : public Component {
    DECLARE_EXE_TYPE(UIComponent)

public:
    UIComponent();                                                // 0x14012de00

    // 0x14012dda0: the body is empty; ~data frees the +0x28 byte array (MemFree,
    // then +0x28 = +0x30 = 0).
    ~UIComponent() override = default;                            // [0] 0x140039760
    void Persist(IO::Stream& stream) override;                          // [3] 0x14012dd00
    void Startup() override;                                      // [4] 0x14012dca0

    // +0x28..+0x37: a growable uint8_t array header (verified against retail
    // slot-3 Serialize, 0x14012dd00, and its resize helper FUN_14000a590):
    // ptr@+0x28, count@+0x30, cap@+0x34 — see uicomponent_serialize.cpp.
    // FUN_14000a590 is Array<uint8_t>'s trait row: stride 1, MemAlloc(cap, 1), the
    // prefix byte-copied, NO init arm (0x14000a5f2..0x14000a60c write only i < old).
    // PUBLIC while the ported bodies read them (B38 decision 1).
    Collection::Array<uint8_t> data;   // +0x28 the controller bytes (count +0x30, capacity +0x34)
};

static_assert(sizeof(UIComponent) == 0x38, "UIComponent is 0x38 (create 0x140039550)");
static_assert(alignof(UIComponent) == 8, "UIComponent alignment");
static_assert(offsetof(UIComponent, data) == 0x28, "UIComponent +0x28");
static_assert(offsetof(UIComponent, data) + offsetof(Collection::Array<uint8_t>, _size) == 0x30, "UIComponent +0x30");
static_assert(offsetof(UIComponent, data) + offsetof(Collection::Array<uint8_t>, _capacity) == 0x34, "UIComponent +0x34");
