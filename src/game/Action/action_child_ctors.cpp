// src/game/Action/action_child_ctors.cpp
// FUN_1400c29a0 (Entity_impl_29a0): the heading from an entity toward a world target,
// used by the Work action-queue builders. No class yet (B.4 inventory A: unattributed).
//
// B.4 R0-EXE: the four Action ctors that lived here (ShowTool 0x140042890,
// RemoveNaturalResource 0x1400cadf0, AnimateNaturalResource 0x1400c57d0,
// CreateNaturalResource 0x1400c6a20) and their by-address doors moved, text unchanged,
// to their classes' files (showtool.cpp, removenaturalresource.cpp,
// animatenaturalresource.cpp, createnaturalresource.cpp).
#include "Action/exe_bodies.h"
// Closure (bodies NOT duplicated; called by name, B3R):
//   Entity_ClearPathQueue        0x140003f60 ComponentEntity::FindComponent<ModelComponent>
//   FUN_140004680                0x140004680 ComponentEntity::FindComponent<MapComponent>
//   CharacterComponent_impl_f3e0 0x14009f3e0 Map::GetTerrainPos (mapquery.cpp / cell->world)
//   Math::Vector3::Normalize     slot 0x1401512c0 / ?Normalize@Vector3@Math@@QEBA?AV12@XZ
//   Math::Vector3::Heading       slot 0x1401512d8 / ?Heading@Vector3@Math@@QEBAMXZ

#include "object/TypeInfo.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Component/exe_bodies.h"
#include "Game/exe_bodies.h"
#include "math/Math.h"
#include "Component/MapComponent.h"
#include "Component/ModelComponent.h"
#include "Entity/ComponentEntity.h"
#include "Game/Map.h"
#include <cstdint>
#include <new>

// Math::Vector3::Normalize (slot 0x1401512c0) and Heading (slot 0x1401512d8) are direct
// members of the real class (B.3.1).

// ============================================================
// FUN_1400c29a0 — Entity_impl_29a0
// Compute the heading (radians, XMM0) from a citizen toward a world target.
//   param_1 = float* worldPos (the target);  param_2 = InternalBasePtr* ({inner ptr}).
// If the entity has a live path node (Entity_ClearPathQueue(*p2) -> node, *node != 0),
// use the node's stored world pos (node+0x158..+0x160). Otherwise derive a world pos
// from its cell node (FUN_140004680 -> node) via the grid math + CharacterComponent
// cell->world sampler. Then v = pos - worldPos; Normalize(v); return Heading(v).
// ============================================================
float HeadingToward(const float* worldPos, ComponentEntity* const* entityIBP) {
    float pos[3] = {0.0f, 0.0f, 0.0f};

    ModelComponent* model = (*entityIBP)->FindComponent<ModelComponent>();   // 0x140003f60

    if (model != nullptr) {
        // --- a ModelComponent: read its world position directly ---
        const Math::Vector3& at = (*entityIBP)->FindComponent<ModelComponent>()->transform.translation;   // 0x140003f60
        pos[0] = at.x;
        pos[1] = at.y;
        pos[2] = at.z;
    } else {
        // --- none: derive a world pos from the entity's MapComponent tile ---
        const MapComponent* node = (*entityIBP)->FindComponent<MapComponent>();   // 0x140004680

        // B6 (14d G61): 0x1400c2a30 / 0x1400c2a37 read [0x1401c3dd8]+0xf8 and 0x1400c2a4b its
        // size with no test; 0x1400c2a76 IDIV R9D divides by it with no zero test.
        Map* mapPtr = GameUpdateLoop::_singleton->m_game;
        int32_t width = mapPtr->size;

        int32_t s28 = node->tileX0;
        int32_t s2a = node->tileY0;
        int32_t s2c = node->tileX1;
        int32_t s2e = node->tileY1;

        int32_t cellIdx = (s2a + (s2e >> 1)) * width + (s2c >> 1) + s28;
        int32_t quot = cellIdx / width;   // EAX after IDIV
        int32_t rem  = cellIdx % width;   // EDX after IDIV
        float   center = static_cast<float>(width >> 1) - 0.5f;  // (width>>1) - DAT_140172114

        float inVec[3];
        inVec[0] = static_cast<float>(rem)  - center;  // local_28
        inVec[1] = static_cast<float>(quot) - center;  // local_24
        inVec[2] = 0.0f;                               // local_20

        float outVec[3] = {0.0f, 0.0f, 0.0f};
        mapPtr->GetTerrainPos(outVec, inVec, 0, nullptr);   // 0x14009f3e0
        pos[0] = outVec[0];
        pos[1] = outVec[1];
        pos[2] = outVec[2];
    }

    // --- v = pos - worldPos ; Normalize(v) ; return Heading(v) ---
    Math::Vector3 v(pos[0] - worldPos[0],
                    pos[1] - worldPos[1],
                    pos[2] - worldPos[2]);
    Math::Vector3 n = v.Normalize();
    return n.Heading();
}
