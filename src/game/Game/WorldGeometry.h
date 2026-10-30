#pragma once
// ============================================================================
// src/game/Game/WorldGeometry.h — ENDGAME B.3.8 block LOOP (B38_RULES sec. 5,
// "the 5 unreflected exe subclasses of Runtime classes")
//
// `WorldGeometry` (0x230): the exe's unreflected `Graphics::Geometry` subclass
// that draws the terrain (heightmap chunks), the water contour mesh and the
// decal SoA arrays — GameUpdateLoop+0x110 in game, MenuScene+0x28 on the menu.
// Retail has no name for it (B38_SLOTS.md "{exe Graphics::Geometry subclass A}");
// the name is the tree's.
//
// RETAIL (Application-x64.exe): table 0x14015f110, 9 slots, MemAlloc(0x230),
// ctor 0x1400b00e0 (and the inlined copy in TradeWork_impl_ed50).
//   [0] 0x14001ecc0 scalar deleting dtor (body 0x1400aed50)
//   [1] 0x1400aed40 Update       (JMP 0x1400aad50, the dirty-chunk rebuild)
//   [2] 0x1400aea10 Draw         (per draw item)
//   [3] 0x1400ae490 AddDrawItems (frustum cull + Renderer::AddDrawItem)
//   [4]..[8] Graphics::Geometry's (imports)
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "graphics/Geometry.h"
#include "video/Buffers.h"

// B.4 GAME-b: forward declarations for the appended members.
#include "Action/PathSearch.h"   // RawArray<T>, RawArrayFree (the arrays at +0xD8 and +0x1A0)
#include "Action/PathAvl.h"      // AvlTreeHead, AvlNode28 (the +0xE8 set)
#include "Game/WorldGeometryElements.h"   // TerrainVertex, GeometryBatch(_DestroyRange), GeometryDrawRange
class TerrainGenerator;       // Game/TerrainGenerator.h
struct TerrainHeightfield;    // Game/TerrainHeightfield.h
namespace Video { class ImageBuffer; }
namespace Graphics { class MaterialInstance; }
namespace IO { class Stream; }
namespace Object { class External; }

// ---------------------------------------------------------------------------
// The members +0xD8 .. +0x220 that ~WorldGeometry (body 0x1400aed50) releases.
// Retail's dtor body deletes +0x148 and frees +0x140, then releases the rest in
// REVERSE address order: +0x220 down to +0x1A0, the four buffers +0x180 /
// +0x160 / +0x120 / +0x100 (the imported ~IndexBuffer / ~VertexBuffer), the
// +0xE8 set, +0xD8. That is C++'s reverse-declaration member destruction, so
// each of them is a member with its own destructor (B.4 Z CODEA).
// ---------------------------------------------------------------------------

// A Collection::Array<T> header (RawArray's three fields) whose destructor is
// retail's release for it: `Release` (default: the inlined ~Array of a
// trivially destructible T, MemFree then the fields zeroed). The ctor stays
// trivial: 0x1400b00e0 writes the fields in WorldGeometry's ctor body.
template <class T, void (*Release)(RawArray<T>&) = &RawArrayFree<T>>
struct WorldGeometryArray : RawArray<T> {
    WorldGeometryArray() = default;
    WorldGeometryArray(const WorldGeometryArray&) = delete;
    WorldGeometryArray& operator=(const WorldGeometryArray&) = delete;
    ~WorldGeometryArray() { Release(*this); }
};

// +0x1E0: 0x1400175d0, each inner {data, size|cap} freed and zeroed in
// ascending order, then the outer block (gameupdateloop_teardown.cpp).
void ArrayOfArrays_Clear_175d0(RawArray<RawArray<Math::Vector2>>& outlines);  // 0x1400175d0

// +0xE8: the dirty-chunk set, a Collection::Set header {root, count}. Retail's
// release (0x1400aeee0..0x1400aef18): both subtrees through 0x140014c30, the
// root node, then root = 0 and the dword count = 0.
struct DirtyChunkSet : AvlTreeHead<AvlNode28> {
    DirtyChunkSet() = default;
    DirtyChunkSet(const DirtyChunkSet&) = delete;
    DirtyChunkSet& operator=(const DirtyChunkSet&) = delete;
    ~DirtyChunkSet();                                                  // geometry_draw.cpp
};
static_assert(sizeof(DirtyChunkSet) == 0x10, "DirtyChunkSet is the Set header {root, count}");

class WorldGeometry final : public Graphics::Geometry {
public:
    WorldGeometry();                                                  // 0x1400b00e0

    // body 0x1400aed50
    ~WorldGeometry() override;                                        // [0] 0x14001ecc0
    void Update(const System::Time& time) override;                   // [1] 0x1400aed40
    void Draw(Video::Context& context, const Graphics::Camera& camera,
              Graphics::ExternalPtrHandle material,
              float opacity, int a, int b, int c) const override;     // [2] 0x1400aea10
    void AddDrawItems(const Graphics::Scene& scene,
                      Graphics::Renderer& renderer) const override;   // [3] 0x1400ae490

    // 0x1400aad50: the dirty-chunk rebuild slot 1 JMPs to (a non-virtual member;
    // also reached by address through the Reimpl_GeometryChunkUpdate door).
    void RebuildDirtyChunks();
    void RemoveDecal(int decalId);                                   // 0x1400b0d00 (defined in Component/decalcomponent.cpp)
    // B.4 ACT-a (bodies in Action/action_pf_road.cpp)
    void MarkChunksDirty(int x, int y);                              // 0x1400ab150
    void RebuildRoadMesh(class Map* map);                            // 0x1400af370
    // B.4 GAME-b (WorldGeometryElements.h; Serialize bodies in geometry_serialize.cpp,
    // the mesh / image helpers in serialize_heightmap_mesh.cpp / serialize_image_upload.cpp)
    void Serialize(IO::Stream& stream);                               // 0x1400b04d0
    void SerializeLegacyBatches(IO::Stream& stream);                  // 0x1400af1a0
    void BuildTerrainMesh();                                          // 0x1400ac1e0
    void UploadHeightImage();                                         // 0x1400ac780
    void FillTerrainVertices(int colStart, int rowStart, int width, int height,
                             TerrainVertex* out) const;               // 0x1400ac880
    Math::Vector3 TerrainNormal(int col, int row) const;              // 0x1400ae270
    // B.4 GAME-c: the new-game terrain build (worldgen.cpp, water_contour_mesh.cpp)
    void BuildTerrain(TerrainGenerator* generator, int sizeIndex, uint32_t seed);  // 0x1400b0230
    void BuildFromHeightfield(TerrainHeightfield* heightfield);       // 0x1400ac4b0
    void BuildWaterMesh(TerrainHeightfield* heightfield);             // 0x1400ab260
    void BuildWaterBodies(const TerrainHeightfield* heightfield);     // 0x1400ab260 phase 1 (B5: split body)
    void BuildWaterShores(const TerrainHeightfield* heightfield);     // 0x1400ab260 phase 2
    // The device-lost pair GameUpdateLoop::OnVideoError / OnVideoRestart call
    // (0x140095985 / 0x140095945); bodies in geometry_draw.cpp.
    void OnVideoError();                                              // 0x1400b0be0
    void OnVideoRestart();                                            // 0x1400b0c50

    // EXT SEAM (NOT RETAIL, docs/extensions/handins/TERRAINBATCH.md): ext.terrain_batch's
    // live bool, null by default (DESIGN.md hook rule 5) and under the faithful profile;
    // registry_boot.cpp sets it. A static: the 0x230-byte layout is unchanged.
    static const bool* _extTerrainBatch;


public:
    int32_t  _gridSize;           // +0xC0  heightmap edge (cells)
    int32_t  _fieldC4;            // +0xC4
    int32_t  _chunkSize;          // +0xC8  cells per chunk edge
    int32_t  _fieldCC;            // +0xCC
    int32_t  _fieldD0;            // +0xD0
    int32_t  _chunkIndexStride;   // +0xD4  index count per chunk (the ctor leaves it)
    WorldGeometryArray<int16_t> _heights;   // +0xD8  the int16 heightmap (0x14009e0e0 stores one word; B.4 ACT-a H4)
    DirtyChunkSet _dirtyChunks;   // +0xE8  set of dirty chunks (nodes: children +0x10/+0x18; count +0xF0)
    Graphics::MaterialInstance* _terrainMaterial; // +0xF8  MaterialInstance* for the chunks
    Video::VertexBuffer _vertexBuffer;       // +0x100 terrain chunk vertices
    Video::IndexBuffer  _indexBuffer;        // +0x120
    uint8_t* _heightBytes;        // +0x140 raw heightmap (4 bytes/cell)
    Video::ImageBuffer* _heightImage; // +0x148 Video::ImageBuffer* uploaded from +0x140
    Graphics::MaterialInstance* _waterMaterialA;  // +0x150
    Graphics::MaterialInstance* _waterMaterialB;  // +0x158
    Video::VertexBuffer _waterVertexBuffer;  // +0x160 water contour mesh
    Video::IndexBuffer  _waterIndexBuffer;   // +0x180
    // Nine {data, size, capacity} arrays, +0x1A0..+0x22F (no ctor: 0x1400b00e0
    // zeroes each with two qword stores, the tree writes the three named fields,
    // H7; each member's destructor is retail's release of it, run in reverse
    // declaration order as 0x1400aed50 does). Element types: WorldGeometryElements.h.
    WorldGeometryArray<TerrainVertex>     _contourVertices;   // +0x1A0 contour vertex accumulator
    WorldGeometryArray<uint16_t>          _contourIndices;    // +0x1B0 contour index accumulator
    WorldGeometryArray<GeometryDrawRange> _contourRangesA;    // +0x1C0 draw ranges, mode 0x6000
    WorldGeometryArray<GeometryDrawRange> _contourRangesB;    // +0x1D0 draw ranges, mode 0x8000
    WorldGeometryArray<RawArray<Math::Vector2>, &ArrayOfArrays_Clear_175d0>
                                          _contourOutlines;   // +0x1E0 array of point arrays
    // +0x1F0 / +0x200: real Collection::Arrays over kSpec_GeometryBatch (B.4 H5-P2,
    // behaviour:): the ctor's two qword zero stores each (0x1400b01d5..0x1400b01ea)
    // are the member ctor; retail's release (0x1400aedd3..0x1400aee28: 0x14001fb80
    // over the elements, count = the header's low dword, then MemFree and the fields
    // zeroed) is ~Array's ClearCallback over the row.
    GeometryBatchArray                    _decals;            // +0x1F0 0x60-byte batches, mode 0x2000
    GeometryBatchArray                    _roadBatches;       // +0x200 0x60-byte batches, mode 0x4000
    WorldGeometryArray<Graphics::MaterialInstance*> _batchMaterials;   // +0x210 per-batch material refs (ExternalPtr<MaterialInstance>, 0x140020150; typed B8 RT2)
    // +0x220: a real Collection::Array<int32_t>, the last member: retail's ctor zeroes
    // +0x220 / +0x228 as its last two qword member stores (0x1400b01ff / 0x1400b0206,
    // before the +0x30 body store) and the dtor body 0x1400aed50 releases it FIRST
    // (MemFree, fields zeroed). Grows through 0x1400021d0 (stride 4, tail as found:
    // the trait row of int32_t).
    Collection::Array<int32_t>            _batchCounters;     // +0x220 per-batch fill counters
    void BuildRoadQuad(int x, int y, GeometryBatchVertex* quad, int dirIdx, uint8_t blend);   // 0x1400acc10 (Action/action_pf_road.cpp)
    int CellQuadRay(int x, int y, const float* dir, const float* origin, const float* maxT, float* outHit, float* outT, float* outNormal);   // 0x1400aaa20 (body: RayTestCell) (Action/action_pf_road.cpp)
    float HeightUnderPoint(const float* point, float* outNormal);   // 0x1400b0f90 (body: TerrainHeightUnder; height-only: TerrainHeightOnly) (Game/worldgeometry_ray.cpp)
    float HeightUnderPointReference(const float* point, float* outNormal);   // NOT RETAIL: TerrainHeightUnder alone, the reference terrain_ray_test.cpp pins TerrainHeightOnly to
    int AddDecal(int param_2, int param_3, int param_4, int param_5, uint8_t param_6, uint32_t param_7, Graphics::MaterialInstance* material, bool param_9, float param_10, float param_11, float param_12, float param_13, float param_14, char param_15, int param_16, int param_17, bool param_19);   // 0x1400b0d80 (Player/tool_base_highlight.cpp)
    void TessellateDecal(int param_2, char param_3, int param_4, int param_5);   // 0x1400ace40 (Player/tool_base_highlight_tess.cpp)
    uint8_t TerrainMarch(float* param_2, float* param_3, float* param_4, float* param_5, float* param_6);   // 0x1400aa530 (Player/tool_base_ray.cpp)
    void StoreBatchMaterial(int index, Graphics::MaterialInstance* decal);   // 0x1400acd90 (Player/roadtool_calcpath.cpp)

    // EXT SEAM (NOT RETAIL, docs/extensions/handins/CREATEONDEMAND.md, DESIGN.md hook rule 5):
    // ext.create_on_demand's prefetch. Null by default and under the faithful profile. Run with each
    // material the terrain draws with as it takes it (BuildTerrain's ground and water materials, the
    // same and the batches' at the end of a loading Serialize / SerializeLegacyBatches, AddDecal's,
    // StoreBatchMaterial's):
    // the extension queues the first-use creates of its textures, ahead of the first frame that draws
    // the terrain, and keeps them out of its idle eviction.
    static void (*_extMaterialTaken)(const Graphics::MaterialInstance* material);
};

static_assert(sizeof(WorldGeometry) == 0x230, "WorldGeometry is MemAlloc(0x230)");
static_assert(offsetof(WorldGeometry, _fieldC4) == 0xC4, "WorldGeometry::_fieldC4");
static_assert(offsetof(WorldGeometry, _fieldCC) == 0xCC, "WorldGeometry::_fieldCC");
static_assert(offsetof(WorldGeometry, _fieldD0) == 0xD0, "WorldGeometry::_fieldD0");
static_assert(offsetof(WorldGeometry, _waterMaterialB) == 0x158, "WorldGeometry::_waterMaterialB");
static_assert(alignof(WorldGeometry) == 8, "qword-aligned");
static_assert(offsetof(WorldGeometry, _gridSize) == 0xC0, "+0xC0");
static_assert(offsetof(WorldGeometry, _chunkSize) == 0xC8, "+0xC8");
static_assert(offsetof(WorldGeometry, _chunkIndexStride) == 0xD4, "+0xD4");
static_assert(offsetof(WorldGeometry, _heights) == 0xD8, "+0xD8");
static_assert(offsetof(WorldGeometry, _dirtyChunks) == 0xE8, "+0xE8");
static_assert(offsetof(WorldGeometry, _dirtyChunks.count) == 0xF0, "+0xF0");
static_assert(offsetof(WorldGeometry, _terrainMaterial) == 0xF8, "+0xF8");
static_assert(offsetof(WorldGeometry, _vertexBuffer) == 0x100, "+0x100");
static_assert(offsetof(WorldGeometry, _indexBuffer) == 0x120, "+0x120");
static_assert(offsetof(WorldGeometry, _heightBytes) == 0x140, "+0x140");
static_assert(offsetof(WorldGeometry, _heightImage) == 0x148, "+0x148");
static_assert(offsetof(WorldGeometry, _waterMaterialA) == 0x150, "+0x150");
static_assert(offsetof(WorldGeometry, _waterVertexBuffer) == 0x160, "+0x160");
static_assert(offsetof(WorldGeometry, _waterIndexBuffer) == 0x180, "+0x180");
static_assert(offsetof(WorldGeometry, _contourVertices) == 0x1A0, "+0x1A0");
static_assert(offsetof(WorldGeometry, _contourIndices) == 0x1B0, "+0x1B0");
static_assert(offsetof(WorldGeometry, _contourRangesA) == 0x1C0, "+0x1C0");
static_assert(offsetof(WorldGeometry, _contourRangesB) == 0x1D0, "+0x1D0");
static_assert(offsetof(WorldGeometry, _contourOutlines) == 0x1E0, "+0x1E0");
static_assert(offsetof(WorldGeometry, _decals) == 0x1F0, "+0x1F0");
static_assert(offsetof(WorldGeometry, _roadBatches) == 0x200, "+0x200");
static_assert(offsetof(WorldGeometry, _batchMaterials) == 0x210, "+0x210");
static_assert(offsetof(WorldGeometry, _batchCounters) == 0x220, "+0x220");
static_assert(offsetof(WorldGeometry, _batchCounters) + offsetof(Collection::Array<int32_t>, _size) == 0x228, "+0x228");
