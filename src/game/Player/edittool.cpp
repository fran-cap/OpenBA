// src/game/Player/edittool.cpp
// Clean reimplementation of EditTool vtable methods from Application-x64.exe.
//
// EditTool (0x38 bytes) extends Tool (0x28 bytes) directly.
// Handles entity selection and editing in-game.
//
// Vtable at 0x140161ca0 (13 slots). Four are reimplemented here:
//   Slot0   Dtor       (0x14002db00) — scalar deleting destructor
//   Slot6   OnKey      (0x1400fd030) — entity selection via ray picking
//   Slot7   Startup    (0x1400fcda0) — dialog positioning near entity
//   Slot10  Serialize  (0x1400fd290) — compare entity reference
//
// Decompiled from Application-x64.exe.

#include "Player/edittool_funcs.h"
#include "Player/edittool_anchor.h"   // review F-166: the citizen fallback's display test
#include "options.h"
#include "EditTool.h"
#include "Player/dragtool.h"   // Tool + DragTool (ENDGAME B.3.8 TOOL)
#include "Component/ModelComponent.h"
#include "Dialog/ToolUI.h"
#include "Dialog/EntityUI.h"
#include "Dialog/EntityUI.h"
#include "Dialog/ToolUI.h"
#include "math/Math.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Game/PlayerToolManager.h"   // Tool::dialogPtr (+0x80 / +0x90)
#include "volume/Volume.h"
#include "Player/exe_bodies.h"
#include "Dialog/exe_bodies.h"
#include "object/TypeInfo.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <new>
#include "object/Internal.h"   // ENDGAME B.3.6 UI callers (K4)
#include "ui/DialogInstance.h"   // ENDGAME B.3.6 UI callers (K4)
#include "ui/Element.h"        // ENDGAME B.3.6 UI callers (K4)
#include "ui/UserInterface.h"  // ENDGAME B.3.6 UI callers (K4)
#include "picking/Scene.h"     // ENDGAME B.3.7 callers (K2)
#include "input/Input.h"       // ENDGAME B.3.7 callers (K3)
#include "Game/inputbindings.h"   // GameKey (the KeyMap binding ids)
#include "video/Device.h"   // ENDGAME B.3.9 SS2: Video::Device::_singleton
#include "Entity/ComponentEntity.h"
#include "Game/WorldGeometry.h"
#include "ext/registry.h"                 // EXT SEAM (NOT RETAIL, docs/extensions/handins/EYECAM.md): Ext::Ext()
#include "ext/eye_camera/eye_camera.h"   // EXT SEAM (NOT RETAIL, docs/extensions/handins/EYECAM.md): ext.eye_camera's followed entity
#include "Game/Map.h"
#include "Game/PlayerController.h"   // PlayerController::WorldToScreen (0x140098210)

// BANISHED_SELECT_DIAG=1 traces the live EditTool pick path (A/B-safe: off by default).
static int SelDiagOn() {
    static int s = -1;
    if (s < 0) { const char* e = Opt().selectDiag; s = (e && e[0] && e[0] != '0') ? 1 : 0; }
    return s;
}


// ============================================================
// EditTool Slot10 (vtable +0x50) — "is this the entity I have selected?"
// ============================================================
// Calls InternalBasePtr::CheckPtr on self->lastEntityRef (+0x30), then compares
// *entityRef with it. Returns true if the entity references match — i.e. whether a
// given entity is the one currently being edited/selected by this tool.
//
// NAMING (2026-07-31): this was `EditTool_Serialize_Impl`. It is NOT a serializer;
// the name came from an early guess at the slot's purpose and survived long enough to
// mislead a crash investigation — a frame reading "Serialize" in a citizen-death
// teardown looks like nonsense and delays the read of an otherwise clean stack.
//
// CONTRACT: `entityRef` is DEREFERENCED. Every caller must pass it. Retail's own
// caller (TradeWork_impl_3480, 0x140103480) seeds a local from *param_2 immediately
// before the call and passes its address; a one-arg call site in nomadcomponent.cpp
// left RDX holding stale garbage and faulted here on every citizen death.
//
// Original: 0x1400fd290
bool EditTool::IsSelected(ComponentEntity* const* ref) {
    // CheckPtr on self->lastEntityRef to resolve lazy/weak pointers

    // Compare: does *ref match self->lastEntityRef?
    return *ref == lastEntityRef;
}

// ============================================================
// Exe helpers for Startup and OnKey, called by name (Player/exe_bodies.h unless
// noted):
//   0x140100380 PlayerScreenRect_Entity_0380 -- entity screen bounding rectangle
//   0x140098210 PlayerScreenRect_WorldToScreen_098210 -- project a world Vector3
//               through the camera (citizen fallback, TryProjectEntityModelPos)
//   0x140066800 EntityUI::SetPinned -- check the pin box, sync the pin
//   0x1401005a0 PlayerRay_GetMouseRay_1005a0 -- camera ray from the mouse position
//   0x1400aa530 PlayerRay_TerrainMarch_aa530 -- map hit test (was a dropped-page
//               typo 0x14010a530 once; the CALL at 0x1400fd12a in EditTool_Draw)
//   0x1400a0280 PlayerEdit_EntityAtCell_a0280 -- entity at a world cell
//   0x1400fcb40 HighlightClear_Impl -- clear the selection highlight
//   0x1400fd3b0 EditTool_SelectEntity_Impl (Dialog/exe_bodies.h)
// Import slots: 0x1401528a8 Volume::Rectangle's ctor (RtVolume_Rectangle_Ctor),
// 0x140152a98 Object::Base::IsDerivedFrom (Object::IsDerivedFromNullSafe),
// 0x140152a90 Object::InternalBasePtr's ctor (placement new).
// The bodies' uint8_t returns are read as the bool the old door typedefs read.
//
// 0x1400fcc90 EditTool::CloseInfoDialog (DESELECT: clears the selection outline + closes
// the info dialog): called by name from Update's no-rect branch and Shutdown (slot 5).
// ============================================================

// ============================================================
// EditTool::Startup (vtable +0x38, Slot7) — Dialog positioning near entity
// ============================================================
// Gets dialog controller via IBP at self+0x28, casts with IsDerivedFrom
// to ToolUI and PageToggleUI types. If ToolUI controller's flag at +0x40
// is false, gets entity screen rect and positions dialog near it.
//
// Original: 0x1400fcda0
// Per-frame selection-outline follow re-sync (tool_base_render.cpp). Keeps a
// moving selected entity's yellow outline glued to it as it walks. Self-gates on
// BANISHED_SELECTION_OUTLINE, so it is a no-op on the shipping default path.
extern "C" void HighlightFollow_Impl(EditTool* self);

// Resolve the ModelComponent class type-id: prefer the raw .rdata DWORD; in the
// standalone exe that constant is null, so fall back to the live factory tree by
// name (the same reason 0x140003f60's registered reimpl finds nothing in SA).
static uint32_t ModelComponentTypeId() {
    return ModelComponent::TypeId();   // ENDGAME B.3.8 callers: the class's record
}

// Project a dynamic entity's (e.g. a citizen's) live world position to a screen point.
// Used as the reposition fallback for entities that Entity_impl_0380 can't bound (no
// static footprint / model box). Reads the origin (translation) of the entity's
// ModelComponent world Transform — ModelComponent+0x148 is a Math::Transform (0x1C),
// whose last 3 floats (+0x158/+0x15c/+0x160) are the world x/y/z — and projects it
// through the camera. Returns false (the caller then closes the panel, as retail does for
// an entity with no screen rect) if the component is missing or the point is behind the
// camera or off the display.
static bool TryProjectEntityModelPos(const ComponentEntity* entityObj, float* outX, float* outY) {
    if (!entityObj) return false;

    // Scan the entity's components (+0x30 array, +0x38 count) for its ModelComponent.
    // This mirrors 0x140003f60 with the type id taken from the factory tree.
    const uint32_t modelTid = ModelComponentTypeId();
    if (!modelTid) return false;

    const ModelComponent* modelComp = nullptr;
    int count = entityObj->_components._size;
    for (int i = 0; i < count; i++) {
        Component* comp = entityObj->_components[i].Get();
        if (!comp) continue;
        if (comp->Type() == modelTid) {
            if (Object::IsDerivedFromNullSafe(comp, modelTid)) modelComp = static_cast<const ModelComponent*>(comp);
            break;
        }
    }
    if (!modelComp) return false;

    // World position = ModelComponent transform translation (+0x148 + 0x10).
    const Math::Vector3 worldPos = modelComp->transform.translation;

    PlayerController* camHolder = GameUpdateLoop::_singleton->m_playerController;
    if (!camHolder) return false;

    float screen[2] = { 0.0f, 0.0f };
    if (!camHolder->WorldToScreen(&worldPos, screen)) return false;
    // NOT RETAIL (review F-166): a point off the display (or inf / NaN) is refused, as
    // 0x1400fe960 refuses an off-display box; Update would cast it to int out of range.
    // WorldToScreen returned 1, so the device is present.
    const ::Video::Device* disp = ::Video::Device::_singleton;
    if (!EditToolAnchor::OnDisplay(screen[0], screen[1], disp->backBufferWidth, disp->backBufferHeight))
        return false;
    *outX = screen[0];
    *outY = screen[1];
    return true;
}

// ============================================================
// EditTool close-info-dialog — port of Entity_impl_cc90 (0x1400fcc90)
// ============================================================
// Retail sequence: (1) Entity_impl_cb40 clears the selection highlight (it reads
// the entity from tool+0x30, so it runs BEFORE the refs drop), (2) tool+0x28
// (dialog IBP) and tool+0x30 (entity IBP) are nulled, (3) the dialog's ToolUI
// controller pin flag (+0x40) is read and, when unpinned, the dialog is closed
// through UI::UserInterface::CloseDialog — which releases the DialogInstance and
// runs every controller's dtor slot, so the panel objects are actually destroyed
// (no stale entity context survives).
//
// Callers, as retail's: EditTool::Update (0x1400fcda0) on the dead-entity branch and
// when the entity has no screen rect, and Shutdown (slot 5, the 0x1400fd2e0 thunk).
void EditTool::CloseInfoDialog() {
    // (1) clear selection highlight (0x1400fcb40, registered unconditionally by
    // RegisterPlayerRenderBatch).
    HighlightClear();

    // The open dialog, read once after the clear (0x1400fcc9e MOV RAX,[RBX+0x28]).
    UI::DialogInstance* dialog = entityRef;

    // (2) drop the tool's dialog + selected-entity refs so the tick early-outs and
    // the same footprint can be re-selected later.
    // Retail cc90 runs cb40 UNCONDITIONALLY (above), then gates everything below on
    // tool+0x28 being non-null. Returning here still clears the outline, which is
    // the whole point of the right-click path.
    if (!dialog) return;

    entityRef = nullptr;
    lastEntityRef = nullptr;

    // PIN CHECK. Retail cc90 reads the dialog's ToolUI controller and closes only
    // when +0x40 (pinned) is clear; a pinned panel survives deselect. The old direct
    // caller had already done this check, so it was omitted here -- but slot 5 (tool
    // teardown) has NOT, and without it a right-click would close pinned panels too.
    // Row 692 (B7 RES), 0x1400fccf8-0x1400fcd4c: GetController, then a null controller
    // narrows to 0 (0x1400fcd06) and a non-ToolUI one to 0 (CMOVNZ at 0x1400fcd2d); the
    // pin byte [narrowed+0x40] is read with no test.
    {
        const uint32_t tidToolUI = ToolUI::TypeId();
        Object::InternalPtr<UI::DialogController> ctrl = dialog->GetController(tidToolUI);
        ToolUI* toolUI = nullptr;
        if (ctrl.Get() != nullptr && ctrl->IsDerivedFrom(tidToolUI))
            toolUI = static_cast<ToolUI*>(ctrl.Get());
        if (toolUI->pinned != 0) return;   // pinned -> leave it open
    }

    // (3) close the dialog: 0x1400fcd52-0x1400fcd6f, CloseDialog on the loop's
    // UserInterface ([[0x1401c3dd8]+0x98]) with a stack InternalPtr of the dialog. Row 692:
    // no second dialog test, no address-range test, no UserInterface test.
    Object::InternalPtr<UI::DialogInstance> dialogRef(dialog);
    GameUpdateLoop::_singleton->m_userInterface->CloseDialog(&dialogRef);
    // Fires on entity-death AND on right-click deselect (vtable slot 5), so this
    // must not claim a cause it cannot know.
    printf("[EditTool] deselect -> closed info dialog %p\n", static_cast<void*>(dialog));
    fflush(stdout);
}

// One-shot bounded reposition tracer (BANISHED_SELECT_DIAG). Startup runs every
// frame, so cap output to a handful of lines.
static bool RepoDiag() {
    if (!SelDiagOn()) return false;
    static int budget = 8;
    if (budget <= 0) return false;
    --budget;
    return true;
}
#define REPO_DIAG(...) do { if (RepoDiag()) { printf(__VA_ARGS__); fflush(stdout); } } while (0)

void EditTool::Update(const System::Time*, char) {
    // Re-sync the selection outline to the (possibly moving) selected entity every
    // frame BEFORE the dialog-positioning early-outs below — the follow reads the
    // selected-entity ref at +0x30 and is independent of the info dialog at +0x28.
    HighlightFollow_Impl(this);

    // Early out: if entity IBP at +0x28 is null, nothing to do
    if (entityRef == nullptr)
        return;

    // Resolve lazy/weak pointer (CheckPtr, a bare RET). Retail 0x1400fcda0 tests +0x28
    // for null only (0x1400fcdac) and has no address-range test: the panel's close
    // button runs EntityUI::OnElementEvent's id-1 arm, SelectTool(0), which destroys
    // this tool, so +0x28 never outlives its dialog (review F-190).

    // Get ToolUI controller from dialog.
    UI::DialogInstance* dialog = entityRef;
    ToolUI* toolUI = nullptr;

    {
        const uint32_t tidToolUI = ToolUI::TypeId();
        Object::InternalPtr<UI::DialogController> controller = dialog->GetController(tidToolUI);
        if (controller.Get() != nullptr) {
            if (controller->IsDerivedFrom(tidToolUI)) {
                toolUI = static_cast<ToolUI*>(controller.Get());
            }
        }
    }

    // Get the EntityUI (PageToggleUI) controller from the same dialog
    EntityUI* pageToggle = nullptr;

    {
        const uint32_t tidEntityUI = EntityUI::TypeId();
        Object::InternalPtr<UI::DialogController> controller = dialog->GetController(tidEntityUI);
        if (controller.Get() != nullptr) {
            if (controller->IsDerivedFrom(tidEntityUI)) {
                pageToggle = static_cast<EntityUI*>(controller.Get());
            }
        }
    }

    REPO_DIAG("[Repos] entityRef(dialog)=%p toolUIIBP=%p pageToggleIBP=%p\n",
              static_cast<void*>(dialog), static_cast<void*>(toolUI), static_cast<void*>(pageToggle));

    // Check the ToolUI's pinned flag (+0x40). Retail reads it with no test of the
    // narrowed controller (0x1400fce7e MOV RAX,[RBP-0x30] / 0x1400fce82 CMP byte ptr
    // [RAX+0x40],0), and later reads the EntityUI's +0x30 the same way (0x1400fcea0 /
    // 0x1400fceac): every info dialog carries both controllers (review F-190).
    if (toolUI->pinned != 0) {
        // Flag is set — skip positioning
        REPO_DIAG("[Repos] BAIL: toolUI+0x40 pinned flag set\n");
        return;
    }

    // Retail close-on-entity-death (0x1400fcda0 -> Entity_impl_cc90): for an
    // UNPINNED info panel, when the entity can no longer produce a screen rect the
    // tool deselects and closes the dialog. In retail that fires because a dying
    // entity's model/footprint components disappear and Entity_impl_0380 fails. We
    // detect death directly — datasource weak-ref nulled or marked-for-removal
    // (entity+0x40 bit 0x400) via SA_DialogEntityAlive on the EntityUI controller —
    // BEFORE touching the entity's component array below. Without this, standalone
    // kept dead-entity panels open forever (the upgrade-path crash class the
    // SA_DialogEntityAlive wrapper guards papered over; those guards stay as the
    // backstop for PINNED panels, which retail also leaves open).
    if (!SA_DialogEntityAlive(pageToggle)) {
        CloseInfoDialog();
        return;
    }

    // Construct a Volume::Rectangle for entity bounding rect
    struct Rectangle rectangle;
    RtVolume_Rectangle_Ctor(&rectangle);

    // Get entity reference from the EntityUI controller (+0x30)
    ComponentEntity* entityObj = pageToggle->entity;

    // Get entity's screen bounding rectangle
    ComponentEntity* entityParam = entityObj;
    bool gotRect = PlayerScreenRect_EntityRect_0380(entityObj, &entityParam, &rectangle) != 0;

    REPO_DIAG("[Repos] entityObj=%p gotRect=%d rect(L,T,R,B)=[%.1f %.1f %.1f %.1f]\n",
              static_cast<void*>(entityObj), (int)gotRect,
              rectangle.min.x, rectangle.min.y, rectangle.max.x, rectangle.max.y);

    // Citizen / dynamic-entity fallback: 0380 only yields a rect for entities with a
    // static footprint or model bounding-box (buildings). Citizens have neither, so it
    // returns 0 and — without this — their info panel stays at the .rsc default corner.
    // Project the entity's ModelComponent world-transform origin to screen and synthesize
    // a zero-width rect centred on it, so the shared positioning math below drops the
    // panel just above the citizen. (Retail's 0380 also fails here; this is a standalone
    // QoL addition, guarded so it never runs when 0380 already produced a rect.)
    if (!gotRect) {
        float sx = 0.0f, sy = 0.0f;
        if (TryProjectEntityModelPos(entityObj, &sx, &sy)) {
            rectangle.min.x = sx;   // left
            rectangle.min.y = sy;   // top
            rectangle.max.x = sx;   // right (== left -> midX = sx, centred)
            gotRect = true;
            REPO_DIAG("[Repos] citizen fallback: projected worldpos -> screen(%.1f,%.1f)\n", sx, sy);
        }
    }

    // EXT SEAM (NOT RETAIL, docs/extensions/handins/EYECAM.md §11): while
    // ext.eye_camera is engaged on THIS panel's citizen, the citizen sits at or
    // behind the camera and projects to no rect, which retail reads as
    // "off the display" and closes the panel (and with it the FollowDialog).
    // The panel stays where it is; nothing else of the tick changes.
    if (!gotRect && Ext::Ext().eye_camera && Ext::EyeCamera::FollowedEntity() == entityObj)
        return;

    if (!gotRect) {
        // Retail 0x1400fcebc-0x1400fcec8: when 0x140100380 yields no rect (the entity is
        // off the display, or has neither a mesh box nor a footprint) it deselects and
        // closes the unpinned panel (0x1400fcc90) and returns. The citizen projection
        // above runs first (a standalone addition): it supplies the rect retail's box
        // path gives an on-screen citizen, so only an entity retail would also lose
        // reaches here.
        REPO_DIAG("[Repos] BAIL: no screen rect (0380 failed for this entity)\n");
        CloseInfoDialog();   // 0x1400fcc90
        return;
    }

    // Get dialog root element and its padded size
    Object::InternalPtr<UI::Element> root = dialog->GetRoot();

    int dialogWidth = 0, dialogHeight = 0;
    UI::Element* rootElement = root.Get();
    if (rootElement) {
        rootElement->GetPaddedSize(dialogWidth, dialogHeight);
    }

    // Calculate dialog position from entity rect.
    // The rect is a Volume::Rectangle (16 bytes) filled by fe960:
    //   +0x00 = minX (left), +0x04 = minY (top), +0x08 = maxX (right), +0x0c = maxY.
    // In the original disasm (EditTool::OnCreate @0x1400fcda0) the 16-byte rect spans two
    // stack slots: local_30 (bytes 0..7) and local_28 (bytes 8..15). It reads:
    //   local_res20     = local_30._0_4_  -> rect+0x00 (minX / left)
    //   local_res8._4_4_= local_30._4_4_  -> rect+0x04 (minY / top)
    //   (float)local_28 = rect+0x08       -> maxX / right   <-- NOT rect+0x10
    //   midX = (left + right) * 0.5
    // A prior port misread local_28 as rect+0x10 (past the 16-byte rect), which reads 0
    // and pulls every dialog toward the left edge. maxX lives at +0x08.
    float rectLeft  = rectangle.min.x;
    float rectTop   = rectangle.min.y;
    float rectRight = rectangle.max.x;

    float midX = (rectLeft + rectRight) * 0.5f;

    // Construct Vector2 for dialog position.
    // ??0Vector2@Math@@QEAA@AEBVScalar@1@0@Z  retail 0x180070db0 (was the
    // 0x1401522d8 dispatch slot)
    Math::Vector2 vec2{Math::Scalar(midX), Math::Scalar(rectTop)};

    // Position = (vec2.x - dialogWidth/2, vec2.y - dialogHeight) / uiScale
    int posX = (int)vec2.x - (dialogWidth >> 1);
    int posY = (int)vec2.y - dialogHeight;

    // Get UI scale factor from dialog's UI instance (dialog+0x10 = UI, UI+0xdc = scale).
    float uiScale = dialog->_ui->_uiScale;

    // Apply scale and set position. Retail 0x1400fcf8e-0x1400fcfcf goes from the two
    // DIVSS / CVTTSS2SI straight to SetUpperLeft(x, y, true): no clamp here; the edge
    // handling is SetUpperLeft's own (review F-191).
    int scaledX = (int)((float)posX / uiScale);
    int scaledY = (int)((float)posY / uiScale);

    REPO_DIAG("[Repos] SETPOS midX=%.1f rectTop=%.1f dlgW=%d dlgH=%d uiScale=%.3f "
              "-> scaled=(%d,%d)\n", midX, rectTop, dialogWidth,
              dialogHeight, uiScale, scaledX, scaledY);
    dialog->SetUpperLeft(scaledX, scaledY, true);

    // If game world flag bit 10 is set and pageToggle controller exists, call helper
    uint32_t worldFlags = GameUpdateLoop::_singleton->m_gameFlags;
    if (((worldFlags >> 10) & 1) != 0 && pageToggle != nullptr) {
        pageToggle->SetPinned();   // 0x140066800
    }
}

// ============================================================
// EditTool::OnKey (vtable +0x30, Slot6) — Entity selection via ray picking
// ============================================================
// If param2 is true and tool is in a valid state, reads KeyMap,
// computes camera ray, does map hit test + picking scene intersection
// to find entities under cursor. Calls fn_EditTool_SelectEntity to
// select/deselect the found entity.
//
// Original: 0x1400fd030
char EditTool::OnKey(char param2) {
    if (SelDiagOn()) {
        static int calls = 0;
        if (calls < 3) { calls++;
            printf("[SelDiag] OnKey#%d param2=%d CamRay=%p "
                   "inputConfig=%p\n", calls, param2,
                   (void*)&PlayerRay_GetMouseRay_1005a0,
                   (void*)Input::Configuration::_singleton);
            fflush(stdout);
        }
    }

    if (param2 == 0) return 1;

    // Check the tool manager's state (Tool +0x10)
    PlayerToolManager* manager = dialogPtr;
    bool flag80 = manager->tutorialRestricted != 0;
    if (flag80 && manager->tutorialSelectTarget == nullptr) {
        return 1;
    }

    // The KeyMap: Input::Configuration::_singleton->_keyMap (retail's triple
    // deref of the import slot 0x140151738).
    if (!Input::Configuration::_singleton) return 1;
    const Input::KeyMap* keyMap = Input::Configuration::_singleton->_keyMap;

    // Check KeyMap::Pressed(keyMap, 0, 5)
    bool pressed = keyMap->Pressed(0, ToUnderlying(GameKey::MouseLeft));
    if (SelDiagOn() && pressed)
        printf("[SelDiag] action-5 PRESSED (keyMap=%p) — running pick\n", static_cast<const void*>(keyMap));
    if (!pressed) return 1;

    // Get world data pointers
    Map* entityList = GameUpdateLoop::_singleton->m_game;   // +0xf8 (the tree called it the entity list)

    // Compute camera ray from mouse position
    // rayOrigin = 12 bytes (Vector3), rayDir = 12 bytes (Vector3)
    float rayOrigin[3] = {0.0f, 0.0f, 0.0f};
    float rayDir[3] = {0.0f, 0.0f, 0.0f};
    // THE PICK RANGE CAP. Retail stamps this 400.0f BEFORE the map hit test
    // (0x1400fd0e5: MOV dword ptr [RBP + 0x6f],0x43c80000) and then hands the SAME stack
    // slot to Picking::Scene::Intersect as its R9 max-distance (0x1400fd1c6: LEA R9,
    // [RBP + 0x6f]) with NO re-initialization in between — the only writes in that window
    // are the hitPoint/hitNormal buffers (RBP+0x17/0x27) and foundEntity (RBP+0x7f).
    // So it is one in/out variable: the terrain hit shortens it, and the picking pass is
    // then capped at the terrain distance. That is what stops the ray selecting a mesh
    // BEHIND the ground.
    // This port had it backwards in both directions — it started at 0.0f and was then
    // reset to 400.0f just before the picking call, which threw the terrain distance away.
    float maxDist = 400.0f;   // 0x43c80000

    PlayerRay_GetMouseRay_1005a0(keyMap, rayOrigin, rayDir);

    // Map hit test to find world position under cursor
    // MUST be 4 floats, matching retail EditTool::Draw's `float local_58[4]` (0x1400fd030).
    // MapHitTest's callee (retail 0x1400aaa20) writes a full Vector3 through this out-param
    // on EVERY call, hit or miss — its tail is
    // `MOVSS [RCX],XMM1 / [RCX+4],XMM0 / [RCX+8],XMM2` at 0x1400aad2a-33. On a MISS t == 0,
    // so it writes the RAY ORIGIN, i.e. the camera position.
    // This was declared `float hitPos[2]` with the slack put in a SEPARATE `hitDistBuf[2]`
    // that nothing read — so the optimiser deleted it and the third float landed on
    // `foundEntity` (frame: 0x38 = hitPos[0..1], 0x40 = foundEntity). The camera Z then
    // travelled into EditTool_SelectEntity as an entity pointer and crashed at
    // edittool_select.cpp:189 (observed 0x4167CF1B = 14.4877, and 0x4167EB1A on 2026-07-26 —
    // the same camera, slightly different zoom, which is why the two values are within 0.006).
    // Reaches SelectEntity only when the terrain ray MISSES (so the EntityAtCell branch that
    // would overwrite the slot is skipped) and the picking pass also misses.
    float hitPos[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    ComponentEntity* foundEntity = nullptr;

    WorldGeometry* mapComponent = GameUpdateLoop::_singleton->m_worldGeometry;

    if (mapComponent) {
        float maxDistArr[2];
        maxDistArr[0] = 400.0f;  // 0x43c80000
        maxDistArr[1] = 0.0f;

        bool mapHit = mapComponent->TerrainMarch(rayOrigin, rayDir,
                                                   maxDistArr, hitPos, &maxDist) != 0;

        if (mapHit) {
            int gridSize = entityList->size;
            int halfGrid = gridSize >> 1;

            // Convert world position to grid cell
            int cellX = (int)(hitPos[0] + (float)halfGrid);
            int cellY = (int)(hitPos[1] + (float)halfGrid);

            // Clamp to grid bounds
            if (cellX < 0) cellX = 0;
            if (cellX >= gridSize) cellX = gridSize - 1;
            if (cellY < 0) cellY = 0;
            if (cellY >= gridSize) cellY = gridSize - 1;

            int cellIndex = gridSize * cellY + cellX;

            if (cellIndex > 0) {
                ComponentEntity* retIBP = nullptr;
                ComponentEntity** result = entityList->EntityAtCell(&retIBP, cellIndex);
                if (result) foundEntity = *result;
            }
            if (SelDiagOn())
                printf("[SelDiag] mapHit=1 hitPos=(%.2f,%.2f) cell=(%d,%d) cellEntity=%p\n",
                       hitPos[0], hitPos[1], cellX, cellY, static_cast<void*>(foundEntity));
        } else if (SelDiagOn()) {
            printf("[SelDiag] mapHit=0 (ray missed terrain) ray o=(%.2f,%.2f,%.2f) d=(%.2f,%.2f,%.2f)\n",
                   rayOrigin[0], rayOrigin[1], rayOrigin[2], rayDir[0], rayDir[1], rayDir[2]);
        }
    } else if (SelDiagOn()) {
        printf("[SelDiag] MapHitTest skipped (mapComp=%p)\n",
               (void*)mapComponent);
    }

    // Picking scene intersection (3D ray picking for precise entity selection)
    Picking::Scene* pickingScene = GameUpdateLoop::_singleton->m_pickingScene;
    Math::Vector3 hitNormal(0.0f, 0.0f, 0.0f);
    Math::Vector3 hitPoint(0.0f, 0.0f, 0.0f);

    if (pickingScene) {
        // NO reset here — retail deliberately carries the map hit test's shortened
        // distance into the pick (see the maxDist declaration). Re-stamping 400.0f is
        // what let the pick reach through terrain.
        int hitId = pickingScene->Intersect(
            Math::Vector3(rayOrigin[0], rayOrigin[1], rayOrigin[2]),
            Math::Vector3(rayDir[0], rayDir[1], rayDir[2]),
            Math::Scalar(maxDist), hitPoint, hitNormal);
        if (hitId >= 0) {
            // Get the entity associated with this picking hit.
            // Picking::Scene's user data is `void*` (retail's API); the picking
            // component stores the owning ComponentEntity there.
            foundEntity = reinterpret_cast<ComponentEntity*>(   // callback context: Picking::Scene::GetUserData returns retail's void* (?GetUserData@Scene@Picking@@QEBAPEAXI@Z); the picking component stores the owning ComponentEntity there  // CAST-OK: user_data
                pickingScene->GetUserData(static_cast<uint32_t>(hitId)));
        }
        if (SelDiagOn())
            printf("[SelDiag] Picking hitId=%d -> foundEntity=%p\n", hitId, static_cast<void*>(foundEntity));
    } else if (SelDiagOn()) {
        printf("[SelDiag] Picking skipped (scene=%p)\n", static_cast<void*>(pickingScene));
    }

    // Select the found entity if valid and different from current
    if (foundEntity != nullptr) {
        // If flag80 is set, verify entity matches expected type
        if (flag80) {
            // CORRECTED 2026-08-01. This used to blame "a mesh registered without
            // Picking::Scene::SetMeshUserData", citing foundEntity=0x4167eb1a from
            // 2026-07-26. That was WRONG and cost a second investigation: the float bit
            // pattern was the CAMERA POSITION, written past the end of the 8-byte `hitPos`
            // buffer by MapHitTest's 12-byte Vector3 store (see the hitPos declaration
            // above, now float[4] as retail has it). Picking was never at fault — the
            // exports on that path were re-verified against the Runtime DLL and all match.
            // Kept as defence in depth only. NOTE it did not fire on the 2026-08-01 repeat:
            // flag80 was 0, so this whole branch was skipped and the corrupt value went
            // straight through to SelectEntity.
            // LEFT: the tree's address probe (a guard retail lacks, 14d row 74).
            uintptr_t foundBits = reinterpret_cast<uintptr_t>(foundEntity);   // CAST-OK: ptr_int
            if (foundBits < 0x10000 || (foundBits & 0x7) != 0) {
                static int s_warned = 0;
                if (s_warned < 4) {
                    s_warned++;
                    fprintf(stderr, "[EditTool] WARN: picking user data %p is not a valid entity "
                            "pointer — skipping select. A mesh was registered without "
                            "Picking::Scene::SetMeshUserData.\n", static_cast<void*>(foundEntity));
                    fflush(stderr);
                }
                return 1;
            }
            // The entity's description (+0x28) against the manager's pending group (+0x90).
            if (foundEntity->_description != manager->tutorialSelectTarget) {
                return 1;  // type mismatch, skip
            }
        }

        // Check if entity is different from last selected
        if (lastEntityRef != foundEntity) {
            // EditTool::SelectEntity (0x1400fd3b0)
            Object::InternalPtr<ComponentEntity> entityParam(foundEntity);
            SelectEntity(&entityParam);
            if (SelDiagOn())
                printf("[SelDiag] SELECT entity=%p -> dialog(self+0x28)=%p\n",
                       static_cast<void*>(foundEntity), static_cast<void*>(entityRef));
        }
    } else if (SelDiagOn()) {
        printf("[SelDiag] no entity under cursor — nothing selected\n");
    }

    return 1;
}

// ============================================================
// EditTool members (EditTool.h) — ENDGAME B.3.8 TOOL.
// The hand-built g_EditTool_Vtable[13], its slot stubs, the _SA factories and
// RegisterEditToolVtable are gone: the compiler builds the table, and nothing
// reads 0x140161ca0 back by address (the old dtor was the only reader).
// Inherited from Tool, as in retail: [11] no setting (0x140004950 `*ret = 0`:
// EditTool has no ToolSetting; SetEditMode dereferences the returned slot).
// ============================================================
#include "Object/factory_registration.h"

// [5] 0x1400fd2e0 is a JMP thunk straight to 0x1400fcc90 (deselect + close the
// info dialog). SetEditMode's teardown (the only slot-5 caller) invokes it, and
// that is HOW retail clears the selection outline AND closes the panel when the
// player right-clicks. NOT the same as registering 0x1400fcc90 by address: that
// address also backs a per-frame deactivate slot on a DIFFERENT table
// (0x1401ee710), and registering it made cc90 run every frame
// (reg_player_render.cpp records the measurement). Retail reads the dialog IBP
// from +0x28 itself.
void EditTool::Shutdown() { CloseInfoDialog(); }

// [6] OnKey (0x1400fd030), [7] Update (0x1400fcda0) and [10] IsSelected
// (0x1400fd290: "is *ref the entity I have selected?" — callers MUST pass the
// second argument; a one-arg call site once left RDX stale and faulted inside it,
// nomadcomponent.cpp) are defined above.

// Cursor-free select harness (BANISHED_TOOL_HARNESS=select). The live pick needs the OS
// cursor over an entity, which the headless settle-frame lacks. This mirrors OnKey's
// cell-fallback: scan the region map around the start-town centre via EntityAtCell and
// drive fn_EditTool_SelectEntity on each occupant until one opens an info dialog
// (editTool+0x28 goes non-null). Entities without a UIDescription (trees/rocks) safely
// no-op, so the first building/citizen hit is what opens. Returns 1 if a dialog opened.
// Drive the select path on ONE specific entity (a definitive UIDescription-bearing building
// from the founding-house capture). Returns 1 if an info dialog opened (editTool+0x28 set).
// Is `tool` really an EditTool? EntityUI+0x28 holds whatever tool is CURRENTLY active
// — after any build/placement tool has been activated (SetEditMode with a template)
// that is a DragTool-derived object, and EditTool's fields alias its fields: EditTool
// +0x28/+0x30 (dialog IBP / selected-entity IBP) sit exactly on DragTool's dragState
// and endCell/lastEndCell int pair. Driving the select path on one of those reads the
// two cell ints as an entity pointer — measured 2026-08-19 with the pasture harness:
// FencedTool+0x30 = 0xffffffff00008263 (endCell 0x8263, lastEndCell -1), which
// Entity_impl_cb40 handed straight to the 0x140004740 component finder and faulted on
// entity+0x38. So the answer decides whether this pointer may be used as an EditTool.
extern "C" bool EditTool_IsEditTool(Tool* tool) {
    if (!tool) return false;
    const uint32_t tid = EditTool::TypeId();   // B38 D4: the class static, not a lazy cache
    if (tid == 0 || tid == 0xFFFFFFFFu) return false;
    return Object::IsDerivedFromNullSafe(tool, tid);
}

// Get an EditTool to drive a panel with, the way a player does: if the active tool is
// something else (a build tool the harness or the player left armed), switch back to
// edit mode first — SetEditMode(pc, 0) destroys that tool and installs an EditTool —
// rather than reinterpreting it. Returns null if no EditTool could be activated.
// B4 P2 T1: typed (the tool manager in, the EditTool out). extern "C" keeps the link name
// the harnesses' local declarations bind to (several still spell the two pointers void*).
extern "C" EditTool* EditTool_HarnessAcquire(PlayerToolManager* entityUI) {
    if (!entityUI) return nullptr;
    PlayerToolManager* manager = entityUI;
    Tool* tool = manager->currentTool.Get();
    if (EditTool_IsEditTool(tool)) return static_cast<EditTool*>(tool);   // IsDerivedFrom(EditTool) above
    if (tool) {
        static int warned = 0;
        if (warned < 4) {
            ++warned;
            printf("[EditSel] active tool %p is not an EditTool — switching to edit mode "
                   "(SetEditMode 0) before touching a panel\n", tool);
            fflush(stdout);
        }
    }
    entityUI->SelectTool(nullptr);
    tool = manager->currentTool.Get();
    return EditTool_IsEditTool(tool) ? static_cast<EditTool*>(tool) : nullptr;
}

extern "C" int EditTool_HarnessSelectEntity(EditTool* editTool, ComponentEntity* entity) {
    if (!editTool || !entity) return 0;
    // Refuse a non-EditTool outright: d3b0 would write the dialog + entity refs into
    // whatever object this is, and Entity_impl_cb40 would deref its +0x30 as an entity.
    if (!EditTool_IsEditTool(editTool)) {
        static int warned = 0;
        if (warned < 4) {
            ++warned;
            printf("[EditSel] REFUSED: %p is not an EditTool (its +0x30 is not a selected "
                   "entity) — call EditTool_HarnessAcquire first\n", editTool);
            fflush(stdout);
        }
        return 0;
    }
    EditTool* tool = editTool;
    Object::InternalPtr<ComponentEntity> entityParam(entity);
    tool->SelectEntity(&entityParam);   // 0x1400fd3b0
    void* dlg = tool->entityRef;
    printf("[EditSel] direct select entity=%p -> dialog=%p (%s)\n",
           entity, dlg, dlg ? "OPENED" : "no dialog");
    return dlg != 0 ? 1 : 0;
}

// Drive the select path on a CITIZEN. Citizens aren't in the region-map cell array
// (they move) — they register in the Picking::Scene via PickingComponent OnAdded /
// the move-tick (see citizenai_executor.cpp RegisterCitizenPickMesh). Their pick
// userData is the entity backref we can recover with Picking::Scene::GetUserData.
// This bypasses the cursor ray (headless harness) but exercises the exact chain a
// real click uses: pick userData -> EditTool_SelectEntity -> CitizenUI dialog.
extern "C" int EditTool_HarnessSelectCitizen(EditTool* editTool) {
    if (!editTool) { printf("[EditSel] no active EditTool\n"); return 0; }
    Picking::Scene* scene = GameUpdateLoop::_singleton->m_pickingScene;
    if (!scene) { printf("[EditSel] picking scene null\n"); return 0; }
    EditTool* tool = editTool;

    // Citizens register with low pick-ids (0..N). Probe the first several; the first
    // that yields a live userData entity gets selected.
    for (int id = 0; id < 16; id++) {
        void* userData = scene->GetUserData(static_cast<uint32_t>(id));
        if (!userData) continue;
        printf("[EditSel] citizen pick-id %d -> entity=%p; selecting...\n", id, userData);
        // Picking::Scene's user data is `void*` (retail's API).
        Object::InternalPtr<ComponentEntity> entityParam(reinterpret_cast<ComponentEntity*>(userData));   // callback context: Picking::Scene::GetUserData returns retail's void* (?GetUserData@Scene@Picking@@QEBAPEAXI@Z); the picking component stores the owning ComponentEntity there  // CAST-OK: user_data
        tool->SelectEntity(&entityParam);   // 0x1400fd3b0
        void* dlg = tool->entityRef;
        printf("[EditSel] citizen select entity=%p -> dialog=%p (%s)\n",
               userData, dlg, dlg ? "OPENED" : "no dialog");
        if (dlg) {
            return 1;
        }
    }
    printf("[EditSel] no citizen pick userData found in ids 0..15\n");
    return 0;
}

extern "C" int EditTool_HarnessSelectNearby(EditTool* editTool) {
    if (!editTool) { printf("[EditSel] no active EditTool\n"); return 0; }

    Map* entityList = GameUpdateLoop::_singleton->m_game;   // +0xf8
    int gridSize = (entityList && entityList->size > 0) ? entityList->size : 256;
    EditTool* tool = editTool;
    int tried = 0, found = 0;

    // Expanding Chebyshev rings around centre (124,124), same valley the placement/
    // removal harnesses target.
    for (int ring = 0; ring <= 16; ring++) {
        for (int dy = -ring; dy <= ring; dy++) {
            for (int dx = -ring; dx <= ring; dx++) {
                if (ring > 0 && dx > -ring && dx < ring && dy > -ring && dy < ring)
                    continue;  // ring perimeter only
                int cx = 124 + dx, cy = 124 + dy;
                if (cx < 0 || cx >= gridSize || cy < 0 || cy >= gridSize) continue;
                int cellIndex = gridSize * cy + cx;
                if (cellIndex <= 0) continue;

                ComponentEntity* retIBP = nullptr;
                ComponentEntity** result = entityList->EntityAtCell(&retIBP, cellIndex);
                ComponentEntity* entity = result ? *result : nullptr;
                if (!entity) continue;
                found++;

                Object::InternalPtr<ComponentEntity> entityParam(entity);
                tool->SelectEntity(&entityParam);   // 0x1400fd3b0
                tried++;
                void* dlg = tool->entityRef;
                if (dlg != 0) {
                    printf("[EditSel] cell(%d,%d) entity=%p OPENED info dialog=%p "
                           "(tried=%d occupants=%d)\n", cx, cy, static_cast<void*>(entity), dlg, tried, found);
                    return 1;
                }
            }
        }
    }
    printf("[EditSel] no dialog opened after scanning rings (occupants=%d tried=%d) — "
           "entities may lack a UIDescription\n", found, tried);
    return 0;
}
// create 0x14002daa0: MemAlloc(0x38, 8), then the ctor 0x1400fd340.
::Object::Base* EditTool::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(EditTool), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) EditTool());
}
// createInPlace 0x14002dae0: `if (storage) ctor(storage)`.
::Object::Base* EditTool::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) EditTool());
}

DEFINE_EXE_TYPE(EditTool, "Tool")
