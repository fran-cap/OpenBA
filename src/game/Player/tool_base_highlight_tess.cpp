// src/game/Player/tool_base_highlight_tess.cpp
// TradeWork_impl_ace40 (0x1400ace40) — the radius / tiled-decal tile tessellation
// builder. Shared by BOTH the PlacementTool radius-highlight overlay (via
// Player_HighlightAdd_55430 -> TradeWork_impl_0d80) AND the building ground-decal
// path (DecalComponent::Draw -> TradeWork_impl_0d80). It builds the vertex + index
// buffers for one tile-renderer slot (gameWorld+0x110, slot = base+0x1f0 + idx*0x60),
// deduping shared corner vertices through an AVL BST keyed by a 0x18-byte descriptor.
//
// Promoted from docs/history/notes/wave5_drafts/ace40.draft.cpp after end-to-end integration for
// the terrain-decal bring-up (BANISHED_LIVE_DECAL). FAITHFUL 1:1 PORT of the Ghidra
// decompile, cross-checked against disassembly for every float lane, CONCAT pack,
// dropped-arg call, and field offset. Do NOT "improve" — a wrong port here is SILENT
// GEOMETRY CORRUPTION.
//
// STANDALONE-ONLY: like the rest of tool_base_highlight.cpp, this reimpl is reached
// only via RegisterExeAddr. ResolveAddr on a .rdata constant returns NULL in standalone
// (see reference_standalone_rdata_null_fallback), so the .rdata constants below are
// embedded here as their retail values.
//
// The slot is a GeometryBatch (Game/WorldGeometryElements.h, the 0x60-byte decal
// element at WorldGeometry +0x1F0; batch = decals.data + param_2):
//   vertices (+0x08, 0x14-byte GeometryBatchVertex)   indices (+0x18, uint16)
//   minX / minY / maxX / maxY (+0x2c..+0x38)   rotation (+0x40, int yawDeg)
//   alpha (+0x3C, byte passed to cc10)   tiled (+0x44, modeFlag cVar1)
//   uvParams[0..4] (+0x48, float UV params when modeFlag==0)

#include "Game/exe_constants.h"   // kRdata_* (B3R Leftovers: the .rdata constants)
#include "math/Math.h"
#include "system/MemoryAllocator.h"
#include "Action/exe_bodies.h"
#include "collection/Collection.h"
#include <cstdint>
#include <cstring>   // std::memcpy (the 8 + 8 + 4 vertex copy)
#include <cstdio>
#include <cstdlib>
#include "Player/exe_bodies.h"
#include "Game/WorldGeometry.h"           // TradeWork_impl_ace40 reads WorldGeometry::_decals
#include "Game/WorldGeometryElements.h"   // GeometryBatch / GeometryBatchVertex (0x14001fa80 / 0x14001ee40)

// ---- engine .rdata constants (retail values; see header) -------------------------
namespace {
constexpr float    kDAT_140172260 =  4096.0f;    // 0x45800000  (UV tile-size const)
constexpr float    kDAT_140172280 = -1.0f;       // 0xbf800000
constexpr float    kDAT_140172114 =  0.5f;       // 0x3f000000  (the rotation seed)
// 0x1401722c0 (0x80000000) is the XORPS sign mask retail negates the seed with: a float negate.
constexpr uint8_t  kDAT_1401c23cf = 0x2e;        // base index byte (iVar13 seed = 46)
}  // namespace

// ---- the vertex-dedup set (tool_base_highlight.cpp, Player/exe_bodies.h) -----------
//   0x14001f6b0 DecalVertexSetInsert, 0x14001fc00 DecalVertexSetFind,
//   0x140014b00 Avl38FreeSubtree<DecalVertexNode>.

namespace {

// ---- exe sub-callees, called by name (Action/exe_bodies.h) -------------------------
//   0x1400acc10 Pf_RoadQuad_cc10, 0x1400ae180 Pf_TileShapeLut_ae180,
//   0x14001ee40 / 0x14001f8d0: Collection::Array<GeometryBatchVertex>::SetSize in its two
//   forms (GeometryBatch::vertices' member; the RawArray pool's RawArraySetSize).
// Static MemAlloc/MemFree (slots 0x140152b60/0x140152b68) are
// System::MemoryAllocator::MemAlloc / MemFree.
//
// ---- engine Transform2/Vector2 ops (.rdata dispatch slots), called as the members of
// Math::Transform2 / Math::Vector2, each constructing / returning into its stack block:
//   0x140151e58 Vector2::Vector2()            0x140152648 Transform2(Scalar&, Vector2&)
//   0x140152650 Transform2::FromYawOffset     0x140152640 Transform2::operator*(Transform2)
//   0x140152658 Transform2::operator*(Vector2)

// The two growable lists inside a highlight slot (GeometryBatch::vertices / indices):
//   +0x08 {data, size, cap}  GeometryBatchVertex records, 0x14 bytes each (the pool ->
//                            buffer copy at the end of TessellateSlot moves them 8+8+4);
//   +0x18 {data, size, cap}  uint16 triangle indices.
// ENDGAME B.3.2 step 2: AppendIndex WAS a verbatim copy of rtcoll::SetSizeCore for
// the +0x18 list (exact-equal early return; size stored before the capacity test;
// `oldCount != 0 ? n*3 : n` with newCount == oldCount + 1 by construction, i.e. the
// reference rule; MemAlloc(cap * 2, align 2) through the static slot 0x140152b60 —
// the same locking static rtcoll::MemAlloc is; copy the prefix only when the block
// moved; the new slot takes the fill; the COMPUTED element address null-tested;
// free the old block when it moved). The trait-derived Array<uint16_t> row
// (stride 2, align 2, byteCopy, init == nullptr, destroy == nullptr) is that
// exactly. The vertex list is only ever destroyed here (its grow is 0x14001ee40,
// outside this file); its element is the trivially destructible GeometryBatchVertex,
// so the dtor is MemFree + zero regardless. Both are typed Collection::Array members
// of GeometryBatch (B.4 H5-P2) over the same erased core.

// cc10's output: retail's 0x60-byte stack block, the four 0x14-byte corner records
// first. No constructor: retail does not initialise it.
struct TessCornerBlock {
    GeometryBatchVertex corner[4];    // +0x00 / +0x14 / +0x28 / +0x3C
    uint8_t             _tail[0x10];  // +0x50
};
static_assert(sizeof(TessCornerBlock) == 0x60, "the ace40 corner block is 0x60 bytes");

// AppendIndex — inlined "append one short index to the growable index buffer at
// slot+0x18/+0x20/+0x24" block (orig emits it verbatim per triangle vertex).
inline void AppendIndex(GeometryBatch* batch, uint16_t idx) {
    batch->indices.SetSize(batch->indices._size + 1, idx);
}

// EmitVertex — resolve ONE corner descriptor to a vertex index (find/insert/append),
// then append that index to the index buffer.
inline void EmitVertex(GeometryBatch* batch, AvlTreeHead<DecalVertexNode>* set,
                       RawArray<GeometryBatchVertex>* pool,
                       const GeometryBatchVertex* desc, DecalVertexNode** outNode) {
    // The set is searched with the corner record itself (its 0x14 bytes are the key).
    DecalVertexNode* node = DecalVertexSetFind(set, desc);   // 0x14001fc00
    uint16_t idx;
    if (node == nullptr) {
        int curCount = pool->size;
        DecalVertexKey rec;
        rec.vertex = *desc;
        rec.index  = curCount;                // CONCAT44((int)local_2c8, local_268)
        idx = (uint16_t)curCount;
        // CRITICAL argument split (matches ace40 disasm exactly): the BST insert stores
        // `rec` — the 6-int key whose +0x14 slot is the vertex INDEX — so that a later
        // dedup HIT reads back the correct index via node+0x14. The vertex POOL stores the
        // raw `desc` (position + UVs). Passing these swapped makes node+0x14 hold garbage
        // descriptor bytes; every shared-corner dedup hit then returns a wild index and the
        // triangle stretches off toward the bogus vertex (the "sliver to map center" bug).
        DecalVertexSetInsert(set, outNode, &rec);               // 0x14001f6b0 (key = rec)
        RawArraySetSize(*pool, curCount + 1, *desc);            // 0x14001f8d0 (vertex = desc)
    } else {
        idx = (uint16_t)node->key.index;   // the low word (retail reads a word at node+0x14)
    }
    AppendIndex(batch, idx);
}

} // namespace

// =================================================================================
// TradeWork_impl_ace40 — 0x1400ace40
// =================================================================================
void WorldGeometry::TessellateDecal(int param_2, char param_3,
                                     int param_4, int param_5) {
    const float fUV = kRdata_14017214c;   // g_fUVScale (1.0f in retail)

    GeometryBatch* batch = _decals._data + param_2;   // +0x1f0
    int iVar24 = (batch->maxX - batch->minX) + 1; // width
    int iVar28 = (batch->maxY - batch->minY) + 1; // height

    // four scratch Vector2 (local_1d8..local_1c0); ctor (0x140151e58) x4.
    Math::Vector2 corner[4];

    float fVar3 = fUV;
    char  cVar1 = batch->tiled;   // modeFlag

    float fVar8 = 0.0f;   // the rotation seed (uVar8)
    float fVar33 = 0.0f;
    float fVar31, fVar32, fVar34, fVar35, fVar36;
    float local_288, fStack_284;
    float local_290_hi;

    if (cVar1 == '\0') {
        float fVar_div = batch->uvParams[0];
        local_288 = 0.0f;
        fVar33 = batch->uvParams[1] / fVar_div;
        fVar31 = (batch->uvParams[3] - batch->uvParams[1]) / fVar_div;
        fVar32 = (batch->uvParams[4] - batch->uvParams[2]) / fVar_div;
        fVar34 = batch->uvParams[2] / fVar_div;
        fVar35 = kDAT_140172260;
        fStack_284 = fUV;
        local_290_hi = kDAT_140172280;
        fVar36 = fUV;
        fVar8 = kDAT_140172114;
    } else {
        fVar36 = (float)iVar24;
        local_290_hi = (float)iVar28;
        local_288 = (float)batch->minX;
        fStack_284 = (float)batch->minY;
        fVar31 = fUV;
        fVar32 = fUV;
        fVar34 = fVar33;     // 0
        fVar35 = fUV;
    }

    Math::Scalar local_2a0;                                // 0.0f
    Math::Scalar local_29c((float)batch->rotation);     // yawDeg
    Math::Scalar local_2b0_0;                              // 0.0f

    Math::Vector2 local_228(fVar8, fVar8);

    Math::Vector2 local_298;

    Math::Vector2 local_2a8(-fVar8, -fVar8);   // XORPS with the sign mask at 0x1401722c0

    // Transform2 chain (orig slots 0x140152648/650/640). Each Transform2 is 0x18 bytes
    // (6 floats: [m00, m01, m10, m11, tx, ty]).

    // The three "raw data" operands of the operator* chain. CRITICAL: these MUST be
    // contiguous 0x18-byte Transform2 buffers. The prior port built them from separate,
    // non-adjacent stack locals (local_2e8/uStack_2e0/local_2d8, local_220/218/210,
    // local_208/200/1f8) and — worse — passed only &local_2e8/&local_2d8 into the chain,
    // DROPPING the uv-rect and the 4096 scale entirely. Result: T_1f0 had neither the
    // uv-rect mapping nor the ×4096 texel scale, so every per-tile UV came out as the raw
    // 0..1 value and truncated to int16 (0, 0/1) -> the decal sampled texel (0,0) (the empty
    // corner of the dirt-splat atlas) -> transparent black -> invisible. Rebuilt as real
    // contiguous buffers and wired EXACTLY per the ace40 disassembly at 0x1400ad0d0..0ad127:
    //   T_1f0 = ((((flip * r108) * r168) * r138) * uvRect) * scale
    // Layout confirmed from the disasm store pattern (e.g. RBP+0x18 = [fVar31,0,0,fVar32,
    // fVar33,fVar34] = uvRect; RBP+0x30 = [fVar35,0,0,fVar35,0,0] = scale; RSP+0x50 = flip).
    Math::Transform2 TF_flip, TF_uvRect, TF_scale;
    TF_flip.rotX   = Math::Vector2(fVar36, 0.0f);   TF_flip.rotY   = Math::Vector2(0.0f, local_290_hi);
    TF_flip.translation   = Math::Vector2(local_288, fStack_284);
    TF_uvRect.rotX = Math::Vector2(fVar31, 0.0f);   TF_uvRect.rotY = Math::Vector2(0.0f, fVar32);
    TF_uvRect.translation = Math::Vector2(fVar33, fVar34);
    TF_scale.rotX  = Math::Vector2(fVar35, 0.0f);   TF_scale.rotY  = Math::Vector2(0.0f, fVar35);
    TF_scale.translation  = Math::Vector2(0.0f, 0.0f);

    // FromYawOffset (0x140152650) reads an 8-byte (Vector2) OFFSET from R8. The disasm
    // passes a dedicated zeroed slot ([RBP-0x60] = MOV qword,0 → {0,0}); we must give it a
    // real 8-byte zero Vector2 — NOT &local_2a0 (a lone int), whose second float lane reads a
    // garbage adjacent stack local. That lane held -1.0, injecting a translate(0,-1) that
    // shifted every decal's diffuse V down by one uv-cell height (0.15625): the barn survived
    // (v0=0.844 → 0.688, still positive) but top-row cells (v0=0) folded NEGATIVE → sampled
    // off-atlas → transparent → houses/stockpiles invisible. Verified via [ace40::op] trace:
    // with {0,0} r168 offset, decal V now maps positive/in-atlas (barn f5=3456=0.84375·4096).
    Math::Vector2 yawOffset;   // disasm [RBP-0x60] clean {0,0}
    const Math::Transform2 T_138(local_2a0, local_228);                                     // 0x140152648 (ctor1)
    const Math::Transform2 T_168 = Math::Transform2::FromYawOffset(local_29c, yawOffset);   // 0x140152650 (yawOff)
    const Math::Transform2 T_108(local_2b0_0, local_2a8);                                   // 0x140152648 (ctor3)

    const Math::Transform2 T_160 = TF_flip * T_108;     // 0x140152640  flip   * ctor3
    const Math::Transform2 T_148 = T_160 * T_168;       //              (..)   * yawOff
    const Math::Transform2 T_118 = T_148 * T_138;       //              (..)   * ctor1
    const Math::Transform2 T_e8  = T_118 * TF_uvRect;   //              (..)   * uvRect
    const Math::Transform2 T_1f0 = T_e8 * TF_scale;     //              (..)   * scale


    // 4 unit-quad corners via operator*(Vector2) (0x140152658): {0,0},{1,0},{1,1},{0,1}.
    auto mulVec = [&](const Math::Vector2& unitCorner) -> Math::Vector2 {
        local_2a8 = unitCorner;
        local_298 = T_1f0 * local_2a8;
        return local_298;
    };
    corner[0] = mulVec(Math::Vector2(0.0f, 0.0f));
    corner[1] = mulVec(Math::Vector2(1.0f, 0.0f));
    corner[2] = mulVec(Math::Vector2(1.0f, 1.0f));
    corner[3] = mulVec(Math::Vector2(0.0f, 1.0f));

    // dedup pool + BST.
    RawArray<GeometryBatchVertex> pool = {nullptr, 0, 0};
    AvlTreeHead<DecalVertexNode> bst = {nullptr, 0, {}};

    int iVar13 = (int)kDAT_1401c23cf;
    fVar33 = (float)((batch->maxX + batch->minX) >> 1);
    int iVar25 = (batch->maxY + batch->minY) >> 1;
    // (retail reuses the local_2b0 / local_2a0 / local_29c slots for iVar13 / iVar25 /
    // fVar33 here, and spills plVar30 to the local_228 slot and reloads it at the end of
    // each row; nothing reads them back, and they are not modelled.)

    // free + reset the slot's vertex and index buffers.
    // ENDGAME B.3.2 step 2: both WERE inlined Collection::Array<T>::~Array bodies
    // for a trivially destructible T (MemFree the buffer — null-guarded in both —
    // then zero _data and the _size/_capacity qword). Now the dtor itself, in place.
    // ppvVar9 keeps the pre-free index pointer because the fnE180 call below still
    // feeds its upper bytes through a register-passthrough byte argument.
    uint16_t* ppvVar9 = batch->indices._data;
    batch->vertices.DestroyAndFree();
    batch->indices.DestroyAndFree();

    DecalVertexNode* outNode;

    // main tile loop (y = minY..maxY, x = minX..maxX).
    int iVar18 = batch->minY;
    if (iVar18 <= batch->maxY) {
        do {
            int iVar22 = batch->minX;
            int iVar11 = batch->maxX;
            if (iVar22 <= iVar11) {
                do {
                    bool doTile;
                    if (param_3 == '\0') {
                        doTile = true;
                    } else {
                        uint32_t dist = (uint32_t)((iVar18 - iVar25) * (iVar18 - iVar25) +
                                                   (iVar22 - (int)fVar33) * (iVar22 - (int)fVar33));
                        doTile = (param_4 * param_4 <= (int)dist) && ((int)dist <= param_5 * param_5);
                    }

                    if (doTile) {
                        if (cVar1 != '\0') {
                            int iVar25b = batch->minX;
                            int iVar13b = iVar22 - 1;
                            int iVar14  = iVar22 + 1;
                            char cVar26, cVar23, cVar17, cVar12;
                            cVar26 = ((iVar13b < iVar25b) && (batch->maxY < iVar18 + 1)) ? '\0' : '\x01';
                            cVar23 = ((iVar11   < iVar14 ) && (batch->maxY < iVar18 + 1)) ? '\0' : '\x01';
                            cVar17 = ((iVar13b < iVar25b) && (iVar18 - 1 < batch->minY)) ? '\0' : '\x01';
                            cVar12 = ((iVar11   < iVar14 ) && (iVar18 - 1 < batch->minY)) ? '\0' : '\x01';
                            bool bVar2 = iVar18 + 1 <= batch->maxY;
                            iVar13 = Pf_TileShapeLut_ae180(
                                reinterpret_cast<void*>((uint64_t)((uintptr_t)ppvVar9 & ~0xffULL) | (uint8_t)bVar2),  // CAST-OK: ptr_int
                                (char)(iVar25b <= iVar13b), (char)(iVar14 <= iVar11),
                                (char)(batch->minY <= iVar18 - 1),
                                (char)bVar2, cVar12, cVar17, cVar23, cVar26);
                        }

                        // cc10: emit the 4 corner descriptors into descBlock.
                        TessCornerBlock descBlock;
                        BuildRoadQuad(iVar22, iVar18, descBlock.corner, iVar13,
                                         batch->alpha);

                        // per-tile UV: 4 corners, write short pos pair into descBlock.
                        {
                            int32_t xOff[4]; int32_t yOff[4];
                            // retail: one qword store per pair (H7: two stores each)
                            xOff[0] = 0; xOff[1] = 1;   // {0,1}
                            xOff[2] = 1; xOff[3] = 0;   // {1,0}
                            yOff[0] = 0; yOff[1] = 0;   // {0,0}
                            yOff[2] = 1; yOff[3] = 1;   // {1,1}
                            for (int64_t k = 0; k < 4; ++k) {
                                float u = (float)((xOff[k] - batch->minX) + iVar22)
                                          * (fVar3 / (float)iVar24);
                                float v = (float)((yOff[k] - batch->minY) + iVar18)
                                          * (fVar3 / (float)iVar28);
                                local_2a8 = Math::Vector2(u, v);
                                local_298 = T_1f0 * local_2a8;   // 0x140152658
                                descBlock.corner[k].uv[0] = (int16_t)(int)local_298.x;   // +0x08
                                descBlock.corner[k].uv[1] = (int16_t)(int)local_298.y;   // +0x0A
                            }
                        }

                        const GeometryBatchVertex* c0 = &descBlock.corner[0];   // +0x00
                        const GeometryBatchVertex* c1 = &descBlock.corner[1];   // +0x14
                        const GeometryBatchVertex* c2 = &descBlock.corner[2];   // +0x28
                        const GeometryBatchVertex* c3 = &descBlock.corner[3];   // +0x3c

                        auto emit = [&](const GeometryBatchVertex* desc) {
                            EmitVertex(batch, &bst, &pool, desc, &outNode);
                        };

                        if (((iVar22 + iVar18) & 1U) == 0) {
                            emit(c0); emit(c3); emit(c1);
                            emit(c1); emit(c3); emit(c2);
                        } else {
                            // odd-parity tile: triangles (c0,c2,c1) and (c0,c3,c2) — the
                            // last vertex is c2 (descBlock+0x28), NOT c1. Matches the ace40
                            // disasm else-branch find order 278,250,264,278,23c,250.
                            emit(c0); emit(c2); emit(c1);
                            emit(c0); emit(c3); emit(c2);
                        }
                    } // doTile

                    iVar22 = iVar22 + 1;
                    iVar11 = batch->maxX;
                } while (iVar22 <= iVar11);
            }
            iVar18 = iVar18 + 1;
        } while (iVar18 <= batch->maxY);
    }

    // finalize: copy pooled vertices into the slot's vertex buffer.
    batch->vertices.SetSize(pool.size);                    // 0x14001ee40
    int     total   = pool.size;
    // Each 0x14-byte vertex moves as 8 + 8 + 4 bytes, member by member (a
    // GeometryBatchVertex assignment would compile to one 16-byte + one 4-byte
    // move at -O2, a store-width change).
    GeometryBatchVertex*       vb  = batch->vertices._data;
    const GeometryBatchVertex* src = pool.data;
    for (int64_t i = 0; i < total; ++i) {
        std::memcpy(vb[i].position, src[i].position, sizeof vb[i].position);   // +0x00, 8 bytes
        std::memcpy(vb[i].uv, src[i].uv, sizeof vb[i].uv);                     // +0x08, 8 bytes
        std::memcpy(vb[i].normal, src[i].normal, sizeof vb[i].normal);         // +0x10, 4 bytes
    }

    // free the BST + vertex pool.
    if (bst.root != nullptr) {
        Avl38FreeSubtree(&bst.root, bst.root->left);    // 0x140014b00
        Avl38FreeSubtree(&bst.root, bst.root->right);   // 0x140014b00
        System::MemoryAllocator::MemFree(bst.root);   // bst.root != 0 here
    }
    bst.root  = nullptr;
    bst.count = 0;
    if (pool.data) System::MemoryAllocator::MemFree(pool.data);

    (void)corner;
}
