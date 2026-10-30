// src/game/Dialog/citizenui.cpp
// Clean reimplementation of CitizenUI panel methods.
//
// CitizenUI (0xE8 bytes): Citizen info panel.
// Layout:
//   +0x30 imageMale (InternalBasePtr)
//   +0x38 imageFemale
//   +0x40 textName
//   +0x48 numberAge
//   +0x50 textProfession
//   +0x58 textEducatedYes
//   +0x60 textEducatedNo
//   +0x68 textClothing
//   +0x70 textTool
//   +0x78 buttonProfession
//   +0x80 buttonHome
//   +0x88 buttonFollow
//   +0x90 textNameIs
//   +0x98 textAction
//   +0xA0 heartsIconArray (0x20 bytes: ptr, count, cap, ptr2, count2, cap2)
//   +0xC0 starsIconArray  (0x20 bytes)
//   +0xE0 sub-dialog InternalBasePtr (follow-dialog reference)

#include "Dialog/ui_panels.h"
#include "Dialog/FollowDialog.h"
#include "Dialog/citizen_icon_helpers.h"
#include "system/Value.h"
#include "ui/UserInterface.h"
#include "object/TypeInfo.h"
#include "Entity/exe_bodies.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "system/String.h"
#include "system/Filename.h"
#include "object/External.h"   // ENDGAME B.3.3 step 6A
#include <cstdint>
#include "ui/Element.h"  // ENDGAME B.3.6 UI callers (K3)
#include "ui/DialogInstance.h"  // ENDGAME B.3.6 UI callers (K3)
#include "ui/UserInterface.h"  // ENDGAME B.3.6 UI callers (K3)
#include "ui/String.h"  // ENDGAME B.3.6 UI callers (K3)
#include "ui/StringTable.h"  // ENDGAME B.3.6 UI callers (K3)
#include <new>  // ENDGAME B.3.6 UI callers (K3)
#include "ui/NumberElement.h"  // ENDGAME B.3.6 UI callers round 2 (K3)
#include "ui/TextElement.h"  // ENDGAME B.3.6 UI callers round 2 (K3)
#include "ui/Dialog.h"   // X::TypeId() (ENDGAME B.3.9)
#if defined(_DEBUG)
#include <cstdio>
#include <cstdlib>
#endif

#include "Object/factory_registration.h"
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include <cstddef>
#include "Component/CitizenAIComponent.h"   // X::TypeId() (B.3.8 callers)
#include "Component/CitizenComponent.h"   // X::TypeId() (B.3.8 callers)
#include "Component/CitizenDescription.h"   // X::TypeId() (B.3.8 callers)
#include "Entity/ComponentEntity.h"   // X::TypeId() (B.3.8 callers)
#include "Player/EditTool.h"   // X::TypeId() (B.3.8 callers)
#include "Work/Work.h"         // B.3.x W0: Work::GetTarget by name
#include "Dialog/exe_bodies.h"
// EXT SEAM (NOT RETAIL, docs/extensions/handins/TOOLNAMES.md): ext.tool_names (class C)
#include "ext/tool_names/tool_names.h"
#include "ext/registry.h"
#include "Component/RawMaterialDescription.h"   // the tool item: _stringTable, _name
#include "Game/exe_constants.h"   // B3R: kRdata_140172198
#include "system/MemoryAllocator.h"   // B3R: the allocator doors
#include "Component/AttendanceComponent.h"   // B4 DLG-a: CitizenComponent::school
#include "Game/Profession.h"
#include "Game/ProfessionList.h"
#include "Game/PlayerToolManager.h"
#include "Game/WorkScheduler.h"        // WorkScheduler::trees (GameUpdateLoop +0x118)
#include "Game/WorkSchedulerNodes.h"   // WorkSchedulerIdNode

// A partially-initialized standalone citizen can leave CitizenAI description/pointer
// fields as garbage (0, the -1 unresolved-IBP sentinel, or non-canonical high values
// like 0xfffffffffffffee7). CitizenUI_Slot7 dereferences several of these directly
// (matching the original, which relies on the live game having them resolved). Reject
// anything that isn't a plausible Win64 user-space heap pointer before dereferencing,
// so the panel shows what it can instead of faulting.
static inline bool CitUI_OkAddr(uintptr_t p) {
    return p >= 0x10000 && p <= 0x00007FFFFFFFFFFFULL;
}
static inline bool CitUI_OkPtr(const void* p) {   // LEFT: void* is a raw address (diagnostic probe), not an object: checks it lies in [0x10000, 0x7FFFFFFFFFFF] before a citizen-dialog deref
    return CitUI_OkAddr((uintptr_t)p);  // CAST-OK: ptr_int
}

// TextElement::SetParameter (Runtime 0x18011e9b0) writes _params._data[index] but only
// when index < _params._size. A citizen dialog element that
// standalone resolves by name can be a partially-initialized TextElement with a positive
// param count but a garbage param-array pointer — the write then faults. Only
// call SetParameter when the element genuinely has a valid parameter array.
// A System::String is an 8-byte pointer to a ref-counted buffer (buffer[-4] = refcount).
// SetParameter/SetText increment the source String's refcount (write buffer[-4]); an
// uninitialized citizen field (e.g. the name at CitizenComponent+0x28 is unset in standalone)
// holds a garbage buffer pointer, faulting there. Accept only a null (empty) or a real
// buffer pointer.
static inline bool CitUI_OkString(const System::String* strObj) {
    if (!CitUI_OkPtr(strObj)) return false;
    uint64_t buf = reinterpret_cast<uint64_t>(strObj->_data);  // CAST-OK: ptr_int
    return buf == 0 || CitUI_OkAddr(buf);
}

static inline bool CitUI_OkTextParam(const UI::TextElement* elem) {
    if (!CitUI_OkPtr(elem)) return false;
    int count = elem->_params._size;
    if (count <= 0) return false;  // SetParameter is a no-op for 0 params anyway
    const System::String* arr = elem->_params._data;
    if (!CitUI_OkPtr(arr)) return false;
    // The destination param slot arr[0] holds the CURRENT System::String (an 8-byte
    // ref-counted buffer pointer); SetParameter releases it (touches buffer+8) before
    // assigning the new one. A standalone dialog can allocate the param array but leave
    // its String entries uninitialized (garbage buffer pointer), faulting in the release.
    // Require the slot to be null (empty String) or a real pointer.
    char* cur = arr[0]._data;   // index 0 (all our SetParameter calls use 0)
    if (cur != nullptr && !CitUI_OkPtr(cur)) return false;
    // SetParameter also walks the element's _parent chain setting a dirty flag on
    // each ancestor. A detached / uninitialized standalone element can have a garbage
    // parent link, faulting in that loop before the (valid) param array is even touched.
    // Require the whole chain to be well-formed: every link a real pointer, terminating
    // at 0, within a sane depth. A live tree-attached element always satisfies this.
    const UI::Element* p = elem;
    for (int i = 0; i < 64 && p != nullptr; ++i) {
        if (!CitUI_OkPtr(p)) return false;
        p = p->_parent;
    }
    return p == nullptr;  // reached the root cleanly (not truncated by the depth cap)
}

// CitizenUI_UpdateIconDisplay_Impl is in the shared header Dialog/citizen_icon_helpers.h;
// the icon group's Init (0x14006abf0) is TownHall_GroupRatingInit (Dialog/RatingGroup.h).

// ============================================================
// WorldGrid_FindEntity (0x1400f9a60)
// ============================================================
// BST search: finds the Work with id `tileIndex` in the work scheduler's first
// id tree (GameUpdateLoop +0x118 is the WorkScheduler; +0x30 is trees[0], its
// nodes WorkSchedulerIdNode: key +0x00, work +0x08, left +0x18, right +0x20).
// On match: the node's Work; else null.
static Work* WorldGrid_FindEntity_Impl(const WorkScheduler* scheduler, int tileIndex)
{
    const WorkSchedulerIdNode* piVar1 = scheduler->trees[0].root;
    while (true) {
        if (piVar1 == nullptr) {
            return nullptr;
        }
        if (piVar1->key <= tileIndex) {
            if (tileIndex <= piVar1->key) {
                // Match
                return piVar1->work.Get();
            }
            // Go right
            piVar1 = piVar1->right;
        } else {
            // Go left
            piVar1 = piVar1->left;
        }
    }
}

// ============================================================
// Helper: Find element by name in dialog instance
// ============================================================
static UI::Element* FindDialogElem(UI::DialogInstance* dialogInst, const char* name) {
    System::String nameStr(name);
    UI::Element* result = dialogInst->FindElement(nameStr).Get();
    // A missing element yields a non-null sentinel / unresolved value here rather than
    // 0 (seen as 0xffffffff00000000), which defeats the `if (elem)` guards downstream
    // and faults inside the engine. Normalise anything that isn't a real pointer to 0.
    return CitUI_OkPtr(result) ? result : nullptr;
}

// Helper: Find element by name in element tree
static UI::Element* FindElemInTree(UI::Element* elem, const char* name) {
    System::String nameStr(name);
    UI::Element* result = elem->FindElement(nameStr).Get();
    // Normalise a missing/unresolved element to 0 (see FindDialogElem).
    return CitUI_OkPtr(result) ? result : nullptr;
}

// The citizen this panel shows: the data source's CitizenComponent. The generic
// finder the tree called returned null for a null entity (kept).
static CitizenComponent* CitizenUI_FindCitizen(Object::Base* dataSource) {
    bool isEntity = ::Object::IsDerivedFromNullSafe(dataSource, ComponentEntity::TypeId());
    ComponentEntity* entity = isEntity ? static_cast<ComponentEntity*>(dataSource) : nullptr;
    return entity ? entity->FindComponent<CitizenComponent>() : nullptr;   // 0x140009790
}

// ENDGAME B.3.9: ComponentEntity / CitizenDescription / CitizenComponent (was
// TypeId_CitizenComponent_FC) / EditTool all have a ported TypeId() now.
// B3R: the age multiplier is the .rdata constant 0x140172198 = 5.0f
// (kRdata_140172198). The old local 1.0f fallback installed over g_pAgeMultiplier
// when it was null never ran: the cell is registered before EngineFuncsInit binds it.

// ============================================================
// Slot4: Setup — finds all named elements, stores pointers, sets button IDs
// ============================================================
// Original: 0x1400651A0
//
// Finds 15 named UI elements and stores them in CitizenUI fields.
// Elements found via DialogInstance::FindElement go through the full dialog tree;
// elements found via Element::FindElement use the container (+0x20).
// groupHearts and groupStars are processed into icon arrays by FUN_14006abf0.
// Three buttons get element IDs: buttonProfession(0x227), buttonHome(0x226),
// buttonFollow(0x228).

// [4] 0x1400651a0
void CitizenUI::OnCreated() {
    if (!SA_DialogEntityAlive(this)) return;
    UI::DialogInstance* dialogInst = _dialog;
    UI::Element* parentElem = _container;

    // --- Image elements (found via DialogInstance) ---
    imageMale   = FindDialogElem(dialogInst, "imageMale");
    imageFemale = FindDialogElem(dialogInst, "imageFemale");

    // --- Text/number elements (found via Element tree from +0x20) ---
    // Retail stores each found element unchecked; each is read as its class.
    numberAge       = static_cast<UI::NumberElement*>(FindElemInTree(parentElem, "numberAge"));
    textName        = static_cast<UI::TextElement*>(FindElemInTree(parentElem, "textName"));
    textProfession  = static_cast<UI::TextElement*>(FindElemInTree(parentElem, "textProfession"));
    textEducatedNo  = FindElemInTree(parentElem, "textEducatedNo");
    textEducatedYes = FindElemInTree(parentElem, "textEducatedYes");
    textClothing    = static_cast<UI::TextElement*>(FindElemInTree(parentElem, "textClothing"));
    textTool        = static_cast<UI::TextElement*>(FindElemInTree(parentElem, "textTool"));
    textNameIs      = FindElemInTree(parentElem, "textNameIs");
    textAction      = static_cast<UI::TextElement*>(FindElemInTree(parentElem, "textAction"));

    // --- Icon groups (heart/star arrays built from group children) ---
    {
        UI::Element* groupHearts = FindElemInTree(parentElem, "groupHearts");
        TownHall_GroupRatingInit(&hearts, &groupHearts);   // 0x14006abf0
    }
    {
        UI::Element* groupStars = FindElemInTree(parentElem, "groupStars");
        TownHall_GroupRatingInit(&stars, &groupStars);     // 0x14006abf0
    }

    // --- Button elements (found via DialogInstance, with element IDs) ---
    buttonProfession = FindDialogElem(dialogInst, "buttonProfession");
    buttonHome       = FindDialogElem(dialogInst, "buttonHome");
    buttonFollow     = FindDialogElem(dialogInst, "buttonFollow");

    buttonProfession->SetElementId(0x227);
    buttonHome->SetElementId(0x226);
    buttonFollow->SetElementId(0x228);
}

// ============================================================
// Slot6: Close sub-dialog if open, then clear the reference.
// ============================================================
// Original: 0x140065130

// [6] 0x140065130
void CitizenUI::OnClosed() {
    if (!SA_DialogEntityAlive(this)) return;

    if (followDialog.Get() != nullptr) {
        // The UserInterface of the owning DialogInstance
        UI::UserInterface* userInterface = _dialog->_ui;
        userInterface->CloseDialog(&followDialog);
    }

    // Reset +0xE0 to null InternalBasePtr
    followDialog = Object::InternalPtr<UI::DialogInstance>();
}

// ============================================================
// Slot7: Update — refreshes citizen info display
// ============================================================
// Original: 0x140064AE0
//
// Gets the CitizenComponent from the data-source entity. Updates:
// - Gender icon visibility (male/female from `gender`)
// - Education status (education vs description maxEducation)
// - Name text (the citizen's name String)
// - Age number (age * the age multiplier)
// - Clothing text (clothingLevel index into the element's string table)
// - Tool text (+0xAC / +0xA8 tool count and item vs description +0x110)
// - Profession text (profession, or the child / student title)
// - Health hearts (FUN_14006aed0 with health vs description maxHealth)
// - Happiness stars (FUN_14006aed0 with happiness vs description maxHappiness)
// - Profession button enabled state (workplace or school assigned)
// - Home button virtual call (likely sets enabled state)
// - Action text (from the CitizenAIComponent's state)

// [7] 0x140064ae0
void CitizenUI::OnUpdateLayout() {
    if (!SA_DialogEntityAlive(this)) return;

    // Find the CitizenComponent of the data source (+0x28)
    CitizenComponent* ai = CitizenUI_FindCitizen(_dataSource);
    if (!ai) return;

    // Get CitizenDescription from Component::_description (+0x18), check IsDerivedFrom
    Description* descBase = ai->_description;
    bool isDesc = ::Object::IsDerivedFromNullSafe(descBase, CitizenDescription::TypeId());
    CitizenDescription* citizenDesc = isDesc ? static_cast<CitizenDescription*>(descBase) : nullptr;
    if (!CitUI_OkPtr(citizenDesc)) citizenDesc = nullptr;

    // CitizenComponent::description (+0x288) caches the CitizenDescription pointer; the
    // original reads its maxEducation / adultAge directly. In standalone that cache can be
    // garbage (unresolved) while the Component-base description (validated above as
    // citizenDesc) is valid. Use +0x288 when it looks like a real pointer, else fall
    // back to citizenDesc (the same CitizenDescription); 0 if neither is valid.
    CitizenDescription* aiDesc = ai->description;
    if (!CitUI_OkPtr(aiDesc)) aiDesc = citizenDesc;
    if (!CitUI_OkPtr(aiDesc)) aiDesc = nullptr;

    // --- Gender icon visibility ---
    // gender (+0x38): 0 = female, nonzero = male
    bool isMale = (ai->gender != 0);
    // if isMale, show imageMale (hide female), else show imageFemale (hide male)
    if (isMale) {
        if (imageFemale) imageFemale->SetFlagsOnOff(1, 0);  // hide imageFemale
    } else {
        if (imageFemale) imageFemale->SetFlagsOnOff(0, 1);  // show imageFemale
    }
    if (imageMale) imageMale->SetFlagsOnOff(isMale ? 0 : 1, isMale ? 1 : 0);

    // --- Education visibility ---
    // education (+0x210) against the description's maxEducation (+0x118) (aiDesc =
    // the CitizenDescription; see aiDesc note above). Skip if no description resolved.
    bool isEducated = aiDesc && (ai->education < aiDesc->maxEducation);
    if (isEducated) {
        if (textEducatedNo) textEducatedNo->SetFlagsOnOff(0, 1);  // show textEducatedNo
    } else {
        if (textEducatedNo) textEducatedNo->SetFlagsOnOff(1, 0);  // hide textEducatedNo
    }
    if (textEducatedYes) textEducatedYes->SetFlagsOnOff(
                             isEducated ? 1 : 0, isEducated ? 0 : 1);

    // --- Name text ---
    // Guard both the destination element AND the source name String (+0x28), which is
    // uninitialized on a standalone citizen and would fault when SetParameter bumps its
    // ref-count.
    bool okName = CitUI_OkString(&ai->name);
    bool okParam = CitUI_OkTextParam(textName);
    if (okName && okParam)
        textName->SetParameter(0, ai->name);

    // --- Age number ---
    float ageFloat = ai->age;
    int ageYears = (int)(ageFloat * kRdata_140172198);
    if (numberAge) numberAge->SetValue(static_cast<int32_t>(ageYears));

    // --- Clothing text (string table lookup) ---
    // UI::String(the element's text table, clothingLevel)
    if (textClothing) {
        uint32_t clothingIdx = ai->clothingLevel;
        UI::String text = textClothing->_text;
        text.index = clothingIdx;
        textClothing->SetText(&text);
    }

    // --- Tool text ---
    // tool count at +0xAC, tool item at +0xA8
    // If tool count < 1: value = 0
    // Else: compare the tool item with the description's +0x110; 1 if same, 2 if different
    int toolValue;
    if (ai->field_0xAC < 1) {
        toolValue = 0;
    } else {
        int toolItem = ai->field_0xA8;
        // The same checked cast of the same _description as citizenDesc above.
        // desc2 (CitizenDescription) may be null on a partially-initialized standalone
        // citizen — treat a missing description as "default tool" rather than faulting.
        CitizenDescription* desc2 = citizenDesc;
        toolValue = desc2 ? ((toolItem != desc2->_i110) + 1) : 1;
    }
    if (textTool) {
        UI::String text = textTool->_text;
        text.index = toolValue;
        // EXT SEAM (NOT RETAIL, docs/extensions/handins/TOOLNAMES.md): ext.tool_names (class C) names the
        // tool the citizen carries from the item's own string table; retail's word (above) when no note
        // matches.
        if (toolValue != 0 && ::Ext::Ext().tool_names) {
            const RawMaterialDescription* item = ::Ext::ToolNames::ToolFor(ai, ai->field_0xA8);
            if (item != nullptr && item->_stringTable != nullptr) {
                UI::String named(Object::ExternalPtr<UI::StringTable>(item->_stringTable), item->_name);
                if (named.IsValid()) {
                    text = named;
                }
            }
        }
        textTool->SetText(&text);
    }

    // --- Profession text ---
    // If a child (age < adultAge) or at school:
    //   not at school: ProfessionList::childProfession (+0x40)
    //   at school:     ProfessionList::studentProfession (+0x48)
    // Else: the current profession (+0x1F0).
    // Text = UI::String(profession->stringTable, profession's name key +0x30).
    if (textProfession) {
        // gw+0xB0 is the ProfessionList, read directly off the GameWorld base
        // (single deref) — matching retail (0x140064AE0).
        GameUpdateLoop* gwBase = GameUpdateLoop::_singleton;
        ProfessionList* profListBase = CitUI_OkPtr(gwBase) ? gwBase->m_professionList : nullptr;

        bool isChild = aiDesc && (ai->age < aiDesc->adultAge);
        bool hasSchool = (ai->school != nullptr);

        Profession* titled = nullptr;
        if (isChild || hasSchool) {
            if (CitUI_OkPtr(profListBase)) {
                titled = (ai->school == nullptr)
                             ? profListBase->childProfession     // child
                             : profListBase->studentProfession;  // student
            }
        } else {
            titled = ai->profession;
        }
        if (CitUI_OkPtr(titled)) {
            UI::String text(Object::ExternalPtr<UI::StringTable>(titled->stringTable),
                            titled->stringName);
            textProfession->SetText(&text);
        }
    }

    // --- Health hearts and happiness stars ---
    // Max values come from the CitizenDescription (maxHealth, maxHappiness);
    // use the resolved aiDesc/citizenDesc and skip if neither is a valid pointer.
    {
        CitizenDescription* hd = aiDesc ? aiDesc : citizenDesc;
        if (CitUI_OkPtr(hd)) {
            CitizenUI_UpdateIconDisplay_Impl(hearts.whole, hearts.half, ai->health, hd->maxHealth);
            CitizenUI_UpdateIconDisplay_Impl(stars.whole, stars.half, ai->happiness, hd->maxHappiness);
        }
    }

    // --- Profession button enabled state ---
    // Enabled if a workplace (+0x248) or a school (+0x208) is assigned
    bool profEnabled = (ai->workplace != nullptr) || (ai->school != nullptr);
    if (buttonProfession) {
        buttonProfession->Enable(profEnabled);   // Element slot 8 Enable (vtable+0x40)
    }

    // --- Home button virtual call ---
    if (buttonHome) {
        // Original passes result of CheckPtr on the citizen (always true if we got here)
        buttonHome->Enable(true);   // Element slot 8 Enable (vtable+0x40)
    }

    // --- textNameIs: set name parameter ---
    // In standalone the "textNameIs" element resolves to a partially-constructed
    // TextElement whose parameter-array String entries are uninitialized (garbage buffer
    // pointers), so SetParameter faults deep inside System::String::operator= even though
    // the element, its param array and its parent chain all validate. It is a redundant
    // secondary name label — the primary textName above already shows the citizen's name
    // — so skip it in standalone. (The original keeps the CheckPtr + set here.)

    // --- Action text ---
    // The citizen's entity (Component::_entity), then its CitizenAIComponent
    // (FUN_140011050 at 0x140064fc2).
    ComponentEntity* entityFromAI = ai->_entity.Get();
    // Guard the entity backref (component-list walk dereferences its component array);
    // a partially-initialized standalone citizen can leave it as garbage.
    if (CitUI_OkPtr(entityFromAI)) {
    CitizenAIComponent* aiComp = entityFromAI->FindComponent<CitizenAIComponent>();   // 0x140011050

    // Read the action index (CitizenAIComponent::state) -- unguarded, as retail
    // (0x140064fd6): every citizen carries one.
    int actionIndex = aiComp->state;

    // Special case: if action is 9 (idle/searching), check if citizen has a home tile
    if (actionIndex == 9) {
        Work* homeWork = nullptr;
        int homeTile = ai->field_0x30;
        if (homeTile >= 0) {
            // Look up the Work at the home tile via the world work tree (gw+0x118)
            homeWork = WorldGrid_FindEntity_Impl(GameUpdateLoop::_singleton->m_tutorial, homeTile);
        }
        if (homeWork != nullptr) {
            // The work's target against the citizen's profession
            Profession* target = nullptr;
            homeWork->GetTarget(&target);   // slot 10
            if (target != ai->profession) {
                actionIndex = 0x10;  // "looking for work" or similar
            }
        }
    }

    // Set action text from the element's string table
    if (textAction) {
        UI::String text = textAction->_text;
        text.index = actionIndex;
        textAction->SetText(&text);
    }
    }  // if (CitUI_OkPtr(entityFromAI))
}

// ============================================================
// Slot9: Button dispatch — profession (0x227), home (0x226), follow (0x228)
// ============================================================
// Original: 0x1400650F0
//
// Handles three button clicks:
// - buttonProfession (0x227): Navigate camera to the citizen's school or workplace
// - buttonHome (0x226): Navigate camera to citizen's home building
// - buttonFollow (0x228): Open follow dialog to track this citizen
//
// Navigation uses PlayerToolManager::CenterOnEntity (FUN_140103040) which moves
// the camera to the entity's world position or build footprint center.

// [9] 0x1400650f0
void CitizenUI::OnElementEvent(Object::InternalPtr<UI::Element>* /*element*/, int32_t param3, int32_t param4,
                               int32_t /*c*/, int32_t /*d*/, int32_t /*e*/, void* /*userData*/) {
    // Gate on datasource-alive like the sibling slots (4/6/7): a click on a dead
    // citizen's still-open panel must not drive this body into a freed entity.
    if (!SA_DialogEntityAlive(this)) return;
    // Only handle event type 0x12 (button click)
    if (param3 != 0x12) return;

    if (param4 == 0x227) {
        // --- buttonProfession: go to school / workplace ---
        CitizenComponent* ai = CitizenUI_FindCitizen(_dataSource);

        // Both arms hand RDX = &(copy of DialogController +0x18) and R8 = &(copy of the
        // target entity) (0x1400649d0: 0x140064a61-0x140064a6d on the at-school arm).
        ComponentEntity* targetRef;
        UI::DialogInstance* dialogRef = _dialog;
        if (ai->school != nullptr) {
            // At school (+0x208): navigate to the school's entity (Component::_entity, +0x10)
            AttendanceComponent* school = ai->school.Get();
            targetRef = school->_entity.Get();
        } else {
            if (ai->workplace == nullptr) return;
            // Has a workplace (+0x248): navigate there
            targetRef = ai->workplace.Get();
        }

        PlayerToolManager* gameSubsys = GameUpdateLoop::_singleton->m_toolManager;
        gameSubsys->CenterOnEntity(&dialogRef, &targetRef);
    }
    else if (param4 == 0x226) {
        // --- buttonHome: go to home building ---
        CitizenComponent* ai = CitizenUI_FindCitizen(_dataSource);

        ComponentEntity* homeEntity = ai->home.Get();
        if (homeEntity == nullptr) return;

        ComponentEntity* targetRef = homeEntity;
        UI::DialogInstance* dialogRef = _dialog;
        PlayerToolManager* gameSubsys = GameUpdateLoop::_singleton->m_toolManager;
        gameSubsys->CenterOnEntity(&dialogRef, &targetRef);   // 0x140103040
    }
    else if (param4 == 0x228) {
        // --- buttonFollow: open follow dialog ---
        // Creates a FollowDialog (0xD8 bytes) from Dialog/Follow.rsc and adds it to the UI.
        Object::Base* dataSource = _dataSource;
        bool isEntity = ::Object::IsDerivedFromNullSafe(dataSource, ComponentEntity::TypeId());
        ComponentEntity* entity = isEntity ? static_cast<ComponentEntity*>(dataSource) : nullptr;

        // Find the CitizenComponent on the entity
        CitizenComponent* citizenAIRef = entity->FindComponent<CitizenComponent>();

        // Load Dialog/Follow.rsc resource
        System::Filename filename("Dialog/Follow.rsc");
        const ::Object::External* extResource =
            ::Object::ExternalFactory::_singleton->FindOrLoad(filename);

        // Type-check the resource
        const UI::Dialog* checkedRes = nullptr;
        if (extResource) {
            bool isDialog = ::Object::IsDerivedFromNullSafe(extResource, UI::Dialog::TypeId());
            if (isDialog) checkedRes = static_cast<const UI::Dialog*>(extResource);
        }

        // Allocate and construct FollowDialog (0xD8 bytes)
        void* followMem = System::MemoryAllocator::MemAlloc(0xD8, 8);
        FollowDialog* resultPtr = nullptr;
        if (followMem) {
            // The real ctor (B38 B8-5): DialogInstance(ui, resource, null parent, 0, 0, 0),
            // then FollowDialog's own table and zeroed +0xB8..+0xD0.
            FollowDialog* fd = ::new (followMem) FollowDialog(
                _dialog->_ui, checkedRes, Object::InternalPtr<UI::DialogInstance>());

            fd->citizen = citizenAIRef;   // entity to follow (the citizen IBP)
            fd->citizenUI = this;                      // controller back-reference
            // +0xC8 textFollow / +0xD0 textAction: zeroed by the ctor, set by slot 4.

            resultPtr = fd;
        }

        // Store dialog ref at CitizenUI+0xE0
        followDialog = Object::InternalPtr<UI::DialogInstance>(resultPtr);

        // Add dialog to UI
        Object::InternalPtr<UI::DialogInstance> dialogRef = followDialog;
        _dialog->_ui->AddDialog(&dialogRef);
    }
}

// ============================================================
// STANDALONE promotion — real CitizenUI controller vtable
// ============================================================
// Clicking a citizen (now pickable via the Picking::Scene) opens a dialog whose
// controller is CitizenUI. In standalone this type was a StubNop (all slots no-op)
// so the panel opened blank. CitizenUI is a real class now (table 0x14015ba00):
//   +0x20 (idx4) OnCreated, +0x30 (idx6) OnClosed, +0x38 (idx7) OnUpdateLayout,
//   +0x40 (idx8) OnUpdate — retail inherits the base (a no-op); the framework
//   dispatches ONLY +0x40 each frame (Runtime 0x180115400), so the tree drives
//   OnUpdateLayout from it (standalone deviation), +0x48 (idx9) OnElementEvent.

// [8] STANDALONE DEVIATION (retail inherits the base slot) — the tree's wiring, kept.
void CitizenUI::OnUpdate(const System::Time& /*time*/) {
    CitizenUI::OnUpdateLayout();
}


// create 0x140010f80: MemAlloc(0xe8, 8), then the ctor (DialogController ->
// Internal: AddObject, the compiler's stamps, the field initialisers).
::Object::Base* CitizenUI::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(CitizenUI), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) CitizenUI());
}
// Retail's createInPlace does not build a controller in place (B8-13); the
// tree's in-place entry did, and that is kept.
::Object::Base* CitizenUI::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) CitizenUI());
}

DEFINE_EXE_TYPE(CitizenUI, "UI::DialogController")

