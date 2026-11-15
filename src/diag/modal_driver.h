#pragma once
// src/diag/modal_driver.h — Wave 10B lane SHOW
//
// A harness callback placed in Engine::MainLoop's PER-TICK CALLBACK ARRAY at
// MainLoop+0x90. That array is the one thing that runs INSIDE the nested modal
// pump `UI::UserInterface::ShowDialog` (0x18011fd10) spins, and it is therefore
// the only place from which an unattended driver can dismiss a modal dialog.
//
// Everything else in the harness ticks from `MenuDialogLoopT::Update` or from a
// GameUpdateLoop, both of which hang off `MainLoop::Update` — and the nested
// pump calls `MainLoop::Frame(true)`, whose `param_1 == true` SKIPS
// `MainLoop::Update` entirely (0x1800dd28e is inside `if (!param_1)`). That is
// the mechanical reason `BANISHED_MODAL_ANSWER` had to be a BYPASS rather than
// a driver: with a modal up, no existing harness tick ever runs again until a
// human clicks.
//
// See docs/history/runtime_port/WAVE10B_LANE_SHOW.md.
//
// SWITCH (off by default):
//   BANISHED_MODAL_DRIVER=<answer>[,<ticks>]
//       Arm the driver. When a modal nested UI is seen for <ticks> consecutive
//       nested ticks (default 3), dismiss its front dialog with result
//       <answer>, exactly the way a MessageDialog button does:
//           *(uint32_t*)(dialog + 0x24) = answer;          // SetDialogResult
//           UI::UserInterface::CloseDialog(dialog->_ui, InternalPtr(dialog));
//       <answer> may be negative; use BANISHED_MODAL_DRIVER=0 to arm with
//       answer 0 (an explicit "0" arms — it is NOT read as "off", unlike the
//       other BANISHED_* switches, because 0 is a meaningful dialog result).
//       Leave the variable UNSET to disable.

#include <cstdint>

namespace ModalDriver {

// True when BANISHED_MODAL_DRIVER is set. Cheap; safe before Poll().
bool Enabled();

// Idempotent. Reads Engine::MainLoop::_singleton (0x1801fe750) and, once it is
// non-null, appends our callback pair to MainLoop's draw (+0x80) and update
// (+0x90) arrays. Returns true once armed. Safe to call every frame.
bool Poll();

// Number of modals this driver has dismissed. This is the EFFECTIVENESS
// COUNTER: a run that reports armed=1 dismissed=0 proves the route was never
// exercised, which is a different verdict from "the route does not work".
int DismissedCount();

}  // namespace ModalDriver
