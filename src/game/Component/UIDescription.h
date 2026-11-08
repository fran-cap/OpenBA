#pragma once
// ============================================================================
// src/game/Component/UIDescription.h — ENDGAME B.3.8 K1 (B38_RULES.md D1-D5, §2.2)
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §6):
//   reflected "UIDescription", parent "Description", ti 0x1401c7f90
//   create 0x1400395b0: MemAlloc(0x48, 8), `??0External@Object@@`, the
//   Description stamp, the leaf stamp, the field defaults. createInPlace 0x140039620.
//   table 0x140164a70, 10 slots:
//     [0] 0x140039700 scalar deleting dtor   [1] 0x1400395a0 TypeName   [2] 0x140039590 Type
//     [3] 0x14012dfa0 GetFileVersion = 4   [4] External::Compile (import, inherited)
//     [5] 0x14012de80 Serialize (`MOV AL,1`)
//     [6] 0x140039680 Reconstruct   [7] 0x140001640 Destruct (shared)
//     [8] 0x1400396e0 GetComponentType: UIComponent's TypeId
//     [9] 0x140039620 Construct (folded with createInPlace)
//   sizeof 0x48.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Component/Description.h"
#include "Object/exe_type.h"
#include "object/Type.h"
#include "system/String.h"
namespace UI { class Dialog; class DialogInstance; class ElementDescription; class DialogControllerConfig; }
namespace Object { class Base; }

// +0x30 element (0x28) of UIDescription (Serialize 0x14012de80, dtor 0x140039700 /
// 0x1400397d0): Object::Type, three ExternalPtrs (0x140027ad0, 0x140039840,
// 0x140027ad0; their classes not settled here), a String.
struct UIDescriptionEntry {
    Object::Type   type;         // +0x00 (Object::Type::Serialize, import 0x1401516b0)
    uint8_t        _pad04[4];    // +0x04
    UI::ElementDescription*     element;  // +0x08 ExternalPtr (0x140027ad0): the controller's element (B4 DLG-d: 0x14012e100)
    UI::DialogControllerConfig* config;  // +0x10 ExternalPtr (0x140039840): its config (B4 DLG-d: 0x14012e100)
    UI::ElementDescription*     tabButton;  // +0x18 ExternalPtr (0x140027ad0): the tab button (B4 DLG-d: 0x14012e100)
    System::String name;         // +0x20
};
static_assert(sizeof(UIDescriptionEntry) == 0x28, "UIDescriptionEntry stride 0x28");
static_assert(offsetof(UIDescriptionEntry, type) == 0x0, "UIDescriptionEntry::type");
static_assert(offsetof(UIDescriptionEntry, _pad04) == 0x4, "UIDescriptionEntry::_pad04");
static_assert(offsetof(UIDescriptionEntry, element) == 0x08, "UIDescriptionEntry +0x08");
static_assert(offsetof(UIDescriptionEntry, config) == 0x10, "UIDescriptionEntry +0x10");
static_assert(offsetof(UIDescriptionEntry, tabButton) == 0x18, "UIDescriptionEntry +0x18");
static_assert(offsetof(UIDescriptionEntry, name) == 0x20, "UIDescriptionEntry +0x20");

class UIDescription final : public Description {
    DECLARE_EXE_TYPE(UIDescription)

public:
    UIDescription();

    ~UIDescription() override;                                             // [0] 0x140039700
    int32_t           GetFileVersion() const override { return 4; }    // [3] 0x14012dfa0
    bool              Serialize(IO::Stream& stream) override;                    // [5] 0x14012de80
    Object::External* Reconstruct() override;                              // [6] 0x140039680
    void              Destruct() override;                                 // [7] 0x140001640
    Object::Type*     GetComponentType(Object::Type* ret) const override;  // [8] 0x1400396e0
    virtual Object::External* Construct();                                 // [9] 0x140039620

    // ---- fields (PUBLIC: the components read them at raw offsets today) ----
    int32_t _settingsBufferSize;                           // +0x20 serialized LAST; slot 8 builds a UIComponent only when > 0
    uint8_t _pad24[4];                          // +0x24 never written by the ctor
    UI::Dialog* _dialog;                        // +0x28 ExternalPtr<UI::Dialog> (0x140028650: ?_typeInfo@Dialog@UI@@ gate)
    UIDescriptionEntry* _entries;               // +0x30 element array data
    int32_t _entryCount;                        // +0x38
    int32_t _entryCap;                          // +0x3c
    int32_t _displayPage;                           // +0x40
    uint8_t _pad44[4];                          // +0x44 never written by the ctor

    // B4 DLG-d (appended): 0x14012e100, ToolUI::OnCreated's call: adds each
    // entry's controller to the dialog (a tab page when the named element is a
    // TabElement). Body in Dialog/toolui.cpp.
    void AddControllers(UI::DialogInstance* const* dialog, Object::Base* dataSource) const;
};

static_assert(sizeof(UIDescription) == 0x48, "UIDescription is 0x48 (create 0x1400395b0 MemAlloc(0x48))");
static_assert(offsetof(UIDescription, _pad24) == 0x24, "UIDescription::_pad24");
static_assert(offsetof(UIDescription, _pad44) == 0x44, "UIDescription::_pad44");
static_assert(alignof(UIDescription) == 8, "UIDescription alignment");
static_assert(offsetof(UIDescription, _settingsBufferSize) == 0x20, "UIDescription::_settingsBufferSize @ +0x20");
static_assert(offsetof(UIDescription, _dialog) == 0x28, "UIDescription::_dialog @ +0x28");
static_assert(offsetof(UIDescription, _entries) == 0x30, "UIDescription::_entries @ +0x30");
static_assert(offsetof(UIDescription, _entryCount) == 0x38, "UIDescription::_entryCount @ +0x38");
static_assert(offsetof(UIDescription, _entryCap) == 0x3c, "UIDescription::_entryCap @ +0x3c");
static_assert(offsetof(UIDescription, _displayPage) == 0x40, "UIDescription::_displayPage @ +0x40");
