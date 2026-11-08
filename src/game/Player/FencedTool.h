#pragma once
// ============================================================================
// src/game/Player/FencedTool.h — ENDGAME B.3.8 TOOL (B38_RULES.md D1-D5)
//
// The fenced-area tool (pastures, crop fields' fence ring), a REAL C++ class
// with a compiler vtable.
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §6, bytes read 2026-09-26):
//   reflected "FencedTool", parent "DragTool", ti 0x1401c6358
//   create 0x1400268d0: MemAlloc(0x70, 8), ctor 0x1400dd070: `??0Internal`;
//     Tool part; DragTool part; stamp 0x1401605d0; +0x38..+0x50 = 0;
//     dword +0x58 = -1; +0x68 = 0. +0x5c/+0x60/+0x64 are NOT written (Startup
//     fills +0x5c/+0x60); the tree zero-filled them and this class keeps that.
//   createInPlace 0x140026910: `if (storage) ctor(storage)`
//   dtor 0x1400dd020: restamps only, tail-JMP `~Internal`
//   table 0x1401605d0, 15 slots (Ghidra's labels in this table are wrong —
//   "TimeGoalInstance_*" — the bodies are the fence tool's):
//     [0]  0x140026930 scalar deleting dtor
//     [1]  0x1400268c0 TypeName   [2] 0x1400268b0 Type   [3] = parent
//     [4]  0x1400dcd50 Startup: resolve the 3 descriptions + RotationSetting,
//          allocate the preview MeshInstance
//     [5]  0x1400dccc0 Shutdown: remove the preview from the scene, free it
//     [6]  0x1400dcbe0 OnKey: DIRECT call to DragTool::OnKey, then rotate
//     [7]  0x1400dcad0 Update: the fence preview mesh
//     [8]  0x1400dc440 Draw
//     [9]  0x1400dcd10 OnCancel: drop the preview, then TAIL-JMP 0x1400fc640
//     [10] = Tool's
//     [11] 0x140026920 `*ret = TypeId(RotationSetting)` (shared with PlacementTool)
//     [12] = DragTool's (cursor 5)
//     [13] 0x1400dae00 CalcPath (fencedtool_calcpath.cpp)
//     [14] 0x1400db490 CalcCost
//   sizeof 0x70.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Player/dragtool.h"
#include "Object/exe_type.h"

class CreateFencedDescription;   // Component/CreateFencedDescription.h
class MapDescription;            // Component/MapDescription.h
class ModelDescription;          // Component/ModelDescription.h
class RotationSetting;           // Player/RotationSetting.h
namespace Graphics { class MeshInstance; }

class FencedTool final : public DragTool {
    DECLARE_EXE_TYPE(FencedTool)

public:
    FencedTool()                                                           // 0x1400dd070
        : fencedDesc(nullptr), areaDesc(nullptr), fenceDesc(nullptr), rotationSetting(nullptr),
          sceneObjectId(-1), footprintWidth(0), footprintDepth(0), _pad64{}, meshInstance(nullptr) {}

    // 0x1400dd020: no body
    ~FencedTool() override = default;                                      // [0]  0x140026930
    void Startup() override;                                               // [4]  0x1400dcd50
    void Shutdown() override;                                              // [5]  0x1400dccc0
    char OnKey(char active) override;                                      // [6]  0x1400dcbe0
    void Update(const System::Time* time, char active) override;       // [7]  0x1400dcad0
    void Draw(Video::Context* context, char active) override;                        // [8]  0x1400dc440
    char OnCancel() override;                                              // [9]  0x1400dcd10
    Object::Type* GetSettingType(Object::Type* ret) const override;        // [11] 0x140026920
    bool CalcPath(int startCell, int endCell) override;                    // [13] 0x1400dae00
    int  CalcCost(int startCell, int endCell, uint32_t* outWidth,
                  int* outHeight, int* outDepth, int* outFenceSegments) override;  // [14] 0x1400db490

    // The placement rectangle, world anchor and yaw from the drag (body in tool_base_edit.cpp).
    uint64_t CalcAnchor(uint32_t* param_2, uint32_t* param_3, uint32_t* param_4, uint32_t* param_5,
                        float* param_6, uint32_t* param_7) const;           // 0x1400dbf90

    CreateFencedDescription* fencedDesc;   // +0x38 cell 0x1401c4100 (B4 PLAYER)
    MapDescription*   areaDesc;          // +0x40 cell 0x1401c7a18 (B4 PLAYER)
    ModelDescription* fenceDesc;         // +0x48 cell 0x1401c7a88 (B4 PLAYER)
    RotationSetting*  rotationSetting;   // +0x50 InternalBasePtr<RotationSetting> (B4 PLAYER)
    int32_t  sceneObjectId;     // +0x58 Graphics::Scene object id, -1 = not added
    uint32_t footprintWidth;          // +0x5c from the size description +0x44
    uint32_t footprintDepth;          // +0x60 from the size description +0x48
    uint8_t  _pad64[4];         // +0x64
    Graphics::MeshInstance* meshInstance; // +0x68 the fence preview (B4 PLAYER)
};


static_assert(sizeof(FencedTool) == 0x70, "FencedTool is 0x70 (create 0x1400268d0 MemAlloc(0x70))");
static_assert(offsetof(FencedTool, _pad64) == 0x64, "FencedTool::_pad64");
static_assert(alignof(FencedTool) == 8, "FencedTool is 8-aligned");
static_assert(offsetof(FencedTool, fencedDesc)      == 0x38, "FencedTool +0x38 (ctor 0x1400dd0c8)");
static_assert(offsetof(FencedTool, areaDesc)        == 0x40, "FencedTool +0x40");
static_assert(offsetof(FencedTool, fenceDesc)       == 0x48, "FencedTool +0x48");
static_assert(offsetof(FencedTool, rotationSetting) == 0x50, "FencedTool +0x50 (ctor 0x1400dd0d4)");
static_assert(offsetof(FencedTool, sceneObjectId)   == 0x58, "FencedTool +0x58 = -1 (ctor 0x1400dd0d8)");
static_assert(offsetof(FencedTool, footprintWidth)        == 0x5c, "FencedTool +0x5c");
static_assert(offsetof(FencedTool, footprintDepth)        == 0x60, "FencedTool +0x60");
static_assert(offsetof(FencedTool, meshInstance)    == 0x68, "FencedTool +0x68 (ctor 0x1400dd0df)");
