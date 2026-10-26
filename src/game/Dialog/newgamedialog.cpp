// src/game/Dialog/newgamedialog.cpp
// Clean reimplementation of NewGameDialog methods, and MainMenuDialog's
// StartNewGame (0x1400b4540), which the dialog's OK button runs.
//
// NewGameDialog (0xAE8 bytes): New game setup dialog. Fields: NewGameDialog.h.
//
// Original source: C:\Projects\Buildy\code\Game\Dialog/NewGameDialog.h
//                  Dialog\NewGameDialog.cpp
//
// LEFT (B.4 W1 DLG-b): the three generator classes (Game/MapGenerator.h,
// ClimateGenerator.h, TerrainGenerator.h) declare their StringTable ExternalPtr
// and their name / description Strings as `void*`; the reads of those fields
// stay casts until those headers are typed. EditElement::_text is the String's
// raw `char*` (H5, runtime UI header).

#include "load_time.h"   // [LoadTime] stamps (NOT RETAIL, print-only; block LOADSTAMP)
#include "ui/Sprite.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/TextElement.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/ComboElement.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/EditElement.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/NumberEditElement.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/ButtonElement.h"
#include "ui/Element.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/DialogInstance.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/UserInterface.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/String.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/StringTable.h"   // ENDGAME B.3.6 UI callers (K2)
#include "Dialog/NewGameDialog.h"
#include "Dialog/MainMenuDialog.h"
#include "Dialog/ui_dialogs.h"
#include "options.h"
#include "Dialog/dialog_exe_helpers.h"
#include "ui/DialogInstance.h"
#include "Game/game.h"   // B3R STORAGE: g_gameContainer
#include "Game/exe_bodies.h"   // B3R STORAGE: g_gameSettings
#include "Game/NameDatabase.h"   // the Cities.ndb resource (NameDB_GenerateName)
#include "Game/GameSettings.h"
#include "system/String.h"
#include "system/Filename.h"
#include "object/External.h"   // ENDGAME B.3.3 step 6A
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "object/TypeInfo.h"   // ENDGAME B.3.3 step 6C
#include "system/Value.h"      // ENDGAME B.3.4b System callers: System::Random
#include "Game/GameUpdateLoop.h"       // ENDGAME B.3.9 EXA: the real loop classes
#include "Game/GameLoadDialogLoopT.h"
#include "Game/GameLoaderLoop.h"
#include "Game/MenuDialogLoopT.h"
#include "Game/MapGenerator.h"   // X::TypeId() (ENDGAME B.3.9)
#include "Game/ClimateGenerator.h"   // X::TypeId() (ENDGAME B.3.9)
#include "Game/TerrainGenerator.h"   // X::TypeId() (ENDGAME B.3.9)
#include "Dialog/exe_bodies.h"
#include "system/MemoryAllocator.h"   // B3R: the allocator doors
#include "rt_crt.h"   // rtcrt::StrICmp, retail's _stricmp (D.4 Q9)

// ============================================================
// Helper: find element by name, returns the typed element
// ============================================================
template <class T>
static T* FindElementAs(NewGameDialog* dialog, const char* name) {
    System::String nameStr(name);
    return static_cast<T*>(dialog->FindElement(nameStr).Get());
}

// ENDGAME B.3.9: MapGenerator / ClimateGenerator / TerrainGenerator / NameDatabase all
// have a ported TypeId() now (class statics, always populated), so the
// EnsureGeneratorTypeIds()/LazyTypeId() lazy-cache over the exe .rdata globals is dead.

// Type-check a loaded resource. In STANDALONE the Runtime factory tree is
// incomplete, so Object::IsDerivedFrom walks up the type's parent chain into a
// NULL factory and crashes (NULL-this). Per feedback_factory_cascade_strategy we
// trust FindOrLoad's result and skip the derivation check. typeId is unused (kept
// as a parameter so every call site still names which type it means).
static inline bool ResourceIsType(const ::Object::External* resource, uint32_t typeId) {
    if (!resource) return false;
    (void)typeId;
    return true;
}

// ============================================================
// Helper: populate combo from ExternalList with type check
// ============================================================
// `visible` is the generator's exposed flag (MapGenerator +0x38, the Climate /
// Terrain generators' +0x30).
template <class Generator, class Flag>
static void PopulateGeneratorCombo(UI::ComboElement* combo, ::Object::ExternalList* externalList,
                                   uint32_t typeId, Flag Generator::*visible) {
    // Standalone safety: the generator ExternalLists may be unpopulated if the Game
    // resource container hasn't run yet — leave the combo empty rather than
    // dereferencing a null list.
    if (!externalList) return;
    int count = externalList->GetResourceCount();
    for (int i = 0; i < count; i++) {
        ::Object::External* resource = externalList->_resources[i];

        // IsDerivedFrom check (trust-helper: skips the crashing walk in standalone)
        Generator* typed = nullptr;
        if (ResourceIsType(resource, typeId)) {
            typed = static_cast<Generator*>(resource);
        }

        // Check the visibility flag
        if (typed && typed->*visible != 0) {
            // UI::String from the resource's name: its StringTable (+0x20) and name key (+0x28).
            UI::String uiStr(Object::ExternalPtr<UI::StringTable>(typed->_field20), typed->_field28);

            // Convert UI::String to System::String
            System::String sysStr(uiStr.operator System::String());

            // Sprite(ExternalPtr<SpriteSheet>(null), name) stores {null, -1} without
            // reading the name (sprite.cpp, retail 0x18011b9c0) == the default ctor.
            UI::Sprite sprite;

            combo->AddItem(sprite, sysStr, (void*)(intptr_t)i);  // CAST-OK: ptr_int
        }

        // Re-fetch count in case it changed (matches original)
        count = externalList->GetResourceCount();
    }
}

// ============================================================
// GenerateTownName — reimplementation of FUN_140099ac0
// ============================================================
// Loads "Game/Names/Cities.ndb", generates a random town name using the
// NameDatabase, then checks if it ends in "vill" and appends "e" to make "ville".
// Original: 0x140099ac0.
// retStr is the caller's UNINITIALISED return slot: 0x1400a4b30 constructs the String in it
// (known_port_issues rows 572 (a) / 941).
static void GenerateTownName(System::String& retStr, System::Random* random, int maxLen) {
    // Load the city-name database. The IsDerivedFrom type-check retail runs on
    // the result is skipped: the standalone factory tree is incomplete and the
    // walk NULL-faults, while FindOrLoad by exact filename is authoritative
    // (same call shape as citizen_names.cpp's Male.ndb/Female.ndb load).
    const NameDatabase* nameDB = nullptr;
    if (::Object::ExternalFactory::_singleton) {
        System::Filename fn("Game/Names/Cities.ndb");
        nameDB = static_cast<const NameDatabase*>(
            ::Object::ExternalFactory::_singleton->FindOrLoad(fn));
    }

    // minLen 5 is retail's constant; maxLen is derived from the edit box's
    // _maxLength by the caller. NameDB_GenerateName (0x1400a4b30, registered by
    // standalone/reg_namedb.cpp) constructs the String in retStr even when the
    // database is missing, so retStr is always live for the suffix fix-up below.
    NameDB_GenerateName(&retStr, nameDB, random, 5, maxLen);   // 0x1400a4b30

    // Retail: a name longer than 4 chars ending in "vill" gets an "e" appended,
    // so the generator's "-vill" stems read as "-ville".
    bool endsVill = false;
    int slen = retStr.GetLength();
    if (slen > 4) {
        System::String sfx = retStr.SubString(slen - 4, slen - 1);
        endsVill = (sfx == "vill");
    }
    if (endsVill) {
        retStr += "e";
    }
}

static void GenerateRandomName(UI::EditElement* editElem, System::Random* random) {
    // The edit box's max char count
    int maxLen = editElem->_maxLength;

    // Original formula: (maxLen / 3 + adjustment) * 2
    // Integer division with sign correction
    int third = maxLen / 3;
    int signAdj = (maxLen >> 31);  // sign bit
    int divResult = third + signAdj;
    int longDivAdj = (int)(((int64_t)maxLen / 3 + ((int64_t)maxLen >> 63)) & 0xFFFFFFFF);
    int finalAdj = (int)((uint32_t)longDivAdj >> 31);
    int nameLen = (divResult + finalAdj) * 2;

    // The uninitialised return slot of 0x140099ac0 (constructed once, by 0x1400a4b30).
    union NameSlot {
        System::String str;
        NameSlot() {}
        ~NameSlot() { str.~String(); }
    } nameSlot;
    System::String& nameStr = nameSlot.str;
    GenerateTownName(nameStr, random, nameLen);

    // Mirrors the seed logging below: both halves of the "randomise" row report
    // what they produced, so a dead generator is visible in the log instead of
    // only on screen.
    {
        const char* sp = nameStr._data;
        fprintf(stderr, "[NewGameDialog] town name generated: \"%s\" (maxLen=%d)\n",
                sp ? sp : "(empty)", nameLen);
        fflush(stderr);
    }

    editElem->SetText(nameStr);
}

// ============================================================
// Helper: generate random seed
// ============================================================
static void GenerateRandomSeed(UI::NumberEditElement* seedElem, System::Random* random) {
    // The number box's max value
    int maxVal = seedElem->_max;

    // BANISHED_SEED pins the world seed instead of randomising it. Needed for
    // A/B testing: a RenderDoc capture is only comparable against another
    // capture if both runs generated the same world, and autostart otherwise
    // rolls a fresh seed every launch. Purely additive — unset behaves exactly
    // like the original.
    //
    // ⚠ SCOPE — this value reaches FURTHER than "world generation", but runs still are
    // NOT reproducible, and the reason is not the seeding.
    //   BANISHED_SEED -> this seed element -> GameUpdateLoop::m_seed (StartNewGame)
    //                 -> Random(seed) for the gameplay RNG at GameUpdateLoop::m_random
    //                    (gameupdateloop_load.cpp).
    // So the in-sim RNG that drives citizen gender, ages and model variants IS pinned.
    // Yet identical configs measurably diverge: 2026-07-31 gave gender splits 14/13,
    // 15/12 and 13/14 and extinction frames spanning ~7500-10500. Same seed, same
    // starting state, different outcomes => the divergence is in the ORDER or COUNT of
    // RNG consumption (or a second entropy source), NOT in the seed.
    // **Do not A/B two fixes on one run of each and rank them by outcome** — and do not
    // "fix" this by pinning m_random; it is already pinned.
    //
    // Both branches log, to stderr, ALWAYS. This used to be a printf on the pinned
    // branch only, i.e. into the stream that stalls (standalone_debug.log freezes at
    // ~22KB), so no log could confirm the seed had been applied and a silent random
    // seed was indistinguishable from a pinned one. Every past "this run used seed N"
    // was an assumption.
    const char* envSeed = Opt().seed;
    if (envSeed && envSeed[0]) {
        uint32_t fixed = (uint32_t)strtoul(envSeed, nullptr, 0);
        if (maxVal > 0) fixed %= (uint32_t)(maxVal + 1);
        seedElem->SetValue(fixed);
        fprintf(stderr, "[NewGameDialog] BANISHED_SEED=%s -> seed PINNED to %u "
                "(also seeds the gameplay RNG at gw+0xd8; runs still diverge — see the "
                "scope note above)\n", envSeed, fixed);
        fflush(stderr);
        return;
    }

    uint32_t seed = random->Rand((uint32_t)(maxVal + 1));
    seedElem->SetValue(seed);
    fprintf(stderr, "[NewGameDialog] BANISHED_SEED unset -> world seed RANDOMISED to %u\n",
            seed);
    fflush(stderr);
}

// ============================================================
// NewGameDialog::UpdateDescription — the selected map generator's description
// ============================================================
void NewGameDialog::UpdateDescription() {
    // Standalone safety: skip if the map-generator list is unresolved (covers both
    // OnOpened and the OnElementEvent combo-change call site).
    if (!mapGeneratorList) return;

    // Selected map generator resource
    int selectedIdx = comboCondition->_currentItem;
    ::Object::External* resource = mapGeneratorList->_resources[selectedIdx];

    MapGenerator* mapGen = nullptr;
    if (ResourceIsType(resource, MapGenerator::TypeId())) {
        mapGen = static_cast<MapGenerator*>(resource);
    }

    // UI::String from the description: StringTable (+0x20), description key (+0x30).
    UI::String uiStr(Object::ExternalPtr<UI::StringTable>(mapGen->_field20), mapGen->_field30);

    textDescription->SetText(&uiStr);
}

// The start condition's name key (MapGenerator +0x28), for the log lines.
static const char* GeneratorName(const ::Object::External* res) {
    return static_cast<const MapGenerator*>(res)->_field28._data;
}

// ============================================================
// SelectStartConditionOverride (standalone-only, additive)
// ============================================================
// BANISHED_START_CONDITION picks the start-condition combo entry: digits = list
// index, anything else = case-insensitive substring of the entry's name key
// (e.g. "Easy"/"Medium"/"Hard"), "none" = off.
// When unset, an autostart run defaults to "easy" so the livestock start path
// (free-roaming cows/sheep/chickens from the simple pool) is exercised; manual
// runs are untouched. Runs after the saved-selection restore so it wins, and
// before UpdateDescription so the UI shows the forced choice. Logs every entry
// (index, name, visibility) so the index<->name mapping is verifiable in the log.
// NOTE: retail's StartGame treats the combo's current item as a LIST index (all
// retail start conditions are visible, so combo index == list index); we match that.
static void SelectStartConditionOverride(NewGameDialog* self) {
    const char* want = Opt().startCondition;
    if (!want || !want[0]) {
        if (!Opt().autostart)
            return;                       // manual run, no override requested
        want = "easy";                    // autostart default
    }
    if (std::strcmp(want, "none") == 0) return;
    if (!self->mapGeneratorList || !self->comboCondition) return;

    bool numeric = true;
    for (const char* p = want; *p; ++p)
        if (*p < '0' || *p > '9') { numeric = false; break; }

    auto containsNoCase = [](const char* hay, const char* needle) -> bool {
        if (!hay || !needle || !*needle) return false;
        size_t nlen = std::strlen(needle);
        for (const char* h = hay; *h; ++h) {
            size_t i = 0;
            while (i < nlen && h[i] &&
                   std::tolower((unsigned char)h[i]) ==
                   std::tolower((unsigned char)needle[i]))
                ++i;
            if (i == nlen) return true;
        }
        return false;
    };

    ::Object::ExternalList* list = self->mapGeneratorList;
    int count = list->GetResourceCount();
    int target = -1;
    for (int i = 0; i < count; ++i) {
        const ::Object::External* res = list->_resources[i];
        const char* name = res ? GeneratorName(res) : nullptr;
        uint8_t vis = res ? static_cast<const MapGenerator*>(res)->enabled : 0;
        fprintf(stderr, "[NewGameDialog] startCondition[%d] name=\"%.32s\" visible=%d\n",
                i, name ? name : "(null)", (int)vis);
        if (target < 0 && !numeric && containsNoCase(name, want))
            target = i;
    }
    if (numeric) {
        int idx = std::atoi(want);
        if (idx >= 0 && idx < count) target = idx;
    }
    if (target >= 0) {
        self->comboCondition->SetCurrentItem(target);
        fprintf(stderr, "[NewGameDialog] start condition FORCED to [%d] (want=\"%s\")\n",
                target, want);
    } else {
        fprintf(stderr, "[NewGameDialog] start condition override \"%s\" matched "
                "nothing (%d entries) — selection left alone\n", want, count);
    }
    fflush(stderr);
}

// Restore one combo's saved selection: retail's shape (an in-range negative
// index is used as-is, an out-of-range one falls back to 0).
static void RestoreComboSelection(UI::ComboElement* combo, int32_t saved) {
    if (saved < 0) {
        int count = combo->GetItemCount();
        if (saved < count) {
            combo->SetCurrentItem(saved);
        } else {
            combo->SetCurrentItem(0);
        }
    } else {
        combo->SetCurrentItem(saved);
    }
}

// ============================================================
// OnOpened (0x1400706d0): Startup — build new game UI
// ============================================================
// Finds all UI elements, populates combo boxes with map/climate/terrain
// generators, generates random name and seed, restores saved combo
// selections from game settings.
//
// Original: 315 lines decompiled.

void NewGameDialog::OnOpened() {
    // The three generator lists (container +0x10 / +0x18 / +0x20) were filled at boot
    // by GameContainer_Init 0x140099db0 (InnerLoaderLoop::Load), as in retail.

    // --- Find all UI elements ---
    editName        = FindElementAs<UI::EditElement>(this, "editName");
    editSeed        = FindElementAs<UI::NumberEditElement>(this, "editSeed");
    textDescription = FindElementAs<UI::TextElement>(this, "textDescription");
    comboCondition  = FindElementAs<UI::ComboElement>(this, "comboCondition");
    comboTerrain    = FindElementAs<UI::ComboElement>(this, "comboTerrain");
    comboTerrainSize = FindElementAs<UI::ComboElement>(this, "comboTerrainSize");
    comboClimate    = FindElementAs<UI::ComboElement>(this, "comboClimate");
    comboDisaster   = FindElementAs<UI::ComboElement>(this, "comboDisaster");

    // --- Cache the generator ExternalLists from the Game container ---
    GameContainer* container = g_gameContainer;
    if (container) {
        mapGeneratorList     = container->startConditions.Get();
        climateGeneratorList = container->climateList.Get();
        terrainGeneratorList = container->terrainList.Get();
    }

    // --- Populate generator combos. Guarded for standalone: the ExternalLists may
    //     be unpopulated (PopulateGeneratorCombo null-checks the list internally). ---
    PopulateGeneratorCombo(comboCondition, mapGeneratorList,
                           MapGenerator::TypeId(), &MapGenerator::enabled);
    PopulateGeneratorCombo(comboClimate, climateGeneratorList,
                           ClimateGenerator::TypeId(), &ClimateGenerator::_field30);
    PopulateGeneratorCombo(comboTerrain, terrainGeneratorList,
                           TerrainGenerator::TypeId(), &TerrainGenerator::_field30);

    // --- Find button elements ---
    UI::Element* buttonNewName = FindElementAs<UI::Element>(this, "buttonNewName");
    UI::Element* buttonNewSeed = FindElementAs<UI::Element>(this, "buttonNewSeed");
    UI::Element* buttonCancel  = FindElementAs<UI::Element>(this, "buttonCancel");
    UI::ButtonElement* ok      = FindElementAs<UI::ButtonElement>(this, "buttonOk");

    // --- Assign element IDs ---
    // buttonNewName = 0, buttonNewSeed = 1, buttonOk = 2, buttonCancel = 3, comboCondition = 4
    buttonNewName->SetElementId(0);

    buttonNewSeed->SetElementId(1);

    buttonOk = ok;
    buttonOk->SetElementId(2);

    buttonCancel->SetElementId(3);

    comboCondition->SetElementId(4);

    // --- Generate random name (guarded: editName must be present) ---
    if (editName)
        GenerateRandomName(editName, &random);

    // --- Generate random seed (guarded: editSeed must be present) ---
    if (editSeed)
        GenerateRandomSeed(editSeed, &random);

    // --- Restore saved combo selections from game settings ---
    // Standalone: the settings block may be NULL — skip the whole restore block
    // rather than dereferencing null.
    GameSettings* settings = g_gameSettings;
    if (settings) {
        RestoreComboSelection(comboCondition, settings->lastStart);
        RestoreComboSelection(comboTerrain, settings->lastTerrain);
        RestoreComboSelection(comboTerrainSize, settings->lastMapSize);
        RestoreComboSelection(comboClimate, settings->lastClimate);

        // Disaster combo: the same test spelled the other way round
        {
            int idx = settings->lastDisaster;
            if (idx < 0) {
                int count = comboDisaster->GetItemCount();
                if (count <= idx) {
                    comboDisaster->SetCurrentItem(0);
                } else {
                    comboDisaster->SetCurrentItem(settings->lastDisaster);
                }
            } else {
                comboDisaster->SetCurrentItem(settings->lastDisaster);
            }
        }
    }  // end if (settings) — standalone null-settings guard

    // --- Standalone: env/autostart start-condition override (wins over the
    //     saved-selection restore above; see SelectStartConditionOverride) ---
    SelectStartConditionOverride(this);

    // --- Combo inventory (standalone verification) ---
    // Retail contents, for comparison: condition 3 (Hard/Medium/Easy),
    // terrain 2 (Valleys/Mountains), terrainSize 3 (Small/Medium/Large),
    // climate 3 (Mild/Fair/Harsh), disaster 2 (On/Off). The first three come
    // from the generator ExternalLists, the last two from the .rsc combo's
    // _initialData StringTable, so a 0 count tells you which half is missing.
    {
        struct { const char* name; UI::ComboElement* combo; } combos[] = {
            { "comboCondition",   comboCondition   },
            { "comboTerrain",     comboTerrain     },
            { "comboTerrainSize", comboTerrainSize },
            { "comboClimate",     comboClimate     },
            { "comboDisaster",    comboDisaster    },
        };
        for (auto& c : combos) {
            if (!c.combo) { fprintf(stderr, "[NewGameDialog] %s MISSING\n", c.name); continue; }
            fprintf(stderr, "[NewGameDialog] %s items=%d selected=%d\n", c.name,
                    c.combo->GetItemCount(), c.combo->_currentItem);
        }
        fflush(stderr);
    }

    // --- Update description text from selected map generator ---
    UpdateDescription();
}

// ============================================================
// OnUpdateLayout (0x140070650): sync OK button enabled state
// ============================================================
// Enables buttonOk if editName has content, disables if empty.

// ============================================================
// NewGameDialog::OnElementEvent (0x140070110)
// ============================================================
// Handles button clicks and combo changes in the New Game dialog.
// eventType 0x12 = button click:
//   eventData 0 = buttonNewName → generate random name
//   eventData 1 = buttonNewSeed → generate random seed
//   eventData 2 = buttonOk     → start new game
//   eventData 3 = buttonCancel → close dialog
// eventType 0x14 = combo change:
//   eventData 4 = comboCondition → update description text
//
// Original: 0x140070110.

void NewGameDialog::StartGame() {
    // Read combo selected items
    int condIdx = comboCondition->_currentItem;

    int climateIdx = comboClimate->_currentItem;

    // --- Climate override (standalone autostart + testing) ---
    // ClimateList.rsc order: 0 = Mild, 1 = Fair, 2 = Harsh. BANISHED_CLIMATE =
    // mild|fair|harsh (or 0|1|2). Autostart otherwise takes the combo default, which is
    // whatever the saved settings or index 0 give — the stress harness pins "fair" so the
    // seasonal temperature curve (and with it snow cover, fuel burn and field idling) is
    // the same town-to-town.
    if (const char* envClimate = Opt().climate) {
        int want = -1;
        if (!strcmp(envClimate, "0") || !rtcrt::StrICmp(envClimate, "mild"))       want = 0;
        else if (!strcmp(envClimate, "1") || !rtcrt::StrICmp(envClimate, "fair"))  want = 1;
        else if (!strcmp(envClimate, "2") || !rtcrt::StrICmp(envClimate, "harsh")) want = 2;
        if (want >= 0) {
            fprintf(stderr, "[NewGameDialog] BANISHED_CLIMATE=%s -> climateIdx %d -> %d\n",
                    envClimate, climateIdx, want);
            climateIdx = want;
        } else {
            fprintf(stderr, "[NewGameDialog] BANISHED_CLIMATE=%s not understood "
                    "(mild|fair|harsh|0|1|2); combo value %d kept\n", envClimate, climateIdx);
        }
    }

    int terrainIdx = comboTerrain->_currentItem;

    // Combo item data: the generator's index in its ExternalList
    void* mapGenItem = comboCondition->GetItemData(condIdx);
    ::Object::External** mapPtrArray = mapGeneratorList->_resources;
    ::Object::External* mapGenResource = mapPtrArray[(intptr_t)mapGenItem];  // CAST-OK: ptr_int
    MapGenerator* mapGen = nullptr;
    if (ResourceIsType(mapGenResource, MapGenerator::TypeId())) mapGen = static_cast<MapGenerator*>(mapGenResource);

    // --- Difficulty / StartCondition override (standalone autostart + testing) ---
    // The autostart harness auto-clicks "OK" without choosing a difficulty, and the
    // saved-settings combo restore is skipped when the settings block is null, so we
    // would otherwise get whatever the combo defaults to (Hard/Medium). Resolve the
    // MapGenerator directly from the StartCondition list by index instead.
    //   StartConditions.rsc list order: 0 = Hard, 1 = Medium, 2 = Easy.
    //   BANISHED_DIFFICULTY = easy|medium|hard (or 2|1|0) forces a level.
    //   Under autostart with no override, default to Easy so the full starting town
    //   (StorageBarn + StorageYard + 6 WoodHouses, 6 families) spawns.
    {
        int wantIdx = -1;
        const char* envDiff = Opt().difficulty;
        if (envDiff) {
            if (!strcmp(envDiff, "2") || !strcmp(envDiff, "easy") || !strcmp(envDiff, "Easy"))
                wantIdx = 2;
            else if (!strcmp(envDiff, "1") || !strcmp(envDiff, "medium") || !strcmp(envDiff, "Medium"))
                wantIdx = 1;
            else if (!strcmp(envDiff, "0") || !strcmp(envDiff, "hard") || !strcmp(envDiff, "Hard"))
                wantIdx = 0;
        } else {
            if (Opt().autostart)
                wantIdx = 2;  // Easy default for the autostart demo town
        }
        if (wantIdx >= 0 && mapGeneratorList && mapPtrArray) {
            int listCount = mapGeneratorList->GetResourceCount();
            if (wantIdx < listCount) {
                ::Object::External* r = mapPtrArray[wantIdx];
                if (ResourceIsType(r, MapGenerator::TypeId())) {
                    mapGen = static_cast<MapGenerator*>(r);
                    fprintf(stderr, "[NewGameDialog] difficulty override -> listIdx=%d (0=Hard 1=Medium 2=Easy) mapGen=%p\n",
                            wantIdx, (void*)mapGen);
                    fflush(stderr);
                }
            }
        }
    }

    void* climateItem = comboClimate->GetItemData(climateIdx);
    ::Object::External* climateResource = climateGeneratorList->_resources[(intptr_t)climateItem];  // CAST-OK: ptr_int
    ClimateGenerator* climateGen = nullptr;
    if (ResourceIsType(climateResource, ClimateGenerator::TypeId())) climateGen = static_cast<ClimateGenerator*>(climateResource);

    void* terrainItem = comboTerrain->GetItemData(terrainIdx);
    ::Object::External* terrainResource = terrainGeneratorList->_resources[(intptr_t)terrainItem];  // CAST-OK: ptr_int
    TerrainGenerator* terrainGen = nullptr;
    if (ResourceIsType(terrainResource, TerrainGenerator::TypeId())) terrainGen = static_cast<TerrainGenerator*>(terrainResource);

    // Terrain size index
    uint32_t terrainSize = (uint32_t)comboTerrainSize->_currentItem;

    // --- Map size override (standalone autostart + testing) ---
    // terrainSize is a row index into the TerrainGenerator's size table
    // (Valleys.rsc / Mountains.rsc: 0 = 256 small, 1 = 384 medium, 2 = 512 large).
    // BANISHED_MAP_SIZE = small|medium|large (or 0|1|2). The stress harness sets
    // "medium" by default; unset leaves the combo's value alone.
    if (const char* envSize = Opt().mapSize) {
        int want = -1;
        if (!strcmp(envSize, "0") || !rtcrt::StrICmp(envSize, "small"))       want = 0;
        else if (!strcmp(envSize, "1") || !rtcrt::StrICmp(envSize, "medium")) want = 1;
        else if (!strcmp(envSize, "2") || !rtcrt::StrICmp(envSize, "large"))  want = 2;
        if (want >= 0) {
            fprintf(stderr, "[NewGameDialog] BANISHED_MAP_SIZE=%s -> terrainSize %u -> %d\n",
                    envSize, terrainSize, want);
            terrainSize = (uint32_t)want;
        } else {
            fprintf(stderr, "[NewGameDialog] BANISHED_MAP_SIZE=%s not understood "
                    "(small|medium|large|0|1|2); combo value %u kept\n", envSize, terrainSize);
        }
    }

    // Seed value
    uint32_t seed = (uint32_t)editSeed->GetValue();

    // Disaster enabled (0 = disasters on, nonzero = off)
    int32_t disasterIdx = comboDisaster->_currentItem;
    int disasterOff = (disasterIdx == 0) ? 0 : 1;

    // --- What the player actually chose (standalone verification) ---
    // Every row of the dialog lands in the GameUpdateLoop below, so log the
    // resolved pick for each one. The three generator rows are named from the
    // resource's StringTable key (+0x28); terrainSize and disaster have no
    // resource behind them, so they log as raw combo indices. worldFlags is the
    // word copied to GameUpdateLoop::m_gameFlags, whose bits 4-7 are noSick /
    // noCropDisease / noFire / noTornado — 0x00 means all four disasters are live.
    {
        auto nameOf = [](const ::Object::External* res) -> const char* {
            if (!res) return "(null)";
            const char* n = GeneratorName(res);
            return n ? n : "(unnamed)";
        };
        fprintf(stderr, "[NewGameDialog::StartGame] picks: condition[%d]=\"%s\" "
                "terrain[%d]=\"%s\" size=%u climate[%d]=\"%s\" disaster[%d]=%s "
                "seed=%u -> worldFlags=0x%02X\n",
                condIdx, nameOf(mapGen), terrainIdx, nameOf(terrainGen), terrainSize,
                climateIdx, nameOf(climateGen), disasterIdx,
                disasterOff ? "OFF" : "ON", seed,
                (disasterIdx == 0) ? 0 : 0xF0);
        fflush(stderr);
    }

    // The map name: the name edit box's String.
    const System::String& nameStr = editName->GetText();

    // Save combo selections to game settings
    GameSettings* settings = g_gameSettings;
    settings->lastStart = condIdx;
    settings->lastTerrain = terrainIdx;
    settings->lastMapSize = (int32_t)terrainSize;
    settings->lastClimate = climateIdx;
    settings->lastDisaster = disasterIdx;

    // StartNewGame 0x1400b4540: GameUpdateLoop -> GameLoadDialogLoopT -> LoaderLoop,
    // then the next loop on the menu loop or the running game.
    // Retail's argument order is (mapGen, terrainGen, climateGen):
    // args 6/7/8 land in GameUpdateLoop +0x40/+0x48/+0x50 in that order.
    mainMenu->StartNewGame(nameStr, seed, terrainSize,
                           /*disastersOff=*/disasterIdx != 0, mapGen, terrainGen, climateGen,
                           /*scenarioResIdx=*/(uint32_t)-1, /*savedStateIBP=*/nullptr);
}

// ============================================================
// MainMenuDialog::StartNewGame (exe 0x1400b4540)
// ============================================================
// Builds the GameUpdateLoop -> GameLoadDialogLoopT -> LoaderLoop chain and hands it
// to the running game (or to the owning menu loop) as the next loop.
//
// Two callers: NewGameDialog's Start (the path BANISHED_AUTOSTART drives) and
// ScenarioDialog::LoadSelected.
// A new sandbox game passes scenarioResIdx = -1 and a null saved-state IBP, while a
// scenario passes its resource index and (for a saved scenario) the restored state.
void* MainMenuDialog::StartNewGame(const System::String& name, uint32_t seed, uint32_t terrainSize,
                                   bool disastersOff, MapGenerator* mapGen, TerrainGenerator* terrainGen,
                                   ClimateGenerator* climateGen,
                                   uint32_t scenarioResIdx,
                                   const Object::InternalPtr<Achievement::AchievementInstance>* savedStateIBP) {
    {
        LoadTime::Stamp("newgame.click");   // NOT RETAIL: print-only
        fprintf(stderr, "[StartNewGame] ENTER worldSetup=%p seed=%u size=%u disastersOff=%d "
                "scenarioIdx=%d saved=%p\n",
                (void*)this, seed, terrainSize, (int)disastersOff, (int)scenarioResIdx,
                savedStateIBP);
        fflush(stderr);

        // Step 1: GameUpdateLoop (0x160 bytes). ENDGAME B.3.9 EXA: the real class
        // (its ctor is 0x140098060, which retail CALLs here). Retail passes
        // savedState = 0 and a copy of *param_10 (the saved-state IBP) by
        // address; the tree's new-game caller hands a null param_10, which the
        // tree has always read as a null IBP. Retail's +0xD0 is `p8 ? 0 : 0xF0`;
        // this function's `disastersOff` is the inverse sense, so p8 = !disastersOff
        // writes exactly the bytes the old inline stores wrote.
        void* gameLoop = System::MemoryAllocator::MemAlloc(0x160, 8);
        Engine::UpdateLoop* pGameLoop = nullptr;
        if (gameLoop) {
            const Object::InternalPtr<Achievement::AchievementInstance> ibp = savedStateIBP ? *savedStateIBP : Object::InternalPtr<Achievement::AchievementInstance>();
            pGameLoop = ::new (gameLoop) GameUpdateLoop(
                nullptr, ibp, (int32_t)scenarioResIdx,
                name,
                (int32_t)seed, (int32_t)terrainSize, /*p8=*/!disastersOff,
                mapGen, terrainGen, climateGen);
        }

        // Step 2: GameLoadDialogLoopT (0xB60 bytes) — retail inlines the subclass
        // ctor here; the real class's ctor is that body.
        void* dlLoop = System::MemoryAllocator::MemAlloc(0xB60, 4);
        Engine::UpdateLoop* pDlLoop = nullptr;
        if (dlLoop) {
            System::Time delayTime;
            delayTime._ticks = 0;
            RtSystem_Time_Ctor(&delayTime, 1.0f);
            System::String loadingStr;
            GetLoadingSoundString(&loadingStr);
            pDlLoop = ::new (dlLoop) GameLoadDialogLoopT(loadingStr, pGameLoop, delayTime);
        }

        // Step 3: outer LoaderLoop (0x48 bytes): `??0LoaderLoop` + the 0x1401588d0
        // stamp, i.e. the real GameLoaderLoop.
        void* outerLoop = System::MemoryAllocator::MemAlloc(0x48, 4);
        GameLoaderLoop* pOuterLoop = nullptr;
        if (outerLoop) {
            System::Time zeroTime;
            zeroTime._ticks = 0;
            RtSystem_Time_Ctor(&zeroTime, 0.0f);
            pOuterLoop = ::new (outerLoop) GameLoaderLoop(pDlLoop, zeroTime);
        }

        // Step 4: Set next loop.
        // No menu loop (the pause menu) -> the running game's next loop.
        fprintf(stderr, "[StartNewGame] loops built game=%p dl=%p outer=%p; "
                "menuLoop=%p game=%p -> %s\n",
                (void*)pGameLoop, (void*)pDlLoop, (void*)pOuterLoop, (void*)menuLoop, (void*)game,
                menuLoop ? "SetDialogLoaderNextLoop(loop)" : "SetEngineNextLoop(engine)");
        fflush(stderr);
        if (menuLoop == nullptr) {
            game->SetEngineNextLoop(pOuterLoop);
        } else {
            menuLoop->SetLoaderNextLoop(pOuterLoop);
        }
        LoadTime::Stamp("newgame.queued");   // NOT RETAIL: print-only
        fprintf(stderr, "[StartNewGame] EXIT — next loop set, transition pending\n");
        fflush(stderr);
        return pOuterLoop;
    }
}

void NewGameDialog::OnElementEvent(Object::InternalPtr<UI::Element>* /*element*/, int32_t eventType, int32_t eventData, int32_t /*c*/, int32_t /*d*/, int32_t /*e*/, void* /*userData*/) {
    fprintf(stderr, "[NewGameDialog::OnEvent] this=%p type=0x%x data=%d\n",
            (void*)this, eventType, eventData);
    fflush(stderr);

    if (eventType == 0x12) {
        // Button click
        if (eventData == 2) {
            // buttonOk: start game
            StartGame();
            return;
        }
        if (eventData == 3) {
            // buttonCancel: close dialog
            Object::InternalPtr<UI::DialogInstance> basePtr(this);
            _ui->CloseDialog(&basePtr);
            return;
        }
        if (eventData == 0) {
            // buttonNewName: generate random name
            GenerateRandomName(editName, &random);
            return;
        }
        if (eventData == 1) {
            // buttonNewSeed: generate random seed
            GenerateRandomSeed(editSeed, &random);
            return;
        }
    } else if (eventType == 0x14 && eventData == 4) {
        // comboCondition selection changed: update description text
        UpdateDescription();
    }
}

// ============================================================
// OnClosed (vtable +0x28, exe 0x1400706c0): RET 0 (3 bytes)
// ============================================================
// No-op shutdown; nothing to clean up.
void NewGameDialog::OnClosed() {}

void NewGameDialog::OnUpdateLayout() {
    // The name edit box's text length (0 when empty).
    int len = editName->GetText().GetLength();

    // Element slot 8: Enable(bool)
    buttonOk->Enable(len > 0 ? 1 : 0);
}


// ============================================================
// NewGameDialog construction and reflection
// ============================================================
// Vtable layout (exe @ 0x14015c920, 10 slots — verified via read_memory):
//   +0x00 Slot0 Dtor             — 0x140012cc0 (scalar-deleting dtor thunk)
//   +0x08 Slot1 TypeName         — 0x140012c60 (const-load)
//   +0x10 Slot2 TypeId           — 0x140012c50 (const-load)
//   +0x18 Slot3 Persist          — 0x14003a948 (runtime thunk)
//   +0x20 Slot4 OnOpened         — 0x1400706d0
//   +0x28 Slot5 OnClosed         — 0x1400706c0
//   +0x30 Slot6 OnUpdateLayout   — 0x140070650 (OK-btn sync)
//   +0x38 Slot7 OnUpdate         — 0x14003a990 (runtime thunk → DialogInstance::OnUpdate)
//   +0x40 Slot8 OnElementEvent   — 0x140070110
//   +0x48 Slot9 OnSerializeSettings — 0x14003a954 (runtime thunk)
//
// The real ctor is exe 0x140071040 (Ghidra mislabels it "NewGameDialog_CopyCtor";
// it is the 5-arg ctor MainMenuDialog::OpenNewGame calls). 0x140071120 is the Dtor.

#include "Object/factory_registration.h"

// The real ctor — retail 0x140071040 ("NewGameDialog_CopyCtor" in Ghidra):
//   DialogInstance::DialogInstance(self, ui, extRes, &nullParent, 0,0,0)
//   zero +0xB8..+0x110 (the 12 element-pointer fields)
//   System::Random::Random(self+0x118)
//   mainMenu(+0xAE0) = *parentMenuIBP   (the MainMenuDialog, used by StartGame)
NewGameDialog::NewGameDialog(UI::UserInterface* ui, const UI::Dialog* dialog,
                             Object::InternalPtr<UI::DialogInstance> parent,
                             Object::InternalPtr<MainMenuDialog> parentMenu)
    : UI::DialogInstance(ui, dialog, parent, nullptr, nullptr, 0),
      comboTerrain(nullptr), comboTerrainSize(nullptr), comboClimate(nullptr),
      comboCondition(nullptr), comboDisaster(nullptr), editName(nullptr),
      editSeed(nullptr), buttonOk(nullptr), textDescription(nullptr),
      mapGeneratorList(nullptr), climateGeneratorList(nullptr),
      terrainGeneratorList(nullptr) {
    // The System::Random member at +0x118 was built by its real ctor
    // ??0Random@System@@QEAA@XZ (0x1800c7f20) in the member initialisers, after the
    // twelve element pointers and before mainMenu: retail's position (the
    // reflected ctor 0x140071120 calls it at 0x1400711a9 the same way).
    mainMenu = parentMenu;
}

// The reflected create path (create 0x140012c70). The tree's factory handed out
// a zero-filled object with no base ctor; this keeps the zero fields and runs
// the default DialogInstance ctor.
NewGameDialog::NewGameDialog()
    : comboTerrain(nullptr), comboTerrainSize(nullptr), comboClimate(nullptr),
      comboCondition(nullptr), comboDisaster(nullptr), editName(nullptr),
      editSeed(nullptr), buttonOk(nullptr), textDescription(nullptr),
      mapGeneratorList(nullptr), climateGeneratorList(nullptr),
      terrainGeneratorList(nullptr), random{}, mainMenu() {}

// create 0x140012c70: MemAlloc(0xae8, 8), then the ctor.
::Object::Base* NewGameDialog::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(NewGameDialog), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(::new (p) NewGameDialog());
}
// createInPlace 0x140012cb0 (the tree's in-place build is kept, B8-13).
::Object::Base* NewGameDialog::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(::new (storage) NewGameDialog());
}

DEFINE_EXE_TYPE(NewGameDialog, "UI::DialogInstance")
