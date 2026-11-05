// ============================================================================
// tests/unit/ext_terrain_undo_identity_test.cpp -- ext.terrain_edit Undo: the planted resource identity
// ============================================================================
// Review F-242. PlantStroke records each planted resource, and UndoLast removes the cell's occupant
// only while it is that resource ("one that was chopped, mined or replaced since is left alone").
// Every ComponentEntity is MemAlloc(0x48, 8), and the allocator hands a freed block straight back to
// the next allocation of that size: a tree a woodcutter chops and the sapling a forester then plants
// on the same cell can share an address. The address alone is not the identity; the object's
// generation (Object::Internal::_generation, InternalFactory::_random.Rand() at birth) tells the two
// apart.
// ============================================================================
#include "unit.h"
#include "ext/terrain_edit/terrain_undo.h"
#include "system/MemoryAllocator.h"

#include <cstdint>
#include <vector>

UNIT_TEST(ext_terrain_undo_a_replanted_cell_is_not_the_planted_entity) {
    using namespace Ext::TerrainEdit;
    RtSystemAllocatorInstallPlatform();
    System::MemoryAllocator a{};
    a.minBin = 0xbf;
    CHECK(a.Startup(1, 0));

    // a town's worth of live 0x48 entities around the planted one
    std::vector<void*> town;
    for (int i = 0; i < 64; ++i) town.push_back(a.Allocate(0x48, 8));
    void* planted = a.Allocate(0x48, 8);   // PlantAt: MemAlloc(0x48, 8)
    for (int i = 0; i < 64; ++i) town.push_back(a.Allocate(0x48, 8));
    const uint32_t plantedGeneration = 0x1234abcdu;

    const int cell = 1234;
    UndoStack u;
    u.BeginStroke();
    u.RecordPlanted(cell, reinterpret_cast<uintptr_t>(planted), plantedGeneration);   // PlantStroke
    u.CommitStroke();

    a.Free(planted);                       // a woodcutter chops the tree
    void* sapling = a.Allocate(0x48, 8);   // a forester plants on that cell
    const uint32_t saplingGeneration = 0x9e3779b9u;
    CHECK(sapling == planted);             // the allocator reuses the block: the address alone is no identity

    Stroke s;
    CHECK(u.Pop(s));
    CHECK(s.planted.size() == 1);
    CHECK(s.planted[0].cell == cell);
    // UndoLast's test on the cell's occupant
    CHECK(!s.planted[0].IsStanding(reinterpret_cast<uintptr_t>(sapling), saplingGeneration));
    // the planted tree itself, still standing, is removed
    CHECK(s.planted[0].IsStanding(reinterpret_cast<uintptr_t>(planted), plantedGeneration));
    // no occupant, or another entity
    CHECK(!s.planted[0].IsStanding(0, plantedGeneration));
    CHECK(!s.planted[0].IsStanding(reinterpret_cast<uintptr_t>(town[0]), plantedGeneration));

    a.Free(sapling);
    for (void* p : town) a.Free(p);
    a.Shutdown();
}
