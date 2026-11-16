#pragma once
// ============================================================================
// src/game/Tutorial/TutorialGoal.h — ENDGAME B.3.8 block GOAL (B38_RULES D1-D5, D8)
//
// The abstract tutorial goal: one condition a TutorialStep waits on.
//
// RETAIL (Application-x64.exe, B38_SLOTS.md sections 1 and 6):
//   reflected "TutorialGoal", parent "Object::External", ti 0x1401c6438,
//   abstract (create/createInPlace 0x140003730); DEFINE_EXE_TYPE with the
//   abstract factory (ENDGAME B.3.9 RF).
//   table 0x1401607a0, 9 slots:
//     [0] 0x140027220 scalar deleting dtor: stamp, ~External, MemFree on &1
//     [1] 0x140027210 TypeName  [2] 0x140027200 Type
//     [3] 0x1400dec70 GetFileVersion `MOV EAX,1`
//     [4] Object::External::Compile (import) — inherited
//     [5] 0x1400dec30 Serialize: an EMPTY Serializer(stream, 0) scope, MOV AL,1
//     [6] Reconstruct PURE  [7] Destruct PURE (External's)
//     [8] PURE, new here: IsComplete — TutorialInstance::StepExecute (0x1400e1db0) calls it
//         as `char (this, time, &instanceIBP)` and counts the steps' goals that
//         answer non-zero. The name is ours (the exe has no symbols).
//   Every leaf adds slot 9, its own Construct(). sizeof == External's 0x20.
// ============================================================================

#include <cstdint>
#include "object/External.h"
#include "Object/exe_type.h"   // DECLARE_EXE_TYPE (ENDGAME B.3.9 RF)
namespace System { class Time; }

class TutorialGoal : public Object::External {
    DECLARE_EXE_TYPE(TutorialGoal)

public:
    ~TutorialGoal() override = default;                            // [0] 0x140027220
    // [1] TypeName 0x140027210 and [2] Type 0x140027200: DECLARE_EXE_TYPE (retail `MOV RAX,[ti+8]` / `MOV EAX,[ti]`).
    int32_t       GetFileVersion() const override { return 1; }    // [3] 0x1400dec70
    bool          Serialize(IO::Stream& stream) override;                // [5] 0x1400dec30
    // [6] Reconstruct, [7] Destruct: pure (External's)
    virtual bool  IsComplete(const System::Time* time, void* instance) = 0;      // [8] pure

protected:
    // Retail inlines this into each leaf ctor: `??0External@Object@@` then the
    // 0x1401607a0 stamp (the compiler's).
    TutorialGoal() = default;
};

static_assert(sizeof(TutorialGoal) == 0x20, "TutorialGoal adds no bytes to Object::External");
