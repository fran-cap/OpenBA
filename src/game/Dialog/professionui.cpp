// src/game/Dialog/professionui.cpp
// Clean reimplementation of ProfessionUI game logic.
//
// ProfessionUI (0x78 bytes) -- profession management panel.
// Extends DialogController (0x30 base). Has Ctor at 0x1400775a0.
//
// Struct layout:
//   +0x00: DialogController base (0x30)
//   +0x30: int* countsArray          // per-profession citizen count
//   +0x38: (unused/zeroed)
//   +0x40: void** spinnerElements    // InternalBasePtr[] for spinner elements
//   +0x48: uint32_t spinnerCount
//   +0x4C: uint32_t spinnerCapacity
//   +0x50: void** numberElements     // InternalBasePtr[] for number display elements
//   +0x58: uint32_t numberCount
//   +0x5C: uint32_t numberCapacity
//   +0x60: void* bstRoot             // BST root (profession -> index mapping)
//   +0x68: uint32_t bstNodeCount
//   +0x6C: (padding)
//   +0x70: uint32_t rotationIndex    // for cycling through citizens in Slot9
//   +0x74: (padding)
//
// BST node layout (0x30 bytes):
//   +0x00: void* key  (ExternalPtr to profession description)
//   +0x08: int index   (profession table index)
//   +0x0C: (padding)
//   +0x10: void* parent
//   +0x18: void* left
//   +0x20: void* right
//   +0x28: int color (red-black flag)
//   +0x2C: (padding)
//
// Decompiled from Application-x64.exe.

#include "Dialog/ui_panels.h"
#include "system/Value.h"
#include "ui/UserInterface.h"
#include "object/TypeInfo.h"
#include "Entity/exe_bodies.h"
#include "Game/exe_bodies.h"
#include "Dialog/exe_bodies.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Object/factory_registration.h"
#include "system/String.h"
#include "system/Filename.h"
#include "object/External.h"   // ENDGAME B.3.3 step 6A
#include <cstdint>
#include <cstdio>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "ui/Element.h"  // ENDGAME B.3.6 UI callers (K3)
#include "ui/String.h"  // ENDGAME B.3.6 UI callers (K3)
#include "ui/Sprite.h"  // ENDGAME B.3.6 UI callers (K3)
#include "ui/SpriteSheet.h"  // ENDGAME B.3.6 UI callers (K3)
#include "ui/StringTable.h"  // ENDGAME B.3.6 UI callers (K3)
#include <new>  // ENDGAME B.3.6 UI callers (K3)
#include "ui/LabelElement.h"  // ENDGAME B.3.6 UI callers round 2 (K3)
#include "ui/NumberEditElement.h"  // ENDGAME B.3.6 UI callers round 2 (K3)
#include "ui/NumberElement.h"  // ENDGAME B.3.6 UI callers round 2 (K3)
#include "ui/TableElement.h"  // ENDGAME B.3.6 UI callers round 2 (K3)
#include "ui/ElementDescription.h"  // ENDGAME B.3.6 UI callers round 2 (K3)
#include "ui/TextElement.h"  // ENDGAME B.3.6 UI callers round 2 (K3)
#include "Component/WorkDescription.h"   // X::TypeId() (B.3.9 CG2)
#include "Component/WorkPlaceComponent.h"   // X::TypeId() (B.3.9 CG2)
#include "Component/WorkComponent.h"   // X::TypeId() (B.3.9 CG2)
#include "system/MemoryAllocator.h"   // B3R: the allocator doors
#include "Component/exe_bodies.h"   // B3R: CitizenEmploy_Strat_*

#include "Dialog/ProfessionUI.h"
#include "Game/Profession.h"
#include "Game/ProfessionList.h"
#include "Entity/CitizenBehavior.h"
#include "Game/PlayerToolManager.h"
#include "Entity/ComponentEntity.h"
#include "Component/CitizenComponent.h"
#include "Component/CitizenDescription.h"
#include "Component/MapComponent.h"

// LEFT in this file (B4 P2 DLG-2): the town's worker functions
// (CitizenEmploy_Strat_*, FindAvailableBuilding) take the
// citizen list and the profession as integers and are called from other blocks'
// files: one cast per argument, `// LEFT: SIG` (wave X).

// ============================================================
// File-local helpers: AVL BST rotations + rebalance
// ============================================================
// The profession -> row Set at ProfessionUI::professionIndexRoot (+0x60); its
// node is ProfessionUI::ProfessionIndexNode (0x30 bytes).
//
// FUN_14000e580: promotes node's left child to this node's position (right-rotation)
// FUN_14000e450: promotes node's right child to this node's position (left-rotation)

using ProfNode = ProfessionUI::ProfessionIndexNode;

static void BST_UpdateNode(ProfNode* n) {
    // Recompute balance and height from children.
    int16_t lh = 0, rh = 0;
    ProfNode* lc = n->left;
    ProfNode* rc = n->right;
    if (lc) lh = (int16_t)(lc->height + 1);
    if (rc) rh = (int16_t)(rc->height + 1);
    n->balance = (int16_t)(rh - lh);
    n->height = (rh > lh) ? rh : lh;
}

// FUN_14000e580 — promotes node->left child (right-rotation at node)
static ProfNode* BST_RotateRight_e580(ProfNode** rootPtr, ProfNode* node) {
    ProfNode* parent = node->parent;
    ProfNode* pivot  = node->left;  // left child becomes new root

    if (node == *rootPtr) *rootPtr = pivot;
    if (parent) {
        if (parent->left == node) parent->left = pivot;
        else                      parent->right = pivot;
    }
    pivot->parent = parent;

    // node.left = pivot.right; if pivot.right, reparent it
    ProfNode* pivotRight = pivot->right;
    node->left = pivotRight;
    if (pivotRight) pivotRight->parent = node;

    pivot->right = node;
    node->parent = pivot;

    // Update heights in correct order
    ProfNode* pr = pivot->left;
    if (pr) BST_UpdateNode(pr);
    ProfNode* pr2 = pivot->right;
    if (pr2) BST_UpdateNode(pr2);
    BST_UpdateNode(pivot);
    return pivot;
}

// FUN_14000e450 — promotes node->right child (left-rotation at node)
static ProfNode* BST_RotateLeft_e450(ProfNode** rootPtr, ProfNode* node) {
    ProfNode* parent = node->parent;
    ProfNode* pivot  = node->right;  // right child becomes new root

    if (node == *rootPtr) *rootPtr = pivot;
    if (parent) {
        if (parent->left == node) parent->left = pivot;
        else                      parent->right = pivot;
    }
    pivot->parent = parent;

    // node.right = pivot.left; if pivot.left, reparent it
    ProfNode* pivotLeft = pivot->left;
    node->right = pivotLeft;
    if (pivotLeft) pivotLeft->parent = node;

    pivot->left = node;
    node->parent = pivot;

    ProfNode* pl = pivot->left;
    if (pl) BST_UpdateNode(pl);
    ProfNode* pl2 = pivot->right;
    if (pl2) BST_UpdateNode(pl2);
    BST_UpdateNode(pivot);
    return pivot;
}

// BSTRebalance_Impl (FUN_140013480): Walk up from startNode, updating heights/balance
// and performing AVL rotations as needed.
// treeRootPtr: pointer to the BST root field (&professionIndexRoot)
// startNodePtr: pointer to the node to begin from (advances to parent each iteration)
static void BSTRebalance_Impl(ProfNode** treeRootPtr, ProfNode** startNodePtr) {
    ProfNode* cur = *startNodePtr;
    while (cur != nullptr) {
        ProfNode* curNode = *startNodePtr;
        ProfNode* nextParent = curNode->parent;

        // Recompute balance/height
        int16_t lh = 0, rh = 0;
        ProfNode* lc = curNode->left;
        ProfNode* rc = curNode->right;
        if (lc) lh = (int16_t)(lc->height + 1);
        if (rc) rh = (int16_t)(rc->height + 1);
        curNode->balance = (int16_t)(rh - lh);
        curNode->height = (rh > lh) ? rh : lh;

        // Right-heavy (balance > 1): need left-rotation
        ProfNode* pivot = curNode->right;
        if (curNode->balance > 1 && pivot != nullptr) {
            // If right child is left-heavy (balance < 0): double rotation (right-left)
            if (pivot->balance < 1 && pivot->balance < 0) {
                BST_RotateRight_e580(treeRootPtr, pivot);
                curNode = *startNodePtr;
            }
            BST_RotateLeft_e450(treeRootPtr, curNode);
        }

        curNode = *startNodePtr;
        // Left-heavy (balance < -1): need right-rotation
        pivot = curNode->left;
        if (curNode->balance < -1 && pivot != nullptr) {
            // If left child is right-heavy (balance > 0): double rotation (left-right)
            if (pivot->balance > -1 && pivot->balance > 0) {
                BST_RotateLeft_e450(treeRootPtr, pivot);
                curNode = *startNodePtr;
            }
            BST_RotateRight_e580(treeRootPtr, curNode);
        }

        *startNodePtr = nextParent;
        cur = nextParent;
    }
}

// ---- ProfessionUI: PROMOTED to the real reimpl (profession-assignment bring-up) -
// The toolbar "professions" overlay (Template\UtilityProfessions.rsc). Its controller
// lists every profession with a worker-count spinner + a "goto" button. The real
// reimpl lives in src/game/Dialog/professionui.cpp; the generic stub left every slot
// a no-op so the overlay opened empty. Wire the three real slots:
//   Slot5 (+0x28, setup)  — builds the profession table, spinner/number element
//                           caches, and the profession→index BST; self-calls Slot8.
//   Slot8 (+0x40, update) — recounts citizens per profession each frame and refreshes
//                           the spinner values + worker-slot numbers. DialogInstance::
//                           OnUpdate dispatches this every frame once the controller is
//                           attached — INCLUDING before Slot5 has populated the caches,
//                           at which point the reimpl would deref the null spinner array
//                           (+0x40). Guard: skip until setup has run (the counts
//                           array + the spinner array both non-null).
//   Slot9 (+0x48, event)  — spinner change reassigns citizens between professions
//                           (uses the CitizenList Find/Request/Remove worker fns already
//                           registered for the Town Hall profession tab); button click
//                           centers the camera on a citizen of that profession.
// Construction stays the stub alloc+zero+vtable; the framework fills the base
// dialog/datasource fields afterward — same shape as the MapUI/ImmigrationUI promotions.
namespace professionui_sa {
    // ENDGAME B.3.9 CG2: EnsureTypeIds retired — it healed TypeId_NumberEditElement
    // (a per-file lazy-fill of the exe-side global) which the conversion below to
    // UI::NumberEditElement::TypeId() (the ported Runtime static) makes unnecessary.
    void Trace([[maybe_unused]] const char* slot, ProfessionUI* self) {
        static int n=0; if (n>=32) return; ++n;
        uint32_t spinN = self ? static_cast<uint32_t>(self->spinners._size) : 0;
        printf("[ProfessionUI] %s self=%p dlg(+0x18)=%p root(+0x20)=%p counts(+0x30)=%p "
               "spinners(+0x40)=%p n=%u numbers(+0x50)=%p n=%u\n", slot, self,
               self?self->_dialog:nullptr,
               self?self->_container:nullptr,
               self?self->counts._data:nullptr,
               self?self->spinners._data:nullptr, spinN,
               self?self->numbers._data:nullptr,
               self?static_cast<uint32_t>(self->numbers._size):0);
        // Dump the per-profession citizen counts (+0x30 int array) so we can confirm
        // Slot8 computes real assignments, not all-zero placeholder rows.
        int32_t* counts = self?self->counts._data:nullptr;
        if (counts && spinN>0 && spinN<=64) {
            printf("[ProfessionUI]   counts[0..%u]:", spinN-1);
            for (uint32_t i=0;i<spinN;i++) printf(" %d", counts[i]);
            printf("\n");
        }
        fflush(stdout);
    }
}

// ============================================================
// ProfessionUI::Slot5 (vtable +0x28) -- Setup/populate
// ============================================================
// Finds "professionTable", populates it with profession entries.
// For each profession: creates table cell from template, finds child
// elements (label, spinner, number, button), sets name/sprite/text/IDs,
// inserts into BST, pushes elements to arrays.
//
// Original: 0x140076CA0.

// [5] 0x140076ca0
void ProfessionUI::OnOpened() {
    // 0x140076ca0 starts with the "professionTable" String / FindElement, no test: the
    // framework runs this slot once per open (UserInterface::AddDialog ->
    // DialogInstance::OnOpened). A second run re-clones the rows; the harness re-drive
    // that made one is gone (B9 RES, known_port_issues row 1200).
    professionui_sa::Trace("Slot5", this);

    // Find "professionTable" element in the controller's container
    System::String nameStr("professionTable");
    UI::TableElement* tableElem =
        static_cast<UI::TableElement*>(_container->FindElement(nameStr).Get());

    // Get column count from the table
    int colCount = tableElem->_columnCount;

    // Get profession count from the game's profession list
    ProfessionList* profListPtr = GameUpdateLoop::_singleton->m_professionList;
    int profCount = profListPtr->professions.GetSize();


    // Get template factory: the first child's description (Element::_description)
    UI::Element* firstChild = tableElem->Child(0).Get();
    const UI::ElementDescription* factory = firstChild->_description;

    // Set table size: ceil(profCount / colCount) rows
    int rowCount = ((colCount - 1 + profCount) / colCount * colCount) / colCount;
    tableElem->SetSize(rowCount, colCount);


    // Phase 1: Populate table cells from template factory
    int col = 0;
    int row = 0;

    for (int i = 0; i < profCount; i++) {
        // The owning dialog's Dialog resource
        const UI::Dialog* templateData = _dialog->_dialog;

        // Clone the template: `factory` is the first child's Element+0xB8 `_description`
        // (ExternalPtr<UI::ElementDescription>), so vt[8] is ElementDescription slot 8
        // CreateInstance(ret, dialog) -- a RETSLOT virtual (RULE B6-2).
        Object::InternalPtr<UI::Element> cloneBuf;
        Object::InternalPtr<UI::Element>* cloned = factory->CreateInstance(&cloneBuf, templateData);

        tableElem->SetChild(cloned, col, row);

        col++;
        if (col >= tableElem->_rowCount) {
            row++;
            col = 0;
        }
    }

    // Phase 2: For each profession, set up child elements
    int tableCol = 0;
    int tableRow = 0;
    int idBase = 300;
    Profession** profArray = profListPtr->professions.GetData();

    for (int i = 0; i < profCount; i++) {
        // Re-read the flat-index stride from the table each iteration.
        // The original (0x140076eb4) does `IMUL EBX,R13D` with EBX = *(table+0xC4)
        // then `ADD EBX,R12D`, i.e. flatIdx = (+0xC4) * fastIdx + slowIdx, where the
        // fast index (tableCol here) wraps at +0xC0. +0xC4 is the column stride (=2),
        // +0xC0 is the row count (=10). Using +0xC0 as the stride and multiplying the
        // SLOW index instead collapses the intended COLUMN-MAJOR layout (builder below
        // laborer, as retail draws it) into row-major (builder to the right). Match the
        // original exactly: stride = +0xC4, flatIdx = stride*tableCol + tableRow.
        int stride = tableElem->_columnCount;

        int flatIdx = stride * tableCol + tableRow;
        UI::Element* cellElem = tableElem->Child(flatIdx).Get();

        // Get 4 children of cell (indices from disassembly: 0, 1, 3, 4):
        //   child[0] = text/label element (name + sprite)
        //   child[1] = spinner element (worker count)
        //   child[3] = number element (actual count)
        //   child[4] = button element (goto)
        UI::LabelElement* textLabel = static_cast<UI::LabelElement*>(cellElem->Child(0).Get());
        UI::Element* spinner = cellElem->Child(1).Get();
        UI::NumberElement* number = static_cast<UI::NumberElement*>(cellElem->Child(3).Get());
        UI::Element* button = cellElem->Child(4).Get();

        // Set button element ID: idBase + 200
        button->SetElementId(idBase + 200);

        // Get profession ExternalPtr from array
        Profession* profDesc = profArray[i];

        // Set element name from profession file path
        {
            // `GetPath` returns by value through a hidden return slot and the callee
            // CONSTRUCTS into it; copy-initialising the local is the same store.
            System::Filename pathObj = profDesc->GetPath();
            System::Filename identObj = pathObj.GetPathIdentifer();
            spinner->SetName(identObj.AsString());
        }

        // BST insert: key=profDesc, value=idBase-300 (=i)
        int bstIndex = idBase - 300;
        ProfNode** bstRootPtr = &professionIndexRoot;
        ProfNode* existingRoot = *bstRootPtr;

        if (existingRoot == nullptr) {
            // First node -- allocate and set as root
            ProfNode* newNode = static_cast<ProfNode*>(System::MemoryAllocator::MemAlloc(0x30, 8));
            if (newNode) {
                newNode->key = profDesc;
                newNode->index = bstIndex;
                newNode->_pad0C = 0;
                newNode->parent = nullptr;
                newNode->left = nullptr;
                newNode->right = nullptr;
                newNode->balance = 0;   // one dword store over balance / height
                newNode->height = 0;
                *bstRootPtr = newNode;
            }
            professionIndexCount = professionIndexCount + 1;   // 0x14007712d, the root arm's JMP
        } else {
            // BST insert + rebalance, in retail's order (0x140077068..0x14007712d): walk
            // first, allocate only at the null child, rebalance from the parent walked to
            // ([RSP+0x38]), then count. An equal key (JNC 0x140077131) allocates nothing,
            // rebalances nothing and does not count.
            ProfNode* current = existingRoot;
            bool duplicate = false;
            ProfNode** childSlot = nullptr;
            while (childSlot == nullptr && !duplicate) {
                Profession* curKey = current->key;
                if (profDesc < curKey) {
                    if (current->left == nullptr) {
                        childSlot = &current->left;    // 0x1400770e4, node + 0x18
                    } else {
                        current = current->left;
                    }
                } else if (curKey < profDesc) {
                    if (current->right == nullptr) {
                        childSlot = &current->right;   // 0x1400770a7, node + 0x20
                    } else {
                        current = current->right;
                    }
                } else {
                    duplicate = true;                  // 0x14007707b JNC 0x140077131
                }
            }
            if (!duplicate) {
                ProfNode* newNode = static_cast<ProfNode*>(System::MemoryAllocator::MemAlloc(0x30, 8));
                if (newNode) {
                    newNode->key = profDesc;
                    newNode->index = bstIndex;
                    newNode->_pad0C = 0;
                    newNode->parent = current;
                    newNode->left = nullptr;
                    newNode->right = nullptr;
                    newNode->balance = 0;   // one dword store over balance / height
                    newNode->height = 0;
                }
                *childSlot = newNode;
                ProfNode* rebalanceFrom = current;   // [RSP+0x38], the parent
                BSTRebalance_Impl(&professionIndexRoot, &rebalanceFrom);   // 0x140013480
                professionIndexCount = professionIndexCount + 1;            // 0x14007712d
            }
        }

        // Set text, sprite, tooltip on text/label element from profession description
        UI::String uiStr(profDesc->stringTable,
                         profDesc->stringName);
        textLabel->SetText(&uiStr);

        UI::Sprite sprite(profDesc->spriteSheet,
                          profDesc->stringName);
        textLabel->SetSprite(&sprite);

        // Tooltip: UI::String from the table + the string name at +0x38
        UI::String tooltip(profDesc->stringTable,
                           profDesc->stringDesc);
        textLabel->SetToolTip(tooltip);

        // Set user data and element ID on spinner
        // Original only sets user data + child element ID, but the event system
        // sends the spinner's own element ID as param4 in msg 0x15 notifications.
        // Without SetElementId on the spinner itself, param4 is -1 and Slot9 never
        // dispatches to HandleSpinner.
        spinner->SetUserData(reinterpret_cast<Object::Base*>((uintptr_t)idBase));  // CAST-OK: ptr_int
        spinner->SetElementId(idBase);

        // Get child[0] of spinner: set its element ID (original does this too)
        UI::Element* spinnerChild0 = spinner->Child(0).Get();
        spinnerChild0->SetElementId(idBase);

        // Get child[0] again, verify NumberEditElement, wrap in IBP for array push
        UI::Element* numEditRaw = spinner->Child(0).Get();
        // ENDGAME B.3.9 CG2: TypeId_NumberEditElement -> UI::NumberEditElement::TypeId()
        // (the ported Runtime static always answers; the null-guard on the exe-side
        // global retires with the conversion).
        UI::NumberEditElement* numEditChecked = nullptr;
        if (::Object::IsDerivedFromNullSafe(numEditRaw, UI::NumberEditElement::TypeId()))
            numEditChecked = static_cast<UI::NumberEditElement*>(numEditRaw);

        // Push spinner's NumberEditElement to the spinner array at +0x40
        spinners.SetSize(spinners.GetSize() + 1, numEditChecked);

        // Push number element to the number array at +0x50
        numbers.SetSize(numbers.GetSize() + 1, number);

        // If this profession is the transient "default/laborer" profession (profList+0x30,
        // _default="laborer" in Profession.rsc), hide the two worker-count elements that
        // are meaningless for the unassigned-laborer pool: child[3] "numberAvailable" and
        // child[2] "ofText" (the "N of M" phrase). The spinner (locked in Slot8) and the
        // child[4] "buttonGoto" (cycle-to-a-laborer) REMAIN visible — retail keeps them.
        // NB: retail hides child index 2 here (verified in the 0x140076CA0 disassembly:
        // `MOV R8D,0x2` before the Child call), NOT index 4 — hiding 4 wrongly removed the
        // laborer's goto button.
        Profession* noneProfession = profListPtr->field_0x30;
        if (profDesc == noneProfession) {
            number->SetFlagsOnOff(1, 0);   // child[3] numberAvailable

            UI::Element* ofTextChild = cellElem->Child(2).Get();   // child[2] ofText
            ofTextChild->SetFlagsOnOff(1, 0);
        }

        // Advance table position
        tableCol++;
        if (tableCol >= tableElem->_rowCount) {
            tableRow++;
            tableCol = 0;
        }

        idBase++;
    }

    // Resize counts array to profCount.
    // B.3.2: Collection::Array<T>::SetSize — retail 0x1400021d0, stride 4 align 4,
    // grown tail left as the allocation found it (no init arm). Slot8 zeroes it below.
    counts.SetSize(profCount);

    // Call Slot8 (vtable +0x40) with Time(0.0)
    System::Time timeVal;
    RtSystem_Time_Ctor(&timeVal, 0.0f);
    OnUpdate(timeVal);   // slot 8, virtual
}

// ============================================================
// ProfessionUI::Slot8 (vtable +0x40) -- Update counts
// ============================================================
// Iterates all citizens, counts how many are assigned to each
// profession (via BST lookup), then updates the NumberEditElement
// values and min/max ranges.
//
// Original: 0x1400765A0.

// [8] 0x1400765a0
void ProfessionUI::OnUpdate(const System::Time& /*time*/) {
    // No set-up test (B8 RES, row 1062): 0x1400765a0 zeroes the counts first. Every
    // route opens the dialog through UserInterface::AddDialog, whose OnOpened (controller
    // slot 5, which allocates the caches) runs before the dialog is queued for updates.
    professionui_sa::Trace("Slot8", this);

    GameUpdateLoop* worldSingleton = GameUpdateLoop::_singleton;
    CitizenBehavior* citizenMgr = worldSingleton->m_citizenList;
    ProfessionList* profListPtr = worldSingleton->m_professionList;
    int profCount = profListPtr->professions.GetSize();

    // Zero out counts array
    int* countsNow = counts._data;
    for (int i = 0; i < profCount; i++) {
        countsNow[i] = 0;
    }

    // Iterate all citizens
    int citizenCount = citizenMgr->citizens._size;
    Object::InternalPtr<CitizenComponent>* citizenArray = citizenMgr->citizens._data;

    for (int i = 0; i < citizenCount; i++) {
        // CheckPtr on citizen IBP (original does this)
        CitizenComponent* citizen = citizenArray[i].Get();

        // Check if citizen is adult: age (float at +0x3C) >= adultAge (float at desc+0x44)
        CitizenDescription* citizenDesc = citizen->description;
        float birthAge = citizenDesc->adultAge;
        float currentAge = citizen->age;
        if (currentAge < birthAge) continue;

        // Check if citizen is not a student: school at +0x208 must be 0
        if (citizen->school != nullptr) continue;

        // CheckPtr again before reading profession (original does double CheckPtr)
        citizen = citizenArray[i].Get();

        // Look up profession in BST
        Profession* profKey = citizen->profession;

        // BST search
        ProfNode* node = professionIndexRoot;
        while (node) {
            Profession* nodeKey = node->key;
            if (profKey < nodeKey) {
                node = node->left;
            } else if (profKey > nodeKey) {
                node = node->right;
            } else {
                break;
            }
        }

        // 0x1400766ae-0x1400766b9: the find's result (null on a miss) is read at +0x8
        // with no test; every citizen's profession is one OnOpened indexed.
        countsNow[node->index]++;
    }

    // Update first spinner (index 0): set value and min/max to same value (locked)
    spinners[0]->SetValue(countsNow[0]);
    spinners[0]->SetMinMax(countsNow[0], countsNow[0]);

    // Update remaining spinners
    int spinnerN = spinners._size;
    for (int i = 1; i < spinnerN; i++) {
        spinners[i]->SetValue(countsNow[i]);
        // Min=0, Max=counts[i]+counts[0] (can reassign from "None" profession)
        spinners[i]->SetMinMax(0, countsNow[i] + countsNow[0]);
    }

    // Update number elements with actual worker counts per profession
    Profession** profArrayPtr = worldSingleton->m_professionList->professions.GetData();

    for (int p = 0; p < profCount; p++) {
        Profession* profKey = profArrayPtr[p];

        // Look up profession in world BST at citizenMgr+0x100 to get entity count
        ProfessionBucket* bstNode = citizenMgr->field_0x100._root;
        int entityCount = 0;

        while (bstNode) {
            Profession* nodeKey = bstNode->element.key;
            if (profKey < nodeKey) {
                bstNode = bstNode->left;
            } else if (profKey > nodeKey) {
                bstNode = bstNode->right;
            } else {
                entityCount = bstNode->element.entities._size;
                break;
            }
        }

        // Row 451 (B7 RES): 0x1400767e1-0x14007694a tests nothing in this walk -- the
        // re-found bucket (its +0x8 read even on a miss), the entity, every component slot
        // (`[[slot]+0x10]`), the WorkPlaceComponent (+0x50 read through the narrowed
        // pointer), its +0x10 owner and the WorkComponent. The two scans are
        // ComponentEntity::FindComponent<T>()'s shape (cells 0x1401c80e0 / 0x1401c8070).
        int totalWorkers = 0;
        for (int e = 0; e < entityCount; e++) {
            // Re-search BST each iteration (original does this to get fresh entity list)
            ProfessionBucket* bstNode2 = citizenMgr->field_0x100._root;
            ProfessionBucket* hit = nullptr;
            while (bstNode2) {
                Profession* nk = bstNode2->element.key;
                if (profKey < nk) { bstNode2 = bstNode2->left; }
                else if (profKey > nk) { bstNode2 = bstNode2->right; }
                else { hit = bstNode2; break; }
            }
            Object::InternalPtr<ComponentEntity>* freshEntityList = hit->element.entities._data;

            // CheckPtr on entity IBP (original does this)
            ComponentEntity* entityBP = freshEntityList[e].Get();

            // Wrap in IBP and CheckPtr (original does InternalBasePtr_Ctor + CheckPtr)
            Object::InternalPtr<WorkPlaceComponent> wpcIBP(entityBP->FindComponent<WorkPlaceComponent>());
            WorkPlaceComponent* workplaceComp = wpcIBP.Get();

            // Check if workplace is enabled (+0x50 != 0)
            if (workplaceComp->isAccepting != 0) {
                // The WorkComponent on the owner entity via +0x10
                ComponentEntity* parentEntity = workplaceComp->_entity.Get();
                Object::InternalPtr<WorkComponent> slcIBP(parentEntity->FindComponent<WorkComponent>());
                WorkComponent* storageLocComp = slcIBP.Get();

                int workerSlots = storageLocComp->workState;
                if (workerSlots == 0) {
                    workerSlots = storageLocComp->CalcMaxWorkers();   // 0x14012e5c0
                    storageLocComp->workState = workerSlots;
                }
                totalWorkers += storageLocComp->workState;
            }
        }

        // Set number element value (with CheckPtr, matching original)
        numbers[p]->SetValue(static_cast<int32_t>(totalWorkers));
    }
}

// ============================================================
// ProfessionUI::HandleSpinner (was FUN_1400763c0)
// ============================================================
// Handles spinner value change for profession assignment.
// When user adjusts the spinner for a profession:
//   - If new value > current count: add citizens to that profession
//     (tries idle -> working -> student -> any fallback)
//   - If new value < current count: remove citizens from that profession
// Then refreshes the display via Slot8 (vtable +0x40).
//
// Original: 0x1400763c0.

void ProfessionUI::HandleSpinner(int profIndex) {
    // NO g_wpcRosterOnly guard (removed 2026-08-17). It used to be held here because the
    // Attach/Detach visual block "faults on inert standalone objects (READ of 0x10 in
    // Detach's e710 getter when the player lowered a builder count)". That exact fault is
    // now guarded at the source — workplacecomponent.cpp:276 returns early on a null
    // accessor — so the premise expired and the guard outlived it. Holding it here was not
    // free: the same flag gates Attach's clear of the "no workers" status bit, so every
    // seat made through this spinner left the ? icon up over a fully-staffed building.
    // g_wpcRosterOnly is a world-BUILD shim (see its definition); a spinner click is
    // gameplay time, which is exactly when the visual is meant to run. See the block above
    // WorkPlaceUI idx9 in workplaceui.cpp for the full measurement.
    int64_t idx = (int64_t)profIndex;

    // Get spinner element at this index and read desired value
    int desired = spinners[idx]->GetValue();

    // Get current count from counts array
    int* countsNow = counts._data;
    int current = countsNow[profIndex];


    // Add citizens if desired > current
    // (LEFT: FindAvailableBuilding takes the citizen list and the profession as
    // integers — Game/exe_bodies.h, a wave-X family of another agent: one cast each below)
    if (current < desired) {
        uint64_t toAdd = (uint64_t)(uint32_t)(desired - current);
        do {
            CitizenBehavior* citizenList = GameUpdateLoop::_singleton->m_citizenList;
            Profession* profId = GameUpdateLoop::_singleton->m_professionList->professions.GetData()[idx];

            // Request a worker slot
            ComponentEntity* retBuf[2] = {nullptr, nullptr};
            citizenList->FindAvailableBuilding(retBuf, profId);   // 0x14008d100 (door misnamed fn_CitizenList_RequestWorker)
            ComponentEntity* slot = retBuf[0];

            // Try to find a citizen: idle -> working -> student -> any
            ComponentEntity* slotCopy = slot;
            CitizenComponent* findBuf[1] = {nullptr};
            CitizenComponent** result = citizenList->FindWorkerToRelocate(findBuf, &slotCopy, profId);   // 0x14008cbf0
            CitizenComponent* found = *result;

            if (found == nullptr) {
                if (slot != nullptr) {
                    slotCopy = slot;
                    CitizenComponent* fb2[1] = {nullptr};
                    result = citizenList->FindNearestHousedLaborer(fb2, &slotCopy, profId);   // 0x14008ca20
                    found = *result;
                    if (found != nullptr) goto next_iter;
                }
                slotCopy = slot;
                CitizenComponent* fb3[1] = {nullptr};
                result = citizenList->FindLaborerFromFullestHouse(fb3, &slotCopy, profId);   // 0x14008c5f0
                found = *result;

                if (found == nullptr) {
                    slotCopy = slot;
                    CitizenComponent* anyBuf = nullptr;
                    citizenList->FindIdleLaborer(&anyBuf, &slotCopy, profId);   // 0x14008c4b0
                    found = anyBuf;
                }
            }
next_iter:
            toAdd--;
        } while (toAdd != 0);
    }

    // Remove citizens if desired < current
    if (desired < current) {
        uint64_t toRemove = (uint64_t)(uint32_t)(current - desired);
        do {
            CitizenBehavior* citizenList = GameUpdateLoop::_singleton->m_citizenList;
            Profession* profession = GameUpdateLoop::_singleton->m_professionList->professions.GetData()[idx];
            citizenList->RemoveWorker(profession);   // 0x140091180
            toRemove--;
        } while (toRemove != 0);
    }

    // Refresh display: call Slot8 (vtable +0x40) with Time(0.0)
    System::Time timeVal;
    RtSystem_Time_Ctor(&timeVal, 0.0f);
    OnUpdate(timeVal);   // slot 8, virtual
}

// ============================================================
// ProfessionUI::HandleButton (was FUN_140076a30)
// ============================================================
// Handles button click to find and center camera on a citizen of
// the given profession. Cycles through eligible citizens using
// a rotation index (gotoIndex, +0x70) that wraps around.
//
// Only considers adult, non-student citizens (age >= adultAge, school == 0).
// Calls PlayerToolManager::CenterOnEntity to snap the camera and select
// the citizen in the EditTool.
//
// Original: 0x140076a30.

void ProfessionUI::HandleButton(int profIndex) {
    GameUpdateLoop* game = GameUpdateLoop::_singleton;

    CitizenBehavior* citizenMgr = game->m_citizenList;
    int citizenCount = citizenMgr->citizens._size;
    Object::InternalPtr<CitizenComponent>* citizenArray = citizenMgr->citizens._data;

    // Get the profession ID for this index (compared with the citizens' raw field)
    Profession* profId = game->m_professionList->professions.GetData()[profIndex];

    // Count eligible (adult, non-student) citizens
    int eligible = 0;
    for (int i = 0; i < citizenCount; i++) {
        CitizenComponent* citizen = citizenArray[i].Get();

        // Check adult: age >= adultAge (desc+0x44)
        CitizenDescription* citizenDesc = citizen->description;
        float birthAge = citizenDesc->adultAge;
        float currentAge = citizen->age;
        if (currentAge < birthAge) continue;

        // Check not a student: school at +0x208 must be 0
        if (citizen->school != nullptr) continue;

        eligible++;
    }

    if (eligible == 0) return;

    // Advance rotation index
    int* rotIdx = &gotoIndex;
    (*rotIdx)++;
    if (*rotIdx < 0) *rotIdx = 0;

    // Search for next matching citizen (same profession, adult, alive)
    int scanned = 0;
    while (true) {
        if (*rotIdx >= citizenCount) {
            *rotIdx = 0;
        }

        CitizenComponent* citizen = citizenArray[*rotIdx].Get();

        // Check if citizen matches this profession
        if (citizen->profession == profId) {
            citizen = citizenArray[*rotIdx].Get();

            // Check adult and not a student
            CitizenDescription* citizenDesc = citizen->description;
            float birthAge = citizenDesc->adultAge;
            float currentAge = citizen->age;

            if (currentAge >= birthAge && citizen->school == nullptr) {
                // Found matching citizen -- check if same profession still (re-read after CheckPtr)
                citizen = citizenArray[*rotIdx].Get();
                if (citizen->profession != profId) {
                    // Profession changed between checks -- still go to this citizen
                    return;
                }

                // Get citizen's entity (at +0x10)
                citizen = citizenArray[*rotIdx].Get();
                ComponentEntity* entityPtr = citizen->_entity.Get();

                // Center camera on this citizen
                UI::DialogInstance* dialogInst = _dialog;
                game->m_toolManager->CenterOnEntity(&dialogInst, &entityPtr);   // 0x140103040
                return;
            }
        }

        if (scanned >= eligible) {
            // Exhausted all eligible citizens without finding a match in this profession
            // Fall through to go to the current rotation citizen anyway
            citizen = citizenArray[*rotIdx].Get();
            if (citizen->profession != profId) {
                return;
            }

            citizen = citizenArray[*rotIdx].Get();
            ComponentEntity* entityPtr = citizen->_entity.Get();

            UI::DialogInstance* dialogInst = _dialog;
            game->m_toolManager->CenterOnEntity(&dialogInst, &entityPtr);   // 0x140103040
            return;
        }

        (*rotIdx)++;
        scanned++;
    }
}

// ============================================================
// ProfessionUI::Slot9 (vtable +0x48) -- Event handler
// ============================================================
// Handles spinner changes (msg 0x12) and button clicks (msg 0x15).
// Spinner: reassigns citizens between professions.
// Button: finds a citizen of that profession and centers camera on them.
//
// Original: 0x140076C10.

// [9] 0x140076c10
void ProfessionUI::OnElementEvent(Object::InternalPtr<UI::Element>* /*element*/, int32_t param3, int32_t param4,
                                  int32_t /*c*/, int32_t /*d*/, int32_t /*e*/, void* /*userData*/) {
    // 0x140076c10 starts `CMP R8D,0x12` / `0x15`: no set-up test (element events
    // reach an opened dialog only).
    professionui_sa::Trace("Slot9", this);
    ProfessionList* profListPtr = GameUpdateLoop::_singleton->m_professionList;
    int profCount = profListPtr->professions.GetSize();


    // Original logic: msg 0x15 handles BOTH spinner (300-range) and button (500-range).
    // msg 0x12 also handles button (500-range).
    if (param3 != 0x12) {
        if (param3 != 0x15) return;
        // msg 0x15: spinner value changed -- reassign citizens
        if (param4 >= 300 && param4 <= profCount + 300) {
            HandleSpinner(param4 - 300);
        }
    }
    // msg 0x12 or 0x15: button clicked -- find and center on citizen
    if (param4 > 499 && param4 <= profCount + 500) {
        HandleButton(param4 - 500);
    }
}

// ============================================================
// ~ProfessionUI (0x1400774e0, called by the scalar deleting dtor 0x140013450)
// ============================================================
// Retail frees the profession Set's nodes (0x14000e320: left subtree, right
// subtree, then the node), zeroes its root and count, then releases the three
// Arrays in the order +0x50, +0x40, +0x30 — which is what the members' own
// dtors do after this body (reverse declaration order) — and chains
// ~DialogController.
static void FreeProfessionIndex(ProfNode* node) {   // 0x14000e320
    if (node != nullptr) {
        FreeProfessionIndex(node->left);
        FreeProfessionIndex(node->right);
        System::MemoryAllocator::MemFree(node);
    }
}

ProfessionUI::~ProfessionUI() {
    ProfNode* root = professionIndexRoot;
    if (root != nullptr) {
        FreeProfessionIndex(root->left);
        FreeProfessionIndex(root->right);
        System::MemoryAllocator::MemFree(root);
    }
    professionIndexRoot = nullptr;
    professionIndexCount = 0;
}
