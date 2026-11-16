// src/game/Dialog/cemeteryui.cpp
// Clean reimplementation of CemeteryUI game logic.
//
// CemeteryUI (0x38 bytes) — cemetery progress bar panel.
// Extends DialogController. Fields:
//   +0x30 progress (the ProgressElement)
//
// Decompiled from Application-x64.exe.

#include "Dialog/ui_panels.h"
#include "ui/UserInterface.h"
#include "object/TypeInfo.h"
#include "Entity/exe_bodies.h"
#include "Dialog/exe_bodies.h"
#include "Dialog/CemeteryUI.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Game/Map.h"
#include "Object/factory_registration.h"
#include "system/String.h"
#include <cstdint>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "ui/Element.h"  // ENDGAME B.3.6 UI callers (K3)
#include <new>  // ENDGAME B.3.6 UI callers (K3)
#include "ui/ProgressElement.h"  // ENDGAME B.3.6 UI callers round 2 (K3)
#include "Component/CemeteryComponent.h"   // X::TypeId() (B.3.8 callers)
#include "Component/TombstoneComponent.h"   // X::TypeId() (B.3.8 callers)
#include "Entity/ComponentEntity.h"   // X::TypeId() (ENDGAME B.3.9)

// ============================================================
// 0x140046dd0 — the cemetery's occupied plots
// ============================================================
// Counts the plots whose map-cell occupant has a TombstoneComponent (a buried
// citizen). Retail's body walks CemeteryComponent::plots (+0x48, count +0x50,
// re-read per pass) and, for each non-null cell occupant, runs the inlined
// FindComponent<TombstoneComponent>. A member of CemeteryComponent in retail;
// kept file-local (LEFT: COMP-a's class).
static int CemeteryGetBurialCount_Impl(CemeteryComponent* cemetery) {
    int burials = 0;
    for (int i = 0; i < cemetery->plots._size; i++) {
        // The occupant is the 0x30-byte cell's +0x08 entity.
        const MapCell* cells = GameUpdateLoop::_singleton->m_game->cells.GetData();
        ComponentEntity* occupant = cells[(int64_t)cemetery->plots[i]].occupant.Get();
        if (occupant == nullptr) continue;
        if (occupant->FindComponent<TombstoneComponent>() != nullptr) burials++;
    }
    return burials;
}

// CemeteryUI: PROMOTED from a bare UI_DIALOG_STUB 2026-08-23. The panel bodies in
// src/game/Dialog/cemeteryui.cpp were fully ported but wired into NOTHING -- the stub
// vtable left slots 4 and 7 as no-ops, so neither ProgressElement text parameter was
// ever set and the panel rendered a literal "  of   graves" with both numbers blank.
// Same shape as AttendanceUI (attendanceui.cpp).

// The data source's CemeteryComponent (retail's checked cast of +0x28, then
// FindComponent<CemeteryComponent> 0x140010a00). The generic finder the tree
// called returned null for a null entity (kept).
static CemeteryComponent* CemeteryUI_FindComponent(Object::Base* dataSource) {
    ComponentEntity* entity = ::Object::IsDerivedFromNullSafe(dataSource, ComponentEntity::TypeId())
                                  ? static_cast<ComponentEntity*>(dataSource) : nullptr;
    return entity ? entity->FindComponent<CemeteryComponent>() : nullptr;
}

// ============================================================
// CemeteryUI::Slot4 (vtable +0x20) — Setup/refresh
// ============================================================
// Finds the ProgressElement by type, stores it at +0x30; the progress maximum is
// the CemeteryComponent's plot count.
//
// Original: 0x14005F090.

// [4] 0x14005f090
void CemeteryUI::OnCreated() {
    // Find the ProgressElement under the container (+0x20)
    UI::Element* found = _container->FindElementType(UI::ProgressElement::TypeId()).Get();
    progress = ::Object::IsDerivedFromNullSafe(found, UI::ProgressElement::TypeId())
                   ? static_cast<UI::ProgressElement*>(found) : nullptr;

    // Setup: bail on a dead / marked-for-removal datasource (see Slot7).
    if (!SA_DialogEntityAlive(this)) return;
    CemeteryComponent* cemetery = CemeteryUI_FindComponent(_dataSource);

    // Progress maximum = the plot count
    progress->_maxValue = (float)cemetery->plots._size;
}

// ============================================================
// CemeteryUI::Slot7 (vtable +0x38) — Update handler
// ============================================================
// Gets burial count and plot count from CemeteryComponent,
// updates progress bar value and two text parameters.
//
// Original: 0x14005EF50.

// [7] 0x14005ef50
void CemeteryUI::OnUpdateLayout() {
    // Per-frame (+0x38): bail on a dead / marked-for-removal datasource before the
    // IsDerivedFrom probe (reliable via the one-frame removal deferral, see
    // entitylist.cpp Step 0).
    if (!SA_DialogEntityAlive(this)) return;
    CemeteryComponent* cemetery = CemeteryUI_FindComponent(_dataSource);

    // Burial count as the progress current value
    int burialCount = CemeteryGetBurialCount_Impl(cemetery);
    progress->_currentValue = (float)burialCount;

    // Text param 0: burial count
    {
        System::String str = System::String::Format("%i", (unsigned int)burialCount);
        progress->SetProgressTextParameter(0, str);
    }

    // Text param 1: plot count
    int maxSlots = cemetery->plots._size;
    {
        System::String str = System::String::Format("%i", (unsigned int)maxSlots);
        progress->SetProgressTextParameter(1, str);
    }
}
