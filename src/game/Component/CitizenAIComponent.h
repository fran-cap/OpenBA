#pragma once
// ============================================================================
// src/game/Component/CitizenAIComponent.h — ENDGAME B.3.8 block CIT
//
// `CitizenAIComponent` (a citizen's decision state machine) as a real C++ class
// with a compiler-generated vtable (B38_RULES.md D1-D4). The engine/types POD
// is a forwarder to this file.
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §6):
//   reflected "CitizenAIComponent", parent "Component", ti 0x1401c70e8
//   create 0x14002fb20: MemAlloc(0xd0, 8) + ctor 0x14010fb70
//   ctor   0x14010fb70: Component ctor, stamp 0x140162470, +0x28 = 0xc,
//                       +0x2c = -1, +0x30 (word) = 0, +0x38 = 0, sixteen
//                       System::Time() at +0x40..+0xb8 (import 0x140152af8),
//                       +0xc0 = +0xc8 = 0, flags |= 0x89
//   dtor   0x14002fcf0 (scalar, 83 B): restamp, ~Internal, MemFree on &1
//   table  0x140162470, 16 slots:
//     [0] 0x14002fcf0 scalar deleting dtor   [1]/[2] TypeName/Type
//     [3] 0x14010f960 Persist                [4] 0x14010f920 Startup
//     [6] 0x14010f880 OnAdded (JMP 0x14010f890 CheckAge)
//     [9] 0x14010f3e0 Update                 [15] 0x14010f340 OnMessage
//     every other slot = Component's
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Component/Component.h"
#include "system/Value.h"            // System::Time (leisureTimer)
#include "Object/exe_type.h"

class CitizenComponent;
namespace System { class Time; }

#include "Component/StateCandidate.h"   // the 12-byte candidate record (CitizenAI_SortByPriority sorts it)

struct StateCandidateList {
    StateCandidate* data;   // +0x00
    int32_t count;          // +0x08
    int32_t capacity;       // +0x0C
};
static_assert(sizeof(StateCandidateList) == 0x10, "StateCandidateList is {ptr, int, int}");
static_assert(offsetof(StateCandidateList, data) == 0x0, "");
static_assert(offsetof(StateCandidateList, count) == 0x8, "");
static_assert(offsetof(StateCandidateList, capacity) == 0xc, "");

class CitizenAIComponent final : public Component {
    DECLARE_EXE_TYPE(CitizenAIComponent)

public:
    CitizenAIComponent();                                         // 0x14010fb70

    // no own work
    ~CitizenAIComponent() override = default;                     // [0]  0x14002fcf0
    void Persist(IO::Stream& stream) override;                          // [3]  0x14010f960
    void Startup() override;                                      // [4]  0x14010f920
    void OnAdded() override;                                      // [6]  0x14010f880 (JMP CheckAge)
    void Update(const System::Time* time) override;                             // [9]  0x14010f3e0
    void OnMessage(int message, void* data,
                   uint32_t arg0, uint32_t arg1) override;        // [15] 0x14010f340

    // B.4 W1 COMP-b: the bodies whose receiver (RCX) is the CitizenAIComponent.
    void     Decide(const System::Time* time);                    // 0x14010f3e0 (the body Update runs)
    void     TransitionState(char needsOnly);                     // 0x14010db90
    void     CheckEducation();                                    // 0x14010f7a0
    void     CheckAge();                                          // 0x14010f890
    void     BuildCandidateStateList(StateCandidateList* outList, char needsOnly); // 0x14010dca0
    int      ValidateAndEnterState(int state);                    // 0x14010f1f0
    uint64_t CanReachHome();                                      // 0x14010db20 (tree: "helper_db20")
    // The state handlers ValidateAndEnterState dispatches (Ghidra's names; several
    // are misnomers, see the comments at the switch).
    bool     State_Wander();                                      // 0x14010a2b0  state 0xb
    uint64_t State_Leisure();                                     // 0x14010a560  state 0xc
    uint64_t State_Idle();                                        // 0x14010b2d0  state 9
    uint64_t State_FindHousing();                                 // 0x14010b340  state 2
    uint64_t State_GatherResources2();                            // 0x14010b3e0  state 7
    uint64_t State_GatherResources();                             // 0x14010bc90  state 6
    uint64_t State_Education();                                   // 0x14010c360  state 3
    uint64_t State_SeekHappiness();                               // 0x14010c580  state 8
    uint64_t State_GrowUp();                                      // 0x14010c890  state 4
    uint64_t State_AdultTransition();                             // 0x14010cd80  states 0, 0xe
    uint64_t State_ElderTransition();                             // 0x14010d2c0  states 1, 0xf
    uint64_t State_FindWork();                                    // 0x14010d560  state 10
    uint64_t State_Work();                                        // 0x14010d790  state 5

    // PUBLIC while the ported bodies read them (B38 decision 1).
    // Fields (B.4 W1 COMP-b) from the ctor 0x14010fb70 and Persist 0x14010f960.
    int32_t  state;              // +0x28 int (serialized, version > 1), ctor 0xc: the state
                                 //       TransitionState entered
    int32_t  queuedState;        // +0x2C int (serialized first), ctor -1: tried before the
                                 //       candidate list, then reset to -1
    bool     seekingFood;        // +0x30 bool (version > 0), ctor 0 (one word store with +0x31):
                                 //       Decide sets it when the hunger transition enters state 0xe
    bool     seekingWarmth;      // +0x31 bool (version > 0): the same for coldness and state 0xf
    uint8_t  _pad32[6];          // +0x32
    Object::InternalPtr<CitizenComponent> citizen;   // +0x38 InternalPtr, validated against cell 0x1401c7158 on load;
                                 //       Startup 0x14010f920 finds the sibling CitizenComponent
    System::Time timers[16];     // +0x40 System::Time per state: ValidateAndEnterState enters a
                                 //       state only while its timer is < 1; Decide counts them down
                                 //       (version < 5 streams carry 14, +0xB0/+0xB8 then = Time(0))
    System::Time leisureTimer;   // +0xC0 System::Time (version > 2): += dt (Decide); the leisure
                                 //       candidate's priority reads its seconds
    System::Time dataC8;         // +0xC8 System::Time (version > 3): += dt (Decide), reset to 0
                                 //       while the citizen carries nothing; the LongTermPlan
                                 //       candidate's priority compares it
};

// The SendComponentMessage actions the work / AI action ctors build (retail: 26 readers
// of the type cell [0x1401c70e8], e.g. PerformWork_Action_CopyCtor 0x1400baa7e,
// Pathfinding_FindPath 0x1400bf81e, RemoveAnimal_Action_Ctor 0x1400bbf30) target this
// class; SendComponentMessage::AbstractImpl finds it on the citizen's entity and calls
// its slot-15 OnMessage 0x14010f340 (an immediate AI re-decision). The arms read
// CitizenAIComponent::TypeId(), the class's registered id, as retail reads the cell.

static_assert(sizeof(CitizenAIComponent) == 0xD0, "CitizenAIComponent is 0xd0 (create 0x14002fb20)");
static_assert(alignof(CitizenAIComponent) == 8, "CitizenAIComponent alignment");
static_assert(offsetof(CitizenAIComponent, state)         == 0x28, "");
static_assert(offsetof(CitizenAIComponent, queuedState)   == 0x2C, "");
static_assert(offsetof(CitizenAIComponent, seekingFood)   == 0x30, "");
static_assert(offsetof(CitizenAIComponent, seekingWarmth) == 0x31, "");
static_assert(offsetof(CitizenAIComponent, _pad32)        == 0x32, "");
static_assert(offsetof(CitizenAIComponent, citizen)       == 0x38, "");
static_assert(offsetof(CitizenAIComponent, timers)        == 0x40, "");
static_assert(offsetof(CitizenAIComponent, leisureTimer)  == 0xC0, "");
static_assert(offsetof(CitizenAIComponent, dataC8)        == 0xC8, "");
