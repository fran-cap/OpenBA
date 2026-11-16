// src/diag/tool_harness.h
// Env-gated (BANISHED_TOOL_HARNESS) in-game tool-injection harness. Once the
// world is built, it constructs a Player tool, wires its fields, and invokes
// its vtable slots directly (autostart-style) so the otherwise-dormant tool
// code paths fire and can be breakpoint-validated in the debugger.
//
// See docs/history/notes/STANDALONE_REG_WAVES_PLAN.md (Wave 5 / Player last-8).
#pragma once

class PlayerToolManager;   // src/game/Game/PlayerToolManager.h

extern "C" {

// Called once per in-game frame from GameUpdateLoop::Update (after the world is
// built). entityUI = GameUpdateLoop::m_toolManager; frameNum = the loop's frame
// counter (used to settle before firing). No-op unless BANISHED_TOOL_HARNESS
// is set. Fires its tool sequence exactly once.
void ToolHarness_Tick(PlayerToolManager* entityUI, int frameNum);

}  // extern "C"

