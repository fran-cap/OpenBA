// src/game/Player/tool_base_createbar.cpp
// ============================================================================
// CreateBar populate + position (GUI Keystone F, Phase 3B item 2)
// ============================================================================
// Faithful 1:1 reimplementation of the in-game building-placement "CreateBar"
// update path. PlacementTool::Serialize (and the Drag tools) call this every
// frame while a tool is active; it fills the CreateBar dialog's "textSize" /
// "textStatus" / "imageIcon" elements with the footprint size + cost/status
// text + red/white validity tint, projects the building footprint to screen
// space, and repositions (smoothed) the CreateBar dialog over that footprint.
//
// Ported exe functions:
//   operator_impl_fc60          0x1400ffc60  -> UpdateToolUI_fc60   (main)
//   operator_impl_fc60_sub_e600 0x14005e600  -> UpdateToolUI_e600   ("textSize")
//   operator_impl_e420          0x14005e420  -> UpdateToolUI_e420   ("textStatus")
//   FUN_14005e700               0x14005e700  -> UpdateToolUI_e700   (size-region elems)
//   FUN_14005e360               0x14005e360  -> UpdateToolUI_e360   ("imageIcon" tint)
//   operator_helper_ebb0        0x1400febb0  -> UpdateToolUI_ebb0   (box corners)
//
// Sub-callees resolved (not re-ported here):
//   CharacterComponent_impl_ec70 0x1400fec70  (REG) — build 4 footprint corners
//   FUN_1400fe960                0x1400fe960  (REG) — project corners -> screen rect
//   Runtime-x64.dll exports (engine_funcs + local ResolveExport for the by-value
//   Math/Volume/String helpers not present in engine_funcs.h).
//
// ABI NOTES (verified against the Ghidra disassembly — get these wrong and it
// hardfaults on the Microsoft x64 calling convention):
//   * MEMBER functions that return a struct BY VALUE use a hidden return buffer:
//     RCX=this, RDX=hidden_retbuf, R8+=args. This applies to:
//        Vector3::Min          ?Min@Vector3@Math@@QEBA?AV12@AEBV12@@Z
//        Transform::operator*  ??DTransform@Math@@QEBA?AVVector3@1@AEBV21@@Z
//        UI::String -> System::String  ??BString@UI@@QEBA?AV0System@@XZ
//        DialogInstance::GetController (engine_funcs, retval-2nd)
//     -> the returned value is written to the retbuf AND returned in RAX.
//   * fn_UIString_Ctor ALWAYS takes 3 args (retbuf, stringTable, stringName);
//     Ghidra renders it with 2. e420 supplies R8 = the string-name arg.
//   * UI::String is 16 bytes; System::String is 8 bytes (COW refcounted). The
//     empty-string idiom is (*p==0 || *(int*)(*p-8)==0).
//   * type-id .rdata globals (DAT_1401c4d40 = "BuildRequirementUI" controller,
//     DWORD_1401c7f58 = "ToolbarDescription" component, ImageElement typeinfo)
//     read as 0 in standalone — filled lazily via ResolveAddr-then-GetTypeIdByName.

#include "ui/UserInterface.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "math/Math.h"
#include "system/String.h"
#include "Object/factory_registration.h"   // ::Object::GetTypeIdByName
#include "Dialog/BuildRequirementUI.h"
#include "Component/ToolbarDescription.h"
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "object/External.h"   // ENDGAME B.3.6 UI callers (K4)
#include "ui/DialogInstance.h"   // ENDGAME B.3.6 UI callers (K4)
#include "ui/Element.h"        // ENDGAME B.3.6 UI callers (K4)
#include "ui/String.h"         // ENDGAME B.3.6 UI callers (K4)
#include "ui/TextElement.h"    // ENDGAME B.3.6 UI callers (K4)
#include "ui/ImageElement.h"   // UI::ImageElement::TypeId() (ENDGAME B.3.9 TR)
#include <new>
#include "Player/exe_bodies.h"
#include "volume/Volume.h"
#include "object/TypeInfo.h"
#include "Component/ComponentDescription.h"
#include "Game/Map.h"
#include "Game/PlayerToolManager.h"
#include "ui/DialogController.h"
#include "Entity/ComponentEntity.h"   // B4 S4 RT2: InternalPtr<ComponentEntity>::Get() needs the class complete
#include "ext/registry.h"           // Ext::Fix().ui_scale_layout (the CreateBar position seam)

namespace {

// System::String empty test: (*p==0) || (*(int*)(*p - 8) == 0) — GetLength's read.
inline bool StringIsEmpty(const System::String& s) {
    return (s._data == nullptr) || (s.GetLength() == 0);
}

// The dialog's element of that name as the TextElement the CreateBar holds there.
UI::TextElement* FindText(UI::DialogInstance* dialog, const System::String& name) {
    UI::Element* elem = dialog->FindElement(name).Get();
    return static_cast<UI::TextElement*>(elem);   // the CreateBar's text labels
}

}  // namespace

// ============================================================================
// BuildRequirementUI::SetSizeText (0x14005e600) — set the "textSize" element
// ============================================================================
// Finds "textSize" on the controller's DialogInstance (+0x18), sets the string as
// text-parameter 0, and hides/shows the element by whether the string is empty.
void BuildRequirementUI::SetSizeText(const System::String* str) {
    System::String nameStr("textSize");

    UI::TextElement* elem = FindText(_dialog, nameStr);

    if (elem) {
        elem->SetParameter(0, *str);

        // hide/show by empty string
        System::String tmp(*str);
        bool isEmpty = StringIsEmpty(tmp);

        elem->SetFlagsOnOff(isEmpty ? 1u : 0u, isEmpty ? 0u : 1u);
    }
}

// ============================================================================
// BuildRequirementUI::SetStatusText (0x14005e420) — set the "textStatus" element
// ============================================================================
// Scans the controller's description (+0x30) for the ToolbarDescription, then finds
// "textStatus" and sets:
//   SetText(elem, UIString(desc.stringTable, costString))
//   SetParameter(elem, 0, (System::String)UIString(desc.stringTable, desc+0x40))
//   hide/show by whether costString is empty.
void BuildRequirementUI::SetStatusText(const System::String* costStr) {
    ComponentDescription* descEntity = description;
    if (!descEntity) {
        return;
    }

    // Find the ToolbarDescription (the sub-description list: base +0x28, count +0x30).
    ToolbarDescription* comp = nullptr;
    {
        int count = descEntity->_subDescCount;
        uint32_t tid = ToolbarDescription::TypeId();
        for (int i = 0; i < count; i++) {
            Description* c = descEntity->_subDescs[i];
            if (c->Type() == tid) {
                Description* obj = descEntity->_subDescs[i];
                comp = Object::IsDerivedFromNullSafe(obj, tid) ? static_cast<ToolbarDescription*>(obj)
                                                              : nullptr;
                break;
            }
        }
    }

    System::String nameStr("textStatus");
    UI::TextElement* elem = FindText(_dialog, nameStr);
    if (!elem) {
        return;
    }

    // Defensive: the original assumes the ToolbarDescription always exists and
    // unconditionally derefs comp+0x30 (a guard the tree keeps; known_port_issues 14d).
    if (!comp) {
        return;
    }

    UI::StringTable* stringTable = comp->_stringTable;


    // UIString_A = UIString(stringTable, costString); SetText(elem, UIString_A).
    UI::String uiA(Object::ExternalPtr<UI::StringTable>(stringTable), *costStr);
    UI::String uiA_copy(uiA);
    elem->SetText(&uiA_copy);

    // UIString_B = UIString(stringTable, desc+0x40); SetParameter(elem, 0, (SysStr)UIString_B).
    UI::String uiB(Object::ExternalPtr<UI::StringTable>(stringTable), comp->_str2);
    System::String sysB(uiB.operator System::String());
    elem->SetParameter(0, sysB);

    // hide/show by whether costString (UIString_A) is empty.
    System::String sysA(uiA.operator System::String());
    System::String sysA_copy(sysA);
    bool isEmpty = StringIsEmpty(sysA_copy);

    elem->SetFlagsOnOff(isEmpty ? 1u : 0u, isEmpty ? 0u : 1u);
}

// ============================================================================
// BuildRequirementUI::SetSizeMultiplier (0x14005e700) — the per-cost labels
// ============================================================================
// +0x38 = the size multiplier (param_8). For each of the +0x58 costs (label element
// array +0x40, quantity array +0x50): text = "%i" of quantity[i] * multiplier.
void BuildRequirementUI::SetSizeMultiplier(uint32_t param_8) {
    field_0x38 = param_8;
    int count = counts._size;
    if (count <= 0) return;
    // Retail indexes the label-element array (+0x40, count at +0x48) using the REQUIREMENT
    // count (+0x58) and never null-checks the element — safe only because the two arrays
    // move together there (one labelBuild%i element per requirement). In standalone they can
    // diverge, and the bridge tool's first drag faulted in UI::TextElement::SetParameter on
    // a null element. Log the shape once and skip null entries rather than fault (a guard
    // retail lacks; known_port_issues 14d).
    {
        static bool s_logged = false;
        if (!s_logged) {
            s_logged = true;
            fprintf(stderr, "[e700] ctrl=%p reqCount(+0x58)=%d labelCount(+0x48)=%u "
                    "labelArr(+0x40)=%p sizeArr(+0x50)=%p\n",
                    static_cast<void*>(this), count, static_cast<uint32_t>(labels._size),
                    static_cast<void*>(labels._data), static_cast<void*>(counts._data));
            fflush(stderr);
        }
    }
    for (int i = 0; i < count; i++) {
        int mult = static_cast<int>(field_0x38);
        int val = counts._data[i] * mult;

        System::String s = System::String::Format("%i", static_cast<int>(static_cast<uint32_t>(val)));

        UI::Element** elemArr = labels._data;
        UI::Element* elem = nullptr;
        if (elemArr) {
            elem = elemArr[i];
        }
        if (elem) {
            static_cast<UI::TextElement*>(elem)->SetParameter(0, s);   // the labelBuild%i text labels
        } else {
            // Loud: retail would have faulted here, so this means our controller state is
            // wrong upstream (label array shorter than the requirement count).
            static int s_warned = 0;
            if (s_warned < 4) {
                s_warned++;
                fprintf(stderr, "[e700] WARN: null label element at i=%d (reqCount=%d, "
                        "labelArr=%p) — skipping; upstream BuildRequirementUI state is short\n",
                        i, count, static_cast<void*>(elemArr));
                fflush(stderr);
            }
        }
    }
}

// ============================================================================
// BuildRequirementUI::SetIconColor (0x14005e360) — tint the "imageIcon" element
// ============================================================================
// Finds "imageIcon" on the controller's DialogInstance, type-checks it as a
// UI::ImageElement, and calls its virtual slot +0x50 (SetColor) with the tint
// (0xffffffff valid / 0xffff0000 invalid).
void BuildRequirementUI::SetIconColor(uint32_t color) {
    System::String nameStr("imageIcon");

    UI::Element* obj = _dialog->FindElement(nameStr).Get();

    uint32_t imgTid = UI::ImageElement::TypeId();
    UI::Element* ptr = (obj && Object::IsDerivedFromNullSafe(obj, imgTid)) ? obj : nullptr;

    if (ptr != nullptr) {
        // virtual slot +0x50 = SetColor(this, color).
        ptr->SetColor(color);
    }
}

// ============================================================================
// UpdateToolUI_ebb0 (0x1400febb0) — box corners -> transformed Vector3[8]
// ============================================================================
// Fills outCorners[0..7] from the placement Box, clamping each corner componentwise
// to (1e6, 1e6, 2.5) then applying the mesh Transform. Returns 8 (the corner count).
// param_1 is a dummy the original never reads.
extern "C" uint32_t UpdateToolUI_ebb0(const void* /*dummy*/, const ::Box* box,
                                      const Math::Transform* transform, Math::Vector3* outCorners) {
    RtVolume_Box_GetCorners(box, outCorners);
    const Math::Vector3 clampConst(1000000.0f, 1000000.0f, 2.5f);  // 0x49742400,0x49742400,0x40200000
    Math::Vector3* corner = outCorners;
    for (int i = 0; i < 8; i++) {
        *corner = corner->Min(clampConst);
        *corner = transform->operator*(*corner);
        corner += 1;
    }
    return 8;
}

// ============================================================================
// UpdateCreateBar (0x1400ffc60) — populate + position the CreateBar
// ============================================================================
// param_1 = the tool's owner, the PlayerToolManager (Tool +0x10): +0x00 the open
// dialog, +0x08/+0x0C the smoothed x/y coords OpenToolDialog seeds to -1.0.
// (PlayerToolManager.h is block GAME-b's: a free function over the typed manager.)
void PlayerToolManager::UpdateCreateBar(int param_2, int param_3,
                     const System::String* param_4, char param_5, char param_6, char param_7,
                     int param_8, int param_9, int param_10, const ::Box* param_11,
                     const Math::Transform* param_12) {
    // No dialog open -> nothing to update.
    if (field_0x00 == nullptr) return;
    UI::DialogInstance* dialog = field_0x00;

    // Resolve the CreateBar controller (BuildRequirementUI) from the dialog.
    uint32_t ctrlTid = BuildRequirementUI::TypeId();
    UI::DialogController* found = dialog->GetController(ctrlTid).Get();
    BuildRequirementUI* ctrl = nullptr;
    if (found != nullptr) {
        if (Object::IsDerivedFromNullSafe(found, ctrlTid)) ctrl = static_cast<BuildRequirementUI*>(found);
    }
    // One-shot diagnostic: confirm the CreateBar controller resolved (type-ids correct)
    // — else the populate silently no-ops here.
    {
        static bool s_logged = false;
        if (!s_logged) {
            s_logged = true;
            fprintf(stderr, "[fc60] ctrlTid=%u ctrl=%p (null => wrong type-id, populate skipped)\n",
                    ctrlTid, static_cast<void*>(ctrl));
            fflush(stderr);
        }
    }
    if (ctrl == nullptr) return;  // no live controller

    // --- Size text (controller-cached) ---
    if (param_6 == 0) {
        System::String fmtBuf;                         // System::String from Format
        if (param_7 == 0) {
            fmtBuf = System::String::Format("%ix%i",
                                 static_cast<int>(static_cast<uint32_t>(param_2)),
                                 static_cast<int>(static_cast<uint32_t>(param_3)));
        } else {
            fmtBuf = System::String::Format("%i",
                                 static_cast<int>(static_cast<uint32_t>(param_2)));
        }
        ctrl->SetSizeText(&fmtBuf);
        ctrl->SetSizeMultiplier(static_cast<uint32_t>(param_8));
    } else {
        System::String emptyStr;                       // null System::String -> clears "textSize"
        ctrl->SetSizeText(&emptyStr);
    }

    // --- Status text (cost) + icon tint ---
    ctrl->SetStatusText(param_4);
    uint32_t color = (param_5 != 0) ? 0xffffffffu : 0xffff0000u;
    ctrl->SetIconColor(color);

    // --- Build the 8 footprint corners (default-constructed = zero) ---
    Math::Vector3 corners[8];
    uint32_t cornerCount = 0;
    const bool boxPath = (param_9 < 0) || (param_10 < 0);
    if (boxPath) {
        // Single building / no drag: use the mesh Box, clamped + transformed, then
        // an extra componentwise Min against (1e6, 1e6, 3.0).
        cornerCount = UpdateToolUI_ebb0(nullptr, param_11, param_12, corners);
        const Math::Vector3 clampConst(1000000.0f, 1000000.0f, 3.0f);  // 0x49742400,0x49742400,0x40400000
        for (int i = 0; i < 8; i++) {
            corners[i] = corners[i].Min(clampConst);
        }
    } else {
        // Drag rectangle: project start/end cells to world corners.
        int mapW = GameUpdateLoop::_singleton->m_game->size;
        if (mapW != 0) {
            cornerCount = PlayerScreenRect_BuildCorners_ec70(
                static_cast<uint64_t>(static_cast<uint32_t>(param_9 % mapW)),  // p1 (unused by ec70)
                param_9 % mapW,     // col_start
                param_9 / mapW,     // row_start
                param_10 % mapW,    // col_end
                param_10 / mapW,    // row_end
                corners);
        }
    }

    // --- Project corners into a screen rectangle ---
    struct Rectangle rect;   // Volume::Rectangle {min.xy, max.xy}
    RtVolume_Rectangle_Ctor(&rect);
    uint8_t ok = PlayerScreenRect_AccumulateRect_fe960(0, cornerCount, corners, &rect);
    if (ok == 0) return;

    // --- Read the dialog root bounds (+0x68 left, +0x6c top, +0x70 right, +0x74 bottom) ---
    UI::Element* rootElem = dialog->GetRoot().Get();
    {
        static bool s_rootLogged = false;
        if (!s_rootLogged) { s_rootLogged = true;
            fprintf(stderr, "[fc60] post-fe960: rect=(%.0f,%.0f,%.0f,%.0f) rootElem=%p\n",
                    rect.min.x, rect.min.y, rect.max.x, rect.max.y, static_cast<void*>(rootElem));
            fflush(stderr);
        }
    }
    if (!rootElem) return;
    int left   = rootElem->_layoutLeft;
    int top    = rootElem->_layoutTop;
    int right  = rootElem->_layoutRight;
    int bottom = rootElem->_layoutBottom;

    // --- Compute the centered target position from the projected rect ---
    const float minX = rect.min.x, minY = rect.min.y, maxX = rect.max.x, maxY = rect.max.y;
    float cx = (minX + maxX) * 0.5f;                   // DAT_140172114 = 0.5
    float cy = boxPath ? minY                          // box branch: cy = rect min-y
                       : (minY + maxY) * 0.5f;         // cells branch: cy = rect centroid-y
    int halfW = (right - left) >> 1;
    int halfH = (bottom - top) >> 1;
    int screenX = static_cast<int>(cx) - halfW;
    int screenY = static_cast<int>(cy) - halfH;

    // --- Smooth toward the target (0.25 new / 0.75 cached); first frame (cached<0) snaps ---
    float* cachedX = &field_0x08;
    float* cachedY = &field_0x0C;
    if (*cachedX >= 0.0f) {
        *cachedX = static_cast<float>(screenX) * 0.25f + *cachedX * 0.75f;  // DAT_140172100 / DAT_140172124
        *cachedY = static_cast<float>(screenY) * 0.25f + *cachedY * 0.75f;
    } else {
        *cachedX = static_cast<float>(screenX);
        *cachedY = static_cast<float>(screenY);
    }

    // FIX SEAM (fix.ui_scale_layout, NOT RETAIL; docs/retail_bugs.md section 5): fe960 already scaled the rect
    // by the UI scale (layout units), and retail's SetUpperLeft(.., true) at 0x1401000c7 scales it again, so
    // below 100 % the bar lands toward the screen's upper-left (a phone's 50 %: at half the ghost's position).
    // With the row on the position is passed as the layout units it already is. Off: retail's double scale.
    const bool scaled = !Ext::Fix().ui_scale_layout;
    dialog->SetUpperLeft(static_cast<int>(*cachedX), static_cast<int>(*cachedY), scaled);
    {
        static bool s_posLogged = false;
        if (!s_posLogged) {
            s_posLogged = true;
            fprintf(stderr, "[fc60] positioned CreateBar: rect=(%.1f,%.1f,%.1f,%.1f) "
                    "root=(%d,%d,%d,%d) -> upperLeft=(%d,%d)\n",
                    rect.min.x, rect.min.y, rect.max.x, rect.max.y, left, top, right, bottom,
                    static_cast<int>(*cachedX), static_cast<int>(*cachedY));
            fflush(stderr);
        }
    }
}

