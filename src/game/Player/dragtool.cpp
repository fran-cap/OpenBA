// src/game/Player/dragtool.cpp
// Clean reimplementation of DragTool vtable methods from Application-x64.exe.
//
// DragTool (ABSTRACT, 0x38 bytes) — base class for click-and-drag placement tools.
// Extends Tool (0x28 bytes), vtable at 0x140161c10 (15 slots).
//
// Sub-classes: BridgeTool, RoadTool, WallTool, etc.
// DragTool::OnKey implements the shared drag lifecycle:
//   1. key==0 (key up): close dialog if open, check Released -> reset dragState
//   2. key!=0 && dragState==Idle: check Pressed -> pick start cell, open dialog
//   3. key!=0 && dragState==Dragging: update end cell, call virtual BuildPath,
//      update UI via operator_impl_fc60, handle release -> commit or cancel
//
// Decompiled from Application-x64.exe.

#include "Player/dragtool.h"
#include "Object/factory_registration.h"
#include <new>
#include "math/Math.h"
#include "volume/Volume.h"
#include "ui/DialogInstance.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "system/String.h"
#include "Player/exe_bodies.h"
#include <cstdint>
#include <cstdio>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "ui/UserInterface.h"   // ENDGAME B.3.6 UI callers (K4)
#include "input/Input.h"        // ENDGAME B.3.7 callers (K3)
#include "Game/inputbindings.h"   // GameKey (the KeyMap binding ids)
#include "Object/exe_type.h"   // DEFINE_EXE_ABSTRACT_TYPE
#include "Game/PlayerToolManager.h"
#include "Component/ToolbarDescription.h"
#include "Component/ComponentDescription.h"   // FindDescription<T> (0x140005160)
#include "Entity/ComponentEntity.h"   // B4 S4 RT2: InternalPtr<ComponentEntity>::Get() needs the class complete

// ============================================================
// DragTool members (dragtool.h). ENDGAME B.3.8 TOOL: the hand-rolled
// `DragTool_Dtor` (it restamped two UNREGISTERED tables, i.e. stored a null
// vptr, B38 D9 #3) is gone; the compiler's dtor chain restamps the real ones.
// ============================================================

// [1] TypeName / [2] Type, the record and retail's abstract factory (create =
// the logging stub, in-place = null): ENDGAME B.3.9 RF.
DEFINE_EXE_ABSTRACT_TYPE(DragTool, "Tool")

// [6] 0x1400fc6b0 OnKey and [9] 0x1400fc640 OnCancel: bodies below.

// ============================================================
// Exe helpers, called by name (Player/exe_bodies.h):
//   0x140101270 PlayerRay_ScreenToCell_101270 -- ray-picks the map cell under the
//               cursor: row*gridSize + col, or -1 if off-map
//   0x1400ff9e0 ToolUI_OpenToolDialog_ff9e0 -- closes an existing dialog, loads
//               "Dialog/CreateBar.rsc", creates the DialogInstance with its ToolUI
//               child, adds it to the UI manager. Args: (dialogIBP_ptr, retval_ptr,
//               descriptionPtr)
//   0x1400ffbf0 DragTool_PlayTickSound_ffbf0 -- the "tick" when endCell changes
//               during a drag (a timer at +0x48 on the IBP storage stops rapid-fire)
//   0x140005160 DragTool_FindDescription_5160 -- the description (DAT_1401c7f58)
//               search; the found pointer lands in retval
//   0x1400ffc60 UpdateToolUI_fc60 (operator_impl_fc60) -- cost text, status text,
//               icon tint and screen position of the placement dialog
//   0x1400ff980 DragTool_CloseToolDialog_ff980 -- closes the dialog if open, nulls
//               the IBP
//   0x1401000f0 DragTool_PlayPlaceSound_1000f0 / 0x140100140
//   DragTool_PlayErrorSound_100140 -- the place / error sounds (sound ids at
//               g_pKeyMapSingleton + 0x88 / + 0x90)
// All eight are registered, so the SA no-op fallbacks that used to stand in for a
// null resolve are gone with the resolver.
// Import slot 0x1401513f8 (Volume::Box::Box) is NOT filled in standalone (a STUB):
// its reader below is its null arm.
// ============================================================

// ============================================================
// Helper: get KeyMap from external reference
// ============================================================
// (Ghidra shows: MOV RAX,[0x140151738]; MOV R8,[RAX]; MOV R14,[R8])
static inline const Input::KeyMap* GetKeyMap() {
    return Input::Configuration::_singleton->_keyMap;
}

// ============================================================
// DragTool::OnKey — Main input handler (vtable +0x30, Slot6)
// ============================================================
// Original: 0x1400fc6b0 (217 instructions)
//
// The key parameter:
//   key == 0: key-up event (mouse button released)
//   key != 0: key-down / held event (mouse button pressed/held)
//
// State machine:
//   dragState==Idle:
//     On key-down with Pressed(0,5): pick start cell, set startCell=endCell,
//     if valid: set dragState=Dragging, open dialog
//
//   dragState==Dragging:
//     Ensure dialog is open, pick current cell, update endCell
//     If endCell changed: play tick, update lastEndCell
//     Call virtual CalcCost (+0x70) for segment count + path type
//     Find description, extract cost string
//     Call UpdateToolUI for UI overlay
//     On release: check singleClick behavior
//
//   key==0: close dialog if open, if Released(0,5): reset dragState+singleClick
//
// The owner (Tool +0x10, the PlayerToolManager) holds the open tool dialog at its
// +0x00; the original re-reads [self+0x10] at each use site, as below.
char DragTool::OnKey(char key) {
    const Input::KeyMap* keyMap = GetKeyMap();

    if (key == 0) {
        // ---- Key-up event: cleanup ----

        // Close dialog if the manager holds one
        // Original: CMP [RBX], 0 — dereferences the manager's +0x00
        if (dialogPtr != nullptr && dialogPtr->field_0x00 != nullptr) {
            Object::InternalPtr<UI::DialogInstance> tempIBP(dialogPtr->field_0x00);
            GameUpdateLoop::_singleton->m_userInterface->CloseDialog(&tempIBP);
            dialogPtr->field_0x00 = nullptr;   // write the null IBP back
        }

        // Check if the primary action button was released
        // ENDGAME B.3.7: fn_KeyMap_Released was bound only to the unregistered exe
        // slot 0x1401528f0 and was always null, so this reset never ran; the member
        // call restores retail's key-up path (CALLER_SURFACE.md 6, defect 1).
        if (keyMap->Released(0, ToUnderlying(GameKey::MouseLeft))) {
            // Reset both dragState and singleClick (original does MOV word [self+0x28], 0)
            dragState = DragState::Idle;
            singleClick = 0;
        }
    }
    else if (dragState == DragState::Idle) {
        // ---- Idle state: check for drag start ----
        // KeyMap::Pressed at dispatch 0x140151758 — checks if button newly pressed
        bool pressed = keyMap->Pressed(0, ToUnderlying(GameKey::MouseLeft));
        if (pressed) {
            int cell = PlayerRay_ScreenToCell_101270(keyMap);
            startCell = cell;
            endCell = cell;

            if (cell >= 0) {
                // Begin dragging: set state, open placement dialog
                dragState = DragState::Dragging;
                singleClick = 0;
                UI::DialogInstance* retval;
                // Original: FUN_1400ff9e0(*(longlong**)(self+0x10), &retval, *(self+0x20))
                dialogPtr->OpenToolDialog(&retval, descriptionPtr);
            }
        }
    }
    else {
        // ---- Dragging state: update path and handle release ----

        // Ensure dialog is open (re-open if it was somehow closed)
        // Original: CMP [manager], 0
        if (dialogPtr->field_0x00 == nullptr) {
            UI::DialogInstance* tempStr;
            dialogPtr->OpenToolDialog(&tempStr, descriptionPtr);
        }

        // Pick current cell under cursor
        int currentCell = PlayerRay_ScreenToCell_101270(dialogPtr);
        if (currentCell >= 0) {
            endCell = currentCell;
        }

        // If endCell changed, play tick sound and update lastEndCell
        if (lastEndCell != endCell) {
            lastEndCell = endCell;
            dialogPtr->PlayTickSound();
        }

        // ---- Virtual CalcCost (slot 14, +0x70) ----
        // (startCell, endCell, &pathType, &tileH, &tileW, &extraParam) -> segment count
        // pathType: 0=normal, 1=single, 2=invalid
        uint32_t pathType[2] = {0};
        int tileH[2] = {0};
        int tileW[2] = {0};
        int extraParam = 0;
        int segmentCount = CalcCost(startCell, endCell, pathType, tileH, tileW, &extraParam);

        // ---- Cost tooltip (FindDescription -> cost string -> UpdateToolUI) ----
        System::String costString;
        {
            // 0x1400fc80d FindDescription, then 0x1400fc816 CMP R15D,[RCX+0x58]: the result is
            // read with no test, as here.
            ToolbarDescription* descResult = descriptionPtr->FindDescription<ToolbarDescription>();   // 0x140005160

            // The description's cost-string array: data +0x50, count +0x58.
            bool costOverflow = (segmentCount >= descResult->_entries._size);

            if (!costOverflow) {
                // Retail copy-constructs a temporary from the array slot, copies that
                // into costString, then releases the temporary. The scope does both.
                System::String tempString(descResult->_entries._data[segmentCount]);
                costString = tempString;
            }
            // else: retail's srcString is the null String, so costString stays empty
            // and its release is a no-op.

            // ---- Init Transform and Box for UI positioning ----
            // ??0Transform@Math@@QEAA@XZ  retail 0x180015080 (was the
            // 0x140151328 dispatch slot)
            Math::Transform transform;
            ::Box box;              // Volume::Box
            RtVolume_Box_Ctor(&box);   // ??0Box@Volume@@QEAA@XZ 0x180011ea0 (import 0x1401513f8, 0x1400fc890): the empty box

            // ---- Call UpdateToolUI (operator_impl_fc60 at 0x1400ffc60) ----
            dialogPtr->UpdateCreateBar(// param_1
                tileH[0],                              // param_2
                tileW[0],                              // param_3
                &costString,                           // param_4: cost string
                (segmentCount == 0) ? 1 : 0,           // param_5: isEmpty
                (pathType[0] == 2) ? 1 : 0,            // param_6: isInvalid
                (pathType[0] == 1) ? 1 : 0,            // param_7: isSingle
                extraParam,                             // param_8
                startCell,                        // param_9
                endCell,                          // param_10
                &box,                                   // param_11
                &transform                              // param_12
            );
        }

        // ---- Handle mouse release / commit ----
        if (singleClick != 0) {
            // Single-click mode: waiting for second click to confirm placement.
            // Original uses KeyMap::Pressed (dispatch 0x140151758).
            bool pressed = keyMap->Pressed(0, ToUnderlying(GameKey::MouseLeft));
            if (pressed) {
                if (currentCell < 0) {
                    // Invalid target cell
                    DragTool_PlayErrorSound_100140(nullptr);
                } else {
                    // Virtual CalcPath (slot 13, +0x68): validate + commit
                    bool valid = CalcPath(startCell, endCell);

                    if (valid) {
                        DragTool_PlayPlaceSound_1000f0(nullptr);
                    } else {
                        DragTool_PlayErrorSound_100140(nullptr);
                    }
                }

                // Reset both dragState and singleClick, close dialog
                // Original: MOV word ptr [RDI + 0x28], 0x0
                dragState = DragState::Idle;
                singleClick = 0;
                dialogPtr->CloseToolDialog();
            }
        } else {
            // Normal drag mode: check if mouse button was just released.
            // Original checks: Released(0,5) || GetValue(0,5)==0
            // ENDGAME B.3.7: Released used to be always false (see the key-up
            // branch above); it now reports the release itself.
            bool released = keyMap->Released(0, ToUnderlying(GameKey::MouseLeft));
            if (!released) {
                short value = keyMap->GetValue(0, ToUnderlying(GameKey::MouseLeft));
                released = (value == 0);
            }

            if (released) {
                if (startCell == endCell) {
                    // User clicked without dragging — enter single-click mode
                    // (will commit on next click)
                    singleClick = 1;
                } else if (currentCell < 0) {
                    // Dragged to invalid cell — play error but do NOT close dialog
                    // (user can keep dragging to a valid cell)
                    DragTool_PlayErrorSound_100140(nullptr);
                } else {
                    // Valid drag release — virtual CalcPath (slot 13): validate and commit
                    bool valid = CalcPath(startCell, endCell);

                    if (valid) {
                        DragTool_PlayPlaceSound_1000f0(nullptr);
                    } else {
                        DragTool_PlayErrorSound_100140(nullptr);
                    }

                    // Close dialog and reset dragState
                    // Original: MOV byte ptr [RDI + 0x28], 0x0 (only dragState, keeps singleClick)
                    dialogPtr->CloseToolDialog();
                    dragState = DragState::Idle;
                }
            }
        }

    }

    return 1; // Always returns 1 (event consumed)
}

// ============================================================
// DragTool cancel / "can-deactivate?" — vtable +0x48, Slot9
// ============================================================
// Original: 0x1400fc640, shared across the DragTool family (Ghidra mislabels it
// BridgeTool_Update). EntityUI's right-click path asks this before tearing the tool
// down: mid-drag it cancels the drag (clears dragState+singleClick, closes the tool
// dialog) and returns 1 so the tool STAYS; idle it returns 0 and teardown proceeds.
// The Tool_Ret0 stub in that slot meant a right-click mid-drag tore the tool down
// with the cost dialog still open — the "icon left on screen" dismiss bug.
char DragTool::OnCancel() {
    if (dragState == DragState::Idle) return 0;
    dragState = DragState::Idle;
    singleClick = 0;   // retail: MOV word ptr [self+0x28], 0
    PlayerToolManager* owner = dialogPtr;
    if (owner && owner->field_0x00) {
        Object::InternalPtr<UI::DialogInstance> tempIBP(owner->field_0x00);
        GameUpdateLoop::_singleton->m_userInterface->CloseDialog(&tempIBP);
        owner->field_0x00 = nullptr;
    }
    return 1;
}