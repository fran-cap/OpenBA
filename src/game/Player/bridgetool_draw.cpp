// bridgetool_draw.cpp — BridgeTool::Draw (vtable +0x40, slot 8) — 0x1400fadc0.
//
// This is the RED/GREEN validity overlay the player sees while dragging a bridge.
// Until now BridgeTool's slot 8 was `Tool_Ret0`, so a bridge drag rendered nothing:
// CalcCost happily reported `valid=1 len=N` every frame and the placement worked, but
// there was no on-map preview at all. The headless harness could not see this — it
// drives CalcCost/CalcPath directly and never renders (docs/history/notes/BRIDGE_COMPLETE_BRIEF.md,
// "What the harness could not tell us").
//
// Ghidra mislabels 0x1400fadc0 as `BridgeTool_OnRemove`; the body is unmistakably a
// draw: it builds a tile array and hands it to the shared tile renderer 0x1401008c0
// (`CharacterComponent_impl_08c0` = fn_TileRender), exactly as RoadTool::Draw
// (0x140105190, ported in roadtool.cpp) does.
//
// Shape of the overlay — two kinds of tile, both pushed into ONE array:
//   * body cells   — every cell of the bounding rect between the drag start and the
//                    snapped end. Green when the path validates AND the cell's terrain
//                    is in (maskBody | maskBodyExtra) AND the cell is unoccupied.
//   * clearance    — only while actually dragging, a ring of `neighborRadius` cells to
//     ring cells    each side of the span (perpendicular to the run), mirroring the
//                    clearance test ValidatePath performs.
//
// Colors are literal in the binary: 0xffff0000 red, 0xff00ff00 green.
//
// Map-grid and CreateBridgeDescription layouts are documented in
// bridgetool_validate.cpp; the small accessors below are the same ones (kept file-local
// to match how roadtool.cpp carries its own copies).
//
// Field used here that ValidatePath does NOT read:
//   +0x28 maskBodyExtra — OR'd with maskBody for the preview's "is this cell part of
//                         the span?" test only. ValidatePath uses maskBody alone.
//
// Argument slots verified against the disassembly rather than the decompiler (this
// project has twice been burned by trusting decompiled argument order):
//   0x1400fadfc  CALL 0x140101270   RCX = self                  -> ScreenToCell
//   0x1400fae0b  CALL 0x1400fb710   RCX = self, EDX = [self+0x30] -> SnapEndCell
//   0x1400fae83  CALL 0x1400fb2d0   RCX = self, EDX = start, R8D = end, R9D = 0
//   0x1400fb27b  CALL 0x1401008c0   RDX = context, R8 = &tiles, R9 = &empty
//                                   (RCX is never loaded — a leftover register; the
//                                    road port passes nullptr for it too)

#include "BridgeTool.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Player/exe_bodies.h"
#include "collection/Collection.h"
#include "Component/CreateBridgeDescription.h"
#include "Game/Map.h"
#include "Action/PathAvl.h"
#include <cstdint>
#include <cstdio>

namespace {

// ---- map-grid accessors (see bridgetool_validate.cpp for the layout) --------
inline Map* MapGrid() {
    GameUpdateLoop* gameWorld = GameUpdateLoop::_singleton;
    if (!gameWorld) return nullptr;
    return gameWorld->m_game;
}
inline uint32_t TerrainBit(const MapCell* cell) {
    return Bits(PathBitOf(cell->terrainType));
}
// Pending/queued-cell BST at Map +0xE8 (AvlNode28: key +0x00, left +0x10, right +0x18).
bool PendingContains(const Map* mapGrid, int cellIdx) {
    const AvlNode28* node = mapGrid->pendingRoadCells.root;
    while (node != nullptr) {
        if (cellIdx < node->key) {
            node = node->left;
        } else {
            if (cellIdx <= node->key) return true;
            node = node->right;
        }
    }
    return false;
}

// ---- tile overlay array (ToolTileRecord, Player/Tool.h) ---------------------
//   variant: 1 = the cell's terrain is in the bridge body mask, else 0; angle: 0.
// ENDGAME B.3.2 step 2: the tile list is a real `Collection::Array` (stride 0x14,
// align 4, byteCopy); PushTileEntry only ever appends ONE element, so its
// `(count == 0) ? n : n*3` capacity rule is exactly SetSizeCore's. One
// out-of-memory-only difference: the old body returned early when MemAlloc came back
// null (count untouched), while SetSizeCore stores the new size and skips the null
// element address instead.
using TileArray = Collection::Array<ToolTileRecord>;

void PushTileEntry(TileArray* arr, const ToolTileRecord* entry) {
    arr->SetSize(arr->_size + 1, *entry);
}

constexpr uint32_t kRed   = 0xffff0000u;
constexpr uint32_t kGreen = 0xff00ff00u;

// FUN_140101270 — ScreenToCell (ray-picks the map cell under the cursor):
// PlayerRay_ScreenToCell_101270 (Player/exe_bodies.h), called by name.

}  // namespace

// ============================================================
// BridgeTool::Draw (vtable +0x40, Slot8) — 0x1400fadc0
// ============================================================
void BridgeTool::Draw(Video::Context* context, char skip) {
    if (skip != '\0') return;

    // ---- 1. Resolve the span being previewed -------------------------------
    int startCell, endCell;
    if (dragState == DragState::Idle) {
        // Not dragging: preview the single cell under the cursor.
        startCell = PlayerRay_ScreenToCell_101270(this);
        endCell   = startCell;
    } else {
        startCell = this->startCell;
        endCell   = SnapEndCell(this->endCell);   // 0x1400fb710
    }
    if (startCell < 0) return;
    if (endCell < 0) endCell = startCell;

    // No test of the map, its size, its cells or the description (0x1400fadc0 reads
    // [GameWorld+0xf8], its +0x00 and +0x80, and this +0x38 unguarded).
    Map* mapGrid = MapGrid();
    const int width = mapGrid->size;
    const MapCell* terrainBase = mapGrid->cells.GetData();

    const CreateBridgeDescription* desc = bridgeDesc;

    const uint32_t maskBody      = desc->_placeBits;
    const uint32_t maskApproach  = desc->_invalidBits;
    const uint32_t maskBodyExtra = desc->_startEndBits;
    const uint32_t maskNeighbor  = desc->_edgeBits;
    const int      radius        = desc->_edgeWidth;
    const uint32_t maskSpan      = maskBody | maskBodyExtra;

    // ---- 2. Bounding rect of the span --------------------------------------
    const int startRow = startCell / width, startCol = startCell % width;
    const int endRow   = endCell   / width, endCol   = endCell   % width;

    const int minCol = (endCol < startCol) ? endCol : startCol;
    const int maxCol = (endCol < startCol) ? startCol : endCol;
    const int minRow = (endRow < startRow) ? endRow : startRow;
    const int maxRow = (endRow < startRow) ? startRow : endRow;

    // Does the whole span validate? Drives the base color of every body cell.
    const bool pathValid =
        static_cast<int>(ValidatePath(startCell, endCell, nullptr)) != 0;   // 0x1400fb2d0

    const bool isDrag = (startCell != endCell);

    // Retail indexes the terrain array with no bounds test at all (a ring within
    // `radius` of the map edge reads the cells beyond it, as retail does).
    TileArray tiles;

    // ---- 3. Body cells + clearance ring ------------------------------------
    for (int row = minRow; row <= maxRow; row++) {
        for (int col = minCol; col <= maxCol; col++) {
            const int bodyIdx = width * row + col;

            const MapCell* cell = &terrainBase[bodyIdx];
            const uint32_t  bit  = TerrainBit(cell);

            // type: is this cell's terrain part of the bridge body (i.e. water)?
            const uint32_t type = (maskBody & bit) != 0 ? 1u : 0u;

            // `color` deliberately persists across the clearance-ring iterations
            // below — retail reuses one register for both, so a red body cell forces
            // its whole ring red. Faithful, if surprising.
            uint32_t color;
            if (!pathValid || (maskSpan & bit) == 0 ||
                cell->blocker != nullptr) {
                color = kRed;
            } else {
                color = kGreen;
            }
            // Hovering a single cell: any approach (bank) terrain reads green, so the
            // player can see where a bridge is allowed to start.
            if (!isDrag && (maskApproach & bit) != 0) color = kGreen;

            ToolTileRecord entry = {col, row, static_cast<int32_t>(type), color, 0};
            PushTileEntry(&tiles, &entry);

            if (!isDrag) continue;

            // Clearance ring: `radius` cells to each side, perpendicular to the run.
            for (int off = -radius; off <= radius; off++) {
                if (off == 0) continue;

                int ringCol, ringRow;
                if (minCol == maxCol) {          // vertical span -> ring spreads in X
                    ringCol = col + off;
                    ringRow = row;
                } else {                          // horizontal span -> ring spreads in Y
                    ringCol = col;
                    ringRow = row + off;
                }
                const int idx = width * ringRow + ringCol;

                const MapCell* rCell = &terrainBase[idx];
                const uint32_t  rBit  = TerrainBit(rCell);

                const bool free = (rCell->blocker == nullptr)
                               && ((rBit & Bits(PathBits::Fast)) == 0)
                               && ((rBit & Bits(PathBits::Faster)) == 0)
                               && (rCell->roadType == MapCell::kNoRoadBatch);
                if (!free || PendingContains(mapGrid, idx)) color = kRed;

                if (color != kRed)
                    color = ((maskNeighbor & rBit) != 0) ? kGreen : kRed;

                ToolTileRecord rEntry = {ringCol, ringRow, static_cast<int32_t>(type), color, 0};
                PushTileEntry(&tiles, &rEntry);
            }
        }
    }

    // ---- 4. Submit -----------------------------------------------------------
    // Retail passes an (uninitialised) RCX and a second, always-empty tile array in R9.
    Collection::Array<ToolSegmentRecord> empty;
    PlayerTileRender_DrawOverlay_08c0(nullptr, context, &tiles, &empty);   // 0x1401008c0

    // Report the first hover frame and the first real DRAG frame separately — a hover
    // only ever emits one tile, so "it renders" says nothing about the span loop.
    {
        static bool s_loggedHover = false, s_loggedDrag = false;
        bool* slot = isDrag ? &s_loggedDrag : &s_loggedHover;
        if (!*slot && tiles._size > 0) {
            *slot = true;
            int green = 0;
            for (int i = 0; i < tiles._size; i++)
                if (tiles._data[i].color == kGreen) green++;
            fprintf(stderr, "[Bridge] Draw(%s): start=%d end=%d rect=(%d,%d)..(%d,%d) "
                    "valid=%d radius=%d tiles=%d green=%d red=%d render=%p\n",
                    isDrag ? "drag" : "hover", startCell, endCell, minCol, minRow,
                    maxCol, maxRow, (int)pathValid, radius, tiles._size, green,
                    tiles._size - green, (void*)&PlayerTileRender_DrawOverlay_08c0);
            fflush(stderr);
        }
    }

    // `tiles` and `empty` are released by ~Array at scope exit — exactly one
    // destruction per path (no return between their declarations and here).
}
