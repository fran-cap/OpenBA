// src/game/Tutorial/tutorialsetupcamera.cpp
// Clean reimplementation of TutorialSetupCamera vtable methods.
//
// TutorialSetupCamera (0x30 bytes), vtable at 0x140160ca0.
// Extends TutorialSetup (which extends Object::External).
//
// Fields (beyond External base at +0x08..+0x1F):
//   +0x20: int32 cameraX       — cell X offset within chunk
//   +0x24: int32 cameraChunkRow — chunk row index
//   +0x28: int32 cameraY       — cell Y offset within chunk
//   +0x2C: int32 cameraChunkCol — chunk column index
//
// Slot8 (Apply) computes absolute camera position:
//   absX = cameraChunkRow * mapCellSize + cameraX
//   absY = cameraChunkCol * mapCellSize + cameraY
// Then writes to the camera object at GameWorld+0x120.
//
// Decompiled from Application-x64.exe using Ghidra.

#include "Tutorial/TutorialSetupCamera.h"
#include "object/External.h"
#include "object/TypeInfo.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Game/PlayerToolManager.h"
#include "Game/Map.h"
#include "object/Internal.h"      // Object::detail::InternalMemAlloc / InternalMemFree
#include "Object/factory_registration.h"
#include <cstdint>
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch D
#include <new>
#include "Entity/ComponentEntity.h"   // B4 S4 RT2: InternalPtr<ComponentEntity>::Get() needs the class complete

// ============================================================
// Construction / destruction (ENDGAME B.3.8 block GOAL)
// ============================================================
// ctor 0x1400e2200 (inlined in create 0x140028400 / createInPlace 0x140028470):
// External(), the TutorialSetup and TutorialSetupCamera stamps (the
// compiler's), then two qword stores of -1 over the four fields.
TutorialSetupCamera::TutorialSetupCamera()
    : cameraX(-1), cameraChunkRow(-1), cameraY(-1), cameraChunkCol(-1) {}

// ============================================================
// TutorialSetupCamera::Serialize (vtable +0x28, 0x1400e2120)
// ============================================================
// Serializes 4 int32 fields: cameraX, cameraChunkRow, cameraY, cameraChunkCol.
bool TutorialSetupCamera::Serialize(IO::Stream& stream) {
    // The serializer.
    IO::Serializer serializer(stream, 0);

    serializer.Serialize(cameraX, nullptr);
    serializer.Serialize(cameraChunkRow, nullptr);
    serializer.Serialize(cameraY, nullptr);
    serializer.Serialize(cameraChunkCol, nullptr);
    return true;
}

// ============================================================
// TutorialSetupCamera::Reconstruct [6] 0x1400284d0 / Destruct [7] 0x140001640
// ============================================================
Object::External* TutorialSetupCamera::Reconstruct() {
    this->~TutorialSetupCamera();
    return ::new (static_cast<void*>(this)) TutorialSetupCamera();
}
void TutorialSetupCamera::Destruct() { this->~TutorialSetupCamera(); }

// ============================================================
// TutorialSetupCamera::Begin (vtable +0x40, 0x1400e20d0)
// ============================================================
// absX = chunkRow * cellSize + cellX; absY = chunkCol * cellSize + cellY,
// written to the camera object (GameWorld+0x120) +0xa8 / +0xac, flag +0xa4.
void TutorialSetupCamera::Begin() {
    if (GameUpdateLoop::_singleton == nullptr) return;

    GameUpdateLoop* gameWorld = GameUpdateLoop::_singleton;

    // The map edge in cells (Map +0x00, the loop's +0xF8)
    Map* gridPtr = gameWorld->m_game;
    if (!gridPtr) return;
    int32_t cellSize = gridPtr->size;

    // The PlayerToolManager (GameWorld+0x120), which holds the camera target
    PlayerToolManager* camera = gameWorld->m_toolManager;
    if (!camera) return;

    // Compute absolute camera position
    int32_t absX = cameraChunkRow * cellSize + cameraX;
    int32_t absY = cameraChunkCol * cellSize + cameraY;

    // Write to camera object
    camera->cameraMoveStartCell = absX;
    camera->cameraMoveActive = true;   // Flag: camera position is set / needs update
    camera->cameraMoveEndCell = absY;
}

// ============================================================
// TutorialSetupCamera::Construct (vtable +0x58, 0x140028470)
// ============================================================
Object::External* TutorialSetupCamera::Construct() {
    return ::new (static_cast<void*>(this)) TutorialSetupCamera();
}

// create 0x140028400: MemAlloc(0x30, 8), then the (inlined) ctor.
::Object::Base* TutorialSetupCamera::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(TutorialSetupCamera), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(::new (p) TutorialSetupCamera());
}
// createInPlace 0x140028470.
::Object::Base* TutorialSetupCamera::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(::new (storage) TutorialSetupCamera());
}

DEFINE_EXE_TYPE(TutorialSetupCamera, "TutorialSetup")
