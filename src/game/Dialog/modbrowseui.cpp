// src/game/Dialog/modbrowseui.cpp
// Clean reimplementation of ModBrowseUI methods.
//
// ModBrowseUI (0x30 bytes): Mod browser panel.
// Extends DialogController (0x30 base). No subclass fields.
//
// Retail has no body past its `RET 0` slot 3 (Persist); the dtor is the defaulted
// ~ModBrowseUI (Dialog/ModBrowseUI.h), the factory in ui_dialog_stub_factories.cpp.

#include "Dialog/ui_panels.h"
#include <cstdint>

// ============================================================
