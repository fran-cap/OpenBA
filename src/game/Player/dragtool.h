#pragma once
// ============================================================================
// src/game/Player/dragtool.h — ENDGAME B.3.8 TOOL (B38_RULES.md §2.1, D1-D5)
//
// `DragTool`, the abstract base of the click-and-drag tools (Bridge, Road,
// Removal, Fenced, DragPlacement), as a REAL C++ class with a compiler vtable.
// (This header is the class's, named after it; the file system is case-
// insensitive, so it keeps its existing lowercase spelling.)
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §1, bytes read 2026-09-26):
//   reflected "DragTool", parent "Tool", ti 0x1401c6da0, create = 0x140003730
//   ctor   inlined into every leaf ctor (e.g. 0x1400fb890): the Tool part, then
//          stamp 0x140161c10; `+0x28 word = 0` (dragState, singleClick);
//          `+0x2c qword = -1`; `+0x34 dword = -1` (start/end/last cells)
//   table  0x140161c10, 15 slots:
//   [0]  0x14002da20 scalar deleting dtor: stamp 0x140161c10, stamp
//        0x140161f10, `~Internal`, MemFree on &1 — ~DragTool has no body
//   [1]  0x14002da10 TypeName   [2] 0x14002da00 Type
//   [3]-[5], [7], [8] = Tool's (Internal::Persist / pure)
//   [6]  0x1400fc6b0 OnKey — the shared drag lifecycle (dragtool.cpp)
//   [9]  0x1400fc640 OnCancel — cancel an active drag, 1 = keep the tool
//   [10], [11] = Tool's
//   [12] 0x1400fc9d0 `MOV EAX,5; RET` (cursor)
//   [13], [14] _purecall — CalcPath (commit) / CalcCost (preview), introduced
//
// DOORS (B38 D5 (b) rows): reg_code_lane6.cpp / reg_player_drag.cpp register
// the names `DragTool_OnKey` (0x1400fc6b0) and `DragTool_CancelDrag_Impl`
// (0x1400fc640), so those two stay as one-line doors onto the members, which
// hold the bodies (ENDGAME B.3.8 callers CB4).
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Player/Tool.h"
#include "Object/exe_type.h"   // DECLARE_EXE_TYPE (ENDGAME B.3.9 RF)

// DragTool::dragState (+0x28). Retail stores only these two values; singleClick (+0x29) is a
// separate 0/1 flag that only means something while Dragging (released on the start cell).
enum class DragState : uint8_t {
    Idle     = 0,   // no drag in progress
    Dragging = 1,   // start cell picked, end cell follows the cursor
};

class DragTool : public Tool {
    DECLARE_EXE_TYPE(DragTool)

public:
    // ---- the 15 slots, in retail order (declared here: the ones DragTool fills)
    // no body in retail
    ~DragTool() override = default;                                    // [0]  0x14002da20
    // [1] TypeName 0x14002da10 and [2] Type 0x14002da00: DECLARE_EXE_TYPE (retail `MOV RAX,[ti+8]` / `MOV EAX,[ti]`).
    char          OnKey(char active) override;                         // [6]  0x1400fc6b0
    char          OnCancel() override;                                 // [9]  0x1400fc640
    int           GetCursor() const override { return 5; }             // [12] 0x1400fc9d0
    // [13] commit the drag between two cells; AL = success.
    virtual bool  CalcPath(int startCell, int endCell) = 0;            // [13] pure
    // [14] evaluate the drag: returns the segment count and fills four
    // out-values (DragTool::OnKey reads them as path type, tile H, tile W and
    // an extra word; each leaf names them after its own use).
    virtual int   CalcCost(int startCell, int endCell, uint32_t* out0,
                           int* out1, int* out2, int* out3) = 0;       // [14] pure

    // ---- the 0x10 bytes DragTool adds, retail order -------------------------
    DragState dragState;    // +0x28 Idle / Dragging
    uint8_t  singleClick;   // +0x29 1 = released on the start cell (confirm on next click)
    uint8_t  _pad2A[2];     // +0x2a never written by retail (zeroed here)
    int32_t  startCell;     // +0x2c -1 = invalid
    int32_t  endCell;       // +0x30 -1 = invalid
    int32_t  lastEndCell;   // +0x34 change detection for the tick sound

protected:
    DragTool() : dragState(DragState::Idle), singleClick(0), _pad2A{0, 0},
                 startCell(-1), endCell(-1), lastEndCell(-1) {}
};


static_assert(sizeof(DragTool) == 0x38, "DragTool is 0x38 bytes");
static_assert(offsetof(DragTool, _pad2A) == 0x2A, "DragTool::_pad2A");
static_assert(alignof(DragTool) == 8, "DragTool is 8-aligned");
static_assert(sizeof(DragState) == 1, "DragState is the +0x28 byte");
static_assert(offsetof(DragTool, dragState)   == 0x28, "DragTool +0x28 dragState (word store 0x1400fb8cb)");
static_assert(offsetof(DragTool, singleClick) == 0x29, "DragTool +0x29 singleClick");
static_assert(offsetof(DragTool, startCell)   == 0x2c, "DragTool +0x2c startCell (qword -1 at 0x1400fb8cf)");
static_assert(offsetof(DragTool, endCell)     == 0x30, "DragTool +0x30 endCell");
static_assert(offsetof(DragTool, lastEndCell) == 0x34, "DragTool +0x34 lastEndCell (dword -1 at 0x1400fb8d7)");
