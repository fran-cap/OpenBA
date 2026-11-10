// ============================================================================
// tests/unit/ext_work_cand_test.cpp -- ext.work_cand's candidate list
// ============================================================================
// src/ext/work_cand/work_cand.h without the game, against retail's insert written out again: the descent of
// 0x1400f8000 (0x1400f80d0..0x1400f8130), compare for compare, into a plain binary tree. The rebalance
// (0x14000e360) only rotates, which keeps the in-order sequence, and a search in a binary tree under a
// strict order meets an equal key wherever the tree's shape puts it, so the plain tree walks and drops
// exactly as retail's AVL does. Checked: the jobs in walk order, the stored costs, the insert counts, over
// one and two builds into one list.
// ============================================================================
#include "unit.h"
#include "ext/work_cand/work_cand.h"

#include <climits>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace {

struct FakeWork {
    int32_t id;   // Work +0x20
};

struct FakeIdOf {
    int32_t operator()(const FakeWork* w) const { return w->id; }
};

using List = Ext::WorkCand::CandidateList<FakeWork, FakeIdOf>;

// Retail's tree without the rebalance (the header says why that is enough).
class RetailTree {
public:
    // 0x1400f8000's insert of (work, cost); returns false when it is dropped as a duplicate.
    bool Insert(FakeWork* work, int32_t cost) {
        if (root_ == nullptr) {
            root_ = NewNode(work, cost);
            ++count_;
            return true;
        }
        Node* self = root_;
        for (;;) {
            bool left = false;
            if (cost < self->cost) {                     // 0x1400f80d5 JL: left
                left = true;
            } else if (!(cost > self->cost)) {           // 0x1400f80d7 JG skips the tiebreak
                if (work->id < self->work->id) {         // 0x1400f80ff JL: left
                    left = true;
                }
            }
            if (!left) {
                if (!(self->cost < cost)) {              // 0x1400f8106 JL: right
                    if (self->cost > cost) return false; // 0x1400f8108 JG: no insert (not reachable)
                    if (self->work->id >= work->id) return false;   // 0x1400f8130 JGE: the duplicate
                }
                if (self->right == nullptr) {
                    self->right = NewNode(work, cost);
                    ++count_;
                    return true;
                }
                self = self->right;
                continue;
            }
            if (self->left == nullptr) {
                self->left = NewNode(work, cost);
                ++count_;
                return true;
            }
            self = self->left;
        }
    }

    // 0x1400f7b60's order: in order, iteratively (a sorted input makes the plain tree a list).
    void Walk(std::vector<FakeWork*>& works, std::vector<int32_t>& costs) const {
        works.clear();
        costs.clear();
        std::vector<Node*> stack;
        Node* cur = root_;
        while (cur != nullptr || !stack.empty()) {
            while (cur != nullptr) {
                stack.push_back(cur);
                cur = cur->left;
            }
            cur = stack.back();
            stack.pop_back();
            works.push_back(cur->work);
            costs.push_back(cur->cost);
            cur = cur->right;
        }
    }

    int Count() const { return count_; }

private:
    struct Node {
        FakeWork* work;
        int32_t   cost;
        Node*     left;
        Node*     right;
    };
    Node* NewNode(FakeWork* work, int32_t cost) {
        nodes_.push_back(std::unique_ptr<Node>(new Node{work, cost, nullptr, nullptr}));
        return nodes_.back().get();
    }
    std::vector<std::unique_ptr<Node>> nodes_;
    Node* root_ = nullptr;
    int count_ = 0;
};

// A portable generator (the same numbers on every platform).
struct Lcg {
    uint64_t s;
    uint32_t Next() {
        s = s * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<uint32_t>(s >> 33);
    }
    int32_t Range(int32_t lo, int32_t hi) {   // [lo, hi]
        return lo + static_cast<int32_t>(Next() % static_cast<uint32_t>(hi - lo + 1));
    }
};

bool SameWalk(const List& list, const RetailTree& tree) {
    std::vector<FakeWork*> works;
    std::vector<int32_t> costs;
    tree.Walk(works, costs);
    if (list.Size() != works.size() || static_cast<int>(list.Size()) != tree.Count()) {
        return false;
    }
    for (size_t i = 0; i < works.size(); ++i) {
        if (list.At(i).item != works[i] || list.At(i).cost != costs[i]) {
            return false;
        }
    }
    return true;
}

// One build of `inserts` (work index, cost) into both; checks the walk and the count of inserts.
bool BuildBoth(List& list, RetailTree& tree, std::vector<FakeWork>& works,
               const std::vector<std::pair<int, int32_t>>& inserts) {
    int retailInserted = 0;
    for (const auto& in : inserts) {
        list.Stage(&works[static_cast<size_t>(in.first)], in.second);
        if (tree.Insert(&works[static_cast<size_t>(in.first)], in.second)) {
            ++retailInserted;
        }
    }
    const int inserted = list.Commit();
    return inserted == retailInserted && SameWalk(list, tree);
}

}  // namespace

UNIT_TEST(ext_work_cand_key_is_retails_cost_then_id_order) {
    const int32_t values[] = {INT_MIN, INT_MIN + 1, -2, -1, 0, 1, 2, INT_MAX - 1, INT_MAX};
    for (int32_t c1 : values) {
        for (int32_t i1 : values) {
            for (int32_t c2 : values) {
                for (int32_t i2 : values) {
                    const bool less = c1 < c2 || (c1 == c2 && i1 < i2);
                    CHECK_EQ(Ext::WorkCand::Key(c1, i1) < Ext::WorkCand::Key(c2, i2), less);
                    CHECK_EQ(Ext::WorkCand::Key(c1, i1) == Ext::WorkCand::Key(c2, i2), c1 == c2 && i1 == i2);
                }
            }
        }
    }
}

// Many ties: costs and ids drawn from small ranges, so equal costs are broken by the id and equal pairs are
// dropped (the first job of a pair stays).
UNIT_TEST(ext_work_cand_one_build_walks_and_drops_as_retail) {
    Lcg rng{12345};
    int mismatches = 0;
    int dropped = 0;
    for (int round = 0; round < 2000; ++round) {
        const int n = rng.Range(0, 120);
        const int32_t spread = rng.Range(0, 6);
        std::vector<FakeWork> works(static_cast<size_t>(n));
        std::vector<std::pair<int, int32_t>> inserts;
        for (int k = 0; k < n; ++k) {
            works[static_cast<size_t>(k)].id = rng.Range(-spread, spread);
            inserts.push_back({k, rng.Range(-spread, spread)});
        }
        List list;
        list.Clear();
        RetailTree tree;
        if (!BuildBoth(list, tree, works, inserts)) ++mismatches;
        dropped += n - tree.Count();
    }
    CHECK_EQ(mismatches, 0);
    CHECK(dropped > 0);   // the duplicate rule was exercised
}

// The shapes the game feeds it: ascending (fresh jobs: queue order and id both count up), descending, all
// one key, the extremes of both fields, and an ascending run with a few re-posted jobs (an old cost, a new
// id) mixed in.
UNIT_TEST(ext_work_cand_input_shapes) {
    {
        std::vector<FakeWork> works(500);
        std::vector<std::pair<int, int32_t>> up;
        std::vector<std::pair<int, int32_t>> down;
        for (int k = 0; k < 500; ++k) {
            works[static_cast<size_t>(k)].id = 1000 + k;
            up.push_back({k, 2000 + k});
            down.push_back({499 - k, 2000 + 499 - k});
        }
        List a;
        RetailTree ta;
        CHECK(BuildBoth(a, ta, works, up));
        CHECK_EQ(a.Size(), static_cast<size_t>(500));
        List b;
        RetailTree tb;
        CHECK(BuildBoth(b, tb, works, down));
    }
    {
        std::vector<FakeWork> works(50);
        std::vector<std::pair<int, int32_t>> same;
        for (int k = 0; k < 50; ++k) {
            works[static_cast<size_t>(k)].id = 7;
            same.push_back({k, 3});
        }
        List l;
        RetailTree t;
        CHECK(BuildBoth(l, t, works, same));
        CHECK_EQ(l.Size(), static_cast<size_t>(1));
        CHECK(l.At(0).item == &works[0]);   // the first job of the key stays
    }
    {
        const int32_t extremes[] = {INT_MIN, -1, 0, 1, INT_MAX};
        std::vector<FakeWork> works;
        std::vector<std::pair<int, int32_t>> inserts;
        for (int32_t id : extremes) {
            for (int32_t cost : extremes) {
                works.push_back(FakeWork{id});
                inserts.push_back({static_cast<int>(works.size()) - 1, cost});
            }
        }
        std::vector<std::pair<int, int32_t>> reversed(inserts.rbegin(), inserts.rend());
        List l;
        RetailTree t;
        CHECK(BuildBoth(l, t, works, reversed));
        CHECK_EQ(l.Size(), static_cast<size_t>(25));
    }
    {
        Lcg rng{777};
        std::vector<FakeWork> works(800);
        std::vector<std::pair<int, int32_t>> inserts;
        for (int k = 0; k < 800; ++k) {
            works[static_cast<size_t>(k)].id = k;
            const bool reposted = rng.Range(0, 19) == 0;
            inserts.push_back({k, reposted ? rng.Range(0, k) : k});
        }
        List l;
        RetailTree t;
        CHECK(BuildBoth(l, t, works, inserts));
    }
}

// 0x1400f79b0 builds the workplace's jobs, walks, then builds the whole bucket into the SAME tree: the
// second build merges, and a job already there is dropped again. A job whose cost changed between the builds
// is a new key (the tree keeps the cost each node was inserted with) and appears twice, in retail as here.
UNIT_TEST(ext_work_cand_second_build_merges_as_retail) {
    Lcg rng{99};
    int mismatches = 0;
    for (int round = 0; round < 1500; ++round) {
        const int n = rng.Range(0, 100);
        const int32_t spread = rng.Range(1, 40);
        std::vector<FakeWork> works(static_cast<size_t>(n));
        std::vector<int32_t> cost(static_cast<size_t>(n));
        for (int k = 0; k < n; ++k) {
            works[static_cast<size_t>(k)].id = rng.Range(0, spread);
            cost[static_cast<size_t>(k)] = rng.Range(-spread, spread);
        }
        std::vector<std::pair<int, int32_t>> first;    // the workplace's jobs: a subset
        std::vector<std::pair<int, int32_t>> second;   // the whole bucket, a few costs moved
        for (int k = 0; k < n; ++k) {
            if (rng.Range(0, 2) == 0) first.push_back({k, cost[static_cast<size_t>(k)]});
        }
        for (int k = 0; k < n; ++k) {
            const bool moved = rng.Range(0, 9) == 0;
            second.push_back({k, moved ? cost[static_cast<size_t>(k)] + 1 : cost[static_cast<size_t>(k)]});
        }
        List list;
        list.Clear();
        RetailTree tree;
        if (!BuildBoth(list, tree, works, first)) ++mismatches;
        if (!BuildBoth(list, tree, works, second)) ++mismatches;
        // an empty first build (no job of the workplace) leaves the second to start from nothing
        List empty;
        RetailTree emptyTree;
        if (!BuildBoth(empty, emptyTree, works, {})) ++mismatches;
        if (!BuildBoth(empty, emptyTree, works, second)) ++mismatches;
    }
    CHECK_EQ(mismatches, 0);
}

// Clear starts a new walk: nothing of the previous one is left (the game reuses one list per nesting level).
UNIT_TEST(ext_work_cand_clear_forgets_the_previous_walk) {
    std::vector<FakeWork> works = {FakeWork{1}, FakeWork{2}, FakeWork{3}};
    List list;
    list.Stage(&works[2], 5);
    list.Stage(&works[0], 9);
    list.Commit();
    CHECK_EQ(list.Size(), static_cast<size_t>(2));
    list.Clear();
    CHECK_EQ(list.Size(), static_cast<size_t>(0));
    CHECK_EQ(list.Commit(), 0);
    list.Stage(&works[1], 1);
    CHECK_EQ(list.Commit(), 1);
    CHECK_EQ(list.Size(), static_cast<size_t>(1));
    CHECK(list.At(0).item == &works[1]);
}
