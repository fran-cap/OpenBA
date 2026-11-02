#pragma once
// ============================================================================
// src/game/Component/CharacterComponent.h — ENDGAME B.3.8 block CIT
//
// `CharacterComponent` (movement + the action stack of citizens and animals) as
// a real C++ class with a compiler-generated vtable (B38_RULES.md D1-D4). The
// engine/types POD is a forwarder to this file.
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §6):
//   reflected "CharacterComponent", parent "Component", ti 0x1401c7078
//   create 0x14002f960: MemAlloc(0x80, 8) + ctor 0x140109d20
//   ctor   0x140109d20: Component ctor, stamp 0x140162370, +0x28 (byte) = 0,
//                       +0x2c = +0x34 = 0, +0x40 = -1, action stack at +0x48
//                       {0, 0, 0, top +0x60 = -1}, +0x68 = +0x70 = +0x78 = 0,
//                       flags |= 0x8f
//   dtor   0x14002faa0 (scalar): restamp, 0x1400c5150(+0x48) (the action-stack
//                       release), stamp 0x140162920, ~Internal, MemFree on &1
//   table  0x140162370, 16 slots:
//     [0] 0x14002faa0 scalar deleting dtor   [1]/[2] TypeName/Type
//     [3] 0x140109ab0 Persist                [4] 0x140109a30 Startup
//     [6] 0x1401099c0 OnAdded (char index)   [7] 0x140109980 OnRemove
//     [8] 0x1401094c0 Tick (move-tick, full rate)   [9] 0x140109480 Update
//     every other slot = Component's
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Component/Component.h"
#include "Object/exe_type.h"
#include "math/Math.h"
#include "Action/Action.h"   // ActionStack (+0x48)

class Action;
class ModelComponent;
class PickingComponent;
class CharacterDescription;

class CharacterComponent final : public Component {
    DECLARE_EXE_TYPE(CharacterComponent)

public:
    CharacterComponent();                                         // 0x140109d20

    // [0] 0x14002faa0: restamp, 0x1400c5150(+0x48) (0x14002facb), Component's stamp,
    // ~Internal. The body is empty: ~actionStack (0x1400c5150) is the member's dtor, and
    // +0x68..+0x7F (trivial) release nothing, so the compiler's chain is retail's order.
    ~CharacterComponent() override = default;                     // [0] 0x14002faa0
    void Persist(IO::Stream& stream) override;                          // [3] 0x140109ab0
    void Startup() override;                                      // [4] 0x140109a30
    void OnAdded() override;                                      // [6] 0x1401099c0 char-index allocation
    void OnRemove() override;                                     // [7] 0x140109980
    void Tick(const System::Time* time) override;                               // [8] 0x1401094c0
    void Update(const System::Time* time) override;                             // [9] 0x140109480

    // ---- non-virtual members (retail addresses) ----
    // 0x14010a0e0: steer toward `target` under mover control (+0x28 = 1).
    void DriveToward(const Math::Vector3* target, float heading, float speed);
    // 0x14010a060: face along `dir` (x, y) -- +0x28 = 0, +0x2C the normalized direction,
    // +0x3C its heading (action_base_move.cpp).
    void SetFacingDirection(const Math::Vector3& dir);
    // 0x14010a1f0: play animation `animId` on the model (blend `p3`), +0x38 = the mesh
    // speed scale * `p4` (action_base_move.cpp).
    void SetAnimation(uint32_t animId, float p3, float p4);

    // PUBLIC while the ported bodies read them (B38 decision 1).
    // Fields (B.4 W0-H1) from ctor 0x140109d20, Persist 0x140109ab0 and the
    // action-list release 0x1400c5150 the dtor runs on +0x48. Persist's import
    // slots: 0x140152a68 Math::Vector3::Serialize, 0x140152a60 float,
    // 0x140152a58 bool, 0x140152ac8 int, 0x140152aa0 InternalBasePtr + an
    // IsDerivedFrom check against the cell named on the field.
    bool     field_0x28;          // +0x28 bool, ctor 0; serialized
    uint8_t  _pad29[3];           // +0x29
    Math::Vector3 field_0x2C;     // +0x2C serialized by 0x140152a68 (Vector3::Serialize); ctor 0
                                  //       (the ctor zeroes +0x2C..+0x3B with two qword stores)
    float    field_0x38;          // +0x38 float, ctor 0; serialized
    float    field_0x3C;          // +0x3C float, NOT written by the retail ctor (the tree zeroes
                                  //       it); serialized
    int32_t  characterIndex;      // +0x40 int, ctor -1; the index the character manager
                                  //       (GameUpdateLoop+0x88, 0x1400183f0) returns on load
    uint8_t  _pad44[4];           // +0x44 never read by retail; fix.stranded_wade keeps its flags in [0]
                                  //       and a zone in [2..3] (NOT RETAIL, src/ext/stranded_wade/; never saved)
    // +0x48..+0x67: one retail sub-object (the ctor takes its address for unwinding and
    // the dtor releases it through 0x1400c5150): the action stack (Action/Action.h).
    //   +0x00 actions  Collection::Array<InternalPtr<Action>> data (cell 0x1401c5da8; the
    //                  push 0x1400c54e0 stores 8-byte elements and hands the old top to
    //                  0x1400c4f10 as an InternalBasePtr); 0x1400c5150 DeletePtr's every
    //                  element, then MemFree
    //   +0x08 count    Array count (serialized)   +0x0C capacity
    //   +0x10 field_0x10  InternalPtr<Action>, DeletePtr'd by 0x1400c5150; the Work action
    //                  builders DeletePtr it (IAT 0x140151ab8) before storing the lead
    //                  SendComponentMessage (e.g. 0x1400bb89b MOV [RSI+0x10],RAX)
    //   +0x18 current  int, ctor -1: the index into `actions` Update runs (>= 0)
    ActionStack actionStack;      // +0x48
    Object::InternalPtr<ModelComponent>   modelComponent;   // +0x68 (cell 0x1401c7a50)
    Object::InternalPtr<PickingComponent> pickingComponent; // +0x70 (cell 0x1401c7c10)
    CharacterDescription* characterDescription; // +0x78 not serialized: _description when it is a
                                                //       CharacterDescription (cell 0x1401c70b0), on load
};

static_assert(sizeof(CharacterComponent) == 0x80, "CharacterComponent is 0x80 (create 0x14002f960)");
static_assert(alignof(CharacterComponent) == 8, "CharacterComponent alignment");
static_assert(offsetof(CharacterComponent, field_0x28)           == 0x28, "");
static_assert(offsetof(CharacterComponent, _pad29)               == 0x29, "");
static_assert(offsetof(CharacterComponent, field_0x2C)           == 0x2C, "");
static_assert(offsetof(CharacterComponent, field_0x38)           == 0x38, "");
static_assert(offsetof(CharacterComponent, field_0x3C)           == 0x3C, "");
static_assert(offsetof(CharacterComponent, characterIndex)       == 0x40, "");
static_assert(offsetof(CharacterComponent, _pad44)               == 0x44, "");
static_assert(offsetof(CharacterComponent, actionStack)          == 0x48, "");
static_assert(offsetof(CharacterComponent, actionStack.actions._size)     == 0x50, "");
static_assert(offsetof(CharacterComponent, actionStack.actions._capacity) == 0x54, "");
static_assert(offsetof(CharacterComponent, actionStack.field_0x10) == 0x58, "");
static_assert(offsetof(CharacterComponent, actionStack.current)  == 0x60, "");
static_assert(offsetof(CharacterComponent, modelComponent)       == 0x68, "");
static_assert(offsetof(CharacterComponent, pickingComponent)     == 0x70, "");
static_assert(offsetof(CharacterComponent, characterDescription) == 0x78, "");
