// ============================================================================
// src/ext/map_overlays/save_state.cpp -- ext.map_overlays's history in the save tail (class C)
// ============================================================================
// NOT RETAIL. docs/extensions/handins/MAPOVERLAYS.md §3, DESIGN.md "Class C state: the contract".
//
// The state is the town's History (overlay_core.h): what was produced and consumed at which tile in each of
// the last twelve months, by resource template path, and the footsteps per 2x2-tile cell in each of the last
// four seasons. Town() lives here, beside its codec, so the unit build links the codec table without the game.
//
// Wire format v1 (little-endian; History::Encode / Decode, overlay_core.cpp):
//   u32  mapSize                 0..kMaxMapSize (0: a history that never saw a map; nothing follows but
//                                empty lists)
//   i32  now                     the month stamp of the last AdvanceTo (kNoStamp: none)
//   i32  bucketStamp[12]         the month each bucket holds
//   i32  trafficStamp[4]         the season each traffic bucket holds
//   u32  resourceCount           0..kMaxResources
//   resourceCount x { u16 len (1..kMaxPathBytes)  u8 path[len] }      no path twice (compared without case)
//   u32  siteCount
//   siteCount x {                sorted by (z, x), no tile twice, x and z < mapSize
//     u16 x  u16 z  u16 cellCount
//     cellCount x { u16 resource (< resourceCount)  u8 flow (0 produced, 1 consumed)  u32 counts[12] }
//   }                            no (resource, flow) twice at a tile
//   u32  trafficCells            == ceil(mapSize / 2)
//   trafficCells^2 x { u16 count[4] }    row-major, z * trafficCells + x
// Decode accepts exactly what Encode writes and refuses a cut payload, trailing bytes, a map size above
// kMaxMapSize, a count past the payload, or an index outside the table. A refused payload is NOT fatal to the
// load (MAPOVERLAYS.md §3): the history is emptied, one line goes to stderr, and the game plays on.
//
// Standard C++ only: tests/unit/ext_map_overlays_test.cpp links this file with overlay_core.cpp.
// ============================================================================
#include "ext/map_overlays/map_overlays.h"

#include <cstdio>

#include "ext/savetail.h"

namespace Ext {
namespace MapOverlays {

History& Town() {
    static History history;
    return history;
}

namespace {

void Encode(std::vector<uint8_t>& payload) {
    Town().Encode(payload);
}

bool Decode(const uint8_t* bytes, size_t size, int32_t savedVersion) {
    // Validate already refused a NEWER version; an older one would migrate here.
    if (savedVersion == kWireVersion && Town().Decode(bytes, size)) {
        return true;
    }
    Town().Clear();
    fprintf(stderr, "[MapOverlays] the saved history (v%d, %zu bytes) is damaged: the views start empty\n",
            static_cast<int>(savedVersion), size);
    return true;
}

void Reset() {
    Town().SetMapSize(0);
    Town().Clear();
}

}  // namespace

const SaveTail::StateCodec& Codec() {
    static const SaveTail::StateCodec c = { kId, Encode, Decode, Reset };
    return c;
}

}  // namespace MapOverlays
}  // namespace Ext
