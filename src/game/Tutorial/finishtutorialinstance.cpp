// src/game/Goal/finishtutorialinstance.cpp
// Clean reimplementation of FinishTutorialInstance vtable methods.
//
// FinishTutorialInstance (0x28 bytes) — goal instance for tutorial completion.
// Vtable: 0x140160478
//
// Fields (from Ghidra decompilation):
//   +0x00: void*    vtable
//   +0x08: uint32_t field08        — GoalInstance base (Internal base)
//   +0x0C: uint32_t field0C        — GoalInstance base
//   +0x10: int32_t  currentStep    — current tutorial step index
//   +0x18: void*    goalPtr        — InternalBasePtr to parent Goal
//   +0x20: void*    tutorialInst   — InternalBasePtr to TutorialInstance object
//
// Decompiled from Application-x64.exe using Ghidra.

#include <cstdio>
#include "Tutorial/FinishTutorialInstance.h"
#include "Tutorial/FinishTutorialGoal.h"
#include "Tutorial/TutorialInstance.h"
#include "Object/factory_registration.h"
#include "system/Value.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Tutorial/exe_bodies.h"   // 0x1401001f0 / 0x1400e1db0
#include <cstdint>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch D
#include "achievement/Goal.h"   // ENDGAME B.3.7 callers (K2): QUALIFIED base calls
#include "object/TypeInfo.h"    // ENDGAME B.3.7 callers (K2): Object::Base::IsDerivedFrom
#include <new>
#include <atomic>
#include "video/Device.h"   // ENDGAME B.3.9 SS2: Video::Device::_singleton
#include "Tutorial/Tutorial.h"
#include "Game/PlayerToolManager.h"
#include "Component/ComponentDescription.h"
#include "Entity/ComponentEntity.h"   // B4 S4 RT2: InternalPtr<ComponentEntity>::Get() needs the class complete

// Startup/Update call TutWave_TutorialTreeInsert / TutWave_StepExecute by name.
// ENDGAME B.3.8 (B6-5): the one-shot diagnostic counters are namespace-scope
// atomics, not function-local statics — goal instances are built on the loader
// thread (GameUpdateLoop::Load) and ticked on the main thread.
namespace {
std::atomic<int> s_logStartup{0};
std::atomic<int> s_logUpdate{0};
std::atomic<int> s_logCreate{0};
}  // namespace

// ============================================================
// FinishTutorialInstance::Slot3 (vtable +0x18, 0x1400d7bf0)
// ============================================================
// Serialize/Persist: calls GoalInstance::Persist(base), creates Serializer,
// serializes InternalBasePtr at +0x20, then type-checks against TutorialInstance.
//
// Decompiled:
//   GoalInstance::Persist(this, stream);
//   Serializer local_20;
//   IO::Serializer::Serializer(local_20, stream, 0);
//   Object::InternalBasePtr::Serialize(&this->+0x20, local_20);
//   if (*(void**)(this+0x20) != NULL) {
//       if (!IsDerivedFrom(*(void**)(this+0x20), TypeInfo_TutorialInstance)) {
//           *(uint64_t*)(this+0x20) = 0;
//       }
//   }
//   IO::Serializer::~Serializer(local_20);
void FinishTutorialInstance::Persist(IO::Stream& stream) {
    // Achievement::GoalInstance::Persist (slot 3, 0x18009ef70) — QUALIFIED (B7-13):
    // an unqualified call would dispatch back into this slot.
    Achievement::GoalInstance::Persist(stream);

    // The serializer.
    IO::Serializer serializer(stream, 0);

    // Serialize the InternalPtr at +0x20, then, if the deserialized object is
    // not a TutorialInstance, null it out: Object::Base::IsDerivedFrom (import
    // slot 0x140152a98) against the id in TypeInfo_TutorialInstance (dword
    // 0x1401c6668, TutorialInstance's cell — Table 1), exactly retail's
    // 0x1400d7c3a `MOV EDX,[0x1401c6668]` / 0x1400d7c40 `CALL [0x140152a98]`.
    // InternalPtr<T>::Serialize(ser, typeId) is that sequence.
    _tutorialInstance.Serialize(serializer, TutorialInstance::TypeId());
}

// ============================================================
// FinishTutorialInstance::Slot4 (vtable +0x20, 0x1400d7a90)
// ============================================================
// Startup/Load: allocates a TutorialInstance (0x38-byte Internal object),
// initializes its fields, stores it in self+0x20, copies the tutorial
// definition reference from the parent FinishTutorialGoal, then calls
// the tutorial tree insert helper to register the tutorial dialog,
// and sets game world flags to enable tutorial UI.
// Decompiled from disassembly:
//   1. Allocate 0x38 bytes, call Object::Internal::Internal()
//   2. Set vtable to TutorialInstance vtable (0x140160bf8)
//   3. Set field+0x10 = -1, IBP::Ctor(+0x18, NULL), IBP::Ctor(+0x20, NULL), +0x30 = 0
//   4. IBP::Ctor(&local, newObj), store in self+0x20
//   5. IsDerivedFrom(self+0x18, FinishTutorialGoal) -> get goal pointer
//   6. CheckPtr(self+0x20), copy goal->+0x30 to tutInst->+0x30, set tutInst->+0x10 = 0
//   7. Call FinishTutorialGoal_helper_01f0 (tree insert) with:
//      - gameWorld+0x120 (the PlayerToolManager)
//      - tutDef->+0x20 (the tutorial's setup description)
//      - param3 (forwarded)
//      - Video::Device::_singleton->backBufferWidth (+0x04) - 0x1d0
//   8. OR gameWorld+0xd0 flags with 0x5f0
// Retail passes R8 through to 0x1401001f0 untouched (Startup takes no
// argument; `?Startup@GoalInstance@Achievement@@UEAAXXZ`); the tree's
// TutWave_TutorialTreeInsert ignores that parameter, so null is passed.
void FinishTutorialInstance::Startup() {
    FinishTutorialInstance* self = this;
    void* p3 = nullptr;

    // Step 1-3: MemAlloc(0x38, 8) and the TutorialInstance ctor retail inlined
    // here (Internal(), stamp 0x140160bf8, +0x10 = -1, two null
    // InternalBasePtrs, +0x30 = 0). ENDGAME B.3.8: the real class's ctor —
    // the tree stamped `g_TutorialInstanceVtable` only when the opt-in
    // BANISHED_LIVE_TUTORIALINSTANCE_VT registration made 0x140160bf8 resolve,
    // and otherwise left Object::Internal's minted table on the object.
    void* newMem = Object::detail::InternalMemAlloc(sizeof(TutorialInstance), 8);
    TutorialInstance* newObj = nullptr;
    if (newMem != nullptr) {
        newObj = ::new (newMem) TutorialInstance();
    }

    // Step 4: store the new instance in self+0x20
    _tutorialInstance = newObj;

    // Step 5: IsDerivedFrom check on self+0x18 (goal pointer) against FinishTutorialGoal
    // (0x1400d7b2d: CALL IsDerivedFrom; XOR EDI,EDI; CMOVNZ RDI,RBX).
    const Achievement::Goal* goalObj = _goal.Get();
    const FinishTutorialGoal* ftGoal = nullptr;
    bool isDerived = Object::IsDerivedFromNullSafe(goalObj, FinishTutorialGoal::TypeId());
    if (isDerived) {
        ftGoal = static_cast<const FinishTutorialGoal*>(goalObj);
    }

    // Step 6: CheckPtr on self+0x20, then copy goal fields to tutInst, with no
    // test of either (0x1400d7b44: MOV RDX,[RSI]; MOV RAX,[RDI+0x30];
    // MOV [RDX+0x30],RAX; MOV dword ptr [RDX+0x10],0).
    TutorialInstance* tutInst = _tutorialInstance.Get();
    tutInst->tutorialDef = ftGoal->_tutorial;
    tutInst->currentStep = 0;

    { if (s_logStartup.fetch_add(1) == 0) {
        std::fprintf(stderr, "[FinTutInst] Slot4 INIT self=%p tutInst=%p goalObj=%p ftGoal=%p "
                             "tutDef=%p (goalTid=%u)\n",
                static_cast<void*>(self), static_cast<void*>(tutInst),
                static_cast<const void*>(goalObj), static_cast<const void*>(ftGoal),
                tutInst ? static_cast<void*>(tutInst->tutorialDef) : nullptr,
                FinishTutorialGoal::TypeId());
        std::fflush(stderr); } }

    // Step 7: Call FinishTutorialGoal_helper_01f0 (the PlayerToolManager tree insert)
    if (GameUpdateLoop::_singleton != nullptr) {
        PlayerToolManager* treeRoot = GameUpdateLoop::_singleton->m_toolManager;

        // The tutorial's setup description (tutDef +0x20; 0x1400d7b6b: no test)
        Tutorial* tutDef = tutInst->tutorialDef;
        ComponentDescription* tutorialId = tutDef->setup;

        // The device's back-buffer width (+0x04) - 0x1d0 (0x1400d7b60: no test)
        int widthParam = ::Video::Device::_singleton->backBufferWidth - 0x1d0;

        treeRoot->TutorialTreeInsert(tutorialId, p3, static_cast<uint32_t>(widthParam));   // 0x1401001f0
    }

    // Step 8: OR game world flags at +0xd0 with 0x5f0
    if (GameUpdateLoop::_singleton != nullptr) {
        GameUpdateLoop::_singleton->m_gameFlags |= 0x5f0;
    }
}

// ============================================================
// FinishTutorialInstance::Slot5 (vtable +0x28, 0x1400d7a50)
// ============================================================
// Execute/Update: checks the InternalBasePtr at +0x20 (TutorialInstance),
// then delegates to the tutorial step execution helper.
// Decompiled from disassembly:
//   1. CheckPtr(&self->+0x20)
//   2. Tail-call FinishTutorialInstance_helper_1db0(*(self+0x20), p2, p3, p4)
// Note: R8 (p3) and R9 (p4) are implicitly forwarded through the tail JMP.
// Update(const Time&, const GoalList&): RDX / R8 reach the helper unchanged;
// R9 is whatever the caller left (StepExecute ignores it and R8).
void FinishTutorialInstance::Update(const System::Time& time, const Achievement::GoalList& list) {
    FinishTutorialInstance* self = this;
    const System::Time* p2 = &time;               // RDX, forwarded unchanged
    const Achievement::GoalList* p3 = &list;      // R8, forwarded unchanged

    // Read the TutorialInstance pointer
    TutorialInstance* tutInst = _tutorialInstance.Get();
    { if (s_logUpdate.fetch_add(1) < 3) {
        std::fprintf(stderr, "[FinTutInst] Slot5 TICK self=%p tutInst=%p\n",
                static_cast<void*>(self), static_cast<void*>(tutInst)); std::fflush(stderr); } }

    // Delegate to the tutorial step execution helper (0x1400d7a50: tail JMP, no test)
    tutInst->StepExecute(p2, p3, 0);   // 0x1400e1db0
}

// ============================================================
// FinishTutorialInstance::Slot6 (vtable +0x30, 0x1400d7bb0)
// ============================================================
// Check/Shutdown: checks whether the tutorial is complete.
// Reads the TutorialInstance via IBP at +0x20, compares:
//   tutorial->+0x30 (External* to Tutorial definition)
//   tutorialDef->+0x30 (total step count)
//   vs tutorialInst->+0x10 (current step)
// Returns: currentStep >= totalSteps
Achievement::Goal::GoalState FinishTutorialInstance::CheckGoalState() const {
    // Read the TutorialInstance pointer and its tutorial definition, with no test
    // (0x1400d7bb0: MOV RDX,[RBX]; MOV RCX,[RDX+0x30]; MOV ECX,[RCX+0x30]).
    const TutorialInstance* tutInst = _tutorialInstance.Get();
    const Tutorial* tutDef = tutInst->tutorialDef;

    // Compare: totalSteps <= currentStep
    int totalSteps = tutDef->steps._size;
    int currentStep = tutInst->currentStep;

    return (totalSteps <= currentStep) ? Achievement::Goal::kMet : Achievement::Goal::kNotMet;
}

// ============================================================
// Construction / destruction (ENDGAME B.3.8 block GOAL)
// ============================================================
// create 0x140026070: MemAlloc(0x28, 8), GoalInstance() (import slot
// 0x140152858), the vptr, +0x20 = 0 (the base ctor zeroes +0x10 and +0x18).
FinishTutorialInstance::FinishTutorialInstance() : _tutorialInstance(nullptr) {}

// The dtor inside [0] 0x140026200 (and 0x1400d7c70): stamp, DeletePtr on the
// TutorialInstance handle at +0x20; the compiler then runs ~GoalInstance
// (re-stamp + ~Internal, the InternalFactory swap-remove).
FinishTutorialInstance::~FinishTutorialInstance() {
    _tutorialInstance.DeletePtr();
}

::Object::Base* FinishTutorialInstance::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(FinishTutorialInstance), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(::new (p) FinishTutorialInstance());
}

// Retail's createInPlace is the imported Internal::CreateInPlaceObject (null);
// the tree's in-place build is kept (B38_RULES B8-13).
::Object::Base* FinishTutorialInstance::CreateInPlaceObject(void* memory) {
    { if (s_logCreate.fetch_add(1) < 3) {
        std::fprintf(stderr, "[FinTutInst] CREATED (in place) mem=%p\n", memory); std::fflush(stderr); } }
    if (!memory) return nullptr;
    return static_cast<Object::Base*>(::new (memory) FinishTutorialInstance());
}

DEFINE_EXE_TYPE(FinishTutorialInstance, "Achievement::GoalInstance")
