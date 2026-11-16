#pragma once
// ============================================================================
// src/game/Player/Tool.h — ENDGAME B.3.8 TOOL (B38_RULES.md §2.1, D1-D3)
//
// `Tool`, the exe's base of the player's world tools (the object EntityUI
// holds at +0x28 and drives every frame), as a REAL C++ class whose vtable the
// compiler generates. Global namespace, as in retail (reflected "Tool", parent
// literal "Object::Internal"; DEFINE_EXE_TYPE with the abstract factory,
// ENDGAME B.3.9 RF).
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §1, bytes read 2026-09-26):
//   ctor   inlined into every leaf ctor (e.g. BridgeTool 0x1400fb890):
//          `??0Internal@Object@@`; stamp 0x140161f10; +0x18 = 0; +0x20 = 0.
//          +0x10 is NOT written (SetEditMode stores the owner there before
//          slot 4); the tree zero-filled it and this class keeps that.
//   table  0x140161f10, 13 slots, create = 0x140003730 (abstract)
//   [0]  0x14002e9b0 scalar deleting dtor: stamp 0x140161f10, `~Internal`,
//                    `if (flags & 1) MemFree`. ~Tool has no body.
//   [1]  0x14002e950 TypeName   [2] 0x14002e940 Type
//   [3]  Object::Internal::Persist (import) — INHERITED
//   [4]-[9] _purecall
//   [10] 0x1400268a0 `XOR AL,AL; RET`       (shared by 8 tables)
//   [11] 0x140004950 `MOV dword [RDX],0; MOV RAX,RDX; RET` — the no-setting
//        answer through the hidden return slot (shared by 19 tables)
//   [12] 0x140105860 `MOV EAX,1; RET`
//
// SLOT NAMES 4-12 have no retail symbol. They are named from the tree's
// callers: PlayerToolManager::SelectTool (toolbardialog.cpp: [4] start, [5] close,
// [11] setting type), PlayerToolManager::Update 0x1401026a0 / UpdateHotkeys 0x1400ff6e0
// (Dialog/entityui.cpp, Game/camera_input.cpp: [6] key, [7] update, [9] cancel,
// [12] cursor), GameUpdateLoop::Draw ([8] draw), EditTool/PathTool's [10] "is this my
// selected entity".
// Argument lists are those callers' (a `char` flag is a byte in AL/CL/DL/R8B
// exactly as retail passes it).
//
// LAYOUT: Object::Internal (0x10) + three pointers = 0x28. The field NAMES are
// the ones the tree's slot bodies already use (the old PODs'): +0x10 the tool's
// owner handle (DragTool bodies pass it as the placement dialog's
// InternalBasePtr, "dialogPtr"), +0x18 the ToolSetting IBP SetEditMode hands
// over, +0x20 the tool's description (group) entity.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "object/Internal.h"
#include "object/Type.h"
#include "Object/exe_type.h"   // DECLARE_EXE_TYPE (ENDGAME B.3.9 RF)

class PlayerToolManager;      // Game/PlayerToolManager.h
class ToolSetting;            // Player/ToolSetting.h
class ComponentDescription;   // Component/ComponentDescription.h
class ComponentEntity;        // Entity/ComponentEntity.h
namespace Video { class Context; }
namespace System { class Time; }

class Tool : public Object::Internal {
    DECLARE_EXE_TYPE(Tool)

public:
    // ---- the 13 slots, in retail order -------------------------------------
    // no body in retail (slot 0 only restamps)
    ~Tool() override = default;                                        // [0]  0x14002e9b0
    // [1] TypeName 0x14002e950 and [2] Type 0x14002e940: DECLARE_EXE_TYPE (retail `MOV RAX,[ti+8]` / `MOV EAX,[ti]`).
    // [3] Persist — Object::Internal::Persist, inherited (retail IMPORT).
    virtual void  Startup() = 0;                                       // [4]  pure
    virtual void  Shutdown() = 0;                                      // [5]  pure
    virtual char  OnKey(char active) = 0;                              // [6]  pure
    virtual void  Update(const System::Time* time, char active) = 0;// [7]  pure
    virtual void  Draw(Video::Context* context, char active) = 0;      // [8]  pure (the frame's Video::Context, GameUpdateLoop::Draw)
    virtual char  OnCancel() = 0;                                      // [9]  pure
    virtual bool  IsSelected(ComponentEntity* const* entityRef);       // [10] 0x1400268a0 false (the entity's InternalPtr slot)
    // [11] RETSLOT (B8-11): the ToolSetting type this tool wants, 0 = none.
    virtual Object::Type* GetSettingType(Object::Type* ret) const;     // [11] 0x140004950
    virtual int   GetCursor() const;                                   // [12] 0x140105860 1

    // ---- the 0x18 bytes Tool adds, retail order -----------------------------
    // PUBLIC while the slot bodies (free functions over the tool pointer) read
    // them by name (decision 1).
    //
    // B.4 PLAYER (types from SelectTool 0x140102b60, which installs a tool:
    // 0x140102dc2 `MOV [tool+0x10],R13` (R13 = the PlayerToolManager `this`),
    // 0x140102dca `MOV [tool+0x18],RBX` (the registry node's ToolSetting IBP,
    // built from slot 11 and IsDerivedFrom(ToolSetting) 0x140102d89),
    // 0x140102dc6 `MOV [tool+0x20],RAX` (the manager's +0x20 tool group)).
    // The +0x10 NAME is the tree's: it is the owning manager, whose +0x00 is the
    // open tool dialog's InternalBasePtr (known_port_issues 14f row 73).
    PlayerToolManager*    dialogPtr;        // +0x10 the owning tool manager
    ToolSetting*          toolSetting;      // +0x18 InternalPtr<ToolSetting> (ctor zeroes it)
    ComponentDescription* descriptionPtr;   // +0x20 the tool group description (ctor zeroes it)

protected:
    Tool() : Object::Internal(), dialogPtr(nullptr), toolSetting(nullptr), descriptionPtr(nullptr) {}
};


// ---- the overlay records the tools hand to 0x1401008c0 (B4 PLAYER) --------
// The per-cell tile record (0x14 bytes, stride read at 0x1401008c0's second loop):
// col, row, the texture-variant column, the ARGB colour, the angle in degrees.
struct ToolTileRecord {
    int32_t  col;       // +0x00
    int32_t  row;       // +0x04
    int32_t  variant;   // +0x08 texture-variant column (x 0.125 in u)
    uint32_t color;     // +0x0C ARGB
    int32_t  angle;     // +0x10 degrees
};
static_assert(sizeof(ToolTileRecord) == 0x14, "ToolTileRecord is the 0x14-byte 0x1401008c0 record");
static_assert(offsetof(ToolTileRecord, col) == 0x0, "ToolTileRecord::col");
static_assert(offsetof(ToolTileRecord, row) == 0x4, "ToolTileRecord::row");
static_assert(offsetof(ToolTileRecord, variant) == 0x8, "ToolTileRecord::variant");
static_assert(offsetof(ToolTileRecord, color) == 0x0C, "ToolTileRecord::color @ +0x0C");
static_assert(offsetof(ToolTileRecord, angle) == 0x10, "ToolTileRecord::angle @ +0x10");

// The segment record (0x20 bytes, 0x1401008c0's first loop; PathTool's routes):
// two cells, the variant, colour, angle, and the strip's half width.
struct ToolSegmentRecord {
    int32_t  col1;      // +0x00
    int32_t  row1;      // +0x04
    int32_t  variant;   // +0x08
    uint32_t color;     // +0x0C ARGB
    int32_t  angle;     // +0x10 degrees
    int32_t  col2;      // +0x14
    int32_t  row2;      // +0x18
    float    width;     // +0x1C
};
static_assert(sizeof(ToolSegmentRecord) == 0x20, "ToolSegmentRecord is the 0x20-byte 0x1401008c0 record");
static_assert(offsetof(ToolSegmentRecord, col1) == 0x0, "ToolSegmentRecord::col1");
static_assert(offsetof(ToolSegmentRecord, row1) == 0x4, "ToolSegmentRecord::row1");
static_assert(offsetof(ToolSegmentRecord, variant) == 0x8, "ToolSegmentRecord::variant");
static_assert(offsetof(ToolSegmentRecord, color) == 0xC, "ToolSegmentRecord::color");
static_assert(offsetof(ToolSegmentRecord, angle) == 0x10, "ToolSegmentRecord::angle");
static_assert(offsetof(ToolSegmentRecord, row2) == 0x18, "ToolSegmentRecord::row2");
static_assert(offsetof(ToolSegmentRecord, col2) == 0x14, "ToolSegmentRecord::col2 @ +0x14");
static_assert(offsetof(ToolSegmentRecord, width) == 0x1C, "ToolSegmentRecord::width @ +0x1C");

static_assert(sizeof(Tool) == 0x28, "Tool is 0x28 bytes (0x10 Internal + 0x18)");
static_assert(alignof(Tool) == 8, "Tool is 8-aligned");
static_assert(offsetof(Tool, dialogPtr)      == 0x10, "Tool +0x10 owner (Toolbar_ShowGroup_Impl)");
static_assert(offsetof(Tool, toolSetting)    == 0x18, "Tool +0x18 setting IBP (ctor zeroes, 0x1400fb8b9)");
static_assert(offsetof(Tool, descriptionPtr) == 0x20, "Tool +0x20 description (ctor zeroes, 0x1400fb8bd)");
