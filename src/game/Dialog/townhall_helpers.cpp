// src/game/Dialog/townhall_helpers.cpp
//
// Faithful ports of the small exe helper functions the TownHallUI reimplementation
// (townhallui.cpp) reaches through engine_funcs fn_* pointers. Those pointers
// resolve NULL unless the address is registered, which left the town-hall panel's
// setup/refresh dead. reg (standalone/reg_townhallui.cpp) maps each exe address to
// these ports.
//
//   0x14006abf0  GroupRating_Init      — cache "whole"/"half" heart/star ImageElements
//   0x14006aed0  GroupRating_Update    — show N whole + optional half icon from value/max
//   0x140015800  ProfArrayPush         — growable push, 0x40-byte profession rows
//                  (0x140015dd0 row copy + deep entity-list copy, 0x140015e50 shrink free)
//   0x1400159b0  ProdArrayPush         — growable push, 0x28-byte production rows
//   0x140090f20  QueryProductionStat   — clamped range-sum over townMgr+0x150 series
//   0x140015200  InventorySort (asc)   — order by localized resource name
//   0x140015500  InventorySort (desc)  — order by count desc, name tie-break
//   0x140091180  CitizenList_RemoveWorker — demote one worker of a profession to laborer
//
// All decompiled + verified in Ghidra this session (rounding const DAT_140172114 = 0.5f;
// name-compare in the sort comparators is System::String::operator< over the UIString
// localized name; see the per-function notes below).

#include <new>
#include "ui/UserInterface.h"
#include "object/External.h"
#include "object/TypeInfo.h"
#include "Component/exe_bodies.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Game/ProfessionList.h"
#include "system/String.h"
#include "collection/Collection.h"
#include <cstdint>
#include <cstring>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "ui/Element.h"      // ENDGAME B.3.6 UI callers (K1)
#include "ui/String.h"       // ENDGAME B.3.6 UI callers (K1)
#include "ui/StringTable.h"  // ENDGAME B.3.6 UI callers (K1)
#include "ui/ImageElement.h"  // X::TypeId() (B.3.9 CG2)
#include "Component/WorkPlaceComponent.h"   // X::TypeId() (B.3.9 CG2)
#include "Component/CitizenComponent.h"
#include "Component/CitizenDescription.h"
#include "Component/RawMaterialDescription.h"
#include "Entity/CitizenBehavior.h"
#include "Entity/ComponentEntity.h"
#include "Dialog/exe_bodies.h"
#include "Dialog/TownHallUI.h"   // TownHallInventoryEntry
#include "Dialog/introsort24.h"  // the inventory sorts' introsort core
#include "system/MemoryAllocator.h"   // B3R: the allocator doors

// B4 X-G1-2b / Y GAMEB: the entry points take their retail objects.

namespace {

// The row's element of that name, narrowed to an ImageElement (null if not one).
UI::Element* FindImage(UI::Element* row, const char* name) {
    UI::Element* elem = row->FindElement(System::String(name)).Get();
    if (elem && ::Object::IsDerivedFromNullSafe(elem, UI::ImageElement::TypeId())) return elem;
    return nullptr;
}

} // namespace

// ===========================================================================
// 0x14006abf0 — GroupRating::Init(rating, groupElemIBP*)
// For each child row of the group element, cache the "whole" and "half"
// ImageElements (heart/star icons) as InternalPtrs.
// ===========================================================================
// The group is TownHallUI::RatingGroup (Dialog/TownHallUI.h).
void TownHall_GroupRatingInit(TownHallUI::RatingGroup* group, UI::Element* const* groupElem) {
    int childCount = (*groupElem)->ChildCount();
    for (int i = 0; i < childCount; i++) {
        // child(i) -> FindElement("whole") -> cast ImageElement -> push
        UI::Element* whole = FindImage((*groupElem)->Child(i).Get(), "whole");
        group->whole.SetSize(group->whole.GetSize() + 1, whole);

        // child(i) -> FindElement("half") -> cast ImageElement -> push
        UI::Element* half = FindImage((*groupElem)->Child(i).Get(), "half");
        group->half.SetSize(group->half.GetSize() + 1, half);

        childCount = (*groupElem)->ChildCount();   // faithful re-read
    }
}

// ===========================================================================
// 0x14006aed0 — GroupRating::Update(rating, value, max)
// halfSteps = round(value/max * 2N). Whole icon i shows while 2i+1 < halfSteps;
// the half icon shows only at 2i+1 == halfSteps. (DAT_140172114 = 0.5f.)
// ===========================================================================
void TownHall_GroupRatingUpdate(TownHallUI::RatingGroup* group, float value, float maxValue) {
    int wholeCount = group->whole.GetSize();
    int halfSteps = (int)((value / maxValue) * (float)(wholeCount * 2) + 0.5f);
    int odd = 1;
    for (int i = 0; i < wholeCount; i++, odd += 2) {
        group->whole[i]->SetFlagsOnOff((odd >= halfSteps) ? 1u : 0u, (odd < halfSteps) ? 1u : 0u);
        group->half[i]->SetFlagsOnOff((odd != halfSteps) ? 1u : 0u, (odd == halfSteps) ? 1u : 0u);
    }
}

// ===========================================================================
// 0x140015800 — Array<TownHallUI::ProfessionRow>::SetSize (0x40-byte rows): the
// one erased core (rtcoll::SetSizeCore, which the hand-written push here was
// step for step) driven by the hand-derived row below.
// Row: +0x00 counts, +0x08..+0x28 five element InternalPtrs, +0x30 the
// entity list. Row copy (0x140015dd0) deep-copies the entity list
// (0x140015ec0-style ptr-array clone); realloc destroys every old row;
// shrink (0x140015e50) destroys the removed rows: a destroy frees the row's
// entity list. A deep copy together with a per-element destroy is the
// combination Collection.h's banner says a trait-derived row cannot produce.
// ===========================================================================
namespace {

using ProfRow = TownHallUI::ProfessionRow;
static_assert(sizeof(ProfRow) == 0x40, "profession row stride 0x40");
static_assert(offsetof(ProfRow, entities) == 0x30, "");

// 0x140015dd0: the row copy into the new (unconstructed) slot.
void ProfRow_Copy(ProfRow* dst, const ProfRow* src) {
    dst->workers = src->workers;
    dst->slots = src->slots;
    dst->label = src->label;                       // +0x08..+0x28 the five element InternalPtrs
    dst->desired = src->desired;
    dst->slotsNumber = src->slotsNumber;
    dst->buildingsNumber = src->buildingsNumber;
    dst->button = src->button;
    dst->entities._data = nullptr;
    dst->entities._size = 0;
    dst->entities._capacity = 0;
    // deep-copy entity list (ResidenceComponent_helper_5ec0)
    int cnt = src->entities._size;
    dst->entities._size = cnt;
    if (cnt > 0) {
        dst->entities._capacity = cnt;
        dst->entities._data = static_cast<ComponentEntity**>(
            System::MemoryAllocator::MemAlloc((uint64_t)cnt << 3, 8));
        for (int i = 0; i < cnt; i++) dst->entities._data[i] = src->entities._data[i];
    }
}

// 0x140015e50: each destroyed row's entity list goes (~ProfessionRow ->
// ~Array<ComponentEntity*>: MemFree, then the three fields zeroed).
void ProfRows_Destroy(ProfRow* rows, int count) {
    for (int i = 0; i < count; i++) rows[i].~ProfRow();
}

void AssignProfRow(uint8_t* d, const uint8_t* s) {
    rtcoll::AssignAs<ProfRow, &ProfRow_Copy>(d, s);
}

} // namespace

namespace TownHallRows {
// init null: the array only ever grows through the fill form (InitProfessions).
const rtcoll::ArraySpec kSpec_ProfessionRow = {          // exe 0x140015800
    "Array<TownHallUI::ProfessionRow>", 0x40, 8,
    &AssignProfRow, nullptr, &rtcoll::DestroyAs<ProfRow, &ProfRows_Destroy>, false,
    /*assign unwritten*/ 0, 0,
    /*init   unwritten*/ 0, 0,
    /*seed zero       */ 0, 0 };
}  // namespace TownHallRows

// 0x1400159b0 — Array<TownHallUI::ProductionRow>::SetSize (0x28-byte rows, plain
// copy, no per-element destroy): the trait-derived row, called as the member
// (TownHallUI::InitProduction).

// ===========================================================================
// 0x140090f20 — sum a town history series (CitizenBehavior +0x150) over
// [startTick, endTick] (clamped to [0, count-1]).
// ===========================================================================
int CitizenBehavior::QueryProductionStat(int seriesIdx, int startTick, int endTick) const {
    const Collection::Array<int32_t>* seriesBase = statHistory._data;
    if (!seriesBase) return 0;
    const Collection::Array<int32_t>& series = seriesBase[seriesIdx];
    const int32_t* data = series.GetData();
    int count = series.GetSize();
    if (startTick < 0) startTick = 0;
    if (endTick >= count) endTick = count - 1;
    if (!data || startTick > endTick) return 0;
    int sum = 0;
    for (int i = startTick; i <= endTick; i++) sum += data[i];
    return sum;
}

// ===========================================================================
// 0x140015200 / 0x140015500 — sort the UpdateProduction display array
// (0x18-byte entries: +0x00 entity, +0x08 RawMaterialDescription*, +0x10 count).
// Both are retail's 0x18-row introsort (Dialog/introsort24.h) with their own
// comparator, read in Ghidra:
//   0x140015200 (small sort 0x140015ad0), comparator 0x14007c710: the localized
//     names, UIString(desc+0x28 table, desc+0x30 key) -> System::String, compared
//     with System::String::operator<; desc read unguarded.
//   0x140015500 (small sort 0x140015c40, the comparator inlined), comparator
//     0x14007c620: count greater-first, the name compare on a tie.
// The two "phantom" trailing arguments are the callers' (&sortCombo, null); the
// comparators never read them.
// ===========================================================================
namespace {

// The entry is TownHallInventoryEntry (Dialog/TownHallUI.h).
using InvEntry = TownHallInventoryEntry;

// The raw material's localized name: UIString(desc +0x28 table, +0x30 key) -> System::String.
System::String LocalizedName(const RawMaterialDescription* desc) {
    UI::String text(::Object::ExternalPtr<UI::StringTable>(desc->_stringTable), desc->_name);
    return text.operator System::String();
}

// 0x14007c710 — the name compare.
bool InvNameLess(const InvEntry* a, const InvEntry* b) {
    System::String sysA = LocalizedName(a->desc);
    System::String sysB = LocalizedName(b->desc);
    return sysA < sysB;
}

// 0x14007c620 — count descending, the name compare on a tie.
bool InvDescLess(const InvEntry* a, const InvEntry* b) {
    if (a->value > b->value) return true;
    if (a->value < b->value) return false;
    return InvNameLess(a, b);
}

} // namespace

extern "C" void TownHall_InventorySortAsc(TownHallInventoryEntry* items, int count, void*, void*) {
    IntroSort24::Sort(items, count, &InvNameLess);   // 0x140015200
}
extern "C" void TownHall_InventorySortDesc(TownHallInventoryEntry* items, int count, void*, void*) {
    IntroSort24::Sort(items, count, &InvDescLess);   // 0x140015500
}

// ===========================================================================
// 0x140091180 — CitizenList::RemoveWorker(townMgr, professionKey): demote one
// worker of the profession back to laborer. Faithful three-pass order:
//   1. an adult, non-student citizen of the profession with NO workplace
//      — the cheapest demotion;
//   2. else the profession's workplace (the town's profession tree, +0x100)
//      with the most rostered workers — pull its first rostered worker;
//   3. else any adult, non-student citizen of the profession.
// Demotion = SetWorkplace(null) then profession = laborer (ProfessionList +0x30).
// Already-laborer-and-unassigned citizens return unchanged.
// ===========================================================================
namespace {

// The town's profession tree (CitizenBehavior +0x100) is ProfessionBucket
// (Entity/CitizenBehavior.h): the key is the ExternalPtr<Profession>, compared
// as an unsigned qword; the value the profession's workplaces.

// The workplace's WorkPlaceComponent (the tree's null entity test kept).
WorkPlaceComponent* FindWorkPlaceComp(ComponentEntity* entity) {
    if (!entity) return nullptr;
    return entity->FindComponent<WorkPlaceComponent>();
}

// An adult (age >= adultAge), not at school
bool IsWorkingAge(const CitizenComponent* cz) {
    return cz->description->adultAge <= cz->age && cz->school == nullptr;
}

// SetWorkplace(null) + profession = laborer. Returns without touching the
// citizen if it is already an unassigned laborer.
void DemoteToLaborer(CitizenComponent* citizen, Profession* laborer) {
    if (citizen->profession == laborer && citizen->workplace == nullptr) {
        return;
    }
    citizen->SetWorkplace(nullptr);   // 0x140111ee0
    citizen->profession = laborer;
}

} // namespace

void CitizenBehavior::RemoveWorker(const Profession* profession) {
    // Row 1142: the loop singleton and its ProfessionList are read with no test
    // (0x140091199 MOV RAX,[0x1401c3dd8]; 0x1400911a6 MOV RDI,[RAX+0xb0];
    // 0x1400911b0 MOV RDI,[RDI+0x30]).
    Profession* laborer = GameUpdateLoop::_singleton->m_professionList->laborerProfession;
    // `profession` is the ExternalPtr<Profession> the callers hold (compared as an unsigned qword).

    // Pass 1: idle (no-workplace) adult worker of this profession.
    int cnt = citizens._size;
    for (int i = 0; i < cnt; i++) {
        CitizenComponent* cz = citizens._data[i].Get();
        if (IsWorkingAge(cz) && cz->profession == profession && cz->workplace == nullptr) {
            DemoteToLaborer(cz, laborer);
            return;
        }
    }

    // Pass 2: the profession's tree node — the workplace with the most
    // rostered workers.
    ProfessionBucket* node = workplacesByProfession._root;
    ProfessionBucket* found = nullptr;
    while (node) {
        const Profession* k = node->element.key;
        if (profession < k)      node = node->left;
        else if (profession > k) node = node->right;
        else { found = node; break; }
    }
    if (found && found->element.entities._size > 0) {
        int wpCount = found->element.entities._size;
        ComponentEntity* bestEntity = nullptr;
        int bestWorkers = 0;
        for (int i = 0; i < wpCount; i++) {
            ComponentEntity* ent = found->element.entities._data[i].Get();
            WorkPlaceComponent* comp = FindWorkPlaceComp(ent);
            if (!comp) continue;
            int workers = comp->workers._size;
            if (bestWorkers < workers) { bestEntity = ent; bestWorkers = workers; }
        }
        if (bestEntity) {
            WorkPlaceComponent* comp = FindWorkPlaceComp(bestEntity);
            if (comp && comp->workers._data != nullptr) {
                CitizenComponent* worker = comp->workers[0].Get();
                if (worker) { DemoteToLaborer(worker, laborer); return; }
            }
        }
    }

    // Pass 3: any adult, non-student citizen of the profession.
    cnt = citizens._size;
    for (int i = 0; i < cnt; i++) {
        CitizenComponent* cz = citizens._data[i].Get();
        if (IsWorkingAge(cz) && cz->profession == profession) {
            DemoteToLaborer(cz, laborer);
            return;
        }
    }
}
