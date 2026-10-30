// src/diag/tool_harness.cpp
// See tool_harness.h. Env-gated in-game tool-injection harness for the Player
// last-8 debugger pass.
//
// Phase 1 (current): construct a RemovalTool, wire its fields against the live
// EntityUI + a small valid cell rectangle, call Startup, then exercise the
// already-reimplemented CalcCost (and the not-yet-ported CalcPath). This proves
// the harness drives a dormant tool path end-to-end so the debugger can
// validate it. CalcPath (0x1401035e0) is a passthrough (null in standalone)
// until ported — calling it now is a safe no-op that returns 0.
//
// All tool invocation is via raw vtable slots (no C++ decls / name mangling):
//   Tool/DragTool vtable index = byte-offset / 8.
//     +0x20 Startup  = idx 4
//     +0x68 CalcPath = idx 13   (DragTool slot13)
//     +0x70 CalcCost = idx 14   (DragTool slot14)


#include "tool_harness.h"
#include "options.h"
#include "math/Math.h"
#include "ui/UserInterface.h"
#include "object/Internal.h"
#include "object/TypeInfo.h"
#include "graphics/Camera.h"
#include "Entity/exe_bodies.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "system/String.h"   // -Iruntime/types; the operator_fc60 probe
#include "Object/factory_registration.h"  // ::Object::GetTypeIdByName (pinpanel controller lookup)
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <new>
#include "ui/DialogInstance.h"   // ENDGAME B.3.6 UI callers (K4)
#include "ui/ComboElement.h"                  // ENDGAME B.3.6 UI callers (K4)
#include "Component/MapComponent.h"
#include "Component/OrchardComponent.h"
#include "Dialog/OrchardUI.h"
#include "Component/PastureComponent.h"
#include "Dialog/PastureUI.h"
#include "Component/PickingComponent.h"
#include "Component/ModelComponent.h"     // the pick route's aim point (transform +0x148)
#include "Component/CitizenComponent.h"   // the pick route's citizen list entries
#include "Dialog/CitizenUI.h"             // the pick route's panel check
#include "picking/Scene.h"                // Picking::Scene::Intersect / GetUserData
#include "Dialog/SpriteCaptureUI.h"       // the spritecapture route (row 41)
#include "ui/EditElement.h"
#include "ui/ButtonElement.h"
#include "io/FileSystem.h"
#include "Component/TradeComponent.h"
#include "Dialog/TradeUI.h"
#include "Component/UIComponent.h"
#include "Dialog/EntityUI.h"
#include "Dialog/ToolUI.h"
#include "Dialog/UpgradeUI.h"
#include "Player/RemovalTool.h"
#include "Player/Tool.h"
#include "Player/exe_bodies.h"            // UpdateCreateBar (0x1400ffc60)
#include "Action/exe_bodies.h"            // ActHunt_MarkEntityRemoval_c2ea0
#include "Dialog/exe_bodies.h"
#include "graphics/Geometry.h"            // Graphics::Geometry::GetTransform
#include "system/MemoryAllocator.h"       // System::MemoryAllocator::MemAlloc / _singleton
// B4 P2 T1: the classes the harness reaches by name.
#include "Game/PlayerToolManager.h"
#include "Game/Map.h"
#include "Game/Minimap.h"
#include "Entity/ComponentEntity.h"
#include "Entity/EntityList.h"
#include "Entity/CitizenBehavior.h"
#include "Game/WorkScheduler.h"
#include "Player/PlacementTool.h"
#include "Player/RoadTool.h"
#include "Player/FencedTool.h"
#include "Player/EditTool.h"
#include "Player/BridgeTool.h"
#include "graphics/Mesh.h"
#include "ui/ButtonElement.h"
#include "Component/BuildComponent.h"
#include "Component/BuildDescription.h"
#include "Component/CreateBridgeDescription.h"

// Standalone factory from src/game/Player/removaltool.cpp (extern "C";
// Player/edittool_funcs.h). Returns a freshly MemAlloc'd RemovalTool (0x40 bytes)
// with its standalone vtable installed and drag-cell defaults
// (startCell/endCell/lastEndCell = -1).
#include "Player/edittool_funcs.h"

// Standalone PlacementTool bring-up (GUI_PLAN Keystone F, Phase 1).
//   PlacementTool_Factory_SA          — MemAlloc'd 0x70 PlacementTool + std vtable
//                                       (Player/edittool_funcs.h).
//   PlacementTool_HarnessStartupProbe — logs descriptionPtr's component array +
//     the 4 *Description type-ids, confirms ModelDescription present, then calls
//     the faithful Startup (bails safely if Model absent).
//   StartTown_ResolveTemplateByPath   — resolve "Template\\<Name>.rsc" to a
//     building DESCRIPTION ComponentEntity for descriptionPtr (+0x20).
extern "C" int   PlacementTool_HarnessStartupProbe(PlacementTool* self);
extern "C" class ComponentDescription* StartTown_ResolveTemplateByPath(const char* path);
// Phase 2: drive the faithful CREATE path (helper_d650) for a chosen centre cell.
extern "C" int   PlacementTool_HarnessPlaceAtCell(PlacementTool* self, int centerCell);
// Phase 3B: drive the ghost-preview pipeline (Scene::AddObject + TransformFromYaw +
// Geometry::SetTransform) for a fixed cell — a cursor-free stand-in for Serialize.
extern "C" int   PlacementTool_HarnessPreviewAtCell(PlacementTool* self, int centerCell);

// Phase 4: fire the utility-dialog open path (toolbar case 2) headlessly.
//   ToolbarUtility_HarnessOpen(gameSubsys, param) — param = a utility item's
//   Description ptr (a resolved Template\Utility*.rsc). Opens its management dialog.
extern "C" void ToolbarUtility_HarnessOpen(PlayerToolManager* gameSubsys, ComponentDescription* param);

// Map-render bring-up: create + upload the minimap GPU ImageBuffers
// (gameWorld+0x148 +0x08 terrain / +0x10 overlay) from the raw pixel arrays.
extern "C" void Minimap_CreateAndUpload(Minimap* mapObj);
// Drive a named controller's Slot4 (+0x20 SetupImage) on the just-opened dialog.
extern "C" void ToolbarUtility_HarnessDriveController(const char* controllerName, int slotIndex);
// Open the in-game "Map" menu dialog (buttonMap id 11 → MapParamsDialog).
extern "C" void MapParamsDialog_HarnessOpen();

// Phase 3: the SetEditMode reimpl the toolbar case-1 click drives (entityui.cpp).
// Walks the tool-mode tree at entityUI+0x10 (built by PlayerToolManager::Startup 0x140101ee0),
// resolves PlacementTool from the descriptor's CreatePlacedDescription, CreateObject's
// it + Startup, installs it at entityUI+0x28. Declared in Dialog/exe_bodies.h.

// GUI: entity-selection (EditTool) harness (Player/edittool_funcs.h, edittool.cpp):
// EditTool_HarnessSelectNearby scans the region map near centre and drives the EditTool
// select path on the first dialog-capable entity; _HarnessSelectEntity drives it on a
// specific entity (1 if a dialog opened); _HarnessSelectCitizen selects a citizen
// recovered from the Picking::Scene (tests CitizenUI).

// A captured founding-house building entity (starttown.cpp) — a definitive UIDescription-
// bearing entity, independent of the region-map/cursor pick.
extern "C" ComponentEntity* StartTown_GetFoundingHouseEntity(int index);
extern "C" int   StartTown_GetFoundingHouseCount();

// Track A: run the ClearAreaComponent footprint sweep (0x140117d60 port) on a
// building entity — marks its footprint resources for removal. src/game/Component/clearareacomponent.cpp
class ClearAreaComponent;
extern "C" int ClearAreaComponent_HarnessSweep(ComponentEntity* ownerEntity);
extern "C" ClearAreaComponent* ClearAreaComponent_HarnessFind(ComponentEntity* entity, uint32_t* outFlags);
extern "C" int   ClearAreaComponent_HarnessSweepComp(ClearAreaComponent* realComp);
extern "C" ComponentEntity* ClearAreaComponent_HarnessOwner(ClearAreaComponent* realComp);

namespace {

// RemovalTool field offsets (DragTool base + RemovalTool).
//   +0x10  entityUI         (set by EntityUI::SetEditMode in the real flow)
//   +0x20  description       (left null here — Startup -> removalDesc=null, guarded)
//   +0x28  dragState (byte)  / +0x29 singleClick — word write 1 = dragging, no single-click
//   +0x2c  startCell (int)
//   +0x30  endCell   (int)
//   +0x38  removalDesc (set by Startup)
// (B4 P2 T1: the fields are reached by name, Tool.h / dragtool.h / RemovalTool.h.)

bool        s_initialized = false;
bool        s_enabled     = false;
bool        s_fired       = false;
int         s_settleFrame = 60;      // frames after world-build before firing
const char* s_toolName    = "removal";
int         s_pf_panelFrame = 0;   // "pasturefull": frame to open the panel on

void Init() {
    if (s_initialized) return;
    s_initialized = true;

    const char* env = Opt().toolHarness;
    if (!env || env[0] == '\0' ||
        std::strcmp(env, "0") == 0 || std::strcmp(env, "false") == 0) {
        s_enabled = false;
        fprintf(stderr, "[ToolHarness] disabled (BANISHED_TOOL_HARNESS unset)\n");
        return;
    }
    s_enabled  = true;
    s_toolName = env;   // currently only "removal" is wired
    fprintf(stderr, "[ToolHarness] ENABLED (BANISHED_TOOL_HARNESS=\"%s\") — will fire "
           "the tool sequence at frame %d\n", env, s_settleFrame);
}

// Build a cell index from (row,col) on the 256-wide grid (verified live:
// **(int**)(gameWorld+0xf8) == 256). A modest rectangle near map centre, where
// the start-town valley + scattered resources (trees/stone) live.
inline int Cell(int row, int col) { return 256 * row + col; }

void FireRemovalTool(PlayerToolManager* entityUI) {
    fprintf(stderr, "[ToolHarness] constructing RemovalTool (entityUI=%p)...\n", entityUI);
    // RemovalTool_Factory_SA is RemovalTool::CreateNewObject: the receiver is proved.
    RemovalTool* rt = RemovalTool_Factory_SA();
    if (!rt) {
        fprintf(stderr, "[ToolHarness] ERROR: RemovalTool_Factory_SA returned null\n");
        return;
    }

    // Wire fields: entityUI, dragging state, a cell rectangle around centre.
    rt->dialogPtr = entityUI;
    rt->dragState   = 1;   // the one word store 1 is dragState=1, singleClick=0 (H7: two stores)
    rt->singleClick = 0;
    const int startCell = Cell(124, 124);
    const int endCell   = Cell(131, 131);                  // 8x8 selection
    rt->startCell = startCell;
    rt->endCell   = endCell;
    fprintf(stderr, "[ToolHarness] RemovalTool=%p startCell=%d endCell=%d (8x8 @ ~centre)\n",
           rt, startCell, endCell);

    // Startup (+0x20): finds RemovalDescription on description entity (+0x20,
    // null here) -> removalDesc=null. Safe, guarded downstream.
    fprintf(stderr, "[ToolHarness] calling Startup (retail slot 4, +0x20)...\n");
    rt->Startup();

    // CalcCost (+0x70): already reimplemented — computes the removal-area dims.
    // Validates the harness pipeline live (breakpoint here in the debugger).
    uint32_t outValid = 0xdead;
    int outB = -1, outA = -1;
    int outLen = 0xbeef;   // CalcCost's int* out3 (printed %u as before)
    fprintf(stderr, "[ToolHarness] calling CalcCost (retail slot 14, +0x70)...\n");
    uint64_t cc = static_cast<uint64_t>(rt->CalcCost(startCell, endCell, &outValid, &outB, &outA,
                                                     &outLen));
    fprintf(stderr, "[ToolHarness] CalcCost -> ret=%llu valid=%u widthB=%d heightA=%d len=%u "
           "(expect width=8 height=8 valid=0 ret=0)\n",
           (unsigned long long)cc, outValid, outB, outA, outLen);

    // CalcPath (+0x68): now the LIVE ported RemovalCalc_Slot13 (0x1401035e0). It
    // dereferences removalDesc (self+0x38) throughout, so it is only safe to call with a
    // real RemovalDescription. This harness leaves the description entity null (Startup ->
    // removalDesc=null), so we SKIP the call here to avoid a null-deref fault. Drive
    // CalcPath via the real toolbar Remove button instead (BANISHED_LIVE_TOOLBAR=1
    // BANISHED_GAME_PRELOAD=1 BANISHED_LIVE_TOOLS=1), which supplies a live descriptor.
    void* removalDesc = rt->removalDesc;
    if (removalDesc) {
        fprintf(stderr, "[ToolHarness] calling CalcPath (retail slot 13, +0x68) = RemovalCalc_Slot13...\n");
        int cp = rt->CalcPath(startCell, endCell) ? 1 : 0;
        fprintf(stderr, "[ToolHarness] CalcPath -> ret=%d\n", cp);
    } else {
        fprintf(stderr, "[ToolHarness] SKIP CalcPath: removalDesc(+0x38) null (no description entity "
               "wired) — the live CalcPath would fault. Use the toolbar Remove button.\n");
    }

    fprintf(stderr, "[ToolHarness] RemovalTool sequence complete.\n");
}

// GUI_PLAN Keystone F, Phase 1+2: construct a PlacementTool, point descriptionPtr
// (+0x20) at a resolved simple-building template, run the (probe-guarded) Startup,
// then (Phase 2) drive the faithful CREATE path (helper_d650) at a valid centre
// cell so a real building entity materializes in the world — harness-driven, no
// cursor/keypress. dialogPtr (+0x10) is supplied a zeroed controller by the create
// driver (skips the existing-placement constraint).
void FirePlacementTool(int centerRowIn, int centerColIn) {
    // A simple placeable building the Easy start-town already spawns.
    //
    // BANISHED_PLACEMENT_TEMPLATE overrides it, exactly as it already does for
    // FireActivatePlacement below. Added 2026-09-06 for the exe-closeout lane-A
    // verification. The two paths prove different halves and neither alone is enough:
    // FireActivatePlacement drives the real CLICK -> tool-mode-tree -> PlacementTool
    // ACTIVATION but stops at the armed tool (committing needs a real left-click),
    // whereas this path also drives the faithful CREATE so a building actually
    // materializes. Without the override the only template that could be driven to
    // completion here was WoodHouse, so the two food lodges
    // (Template\HunterLodge.rsc, Template\GathererShelter.rsc) had no way to be
    // placed through the real tool at all -- which is what starttown.cpp's
    // "placement for the two lodges is unported" comment actually described: not a
    // missing TOOL (PlacementTool is fully ported and its CreatePlacedDescription
    // node is in the tool-mode tree), but a missing way to point it at them.
    const char* kTemplate = "Template\\WoodHouse.rsc";
    if (const char* e = Opt().placementTemplate) {
        if (e[0]) kTemplate = e;
    }
    fprintf(stderr, "[ToolHarness] resolving building template \"%s\"...\n", kTemplate);
    ComponentDescription* tmpl = StartTown_ResolveTemplateByPath(kTemplate);
    fprintf(stderr, "[ToolHarness] template -> %p\n", tmpl);
    if (!tmpl) {
        fprintf(stderr, "[ToolHarness] ERROR: template resolve returned null\n");
        return;
    }

    // PlacementTool_Factory_SA is PlacementTool::CreateNewObject.
    PlacementTool* tool = PlacementTool_Factory_SA();
    if (!tool) {
        fprintf(stderr, "[ToolHarness] ERROR: PlacementTool_Factory_SA returned null\n");
        return;
    }

    // descriptionPtr (+0x20) = the building description entity Startup searches.
    tool->descriptionPtr = tmpl;
    fprintf(stderr, "[ToolHarness] PlacementTool=%p descriptionPtr(+0x20)=%p — running Startup probe...\n",
           tool, tmpl);

    int ok = PlacementTool_HarnessStartupProbe(tool);
    fprintf(stderr, "[ToolHarness] PlacementTool Startup probe -> %d\n", ok);
    if (!ok) {
        fprintf(stderr, "[ToolHarness] Startup probe failed — skipping CREATE (Phase 2)\n");
        return;
    }

    // Phase 2: place the building at map centre (same valley cell the RemovalTool
    // harness targets), cell(124,124), or the caller's centre. The cell index uses the
    // map's own width (Map::size); the grid is not 256 wide on every map.
    //
    // A WoodHouse is 3x3 and the valley centre is reliably clear enough for it, so the
    // original code placed at exactly one cell and reported the result. The food
    // lodges are larger and the valley centre is frequently already occupied by the
    // start town itself, so a single fixed cell reports "0" for a tool that is working
    // perfectly -- a false negative of exactly the kind this codebase keeps being
    // burned by. Sweep an outward ring around the centre, stop at the first cell the
    // tool ACCEPTS, and say which cell won. A run that exhausts the ring is a real
    // failure; a run that succeeds at radius 9 is not.
    const int width = GameUpdateLoop::_singleton->m_game->size;
    int centerRow = 124, centerCol = 124;
    if (centerRowIn >= 0 && centerColIn >= 0) {
        centerRow = centerRowIn;
        centerCol = centerColIn;
    }
    if (const char* pc = Opt().placementCell) {
        int r = 0, c = 0;
        if (std::sscanf(pc, "%d,%d", &r, &c) == 2) { centerRow = r; centerCol = c; }
    }
    int maxR = 24;
    if (const char* mr = Opt().placementRadius) {
        int v = std::atoi(mr); if (v > 0) maxR = v;
    }
    int placed = 0, usedRow = -1, usedCol = -1, tried = 0;
    for (int r = 0; r <= maxR && !placed; ++r) {
        for (int dr = -r; dr <= r && !placed; ++dr) {
            for (int dc = -r; dc <= r && !placed; ++dc) {
                // Ring only: skip the interior, which the earlier radii already covered.
                if (r > 0 && dr > -r && dr < r && dc > -r && dc < r) continue;
                const int row = centerRow + dr, col = centerCol + dc;
                if (row < 1 || col < 1 || row > width - 2 || col > width - 2) continue;
                ++tried;
                if (PlacementTool_HarnessPlaceAtCell(tool, row * width + col)) {
                    placed = 1; usedRow = row; usedCol = col;
                }
            }
        }
    }
    fprintf(stderr, "[ToolHarness] PlacementTool CREATE -> %d (1=building materialized) "
           "template=\"%s\" cell=(%d,%d) after %d candidate(s) around (%d,%d)\n",
           placed, kTemplate, usedRow, usedCol, tried, centerRow, centerCol);
    if (!placed)
        fprintf(stderr, "[ToolHarness] PlacementTool CREATE FAILED: no cell within r=%d of "
               "(%d,%d) was accepted -- widen with BANISHED_PLACEMENT_RADIUS or move the "
               "centre with BANISHED_PLACEMENT_CELL=<row>,<col> before calling this a "
               "tool defect.\n", maxR, centerRow, centerCol);
    fprintf(stderr, "[ToolHarness] PlacementTool sequence complete.\n");
}

// GUI_PLAN Keystone F, Phase 3: exercise the REAL toolbar-click activation path.
// Instead of hand-building the tool (FirePlacementTool), resolve a building descriptor
// and call SetEditMode — exactly what ToolbarDialog::OnEvent case-1 does on a click.
// SetEditMode walks the tool-mode tree (built by PlayerToolManager::Startup 0x140101ee0),
// matches CreatePlacedDescription -> PlacementTool, CreateObject's + Startup's the tool,
// and installs it at entityUI+0x28. From there the per-frame tick
// (PlayerToolManager::Update 0x1401026a0 / UpdateHotkeys 0x1400ff6e0) drives the tool:
// the CreateBar dialog opens and the ghost preview follows the mouse; a left-click
// (KeyMap action 5) then places the building. This proves the click->activate->preview
// path without hand-wiring the tool.
void FireActivatePlacement(PlayerToolManager* entityUI) {
    if (!entityUI) { fprintf(stderr, "[ToolHarness] ERROR: entityUI null\n"); return; }
    // BANISHED_PLACEMENT_TEMPLATE overrides the building this drives, so the harness can
    // reproduce a specific toolbar button. Needed for Template\FishermansDock.rsc: it is
    // the one placeable carrying a RadiusDescription, which arms PlacementTool's
    // HighlightAdd path (see Player_HighlightAdd_55430).
    const char* kTemplate = "Template\\WoodHouse.rsc";
    if (const char* e = Opt().placementTemplate) {
        if (e[0]) kTemplate = e;
    }
    fprintf(stderr, "[ToolHarness] resolving building template \"%s\"...\n", kTemplate);
    ComponentDescription* tmpl = StartTown_ResolveTemplateByPath(kTemplate);
    fprintf(stderr, "[ToolHarness] template -> %p\n", tmpl);
    if (!tmpl) { fprintf(stderr, "[ToolHarness] ERROR: template resolve returned null\n"); return; }

    Tool* before = entityUI->currentTool.Get();
    fprintf(stderr, "[ToolHarness] SetEditMode(entityUI=%p, descriptor=%p) — activating PlacementTool "
           "via the tool-mode tree (activeTool before=%p)...\n", entityUI, tmpl, before);
    entityUI->SelectTool(tmpl);   // 0x140102b60

    Tool* tool = entityUI->currentTool.Get();
    fprintf(stderr, "[ToolHarness] after SetEditMode: activeTool(entityUI+0x28)=%p\n", tool);
    if (tool) {
        // The tool the tree installs for a CreatePlacedDescription is the PlacementTool.
        PlacementTool* pt = static_cast<PlacementTool*>(tool);
        fprintf(stderr, "[ToolHarness]   tool vtable=%p back-ptr(+0x10)=%p descriptionPtr(+0x20)=%p "
               "descComp(+0x28)=%p meshInstance(+0x68)=%p\n",
               *reinterpret_cast<void**>(tool),   // the vtable word, logged only  // CAST-OK: crash_walk
               pt->dialogPtr, pt->descriptionPtr, pt->descComponent, pt->meshInstance);
        fprintf(stderr, "[ToolHarness] PlacementTool ACTIVE — per-frame tick (LIVE_TOOLS) now drives "
               "OnKey/Serialize; move the mouse over the map + left-click to place.\n");
    } else {
        fprintf(stderr, "[ToolHarness] activation FAILED — no tool at entityUI+0x28 (tree miss or "
               "CreateObject null). Ensure BANISHED_LIVE_TOOLS=1.\n");
    }
}

// Road bring-up: activate the RoadTool through the REAL SetEditMode path — exactly what
// a Toolbar.rsc road-button click does — then drive the tool's own CalcCost (slot 14)
// and CalcPath (slot 13) over a fixed cell span. This is cursor-free: DragTool_OnKey's
// drag needs a live OS cursor over the map (ScreenToCell), which the harness has none
// of, so we set startCell/endCell directly and dispatch the two slots the drag would.
//
// CalcCost proves the road A* (RoadTool_sub_ce00) resolves a path and prices it;
// CalcPath is what actually commits the road. Env: BANISHED_TOOL_HARNESS=road.
void FireRoadTool(PlayerToolManager* entityUI) {
    if (!entityUI) { fprintf(stderr, "[ToolHarness] ERROR: entityUI null\n"); return; }

    // Both road templates carry a CreateRoadDescription; DirtRoad is the basic one the
    // Toolbar "Roads" group opens with. Fall back to StoneRoad if it fails to resolve.
    // BANISHED_ROAD_TEMPLATE=stone flips the preference — the stone template is the only
    // work item with a non-null material (+0x50), i.e. the resource-fetch branch repro.
    const char* kTemplates[] = { "Template\\DirtRoad.rsc", "Template\\StoneRoad.rsc" };
    {
        const char* pref = Opt().roadTemplate;
        if (pref && (pref[0] == 's' || pref[0] == 'S')) {
            kTemplates[0] = "Template\\StoneRoad.rsc";
            kTemplates[1] = "Template\\DirtRoad.rsc";
        }
    }
    ComponentDescription* tmpl = nullptr;
    const char* used = nullptr;
    for (const char* path : kTemplates) {
        fprintf(stderr, "[ToolHarness][road] resolving template \"%s\"...\n", path);
        tmpl = StartTown_ResolveTemplateByPath(path);
        fprintf(stderr, "[ToolHarness][road]   -> %p\n", tmpl);
        if (tmpl) { used = path; break; }
    }
    if (!tmpl) { fprintf(stderr, "[ToolHarness][road] ERROR: no road template resolved\n"); return; }

    fprintf(stderr, "[ToolHarness][road] SetEditMode(entityUI=%p, descriptor=%p from %s) — "
           "activating RoadTool via the tool-mode tree...\n", entityUI, tmpl, used);
    entityUI->SelectTool(tmpl);   // 0x140102b60

    Tool* tool = entityUI->currentTool.Get();
    fprintf(stderr, "[ToolHarness][road] after SetEditMode: activeTool(entityUI+0x28)=%p\n", tool);
    if (!tool) {
        fprintf(stderr, "[ToolHarness][road] activation FAILED — tree miss or CreateObject null. "
               "Expect a node{CreateRoad -> RoadTool} in the [Tools] BuildToolModeTree line.\n");
        return;
    }
    // The tool the tree installs for a CreateRoadDescription is the RoadTool.
    RoadTool* road = static_cast<RoadTool*>(tool);
    // RoadTool::Startup (slot 4) caches the CreateRoadDescription at +0x38; a null there
    // means the descriptor carried no road component and CalcPath/CalcCost will bail.
    fprintf(stderr, "[ToolHarness][road]   vtable=%p controller(+0x10)=%p descriptionPtr(+0x20)=%p "
           "roadDesc(+0x38)=%p\n",
           *reinterpret_cast<void**>(tool),   // the vtable word, logged only  // CAST-OK: crash_walk
           road->dialogPtr, road->descriptionPtr, road->roadDesc);

    // Pick a span the road rules can actually accept. CalcCost/CalcPath require BOTH
    // endpoints to be unoccupied (cell+0x00 entity ptr == 0) and on road-legal terrain
    // ((1 << (cell+0x29 & 0x1f)) & 0x5e0) == 0 — cell stride 0x30, array at mapGrid+0x80.
    // Hardcoding a span just hits the start-town buildings and trees, so scan outward
    // from the valley centre for a clear horizontal run instead (same idea as the
    // PlacementTool harness's ring scan).
    GameUpdateLoop* gw = GameUpdateLoop::_singleton;
    Map* mapGrid = gw ? gw->m_game : nullptr;
    MapCell* terrain = mapGrid ? mapGrid->cells.GetData() : nullptr;
    if (!terrain) {
        fprintf(stderr, "[ToolHarness][road] ERROR: no terrain array (gw=%p mapGrid=%p)\n", gw, mapGrid);
        return;
    }
    const int gridSize = mapGrid->size;
    auto cellClear = [&](int row, int col) -> bool {
        if (row < 0 || col < 0 || row >= gridSize || col >= gridSize) return false;
        const MapCell& c = terrain[(uintptr_t)(gridSize * row + col)];
        if (c.blocker != nullptr) return false;                              // occupied
        uint32_t flag = 1u << (c.terrainType & 0x1f);
        return (flag & 0x5e0) == 0;                                          // legal terrain
    };

    // BANISHED_ROAD_RUNS=<n> / BANISHED_ROAD_LEN=<tiles> — lay n spans instead of one.
    //
    // Highwaymen wants 2,000 stone-paved road tiles. One 6-tile demo span proves the
    // RoadTool works and can never approach that, and the scan below is already
    // self-advancing: a committed span occupies its cells, so the next pass finds a
    // different clear run on its own. Defaults keep the single short span the tool
    // test expects.
    // Site the spans AROUND THE TOWN, not on a fixed row.
    //
    // The scan used to start at row 124 regardless of where the town is. Every span it
    // found was legal ground and committed fine, so the harness reported "20/20 placed"
    // — but BuildRoad::Execute's first act is
    //     Map::CheckPassable(0xe1f, citizenCell, targetCell)
    // and a builder standing in town cannot reach a road drawn across a river or a ridge.
    // Measured with BANISHED_ROAD_CENSUS on the old scan: 262,832 of 263,649 Execute calls
    // bailed on that guard, and not one tile was ever paved. Same trap the cemetery siting
    // hit (docs/history/waves/achievements_wave.md §5.1): "the tool accepted it" is not "a citizen
    // can walk there".
    int roadTownCol = gridSize / 2, roadTownRow = gridSize / 2;
    {
        const uint32_t tidMap = MapComponent::TypeId();
        int32_t sumCol = 0, sumRow = 0; int n = 0;
        const int houses = StartTown_GetFoundingHouseCount();
        for (int i = 0; i < houses && tidMap; ++i) {
            ComponentEntity* e = StartTown_GetFoundingHouseEntity(i);
            if (!e) continue;
            const int cc = e->_components._size;
            Object::InternalPtr<Component>* ca = e->_components._data;
            for (int k = 0; k < cc && ca; ++k) {
                Component* comp = ca[k].Get();
                if (!comp) continue;
                if (comp->Type() != tidMap) continue;
                const MapComponent* mc = static_cast<const MapComponent*>(comp);   // Type() == MapComponent's id
                sumCol += mc->tileX0;
                sumRow += mc->tileY0;
                ++n;
                break;
            }
        }
        if (n > 0) { roadTownCol = static_cast<int>(sumCol / n); roadTownRow = static_cast<int>(sumRow / n); }
        fprintf(stderr, "[ToolHarness][road] town centre = (col %d, row %d) from %d founding "
               "house(s)%s\n", roadTownCol, roadTownRow, n, n ? "" : " -- falling back to map centre");
    }

    int wantRuns = 1;
    if (const char* rr = Opt().roadRuns) { int v = std::atoi(rr); if (v > 0) wantRuns = v; }
    int runLen = 5;
    if (const char* rl = Opt().roadLen) { int v = std::atoi(rl); if (v > 0) runLen = v; }
    int laid = 0, tiles = 0;
    for (int run = 0; run < wantRuns; ++run) {
        const int kRun = runLen;
        int startCell = -1, endCell = -1, foundRow = -1, foundCol = -1;
        // A committed road does NOT set the cell occupancy field this scan tests, so
        // scanning from the same centre every run re-picked the identical span 120
        // times. Start each run further out; the sweep still covers the map.
        for (int dr = run; dr < run + 120 && startCell < 0; ++dr) {
            for (int sign = 0; sign < 2 && startCell < 0; ++sign) {
                int row = roadTownRow + (sign ? -dr : dr);
                int colLo = roadTownCol - 90; if (colLo < 8) colLo = 8;
                int colHi = roadTownCol + 90; if (colHi > gridSize - 8) colHi = gridSize - 8;
                for (int col = colLo; col + kRun < colHi; ++col) {
                    bool allClear = true;
                    for (int k = 0; k <= kRun; ++k)
                        if (!cellClear(row, col + k)) { allClear = false; break; }
                    if (allClear) {
                        foundRow = row; foundCol = col;
                        startCell = gridSize * row + col;
                        endCell   = gridSize * row + col + kRun;
                        break;
                    }
                }
            }
        }
        if (startCell < 0) {
            fprintf(stderr, "[ToolHarness][road] ERROR: found no clear %d-tile run to test\n", kRun + 1);
            break;
        }
        road->startCell = startCell;
        road->endCell   = endCell;
        if (run < 3) fprintf(stderr, "[ToolHarness][road] drag span: row=%d col=%d..%d startCell=%d endCell=%d\n",
               foundRow, foundCol, foundCol + kRun, startCell, endCell);

        // The tool at EntityUI+0x28 is the RoadTool: a DragTool (src/game/Player).
        // Dispatched through DragTool (not the final RoadTool): the slots stay virtual.
        DragTool* drag = static_cast<DragTool*>(tool);

        // DragTool::CalcCost (retail [14], +0x70) — the per-frame preview call
        // DragTool_OnKey makes while dragging: (start, end, &pathType, &tileH,
        // &tileW, &extra) -> int. The harness's local out-types are its own; the
        // pointers are the same bytes.
        {
            uint32_t pathType = 0; int tileH = 0, tileW = 0; int extra = 0;   // printed %d / %u as before
            int rc = drag->CalcCost(startCell, endCell,
                                    &pathType, &tileH, &tileW,
                                    &extra);
            // RoadTool's CalcCost returns 0 on success / 3 on reject (it is NOT a count —
            // that differs from RemovalTool). The path length comes back in the out-params,
            // and param_4 is the validity: 1 = valid, 2 = invalid.
            if (run < 3) fprintf(stderr, "[ToolHarness][road] CalcCost -> rc=%d (0=ok,3=reject) valid=%d "
                   "(1=valid,2=invalid) pathLen=%u/%u\n", rc, pathType, tileH, tileW);
        }

        // DragTool::CalcPath (retail [13], +0x68) — the COMMIT the drag-release
        // makes. Returns true when the road was placed.
        {
            bool placed = drag->CalcPath(startCell, endCell);
            if (placed) { ++laid; tiles += kRun + 1; }
            if (run < 3 || !placed)
                fprintf(stderr, "[ToolHarness][road] CalcPath (commit) -> %d (1=road placed)\n", placed ? 1 : 0);
            if (!placed) {
                fprintf(stderr, "[ToolHarness][road] NOTE: slot13 is still the Tool_Ret0 stub unless "
                       "RoadTool_CalcPath_Impl has been wired — a 0 here is expected in that case.\n");
            }
        }
    }
    fprintf(stderr, "[ToolHarness][road] sequence complete: %d/%d span(s) placed, ~%d tiles"
                    " (BANISHED_ROAD_RUNS=%d len=%d)\n", laid, wantRuns, tiles, wantRuns, runLen);
}

// ---------------------------------------------------------------------------
// FencedTool (pasture) — activate via the tool-mode tree exactly like a toolbar
// pasture click (SetEditMode with Template\Pasture.rsc), then drive the drag
// slots over a scanned-clear rect: slot 14 (CalcCost, rect validation) and
// slot 13 (CalcPath, the ported commit that creates master + shelter + fence).
// Env: BANISHED_TOOL_HARNESS=pasture. Pasture.rsc _minWidth/_minHeight = 7.
// ---------------------------------------------------------------------------
void FirePastureTool(PlayerToolManager* entityUI) {
    if (!entityUI) { fprintf(stderr, "[ToolHarness] ERROR: entityUI null\n"); return; }

    // BANISHED_PLACEMENT_TEMPLATE also overrides the FENCED tool, so this path can
    // place any drag-area building - crop field and orchard included - through the
    // real tool rather than the instant-build spawner. That distinction matters:
    // spawned-complete fields are seated by StartTown_ActivateWorkplace, so only a
    // TOOL-placed field reproduces the missing-profession case.
    const char* kTemplate = "Template\\Pasture.rsc";
    if (const char* e = Opt().placementTemplate) {
        if (e[0]) kTemplate = e;
    }
    fprintf(stderr, "[ToolHarness][pasture] resolving template \"%s\"...\n", kTemplate);
    ComponentDescription* tmpl = StartTown_ResolveTemplateByPath(kTemplate);
    fprintf(stderr, "[ToolHarness][pasture]   -> %p\n", tmpl);
    if (!tmpl) { fprintf(stderr, "[ToolHarness][pasture] ERROR: template resolve failed\n"); return; }

    fprintf(stderr, "[ToolHarness][pasture] SetEditMode — activating FencedTool via the tree...\n");
    entityUI->SelectTool(tmpl);   // 0x140102b60

    Tool* tool = entityUI->currentTool.Get();
    fprintf(stderr, "[ToolHarness][pasture] after SetEditMode: activeTool=%p\n", tool);
    if (!tool) {
        fprintf(stderr, "[ToolHarness][pasture] activation FAILED — check the [Tools] fenced node line.\n");
        return;
    }
    // The tool the tree installs for a CreateFencedDescription is the FencedTool.
    FencedTool* fenced = static_cast<FencedTool*>(tool);
    fprintf(stderr, "[ToolHarness][pasture]   vtable=%p fencedDesc(+0x38)=%p areaDesc(+0x40)=%p "
           "fenceDesc(+0x48)=%p\n",
           *reinterpret_cast<void**>(tool),   // the vtable word, logged only  // CAST-OK: crash_walk
           fenced->fencedDesc, fenced->areaDesc, fenced->fenceDesc);

    // Scan outward from the valley centre for a clear 8x8 block (min size is 7).
    GameUpdateLoop* gw = GameUpdateLoop::_singleton;
    Map* mapGrid = gw ? gw->m_game : nullptr;
    MapCell* terrain = mapGrid ? mapGrid->cells.GetData() : nullptr;
    if (!terrain) { fprintf(stderr, "[ToolHarness][pasture] ERROR: no terrain array\n"); return; }
    const int gridSize = mapGrid->size;
    auto cellClear = [&](int row, int col) -> bool {
        if (row < 0 || col < 0 || row >= gridSize || col >= gridSize) return false;
        const MapCell& c = terrain[(uintptr_t)(gridSize * row + col)];
        if (c.blocker != nullptr) return false;                              // entity in cell
        if (c.footprintCount != 0) return false;                             // occupied flag
        uint32_t flag = 1u << (c.terrainType & 0x1f);
        return (flag & 0x5e0) == 0;                                          // buildable terrain
    };

    // BANISHED_PASTURE_OVER_NATURALS=1: pick a rect that CONTAINS trees/rocks
    // (occupant slot cell+8 non-null on >=3 cells) — the manual-play repro where
    // builders must footprint-clear the site before it can build. CalcCost still
    // passes: naturals occupy cell+8, its occupancy check reads only cell+0x00.
    const bool wantNaturals = [] {
        const char* e = Opt().pastureOverNaturals;
        return e && *e && *e != '0';
    }();
    auto cellHasNatural = [&](int row, int col) -> bool {
        if (row < 0 || col < 0 || row >= gridSize || col >= gridSize) return false;
        return terrain[(uintptr_t)(gridSize * row + col)].occupant != nullptr;
    };

    // BANISHED_FENCED_RUNS=<n> / BANISHED_FENCED_SIDE=<tiles> — lay n fenced areas.
    //
    // Cemetery.rsc declares "createfenced", so a cemetery is a FENCED AREA like a
    // pasture: the FencedTool is what creates its gate as a MultiObject child, and
    // CemeteryComponent::BuildPlotGrid needs that child to lay out its plots. A
    // cemetery dropped by the stress placer (a plain placeStructure) has no child
    // array, bails, has no plots, and buries nobody -- which is why every death in
    // this port reported NO GRAVE. One 8x8 fenced cemetery already yields graves;
    // Tombstone wants 400 of them, so the area has to be repeatable and bigger.
    int fencedRuns = 1;
    if (const char* fr = Opt().fencedRuns) { int v = std::atoi(fr); if (v > 0) fencedRuns = v; }
    int fencedSide = 8;
    if (const char* fs = Opt().fencedSide) { int v = std::atoi(fs); if (v > 0) fencedSide = v; }
    int fencedPlaced = 0;
    // Site the areas around the TOWN, not the map centre. Two things depend on it:
    // Cemetery_FindNearestWithSlot needs ValidateCell(0xe1f) region reachability from
    // the death cell, and the death cells are in the town. Scanning the whole map
    // placed 25 cemeteries and produced 53 graves -- about four cemeteries' worth,
    // the rest in regions the town cannot reach -- while a band on the MAP centre
    // placed 2, because the start town is nowhere near it. Average the founding
    // houses' MapComponent cells, exactly as the bridge harness does.
    int fencedTownCol = gridSize / 2, fencedTownRow = gridSize / 2;
    {
        const uint32_t tidMap = MapComponent::TypeId();
        int32_t sumCol = 0, sumRow = 0; int nh = 0;
        const int houses = StartTown_GetFoundingHouseCount();
        for (int i = 0; i < houses && tidMap; ++i) {
            ComponentEntity* e = StartTown_GetFoundingHouseEntity(i);
            if (!e) continue;
            const int cc = e->_components._size;
            Object::InternalPtr<Component>* ca = e->_components._data;
            for (int k = 0; k < cc && ca; ++k) {
                Component* comp = ca[k].Get();
                if (!comp) continue;
                if (comp->Type() != tidMap) continue;
                const MapComponent* mc = static_cast<const MapComponent*>(comp);   // Type() == MapComponent's id
                sumCol += mc->tileX0;
                sumRow += mc->tileY0;
                ++nh;
                break;
            }
        }
        if (nh > 0) { fencedTownCol = static_cast<int>(sumCol / nh);
                      fencedTownRow = static_cast<int>(sumRow / nh); }
        fprintf(stderr, "[ToolHarness][pasture] town centre = (col %d, row %d) from %d "
                        "founding house(s)%s\n", fencedTownCol, fencedTownRow, nh,
                nh ? "" : " — falling back to map centre");
    }
    for (int frun = 0; frun < fencedRuns; ++frun) {
        const int kSide = fencedSide;
        int startCell = -1, endCell = -1;
        for (int r = frun; r < frun + 60 && startCell < 0; ++r) {
            for (int sign = 0; sign < 2 && startCell < 0; ++sign) {
                int row = fencedTownRow + (sign ? -r : r);
                // Keep the areas NEAR TOWN. Cemetery_FindNearestWithSlot requires
            // ValidateCell(0xe1f) region reachability from the DEATH cell, so an
            // area dropped anywhere clear on the map is usually in a region the
            // town cannot reach and is silently never used: scanning the whole
            // map placed 25 cemeteries and produced 53 graves, about four
            // cemeteries' worth, with the rest unreachable.
            const int colLo = fencedTownCol - 90, colHi = fencedTownCol + 90;
            for (int col = colLo; col + kSide < colHi; ++col) {
                    bool clear = true;
                    int naturals = 0;
                    for (int dy = 0; dy < kSide && clear; ++dy)
                        for (int dx = 0; dx < kSide && clear; ++dx) {
                            if (!cellClear(row + dy, col + dx)) clear = false;
                            else if (cellHasNatural(row + dy, col + dx)) ++naturals;
                        }
                    // cellClear only tests cell+0x00/+0x28/terrain — trees and rocks sit in the
                    // cell+8 occupant slot, so without this the "clear block" scan happily picks
                    // a rect with 20+ naturals in it and the two investigations get conflated.
                    if (clear && (wantNaturals ? naturals >= 3 : naturals == 0)) {
                        startCell = gridSize * row + col;
                        endCell   = gridSize * (row + kSide - 1) + col + kSide - 1;
                        if (frun < 3) fprintf(stderr, "[ToolHarness][pasture] rect: (%d,%d)-(%d,%d) naturals=%d\n",
                               col, row, col + kSide - 1, row + kSide - 1, naturals);
                        break;
                    }
                }
            }
        }
        if (startCell < 0) {
            if (frun < 3) fprintf(stderr, "[ToolHarness][pasture] ERROR: no clear %dx%d block found\n", kSide, kSide);
            continue;   // this run found no room; the next starts further out
        }
        fenced->startCell = startCell;
        fenced->endCell   = endCell;

        // The FencedTool: a DragTool. CalcCost is retail [14] (+0x70), CalcPath [13] (+0x68).
        // Dispatched through DragTool (not the final FencedTool): the slots stay virtual.
        DragTool* drag = static_cast<DragTool*>(tool);
        {
            uint32_t outW = 0; int rectW = 0, rectH = 0, segs = 0;
            int rc = drag->CalcCost(startCell, endCell, &outW, &rectW, &rectH, &segs);
            fprintf(stderr, "[ToolHarness][pasture] CalcCost -> rc=%d (0=ok,1=small,2=big,3=blocked) "
                   "w=%d h=%d perimeter=%d\n", rc, rectW, rectH, segs);
        }
        {
            const bool placed = drag->CalcPath(startCell, endCell);   // AL = success
            if (placed) ++fencedPlaced;
            if (frun < 3) fprintf(stderr, "[ToolHarness][pasture] CalcPath (commit) -> %d (1=pasture placed)\n",
                   placed ? 1 : 0);
        }
    }
    fprintf(stderr, "[ToolHarness][pasture] sequence complete: %d/%d area(s) placed, side %d\n",
            fencedPlaced, fencedRuns, fencedSide);
}

// GUI_PLAN Keystone F, Phase 3B: activate the PlacementTool via the real
// SetEditMode path (as "click" does), then drive the ghost-PREVIEW pipeline for a
// fixed map cell. Serialize's preview normally follows the OS cursor (sub_da00),
// which the headless harness lacks; this proves the preview transform + scene
// submission work by planting a visible ghost mesh at a known cell. Confirms the
// Phase-3B TransformFromYaw fix (was null in standalone → invisible ghost).
void FirePreviewPlacement(PlayerToolManager* entityUI) {
    FireActivatePlacement(entityUI);
    Tool* tool = entityUI ? entityUI->currentTool.Get() : nullptr;
    if (!tool) {
        fprintf(stderr, "[ToolHarness] preview: no active tool — cannot drive ghost preview\n");
        return;
    }
    // Prerequisite check for the CreateBar populate (Phase 3B item 2): drive OnKey
    // (+0x30) with param2=1 — its dialog-open branch calls OpenToolDialog(*(tool+0x10))
    // which loads Dialog/CreateBar.rsc + AddDialog's it, writing the dialog IBP into
    // the controller's +0x00. tool+0x10 = the EntityUI/controller (SetEditMode sets it).
    // Log the controller's +0x00 before/after so we can confirm the CreateBar opens.
    {
        PlayerToolManager* controller = tool->dialogPtr;   // = EntityUI (dialog controller)
        // field_0x00, the dialog IBP, is logged as an integer (%llx).
        uint64_t before = controller ? reinterpret_cast<uint64_t>(controller->field_0x00) : 0;   // +0x00  // CAST-OK: ptr_int
        // EntityUI+0x28 is the active Tool; slot 6 (+0x30) is Tool::OnKey; param 1 -> open CreateBar.
        tool->OnKey(1);
        uint64_t after = controller ? reinterpret_cast<uint64_t>(controller->field_0x00) : 0;   // +0x00  // CAST-OK: ptr_int
        fprintf(stderr, "[ToolHarness] CreateBar open probe: controller(tool+0x10)=%p dialogIBP(+0x00) "
               "before=%llx after=%llx -> %s\n", controller,
               (unsigned long long)before, (unsigned long long)after,
               (after != 0) ? "OPENED" : "not opened");
    }

    const int centerCell = Cell(124, 124);
    fprintf(stderr, "[ToolHarness] driving ghost PREVIEW at cell %d (124,124)...\n", centerCell);
    int r = PlacementTool_HarnessPreviewAtCell(static_cast<PlacementTool*>(tool), centerCell);   // the active tool is the PlacementTool this harness made
    fprintf(stderr, "[ToolHarness] ghost PREVIEW -> %d (1=mesh submitted with a valid transform)\n", r);

    // Phase 3B item 2: drive operator_fc60 (CreateBar populate + position) once against
    // the now-open dialog, mirroring PlacementTool::Serialize's exact arg list, to prove
    // the newly-ported populate path runs crash-free (headless can't move the cursor, so
    // the tick's Serialize never reaches this call on its own). box = meshInstance+0x34
    // (the built bbox), transform via Geometry::GetTransform(mesh); cost = empty string.
    {
        PlayerToolManager* controller = tool->dialogPtr;   // EntityUI / dialog controller
        // The tool FireActivatePlacement installed is the PlacementTool.
        Graphics::MeshInstance* mesh = static_cast<PlacementTool*>(tool)->meshInstance;
        if (controller && mesh &&
            controller->field_0x00 != nullptr /*dialog open*/) {
            const ::Box* box = &mesh->_localBounds;   // Geometry +0x34
            // Geometry::GetTransform(mesh) -> Transform by value (member; import slot
            // 0x140151460 ?GetTransform@Geometry@Graphics@@, 0x180015800).
            const Math::Transform xf(mesh->GetTransform());
            System::String costStr("");   // empty System::String
            fprintf(stderr, "[ToolHarness] driving operator_fc60 (CreateBar populate+position) "
                   "controller=%p mesh=%p...\n", controller, mesh);
            controller->UpdateCreateBar(0, 0, &costStr, /*valid*/1, /*p6*/1, /*p7*/0, /*p8*/1,
                            /*startCell*/-1, /*endCell*/-1, box, &xf);   // 0x1400ffc60
            fprintf(stderr, "[ToolHarness] operator_fc60 returned — CreateBar populate ran crash-free\n");
        } else {
            fprintf(stderr, "[ToolHarness] operator_fc60 probe skipped (controller=%p mesh=%p "
                   "dialogOpen=%d)\n", controller, mesh,
                   controller ? (controller->field_0x00 != nullptr) : 0);
        }
    }
}

// GUI_PLAN Keystone F, Phase 4: exercise the Utility-dialog open path (toolbar
// case 2). A utility toolbar item (statusbar/eventlog/map/profession/limit/help)
// carries a Description ptr at item+0x58 (a resolved Template\Utility*.rsc);
// Toolbar_Utility_Impl finds its UIDescription child, builds a DialogInstance with
// the ToolUI child vtable, and AddDialog's it. This fires that path with a resolved
// template as the param — proving open + subsequent-frame render are crash-free
// without a live cursor/click. Template overridable via BANISHED_UTILITY_TEMPLATE.
void FireUtilityDialog() {
    const char* tmplName = Opt().utilityTemplate;
    if (!tmplName || tmplName[0] == '\0') tmplName = "Template\\UtilityMap.rsc";
    fprintf(stderr, "[ToolHarness] resolving utility template \"%s\"...\n", tmplName);
    ComponentDescription* desc = StartTown_ResolveTemplateByPath(tmplName);
    fprintf(stderr, "[ToolHarness] utility Description -> %p\n", desc);
    if (!desc) {
        fprintf(stderr, "[ToolHarness] ERROR: utility template resolve returned null\n");
        return;
    }

    GameUpdateLoop* gameWorld = GameUpdateLoop::_singleton;
    if (!gameWorld) { fprintf(stderr, "[ToolHarness] ERROR: gameWorld null\n"); return; }
    PlayerToolManager* gameSys = gameWorld->m_toolManager;

    // --- Minimap GPU-buffer bring-up (BEFORE opening the dialog) --------------
    // The minimap subsystem (gameWorld+0x148) has its CPU pixel arrays allocated
    // but never painted / uploaded in standalone. Optionally paint a test pattern
    // (default ON for the map harness so there's visible content), then create +
    // upload the GPU ImageBuffers so MapUI::SetupImage has valid textures to bind.
    {
        Minimap* mapObj = gameWorld->m_imageCapture;
        bool isMap = std::strstr(tmplName, "Map") != nullptr;
        const char* tp = Opt().mapTestPattern;
        bool testPattern = isMap && !(tp && (tp[0] == '0'));  // default ON for map
        if (mapObj && isMap) {
            int width = mapObj->size;
            uint32_t* terr = mapObj->terrainPixels._data;
            uint32_t* over = mapObj->overlayPixels._data;
            if (testPattern && terr && width > 0) {
                // Terrain: 16-cell checkerboard of two greens + blue diagonal.
                for (int y = 0; y < width; y++)
                    for (int x = 0; x < width; x++) {
                        uint32_t c = ((x >> 4) ^ (y >> 4)) & 1 ? 0xff2f8f2fu : 0xff1f5f1fu;
                        if (((x - y) & 0x3f) < 3) c = 0xff3f6fcfu;  // diagonal river
                        terr[y * width + x] = c;
                    }
                mapObj->overlayDirty = 1;  // mark dirty
                fprintf(stderr, "[MapProbe] painted %dx%d test pattern into terrain array\n", width, width);
            }
            if (testPattern && over && width > 0) {
                // Overlay: red border rectangle, otherwise transparent.
                for (int y = 0; y < width; y++)
                    for (int x = 0; x < width; x++) {
                        bool border = x < 2 || y < 2 || x >= width - 2 || y >= width - 2;
                        over[y * width + x] = border ? 0xffff0000u : 0x00000000u;
                    }
            }
            Minimap_CreateAndUpload(mapObj);
            fprintf(stderr, "[MapProbe] Minimap_CreateAndUpload done — buffers +0x08=%p +0x10=%p\n",
                   mapObj->terrainImage, mapObj->overlayImage);
        }
    }

    fprintf(stderr, "[ToolHarness] gameSubsys(gameWorld+0x120)=%p — opening utility dialog "
           "(param=Description %p)...\n", gameSys, desc);

    ToolbarUtility_HarnessOpen(gameSys, desc);
    fprintf(stderr, "[ToolHarness] utility-dialog open returned — dialog should now render "
           "(watch subsequent frames for crashes).\n");

    // Drive controller setup explicitly for the Map (Slot4 SetupImage binds the
    // uploaded terrain/overlay textures). The content sub-tree is often inserted a frame
    // or more after the open, so the +90-frame delayed rescan (ToolHarness_Tick) is what
    // usually lands it; this same-frame attempt is a best-effort. Professions are NOT
    // re-driven: AddDialog's open already ran ProfessionUI::OnOpened (slot 5), and a
    // second run re-clones the table rows (B9 RES, known_port_issues row 1200).
    if (std::strstr(tmplName, "Map") != nullptr)
        ToolbarUtility_HarnessDriveController("MapUI", 4);

    // --- Minimap image-buffer probe (map-render bring-up) ---------------------
    // MapUI::Slot4 (SetupImage) reads the minimap subsystem at gameWorld+0x148,
    // then its terrain ImageBuffer (+0x08) and overlay ImageBuffer (+0x10). If
    // those buffers are null in standalone the minimap can't render. Report them.
    {
        Minimap* mapObj = gameWorld->m_imageCapture;
        fprintf(stderr, "[MapProbe] gameWorld+0x148 (minimap subsystem) = %p\n", mapObj);
        if (mapObj) {
            // A raw qword / dword dump of the object's bytes, not a field read.
            uint64_t* q = reinterpret_cast<uint64_t*>(mapObj);   // CAST-OK: crash_walk
            for (int i = 0; i < 8; i++)
                fprintf(stderr, "[MapProbe]   +0x%02x = 0x%016llx\n", i * 8,
                       (unsigned long long)q[i]);
            int* w = reinterpret_cast<int*>(mapObj);   // CAST-OK: crash_walk
            fprintf(stderr, "[MapProbe]   ints +0x00=%d +0x04=%d +0x18=%d +0x1c=%d\n",
                   w[0], w[1], w[6], w[7]);
            // Sample the raw pixel arrays: +0x18 terrain, +0x28 overlay.
            uint32_t* terr = mapObj->terrainPixels._data;
            uint32_t* over = mapObj->overlayPixels._data;
            int width = w[1];   // Minimap::size
            auto sampleArr = [width](const char* nm, uint32_t* a) {
                if (!a) { fprintf(stderr, "[MapProbe]   %s=null\n", nm); return; }
                int nz = 0, n = width * width;
                for (int i = 0; i < n; i++) if (a[i]) nz++;
                fprintf(stderr, "[MapProbe]   %s nonzero=%d/%d  sample=[%08x %08x %08x %08x] mid=%08x\n",
                       nm, nz, n, a[0], a[1], a[2], a[3], a[n / 2 + width / 2]);
            };
            sampleArr("terrain(+0x18)", terr);
            sampleArr("overlay(+0x28)", over);
        }
    }
}

// GUI (click building/citizen → info dialog): activate the default EditTool via the real
// SetEditMode(pc, 0) path (mode-tree must be built — BANISHED_LIVE_TOOLS=1), then drive the
// cursor-free select scan so an entity's info DialogInstance opens crash-free without a live
// cursor. The live path (real mouse click) runs through EditTool::OnKey every frame.
void FireSelectEntity(PlayerToolManager* entityUI) {
    if (!entityUI) { fprintf(stderr, "[ToolHarness] ERROR: entityUI null\n"); return; }
    EditTool* tool = EditTool_HarnessAcquire(entityUI);   // rejects a non-EditTool
    if (!tool) {
        fprintf(stderr, "[ToolHarness] EditTool activation FAILED (no tool at entityUI+0x28). "
               "Ensure BANISHED_LIVE_TOOLS=1 so the mode-tree (with the key=0 EditTool node) is built.\n");
        return;
    }
    fprintf(stderr, "[ToolHarness] EditTool active (%p)\n", tool);

    // (1) Definitive test: open a captured founding-house building's info dialog directly.
    int houses = StartTown_GetFoundingHouseCount();
    fprintf(stderr, "[ToolHarness] founding houses captured: %d\n", houses);
    int opened = 0;
    for (int i = 0; i < houses && !opened; i++) {
        ComponentEntity* house = StartTown_GetFoundingHouseEntity(i);
        if (!house) continue;
        fprintf(stderr, "[ToolHarness] selecting founding house[%d] entity=%p...\n", i, house);
        opened = EditTool_HarnessSelectEntity(tool, house);
    }

    // (2) Fallback: region-map scan near centre (catches whatever occupies cells).
    if (!opened) {
        fprintf(stderr, "[ToolHarness] no house dialog yet — scanning region map near centre...\n");
        opened = EditTool_HarnessSelectNearby(tool);
    }
    fprintf(stderr, "[ToolHarness] select -> %d (1=an entity's info dialog opened)\n", opened);
}

// Same as FireSelectEntity but targets a CITIZEN recovered from the Picking::Scene,
// so the opened dialog is controlled by CitizenUI (name/age/profession/hearts).
void FireSelectCitizen(PlayerToolManager* entityUI) {
    if (!entityUI) { fprintf(stderr, "[ToolHarness] ERROR: entityUI null\n"); return; }
    EditTool* tool = EditTool_HarnessAcquire(entityUI);   // rejects a non-EditTool
    if (!tool) {
        fprintf(stderr, "[ToolHarness] EditTool activation FAILED. Ensure BANISHED_LIVE_TOOLS=1.\n");
        return;
    }
    fprintf(stderr, "[ToolHarness] EditTool active (%p) — selecting a picked citizen...\n", tool);
    int opened = EditTool_HarnessSelectCitizen(tool);
    fprintf(stderr, "[ToolHarness] selectcitizen -> %d (1=a CitizenUI dialog opened)\n", opened);
}

// ============================================================
// Shared mark-for-removal BACKEND test ("markremoval")
// ============================================================
// Proves the resource-removal backend end-to-end WITHOUT the ClearAreaComponent
// (track A) or RemovalTool::CalcPath (track B, still a passthrough). It fires the
// exact 3-step pattern the retail ClearAreaDescription_helper_7d60 (0x140117d60)
// uses per footprint cell, on ONE real map entity:
//   1. FUN_1400c2ea0(entity)  — MarkForRemoval: notifies each component (slot+0x38)
//      then sets flag bit 0x400 at entity+0x40. (registered: reg_action_hunt)
//   2. FUN_140003110(&removalList, count+1, &entity) — push onto EntityList's
//      pending-removal list at +0xD0. (registered: reg_action_hunt)
// Then EntityList::Update Step 5 (entitylist.cpp) drains removalList every tick via
// EntityList_RemoveEntity and clears it. This harness marks one entity, verifies the
// flag + list count, then logs removalList._size + the tile slot across the next
// frames so we can watch it drain to 0 and the entity vanish. If this works, both
// track A and track B are just front-ends onto a proven backend.
//
// Map/EntityList layout (verified live + vs Ghidra):
//   gameWorld+0xf8 -> map;  *map = grid width (256);  *(map+0x80) = tile array base.
//   tile stride 0x30 bytes; two entity-ptr slots per cell at +0x00 and +0x08.
//   gameWorld+0xe8 -> EntityList;  removalList IBPArray at +0xD0 {data; int count@+8}.

// Drain-observation state (set by FireMarkRemoval, read each frame after).
bool     s_mr_fired      = false;
ComponentEntity*  s_mr_entity   = nullptr;   // the marked entity ptr (post-CheckPtr)
Object::InternalPtr<ComponentEntity>* s_mr_tileSlot = nullptr;   // the tile slot it came from (MapCell blocker / occupant)
int      s_mr_fireFrame  = 0;

// Pinned-panel residual-risk observer state (see FirePinPanel).
bool     s_pin_fired     = false;
UI::DialogController* s_pin_ctrl = nullptr;  // EntityUI controller (its +0x28 = datasource IBP)
ComponentEntity* s_pin_entity    = nullptr;  // the marked-then-freed house entity ptr
int      s_pin_fireFrame = 0;

inline GameUpdateLoop* GetGW() { return GameUpdateLoop::_singleton; }

// Read component[0]'s type-id (best-effort, for identifying what we marked).
uint32_t Comp0TypeId(const ComponentEntity* entity) {
    int cnt = entity->_components._size;
    Object::InternalPtr<Component>* arr = entity->_components._data;
    if (cnt <= 0 || !arr || !arr[0].Get()) return 0;
    Component* comp = arr[0].Get();
    if (!comp) return 0;
    return comp->Type();
}

void FireMarkRemoval() {
    GameUpdateLoop* gw = GetGW();
    if (!gw) { fprintf(stderr, "[MarkRemoval] ERROR: gameWorld null\n"); return; }

    Map* map = gw->m_game;
    EntityList* entityList = gw->m_entityList;
    if (!map || !entityList) {
        fprintf(stderr, "[MarkRemoval] ERROR: map=%p entityList=%p\n", map, entityList);
        return;
    }
    int width = map->size;
    MapCell* tileArray = map->cells.GetData();
    fprintf(stderr, "[MarkRemoval] gw=%p map=%p width=%d tileArray=%p entityList=%p removalList.count(before)=%d\n",
           gw, map, width, tileArray, entityList,
           entityList->removalList._size);

    // Scan a rectangle near map centre for the first markable entity (either cell slot).
    const int lo = 110, hi = 146;
    int scanned = 0, occupied = 0;
    for (int row = lo; row <= hi && !s_mr_fired; ++row) {
        for (int col = lo; col <= hi && !s_mr_fired; ++col) {
            int cell = width * row + col;
            MapCell* cellBase = &tileArray[static_cast<uintptr_t>(cell)];
            ++scanned;
            for (int slot = 8; slot >= 0; slot -= 8) {   // helper_7d60 uses +0x08; also try +0x00
                // +0x08 MapCell::occupant, +0x00 MapCell::blocker
                Object::InternalPtr<ComponentEntity>* slotAddr = slot ? &cellBase->occupant : &cellBase->blocker;
                ComponentEntity* ent = slotAddr->Get();
                if (!ent) continue;
                ++occupied;
                // Mirror helper_7d60: local = slot value; CheckPtr(&local); test +0x40 bit 0x400.
                ComponentEntity* local = ent;
                if (!local) continue;
                uint32_t flags = local->_entityFlags;
                if (flags & 0x400) continue;             // already marked
                uint32_t tid = Comp0TypeId(local);
                int compCnt = local->_components._size;
                fprintf(stderr, "[MarkRemoval] target found: cell=(%d,%d) idx=%d slot=+0x%02x entity=%p "
                       "flags(+0x40)=0x%08x comps(+0x38)=%d comp0.typeId=0x%08x\n",
                       row, col, cell, slot, local, flags, compCnt, tid);

                // --- Step 1: MarkForRemoval (sets flag 0x400, notifies components) ---
                local->MarkForRemoval();
                uint32_t flagsAfter = local->_entityFlags;

                // --- Step 2: push onto EntityList removalList (+0xD0) ---
                IBPArray* rl = &entityList->removalList;
                int cntBefore = rl->_size;
                rl->SetSize(cntBefore + 1, local);   // 0x140003110
                int cntAfter = rl->_size;

                fprintf(stderr, "[MarkRemoval] MARKED: flags 0x%08x -> 0x%08x (bit0x400 %s), "
                       "removalList.count %d -> %d\n",
                       flags, flagsAfter, (flagsAfter & 0x400) ? "SET" : "MISSING",
                       cntBefore, cntAfter);
                fprintf(stderr, "[MarkRemoval] now watching EntityList::Update drain it (removalList.count "
                       "should return to 0 and the tile slot clear within a few frames)...\n");

                s_mr_entity    = local;
                s_mr_tileSlot  = slotAddr;
                s_mr_fired     = true;
                break;
            }
        }
    }
    if (!s_mr_fired) {
        fprintf(stderr, "[MarkRemoval] no markable entity found in scan region (%d..%d)^2 "
               "(scanned=%d occupied=%d) — try a denser (forested) area\n",
               lo, hi, scanned, occupied);
    }
}

// ============================================================
// Pinned-panel-on-dying-entity residual-risk test ("pinpanel")
// ============================================================
// The dialogs-on-dying-entities guard (SA_DialogEntityAlive) rests on a 3-part
// interlock: one-frame removal deferral + EditTool close-on-death + per-panel
// guards. For a PINNED panel the close-on-death is SUPPRESSED — EditTool's
// reposition bails on ToolUI+0x40 (the pin flag) before ever reaching the close
// (edittool.cpp:528 vs :544). So on the frame AFTER the entity is freed, the
// controller's +0x28 datasource weak-ref dangles and the guard reads freed memory.
//
// This scenario reproduces exactly that window: open a founding-house info dialog,
// force the pin (ToolUI+0x40=1), mark+push the house for removal (freed on the next
// drain), then each frame compare the RAW +0x28 read (what the guard does today)
// against a CheckPtr-hardened read (the proposed fix). If, once the entity frees,
// rawDS stays non-null (dangling) while checkedDS goes null, that proves (a) the
// current guard would deref freed memory and (b) a CheckPtr at the top of
// SA_DialogEntityAlive is the correct, sufficient fix. The Tick never derefs rawDS
// past +0x28, so the observation itself stays crash-free.
UI::DialogController* GetControllerByType(UI::DialogInstance* dialog, uint32_t tid) {
    if (!dialog) return nullptr;
    if (tid == 0 || tid == 0xFFFFFFFFu) return nullptr;
    uintptr_t buf[2] = {};
    Object::InternalPtr<UI::DialogController>* ref = ::new (buf) Object::InternalPtr<UI::DialogController>(
        dialog->GetController(tid));
    UI::DialogController* ctrl = ref->Get();
    return ctrl;
}

void FirePinPanel(PlayerToolManager* entityUI) {
    if (!entityUI) { fprintf(stderr, "[PinPanel] ERROR: entityUI null\n"); return; }

    // 1. Activate the default EditTool (mode-tree needs BANISHED_LIVE_TOOLS=1).
    EditTool* tool = EditTool_HarnessAcquire(entityUI);   // rejects a non-EditTool
    if (!tool) { fprintf(stderr, "[PinPanel] EditTool activation FAILED (set BANISHED_LIVE_TOOLS=1)\n"); return; }

    // 2. Open a founding-house info dialog.
    int houses = StartTown_GetFoundingHouseCount();
    ComponentEntity* house = nullptr; int opened = 0;
    for (int i = 0; i < houses && !opened; ++i) {
        house = StartTown_GetFoundingHouseEntity(i);
        if (house) opened = EditTool_HarnessSelectEntity(tool, house);
    }
    if (!opened || !house) { fprintf(stderr, "[PinPanel] no house info dialog opened (houses=%d)\n", houses); return; }
    UI::DialogInstance* dialog = tool->entityRef;   // +0x28, the open dialog

    // 3. Grab the EntityUI (guard target) + ToolUI (pin flag) controllers.
    UI::DialogController* entCtrl  = GetControllerByType(dialog, EntityUI::TypeId());
    UI::DialogController* toolCtrl = GetControllerByType(dialog, ToolUI::TypeId());
    void* ds = entCtrl ? entCtrl->_dataSource : nullptr;
    fprintf(stderr, "[PinPanel] house=%p dialog=%p EntityUIctrl=%p(+0x28 ds=%p) ToolUIctrl=%p\n",
           house, dialog, entCtrl, ds, toolCtrl);
    if (!entCtrl) { fprintf(stderr, "[PinPanel] no EntityUI controller — cannot probe guard\n"); return; }

    // 4. Force the PINNED condition so EditTool's close-on-death can't save us.
    if (toolCtrl) {
        static_cast<ToolUI*>(toolCtrl)->pinned = 1;   // GetController(ToolUI's id)
        fprintf(stderr, "[PinPanel] set ToolUI+0x40=1 (pinned) — close-on-death now suppressed\n");
    } else {
        fprintf(stderr, "[PinPanel] WARN: no ToolUI controller; close-on-death may still fire if EditTool is ticked\n");
    }

    // 5. Mark the house for removal (freed on the next EntityList drain).
    GameUpdateLoop* gw = GetGW();
    EntityList* entityList = gw ? gw->m_entityList : nullptr;
    ComponentEntity* local = house;
    if (!local || !entityList) {
        fprintf(stderr, "[PinPanel] mark preconditions missing (local=%p el=%p)\n",
               local, entityList);
        return;
    }
    local->MarkForRemoval();                                   // 0x1400c2ea0
    IBPArray* rl = &entityList->removalList;
    int cntBefore = rl->_size;
    rl->SetSize(cntBefore + 1, local);   // 0x140003110
    fprintf(stderr, "[PinPanel] MARKED house flags(+0x40)=0x%08x removalList %d->%d — watching the guard "
           "across the free (rawDS vs CheckPtr-hardened)...\n",
           local->_entityFlags,
           cntBefore, rl->_size);

    s_pin_ctrl      = entCtrl;
    s_pin_entity    = local;
    s_pin_fired     = true;
}

// ============================================================
// House-panel-open soak ("housepanel") — open G from NEXT_SESSION_BRIEF.md
// ============================================================
// The ResidenceUI_Slot7+0x295 crash has never been explained, and the autostart
// soak never opens a residence panel — so "the row-bound guard never fired" was
// no evidence. This mode opens a founding-house info dialog (NO pin, NO removal —
// the house entity itself stays alive) and leaves it open, so W_Slot7 repopulates
// the roster rows every frame. Combine with the death harness
// (BANISHED_KILL_CITIZENS=<n> / BANISHED_KILL_FAMILY=<n>) so occupants die and
// cascade OUT of the roster while the panel is live — the suspected crash window
// (mass-death teardown racing the per-frame row refresh). Watch for: the Slot7
// range-guard printf, a crash in standalone_crash.log, or a clean run (= evidence
// the crash needs something else, e.g. births growing a household past the row
// table, which open A still blocks).
// ============================================================================
// WAVE 10C lane ROUTES - a route to ?OnOpened@DialogInstance@UI@@'s SETTINGS half
// ============================================================================
// OFF BY DEFAULT: BANISHED_DLGSETTINGS_ROUTE=<bytes> (1 = the 256-byte default).
// Silent when unset.
//
// THE DEBT. `?OnOpened@DialogInstance@UI@@` is ported and LIVE - lane DLGV moved
// its ATTENDANCE counter 0 -> 2 and 0 -> 3 - but every one of those five entries
// reported `noBuffer`, i.e. `_userData` was null, so the body took its
// second-line early exit and `badMagic=0 records=0`. The MemoryStream, the
// 0xDEADBEEF magic, the Serializer and the whole per-record loop have never run.
//
// WHAT ACTUALLY POPULATES `_userData` - read, not guessed. `_userData` (+0xa8)
// and `_userFlags` (+0xb0) are pure ctor arguments
// (src/engine/ui/dialog_create.cpp:781). Of the sixteen exe-side
// `fn_DialogInstance_Ctor` call sites, FIFTEEN pass `nullptr, 0`. Exactly one
// passes a real buffer - src/game/Dialog/edittool_select.cpp:233:
//
//     ctrlCount = *(uint32_t*)(uiCompIBP + 0x30);
//     ctrlData  = *(void**)  (uiCompIBP + 0x28);
//     fn_DialogInstance_Ctor(dlg, ui, extRes, &nullParent, entityPtr,
//                            ctrlData, (int)ctrlCount);
//
// So `_userData` is the SELECTED ENTITY'S UIComponent settings buffer -
// UIComponent+0x28..+0x37 is a growable byte array (data@0x28, count@0x30,
// cap@0x34) that src/game/Component/uicomponent.cpp:127 already identifies as
// real, serialized state. That is why `housepanel` gave DLGV entries but never a
// buffer: the EditTool route is the right one, and the buffer on the other end
// of it is empty because nothing in our standalone ever grows it.
//
// THE SUBJECT IS BUILT THE WAY THE GUARD READS IT, not faked. This route does
// not stub `_userData` or hand OnOpened a synthetic DialogInstance: it allocates
// a real buffer through the game's own allocator and publishes it into the field
// the select path actually reads (reference_synthetic_subject_beats_a_fake, and
// reference_port_must_reproduce_side_effects - a value that is not published
// into the slot the consumer reads is invisible). Everything downstream is the
// unmodified live path.
//
// THE THREE-SELECT SEQUENCE, and why each step is needed:
//   select(A)  buffer is freshly ZEROED, so magic != 0xDEADBEEF -> OnOpened
//              takes the badMagic exit. That already moves noBuffer -> badMagic
//              and is the first time MemoryStream + the magic read execute.
//   select(B)  closing A runs OnClosed -> SaveSettings, which WRITES the magic
//              and one framed record per controller into A's buffer, dispatching
//              each controller's OnSerializeSettings (+0x50).
//   select(A)  reopens A over a buffer that now has a valid magic and a real
//              record count -> the Serializer, the count read and the per-record
//              loop finally run, and `records` goes non-zero.
//
// Read `records=` and `badMagic=` separately from `entries=`. `entries` moving is
// not coverage: it is exactly the reading that was already green while the half
// this route exists for had never executed once.
static void DlgSettings_InstallBuffer(ComponentEntity* entity, int idx) {
    if (!entity || !System::MemoryAllocator::_singleton) return;   // B3R: read live (was the g_pMemAllocSingleton snapshot)
    const char* e = Opt().dlgSettingsRoute;
    if (!e || !*e || std::strcmp(e, "0") == 0) return;
    int bytes = std::atoi(e);
    if (bytes <= 1) bytes = 256;

    // The same walk src/game/Dialog/edittool_select.cpp:93 does. It lives in an
    // ANONYMOUS namespace there, so it cannot be linked against; the loop is
    // repeated here rather than de-anonymising a file this lane does not own.
    // Entity+0x30 is the component-IBP array, +0x38 the count; a component's
    // type id is its vtable slot +0x10.
    const uint32_t tidUIComp = UIComponent::TypeId();
    UIComponent* uiComp = nullptr;
    if (tidUIComp) {
        const int count = entity->_components._size;
        for (int i = 0; i < count; ++i) {
            Component* comp = entity->_components[static_cast<int64_t>(i)].Get();
            if (!comp) continue;
            const uint32_t tid = comp->Type();
            if (tid == tidUIComp) { uiComp = static_cast<UIComponent*>(comp); break; }
        }
    }
    if (uiComp == nullptr) {
        // A CONTROL on this instrument, not just its verdict. "No UIComponent"
        // and "my type-id lookup is wrong" produce the identical null, and the
        // second would indict a sound conclusion. So dump what the entity DOES
        // carry, by NAME, alongside the id we searched for: if the list is
        // populated with real names and none is UIComponent, the finding is the
        // entity's; if the list is empty or every name is null, the finding is
        // the instrument's.
        const int count = tidUIComp ? entity->_components._size : -1;
        fprintf(stderr, "[DlgSettings:route] house[%d] NO UIComponent. searched tid=0x%08x "
                "components=%d:", idx, tidUIComp, count);
        for (int i = 0; i < count; ++i) {
            Component* comp = entity->_components[static_cast<int64_t>(i)].Get();
            if (!comp) { fprintf(stderr, " <null>"); continue; }
            const uint32_t tid = comp->Type();
            const char* nm = ::Object::GetTypeNameById(tid);
            fprintf(stderr, " %s(0x%08x)", nm ? nm : "?", tid);
        }
        fprintf(stderr, "\n");
        return;
    }
    void* existing = uiComp->data._data;
    if (existing) {
        fprintf(stderr, "[DlgSettings:route] house[%d] ALREADY has a buffer %p count=%u -- left alone\n",
                idx, existing, uiComp->data._size);
        return;
    }
    uint8_t* buf = static_cast<uint8_t*>(System::MemoryAllocator::MemAlloc(static_cast<uint64_t>(bytes), 8));
    if (!buf) { fprintf(stderr, "[DlgSettings:route] house[%d] MemAlloc failed\n", idx); return; }
    std::memset(buf, 0, static_cast<size_t>(bytes));
    uiComp->data._data     = buf;
    uiComp->data._size     = bytes;
    uiComp->data._capacity = bytes;
    fprintf(stderr, "[DlgSettings:route] house[%d] UIComponent=%p settings buffer=%p bytes=%d "
            "(zeroed -- first open must report badMagic)\n", idx, uiComp, buf, bytes);
}

void FireHousePanel(PlayerToolManager* entityUI) {
    if (!entityUI) { fprintf(stderr, "[HousePanel] ERROR: entityUI null\n"); return; }

    EditTool* tool = EditTool_HarnessAcquire(entityUI);   // rejects a non-EditTool
    if (!tool) { fprintf(stderr, "[HousePanel] EditTool activation FAILED (set BANISHED_LIVE_TOOLS=1)\n"); return; }

    // Open the FIRST founding house that yields a dialog. BANISHED_HOUSEPANEL_INDEX
    // picks a specific one instead.
    int houses = StartTown_GetFoundingHouseCount();
    int want = -1;
    if (const char* e = Opt().housePanelIndex) want = std::atoi(e);
    ComponentEntity* house = nullptr; int opened = 0; int pickedIdx = -1;
    for (int i = 0; i < houses && !opened; ++i) {
        if (want >= 0 && i != want) continue;
        house = StartTown_GetFoundingHouseEntity(i);
        if (house) {
            DlgSettings_InstallBuffer(house, i);   // no-op unless the route is armed
            opened = EditTool_HarnessSelectEntity(tool, house);
            pickedIdx = i;
        }
    }
    if (!opened || !house) {
        fprintf(stderr, "[HousePanel] no house info dialog opened (houses=%d want=%d)\n", houses, want);
        return;
    }
    UI::DialogInstance* dialog = tool->entityRef;   // +0x28, the open dialog
    fprintf(stderr, "[HousePanel] OPEN house[%d]=%p dialog=%p — panel stays open; Slot7 now "
           "repopulates rows every frame while the death harness drains the roster\n",
           pickedIdx, house, dialog);

    // Wave 10C lane ROUTES: the close-and-reopen leg. No-op unless
    // BANISHED_DLGSETTINGS_ROUTE is set.
    {
        const char* re = Opt().dlgSettingsRoute;
        if (re && *re && std::strcmp(re, "0") != 0) {
            ComponentEntity* other = nullptr;
            for (int i = 0; i < houses; ++i) {
                if (i == pickedIdx) continue;
                other = StartTown_GetFoundingHouseEntity(i);
                if (other) { DlgSettings_InstallBuffer(other, i); break; }
            }
            if (!other) {
                fprintf(stderr, "[DlgSettings:route] only one founding house (%d) -- cannot close "
                        "A by selecting B, so SaveSettings never runs and records stays 0\n", houses);
                return;
            }
            if (!EditTool_HarnessSelectEntity(tool, other)) {
                fprintf(stderr, "[DlgSettings:route] select(B) FAILED -- A never closed\n");
                return;
            }
            fprintf(stderr, "[DlgSettings:route] select(B) ok -- A closed, SaveSettings should have "
                    "written its buffer\n");
            if (!EditTool_HarnessSelectEntity(tool, house)) {
                fprintf(stderr, "[DlgSettings:route] re-select(A) FAILED\n");
                return;
            }
            fprintf(stderr, "[DlgSettings:route] re-select(A) ok -- OnOpened should now reach the "
                    "record loop (expect records>0)\n");
        }
    }
}

// ============================================================================
// WAVE 10E lane VERIFY — BANISHED_TOOL_HARNESS=uicompcensus
// ============================================================================
// WHAT QUESTION THIS ANSWERS, and why it is a census rather than a guess.
//
// `?OnOpened@DialogInstance@UI@@`'s settings half has never executed: five
// entries, all `noBuffer`. Lane ROUTES established the mechanism and its own
// route (BANISHED_DLGSETTINGS_ROUTE) but could not fire it, because the
// founding houses it had a driver for carry no UIComponent — it dumped all 19
// of their components by name to prove that was the entity's fact and not the
// instrument's.
//
// `_userData` is a DialogInstance CTOR ARGUMENT, and exactly one of the sixteen
// exe-side ctor call sites passes anything but `nullptr, 0`:
// src/game/Dialog/edittool_select.cpp:233, which passes the SELECTED ENTITY'S
// UIComponent buffer (+0x28) and count (+0x30). So the first gate is not
// "controllers" at all — it is "does the entity have a UIComponent". Nothing
// downstream can run for an entity that has none.
//
// Guessing which building type has one costs a run per guess. This walks the
// WHOLE EntityList once and reports, per entity, whether a UIComponent is
// present and what its buffer looks like. It answers the question for every
// type the world contains, in one run, instead of one type per run.
//
// IT CARRIES ITS OWN CONTROL, for the reason lane ROUTES gave: "no UIComponent
// found" and "my type-id lookup is wrong" produce the identical answer, and the
// second would indict a sound conclusion. So the summary prints the type id it
// searched for AND the total number of components it walked. A run that walked
// thousands of components and matched none is the entity's finding; a run whose
// searched id is 0, or that walked no components at all, is the instrument's.
//
// Off by default; prints nothing unless selected.
static void FireUIComponentCensus() {
    if (GameUpdateLoop::_singleton == nullptr) {
        fprintf(stderr, "[UICompCensus] no GameWorld singleton\n");
        return;
    }
    GameUpdateLoop* gw = GameUpdateLoop::_singleton;
    EntityList* entityList = gw->m_entityList;
    Object::InternalPtr<ComponentEntity>* arr = entityList ? entityList->masterArray._data : nullptr;
    int       en  = entityList ? entityList->masterArray._size : 0;

    const uint32_t tidUIComp = UIComponent::TypeId();

    int entities = 0, compsWalked = 0, withUI = 0, withBuffer = 0, shown = 0;
    int maxShow = 40;
    if (const char* m = Opt().uiCompCensusShow) {
        int v = std::atoi(m); if (v >= 0) maxShow = v;
    }

    for (int i = 0; i < en && arr; ++i) {
        ComponentEntity* ent = arr[static_cast<uintptr_t>(i)].Get();
        if (!ent) continue;
        ++entities;

        const int       cN = ent->_components._size;
        Object::InternalPtr<Component>* const cA = ent->_components._data;
        UIComponent* uiComp = nullptr;
        const char* firstName = nullptr;
        for (int c = 0; c < cN && cA; ++c) {
            Component* comp = cA[static_cast<uintptr_t>(c)].Get();
            if (!comp) continue;
            ++compsWalked;
            const uint32_t tid = comp->Type();
            if (!firstName) firstName = ::Object::GetTypeNameById(tid);
            if (tidUIComp && tid == tidUIComp) { uiComp = static_cast<UIComponent*>(comp); break; }
        }
        if (!uiComp) continue;
        ++withUI;

        void* buf   = uiComp->data._data;
        uint32_t n  = uiComp->data._size;
        uint32_t cap= uiComp->data._capacity;
        if (buf && n) ++withBuffer;
        if (shown < maxShow) {
            ++shown;
            fprintf(stderr, "[UICompCensus] entity[%d]=%p first=%s components=%d "
                    "UIComponent=%p buffer=%p count=%u cap=%u\n",
                    i, ent, firstName ? firstName : "?", cN, uiComp, buf, n, cap);
        }
    }

    fprintf(stderr, "[UICompCensus] SUMMARY entities=%d componentsWalked=%d "
            "searchedTypeId=0x%08x withUIComponent=%d withNonEmptyBuffer=%d\n",
            entities, compsWalked, tidUIComp, withUI, withBuffer);
    if (tidUIComp == 0)
        fprintf(stderr, "[UICompCensus]   *** INSTRUMENT FAULT: the UIComponent "
                        "type id resolved to 0, so a zero above is this census's "
                        "finding and not the world's. ***\n");
    else if (compsWalked == 0)
        fprintf(stderr, "[UICompCensus]   *** INSTRUMENT FAULT: no component was "
                        "walked at all (entities=%d). A zero above says nothing "
                        "about the world. ***\n", entities);
    else if (withUI == 0)
        fprintf(stderr, "[UICompCensus]   FINDING: %d components across %d "
                        "entities and NOT ONE UIComponent. On this world no "
                        "DialogInstance can be built with a non-null _userData, "
                        "so OnOpened's settings half is unreachable here.\n",
                compsWalked, entities);
}

// ============================================================================
// WAVE 10E lane VERIFY — BANISHED_TOOL_HARNESS=dlgsettings
// ============================================================================
// The route lane ROUTES built and could not fire, pointed at the entity the
// census above actually found instead of at the founding houses.
//
// The census answered the open question: on a live world of 23175 entities and
// 101865 components, EXACTLY ONE entity carries a UIComponent — and its
// settings buffer is null, which is precisely the case
// DlgSettings_InstallBuffer exists to fill. Founding houses were the wrong
// subject, not the wrong idea.
//
// The three selects and what each is for are lane ROUTES's, unchanged:
//   select(A)  buffer freshly ZEROED -> magic != 0xDEADBEEF -> the badMagic
//              exit. First time the MemoryStream and the magic read execute.
//   select(B)  closing A runs OnClosed -> SaveSettings, which WRITES the magic
//              and one framed record per controller into A's buffer.
//   select(A)  reopens over a valid magic -> the Serializer, the count read and
//              the per-record loop.
//
// B MUST BE AN ENTITY THAT ITSELF OPENS A DIALOG, and that is not a detail:
// EditTool's select returns early when the entity has no UIDescription
// (edittool_select.cpp:209), and an early return never reaches the close of the
// previous panel. A founding house is used, because FireHousePanel already
// proves a founding house opens one.
//
// Read `badMagic=` and `records=` SEPARATELY from `entries=`. `entries` moving
// is not coverage: it was already non-zero while the half this route exists for
// had never executed once.
static void FireDlgSettingsRoute(PlayerToolManager* entityUI) {
    EditTool* tool = EditTool_HarnessAcquire(entityUI);
    if (!tool) {
        fprintf(stderr, "[DlgSettings:route] EditTool activation FAILED "
                        "(set BANISHED_LIVE_TOOLS=1)\n");
        return;
    }
    if (GameUpdateLoop::_singleton == nullptr) return;
    GameUpdateLoop* gw = GameUpdateLoop::_singleton;
    EntityList* entityList = gw->m_entityList;
    Object::InternalPtr<ComponentEntity>* arr = entityList ? entityList->masterArray._data : nullptr;
    int       en  = entityList ? entityList->masterArray._size : 0;
    const uint32_t tidUIComp = UIComponent::TypeId();

    ComponentEntity* A = nullptr; int aIdx = -1;
    for (int i = 0; i < en && arr && !A; ++i) {
        ComponentEntity* ent = arr[static_cast<uintptr_t>(i)].Get();
        if (!ent) continue;
        const int       cN = ent->_components._size;
        Object::InternalPtr<Component>* const cA = ent->_components._data;
        for (int c = 0; c < cN && cA; ++c) {
            Component* comp = cA[static_cast<uintptr_t>(c)].Get();
            if (!comp) continue;
            const uint32_t tid = comp->Type();
            if (tidUIComp && tid == tidUIComp) { A = ent; aIdx = i; break; }
        }
    }
    if (!A) {
        fprintf(stderr, "[DlgSettings:route] no entity with a UIComponent in "
                        "this world (searched tid=0x%08x over %d entities) -- "
                        "the route has no subject here\n", tidUIComp, en);
        return;
    }
    fprintf(stderr, "[DlgSettings:route] subject A = entity[%d]=%p\n", aIdx, A);

    DlgSettings_InstallBuffer(A, aIdx);      // no-op unless the route is armed

    if (!EditTool_HarnessSelectEntity(tool, A)) {
        fprintf(stderr, "[DlgSettings:route] select(A) FAILED -- A has a "
                        "UIComponent but EditTool would not open a panel on it\n");
        return;
    }
    fprintf(stderr, "[DlgSettings:route] select(A) ok -- expect one OnOpened "
                    "with a buffer and badMagic (the buffer is zeroed)\n");

    ComponentEntity* B = nullptr;
    const int houses = StartTown_GetFoundingHouseCount();
    for (int i = 0; i < houses && !B; ++i) {
        ComponentEntity* h = StartTown_GetFoundingHouseEntity(i);
        if (h && h != A) B = h;
    }
    if (!B) {
        fprintf(stderr, "[DlgSettings:route] no founding house to select as B "
                        "-- A never closes, so SaveSettings never runs\n");
        return;
    }
    if (!EditTool_HarnessSelectEntity(tool, B)) {
        fprintf(stderr, "[DlgSettings:route] select(B) FAILED -- A never closed\n");
        return;
    }
    fprintf(stderr, "[DlgSettings:route] select(B) ok -- A closed, SaveSettings "
                    "should have written its buffer\n");

    if (!EditTool_HarnessSelectEntity(tool, A)) {
        fprintf(stderr, "[DlgSettings:route] re-select(A) FAILED\n");
        return;
    }
    // The buffer's first dword IS the magic SaveSettings stored directly, so it
    // is readable here without guessing: printing it separates "SaveSettings
    // never ran" from "OnOpened did not read it".
    {
        const uint32_t tid2 = tidUIComp;
        (void)tid2;
        const int       cN = A->_components._size;
        Object::InternalPtr<Component>* const cA = A->_components._data;
        for (int c = 0; c < cN && cA; ++c) {
            Component* comp = cA[static_cast<uintptr_t>(c)].Get();
            if (!comp) continue;
            const uint32_t tid = comp->Type();
            if (tid != tidUIComp) continue;
            uint8_t* buf = static_cast<UIComponent*>(comp)->data._data;
            fprintf(stderr, "[DlgSettings:route] after close, A's buffer=%p "
                    "head=%08x (0xdeadbeef means SaveSettings wrote it)\n",
                    buf, buf ? *reinterpret_cast<uint32_t*>(buf) : 0u);   // the settings buffer's first dword (the magic), byte storage  // CAST-OK: byte_io

            break;
        }
    }
    fprintf(stderr, "[DlgSettings:route] re-select(A) ok -- read badMagic= and "
                    "records= off the [DlgSettings] ATTENDANCE line, not entries=\n");
}

// ============================================================
// Pasture-panel-open ("pasturepanel") — open the FIRST world pasture's info
// dialog via EditTool select, exactly like clicking it. Exercises the promoted
// PastureUI controller (pastureui.cpp): livestock-type combo populate (Slot4 →
// SelectResourceUI base over the AgriSeed +0x120 list), per-frame slider text
// (Slot7/8), and — combined with BANISHED_PASTUREPANEL_SELECT=<idx> — the
// combo-select event path (Slot9 → Slot11 SetLivestockType → PastureComponent
// Slot14 cmd 2), which retires BANISHED_PASTURE_AUTOTYPE for testing.
// Needs a pasture: either BANISHED_BUILD_DEMO_PASTURE or TOOL_HARNESS=pasture's
// own commit in an earlier run (mutually exclusive with this mode).
// ============================================================
void FirePasturePanel(PlayerToolManager* entityUI) {
    if (!entityUI) { fprintf(stderr, "[PasturePanel] ERROR: entityUI null\n"); return; }

    EditTool* tool = EditTool_HarnessAcquire(entityUI);   // rejects a non-EditTool
    if (!tool) { fprintf(stderr, "[PasturePanel] EditTool activation FAILED\n"); return; }

    if (GameUpdateLoop::_singleton == nullptr) return;
    CitizenBehavior* gs = GameUpdateLoop::_singleton->m_citizenList;
    if (!gs) { fprintf(stderr, "[PasturePanel] no entity-list singleton\n"); return; }
    int n = gs->pastures._size;
    Object::InternalPtr<PastureComponent>* list = gs->pastures._data;
    PastureComponent* pastureComp = (n > 0 && list) ? list->Get() : nullptr;

    // The gs+0x90 registry holds only pastures that have REGISTERED — i.e. finished
    // construction. A pasture the FencedTool just committed is still a building site,
    // so with BANISHED_TOOL_HARNESS=pasturefull that list is empty (measured: n=0) even
    // though the pasture entity exists. Fall back to the EntityList scan FireOrchardPanel
    // uses, which finds the component on the entity regardless of build state — clicking
    // an unfinished pasture opens its panel in retail too.
    if (!pastureComp) {
        GameUpdateLoop* gw = GameUpdateLoop::_singleton;
        EntityList* entityList = gw->m_entityList;
        Object::InternalPtr<ComponentEntity>* arr = entityList ? entityList->masterArray._data : nullptr;
        int       en  = entityList ? entityList->masterArray._size : 0;
        const uint32_t tidPastureComp = PastureComponent::TypeId();
        for (int i = 0; i < en && arr && !pastureComp; ++i) {
            ComponentEntity* ent = arr[static_cast<uintptr_t>(i)].Get();
            if (!ent) continue;
            int       cN = ent->_components._size;
            Object::InternalPtr<Component>* cA = ent->_components._data;
            for (int c = 0; c < cN && cA; ++c) {
                Component* comp = cA[static_cast<uintptr_t>(c)].Get();
                if (!comp) continue;
                if (comp->Type() == tidPastureComp) {
                    pastureComp = static_cast<PastureComponent*>(comp); break;
                }
            }
        }
        fprintf(stderr, "[PasturePanel] registry empty (n=%d); EntityList scan of %d "
               "entities -> pastureComp=%p\n", n, en, pastureComp);
    }
    if (!pastureComp) {
        fprintf(stderr, "[PasturePanel] no pasture in the world — set "
               "BANISHED_BUILD_DEMO_PASTURE or use BANISHED_TOOL_HARNESS=pasturefull\n");
        return;
    }
    ComponentEntity* entRef = pastureComp->_entity.Get();
    if (!entRef) { fprintf(stderr, "[PasturePanel] pasture[0] has no entity backref\n"); return; }

    int opened = EditTool_HarnessSelectEntity(tool, entRef);
    UI::DialogInstance* dialog = tool->entityRef;   // +0x28, the open dialog
    fprintf(stderr, "[PasturePanel] select pasture ent=%p -> opened=%d dialog=%p "
           "(comp=%p type(+0x28)=%p count(+0x38)=%d max(+0x40)=%d)\n",
           entRef, opened, dialog, pastureComp,
           pastureComp->livestockType, pastureComp->animals._size, pastureComp->capacity);

    // BANISHED_PASTUREPANEL_SELECT=<item> — simulate the player picking a
    // livestock type from the combo: exactly the event DialogInstance routes on
    // a combo change (msg 0x14, element id 500) into PastureUI slot 9, which
    // reads the combo's current item and dispatches Slot11 (SetLivestockType).
    // This is the REAL UI path end-to-end; it retires AUTOTYPE for testing.
    const char* selEnv = Opt().pasturePanelSelect;
    if (selEnv && *selEnv && dialog) {
        int wantItem = std::atoi(selEnv);
        uint32_t tidPUI = PastureUI::TypeId();
        UI::DialogController** ctrlArr = dialog->_controllers.GetData();
        int cnt = dialog->_controllers.GetSize();
        PastureUI* pui = nullptr;
        for (int i = 0; ctrlArr && i < cnt; i++) {
            UI::DialogController* c = ctrlArr[i];
            if (!c) continue;
            if (c->Type() == tidPUI) { pui = static_cast<PastureUI*>(c); break; }
        }
        if (!pui) {
            fprintf(stderr, "[PasturePanel] SELECT: no PastureUI controller among %d "
                   "controllers (tid=%u)\n", cnt, tidPUI);
        } else {
            UI::ComboElement* combo = pui->selectResource;   // SelectResourceUI +0x30
            int items = combo ? combo->GetItemCount() : -1;
            fprintf(stderr, "[PasturePanel] SELECT: PastureUI=%p combo=%p items=%d want=%d\n",
                   pui, combo, items, wantItem);
            if (combo && items > 0) {
                if (wantItem >= items) wantItem = items - 1;
                combo->SetCurrentItem(wantItem);
                Object::InternalPtr<UI::Element> comboRef(combo);   // the slot takes the element's InternalPtr (its body never reads it)
                pui->OnElementEvent(
                    &comboRef, 0x14, 500,
                    0, 0, 0, nullptr);
                fprintf(stderr, "[PasturePanel] SELECT fired: item=%d -> pasture type(+0x28)=%p "
                       "max(+0x40)=%d\n", wantItem,
                       pastureComp->livestockType, pastureComp->capacity);
            }
        }
    }
}

// ============================================================
// BANISHED_TOOL_HARNESS=tradepanel — open the TRADING POST info panel.
//
// This is the observability half of Wave A. The TradeUI promotion in
// ui_dialog_stub_factories.cpp wires the real slots but gates them behind
// DepsReady(), and DepsReady() only runs on the FIRST SLOT DISPATCH — which needs
// something to actually construct a TradeUI. Nothing in the autostart path clicks a
// building, so until this exists the promotion is unobservable: "compiled and
// deployed" is not "runs", and a stubbed accessor and a correct one produce the
// same empty table.
//
// Same shape as FirePasturePanel above: activate the EditTool, select the post's
// entity (the real fn_EditTool_SelectEntity path an in-world click takes), then look
// for a TradeUI among the resulting dialog's controllers.
//
// Needs a trading post in the world: BANISHED_BUILD_DEMO_TRADINGPOST=1
// (src/game/Game/starttown.cpp). Prints on every failure branch, so a silent log
// means this never ran rather than that the panel opened.
// ============================================================
// ============================================================
// Orchard-panel-open ("orchardpanel") — open the FIRST world orchard's info dialog
// via EditTool select, exactly like clicking it. Exercises the promoted OrchardUI
// controller (orchardui.cpp): tree-type combo populate (Slot4 -> SelectResourceUI
// base) and the per-frame yield bar (Slot7). With BANISHED_ORCHARDPANEL_SELECT=<idx>
// it also drives the combo-select event path (Slot9 -> Slot11 SetResourceType),
// which is the only way to give an orchard a tree type without the fenced-area tool.
//
// Same shape as FireTradePanel below — the orchard has no world registry list of its
// own that we have mapped, so it scans the EntityList for an OrchardComponent by the
// component's own vtable+0x10 GetTypeId rather than trusting a .rdata type-id slot.
//
// Needs an orchard in the world: BANISHED_BUILD_DEMO_ORCHARD=1
// (src/game/Game/starttown.cpp). Prints on every failure branch, so a silent log
// means this never ran rather than that the panel opened.
// ============================================================
void FireOrchardPanel(PlayerToolManager* entityUI) {
    if (!entityUI) { fprintf(stderr, "[OrchardPanel] ERROR: entityUI null\n"); return; }

    EditTool* tool = EditTool_HarnessAcquire(entityUI);   // rejects a non-EditTool
    if (!tool) { fprintf(stderr, "[OrchardPanel] EditTool activation FAILED\n"); return; }

    GameUpdateLoop* gw = (GameUpdateLoop::_singleton != nullptr) ? GameUpdateLoop::_singleton : nullptr;
    if (!gw) { fprintf(stderr, "[OrchardPanel] gameWorld null\n"); return; }
    EntityList* entityList = gw->m_entityList;
    if (!entityList) { fprintf(stderr, "[OrchardPanel] EntityList (gw+0xe8) null\n"); return; }
    Object::InternalPtr<ComponentEntity>* arr = entityList->masterArray._data;
    int       n   = entityList->masterArray._size;
    const uint32_t tidOrchardComp = OrchardComponent::TypeId();
    if (!arr || n <= 0 || !tidOrchardComp) {
        fprintf(stderr, "[OrchardPanel] cannot scan (arr=%p n=%d tidOrchardComponent=%u)\n",
               arr, n, tidOrchardComp);
        return;
    }

    ComponentEntity* orchEnt = nullptr;
    OrchardComponent* orchComp = nullptr;
    for (int i = 0; i < n && !orchEnt; ++i) {
        ComponentEntity* ent = arr[static_cast<uintptr_t>(i)].Get();
        if (!ent) continue;
        int       cN = ent->_components._size;
        Object::InternalPtr<Component>* cA = ent->_components._data;
        for (int c = 0; c < cN && cA; ++c) {
            Component* comp = cA[static_cast<uintptr_t>(c)].Get();
            if (!comp) continue;
            if (comp->Type() == tidOrchardComp) {
                orchEnt = ent; orchComp = static_cast<OrchardComponent*>(comp); break;
            }
        }
    }
    if (!orchEnt) {
        fprintf(stderr, "[OrchardPanel] no orchard in world (scanned %d entities) — set "
               "BANISHED_BUILD_DEMO_ORCHARD=1\n", n);
        return;
    }

    int opened = EditTool_HarnessSelectEntity(tool, orchEnt);
    UI::DialogInstance* dialog = tool->entityRef;   // +0x28, the open dialog
    fprintf(stderr, "[OrchardPanel] select orchard ent=%p -> opened=%d dialog=%p\n",
           orchEnt, opened, dialog);
    if (!dialog) {
        fprintf(stderr, "[OrchardPanel] no dialog was created — the orchard has no info-panel "
               "UI binding, or SelectEntity refused it\n");
        return;
    }

    // Locate the OrchardUI controller and report the two fields the promotion is
    // supposed to fill: +0x30 combo IBP and +0x38 yield ProgressElement IBP. A null
    // +0x38 with a good +0x30 is the ProgressElement type-id trap; both null means
    // Slot4 never ran at all.
    const uint32_t tidOUI = OrchardUI::TypeId();
    UI::DialogController** ctrlArr = dialog->_controllers.GetData();
    int cnt = dialog->_controllers.GetSize();
    OrchardUI* oui = nullptr;
    for (int i = 0; ctrlArr && i < cnt; i++) {
        UI::DialogController* c = ctrlArr[i];
        if (!c) continue;
        if (c->Type() == tidOUI) { oui = static_cast<OrchardUI*>(c); break; }
    }
    if (!oui) {
        fprintf(stderr, "[OrchardPanel] no OrchardUI controller among %d controllers "
               "(tid=%u)\n", cnt, tidOUI);
        return;
    }
    UI::ComboElement* combo = oui->selectResource;   // SelectResourceUI +0x30
    UI::ProgressElement* yield = oui->yield;
    int items = combo ? combo->GetItemCount() : -1;
    // OrchardComponent+0x60 is the selected tree type (Slot11 writes it, Slot12 reads
    // it). Printing it before AND after the select is what turns "the event fired"
    // into "the type actually landed" — see the diagnostics-report-outcomes rule.
    auto orchType = [&]() -> unsigned long long {
        // treeType (a ComponentDescription*) is logged as an integer (%llx).
        return orchComp ? reinterpret_cast<unsigned long long>(orchComp->treeType) : 0ull;   // +0x60  // CAST-OK: ptr_int
    };
    fprintf(stderr, "[OrchardPanel] OrchardUI=%p combo(+0x30)=%p items=%d yield(+0x38)=%p "
           "comp=%p type(+0x60)=%llx\n",
           oui, combo, items, yield, orchComp, orchType());

    // BANISHED_ORCHARDPANEL_SELECT=<item> — the real combo-change event the
    // DialogInstance routes on a player pick (msg 0x14, element id 500) into
    // OrchardUI slot 9 (SelectResourceUI::Slot9), which dispatches Slot11.
    const char* selEnv = Opt().orchardPanelSelect;
    if (selEnv && *selEnv) {
        int wantItem = std::atoi(selEnv);
        if (combo && items > 0) {
            if (wantItem >= items) wantItem = items - 1;
            combo->SetCurrentItem(wantItem);
            Object::InternalPtr<UI::Element> comboRef(combo);   // the slot takes the element's InternalPtr (its body never reads it)
            oui->OnElementEvent(
                &comboRef, 0x14, 500,
                0, 0, 0, nullptr);
            fprintf(stderr, "[OrchardPanel] SELECT fired: item=%d -> orchard type(+0x60)=%llx "
                   "(%s)\n", wantItem, orchType(),
                   orchType() ? "TYPED" : "STILL UNTYPED - Slot11 did not take");
        } else {
            fprintf(stderr, "[OrchardPanel] SELECT skipped: combo=%p items=%d\n", combo, items);
        }
    }
}

void FireTradePanel(PlayerToolManager* entityUI) {
    if (!entityUI) { fprintf(stderr, "[TradePanel] ERROR: entityUI null\n"); return; }

    EditTool* tool = EditTool_HarnessAcquire(entityUI);   // rejects a non-EditTool
    if (!tool) { fprintf(stderr, "[TradePanel] EditTool activation FAILED\n"); return; }


    // Find the trading post by scanning the entity list for a TradeComponent. The
    // raw .rdata type-id slot 0x1401c4b80 is unregistered in standalone, so match on
    // the component's own vtable+0x10 GetTypeId (same approach as trade_probe.cpp).
    GameUpdateLoop* gw = (GameUpdateLoop::_singleton != nullptr) ? GameUpdateLoop::_singleton : nullptr;
    if (!gw) { fprintf(stderr, "[TradePanel] gameWorld null\n"); return; }
    EntityList* entityList = gw->m_entityList;
    if (!entityList) { fprintf(stderr, "[TradePanel] EntityList (gw+0xe8) null\n"); return; }
    Object::InternalPtr<ComponentEntity>* arr = entityList->masterArray._data;
    int       n   = entityList->masterArray._size;
    const uint32_t tidTradeComp = TradeComponent::TypeId();
    if (!arr || n <= 0 || !tidTradeComp) {
        fprintf(stderr, "[TradePanel] cannot scan (arr=%p n=%d tidTradeComponent=%u)\n",
               arr, n, tidTradeComp);
        return;
    }

    ComponentEntity* postEnt = nullptr;
    for (int i = 0; i < n && !postEnt; ++i) {
        ComponentEntity* ent = arr[static_cast<uintptr_t>(i)].Get();
        if (!ent) continue;
        int       cN = ent->_components._size;
        Object::InternalPtr<Component>* cA = ent->_components._data;
        for (int c = 0; c < cN && cA; ++c) {
            Component* comp = cA[static_cast<uintptr_t>(c)].Get();
            if (!comp) continue;
            if (comp->Type() == tidTradeComp) {
                postEnt = ent; break;
            }
        }
    }
    if (!postEnt) {
        fprintf(stderr, "[TradePanel] no trading post in world (scanned %d entities) — set "
               "BANISHED_BUILD_DEMO_TRADINGPOST=1\n", n);
        return;
    }

    int opened = EditTool_HarnessSelectEntity(tool, postEnt);
    UI::DialogInstance* dialog = tool->entityRef;   // +0x28, the open dialog
    fprintf(stderr, "[TradePanel] select post ent=%p -> opened=%d dialog=%p\n",
           postEnt, opened, dialog);
    if (!dialog) {
        fprintf(stderr, "[TradePanel] no dialog was created — the post has no info-panel UI "
               "binding, or SelectEntity refused it\n");
        return;
    }

    // Enumerate the dialog's controllers and look for TradeUI. Reporting ALL of them
    // distinguishes "TradeUI stayed stubbed" from "no controller was created at all".
    const uint32_t tidTradeUI = TradeUI::TypeId();
    UI::DialogController** ctrlArr = dialog->_controllers.GetData();
    int cnt = dialog->_controllers.GetSize();
    fprintf(stderr, "[TradePanel] dialog has %d controller(s) (TradeUI tid=%u)\n", cnt, tidTradeUI);
    TradeUI* tui = nullptr;
    for (int i = 0; ctrlArr && i < cnt; ++i) {
        UI::DialogController* c = ctrlArr[i];
        if (!c) { fprintf(stderr, "[TradePanel]   ctrl[%d] = <null>\n", i); continue; }
        uint32_t id = c->Type();
        fprintf(stderr, "[TradePanel]   ctrl[%d] = %p tid=%u%s\n", i, c, id,
               id == tidTradeUI ? "   <== TradeUI" : "");
        if (id == tidTradeUI) tui = static_cast<TradeUI*>(c);
    }
    if (!tui) {
        fprintf(stderr, "[TradePanel] NO TradeUI controller — the panel opened but the trade "
               "controller was not instantiated (check Dialog\\Trade.rsc binding)\n");
        return;
    }

    // The setup slot (4) populates the element caches; if the gate let it through,
    // tableSell(+0xC8)/tableBuy(+0xD0) become non-null. That is the single clearest
    // signal of whether DepsReady() passed — and while the 10 leaves are unported it
    // SHOULD read null, with the gate's own "STAYING STUBBED" block just above in
    // stderr. Both null AND no gate message would mean the slot never dispatched.
    void* tableSell = tui->_tableSell;
    void* tableBuy  = tui->_tableBuy;
    fprintf(stderr, "[TradePanel] TradeUI=%p tableSell(+0xC8)=%p tableBuy(+0xD0)=%p -> setup %s\n",
           tui, tableSell, tableBuy,
           (tableSell && tableBuy) ? "RAN (gate passed — panel is LIVE)"
                                   : "did NOT run (gate held it stubbed, as expected "
                                     "while the 10 leaves are unported)");
    fflush(stderr);

    // Dump the price-lookup analysis, if BANISHED_TRADE_PRICE_PROBE armed it. This
    // runs unconditionally at the end of the tradepanel harness rather than on a
    // sample threshold, so that "the panel opened and a520 was never called" prints
    // as an explicit NOT-A-PASS instead of as silence.
}

// Track A: footprint-clear on placement. Runs ClearAreaComponent_FootprintSweep
// (the 0x140117d60 port) on each captured founding-house building, marking any
// footprint resources for removal, then watches EntityList::Update drain them.
void FireClearFootprint() {
    GameUpdateLoop* gw = GetGW();
    if (!gw) { fprintf(stderr, "[ClearFootprint] ERROR: gameWorld null\n"); return; }
    EntityList* el = gw->m_entityList;
    int cntBefore = el ? el->removalList._size : -1;

    int houses = StartTown_GetFoundingHouseCount();
    fprintf(stderr, "[ClearFootprint] founding houses=%d removalList.count(before)=%d\n", houses, cntBefore);

    int totalMarked = 0;
    for (int i = 0; i < houses; ++i) {
        ComponentEntity* house = StartTown_GetFoundingHouseEntity(i);
        if (!house) continue;
        uint32_t caFlags = 0;
        ClearAreaComponent* caComp = ClearAreaComponent_HarnessFind(house, &caFlags);
        ComponentEntity* caOwner = caComp ? ClearAreaComponent_HarnessOwner(caComp) : nullptr;
        // Drive the REAL component's sweep (reads its own +0x10 owner) — the exact op a
        // de-inerted Update tick would perform.
        int marked = caComp ? ClearAreaComponent_HarnessSweepComp(caComp) : 0;
        fprintf(stderr, "[ClearFootprint] house[%d] entity=%p ClearAreaComp=%p owner(+0x10)=%p%s "
               "flags=0x%08x(tickbit=%d) realSweep marked=%d\n",
               i, house, caComp, caOwner, (caOwner == house) ? "(==entity OK)" : "(MISMATCH)",
               caFlags, (caFlags >> 2) & 1, marked);
        totalMarked += marked;
    }

    int cntAfter = el ? el->removalList._size : -1;
    fprintf(stderr, "[ClearFootprint] total marked=%d  removalList.count %d -> %d%s\n",
           totalMarked, cntBefore, cntAfter,
           totalMarked == 0 ? "  (no resources under founding houses — cleared ground; "
                              "port ran crash-free reading each footprint)" : "");

    // Reuse the drain observer (tileSlot=0 → count-only) to watch EntityList::Update
    // destroy the marked entities over the next frames.
    if (totalMarked > 0) { s_mr_fired = true; s_mr_entity = 0; s_mr_tileSlot = 0; }
}

// Track A demonstration: prove the footprint sweep marks a real resource. SA placement
// validity rejects resource cells (unlike retail, where the terrainMask permits placing
// over clearable resources), so we can't anchor a house on a tree here. Instead we point
// a founding house's BuildComponent footprint at a resource cell, sweep, then RESTORE the
// footprint — a safe, deterministic proof that a building whose footprint contains a
// resource marks it for removal via the ported helper_7d60 path.
void FireClearPlace() {
    GameUpdateLoop* gw = GetGW();
    if (!gw) { fprintf(stderr, "[ClearPlace] ERROR: gameWorld null\n"); return; }
    Map* map = gw->m_game;
    if (!map) { fprintf(stderr, "[ClearPlace] ERROR: map null\n"); return; }
    int width = map->size;
    MapCell* tileArray = map->cells.GetData();

    // 1. Find a resource cell (non-null tile occupant not yet flagged) near centre.
    int rcRow = -1, rcCol = -1; ComponentEntity* resEnt = nullptr;
    for (int row = 100; row <= 156 && rcRow < 0; ++row)
        for (int col = 100; col <= 156 && rcRow < 0; ++col) {
            int cell = width * row + col;
            ComponentEntity* occ = tileArray[(uintptr_t)cell].occupant.Get();
            if (occ && !(occ->_entityFlags & 0x400)) {
                rcRow = row; rcCol = col; resEnt = occ;
            }
        }
    if (rcRow < 0) { fprintf(stderr, "[ClearPlace] no resource cell found near centre\n"); return; }
    fprintf(stderr, "[ClearPlace] resource at cell=(%d,%d) entity=%p\n", rcRow, rcCol, resEnt);

    // 2. Get a founding house and its footprint component (the rect at +0x28). The
    //    finder's name says Build; its type id is MapComponent's (Entity/exe_bodies.h).
    ComponentEntity* house = StartTown_GetFoundingHouseCount() > 0 ? StartTown_GetFoundingHouseEntity(0) : nullptr;
    if (!house) { fprintf(stderr, "[ClearPlace] no founding house to borrow\n"); return; }
    MapComponent* bc = house->FindComponent<MapComponent>();
    if (!bc) { fprintf(stderr, "[ClearPlace] house has no BuildComponent\n"); return; }

    // 3. Save the real footprint, then override it to a 1x1 rect over the resource cell.
    int16_t sCol = bc->tileX0;
    int16_t sRow = bc->tileY0;
    int16_t sW   = bc->tileX1;
    int16_t sH   = bc->tileY1;
    bc->tileX0 = (int16_t)rcCol;
    bc->tileY0 = (int16_t)rcRow;
    bc->tileX1 = 1;
    bc->tileY1 = 1;

    EntityList* el = gw->m_entityList;
    int cntBefore = el ? el->removalList._size : -1;

    // 4. Sweep the house (with the overridden footprint) — marks the resource.
    fprintf(stderr, "[ClearPlace] sweeping house[0]=%p with footprint overridden to (%d,%d) 1x1...\n",
           house, rcCol, rcRow);
    int marked = ClearAreaComponent_HarnessSweep(house);

    // 5. Restore the real footprint immediately (house uncorrupted).
    bc->tileX0 = sCol;
    bc->tileY0 = sRow;
    bc->tileX1 = sW;
    bc->tileY1 = sH;

    int cntAfter = el ? el->removalList._size : -1;
    uint32_t resFlags = resEnt->_entityFlags;
    fprintf(stderr, "[ClearPlace] marked=%d  resource flags(+0x40)=0x%08x (bit0x400 %s)  "
           "removalList.count %d -> %d\n",
           marked, resFlags, (resFlags & 0x400) ? "SET" : "MISSING", cntBefore, cntAfter);
    if (marked > 0) { s_mr_fired = true; s_mr_entity = 0; s_mr_tileSlot = 0; }
}

// Track A step-2 demonstration: with the LIVE ClearAreaComponent enabled
// (BANISHED_CLEAR_FOOTPRINT=1), point a founding house's footprint at a resource cell so
// the GAME'S OWN per-frame component tick (not the harness) auto-clears it. Proves the
// full automatic chain: EntityList ticks component -> FootprintSweep -> mark -> drain.
void FireClearLive() {
    GameUpdateLoop* gw = GetGW();
    if (!gw) { fprintf(stderr, "[ClearLive] ERROR: gameWorld null\n"); return; }
    Map* map = gw->m_game;
    if (!map) { fprintf(stderr, "[ClearLive] ERROR: map null\n"); return; }
    int width = map->size;
    MapCell* tileArray = map->cells.GetData();

    int rcRow = -1, rcCol = -1; ComponentEntity* resEnt = nullptr;
    for (int row = 100; row <= 156 && rcRow < 0; ++row)
        for (int col = 100; col <= 156 && rcRow < 0; ++col) {
            int cell = width * row + col;
            ComponentEntity* occ = tileArray[(uintptr_t)cell].occupant.Get();
            if (occ && !(occ->_entityFlags & 0x400)) {
                rcRow = row; rcCol = col; resEnt = occ;
            }
        }
    if (rcRow < 0) { fprintf(stderr, "[ClearLive] no resource cell found\n"); return; }

    ComponentEntity* house = StartTown_GetFoundingHouseCount() > 0 ? StartTown_GetFoundingHouseEntity(0) : nullptr;
    uint32_t caFlags = 0;
    ClearAreaComponent* caComp = house ? ClearAreaComponent_HarnessFind(house, &caFlags) : nullptr;
    if (!house || !caComp) { fprintf(stderr, "[ClearLive] no house/ClearAreaComponent\n"); return; }
    fprintf(stderr, "[ClearLive] house[0]=%p ClearAreaComp=%p flags=0x%08x(tickbit=%d) — %s\n",
           house, caComp, caFlags, (caFlags >> 3) & 1,
           ((caFlags >> 3) & 1) ? "LIVE (will be ticked)"
                                : "NOT live — set BANISHED_CLEAR_FOOTPRINT=1");

    MapComponent* bc = house->FindComponent<MapComponent>();   // the footprint rect (the old "Build" finder's cell is MapComponent's)
    if (!bc) { fprintf(stderr, "[ClearLive] no BuildComponent\n"); return; }
    // Override the footprint to a 1x1 rect over the resource (persistent — the live tick
    // needs it in place when it fires). Minor house corruption acceptable for the probe.
    bc->tileX0 = (int16_t)rcCol;
    bc->tileY0 = (int16_t)rcRow;
    bc->tileX1 = 1;
    bc->tileY1 = 1;
    fprintf(stderr, "[ClearLive] overrode house[0] footprint to resource cell (%d,%d) entity=%p — "
           "waiting for the LIVE component tick to auto-clear it (watch [ClearArea LIVE])...\n",
           rcCol, rcRow, resEnt);

    s_mr_fired = true; s_mr_entity = 0; s_mr_tileSlot = 0;
}

// Track A refinement #2: prove SA placement OVER a resource cell. Builds a WoodHouse
// placement tool (as FirePlacementTool), finds a resource cell near centre, and drives
// the real CREATE path (PlacementTool_HarnessPlaceAtCell -> helper_d650 -> d580 -> f5d0)
// AT that resource cell. With BANISHED_CLEAR_PLACE_OVER=1 the footprint validity accepts
// the clearable-resource cell and the building materialises; without it, placement is
// rejected (placed=0), matching retail's clearable-terrain rule. (Tool/template
// accessors declared at the top of this file.)
void FireClearPlaceOver() {
    GameUpdateLoop* gw = GetGW();
    if (!gw) { fprintf(stderr, "[ClearPlaceOver] ERROR: gameWorld null\n"); return; }
    Map* map = gw->m_game;
    if (!map) { fprintf(stderr, "[ClearPlaceOver] ERROR: map null\n"); return; }
    int width = map->size;
    MapCell* tileArray = map->cells.GetData();

    // Find a resource cell near centre.
    int rcRow = -1, rcCol = -1; ComponentEntity* resEnt = nullptr;
    for (int row = 110; row <= 150 && rcRow < 0; ++row)
        for (int col = 110; col <= 150 && rcRow < 0; ++col) {
            int cell = width * row + col;
            ComponentEntity* occ = tileArray[(uintptr_t)cell].occupant.Get();
            if (occ && !(occ->_entityFlags & 0x400)) {
                rcRow = row; rcCol = col; resEnt = occ;
            }
        }
    if (rcRow < 0) { fprintf(stderr, "[ClearPlaceOver] no resource cell found near centre\n"); return; }

    ComponentDescription* tmpl = StartTown_ResolveTemplateByPath("Template\\WoodHouse.rsc");
    if (!tmpl) { fprintf(stderr, "[ClearPlaceOver] ERROR: WoodHouse template null\n"); return; }
    PlacementTool* tool = PlacementTool_Factory_SA();
    if (!tool) { fprintf(stderr, "[ClearPlaceOver] ERROR: tool factory null\n"); return; }
    tool->descriptionPtr = tmpl;
    if (!PlacementTool_HarnessStartupProbe(tool)) {
        fprintf(stderr, "[ClearPlaceOver] Startup probe failed\n"); return;
    }

    int centerCell = width * rcRow + rcCol;
    fprintf(stderr, "[ClearPlaceOver] placing WoodHouse AT resource cell (%d,%d) ent=%p (BANISHED_CLEAR_PLACE_OVER "
           "must be set to succeed)...\n", rcCol, rcRow, resEnt);
    int placed = PlacementTool_HarnessPlaceAtCell(tool, centerCell);
    fprintf(stderr, "[ClearPlaceOver] CREATE over resource -> %d %s\n", placed,
           placed ? "(building MATERIALISED over the resource — refinement #2 works)"
                  : "(rejected — set BANISHED_CLEAR_PLACE_OVER=1 to allow clearable-resource cells)");
}

// Track A refinement #1: prove the HARVEST dispatch (ClearArea_State0Enter, port of
// helper_7960). Points a founding house's footprint at a resource cell, then drives
// the state-0 ENTER handler directly (the not-done path never derefs the sm, so a
// null sm is safe). Verifies: (a) it runs crash-free over a real resource footprint,
// (b) if the owner has a WorkComponent, a ClearArea work-action is posted to the
// citizen work-scheduler (gw+0x118, primary count at sched[6]). Restores the footprint.
extern "C" char ClearArea_State0Enter(ClearAreaComponent* data, StateMachine* sm);
void FireClearHarvest() {
    GameUpdateLoop* gw = GetGW();
    if (!gw) { fprintf(stderr, "[ClearHarvest] ERROR: gameWorld null\n"); return; }
    Map* map = gw->m_game;
    if (!map) { fprintf(stderr, "[ClearHarvest] ERROR: map null\n"); return; }
    int width = map->size;
    MapCell* tileArray = map->cells.GetData();

    int rcRow = -1, rcCol = -1; ComponentEntity* resEnt = nullptr;
    for (int row = 100; row <= 156 && rcRow < 0; ++row)
        for (int col = 100; col <= 156 && rcRow < 0; ++col) {
            int cell = width * row + col;
            ComponentEntity* occ = tileArray[(uintptr_t)cell].occupant.Get();
            if (occ && !(occ->_entityFlags & 0x400)) {
                rcRow = row; rcCol = col; resEnt = occ;
            }
        }
    if (rcRow < 0) { fprintf(stderr, "[ClearHarvest] no resource cell found near centre\n"); return; }

    ComponentEntity* house = StartTown_GetFoundingHouseCount() > 0 ? StartTown_GetFoundingHouseEntity(0) : nullptr;
    if (!house) { fprintf(stderr, "[ClearHarvest] no founding house\n"); return; }
    uint32_t caFlags = 0;
    ClearAreaComponent* caComp = ClearAreaComponent_HarnessFind(house, &caFlags);
    if (!caComp) { fprintf(stderr, "[ClearHarvest] house has no ClearAreaComponent\n"); return; }

    MapComponent* bc = house->FindComponent<MapComponent>();   // the footprint rect (the old "Build" finder's cell is MapComponent's)
    if (!bc) { fprintf(stderr, "[ClearHarvest] house has no BuildComponent\n"); return; }
    int16_t sCol = bc->tileX0;
    int16_t sRow = bc->tileY0;
    int16_t sW   = bc->tileX1;
    int16_t sH   = bc->tileY1;
    bc->tileX0 = (int16_t)rcCol;
    bc->tileY0 = (int16_t)rcRow;
    bc->tileX1 = 1;
    bc->tileY1 = 1;

    // Work scheduler: gw+0x118; primary AVL count at +0x18 (sched[6]).
    WorkScheduler* sched = gw->m_tutorial;
    int schedBefore = sched ? sched->field_0x10.count : -1;

    fprintf(stderr, "[ClearHarvest] house[0]=%p ClearAreaComp=%p resource=(%d,%d) ent=%p — driving "
           "State0Enter (harvest dispatch)...\n", house, caComp, rcCol, rcRow, resEnt);
    ClearArea_State0Enter(caComp, nullptr);   // not-done path: sm unused

    int schedAfter = sched ? sched->field_0x10.count : -1;

    // Restore footprint.
    bc->tileX0 = sCol;
    bc->tileY0 = sRow;
    bc->tileX1 = sW;
    bc->tileY1 = sH;

    fprintf(stderr, "[ClearHarvest] ran crash-free. scheduler primary count %d -> %d %s\n",
           schedBefore, schedAfter,
           (schedAfter > schedBefore) ? "(ClearArea work POSTED — harvest job queued for a citizen)"
                                      : "(no post — owner likely has no WorkComponent; dispatch ran)");
}

// ============================================================
// Building upgrade ("upgrade") — automated wood-house -> stone-house repro
// ============================================================
// The upgrade path's spawn branch (DestroyComponent state-5, 0x14011c120) only runs
// when state-5 enter (0x14011cc70) takes its REFUND+BUILD branch, and that branch is
// gated on the building's BuildComponent (0x14000d6c0, type 0x1401c7008) having
// delivered(+0x48) != 0 — i.e. the building must have been REALLY CONSTRUCTED. Every
// start-town / tool-placed building has +0x48 == 0, so a manual upgrade click on one
// always fell to REMOVE(no-spawn) and the building simply vanished.
//
// This scenario builds the required subject automatically: with
// BANISHED_BUILD_DEMO_HOUSE=1 the start town seeds one extra house as a bare
// foundation (g_pConstructionDemoEntity) and the organic pipeline constructs it for
// real (gather -> haul -> build labor -> complete). We poll that site until
// delivered >= required, then fire the upgrade exactly the way a click does: open the
// building's info dialog through the EditTool, take its UpgradeUI controller, and
// call vtable slot 9 with (msg 0x12, id 33000, src = the controller's own button).
//
// Env: BANISHED_TOOL_HARNESS=upgrade
//      BANISHED_UPGRADE_WAIT_FRAMES  poll budget before giving up (default 36000)
//      BANISHED_UPGRADE_CLICK_DELAY  frames between opening the dialog and clicking
//                                    (default 15 — UpgradeUI binds its button in the
//                                    slot-8 self-heal a frame or more after the open)
// The whole rest of the sequence is observed through the DestroySM traces
// (BANISHED_DESTROY_TRACE=1) — this only supplies the click.

// The demo foundation's owner entity (starttown.cpp; set alongside g_pConstructionDemoSite).
extern "C" ComponentEntity* g_pConstructionDemoEntity;

enum UpgradeStage { UP_IDLE = 0, UP_WAIT_BUILT, UP_DIALOG_OPEN, UP_DONE };
UpgradeStage s_up_stage      = UP_IDLE;
PlayerToolManager* s_up_entityUI = nullptr;
ComponentEntity* s_up_entity   = nullptr;   // the building being upgraded
EditTool*    s_up_tool       = nullptr;   // active EditTool (dialog owner)
int          s_up_armFrame   = 0;
int          s_up_openFrame  = 0;

int UpgradeWaitFrames() {
    const char* e = Opt().upgradeWaitFrames;
    int n = (e && e[0]) ? std::atoi(e) : 36000;
    return n < 60 ? 36000 : n;
}
// BANISHED_UPGRADE_FORCE_FRAME — see the note at the UP_WAIT_BUILT force branch.
// 0/unset = normal behaviour (wait for the build to finish).
int ForceUpgradeFrame() {
    const char* e = Opt().upgradeForceFrame;
    int n = (e && e[0]) ? std::atoi(e) : 0;
    return n > 0 ? n : 0;
}
int UpgradeClickDelay() {
    const char* e = Opt().upgradeClickDelay;
    int n = (e && e[0]) ? std::atoi(e) : 15;
    return n < 1 ? 15 : n;
}

// BuildComponent (0x14000d6c0 / type 0x1401c7008) on `entity`, CheckPtr'd.
BuildComponent* UpgradeFindBuildComp(ComponentEntity* entity) {
    if (!entity) return nullptr;
    return entity->FindComponent<BuildComponent>();   // 0x14000d6c0
}
// Retail's own two reads: +0x48 = delivered build effort, required = desc(+0x18)+0x30
// * multiplier(+0x4c) — identical to destroycomponent_states.cpp Yard_Delivered/Required.
int UpgradeDelivered(BuildComponent* bc) {
    return bc ? bc->laborProgress : 0;
}
int UpgradeRequired(BuildComponent* bc) {
    if (!bc) return 0;
    Description* desc = bc->_description;
    if (!desc) return 0;
    return static_cast<BuildDescription*>(desc)->_i30 *   // as Yard_Required (destroycomponent_states.cpp)
           bc->costMultiplier;
}

// Pick the upgrade subject: the construction-demo house if one was seeded (the only
// building that ever reaches delivered != 0), else founding house 0 so the run still
// exercises the click + REMOVE branch instead of doing nothing.
ComponentEntity* UpgradePickSubject(bool* outIsDemo) {
    if (g_pConstructionDemoEntity) { *outIsDemo = true;  return g_pConstructionDemoEntity; }
    *outIsDemo = false;
    return StartTown_GetFoundingHouseCount() > 0 ? StartTown_GetFoundingHouseEntity(0)
                                                 : nullptr;
}

void FireUpgradeArm(PlayerToolManager* entityUI, int frameNum) {
    bool isDemo = false;
    ComponentEntity* subject = UpgradePickSubject(&isDemo);

    s_up_entityUI = entityUI;
    s_up_entity   = subject;
    s_up_armFrame = frameNum;
    s_up_stage    = UP_WAIT_BUILT;

    BuildComponent* bc = UpgradeFindBuildComp(subject);
    fprintf(stderr, "[Upgrade] armed at frame %d: subject=%p (%s) BuildComponent=%p "
           "delivered=%d required=%d\n", frameNum, subject,
           isDemo ? "construction-demo house — will be built organically"
                  : "founding house — delivered stays 0, expect REMOVE(no-spawn)",
           bc, UpgradeDelivered(bc), UpgradeRequired(bc));
    if (!subject) {
        fprintf(stderr, "[Upgrade] ERROR: no subject building. Set BANISHED_BUILD_DEMO_HOUSE=1 "
               "so the start town seeds a demo foundation.\n");
        s_up_stage = UP_DONE;
        return;
    }
    if (!isDemo)
        fprintf(stderr, "[Upgrade] NOTE: no construction-demo house (BANISHED_BUILD_DEMO_HOUSE unset?) "
               "— the REFUND+BUILD branch cannot be reached with this subject.\n");
}

// Open the subject's info dialog through the live EditTool (same path a click takes).
bool UpgradeOpenDialog() {
    if (!s_up_entityUI || !s_up_entity) return false;
    // The current tool, taken as the EditTool (SelectEntity refuses any other tool).
    EditTool* tool = static_cast<EditTool*>(s_up_entityUI->currentTool.Get());
    if (!tool) {
        fprintf(stderr, "[Upgrade] installing default EditTool via SetEditMode(pc,0)...\n");
        s_up_entityUI->SelectTool(nullptr);
        tool = static_cast<EditTool*>(s_up_entityUI->currentTool.Get());
    }
    if (!tool) {
        fprintf(stderr, "[Upgrade] EditTool activation FAILED (needs BANISHED_LIVE_TOOLS=1)\n");
        return false;
    }
    s_up_tool = tool;
    int opened = EditTool_HarnessSelectEntity(tool, s_up_entity);
    UI::DialogInstance* dialog = tool->entityRef;   // +0x28, the open dialog
    fprintf(stderr, "[Upgrade] select entity=%p -> opened=%d dialog=%p\n",
           s_up_entity, opened, dialog);
    return opened != 0 && dialog != nullptr;
}

// Fire the upgrade button: UpgradeUI vtable slot 9 (self, &srcElement, msg, id, x, y)
// with msg 0x12 / id 33000 / src == the controller's bound button (+0x30), which is
// exactly the event UpgradeUI_OnRemove accepts. It then writes the target template
// (UpgradeUIConfig+0x20) into DestroyComponent+0x60 and pumps the BuildState SM.
void FireUpgradeClick() {
    UI::DialogInstance* dialog = s_up_tool
        ? s_up_tool->entityRef : nullptr;   // +0x28, the open dialog
    UpgradeUI* ctrl = dialog ? static_cast<UpgradeUI*>(
        GetControllerByType(dialog, UpgradeUI::TypeId())) : nullptr;   // GetController(UpgradeUI's id)
    fprintf(stderr, "[Upgrade] dialog=%p UpgradeUI controller=%p\n", dialog, static_cast<void*>(ctrl));
    if (!ctrl) {
        fprintf(stderr, "[Upgrade] FAILED: no UpgradeUI controller in the panel — this building's "
               "UI has no upgrade page (check the template's UIDescription).\n");
        return;
    }

    // Slot 4 (OnCreated) binds the button into +0x30 and sets its element id. The
    // slot-8 self-heal normally does this; drive it directly if it hasn't run.
    if (ctrl->button == nullptr)
        ctrl->OnCreated();

    UI::ButtonElement* button = ctrl->button;
    void* ds     = ctrl->_dataSource;
    fprintf(stderr, "[Upgrade] controller config(+0x10)=%p datasource(+0x28)=%p button(+0x30)=%p\n",
           ctrl->_config.Get(), ds, button);
    if (!button) {
        fprintf(stderr, "[Upgrade] FAILED: UpgradeUI never bound its button (+0x30 null) — Serialize "
               "was blocked (run with BANISHED_DESTROY_TRACE=1 for the reason).\n");
        return;
    }
    Object::InternalPtr<UI::Element> src(button);   // p2 points at the source-element pointer
    fprintf(stderr, "[Upgrade] >>> firing UpgradeUI slot 9 (msg=0x12 id=33000) <<<\n");
    ctrl->OnElementEvent(&src, 0x12, 33000, 0, 0, 0, nullptr);
    fprintf(stderr, "[Upgrade] click delivered — follow [DestroySM] for state 3 -> 4 -> 5 "
           "and the UPGRADE SPAWN.\n");
}

// Per-frame driver for the "upgrade" scenario.
void UpgradeTick(int frameNum) {
    switch (s_up_stage) {
    case UP_WAIT_BUILT: {
        BuildComponent* bc = UpgradeFindBuildComp(s_up_entity);
        int delivered = UpgradeDelivered(bc);
        int required  = UpgradeRequired(bc);
        int waited    = frameNum - s_up_armFrame;
        if (waited % 120 == 0)
            fprintf(stderr, "[Upgrade] waiting for construction: delivered=%d required=%d "
                   "(%d frames)\n", delivered, required, waited);

        // BANISHED_UPGRADE_FORCE_FRAME=<n>: fire the click n frames after arming WITHOUT
        // waiting for the build to finish. The replacement-spawn branch needs
        // delivered != 0 and so will not run, but DestroySM state-4 enter still fires
        // WorkPlaceComponent_EnterBuilderProfession (0x14012f520) — and mid-construction
        // is the only window where the site's WorkComponent still holds a scheduler work
        // id (+0x28 >= 0), which is the sole trigger for the profession re-key
        // (FUN_1400f8400). Once construction completes the id is cleared, so the normal
        // wait-for-built path can never exercise that branch.
        if (int force = ForceUpgradeFrame(); force > 0 && waited >= force) {
            fprintf(stderr, "[Upgrade] FORCED at %d frames (delivered=%d required=%d) — exercising "
                   "the mid-construction demolish path; no replacement spawn expected\n",
                   waited, delivered, required);
            if (UpgradeOpenDialog()) {
                s_up_openFrame = frameNum;
                s_up_stage     = UP_DIALOG_OPEN;
            } else {
                s_up_stage = UP_DONE;
            }
            return;
        }
        // Ready when the build is finished (delivered >= required, required > 0). That
        // satisfies BOTH gates at once: state-5 enter's delivered != 0 and the state-5
        // tick's delivered >= required spawn condition.
        if (required > 0 && delivered >= required) {
            fprintf(stderr, "[Upgrade] construction COMPLETE (delivered=%d/%d) — opening the "
                   "info dialog\n", delivered, required);
            if (UpgradeOpenDialog()) {
                s_up_openFrame = frameNum;
                s_up_stage     = UP_DIALOG_OPEN;
            } else {
                s_up_stage = UP_DONE;
            }
            return;
        }
        if (waited > UpgradeWaitFrames()) {
            fprintf(stderr, "[Upgrade] TIMEOUT after %d frames: delivered=%d required=%d — the "
                   "demo house never finished building; nothing to upgrade.\n",
                   waited, delivered, required);
            s_up_stage = UP_DONE;
        }
        break;
    }
    case UP_DIALOG_OPEN:
        if (frameNum - s_up_openFrame >= UpgradeClickDelay()) {
            s_up_stage = UP_DONE;
            FireUpgradeClick();
        }
        break;
    default:
        break;
    }
}

// ============================================================
// Bridge ("bridge") — place a bridge, then watch builders finish it
// ============================================================
// Activates the BridgeTool through the real SetEditMode path (same as the road
// scenario — cursor-free, so the drag's start/end cells are set directly), finds a
// bank→water→bank span the tool's own CalcCost accepts, commits it with CalcPath, and
// then polls the placed segments every BANISHED_BRIDGE_POLL frames until each one's
// BuildComponent reports delivered >= required.
//
// Env: BANISHED_TOOL_HARNESS=bridge
//      BANISHED_BRIDGE_TEMPLATE     default "Template\\WoodBridge.rsc"
//      BANISHED_BRIDGE_POLL         poll period in frames (default 600)
//      BANISHED_BRIDGE_WAIT_FRAMES  give-up budget (default 60000)
// Builders are seated by src/diag/panel_staffing_driver.cpp on unattended runs (the
// old BANISHED_BUILDER_AUTOPOOL bypass was retired 2026-08-15); this run is much
// faster under BANISHED_FORCE_SPEED=40.
// ============================================================================
// row 1261 -- BANISHED_TOOL_HARNESS=pick, the placed building and bridge
// ============================================================================
// D.4 LUT dropped the explicit PickingComponent::RefreshMesh after RegisterEntity in
// PlacementTool::CreateAt (tool_base_tile.cpp) and BridgeTool::CalcPath
// (bridgetool_calcpath.cpp): RegisterEntity's OnAdded pass (0x1400c48a0 -> slot 6
// 0x140128b90) is the registration, as in retail. The pick route places a building and
// a bridge through those two bodies and watches each new entity's PickingComponent:
//   at placement (same frame): the scene gained exactly one live entry per component
//     that registered one. A construction site's model is at meshIndex -1 then, so
//     OnAdded's RefreshMesh registers nothing and the gain is 0.
//   every kPickPollFrames after: once BuildComponent's RefreshMesh (0x140108b26) has
//     given every component a mesh, each pickingId names a live entry carrying its
//     userData, no two components share an id, the live entries carrying an owner
//     number the components pointing at it (one mesh each, no stale one), and for
//     every owner a ray dropped onto one of its models resolves to it the way
//     EditTool::OnKey (0x1400fd030) picks. (Per owner, not per segment: a bridge
//     segment's foundation-frame mesh need not cover its model's origin.)
// Lines "[ToolHarness][pickplace] ..." / "[ToolHarness][pickbridge] ...": "AT PLACE ok",
// then "PICK PASS"; "PICK FAIL" on a bad entry or at BANISHED_BRIDGE_WAIT_FRAMES.
struct PickSnapshot {
    int entities = 0;   // EntityList master array size
    int live     = 0;   // live Picking::Scene entries (_nextFree == -2)
};

struct PickWatch {
    const char*      route = nullptr;
    ComponentEntity* entities[40] = {};
    int              count = 0;
    int              armFrame = 0;
    int              lastPoll = 0;
    bool             done = false;
};

const int kPickPollFrames = 60;
PickWatch s_pickWatch[2];   // [0] pickplace, [1] pickbridge

int PickLiveEntries(const Picking::Scene* scene, const void* userData) {
    int live = 0;
    for (int i = 0; i < scene->_entries._size; ++i) {
        const Picking::Scene::Entry& entry = scene->_entries._data[i];
        if (entry._nextFree != -2) continue;
        if (userData && entry._userData != userData) continue;
        ++live;
    }
    return live;
}

PickSnapshot PickTakeSnapshot() {
    GameUpdateLoop* gw = GetGW();
    PickSnapshot snap;
    snap.entities = gw->m_entityList->masterArray._size;
    snap.live = PickLiveEntries(gw->m_pickingScene, nullptr);
    return snap;
}

// One pass over a watch's entities. Returns the number of BAD components and fills the
// counts the caller reports (outOwners / outPicked: distinct owners, and those a ray
// resolved); `verbose` logs every component, otherwise only a BAD one.
int PickCheckEntities(const PickWatch& watch, bool verbose, int* outWithPicking,
                      int* outRegistered, int* outOwners, int* outPicked) {
    Picking::Scene* scene = GetGW()->m_pickingScene;
    int withPicking = 0;
    int registered = 0;
    void* owners[40] = {};
    bool resolved[40] = {};
    int ownerCount = 0;
    int bad = 0;
    for (int i = 0; i < watch.count; ++i) {
        ComponentEntity* entity = watch.entities[i];
        PickingComponent* picking = entity->FindComponent<PickingComponent>();
        ModelComponent* model = entity->FindComponent<ModelComponent>();
        if (!picking) {
            if (verbose)
                fprintf(stderr, "[ToolHarness][%s]   entity %p: no PickingComponent\n",
                        watch.route, static_cast<void*>(entity));
            continue;
        }
        ++withPicking;
        const int id = picking->pickingId;
        void* owner = picking->userData.Get();
        if (id < 0) {
            if (verbose)
                fprintf(stderr, "[ToolHarness][%s]   entity %p: pickingId=-1 (meshIndex=%d), no "
                        "pick mesh yet\n", watch.route, static_cast<void*>(entity),
                        model ? model->meshIndex : -99);
            continue;
        }
        ++registered;
        const bool inRange = id < scene->_entries._size;
        const Picking::Scene::Entry* entry = inRange ? &scene->_entries._data[id] : nullptr;
        const bool liveOwn = entry && entry->_nextFree == -2 && entry->_userData == owner;
        int ownerComponents = 0;
        int sameId = 0;
        for (int j = 0; j < watch.count; ++j) {
            PickingComponent* other = watch.entities[j]->FindComponent<PickingComponent>();
            if (!other || other->pickingId < 0) continue;
            if (other->userData.Get() == owner) ++ownerComponents;
            if (other->pickingId == id) ++sameId;
        }
        const int ownerEntries = PickLiveEntries(scene, owner);

        // Straight-down rays over a 13x13 grid, 0.5 apart, around the model's origin: a
        // construction frame (stakes, a deck section) need not cover the origin itself.
        // The first ray that answers this owner wins; otherwise the first hit is kept.
        int hitId = -1;
        void* hitData = nullptr;
        for (int k = 0; model && k < 13 * 13 && hitData != owner; ++k) {
            const Math::Vector3& pos = model->transform.translation;
            const float dx = 0.5f * static_cast<float>(k % 13 - 6);
            const float dy = 0.5f * static_cast<float>(k / 13 - 6);
            const Math::Vector3 start(pos.x + dx, pos.y + dy, pos.z + 50.0f);
            const Math::Vector3 dir(0.0f, 0.0f, -1.0f);
            Math::Vector3 hitPos, hitNormal;
            const int id2 = scene->Intersect(start, dir, Math::Scalar(400.0f), hitPos, hitNormal);
            void* data2 = (id2 >= 0) ? scene->GetUserData(static_cast<uint32_t>(id2)) : nullptr;
            if (data2 && (data2 == owner || !hitData)) {
                hitId = id2;
                hitData = data2;
            }
        }
        const bool ok = liveOwn && sameId == 1 && ownerEntries == ownerComponents;
        int slot = 0;
        while (slot < ownerCount && owners[slot] != owner) ++slot;
        if (slot == ownerCount) owners[ownerCount++] = owner;
        if (hitData == owner) resolved[slot] = true;
        if (!ok) ++bad;
        if (verbose || !ok)
            fprintf(stderr, "[ToolHarness][%s]   entity %p pickingId=%d live=%d userData=%p (%s) "
                    "idUsers=%d ownerEntries=%d ownerComponents=%d meshIndex=%d ray hitId=%d -> %p "
                    "(%s) %s\n", watch.route, static_cast<void*>(entity), id, liveOwn ? 1 : 0,
                    owner, owner == entity ? "self" : "owner", sameId, ownerEntries,
                    ownerComponents, model ? model->meshIndex : -99, hitId, hitData,
                    hitData == owner ? "resolves" : (hitData ? "other entity" : "no hit"),
                    ok ? "ok" : "BAD");
    }
    *outWithPicking = withPicking;
    int picked = 0;
    for (int k = 0; k < ownerCount; ++k) {
        if (resolved[k]) ++picked;
    }
    *outRegistered = registered;
    *outOwners = ownerCount;
    *outPicked = picked;
    return bad;
}

// The same-frame check after a placement; arms the watch PickTick then polls.
void PickArm(int slot, const char* route, const PickSnapshot& before) {
    GameUpdateLoop* gw = GetGW();
    EntityList* list = gw->m_entityList;
    PickWatch& watch = s_pickWatch[slot];
    watch.route = route;
    for (int i = before.entities; i < list->masterArray._size && watch.count < 40; ++i)
        watch.entities[watch.count++] = list->masterArray._data[i].Get();
    int withPicking = 0;
    int registered = 0;
    int owners = 0;
    int picked = 0;
    const int bad = PickCheckEntities(watch, true, &withPicking, &registered, &owners, &picked);
    const int liveAfter = PickLiveEntries(gw->m_pickingScene, nullptr);
    const int gained = liveAfter - before.live;
    const bool ok = watch.count > 0 && bad == 0 && gained == registered;
    fprintf(stderr, "[ToolHarness][%s] AT PLACE %s: %d new entities, %d with PickingComponent, "
            "%d registered, scene live entries %d -> %d (+%d)\n", route, ok ? "ok" : "BAD",
            watch.count, withPicking, registered, before.live, liveAfter, gained);
    if (!ok) {
        fprintf(stderr, "[ToolHarness][%s] PICK FAIL at placement\n", route);
        watch.done = true;
    }
}

int BridgeWaitFrames();   // below: the give-up budget, shared with the bridge route

void PickTick(int frameNum) {
    for (PickWatch& watch : s_pickWatch) {
        if (!watch.route || watch.done) continue;
        if (watch.armFrame == 0) {
            watch.armFrame = frameNum;
            watch.lastPoll = frameNum;
            continue;
        }
        if (frameNum - watch.lastPoll < kPickPollFrames) continue;
        watch.lastPoll = frameNum;
        int withPicking = 0;
        int registered = 0;
        int owners = 0;
        int picked = 0;
        const int bad = PickCheckEntities(watch, false, &withPicking, &registered, &owners, &picked);
        const bool complete = withPicking > 0 && registered == withPicking;
        if (bad == 0 && !complete) {
            if (frameNum - watch.armFrame > BridgeWaitFrames()) {
                fprintf(stderr, "[ToolHarness][%s] PICK FAIL: %d of %d components registered "
                        "after %d frames\n", watch.route, registered, withPicking,
                        frameNum - watch.armFrame);
                watch.done = true;
            }
            continue;
        }
        if (bad == 0)
            PickCheckEntities(watch, true, &withPicking, &registered, &owners, &picked);
        const bool pass = bad == 0 && picked == owners;
        fprintf(stderr, "[ToolHarness][%s] PICK %s at frame %d (+%d): %d entities, %d with "
                "PickingComponent, %d registered once each, %d of %d owners resolved by a ray\n",
                watch.route, pass ? "PASS" : "FAIL", frameNum, frameNum - watch.armFrame,
                watch.count, withPicking, registered, picked, owners);
        watch.done = true;
    }
}

extern "C" ComponentEntity* g_BridgeLastMaster;
extern "C" ComponentEntity* g_BridgeLastSegments[32];
extern "C" int   g_BridgeLastSegmentCount;

enum BridgeStage { BR_IDLE = 0, BR_DRAG, BR_WAIT_BUILT, BR_SELECT_SETTLE, BR_DONE };
BridgeStage s_br_stage     = BR_IDLE;
int         s_br_armFrame  = 0;
int         s_br_lastPoll  = 0;
bool        s_br_dumped    = false;
bool        s_br_selected  = false;   // BANISHED_BRIDGE_SELECT fired yet?
// BANISHED_BRIDGE_DRAG_FRAMES=N — hold the tool in the DRAGGING state (+0x28=1) over the
// chosen span for N frames before committing, so BridgeTool::Draw (slot 8, the red/green
// preview ported in bridgetool_draw.cpp) renders the multi-cell drag path rather than only
// the single-cell hover the harness would otherwise produce.
DragTool*   s_br_tool      = nullptr;
int         s_br_dragStart = -1, s_br_dragEnd = -1;
int         s_br_dragUntil = 0;
bool        s_br_pickCheck = false;   // the pick route: check the commit's pick meshes, stop

void BridgeCommit(DragTool* tool, int startCell, int endCell);

int BridgeDragFrames() {
    const char* e = Opt().bridgeDragFrames;
    if (!e || !e[0]) return 0;
    int n = std::atoi(e);
    return (n > 0) ? n : 0;
}

int BridgePollPeriod() {
    const char* e = Opt().bridgePoll;
    int n = (e && e[0]) ? std::atoi(e) : 600;
    return n < 30 ? 600 : n;
}
int BridgeWaitFrames() {
    const char* e = Opt().bridgeWaitFrames;
    int n = (e && e[0]) ? std::atoi(e) : 60000;
    return n < 600 ? 60000 : n;
}

// One-shot inventory of what a placed segment actually carries, so "did builders
// finish it" is answered against the real component set rather than an assumption
// that a bridge segment is constructed like a building.
// Every component name the standalone factory registry knows, so an unrecognised
// type id in the dump means "not a game component we register", not "we forgot to
// list it here".
const char* const kComponentNames[] = {
    "AbsorbResourceComponent", "AmbientEmitterComponent", "AmbientEnvironmentComponent",
    "AnimalComponent", "AttendanceComponent", "AutoPickupComponent", "BoatComponent",
    "BuildComponent", "CemeteryComponent", "CharacterComponent", "CitizenAIComponent",
    "CitizenComponent", "ClearAreaComponent", "ClearResourceComponent",
    "ConsumeProduceComponent", "CropFieldComponent", "DecalComponent", "DestroyComponent",
    "DiseaseComponent", "FireComponent", "FishingComponent", "FloorComponent",
    "ForesterComponent", "FruitComponent", "GrowthComponent", "HappinessComponent",
    "HealerComponent", "HerbalistComponent", "HerdComponent", "HighlightComponent",
    "HuntComponent", "ImmigrationComponent", "InteractComponent", "LivestockComponent",
    "MapComponent", "MarketComponent", "MarketLocationComponent", "MineComponent",
    "ModelComponent", "MultiObjectComponent", "NaturalResourceComponent", "NomadComponent",
    "OrchardComponent", "ParticleComponent", "PastureComponent", "PickingComponent",
    "PopulationComponent", "QuarryComponent", "RawMaterialComponent", "ResidenceComponent",
    "SpawnComponent", "StateMachineComponent", "StatusIconComponent", "StorageComponent",
    "StorageDisplayComponent", "StorageLocationComponent", "TerraformComponent",
    "TombstoneComponent", "TornadoComponent", "TrackerComponent", "TradeComponent",
    "UIComponent", "WellComponent", "WildAnimalComponent", "WorkComponent",
    "WorkPlaceComponent", "ZoneComponent",
};

const char* BridgeComponentName(uint32_t tid) {
    static uint32_t ids[sizeof(kComponentNames) / sizeof(kComponentNames[0])] = {};
    static bool resolved = false;
    if (!resolved) {
        resolved = true;
        for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i)
            ids[i] = ::Object::GetTypeIdByName(kComponentNames[i]);
    }
    for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i)
        if (ids[i] && ids[i] == tid) return kComponentNames[i];
    return "?";
}

void BridgeDumpEntity(const char* what, ComponentEntity* ent) {
    if (!ent) { fprintf(stderr, "[Bridge] %s = null\n", what); return; }
    const uint32_t tidPicking = PickingComponent::TypeId();

    ComponentEntity* e = ent;
    const int count = e->_components._size;
    Object::InternalPtr<Component>* arr = e->_components._data;
    fprintf(stderr, "[Bridge] %s=%p components=%d:", what, ent, count);
    for (int i = 0; i < count && arr; ++i) {
        Component* comp = arr[static_cast<uintptr_t>(i)].Get();
        if (!comp) continue;
        uint32_t tid = comp->Type();
        fprintf(stderr, " %s(%u)", BridgeComponentName(tid), tid);
        // Picking: report the user data we seated — this is the SetMeshUserData fix.
        if (tidPicking && tid == tidPicking)
            fprintf(stderr, "{pickId=%d userData=%p master=%p}",
                   static_cast<PickingComponent*>(comp)->pickingId,
                   static_cast<PickingComponent*>(comp)->userData.Get(),
                   g_BridgeLastMaster);
    }
    fprintf(stderr, "\n");
}

void FireBridgeTool(PlayerToolManager* entityUI) {
    if (!entityUI) { fprintf(stderr, "[ToolHarness][bridge] ERROR: entityUI null\n"); return; }

    const char* tmplPath = Opt().bridgeTemplate;
    if (!tmplPath || !tmplPath[0]) tmplPath = "Template\\WoodBridge.rsc";
    fprintf(stderr, "[ToolHarness][bridge] resolving template \"%s\"...\n", tmplPath);
    ComponentDescription* tmpl = StartTown_ResolveTemplateByPath(tmplPath);
    fprintf(stderr, "[ToolHarness][bridge]   -> %p\n", tmpl);
    if (!tmpl) { fprintf(stderr, "[ToolHarness][bridge] ERROR: template did not resolve\n"); return; }

    entityUI->SelectTool(tmpl);   // 0x140102b60
    Tool* tool = entityUI->currentTool.Get();
    fprintf(stderr, "[ToolHarness][bridge] after SetEditMode: activeTool=%p\n", tool);
    if (!tool) {
        fprintf(stderr, "[ToolHarness][bridge] activation FAILED — expect a node{CreateBridge -> "
               "BridgeTool} in the [Tools] BuildToolModeTree line.\n");
        return;
    }
    // The tool the tree installs for a CreateBridgeDescription is the BridgeTool.
    BridgeTool* bridge = static_cast<BridgeTool*>(tool);
    CreateBridgeDescription* bridgeDesc = bridge->bridgeDesc;
    fprintf(stderr, "[ToolHarness][bridge]   descriptionPtr(+0x20)=%p bridgeDesc(+0x38)=%p\n",
           bridge->descriptionPtr, bridgeDesc);
    if (!bridgeDesc) {
        fprintf(stderr, "[ToolHarness][bridge] FAILED: Startup left bridgeDesc null — the tool "
               "descriptor carries no CreateBridgeDescription.\n");
        return;
    }

    // The masks ValidatePath enforces (see bridgetool_validate.cpp).
    const uint32_t maskBody     = bridgeDesc->_u20;
    const uint32_t maskApproach = bridgeDesc->_u24;
    fprintf(stderr, "[ToolHarness][bridge]   maskBody=0x%x maskApproach=0x%x maskRequired=0x%x "
           "maskNeighbor=0x%x radius=%d\n", maskBody, maskApproach,
           bridgeDesc->_u2c, bridgeDesc->_u30, bridgeDesc->_i34);

    GameUpdateLoop* gw = GameUpdateLoop::_singleton;
    Map* mapGrid = gw ? gw->m_game : nullptr;
    MapCell* terrain = mapGrid ? mapGrid->cells.GetData() : nullptr;
    if (!terrain) {
        fprintf(stderr, "[ToolHarness][bridge] ERROR: no terrain array (gw=%p mapGrid=%p)\n", gw, mapGrid);
        return;
    }
    const int W = mapGrid->size;
    auto bitAt = [&](int idx) -> uint32_t {
        return 1u << (terrain[static_cast<uintptr_t>(idx)].terrainType & 0x1f);
    };

    // The BridgeTool: a DragTool. CalcCost is retail [14] (+0x70). Dispatched through
    // DragTool (not the final BridgeTool) so the slot stays a virtual call.
    DragTool* drag = static_cast<DragTool*>(tool);

    // Where the town is, so we cross NEAR it. The first crossable span on the map is
    // typically at the map edge, and builders then spend the whole job walking (and a
    // fair number of them dying) to reach it. Average the founding houses' MapComponent
    // cells; fall back to the map centre if the start town is not up yet.
    int townCol = W / 2, townRow = W / 2;
    {
        const uint32_t tidMap = MapComponent::TypeId();
        int32_t sumCol = 0, sumRow = 0; int n = 0;
        const int houses = StartTown_GetFoundingHouseCount();
        for (int i = 0; i < houses && tidMap; ++i) {
            ComponentEntity* e = StartTown_GetFoundingHouseEntity(i);
            if (!e) continue;
            const int cc = e->_components._size;
            Object::InternalPtr<Component>* ca = e->_components._data;
            for (int k = 0; k < cc && ca; ++k) {
                Component* comp = ca[k].Get();
                if (!comp) continue;
                if (comp->Type() != tidMap) continue;
                const MapComponent* mc = static_cast<const MapComponent*>(comp);   // Type() == MapComponent's id
                sumCol += mc->tileX0;
                sumRow += mc->tileY0;
                ++n;
                break;
            }
        }
        if (n > 0) { townCol = static_cast<int>(sumCol / n); townRow = static_cast<int>(sumRow / n); }
        fprintf(stderr, "[ToolHarness][bridge] town centre = (col %d, row %d) from %d founding "
               "house(s)%s\n", townCol, townRow, n, n ? "" : " — falling back to map centre");
    }

    // Collect every bank→water→bank run (rows, then columns; body 2..kMaxBody cells),
    // then test them NEAREST-TOWN-FIRST and let the tool's own CalcCost be the judge.
    struct Cand { int a, b, line, from, to, len, axis, dist; };
    const int kMaxCand = 512;
    Cand cands[kMaxCand];
    int nCand = 0, nSeen = 0;
    // BANISHED_BRIDGE_LONGEST=<minLen> — rank candidates by SPAN LENGTH (longest
    // first) instead of by distance to town, and raise the body cap to match.
    //
    // The Golden Gate achievement wants a bridge at least 50 units long (stat 29).
    // The default ranking is nearest-town-first with a 24-cell body cap, which is
    // right for "prove the bridge tool works" -- it picks the little crossing beside
    // the settlement -- and makes a 50-cell span doubly unreachable: too long to be
    // collected at all, and outranked by every short one if it were. Length ranking is
    // opt-in so the ordinary bridge test keeps choosing the convenient crossing.
    const int kMaxAttempts = 400;
    int  wantLongest = 0;
    if (const char* e = Opt().bridgeLongest) {
        const int v = std::atoi(e);
        if (v > 0) wantLongest = v;
    }
    const int kMaxBody = wantLongest ? 200 : 24;
    if (wantLongest)
        fprintf(stderr, "[ToolHarness][bridge] LONGEST-SPAN mode: want >= %d cells, "
                        "body cap %d, ranking by length""\n", wantLongest, kMaxBody);
    for (int axis = 0; axis < 2; ++axis) {
        for (int line = 1; line < W - 1; ++line) {
            int runStart = -1;
            for (int p = 1; p < W - 1; ++p) {
                const int idx = (axis == 0) ? (W * line + p) : (W * p + line);
                const uint32_t bit = bitAt(idx);
                if (maskBody & bit) {
                    if (runStart < 0) runStart = p;
                    continue;
                }
                if (runStart >= 0) {
                    const int len = p - runStart;
                    const int beforeIdx = (axis == 0) ? (W * line + runStart - 1)
                                                      : (W * (runStart - 1) + line);
                    if (len >= 2 && len <= kMaxBody &&
                        (maskApproach & bitAt(beforeIdx)) && (maskApproach & bit)) {
                        // midpoint of the span, in cells
                        const int midP = runStart + len / 2;
                        const int mCol = (axis == 0) ? midP : line;
                        const int mRow = (axis == 0) ? line : midP;
                        const int dc = mCol - townCol, dr = mRow - townRow;
                        const Cand cand = { beforeIdx,
                                            (axis == 0) ? (W * line + p) : (W * p + line),
                                            line, runStart - 1, p, len, axis,
                                            dc * dc + dr * dr };
                        ++nSeen;
                        if (nCand < kMaxCand) {
                            cands[nCand++] = cand;
                        } else {
                            // Keep the NEAREST kMaxCand: evict the current farthest rather
                            // than dropping everything past the cap, which would silently
                            // bias the pick towards whatever the scan happened to reach first.
                            int worst = 0;
                            if (wantLongest) {
                                for (int q = 1; q < kMaxCand; ++q)
                                    if (cands[q].len < cands[worst].len) worst = q;
                                if (cand.len > cands[worst].len) cands[worst] = cand;
                            } else {
                                for (int q = 1; q < kMaxCand; ++q)
                                    if (cands[q].dist > cands[worst].dist) worst = q;
                                if (cand.dist < cands[worst].dist) cands[worst] = cand;
                            }
                        }
                    }
                    runStart = -1;
                }
            }
        }
    }
    // Insertion sort by distance — nCand is small and this keeps the harness allocation-free.
    for (int i = 1; i < nCand; ++i) {
        Cand key = cands[i];
        int j = i - 1;
        if (wantLongest)
            while (j >= 0 && cands[j].len < key.len) { cands[j + 1] = cands[j]; --j; }
        else
            while (j >= 0 && cands[j].dist > key.dist) { cands[j + 1] = cands[j]; --j; }
        cands[j + 1] = key;
    }
    if (wantLongest && nCand > 0)
        fprintf(stderr, "[ToolHarness][bridge] best qualifying span = %d cells at "
                        "dist^2=%d (%d candidates)\n", cands[0].len, cands[0].dist, nCand);
    int startCell = -1, endCell = -1, attempts = 0;
    for (int i = 0; i < nCand && attempts < kMaxAttempts && startCell < 0; ++i) {
        const Cand& c = cands[i];
        ++attempts;
        bridge->startCell = c.a;
        bridge->endCell   = c.b;
        uint32_t pathType = 0; int h = 0, wd = 0; int extra = 0;   // pathType printed %d as before
        int rc = drag->CalcCost(c.a, c.b, &pathType, &h, &wd,
                                &extra);
        if (rc == 0 && pathType == 1) {
            startCell = c.a; endCell = c.b;
            fprintf(stderr, "[ToolHarness][bridge] span accepted after %d of %d candidates "
                   "(nearest-town first): %s line=%d %d..%d start=%d end=%d bodyLen=%d "
                   "dist^2=%d from town (CalcCost rc=%d valid=%d)\n",
                   attempts, nCand, c.axis == 0 ? "row" : "col", c.line, c.from, c.to,
                   c.a, c.b, c.len, c.dist, rc, pathType);
        }
    }
    if (startCell < 0) {
        fprintf(stderr, "[ToolHarness][bridge] ERROR: no crossable span found (%d of %d candidates "
               "tried, all rejected by CalcCost)\n", attempts, nCand);
        return;
    }

    // Optionally hold the drag open first so slot 8 (Draw) renders the span preview.
    if (BridgeDragFrames() > 0) {
        bridge->dragState = 1;                               // dragState = dragging
        bridge->startCell = startCell;
        bridge->endCell   = endCell;
        s_br_tool = drag; s_br_dragStart = startCell; s_br_dragEnd = endCell;
        s_br_dragUntil = 0;                                  // armed on the first tick
        s_br_stage = BR_DRAG;
        fprintf(stderr, "[ToolHarness][bridge] holding the drag open for %d frames (%d..%d) so "
               "BridgeTool::Draw renders the preview...\n",
               BridgeDragFrames(), startCell, endCell);
        return;
    }
    BridgeCommit(drag, startCell, endCell);
}

// DragTool::CalcPath (retail [13], +0x68) — the commit a drag-release makes.
void BridgeCommit(DragTool* tool, int startCell, int endCell) {
    tool->dragState = 0;                                     // drag released
    const PickSnapshot pickBefore = s_br_pickCheck ? PickTakeSnapshot() : PickSnapshot();
    bool placed = tool->CalcPath(startCell, endCell);
    fprintf(stderr, "[ToolHarness][bridge] CalcPath (commit) -> %d; master=%p segments=%d\n",
           placed ? 1 : 0, g_BridgeLastMaster, g_BridgeLastSegmentCount);
    if (s_br_pickCheck) {
        PickArm(1, "pickbridge", pickBefore);
        s_br_stage = BR_DONE;
        return;
    }
    if (!placed || g_BridgeLastSegmentCount == 0) {
        fprintf(stderr, "[ToolHarness][bridge] nothing was placed — stopping.\n");
        return;
    }
    BridgeDumpEntity("master", g_BridgeLastMaster);
    for (int i = 0; i < g_BridgeLastSegmentCount; ++i) {
        char label[32];
        std::snprintf(label, sizeof(label), "segment[%d]", i);
        BridgeDumpEntity(label, g_BridgeLastSegments[i]);
    }
    s_br_dumped = true;
    s_br_stage    = BR_WAIT_BUILT;
    s_br_armFrame = 0;   // set on the first tick
}

// ---------------------------------------------------------------------------
// BANISHED_BRIDGE_SELECT — reproduce the "click a placed bridge" crash headlessly.
//
// A human found (2026-07-26) that clicking a placed bridge crashes the NEXT frame in
// Graphics::EdgeMeshInstance::Draw -> MeshInstance::GetNodeToWorld -> Transform::Lerp.
// That is the SELECTION OUTLINE, not the info dialog: the click submits a highlight
// mesh to the Graphics::Scene and the following render trips over it.
//
// It drives the same EditTool::SelectEntity path FireSelectEntity uses for houses, so
// both halves of a click run: the info DialogInstance is built AND Entity_impl_c9e0
// submits the selection outline.
//
// Why a bridge and not a house: c9e0 walks the entity's MultiObjectComponent CHILDREN
// (FUN_140004740 -> +0x48 array / +0x50 count) and outlines each one. That walk
// returned null for every entity in the game until 0x1401c7ac0 was registered in the
// previous wave, so this path is newly live — and the bridge, whose segments are those
// children, is the entity that exercises it.
//
// Set BANISHED_BRIDGE_SELECT=1 (optionally =N for "N frames after the commit",
// default 120) to fire it. BANISHED_OUTLINE_TRACE=1 shows where c4a0 lands per segment.

int BridgeSelectDelay() {
    const char* e = Opt().bridgeSelect;
    if (!e || !e[0] || std::strcmp(e, "0") == 0 || std::strcmp(e, "false") == 0) return -1;
    int n = std::atoi(e);
    return (n > 1) ? n : 120;
}

void BridgeSelectFire(PlayerToolManager* entityUI) {
    if (!g_BridgeLastMaster) {
        fprintf(stderr, "[Bridge][select] no master entity — nothing to select.\n");
        return;
    }
    // The bridge harness left the BridgeTool active at entityUI+0x28; swap back to the
    // default EditTool (the one a player would be holding when they click a bridge).
    // The current tool is taken as the EditTool (SelectEntity refuses any other tool).
    entityUI->SelectTool(nullptr);
    EditTool* tool = entityUI ? static_cast<EditTool*>(entityUI->currentTool.Get())
                              : nullptr;
    if (!tool) {
        fprintf(stderr, "[Bridge][select] EditTool activation FAILED (nothing at entityUI+0x28) — "
               "needs BANISHED_LIVE_TOOLS=1.\n");
        return;
    }
    fprintf(stderr, "[Bridge][select] EditTool active (%p); selecting bridge master=%p — this is a "
           "full click (info dialog + selection outline).\n", tool, g_BridgeLastMaster);
    fflush(stdout);

    const int opened = EditTool_HarnessSelectEntity(tool, g_BridgeLastMaster);

    fprintf(stderr, "[Bridge][select] select -> %d (1=info dialog opened). Returned without "
           "faulting; a crash from this lands in the NEXT Draw.\n", opened);
    fflush(stdout);
}

// Poll the placed segments until each reports delivered >= required.
void BridgeTick(PlayerToolManager* entityUI, int frameNum) {
    if (!s_br_selected) {
        const int delay = BridgeSelectDelay();
        if (delay > 0 && s_br_armFrame != 0 && frameNum - s_br_armFrame >= delay) {
            s_br_selected = true;
            BridgeSelectFire(entityUI);
        }
    }

    if (s_br_armFrame == 0) { s_br_armFrame = frameNum; s_br_lastPoll = frameNum; }
    if (frameNum - s_br_lastPoll < BridgePollPeriod()) {
        if (frameNum - s_br_armFrame <= BridgeWaitFrames()) return;
    }
    s_br_lastPoll = frameNum;

    // The construction site may be the MASTER (one build job for the whole span) rather
    // than the individual deck pieces, so poll both.
    int done = 0, withBuild = 0;
    BuildComponent* mbc = UpgradeFindBuildComp(g_BridgeLastMaster);
    if (mbc) {
        ++withBuild;
        int delivered = UpgradeDelivered(mbc), required = UpgradeRequired(mbc);
        if (required > 0 && delivered >= required) ++done;
        fprintf(stderr, "[Bridge] frame %d master=%p build=%p delivered=%d required=%d\n",
               frameNum, g_BridgeLastMaster, mbc, delivered, required);
    }
    for (int i = 0; i < g_BridgeLastSegmentCount; ++i) {
        BuildComponent* bc = UpgradeFindBuildComp(g_BridgeLastSegments[i]);
        if (!bc) continue;
        ++withBuild;
        int delivered = UpgradeDelivered(bc);
        int required  = UpgradeRequired(bc);
        if (required > 0 && delivered >= required) ++done;
        if (i < 4)
            fprintf(stderr, "[Bridge] frame %d segment[%d]=%p build=%p delivered=%d required=%d\n",
                   frameNum, i, g_BridgeLastSegments[i], bc, delivered, required);
    }
    fprintf(stderr, "[Bridge] frame %d (%d since commit): %d/%d build sites complete "
           "(master=%s, %d segments)\n", frameNum, frameNum - s_br_armFrame,
           done, withBuild, mbc ? "yes" : "no", g_BridgeLastSegmentCount);

    if (withBuild > 0 && done == withBuild) {
        fprintf(stderr, "[Bridge] COMPLETE — builders finished the bridge.\n");
        // Click it again now that it is BUILT, not a foundation: completion swaps the
        // ModelComponent to the finished mesh, so the selection outline's per-variant
        // prebuilt lookup (c4a0's PREBUILT branch) resolves a different mesh than it did
        // on the foundation. Selecting only the foundation would leave that untested.
        if (BridgeSelectDelay() > 0) {
            fprintf(stderr, "[Bridge][select] re-selecting the COMPLETED bridge...\n");
            BridgeSelectFire(entityUI);
            s_br_stage = BR_SELECT_SETTLE;
            s_br_lastPoll = frameNum;
            return;
        }
        s_br_stage = BR_DONE;
        return;
    }
    if (withBuild == 0) {
        fprintf(stderr, "[Bridge] neither the master nor any segment carries a BuildComponent — a "
               "bridge is not constructed like a building; see the component dump above "
               "for what actually drives it.\n");
        s_br_stage = BR_DONE;
        return;
    }
    if (frameNum - s_br_armFrame > BridgeWaitFrames()) {
        fprintf(stderr, "[Bridge] TIMEOUT after %d frames with %d/%d complete.\n",
               frameNum - s_br_armFrame, done, withBuild);
        s_br_stage = BR_DONE;
    }
}

// ============================================================================
// B8 RES -- BANISHED_TOOL_HARNESS=pick (known_port_issues row 252, audit Q2)
// ============================================================================
// Picks a citizen through the real picking path, the way EditTool::OnKey
// (0x1400fd030) does after its mouse ray: Picking::Scene::Intersect on a ray, the
// hit's Picking::Scene::GetUserData (the entity PickingComponent::RefreshMesh
// 0x140128f50 registered with the pick mesh), then EditTool::SelectEntity
// (0x1400fd3b0). Only the OS cursor is replaced: the ray starts 50 units above the
// citizen's model position and points straight down (Z is up). A citizen whose pick
// mesh was never registered (row 252's upstream finding) answers no hit.
// Success line: "[ToolHarness][pick] PICKED citizen ... panel OPENED ... CitizenUI=1".
// The same route then places a building and a bridge for row 1261 (PickArm / PickTick).
void FirePick(PlayerToolManager* toolManager) {
    if (!toolManager) { fprintf(stderr, "[ToolHarness][pick] ERROR: tool manager null\n"); return; }
    EditTool* tool = EditTool_HarnessAcquire(toolManager);
    if (!tool) { fprintf(stderr, "[ToolHarness][pick] EditTool activation FAILED\n"); return; }

    GameUpdateLoop* gw = GetGW();
    Picking::Scene* scene = gw->m_pickingScene;
    CitizenBehavior* town = gw->m_citizenList;
    const int citizenCount = town->citizens._size;
    fprintf(stderr, "[ToolHarness][pick] citizens=%d pickingScene=%p EditTool=%p\n",
            citizenCount, static_cast<void*>(scene), static_cast<void*>(tool));

    int tried = 0;
    for (int i = 0; i < citizenCount && tried < 16; ++i) {
        CitizenComponent* citizen = town->citizens._data[i].Get();
        ComponentEntity* entity = citizen->_entity.Get();
        ModelComponent* model = entity->FindComponent<ModelComponent>();
        PickingComponent* picking = entity->FindComponent<PickingComponent>();
        if (!model) continue;   // harness: nothing to aim at
        ++tried;

        const Math::Vector3& pos = model->transform.translation;
        const Math::Vector3 start(pos.x, pos.y, pos.z + 50.0f);
        const Math::Vector3 dir(0.0f, 0.0f, -1.0f);
        Math::Vector3 hitPos, hitNormal;
        const int hitId = scene->Intersect(start, dir, Math::Scalar(400.0f), hitPos, hitNormal);
        void* hitData = (hitId >= 0) ? scene->GetUserData(static_cast<uint32_t>(hitId)) : nullptr;
        fprintf(stderr, "[ToolHarness][pick] citizen[%d] entity=%p pos=(%.2f,%.2f,%.2f) "
                "PickingComponent=%p pickingId=%d -> hitId=%d userData=%p (%s)\n",
                i, static_cast<void*>(entity), pos.x, pos.y, pos.z,
                static_cast<void*>(picking), picking ? picking->pickingId : -1,
                hitId, hitData,
                hitData == entity ? "this citizen" : (hitData ? "another entity" : "no hit"));
        if (hitData != entity) continue;

        Object::InternalPtr<ComponentEntity> entityParam(entity);
        tool->SelectEntity(&entityParam);   // 0x1400fd3b0, as OnKey
        UI::DialogInstance* dialog = tool->entityRef;
        const bool citizenUI = dialog && dialog->GetController(CitizenUI::TypeId()).Get() != nullptr;
        fprintf(stderr, "[ToolHarness][pick] PICKED citizen entity=%p -> panel %s (dialog=%p, "
                "CitizenUI=%d)\n", static_cast<void*>(entity), dialog ? "OPENED" : "NOT opened",
                static_cast<void*>(dialog), citizenUI ? 1 : 0);
        return;
    }
    fprintf(stderr, "[ToolHarness][pick] FAILED: no citizen answered its own pick ray "
            "(%d tried)\n", tried);
}

// ============================================================================
// row 41 -- BANISHED_TOOL_HARNESS=spritecapture
// ============================================================================
// Retail reaches SpriteCaptureUI through Game/Toolbar.rsc's hidden "capture" entry
// (`_visible = false`, Action Utility, hot key Camera0, autoHotKey):
// Template/UtilityCapture.rsc, whose UIDescription inserts SpriteCaptureUI with
// Dialog/Capture.rsc into Dialog/StandardDialog.rsc. The exe names no dialog path for
// it (its only Dialog\ literals are MainMenu, Loading and ShiningRock). This route opens
// it the way the toolbar's Utility action does (PlayerToolManager::ToggleUtilityDialog
// 0x140101a80), types a file name into editCapture the way a player would, and
// kCaptureClickDelay frames later clicks buttonCapture: DialogInstance::OnElementEvent
// with msg 0x12 and the id OnCreated gave it (1000), so slot 9 (0x140079620) runs
// StartCapture (0x1400792b0). OnUpdate (0x1400793a0) saves one frame later; the route
// reports the controller state and the file SaveText wrote.
// THE SAVE FAULTS, AS IN THE SHIPPED GAME: External::SaveText (0x1800bb9d0) writes
// through a TextFileStream whose TextEncoding (vtable 0x1801559d0) answers CreateWriter
// with null (0x1800b9a70, `XOR EAX,EAX; RET`), and SpriteCapture::Serialize
// (0x1800d9530) calls through that writer unguarded (0x1800d958d). Only the toolkit's
// Runtime-x64-profile.dll exports a TextWriter. So this mode reproduces the developer
// tool up to that fault (known_port_issues row 41); it is not a PASS/FAIL route.
// Lines: "... buttonCapture clicked -> pendingExternal=<non-null>", then the crash in
// SpriteCapture::Serialize; "SAVED <path>" would mean a text writer exists.
constexpr int kCaptureClickDelay = 60;
constexpr int kCaptureCheckDelay = 10;
const char* const kCaptureName = "Game/Help/Dialog/HarnessCapture";
int                 s_sc_clickFrame = 0;
int                 s_sc_checkFrame = 0;
UI::DialogInstance* s_sc_dialog     = nullptr;
SpriteCaptureUI*    s_sc_ui         = nullptr;

void FireSpriteCapture(PlayerToolManager* toolManager, int frameNum) {
    const char* kTemplate = "Template\\UtilityCapture.rsc";
    ComponentDescription* desc = StartTown_ResolveTemplateByPath(kTemplate);
    if (!desc) {
        fprintf(stderr, "[ToolHarness][spritecapture] FAILED: %s did not resolve\n", kTemplate);
        return;
    }
    UI::DialogInstance* dialog = toolManager->ToggleUtilityDialog(desc);   // 0x140101a80
    SpriteCaptureUI* ui = dialog
        ? static_cast<SpriteCaptureUI*>(dialog->GetController(SpriteCaptureUI::TypeId()).Get())
        : nullptr;
    fprintf(stderr, "[ToolHarness][spritecapture] %s -> dialog=%p SpriteCaptureUI=%p\n", kTemplate,
            static_cast<void*>(dialog), static_cast<void*>(ui));
    if (!ui) {
        fprintf(stderr, "[ToolHarness][spritecapture] FAILED: no SpriteCaptureUI on the dialog\n");
        return;
    }
    fprintf(stderr, "[ToolHarness][spritecapture] OnCreated state: editCapture=%p buttonCapture=%p "
            "captureBox=%p box=(%d,%d)-(%d,%d) text=\"%s\"\n", static_cast<void*>(ui->editCapture),
            static_cast<void*>(ui->buttonCapture), static_cast<void*>(ui->captureBox), ui->curX,
            ui->curY, ui->anchorX, ui->anchorY,
            ui->editCapture ? ui->editCapture->GetText()._data : "");
    System::String name(kCaptureName);
    ui->editCapture->SetText(name);
    s_sc_dialog = dialog;
    s_sc_ui = ui;
    s_sc_clickFrame = frameNum + kCaptureClickDelay;
}

void SpriteCaptureTick(int frameNum) {
    if (s_sc_clickFrame != 0 && frameNum >= s_sc_clickFrame) {
        s_sc_clickFrame = 0;
        Object::InternalPtr<UI::Element> button(s_sc_ui->buttonCapture);
        s_sc_dialog->OnElementEvent(&button, 0x12, 1000, 0, 0, 0, nullptr);   // the button click
        fprintf(stderr, "[ToolHarness][spritecapture] frame %d: buttonCapture clicked -> "
                "pendingExternal=%p saveFrameCount=%d box=(%d,%d)-(%d,%d)\n", frameNum,
                static_cast<void*>(s_sc_ui->pendingExternal), s_sc_ui->saveFrameCount,
                s_sc_ui->curX, s_sc_ui->curY, s_sc_ui->anchorX, s_sc_ui->anchorY);
        if (!s_sc_ui->pendingExternal) {
            fprintf(stderr, "[ToolHarness][spritecapture] FAILED: the click started no capture\n");
            return;
        }
        s_sc_checkFrame = frameNum + kCaptureCheckDelay;
        return;
    }
    if (s_sc_checkFrame == 0 || frameNum < s_sc_checkFrame) return;
    s_sc_checkFrame = 0;
    // OnUpdate's own path: GetSourceDirectory + the edit text + ".rsc".
    const System::String sourceDir = ::IO::FileSystem::_singleton->GetSourceDirectory().AsString();
    const System::String path = sourceDir + s_sc_ui->editCapture->GetText() + ".rsc";
    fprintf(stderr, "[ToolHarness][spritecapture] frame %d: pendingExternal=%p sourceDirectory=\"%s\"\n",
            frameNum, static_cast<void*>(s_sc_ui->pendingExternal), sourceDir._data);
    if (s_sc_ui->pendingExternal) {
        fprintf(stderr, "[ToolHarness][spritecapture] FAILED: OnUpdate never reached its save arm\n");
        return;
    }
    FILE* f = std::fopen(path._data, "rb");
    if (!f) {
        fprintf(stderr, "[ToolHarness][spritecapture] FAILED: OnUpdate saved but %s is not there\n",
                path._data);
        return;
    }
    char head[512] = {};
    const size_t got = std::fread(head, 1, sizeof(head) - 1, f);
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fclose(f);
    fprintf(stderr, "[ToolHarness][spritecapture] SAVED %s (%ld bytes); head:\n", path._data, size);
    int lines = 0;
    for (char* line = head; line < head + got && lines < 16; ++lines) {
        char* end = std::strchr(line, '\n');
        if (end) *end = '\0';
        fprintf(stderr, "[ToolHarness][spritecapture]   | %s\n", line);
        if (!end) break;
        line = end + 1;
    }
}

// ============================================================================
// B8 RES -- BANISHED_TOOL_HARNESS=panelclose (row 994 G33, audit Q3)
// ============================================================================
// Opens a founding house's info panel through the real select (EditTool::SelectEntity),
// then kPanelCloseDelay frames later clicks its close button the way the UI delivers a
// click: DialogInstance::OnElementEvent (0x180114ed0) with the dialog's "buttonClose"
// element, msg 0x12, id 1 -- every controller's slot 9 runs, ToolUI's close arm
// (0x14007bc30: CloseDialog) and EntityUI's id-1 arm (0x140067050: SelectTool(0), the
// tool teardown that deselects). Five frames later it reports whether the dialog left
// the UserInterface's open list. The old dialog / tool are compared by pointer only
// after the click, never dereferenced.
// Success lines: "[ToolHarness][panelclose] CLOSE clicked ... tool replaced=1" and
// "[ToolHarness][panelclose] after 5 frames: dialog open=0 ... CLOSED".
constexpr int kPanelCloseDelay = 120;
int                 s_pc_closeFrame = 0;
int                 s_pc_checkFrame = 0;
UI::DialogInstance* s_pc_dialog     = nullptr;
EditTool*           s_pc_tool       = nullptr;

void FirePanelClose(PlayerToolManager* toolManager, int frameNum) {
    if (!toolManager) { fprintf(stderr, "[ToolHarness][panelclose] ERROR: tool manager null\n"); return; }
    EditTool* tool = EditTool_HarnessAcquire(toolManager);
    if (!tool) { fprintf(stderr, "[ToolHarness][panelclose] EditTool activation FAILED\n"); return; }

    const int houses = StartTown_GetFoundingHouseCount();
    for (int i = 0; i < houses; ++i) {
        ComponentEntity* house = StartTown_GetFoundingHouseEntity(i);
        if (!house) continue;
        if (!EditTool_HarnessSelectEntity(tool, house)) continue;
        s_pc_dialog     = tool->entityRef;
        s_pc_tool       = tool;
        s_pc_closeFrame = frameNum + kPanelCloseDelay;
        fprintf(stderr, "[ToolHarness][panelclose] OPEN house[%d]=%p dialog=%p EditTool=%p; "
                "close scheduled for frame %d\n", i, static_cast<void*>(house),
                static_cast<void*>(s_pc_dialog), static_cast<void*>(tool), s_pc_closeFrame);
        return;
    }
    fprintf(stderr, "[ToolHarness][panelclose] FAILED: no founding house opened a panel "
            "(houses=%d)\n", houses);
}

void PanelCloseTick(PlayerToolManager* toolManager, int frameNum) {
    UI::UserInterface* ui = GetGW()->m_userInterface;
    if (s_pc_closeFrame != 0 && frameNum >= s_pc_closeFrame) {
        s_pc_closeFrame = 0;
        UI::DialogInstance* dialog = s_pc_dialog;
        Object::InternalPtr<UI::DialogInstance> dialogRef(dialog);
        const bool openBefore = ui->IsDialogOpen(&dialogRef);
        System::String closeName("buttonClose");
        Object::InternalPtr<UI::Element> closeButton = dialog->FindElement(closeName);
        fprintf(stderr, "[ToolHarness][panelclose] frame %d: dialog=%p open=%d buttonClose=%p "
                "EditTool=%p selected=%p\n", frameNum, static_cast<void*>(dialog),
                openBefore ? 1 : 0, static_cast<void*>(closeButton.Get()),
                static_cast<void*>(s_pc_tool), static_cast<void*>(s_pc_tool->lastEntityRef));
        if (!closeButton.Get()) {
            fprintf(stderr, "[ToolHarness][panelclose] FAILED: the panel has no buttonClose\n");
            return;
        }
        dialog->OnElementEvent(&closeButton, 0x12, 1, 0, 0, 0, nullptr);   // the button click
        Tool* toolAfter = toolManager->currentTool.Get();
        EditTool* editAfter = EditTool_IsEditTool(toolAfter) ? static_cast<EditTool*>(toolAfter)
                                                             : nullptr;
        fprintf(stderr, "[ToolHarness][panelclose] CLOSE clicked: tool replaced=%d "
                "(was %p, now %p) new EditTool dialog=%p selected=%p\n",
                toolAfter != s_pc_tool ? 1 : 0, static_cast<void*>(s_pc_tool),
                static_cast<void*>(toolAfter),
                editAfter ? static_cast<void*>(editAfter->entityRef) : nullptr,
                editAfter ? static_cast<void*>(editAfter->lastEntityRef) : nullptr);
        s_pc_checkFrame = frameNum + 5;
        return;
    }
    if (s_pc_checkFrame != 0 && frameNum >= s_pc_checkFrame) {
        s_pc_checkFrame = 0;
        Object::InternalPtr<UI::DialogInstance> dialogRef(s_pc_dialog);   // pointer compare only
        const bool openAfter = ui->IsDialogOpen(&dialogRef);
        fprintf(stderr, "[ToolHarness][panelclose] after 5 frames: dialog open=%d (%s)\n",
                openAfter ? 1 : 0, openAfter ? "STILL OPEN" : "CLOSED");
    }
}

}  // namespace

// GameUpdateLoop's Update hands its m_toolManager over (gameupdateloop.cpp; its local
// declaration still spells the parameter void*, C linkage).
extern "C" void ToolHarness_Tick(PlayerToolManager* entityUI, int frameNum) {

    Init();
    if (!s_enabled) return;

    // markremoval drain observation: after the mark fires, log removalList._size +
    // the tile slot for ~12 frames so we can watch EntityList::Update drain it.
    if (s_mr_fired) {
        int rel = frameNum - s_mr_fireFrame;
        if (rel >= 0 && rel <= 12) {
            GameUpdateLoop* gw = GetGW();
            if (gw) {
                EntityList* el = gw->m_entityList;
                int cnt = el ? el->removalList._size : -1;
                ComponentEntity* slotVal = s_mr_tileSlot ? s_mr_tileSlot->Get() : nullptr;
                bool drained = s_mr_tileSlot ? (cnt == 0 && slotVal != s_mr_entity)
                                             : (cnt == 0);
                fprintf(stderr, "[Removal] +%2d frames: removalList.count=%d  tileSlot=%p  (%s)\n",
                       rel, cnt, slotVal,
                       drained ? "DRAINED — marked entities removed" : "pending");
            }
        }
    }

    // pinpanel: after the mark fires, each frame compare the RAW datasource read
    // (+0x28, what SA_DialogEntityAlive does) against a CheckPtr-hardened read, and
    // track when the entity actually frees. rawDS staying non-null while checkedDS
    // nulls == the current guard would read freed memory; the CheckPtr is the fix.
    if (s_pin_fired && s_pin_ctrl) {
        int rel = frameNum - s_pin_fireFrame;
        if (rel >= 0 && rel <= 14) {
            GameUpdateLoop* gw = GetGW();
            EntityList* el = gw ? gw->m_entityList : nullptr;
            int cnt = el ? el->removalList._size : -1;
            UI::DialogController* ctrl = s_pin_ctrl;
            // RAW +0x28 (guard's read) — pointer only, never dereferenced here.
            void* rawDS = ctrl->_dataSource;
            // CheckPtr-hardened read: CheckPtr a COPY of the +0x28 IBP so the probe
            // is non-destructive; it nulls if the weak-ref's target was freed.
            void* checkedDS = ctrl->_dataSource;
            // NOTE: CheckPtr does NOT null this dangling ref (the datasource weak-ref
            // isn't generation-invalidated by a raw free) — so a guard-side CheckPtr is
            // NOT the fix. The real fix closes the dialog at the drain when its entity is
            // marked-for-removal (EntityList_CloseDialogsForDyingEntities). With that in
            // place this brute-force harness no longer crashes either — the drain closes
            // the panel. Kept as a headless regression probe. checkedDS is logged only to
            // show CheckPtr's (in)effectiveness, not as a fix signal.
            fprintf(stderr, "[PinPanel] +%2d frames: removalCount=%d rawDS=%p checkedDS=%p entity=%p%s\n",
                   rel, cnt, rawDS, checkedDS, s_pin_entity,
                   (rawDS == nullptr) ? "  (datasource cleared — dialog closed)" : "");
        }
    }

    // Delayed re-scan: 90 frames after the initial fire, re-enumerate the open
    // dialogs and drive MapUI again. The UIDescription controller inserts the
    // Map.rsc:content sub-tree (imageMap/imageOverlay/frustum) on a LATER frame
    // than the open, so a same-frame scan misses it. Only for the map paths.
    static bool s_rescanned = false;
    if (s_fired && !s_rescanned && frameNum >= s_settleFrame + 90) {
        s_rescanned = true;
        if (std::strcmp(s_toolName, "utility") == 0 ||
            std::strcmp(s_toolName, "scan") == 0) {
            const char* tmpl = Opt().utilityTemplate;
            bool isProf = tmpl && std::strstr(tmpl, "Profession") != nullptr;
            fprintf(stderr, "[ToolHarness] DELAYED rescan at frame %d (looking for %s content)...\n",
                   frameNum, isProf ? "profession" : "minimap");
            // ProfessionUI's slot 5 already ran in AddDialog's open: not re-driven
            // (row 1200); the rescan only reports for it.
            if (!isProf) ToolbarUtility_HarnessDriveController("MapUI", 4);
        }
    }

    // upgrade: poll the construction-demo house until it is really built, then click
    // its upgrade button (see the UpgradeStage machine above).
    if (s_up_stage != UP_IDLE && s_up_stage != UP_DONE) UpgradeTick(frameNum);

    // bridge: poll the placed span until the builders finish it.
    if (s_br_stage == BR_DRAG) {
        if (s_br_dragUntil == 0) s_br_dragUntil = frameNum + BridgeDragFrames();
        else if (frameNum >= s_br_dragUntil) {
            fprintf(stderr, "[ToolHarness][bridge] drag preview held %d frames; committing.\n",
                   BridgeDragFrames());
            BridgeCommit(s_br_tool, s_br_dragStart, s_br_dragEnd);
        }
    }
    else if (s_br_stage == BR_WAIT_BUILT) BridgeTick(entityUI, frameNum);
    // After the post-completion re-select, hold the loop open ~300 frames so the crash
    // (if any) has frames to land in — it faults in Draw, not in the click.
    else if (s_br_stage == BR_SELECT_SETTLE && frameNum - s_br_lastPoll > 300) {
        fprintf(stderr, "[Bridge] survived %d frames of Draw after selecting the completed bridge.\n",
               frameNum - s_br_lastPoll);
        s_br_stage = BR_DONE;
    }

    // ---- "pasturefull": the two halves of the pasture feature in ONE run ----
    // FirePastureTool places a real fenced pasture through the tool-mode tree
    // (item 3 of PLAN §1.2); s_pf_panelFrame frames later FirePasturePanel opens
    // THAT pasture's info panel and drives the livestock combo (item 5). Running
    // them separately needs BANISHED_BUILD_DEMO_PASTURE for the second half, which
    // is the scaffolded master-only pasture, not a tool-placed one — so only this
    // chained mode proves the player-reachable path end to end.
    if (s_pf_panelFrame != 0 && frameNum >= s_pf_panelFrame) {
        s_pf_panelFrame = 0;
        fprintf(stderr, "[ToolHarness][pasturefull] opening the tool-placed pasture's "
                        "panel at frame %d\n", frameNum);
        FirePasturePanel(entityUI);
    }

    // pick: the placed building's and the bridge's pick meshes (PickTick above).
    PickTick(frameNum);

    // spritecapture: the scheduled capture click and the save check.
    SpriteCaptureTick(frameNum);

    // panelclose: the scheduled close click and its check (FirePanelClose above).
    if (s_pc_closeFrame != 0 || s_pc_checkFrame != 0) PanelCloseTick(entityUI, frameNum);

    if (s_fired) return;
    if (frameNum < s_settleFrame) return;

    s_fired = true;
    fprintf(stderr, "[ToolHarness] firing at frame %d (tool=\"%s\")\n", frameNum, s_toolName);
    if (std::strcmp(s_toolName, "click") == 0) {
        FireActivatePlacement(entityUI);
    } else if (std::strcmp(s_toolName, "preview") == 0) {
        FirePreviewPlacement(entityUI);
    } else if (std::strcmp(s_toolName, "road") == 0) {
        FireRoadTool(entityUI);
    } else if (std::strcmp(s_toolName, "bridge") == 0) {
        FireBridgeTool(entityUI);
    } else if (std::strcmp(s_toolName, "pasture") == 0) {
        FirePastureTool(entityUI);
    } else if (std::strcmp(s_toolName, "pasturefull") == 0) {
        FirePastureTool(entityUI);
        int delay = 240;
        if (const char* d = Opt().pastureFullDelay) {
            int v = std::atoi(d); if (v > 0) delay = v;
        }
        s_pf_panelFrame = frameNum + delay;
        fprintf(stderr, "[ToolHarness][pasturefull] tool fired; panel scheduled for frame %d\n",
               s_pf_panelFrame);
    } else if (std::strcmp(s_toolName, "placement") == 0) {
        FirePlacementTool(-1, -1);
    } else if (std::strcmp(s_toolName, "select") == 0) {
        FireSelectEntity(entityUI);
    } else if (std::strcmp(s_toolName, "selectcitizen") == 0) {
        FireSelectCitizen(entityUI);
    } else if (std::strcmp(s_toolName, "pick") == 0) {
        FirePick(entityUI);
        // Row 1261: a building placed through PlacementTool::CreateAt, then a bridge
        // through BridgeTool::CalcPath (BridgeCommit checks it).
        const PickSnapshot before = PickTakeSnapshot();
        // Next to the first founding house, so the builders reach the site.
        MapComponent* houseMap = StartTown_GetFoundingHouseCount() > 0
            ? StartTown_GetFoundingHouseEntity(0)->FindComponent<MapComponent>() : nullptr;
        if (houseMap)
            FirePlacementTool(houseMap->tileY0, houseMap->tileX0 + 8);
        else
            FirePlacementTool(-1, -1);
        PickArm(0, "pickplace", before);
        s_br_pickCheck = true;
        FireBridgeTool(entityUI);
    } else if (std::strcmp(s_toolName, "spritecapture") == 0) {
        FireSpriteCapture(entityUI, frameNum);
    } else if (std::strcmp(s_toolName, "panelclose") == 0) {
        FirePanelClose(entityUI, frameNum);
    } else if (std::strcmp(s_toolName, "utility") == 0) {
        FireUtilityDialog();
    } else if (std::strcmp(s_toolName, "markremoval") == 0) {
        s_mr_fireFrame = frameNum;
        FireMarkRemoval();
    } else if (std::strcmp(s_toolName, "upgrade") == 0) {
        FireUpgradeArm(entityUI, frameNum);
    } else if (std::strcmp(s_toolName, "pinpanel") == 0) {
        s_pin_fireFrame = frameNum;
        FirePinPanel(entityUI);
    } else if (std::strcmp(s_toolName, "uicompcensus") == 0) {
        FireUIComponentCensus();          // WAVE 10E lane VERIFY
    } else if (std::strcmp(s_toolName, "dlgsettings") == 0) {
        FireUIComponentCensus();          // the census first, so the subject is named
        FireDlgSettingsRoute(entityUI);   // WAVE 10E lane VERIFY
    } else if (std::strcmp(s_toolName, "housepanel") == 0) {
        FireHousePanel(entityUI);
    } else if (std::strcmp(s_toolName, "pasturepanel") == 0) {
        FirePasturePanel(entityUI);
    } else if (std::strcmp(s_toolName, "orchardpanel") == 0) {
        FireOrchardPanel(entityUI);
    } else if (std::strcmp(s_toolName, "tradepanel") == 0) {
        FireTradePanel(entityUI);
    } else if (std::strcmp(s_toolName, "clearfootprint") == 0) {
        s_mr_fireFrame = frameNum;
        FireClearFootprint();
    } else if (std::strcmp(s_toolName, "clearplace") == 0) {
        s_mr_fireFrame = frameNum;
        FireClearPlace();
    } else if (std::strcmp(s_toolName, "clearlive") == 0) {
        s_mr_fireFrame = frameNum;
        FireClearLive();
    } else if (std::strcmp(s_toolName, "clearharvest") == 0) {
        s_mr_fireFrame = frameNum;
        FireClearHarvest();
    } else if (std::strcmp(s_toolName, "clearplaceover") == 0) {
        s_mr_fireFrame = frameNum;
        FireClearPlaceOver();
    } else if (std::strcmp(s_toolName, "mapmenu") == 0) {
        MapParamsDialog_HarnessOpen();
    } else if (std::strcmp(s_toolName, "scan") == 0) {
        // Scan all live HUD dialogs (UI+0x28 list) for a MapUI controller /
        // imageMap element — locate where the visual minimap lives.
        ToolbarUtility_HarnessDriveController("MapUI", 4);
    } else {
        // Default / "removal": the original DragTool path (needs the live EntityUI).
        if (!entityUI) return;
        FireRemovalTool(entityUI);
    }
}

