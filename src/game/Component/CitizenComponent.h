#pragma once
// ============================================================================
// src/game/Component/CitizenComponent.h — ENDGAME B.3.8 block CIT
//
// `CitizenComponent` (a citizen's full state: health, happiness, inventory,
// housing, work, the age/clothing mesh) as a real C++ class with a
// compiler-generated vtable (B38_RULES.md D1-D4). The engine/types POD is a
// forwarder to this file.
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §6):
//   reflected "CitizenComponent", parent "Component", ti 0x1401c7158
//   create 0x140030110: MemAlloc(0x290, 8) + ctor 0x140114770
//   ctor   0x140114770: Component ctor, stamp 0x140162580, scalars (+0x30 = -1,
//                       +0xa8 = -1, +0x1d8 = -1 (qword), +0x1e0 = 4, the rest 0),
//                       Graphics::MeshInstance::MeshInstance() at +0xb8,
//                       flags |= 0xcb
//   dtor   0x1401146f0: restamp, ~MeshInstance(+0xb8) (import 0x1401514a8),
//                       MemFree(+0x98) + zero +0x98/+0xa0, System::String::~String
//                       at +0x28 (import 0x140152af0), stamp 0x140162920,
//                       ~Internal; scalar 0x140030270 = dtor + MemFree on &1
//   table  0x140162580, 16 slots:
//     [0] 0x140030270 scalar deleting dtor   [1]/[2] TypeName/Type
//     [3] 0x140114160 Persist                [4] 0x140114050 Startup
//     [6] 0x140114000 OnAdded (town list)    [7] 0x140113ca0 OnRemove
//     [9] 0x140112e70 Update                 [11] 0x1401140f0 Slot11 (post-load)
//     [15] 0x140112260 OnMessage (NeedsUpdate; EDX msg, R8 pointer, R9D int)
//     every other slot = Component's
//
// THE +0xB8 MeshInstance: retail constructs and destroys a real
// Graphics::MeshInstance there (ctor CALL [0x140151490] at 0x140114808, dtor
// CALL [0x1401514a8] at 0x140114717). It is a real member (B4 Y CW): built by
// the ctor between +0xAC and +0x1D8, destroyed first of the three non-trivial
// members, exactly as retail (known_port_issues row 650, FIXED by row 720).
//
// FIELDS (ENDGAME B.4 W0-H1): every field from retail's ctor 0x140114770, dtor
// 0x1401146f0 and serializer 0x140114160 (its import slots: 0x140152ac8 int,
// 0x140152a60 float, 0x140152a50 uint, 0x140152a58 bool, 0x140152aa8 Time,
// 0x140152aa0 InternalBasePtr + an IsDerivedFrom check against the cell named
// on each field, 0x140151238 String). The serializer passes no names; names come
// from the profile build's assertions (`_parent`, `_prevSibling`,
// `_nextSibling`) and from what retail does with the field (Update 0x140112e70,
// the happiness update 0x140112ae0, PostLifeEvent, the two assign bodies).
// A field with neither keeps a typed guess name `field_0xNN`.
//
// H5 (the rule sheet): +0x28 (System::String), +0x98 (Collection::Array) and
// +0xB8 (Graphics::MeshInstance) are real members: the defaulted dtor runs
// ~MeshInstance(+0xB8), ~Array(+0x98: MemFree + zero) and ~String(+0x28) in
// that order, which is retail's dtor 0x1401146f0. System::Time fields are `System::Time`
// (src/engine/system/Value.h: one int64 tick count, no vtable, trivially
// constructible and destructible, so declaring it runs no code; B4 P2 CIT-4),
// except +0x60 / +0x70 / +0x78 / +0x1E8, still `int64_t` ticks: member bodies in
// citizen_updatehealth.cpp (CIT-2) and citizen_needsupdate.cpp (CIT-1) do integer
// arithmetic on them (LEFT for those blocks / wave X).
// InternalBasePtr / ExternalPtr members are declared as the bare pointer they
// hold (`T*`, retail's layout: one qword; the tree's precedent is
// ComponentEntity::_description).
//
// CORRECTED (B.4 W0-H1, retail): +0x238 is the HOME, not the workplace: the
// status icon 0 ("imageStatusHome") is raised on `+0x238 == 0`, the happiness
// update scores the home tile through it, and 0x1401163d0 (Ghidra and the tree:
// "AssignWorkplace") writes +0x238 / +0x240 and walks the children ring with
// them (children follow their parents home), finding the ResidenceComponent
// (cell 0x1401c7cf0) through 0x140008090. +0x240 is the household slot in that
// residence (ResidenceComponent_IsHouseholdFull 0x14012a4f0(res, +0x240)).
// +0x248 is the WORKPLACE: 0x140111ee0 (Ghidra and the tree: "AssignHousing")
// replaces it and adds / removes the citizen through 0x140002e10 (the
// WorkPlaceComponent finder) and 0x14012fa20 / 0x14012f7e0; Update raises the
// "no job" icon 3 on `profession+0x4C == 0 && +0x248 == 0`.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Component/Component.h"
#include "Object/exe_type.h"
#include "system/Value.h"   // System::Time (8 bytes, trivial)
#include "collection/Collection.h"   // Collection::Array (+0x98)
#include "graphics/Mesh.h"           // Graphics::MeshInstance (+0xB8, embedded)

class Disease;
class Profession;
class AttendanceComponent;
class ComponentEntity;
class CharacterComponent;
class StatusIconComponent;
class CitizenDescription;
class WorkPlaceComponent;
class ComponentDescription;
namespace Math { class Vector3; }

class CitizenComponent final : public Component {
    DECLARE_EXE_TYPE(CitizenComponent)

public:
    CitizenComponent();                                           // 0x140114770

    // 0x1401146f0: ~MeshInstance(+0xB8) 0x140114717, MemFree(+0x98) + zero
    // +0x98 / +0xA0, ~String(+0x28) 0x14011473f: the members' own destructors,
    // in reverse declaration order.
    ~CitizenComponent() override = default;                       // [0]  0x140030270
    void Persist(IO::Stream& stream) override;                          // [3]  0x140114160
    void Startup() override;                                      // [4]  0x140114050
    void OnAdded() override;                                      // [6]  0x140114000
    void OnRemove() override;                                     // [7]  0x140113ca0
    void Update(const System::Time* time) override;                             // [9]  0x140112e70
    void Slot11() override;                                       // [11] 0x1401140f0
    void OnMessage(int message, void* data,
                   uint32_t arg0, uint32_t arg1) override;        // [15] 0x140112260

    // ---- non-virtual members (B.4 W1 COMP-b). Retail has no symbols for them:
    // each is a member because its body reads this class's fields through RCX
    // (B4_RULES B1); the names say what retail does (correction 3: 0x1401163d0
    // sets the HOME, 0x140111ee0 the WORKPLACE). A retail InternalPtr returned
    // through the RDX slot is returned as the typed pointer it holds (the
    // FindComponent<T> precedent, B10); an InternalPtr passed by address is
    // passed as the pointer it holds (every caller hands over a local copy).
    bool  ShouldSwitchWorkplace();                                // 0x1401109e0
    CitizenComponent* FindSpouse();                               // 0x14010fe40
    void  SetParent(CitizenComponent* parent);                    // 0x140111fa0
    void  RemoveChild(CitizenComponent* child);                   // 0x140112040
    void  AddChild(CitizenComponent* child);                      // 0x1401168a0
    ComponentEntity* FindWorkplace();                             // 0x14010ffc0 (profession not ranked)
    ComponentEntity* FindRankedWorkplace();                       // 0x140110470 (profession field4C set)
    ComponentEntity* FindNewWorkplace();                          // 0x14010ff80
    char  CanFindHousing();                                       // 0x140115be0
    void  GiveBirth();                                            // 0x140111b90
    void  EatFrom(ComponentEntity* target);                       // 0x1401152e0 (tree: "ReleaseWorkplace")
    ComponentEntity* FindHome(int32_t* slot, bool anyHousehold);  // 0x140110e70
    char  CanReach(ComponentEntity* target);                      // 0x1401158d0 (tree: "CheckWorkplaceValid")
    void  UpdateHealth();                                         // 0x140112990 (tree: "ConsumeFood")
    void  UpdateHappiness();                                      // 0x140112ae0 (tree: "ConsumeFuel")
    void  SetHome(ComponentEntity* home, int32_t slot);           // 0x1401163d0 (tree: "AssignWorkplace")
    void  SetWorkplace(ComponentEntity* workplace);               // 0x140111ee0 (tree: "AssignHousing")
    AttendanceComponent* FindAttendance(AttendanceComponent* exclude,
                                        Profession* key);         // 0x140111930
    char  FindCareBuilding();                                     // 0x140111590
    void  FindStorage();                                          // 0x1401116f0
    char  TryEnrolInSchool(AttendanceComponent* exclude);         // 0x140111840
    void  PostLifeEvent(int eventType, int severity,
                        const char* text);                        // 0x140113af0
    uint64_t HandleLifeEvent(int eventType, const char* text);    // 0x140116dd0
    void  OrFamilyFlags(uint32_t flags);                          // 0x140113a70
    char  UpdateDisease(const System::Time* dt);                  // 0x1401127d0 (tree: "UpdateHealth")
    char  RecoverOrDie();                                         // 0x1401123b0
    void  SpreadDisease();                                        // 0x140112470
    void  CatchDisease();                                         // 0x140112620
    void  Infect(Disease* disease);                               // 0x140116b90
    void  AddHappiness(int amount);                               // 0x14010fde0 (message 1)
    void  AdvanceSchooling();                                     // 0x1401117a0 (message 4)
    void  ShortenDisease();                                       // 0x140111540 (message 5)
    void  WarmAt(ComponentEntity* target);                        // 0x1401121e0 (message 0)
    void  Initialize(const Math::Vector3* pos, uint32_t gender, char isElder, float age,
                     char forceEmploy, CitizenComponent* parent,
                     Profession* profession);                     // 0x140117080
    void  SetupYoung();                                           // 0x140115110
    void  SetupAdult();                                           // 0x1401151a0
    void  SetupElder();                                           // 0x140115230
    int   HasSufficientResources();                               // 0x1401156e0 (home food per household)
    int   HasSufficientResources2();                              // 0x1401157f0 (home fuel per household)
    int   CarryCount();                                           // 0x140115de0 (Ghidra: "GetHappiness")
    void  LongTermPlan();                                         // 0x140115e60 (AI state 0xd)
    void  HideTool();                                             // 0x1401176e0 (action_base_complete.cpp)
    void  ShowTool(int toolId);                                   // 0x1401173b0 (action_base_complete.cpp)
    uint8_t BreakCheck();                                         // 0x140116650 (break / task due; action_base_move.cpp)
    void  LeaveSchool(char reroll);                               // 0x140115a50 (+0x208; attendancecomponent.cpp)
    void  ReleaseLink230();                                       // 0x140111650 (+0x230; attendancecomponent.cpp)
    void  ReleaseCareBuilding();                                  // 0x1401159b0 (+0x228; attendancecomponent.cpp)
    int   FreeStorageCapacity() const;                            // 0x140116540 (work_leaf_gather.cpp)
    void  StorageDropOff(ComponentDescription* material, int count);   // 0x140116760 (action_pf_ctors.cpp)
    int   StorageCountDelta(const ComponentDescription* material);     // 0x1401167c0 (action_pf_ctors.cpp)

    // PUBLIC while the ported bodies read them (B38 decision 1).
    System::String name;           // +0x28 the citizen's name (ctor 0x140114770 stores its null
                                   //       data inline; dtor 0x1401146f0 runs ~String at 0x14011473f);
                                   //       serialized first (0x140151238), handed to the event log
                                   //       by PostLifeEvent (EventLog_AddCitizenEvent's name
                                   //       argument), ~String in the dtor
    int32_t  field_0x30;           // +0x30 int, ctor -1; serialized after +0xB0
    int32_t  field_0x34;           // +0x34 int, ctor 0; serialized last
    int32_t  gender;               // +0x38 int; 0 = female: Update's childbearing block runs only
                                   //       on `+0x38 == 0` (0x140113873)
    float    age;                  // +0x3C += dt * desc->agingRate; >= desc->adultAge = adult
    float    maxAge;               // +0x40 `maxAge <= age` -> HandleLifeEvent(2) (old age)
    float    hunger;               // +0x44 += dt * desc->hungerRate; capped at desc->maxHunger
    float    coldness;             // +0x48 += dt * desc->coldRate[clothingLevel]; capped at desc->maxColdness
    float    pregnancyTimer;       // +0x4C > 0 counts down to a birth, < 0 counts up (the wait after one)
    float    health;               // +0x50 birth-risk ratio health / desc->maxHealth
    uint32_t statusFlags;          // +0x54 |= 1 hungry, |= 2 cold (Update)
    System::Time foodTimer;        // +0x58 += dt; each desc->foodInterval -> ConsumeFood
    System::Time field_0x60;       // +0x60 System::Time: += dt (Update)
    Disease* disease;              // +0x68 ExternalPtr<Disease> (serialize helper 0x140030790)
    System::Time field_0x70;       // +0x70 System::Time, serialized
    System::Time field_0x78;       // +0x78 System::Time, serialized
    float    happiness;            // +0x80 the happiness update 0x140112ae0, clamped to [0, desc->maxHappiness]
    int32_t  field_0x84;           // +0x84 int, NOT written by the ctor, not serialized; the happiness
                                   //       update compares it with desc+0xB0
    uint32_t happinessFlags;       // +0x88 the bits the happiness update 0x140112ae0 scores
    uint8_t  _pad8C[4];            // +0x8C
    System::Time happinessTimer;   // +0x90 += dt; each desc+0xA0 -> the happiness update
    Collection::Array<ComponentDescription*> consumedTypes;
                                   // +0x98 Array<ExternalPtr<ComponentDescription>> (the tree's
                                   //       bare-pointer convention for ExternalPtr): ctor zeroes
                                   //       +0x98 / +0xA0, dtor MemFrees it and zeroes them;
                                   //       serialized by 0x1400049c0 (validated ComponentDescription);
                                   //       EatFrom 0x1401152e0 appends the eaten material's description;
                                   //       the happiness update compares its count (+0xA0) with desc+0xA8/+0xAC
    int32_t  field_0xA8;          // +0xA8 int, ctor -1; serialized (version > 0)
    int32_t  field_0xAC;           // +0xAC int, ctor 0; serialized
    uint32_t field_0xB0;           // +0xB0 uint (Encoding slot 0x98), ctor 0
    uint8_t  _padB4[4];            // +0xB4

    Graphics::MeshInstance meshInstance; // +0xB8 the carried tool / item mesh (ctor 0x140114808,
                                         //       dtor 0x140114717; ShowTool 0x1401173b0 SetMesh's it)

    int32_t  toolId;               // +0x1D8 int, ctor -1 (one qword store with +0x1DC); serialized
    int32_t  toolSceneId;          // +0x1DC int, ctor -1; NOT serialized; HideTool 0x1401176e0 guards >= 0
    int32_t  field_0x1E0;          // +0x1E0 int, ctor 4 (one qword store with +0x1E4); serialized (version > 1)
    int32_t  clothingLevel;        // +0x1E4 indexes desc->coldRate[]; reset to 0 when clothingTimer < 0
    System::Time clothingTimer;    // +0x1E8 System::Time: -= dt; < 0 -> clothingLevel = 0
    Profession* profession;        // +0x1F0 ExternalPtr<Profession> (0x140018020); Update reads its +0x4C
    Profession* pendingProfession; // +0x1F8 ExternalPtr<Profession>; != 0 -> HandleLifeEvent(7, its name)
    System::Time field_0x200;      // +0x200 += dt (Update), not serialized
    Object::InternalPtr<AttendanceComponent> school;   // +0x208 InternalPtr (cell 0x1401c3f40); != 0: schoolTime += dt
    int32_t  education;            // +0x210 raised to desc->maxEducation (Update)
    uint8_t  _pad214[4];           // +0x214
    System::Time schoolTime;       // +0x218 += dt while school != 0 (version > 2)
    bool     field_0x220;          // +0x220 bool; 0 at adulthood -> TryEnrolInSchool 0x140111840
    uint8_t  _pad221[7];           // +0x221
    Object::InternalPtr<AttendanceComponent> careBuilding; // +0x228 InternalPtr (cell 0x1401c3f40); FindCareBuilding 0x140111590
    Object::InternalPtr<AttendanceComponent> field_0x230;  // +0x230 InternalPtr (cell 0x1401c3f40); found through desc+0x28
                                       //        by 0x1401116f0; != 0 scores happiness
    Object::InternalPtr<ComponentEntity> home;         // +0x238 InternalPtr<ComponentEntity> (cell 0x1401c5d70): the residence
    int32_t  homeSlot;             // +0x240 int, NOT written by the ctor; the household slot in the residence
    uint8_t  _pad244[4];           // +0x244 never read by retail; fix.stranded_wade keeps the lost home's
                                   //        cell + 1 here (NOT RETAIL, src/ext/stranded_wade/; never saved)
    Object::InternalPtr<ComponentEntity> workplace;    // +0x248 InternalPtr<ComponentEntity> (cell 0x1401c5d70)
    Object::InternalPtr<CitizenComponent> spouse;      // +0x250 InternalPtr (cell 0x1401c7158); set pairwise by the marriage scan
    Object::InternalPtr<CitizenComponent> _parent;     // +0x258 InternalPtr (profile assert `child->_parent.IsPtrNull()`)
    Object::InternalPtr<CitizenComponent> _firstChild; // +0x260 InternalPtr: head of the children ring (RemoveChild 0x140112040)
    Object::InternalPtr<CitizenComponent> _nextSibling; // +0x268 InternalPtr (profile assert)
    Object::InternalPtr<CitizenComponent> _prevSibling; // +0x270 InternalPtr (profile assert)
    Object::InternalPtr<CharacterComponent> character; // +0x278 InternalPtr (cell 0x1401c7078); Update reads its +0x60
    Object::InternalPtr<StatusIconComponent> statusIcon; // +0x280 InternalPtr (cell 0x1401c7e08); SetFlag 0x14012c3f0
    CitizenDescription* description; // +0x288 ExternalPtr<CitizenDescription> (0x140030990)
};

static_assert(sizeof(CitizenComponent) == 0x290, "CitizenComponent is 0x290 (create 0x140030110)");
static_assert(alignof(CitizenComponent) == 8, "CitizenComponent alignment");
static_assert(offsetof(CitizenComponent, name)                 == 0x28,  "");
static_assert(offsetof(CitizenComponent, field_0x30)           == 0x30,  "");
static_assert(offsetof(CitizenComponent, field_0x34)           == 0x34,  "");
static_assert(offsetof(CitizenComponent, gender)               == 0x38,  "");
static_assert(offsetof(CitizenComponent, age)                  == 0x3C,  "");
static_assert(offsetof(CitizenComponent, maxAge)               == 0x40,  "");
static_assert(offsetof(CitizenComponent, hunger)               == 0x44,  "");
static_assert(offsetof(CitizenComponent, coldness)             == 0x48,  "");
static_assert(offsetof(CitizenComponent, pregnancyTimer)       == 0x4C,  "");
static_assert(offsetof(CitizenComponent, health)               == 0x50,  "");
static_assert(offsetof(CitizenComponent, statusFlags)          == 0x54,  "");
static_assert(offsetof(CitizenComponent, foodTimer)            == 0x58,  "");
static_assert(offsetof(CitizenComponent, field_0x60)           == 0x60,  "");
static_assert(offsetof(CitizenComponent, disease)              == 0x68,  "");
static_assert(offsetof(CitizenComponent, field_0x70)           == 0x70,  "");
static_assert(offsetof(CitizenComponent, field_0x78)           == 0x78,  "");
static_assert(offsetof(CitizenComponent, happiness)            == 0x80,  "");
static_assert(offsetof(CitizenComponent, field_0x84)           == 0x84,  "");
static_assert(offsetof(CitizenComponent, happinessFlags)       == 0x88,  "");
static_assert(offsetof(CitizenComponent, _pad8C)               == 0x8C,  "");
static_assert(offsetof(CitizenComponent, happinessTimer)       == 0x90,  "");
static_assert(offsetof(CitizenComponent, consumedTypes)        == 0x98,  "");
static_assert(sizeof(Collection::Array<ComponentDescription*>) == 0x10, "+0x98 Array is {data, size, capacity}");
static_assert(sizeof(Graphics::MeshInstance) == 0x120, "+0xB8 MeshInstance ends at +0x1D8");
static_assert(offsetof(CitizenComponent, field_0xA8)           == 0xA8,  "");
static_assert(offsetof(CitizenComponent, field_0xAC)           == 0xAC,  "");
static_assert(offsetof(CitizenComponent, field_0xB0)           == 0xB0,  "");
static_assert(offsetof(CitizenComponent, _padB4)               == 0xB4,  "");
static_assert(offsetof(CitizenComponent, meshInstance)         == 0xB8,  "");
static_assert(offsetof(CitizenComponent, toolId)               == 0x1D8, "");
static_assert(offsetof(CitizenComponent, toolSceneId)          == 0x1DC, "");
static_assert(offsetof(CitizenComponent, field_0x1E0)          == 0x1E0, "");
static_assert(offsetof(CitizenComponent, clothingLevel)        == 0x1E4, "");
static_assert(offsetof(CitizenComponent, clothingTimer)        == 0x1E8, "");
static_assert(offsetof(CitizenComponent, profession)           == 0x1F0, "");
static_assert(offsetof(CitizenComponent, pendingProfession)    == 0x1F8, "");
static_assert(offsetof(CitizenComponent, field_0x200)          == 0x200, "");
static_assert(offsetof(CitizenComponent, school)               == 0x208, "");
static_assert(offsetof(CitizenComponent, education)            == 0x210, "");
static_assert(offsetof(CitizenComponent, _pad214)              == 0x214, "");
static_assert(offsetof(CitizenComponent, schoolTime)           == 0x218, "");
static_assert(offsetof(CitizenComponent, field_0x220)          == 0x220, "");
static_assert(offsetof(CitizenComponent, _pad221)              == 0x221, "");
static_assert(offsetof(CitizenComponent, careBuilding)         == 0x228, "");
static_assert(offsetof(CitizenComponent, field_0x230)          == 0x230, "");
static_assert(offsetof(CitizenComponent, home)                 == 0x238, "");
static_assert(offsetof(CitizenComponent, homeSlot)             == 0x240, "");
static_assert(offsetof(CitizenComponent, _pad244)              == 0x244, "");
static_assert(offsetof(CitizenComponent, workplace)            == 0x248, "");
static_assert(offsetof(CitizenComponent, spouse)               == 0x250, "");
static_assert(offsetof(CitizenComponent, _parent)              == 0x258, "");
static_assert(offsetof(CitizenComponent, _firstChild)          == 0x260, "");
static_assert(offsetof(CitizenComponent, _nextSibling)         == 0x268, "");
static_assert(offsetof(CitizenComponent, _prevSibling)         == 0x270, "");
static_assert(offsetof(CitizenComponent, character)            == 0x278, "");
static_assert(offsetof(CitizenComponent, statusIcon)           == 0x280, "");
static_assert(offsetof(CitizenComponent, description)          == 0x288, "");
