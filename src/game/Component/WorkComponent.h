#pragma once
// ============================================================================
// src/game/Component/WorkComponent.h — ENDGAME B.3.8 block VIS
//
// `WorkComponent` as a real C++ class with a compiler-generated vtable
// (B38_RULES.md D1-D4). The engine/types POD is a forwarder to this file.
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §6):
//   reflected "WorkComponent", parent "Component", ti 0x1401c8070
//   create 0x140039e70: MemAlloc(0x38, 8) + ctor 0x14012e820
//   ctor   0x14012e820: Component ctor, stamp 0x140164be0, +0x28 = -1,
//                       +0x2c = 0x80000000, +0x30 = 0, flags |= 3 (0x83)
//   dtor   0x14012e7e0 (slot 0 0x140039fb0, 83 B: the plain restamp + ~Internal shape)
//   table  0x140164be0, 16 slots:
//     [0]  0x140039fb0 scalar deleting dtor  [1]/[2] TypeName/Type
//     [3]  0x14012e710 Persist               [6] 0x14012e6f0 OnAdded (Ghidra "Draw": +0x30 = CalcMaxWorkers)
//     [7]  0x14012e6c0 OnRemove (cancel my work)
//     [13] 0x14012e6b0 own `XOR EAX,EAX; RET` [14] 0x14012e6a0 own `RET`
//     every other slot = Component's
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Component/Component.h"
#include "Object/exe_type.h"

class Work;   // Work/Work.h (PostWork's InternalPtr<Work>)

class WorkComponent final : public Component {
    DECLARE_EXE_TYPE(WorkComponent)

public:
    // The "posted no work" sentinel of workFlags (ctor 0x14012e820 stores 0x80000000; the field is
    // retail's int: Serialize(int&) at 0x14012e7ab).
    static constexpr int32_t kWorkFlagsNone = static_cast<int32_t>(0x80000000u);

    WorkComponent();                                              // 0x14012e820

    // 0x14012e7e0
    ~WorkComponent() override = default;                          // [0]  0x140039fb0
    void     Persist(IO::Stream& stream) override;                      // [3]  0x14012e710
    void     OnAdded() override;                                  // [6]  0x14012e6f0
    void     OnRemove() override;                                 // [7]  0x14012e6c0
    uint32_t CommandMask(int command) override;                   // [13] 0x14012e6b0 own ret 0
    void     Command(int command) override;                       // [14] 0x14012e6a0 own RET

    // ---- non-virtual members (retail addresses) ----
    int  CalcMaxWorkers() const;                                  // 0x14012e5c0 the starting worker count
    bool IsWorkPosted() const;                                    // 0x14012ead0 workId is in the goals tree
    void CancelWork();                                            // 0x14012eb50 take the posted work back
    void PostWork(uint32_t code, char flag, Object::InternalPtr<Work>* pWorkIBP);   // 0x14012ec60 post a work of `code` to the work scheduler

    // PUBLIC while the ported bodies read them (B38 decision 1).
    int32_t  workId;       // +0x28 posted work id (ctor -1 = none posted)
    int32_t  workFlags;    // +0x2c (ctor 0x80000000 = the same sentinel; B6 row 19: retail serializes it through Serialize(int&), 0x14012e7ab); really the posted work's
                           //        queue-order key (CancelWork copies Work::_queueOrder back; known_port_issues 201)
    int32_t  workState;    // +0x30 work capacity (ctor 0); really the cached max worker count
                           //        (OnAdded 0x14012e6f0 stores CalcMaxWorkers; known_port_issues 173)
    uint8_t  _pad34[4];    // +0x34
};

static_assert(sizeof(WorkComponent) == 0x38, "WorkComponent is 0x38 (create 0x140039e70)");
static_assert(offsetof(WorkComponent, _pad34) == 0x34, "WorkComponent::_pad34");
static_assert(alignof(WorkComponent) == 8, "WorkComponent alignment");
static_assert(offsetof(WorkComponent, workId) == 0x28, "");
static_assert(offsetof(WorkComponent, workFlags) == 0x2c, "");
static_assert(offsetof(WorkComponent, workState) == 0x30, "");
