// ============================================================================
// src/ext/leaf_lod/leaf_lod_build.cpp -- ext.leaf_lod's canopy reduction (leaf_lod_build.h)
// ============================================================================
// NOT RETAIL.
// ============================================================================
#include "ext/leaf_lod/leaf_lod_build.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <numeric>

namespace Ext {
namespace LeafLod {

namespace {

int16_t CornerAt(const uint8_t* vertices, int32_t vertex, int which) {
    int16_t v;
    std::memcpy(&v, vertices + static_cast<size_t>(vertex) * kStride + kCornerAt + which * 2, sizeof(v));
    return v;
}

int32_t Find(std::vector<int32_t>& parent, int32_t v) {
    while (parent[v] != v) {
        parent[v] = parent[parent[v]];
        v = parent[v];
    }
    return v;
}

// The two sets joined under the lower root, so a card's root is its lowest vertex.
void Join(std::vector<int32_t>& parent, int32_t a, int32_t b) {
    const int32_t ra = Find(parent, a);
    const int32_t rb = Find(parent, b);
    const int32_t lo = std::min(ra, rb);
    parent[ra] = lo;
    parent[rb] = lo;
}

}  // namespace

namespace {

constexpr int   kDirections = 200;     // outline directions, spread evenly over the sphere
constexpr float kCrowding   = 0.02f;   // the weight of a card's distance to its nearest neighbour
constexpr int   kMaxOrdered = 96;      // cards per list the order is worked out for

}  // namespace

Order KeepOrder(const std::vector<CardSite>& cards, float radius) {
    const int n = static_cast<int>(cards.size());
    Order out;
    out.order.resize(static_cast<size_t>(n));
    std::iota(out.order.begin(), out.order.end(), 0);
    out.giveUp.assign(static_cast<size_t>(std::max(n - 1, 0)), 3.0e38f);
    if (n < 3 || n > kMaxOrdered) return out;

    // the directions (a Fibonacci sphere) and their weights: 1 straight up, 0.7 level, 0.1 straight down
    // (the camera looks down on a crown: its top and sides are its outline, its underside is hidden)
    std::vector<float> reach(static_cast<size_t>(n) * kDirections);   // card x direction: centre . direction
    std::vector<float> weight(kDirections);
    std::vector<float> full(kDirections, -3.0e38f);
    float extent = 0.0f;
    for (int j = 0; j < kDirections; ++j) {
        const float z = 1.0f - 2.0f * (static_cast<float>(j) + 0.5f) / static_cast<float>(kDirections);
        const float r = std::sqrt(std::max(0.0f, 1.0f - z * z));
        const float a = static_cast<float>(j) * 2.3999632f;   // the golden angle
        const float d[3] = {r * std::cos(a), r * std::sin(a), z};
        weight[j] = z < 0.0f ? 0.1f + 0.6f * (z + 1.0f) : 0.7f + 0.3f * z;
        float low = 3.0e38f;
        for (int i = 0; i < n; ++i) {
            const float* c = cards[i].centre;
            const float h = c[0] * d[0] + c[1] * d[1] + c[2] * d[2];
            reach[static_cast<size_t>(i) * kDirections + j] = h;
            full[j] = std::max(full[j], h);
            low = std::min(low, h);
        }
        extent += full[j] - low;
    }
    // the crown's size: its mean width over the directions, a card's radius past each end
    const float size = std::max(extent / static_cast<float>(kDirections) + 2.0f * radius, 1.0e-6f);

    std::vector<int32_t> alive = out.order;
    std::vector<int32_t> removed;
    std::vector<float> first(kDirections);    // the farthest card left in each direction...
    std::vector<float> second(kDirections);   // ...and the next
    std::vector<int32_t> holder(kDirections);
    while (alive.size() > 1) {
        for (int j = 0; j < kDirections; ++j) {
            first[j] = -3.0e38f;
            second[j] = -3.0e38f;
            holder[j] = -1;
            for (int32_t i : alive) {
                const float h = reach[static_cast<size_t>(i) * kDirections + j];
                if (h > first[j]) {
                    second[j] = first[j];
                    first[j] = h;
                    holder[j] = i;
                } else if (h > second[j]) {
                    second[j] = h;
                }
            }
        }
        size_t best = 0;
        float bestCost = 3.0e38f;
        float bestGiven = 0.0f;
        for (size_t k = 0; k < alive.size(); ++k) {
            const int32_t candidate = alive[k];
            // the outline without it: how far it has moved in, in each direction, since every card was there
            float squares = 0.0f;
            float given = 0.0f;
            for (int j = 0; j < kDirections; ++j) {
                const float h = holder[j] == candidate ? second[j] : first[j];
                const float moved = weight[j] * (full[j] - h) / size;
                squares += moved * moved;
                given = std::max(given, moved);
            }
            float nearest = 3.0e38f;
            for (int32_t i : alive) {
                if (i == candidate) continue;
                float d2 = 0.0f;
                for (int axis = 0; axis < 3; ++axis) {
                    const float d = cards[i].centre[axis] - cards[candidate].centre[axis];
                    d2 += d * d;
                }
                nearest = std::min(nearest, d2);
            }
            const float cost = std::sqrt(squares / static_cast<float>(kDirections)) +
                               kCrowding * std::sqrt(nearest) / size;
            const bool better = cost < bestCost ||
                                (cost == bestCost && cards[candidate].id > cards[alive[best]].id);
            if (better) {
                best = k;
                bestCost = cost;
                bestGiven = given;
            }
        }
        out.giveUp[removed.size()] = bestGiven;
        removed.push_back(alive[best]);
        alive.erase(alive.begin() + static_cast<std::ptrdiff_t>(best));
    }
    out.order.clear();
    out.order.push_back(alive[0]);
    out.order.insert(out.order.end(), removed.rbegin(), removed.rend());
    // what is given up never shrinks as more cards go (it is measured against the whole crown)
    for (size_t t = 1; t < out.giveUp.size(); ++t) out.giveUp[t] = std::max(out.giveUp[t], out.giveUp[t - 1]);
    return out;
}

float LeavingOf(const std::vector<float>& giveUp, int32_t count, float keepLast, float tolerance) {
    const float byShare = (1.0f - std::clamp(keepLast, 0.05f, 1.0f)) * static_cast<float>(count);
    int32_t byOutline = 0;
    while (byOutline < static_cast<int32_t>(giveUp.size()) && giveUp[byOutline] <= tolerance) ++byOutline;
    return std::min(byShare, static_cast<float>(byOutline));
}

float ShareAt(int32_t place, int32_t count, float progress, float leaving) {
    if (count <= 0 || leaving <= 0.0f) return 1.0f;
    // the card's turn to leave: the last of the order is turn 0
    const float turn = static_cast<float>(count - 1 - place);
    if (turn >= leaving) return 1.0f;
    const float p = std::clamp(progress, 0.0f, 1.0f) * (leaving + kWindow);
    return std::clamp((turn + kWindow - p) / kWindow, 0.0f, 1.0f);
}

int LevelAt(float distance, float start, float end, int levels) {
    if (levels <= 0 || distance <= start) return 0;
    if (end <= start) return levels;
    const float t = (distance - start) / (end - start);
    const int level = static_cast<int>(std::ceil(t * static_cast<float>(levels)));
    return std::clamp(level, 1, levels);
}

Plan Analyse(const uint8_t* vertices, size_t vertexBytes, const uint16_t* indices, size_t indexCount,
             const std::vector<List>& lists) {
    Plan plan;
    plan.lists = lists;
    plan.plans.resize(lists.size());
    const int32_t vertexTotal = static_cast<int32_t>(vertexBytes / kStride);

    for (size_t li = 0; li < lists.size(); ++li) {
        const List& in = lists[li];
        ListPlan& lp = plan.plans[li];
        const bool inRange = in.startIndex >= 0 && in.indexCount >= 0 &&
                             static_cast<size_t>(in.startIndex) + static_cast<size_t>(in.indexCount) <= indexCount;
        if (!inRange) {
            lp.empty = true;
            continue;
        }
        if (!in.canopy || in.baseVertex < 0) continue;
        const uint16_t* idx = indices + in.startIndex;
        const int32_t triangles = in.indexCount / 3;
        int32_t span = 0;
        for (int32_t i = 0; i < triangles * 3; ++i) span = std::max(span, static_cast<int32_t>(idx[i]) + 1);
        if (span == 0 || in.baseVertex + span > vertexTotal) continue;

        // the leaf vertices, and the cards: the connected sets of all-leaf triangles
        std::vector<uint8_t> leaf(static_cast<size_t>(span), 0);
        for (int32_t v = 0; v < span; ++v) {
            const bool corner = CornerAt(vertices, in.baseVertex + v, 0) != 0 ||
                                CornerAt(vertices, in.baseVertex + v, 1) != 0;
            leaf[v] = corner ? 1 : 0;
        }
        std::vector<int32_t> parent(static_cast<size_t>(span));
        std::iota(parent.begin(), parent.end(), 0);
        std::vector<uint8_t> cardTriangle(static_cast<size_t>(triangles), 0);
        for (int32_t t = 0; t < triangles; ++t) {
            const int32_t a = idx[t * 3 + 0];
            const int32_t b = idx[t * 3 + 1];
            const int32_t c = idx[t * 3 + 2];
            if (!(leaf[a] && leaf[b] && leaf[c])) continue;
            cardTriangle[t] = 1;
            Join(parent, a, b);
            Join(parent, a, c);
        }
        // the cards (numbered in the order their first triangle comes), their centres
        std::vector<int32_t> cardOfRoot(static_cast<size_t>(span), -1);
        std::vector<CardSite> sites;
        std::vector<int32_t> members;
        int32_t corner = 0;   // the largest corner weight: the cards' half size, in card units x 8192
        lp.triangleCard.assign(static_cast<size_t>(triangles), -1);
        for (int32_t t = 0; t < triangles; ++t) {
            if (!cardTriangle[t]) continue;
            const int32_t root = Find(parent, idx[t * 3]);
            if (cardOfRoot[root] < 0) {
                cardOfRoot[root] = static_cast<int32_t>(sites.size());
                sites.push_back(CardSite{{0.0f, 0.0f, 0.0f}, static_cast<uint32_t>(root)});
                members.push_back(0);
            }
            lp.triangleCard[t] = cardOfRoot[root];
        }
        if (sites.empty()) continue;
        lp.cardOf.assign(static_cast<size_t>(span), -1);
        for (int32_t v = 0; v < span; ++v) {
            if (!leaf[v]) continue;
            const int32_t card = cardOfRoot[Find(parent, v)];
            if (card < 0) continue;
            lp.cardOf[v] = card;
            corner = std::max(corner, std::abs(static_cast<int32_t>(CornerAt(vertices, in.baseVertex + v, 0))));
            corner = std::max(corner, std::abs(static_cast<int32_t>(CornerAt(vertices, in.baseVertex + v, 1))));
            int16_t p[3];
            std::memcpy(p, vertices + static_cast<size_t>(in.baseVertex + v) * kStride, sizeof(p));
            for (int axis = 0; axis < 3; ++axis) sites[card].centre[axis] += static_cast<float>(p[axis]);
            ++members[card];
        }
        for (size_t c = 0; c < sites.size(); ++c) {
            for (int axis = 0; axis < 3; ++axis) sites[c].centre[axis] /= static_cast<float>(std::max(members[c], 1));
        }
        // a card's radius in the positions' unit (1 / 512): its corner weight is in card units x 8192, and a
        // card unit is taken as one world unit (the cards drawn are about that size)
        const Order order = KeepOrder(sites, static_cast<float>(corner) * (512.0f / 8192.0f));
        lp.place.assign(sites.size(), 0);
        for (size_t k = 0; k < order.order.size(); ++k) lp.place[order.order[k]] = static_cast<int32_t>(k);
        lp.giveUp = order.giveUp;
        lp.copy = false;
        lp.span = span;
        lp.cards = static_cast<int32_t>(sites.size());
        plan.cards += lp.cards;
    }
    return plan;
}

Reduced Reduce(const Plan& plan, const uint8_t* vertices, size_t vertexBytes, const uint16_t* indices,
               float progress, const Shape& shape) {
    Reduced out;
    out.vertices.assign(vertices, vertices + vertexBytes);
    out.lists = plan.lists;

    for (size_t li = 0; li < plan.lists.size(); ++li) {
        const List& in = plan.lists[li];
        const ListPlan& lp = plan.plans[li];
        List& o = out.lists[li];
        o.startIndex = static_cast<int32_t>(out.indices.size());
        o.indexCount = 0;
        if (lp.empty) {
            o.canopy = false;
            continue;
        }
        const uint16_t* idx = indices + in.startIndex;
        if (lp.copy) {
            out.indices.insert(out.indices.end(), idx, idx + in.indexCount);
            o.indexCount = in.indexCount;
            o.canopy = false;
            continue;
        }

        // each card's size share at this progress, and the staying cards' growth
        const float leaving = LeavingOf(lp.giveUp, lp.cards, shape.keepLast, shape.outline);
        std::vector<float> share(static_cast<size_t>(lp.cards), 1.0f);
        float area = 0.0f;
        for (int32_t card = 0; card < lp.cards; ++card) {
            const float s = ShareAt(lp.place[card], lp.cards, progress, leaving);
            share[card] = s < kGone ? 0.0f : s;
            area += share[card];
            if (share[card] > 0.0f) ++out.drawn;
        }
        const float left = std::max(area / static_cast<float>(lp.cards), 0.05f);
        const float growth = std::pow(1.0f / left, 0.5f * std::max(shape.grow, 0.0f));

        // the leaf corners: a staying card grows, a leaving one only shrinks
        for (int32_t local = 0; local < lp.span; ++local) {
            const int32_t card = lp.cardOf[local];
            if (card < 0 || share[card] <= 0.0f) continue;
            const float scale = share[card] >= 1.0f ? growth : share[card];
            const int32_t v = in.baseVertex + local;
            for (int which = 0; which < 2; ++which) {
                const float f = static_cast<float>(CornerAt(vertices, v, which)) * scale;
                const long r = std::clamp(std::lround(f), -32767l, 32767l);
                const int16_t corner = static_cast<int16_t>(r);
                std::memcpy(out.vertices.data() + static_cast<size_t>(v) * kStride + kCornerAt + which * 2, &corner,
                            sizeof(corner));
            }
        }
        const int32_t triangles = in.indexCount / 3;
        for (int32_t t = 0; t < triangles; ++t) {
            const int32_t card = lp.triangleCard[t];
            if (card >= 0 && share[card] <= 0.0f) continue;
            out.indices.insert(out.indices.end(), idx + t * 3, idx + t * 3 + 3);
        }
        o.indexCount = static_cast<int32_t>(out.indices.size()) - o.startIndex;
    }
    return out;
}

}  // namespace LeafLod
}  // namespace Ext
