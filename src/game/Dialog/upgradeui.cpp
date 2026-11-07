// src/game/Dialog/upgradeui.cpp
// Clean reimplementation of UpgradeUI game logic.
//
// UpgradeUI (0x38 bytes) — upgrade panel controller.
// Extends DialogController (0x30 base).
// +0x30 = the upgrade button (InternalPtr<ButtonElement>)
//
// Decompiled from Application-x64.exe.

#include "Dialog/ui_panels.h"
#include "system/String.h"
#include "system/Value.h"
#include "ui/UserInterface.h"
#include "ui/DialogInstance.h"
#include "object/TypeInfo.h"
#include "Dialog/exe_bodies.h"
#include "Player/exe_bodies.h"   // B3R: RemovalCalc_StateMachine_dfa0
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "ui/Element.h"  // ENDGAME B.3.6 UI callers (K3)
#include "ui/Sprite.h"  // ENDGAME B.3.6 UI callers (K3)
#include "ui/SpriteSheet.h"  // ENDGAME B.3.6 UI callers (K3)
#include "object/External.h"  // ENDGAME B.3.6 UI callers (K3)
#include <new>  // ENDGAME B.3.6 UI callers (K3)
#include "ui/ImageElement.h"  // ENDGAME B.3.6 UI callers round 2 (K3)
#include "Object/factory_registration.h"
#include "Dialog/UpgradeUIConfig.h"   // X::TypeId() (B.3.8 callers)
#include "Entity/ComponentEntity.h"   // X::TypeId() (B.3.8 callers)
#include "ui/ButtonElement.h"   // X::TypeId() (B.3.9 CG2)
#include "Component/ToolbarDescription.h"   // X::TypeId() (B.3.9 CG2)
#include "Component/ComponentDescription.h"
#include "Component/DestroyComponent.h"
#include "Dialog/UpgradeUI.h"

namespace {

// The bound config narrowed to an UpgradeUIConfig (retail's IsDerivedFrom on
// DWORD_1401c5868), null otherwise.
UpgradeUIConfig* UpgradeConfig(UpgradeUI* self) {
    UI::DialogControllerConfig* cfg = self->_config.Get();
    return ::Object::IsDerivedFromNullSafe(cfg, UpgradeUIConfig::TypeId())
        ? static_cast<UpgradeUIConfig*>(cfg) : nullptr;
}

}  // namespace

// ============================================================
// UpgradeUI::OnCreated (0x140086d90)
// ============================================================
// Narrows the container to the upgrade button, stamps its id, puts the
// target template's ToolbarDescription icon on the button's image child, and
// hands the button the target template as its user data.
void UpgradeUI::OnCreated() {
    // SA guard: only safe when the bound config is an UpgradeUIConfig with a
    // target template (the stub config zero-filled it before the real port).
    UpgradeUIConfig* upgradeConfig = UpgradeConfig(this);
    if (!upgradeConfig || upgradeConfig->_target == nullptr) return;

    UI::Element* container = _container;
    button = ::Object::IsDerivedFromNullSafe(container, UI::ButtonElement::TypeId())
        ? static_cast<UI::ButtonElement*>(container) : nullptr;
    button->SetElementId(33000);

    // The button's child 4 is its ImageElement.
    UI::Element* child = button->Child(4).Get();
    UI::ImageElement* image = ::Object::IsDerivedFromNullSafe(child, UI::ImageElement::TypeId())
        ? static_cast<UI::ImageElement*>(child) : nullptr;

    // The target template (UpgradeUIConfig +0x20, an ExternalPtr<ComponentDescription>).
    ComponentDescription* target = upgradeConfig->_target;

    // Find the ToolbarDescription among the template's descriptions.
    ToolbarDescription* toolbar = nullptr;
    if (target && target->_subDescCount > 0) {
        int compCount = target->_subDescCount;
        for (int i = 0; i < compCount; i++) {
            Object::Base* comp = target->_subDescs[i];
            if (!comp) continue;

            if (comp->Type() == ToolbarDescription::TypeId()) {
                toolbar = ::Object::IsDerivedFromNullSafe(comp, ToolbarDescription::TypeId())
                    ? static_cast<ToolbarDescription*>(comp) : nullptr;
                break;
            }
        }
    }

    // SA guard: retail data guarantees a ToolbarDescription on the upgrade
    // target template; skip the icon when the standalone template lacks one.
    if (!toolbar) return;

    UI::Sprite sprite(Object::ExternalPtr<UI::SpriteSheet>(toolbar->_spriteSheet),
                      toolbar->_name);
    image->SetSprite(&sprite);

    button->SetUserData(target);
}

// ============================================================
// UpgradeUI::OnElementEvent (0x140086c90)
// ============================================================
// On the button's click (id 33000), writes the upgrade target into the
// entity's DestroyComponent and starts the upgrade.
void UpgradeUI::OnElementEvent(Object::InternalPtr<UI::Element>* element, UI::EventId eventType, int32_t eventId,
                               int32_t /*c*/, int32_t /*d*/, int32_t /*e*/, void* /*userData*/) {
    if (!element) return;

    // Only handle button clicks (msg 0x12) with ID 33000
    if (eventType != UI::EventId::Activated || eventId != 33000)
        return;

    // The event source must be our button.
    if (element->Get() != button)
        return;

    ::Object::Base* dataSource = _dataSource;
    if (!dataSource)
        return;

    // Don't start an upgrade on a building that is already dead / marked-for-removal
    // (the panel can linger one marked-but-alive frame under the removal deferral —
    // entitylist.cpp Step 0 — during which a click must not re-arm its DestroyComponent).
    if (!SA_DialogEntityAlive(this))
        return;

    if (!::Object::IsDerivedFromNullSafe(dataSource, ComponentEntity::TypeId()))
        return;
    ComponentEntity* entity = static_cast<ComponentEntity*>(dataSource);

    DestroyComponent* destroyComp = entity->FindComponent<DestroyComponent>();
    if (!destroyComp) {
        std::fprintf(stderr, "[UpgradeUI] OnRemove: no DestroyComponent on "
                     "entity %p — upgrade aborted\n", static_cast<void*>(entity));
        std::fflush(stderr);
        return;
    }

    UpgradeUIConfig* upgradeConfig = UpgradeConfig(this);
    if (!upgradeConfig)
        return;

    // The upgrade target: DestroyComponent +0x60 = the config's target template.
    destroyComp->upgradeTarget = upgradeConfig->_target;

    // Start the upgrade: pump the BuildState state machine. 0x14011dfa0 is the
    // generic driver ("StartUpgrade" was a misnomer); the disasm-verified port
    // is RemovalCalc_StateMachine_dfa0 (keystone_a_removalcalcpath.cpp).
    std::fprintf(stderr, "[UpgradeUI] OnRemove: target=%p written to "
                 "destroyComp=%p +0x60, pumping SM\n",
                 upgradeConfig->_target, static_cast<void*>(destroyComp));
    std::fflush(stderr);
    destroyComp->RunStateMachine();   // 0x14011dfa0
}

// ============================================================
// Standalone promotion — real UpgradeUI vtable (2026-07-20)
// ============================================================
// Retail vtable @ 0x14015df00 (11 slots): overrides 0,1,2,4,7,9. Slot 7
// (0x140086d80) is a 3-byte RET 0 no-op, slots 3,5,6,8,10 are inherited
// UI::DialogController base thunks. OnCreated (icon + button wiring) is driven
// once from slot 8 (OnUpdate) as a self-heal if the framework never calls
// slot 4; slot 9 is the click handler.

// [8] STANDALONE DEVIATION (retail inherits the base slot) — the tree's wiring, kept.
void UpgradeUI::OnUpdate(const System::Time& /*time*/) {
    if (button == nullptr) UpgradeUI::OnCreated();
}


// create 0x140016b30: MemAlloc(0x38, 8), then the ctor (DialogController ->
// Internal: AddObject, the compiler's stamps, the field initialisers).
::Object::Base* UpgradeUI::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(UpgradeUI), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) UpgradeUI());
}
// Retail's createInPlace does not build a controller in place (B8-13); the
// tree's in-place entry did, and that is kept.
::Object::Base* UpgradeUI::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) UpgradeUI());
}

DEFINE_EXE_TYPE(UpgradeUI, "UI::DialogController")
