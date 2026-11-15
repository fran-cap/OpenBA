// src/game/Dialog/radiusui.cpp
// Clean reimplementation of RadiusUI game logic.
//
// RadiusUI (0x38 bytes) — displays building radius overlay.
// Extends DialogController. highlightIndex (int32_t +0x30) = radius overlay index (-1 = none).
//
// Decompiled from Application-x64.exe.

#include "Dialog/ui_panels.h"
#include <new>
#include "object/TypeInfo.h"
#include "Entity/exe_bodies.h"
#include "Dialog/exe_bodies.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Game/Map.h"
#include "Game/WorldGeometry.h"   // WorldGeometry::RemoveDecal (0x1400b0d00)
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "Entity/ComponentEntity.h"   // X::TypeId() (B.3.9 CG2)
#include "Component/RadiusDescription.h"   // X::TypeId() (B.3.9 CG2)
#include "Player/exe_bodies.h"
#include "Component/exe_bodies.h"
#include "Dialog/RadiusUI.h"
#include "Component/ComponentDescription.h"
#include "Component/MapComponent.h"

// ============================================================
// RadiusUI::Slot5 (vtable +0x28) — Calculate and store radius overlay
// ============================================================
// When the panel is shown, finds the RadiusDescription among the entity's
// description's sub-descriptions, gets the MapComponent, calculates the center
// tile index, and stores the radius overlay display index at +0x30.
//
// Original: 0x140077650.

// [5] 0x140077650
void RadiusUI::OnOpened() {
    if (!SA_DialogEntityAlive(this)) {
        return;
    }

    // Get entity (the data source, +0x28), check IsA ComponentEntity
    Object::Base* entity = _dataSource;
    uint32_t entityTypeId = ComponentEntity::TypeId();
    ComponentEntity* checkedEntity = nullptr;
    if (::Object::IsDerivedFromNullSafe(entity, entityTypeId)) {
        checkedEntity = static_cast<ComponentEntity*>(entity);
    }

    // SA-only guard (retail derefs this unguarded): retail can only route a RadiusUI
    // controller to a live ComponentEntity, but our promoted vtable is also dispatched
    // from the harness-driven panel open, where the data source (+0x28) can still be
    // null on the first Slot5 before the framework seats it.
    if (!checkedEntity) { this->highlightIndex = -1; return; }

    // The entity's description (ComponentEntity::_description, +0x28)
    ComponentDescription* subObj = checkedEntity->_description;

    // Search the description's sub-descriptions for the RadiusDescription
    // ENDGAME B.3.9 CG2: type-id cell 0x1401c4870 is RadiusDescription's
    // (RadiusDescription::TypeId(); Table 1).
    uint32_t radiusTypeId = RadiusDescription::TypeId();
    RadiusDescription* radiusComp = nullptr;
    int count = subObj->_subDescCount;
    Description** arrayBase = subObj->_subDescs;
    for (int i = 0; i < count; i++) {
        Description* comp = arrayBase[i];
        // vtable[2] (offset 0x10) = GetTypeId. Ghidra prints this as `(...)()` — that
        // means "args unknown", NOT "no args"; the callee still reads `this` from RCX.
        int typeId = comp->Type();
        if (typeId == (int)radiusTypeId) {
            if (::Object::IsDerivedFromNullSafe(comp, radiusTypeId)) {
                radiusComp = static_cast<RadiusDescription*>(comp);
            }
            break;
        }
    }

    // Find the placement component on the entity (0x1401c79e0, MapComponent).
    MapComponent* buildComp = checkedEntity->FindComponent<MapComponent>();

    // SA-only guards (retail derefs both unguarded). Retail reaches Slot5 only from a
    // .rsc panel whose entity is a placed building carrying both components; our
    // promoted controller is also mounted by the harness panel-open path, which can
    // select an entity whose description has no RadiusDescription. Without this,
    // 0x140055430 reads radiusComp+0x20 off a null pointer.
    if (!buildComp || !radiusComp) {
        this->highlightIndex = -1;
        return;
    }

    // Calculate center tile index from the placement component's footprint rect
    // (tileX0 / tileY0 the corner, tileX1 / tileY1 the extent, int16 each):
    int gridSize = GameUpdateLoop::_singleton->m_game->size;
    int centerY = (int)buildComp->tileY0 + ((int)buildComp->tileY1 >> 1);
    int centerX = (int)buildComp->tileX0 + ((int)buildComp->tileX1 >> 1);
    int tileIndex = centerY * gridSize + centerX;

    // Calculate radius and store display index
    // 0x140055430 RadiusDescription::AddHighlight.
    this->highlightIndex = radiusComp->AddHighlight(tileIndex);

}

// ============================================================
// RadiusUI::Slot6 (vtable +0x30) — Remove radius overlay
// ============================================================
// When the panel is hidden/closed, removes the active radius overlay
// from the terrain (WorldGeometry::RemoveDecal) and resets the index to -1.
//
// Original: 0x140077600, 28 bytes.

// [6] 0x140077600 — unconditional: it resets +0x30 to -1 in both arms, and the
// manager pointer read is guarded by highlightIndex >= 0 inside the body.
void RadiusUI::OnClosed() {
    if (this->highlightIndex >= 0) {
        // Game/World singleton +0x110 = the terrain (GameUpdateLoop::m_worldGeometry)
        WorldGeometry* radiusMgr = GameUpdateLoop::_singleton->m_worldGeometry;
        // 0x1400b0d00 WorldGeometry::RemoveDecal.
        radiusMgr->RemoveDecal(this->highlightIndex);
    }
    this->highlightIndex = -1;
}
