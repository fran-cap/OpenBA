#pragma once
// ============================================================================
// src/game/Tutorial/TutorialSetupGameSpeed.h — ENDGAME B.3.8 block GOAL
//
// A tutorial setup that sets (and optionally unpauses) the game speed.
//
// RETAIL (Application-x64.exe, B38_SLOTS.md section 6):
//   reflected "TutorialSetupGameSpeed", parent "TutorialSetup", ti 0x1401c6748
//   create 0x1400289c0 (MemAlloc(0x28, 8)), createInPlace 0x140028a20
//   ctor 0x1400e2e40
//   table 0x140160da0, 12 slots:
//     [0] 0x140028ac0 scalar deleting dtor: stamp, stamp TutorialSetup,
//         ~External, MemFree on &1
//     [1] TypeName  [2] Type  [3] 0x1400e2df0 GetFileVersion `MOV EAX,1`
//     [4] External::Compile (inherited)  [5] 0x1400e2d90 Serialize
//     [6] 0x140028a70 Reconstruct  [7] 0x140001640 Destruct
//     [8] 0x1400e2d60 Begin  [9] 0x1400e2d50 Update `RET 0`
//     [10] 0x1400e2d40 End `RET 0`  [11] 0x140028a20 Construct
//   sizeof 0x28.
// ============================================================================

#include <cstddef>
#include <cstdint>
#include "Tutorial/TutorialSetup.h"
#include "Object/exe_type.h"

class TutorialSetupGameSpeed final : public TutorialSetup {
    DECLARE_EXE_TYPE(TutorialSetupGameSpeed)

public:
    TutorialSetupGameSpeed();                                      // 0x1400e2e40

    ~TutorialSetupGameSpeed() override = default;                  // [0] 0x140028ac0
    int32_t           GetFileVersion() const override { return 1; }// [3] 0x1400e2df0
    bool              Serialize(IO::Stream& stream) override;            // [5] 0x1400e2d90
    Object::External* Reconstruct() override;                      // [6] 0x140028a70
    void              Destruct() override;                         // [7] 0x140001640
    void              Begin() override;                            // [8] 0x1400e2d60
    void              Update(const System::Time* /*time*/) override {}           // [9] 0x1400e2d50 RET
    void              End() override {}                            // [10] 0x1400e2d40 RET
    virtual Object::External* Construct();                         // [11] 0x140028a20

public:
    int32_t  speed;              // +0x20: game speed to set (default 1)
    bool     unpause;           // +0x24: also clear the controller's +0x30 byte (Serialize(bool&))
    uint8_t  _pad25[3];          // +0x25: padding
};

static_assert(offsetof(TutorialSetupGameSpeed, speed) == 0x20, "speed offset");
static_assert(offsetof(TutorialSetupGameSpeed, unpause) == 0x24, "_field24 offset");
static_assert(sizeof(TutorialSetupGameSpeed) == 0x28, "TutorialSetupGameSpeed is 0x28 (create 0x1400289c0)");
static_assert(offsetof(TutorialSetupGameSpeed, _pad25) == 0x25, "TutorialSetupGameSpeed::_pad25");
static_assert(alignof(TutorialSetupGameSpeed) == 8, "qword-aligned");
