// ============================================================================
// src/ext/trader_request/save_state.cpp -- ext.trader_request's per-town state in the save tail (class C)
// ============================================================================
// NOT RETAIL. docs/extensions/handins/TRADERREQUEST.md, DESIGN.md "Class C state: the contract".
//
// The state is the town's list of trader requests (trader_request.h): per trading post, the trader it asks
// for and whether the request is for the next visit or every visit. A post is stored by the corner tile of
// its footprint (trader_request.h, "NOT THE ENTITY ID"), a trader by its name key in the TradeDescription,
// never by index: mods change the trader list between sessions, and a name the game no longer has simply
// requests nothing.
//
// Wire format v2 (little-endian), the one Encode writes:
//   u32  count            0..kMaxRequests
//   count x {
//     i32  tileX, tileY   the trading post's footprint corner, each >= 0
//     u32  mode           1 = the next visit, 2 = every visit
//     u32  length         1..kMaxTraderBytes
//     u8   trader[length] the trader's name key, bytes 0x20..0xFF, no NUL
//   }
// Wire format v1, still read: as v2 with one `i32 post` (the post's entity id, >= 0) in place of the tile.
// Each id is turned into the post's tile as it is read (LegacyPostKey: the ids are the ones the save's
// entities carry at load). A request whose id names no trading post, or a post an earlier request already
// named, is dropped: the id had moved before the save was made, and the request with it was already lost.
//
// Decode refuses the payload (the save is damaged) on a count the payload cannot hold, a negative tile or
// post, a post twice, an unknown mode, an empty, over-long or control-character trader, bytes left over, or
// a cut-short payload. A refused payload changes nothing: the old list stays.
//
// Standard C++ only: tests/unit/ext_trader_request_test.cpp links this file with trader_request.cpp and
// savetail.cpp.
// ============================================================================

#include "ext/trader_request/trader_request.h"

#include "ext/savetail.h"

#include <string>
#include <vector>

namespace Ext {
namespace TraderRequest {
namespace {

constexpr int32_t kLegacyVersion = 1;              // a post by its entity id
constexpr int32_t kTileVersion = 2;                // a post by its footprint corner
constexpr size_t kMinLegacyBytes = 4 + 4 + 4 + 1;      // post + mode + length + a one-byte trader
constexpr size_t kMinEntryBytes = 4 + 4 + 4 + 4 + 1;   // tile x, y + mode + length + a one-byte trader

void Encode(std::vector<uint8_t>& payload) {
    const std::vector<Request>& requests = Requests();
    SaveTail::Writer w(payload);
    w.U32(static_cast<uint32_t>(requests.size()));
    for (const Request& r : requests) {
        w.I32(r.post.tileX);
        w.I32(r.post.tileY);
        w.U32(r.mode);
        w.String(r.trader);
    }
}

bool Decode(const uint8_t* bytes, size_t size, int32_t savedVersion) {
    if (savedVersion != kLegacyVersion && savedVersion != kTileVersion) {   // Validate already refused a NEWER version
        return false;
    }
    const bool legacy = savedVersion == kLegacyVersion;
    SaveTail::Reader r(bytes, size);
    uint32_t count = 0;
    if (!r.U32(count) || count > kMaxRequests ||
        count > r.left / (legacy ? kMinLegacyBytes : kMinEntryBytes)) {
        return false;
    }
    std::vector<Request> requests;
    std::vector<int32_t> legacyPosts;   // version 1: the ids read so far (a post twice is damage)
    for (uint32_t i = 0; i < count; ++i) {
        Request q;
        uint32_t mode = 0;
        int32_t legacyPost = -1;
        if (legacy) {
            if (!r.I32(legacyPost) || legacyPost < 0) {
                return false;
            }
            for (int32_t seen : legacyPosts) {
                if (seen == legacyPost) {
                    return false;
                }
            }
            legacyPosts.push_back(legacyPost);
        } else if (!r.I32(q.post.tileX) || !r.I32(q.post.tileY) || !PostKeyIsValid(q.post)) {
            return false;
        }
        if (!r.U32(mode) || (mode != kModeOnce && mode != kModeAlways)) {
            return false;
        }
        q.mode = static_cast<uint8_t>(mode);
        if (!r.String(q.trader) || !TraderIsValid(q.trader)) {
            return false;
        }
        if (legacy && !LegacyPostKey(legacyPost, q.post)) {
            continue;   // the id names no trading post: dropped
        }
        bool twice = false;
        for (const Request& seen : requests) {
            if (seen.post == q.post) {
                twice = true;
                break;
            }
        }
        if (twice) {
            if (legacy) {
                continue;   // two ids, one post (cannot happen in a live list): the first stands
            }
            return false;
        }
        requests.push_back(q);
    }
    if (!r.AtEnd()) {
        return false;
    }
    ReplaceRequests(requests);
    return true;
}

void Reset() {
    ResetState();
}

}  // namespace

const SaveTail::StateCodec& Codec() {
    static const SaveTail::StateCodec c = { kId, Encode, Decode, Reset };
    return c;
}

}  // namespace TraderRequest
}  // namespace Ext
