#pragma once
// The by-name External reference (0x18 bytes): the resource's name, the
// ExternalFactory generation it was last resolved at, and the cached result.
//
// Retail has ONE such record: every call of the by-name resolver 0x14001b1d0
// (MapGenerator, TerrainGenerator, BuildingGoal, the tutorial steps, the start
// town) hands it, and the goals' / tutorials' ExternalPtr-by-name resolvers
// (0x1400265b0, 0x140027de0) and the terrain-material resolver 0x14001fd10 read
// the same three fields. B4 S4 RESOLVE: MapGenerator's `ExternalRef` is this
// record (a `using` alias, Game/MapGenerator.h).
//
// B4 Y GAMEA (H5): `name` is a real System::String. Retail's default ctor is
// inlined in every owner, in member order (String() qword 0, dword -1 at +0x08,
// qword 0 at +0x10, +0x0C unwritten): StatGoal 0x1400daad2, TutorialGoalCount
// 0x1400dfaa3, TutorialSetupRestrictPlayer 0x1400e326f, MapGenerator 0x1400a3077 /
// 0x1400a3123 / 0x1400a3147 / 0x1400a316b, TerrainGenerator 0x1400b1845 /
// 0x1400b185c / 0x1400b1873. Its copy ctor is 0x14001af90 (String copy, then
// +0x08 dword, +0x10 qword); its dtor is ~String alone, which every owner's dtor
// runs after its body in reverse declaration order (StatGoal 0x140026540,
// TutorialGoalCount 0x140027820, TutorialGoalDialog 0x140027590,
// TutorialImageHotKey 0x140027d70, TutorialImageToolbarIcon 0x140028090,
// TutorialSetupDialog 0x1400e2bf0, TutorialSetupRestrictPlayer 0x140028bb0,
// MapGenerator 0x14001aea0, TerrainGenerator 0x140020630). Array elements are
// built and copied by the arrays' rows (0x14001b340 / 0x14001b4e0), not by
// these members.

#include <cstddef>
#include <cstdint>
#include "system/String.h"

namespace Object { class External; }

struct NamedExternalRef {
    System::String name;            // +0x00 the resource's name
    int32_t  generation;            // +0x08 ExternalFactory generation (-1: never resolved)
    uint8_t  _pad0C[4];             // +0x0C written by neither the ctor nor the resolver
    Object::External* resolved;     // +0x10 the cached External (null when not found)

    NamedExternalRef() : generation(-1), resolved(nullptr) {}
    // 0x14001af90: +0x0C is not copied.
    NamedExternalRef(const NamedExternalRef& src)
        : name(src.name), generation(src.generation), resolved(src.resolved) {}
    NamedExternalRef& operator=(const NamedExternalRef&) = delete;

    // 0x1400265b0: ExternalPtr<External>'s lazy resolve by name (Goal/goal_exe_helpers.cpp).
    void Resolve();
    // 0x140027de0: ExternalPtr<Toolbar>'s (Tutorial/tutorial_base.cpp).
    void ResolveToolbar();
    // 0x14001b1d0: the by-name resolver every owner calls (Component/componentdescription.cpp).
    void ResolveDescription();
};
static_assert(sizeof(NamedExternalRef) == 0x18, "the by-name reference is 0x18");
static_assert(offsetof(NamedExternalRef, name) == 0x00, "NamedExternalRef +0x00 name");
static_assert(offsetof(NamedExternalRef, generation) == 0x08, "NamedExternalRef +0x08 generation");
static_assert(offsetof(NamedExternalRef, _pad0C) == 0xC, "NamedExternalRef::_pad0C");
static_assert(offsetof(NamedExternalRef, resolved) == 0x10, "NamedExternalRef +0x10 resolved");
