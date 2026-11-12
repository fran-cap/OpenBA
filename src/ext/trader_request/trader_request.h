#pragma once
// ============================================================================
// src/ext/trader_request/trader_request.h -- ext.trader_request (class C, default ON)
// ============================================================================
// NOT RETAIL. docs/extensions/handins/TRADERREQUEST.md. A trading post can ask for one trader instead of
// taking whoever turns up; a requested trader's goods cost double.
//
// RETAIL. Which trader docks is a uniform roll over the TradeDescription's merchants, made when the boat
// arrives (TradeComponent::BuildMerchantManifest, 0x14005b580). The player can order goods from a trader
// (the Orders tab: at 1.25x) but cannot choose who comes.
//
// THE EXTENSION. Per trading post, a request: a trader and a mode (the next visit only, or every visit).
// When a boat docks at a post that has one, the requested trader is the one that arrived, and every line of
// that visit's manifest is priced at kPriceFactor times what retail would ask (after retail's own 1.25x for
// an ordered good). What the trader pays for the town's goods is unchanged: trade is barter, so doubling
// both sides would cancel. A "next visit" request is used up by the arrival. A post with no request is
// retail's roll and retail's prices.
//
// The doubled price is written into the manifest lines retail already serializes, so a visit in progress
// survives a save and a load with no state of ours.
//
// CLASS C. The requests are the town's own state: they travel in the row's record in the save tail (Codec;
// format version 1). A request names its post by the post's entity id (stable across a save and a load, as
// ext.tool_names and ext.family_tree name a citizen) and its trader by the trader's name key in the
// TradeDescription (TradeMerchant::name), never by index: mods change the list between sessions. A trader
// the game no longer has is never "arriving": the post gets retail's roll and the request stays.
//
// THE PANEL. The trading post window's Orders tab gets one row at its foot: "Request trader
// (price mult)", a combo of the traders ("Any trader" first) and a combo of the mode.
//
// The list and the codec are standard C++ only: tests/unit/ext_trader_request_test.cpp links
// trader_request.cpp and save_state.cpp on their own. The game side is trader_request_town.cpp (the arrival,
// the demolished post) and panel_row.cpp (the Orders tab's row).
// ============================================================================
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "ext/registry.h"   // Ext::Ext().trader_request (class C: the game's locked value)
#include "ui/EventId.h"     // UI::EventId (PanelEvent's event id)

namespace Ext { namespace SaveTail { struct StateCodec; } }

class TradeComponent;
class TradeDescription;
class TradeUI;

namespace Ext {
namespace TraderRequest {

constexpr const char* kId = "trader_request";
// What a requested trader's goods cost, as a multiple of retail's price for the same visit.
constexpr int32_t kPriceFactor = 2;

constexpr uint8_t kModeOnce = 1;     // the next visit only: the arrival uses the request up
constexpr uint8_t kModeAlways = 2;   // every visit, until the player sets the post back to "Any trader"

constexpr std::size_t kMaxRequests = 4096;   // a town never has this many trading posts; the decoder's limit
constexpr std::size_t kMaxTraderBytes = 255;

// ---- the list (the town's state) -------------------------------------------------------------------------

struct Request {
    int32_t post = -1;      // the trading post's entity id
    std::string trader;     // TradeMerchant::name, the trader's key in the post's TradeDescription
    uint8_t mode = kModeOnce;
};

// The live list, in the order the requests were first made. One request per post.
const std::vector<Request>& Requests();
// The post's request into `out`; false when it has none.
bool RequestOf(int32_t post, Request& out);
// Sets (or changes) the post's request. A negative post, an empty or over-long trader or an unknown mode is
// refused. Returns whether the list changed.
bool SetRequest(int32_t post, const std::string& trader, uint8_t mode);
// Removes the post's request. Returns whether the list changed.
bool ClearRequest(int32_t post);
// Replaces the whole list (the codec's decode).
void ReplaceRequests(const std::vector<Request>& requests);
// A new game, or the game ends: no requests, every panel row forgotten.
void ResetState();

// Whether a trader name can be stored: 1..kMaxTraderBytes bytes, none below 0x20.
bool TraderIsValid(const std::string& trader);

// A boat docks at `post`, whose description lists `traders` (TradeMerchant::name, in order). Returns the
// index of the requested trader, or -1 when the post has no request or its trader is not in the list. A
// kModeOnce request that is honoured is removed.
int TakeArrival(int32_t post, const std::vector<std::string>& traders);

// A trader's line in the row's combo: its display name without a trailing " Merchant" or " Trader" (compared
// without case; "General Goods Merchant" is cut off in the combo), then the price multiple: "Food (2x)".
// A name that is only that word, or has none, is kept whole.
std::string ComboLabel(const std::string& displayName);

// A manifest line's price for a requested visit: `price` (retail's, after its order markup) times
// kPriceFactor, never below 1 and never past INT32_MAX.
int32_t RequestedPrice(int32_t price);

// ---- the game side (trader_request_town.cpp, panel_row.cpp; not in the unit build) -----------------------

// TradeComponent::BuildMerchantManifest, right after retail's roll: the trader that arrives at this post.
// Returns true when the post's request was honoured (`merchantIdx` is then the requested trader and the
// visit's prices are RequestedPrice); false leaves `merchantIdx` alone.
bool ArrivingSlow(const TradeComponent* post, const TradeDescription* desc, int& merchantIdx);
// TradeComponent::Teardown (the post is demolished): its request goes with it.
void PostRemovedSlow(const TradeComponent* post);

// The Orders tab's row (panel_row.cpp). One EXT SEAM line per site in tradeui.cpp.
void PanelOpenedSlow(TradeUI* panel);   // OnCreated, at its end: adds the row
void PanelLayoutSlow(TradeUI* panel);   // OnUpdateLayout: follows the post's request (an arrival may use it up)
// OnElementEvent: true when the event was the row's own
bool PanelEventSlow(TradeUI* panel, UI::EventId msg, int32_t id);
void PanelsForget();                    // the game ends: forget every row

// The seams: each tests the row's locked value once.
inline bool Active() {
    return Ext::Ext().trader_request;
}
inline bool Arriving(const TradeComponent* post, const TradeDescription* desc, int& merchantIdx) {
    return Active() && ArrivingSlow(post, desc, merchantIdx);
}
inline void PostRemoved(const TradeComponent* post) {
    if (Active()) {
        PostRemovedSlow(post);
    }
}
inline void PanelOpened(TradeUI* panel) {
    if (Active()) {
        PanelOpenedSlow(panel);
    }
}
inline void PanelLayout(TradeUI* panel) {
    if (Active()) {
        PanelLayoutSlow(panel);
    }
}
inline bool PanelEvent(TradeUI* panel, UI::EventId msg, int32_t id) {
    return Active() && PanelEventSlow(panel, msg, id);
}

// The save-tail codec for this extension's per-town list (save_state.cpp; registered in state_codecs.cpp).
const SaveTail::StateCodec& Codec();

}  // namespace TraderRequest
}  // namespace Ext
