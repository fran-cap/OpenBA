#pragma once
// ============================================================================
// src/game/Dialog/CitizenUI.h — ENDGAME B.3.8 block PNL (B38_RULES D1-D5)
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §6): reflected "CitizenUI", parent
//   "UI::DialogController", table 0x14015ba00, 11 slots, sizeof 0xe8.
//   create 0x140010f80: MemAlloc(0xe8, 8), ??0DialogController, stamp, fields.
//   [0] 0x140010fd0 scalar deleting dtor (~CitizenUI + MemFree on &1)
//   [1] TypeName  [2] Type  (DECLARE_EXE_TYPE)
//   [4] 0x1400651a0  OnCreated
//   [6] 0x140065130  OnClosed
//   [7] 0x140064ae0  OnUpdateLayout
//   [8] retail inherits the base; the tree overrides it (STANDALONE DEVIATION)
//   [9] 0x1400650f0  OnElementEvent
//   every other slot is UI::DialogController's (inherited, not declared).
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "ui/DialogController.h"
#include "Object/exe_type.h"
#include "collection/Collection.h"   // Collection::Array
#include "Dialog/RatingGroup.h"      // the hearts / stars groups (+0xA0 / +0xC0)

namespace UI { class Element; class TextElement; class NumberElement; class DialogInstance; }

class CitizenUI final : public UI::DialogController {
    DECLARE_EXE_TYPE(CitizenUI)

public:
    CitizenUI() = default;                               // 0x140010f80 (ctor inlined in the factory)

    // 0x140010fd0 minus the free. The compiler runs the four icon Arrays' dtors
    // (+0xd0, +0xc0, +0xb0, +0xa0, as retail 0x140065990) and chains
    // ~DialogController (and so ~Internal: RemoveObject).
    ~CitizenUI() override = default;                              // [0] 0x140010fd0
    void OnCreated() override;   // [4] 0x1400651a0
    void OnClosed() override;   // [6] 0x140065130
    void OnUpdateLayout() override;   // [7] 0x140064ae0
    // [8] STANDALONE DEVIATION: retail inherits DialogController::OnUpdate (RET); the tree drives this panel's per-frame refresh here.
    void OnUpdate(const System::Time& time) override;
    void OnElementEvent(Object::InternalPtr<UI::Element>* element, int32_t a, int32_t b,
                        int32_t c, int32_t d, int32_t e, void* userData) override;   // [9] 0x1400650f0

    // ---- fields (B4 DLG-a: the element names are the strings OnCreated
    // 0x1400651a0 finds them by; typed with the class each is read as) --------
    UI::Element*       imageMale = nullptr;          // +0x30 "imageMale"
    UI::Element*       imageFemale = nullptr;        // +0x38 "imageFemale"
    UI::TextElement*   textName = nullptr;           // +0x40 "textName" (SetParameter)
    UI::NumberElement* numberAge = nullptr;          // +0x48 "numberAge"
    UI::TextElement*   textProfession = nullptr;     // +0x50 "textProfession" (SetText)
    UI::Element*       textEducatedYes = nullptr;    // +0x58 "textEducatedYes"
    UI::Element*       textEducatedNo = nullptr;     // +0x60 "textEducatedNo"
    UI::TextElement*   textClothing = nullptr;       // +0x68 "textClothing" (SetText)
    UI::TextElement*   textTool = nullptr;           // +0x70 "textTool" (SetText)
    UI::Element*       buttonProfession = nullptr;   // +0x78 "buttonProfession" (id 0x227)
    UI::Element*       buttonHome = nullptr;         // +0x80 "buttonHome" (id 0x226)
    UI::Element*       buttonFollow = nullptr;       // +0x88 "buttonFollow" (id 0x228)
    UI::TextElement*   textNameIs = nullptr;         // +0x90 "textNameIs" (SetParameter)
    UI::TextElement*   textAction = nullptr;         // +0x98 "textAction" (SetText)
    // +0xA0 / +0xC0: the "groupHearts" / "groupStars" icon groups (Dialog/RatingGroup.h),
    // each two Collection::Array<InternalPtr<UI::Element>> (whole icons, half icons).
    // B.4 P2 DLG-1 (behaviour: H5): Array members. Retail's ctor 0x140065a40
    // zeroes the four qwords of each group (0x140065ab9..0x140065adf): Array's
    // default ctor. Retail's dtor 0x140065990 frees +0xD0, +0xC0, +0xB0, +0xA0
    // (MemFree data, then qword 0 over data and count|capacity): the members'
    // ~Array in reverse declaration order.
    RatingGroup hearts;                              // +0xA0 (whole +0xA0, half +0xB0)
    RatingGroup stars;                               // +0xC0 (whole +0xC0, half +0xD0)
    Object::InternalPtr<UI::DialogInstance> followDialog;   // +0xE0 the open FollowDialog (null)
};

static_assert(sizeof(CitizenUI) == 0xe8, "CitizenUI is 0xe8 (create 0x140010f80 MemAlloc(0xe8))");
static_assert(alignof(CitizenUI) == 8, "qword-aligned (the vptr)");
static_assert(offsetof(CitizenUI, imageMale) == 0x30, "CitizenUI::imageMale @ +0x30");
static_assert(offsetof(CitizenUI, imageFemale) == 0x38, "CitizenUI::imageFemale @ +0x38");
static_assert(offsetof(CitizenUI, textName) == 0x40, "CitizenUI::textName @ +0x40");
static_assert(offsetof(CitizenUI, numberAge) == 0x48, "CitizenUI::numberAge @ +0x48");
static_assert(offsetof(CitizenUI, textProfession) == 0x50, "CitizenUI::textProfession @ +0x50");
static_assert(offsetof(CitizenUI, textEducatedYes) == 0x58, "CitizenUI::textEducatedYes @ +0x58");
static_assert(offsetof(CitizenUI, textEducatedNo) == 0x60, "CitizenUI::textEducatedNo @ +0x60");
static_assert(offsetof(CitizenUI, textClothing) == 0x68, "CitizenUI::textClothing @ +0x68");
static_assert(offsetof(CitizenUI, textTool) == 0x70, "CitizenUI::textTool @ +0x70");
static_assert(offsetof(CitizenUI, buttonProfession) == 0x78, "CitizenUI::buttonProfession @ +0x78");
static_assert(offsetof(CitizenUI, buttonHome) == 0x80, "CitizenUI::buttonHome @ +0x80");
static_assert(offsetof(CitizenUI, buttonFollow) == 0x88, "CitizenUI::buttonFollow @ +0x88");
static_assert(offsetof(CitizenUI, textNameIs) == 0x90, "CitizenUI::textNameIs @ +0x90");
static_assert(offsetof(CitizenUI, textAction) == 0x98, "CitizenUI::textAction @ +0x98");
static_assert(offsetof(CitizenUI, hearts) == 0xA0, "CitizenUI::hearts @ +0xA0");
static_assert(offsetof(CitizenUI, stars) == 0xC0, "CitizenUI::stars @ +0xC0");
static_assert(offsetof(CitizenUI, followDialog) == 0xE0, "CitizenUI::followDialog @ +0xE0");
