// ============================================================================
// src/ext/trader_request/save_state.cpp -- ext.trader_request's per-town state in the save tail (class C)
// ============================================================================
// NOT RETAIL. docs/extensions/handins/TRADERREQUEST.md, DESIGN.md "Class C state: the contract".
//
// The state is the town's list of trader requests (trader_request.h): per trading post, the trader it asks
// for and whether the request is for the next visit or every visit. A post is stored by its entity id, a
// trader by its name key in the TradeDescription, never by index: mods change the trader list between
// sessions, and a name the game no longer has simply requests nothing.
//
// Wire format v1 (little-endian):
//   u32  count            0..kMaxRequests
//   count x {
//     i32  post           the trading post's entity id, >= 0
//     u32  mode           1 = the next visit, 2 = every visit
//     u32  length         1..kMaxTraderBytes
//     u8   trader[length] the trader's name key, bytes 0x20..0xFF, no NUL
//   }
// Decode accepts exactly what Encode writes and refuses the payload (the save is damaged) on a count the
// payload cannot hold, a negative post, a post twice, an unknown mode, an empty, over-long or
// control-character trader, bytes left over, or a cut-short payload. A refused payload changes nothing: the
// old list stays.
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

constexpr size_t kMinEntryBytes = 4 + 4 + 4 + 1;   // post + mode + length + a one-byte trader

void Encode(std::vector<uint8_t>& payload) {
    const std::vector<Request>& requests = Requests();
    SaveTail::Writer w(payload);
    w.U32(static_cast<uint32_t>(requests.size()));
    for (const Request& r : requests) {
        w.I32(r.post);
        w.U32(r.mode);
        w.String(r.trader);
    }
}

bool Decode(const uint8_t* bytes, size_t size, int32_t savedVersion) {
    if (savedVersion != 1) {   // Validate already refused a NEWER version
        return false;
    }
    SaveTail::Reader r(bytes, size);
    uint32_t count = 0;
    if (!r.U32(count) || count > kMaxRequests || count > r.left / kMinEntryBytes) {
        return false;
    }
    std::vector<Request> requests;
    for (uint32_t i = 0; i < count; ++i) {
        Request q;
        uint32_t mode = 0;
        if (!r.I32(q.post) || q.post < 0) {
            return false;
        }
        if (!r.U32(mode) || (mode != kModeOnce && mode != kModeAlways)) {
            return false;
        }
        q.mode = static_cast<uint8_t>(mode);
        if (!r.String(q.trader) || !TraderIsValid(q.trader)) {
            return false;
        }
        for (const Request& seen : requests) {
            if (seen.post == q.post) {
                return false;
            }
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
