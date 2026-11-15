#pragma once
// ============================================================================
// src/game/Component/StorageDisplayComponent.h — ENDGAME B.3.8 block VIS
//
// `StorageDisplayComponent` as a real C++ class with a compiler-generated
// vtable (B38_RULES.md D1-D4).
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §6):
//   reflected "StorageDisplayComponent", parent "Component", ti 0x1401c4950
//   create 0x14000c770: MemAlloc(0x38, 8) + ctor 0x140056a00
//   ctor   0x140056a00: Component ctor, stamp 0x14015a780, +0x28 = +0x30 = 0,
//                       flags |= 0xcb
//   dtor   0x1400569b0: FUN_14000c9a0(+0x28) (~Array<Pile>, the member's dtor),
//                       restamp, ~Internal
//   table  0x14015a780, 16 slots:
//     [0]  0x14000c940 scalar deleting dtor  [1]/[2] TypeName/Type
//     [3]  0x140056970 Persist               [4] 0x140056930 own `RET`
//     [6]  0x140056730 OnAdded (Ghidra "Draw": pile grid layout)
//     [7]  0x1400566b0 OnRemove (Ghidra "OnCreate": remove the pile meshes)
//     [9]  0x1400566a0 Update (`JMP 0x1400563e0`)
//     [11] 0x140056940 Slot11 (Ghidra "OnUpdate2", 28 B)
//     every other slot = Component's
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Component/Component.h"
#include "Object/exe_type.h"
#include "graphics/Mesh.h"   // Graphics::MeshInstance (the pile element)
#include "collection/Collection.h"   // Collection::Array<Pile> (+0x28)

class ComponentDescription;   // Component/ComponentDescription.h

class StorageDisplayComponent final : public Component {
    DECLARE_EXE_TYPE(StorageDisplayComponent)

public:
    StorageDisplayComponent();                                    // 0x140056a00

    // 0x1400569b0: restamp, FUN_14000c9a0(+0x28) = ~Array<Pile> (each pile's
    // ~MeshInstance in ascending order, MemFree, the header zeroed), restamp,
    // ~Internal. All of it is the member destruction of `piles`.
    ~StorageDisplayComponent() override = default;                // [0]  0x14000c940
    void Persist(IO::Stream& stream) override;                          // [3]  0x140056970
    void Startup() override;                                      // [4]  0x140056930 own RET
    void OnAdded() override;                                      // [6]  0x140056730
    void OnRemove() override;                                     // [7]  0x1400566b0
    void Update(const System::Time* time) override;                             // [9]  0x1400566a0
    void Slot11() override;                                       // [11] 0x140056940

    // One pile on the footprint, 0x128 bytes: the MeshInstance and its scene id
    // (Graphics::Scene::AddObject's answer, -1 while not in the scene).
    // The element operations are 0x14000ca10's (Array<Pile>::SetSize): a new tail
    // element is MeshInstance() + sceneId = -1, a realloc copy is the MeshInstance
    // copy ctor (import 0x1401514a0) + sceneId; neither writes the pad.
    struct Pile {
        Pile() : sceneId(-1) {}
        Pile(const Pile& src) : mesh(src.mesh), sceneId(src.sceneId) {}
        Pile& operator=(const Pile&) = delete;
        Graphics::MeshInstance mesh;   // +0x000
        int32_t  sceneId;              // +0x120
        uint8_t  _pad124[4];           // +0x124
    };
    using PileArray = Collection::Array<Pile>;

    void Refresh();                                               // 0x1400563e0 the piles from the StorageComponent's items
    void SetPileCount(int newCount) { piles.SetSize(newCount); }  // 0x14000ca10 Array<Pile>::SetSize on +0x28
    void SetPileMesh(int index, float fraction, ComponentDescription* material);  // 0x1400562b0 (null material: clear)

    // PUBLIC while the ported bodies read them (B38 decision 1).
    PileArray piles;       // +0x28 data, +0x30 count, +0x34 capacity (ctor zeroes all three)
};

static_assert(sizeof(StorageDisplayComponent::Pile) == 0x128, "pile stride 0x128 (0x14000ca10)");
static_assert(offsetof(StorageDisplayComponent::Pile, mesh) == 0x0, "Pile::mesh");
static_assert(offsetof(StorageDisplayComponent::Pile, _pad124) == 0x124, "Pile::_pad124");
static_assert(offsetof(StorageDisplayComponent::Pile, sceneId) == 0x120, "");

static_assert(sizeof(StorageDisplayComponent) == 0x38, "StorageDisplayComponent is 0x38 (create 0x14000c770)");
static_assert(alignof(StorageDisplayComponent) == 8, "StorageDisplayComponent alignment");
static_assert(offsetof(StorageDisplayComponent, piles) == 0x28, "");
static_assert(offsetof(StorageDisplayComponent, piles) + offsetof(StorageDisplayComponent::PileArray, _size) == 0x30, "");
static_assert(offsetof(StorageDisplayComponent, piles) + offsetof(StorageDisplayComponent::PileArray, _capacity) == 0x34, "");
