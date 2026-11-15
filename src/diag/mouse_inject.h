#pragma once
// ---------------------------------------------------------------------------
// src/diag/mouse_inject.h — the hover / tooltip / drag-drop route
// ---------------------------------------------------------------------------
// Wave 10D lane MOUSE. See docs/history/runtime_port/WAVE10D_LANE_MOUSE.md.
//
// OFF unless BANISHED_MOUSE_INJECT is set. Prints nothing when unset.
//
// Two seams, both in the caller of UI::UserInterface::Update, because the
// injector must write the input bytes AFTER the frame's input poll and BEFORE
// the UI reads them, and must read the element flags back AFTER the UI has run:
//
//     MouseInject::PreUpdate(ui);     // place the cursor
//     ui->Update(*time);              // the game hovers, for real
//     MouseInject::PostUpdate(ui);    // did it agree?
//
// Both are no-ops — a single cached Opt() test — when the switch is unset.
namespace UI { class UserInterface; }

namespace MouseInject {

// True once BANISHED_MOUSE_INJECT names a script. Cheap; safe to call per frame.
bool Enabled();

void PreUpdate(UI::UserInterface* ui);
void PostUpdate(UI::UserInterface* ui);

}  // namespace MouseInject
