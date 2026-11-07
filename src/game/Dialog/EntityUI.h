#pragma once
// ============================================================================
// src/game/Dialog/EntityUI.h — ENDGAME B.3.8 block PNL (B38_RULES D1-D5)
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §6): reflected "EntityUI", parent
//   "UI::DialogController", table 0x14015bb90, 11 slots, sizeof 0x48.
//   create 0x140011270: MemAlloc(0x48, 8), ??0DialogController, stamp, fields.
//   [0] 0x1400112d0 scalar deleting dtor (~EntityUI + MemFree on &1)
//   [1] TypeName  [2] Type  (DECLARE_EXE_TYPE)
//   [3] 0x1400672a0  RET 0
//   [4] 0x1400670d0  OnCreated
//   [6] 0x140067090  OnClosed
//   [7] 0x140066d80  OnUpdateLayout
//   [8] 0x140066ad0  OnUpdate
//   [9] 0x140067050  OnElementEvent
//   every other slot is UI::DialogController's (inherited, not declared).
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "ui/DialogController.h"
#include "Object/exe_type.h"

class ComponentEntity;           // Entity/ComponentEntity.h
namespace UI { class CheckElement; class PageElement; }  // src/engine/ui/

class EntityUI final : public UI::DialogController {
    DECLARE_EXE_TYPE(EntityUI)

public:
    EntityUI() = default;                                // 0x140011270 (ctor inlined in the factory)

    // 0x1400112d0 minus the free. The compiler chains ~DialogController (and so
    // ~Internal: RemoveObject), which the tree's hand-built slot 0 skipped.
    ~EntityUI() override = default;                               // [0] 0x1400112d0
    void Persist(IO::Stream&/*stream*/) override {}   // [3] 0x1400672a0  RET 0 in retail
    void OnCreated() override;   // [4] 0x1400670d0
    void OnClosed() override;   // [6] 0x140067090
    void OnUpdateLayout() override;   // [7] 0x140066d80
    void OnUpdate(const System::Time& time) override;   // [8] 0x140066ad0
    void OnElementEvent(Object::InternalPtr<UI::Element>* element, UI::EventId a, int32_t b,
                        int32_t c, int32_t d, int32_t e, void* userData) override;   // [9] 0x140067050

    // B4 PLAYER (bodies in Player/tool_base_trade.cpp): the pin check box.
    void SyncPin();     // 0x1400666b0 copy the box into the ToolUI, add / remove the entity in the tool manager's +0x60 set
    void SetPinned();   // 0x140066800 check the box, then SyncPin

    // ---- fields (B.4 W0-H2; retail create 0x140011270 zeroes all three,
    // OnCreated 0x1400670d0 fills them). Retail's types are InternalPtr<T>;
    // declared as plain T* (layout-identical, trivial; B.4 H5).
    ComponentEntity* entity   = nullptr;   // +0x30 the controller's +0x28 object if IsDerivedFrom ComponentEntity
    UI::CheckElement* checkPin = nullptr;  // +0x38 FindElement("checkPin"), SetElementId(100); read as CheckElement::_checked
    UI::PageElement* pages    = nullptr;   // +0x40 FindElement("pages"); read as PageElement (Get/SetActivePage)
};

static_assert(sizeof(EntityUI) == 0x48, "EntityUI is 0x48 (create 0x140011270 MemAlloc(0x48))");
static_assert(alignof(EntityUI) == 8, "qword-aligned (the vptr)");
static_assert(offsetof(EntityUI, entity) == 0x30, "EntityUI::entity @ +0x30 (0x1400670d0)");
static_assert(offsetof(EntityUI, checkPin) == 0x38, "EntityUI::checkPin @ +0x38 (0x1400670d0)");
static_assert(offsetof(EntityUI, pages) == 0x40, "EntityUI::pages @ +0x40 (0x1400670d0)");
