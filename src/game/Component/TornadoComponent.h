#pragma once
// ============================================================================
// src/game/Component/TornadoComponent.h — ENDGAME B.3.8 block CIT
//
// `TornadoComponent` (the tornado disaster's path, movement and damage) as a real
// C++ class with a compiler-generated vtable (B38_RULES.md D1-D4). (The visual
// `Tornado` Graphics::Particle class is block LOOP's.)
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §6):
//   reflected "TornadoComponent", parent "Component", ti 0x1401c4ad8
//   create 0x14000d500: MemAlloc(0x68, 8) + ctor 0x140058cf0
//   ctor   0x140058cf0: Component ctor, stamp 0x14015aa80, +0x28 = 0, path
//                       block at +0x30 {+0x30 = 0, +0x38 (dword) = 0, +0x40 =
//                       +0x48 = 0, +0x50 = -1}, +0x58 = 0, +0x60 (dword) = 0,
//                       flags |= 0x84 (bit 2: the full-rate tree walk -> slot 8)
//   dtor   0x14000d640 (scalar): restamp, MemFree(+0x40), +0x40 = +0x48 = 0
//                       (~path: its ~points, the inlined ~Array over this+0x30
//                       +0x10), stamp 0x140162920, ~Internal,
//                       MemFree on &1
//   table  0x14015aa80, 16 slots:
//     [0] 0x14000d640 scalar deleting dtor   [1]/[2] TypeName/Type
//     [3] 0x140058b80 Persist                [4] 0x1400585a0 Startup (path)
//     [8] 0x140057cd0 Tick (per frame)       every other slot = Component's
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Component/Component.h"
#include "Object/exe_type.h"
#include "math/Math.h"       // Math::Vector3
#include "system/Value.h"    // System::Time
#include "collection/Collection.h"   // Collection::Array (path.points)

class TornadoComponent final : public Component {
    DECLARE_EXE_TYPE(TornadoComponent)

public:
    TornadoComponent();                                           // 0x140058cf0

    // 0x14000d640: the body is empty; ~path (its ~points) frees and zeroes the
    // waypoint array at +0x40 / +0x48.
    ~TornadoComponent() override = default;                       // [0] 0x14000d640
    void Persist(IO::Stream& stream) override;                          // [3] 0x140058b80
    void Startup() override;                                      // [4] 0x1400585a0
    void Tick(const System::Time* time) override;                               // [8] 0x140057cd0

    // The waypoint path at +0x30 (H6): 0x1400a70c0 and 0x1400a71e0 take `this+0x30`
    // as their receiver. The point array is an Array<Vector3> (SetSize 0x14000d030,
    // append 0x14000cf40, both with Vector3's zeroing ctor as the init arm; freed by
    // the retail dtor 0x14000d675..0x14000d685).
    struct Path {
        Math::Vector3  target;     // +0x00 the waypoint being walked to (Advance latches it, z = 0)
        uint8_t        _pad0C[4];  // +0x0C
        Collection::Array<Math::Vector3> points;   // +0x10 the waypoints (count +0x18, capacity +0x1C)
        int32_t        index;      // +0x20 the current waypoint (ctor -1)
        uint8_t        _pad24[4];  // +0x24

        // 0x1400a70c0: the first waypoint after `index` at least `radius` from `pos`
        // (the waypoint's Z taken as 0), -1 past the end.
        int  FindNext(const float* pos, float radius) const;
        // 0x1400a71e0: once `target` is within `arriveRadius`, latch the next
        // waypoint more than `skipRadius` away. False only when the path is exhausted.
        bool Advance(const float* pos, float skipRadius, float arriveRadius);
    };

    // 0x140058980: recursive midpoint displacement over points[lo..hi].
    void BuildPathFractal(float* points, int lo, int hi);
    // 0x140058040: the damage countdown and the three strike sets.
    void DamageTick(const System::Time* dt);

    // PUBLIC while the ported bodies read them (B38 decision 1).
    System::Time   damageCountdown; // +0x28 reloaded from TornadoDescription::_damageInterval
    Path           path;            // +0x30 .. +0x57
    Math::Vector3  position;        // +0x58 live world position
    uint8_t        _pad64[4];       // +0x64
};

static_assert(sizeof(TornadoComponent) == 0x68, "TornadoComponent is 0x68 (create 0x14000d500)");
static_assert(offsetof(TornadoComponent, _pad64) == 0x64, "TornadoComponent::_pad64");
static_assert(alignof(TornadoComponent) == 8, "TornadoComponent alignment");
static_assert(offsetof(TornadoComponent, damageCountdown) == 0x28, "");
static_assert(offsetof(TornadoComponent, path)            == 0x30, "the path block (0x1400a71e0's receiver)");
static_assert(sizeof(TornadoComponent::Path)              == 0x28, "");
static_assert(offsetof(TornadoComponent::Path, target) == 0x0, "Path::target");
static_assert(offsetof(TornadoComponent::Path, _pad0C) == 0xC, "Path::_pad0C");
static_assert(offsetof(TornadoComponent::Path, _pad24) == 0x24, "Path::_pad24");
static_assert(offsetof(TornadoComponent::Path, points)    == 0x10, "path +0x40 (the retail dtor frees it)");
static_assert(offsetof(TornadoComponent::Path, points) + offsetof(Collection::Array<Math::Vector3>, _size) == 0x18, "path count +0x48");
static_assert(offsetof(TornadoComponent::Path, points) + offsetof(Collection::Array<Math::Vector3>, _capacity) == 0x1C, "path capacity +0x4C");
static_assert(offsetof(TornadoComponent::Path, index)     == 0x20, "path index +0x50 (ctor -1)");
static_assert(offsetof(TornadoComponent, position)        == 0x58, "");
