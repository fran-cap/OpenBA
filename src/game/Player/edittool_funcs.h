#pragma once
// The EditTool helper the harnesses call by name. The EditTool slot bodies are
// members (EditTool.h; ENDGAME B.3.8 callers).

#include "EditTool.h"
#include <cstdint>


// The harnesses' EditTool entry points (defined in edittool.cpp; no retail address:
// standalone drivers). B4 P2 SA: declared here once, in place of each harness's local
// `extern "C"` copy.
extern "C" bool      EditTool_IsEditTool(Tool* tool);  // B8: harness / port-only -- standalone driver entry, no retail symbol
extern "C" EditTool* EditTool_HarnessAcquire(PlayerToolManager* entityUI);  // B8: harness / port-only -- standalone driver entry, no retail symbol
extern "C" int       EditTool_HarnessSelectEntity(EditTool* editTool, ComponentEntity* entity);  // B8: harness / port-only -- standalone driver entry, no retail symbol
extern "C" int       EditTool_HarnessSelectCitizen(EditTool* editTool);  // B8: harness / port-only -- standalone driver entry, no retail symbol
extern "C" int       EditTool_HarnessSelectNearby(EditTool* editTool);  // B8: harness / port-only -- standalone driver entry, no retail symbol

// The harness factories of the two build tools (placementtool.cpp / removaltool.cpp,
// the bodies `CreateNewObject` forwards to, with C linkage; src/diag/tool_harness.cpp also calls them).
class PlacementTool;
extern "C" PlacementTool* PlacementTool_Factory_SA(void);  // B8: harness / port-only -- standalone driver entry, no retail symbol
class RemovalTool;
extern "C" RemovalTool* RemovalTool_Factory_SA(void);  // B8: harness / port-only -- standalone driver entry, no retail symbol
