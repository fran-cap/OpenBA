// src/game/Tutorial/tutorialsetuprestrictplayer.cpp
// Clean reimplementation of TutorialSetupRestrictPlayer vtable methods.
//
// TutorialSetupRestrictPlayer (0x68 bytes), vtable at 0x140160e20.
// Extends TutorialSetup (which extends Object::External).
//
// Fields (beyond External base at +0x08..+0x1F):
//   +0x20: int32 restrictMinX        — cell X offset (min bound)
//   +0x24: int32 restrictMinChunkRow — chunk row (min bound)
//   +0x28: int32 restrictMaxX        — cell X offset (max bound)
//   +0x2C: int32 restrictMaxChunkCol — chunk col (max bound)
//   +0x30: int32 restrictToolFlags   — tool restriction bitmask
//   +0x38: ExternalRef (tool restriction) — {String(8), stamp(4), pad(4), cachedPtr(8)}
//   +0x50: ExternalRef (camera restriction) — {String(8), stamp(4), pad(4), cachedPtr(8)}
//
// Slot8 (Apply) computes absolute camera positions from chunk*cellSize+offset,
// resolves ExternalRef cached pointers, writes restriction state to PlayerController.
// Slot10 (Undo) clears the restriction state back to defaults.
//
// Decompiled from Application-x64.exe using Ghidra.

#include "Tutorial/TutorialSetupRestrictPlayer.h"
#include "object/Internal.h"      // Object::detail::InternalMemAlloc / InternalMemFree
#include "Object/factory_registration.h"
#include "Component/exe_bodies.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "system/Filename.h"
#include "system/String.h"
#include "object/External.h"   // ENDGAME B.3.3 step 6A
#include "object/TypeInfo.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>
#include <cstdlib>
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch D
#include "Component/ComponentDescription.h"
#include "Game/Map.h"
#include "Game/PlayerToolManager.h"

// ============================================================
// ENDGAME B.3.8: the String::Serialize / Filename ctor+dtor dispatch-slot
// pointers this file filled lazily are gone — the Serialize member calls
// System::String::Serialize and scopes a System::Filename directly (B6-5).
// ============================================================

// ============================================================
// The two ExternalRefs resolve through ResolveExternalByName_Impl (0x14001b1d0),
// ExternalFactory::Find alone (0x14001b215, the import 0x140152460): find-only, no
// load. Row 83 (B7 RES): the tree's TutRestrict_ResolveRefOrLoad added a FindOrLoad of
// the separator-normalised name on a miss, a load retail does not do; it is gone. If a
// restricted tutorial step now resolves a null tool (PlayerToolManager +0x88), the
// upstream finding is the preload that left the name out of the factory.
// ============================================================


// ============================================================
// Construction / destruction (ENDGAME B.3.8 block GOAL)
// ============================================================
// ctor 0x1400e3220 (createInPlace 0x140028b80 null-checks then runs it):
// External(), the TutorialSetup and TutorialSetupRestrictPlayer stamps (the
// compiler's), then the fields.
TutorialSetupRestrictPlayer::TutorialSetupRestrictPlayer()
    : restrictMinX(-1), restrictMinChunkRow(-1),      // one qword store of -1
      restrictMaxX(-1), restrictMaxChunkCol(-1),      // one qword store of -1
      restrictToolFlags(0), _pad34{},
      toolRef(),
      cameraRef() {}

// The dtor inside [0] 0x140028bb0: ~String +0x50, ~String +0x38; the compiler
// re-stamps TutorialSetup and runs ~External.
TutorialSetupRestrictPlayer::~TutorialSetupRestrictPlayer() {
    // B4 Y GAMEA: cameraRef's ~String (0x140028be0), then toolRef's (0x140028bf0):
    // the members', after this empty body.
}

// ============================================================
// TutorialSetupRestrictPlayer::Serialize (vtable +0x28, 0x1400e3090)
// ============================================================
// 5 int32 restriction fields and 2 ExternalRef strings; on load (serializer
// mode == 0) each ExternalRef is resolved by name.
bool TutorialSetupRestrictPlayer::Serialize(IO::Stream& stream) {
    // The serializer.
    IO::Serializer serializer(stream, 0);

    // Serialize 5 int32 fields: +0x20, +0x24, +0x28, +0x2C, +0x30
    serializer.Serialize(restrictMinX, nullptr);
    serializer.Serialize(restrictMinChunkRow, nullptr);
    serializer.Serialize(restrictMaxX, nullptr);
    serializer.Serialize(restrictMaxChunkCol, nullptr);
    serializer.Serialize(restrictToolFlags, nullptr);

    // Serialize ExternalRef1 (tool restriction) at +0x38, inside the Filename
    // stack temporary retail's compiler emitted.
    {
        System::Filename tempFilename;
        (void)tempFilename;

        toolRef.name.Serialize(serializer);

        // On load: resolve external by name
        if (serializer._stream && static_cast<int>(serializer.GetDirection()) == 0) {   // IO::Serializer::GetDirection, retail 0x180006210
            toolRef.ResolveDescription();
        }
    }

    // Serialize ExternalRef2 (camera restriction) at +0x50
    {
        System::Filename tempFilename;
        (void)tempFilename;

        cameraRef.name.Serialize(serializer);

        // On load: resolve external by name
        if (serializer._stream && static_cast<int>(serializer.GetDirection()) == 0) {   // IO::Serializer::GetDirection, retail 0x180006210
            cameraRef.ResolveDescription();
        }
    }
    return true;
}

// ============================================================
// TutorialSetupRestrictPlayer::Reconstruct [6] 0x140028b90 / Destruct [7]
// ============================================================
Object::External* TutorialSetupRestrictPlayer::Reconstruct() {
    this->~TutorialSetupRestrictPlayer();
    return ::new (static_cast<void*>(this)) TutorialSetupRestrictPlayer();
}
void TutorialSetupRestrictPlayer::Destruct() { this->~TutorialSetupRestrictPlayer(); }

// A copy of a by-name reference, resolved; the External it names (retail
// copies the 0x18 reference to the stack and resolves the copy).
static Object::External* ResolveCopy(const NamedExternalRef& ref) {
    NamedExternalRef local(ref);
    local.ResolveDescription();
    return local.resolved;   // then the copy's ~String
}

// The resolved reference as the ComponentDescription it was validated as
// (0x14001b1d0 keeps only a ComponentDescription: IsDerivedFrom 0x1401c7318).
static ComponentDescription* AsDescription(Object::External* e) {
    return static_cast<ComponentDescription*>(e);
}

// ============================================================
// TutorialSetupRestrictPlayer::Begin (vtable +0x40, 0x1400e2f20)
// ============================================================
// Applies player restrictions to the PlayerToolManager at GameWorld+0x120.
// Flow:
// 1. Compute absolute min/max positions: chunkRow * cellSize + cellX
// 2. Copy ExternalRef2 (+0x50) into a local, resolve it, get cached ptr
// 3. Copy ExternalRef1 (+0x38) into a local, resolve it, get cached ptr
// 4. Write restriction state to the manager (+0x80..+0xA0)
// 5. Resolve original toolRef (+0x38) and if it has a cached ptr,
//    also set camera restriction bounds (+0xA4..+0xAC)
void TutorialSetupRestrictPlayer::Begin() {
    if (GameUpdateLoop::_singleton == nullptr) return;
    GameUpdateLoop* gameWorld = GameUpdateLoop::_singleton;

    // The map edge in cells (Map +0x00, the loop's +0xF8)
    Map* grid = gameWorld->m_game;
    if (!grid) return;
    int32_t cellSize = grid->size;

    // Compute absolute positions
    int32_t absMin = restrictMinChunkRow * cellSize + restrictMinX;
    int32_t absMax = restrictMaxChunkCol * cellSize + restrictMaxX;

    // Copy ExternalRef2 (+0x50) into local, resolve, get cached ptr
    Object::External* cameraRefPtr = ResolveCopy(cameraRef);

    // Copy ExternalRef1 (+0x38) into local, resolve, get cached ptr
    Object::External* toolRefPtr = ResolveCopy(toolRef);

    // Write restriction state to the PlayerToolManager at GameWorld+0x120
    PlayerToolManager* playerCtrl = gameWorld->m_toolManager;
    if (!playerCtrl) return;

    int32_t toolFlags = restrictToolFlags;

    playerCtrl->tutorialRestricted = true;                         // restrictions active
    playerCtrl->tutorialTool = AsDescription(toolRefPtr);    // resolved tool description
    playerCtrl->tutorialSelectTarget = cameraRefPtr;                 // resolved camera description
    playerCtrl->tutorialStartCell = absMin;                       // min bound
    playerCtrl->tutorialEndCell = absMax;                       // max bound
    playerCtrl->tutorialRotation = toolFlags;                    // tool restriction flags

    // Resolve toolRef on original (+0x38) and if it has a cached External ptr,
    // also set camera restriction bounds
    toolRef.ResolveDescription();
    if (toolRef.resolved != nullptr) {
        PlayerToolManager* playerCtrl2 = gameWorld->m_toolManager;
        playerCtrl2->cameraMoveStartCell = absMin;
        playerCtrl2->cameraMoveEndCell = absMax;
        playerCtrl2->cameraMoveActive = true;
    }
}

// ============================================================
// TutorialSetupRestrictPlayer::End (vtable +0x50, 0x1400e2e90)
// ============================================================
// Clears all player restrictions from the PlayerToolManager at GameWorld+0x120.
// If toolRef has a cached External, calls SelectTool(nullptr).
void TutorialSetupRestrictPlayer::End() {
    if (GameUpdateLoop::_singleton == nullptr) return;
    GameUpdateLoop* gameWorld = GameUpdateLoop::_singleton;

    PlayerToolManager* playerCtrl = gameWorld->m_toolManager;
    if (!playerCtrl) return;

    // Clear restriction state (+0x98 / +0x9C: one qword store of -1, H7)
    playerCtrl->tutorialRestricted = false;
    playerCtrl->tutorialTool = nullptr;
    playerCtrl->tutorialSelectTarget = nullptr;
    playerCtrl->tutorialStartCell = -1;
    playerCtrl->tutorialEndCell = -1;
    playerCtrl->tutorialRotation = -1;                      // tool flags cleared

    // Resolve toolRef on original (+0x38) and if it has a cached External,
    // clear the tool selection (SelectTool(nullptr), 0x140102b60)
    toolRef.ResolveDescription();
    if (toolRef.resolved != nullptr) {
        PlayerToolManager* playerCtrl2 = gameWorld->m_toolManager;
        playerCtrl2->SelectTool(nullptr);   // 0x140102b60 (the door was named SetEditMode)
    }
}

// ============================================================
// TutorialSetupRestrictPlayer::Construct (vtable +0x58, 0x140028b80)
// ============================================================
Object::External* TutorialSetupRestrictPlayer::Construct() {
    return ::new (static_cast<void*>(this)) TutorialSetupRestrictPlayer();
}

// create 0x140028b40: MemAlloc(0x68, 8), then the ctor.
::Object::Base* TutorialSetupRestrictPlayer::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(TutorialSetupRestrictPlayer), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(::new (p) TutorialSetupRestrictPlayer());
}
// createInPlace 0x140028b80.
::Object::Base* TutorialSetupRestrictPlayer::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(::new (storage) TutorialSetupRestrictPlayer());
}

DEFINE_EXE_TYPE(TutorialSetupRestrictPlayer, "TutorialSetup")
