// engine/src/ui/attendanceui.cpp
// Clean reimplementation of AttendanceUI game logic.
//
// AttendanceUI (0x38 bytes) — attendance progress bar panel.
// Extends DialogController. Fields:
//   +0x30 progress (the ProgressElement)
//
// Decompiled from Application-x64.exe.

#include "Dialog/ui_panels.h"
#include "ui/UserInterface.h"
#include "object/TypeInfo.h"
#include "Entity/exe_bodies.h"
#include "Object/factory_registration.h"
#include "system/String.h"
#include <cstdint>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "ui/Element.h"  // ENDGAME B.3.6 UI callers (K3)
#include <new>  // ENDGAME B.3.6 UI callers (K3)
#include "ui/ProgressElement.h"  // ENDGAME B.3.6 UI callers round 2 (K3)
#include "Component/AttendanceComponent.h"   // X::TypeId() (B.3.8 callers)
#include "Component/AttendanceDescription.h"   // X::TypeId() (B.3.8 callers)
#include "Entity/ComponentEntity.h"   // X::TypeId() (B.3.8 callers)
#include "Dialog/AttendanceUI.h"

// ---- AttendanceUI: PROMOTED to the real reimpl (2026-08-15) ------------------
// Symptom: the school panel read "students" with no number in front of it.
//
// Both bodies have been ported in src/game/Dialog/attendanceui.cpp since that
// file was written, and both were wired into NOTHING — the generic stub's 48
// no-op slots. This is the "stub sweep is a WIRING job" shape again
// (project_next_stub_controller_sweep): a ported body is not a reached body.
//
// The count is not a label of its own; it is TEXT PARAMETER 0 of the panel's
// ProgressElement ("{0} students" in the .rsc). With idx7 a no-op the parameter
// was never set, so the element rendered its literal remainder — the number was
// not wrong, it was absent, which is exactly what "students" with nothing in
// front of it looks like.
//
// Slot indices verified from the vtable BYTES at 0x14015b070 (11 slots wide; the
// RTTI name string "Attendan..." starts immediately after idx10):
//   idx4 (+0x20) 0x14005dd40 Slot4 — find the ProgressElement, set its MAXIMUM
//                                    from AttendanceDescription+0x20 (capacity)
//   idx7 (+0x38) 0x14005dc50 Slot7 — per-frame: set current value + text param 0
// Every other slot is one of the shared 0x14003a9xx no-op thunks, so wiring
// these two is full behavioural coverage for this class.


// ENDGAME B.3.9: ProgressElement/ComponentEntity/AttendanceDescription/AttendanceComponent
// all have a ported TypeId() now, so the old EnsureTypeIds()/Ready() lazy-cache over the
// exe .rdata globals (TypeId_ProgressElement etc.) is dead — the static always answers.

// The data source's AttendanceComponent (retail's checked cast of +0x28, then
// FindComponent<AttendanceComponent>). The generic finder the tree called returned
// null for a null entity (kept).
static AttendanceComponent* AttendanceUI_FindComponent(Object::Base* dataSource) {
    ComponentEntity* entity = ::Object::IsDerivedFromNullSafe(dataSource, ComponentEntity::TypeId())
                                  ? static_cast<ComponentEntity*>(dataSource) : nullptr;
    return entity ? entity->FindComponent<AttendanceComponent>() : nullptr;
}

// ============================================================
// AttendanceUI::Slot4 (vtable +0x20) — Setup/refresh
// ============================================================
// Finds the ProgressElement by type, stores it at +0x30. The progress maximum is
// the AttendanceDescription's capacity (+0x20).
//
// Original: 0x14005DD40.

// [4] 0x14005dd40 — find the ProgressElement, set its maximum (capacity).
void AttendanceUI::OnCreated() {
    // Find the ProgressElement under the container (+0x20)
    UI::Element* found = _container->FindElementType(UI::ProgressElement::TypeId()).Get();
    progress = ::Object::IsDerivedFromNullSafe(found, UI::ProgressElement::TypeId())
                   ? static_cast<UI::ProgressElement*>(found) : nullptr;

    AttendanceComponent* attendance = AttendanceUI_FindComponent(_dataSource);

    // The component's description, checked as an AttendanceDescription
    Description* description = attendance->_description;
    AttendanceDescription* attendDesc =
        ::Object::IsDerivedFromNullSafe(description, AttendanceDescription::TypeId())
            ? static_cast<AttendanceDescription*>(description) : nullptr;

    // Progress maximum = the capacity (+0x20)
    progress->_maxValue = (float)attendDesc->_maxCitizens;
}

// ============================================================
// AttendanceUI::Slot7 (vtable +0x38) — Update handler
// ============================================================
// The waiting-citizen count as the progress value and text parameter 0.
//
// Original: 0x14005DC50.

// [7] 0x14005dc50 — per-frame: current value + text parameter 0.
void AttendanceUI::OnUpdateLayout() {
    AttendanceComponent* attendance = AttendanceUI_FindComponent(_dataSource);

    // The queue count (+0x30)
    int currentCount = attendance->queue._size;

    progress->_currentValue = (float)currentCount;

    // Format count as text and set on progress element
    System::String countStr = System::String::Format("%d", (unsigned int)currentCount);
    progress->SetProgressTextParameter(0, countStr);
}
