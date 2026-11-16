// src/game/Dialog/dialog_exe_helpers.cpp
// Notes on two exe-internal helpers referenced by the Dialog module; no bodies here.
//
//   0x14005dec0  is AutoSaveDialog::Autosave (autosavedialog.cpp).
//   (GameContainer_EnsureLists, the standalone stand-in for 0x140099db0, is gone:
//   GameContainer_Init runs from InnerLoaderLoop::Load as retail; D.4 RESIDUE.)
//
// Decompiled from Application-x64.exe via Ghidra.

#include "ui/UserInterface.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/String.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/StringTable.h"   // ENDGAME B.3.6 UI callers (K2)
#include <new>   // ENDGAME B.3.6 UI callers (K2)
#include "achievement/AchievementManager.h"   // ENDGAME B.3.7 callers (K1)
#include "save/SaveLoad.h"   // ENDGAME B.3.7 callers (K1)
#include "dialog_exe_helpers.h"
#include "system/Value.h"
#include "object/TypeInfo.h"
#include "Game/game.h"   // B3R STORAGE: g_gameContainer
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "system/String.h"
#include "system/Filename.h"
#include "object/External.h"   // ENDGAME B.3.3 step 6A
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "Component/BuildComponent.h"   // X::TypeId() (B.3.8 callers)
#include "Dialog/exe_bodies.h"
#include "Dialog/AutoSaveDialog.h"
#include "Entity/ComponentEntity.h"   // ComponentEntity::FindComponent<T>


// (FUN_14005dec0, the autosave handler, is AutoSaveDialog::Autosave in
// autosavedialog.cpp; its B4 FORWARD Dialog_AutoSaveHandler had one caller,
// src/diag/saveload_harness.cpp, which now calls the member: deleted.)
