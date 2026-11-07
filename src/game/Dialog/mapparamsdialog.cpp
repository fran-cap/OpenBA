#include "ui/TextElement.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/NumberElement.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/EditElement.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/ButtonElement.h"
#include "Game/MapGenerator.h"
#include "Game/ClimateGenerator.h"
#include "Game/TerrainGenerator.h"
#include "ui/Element.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/DialogInstance.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/UserInterface.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/String.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/StringTable.h"   // ENDGAME B.3.6 UI callers (K2)
#include <new>   // ENDGAME B.3.6 UI callers (K2)
#include "Dialog/MapParamsDialog.h"
#include "Object/factory_registration.h"
#include <cstdio>
#include "ui/DialogInstance.h"
#include "object/TypeInfo.h"
#include "Game/exe_bodies.h"
#include "Game/NameDatabase.h"   // the Cities.ndb resource (NameDB_GenerateName)
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "system/String.h"
#include "system/Filename.h"
#include "object/External.h"   // ENDGAME B.3.3 step 6A
#include <cstdint>
#include <cstring>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B

// ============================================================
// [5] MapParamsDialog::OnClosed (vtable +0x28) — Shutdown
// ============================================================
// 3-byte no-op: XOR EAX,EAX; RET. Returns 0.
// Original: 0x14006b800.

void MapParamsDialog::OnClosed() {}

// Find an element by name, as its class.
template <class T>
static T* FindElementAs(MapParamsDialog* dialog, const char* name) {
    System::String nameStr(name);
    return static_cast<T*>(dialog->FindElement(nameStr).Get());
}

// The UI::String of a generator's name: its StringTable (+0x20) and name key (+0x28).
template <class Generator>
static UI::String GeneratorNameString(const Generator* desc) {
    UI::String uiStr(Object::ExternalPtr<UI::StringTable>(desc->_field20), desc->_field28);
    uiStr.pad0C = 0;   // built in a zeroed 16-byte slot before
    return uiStr;
}

// ============================================================
// [4] MapParamsDialog::OnOpened (0x14006b810) — Startup
// ============================================================
// Finds all UI elements by name, stores element pointers.
// Sets button IDs (NewName=0, OK=1, Cancel=2).
// Populates fields from the game world state: map name, seed,
// terrain, terrain size, climate, condition, disaster text.
//
// Original: 0x14006b810
void MapParamsDialog::OnOpened() {
    printf("[MapParamsDialog::Startup] self=%p\n", (void*)this);

    // --- Find and store all named elements ---
    editName        = FindElementAs<UI::EditElement>(this, "editName");
    numberSeed      = FindElementAs<UI::NumberElement>(this, "numberSeed");
    textCondition   = FindElementAs<UI::TextElement>(this, "textCondition");
    textTerrain     = FindElementAs<UI::TextElement>(this, "textTerrain");
    textTerrainSize = FindElementAs<UI::TextElement>(this, "textTerrainSize");
    textClimate     = FindElementAs<UI::TextElement>(this, "textClimate");
    textDisaster    = FindElementAs<UI::TextElement>(this, "textDisaster");

    // --- Find button elements (local vars, not stored on this) ---
    UI::Element* buttonNewName = FindElementAs<UI::Element>(this, "buttonNewName");
    UI::ButtonElement* buttonCancel = FindElementAs<UI::ButtonElement>(this, "buttonCancel");
    UI::ButtonElement* ok = FindElementAs<UI::ButtonElement>(this, "buttonOk");

    buttonOk = ok;

    // --- Set element IDs: NewName=0, OK=1, Cancel=2 ---
    if (buttonNewName) buttonNewName->SetElementId(0);
    if (ok) ok->SetElementId(1);
    if (buttonCancel) buttonCancel->SetElementId(2);

    // The cancel button's hot key index (8)
    if (buttonCancel) {
        buttonCancel->_keyIndex = 8;
    }

    // --- Populate fields from game world ---
    GameUpdateLoop* gameWorld = GameUpdateLoop::_singleton;

    // Map name on editName: FUN_140011f90 copies the game's save name
    {
        System::String nameBuf(gameWorld->m_saveName);
        if (editName) editName->SetText(nameBuf);
    }

    // Seed on numberSeed
    {
        int seed = gameWorld->m_seed;
        if (numberSeed) numberSeed->SetValue(static_cast<int32_t>(seed));
    }

    // Set the text fields. Retail uses TWO different UI::String constructors here
    // and they are not interchangeable:
    //   - condition / terrain / climate take the NAME-LOOKUP ctor
    //     UI::String(ExternalPtr<StringTable>, const System::String& name) — the
    //     generator resource carries its own table at +0x20 and its name KEY (a
    //     System::String) at +0x28, and the ctor resolves the key to an index.
    //   - terrainSize / disaster take the INDEX ctor UI::String(table, int), with
    //     the table on the element itself (its _text) and a plain integer.
    // These three used to be built by memcpy'ing desc+0x28 into the struct's int
    // field, which copied the low 32 bits of the name String's CHAR POINTER as if
    // it were a string-table index. Always out of range, so all three rendered
    // blank while name/seed/size/disaster (which take other paths) looked fine.
    //
    // (GameUpdateLoop's three generator configs are typed.)

    // textCondition — the map generator
    {
        const MapGenerator* desc = gameWorld->m_mapConfig;
        if (desc && textCondition) {
            UI::String uiStr = GeneratorNameString(desc);
            textCondition->SetText(&uiStr);
        }
    }

    // textTerrain — the terrain generator
    {
        const TerrainGenerator* desc = gameWorld->m_terrainConfig;
        if (desc && textTerrain) {
            UI::String uiStr = GeneratorNameString(desc);
            textTerrain->SetText(&uiStr);
        }
    }

    // textTerrainSize — the element's own StringTable, the game's map size index
    {
        if (textTerrainSize) {
            UI::String uiStr;
            uiStr.table = textTerrainSize->_text.table;   // ExternalPtr<StringTable>
            uiStr.index = gameWorld->m_mapSize;           // int index
            uiStr.pad0C = 0;
            textTerrainSize->SetText(&uiStr);
        }
    }

    // textClimate — the climate generator
    {
        const ClimateGenerator* desc = gameWorld->m_climateConfig;
        if (desc && textClimate) {
            UI::String uiStr = GeneratorNameString(desc);
            textClimate->SetText(&uiStr);
        }
    }

    // textDisaster — the element's own StringTable, the game flags' bit 6
    {
        if (textDisaster) {
            uint32_t disasterFlags = gameWorld->m_gameFlags;
            uint32_t disasterEnabled = ((disasterFlags >> 6) & 1) != 0 ? 1 : 0;
            UI::String uiStr;
            uiStr.table = textDisaster->_text.table;
            uiStr.index = (int32_t)disasterEnabled;
            uiStr.pad0C = 0;
            textDisaster->SetText(&uiStr);
        }
    }
}

// [6] MapParamsDialog::OnUpdateLayout (0x14006b790) — Update
// Checks if editName field has text; enables/disables OK button accordingly
// (Element slot 8, Enable).
// ============================================================
// [8] MapParamsDialog::OnElementEvent (0x14006b5f0) — OnEvent
// ============================================================
// Handles button clicks in the Map Parameters dialog.
// eventType 0x12 = button click:
//   eventData 0 = buttonNewName → generate random map name (0x14006b700)
//   eventData 1 = buttonOk     → save name to game world, close (0x14006b690)
//   eventData 2 = buttonCancel → close dialog
//
// Original: 0x14006b5f0.

// 0x14006b690: the game's save name = the edit box's text, then close.
void MapParamsDialog::ApplyName() {
    GameUpdateLoop* gameWorld = GameUpdateLoop::_singleton;

    if (editName) {
        const System::String& nameStr = editName->GetText();
        gameWorld->m_saveName = nameStr;
    }

    // Close dialog
    Object::InternalPtr<UI::DialogInstance> basePtr(this);
    _ui->CloseDialog(&basePtr);
}

// 0x14006b700: a random town name sized from the edit box, into the edit box.
void MapParamsDialog::GenerateName() {
    if (editName) {
        // The edit box's max char count
        uint32_t maxLen = (uint32_t)editName->_maxLength;
        int third = (int)maxLen / 3;
        int signAdj = ((int)maxLen >> 31);
        int divResult = third + signAdj;
        int longDivAdj = (int)(((int64_t)(int)maxLen / 3 + ((int64_t)(int)maxLen >> 63)) & 0xFFFFFFFF);
        int finalAdj = (int)((uint32_t)longDivAdj >> 31);
        int nameLen = (divResult + finalAdj) * 2;

        // The game's Random
        System::Random* random = GameUpdateLoop::_singleton->m_random;

        // GenerateTownName — reimpl of FUN_140099ac0
        // Load NameDatabase, generate name, fix "vill" suffix.
        // The name String is constructed ONCE, by 0x1400a4b30 into its return slot:
        // 0x14006b700 hands 0x140099ac0 an uninitialised stack slot (local_res8), which
        // passes it on to 0x1400a4b30; 0x14006b700 destroys it after SetText. The union
        // is that uninitialised slot (no default construction before the generator's).
        union NameSlot {
            System::String str;
            NameSlot() {}
            ~NameSlot() { str.~String(); }
        } nameSlot;
        System::String& nameBuf = nameSlot.str;
        {
            System::Filename fnBuf("Game/Names/Cities.ndb");
            const NameDatabase* nameDB = static_cast<const NameDatabase*>(
                ::Object::ExternalFactory::_singleton->FindOrLoad(fnBuf));
            // The IsDerivedFrom type-check retail runs here is skipped, as in
            // NewGameDialog's GenerateTownName and citizen_names.cpp: the
            // standalone factory tree is incomplete so the walk NULL-faults,
            // and *TypeId_NameDatabase is itself NULL until something
            // lazy-resolves it. FindOrLoad by exact filename is authoritative.

            NameDB_GenerateName(&nameSlot.str, nameDB, random, 5, nameLen);   // 0x1400a4b30

            bool endsVill = false;
            if (nameBuf._data != nullptr) {
                int slen = nameBuf.GetLength();
                if (slen > 4) {
                    System::String sfx = nameBuf.SubString(slen - 4, slen - 1);
                    endsVill = (sfx == "vill");
                }
            }
            if (endsVill) {
                // ret is unused (see recipe's Append note): mutate nameBuf directly.
                nameBuf += "e";
            }
        }
        editName->SetText(nameBuf);
    }
}

void MapParamsDialog::OnElementEvent(Object::InternalPtr<UI::Element>* /*element*/, UI::EventId eventType, int32_t eventData, int32_t /*c*/, int32_t /*d*/, int32_t /*e*/, void* /*userData*/) {
    if (eventType != UI::EventId::Activated) return;

    if (eventData == 1) {
        // buttonOk: save map name to game world and close
        ApplyName();
        return;
    }

    if (eventData == 2) {
        // buttonCancel: close dialog
        Object::InternalPtr<UI::DialogInstance> basePtr(this);
        _ui->CloseDialog(&basePtr);
        return;
    }

    if (eventData == 0) {
        // buttonNewName: generate random name using the game's Random
        GenerateName();
    }
}

void MapParamsDialog::OnUpdateLayout() {
    // The name edit box's text length (0 when empty).
    int textLen = editName->GetText().GetLength();

    // Enable/disable OK button based on text length (Element slot 8)
    buttonOk->Enable(textLen > 0);
}

// ============================================================
// Construction, destruction, reflection (ENDGAME B.3.8 block DLG)
// ============================================================
// Retail 0x14006bd80: DialogInstance base ctor (the caller's InternalBasePtr
// copied to a stack slot, its ADDRESS passed on), the vtable store, then
// +0xB8..+0xF0 zeroed: editName, numberSeed, textTerrain, textTerrainSize,
// textClimate, textCondition, textDisaster, buttonOk — borrowed element
// pointers OnOpened fills in.

MapParamsDialog::MapParamsDialog()
    : editName(nullptr), numberSeed(nullptr), textTerrain(nullptr), textTerrainSize(nullptr),
      textClimate(nullptr), textCondition(nullptr), textDisaster(nullptr), buttonOk(nullptr) {}

MapParamsDialog::MapParamsDialog(UI::UserInterface* ui, const UI::Dialog* dialog,
                                 Object::InternalPtr<UI::DialogInstance> parent)
    : UI::DialogInstance(ui, dialog, parent, nullptr, nullptr, 0),
      editName(nullptr), numberSeed(nullptr), textTerrain(nullptr), textTerrainSize(nullptr),
      textClimate(nullptr), textCondition(nullptr), textDisaster(nullptr), buttonOk(nullptr) {}

// create 0x140012100: MemAlloc(0xf8, 8), then the ctor.
::Object::Base* MapParamsDialog::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(MapParamsDialog), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(::new (p) MapParamsDialog());
}
// createInPlace 0x140012140 (the tree's in-place build is kept, B8-13).
::Object::Base* MapParamsDialog::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(::new (storage) MapParamsDialog());
}

// The reflection record stays at the TU position of the UI_DIALOG_STUB row it
// replaces (ui_dialog_stub_factories.cpp, B38 D4).
