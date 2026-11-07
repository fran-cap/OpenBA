#pragma once
// ============================================================================
// src/game/Component/DestroyComponent.h — ENDGAME B.3.8 block MI-A
//
// `DestroyComponent` as a real C++ class with a compiler-generated vtable and its
// state controller as an EMBEDDED MEMBER (B38_RULES.md D1-D6; there is no
// multiple inheritance). The engine/types POD is a forwarder to this file.
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §2 and §6, bytes read 2026-09-26):
//   reflected "DestroyComponent", parent "ControllerComponent", ti 0x1401c75f0
//   create 0x140033bc0: MemAlloc(0x68, 8) + ctor 0x14011dc80
//   createInPlace = the imported `Internal::CreateInPlaceObject` (returns null);
//     the tree's in-place entry builds the object and is kept (B38 B8-13).
//   ctor 0x14011dc80: Component ctor; member at +0x28; +0x48 = 0 (qword),
//     +0x50 = 0 (word), +0x54 = 0 (qword), +0x60 = 0. No flag OR (0x80).
//   dtor 0x14011dc20: restamp only, ~Internal (no own work).
//     [0] 0x140033dd0 = the dtor inlined + MemFree on &1.
//   table 0x140163120, 18 slots:
//     [0] 0x140033dd0 dtor  [1]/[2] TypeName/Type  [3] 0x14011db50 Persist
//     [4] 0x14011db00 Startup (copy the description's default target)
//     [15] 0x14011daf0 OnMessage (demolition-progress credit)
//     [16] 0x140033c00 `MOV EAX,4`  [17] 0x140004080 `LEA RAX,[RCX+0x28]`
//     every other slot = ControllerComponent's / Component's.
//   member table 0x1401630d0 (9 slots): the table-driven state controller over the
//     class's state table (ControllerLive::Dispatcher<DestroyCtrlTag>), stamped at
//     this+0x28 after the interface 0x14015f070; back-pointer at this+0x40.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Component/ControllerComponent.h"
#include "Component/controller_state_table.h"
#include "Object/exe_type.h"
#include "system/Value.h"   // System::Time

class ComponentDescription;   // Component/ComponentDescription.h

struct DestroyCtrlTag { using Owner = class DestroyComponent; };   // the handlers' receiver (controller_state_table.h)

class DestroyComponent final : public ControllerComponent {
    DECLARE_EXE_TYPE(DestroyComponent)

public:
    using StateController = ::ControllerLive::TableController<DestroyCtrlTag>;

    DestroyComponent();                                   // 0x14011dc80

    // 0x14011dc20
    ~DestroyComponent() override;                                 // [0]  0x140033dd0
    void          Persist(IO::Stream& stream) override;                 // [3]  0x14011db50
    void          Startup() override;                             // [4]  0x14011db00
    void          OnMessage(int message, void* data,
                            uint32_t arg0, uint32_t arg1) override;  // [15] 0x14011daf0
    int         GetDefaultState() const override { return SmId(BuildingState::HaulAway); }  // [16] 0x140033c00
    Controller* GetController() override { return &_controller; }  // [17] 0x140004080

    // ---- non-virtual members (retail addresses; RCX = this) ----
    bool StorageEmpty();                                          // 0x14011da80
    bool TargetDiffers();                                         // 0x14011e600
    bool TargetIsCurrent();                                       // 0x14011e5b0
    bool IsBeingDemolished();                                     // 0x14011e690
    bool BeginBurning(const System::Time* time);                  // 0x14011e7b0
    bool InRemovalState();                                        // 0x14011e710 (B4 PLAYER; body in Player/tool_base_path.cpp)
    void StopBurning();                                           // 0x14011ea10
    void ScatterContents(int baseX, int baseY, int width, int depth);  // 0x14011c530
    void RubbleStep();                                            // 0x14011d5d0
    void RunStateMachine();                                       // 0x14011dfa0 (B4 PLAYER; body in Player/keystone_a_removalcalcpath.cpp)
    void PostDestroyEvent();                                      // 0x14011e4d0 event 1 on the current node (B4 PLAYER; same file)
    // The state handlers of the controller's table (RCX = [node+0x18] = this,
    // RDX = the state machine, R8 = the elapsed Time for the per-frame column).
    char State3Enter(StateMachine* sm);                                   // 0x14011d7b0
    char State4Enter(StateMachine* sm);                                   // 0x14011cfd0
    char State4Tick(StateMachine* sm);                                    // 0x14011cec0
    char State5Enter();                                           // 0x14011cc70
    char State5Tick();                                            // 0x14011c120
    char State6Enter();                                           // 0x14011d4d0
    char State6Update();                                          // 0x14011d400
    char State6Tick(const System::Time* elapsed);                     // 0x14011d490
    char State7Rebuild(StateMachine* sm);                                 // 0x14011d2a0 state 7 event 0
    char State7Remove(StateMachine* sm);                                  // 0x14011d1a0 state 7 event 1
    char CancelDemolition(StateMachine* sm);                              // 0x14011bcc0 states 4/5 event 0

    // PUBLIC while the ported bodies read them at raw offsets (B38 decision 1).
    // Types from Persist 0x14011db50: +0x54 / +0x58 Serialize(int&) (IAT 0x140152ac8),
    // +0x51 / +0x50 Serialize(bool&) (0x140152a58), +0x60 ExternalPtr<ComponentDescription>
    // (0x140002340), +0x48 Time::Serialize (0x140152aa8).
    StateController _controller;   // +0x28 the embedded controller (D6)
    System::Time    burnTime;      // +0x48 the collapse countdown (BeginBurning 0x14011e7b0 seeds it;
                                   //       state 6's tick 0x14011d490 runs it down)
    bool            burning;       // +0x50 set by BeginBurning: state 3 enter branches to 6 (collapse)
    bool            hasBuildComponent; // +0x51 the entity carried a BuildComponent (state 4 enter)
    uint8_t         _pad52[2];     // +0x52
    int32_t         progress;      // +0x54 demolition progress (OnMessage credit; 0x14011c14f JL)
    int32_t         progressTarget;// +0x58 the target state 5 enter computes
    uint8_t         _pad5C[4];     // +0x5c never written by the ctor
    ComponentDescription* upgradeTarget; // +0x60 the replacement template (Startup: the
                                         //       DestroyDescription's _replaceEntity)
};

static_assert(sizeof(DestroyComponent) == 0x68, "DestroyComponent is 0x68 (create 0x140033bc0)");
static_assert(offsetof(DestroyComponent, _pad52) == 0x52, "DestroyComponent::_pad52");
static_assert(offsetof(DestroyComponent, _pad5C) == 0x5C, "DestroyComponent::_pad5C");
static_assert(alignof(DestroyComponent) == 8, "DestroyComponent alignment");
static_assert(offsetof(DestroyComponent, _controller) == 0x28,
              "DestroyComponent's controller member at +0x28 (slot 17 0x140004080: LEA RAX,[RCX+0x28])");
static_assert(offsetof(DestroyComponent, _controller) + offsetof(ControllerComponent::Controller, _owner) == 0x40,
              "controller back-pointer at +0x40 (ctor 0x14011dc80)");
static_assert(offsetof(DestroyComponent, burnTime) == 0x48, "DestroyComponent +0x48");
static_assert(offsetof(DestroyComponent, burning) == 0x50, "DestroyComponent +0x50");
static_assert(offsetof(DestroyComponent, hasBuildComponent) == 0x51, "DestroyComponent +0x51");
static_assert(offsetof(DestroyComponent, progress) == 0x54, "DestroyComponent +0x54");
static_assert(offsetof(DestroyComponent, progressTarget) == 0x58, "DestroyComponent +0x58");
static_assert(offsetof(DestroyComponent, upgradeTarget) == 0x60, "DestroyComponent +0x60");
