// engine/src/ui/statusiconui.cpp
// Clean reimplementation of StatusIconUI game logic.
//
// StatusIconUI (0x40 bytes) — status icon grid panel.
// Extends DialogController. Fields: Dialog/StatusIconUI.h (the icon element
// array at +0x30).
//
// Decompiled from Application-x64.exe.

#include "Dialog/ui_panels.h"
#include "system/Value.h"
#include "ui/UserInterface.h"
#include "object/TypeInfo.h"
#include "Dialog/exe_bodies.h"
#include "system/String.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "ui/Element.h"  // ENDGAME B.3.6 UI callers (K3)
#include "ui/DialogInstance.h"  // ENDGAME B.3.6 UI callers (K3)
#include "ui/PageElement.h"
#include "Object/factory_registration.h"
#include <cstddef>
#include "Component/StatusIconComponent.h"   // X::TypeId() (B.3.8 callers)
#include "Dialog/EntityUI.h"   // X::TypeId() (B.3.8 callers)
#include "Dialog/StatusIconUIConfig.h"   // X::TypeId() (B.3.8 callers)
#include "Entity/ComponentEntity.h"   // X::TypeId() (B.3.8 callers)

// Type ids for the two guards in OnUpdateLayout. BOTH were once wrong, and the two
// errors compounded: the outer one made the page gate unreachable, and the inner one
// would have kept it broken even after the outer was fixed.
//
//   _config (+0x10) is a StatusIconUIConfig — retail reads DAT_1401c56e0, written by
//   StatusIconUIConfig_Register (0x14014c865). It is NOT DialogController.
//
//   The dialog's controller asked for is an EntityUI — retail reads DAT_1401c4f00,
//   written by RegisterType_EntityUI (0x14014bf65). It is NOT PageToggleUI.

// ============================================================
// StatusIconUI::OnCreated (vtable +0x20) — Cache icon child elements
// ============================================================
// Finds the "icons" container element, gets its child count,
// resizes the internal array, and copies all child InternalPtrs
// into the array for fast access during OnUpdateLayout.
//
// Original: 0x14007b270.

// [4] 0x14007b270
void StatusIconUI::OnCreated() {
    // Find "icons" container element by name. The original does an
    // IsDerivedFrom check here; the tree skips it (FindElement by name
    // returns the element).
    System::String nameBuf("icons");
    UI::Element* iconsElem = _container->FindElement(nameBuf).Get();

    // Get child count
    int childCount = iconsElem->ChildCount();

    // Resize the array to hold childCount entries: Collection::Array<T>::SetSize
    // (retail 0x1400032e0, stride 8 align 8, new tail null).
    icons.SetSize(childCount, nullptr);

    // Copy each child's InternalPtr into the array
    for (int i = 0; i < icons.GetSize(); i++) {
        icons[i] = iconsElem->Child(i).Get();
    }
}

// ============================================================
// StatusIconUI::OnUpdateLayout (vtable +0x38) — Update icon visibility
// ============================================================
// Reads the StatusIconComponent's active bitfield, checks the active page
// of the dialog's EntityUI controller, and shows/hides each icon element
// accordingly.
//
// Original: 0x14007b0a0.

// [7] 0x14007b0a0
void StatusIconUI::OnUpdateLayout() {
    if (!SA_DialogEntityAlive(this)) return;
    if (icons.GetData() == nullptr)
        StatusIconUI::OnCreated();

    // The entity from the data source (IsA ComponentEntity)
    Object::Base* dataSource = _dataSource;
    ComponentEntity* entity = nullptr;
    if (::Object::IsDerivedFromNullSafe(dataSource, ComponentEntity::TypeId()))
        entity = static_cast<ComponentEntity*>(dataSource);

    // Find the StatusIconComponent (retail's finder 0x140002f90, which the tree
    // called "MeshInstance"); its billboard's activeMask is the status-icon bitfield
    // — the SAME bits that drive the 3D warning billboards. The tree's finder
    // returned null for a null entity: kept.
    StatusIconComponent* statusComp =
        entity ? entity->FindComponent<StatusIconComponent>() : nullptr;

    // Determine if icons should be visible based on page state.
    //
    // A workplace-building dialog mounts this SAME "icons" ribbon TWICE (see
    // ToolKit/resource/Template/Pasture.rsc:262-273): once into "userGroup1" (the
    // content area, bottom right) with NO config, and once into "userTitle2" (the
    // TITLE BAR) with a config — e.g. CropField.rsc:256 `StatusIconUIConfig
    // titleBuildStatus { bool _showOnMainPage = false; }`. The config's
    // _showOnMainPage is the ONLY thing that keeps the title-bar copy off the main
    // info page, so getting this gate wrong renders the stop-work icon twice.
    bool iconsActive = true;

    // _config is the optional per-mount StatusIconUIConfig, NOT a DialogController.
    UI::DialogControllerConfig* config = _config.Get();
    if (config != nullptr) {
        StatusIconUIConfig* cfgPtr = nullptr;
        if (::Object::IsDerivedFromNullSafe(config, StatusIconUIConfig::TypeId()))
            cfgPtr = static_cast<StatusIconUIConfig*>(config);

        // Retail reads the `_showOnMainPage` BOOL (+0x20 of the narrowed config, no
        // null test) and enters the page restriction when it is FALSE.
        if (!cfgPtr->_showOnMainPage) {
            // The type filter is EntityUI's id. Retail (0x14007b14b) is
            //   MOV R8D,[0x1401c4f00]   ; EntityUI type id
            //   CALL [0x140151768]      ; DialogInstance::GetController
            // Ghidra renders this call with two arguments and hides the filter.
            UI::DialogController* ctrl = _dialog->GetController(EntityUI::TypeId()).Get();

            EntityUI* entityUI = nullptr;
            if (ctrl != nullptr && ::Object::IsDerivedFromNullSafe(ctrl, EntityUI::TypeId()))
                entityUI = static_cast<EntityUI*>(ctrl);

            // Retail: iconsActive = (GetActivePage(pages) - 2U <= 1), i.e. the page
            // is pageBuild(2) or pageDestroy(3) per StandardDialog.rsc:64-74, with the
            // EntityUI (+0x40 its pages) read unguarded (0x14007b0a0).
            UI::PageElement* pageElem = entityUI->pages;
            int activePage = pageElem->GetActivePage();
            iconsActive = static_cast<uint32_t>(activePage - 2) <= 1u;
        }
    }

    // Loop through icon elements, show/hide based on bitfield
    uint32_t bitMask = 1;

    for (int i = 0; i < icons.GetSize(); i++) {
        UI::Element* iconElem = icons[i];

        // The status-icon active bitfield. Guard the read: if no StatusIconComponent
        // was found (a partial-world entity, or a building without one in standalone),
        // treat all bits as clear → every icon is hidden, which is the correct "no
        // active warnings" appearance (and never faults).
        uint32_t bitfield = statusComp ? statusComp->_geometry.activeMask : 0;
        bool bitSet = (bitfield & bitMask) != 0;

        if (bitSet && iconsActive) {
            // Show icon
            iconElem->SetFlagsOnOff(0, 1);
        } else {
            // Hide icon
            iconElem->SetFlagsOnOff(1, 0);
        }

        // Rotate bit mask left
        bitMask = (bitMask << 1) | (bitMask >> 31);
    }
}

// ============================================================
// STANDALONE promotion — real StatusIconUI controller vtable
// ============================================================
// StatusIconUI is the sub-controller that owns the "icons" group in an entity info
// panel's title bar (the imageStatusHome/Food/Cold/Job/Tool/Disease icons for a citizen,
// and the equivalent construction/warning icons for a building). In standalone it was a
// StubNop (ui_dialog_stub_factories.cpp), so NEITHER its OnCreated (+0x20 — caches the
// icon child elements) NOR its visibility update (+0x38 — shows/hides each icon per the
// StatusIconComponent's active bits) ever ran. The class now has:
//   +0x20 (idx4)  OnCreated      — cache the "icons" group children (runs once at setup)
//   +0x38 (idx7)  OnUpdateLayout — the exe's real visibility-update slot
//   +0x40 (idx8)  inherited      — DialogController::OnUpdate (table slot 0x14003a96c);
//                                  slot 7 runs every frame from DialogInstance::ComputeLayout
//                                  (B6 row 36).
//
// The StatusIconComponent's bits are populated by the citizen/building status logic
// (src/game/Component/statusiconcomponent.cpp). When they are all clear (a healthy
// citizen) every icon is hidden — the correct "no active warnings" appearance.

// create 0x140014180: MemAlloc(0x40, 8), then the ctor (DialogController ->
// Internal: AddObject, the compiler's stamps, the field initialisers).
::Object::Base* StatusIconUI::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(StatusIconUI), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) StatusIconUI());
}
// Retail's createInPlace does not build a controller in place (B8-13); the
// tree's in-place entry did, and that is kept.
::Object::Base* StatusIconUI::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) StatusIconUI());
}

DEFINE_EXE_TYPE(StatusIconUI, "UI::DialogController")

