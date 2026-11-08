// src/game/Game/gamegrid_serialize.cpp
//
// Game/map-cell-grid serializer. Called during SAVE/LOAD by the master
// GameUpdateLoop serializer (0x140095a90) on the object at GameUpdateLoop+0xF8
// ("m_game"). Ported from Application-x64.exe 0x14009f100 (Ghidra's stale
// auto-name for this function is "CreateNaturalResource_impl_f100" -- ignore
// that label, it has nothing to do with CreateNaturalResource; the function
// actually serializes the map cell grid).
//
// Shape (verified against the disassembly, not just the decompiler's pseudocode --
// several offsets the decompiler reported turned out to be off by 0x10 due to
// Ghidra mistyping a couple of locals; the asm below is what this file follows):
//   - Serializer ctor (stream, version=4)
//   - int at self+0x00                                  (grid width)
//   - int cell-array count at self+0x88 (header @ self+0x80: +0x00 base ptr,
//     +0x08 count, +0x0c capacity), LOAD-only resize via 0x14001aa90
//   - bulk cell-array serialize via 0x14001ac10 (stride 0x30 bytes/cell)
//   - 9 shorts at self+0x90
//   - LOAD-only fixup block:
//       self+0xa2 = 1
//       version<2 back-fill of per-cell byte elem+0x29 via the CreateNaturalResource
//         terrain-check/classify helper pair (0x14009cb00 / 0x14009e2c0)
//       per-cell flag normalize pass (elem+0x28 sentinel fix, elem+0x2a AVL lookup
//         against the tree at self+0xe8)
//       Map_Invalidate(self)                              (0x14009c480, 1 arg)
//       region system build(self)                      (0x14009e8c0, 1 arg: Pathfinder_Seed +
//         Region_Seed, Game/exe_bodies.h)
//       TradeWork_impl_f370(*(gameWorld+0x110), self)      (0x1400af370)
//
// Per-cell element layout (0x30 bytes), offsets confirmed identical between the
// resize helper and the bulk-serialize helper:
//   +0x00 InternalBasePtr A (8B, cleared to null if not derived from ComponentEntity)
//   +0x08 InternalBasePtr B (8B, same clearing rule)
//   +0x10 ExternalPtr resource/description ref (8B, string-table serialize)
//   +0x18 opaque uint32 (4B, NOT individually serialized -- copied verbatim on resize)
//   +0x1c opaque 9-byte raw block (sized-raw serialize, spans 0x1c..0x24)
//   +0x26 ushort (2B)
//   +0x28 flags byte   (terrain-fill flag; the outer fixup's sentinel/AVL pass)
//   +0x29 byte         (sub-terrain classification written by the version<2 back-fill)
//   +0x2a byte         (sentinel: 0xff = "unset")
//   +0x2b, +0x2c bytes
//   +0x2d signed char
//
#include "object/TypeInfo.h"
#include "Component/still_terrain.h"   // STILLTERRAIN: StillTerrain::Bump (the terrain epoch)
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Game/Map.h"              // B4 GAME-a: Map::Serialize, MapCell
#include "Game/WorldGeometry.h"    // WorldGeometry::RebuildRoadMesh (0x1400af370)
#include "Action/PathAvl.h"        // AvlNode28, the +0xE8 tree's node (B4 P2 GAME-1)
#include "Game/terrain_sidecar.h"
#include "collection/Collection.h"
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch B
#include "io/Stream.h"      // ENDGAME B.3.4 callers batch H: IO::Stream::GetDirection
#include <cstdint>
#include <cstdio>
#include <new>
#include "system/Error.h"   // NOT RETAIL (review F-026): a damaged map record is retail's fatal error box
#include "object/Internal.h"   // ENDGAME B.3.4 caller batch G
#include "Entity/ComponentEntity.h"
#include "Game/exe_bodies.h"
#include "Action/exe_bodies.h"      // ActCNR_CheckTerrain_cb00, Pf_RoadMeshRebuild_f370
#include "Component/exe_bodies.h"   // CreateNaturalResource_helper_e2c0
#include "Component/ComponentDescription.h"   // B8 RT2: ::Object::SerializeExternal<ComponentDescription> (the downcast needs it complete)
#include "Game/cell_stamp.h"   // AMBMEMO: CellStamp::NoteWrite after a cell store
#include "cell_trace.h"   // red square: BANISHED_CELL_DUMP (src/diag/cell_trace.h)
#include "ext/registry.h"   // Ext::Fix().terrain_resort
#include "ext/terrain_resort/terrain_resort.h"   // fix.terrain_resort (docs/extensions/handins/TERRAINRESORT.md)

// ============================================================
// Functions ported elsewhere, called by name through their declaring headers:
//   0x14009cb00 ActCNR_CheckTerrain_cb00            terrain-occupied check (Action/exe_bodies.h)
//   0x14009e2c0 CreateNaturalResource_helper_e2c0   per-cell terrain classify (Component/exe_bodies.h)
//   0x140002340 Reimpl_ExternalPtr_Serialize_StringTable  ExternalPtr via the string table (Game/exe_bodies.h)
//   0x1400af370 Pf_RoadMeshRebuild_f370             road mesh rebuild (Action/exe_bodies.h)
//   0x14009e8c0 Pathfinder_Seed + Region_Seed  the region system build, two halves (Game/exe_bodies.h)
// ============================================================

// ============================================================
// GameGrid_ResizeCellArray_aa90 (0x14001aa90)
//
// Resizes the cell array to `newCount` elements (stride 0x30 bytes).
// `header` is the 0x10-byte array-header embedded in the Game object:
//   +0x00 base ptr (int64), +0x08 count (int32), +0x0c capacity (int32).
// Grow-only allocation: capacity only ever increases, x3 on a "+1" grow step
// (a classic amortized-growth heuristic), verbatim from the disassembly.
//
// ENDGAME B.3.2 step 2: the body WAS a hand-written copy of
// `rtcoll::SetSizeCore` (src/engine/collection/array.cpp) and is now one
// `Collection::Array<>::SetSize` over the row below. Compared arm for arm:
// exact-equal early return, `_size` stored before the capacity test,
// treble-on-single-append-from-non-empty, copy the preserved prefix only when
// the block moved, init the new tail, no element destroy, free the old block
// on a realloc (both go through the null-tolerant 0x1800c7ad0). The one
// difference: the allocation now goes through the LOCKING static `MemAlloc`
// (0x1800c7a50) rather than `fn_MemAllocStatic` — the same export, reached
// through rtcoll's own resolver.
// ============================================================
namespace {

// One map cell, stride 0x30, align 8. Both arms write +0x00..+0x24 and
// +0x26..+0x2d: byte +0x25 and bytes +0x2e..+0x2f are touched by NEITHER
// (the `assign unwritten` pair below names the trailing hole; +0x25 is the
// second, non-contiguous one). Partial copy, so byteCopy=false; trivially
// destructible.
//
// B4 P2 GAME-1: the element is MapCell (Game/Map.h), reached through
// Collection.h's element adapters; every field by name. The tree's merged
// stores (one dword over +0x26..+0x29, one dword over +0x2A..+0x2D; the qword
// over +0x1C..+0x23 stays one qword) are the named fields' stores, in address order (H7): the
// same bytes, +0x25 and +0x2E..+0x2F still unwritten.
void CopyMapCell(MapCell* d, const MapCell* s) {
    d->blocker = s->blocker;            // +0x00
    d->occupant = s->occupant;          // +0x08
    d->lastGrownDescription = s->lastGrownDescription;      // +0x10
    d->regionId = s->regionId;          // +0x18
    d->influenceCounts = s->influenceCounts;      // +0x1C..+0x24, the 9-byte block (retail: one qword and one byte)

    d->zone = s->zone;                  // +0x26

    d->footprintCount = s->footprintCount;   // +0x28
    d->terrainType = s->terrainType;    // +0x29
    d->roadType = s->roadType;      // +0x2A
    d->roadAlpha = s->roadAlpha;      // +0x2B
    d->soilQuality = s->soilQuality;      // +0x2C
    d->floorCount = s->floorCount;      // +0x2D
}
// Brand-new element: zero the refs/raw block, sentinel the flag bytes.
void ZeroMapCell(MapCell* d) {
    d->blocker = nullptr;               // +0x00
    d->occupant = nullptr;              // +0x08
    d->lastGrownDescription = nullptr;            // +0x10
    d->regionId = -1;                   // +0x18 0xffffffff
    d->influenceCounts = {};                 // +0x1C..+0x24 zero (retail: one qword 0 and one byte 0)
    d->zone = 0;                        // +0x26..+0x29 zero
    d->footprintCount = 0;
    d->terrainType = PathType{};
    d->roadType = MapCell::kNoRoadBatch;   // +0x2A = 0xff
    d->roadAlpha = 0;                  // +0x2B..+0x2D = 0
    d->soilQuality = 0;
    d->floorCount = 0;
}
void AssignMapCell(uint8_t* dst, const uint8_t* src) {
    rtcoll::AssignAs<MapCell, &CopyMapCell>(dst, src);
}
void InitMapCell(uint8_t* dst) {
    rtcoll::InitAs<MapCell, &ZeroMapCell>(dst);
}
}  // namespace

// stays explicit: `init` is a patterned fill (0xffffffff at +0x18, 0xff at
// +0x2a), `assign` is partial (byteCopy=false), and both arms carry unwritten
// extents — a probe contract, not a trait. Map::cells (Game/Map.h) is the
// Collection::Array over this row.
extern const rtcoll::ArraySpec kSpec_MapCell = {           // exe 0x14001aa90
    "Array<MapCell>", 0x30, 8,
    &AssignMapCell, &InitMapCell, nullptr, false,
    /*assign unwritten*/ 0x2e, 0x30,   // plus the single byte +0x25
    /*init   unwritten*/ 0x2e, 0x30,   // plus the single byte +0x25
    /*seed zero       */ 0,    0 };

// ============================================================
// GameGrid_SerializeCellArray_ac10 (0x14001ac10)
//
// Bulk-serializes `count` cells starting at `cells` (stride 0x30 bytes).
// Per element: two InternalBasePtr refs (each nulled if not derived from
// ComponentEntity after load), one string-table ExternalPtr ref, a 9-byte raw
// block, a ushort, five bytes, and a signed char.
// ============================================================
static void GameGrid_SerializeCellArray_ac10(IO::Serializer* serializer, int32_t count, MapCell* cells) {
    if (count <= 0) return;

    MapCell* elem = cells;
    for (int32_t i = 0; i < count; ++i) {
        elem->blocker.Object::InternalBasePtr::Serialize(*serializer);
        // NOT RETAIL (F-143): null and type tests on the held Internal*, before any
        // downcast to ComponentEntity*; the answer is the same.
        if (!elem->blocker.IsNull() && !::Object::IsDerivedFromNullSafe(elem->blocker, ComponentEntity::TypeId())) {
            elem->blocker = nullptr;
            CellStamp::NoteWrite(elem);   // [AMBMEMO] cell write clock
        }

        elem->occupant.Object::InternalBasePtr::Serialize(*serializer);
        // NOT RETAIL (F-143): as for the blocker above.
        if (!elem->occupant.IsNull() && !::Object::IsDerivedFromNullSafe(elem->occupant, ComponentEntity::TypeId())) {
            elem->occupant = nullptr;
            CellStamp::NoteWrite(elem);   // [AMBMEMO] cell write clock
        }

        ::Object::SerializeExternal<ComponentDescription>(*serializer, elem->lastGrownDescription);

        serializer->Serialize(9, elem->influenceCounts.data(), nullptr);

        serializer->Serialize(elem->zone, nullptr);

        int version = serializer->GetVersion();
        if (version < 3) {
            // Retail reads/writes a throwaway ushort here for old save versions
            // (a field that no longer exists past version 3); we mirror that
            // with a scratch local so the stream position stays in sync.
            uint16_t scratch = 0;
            serializer->Serialize(scratch, nullptr);
        }

        serializer->Serialize(elem->footprintCount, nullptr);
        serializer->Serialize(reinterpret_cast<uint8_t&>(elem->terrainType), nullptr);   // the uchar door
        serializer->Serialize(elem->roadType, nullptr);
        serializer->Serialize(elem->roadAlpha, nullptr);
        serializer->Serialize(elem->soilQuality, nullptr);
        serializer->Serialize(elem->floorCount, nullptr);

        ++elem;
    }
}

// ============================================================
// Map::Serialize (0x14009f100) -- top-level entry point.
// ============================================================
void Map::Serialize(IO::Stream& stream) {
    IO::Serializer serializer(stream, 4);

    serializer.Serialize(size, nullptr);

    // The cell Array at +0x80: its count, then (load) SetSize 0x14001aa90.
    int32_t count = cells.GetSize();
    serializer.Serialize(count, nullptr);

    bool isSaving = static_cast<int>(stream.GetDirection()) != 0;   // IO::Stream::GetDirection, retail 0x180006200
    // NOT RETAIL (review F-026): a map record whose cell array does not cover its edge
    // (Map::CellsCoverSize) is refused here, before the cells are allocated, through retail's
    // own fatal error box (System::Error::SetText(.., true) + Display: the box, then exit), as
    // gameupdateloop_serialize.cpp refuses a save it cannot load. The load cannot continue: the
    // game's cell walks, cell indexes and divisions all take `size` as the edge.
    if (!isSaving && !CellsCoverSize(size, count)) {
        fprintf(stderr, "[Map::Serialize] FATAL: map edge %d, %d cells\n", size, count);
        fflush(stderr);
        System::Error::SetText("This save's map is damaged and cannot be loaded.", true);
        System::Error::Display();   // the box; _stopApplication then exits the process
        return;
    }
    if (!isSaving) {
        cells.SetSize(count);   // 0x14001aa90
    }

    GameGrid_SerializeCellArray_ac10(&serializer, count, cells.GetData());
    if (!isSaving) StillTerrain::Bump();   // [STILLTERRAIN] size, the cell array and every floorCount were streamed in

    // 9 shorts at +0x90.
    serializer.Serialize(9, influenceTotals, nullptr);

    if (!isSaving) {
        // ---- LOAD-only fixup block ----
        zonesStale = true;

        int version = serializer.GetVersion();
        CellTrace::DumpCells(this, "raw", version);   // the cells as saved, before the fix-up

        if (version < 2) {
            int32_t width = size;
            for (int32_t i = 0; i < width * width; ++i) {
                MapCell* e = &cells.GetData()[i];
                if (e->blocker == nullptr &&
                    e->occupant == nullptr) {
                    bool occupied = CheckCellTerrain(i) != 0;   // 0x14009cb00
                    if (!occupied) {
                        int cls = ClassifyTerrain(i);   // 0x14009e2c0
                        e->terrainType = static_cast<PathType>(cls);
                    }
                }
                // Faithful to retail: the width*width bound is re-read from
                // +0x00 every iteration (loop-invariant unless a helper
                // above mutates the grid width, which none do here).
                width = size;
            }
        }

        // Per-cell flag normalize pass (runs regardless of version).
        {
            int32_t width = size;
            for (int32_t i = 0; i < width * width; ++i) {
                MapCell* e = &cells.GetData()[i];

                if (e->footprintCount == 0xff) {
                    e->footprintCount = 0;
                }

                if (e->footprintCount == 0) {
                    uint8_t mask = static_cast<uint8_t>(Bits(PathBitOf(e->terrainType)));
                    if ((mask & Bits(PathBits::Fast)) == 0 && (mask & Bits(PathBits::Faster)) == 0 &&
                        e->roadType == MapCell::kNoRoadBatch) {
                        // Walk the AVL tree rooted at +0xe8 for key == i (the
                        // 0x28 AvlNode28 of Action/PathAvl.h: key +0, left +0x10,
                        // right +0x18).
                        const AvlNode28* node = pendingRoadCells.root;
                        while (node != nullptr) {
                            int32_t key = node->key;
                            if (i < key) {
                                node = node->left;
                            } else if (key >= i) {
                                e->footprintCount = 1;
                                break;
                            } else {
                                node = node->right;
                            }
                        }
                    } else {
                        e->footprintCount = 1;
                    }
                }

                width = size;
            }
        }

        // FIX terrain_resort (NOT RETAIL, docs/extensions/handins/TERRAINRESORT.md): a save holds the
        // stale "steep" cells retail's flattening left; heal them before the zones are built.
        if (Ext::Fix().terrain_resort) {
            Ext::TerrainResort::HealAllOnLoad(this);
        }
        CellTrace::DumpCells(this, "fixed", version);
        TerrainSidecar::Invalidate();   // [terrain sidecar] the whole array was just streamed in
        CellStamp::Invalidate();          // [AMBMEMO] the whole array was just streamed in
        // 0x14009c480 zone refresh. Retail's call (0x14009f29e) sets only RCX = this;
        // the body reads no other argument register: RDX / R8D are written before any
        // read (0x14009c4bd, 0x14009c520/524) and R9 never, and the flood fill it calls
        // (0x14009c170) spills R9D to a home slot that is rewritten before it is read
        // (0x14009c365 / 0x14009c3e8) and overwrites R9D at 0x14009c26f. The body takes
        // this one argument.
        FillZones();   // 0x14009c480

        // 0x14009f2a6: the region system build 0x14009e8c0 (RCX = this; it reads only the
        // Map, dim = +0x00). The tree ports it in two halves (Game/exe_bodies.h): the
        // pathfinder thread at +0x68, then the region graph (+0x70, flushed: every cell's
        // regionId) and the region-pair cache (+0x78). Retail builds them here, after the
        // entity list (0x1400c44a0 runs before this serializer), so the cell blockers the
        // flag bytes and the flush read are the save's.
        Pathfinder_Seed(this, size);   // 0x14009e8c0-0x14009eb16
        Region_Seed(this, size);       // 0x14009eb18-0x14009ebd9

        if (GameUpdateLoop::_singleton != nullptr) {
            GameUpdateLoop* gameWorld = GameUpdateLoop::_singleton;
            WorldGeometry* worldGeometry = gameWorld->m_worldGeometry;
            worldGeometry->RebuildRoadMesh(this);   // 0x1400af370
        }
    }
}
