// ============================================================================
// tests/unit/ext_leaf_lod_test.cpp -- ext.leaf_lod's canopy reduction
// (src/ext/leaf_lod/leaf_lod_build.h, docs/extensions/LEAF_LOD.md)
// ============================================================================
// A made-up mesh (a trunk quad and leaf cards, no game data): progress 0 is the mesh itself; the trunk is
// never cut; the levels are nested and shrink a leaving card before it goes; a card that only doubles a
// neighbour leaves first and the cards that draw the outline last; the cap stops a list where its outline
// would move; a list that is not a canopy is copied and one out of range is emptied; the level a zoom picks.
// ============================================================================
#include "unit.h"
#include "ext/leaf_lod/leaf_lod_build.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <set>
#include <vector>

namespace {

namespace LL = Ext::LeafLod;

struct Mesh {
    std::vector<uint8_t>  vertices;
    std::vector<uint16_t> indices;
    std::vector<LL::List> lists;
};

void AddVertex(Mesh& m, float x, float y, float z, int16_t cornerX, int16_t cornerY) {
    uint8_t v[LL::kStride] = {};
    const int16_t p[3] = {static_cast<int16_t>(std::lround(x * 512.0f)), static_cast<int16_t>(std::lround(y * 512.0f)),
                          static_cast<int16_t>(std::lround(z * 512.0f))};
    std::memcpy(v, p, sizeof(p));
    const int16_t c[2] = {cornerX, cornerY};
    std::memcpy(v + LL::kCornerAt, c, sizeof(c));
    m.vertices.insert(m.vertices.end(), v, v + LL::kStride);
}

// Four vertices and two triangles; corner 0 = a trunk quad, else a leaf card of that corner weight.
void AddQuad(Mesh& m, uint16_t first, float x, float y, float z, int16_t corner) {
    AddVertex(m, x, y, z, static_cast<int16_t>(-corner), static_cast<int16_t>(-corner));
    AddVertex(m, x, y, z, corner, static_cast<int16_t>(-corner));
    AddVertex(m, x, y, z, corner, corner);
    AddVertex(m, x, y, z, static_cast<int16_t>(-corner), corner);
    const uint16_t tri[6] = {first, static_cast<uint16_t>(first + 1), static_cast<uint16_t>(first + 2),
                             first, static_cast<uint16_t>(first + 2), static_cast<uint16_t>(first + 3)};
    m.indices.insert(m.indices.end(), tri, tri + 6);
}

// One canopy list: a trunk quad, then a card at each of `centres`.
Mesh Tree(const std::vector<std::vector<float>>& centres) {
    Mesh m;
    AddQuad(m, 0, 0.0f, 0.0f, 0.0f, 0);
    for (size_t i = 0; i < centres.size(); ++i) {
        AddQuad(m, static_cast<uint16_t>(4 + i * 4), centres[i][0], centres[i][1], centres[i][2], 5000);
    }
    LL::List l;
    l.baseVertex = 0;
    l.vertexCount = static_cast<int32_t>(m.vertices.size() / LL::kStride);
    l.startIndex = 0;
    l.indexCount = static_cast<int32_t>(m.indices.size());
    l.canopy = true;
    m.lists.push_back(l);
    return m;
}

// Twelve cards on a ring of radius 2 at two heights, and `extra` more cards.
std::vector<std::vector<float>> Crown(const std::vector<std::vector<float>>& extra) {
    std::vector<std::vector<float>> c;
    for (int i = 0; i < 6; ++i) {
        const float a = 6.2831853f * static_cast<float>(i) / 6.0f;
        c.push_back({2.0f * std::cos(a), 2.0f * std::sin(a), 4.0f});
        c.push_back({1.4f * std::cos(a + 0.5f), 1.4f * std::sin(a + 0.5f), 5.5f});
    }
    c.insert(c.end(), extra.begin(), extra.end());
    return c;
}

LL::Plan PlanOf(const Mesh& m) {
    return LL::Analyse(m.vertices.data(), m.vertices.size(), m.indices.data(), m.indices.size(), m.lists);
}

LL::Reduced At(const Mesh& m, const LL::Plan& plan, float progress, const LL::Shape& shape) {
    return LL::Reduce(plan, m.vertices.data(), m.vertices.size(), m.indices.data(), progress, shape);
}

// The first vertices (the cards' numbers, trunk = 0) of the quads a level draws.
std::set<int> QuadsOf(const LL::Reduced& r) {
    std::set<int> quads;
    for (size_t i = 0; i + 2 < r.indices.size(); i += 3) quads.insert(r.indices[i] / 4);
    return quads;
}

int16_t CornerOf(const std::vector<uint8_t>& vertices, int vertex) {
    int16_t c;
    std::memcpy(&c, vertices.data() + static_cast<size_t>(vertex) * LL::kStride + LL::kCornerAt, sizeof(c));
    return c;
}

LL::Shape Loose(float keepLast) {
    LL::Shape s;
    s.keepLast = keepLast;
    s.grow = 0.0f;
    s.outline = 1.0e9f;   // no cap
    return s;
}

}  // namespace

UNIT_TEST(ext_leaf_lod_progress_zero_is_the_mesh) {
    const Mesh m = Tree(Crown({}));
    const LL::Plan plan = PlanOf(m);
    CHECK_EQ(plan.cards, 12);
    const LL::Reduced r = At(m, plan, 0.0f, Loose(0.5f));
    CHECK(r.vertices == m.vertices);
    CHECK(r.indices == m.indices);
    CHECK_EQ(r.drawn, 12);
    CHECK_EQ(r.lists[0].startIndex, 0);
    CHECK_EQ(r.lists[0].indexCount, static_cast<int32_t>(m.indices.size()));
}

UNIT_TEST(ext_leaf_lod_last_level_keeps_the_share_and_the_trunk) {
    const Mesh m = Tree(Crown({}));
    const LL::Plan plan = PlanOf(m);
    const LL::Reduced r = At(m, plan, 1.0f, Loose(0.5f));
    CHECK_EQ(r.drawn, 6);
    const std::set<int> quads = QuadsOf(r);
    CHECK_EQ(static_cast<int>(quads.size()), 7);   // six cards and the trunk
    CHECK(quads.count(0) == 1);
    CHECK_EQ(r.lists[0].indexCount, 7 * 6);
    // the trunk's vertices are untouched, a kept card's corners too (grow 0)
    CHECK(std::memcmp(r.vertices.data(), m.vertices.data(), 4 * LL::kStride) == 0);
    for (int q : quads) {
        if (q != 0) CHECK_EQ(CornerOf(r.vertices, q * 4 + 2), 5000);
    }
}

UNIT_TEST(ext_leaf_lod_levels_are_nested_and_shrink_before_they_cut) {
    const Mesh m = Tree(Crown({}));
    const LL::Plan plan = PlanOf(m);
    const LL::Shape shape = Loose(0.5f);
    std::set<int> before = QuadsOf(At(m, plan, 0.0f, shape));
    bool shrinking = false;
    for (int n = 1; n <= 64; ++n) {
        const LL::Reduced r = At(m, plan, static_cast<float>(n) / 64.0f, shape);
        const std::set<int> now = QuadsOf(r);
        for (int q : now) CHECK(before.count(q) == 1);   // nothing comes back
        for (int q : now) {
            const int16_t c = CornerOf(r.vertices, q * 4 + 2);
            if (q != 0 && c < 5000) {
                shrinking = true;
                CHECK(c > 0);
            }
        }
        before = now;
    }
    CHECK(shrinking);   // a leaving card is drawn smaller for a while, not cut at full size
    CHECK_EQ(static_cast<int>(before.size()), 7);
}

UNIT_TEST(ext_leaf_lod_grow_scales_the_staying_cards) {
    const Mesh m = Tree(Crown({}));
    const LL::Plan plan = PlanOf(m);
    LL::Shape shape = Loose(0.5f);
    shape.grow = 1.0f;
    const LL::Reduced r = At(m, plan, 1.0f, shape);
    // half the cards: each staying card's side grows by sqrt(2)
    for (int q : QuadsOf(r)) {
        if (q != 0) CHECK_EQ(CornerOf(r.vertices, q * 4 + 2), static_cast<int16_t>(std::lround(5000.0f * std::sqrt(2.0f))));
    }
}

UNIT_TEST(ext_leaf_lod_a_doubled_card_leaves_first_the_outline_last) {
    // the crown, a card at its very middle (inside every outline) and a twin of card 1
    std::vector<std::vector<float>> centres = Crown({{0.0f, 0.0f, 4.7f}});
    std::vector<LL::CardSite> sites;
    for (size_t i = 0; i < centres.size(); ++i) {
        sites.push_back(LL::CardSite{{centres[i][0], centres[i][1], centres[i][2]}, static_cast<uint32_t>(i)});
    }
    const LL::Order order = LL::KeepOrder(sites, 0.6f);
    CHECK_EQ(static_cast<int>(order.order.size()), 13);
    CHECK_EQ(order.order.back(), 12);          // the middle card goes first...
    CHECK_EQ(order.giveUp[0], 0.0f);           // ...and the outline gives nothing up for it
    CHECK(order.giveUp[1] > 0.0f);             // every other card draws some of the outline
    for (size_t t = 1; t < order.giveUp.size(); ++t) CHECK(order.giveUp[t] >= order.giveUp[t - 1]);
    // every card is in the order once
    std::set<int32_t> seen(order.order.begin(), order.order.end());
    CHECK_EQ(static_cast<int>(seen.size()), 13);
}

UNIT_TEST(ext_leaf_lod_the_cap_stops_a_list_where_its_outline_would_move) {
    const Mesh m = Tree(Crown({{0.0f, 0.0f, 4.7f}}));
    const LL::Plan plan = PlanOf(m);
    CHECK_EQ(plan.cards, 13);
    LL::Shape shape = Loose(0.3f);
    shape.outline = 0.0f;   // not at all: only the middle card may go
    const LL::Reduced r = At(m, plan, 1.0f, shape);
    CHECK_EQ(r.drawn, 12);
    CHECK(QuadsOf(r).count(13) == 0);   // the middle card is quad 13 (the trunk is quad 0)
    CHECK_EQ(LL::LeavingOf(plan.plans[0].giveUp, 13, 0.3f, 0.0f), 1.0f);
    CHECK(LL::LeavingOf(plan.plans[0].giveUp, 13, 0.3f, 1.0e9f) > 9.0f);
    CHECK_EQ(LL::LeavingOf(plan.plans[0].giveUp, 13, 1.0f, 1.0e9f), 0.0f);
}

UNIT_TEST(ext_leaf_lod_share_at) {
    // 10 cards, 4 leaving: the six kept longest never shrink
    for (int place = 0; place < 6; ++place) CHECK_EQ(LL::ShareAt(place, 10, 1.0f, 4.0f), 1.0f);
    for (int place = 6; place < 10; ++place) {
        CHECK_EQ(LL::ShareAt(place, 10, 0.0f, 4.0f), 1.0f);
        CHECK_EQ(LL::ShareAt(place, 10, 1.0f, 4.0f), 0.0f);
    }
    // the last of the order leaves first, and a share only falls as the progress rises
    CHECK(LL::ShareAt(9, 10, 0.3f, 4.0f) < LL::ShareAt(8, 10, 0.3f, 4.0f));
    float last = 1.0f;
    for (int n = 0; n <= 100; ++n) {
        const float s = LL::ShareAt(8, 10, static_cast<float>(n) / 100.0f, 4.0f);
        CHECK(s <= last);
        last = s;
    }
    CHECK_EQ(LL::ShareAt(9, 10, 0.7f, 0.0f), 1.0f);   // nothing leaves
}

UNIT_TEST(ext_leaf_lod_other_lists_are_copied_or_emptied) {
    Mesh m = Tree(Crown({}));
    const int32_t canopyIndices = static_cast<int32_t>(m.indices.size());
    // a second list over the same vertices that is not a canopy, and a third out of the index range
    LL::List plain = m.lists[0];
    plain.canopy = false;
    plain.startIndex = 6;
    plain.indexCount = 12;
    m.lists.push_back(plain);
    LL::List wild = m.lists[0];
    wild.startIndex = canopyIndices - 3;
    wild.indexCount = 30;
    m.lists.push_back(wild);
    const LL::Plan plan = PlanOf(m);
    CHECK_EQ(plan.cards, 12);
    const LL::Reduced r = At(m, plan, 1.0f, Loose(0.5f));
    CHECK_EQ(r.lists[0].indexCount, 7 * 6);
    CHECK_EQ(r.lists[1].startIndex, 7 * 6);
    CHECK_EQ(r.lists[1].indexCount, 12);
    for (int i = 0; i < 12; ++i) CHECK_EQ(r.indices[static_cast<size_t>(7 * 6 + i)], m.indices[static_cast<size_t>(6 + i)]);
    CHECK_EQ(r.lists[2].indexCount, 0);
    CHECK_EQ(static_cast<int>(r.indices.size()), 7 * 6 + 12);
}

UNIT_TEST(ext_leaf_lod_level_at) {
    CHECK_EQ(LL::LevelAt(20.0f, 60.0f, 150.0f, 64), 0);
    CHECK_EQ(LL::LevelAt(60.0f, 60.0f, 150.0f, 64), 0);
    CHECK_EQ(LL::LevelAt(60.5f, 60.0f, 150.0f, 64), 1);
    CHECK_EQ(LL::LevelAt(105.0f, 60.0f, 150.0f, 64), 32);
    CHECK_EQ(LL::LevelAt(150.0f, 60.0f, 150.0f, 64), 64);
    CHECK_EQ(LL::LevelAt(400.0f, 60.0f, 150.0f, 64), 64);
    CHECK_EQ(LL::LevelAt(70.0f, 60.0f, 60.0f, 64), 64);   // no range: the last level past the start
    CHECK_EQ(LL::LevelAt(70.0f, 60.0f, 150.0f, 0), 0);
}
