// src/game/Component/cropfieldcomponent_states.cpp
// CropFieldComponent controller state machine.
//
// Structurally the Orchard's twin: a ControllerComponent (0x68) driving a drag-created
// area through plant / tend / harvest, with its own private state table. Where the
// orchard has six states (2/9/10/12/11/20) the crop field has FOUR bodies (2/9/10/11)
// plus two transition-only targets (3 and 21).
//
// ── How this table was recovered ──────────────────────────────────────────────────
// CropFieldDescription_Setup (0x14014edb0) tail-calls the populator. Ghidra decompiles
// that call as `TownHallUI_Ctor()` and names the whole helper family `TownHallUI_*`,
// which is WRONG — the disassembly is unambiguous:
//     14014edb4: CALL 0x14011b150
//     14014edb9: MOV dword ptr [0x1401d7ad8],EAX
// So the real populator is 0x14011b150, the state array is 0x1401d6b50 and the count is
// 0x1401d7ad0. (0x1401d7ad8, the thing Setup writes, is a separate "registered ok" flag
// — do not mistake it for the count.) Every handler address below was read from that
// function's DISASSEMBLY operands, not from Ghidra's symbol names.
//
// ── Register-helper argument order (0x1400330d0), verified from its own disassembly ──
// It stages the new entry in a stack buffer at RSP+0x30 and memcpy-style installs it:
//     [RSP+0x30] = ECX          -> +0x00 id
//     [RSP+0x38] = RDX          -> +0x08 enter
//     [RSP+0x40] = 0            -> +0x10 trans30   (always zero for this controller)
//     [RSP+0x48] = [RSP+0x170]  -> +0x18 trans38   (the 5th/stack argument)
//     [RSP+0x50] = R8           -> +0x20 update
// i.e. the shape is sub_330d0(id, enter, UPDATE, _, TRANS38) — update and trans38 are
// NOT in source order, and reading them the other way round would silently swap the
// per-frame handler with the update handler. Companions: 0x1400331f0 adds a transition,
// 0x1400332a0 adds an event.
//
// ── The recovered table ───────────────────────────────────────────────────────────
//   state 2   enter 0x14011a920  trans38 0x14011a860   -> 3, 9        event 3 -> 0x14011a7b0
//   state 9   enter 0x14011a310  trans38 0x14011a1e0   -> 3, 10
//   state 10  enter 0x14011a100  trans38 0x140119fd0
//                                update  0x140119f70   -> 3, 21, 11   event 4 -> 0x140119f00
//   state 11  enter 0x140119e60  trans38 0x140119d80   -> 3, 21
// States 3 and 21 are transition TARGETS with no registered body — exactly as the
// orchard table leaves its state 3 — so they are deliberately absent here too.
//
// ── STATUS: ALL ELEVEN HANDLER BODIES ARE PORTED ───────────────────────────────
// This block used to read "FIRST PASS: the handler BODIES are not ported yet, so every
// handler is registered as nullptr". That has been FALSE since the crop-field wave
// (81dc93c "reaches state 10", cbe5ab0 "HARVESTS", ce26616 "61 harvested"); the comment
// simply outlived the port that replaced it — the same trap as
// reference_stub_comment_outlived_the_port_that_replaced_it. Corrected 2026-09-06 after
// re-measuring, so the next reader does not re-port work that is already done.
//
// Every address in the table above now has a body in this file, and each body carries
// its retail address in its own banner comment:
//   0x14011a920 State2Enter   0x14011a860 State2PerFrame   0x14011a7b0 Event3Plant
//   0x14011a310 State9Enter   0x14011a1e0 State9PerFrame
//   0x14011a100 State10Enter  0x140119fd0 State10PerFrame  0x140119f70 State10Update
//                                                          0x140119f00 Event4Harvest
//   0x140119e60 State11Enter  0x140119d80 State11PerFrame
// The `nullptr` arguments that remain in the CropFieldCtrl_PopulateTable calls below are
// the trans30 slot, which retail leaves genuinely empty for this controller (the
// register helper at 0x1400330d0 hard-writes 0 into +0x10 for every entry) — they are
// NOT unported handlers. Read TableRegisterState's signature before assuming otherwise:
// it is (id, enter, TRANS30, TRANS38, UPDATE), so the per-frame body sits in the 4th
// argument and the update body in the 5th.

#include "Component/controller_state_table.h"
#include "Component/CropFieldComponent.h"
#include "Work/PlantArea.h"        // ENDGAME B.3.8 MI-B: the real Work classes
#include "Work/HarvestArea.h"
#include "Work/TendArea.h"
#include <new>
#include "object/TypeInfo.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "system/Value.h"   // ENDGAME B.3.4b System caller batch
#include "Component/NaturalResourceDescription.h"   // ENDGAME B.3.9 C1: 0x1401c7b68 cell class
#include "Component/RawMaterialDescription.h"        // ENDGAME B.3.9 C1: 0x1401c7cb8 cell class
#include "Component/exe_bodies.h"
#include "ext/registry.h"   // Ext::Fix().crop_planting_exit
#include "system/MemoryAllocator.h"
#include "Work/exe_bodies.h"
#include "Game/exe_constants.h"
#include <cstring>

// Watched by the citizen idle claim walk (citizenai_housing_idle.cpp) so we can tell
// "never reached" from "reached and rejected".
extern "C" void* g_pCropPlantWork = nullptr;

// CropFieldCtrlTag lives in CropFieldComponent.h (the member type needs it).
using CropFieldDispatch = ::ControllerLive::Dispatcher<CropFieldCtrlTag>;

// ── Helpers ───────────────────────────────────────────────────────────────────────
// 0x140002e10 FindComponent_WorkPlace(entity, outIBP) -> WorkPlaceComponent
//             (AttendanceComponent_GetJobHostComponent), called directly.
// 0x14012fc00 WorkPlaceComponent_SetAccepting(wpc, accepting) -> drives WPC+0x50.
// .rdata: 0x1401721d4 = 33.0f, 0x14017225c = 3600.0f, 0x1401721b8 = 12.0f (exe_constants.h).
#include "Component/WorkPlaceComponent.h"
#include "Component/NaturalResourceComponent.h"
#include "Component/WorkComponent.h"
#include "Component/MapComponent.h"
#include "Component/ComponentDescription.h"
#include "Entity/ComponentEntity.h"
#include "Game/Map.h"
#include "Game/Calendar.h"
#include "Game/WeatherSimulation.h"

namespace {

float BitsF(uint32_t bits) { float f; std::memcpy(&f, &bits, sizeof f); return f; }
const float kTempMin     = kRdata_1401721d4;               // 33.0f
const float kYearSeconds = BitsF(kRdata_14017225c);        // 3600.0f
const float kYearMonths  = BitsF(kRdata_1401721b8);        // 12.0f

}  // namespace

// Push the component's +0x48 master switch into the workplace's accepting flag.
// Shared by state-2 enter and the event-3 plant command, both of which do exactly
// this before doing anything else.
void CropFieldComponent::PushAccepting() {
    WorkPlaceComponent* wpc = _entity->FindComponent<WorkPlaceComponent>();   // 0x140002e10
    if (!wpc) return;
    wpc->SetAccepting(workEnabled);   // 0x14012fc00
}

// ── Slot array layout (self+0x58, stride 0x20, count self+0x60): CropSlot ─────────
// Recovered from the state-9 enter fill loop and every consumer below:
//   +0x00 int   cellIndex      map cell this slot covers
//   +0x04 int   state          0 = empty, 1 = growing, 2 = done/harvested
//   +0x08 IBP                  (zeroed at plant)
//   +0x10 IBP                  the planted NaturalResource; its +0x28 is growth 0..1
//   +0x18 byte  plantHere      whether this cell gets a crop (spacing + shuffle)
// self+0x48 = master switch, self+0x49 = "a crop type is set", self+0x50 = the
// description the crop type is read from.
namespace {

// Called directly: 0x1400335b0 CropFieldComponent_ResizeEntityList (the slot array;
// its new-tail write is a PARTIAL zero, +8/+16 only, so it is not a SpecFor<T> resize),
// 0x14011aa20 CropFieldComponent::ValidateEntityRefs (slot refresh), 0x140004680 the
// MapComponent finder, 0x140003050 / 0x140005380 the Work / CropField component
// finders, 0x14012ec60 CitizenAI_WorkScheduler_Post, 0x14012ead0 / 0x14012eb50
// WorkComponent_CheckWorkSlot / _RegisterWorkSlot, 0x140124750
// MapComponent_DetachOccupancy_4750. .rdata 0x140172144 = 0.99f (ripe).

// Scan a description's sub-description list (items +0x28 / count +0x30) for a type.
Description* FindInDescList(ComponentDescription* list, uint32_t tid) {
    if (!list || !tid) return nullptr;
    const int n = list->_subDescCount;
    Description** items = list->_subDescs;
    for (int i = 0; i < n && items; ++i)
        if (items[i] && ::Object::IsDerivedFromNullSafe(items[i], tid)) return items[i];
    return nullptr;
}

// The shared tail of all three area-work constructors (PlantArea 0x1400f46a0,
// HarvestArea 0x1400ed4c0, and the state-10 TendArea build): Work_Ctor25, install the
// leaf vtable, zero {+0x48 q, +0x50 d, +0x58 q, +0x60 q}, then — for Plant/Harvest —
// size a per-slot byte array at +0x58 to the crop field's slot count and fill it with 1s.
//
// ENDGAME B.3.8 MI-B: the work is built as its REAL class (src/game/Work/<X>.h),
// chosen by the caller (the class retail's ctor stamps): `new (mem) X()` runs the base
// init, the leaf's field zeroing and its *_ResolveFuncs, the compiler stamps the
// class's own table, and Work::InitFromEntity is Work_Ctor25's body (0x1400f72e0).
// Same bytes as the hand build, with no read-back of the retail tables (PlantArea
// 0x140161890, HarvestArea 0x1401614a0, TendArea 0x140161990). The tail stores below
// are kept (they match the ctors' zeroing).
//
// The three retail ctors differ in their tails and the object SIZES differ with them:
//   PlantArea   0x1400f46a0 (0x70): +0x58 q = 0, +0x60 q = 0, +0x68 q = descList
//   HarvestArea 0x1400ed4c0 (0x68): +0x58 q = 0, +0x60 q = 0
//   TendArea    0x14011a100 (0x60): +0x58 DWORD = 0   -- nothing at +0x60
// The shared tail used to zero +0x60 as a qword for all three: for TendArea that is
// 8 bytes PAST the 0x60-byte allocation, i.e. the boundary tag of the next heap block.
// Every crop field entering state 10 zeroed a neighbour's tag; the engine allocator's
// coalescing then produced the "free-list link with wrong high bits" faults in
// EntityList_Shutdown at the end of every long 800x10 stress run (2026-09-04, caught
// in the act by the REDZONE watchdog: preceding block = g_TendArea_Vtable object,
// frame 3146, right after "[CropXition] STATE 10 ENTER - posting TendArea").
//
// Each class's tail is written through its own fields (the merged qword stores are
// two named stores, H7).

// The per-slot byte array Plant/Harvest carry at +0x58: sized to the crop field's slot
// count and filled with 1s.
// The array is PlantArea::field_58 / HarvestArea::_field58 (both Collection::Array<bool>;
// exe 0x14000a590 IS its SetSize(n), which leaves the new tail as found: the loop writes
// every byte, 0x1400f4734..0x1400f4751).
void CF_FillPerSlotArray(Collection::Array<bool>* arr, ComponentEntity* entity) {
    CropFieldComponent* crop = entity->FindComponent<CropFieldComponent>();   // 0x140005380
    if (!crop) return;
    arr->SetSize(crop->slots._size);
    const int n = arr->GetSize();
    bool* bytes = arr->GetData();
    if (bytes) for (int i = 0; i < n; ++i) bytes[i] = 1;
}

Work* CF_MakePlantArea(CropFieldComponent* comp) {
    if (!::System::MemoryAllocator::_singleton) return nullptr;
    void* mem = ::System::MemoryAllocator::MemAlloc(0x70, 8);
    if (!mem) return nullptr;
    ComponentEntity* entity = comp->_entity.Get();
    PlantArea* work = new (mem) PlantArea();
    work->InitFromEntity(&entity);
    work->_cells._root = nullptr;
    work->_cells._count = 0;
    // +0x58 / +0x60: the `field_58` member's ctor (0x1400f46a0's two qword stores).
    work->field_68 = comp->cropDescription;
    CF_FillPerSlotArray(&work->field_58, work->field_10.Get());
    return work;
}

Work* CF_MakeHarvestArea(CropFieldComponent* comp) {
    if (!::System::MemoryAllocator::_singleton) return nullptr;
    void* mem = ::System::MemoryAllocator::MemAlloc(0x68, 8);
    if (!mem) return nullptr;
    ComponentEntity* entity = comp->_entity.Get();
    HarvestArea* work = new (mem) HarvestArea();
    work->InitFromEntity(&entity);
    work->_cells._root = nullptr;
    work->_cells._count = 0;
    // +0x58 / +0x60: the `_field58` member's ctor (0x1400ed4c0's two qword stores).
    CF_FillPerSlotArray(&work->_field58, work->field_10.Get());
    return work;
}

Work* CF_MakeTendArea(CropFieldComponent* comp) {
    if (!::System::MemoryAllocator::_singleton) return nullptr;
    void* mem = ::System::MemoryAllocator::MemAlloc(0x60, 8);
    if (!mem) return nullptr;
    ComponentEntity* entity = comp->_entity.Get();
    TendArea* work = new (mem) TendArea();
    work->InitFromEntity(&entity);
    work->_citizens._root = nullptr;
    work->_citizens._count = 0;
    work->tendMode = 0;
    return work;
}

// Post a freshly built work to the reservation ledger, exactly as every state enter does.
void CF_PostWork(CropFieldComponent* comp, Work* work) {
    if (!work) {
        return;
    }
    ::Object::InternalPtr<Work> workIbp(work);
    WorkComponent* workComp = comp->_entity->FindComponent<WorkComponent>();   // 0x140003050
    if (!workComp) {
        return;
    }
    workComp->PostWork(1, 0, &workIbp);
    g_pCropPlantWork = work;   // watched by the citizen claim-walk probe
}

// Release the held work slot if one is occupied (the 3050 -> ead0 -> eb50 idiom every
// crop-field per-frame handler opens with).
void CF_ReleaseWorkSlot(CropFieldComponent* comp) {
    WorkComponent* workComp = comp->_entity->FindComponent<WorkComponent>();   // 0x140003050
    if (!workComp) return;
    if (workComp->IsWorkPosted())
        workComp->CancelWork();
}

// Count slots in a given state.
int CountSlots(CropFieldComponent* comp, int state) {
    int n = 0;
    const int total = comp->slots._size;
    for (int i = 0; i < total; ++i) {
        CropSlot* s = comp->slots._data ? &comp->slots._data[i] : nullptr;
        if (s && s->state == state) ++n;
    }
    return n;
}

}  // namespace

// retail 0x14011b410 — mean crop maturity: average of min(growth, 1.0) over slots in
// state 1, divided by the TOTAL slot count (not by the number of growing slots, so a
// half-planted field can never read ripe).
float CropFieldComponent::Maturity() {
    const int total = slots._size;
    if (total == 0) return 0.0f;
    ValidateEntityRefs();
    const float cap = kRdata_14017214c;
    float sum = 0.0f;
    for (int i = 0; i < total; ++i) {
        CropSlot* s = slots._data ? &slots._data[i] : nullptr;
        if (!s || s->state != 1) continue;
        NaturalResourceComponent* nr = s->resource.Get();
        if (!nr) continue;
        float g = nr->resourceScale;
        if (g > cap) g = cap;
        sum += g;
    }
    return sum / (float)total;
}

// ── state 2 enter — 0x14011a920 ───────────────────────────────────────────────────
// Nothing but "publish my master switch to the workplace".
char CropFieldComponent::State2Enter() {
    PushAccepting();
    return 0;
}

// ── event 3 on state 2 — 0x14011a7b0 — the panel's PLANT command ──────────────────
// Publish accepting, then request state 9.
char CropFieldComponent::Event3Plant(StateMachine* sm) {
    PushAccepting();
    ::ControllerLive::RequestStateTransition(sm, 9);
    return 1;
}

// ── state 2 per-frame — 0x14011a860 — the SEASONAL PLANTING TRIGGER ───────────────
// Faithful shape:
//   if (self+0x48 == 0) return 1;                       // field switched off
//   if (weather(gw+0x130)+0x18 < 33.0f) return 1;       // too cold
//   v = (int)((Time::AsSeconds(gw+0x128 + 8) / 3600.0f) * 12.0f);
//   if (v < 0) return 1;
//   if ((unsigned)(v - 3) > 8) Event3Plant(self, sm);   // -> state 9
//
// ⚠ That last test is an UNSIGNED compare on `v - 3`, so it is true both when v > 11
// AND when v < 3 (the subtraction underflows to a huge unsigned). It is retail's
// idiomatic "outside the closed window [3, 11]" check — i.e. plant in the cold months
// at either end of the year, not mid-season. Writing it as a signed `v - 3 > 8` would
// silently drop the entire v < 3 arm and the field would only ever plant late in the
// year. Kept as an explicit unsigned cast so the behaviour cannot drift.
// The year clock is GameUpdateLoop::m_dayCounter (Calendar), the temperature m_weather's.
char CropFieldComponent::State2PerFrame(StateMachine* sm) {
    if (!workEnabled) return 1;
    if (GameUpdateLoop::_singleton == nullptr) return 1;

    GameUpdateLoop* gw = GameUpdateLoop::_singleton;
    const Calendar* const seasonMgr = gw->m_dayCounter;
    const WeatherSimulation* const weather = gw->m_weather;
    if (!seasonMgr || !weather) return 1;

    if (weather->temperature < kTempMin) return 1;   // +0x18

    const float secs = seasonMgr->yearTime.AsSeconds();   // +0x08
    const int v = static_cast<int>((secs / kYearSeconds) * kYearMonths);
    if (v < 0) return 1;
    if (static_cast<uint32_t>(v - 3) > 8u)
        Event3Plant(sm);
    return 1;
}

// ── state 9 enter — 0x14011a310 — BUILD THE GRID AND POST A PlantArea ─────────────
// 1. Read the footprint from the MapComponent: origin (+0x28,+0x2a), extent (+0x2c,+0x2e).
// 2. Resize the slot array to w*h, filling each slot with {cellIndex, state 0, null, null}.
// 3. Decide which cells actually get a crop: if the crop type (desc list +0x20 off the
//    CropFieldDescription) is unset, +0x49 = 0 and nothing is planted; otherwise +0x49 = 1
//    and every spacing-th cell is marked, spacing = RawMaterialDescription+0x4c.
// 4. Shuffle those marks (Fisher-Yates over the game RNG) so rows are not identical.
// 5. Re-init the map component, then post a PlantArea work carrying the desc list.
char CropFieldComponent::State9Enter() {
    MapComponent* map = _entity->FindComponent<MapComponent>();   // 0x140004680
    if (!map) return 1;
    // Retail loads the footprint with MOVSX: signed shorts.
    const int ox = static_cast<int16_t>(map->tileX0);
    const int oy = static_cast<int16_t>(map->tileY0);
    const int w  = static_cast<int16_t>(map->tileX1);
    const int h  = static_cast<int16_t>(map->tileY1);

    slots.SetSize(w * h);   // 0x1400335b0

    if (GameUpdateLoop::_singleton == nullptr) return 1;
    GameUpdateLoop* gw = GameUpdateLoop::_singleton;
    const Map* mapGrid = gw->m_game;   // +0xf8
    if (!mapGrid) return 1;
    const int mapW = mapGrid->size;

    for (int row = 0; row < h; ++row) {
        for (int col = 0; col < w; ++col) {
            CropSlot* s = slots._data ? &slots._data[row * w + col] : nullptr;
            if (!s) continue;
            s->cell = (row + oy) * mapW + col + ox;
            s->resource = ::Object::InternalPtr<NaturalResourceComponent>(nullptr);
            s->entity = ::Object::InternalPtr<ComponentEntity>(nullptr);
            s->state = 0;
        }
    }

    const int count = slots._size;
    if (count <= 0) return 1;
    uint8_t* marks = (::System::MemoryAllocator::_singleton)
        ? static_cast<uint8_t*>(::System::MemoryAllocator::MemAlloc(count, 1)) : nullptr;
    if (!marks) return 1;

    // Crop type: ENDGAME B.3.9 C1 (D5) -- 0x1401c7b68 is NaturalResourceDescription
    // (INVENTORY_C Table 1), NOT CropFieldDescription as the old local name/comment
    // said; the cell is what retail's FUN_14012ea70-family reads here, and BY CELL
    // is the rule, not the name a prior pass guessed. In the description at self+0x50,
    // then its +0x20 (NaturalResourceDescription::_building). Null means "no crop
    // chosen" — the field stays bare and +0x49 clears.
    Description* cropDesc = FindInDescList(cropDescription, NaturalResourceDescription::TypeId());
    ComponentDescription* cropList = cropDesc
        ? static_cast<NaturalResourceDescription*>(cropDesc)->_building
        : nullptr;

    if (cropList == nullptr) {
        hasCrop = false;
        for (int i = 0; i < count; ++i) marks[i] = 0;
    } else {
        hasCrop = true;
        Description* rawMat = FindInDescList(cropList, RawMaterialDescription::TypeId());
        const int spacing = rawMat
            ? static_cast<RawMaterialDescription*>(rawMat)->_createChance : 1;
        for (int i = 0; i < count; ++i)
            marks[i] = (spacing != 0 && (i % spacing) == 0) ? 1 : 0;
    }

    System::Random* rng = gw->m_random;
    if (rng) {
        for (int i = 0; i < count; ++i) {
            const uint32_t j = rng->Rand(static_cast<uint32_t>(count));
            if (static_cast<uint32_t>(i) != j) {
                uint8_t t = marks[i]; marks[i] = marks[j]; marks[j] = t;
            }
        }
    }
    for (int i = 0; i < count; ++i) {
        CropSlot* s = slots._data ? &slots._data[i] : nullptr;
        if (s) s->plantHere = (marks[i] != 0);
    }

    map->RemoveFromMap();

    // PlantArea is 0x70 and carries the desc list at +0x68 (vtable 0x140161890).
    CF_PostWork(this, CF_MakePlantArea(this));

    if (::System::MemoryAllocator::_singleton) ::System::MemoryAllocator::MemFree(marks);
    return 1;
}

// ── state 9 per-frame — 0x14011a1e0 ───────────────────────────────────────────────
// Refresh the slots, then: all done -> 21; all either growing or done -> 10 (tend).
// FIX crop_planting_exit (docs/retail_bugs.md section 11): retail's state 9 has no 21 in its
// transition list, so its 21 request is dropped and the field stays here. The table below
// carries 21 for the row; with the row off the request is not made, which is what retail's
// dropped request amounts to (nothing else asks a crop field for 21 in state 9).
char CropFieldComponent::State9PerFrame(StateMachine* sm) {
    ValidateEntityRefs();
    const int total   = slots._size;
    const int growing = CountSlots(this, 1);
    const int done    = CountSlots(this, 2);

    if (done == total) {
        CF_ReleaseWorkSlot(this);
        if (Ext::Fix().crop_planting_exit)
            ::ControllerLive::RequestStateTransition(sm, 21);
    } else if (growing + done == total) {
        CF_ReleaseWorkSlot(this);
        ::ControllerLive::RequestStateTransition(sm, 10);
    }
    return 1;
}

// ── state 10 enter — 0x14011a100 — post a TendArea ────────────────────────────────
// TendArea is 0x60 with no per-slot array and no desc field (vtable 0x140161990).
char CropFieldComponent::State10Enter() {
    CF_PostWork(this, CF_MakeTendArea(this));
    return 1;
}

// ── state 10 update — 0x140119f70 — release the held work slot ────────────────────
char CropFieldComponent::State10Update() {
    CF_ReleaseWorkSlot(this);
    return 1;
}

// ── event 4 on state 10 — 0x140119f00 — the panel's HARVEST command ───────────────
// It reads no field of the component (RCX unused): it stays a free function (B1).
static char CropField_Event4Harvest(CropFieldComponent* /*self*/, StateMachine* sm) {
    ::ControllerLive::RequestStateTransition(sm, 11);
    return 1;
}

// ── state 10 per-frame — 0x140119fd0 — the HARVEST TRIGGER ────────────────────────
// All slots done -> 21. Then, if the field is on AND has a crop type:
//   warm (temp >= 33) AND the year position inside [6,11]  -> harvest NOW
//   otherwise                                              -> harvest only if ripe
//                                                             (mean maturity > 0.99)
// The (unsigned)(month - 6) > 5 test is again retail's "outside [6,11]" idiom — true
// for month < 6 via underflow as well as for month > 11.
// The year clock is GameUpdateLoop::m_dayCounter (Calendar), the temperature m_weather's.
char CropFieldComponent::State10PerFrame(StateMachine* sm) {
    ValidateEntityRefs();

    const int total = slots._size;
    if (CountSlots(this, 2) == total)
        ::ControllerLive::RequestStateTransition(sm, 21);

    if (!workEnabled) return 1;
    if (!hasCrop) return 1;
    if (GameUpdateLoop::_singleton == nullptr) return 1;

    GameUpdateLoop* gw = GameUpdateLoop::_singleton;
    const WeatherSimulation* const weather = gw->m_weather;
    const Calendar* const seasonMgr = gw->m_dayCounter;
    if (!weather || !seasonMgr) return 1;

    bool harvestNow = false;
    int  monthDbg = -1;
    const float temp = weather->temperature;   // +0x18
    if (temp >= kTempMin) {
        const float secs = seasonMgr->yearTime.AsSeconds();   // +0x08
        monthDbg = static_cast<int>((secs / kYearSeconds) * kYearMonths);
        if (static_cast<uint32_t>(monthDbg - 6) <= 5u) { harvestNow = true; }
    }
    const float maturity = Maturity();
    if (!harvestNow && maturity > kRdata_140172144) harvestNow = true;

    if (harvestNow) CropField_Event4Harvest(this, sm);
    return 1;
}

// ── state 11 enter — 0x140119e60 — post a HarvestArea ─────────────────────────────
// HarvestArea is 0x68 with a per-slot byte array and no desc field (vtable 0x1401614a0).
char CropFieldComponent::State11Enter() {
    CF_PostWork(this, CF_MakeHarvestArea(this));
    return 1;
}

// ── state 11 per-frame — 0x140119d80 ──────────────────────────────────────────────
// When every slot reads done, release the work slot and go to 21.
char CropFieldComponent::State11PerFrame(StateMachine* sm) {
    ValidateEntityRefs();
    if (CountSlots(this, 2) == slots._size) {
        CF_ReleaseWorkSlot(this);
        ::ControllerLive::RequestStateTransition(sm, 21);
    }
    return 1;
}

// The table holds one-line adapters: the dispatcher hands the handler `[node+0x18]`
// (the CropFieldComponent) and the state machine.
static char CropField_State2Enter(CropFieldComponent* c, StateMachine* /*sm*/)                 { return c->State2Enter(); }        // 0x14011a920
static char CropField_Event3Plant(CropFieldComponent* c, StateMachine* sm)       { return c->Event3Plant(sm); }     // 0x14011a7b0
static char CropField_State2PerFrame(CropFieldComponent* c, StateMachine* sm, const System::Time* /*time*/)    { return c->State2PerFrame(sm); }  // 0x14011a860
static char CropField_State9Enter(CropFieldComponent* c, StateMachine* /*sm*/)                 { return c->State9Enter(); }       // 0x14011a310
static char CropField_State9PerFrame(CropFieldComponent* c, StateMachine* sm, const System::Time* /*time*/)    { return c->State9PerFrame(sm); }  // 0x14011a1e0
static char CropField_State10Enter(CropFieldComponent* c, StateMachine* /*sm*/)                { return c->State10Enter(); }      // 0x14011a100
static char CropField_State10Update(CropFieldComponent* c, StateMachine* /*sm*/)               { return c->State10Update(); }     // 0x140119f70
static char CropField_State10PerFrame(CropFieldComponent* c, StateMachine* sm, const System::Time* /*time*/)   { return c->State10PerFrame(sm); } // 0x140119fd0
static char CropField_State11Enter(CropFieldComponent* c, StateMachine* /*sm*/)                { return c->State11Enter(); }      // 0x140119e60
static char CropField_State11PerFrame(CropFieldComponent* c, StateMachine* sm, const System::Time* /*time*/)   { return c->State11PerFrame(sm); } // 0x140119d80

// Replica of the populator at 0x14011b150, in registration order. Order matters: it
// fixes the state INDEX that StateMachine::Tick carries in its +0x09/+0x0a/+0x0b bytes.
static void CropFieldCtrl_PopulateTable() {
    using namespace ::ControllerLive;
    auto& t = CropFieldDispatch::table;
    if (t.count != 0) return;                       // idempotent

    // ── state 2 — DEFAULT / idle ──────────────────────────────────────────────────
    // Slot 16 (GetDefaultState) is the shared 0x1400026c0 = `MOV EAX,2; RET`, so this
    // is where a new field starts. enter 0x14011a920 / trans38 0x14011a860; the event-3
    // body 0x14011a7b0 is the panel's plant command, mirroring the orchard's event 3.
    TableRegisterState(t, 2, &CropField_State2Enter, nullptr,
                       &CropField_State2PerFrame, nullptr);
    TableAddTransition(t, 3,  nullptr);
    TableAddTransition(t, 9,  nullptr);
    TableAddEvent(t, 3, &CropField_Event3Plant);

    // ── state 9 — PLANT ───────────────────────────────────────────────────────────
    // enter 0x14011a310 / trans38 0x14011a1e0.
    TableRegisterState(t, 9, &CropField_State9Enter, nullptr,
                       &CropField_State9PerFrame, nullptr);
    TableAddTransition(t, 3,  nullptr);
    TableAddTransition(t, 10, nullptr);
    // NOT RETAIL: fix.crop_planting_exit's re-clear (docs/retail_bugs.md section 11). Retail lists
    // {3, 10} only. The table is built at static init, before settings, so the entry is always
    // here and State9PerFrame makes the request only with the row on. Appended last, so the
    // indices of 3 and 10 (the transition index a save carries) are retail's.
    TableAddTransition(t, 21, nullptr);

    // ── state 10 — TEND / GROW ────────────────────────────────────────────────────
    // The only state with an UPDATE handler (0x140119f70) as well as a per-frame
    // trans38 (0x140119fd0); enter 0x14011a100. Event 4 (0x140119f00) is the harvest
    // command, and 21 is the clear/cut target.
    TableRegisterState(t, 10, &CropField_State10Enter, nullptr,
                       &CropField_State10PerFrame,
                       &CropField_State10Update);
    TableAddTransition(t, 3,  nullptr);
    TableAddTransition(t, 21, nullptr);
    TableAddTransition(t, 11, nullptr);
    TableAddEvent(t, 4, &CropField_Event4Harvest);

    // ── state 11 — HARVEST ────────────────────────────────────────────────────────
    // enter 0x140119e60 / trans38 0x140119d80.
    TableRegisterState(t, 11, &CropField_State11Enter, nullptr,
                       &CropField_State11PerFrame, nullptr);
    TableAddTransition(t, 3,  nullptr);
    TableAddTransition(t, 21, nullptr);
}

// Built ONCE at static-init time, as retail's populator 0x14011b150 does (B38 D6/D7).
// Before, every CropFieldComponent factory call re-ran it from both threads.
static const bool s_cropFieldStateTableBuilt = (CropFieldCtrl_PopulateTable(), true);
