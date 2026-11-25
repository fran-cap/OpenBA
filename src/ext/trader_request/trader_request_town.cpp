// src/ext/trader_request/trader_request_town.cpp -- NOT RETAIL. ext.trader_request (class C): the game side.
// A post's key, the arrival (which trader docks at a post with a request), the demolished post and the
// version 1 record's ids. See trader_request.h and docs/extensions/handins/TRADERREQUEST.md.
#include "ext/trader_request/trader_request.h"

#include "Component/MapComponent.h"
#include "Component/TradeComponent.h"
#include "Component/TradeDescription.h"
#include "Entity/ComponentEntity.h"
#include "Entity/EntityList.h"
#include "Game/GameUpdateLoop.h"

#include <cstdio>

namespace Ext {
namespace TraderRequest {

PostKey KeyOfEntity(const ComponentEntity* entity) {
    PostKey key;
    const MapComponent* footprint = entity != nullptr ? entity->FindComponent<MapComponent>() : nullptr;
    if (footprint != nullptr) {
        key.tileX = footprint->tileX0;
        key.tileY = footprint->tileY0;
    }
    return key;
}

// The id is an index of the entity list's master array (`masterArray[e->_entityId] == e`), true of every
// entity when a save is read.
bool LegacyPostKey(int32_t entityId, PostKey& out) {
    GameUpdateLoop* game = GameUpdateLoop::_singleton;
    EntityList* list = game != nullptr ? game->m_entityList : nullptr;
    if (list == nullptr || entityId < 0 || entityId >= list->masterArray._size) {
        std::fprintf(stderr, "[TraderRequest] a saved request names entity %d, which this town does not have: "
                     "dropped\n", entityId);
        return false;
    }
    const ComponentEntity* entity = list->masterArray._data[entityId].Get();
    const PostKey key = KeyOfEntity(entity);
    if (entity == nullptr || entity->FindComponent<TradeComponent>() == nullptr || !PostKeyIsValid(key)) {
        std::fprintf(stderr, "[TraderRequest] a saved request names entity %d, which is not a trading post: "
                     "dropped\n", entityId);
        return false;
    }
    out = key;
    return true;
}

bool ArrivingSlow(const TradeComponent* post, const TradeDescription* desc, int& merchantIdx) {
    const PostKey key = KeyOfEntity(post->_entity.Get());
    Request request;
    if (!PostKeyIsValid(key) || !RequestOf(key, request)) {
        return false;
    }
    std::vector<std::string> traders;
    const int32_t count = desc->_merchantCount;
    traders.reserve(static_cast<std::size_t>(count > 0 ? count : 0));
    for (int32_t i = 0; i < count; ++i) {
        const char* name = desc->_merchants[i].name._data;
        traders.push_back(name != nullptr ? std::string(name) : std::string());
    }
    const int requested = TakeArrival(key, traders);
    if (requested < 0) {
        std::fprintf(stderr, "[TraderRequest] post (%d, %d) asks for \"%s\", which this game does not have: "
                     "retail's roll stands\n", key.tileX, key.tileY, request.trader.c_str());
        return false;
    }
    std::fprintf(stderr, "[TraderRequest] post (%d, %d): \"%s\" arrives as requested (%s), prices x%d\n",
                 key.tileX, key.tileY, request.trader.c_str(),
                 request.mode == kModeOnce ? "this visit" : "every visit", kPriceFactor);
    merchantIdx = requested;
    return true;
}

void PostRemovedSlow(const TradeComponent* post) {
    ClearRequest(KeyOfEntity(post->_entity.Get()));
}

}  // namespace TraderRequest
}  // namespace Ext
