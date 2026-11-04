// src/game/Action/completeroad.cpp
// Clean C++ reimplementation of CompleteRoad action methods.
// Source: Game/Action/CompleteRoad.cpp (from profile build debug strings)
//
// CompleteRoad -- 0x30 bytes. Completes road construction.
// Inherits: Object::Internal -> Action (0x20) -> CompleteRoad
// Vtable at 0x14015fb70 (10 slots).
//
// Decompiled from Application-x64.exe using Ghidra.

#include "CompleteRoad.h"
#include "object/Internal.h"
#include "object/TypeInfo.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Action/exe_bodies.h"   // the B4 FORWARD ctor door
#include "Game/Map.h"   // Map::FinishRoadCell 0x14009c640
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch C
#include "Action/saved_bounds.h"
#include <cstdio>
#include <cstdint>
#include <new>

// ============================================================
// Helpers, called by name (B3R)
//   0x14009c640 Map::FinishRoadCell -- complete road tile (tileIndex, direction, roadType)
// DAT_1401c3dd8 is GameUpdateLoop::_singleton; +0xF8 its Map.
// ============================================================

// ============================================================
// CompleteRoad::Serialize (0x1400c5880)
// Serializes: status(+0x18), int(+0x20), uchar(+0x28), int(+0x24)
// ============================================================
void CompleteRoad::Persist(IO::Stream& stream) {
    Action::Persist(stream);   // inlined in retail: Serializer(stream, 0), status +0x18

    IO::Serializer ser(stream, 0);
    ser.Serialize(roadTileIndex, nullptr);   // +0x20
    ser.Serialize(roadDirection, nullptr);   // +0x28
    ser.Serialize(roadType, nullptr);        // +0x24
}

// ============================================================
// CompleteRoad::AbstractImpl / _purecall (0x1400c5850)
// Calls road tile completion on the world map (GameUpdateLoop +0xF8).
// Returns 0 (action complete).
// ============================================================

int CompleteRoad::AbstractImpl(ActionStack* actionStack) {                     // [4] 0x1400c5850
    (void)actionStack;
    Map* map = GameUpdateLoop::_singleton->m_game;
    // NOT RETAIL (review F-105): roadTileIndex comes from the save (Persist +0x20) and
    // FinishRoadCell writes the cell it names; retail 0x14009c640 has no bound. An index off
    // the map drops the action (return 0, as the completed road) instead of writing past
    // Map::cells.
    if (!ActionBounds::IsMapCell(roadTileIndex, map->cells.GetSize())) {
        return 0;
    }
    map->FinishRoadCell(roadTileIndex, roadDirection, roadType);   // 0x14009c640
    return 0;
}

// ============================================================
// class CompleteRoad (CompleteRoad.h) — ENDGAME B.3.8 ACT. The hand-built
// g_CompleteRoad_Vtable[10], its per-class type-id cache and the
// Action_Alloc / Action_InitInPlace factories are gone: the compiler emits the
// table, DECLARE_EXE_TYPE answers slots 1/2.
// ============================================================
#include "Object/factory_registration.h"

// create 0x140021ec0 (inlined ctor). The tree's factory zeroed every field
// above +0x10; kept (B38 B8-13 / D1: no behaviour change in this step).
CompleteRoad::CompleteRoad()
    : roadTileIndex(0), roadType(0), roadDirection(0), _pad29{} {}

// 0x1400c59d0 (CompleteRoad_Action_CopyCtor):
// +0x20 = tileIndex (R8D), +0x24 = roadType (EDX), +0x28 = direction (R9B).
CompleteRoad::CompleteRoad(uint32_t roadTypeArg, uint32_t tileIndex, uint8_t direction)
    : roadTileIndex(static_cast<int32_t>(tileIndex)),
      roadType(static_cast<int32_t>(roadTypeArg)),
      roadDirection(direction) {}

::Object::Base* CompleteRoad::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(CompleteRoad), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) CompleteRoad());
}
::Object::Base* CompleteRoad::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) CompleteRoad());
}


DEFINE_EXE_TYPE(CompleteRoad, "Action")
