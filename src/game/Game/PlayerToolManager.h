#pragma once
// ============================================================================
// src/game/Game/PlayerToolManager.h — ENDGAME B.4 W0-H2
//
// The 0xB0-byte player tool manager at GameUpdateLoop+0x120 (m_toolManager;
// the tree called the member m_entityUI, but an EntityUI is a 0x48-byte
// UI::DialogController). Load builds it with MemAlloc(0xb0, 4) + ctor
// 0x1401025c0; Update calls 0x1401026a0 on it every frame.
//
// CLASS NAME NOT SETTLED (inventory D's name; the profile exe was not open).
// No vtable: a plain struct built in raw memory.
//
// FIELDS (retail ctor 0x1401025c0; 0x1401026a0):
//   +0x28 currentTool  InternalPtr<Tool>: 0x1401026a0 installs one when it is
//                      null (0x140102b60(this, 0)), tests it with IsA(EditTool)
//                      and calls its slots 7 (+0x38) and 12 (+0x60) each frame
//   +0x80 / +0x88      a pending-tool flag and the tool 0x1401026a0 hands to
//                      0x140102b60 when the flag is set
//   +0x70              a Collection::Array sized to 5 by the ctor
//                      (0x14002e000): data +0x70, count +0x78, capacity +0x7C
//   +0x28 is an InternalPtr<Tool> member (its ctor is retail's null store);
//   +0x00 is still declared as a plain pointer. Every other field is a
//   typed guess from the ctor's store; the tree reads +0x30 as the paused
//   flag and +0x34 as the game speed (ctor 1) — not yet confirmed in retail.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "math/Math.h"   // Math::Vector3 (the saved camera view's position)
#include "collection/Collection.h"   // Collection::Array (the camera views, +0x70)
#include "Action/PathAvl.h"           // AvlTreeHead (the two sets, +0x50 / +0x60)
#include "object/Internal.h"          // Object::InternalPtr (the current tool, +0x28)
#include "system/Value.h"             // System::Time (Update takes it by value)

class ComponentEntity;
class ComponentDescription;
namespace IO { class Stream; }
namespace UI { class DialogInstance; }
struct Box;                   // volume/Volume.h (global, Volume::Box)
namespace System { class String; }

// B.4 GAME-a: one saved camera view of the +0x70 array (stride 0x1C, align 4;
// resize 0x14002e000, Serialize 0x140102920: Vector3, three floats, a bool).
// Only ever an element of that raw-allocated array (never constructed by C++).
struct PlayerCameraView {
    Math::Vector3 position;       // +0x00 (Vector3::Serialize)
    float         yaw;     // +0x0C (Serialize float)
    float         pitch;     // +0x10 (Serialize float)
    float         distance;     // +0x14 (Serialize float)
    bool          used;           // +0x18 (Serialize bool)
    uint8_t       _pad19[3];      // +0x19 never written by the copy
};
static_assert(sizeof(PlayerCameraView) == 0x1C, "PlayerCameraView stride 0x1C (0x14002e000)");
static_assert(offsetof(PlayerCameraView, position) == 0x00, "PlayerCameraView::position @ +0x00");
static_assert(offsetof(PlayerCameraView, yaw) == 0x0C, "PlayerCameraView +0x0C");
static_assert(offsetof(PlayerCameraView, pitch) == 0x10, "PlayerCameraView +0x10");
static_assert(offsetof(PlayerCameraView, distance) == 0x14, "PlayerCameraView +0x14");
static_assert(offsetof(PlayerCameraView, used) == 0x18, "PlayerCameraView::used @ +0x18");
static_assert(offsetof(PlayerCameraView, _pad19) == 0x19, "PlayerCameraView +0x19");

// B.4 GAME-a: one node of the two entity sets at +0x50 / +0x60 (moved here
// from entityui.cpp, whose walkers read it): an AVL tree of 0x38-byte nodes
// keyed by the entity, built by 0x140102e30 (MemAlloc(0x38)), walked and
// removed by 0x14002c690 / 0x140025740.
struct TrackedEntityNode {
    Object::InternalPtr<ComponentEntity> key;   // +0x00 the entity (IBP-serialized by 0x14002e470)
    int32_t            x;         // +0x08 (Serialize int, 0x14002e470)
    int32_t            y;         // +0x0C (Serialize int)
    uint8_t            active;    // +0x10 (the insert stores the dword 1)
    uint8_t            _pad11[7]; // +0x11
    TrackedEntityNode* parent;    // +0x18
    TrackedEntityNode* left;      // +0x20
    TrackedEntityNode* right;     // +0x28
    int16_t            balance;   // +0x30
    int16_t            height;    // +0x32
    uint8_t            _pad34[4]; // +0x34
};
static_assert(sizeof(TrackedEntityNode) == 0x38, "0x140102e30 MemAlloc(0x38)");
static_assert(offsetof(TrackedEntityNode, _pad11) == 0x11, "TrackedEntityNode::_pad11");
static_assert(offsetof(TrackedEntityNode, _pad34) == 0x34, "TrackedEntityNode::_pad34");
static_assert(offsetof(TrackedEntityNode, key) == 0x00, "TrackedEntityNode::key @ +0x00");
static_assert(offsetof(TrackedEntityNode, x) == 0x08, "TrackedEntityNode::x @ +0x08");
static_assert(offsetof(TrackedEntityNode, y) == 0x0C, "TrackedEntityNode::y @ +0x0C");
static_assert(offsetof(TrackedEntityNode, active) == 0x10, "+0x10");
static_assert(offsetof(TrackedEntityNode, parent) == 0x18, "+0x18");
static_assert(offsetof(TrackedEntityNode, left) == 0x20, "+0x20");
static_assert(offsetof(TrackedEntityNode, right) == 0x28, "+0x28");
static_assert(offsetof(TrackedEntityNode, balance) == 0x30, "+0x30");
static_assert(offsetof(TrackedEntityNode, height) == 0x32, "TrackedEntityNode::height @ +0x32");

// B.4 Y GAMEB: the +0x50 set's node. The same 0x38 shape as TrackedEntityNode,
// but its key is a dialog's description, not an entity: 0x1401001f0 (the
// tutorial record), ToggleUtilityDialog 0x140101a80 (its insert through
// 0x140014890) and the save 0x14002e2e0 (the key serialized as an ExternalPtr).
// The 0x18-byte element is the node's base: the shared insert 0x140014890
// copies it whole into the new node.
struct PinnedToolEntry {
    ComponentDescription* key;    // +0x00 compared by address (unsigned); ToggleUtilityDialog 0x140101a80 takes it
    int32_t            x;         // +0x08 the dialog's saved upper-left x
    int32_t            y;         // +0x0C ... and y
    uint8_t            active;    // +0x10 the "open it" byte
    uint8_t            _pad11[7]; // +0x11 copied with the record
};
static_assert(sizeof(PinnedToolEntry) == 0x18, "the 0x18-byte element 0x140014890 copies");
static_assert(offsetof(PinnedToolEntry, key) == 0x0, "PinnedToolEntry::key");
static_assert(offsetof(PinnedToolEntry, x) == 0x8, "PinnedToolEntry::x");
static_assert(offsetof(PinnedToolEntry, y) == 0xC, "PinnedToolEntry::y");
static_assert(offsetof(PinnedToolEntry, active) == 0x10, "PinnedToolEntry::active");
static_assert(offsetof(PinnedToolEntry, _pad11) == 0x11, "PinnedToolEntry::_pad11");
struct PinnedToolNode : PinnedToolEntry {
    PinnedToolNode*    parent;    // +0x18
    PinnedToolNode*    left;      // +0x20
    PinnedToolNode*    right;     // +0x28
    int16_t            balance;   // +0x30
    int16_t            height;    // +0x32
    uint8_t            _pad34[4]; // +0x34
};

// The key the shared set cores compare (0x14002c690: the node's first qword,
// unsigned): one accessor per node type, so the one template core reads each.
inline ComponentEntity* Avl38KeyOf(const TrackedEntityNode* node) { return node->key.Get(); }
inline ComponentDescription* Avl38KeyOf(const PinnedToolNode* node) { return node->key; }

static_assert(sizeof(PinnedToolNode) == 0x38, "0x140014890 MemAlloc(0x38)");
static_assert(offsetof(PinnedToolNode, _pad34) == 0x34, "PinnedToolNode::_pad34");
static_assert(offsetof(PinnedToolNode, key) == 0x00, "PinnedToolNode::key @ +0x00");
static_assert(offsetof(PinnedToolNode, x) == 0x08, "PinnedToolNode::x @ +0x08");
static_assert(offsetof(PinnedToolNode, y) == 0x0C, "PinnedToolNode::y @ +0x0C");
static_assert(offsetof(PinnedToolNode, active) == 0x10, "PinnedToolNode::active @ +0x10");
static_assert(offsetof(PinnedToolNode, parent) == 0x18, "PinnedToolNode::parent @ +0x18");
static_assert(offsetof(PinnedToolNode, left) == 0x20, "PinnedToolNode::left @ +0x20");
static_assert(offsetof(PinnedToolNode, right) == 0x28, "PinnedToolNode::right @ +0x28");
static_assert(offsetof(PinnedToolNode, balance) == 0x30, "PinnedToolNode::balance @ +0x30");
static_assert(offsetof(PinnedToolNode, height) == 0x32, "PinnedToolNode::height @ +0x32");

class Tool;
class ComponentEntity;

// The +0x70 camera views grow through 0x14002e000: stride 0x1c, align 4; the
// copy writes +0x00..+0x18 (six dwords and the byte), the new tail's init three
// zero qwords and a zero byte (+0x00..+0x18): +0x19..+0x1b stay unwritten in
// both. No element destroy. The row is defined in gameupdateloop_load.cpp.
namespace PlayerToolManagerRows { extern const rtcoll::ArraySpec kSpec_CamView; }   // 0x14002e000
using PlayerCameraViewArray = Collection::Array<PlayerCameraView, Collection::DefaultAllocator,
                                                Collection::ExplicitSpec<&PlayerToolManagerRows::kSpec_CamView>>;
static_assert(sizeof(PlayerCameraViewArray) == 0x10, "{data, count, capacity}");

class PlayerToolManager {
public:
    // 0x1401025c0 — the ctor Load runs in the MemAlloc(0xb0, 4) block (body in
    // gameupdateloop_load.cpp). No destructor runs in C++: Shutdown calls
    // 0x1401024d0's port and MemFrees the block.
    PlayerToolManager();

    UI::DialogInstance* toolDialog;   // +0x00 InternalPtr<UI::DialogInstance>: the open tool dialog (0x1400ff9e0 stores it; ctor null)
    float    createBarScreenX;         // +0x08 not written by the ctor; the CreateBar dialog's smoothed screen x (0x1400ffc60: MOVSS [RCX+0x8] at 0x14010004d, [RCX+0xc] at 0x140100097; it blends them; the open path stores -1.0f)
    float    createBarScreenY;         // +0x0C not written by the ctor; the smoothed screen y (the same; two dword stores of 0xbf800000)
    // +0x10: the tool registry, a Collection::Set header {root, int count}
    // (root +0x10, count +0x18; ctor 0, two stores) that SelectTool 0x140102b60
    // walks and 0x14002de90 clears by its header (node: Dialog/ToolRegistryNode.h).
    AvlTreeHead<struct ToolRegistryNode> toolRegistry;
    ComponentDescription* activeToolGroup; // +0x20 ctor 0; the active tool group SelectTool 0x140102b60 stores (B4 H4: typed, DLG-d)
    // +0x28: retail's ctor 0x1401025c0 builds it with ??0InternalBasePtr(null)
    // (0x1401025fe); the teardown 0x1401024d0 DeletePtr()s it. Trivially
    // destructible, so the class still runs no destructor.
    Object::InternalPtr<Tool> currentTool;
    bool     paused;             // +0x30 ctor 0; the toolbar pause / unpause buttons write it (0x1400b92a0 cases 7 / 8), their enable state (0x1400b8d40) and Draw read it (B4 DLG-d)
    uint8_t  _pad31[3];          // +0x31
    int32_t  gameSpeed;         // +0x34 ctor 1 (the tree: game speed)
    bool     followActive;         // +0x38 ctor 0
    uint8_t  _pad39[3];          // +0x39
    float    zoomTarget;         // +0x3C ctor 0 (one qword store with +0x40); Serialize float (0x140102920)
    float    pitchTarget;         // +0x40 ctor 0; Serialize float
    float    manualPitch;         // +0x44 ctor 0.75
    int64_t  tickSoundCooldown;         // +0x48 ctor 0; a Time the camera update (0x1400feec0) counts down to 0
    // +0x50 / +0x60: two Collection::Set headers {root, int count} (POD: the ctor
    // zeroes them, 0x1401024d0 frees their nodes by hand; no C++ dtor runs).
    AvlTreeHead<PinnedToolNode> pinnedToolDialogs;      // +0x50 ctor 0 (root +0x50, count +0x58); the pinned tool-dialog set (key = the dialog's description; 0x140101a80 inserts)
    AvlTreeHead<TrackedEntityNode> trackedEntities;   // +0x60 ctor 0 (root +0x60, count +0x68); the open-entity set (EntityUI::SyncPin 0x1400666b0 inserts / removes, EntityUI::OnClosed removes)
    // +0x70 Array<CameraView> (sized to 5 by the ctor; saved views 1-5). Retail's
    // ctor 0x1401025c0 zeroes it inline and SetSizes it to 5; its dtor 0x1401024d0
    // frees it (inlined ~Array) after the current tool and before the two sets.
    PlayerCameraViewArray cameraViews;
    bool     tutorialRestricted;         // +0x80 pending-tool flag (ctor 0)
    uint8_t  _pad81[7];          // +0x81
    ComponentDescription* tutorialTool; // +0x88 pending tool (ctor 0): the group SelectTool 0x140102b60 forces while +0x80 is set (B4 H4: typed, DLG-d)
    void*    tutorialSelectTarget;         // +0x90 ctor 0
    int32_t  tutorialStartCell;         // +0x98 ctor -1 (one qword store with +0x9C)
    int32_t  tutorialEndCell;         // +0x9C ctor -1
    int32_t  tutorialRotation;         // +0xA0 ctor -1
    bool     cameraMoveActive;         // +0xA4 ctor 0
    uint8_t  _padA5[3];          // +0xA5
    int32_t  cameraMoveStartCell;         // +0xA8 not written by the ctor
    int32_t  cameraMoveEndCell;         // +0xAC not written by the ctor

    // ---- members (D.4 DRAW; bodies in Dialog/entityui.cpp; retail calls them EntityUI::) ----
    void Startup();                                       // 0x140101ee0 (GameUpdateLoop::Startup)
    int32_t Update(System::Time time, bool inputFlag);   // 0x1401026a0 (GameUpdateLoop::Update; returns the cursor)

    // ---- members (B4 GAME-a; bodies in gameupdateloop_serialize.cpp) ----
    void Serialize(IO::Stream& stream);   // 0x140102920

    // ---- members (B4 GAME-a; bodies in camera_input.cpp) ----
    // The camera halves of the manager's Update (0x1401026a0). inputFlag: 1 when
    // no dialog has the input; timeTicks: the frame's System::Time ticks.
    void UpdateCamera(int inputFlag, int64_t timeTicks);    // 0x1400feec0
    void UpdateHotkeys(bool inputFlag);                     // 0x1400ff6e0 (the tool's OnKey, presets, right-click cancel)
    void ApplyCameraView(int index);                        // 0x1400fe8d0 one saved view
    void OnEntityRemoved(ComponentEntity* const* entity);   // 0x140103480 (body: nomadcomponent.cpp)

    // ---- members (B4 DLG-d; bodies in Dialog/toolbardialog.cpp) ----
    // 0x140102b60: make `group`'s tool the current tool (null / 0 = the EditTool).
    void SelectTool(ComponentDescription* group);
    // 0x140101a80: close the open utility dialog of `description`, or open one;
    // returns the opened dialog (retail: an InternalPtr through the hidden slot).
    UI::DialogInstance* ToggleUtilityDialog(ComponentDescription* description);
    // 0x140101ce0: open the in-game pause menu (retail returns nothing; the tree
    // returns the dialog for the unattended menu driver).
    UI::DialogInstance* OpenPauseMenu();
    // 0x140103040: move the camera onto the entity, mark the dialog's EntityUI
    // as following it, and select it with the EditTool.
    void CenterOnEntity(UI::DialogInstance* const* dialog, ComponentEntity* const* entity);
    // 0x140100270: the open dialog whose ToolUI shows `description`, or null
    // (retail: an InternalPtr through the hidden slot). Body in
    // Dialog/toolbar_utility_helpers.cpp.
    // Retail passes the manager in RCX and never reads it: static here.
    static UI::DialogInstance* FindUtilityDialog(const ComponentDescription* description);
    void TrackEntity(ComponentEntity* const* entityPtr, uint32_t x, uint32_t y);   // 0x140102e30 (Dialog/entityui.cpp)
    void Teardown();   // 0x1401024d0 (the teardown body; no C++ dtor runs: Shutdown MemFrees the block) (Game/gameupdateloop_teardown.cpp)
    void UpdateCreateBar(int width, int height, const System::String* cost, char valid, char hideSize, char single, int multiplier, int startCell, int endCell, const ::Box* box, const Math::Transform* transform);   // 0x1400ffc60 (Player/tool_base_createbar.cpp)
    void CloseToolDialog();   // 0x1400ff980 (Player/tool_base_drag.cpp)
    void PlayTickSound();   // 0x1400ffbf0 (Player/tool_base_drag.cpp)
    UI::DialogInstance** OpenToolDialog(UI::DialogInstance** ret, ComponentDescription* description);   // 0x1400ff9e0 (Player/tool_base_toolui.cpp)
    void TutorialTreeInsert(ComponentDescription* tutorialId, void*, uint32_t value);   // 0x1401001f0 (Tutorial/tutorial_wave.cpp)
};

static_assert(sizeof(PlayerToolManager) == 0xB0, "PlayerToolManager is 0xB0 (Load MemAlloc(0xb0, 4))");
static_assert(offsetof(PlayerToolManager, toolDialog) == 0x00, "+0x00");
static_assert(offsetof(PlayerToolManager, createBarScreenX) == 0x08, "+0x08");
static_assert(offsetof(PlayerToolManager, createBarScreenY) == 0x0C, "+0x0C");
static_assert(offsetof(PlayerToolManager, toolRegistry) == 0x10, "+0x10");
static_assert(offsetof(PlayerToolManager, toolRegistry.count) == 0x18, "+0x18");
static_assert(offsetof(PlayerToolManager, activeToolGroup) == 0x20, "+0x20");
static_assert(offsetof(PlayerToolManager, currentTool) == 0x28, "PlayerToolManager::currentTool @ +0x28 (0x1401026a0)");
static_assert(offsetof(PlayerToolManager, paused) == 0x30, "PlayerToolManager::paused @ +0x30 (0x1400b8d40)");
static_assert(offsetof(PlayerToolManager, _pad31) == 0x31, "+0x31");
static_assert(offsetof(PlayerToolManager, gameSpeed) == 0x34, "+0x34");
static_assert(offsetof(PlayerToolManager, followActive) == 0x38, "+0x38");
static_assert(offsetof(PlayerToolManager, _pad39) == 0x39, "+0x39");
static_assert(offsetof(PlayerToolManager, zoomTarget) == 0x3C, "+0x3C");
static_assert(offsetof(PlayerToolManager, pitchTarget) == 0x40, "+0x40");
static_assert(offsetof(PlayerToolManager, manualPitch) == 0x44, "+0x44");
static_assert(offsetof(PlayerToolManager, tickSoundCooldown) == 0x48, "+0x48");
static_assert(offsetof(PlayerToolManager, pinnedToolDialogs) == 0x50, "+0x50");
static_assert(offsetof(PlayerToolManager, pinnedToolDialogs.count) == 0x58, "+0x58 the pinned set's count");
static_assert(offsetof(PlayerToolManager, trackedEntities) == 0x60, "+0x60");
static_assert(offsetof(PlayerToolManager, trackedEntities.count) == 0x68, "+0x68 the open-entity set's count");
static_assert(offsetof(PlayerToolManager, cameraViews) == 0x70, "PlayerToolManager::cameraViews @ +0x70 (count +0x78, capacity +0x7C)");
static_assert(offsetof(PlayerToolManager, tutorialRestricted) == 0x80, "+0x80");
static_assert(offsetof(PlayerToolManager, _pad81) == 0x81, "+0x81");
static_assert(offsetof(PlayerToolManager, tutorialTool) == 0x88, "+0x88");
static_assert(offsetof(PlayerToolManager, tutorialSelectTarget) == 0x90, "+0x90");
static_assert(offsetof(PlayerToolManager, tutorialStartCell) == 0x98, "+0x98");
static_assert(offsetof(PlayerToolManager, tutorialEndCell) == 0x9C, "+0x9C");
static_assert(offsetof(PlayerToolManager, tutorialRotation) == 0xA0, "+0xA0");
static_assert(offsetof(PlayerToolManager, cameraMoveActive) == 0xA4, "+0xA4");
static_assert(offsetof(PlayerToolManager, _padA5) == 0xA5, "+0xA5");
static_assert(offsetof(PlayerToolManager, cameraMoveStartCell) == 0xA8, "+0xA8");
static_assert(offsetof(PlayerToolManager, cameraMoveEndCell) == 0xAC, "+0xAC");
