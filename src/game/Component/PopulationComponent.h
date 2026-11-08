#pragma once
// ============================================================================
// src/game/Component/PopulationComponent.h — ENDGAME B.3.8 block CIT
//
// `PopulationComponent` (a wild-animal population: habitability grid, density
// cells, the herds it spawns) as a real C++ class with a compiler-generated
// vtable (B38_RULES.md D1-D4). The engine/types POD is a forwarder to this file.
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §6):
//   reflected "PopulationComponent", parent "Component", ti 0x1401c4790
//   create 0x14000af50: MemAlloc(0x70, 8) + ctor 0x1400546d0
//   ctor   0x1400546d0: Component ctor, stamp 0x14015a360, +0x28 = 0, +0x30
//                       (dword) = 0, +0x38 = +0x40 = 0, +0x48 (dword) = 0,
//                       +0x50..+0x68 = 0, flags |= 0xcb
//   dtor   0x140054660: restamp, MemFree(+0x50) + zero +0x50/+0x58 (~herds),
//                       MemFree(+0x38) + zero +0x38/+0x40 (~cells: reverse
//                       declaration order), stamp 0x140162920,
//                       ~Internal. Scalar 0x14000b0a0 = dtor + MemFree on &1
//   table  0x14015a360, 16 slots:
//     [0] 0x14000b0a0 scalar deleting dtor   [1]/[2] TypeName/Type
//     [3] 0x1400544a0 Persist                [4] 0x140054220 Startup (herds)
//     [6] 0x1400541c0 OnAdded (town registry)
//     [7] 0x140054180 OnRemove               [9] 0x140054070 Update
//     [11] 0x140054210 Slot11 (its own `RET`) every other slot = Component's
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Component/Component.h"
#include "Object/exe_type.h"
#include "system/Value.h"   // System::Time
#include "collection/Collection.h"   // Collection::Array (cells, herds)
#include "object/Internal.h"         // Object::InternalPtr<T>
namespace rtcoll { extern const ArraySpec kSpec_PtrZero; }   // 0x180008d60 (Entity/EntityList.h)

class PopulationDescription;
class ComponentEntity;
class Map;   // Game/Map.h

class PopulationComponent final : public Component {
    DECLARE_EXE_TYPE(PopulationComponent)

public:
    PopulationComponent();                                        // 0x1400546d0

    // 0x140054660: the body is empty; ~herds (+0x50) then ~cells (+0x38) free and
    // zero the two arrays in retail's order (reverse declaration order).
    ~PopulationComponent() override = default;                    // [0]  0x14000b0a0
    void Persist(IO::Stream& stream) override;                          // [3]  0x1400544a0
    void Startup() override;                                      // [4]  0x140054220
    void OnAdded() override;                                      // [6]  0x1400541c0
    void OnRemove() override;                                     // [7]  0x140054180
    void Update(const System::Time* time) override;                             // [9]  0x140054070
    void Slot11() override;                                       // [11] 0x140054210 (RET)
    float HerdCellFood(int cell) const;                           // 0x140053f20 (body: herdcomponent.cpp)

    // One habitability cell, 0xC bytes (Persist 0x1400544a0 serializes +0, +8, +4).
    struct Cell {
        int32_t density;    // +0x00 ScanHabitability's answer (0: uninhabitable)
        int32_t smoothed;   // +0x04 the smoothed density (SmoothDensity)
        int32_t minimum;    // +0x08
    };

    void SmoothDensity();                                         // 0x1400538c0 5x5 kernel over the cells
    void ScanHabitability(Map* map, int cellIndex);               // 0x140053b50 one cell's block of map tiles
    void SetCellCount(int newCount);                              // 0x14000b0d0 Array<Cell>::SetSize on +0x38
    int   PickTargetCell(int herdCell) const;                     // 0x140053ca0

    // PUBLIC while the ported bodies read them (B38 decision 1). Persist 0x1400544a0:
    // ExternalPtr<PopulationDescription> +0x28, int +0x30, int +0x48, the cell array
    // (+0x38), the herd array (+0x50, InternalPtr<ComponentEntity> elements), Time
    // +0x60, int +0x68, int +0x6c. Both arrays are freed by the retail dtor
    // (the two inlined ~Arrays). The cells grow only through the hand-written
    // SetCellCount (0x14000b0d0); the herds through 0x1400032e0 / 0x140003110
    // (stride 8, new tail ZEROED, no element destroy): kSpec_PtrZero.
    PopulationDescription* description; // +0x28 the component's +0x18, cached by Startup
    int32_t  habitatDim;   // +0x30 cells per edge (map size / _habitatBlock)
    uint8_t  _pad34[4];    // +0x34
    Collection::Array<Cell> cells;   // +0x38 (count +0x40, capacity +0x44)
    int32_t  herdDim;      // +0x48 herd cells per edge (map size / _herdBlock)
    uint8_t  _pad4C[4];    // +0x4C
    using HerdList = Collection::Array<Object::InternalPtr<ComponentEntity>, Collection::DefaultAllocator,
                                       Collection::ExplicitSpec<&rtcoll::kSpec_PtrZero>>;
    HerdList herds;        // +0x50 the spawned herds (count +0x58, capacity +0x5C)
    System::Time smoothTimer; // +0x60 SmoothDensity every 300 s
    int32_t  scanIndex;    // +0x68 the round-robin habitability cursor
    int32_t  unusedSerializedInt;   // +0x6C serialized, not read by the ported bodies
};

static_assert(sizeof(PopulationComponent) == 0x70, "PopulationComponent is 0x70 (create 0x14000af50)");
static_assert(offsetof(PopulationComponent, _pad34) == 0x34, "PopulationComponent::_pad34");
static_assert(offsetof(PopulationComponent, _pad4C) == 0x4C, "PopulationComponent::_pad4C");
static_assert(offsetof(PopulationComponent, herds) + offsetof(PopulationComponent::HerdList, _capacity) == 0x5C, "PopulationComponent::herds capacity");
static_assert(alignof(PopulationComponent) == 8, "PopulationComponent alignment");
static_assert(sizeof(PopulationComponent::Cell) == 0xC, "cell stride 0xC (0x14000b0d0)");
static_assert(offsetof(PopulationComponent::Cell, density) == 0x0, "Cell::density");
static_assert(offsetof(PopulationComponent::Cell, smoothed) == 0x4, "Cell::smoothed");
static_assert(offsetof(PopulationComponent::Cell, minimum) == 0x8, "Cell::minimum");
static_assert(offsetof(PopulationComponent, description) == 0x28, "");
static_assert(offsetof(PopulationComponent, habitatDim) == 0x30, "");
static_assert(offsetof(PopulationComponent, cells) == 0x38, "");
static_assert(offsetof(PopulationComponent, cells) + offsetof(Collection::Array<PopulationComponent::Cell>, _size) == 0x40, "");
static_assert(offsetof(PopulationComponent, cells) + offsetof(Collection::Array<PopulationComponent::Cell>, _capacity) == 0x44, "");
static_assert(offsetof(PopulationComponent, herdDim) == 0x48, "");
static_assert(offsetof(PopulationComponent, herds) == 0x50, "");
static_assert(offsetof(PopulationComponent, herds) + offsetof(PopulationComponent::HerdList, _size) == 0x58, "");
static_assert(offsetof(PopulationComponent, smoothTimer) == 0x60, "");
static_assert(offsetof(PopulationComponent, scanIndex) == 0x68, "");
static_assert(offsetof(PopulationComponent, unusedSerializedInt) == 0x6C, "");
