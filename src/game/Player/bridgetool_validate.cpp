// bridgetool_validate.cpp — BridgeTool path helpers (drag snapping + validation).
//
// Ports the two exe-internal helpers BridgeTool::CalcCost (slot 14) and
// BridgeTool::CalcPath (slot 13) both call. Before this file they were bound via
// ResolveAddr(Module::Exe, ...) to UNREGISTERED addresses, which return 0 in the
// standalone exe — so CalcCost bailed with "return 3" (invalid) on every frame of a
// bridge drag, and CalcPath's own validity gate could never open. Both are pure
// map-grid math with no engine calls, so they port directly.
//
//   BridgeTool_SnapEndCell   0x1400fb710 (sub_b710)
//   BridgeTool_ValidatePath  0x1400fb2d0 (sub_b2d0)
//
// Map-grid layout (mapGrid = gameWorld+0xf8) — identical to the layout the road port
// documents in roadtool_calcpath.cpp:
//   +0x00 gridSize (width) · +0x80 terrain cell array (stride 0x30) · +0xe8 pending BST root
// Terrain cell (stride 0x30):
//   +0x00 occupant entity ptr (0 = empty) · +0x29 terrain type byte · +0x2a road type (0xff = none)
//
// NOTE on the Ghidra listing: the grid pointer decompiles as `int *piVar1`, so its
// `piVar1 + 0x20` is byte offset +0x80 and `piVar1 + 0x3a` is +0xe8 (element-vs-byte
// scaling). The pending-cell BST nodes use left=+0x10 / right=+0x18, which is a DIFFERENT
// node layout from the tool-mode tree's +0x20/+0x28 — two BST shapes exist in this binary.
//
// CreateBridgeDescription masks (self->bridgeDesc, see engine/types/BridgeTool.h):
//   +0x20 maskBody     — terrain the bridge body may span (the water)
//   +0x24 maskApproach — terrain the bridge ends must land on (the banks)
//   +0x2c maskRequired — at least one body cell must carry one of these bits
//   +0x30 maskNeighbor — terrain permitted in the clearance ring around the span
//   +0x34 neighborRadius

#include "BridgeTool.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include <cstdint>
#include <cstdlib>
#include "Player/exe_bodies.h"
#include "Component/CreateBridgeDescription.h"
#include "Game/Map.h"
#include "Action/PathAvl.h"

namespace {

// [0x1401c3dd8]+0xf8, read with no test (0x1400fb715 / 0x1400fb306, row 1180).
inline Map* MapGrid() {
    return GameUpdateLoop::_singleton->m_game;
}

// 1 << (terrainType & 0x1f) — the bit each mask is tested against.
inline uint32_t TerrainBit(const MapCell* cell) {
    return Bits(PathBitOf(cell->terrainType));
}

// The pending/queued-cell BST at Map +0xE8 (AvlNode28: key +0x00, left +0x10,
// right +0x18). Returns true if cellIdx is present, which makes the cell unavailable.
bool PendingContains(const Map* mapGrid, int cellIdx) {
    const AvlNode28* node = mapGrid->pendingRoadCells.root;
    while (node != nullptr) {
        if (cellIdx < node->key) {
            node = node->left;
        } else {
            if (cellIdx <= node->key) return true;   // exact hit
            node = node->right;
        }
    }
    return false;
}

// ValidatePath's phase-1 span scan (0x1400fb2d0): bank -> water -> bank.
enum class SpanScan : int {
    NearBank = 0,   // approach terrain before the body
    Body     = 1,   // the body (water) cells
    FarBank  = 2,   // approach terrain after the body
};

}  // namespace

// ============================================================
// BridgeTool::SnapEndCell (0x1400fb710)
// ============================================================
// Constrains the drag's end cell to a straight run from the start cell: whichever of
// the row/column deltas is smaller collapses, so a bridge is always axis-aligned.
// Ties (|dRow| == |dCol|) snap to a horizontal run, matching retail's `<=`.
int BridgeTool::SnapEndCell(int endCell) const {
    // 0x1400fb715-0x1400fb72d: the map and its width read, then IDIV, with no test.
    Map* mapGrid = MapGrid();
    const int width = mapGrid->size;

    const int startCell = this->startCell;   // DragTool +0x2c
    const int startRow = startCell / width;
    const int startCol = startCell % width;
    const int dCol = startCol - (endCell % width);
    const int dRow = startRow - (endCell / width);

    if (std::abs(dRow) <= std::abs(dCol))
        return (endCell % width) + width * startRow;   // keep the start row (horizontal run)
    return startCol + width * (endCell / width);       // keep the start col (vertical run)
}

// ============================================================
// BridgeTool::ValidatePath (0x1400fb2d0)
// ============================================================
// Walks the axis-aligned span between startCell and endCell and decides whether a bridge
// may be built there, writing the body length (in cells) to outLength.
//
// Three phases, all faithful to the original:
//   1. Span scan — a state machine that must see approach terrain, then body terrain,
//      then approach terrain again (bank -> water -> bank), with at least one body cell
//      carrying a maskRequired bit. Any occupied cell invalidates.
//   2. Locate the first body cell (start of the span proper).
//   3. Clearance ring — every cell within neighborRadius of the body must be free
//      (no occupant, no road, not queued in the pending BST) and, for offsets != 0,
//      must carry a maskNeighbor terrain bit.
//
// Returns nonzero when the bridge is placeable. outLength may be null.
uint32_t BridgeTool::ValidatePath(int startCell, int endCell, int* outLength) const {
    uint32_t result = 1;
    if (outLength) *outLength = 0;

    // 0x1400fb306-0x1400fb31e, 0x1400fb39b: the map, its width and the cell array read
    // with no test; the description at [this+0x38] likewise (row 1180).
    Map* mapGrid = MapGrid();
    const int width = mapGrid->size;
    const MapCell* terrainBase = mapGrid->cells.GetData();

    const CreateBridgeDescription* desc = bridgeDesc;
    const uint32_t maskBody     = desc->_placeBits;
    const uint32_t maskApproach = desc->_invalidBits;
    const uint32_t maskRequired = desc->_requiredBits;
    const uint32_t maskNeighbor = desc->_edgeBits;
    const int      radius       = desc->_edgeWidth;

    // Normalise the span to [minRow..maxRow] x [minCol..maxCol].
    int startRow = startCell / width, startCol = startCell % width;
    int endRow   = endCell   / width, endCol   = endCell   % width;
    int minCol = startCol, maxCol = endCol;
    if (endCol < startCol) { minCol = endCol; maxCol = startCol; }
    int minRow = startRow, maxRow = endRow;
    if (endRow < startRow) { minRow = endRow; maxRow = startRow; }

    // ---- Phase 1: bank -> water -> bank state machine over the span ----
    SpanScan state      = SpanScan::NearBank;
    int  approachNear   = 0;
    int  bodyCount      = 0;
    int  approachFar    = 0;
    int  savedBodyCount = 0;
    bool sawRequired    = false;
    bool spanOk         = false;

    if (minRow <= maxRow) {
        for (int row = minRow; row <= maxRow; row++) {
            if (minCol <= maxCol) {
                const MapCell* cell = &terrainBase[width * row + minCol];
                for (int n = maxCol - minCol + 1; n > 0; n--, cell++) {
                    if (cell->blocker != nullptr)
                        result = 0;                     // cell already occupied
                    const uint32_t bit = TerrainBit(cell);

                    if (state == SpanScan::NearBank) {
                        if (maskBody & bit) {
                            state = SpanScan::Body;
                            sawRequired = sawRequired || ((maskRequired & bit) != 0);
                            bodyCount++;
                        } else if (maskApproach & bit) {
                            approachNear++;
                        } else {
                            result = 0;
                        }
                    } else if (state == SpanScan::Body) {
                        if (maskApproach & bit) {
                            state = SpanScan::FarBank;
                            approachFar++;
                        } else if (maskBody & bit) {
                            sawRequired = sawRequired || ((maskRequired & bit) != 0);
                            bodyCount++;
                        } else {
                            result = 0;
                        }
                    } else {  // SpanScan::FarBank
                        if (maskApproach & bit) approachFar++;
                        else                    result = 0;
                    }
                }
                savedBodyCount = bodyCount;
            }
        }
        spanOk = (state == SpanScan::FarBank) && (approachNear != 0) && (bodyCount != 0) && (approachFar != 0);
    }
    if (!spanOk)      result = 0;
    if (!sawRequired) result = 0;
    if (outLength)    *outLength = bodyCount;

    // ---- Phase 2: locate the first body cell in the span ----
    int bodyRow = minRow, bodyCol = minCol;
    {
        int foundIdx = -1;
        int scanCol = minCol, scanRow = minRow;
        for (int row = minRow; row <= maxRow; row++) {
            int hitIdx = -1;
            int hitCol = scanCol, hitRow = scanRow;
            if (minCol <= maxCol) {
                int idx = width * row + minCol;
                for (int col = minCol; col <= maxCol; col++, idx++) {
                    if (maskBody & TerrainBit(&terrainBase[idx])) {
                        hitIdx = idx; hitCol = col; hitRow = row;
                        break;
                    }
                }
            }
            if (hitIdx >= 0) { foundIdx = hitIdx; scanCol = hitCol; scanRow = hitRow; break; }
        }
        (void)foundIdx;
        bodyCol = scanCol;
        bodyRow = scanRow;
    }

    // ---- Phase 3: clearance ring around the body span ----
    // The body extends from (bodyRow, bodyCol) by savedBodyCount along whichever axis is
    // not already at its maximum — matching retail's `(col != maxCol) * len + col` form.
    const int spanEndCol = (bodyCol != maxCol) ? bodyCol + savedBodyCount : bodyCol;
    const int spanEndRow = (bodyRow != maxRow) ? bodyRow + savedBodyCount : bodyRow;

    for (int row = bodyRow, rowBias = -bodyRow; row <= spanEndRow; row++, rowBias--) {
        if (bodyCol > spanEndCol) continue;
        for (int col = bodyCol; col <= spanEndCol; col++) {
            for (int off = -radius; off <= radius; off++) {
                // A horizontal body walks the ring along the row; a degenerate span
                // (single column) walks it along the column instead.
                int probeRow = off + row;
                int probeCol = col;
                if (bodyCol == spanEndCol) {
                    probeRow = row;
                    probeCol = rowBias + off + row + col;
                }
                const int idx = width * probeRow + probeCol;
                if (idx < 0) { result = 0; continue; }
                const MapCell* cell = &terrainBase[idx];
                const uint32_t  bit  = TerrainBit(cell);

                if (off != 0 && (maskNeighbor & bit) == 0)
                    result = 0;

                const bool free = (cell->blocker == nullptr)
                               && ((bit & Bits(PathBits::Fast)) == 0)
                               && ((bit & Bits(PathBits::Faster)) == 0)
                               && (cell->roadType == MapCell::kNoRoadBatch);
                if (!free || PendingContains(mapGrid, idx))
                    result = 0;
            }
        }
    }

    return result;
}
