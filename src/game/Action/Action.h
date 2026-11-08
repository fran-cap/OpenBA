#pragma once
// ============================================================================
// src/game/Action/Action.h — ENDGAME B.3.8 block ACT (B38_RULES.md §2.1, D1-D5)
//
// `Action`, the exe's citizen action-stack node base, as a REAL C++ class whose
// vtable the compiler generates. Global namespace, as in retail (reflected name
// "Action", parent literal "Object::Internal"; the record is DEFINE_EXE_TYPE in
// action.cpp with retail's abstract factory, ENDGAME B.3.9 RF).
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §1/§6, bytes read 2026-09-26):
//   table 0x14015faa0, 10 slots, pure slot 4, create = 0x140003730 (abstract)
//   ctor (inlined into every leaf create/ctor, e.g. ShowTool create 0x1400013b0):
//        `??0Internal@Object@@` (IAT 0x140152768); stamp 0x14015faa0;
//        +0x10 = 0 (qword); +0x18 = 0 (byte)
//   [0] 0x140021c90 scalar deleting dtor: stamp 0x14015faa0;
//        `if ([this+0x10]) InternalBasePtr::DeletePtr(this+0x10)` (IAT
//        0x140151ab8); `~Internal` (IAT 0x140152770); `if (flags & 1) MemFree`
//   [1] 0x140021c80 TypeName   [2] 0x140021c70 Type
//   [3] 0x1400c4b40 Persist: Serializer(stream, 0); Serialize(bool +0x18);
//        ~Serializer. NO Internal::Persist call.
//   [4] 0x14003addc _purecall — the action's start/precheck (the executor
//        0x1400c4c50 calls it `[vt+0x20](node, stack)` and reads EAX)
//   [5] 0x1400c4a90 `XOR EAX,EAX; RET` — the per-tick main slot
//        (`[vt+0x28](node, stack, time)`, EAX read as int)
//   [6] 0x1400c4b90 `RET 0`   [7] 0x1400c4ba0 `RET 0` (the executor's exit,
//        `[vt+0x38](node, stack)`)   [8] 0x1400c4a30 `RET 0`
//   [9] 0x1400c4a40 `XOR AL,AL; RET`
//
// SLOT NAMES 4-9 are the tree's (the exe has no symbols; B38 B8-6). The argument
// list of 8 is the tree's C body's (B38_RULES §6 NOT VERIFIED); 4, 5 and 7 are
// the executor's (citizenai_executor.cpp:82-100, asm 0x1400c4ced / 0x1400c4d57 /
// 0x1400c4ddc); 6 is 0x1400c4f80's (B3X W0, below).
//
// LAYOUT: Object::Internal (0x10) + InternalBasePtr +0x10 + bool +0x18 = 0x20.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "object/Internal.h"
#include "Object/exe_type.h"   // DECLARE_EXE_TYPE (ENDGAME B.3.9 RF)
#include "collection/Collection.h"   // Collection::Array (ActionStack::actions)

class ComponentEntity;   // B4 P2 S2: slot 9's parameter
struct ActionStack;      // below: slots 4 and 5's parameter

class Action : public Object::Internal {
    DECLARE_EXE_TYPE(Action)

public:
    // ---- the 10 slots, in retail order -------------------------------------
    // The retail dtor's non-vptr work: release +0x10. `~Internal` is chained by
    // the compiler.
    ~Action() override { if (_next) _next.DeletePtr(); }         // [0] 0x140021c90
    // [1]/[2]: the abstract base answers its own registration by name (B38 D4,
    // as the pilot's Component); every leaf overrides both.
    // [1] TypeName 0x140021c80 and [2] Type 0x140021c70: DECLARE_EXE_TYPE (retail `MOV RAX,[ti+8]` / `MOV EAX,[ti]`).
    void        Persist(IO::Stream& stream) override;                   // [3] 0x1400c4b40
    // [4]-[7]: retail's actionStack is the ActionStack at CharacterComponent +0x48 (the
    // executor 0x1400c4c50 passes RDX = its stack to [4] 0x1400c4ced, [5] 0x1400c4d57 and
    // [7] 0x1400c4ddc; 0x1400c4f80 to [6]); `time` is the System::Time* Component::Tick /
    // Update receive. All four take the embedded CharacterComponent::actionStack (wave X).
    virtual int  AbstractImpl(ActionStack* actionStack) = 0;             // [4] _purecall
    virtual int  Execute(ActionStack* actionStack, const System::Time* time);          // [5] 0x1400c4a90 ret 0
    // [6] args from retail's only caller, Character_UpdateAnimation 0x1400c4f80:
    // RDX = the action stack (CharacterComponent+0x48, `MOV RDX,RSI` 0x1400c4fc4),
    // R8 = the Time* CharacterComponent::Update got in RDX (`MOV R8,RDI` 0x1400c4fbe)
    // before `CALL [RAX+0x30]` 0x1400c4fc7. The bodies read neither.
    virtual void Enter(ActionStack* actionStack, const System::Time* time);            // [6] 0x1400c4b90 RET
    virtual void Exit(ActionStack* actionStack);                         // [7] 0x1400c4ba0 RET
    virtual void CheckEntity();                                   // [8] 0x1400c4a30 RET
    // [9] "does this action reference the entity?": RDX = an InternalPtr<ComponentEntity>
    // (CompleteTask 0x1400c5a50 CheckPtrs RDX and compares [RDX] with its +0x28 entity
    // and its citizen's +0x10 entity; the caller 0x1400c52c0 passes &copy of the entity).
    virtual bool GetTarget(ComponentEntity** param);                          // [9] 0x1400c4a40 ret false


    // ---- the 0x10 bytes Action adds, retail order ---------------------------
    // PUBLIC while unconverted bodies (the executor) read them at raw offsets.
    // +0x10 is the next node of the stack entry's chain (B.4 ACT-a: the walk
    // 0x1400c4f10 advances through it, the dtor releases it; the tree called it
    // `_entity`).
    ::Object::InternalPtr<Action> _next; // +0x10 released by the dtor (DeletePtr)
    bool     _status;                     // +0x18 serialized by slot 3; the
                                          //       stack walk 0x1400c4f10 clears it
    uint8_t  _pad19[7];                   // +0x19 never written by the ctor

protected:
    // The inlined retail base ctor: `Internal()`, then +0x10 = 0, +0x18 = 0.
    // The vptr store is the compiler's.
    Action() : Object::Internal(), _next(), _status(false) {}
};

static_assert(sizeof(Action) == 0x20, "Action is 0x20 bytes (0x10 Internal + 0x10)");
static_assert(offsetof(Action, _pad19) == 0x19, "Action::_pad19");
static_assert(alignof(Action) == 8, "Action is 8-aligned");
static_assert(offsetof(Action, _next) == 0x10, "Action +0x10 next IBP (0x1400013ee)");
static_assert(offsetof(Action, _status) == 0x18, "Action +0x18 status byte (0x1400013f6)");

// ActionStack (B.4 ACT-a): the 0x20-byte action list a CharacterComponent embeds at
// +0x48 (its ctor takes the sub-object's address, the dtor releases it through
// 0x1400c5150). Read from the push 0x1400c54e0 (grows `actions` through
// Array<qword>::SetSize 0x1400032e0 when `current + 1 == count`, walks the old top
// with 0x1400c4f10, then stores at ++current) and CharacterComponent's own fields.
//
// `actions` grows through retail's stride-8 row 0x1400032e0 (stride 8, align 8, the new
// tail ZEROED, no element destroy): kSpec_PtrZero (Entity/EntityList.h). Its buffer is
// freed by the member's ~Array AFTER the dtor body (0x1400c51b3..0x1400c51c3: MemFree,
// then +0x00 = +0x08 = 0), which is retail's order: 0x1400c5150 DeletePtr's the elements
// and +0x10 first.
namespace rtcoll { extern const ArraySpec kSpec_PtrZero; }   // 0x180008d60

struct ActionStack {
    using ActionList = Collection::Array<::Object::InternalPtr<Action>, Collection::DefaultAllocator,
                                         Collection::ExplicitSpec<&rtcoll::kSpec_PtrZero>>;

    // CharacterComponent's ctor 0x140109d20 builds the sub-object inline: +0x00 = +0x08 =
    // +0x10 = 0 (0x140109d69..0x140109d70: `actions`' Array ctor and the null +0x10),
    // +0x18 = -1 (0x140109d74). +0x1C is not written.
    ActionStack() : actions(), completionAction(), current(-1) {}
    ~ActionStack();                                   // 0x1400c5150
    // Cancels every entry (slot 8 down each chain), DeletePtr's the entries, current = -1,
    // releases +0x10 (work_base_reimpl.cpp).
    void Clear();                                     // 0x1400c5400
    // The executor: precheck / main / pop over the top entry's chain (citizenai_executor.cpp).
    void Execute(const System::Time* time);           // 0x1400c4c50
    // Slot 6 down the chain from *ptrRef (CharacterComponent::Update; charactercomponent.cpp).
    void UpdateAnimation(Action** ptrRef, const System::Time* deltaTime);   // 0x1400c4f80
    ActionStack(const ActionStack&) = delete;
    ActionStack& operator=(const ActionStack&) = delete;

    ActionList actions;                       // +0x00 the entries (count +0x08, capacity +0x0C)
    ::Object::InternalPtr<Action> completionAction; // +0x10 (the executor 0x1400c4c50 and the stack clear
                                              //       0x1400c5400 DeletePtr it; a trivial type: layout-neutral)
    int32_t  current;                         // +0x18 index of the running entry (-1 = none)
    uint8_t  _pad1C[4];                       // +0x1C
};

static_assert(sizeof(ActionStack) == 0x20, "ActionStack: CharacterComponent +0x48..+0x67");
static_assert(offsetof(ActionStack, _pad1C) == 0x1C, "ActionStack::_pad1C");
static_assert(offsetof(ActionStack, actions)    == 0x00, "ActionStack::actions @ +0x00");
static_assert(offsetof(ActionStack, actions) + offsetof(ActionStack::ActionList, _size)     == 0x08, "ActionStack count @ +0x08");
static_assert(offsetof(ActionStack, actions) + offsetof(ActionStack::ActionList, _capacity) == 0x0C, "ActionStack capacity @ +0x0C");
static_assert(offsetof(ActionStack, completionAction) == 0x10, "ActionStack +0x10");
static_assert(offsetof(ActionStack, current)    == 0x18, "ActionStack::current @ +0x18");

void ActionStackWalkClear(ActionStack* stack, ::Object::InternalPtr<Action>* cursor);   // 0x1400c4f10 (action_pf_ctors.cpp)
void ActionStackPush(ActionStack* stack, const ::Object::InternalPtr<Action>* action);  // 0x1400c54e0 (action_pf_ctors.cpp)
