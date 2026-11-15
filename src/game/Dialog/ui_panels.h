#pragma once
// The panel controller classes (Dialog/Panels.h) and their DialogController base.
//
// B.4 R0-EXE: the C-body declarations that stood here are gone from this header;
// DebugUI's are in debugui_funcs.h. The 18 empty inline
// slot bodies went with B.4 R0-EXE's unreferenced-body deletion (each class
// declares its own empty override).

#include "Dialog/Panels.h"
#include "ui/DialogController.h"

// src/diag/disaster_harness.cpp reaches DebugUI_Slot9 through this header.
#include "Dialog/debugui_funcs.h"
