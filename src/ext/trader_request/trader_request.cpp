// src/ext/trader_request/trader_request.cpp -- NOT RETAIL. ext.trader_request (class C): the town's list of
// trader requests, the arrival's lookup and the requested visit's price. Standard C++ only. See
// trader_request.h and docs/extensions/handins/TRADERREQUEST.md.
#include "ext/trader_request/trader_request.h"

#include <cctype>
#include <limits>

namespace Ext {
namespace TraderRequest {

namespace {

std::vector<Request> g_requests;

int IndexOf(int32_t post) {
    for (std::size_t i = 0; i < g_requests.size(); ++i) {
        if (g_requests[i].post == post) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

}  // namespace

const std::vector<Request>& Requests() {
    return g_requests;
}

bool TraderIsValid(const std::string& trader) {
    if (trader.empty() || trader.size() > kMaxTraderBytes) {
        return false;
    }
    for (char c : trader) {
        if (static_cast<unsigned char>(c) < 0x20) {
            return false;
        }
    }
    return true;
}

bool RequestOf(int32_t post, Request& out) {
    const int i = IndexOf(post);
    if (i < 0) {
        return false;
    }
    out = g_requests[static_cast<std::size_t>(i)];
    return true;
}

bool SetRequest(int32_t post, const std::string& trader, uint8_t mode) {
    if (post < 0 || !TraderIsValid(trader) || (mode != kModeOnce && mode != kModeAlways)) {
        return false;
    }
    const int i = IndexOf(post);
    if (i >= 0) {
        Request& r = g_requests[static_cast<std::size_t>(i)];
        if (r.trader == trader && r.mode == mode) {
            return false;
        }
        r.trader = trader;
        r.mode = mode;
        return true;
    }
    if (g_requests.size() >= kMaxRequests) {
        return false;
    }
    Request r;
    r.post = post;
    r.trader = trader;
    r.mode = mode;
    g_requests.push_back(r);
    return true;
}

bool ClearRequest(int32_t post) {
    const int i = IndexOf(post);
    if (i < 0) {
        return false;
    }
    g_requests.erase(g_requests.begin() + i);
    return true;
}

void ReplaceRequests(const std::vector<Request>& requests) {
    g_requests = requests;
}

void ResetState() {
    g_requests.clear();
    PanelsForget();
}

int TakeArrival(int32_t post, const std::vector<std::string>& traders) {
    const int i = IndexOf(post);
    if (i < 0) {
        return -1;
    }
    const Request& r = g_requests[static_cast<std::size_t>(i)];
    int found = -1;
    for (std::size_t t = 0; t < traders.size(); ++t) {
        if (traders[t] == r.trader) {
            found = static_cast<int>(t);
            break;
        }
    }
    if (found >= 0 && r.mode == kModeOnce) {
        g_requests.erase(g_requests.begin() + i);
    }
    return found;
}

std::string ComboLabel(const std::string& displayName) {
    std::string name = displayName;
    while (!name.empty() && name.back() == ' ') {
        name.pop_back();
    }
    for (const char* suffix : { " merchant", " trader" }) {
        const std::string tail(suffix);
        if (name.size() <= tail.size()) {
            continue;
        }
        bool same = true;
        const std::size_t at = name.size() - tail.size();
        for (std::size_t i = 0; i < tail.size(); ++i) {
            if (std::tolower(static_cast<unsigned char>(name[at + i])) != tail[i]) {
                same = false;
                break;
            }
        }
        if (same) {
            name.erase(at);
            while (!name.empty() && name.back() == ' ') {
                name.pop_back();
            }
            break;
        }
    }
    if (name.empty()) {
        name = displayName;
    }
    return name + " (" + std::to_string(kPriceFactor) + "x)";
}

int32_t RequestedPrice(int32_t price) {
    if (price < 1) {
        return 1;
    }
    if (price > std::numeric_limits<int32_t>::max() / kPriceFactor) {
        return std::numeric_limits<int32_t>::max();
    }
    return price * kPriceFactor;
}

}  // namespace TraderRequest
}  // namespace Ext
