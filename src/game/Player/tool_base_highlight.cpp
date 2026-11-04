// src/game/Player/tool_base_highlight.cpp
// Player wave 5 (TIER-2 geometry) — PlacementTool radius-highlight tessellation.
//
// Closure for HighlightAdd (0x140055430), called from PlacementTool::Serialize/Slot7
// (placementtool.cpp) to draw a building's radius overlay. Worker-thread-free and
// dialog-free (unlike RemovalTool CalcPath / OpenToolDialog). Registered bottom-up by
// RegisterPlayerHighlightBatch (standalone/reg_player_highlight.cpp).
//
//   0x140055430 FUN_140055430        HighlightAdd          -> 0x1400b0d80
//   0x1400b0d80 TradeWork_impl_0d80  tile-slot manager     -> 0x14001fa80(reg), 0x1400ace40
//   0x1400ace40 TradeWork_impl_ace40 radius tessellation   (separate file, agent draft)
//   0x14001f6b0 TradeWork_impl_1f6b0 AVL insert            -> 0x14001f850, 0x140014b40
//   0x14001fc00 TradeWork_impl_1fc00 AVL find              (leaf)
//   0x140014b40 TradeWork_impl_4b40  AVL retrace+rebalance -> 0x140015000(reg), 0x140014ed0(reg)
//   0x14001f850 TradeWork_impl_1f850 AVL node alloc        (leaf, static MemAlloc)
//   0x140014b00 TradeWork_helper_14b00 recursive BST free  (leaf, static MemFree)
//
// AVL node = 0x38 bytes, key = the 6-int (0x18-byte) tile descriptor at node+0x00
// (shorts @+0x00/+0x08, bytes @+0x10..+0x13): +0x18 parent / +0x20 left / +0x28 right
// / +0x30 short balance / +0x32 short height. Same node shape + rotation helpers as
// tool_base_trade.cpp (which exposes PlayerTrade_Rot_15000 = 0x140015000 pivot-left and
// PlayerTrade_Rot_14ed0 = 0x140014ed0 pivot-right, both already registered).
//
// Port is 1:1 with the Ghidra decompile + disasm-verified — do NOT "improve".

#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Game/exe_constants.h"   // kRdata_* (B3R Leftovers: the .rdata constants)
#include "system/MemoryAllocator.h"
#include "Action/exe_bodies.h"
#include <cstdint>
#include <cstring>
#include "Player/exe_bodies.h"
#include "Component/RadiusDescription.h"
#include "Game/Map.h"
#include "Game/WorldGeometry.h"
#include "Game/WorldGeometryElements.h"   // GeometryBatch (the decal element)

// The radius tessellation 0x1400ace40 (TradeWork_impl_ace40, tool_base_highlight_tess.cpp)
// and the tile-entry array grow 0x14001fa80 (GeometryBatchArray::SetSize, the fill form)
// are called by name; so is the static MemAlloc/MemFree pair (slots 0x140152b60 /
// 0x140152b68) -> System::MemoryAllocator. The vertex-dedup set's nodes are
// DecalVertexNode (Player/exe_bodies.h); its rotations / retrace are the shared
// Avl38 templates (0x140015000 / 0x140014ed0 / 0x140014b40).

// 0x140014b00 — recursive BST free: Avl38FreeSubtree (Player/exe_bodies.h).
template void Avl38FreeSubtree<DecalVertexNode>(const void*, DecalVertexNode*);

namespace {

// ============================================================
// 0x14001f850 — AVL node allocator. MemAlloc(0x38), copy the six dwords of the
// key, set parent (+0x18), null children / balance / height. 1:1 port (RCX, the
// caller's loop counter, is never read). Returns the new node (or null on OOM).
// ============================================================
DecalVertexNode* DecalVertexNodeAlloc(const DecalVertexKey* key, DecalVertexNode* parent) {
    DecalVertexNode* p = static_cast<DecalVertexNode*>(System::MemoryAllocator::MemAlloc(0x38, 8));
    if (p == nullptr) return nullptr;
    p->key     = *key;        // +0x00..+0x17 (six dword moves in the tree)
    p->parent  = parent;      // +0x18
    p->left    = nullptr;     // +0x20
    p->right   = nullptr;     // +0x28
    p->balance = 0;           // +0x30 balance / height (one dword store in the tree)
    p->height  = 0;
    return p;
}

}  // namespace


// ============================================================
// 0x14001f6b0 — AVL insert keyed by the vertex record. Returns (in *out) the
// matching / created node. 1:1 port of FUN_14001f6b0 (set = {root, count},
// key = the 0x18-byte element).
// ============================================================
DecalVertexNode** DecalVertexSetInsert(AvlTreeHead<DecalVertexNode>* set, DecalVertexNode** out,
                                       const DecalVertexKey* key) {
    DecalVertexNode* node = set->root;
    if (node == nullptr) {
        DecalVertexNode* n = DecalVertexNodeAlloc(key, nullptr);
        set->count = set->count + 1;
        set->root = n;
        *out = n;
        return out;
    }
    for (;;) {                                    // LAB_14001f710
        DecalVertexNode* cur = node;
        int  i = 0;
        bool descend = false;     // took a child pointer this pass
        bool inserted = false;
        DecalVertexNode* newNode = nullptr;
        // ---- first comparison loop: descend-left / break(equal-or-greater) ----
        for (;;) {
            int16_t sVar3 = cur->key.vertex.position[i];
            if (key->vertex.position[i] < sVar3) {        // LAB_14001f7dd: go left or insert-left
                DecalVertexNode* l = cur->left;
                if (l != nullptr) { node = l; descend = true; break; }
                newNode = DecalVertexNodeAlloc(key, cur);
                cur->left = newNode;
                inserted = true; break;           // LAB_14001f813
            }
            if (sVar3 < key->vertex.position[i]) break;
            if (key->vertex.uv[i] < cur->key.vertex.uv[i]) {
                DecalVertexNode* l = cur->left;
                if (l != nullptr) { node = l; descend = true; break; }
                newNode = DecalVertexNodeAlloc(key, cur);
                cur->left = newNode;
                inserted = true; break;
            }
            if (cur->key.vertex.uv[i] < key->vertex.uv[i]) break;
            {
                uint8_t b1 = key->vertex.normal[i];
                uint8_t b2 = cur->key.vertex.normal[i];
                if (b1 < b2) {
                    DecalVertexNode* l = cur->left;
                    if (l != nullptr) { node = l; descend = true; break; }
                    newNode = DecalVertexNodeAlloc(key, cur);
                    cur->left = newNode;
                    inserted = true; break;
                }
                if (b1 != b2) break;
            }
            i++;
            if (i >= 4) break;
        }
        if (descend) continue;                    // goto LAB_14001f710
        if (inserted) {
            Avl38Retrace(&set->root, &cur);
            set->count = set->count + 1;
            *out = newNode;
            return out;
        }
        // ---- second comparison loop: exact-match(return) / descend-right / insert-right ----
        i = 0;
        bool found = false;
        for (;;) {
            int16_t sVar3 = cur->key.vertex.position[i];
            if (sVar3 < key->vertex.position[i]) break;
            if (key->vertex.position[i] < sVar3) { found = true; break; }   // LAB_14001f7c2
            int16_t nodeS = cur->key.vertex.uv[i];
            if (nodeS < key->vertex.uv[i]) break;
            if (key->vertex.uv[i] < nodeS) { found = true; break; }
            uint8_t b1 = cur->key.vertex.normal[i];
            uint8_t b2 = key->vertex.normal[i];
            if (b1 < b2) break;
            if (b1 != b2) { found = true; break; }
            i++;
            if (3 < i) { found = true; break; }
        }
        if (found) { *out = cur; return out; }
        // descend right, or insert as right child
        DecalVertexNode* r = cur->right;
        if (r == nullptr) {
            newNode = DecalVertexNodeAlloc(key, cur);
            cur->right = newNode;
            Avl38Retrace(&set->root, &cur);
            set->count = set->count + 1;
            *out = newNode;
            return out;
        }
        node = r;                                 // continue outer loop with right child
    }
}

// ============================================================
// 0x14001fc00 — AVL find keyed by the vertex record. Returns the matching node
// or null. 1:1 port of FUN_14001fc00.
// ============================================================
DecalVertexNode* DecalVertexSetFind(const AvlTreeHead<DecalVertexNode>* set, const GeometryBatchVertex* key) {
    DecalVertexNode* node = set->root;
    for (;;) {
        if (node == nullptr) return nullptr;
        int  i = 0;
        bool descendedLeft = false;
        for (;;) {                                 // first loop
            int16_t sVar3 = node->key.vertex.position[i];
            if (key->position[i] < sVar3) { node = node->left; descendedLeft = true; break; }
            if (sVar3 < key->position[i]) break;
            if (key->uv[i] < node->key.vertex.uv[i]) {
                node = node->left; descendedLeft = true; break;
            }
            if (node->key.vertex.uv[i] < key->uv[i]) break;
            {
                uint8_t b1 = key->normal[i];
                uint8_t b2 = node->key.vertex.normal[i];
                if (b1 < b2) { node = node->left; descendedLeft = true; break; }
                if (b1 != b2) break;
            }
            i++;
            if (i >= 4) break;
        }
        if (descendedLeft) continue;
        i = 0;
        for (;;) {                                 // second loop
            int16_t sVar3 = node->key.vertex.position[i];
            if (sVar3 < key->position[i]) break;
            if (key->position[i] < sVar3) return node;
            int16_t nodeS = node->key.vertex.uv[i];
            if (nodeS < key->uv[i]) break;
            if (key->uv[i] < nodeS) return node;
            uint8_t b1 = node->key.vertex.normal[i];
            uint8_t b2 = key->normal[i];
            if (b1 < b2) break;
            if (b1 != b2) return node;
            i++;
            if (3 < i) return node;
        }
        node = node->right;   // descend right
    }
}

// ============================================================
// 0x1400b0d80 — the decal slot manager: find a free 0x60-byte GeometryBatch in
// WorldGeometry::_decals (+0x1F0), or grow the array by one from a zeroed
// template (slot -1); write the decal; tessellate it via 0x1400ace40. Returns the
// slot index (or -1 if the material is null). 1:1 port (19 args; param_18 is
// unused/dead). A WorldGeometry member in retail; LEFT free: WorldGeometry.h is
// block GAME-c-2's (running). Declared in Player/exe_bodies.h (DecalComponent calls it).
// ============================================================
int WorldGeometry::AddDecal(int param_2, int param_3, int param_4,
             int param_5, uint8_t param_6, uint32_t param_7, Graphics::MaterialInstance* material,
             bool param_9, float param_10, float param_11,
             float param_12, float param_13, float param_14,
             char param_15, int param_16, int param_17, bool param_19) {
    if (material == nullptr) return -1;
    // EXT SEAM (NOT RETAIL, docs/extensions/handins/CREATEONDEMAND.md): the decal material's textures
    // are queued for their first-use creates (WorldGeometry.h has the contract).
    if (_extMaterialTaken != nullptr) _extMaterialTaken(material);

    GeometryBatchArray& decals = _decals;
    int iVar3 = 0;
    int iVar2 = decals._size;                                    // count
    bool grew = false;
    if (0 < iVar2) {
        const GeometryBatch* batch = decals._data;
        do {
            if (batch->slot < 0) {                                // free slot marker
                if (-1 < iVar3) { grew = false; goto haveSlot; }
                break;
            }
            iVar3++;
            batch++;                                              // stride 0x60 bytes
        } while (iVar3 < iVar2);
    }
    // no free slot -> grow the array by 1 from a default-constructed template
    // (0x1400b0e01..0x1400b0e47: GeometryBatch(), slot = -1, +0x5C left as found);
    // its destructor (0x1400b0e64..0x1400b0e84: indices, then vertices) runs at the
    // end of the block.
    {
        GeometryBatch tmpl;
        decals.SetSize(decals._size + 1, tmpl);                  // 0x14001fa80
        iVar3 = iVar2;
        grew = true;
    }
haveSlot:
    (void)grew;
    iVar2 = (_gridSize - _fieldC4) >> 1;
    GeometryBatch* entry = &decals._data[iVar3];
    entry->material = material;
    entry->slot = iVar3;
    entry->minX = iVar2 + param_2;
    entry->minY = iVar2 + param_3;
    entry->maxX = iVar2 + param_4;
    entry->maxY = param_5 + iVar2;
    entry->field_0x3C = param_6;
    entry->field_0x40 = static_cast<int32_t>(param_7);
    entry->field_0x44 = param_9;   // the bool parameter's byte, stored as is
    // +0x48..+0x58: retail's five float stack parameters, each a MOVSS load / store (0x1400b0ee7).
    entry->field_0x48[0] = param_10;
    entry->field_0x48[1] = param_11;
    entry->field_0x48[2] = param_12;
    entry->field_0x48[3] = param_13;
    entry->field_0x48[4] = param_14;
    entry->field_0x5C = 1;
    entry->alternateKey = param_19;
    TessellateDecal(iVar3, param_15, param_16, param_17);
    return iVar3;
}

// ============================================================
// 0x140055430 — HighlightAdd. Builds the radius-highlight rectangle around centerCell
// and submits it to the terrain decal system (GameUpdateLoop +0x110, the
// WorldGeometry). param_1 = the RadiusDescription (+0x20 material != null, +0x28
// radius). Returns the highlight slot or -1.
// 1:1 port of FUN_140055430 (b0d80 args disasm-verified; uvScale = g_fUVScale = 1.0f).
// ============================================================
int RadiusDescription::AddHighlight(int param_2) const {
    Graphics::MaterialInstance* target = _material;
    if (target == nullptr) return -1;

    GameUpdateLoop* gameWorld = GameUpdateLoop::_singleton;
    int mapSize = gameWorld->m_game->size;

    // gw+0x110 holds a POINTER to the tile-highlight renderer, not the renderer
    // itself. Retail (0x140055430): TradeWork_impl_0d80(*(longlong*)(gw + 0x110), ...).
    WorldGeometry* highlightMgr = gameWorld->m_worldGeometry;
    if (highlightMgr == nullptr) return -1;
    int row = param_2 / mapSize;
    int col = param_2 % mapSize;
    int r   = _radius;

    const float uv = kRdata_14017214c;   // 1.0f; retail MOVSSes it (and 0.0f) into the float stack slots (0x1400554c0)

    return highlightMgr->AddDecal(// 0x1400b0d80
                    col - r, row - r, r + col, r + row,
                    0xff, 0, target, false,
                    uv, 0.0f, 0.0f, uv, uv,
                    1, r - 2, r + 2, true);
}