// engine/src/ui/residenceui.cpp
// Clean reimplementation of ResidenceUI game logic.
//
// ResidenceUI (0x40 bytes) — residence info panel.
// Extends DialogController (0x30 base).
// +0x30 = dynamic entry array (0x30-byte entries), +0x38 = entry count.
//
// Each entry (0x30 bytes):
//   +0x00 = imageMale element ptr
//   +0x08 = imageFemale element ptr
//   +0x10 = textName element ptr
//   +0x18 = textAge element ptr
//   +0x20 = textProfession element ptr
//   +0x28 = buttonGoto element ptr
//
// Decompiled from Application-x64.exe.

#include "Dialog/ui_panels.h"
#include "system/Value.h"
#include "ui/UserInterface.h"
#include "ui/DialogInstance.h"
#include "object/TypeInfo.h"
#include "Entity/exe_bodies.h"
#include "Dialog/exe_bodies.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "system/String.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "ui/Element.h"  // ENDGAME B.3.6 UI callers (K3)
#include "ui/String.h"  // ENDGAME B.3.6 UI callers (K3)
#include "ui/StringTable.h"  // ENDGAME B.3.6 UI callers (K3)
#include "object/External.h"  // ENDGAME B.3.6 UI callers (K3)
#include <new>  // ENDGAME B.3.6 UI callers (K3)
#include "ui/TextElement.h"  // ENDGAME B.3.6 UI callers round 2 (K3)
#include "Object/factory_registration.h"
#include <cstddef>
#include "Component/ResidenceComponent.h"   // X::TypeId() (B.3.8 callers)
#include "Component/StorageComponent.h"     // fix.residence_goto faithful arm (finder 0x140006270)
#include "ext/registry.h"                   // Ext::Fix().residence_goto
#include "Entity/ComponentEntity.h"   // X::TypeId() (B.3.8 callers)
#include "system/MemoryAllocator.h"   // B3R: the allocator doors
#include "Game/exe_constants.h"   // B3R: kRdata_*
#include "Dialog/ResidenceUI.h"
#include "Game/Profession.h"
#include "Game/ProfessionList.h"
#include "Component/CitizenComponent.h"
#include "Component/CitizenDescription.h"
#include "ui/TableElement.h"
#include "Component/AttendanceComponent.h"   // B4 S4 RT2: InternalPtr<AttendanceComponent>::Get() needs the class complete
#include "Game/PlayerToolManager.h"   // CenterOnEntity 0x140103040 (row 452)

// ResidenceUI::Slot7 is the per-frame roster refresh (driven via the +0x40 W_OnUpdate
// hack in standalone). Its _DEBUG bring-up logs used to fflush an unbuffered log once
// per resident EVERY frame — the dominant cost that tanked the framerate once a second
// entity panel could stay pinned open. Gate them behind BANISHED_DIALOG_TRACE (cached
// once). Kept inside _DEBUG so release builds are unchanged.
#ifdef _DEBUG
#endif

// Range check for pointers a standalone citizen may leave uninitialized. A rostered
// CitizenComponent in standalone can have a garbage description cache (+0x288) or an
// unset name String; the raw derefs the faithful retail port does would fault (MinGW
// has no __try/__except to catch it). Same idea as CitUI_OkPtr in citizenui.cpp.
static inline bool RUI_OkPtr(const void* ptr) {   // LEFT: void* is a raw address (diagnostic probe), not an object: checks it lies in [0x10000, 0x7FFFFFFFFFFF] (description cache / name String pointers)
    uintptr_t p = reinterpret_cast<uintptr_t>(ptr);  // CAST-OK: ptr_int
    return p >= 0x10000 && p <= 0x00007FFFFFFFFFFFULL;
}

// Helper: find an element by name under a parent element, as the UI class it
// is read as (Original pattern: FindElement → CheckPtr → deref)
template <class T>
static T* FindChildElem(UI::Element* parentElem, const char* name) {
    System::String nameStr(name);
    return static_cast<T*>(parentElem->FindElement(nameStr).Get());
}

namespace residenceui_sa {

// ENDGAME B.3.9 CG2: TypeId_ComponentEntity / TypeId_ResidenceComponent no longer
// need healing here — ComponentEntity::TypeId() / ResidenceComponent::TypeId() (the
// class statics) always answer, so the s_ceTid/s_resTid lazy-fill retires.
// B3R: the age multiplier is the .rdata constant kRdata_140172198 (5.0f); the
// never-taken 1.0f fallback over g_pAgeMultiplier (EnsureGlobals) is gone.
bool Ready() {
    return GameUpdateLoop::_singleton != nullptr;
}


// SA-guard: the dialog's data-source entity ref (+0x28) is nulled by entity
// destruction (verified live 2026-07-20: house demolished via the upgrade path
// with its panel open → IsDerivedFrom(null) crashed in Runtime). Bail when the
// entity is gone — the panel just stops refreshing.
static bool DataSourceAlive(ResidenceUI* self) {
    Object::Base* e = self->_dataSource;
    if (!e) return false;
    // Marked-for-removal (entity+0x40 bit 0x400): the entity survives until the
    // EntityList drain, but its components/roster may already be torn down —
    // reading them crashed live 2026-07-20 (garbage into IsDerivedFrom).
    if ((static_cast<ComponentEntity*>(e)->_entityFlags >> 10) & 1)
        return false;
    return true;
}



}  // namespace residenceui_sa

// ============================================================
// ResidenceUI::Slot4 (vtable +0x20) — Setup resident table
// ============================================================
// Finds "residenceTable" (or "residenceLongTable" fallback), iterates
// rows to find child elements (buttonGoto, textName, imageMale, imageFemale,
// textAge, textProfession), stores in dynamic array at +0x30.
// Sets element IDs on buttonGoto: 300 + rowIndex.
//
// Original: 0x1400B5850.

// [4] 0x1400b5850
void ResidenceUI::OnCreated() {
    if (!residenceui_sa::Ready()) return;
    UI::Element* dialogElem = _container;

    // Try "residenceTable" first, fall back to "residenceLongTable"
    UI::TableElement* tableElem = FindChildElem<UI::TableElement>(dialogElem, "residenceTable");

    if (!tableElem) {
        // Fallback: find "residenceLongTable"
        tableElem = FindChildElem<UI::TableElement>(dialogElem, "residenceLongTable");
    }

    // Get row count and column count from table element
    int rowCount = tableElem->_rowCount;
    int colCount = tableElem->_columnCount;

#ifdef _DEBUG
    printf("[ResidenceUI::Slot4] tableElem=%p rowCount=%d colCount=%d\n",
           tableElem, rowCount, colCount);
    fflush(stdout);
#endif

    for (int i = 0; i < rowCount; i++) {
        // 0x30-byte entry: imageMale, imageFemale, textName, textAge, textProfession, buttonGoto
        ResidenceUIRow entry = {};

        // Original uses flat table indices: row * colCount + column
        // Column layout: 0=buttonGoto, 1=textName, 2=imageMale/Female, 3=textAge, 4=textProfession
        int base = i * colCount;

        // Col 0: buttonGoto
        entry.buttonGoto = FindChildElem<UI::Element>(tableElem->Child(base + 0).Get(), "buttonGoto");
        if (entry.buttonGoto) entry.buttonGoto->SetElementId(300 + i);

        // Col 1: textName
        entry.textName = FindChildElem<UI::TextElement>(tableElem->Child(base + 1).Get(), "textName");

        // Col 2: imageMale + imageFemale (container cell)
        UI::Element* iconCell = tableElem->Child(base + 2).Get();
        entry.imageMale = FindChildElem<UI::Element>(iconCell, "imageMale");

        entry.imageFemale = FindChildElem<UI::Element>(tableElem->Child(base + 2).Get(), "imageFemale");

        // Col 3: textAge
        entry.textAge = FindChildElem<UI::TextElement>(tableElem->Child(base + 3).Get(), "textAge");

        // Col 4: textProfession
        entry.textProfession = FindChildElem<UI::TextElement>(tableElem->Child(base + 4).Get(), "textProfession");

#ifdef _DEBUG
        if (i < 3) {
            printf("[ResidenceUI::Slot4]   row[%d] base=%d: imgM=%p imgF=%p txtN=%p txtA=%p txtP=%p btn=%p\n",
                   i, base, entry.imageMale, entry.imageFemale, entry.textName,
                   entry.textAge, entry.textProfession, entry.buttonGoto);
            fflush(stdout);
        }
#endif

        // Push entry into the row array at +0x30 (was FUN_140021020's own
        // inlined SetSize, ResidenceUI_ArrayPush_Impl).
        rows.SetSize(rows.GetSize() + 1, entry);
    }
}

// ============================================================
// ResidenceUI::Slot9 (vtable +0x48) — Button dispatch
// ============================================================
// Handles resident row button clicks. Each row has element ID = 300 + index.
// Clicking navigates camera to that resident citizen.
//
// Original: 0x1400B5820.

// ============================================================
// ResidenceUI::Slot7 (vtable +0x38) — Update resident display
// ============================================================
// Iterates storage component rooms to display resident info per row.
// For each occupied slot: show gender icons, set name/age/profession text.
// For unused rows beyond active count: hide and clear.
//
// Storage layout: +0x50 = room array ptr (0x10-byte entries: ptr, count),
//                 +0x58 = room count.
// Each resident ptr -> citizen entity with name at +0x28, gender at +0x38,
// age at +0x3c, profession at +0x1f0, description at +0x288, death at +0x208.
//
// Original: 0x1400B51F0.

// [7] 0x1400b51f0
void ResidenceUI::OnUpdateLayout() {
    if (!residenceui_sa::Ready() || !residenceui_sa::DataSourceAlive(this)) return;

    // Get entity from data source +0x28
    Object::Base* dataSource = _dataSource;
    bool isEntity = ::Object::IsDerivedFromNullSafe(dataSource, ComponentEntity::TypeId());
    ComponentEntity* entity = isEntity ? static_cast<ComponentEntity*>(dataSource) : nullptr;

    // Find ResidenceComponent — original uses type ID at 0x1401c7cf0 (ResidenceComponent),
    // NOT StorageComponent (0x1401c7e78). Different component with room data at +0x50/+0x58.
    ResidenceComponent* residenceComp = entity->FindComponent<ResidenceComponent>();

#ifdef _DEBUG
#endif

    float ageMul = kRdata_140172198;  // 5.0f
    int totalResidents = 0;
    int rowIdx = 0;

    if (!residenceComp) return;

    // Outer loop: iterate room groups (residentSlotCount, +0x58)
    int groupCount = residenceComp->residentSlots._size;
#ifdef _DEBUG
#endif

    if (groupCount > 0) {
        int64_t groupOffset = 0;   // the family index (retail steps a 0x10 byte offset)

        for (int g = 0; g < groupCount; g++) {
            ResidenceComponent::Family* groupArrayBase = residenceComp->residentSlots._data;
            int residentCount = groupArrayBase[groupOffset]._size;

            if (residentCount > 0) {
                totalResidents += residentCount;
                int endRow = rowIdx + residentCount;
#ifdef _DEBUG
#endif

                for (int r = 0; r < residentCount; r++) {
                    // [DECLARED DEVIATION — ROW-ARRAY BOUND] Retail indexes the row array at
                    // controller+0x30 by resident index with NO bound (0x1400b51f0: the populate
                    // walk is unbounded; only the blank-out loop afterwards is bounded by
                    // controller+0x38). Its invariant is that a household can never hold more
                    // residents than the dialog table has rows.
                    //
                    // [CORRECTION 2026-07-26] The earlier note here blamed StartTown's housing
                    // pass for "rostering a whole family into slot[0] regardless of the
                    // ResidenceDescription capacity". That is not a deviation: desc+0x20 is the
                    // number of HOUSEHOLD SLOTS, not an occupant cap. ResidenceComponent::Serialize
                    // resizes +0x50 to desc+0x20 and stores it in +0x58; every reader
                    // (sub_0e70, this slot) then walks +0x58 entries at stride 0x10, each entry
                    // being its own {data,count,cap} list of citizens. A wood house reads
                    // desc+0x20 == 1 at runtime, and retail's own home finder PREFERS
                    // `*(comp+0x58) == 1` and only falls back to `1 < *(comp+0x58)` — which would
                    // be inverted nonsense if the field were an occupant count. One family in
                    // slot[0] is exactly what retail's housing solver produces.
                    //
                    // So the guard is not covering for a start-town bug; it covers retail's own
                    // unbounded walk, which a household grown past the table's row count (by
                    // BIRTHS) would run off the end of, handing garbage to
                    // UI::Element::SetFlagsOnOff. It has never been observed to fire — and note
                    // the autostart soak never opens a residence panel, so "never fired" is not
                    // yet evidence either way about the ResidenceUI_Slot7+0x295 crash of
                    // 2026-07-25; the mass-death teardown remains an equally good explanation.
                    // Stop at the last real row instead of walking off the end, and say so.
                    const int rowCapacity = rows._size;
                    if (rowIdx >= rowCapacity) {
                        static int s_warned = 0;
                        if (s_warned < 4) { ++s_warned;
                            fprintf(stderr, "[ResidenceUI] GUARD: residence %p has %d resident(s) "
                                    "in group %d but the dialog table only has %d row(s) — "
                                    "truncating (SA over-filled house)\n",
                                    residenceComp, residentCount, g, rowCapacity);
                            fflush(stderr);
                        }
                        break;
                    }
                    // Original CheckPtr's the component IBP every iteration

                    // Get citizen ptr from resident list + CheckPtr
                    Object::InternalPtr<CitizenComponent>* residentListPtr = residenceComp->residentSlots[groupOffset]._data;
                    CitizenComponent* citizen = residentListPtr[r].Get();

#ifdef _DEBUG
#endif

                    // The row's elements (0x30-byte rows at +0x30)
                    ResidenceUIRow& row = rows[rowIdx];

                    // Check gender: citizen+0x38 != 0 means male
                    bool isMale = (citizen->gender != 0);

                    // Show/hide gender icons — CheckPtr each element IBP in row array
                    UI::Element* imageFemale = row.imageFemale;
                    if (imageFemale) {
                        if (isMale) {
                            imageFemale->SetFlagsOnOff(1, 0);
                        } else {
                            imageFemale->SetFlagsOnOff(0, 1);
                        }
                    }
                    UI::Element* imageMale = row.imageMale;
                    if (imageMale) imageMale->SetFlagsOnOff((uint32_t)!isMale, (uint32_t)isMale);

                    // Set name text: citizen+0x28 is the name String
                    UI::TextElement* textName = row.textName;
                    // SA safety: the name String buffer (citizen+0x28) may be unset on a
                    // standalone citizen; SetParameter refcounts the source buffer and
                    // would fault on garbage. 0 (empty String) is fine.
                    uintptr_t nameBuf = reinterpret_cast<uintptr_t>(citizen->name._data);  // CAST-OK: ptr_int
                    if (textName && (nameBuf == 0 || RUI_OkPtr(reinterpret_cast<const void*>(nameBuf))))  // CAST-OK: ptr_int
                        textName->SetParameter(0, citizen->name);

                    // Set age text: (int)(age * 5.0f) formatted as "%i"
                    int ageYears = (int)(citizen->age * ageMul);
                    System::String ageStr = System::String::Format("%i", (int)(uint32_t)ageYears);
                    UI::TextElement* textAge = row.textAge;
                    if (textAge) textAge->SetParameter(0, ageStr);

                    // Determine profession text. Faithful to retail 0x1400B51F0:
                    //   if (age < desc.childThreshold(+0x44) || job(+0x208) != 0):
                    //       job == 0 -> ProfessionList+0x40 (child/student label)
                    //       job != 0 -> ProfessionList+0x48 (employed label)
                    //   else (adult, no job) -> the citizen's own profession (+0x1f0)
                    // (The prior port had these branches scrambled and read +0x208 as a
                    //  "dead" flag; corrected here.) Every deref is range-guarded so a
                    // standalone citizen with a garbage description cache can't fault.
                    UI::TextElement* textProfession = row.textProfession;

                    float age = citizen->age;
                    AttendanceComponent* jobEntity = citizen->school.Get();

                    // Child-age threshold from the citizen description (+0x288 cache;
                    // fall back to the component's own description at +0x18 when the
                    // cache is garbage in standalone).
                    // (the fallback is the Component's Description, read as the
                    // CitizenDescription it is on a citizen: the cache's own class)
                    const Description* descPtr = citizen->description;
                    if (!RUI_OkPtr(descPtr)) descPtr = citizen->_description;
                    float oldAgeThreshold = RUI_OkPtr(descPtr)
                                          ? static_cast<const CitizenDescription*>(descPtr)->adultAge : 1e30f;

                    // UI::String ctor needs 3 args: (retbuf, externalPtr at +0x20,
                    // stringName at +0x30). fn_UIString_Ctor passes all three; the
                    // Ghidra decompile drops R8 (the +0x30 name) on the call.
                    UI::String uiStr;
                    bool builtProf = false;
                    if (age < oldAgeThreshold || jobEntity != nullptr) {
                        // Child (no job) -> ProfessionList::childProfession (+0x40);
                        // employed -> ProfessionList::studentProfession (+0x48).
                        GameUpdateLoop* world = GameUpdateLoop::_singleton;
                        ProfessionList* profList = world ? world->m_professionList : nullptr;
                        if (RUI_OkPtr(profList)) {
                            Profession* entry = (jobEntity == nullptr) ? profList->childProfession
                                                                 : profList->studentProfession;
                            if (RUI_OkPtr(entry)) {
                                uiStr = UI::String(entry->stringTable, entry->stringName);
                                builtProf = true;
                            }
                        }
                    } else {
                        // Adult laborer: the citizen's own profession object (+0x1f0).
                        Profession* profDesc = citizen->profession;
                        if (RUI_OkPtr(profDesc)) {
                            uiStr = UI::String(Object::ExternalPtr<UI::StringTable>(profDesc->stringTable), profDesc->stringName);
                            builtProf = true;
                        }
                    }
                    if (textProfession && builtProf) textProfession->SetText(&uiStr);

                    // Enable buttonGoto: retail 0x1400b54f6 `MOV DL,0x1` before
                    // `CALL [RAX+0x40]` (the call used to leave RDX unset).
                    UI::Element* buttonGoto = row.buttonGoto;
                    if (buttonGoto) {
                        buttonGoto->Enable(true);   // slot 8
                    }

                    rowIdx++;
                }
            }
            groupOffset += 1;
        }
    }

    // Hide unused rows beyond active resident count
    if (totalResidents < rows._size) {
        int i = totalResidents;
        do {
            ResidenceUIRow& row = rows[i];

            // Hide both gender icons
            UI::Element* imageMale = row.imageMale;
            if (imageMale) imageMale->SetFlagsOnOff(1, 0);
            UI::Element* imageFemale = row.imageFemale;
            if (imageFemale) imageFemale->SetFlagsOnOff(1, 0);

            // Clear name text
            {
                System::String emptyStr1("");
                UI::TextElement* textName = row.textName;
                if (textName) textName->SetParameter(0, emptyStr1);
            }

            // Clear age text
            {
                System::String emptyStr2("");
                UI::TextElement* textAge = row.textAge;
                if (textAge) textAge->SetParameter(0, emptyStr2);
            }

            // Reset profession text: UI::String(the element's own table, empty string)
            UI::TextElement* textProfession = row.textProfession;
            if (textProfession) {
                System::String emptyStr3("");
                UI::String origUIStr(textProfession->_text.GetStringTable(), emptyStr3);
                textProfession->SetText(&origUIStr);
            }

            // Disable buttonGoto (retail 0x1400b5664 `XOR EDX,EDX`).
            UI::Element* buttonGoto = row.buttonGoto;
            if (buttonGoto) {
                buttonGoto->Enable(false);   // slot 8
            }

            i++;
        } while (i < rows._size);
    }
}

// ============================================================
// ResidenceUI::GoToCitizen — navigate camera to resident citizen
// ============================================================
// Iterates the component's rooms to find the citizen at the given
// flat index, then centers the camera on that citizen entity.
//
// Original: 0x1400B56A0 (from ResidenceUI_Slot9 0x1400b5843: element event 0x12, id 300 + n).
//
// FIX residence_goto (docs/retail_bugs.md section 2, known_port_issues row 452): retail
// 0x1400b56a0 finds the house's component through 0x140006270, the StorageComponent finder
// (cell 0x1401c7e78, written only by StorageComponent_RegisterType 0x14014fb10; factory
// 0x140038b50 = MemAlloc(0x40)), then walks [comp+0x58] families of [comp+0x50] at stride
// 0x10 -- heap PAST the 0x40-byte StorageComponent. The corrected arm reads the
// ResidenceComponent's family rows (cell 0x1401c7cf0, MemAlloc(0x60), families at +0x50 /
// +0x58), the rows OnUpdateLayout 0x1400b51f0 walks. The lever ("the StorageComponent finder
// at that site") is the one line choosing the component; the walk below is retail's for both.
//
// The faithful arm reads the StorageComponent through the ResidenceComponent layout, i.e.
// +0x50 / +0x58 of a 0x40-byte heap block, exactly as retail does; it is not guarded (a
// fault there is retail's). What lies past the block is OUR allocator's, not retail's
// MemAlloc's: the neighbouring block / allocator header differs, so on a given heap the
// faithful arm may land on different garbage than retail would (and under a redzone
// allocator it reads the redzone).
// The rest is retail's shape: the flat index counts residents family by family; the hit
// hands the resident's entity (CitizenComponent +0x10) and DialogController +0x18 to
// PlayerToolManager::CenterOnEntity 0x140103040 (receiver GameUpdateLoop +0x120).

void ResidenceUI::GoToCitizen(int citizenIndex) {
    const bool fixResidenceGoto = Ext::Fix().residence_goto;
    // Get entity from data source +0x28
    Object::Base* dataSource = _dataSource;
    bool isEntity = ::Object::IsDerivedFromNullSafe(dataSource, ComponentEntity::TypeId());
    ComponentEntity* entity = isEntity ? static_cast<ComponentEntity*>(dataSource) : nullptr;

    ResidenceComponent* residenceComp;
    if (fixResidenceGoto) {
        residenceComp = entity->FindComponent<ResidenceComponent>();
    } else {
        // retail: Entity_FindComponent_Storage 0x140006270, then [+0x50] / [+0x58] past it
        residenceComp = reinterpret_cast<ResidenceComponent*>(entity->FindComponent<StorageComponent>());   // CAST-OK: user_data -- retail's own type confusion at 0x1400b56a0 (+0x50 / +0x58 of a StorageComponent), kept in the faithful arm of fix.residence_goto
    }

    // Iterate the families to find the resident at the flat index
    int flatIdx = 0;
    int groupCount = residenceComp->residentSlots._size;

    for (int g = 0; g < groupCount; g++) {
        int residentCount = residenceComp->residentSlots[g]._size;

        for (int r = 0; r < residentCount; r++) {
            if (flatIdx == citizenIndex) {
                CitizenComponent* citizen = residenceComp->residentSlots[g][r].Get();
                // The resident's entity (Component::_entity, +0x10) and the owning dialog
                // (DialogController::_dialog, +0x18), each copied into a stack slot
                ComponentEntity* citizenEntity = citizen->_entity.Get();
                UI::DialogInstance* parentDialog = _dialog;

                // Center camera on this citizen via the tool manager (gameWorld+0x120)
                GameUpdateLoop::_singleton->m_toolManager->CenterOnEntity(&parentDialog, &citizenEntity);
                return;
            }
            flatIdx++;
        }
    }
}

// [9] 0x1400b5820
void ResidenceUI::OnElementEvent(Object::InternalPtr<UI::Element>* element, int32_t msg, int32_t id,
                                 int32_t /*c*/, int32_t /*d*/, int32_t /*e*/, void* /*userData*/) {
    (void)element;
    if (!residenceui_sa::Ready() || !residenceui_sa::DataSourceAlive(this)) return;

    if (msg != 0x12) return;

    int count = rows._size;
    if (id >= 300 && id < 300 + count) {
        GoToCitizen(id - 300);
    }
}

// ============================================================
// STANDALONE promotion — real ResidenceUI controller vtable
// ============================================================
// A house's info panel lists its residents (name / gender / age / profession, one
// row per occupant) via a ResidenceUI controller bound to the panel's residenceTable.
// In standalone this type was a StubNop (all slots no-op, see ui_dialog_stub_factories
// .cpp — that stub is REMOVED there to avoid a duplicate REGISTER_TYPE), so Slot4
// (build the row table) and Slot7 (fill rows from the ResidenceComponent roster at
// +0x50/+0x58) never dispatched and the list stayed empty even though founding rosters
// the residents. This installs a real per-class vtable, following the StorageUI
// promotion pattern.
//
// Vtable offsets (verified from the exe ResidenceUI vtable @ 0x14015f6d0):
//   +0x00 (idx0) ScalarDtor (0x140020fc0)  +0x10 (idx2) GetTypeId (0x140020f40)
//   +0x20 (idx4) Slot4  (0x1400b5850) — build+cache the per-row element array (+0x30/+0x38)
//   +0x38 (idx7) Slot7  (0x1400b51f0) — populate rows from the ResidenceComponent roster
//   +0x40 (idx8)        (0x14003a96c)  — base DialogController::OnUpdate (per-frame). We
//               drive the refresh here: build once (until +0x38 row count is set), then
//               re-populate every frame so the roster stays live as residents change.
//   +0x48 (idx9) Slot9  (0x1400b5820) — resident-row click dispatch (id 300+index)


// ~ResidenceUI (0x140020fc0): the row array at +0x30 is freed by `rows`' own
// dtor (MemFree + zero data / count / capacity, as the tree's teardown did);
// the compiler then chains ~DialogController -> ~Internal.



// [8] STANDALONE DEVIATION (retail inherits the base slot) — the tree's wiring, kept.
void ResidenceUI::OnUpdate(const System::Time& /*time*/) {
    if (!residenceui_sa::Ready() || !residenceui_sa::DataSourceAlive(this)) return;
    if (rows._size == 0) {
        ResidenceUI::OnCreated();
    }
    ResidenceUI::OnUpdateLayout();
}


// create 0x140020f60: MemAlloc(0x40, 8), then the ctor (DialogController ->
// Internal: AddObject, the compiler's stamps, the field initialisers).
::Object::Base* ResidenceUI::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(ResidenceUI), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) ResidenceUI());
}
// Retail's createInPlace does not build a controller in place (B8-13); the
// tree's in-place entry did, and that is kept.
::Object::Base* ResidenceUI::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) ResidenceUI());
}

DEFINE_EXE_TYPE(ResidenceUI, "UI::DialogController")

// ============================================================
