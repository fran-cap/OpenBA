#pragma once
// src/game/Player/exe_bodies.h -- declarations of the exe bodies DEFINED in
// src/game/Player/ (ENDGAME B.3R, block W0-P0c, B3R_RULES R5).
//
// Every body declared once, copied from its DEFINITION (linkage included),
// grouped by defining file; its retail address(es) are the trailing comment.
// The defining .cpp includes this header so the compiler checks each pair.
// Readers call these by name.

#include <cstdint>
#include "collection/Collection.h"   // Collection::Array<T> (the overlay record arrays)
#include "Player/Tool.h"             // ToolTileRecord / ToolSegmentRecord
#include "Action/PathAvl.h"           // AvlTreeHead (the AVL sets below)
#include "system/MemoryAllocator.h"   // Avl38FreeSubtree's MemFree
#include "Game/WorldGeometryElements.h"   // GeometryBatchVertex (DecalVertexKey's record)
#include <cstddef>
namespace Video { class Context; }

class PlayerToolManager;      // Game/PlayerToolManager.h
class ComponentDescription;   // Component/ComponentDescription.h
class ToolbarDescription;     // Component/ToolbarDescription.h
namespace UI { class DialogInstance; }
class WorldGeometry;          // Game/WorldGeometry.h
class HighlightComponent;     // Component/HighlightComponent.h
class Map;                    // Game/Map.h
class BridgeTool;             // Player/BridgeTool.h
class FencedTool;             // Player/FencedTool.h
class CreatePlacedDescription;   // Component/CreatePlacedDescription.h
class RadiusDescription;         // Component/RadiusDescription.h
namespace System { class String; }
namespace Math { class Vector3; class Transform; }
struct Box;                   // volume/Volume.h (global, Volume::Box)
struct Rectangle;             // volume/Volume.h (global, Volume::Rectangle)
class PlayerController;       // Game/PlayerController.h
class ComponentEntity;        // Entity/ComponentEntity.h

// ---- bridgetool_validate.cpp

// ---- keystone_a_removalcalcpath.cpp
class DestroyComponent;

// ---- tool_base_createbar.cpp
extern "C" uint32_t UpdateToolUI_ebb0(const void* /*never read*/, const ::Box* box, const Math::Transform* transform, Math::Vector3* outCorners);  // 0x1400febb0  // B8: free in retail -- RCX dead: 0x1400ffe72 loads RDX / R8 / R9 only

// ---- tool_base_drag.cpp
void DragTool_PlayPlaceSound_1000f0(const void* /*unused*/);  // 0x1401000f0  // B8: free in retail -- RCX dead: at 0x1400fc91f it is what the virtual call [RAX+0x68] before it left
void DragTool_PlayErrorSound_100140(const void* /*unused*/);  // 0x140100140  // B8: free in retail -- RCX dead: at 0x1400fc926 it is what the virtual call [RAX+0x68] before it left
void DragPlacementTool_ClearPathEntities_1018d0(const void* /*unused*/, int minCol, int minRow, int maxCol, int maxRow, const ComponentDescription* entityTemplate);  // 0x1401018d0  // B8: free in retail -- RCX dead: 0x1400fb9e0 loads EDX / R8D / R9D only

// ---- tool_base_edit.cpp
float PlayerEdit_AvgTerrainHeight_101730(void* , int p2, int p3, int p4, int p5);  // 0x140101730  // B8: free in retail -- RCX dead: 0x1400dc25d does not load it

// ---- tool_base_highlight.cpp
// The decal tessellation's vertex-dedup set (0x1400ace40): 0x38-byte nodes whose
// 0x18-byte element is the 0x14-byte vertex record plus its vertex index. The
// set orders on the record's first 0x14 bytes, compared per column i (0..3) as
// the short at +2i, the short at +8+2i, then the byte at +0x10+i.
struct DecalVertexKey {
    GeometryBatchVertex vertex;   // +0x00 compared per column: position (signed shorts), uv (signed shorts), normal (bytes)
    int32_t             index;    // +0x14 the vertex's index in the slot's vertex list (not compared)
};
static_assert(sizeof(DecalVertexKey) == 0x18, "DecalVertexKey is the 0x18-byte element (0x14001f850 copies six dwords)");
static_assert(offsetof(DecalVertexKey, vertex) == 0x00, "DecalVertexKey::vertex @ +0x00");
static_assert(offsetof(DecalVertexKey, index) == 0x14, "DecalVertexKey::index @ +0x14 (0x1400ace40 reads node+0x14)");
struct DecalVertexNode {
    DecalVertexKey   key;       // +0x00
    DecalVertexNode* parent;    // +0x18
    DecalVertexNode* left;      // +0x20
    DecalVertexNode* right;     // +0x28
    int16_t          balance;   // +0x30
    int16_t          height;    // +0x32
    uint8_t          _pad34[4]; // +0x34
};
static_assert(sizeof(DecalVertexNode) == 0x38, "0x14001f850 MemAlloc(0x38)");
static_assert(offsetof(DecalVertexNode, key) == 0x0, "DecalVertexNode::key");
static_assert(offsetof(DecalVertexNode, _pad34) == 0x34, "DecalVertexNode::_pad34");
static_assert(offsetof(DecalVertexNode, parent) == 0x18 && offsetof(DecalVertexNode, left) == 0x20, "DecalVertexNode links");
static_assert(offsetof(DecalVertexNode, right) == 0x28 && offsetof(DecalVertexNode, balance) == 0x30, "DecalVertexNode links");
static_assert(offsetof(DecalVertexNode, height) == 0x32, "DecalVertexNode::height @ +0x32");
DecalVertexNode** DecalVertexSetInsert(AvlTreeHead<DecalVertexNode>* set, DecalVertexNode** out, const DecalVertexKey* key);  // 0x14001f6b0  // B8: stays free -- already typed on its one node type (AvlTreeHead<DecalVertexNode>); nothing erased
DecalVertexNode* DecalVertexSetFind(const AvlTreeHead<DecalVertexNode>* set, const GeometryBatchVertex* key);  // 0x14001fc00 (0x1400ace40 passes the corner record itself)  // B8: stays free -- already typed on its one node type (AvlTreeHead<DecalVertexNode>); nothing erased

namespace Graphics { class MaterialInstance; }

// ---- tool_base_highlight_tess.cpp

// ---- tool_base_path.cpp
// 0x140104670 / 0x140104770 / 0x1401048a0 are RemovalTool::CanRemoveBuilding / CanRemoveRoad / CanRemoveField (RemovalTool.h).

// ---- tool_base_place.cpp
// 0x1400fd960 is PlacementTool::FlipVariant (PlacementTool.h).

// ---- tool_base_ray.cpp
extern "C" uint8_t PlayerRay_GetMouseRay_1005a0(const void* /*never read*/, float* outOrigin, float* outDir);  // 0x1401005a0  // B8: free in retail -- RCX dead: 0x1400fd0d9's caller does not load it (0x1401012a1 passes its own through)
extern "C" int PlayerRay_ScreenToCell_101270(const void* /*never read*/);  // 0x140101270  // B8: free in retail -- RCX dead: at 0x1400fc76c it is what the call [0x140151758] before it left
// 0x1400fda00 is PlacementTool::GetPlacement (PlacementTool.h).

// ---- tool_base_render.cpp
class EditTool;

// ---- tool_base_screenrect.cpp
extern "C" uint32_t PlayerScreenRect_BuildCorners_ec70(uint64_t /*never read*/, int param_2, int param_3, int param_4, int param_5, Math::Vector3* param_6);  // 0x1400fec70  // B8: free in retail -- RCX dead: 0x1400ffe56 loads RDX / R8 / R9 only
extern "C" uint8_t PlayerScreenRect_AccumulateRect_fe960(uint64_t /*never read*/, uint32_t param_2, const Math::Vector3* param_3, struct Rectangle* param_4);  // 0x1400fe960  // B8: free in retail -- RCX dead: at 0x1400ffeeb it is what the call [0x1401528a8] before it left
extern "C" uint8_t PlayerScreenRect_EntityRect_0380(const void* /*never read*/, ComponentEntity* const* param_2, struct Rectangle* param_3);  // 0x140100380  // B8: free in retail -- RCX unread: 0x1400fceac's RCX is the entity value it stores into the RDX slot

// ---- tool_base_tile.cpp
// 0x1400fd650 is PlacementTool::CreateAt (PlacementTool.h).

// ---- tool_base_tilerender.cpp
extern "C" void PlayerTileRender_DrawOverlay_08c0(const void* /*never read*/, Video::Context* ctx, const Collection::Array<ToolTileRecord>* param_3, const Collection::Array<ToolSegmentRecord>* param_4);  // 0x1401008c0  // B8: free in retail -- RCX dead: 0x1400fc024 does not load it
extern "C" void PlayerTileRender_BuildRecords_1380(const void* /*never read*/, Video::Context* ctx, int param_3, int param_4, uint32_t param_5, int param_6, uint32_t param_7, const CreatePlacedDescription* param_8, const System::String* param_9, float param_10, float param_11, float param_12, char param_13, char param_14, const System::String* param_15, int param_16, int param_17, char param_18, const ComponentDescription* param_19);  // 0x140101380  // B8: free in retail -- RCX dead: 0x1400fdde3 does not load it

// ---- tool_base_toolui.cpp

// ---- tool_base_trade.cpp
// B.4 Y GAMEB: the typed wrappers over the two set cores (one body each in
// retail); TradeWork's assignment set has its pair in Work/TradeWork.h.
struct TrackedEntityNode; struct PinnedToolNode; class ComponentEntity;   // Game/PlayerToolManager.h
void Avl38SetRemove(AvlTreeHead<TrackedEntityNode>* set, TrackedEntityNode* node);   // 0x140025740  // B8: stays free -- already typed (B.4 B13): a typed overload per node type over the one template core Avl38Remove<Node> (0x140025740) (tool_base_trade.cpp); nothing erased
void Avl38SetRemove(AvlTreeHead<PinnedToolNode>* set, PinnedToolNode* node);         // 0x140025740  // B8: stays free -- already typed (B.4 B13): a typed overload per node type over the one template core Avl38Remove<Node> (0x140025740) (tool_base_trade.cpp); nothing erased
void Avl38SetFindRemove(AvlTreeHead<TrackedEntityNode>* set, ComponentEntity* const* key);   // 0x14002c690  // B8: stays free -- already typed (B.4 B13): a typed overload per node type over the one template core Avl38FindRemove<Node> (0x14002c690) (tool_base_trade.cpp); nothing erased

// The two rotations of the 0x38-byte AVL nodes (an 0x18-byte element; parent
// +0x18, left +0x20, right +0x28, balance +0x30, height +0x32). Retail's one
// body per rotation serves every such set (TrackedEntityNode, the decal tile
// tree of tool_base_highlight.cpp, TradeWork's, MarketWork's): one template over
// the node (B.4 B13). A node names `parent`, `left`, `right`, `balance`, `height`.
// A child contributes `child->height + 1`, a missing child 0.
template <class Node>
inline int16_t Avl38ChildHeight(const Node* child) {
    return (child == nullptr) ? (int16_t)0 : (int16_t)(child->height + 1);
}

// Recompute one node's balance / height from its children.
template <class Node>
inline void Avl38Update(Node* c) {
    int16_t lh = Avl38ChildHeight(c->left);
    int16_t rh = Avl38ChildHeight(c->right);
    c->balance = (int16_t)(rh - lh);
    c->height = (lh < rh) ? rh : lh;
}

// 0x140015000 — single rotation, pivot = node's LEFT child, which takes node's
// place; node becomes the pivot's RIGHT child. Returns the pivot.
template <class Node>
Node* Avl38RotateRight(Node** root, Node* node) {
    if (node == *root) {
        *root = node->left;
    }
    Node* parent = node->parent;
    if (parent != nullptr) {
        if (parent->left == node) parent->left = node->left;
        else                      parent->right = node->left;
    }
    Node* pivot = node->left;
    pivot->parent = node->parent;
    Node* pivotRight = pivot->right;
    node->left = pivotRight;
    if (pivotRight != nullptr) pivotRight->parent = node;
    pivot->right = node;
    node->parent = pivot;

    // Recompute balance/height for pivot.left, pivot.right, then pivot.
    if (pivot->left != nullptr)  Avl38Update(pivot->left);
    if (pivot->right != nullptr) Avl38Update(pivot->right);
    Avl38Update(pivot);
    return pivot;
}

// 0x140014ed0 — single rotation, pivot = node's RIGHT child, which takes node's
// place; node becomes the pivot's LEFT child. Returns the pivot.
template <class Node>
Node* Avl38RotateLeft(Node** root, Node* node) {
    if (node == *root) {
        *root = node->right;
    }
    Node* parent = node->parent;
    if (parent != nullptr) {
        if (parent->left == node) parent->left = node->right;
        else                      parent->right = node->right;
    }
    Node* pivot = node->right;
    pivot->parent = node->parent;
    Node* pivotLeft = pivot->left;
    node->right = pivotLeft;
    if (pivotLeft != nullptr) pivotLeft->parent = node;
    pivot->left = node;
    node->parent = pivot;

    if (pivot->left != nullptr)  Avl38Update(pivot->left);
    if (pivot->right != nullptr) Avl38Update(pivot->right);
    Avl38Update(pivot);
    return pivot;
}

// 0x140025920 / 0x140014b40 — walk from *cursor to the root recomputing balance /
// height and rotating (two retail copies of one algorithm). *cursor ends null.
template <class Node>
void Avl38Retrace(Node** root, Node** cursor) {
    while (*cursor != nullptr) {
        Node* node = *cursor;
        Node* up = node->parent;  // saved parent (next iteration target)

        int16_t leftH  = Avl38ChildHeight(node->left);
        int16_t rightH = Avl38ChildHeight(node->right);
        int16_t maxH = (rightH < leftH) ? leftH : rightH;
        // balance = rightH - leftH ; height = max(rightH, leftH)
        node->balance = (int16_t)(rightH - leftH);
        node->height = maxH;

        node = *cursor;
        // Right-heavy (bal > 1). Disasm @1400259ad/b8: if right-child bal < 0,
        // CALL 0x140015000 on the right child first, then CALL 0x140014ed0 on node.
        if (node->balance > 1 && node->right != nullptr) {
            Node* rc = node->right;
            if (rc->balance <= 0 && rc->balance < 0) {  // JG/JNS skip => bal_rc < 0
                Avl38RotateRight(root, rc);
                node = *cursor;
            }
            Avl38RotateLeft(root, node);
        }
        node = *cursor;
        // Left-heavy (bal < -1). Disasm @1400259e1/ec: if left-child bal > 0,
        // CALL 0x140014ed0 on the left child first, then CALL 0x140015000 on node.
        if (node->balance < -1 && node->left != nullptr) {
            Node* lc = node->left;
            if (lc->balance >= 0 && lc->balance > 0) {  // JS/JLE skip => bal_lc > 0
                Avl38RotateLeft(root, lc);
                node = *cursor;
            }
            Avl38RotateRight(root, node);
        }
        *cursor = up;
    }
}

// 0x140014b00 — free a subtree: its left (+0x20), its right (+0x28), then itself.
// `context` (RCX) is only passed down. 1:1 port of FUN_140014b00; one body in retail
// serves every 0x38-node set (the callers' node types, B13).
template <class Node>
void Avl38FreeSubtree(const void* context, Node* node) {
    if (node != nullptr) {
        Avl38FreeSubtree(context, node->left);
        Avl38FreeSubtree(context, node->right);
        System::MemoryAllocator::MemFree(node);
    }
}

// The generic node the unconverted sets of other blocks hand to the C forwarders
// (WORK's TradeWork / MarketWork sets, GAME's / DLG's entity sets): only the
// links and the 8-byte key at +0x00 are read here.
struct Avl38Node {
    const void* key;            // +0x00 compared as an unsigned 64-bit value (0x14002c690)
    uint8_t     _element08[0x10];  // +0x08 the rest of the element (not read)
    Avl38Node*  parent;         // +0x18
    Avl38Node*  left;           // +0x20
    Avl38Node*  right;          // +0x28
    int16_t     balance;        // +0x30
    int16_t     height;         // +0x32
    uint8_t     _pad34[4];      // +0x34
};
static_assert(sizeof(Avl38Node) == 0x38, "Avl38Node is 0x38");
static_assert(offsetof(Avl38Node, key) == 0x0, "Avl38Node::key");
static_assert(offsetof(Avl38Node, _element08) == 0x8, "Avl38Node::_element08");
static_assert(offsetof(Avl38Node, _pad34) == 0x34, "Avl38Node::_pad34");
static_assert(offsetof(Avl38Node, parent) == 0x18 && offsetof(Avl38Node, left) == 0x20, "Avl38Node links");
static_assert(offsetof(Avl38Node, right) == 0x28 && offsetof(Avl38Node, balance) == 0x30, "Avl38Node links");
static_assert(offsetof(Avl38Node, height) == 0x32, "Avl38Node::height @ +0x32");
