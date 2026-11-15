#pragma once
// ============================================================================
// src/game/Component/AnimalComponent.h — ENDGAME B.3.8 block CIT
//
// `AnimalComponent` (the per-creature component) as a real C++ class with a
// compiler-generated vtable (B38_RULES.md D1-D4).
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §6):
//   reflected "AnimalComponent", parent "Component", ti 0x1401c6f98
//   create 0x14002f020: MemAlloc(0x58, 8) + ctor 0x140106fc0
//   ctor   0x140106fc0: Component ctor, stamp 0x140162110, +0x28 (dword),
//                       +0x30, +0x38, +0x40 (byte), +0x44 (qword), +0x50 = 0,
//                       flags |= 0xc9
//   dtor   0x14002f170 (scalar, 83 B): restamp, ~Internal, MemFree on &1
//   table  0x140162110, 16 slots:
//     [0] 0x14002f170 scalar deleting dtor   [1]/[2] TypeName/Type
//     [3] 0x140106e40 Persist                [4] 0x140106d50 Startup (death age)
//     [6] 0x140106c20 OnAdded                [9] 0x140106420 Update
//     [11] 0x140106dd0 Slot11 (104 B)        every other slot = Component's
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Component/Component.h"
#include "system/Value.h"   // System::Time
#include "Object/exe_type.h"

namespace Math { class Vector3; }

class AnimalComponent final : public Component {
    DECLARE_EXE_TYPE(AnimalComponent)

public:
    AnimalComponent();                                            // 0x140106fc0

    // no own work
    ~AnimalComponent() override = default;                        // [0]  0x14002f170
    void Persist(IO::Stream& stream) override;                          // [3]  0x140106e40
    void Startup() override;                                      // [4]  0x140106d50
    void OnAdded() override;                                      // [6]  0x140106c20
    void Update(const System::Time* time) override;                             // [9]  0x140106420
    void Slot11() override;                                       // [11] 0x140106dd0 post-load re-pen

    // ---- non-virtual members (retail addresses) ----
    int  FindHabitat();                                           // 0x140106020
    void UpdateBehavior();                                        // 0x140107320
    void SetWanderTimer(uint32_t animId, float seconds);          // 0x140106250
    void Place(const Math::Vector3* position, float yaw, bool pose); // 0x140107400 (body: herdcomponent.cpp)
    bool IsInWanderBox() const;                                   // 0x140106b10 (body: herdcomponent.cpp)
    void GetWanderBox(int16_t* x, int16_t* y, int16_t* w, int16_t* h, int size) const; // 0x140105e20 (body: pasturecomponent.cpp)
    void ReleaseToWander();                                       // 0x140105ed0 (body: pasturecomponent.cpp)

    // PUBLIC while the ported bodies read them (B38 decision 1). Types from
    // Persist 0x140106e40: int (import 0x140152ac8), System::Time::Serialize
    // (0x140152aa8), float (0x140152a60), bool (0x140152a58), short
    // (0x140152468). No key names: the names say what Update does with them.
    int32_t      habitatCell;   // +0x28 habitat cell index (y * mapDim + x)
    uint8_t      _pad2C[4];     // +0x2C
    System::Time moveTimer;     // +0x30 counts down to the next wander
    int16_t      wanderX;       // +0x38 wander box (GetWanderBox)
    int16_t      wanderY;       // +0x3A
    int16_t      wanderWidth;   // +0x3C
    int16_t      wanderHeight;  // +0x3E
    bool         canGraze;      // +0x40 gates Update's graze latch
    uint8_t      _pad41[3];     // +0x41
    float        age;           // +0x44 in hours
    float        deathAge;      // +0x48 in hours (rolled by Startup)
    uint8_t      _pad4C[4];     // +0x4C
    System::Time grazeTimer;    // +0x50
};

static_assert(sizeof(AnimalComponent) == 0x58, "AnimalComponent is 0x58 (create 0x14002f020)");
static_assert(offsetof(AnimalComponent, _pad2C) == 0x2C, "AnimalComponent::_pad2C");
static_assert(offsetof(AnimalComponent, _pad41) == 0x41, "AnimalComponent::_pad41");
static_assert(offsetof(AnimalComponent, _pad4C) == 0x4C, "AnimalComponent::_pad4C");
static_assert(alignof(AnimalComponent) == 8, "AnimalComponent alignment");
static_assert(offsetof(AnimalComponent, habitatCell) == 0x28, "AnimalComponent::habitatCell @ +0x28 (Serialize int&)");
static_assert(offsetof(AnimalComponent, moveTimer) == 0x30, "AnimalComponent::moveTimer @ +0x30 (Time::Serialize)");
static_assert(offsetof(AnimalComponent, wanderX) == 0x38, "AnimalComponent::wanderX @ +0x38 (Serialize short&)");
static_assert(offsetof(AnimalComponent, wanderY) == 0x3A, "AnimalComponent::wanderY @ +0x3A (Serialize short&)");
static_assert(offsetof(AnimalComponent, wanderWidth) == 0x3C, "AnimalComponent::wanderWidth @ +0x3C (Serialize short&)");
static_assert(offsetof(AnimalComponent, wanderHeight) == 0x3E, "AnimalComponent::wanderHeight @ +0x3E (Serialize short&)");
static_assert(offsetof(AnimalComponent, canGraze) == 0x40, "AnimalComponent::canGraze @ +0x40 (Serialize bool&)");
static_assert(offsetof(AnimalComponent, age) == 0x44, "AnimalComponent::age @ +0x44 (Serialize float&)");
static_assert(offsetof(AnimalComponent, deathAge) == 0x48, "AnimalComponent::deathAge @ +0x48 (Serialize float&)");
static_assert(offsetof(AnimalComponent, grazeTimer) == 0x50, "AnimalComponent::grazeTimer @ +0x50 (Time::Serialize)");
