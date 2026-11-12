// src/ext/trader_request/trader_request_town.cpp -- NOT RETAIL. ext.trader_request (class C): the game side.
// The arrival (which trader docks at a post with a request) and the demolished post. See trader_request.h
// and docs/extensions/handins/TRADERREQUEST.md.
#include "ext/trader_request/trader_request.h"

#include "Component/TradeComponent.h"
#include "Component/TradeDescription.h"
#include "Entity/ComponentEntity.h"

#include <cstdio>

namespace Ext {
namespace TraderRequest {

namespace {

int32_t PostId(const TradeComponent* post) {
    const ComponentEntity* entity = post->_entity.Get();
    return entity != nullptr ? entity->_entityId : -1;
}

}  // namespace

bool ArrivingSlow(const TradeComponent* post, const TradeDescription* desc, int& merchantIdx) {
    const int32_t id = PostId(post);
    Request request;
    if (id < 0 || !RequestOf(id, request)) {
        return false;
    }
    std::vector<std::string> traders;
    const int32_t count = desc->_merchantCount;
    traders.reserve(static_cast<std::size_t>(count > 0 ? count : 0));
    for (int32_t i = 0; i < count; ++i) {
        const char* name = desc->_merchants[i].name._data;
        traders.push_back(name != nullptr ? std::string(name) : std::string());
    }
    const int requested = TakeArrival(id, traders);
    if (requested < 0) {
        std::fprintf(stderr, "[TraderRequest] post %d asks for \"%s\", which this game does not have: "
                     "retail's roll stands\n", id, request.trader.c_str());
        return false;
    }
    std::fprintf(stderr, "[TraderRequest] post %d: \"%s\" arrives as requested (%s), prices x%d\n", id,
                 request.trader.c_str(), request.mode == kModeOnce ? "this visit" : "every visit",
                 kPriceFactor);
    merchantIdx = requested;
    return true;
}

void PostRemovedSlow(const TradeComponent* post) {
    ClearRequest(PostId(post));
}

}  // namespace TraderRequest
}  // namespace Ext
