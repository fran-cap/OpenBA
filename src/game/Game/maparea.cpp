// src/game/Game/maparea.cpp
// Map rectangle tests read by ClearAreaComponent::IsDone (0x140118440,
// Component/clearareacomponent_states.cpp). They read only the cell array (+0x80) and the
// road-cell tree (+0xE8), so the host unit tests link this file
// (tests/unit/clear_area_road_test.cpp).

#include "Game/Map.h"
#include "Action/PathAvl.h"   // AvlNode28 (Map +0xE8)
#include "Entity/ComponentEntity.h"   // InternalPtr<ComponentEntity>::Get needs the class complete

// ============================================================
// 0x14009d860 — every cell of the rect is free of road: no road terrain (types 0 / 1),
// no road pending on it (+0x2A == 0xff) and not in the road-cell tree (+0xE8).
// ============================================================
bool Map::IsGroundClear(int x1, int y1, int w, int h) const {
    const MapCell* tiles = cells.GetData();
    const AvlNode28* bstRoot = pendingRoadCells.root;
    for (int row = y1; row < y1 + h; ++row) {
        for (int col = x1; col < x1 + w; ++col) {
            int cell = size * row + col;
            const MapCell& t = tiles[static_cast<uintptr_t>(cell)];
            uint32_t f = Bits(PathBitOf(t.terrainType));   // +0x29
            if ((f & Bits(PathBits::Fast)) || (f & Bits(PathBits::Faster)) || t.roadType != MapCell::kNoRoadBatch)
                return false;
            for (const AvlNode28* node = bstRoot; node;) {
                int nk = node->key;
                if (cell < nk)      node = node->left;    // +0x10
                else if (cell <= nk) return false;   // in the tree
                else                node = node->right;   // +0x18
            }
        }
    }
    return true;
}

// ============================================================
// 0x14009dac0 — every cell of the rect is empty (no occupant entity at +0x08).
// Retail tests only `occ != 0` (row 1272): a foundation claims its own cells only at
// ClearArea state 2 (MapComponent::OnEvent(1) -> AddToMap), after this test, so its
// own entity is never in the rect here. (CountRemaining 0x140118330 is the one that
// skips self; this one does not.)
// ============================================================
bool Map::IsAreaEmpty(int x1, int y1, int w, int h) const {
    const MapCell* tiles = cells.GetData();
    for (int row = y1; row < y1 + h; ++row) {
        for (int col = x1; col < x1 + w; ++col) {
            if (tiles[static_cast<uintptr_t>(size * row + col)].occupant.Get() != nullptr)
                return false;
        }
    }
    return true;
}
