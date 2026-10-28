#pragma once
// ============================================================================
// src/game/Component/StatusIconComponent.h — ENDGAME B.3.8 block VIS
//
// `StatusIconComponent` as a real C++ class with a compiler-generated vtable
// (B38_RULES.md D1-D4, D6), and its embedded billboard as a REAL
// `Graphics::Geometry` subclass (B38_RULES §1.2: the unreflected exe table
// 0x14015f0c0). The engine/types POD is a forwarder to this file.
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §6):
//   reflected "StatusIconComponent", parent "Component", ti 0x1401c7e08
//   create 0x1400388c0: MemAlloc(0x108, 8) + ctor 0x14012c0c0
//   ctor   0x14012c0c0: Component ctor, stamp 0x140164620; the MEMBER at +0x28:
//                       `??0Geometry@Graphics@@QEAA@XZ` (import 0x140152598),
//                       stamp 0x14015f0c0, +0xc0 (qword) = +0xc8 = +0xcc (byte)
//                       = +0xd0 = 0; then +0x100 = -1; flags |= 0xc2
//   dtor   0x14012c070: restamp, `??1Geometry@Graphics@@` on +0x28 (import
//                       0x1401525a0), restamp 0x140162920, ~Internal
//   table  0x140164620, 16 slots:
//     [0]  0x140038ac0 scalar deleting dtor  [1]/[2] TypeName/Type
//     [3]  0x14012bff0 Persist               [4] 0x14012bed0 Startup (sprite setup)
//     [7]  0x14012beb0 OnRemove (15 B)       [8] 0x14012bbd0 Tick (positioning)
//     [11] 0x14012bf40 Slot11 (post-load)   every other slot = Component's
//   member table 0x14015f0c0, 9 slots (the billboard, StatusIconGeometry):
//     [0] 0x14001ebb0 deleting dtor (~Geometry, MemFree on &1)
//     [1] 0x1400aa430 Update: `RET`
//     [2] 0x1400a9c10 Draw          [3] 0x1400a9b90 AddDrawItems
//     [4]-[8] Graphics::Geometry's (UpdateAttached, GetSubTransform,
//             GetInstanceTransform, SetSharedInstanceData, Clean)
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "graphics/Geometry.h"
#include "Component/Component.h"
#include "Object/exe_type.h"

namespace UI { class SpriteSheet; }

// The billboard Geometry (retail table 0x14015f0c0, 0xd8 bytes). Bodies in
// statusicon_geometry.cpp. Before this class the tree COPIED the minted
// `??_7Geometry@Graphics@@` table and patched slots 2/3 (a D5 slot-copy site).
class StatusIconGeometry final : public Graphics::Geometry {
public:
    StatusIconGeometry();                                                  // inlined in 0x14012c0c0

    // ~Geometry (import 0x1401525a0)
    ~StatusIconGeometry() override = default;                              // [0] 0x14001ebb0
    void Update(const System::Time& time) override;                        // [1] 0x1400aa430 RET
    void Draw(Video::Context& context, const Graphics::Camera& camera,
              Graphics::ExternalPtrHandle material, float t,
              int a, int b, int c) const override;                         // [2] 0x1400a9c10
    void AddDrawItems(const Graphics::Scene& scene,
                      Graphics::Renderer& renderer) const override;        // [3] 0x1400a9b90

    // 0x1400aa440 (StatusIconComponent_helper_a440, one caller: Startup 0x14012bed0):
    // the sprite parameters and the local bounds of a stack of `spriteCount` icons.
    void Setup(float size, int count, uint8_t constant, UI::SpriteSheet* sheet);

    // The billboard's own 0x18 bytes (written by Setup 0x1400aa440).
    float    iconSize;      // +0xc0 (component +0xe8)
    int32_t  spriteCount;   // +0xc4 (component +0xec)
    uint32_t activeMask;    // +0xc8 (component +0xf0) — the persisted icon bits
    uint8_t  constantSize;  // +0xcc (component +0xf4) — camera-constant screen size
    uint8_t  _padCD[3];     // +0xcd
    UI::SpriteSheet* spriteSheet;   // +0xd0 (component +0xf8) StatusIconDescription::_spriteSheet
};

static_assert(sizeof(StatusIconGeometry) == 0xd8, "the billboard spans component +0x28..+0x100");
static_assert(offsetof(StatusIconGeometry, spriteCount) == 0xC4, "StatusIconGeometry::spriteCount");
static_assert(offsetof(StatusIconGeometry, _padCD) == 0xCD, "StatusIconGeometry::_padCD");
static_assert(offsetof(StatusIconGeometry, iconSize) == 0xc0, "billboard +0xc0 (0x14012c10a)");
static_assert(offsetof(StatusIconGeometry, activeMask) == 0xc8, "billboard +0xc8 (0x14012c111)");
static_assert(offsetof(StatusIconGeometry, constantSize) == 0xcc, "billboard +0xcc (0x14012c117)");
static_assert(offsetof(StatusIconGeometry, spriteSheet) == 0xd0, "billboard +0xd0 (0x14012c11d)");

class StatusIconComponent final : public Component {
    DECLARE_EXE_TYPE(StatusIconComponent)

public:
    StatusIconComponent();                                        // 0x14012c0c0

    // 0x14012c070
    ~StatusIconComponent() override = default;                    // [0]  0x140038ac0
    void Persist(IO::Stream& stream) override;                          // [3]  0x14012bff0
    void Startup() override;                                      // [4]  0x14012bed0
    void OnRemove() override;                                     // [7]  0x14012beb0 TREE DIVERGENCE
    void Tick(const System::Time* time) override;                               // [8]  0x14012bbd0
    void Slot11() override;                                       // [11] 0x14012bf40 = ShowAfterLoad

    // 0x14012c3f0 (the tree's CitizenComp_FUN_14012c3f0): set / clear icon bit
    // `flagIndex` in the billboard's activeMask (+0xF0); an empty mask removes the
    // geometry from the scene, a non-empty one adds it (settings permitting) and
    // repositions it through Tick; the +0x20 flag 4 follows, and a change notifies
    // the entity.
    void SetFlag(uint8_t flagIndex, char value);
    // 0x14012c2a0 (StatusIconComponent_helper_c2a0, one caller: DestroyComponent state 3 enter 0x14011d7b0): clear every set icon bit,
    // bit 0 first, each with the scene and entity handling of SetFlag(bit, 0).
    void ClearAllFlags();
    // The body of [11] 0x14012bf40 (re-run the sprite setup, add the geometry to the
    // scene when bits are set and it is not there yet). Slot11 calls it.
    void ShowAfterLoad();

    // PUBLIC while the ported bodies read them (B38 decision 1).
    StatusIconGeometry _geometry;  // +0x28 the billboard (vptr 0x14015f0c0 in retail)
    int32_t  iconId;               // +0x100 Scene object id (ctor -1 = not registered)
    uint8_t  _pad104[4];           // +0x104
};

static_assert(sizeof(StatusIconComponent) == 0x108, "StatusIconComponent is 0x108 (create 0x1400388c0)");
static_assert(offsetof(StatusIconComponent, _pad104) == 0x104, "StatusIconComponent::_pad104");
static_assert(alignof(StatusIconComponent) == 8, "StatusIconComponent alignment");
static_assert(offsetof(StatusIconComponent, _geometry) == 0x28, "member billboard at +0x28 (0x14012c0eb)");
static_assert(offsetof(StatusIconComponent, iconId) == 0x100, "scene id +0x100 (0x14012c124)");
