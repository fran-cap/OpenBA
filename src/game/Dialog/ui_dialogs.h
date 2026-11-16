#pragma once
// Includes Dialog/Dialogs.h; the notes below say where the dialog methods live.

#include "Dialog/Dialogs.h"
#include <cstdint>

// (MainMenuDialog: slots [4] / [5] are the members OnOpened / OnClosed since
// Phase D.4 GAPS; the free MainMenuDialog_Slot4 / _Slot5 copies are gone.)

// ============================================================
// OptionsDialog
// ============================================================

// ENDGAME B.3.8 DLG: OptionsDialog is a real class (Dialog/OptionsDialog.h);
// its slots [4] OnOpened, [5] OnClosed and [8] OnElementEvent are members.
// B.4 DLG-c: OnOk, OnCancel, OnApply, PopulateInputBindList and OnInputBindClick
// are private members too.
