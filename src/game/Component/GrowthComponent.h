#pragma once
// ============================================================================
// src/game/Component/GrowthComponent.h — ENDGAME B.3.8 block CIT
//
// `GrowthComponent` (natural-resource growth and its visual scale) as a real C++
// class with a compiler-generated vtable (B38_RULES.md D1-D4).
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §6):
//   reflected "GrowthComponent", parent "Component", ti 0x1401c77b0
//   create 0x140034b10: MemAlloc(0x50, 8) + ctor 0x1401208f0
//   ctor   0x1401208f0: Component ctor, stamp 0x140163590, +0x28..+0x40 = 0,
//                       +0x48 = +0x4c = 1.0f, flags |= 0xcb
//   dtor   0x140034c50 (scalar, 83 B): restamp, ~Internal, MemFree on &1
//   table  0x140163590, 16 slots:
//     [0] 0x140034c50 scalar deleting dtor   [1]/[2] TypeName/Type
//     [3] 0x1401207c0 Persist                [4] 0x1401206f0 Startup
//     [6] 0x140120690 OnAdded (its own `RET`)
//     [7] 0x140120560 OnRemove (283 B)       [9] 0x140120050 Update
//     [10] 0x14011fea0 PostUpdate (visual scale)
//     [11] 0x1401206a0 Slot11 (64 B)         every other slot = Component's
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Component/Component.h"
#include "Object/exe_type.h"
#include "object/Internal.h"   // Object::InternalPtr<T>
#include "system/Value.h"      // System::Time

class NaturalResourceComponent;   // Component/NaturalResourceComponent.h
class ModelComponent;             // Component/ModelComponent.h
class GrowthDescription;          // Component/GrowthDescription.h

class GrowthComponent final : public Component {
    DECLARE_EXE_TYPE(GrowthComponent)

public:
    GrowthComponent();                                            // 0x1401208f0

    // no own work
    ~GrowthComponent() override = default;                        // [0]  0x140034c50
    void Persist(IO::Stream& stream) override;                          // [3]  0x1401207c0
    void Startup() override;                                      // [4]  0x1401206f0
    void OnAdded() override;                                      // [6]  0x140120690 (RET)
    void OnRemove() override;                                     // [7]  0x140120560 the cut resource's yield
    void Update(const System::Time* time) override;                             // [9]  0x140120050
    void PostUpdate() override;                                   // [10] 0x14011fea0
    void Slot11() override;                                       // [11] 0x1401206a0 post-load: +0x38, PostUpdate

    // ---- non-virtual members (retail addresses; RCX = this) ----
    int  GetResourceIndex();                                      // 0x140120410
    void SetResourceAmount(float amount);                         // 0x140120470
    void SpawnSubResource(GrowthDescription* config);             // 0x14011fba0
    void SetProgress(float progress);                             // 0x140120db0 the growth fraction; picks the model variant / sub-resource state

    // PUBLIC while the ported bodies read them (B38 decision 1). Types from Persist
    // 0x1401207c0: +0x28 / +0x30 InternalBasePtr::Serialize then IsDerivedFrom
    // NaturalResourceComponent (0x1401c7b30) / ModelComponent (0x1401c7a50); +0x48 /
    // +0x4c Serialize(float&); +0x40 Time::Serialize.
    Object::InternalPtr<NaturalResourceComponent> resource;   // +0x28 (Startup: FindComponent 0x1400052c0)
    Object::InternalPtr<ModelComponent>           model;      // +0x30 (Startup: FindComponent 0x140003f60)
    GrowthDescription* config;     // +0x38 Startup's copy of the description (not serialized)
    System::Time spawnTimer;       // +0x40 sub-resource spawn timer (Update)
    float    displayScale; // +0x48 the smoothed displayed scale (PostUpdate; retail ctor 1.0f)
    float    growthScale;  // +0x4C the harvest threshold (Startup; retail ctor 1.0f)
};

static_assert(sizeof(GrowthComponent) == 0x50, "GrowthComponent is 0x50 (create 0x140034b10)");
static_assert(alignof(GrowthComponent) == 8, "GrowthComponent alignment");
static_assert(offsetof(GrowthComponent, resource)    == 0x28, "");
static_assert(offsetof(GrowthComponent, model)       == 0x30, "");
static_assert(offsetof(GrowthComponent, config)      == 0x38, "");
static_assert(offsetof(GrowthComponent, spawnTimer)  == 0x40, "");
static_assert(offsetof(GrowthComponent, displayScale) == 0x48, "");
static_assert(offsetof(GrowthComponent, growthScale) == 0x4C, "");
