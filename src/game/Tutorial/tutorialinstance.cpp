// src/game/Tutorial/tutorialinstance.cpp
// Clean reimplementation of TutorialInstance vtable methods.
//
// TutorialInstance (0x38 bytes) -- tutorial runtime instance.
// Vtable: 0x140160bf8, extends Object::Internal (0x10 bytes base).
//
// Fields (from Ghidra decompilation):
//   +0x00: void*    vtable
//   +0x08: uint8_t  _intBase[8]   -- Internal base fields
//   +0x10: int32_t  currentStep   -- current tutorial step index
//   +0x30: void*    tutorialDef   -- ExternalPtr<Tutorial> definition
//
// Decompiled from Application-x64.exe using Ghidra.

#include "Tutorial/TutorialInstance.h"
#include "object/TypeInfo.h"
#include "Tutorial/exe_bodies.h"   // 0x1400262b0 TutWave_SerializeTutorialRef
#include "Tutorial/Tutorial.h"
#include "Tutorial/TutorialStep.h"
#include "Object/factory_registration.h"
#include <cstdint>
#include <cstdio>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B; InternalMemAlloc / InternalMemFree
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch D
#include <new>

// ============================================================
// Construction / destruction (ENDGAME B.3.8 block GOAL)
// ============================================================
// The ctor inlined in create 0x140028120 and in FinishTutorialInstance's
// Startup (0x1400d7a90): Internal() (the base's real ctor), the stamp (the
// compiler's), then the fields. +0x28 is not written (retail's).
TutorialInstance::TutorialInstance()
    : currentStep(-1), stepDialog(nullptr), stepUI(nullptr), tutorialDef(nullptr) {}

// ============================================================
// TutorialInstance::Persist -- Slot3 (vtable +0x18, 0x1400e1520)
// ============================================================
// Serializes the tutorial instance:
//   1. ExternalPtr<Tutorial> at +0x30 via ExternalPtr_SerializeTutorial
//   2. int32_t currentStep at +0x10
//   3. On load: adjusts currentStep backwards if steps are not completed
//      (walks backwards through tutorial steps checking the completed flag)
//
// Disassembly:
//   CALL [0x140152ad8]  ; Serializer::Ctor(stack, stream, 0)
//   CALL 0x1400262b0    ; ExternalPtr<Tutorial>::Serialize(&self+0x30, serializer)
//   CALL [0x140152ac8]  ; Serializer::Serialize(&self+0x10, int)
//   CMP [serializer+8],0  ; check if loading
//   ...step adjustment loop...
//   CALL [0x140152ad0]  ; Serializer::~Serializer
void TutorialInstance::Persist(IO::Stream& stream) {
    // The serializer.
    IO::Serializer serializer(stream, 0);

    // 1. Serialize ExternalPtr<Tutorial> at +0x30
    ::Object::SerializeExternal<Tutorial>(serializer, tutorialDef);   // 0x1400262b0

    // 2. Serialize int32_t currentStep at +0x10
    serializer.Serialize(currentStep, nullptr);

    // 3. On load: adjust currentStep backwards if steps are not completed
    if (static_cast<int>(serializer.GetDirection()) == 0) {   // IO::Serializer::GetDirection, retail 0x180006210
        // 0x1400e1571: MOV R8,[RBX+0x30]; MOV EAX,[RBX+0x10]; CMP EAX,[R8+0x30] (no test of the Tutorial)
        Tutorial* tutDef = tutorialDef;
        int32_t step = currentStep;
        int32_t totalSteps = tutDef->steps._size;

        // Only adjust if step is within range and positive
        if (step < totalSteps && step > 0) {
            // the steps array; each step's completed bool
            while (step > 0) {
                TutorialStep* currentStepObj = tutDef->steps._data[step];
                if (currentStepObj->completed) break;
                step--;
                currentStep = step;
            }
        }
    }
}

// ============================================================
// Reflection (ENDGAME B.3.8)
// ============================================================
// create 0x140028120: MemAlloc(0x38, 8), then the (inlined) ctor.
::Object::Base* TutorialInstance::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(TutorialInstance), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(::new (p) TutorialInstance());
}
// Retail's createInPlace is the imported Internal::CreateInPlaceObject (null);
// the tree's in-place build is kept (B38_RULES B8-13).
::Object::Base* TutorialInstance::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(::new (storage) TutorialInstance());
}

DEFINE_EXE_TYPE(TutorialInstance, "Object::Internal")
