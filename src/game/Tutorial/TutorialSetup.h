#pragma once
// ============================================================================
// src/game/Tutorial/TutorialSetup.h — ENDGAME B.3.8 block GOAL (B38_RULES D1-D5, D8)
//
// The abstract tutorial setup: game state a TutorialStep puts in place while
// it runs (camera, speed, restrictions, a dialog).
//
// RETAIL (Application-x64.exe, B38_SLOTS.md sections 1 and 6):
//   reflected "TutorialSetup", parent "Object::External", ti 0x1401c66a0,
//   abstract (create/createInPlace 0x140003730); DEFINE_EXE_TYPE with the
//   abstract factory (ENDGAME B.3.9 RF).
//   table 0x140160c30, 11 slots:
//     [0] 0x140028390 scalar deleting dtor: stamp, ~External, MemFree on &1
//     [1] 0x140028380 TypeName  [2] 0x140028370 Type
//     [3] 0x1400e2040 GetFileVersion `MOV EAX,1`
//     [4] Object::External::Compile (import) — inherited
//     [5] 0x1400e2000 Serialize: an EMPTY Serializer(stream, 0) scope, MOV AL,1
//     [6] Reconstruct PURE  [7] Destruct PURE (External's)
//     [8] PURE Begin   — TutorialInstance::RefreshStepUI calls `(this)` when a step opens
//     [9] PURE Update  — TutorialInstance::StepExecute (0x1400e1db0) calls `(this, time)`
//     [10] PURE End    — StepExecute calls `(this)` when the step completes
//     (names ours; the exe has no symbols)
//   Every leaf adds slot 11, its own Construct(). sizeof == External's 0x20.
// ============================================================================

#include <cstdint>
#include "object/External.h"
#include "Object/exe_type.h"   // DECLARE_EXE_TYPE (ENDGAME B.3.9 RF)
namespace System { class Time; }

class TutorialSetup : public Object::External {
    DECLARE_EXE_TYPE(TutorialSetup)

public:
    ~TutorialSetup() override = default;                           // [0] 0x140028390
    // [1] TypeName 0x140028380 and [2] Type 0x140028370: DECLARE_EXE_TYPE (retail `MOV RAX,[ti+8]` / `MOV EAX,[ti]`).
    int32_t       GetFileVersion() const override { return 1; }    // [3] 0x1400e2040
    bool          Serialize(IO::Stream& stream) override;                // [5] 0x1400e2000
    // [6] Reconstruct, [7] Destruct: pure (External's)
    virtual void  Begin() = 0;                                     // [8] pure
    virtual void  Update(const System::Time* time) = 0;                          // [9] pure
    virtual void  End() = 0;                                       // [10] pure

protected:
    // Retail inlines this into each leaf ctor: `??0External@Object@@` then the
    // 0x140160c30 stamp (the compiler's).
    TutorialSetup() = default;
};

static_assert(sizeof(TutorialSetup) == 0x20, "TutorialSetup adds no bytes to Object::External");
