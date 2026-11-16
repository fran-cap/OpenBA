#pragma once
// ============================================================================
// src/game/Component/LivestockComponent.h — ENDGAME B.3.8 block CIT
//
// `LivestockComponent` (a pasture animal's production timer and pasture
// back-ref) as a real C++ class with a compiler-generated vtable (B38_RULES.md
// D1-D4).
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §6):
//   reflected "LivestockComponent", parent "Component", ti 0x1401c7970
//   create 0x140035480: MemAlloc(0x38, 8) + ctor 0x140123ee0
//   ctor   0x140123ee0: Component ctor, stamp 0x1401639b0, +0x28 = +0x30 = 0,
//                       flags |= 0x0b
//   dtor   0x140035680 (scalar, 83 B): restamp, ~Internal, MemFree on &1
//   table  0x1401639b0, 16 slots:
//     [0] 0x140035680 scalar deleting dtor   [1]/[2] TypeName/Type
//     [3] 0x140123e10 Persist                [6] 0x140123c70 OnAdded
//     [7] 0x140123c00 OnRemove               [9] 0x140123b50 Update
//     every other slot = Component's
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Component/Component.h"
#include "Object/exe_type.h"
#include "system/Value.h"   // System::Time

class PastureComponent;

class LivestockComponent final : public Component {
    DECLARE_EXE_TYPE(LivestockComponent)

public:
    LivestockComponent();                                         // 0x140123ee0

    // no own work
    ~LivestockComponent() override = default;                     // [0] 0x140035680
    void Persist(IO::Stream& stream) override;                          // [3] 0x140123e10
    void OnAdded() override;                                      // [6] 0x140123c70
    void OnRemove() override;                                     // [7] 0x140123c00
    void Update(const System::Time* time) override;                             // [9] 0x140123b50

    void SetPasture(PastureComponent* const* pasture);   // 0x140123cc0
    void PenInPasture();                                  // 0x1401240a0

    // PUBLIC while the ported bodies read them (B38 decision 1). Types from Persist
    // 0x140123e10 (InternalPtr validated against PastureComponent; Time).
    Object::InternalPtr<PastureComponent> pasture;   // +0x28 InternalPtr: the pasture it is kept in (null = free-roaming)
    System::Time      productionTimer;  // +0x30
};

static_assert(sizeof(LivestockComponent) == 0x38, "LivestockComponent is 0x38 (create 0x140035480)");
static_assert(alignof(LivestockComponent) == 8, "LivestockComponent alignment");
static_assert(offsetof(LivestockComponent, pasture) == 0x28, "");
static_assert(offsetof(LivestockComponent, productionTimer) == 0x30, "");
