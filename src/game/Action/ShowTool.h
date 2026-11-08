#pragma once
// ============================================================================
// src/game/Action/ShowTool.h — ENDGAME B.3.8 block ACT (B38_RULES.md D1-D5)
//
// `ShowTool`, an exe Action leaf, as a real C++ class with a compiler vtable.
// RETAIL (Application-x64.exe, B38_SLOTS.md §6):
//   reflected "ShowTool", parent "Action", ti 0x1401c3e28
//   create 0x1400013b0: MemAlloc(0x30, 8) + the inlined ctor
//   createInPlace = the IMPORTED `Internal::CreateInPlaceObject` (returns null);
//     the tree's in-place entry builds the object and that is KEPT (B8-13).
//   table 0x1401589b0, 10 slots; every slot not declared below is Action's (= parent)
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Action/Action.h"
#include "Object/exe_type.h"
#include "object/Internal.h"   // Object::InternalPtr<T>

class CitizenComponent;   // Component/CitizenComponent.h

class ShowTool final : public Action {
    DECLARE_EXE_TYPE(ShowTool)

public:
    ShowTool();                                                   // create 0x1400013b0
    ShowTool(CitizenComponent* const* citizen, int toolId);   // 0x140042890
    ~ShowTool() override = default;                            // [0] 0x140001420
    void Persist(IO::Stream& stream) override;                       // [3] 0x140042780
    int  AbstractImpl(ActionStack* actionStack) override;             // [4] 0x140042710
    void Exit(ActionStack* actionStack) override;                     // [7] 0x140042700 own `RET 0` (same bytes as the base body)
    void CheckEntity() override;                               // [8] 0x1400426d0
    bool GetTarget(ComponentEntity** param) override;                      // [9] 0x140042680

    // ---- fields, retail order (PUBLIC: the slot bodies are C functions) ----
    Object::InternalPtr<CitizenComponent> _citizen;   // +0x20 the citizen (Persist checks CitizenComponent)
    int32_t   _toolId;          // +0x28 tool id (-1 = the carried resource)
    uint8_t   _pad2C[4];         // +0x2C padding
};

static_assert(sizeof(ShowTool) == 0x30, "ShowTool is 0x30 (create 0x1400013b0 MemAlloc)");
static_assert(offsetof(ShowTool, _pad2C) == 0x2C, "ShowTool::_pad2C");
static_assert(alignof(ShowTool) == 8, "ShowTool is 8-aligned");
static_assert(offsetof(ShowTool, _citizen) == 0x20, "ShowTool +0x20");
static_assert(offsetof(ShowTool, _toolId) == 0x28, "ShowTool +0x28");
