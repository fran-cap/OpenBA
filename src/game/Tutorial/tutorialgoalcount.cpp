// src/game/Goal/tutorialgoalcount.cpp
// Clean reimplementation of TutorialGoalCount vtable methods.
//
// TutorialGoalCount (0x50 bytes) — tutorial goal counting items/buildings/citizens.
// Vtable: 0x140160950
// Extends TutorialGoal -> Object::External
//
// The Check function (Slot8) is a large switch on countMode (+0x20) with 11 cases.
// Each case counts a different game metric and compares against targetCount (+0x24).
// If greaterOrEqual (+0x28), checks count >= target; else checks count == target.
//
// Decompiled from Application-x64.exe using Ghidra.

#include "Tutorial/TutorialGoalCount.h"
#include "Object/factory_registration.h"
#include "ui/UserInterface.h"
#include "object/External.h"
#include "Entity/exe_bodies.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Tutorial/exe_bodies.h"
#include "Goal/goal_exe_helpers.h"
#include "object/TypeInfo.h"
#include "system/String.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch D
#include "Component/ComponentDescription.h"
#include "Game/Profession.h"
#include <new>
#include "ui/SpinnerElement.h"   // ENDGAME B.3.6 UI callers (K4)
#include "ui/Element.h"
#include "Entity/CitizenBehavior.h"
#include "Entity/ComponentEntity.h"
#include "Component/CitizenComponent.h"
#include "Component/CitizenDescription.h"
#include "Component/StateMachineComponent.h"
#include "Component/StorageComponent.h"
#include "Component/TradeComponent.h"
#include "Component/WorkPlaceComponent.h"
#include "Game/Map.h"
#include "Component/BuildingState.h"

// ============================================================
// Exe-internal helpers, called by name:
//   0x1400265b0 NamedExternalRef::Resolve (Goal/goal_exe_helpers.h) -- resolve the
//               External reference named by a String (lazy load)
//   0x140092010 CitizenBehavior::CountByTemplate   -- the +0x160 tree's bucket count
//   0x1400910a0 CitizenBehavior::CountByProfession -- the +0x100 tree's
//   0x14012fbb0 WorkPlaceComponent::GetMaxWorkers -- available job slots of a
//               WorkPlaceComponent (WorkComponent+0x30, lazily via 0x14012e5c0)
//   0x140013920 the SpinnerElement narrowing (IsDerivedFrom, else null), inlined
//   0x1400e1870 FindUIElementByPath_Impl (find_ui_element_by_path.cpp)
// ============================================================

// 0x1401c7318 is ComponentDescription's cell (cases 0, 7, 10), 0x1401c5a98 is
// Profession's (cases 1, 3, 6) (Table 1): T::TypeId() below.

// ============================================================
// BST lookup helpers — search a binary search tree keyed by pointer
// ============================================================
// The CitizenBehavior entity-bucket trees (Goal/goal_exe_helpers.h): key +0x00,
// left +0x20, right +0x28.
template <class Node, class Key>
static Node* BSTFind(Node* node, const Key* key) {
    while (node != nullptr) {
        if (key < node->element.key) {
            node = node->left;
        } else if (key > node->element.key) {
            node = node->right;
        } else {
            return node;  // found
        }
    }
    return nullptr;
}

// The per-building trade-order tree (TradeComponent +0x60) is a DIFFERENT node
// layout from the world-manager trees above (TradeResourceNode:
// { key +0x00, value +0x08, parent +0x10, left +0x18, right +0x20 }),
// and retail's own case-7 descent (0x1400df290) confirms it:
//   if (key < *node) node = node[3];        // left  = +0x18
//   else { if (key <= *node) { v = node[1]; break; } node = node[4]; }   // right = +0x20
//
// Walking it with the +0x20/+0x28 offsets above reads the RIGHT child as LEFT and
// runs off the end of the node as RIGHT. That still "works" for a key sitting at
// the ROOT, which is why the tutorial's leather-coat goal passed while the iron-tool
// goal — a non-root node — read 0 forever and stalled the trade tutorial.
static const TradeResourceNode* TradeOrderBSTFind(const TradeResourceNode* node,
                                                  const ComponentDescription* key) {
    while (node != nullptr) {
        if (key < node->resource) {
            node = node->left;   // left  (+0x18)
        } else if (key > node->resource) {
            node = node->right;  // right (+0x20)
        } else {
            return node;  // found
        }
    }
    return nullptr;
}

// ============================================================
// Resolve the external type pointer from typeRef
// ============================================================
// Calls FUN_1400265b0 to ensure the external ref is loaded, then reads the
// resolved External and checks IsDerivedFrom T. Returns it if it matches, or
// null if not.
template <class T>
static const T* ResolveTypePtr(NamedExternalRef& ref) {
    ref.Resolve();
    const Object::External* typePtr = ref.resolved;
    if (typePtr) {
        if (Object::IsDerivedFromNullSafe(typePtr, T::TypeId())) {
            return static_cast<const T*>(typePtr);
        }
    }
    return nullptr;
}

// ============================================================
// TutorialGoalCount construction / destruction (ENDGAME B.3.8 block GOAL)
// ============================================================
// ctor 0x1400dfa60 (inlined in create 0x1400277b0; createInPlace 0x1400277f0
// null-checks then runs it): External(), the TutorialGoal and
// TutorialGoalCount stamps (the compiler's), then the fields.
TutorialGoalCount::TutorialGoalCount()
    : countMode(0), targetCount(0),          // one qword store at +0x20
      greaterOrEqual(false), _pad29{},
      typeRef(),                             // null name, generation sentinel
      elementName() {}                       // null string

// The dtor inside [0] 0x140027820: ~String +0x48 (0x14002784b), ~String +0x30
// (0x14002785b); the compiler re-stamps TutorialGoal and runs ~External.
// B4 Y GAMEA (H5): both are the members', after this empty body, in reverse
// declaration order.
TutorialGoalCount::~TutorialGoalCount() {}

// ============================================================
// TutorialGoalCount::Serialize (vtable +0x28, 0x1400df900)
// ============================================================
// Serializes all fields. On a read (stream direction 0) resolves the external
// ref at +0x30.
bool TutorialGoalCount::Serialize(IO::Stream& stream) {
    TutorialGoal::Serialize(stream);   // QUALIFIED: the base body (an empty Serializer scope)

    // The serializer for our fields.
    IO::Serializer ser(stream, 0);

    // +0x20 through the UINT door (rw_vtbl::kScalarUInt, readerWriter +0x98).
    ser.Serialize(countMode, nullptr);

    // targetCount (+0x24) as int
    ser.Serialize(targetCount, nullptr);

    // the type's name (+0x30) as String
    typeRef.name.Serialize(ser);

    // On a read, resolve the external ref: retail tests the STREAM DIRECTION, not the
    // record version (0x1400df988: MOV RAX,[RSP+0x28]; CMP dword ptr [RAX+0x8],0; JNZ).
    if (static_cast<int>(ser.GetDirection()) == 0) {
        typeRef.Resolve();
    }

    // greaterOrEqual (+0x28) as bool
    ser.Serialize(greaterOrEqual, nullptr);

    // elementName (+0x48) as String
    elementName.Serialize(ser);
    return true;
}

// ============================================================
// TutorialGoalCount::Reconstruct [6] 0x140027800 / Destruct [7] 0x140001640
// ============================================================
// `~T()` (slot 0, flags 0), then the ctor (0x1400dfa60) on `this`.
Object::External* TutorialGoalCount::Reconstruct() {
    this->~TutorialGoalCount();
    return ::new (static_cast<void*>(this)) TutorialGoalCount();
}
void TutorialGoalCount::Destruct() { this->~TutorialGoalCount(); }


// ============================================================
// TutorialGoalCount::Check (vtable +0x40, 0x1400df290)
// ============================================================
// 11-case switch on countMode (+0x20). Counts a game metric and
// compares against targetCount (+0x24). If greaterOrEqual (+0x28),
// returns count >= target; otherwise returns count == target.
//
// Count modes:
//   0: Building count by type (the +0x160 tree)
//   1: Citizens assigned to a profession type
//   2: the +0x20 list's count (CitizenBehavior +0x28)
//   3: Profession slot count by type (the +0x100 tree)
//   4: StorageLocation count (CitizenBehavior +0x08)
//   5: Map +0xF0 > 0
//   6: Available job slots across buildings of a profession type
//   7: Trading-post stock total by resource type
//   8: Total stored items across all buildings
//   9: UI spinner/slider element value (by elementName path)
//  10: Built (state != 0) building count by type
//
// param2: unused (game world passed implicitly via g_pGameWorldSingleton)
// param3: InternalBasePtr* — retail's case 9 CheckPtr'd it and read nothing else
// from it, so with CheckPtr deleted (B.3.3 step 6D) nothing here reads it. The
// PARAMETER stays: this is retail's signature and the caller still passes it.
//
// Each int result is answered as `!= 0` (the bool return), exactly as the
// member delegate did before this fold.
// [8] 0x1400df290 — the 11-mode count switch; StepExecute reads the result.
bool TutorialGoalCount::IsComplete(const System::Time* param2, void* param3) {
    (void)param2;
    (void)param3;

    uint32_t mode = countMode;
    int count = 0;

    // Row 82 (B7 RES): retail 0x1400df290 tests no singleton, no list, no entity and no
    // found component: every case reads [g_pGameWorldSingleton+0xf0] / +0xf8 and the
    // FindComponent results (0x140002e10 / 0x140015140 / 0x140006270 / 0x140003ea0)
    // through CheckPtr only. Its only tests are the two BST finds' results (cases 7 / 10)
    // and case 9's element / narrowing.
    switch (mode) {
    // --------------------------------------------------------
    // Case 0: Count buildings of a type
    // --------------------------------------------------------
    case 0: {
        const ComponentDescription* typePtr = ResolveTypePtr<ComponentDescription>(typeRef);
        CitizenBehavior* worldData = GameUpdateLoop::_singleton->m_citizenList;
        count = (int)worldData->CountByTemplate(typePtr);
        break;
    }

    // --------------------------------------------------------
    // Case 1: Count citizens assigned to a specific profession
    // --------------------------------------------------------
    // Iterates the citizen list (CitizenBehavior +0xA0, count +0xA8). For each
    // adult (age >= the description's adult age) not at school, checks if their
    // assigned profession matches the target type. Retail reads [[c+0x288]+0x44] and
    // [c+0x3c] of each CheckPtr'd citizen with no test.
    case 1: {
        CitizenBehavior* worldData = GameUpdateLoop::_singleton->m_citizenList;
        int citizenCount = worldData->citizens._size;
        int matched = 0;
        for (int i = 0; i < citizenCount; i++) {
            const CitizenComponent* citizen = worldData->citizens._data[i].Get();

            float age = citizen->age;
            const CitizenDescription* desc = citizen->description;
            if (age < desc->adultAge) continue;

            if (citizen->school != nullptr) continue;

            // Resolve target profession type and compare
            const Profession* targetProf = ResolveTypePtr<Profession>(typeRef);
            if (citizen->profession == targetProf) {
                matched++;
            }
        }
        count = matched;
        break;
    }

    // --------------------------------------------------------
    // Case 2: the +0x20 list's count (CitizenBehavior +0x28)
    // --------------------------------------------------------
    case 2: {
        CitizenBehavior* worldData = GameUpdateLoop::_singleton->m_citizenList;
        count = worldData->residences._size;
        break;
    }

    // --------------------------------------------------------
    // Case 3: Profession slot count by type
    // --------------------------------------------------------
    case 3: {
        const Profession* typePtr = ResolveTypePtr<Profession>(typeRef);
        CitizenBehavior* worldData = GameUpdateLoop::_singleton->m_citizenList;
        count = (int)worldData->CountByProfession(typePtr);
        break;
    }

    // --------------------------------------------------------
    // Case 4: StorageLocation count — the DynArray at townMgr+0x00/+0x08
    // --------------------------------------------------------
    // Retail is just `*(uint*)(townMgr + 8)`. That is NOT "total population" as this
    // case was previously labelled: townMgr+0x00 is the DATA pointer and +0x08 the
    // COUNT of a DynArray of storage-location entity IBPs — the array
    // StorageLocationComponent's remove path (0x14012d270) erases from with
    // DynArray8_EraseByValue(townMgr, entityIBP). The .rsc name for this CountType is
    // `StorageLocation` (goalstoragebarnfinished).
    //
    // This matters because TutorialGoalCount compares with EQUALITY unless the goal
    // sets `bool _greaterEqual = true` — only two goals in the whole tutorial do. So
    // `goalstoragebarnfinished { _count = 3 }` passes ONLY at exactly 3, and an
    // off-by-one here is a permanent stall, not a delay. Observed 2026-09-04 at
    // stepCutter7 (step 60): count=4 target=3 ge=0, forever.
    case 4: {
        CitizenBehavior* worldData = GameUpdateLoop::_singleton->m_citizenList;
        count = worldData->storageLocations._size;
        break;
    }

    // --------------------------------------------------------
    // Case 5: Map +0xF0 (a BST count) > 0
    // --------------------------------------------------------
    case 5: {
        Map* worldData2 = GameUpdateLoop::_singleton->m_game;
        count = (worldData2->pendingRoadCells.count > 0) ? 1 : 0;
        break;
    }

    // --------------------------------------------------------
    // Case 6: Available job slots (work capacity) across buildings of a profession type
    // --------------------------------------------------------
    // Gets the profession's bucket count from the +0x100 tree, then for each
    // entity of that profession finds its WorkPlaceComponent, checks it is
    // accepting (+0x50), and sums its capacity (0x14012fbb0).
    case 6: {
        // Resolve profession type (re-resolved inside loop to match original)
        const Profession* profType = ResolveTypePtr<Profession>(typeRef);
        CitizenBehavior* worldData = GameUpdateLoop::_singleton->m_citizenList;

        int profCount = (int)worldData->CountByProfession(profType);
        int total = 0;

        for (int i = 0; i < profCount; i++) {
            // Re-resolve profession type each iteration (matches original)
            const Profession* curProfType = ResolveTypePtr<Profession>(typeRef);

            // Re-read worldData each iteration (matches original)
            worldData = GameUpdateLoop::_singleton->m_citizenList;
            // Retail reads the miss's null node's +0x8 (no test after the find).
            ProfessionBucket* node = BSTFind(worldData->workplacesByProfession._root, curProfType);

            ComponentEntity* entity = node->element.entities._data[i].Get();

            // Find WorkPlaceComponent (FUN_140002e10, type 0x1401c80e0)
            WorkPlaceComponent* workplace = entity->FindComponent<WorkPlaceComponent>();

            // Check the workplace is accepting (+0x50)
            if (workplace->isAccepting) {
                // Get work capacity (available job slots)
                total += workplace->GetMaxWorkers();   // 0x14012fbb0
            }
        }
        count = total;
        break;
    }

    // --------------------------------------------------------
    // Case 7: Trading-post stock total by resource type
    // --------------------------------------------------------
    // Iterates the +0x30 entity list (count +0x38), finds each one's
    // TradeComponent, and sums the stock tree's (+0x60) quantity for the type.
    case 7: {
        CitizenBehavior* worldData = GameUpdateLoop::_singleton->m_citizenList;
        int entityCount = worldData->tradePosts._size;
        Object::InternalPtr<ComponentEntity>* entityArray = worldData->tradePosts._data;
        int total = 0;

        for (int i = 0; i < entityCount; i++) {
            ComponentEntity* entity = entityArray[i].Get();

            // Find TradeComponent (FUN_140015140, the tree's fn_FindComponent_Building)
            TradeComponent* building = entity->FindComponent<TradeComponent>();

            // Resolve the resource description type
            const ComponentDescription* typePtr = ResolveTypePtr<ComponentDescription>(typeRef);

            // Search the trading post's stock tree at +0x60 for the type
            const TradeResourceNode* storageNode = TradeOrderBSTFind(building->stock.root, typePtr);

            int storageCount = 0;
            if (storageNode != nullptr) {
                storageCount = storageNode->quantity;
            }
            total += storageCount;

            // Re-read worldData for next iteration (may change)
            worldData = GameUpdateLoop::_singleton->m_citizenList;
        }
        count = total;
        break;
    }

    // --------------------------------------------------------
    // Case 8: Total stored items across all buildings
    // --------------------------------------------------------
    // Iterates the +0x30 entity list, finds each StorageComponent, sums the
    // item counts of its slots.
    case 8: {
        CitizenBehavior* worldData = GameUpdateLoop::_singleton->m_citizenList;
        int entityCount = worldData->tradePosts._size;
        Object::InternalPtr<ComponentEntity>* entityArray = worldData->tradePosts._data;
        int total = 0;

        for (int i = 0; i < entityCount; i++) {
            ComponentEntity* entity = entityArray[i].Get();

            // Find StorageComponent
            StorageComponent* storage = entity->FindComponent<StorageComponent>();

            int itemCount = storage->storageSlots._size;
            for (int j = 0; j < itemCount; j++) {
                total += storage->storageSlots[j]._count;
            }

            // Re-read worldData for next iteration
            worldData = GameUpdateLoop::_singleton->m_citizenList;
        }
        count = total;
        break;
    }

    // --------------------------------------------------------
    // Case 9: UI spinner/slider element value
    // --------------------------------------------------------
    // Finds a UI element by the elementName path (+0x48), narrows it to
    // SpinnerElement (0x140013920), reads its value if not active.
    case 9: {
        Object::InternalPtr<UI::Element> found;
        FindUIElementByPath_Impl(&found, &elementName, nullptr);   // 0x1400e1870

        UI::Element* element = found.Get();
        if (!element) { break; }

        const UI::SpinnerElement* slider =
            Object::IsDerivedFromNullSafe(element, UI::SpinnerElement::TypeId())
                ? static_cast<const UI::SpinnerElement*>(element) : nullptr;
        if (!slider) { break; }

        if (slider->IsActive()) {
            // Retail refuses to read a spinner mid-edit. If this never clears, the
            // goal stalls forever with the right number visible on screen.
            break;
        }

        count = slider->GetValue();
        break;
    }

    // --------------------------------------------------------
    // Case 10: Built building count by type
    // --------------------------------------------------------
    // Gets the template's bucket count, then for each of its entities finds the
    // StateMachineComponent and counts a reported state other than
    // BuildingState::ClearSite (0).
    case 10: {
        const ComponentDescription* typePtr = ResolveTypePtr<ComponentDescription>(typeRef);
        CitizenBehavior* worldData = GameUpdateLoop::_singleton->m_citizenList;

        int buildingCount = (int)worldData->CountByTemplate(typePtr);
        int occupied = 0;

        for (int i = 0; i < buildingCount; i++) {
            // Re-resolve the type each iteration (original code does this)
            const ComponentDescription* curTypePtr = ResolveTypePtr<ComponentDescription>(typeRef);
            worldData = GameUpdateLoop::_singleton->m_citizenList;
            TemplateBucket* node = BSTFind(worldData->entitiesByTemplate._root, curTypePtr);

            // Retail's miss builds InternalBasePtr(null) and finds on it (0x1400df4dc).
            ComponentEntity* entity = nullptr;
            if (node) {
                entity = node->element.entities._data[i].Get();
            }

            StateMachineComponent* buildState = entity->FindComponent<StateMachineComponent>();

            // The reported state: +0x52 pending, or +0x50 when nothing is pending.
            int8_t checkFlag = buildState->stateMachine.pendingState;
            if (checkFlag < 0) {
                checkFlag = buildState->stateMachine.currentInput;
            }
            if (checkFlag != StateByte(BuildingState::ClearSite)) {
                occupied++;
            }
        }
        count = occupied;
        break;
    }

    default:
        // Unknown mode — count stays 0, skip comparison
        goto compare;
    }

compare:

    // Final comparison: greaterOrEqual ? (count >= target) : (count == target)
    if (greaterOrEqual) {
        return (count >= targetCount) ? 1 : 0;
    }
    return (count == targetCount) ? 1 : 0;
}


// [9] 0x1400277f0: null-check, then the ctor (0x1400dfa60) on `this`.
Object::External* TutorialGoalCount::Construct() {
    return ::new (static_cast<void*>(this)) TutorialGoalCount();
}

// create 0x1400277b0: MemAlloc(0x50, 8), then the ctor.
::Object::Base* TutorialGoalCount::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(TutorialGoalCount), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(::new (p) TutorialGoalCount());
}
// createInPlace 0x1400277f0.
::Object::Base* TutorialGoalCount::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(::new (storage) TutorialGoalCount());
}

DEFINE_EXE_TYPE(TutorialGoalCount, "TutorialGoal")
