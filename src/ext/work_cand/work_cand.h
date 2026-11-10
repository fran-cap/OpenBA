// src/ext/work_cand/work_cand.h -- ext.work_cand: the idle work search's candidates in a sorted flat array
// (NOT RETAIL)
//
// docs/extensions/handins/WORKCAND.md. Standard C++ with no game types, so the unit tests build it
// (tests/unit/ext_work_cand_test.cpp). The game side is the plain-cost arm of the work walk,
// src/game/Component/citizenai_housing_idle.cpp (Helper_sub_79b0_Flat).
//
// Retail (0x1400f8000, the plain-cost builder of the walk 0x1400f79b0) inserts every job of a scheduler bucket
// into a transient AVL, one MemAlloc(0x30, 8) node per insert, and walks it in order (0x1400f7b60). Its descent
// (0x1400f80d0..0x1400f8130) orders the jobs by
//   cost (Work +0x24), signed, ascending; then the work id (Work +0x20), signed, ascending,
// and drops an insert whose (cost, id) pair is already in the tree: the node there keeps its job. Both
// compares are strict, so the key is a total order and the in-order walk is a function of the SET of keys,
// not of the tree's shape. This list holds the same set in the same order: one array, no node per job.
//
// The walk builds at most twice into ONE tree (the workplace's jobs, then the whole bucket), so the second
// build merges into what the first left; an insert equal to a job already there is dropped as retail drops it.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Ext {
namespace WorkCand {

// Retail's (cost, id) order as ONE signed 64-bit compare: the cost in the high half, the id's sign bit flipped
// in the low half (so the low half counts up with the signed id). cost * 2^32 cannot overflow for any int32.
inline int64_t Key(int32_t cost, int32_t id) {
    const uint32_t low = static_cast<uint32_t>(id) ^ 0x80000000u;
    return static_cast<int64_t>(cost) * 4294967296LL + static_cast<int64_t>(low);
}

// The candidates of one walk. `Item` is the job (Work); `IdOf` reads its id (Work +0x20) when the key is made,
// as retail's descent reads the id of both jobs at each compare.
template <class Item, class IdOf>
class CandidateList {
public:
    struct Entry {
        int64_t key;    // Key(cost, id)
        Item*   item;   // the job
        int32_t cost;   // the cost the insert read (retail stores it in the node, +0x08)
        int32_t order;  // its place among this build's inserts (the tiebreak that keeps the first)
    };

    // A new walk: no candidates.
    void Clear() {
        entries_.clear();
        staged_.clear();
    }

    // One insert of the build in progress, in retail's insert order (the source tree's in-order walk).
    void Stage(Item* item, int32_t cost) {
        Entry e;
        e.key = Key(cost, IdOf()(item));
        e.item = item;
        e.cost = cost;
        e.order = static_cast<int32_t>(staged_.size());
        staged_.push_back(e);
    }

    // The end of a build: every staged insert goes in as retail's would, one by one: the first of a key is
    // kept, a later insert of a key already present (staged earlier, or left by the previous build) is
    // dropped. Returns how many were inserted (retail's count, +1 per insert, 0x1400f81f8).
    int Commit() {
        const size_t before = entries_.size();
        if (staged_.empty()) {
            return 0;
        }
        if (!StrictlyAscending(staged_)) {
            // (key, order) is a total order: equal keys stay in insert order, the first one leads its run.
            std::sort(staged_.begin(), staged_.end(), [](const Entry& a, const Entry& b) {
                return a.key < b.key || (a.key == b.key && a.order < b.order);
            });
            DropRepeats(staged_);
        }
        if (entries_.empty()) {
            entries_.swap(staged_);
            staged_.clear();
            return static_cast<int>(entries_.size());
        }
        // A second build: the ids are read again now, as retail's descent reads both jobs' ids at the time
        // of each insert (the costs are the stored ones, as in retail's nodes).
        for (Entry& e : entries_) {
            e.key = Key(e.cost, IdOf()(e.item));
        }
        merged_.clear();
        merged_.reserve(entries_.size() + staged_.size());
        size_t i = 0;
        size_t j = 0;
        while (i < entries_.size() && j < staged_.size()) {
            if (entries_[i].key < staged_[j].key) {
                merged_.push_back(entries_[i]);
                ++i;
            } else if (staged_[j].key < entries_[i].key) {
                merged_.push_back(staged_[j]);
                ++j;
            } else {
                // already in the list: the earlier job stays, the insert is dropped
                ++j;
            }
        }
        merged_.insert(merged_.end(), entries_.begin() + static_cast<std::ptrdiff_t>(i), entries_.end());
        merged_.insert(merged_.end(), staged_.begin() + static_cast<std::ptrdiff_t>(j), staged_.end());
        entries_.swap(merged_);
        staged_.clear();
        return static_cast<int>(entries_.size() - before);
    }

    // The candidates in retail's walk order.
    size_t Size() const { return entries_.size(); }
    const Entry& At(size_t i) const { return entries_[i]; }

private:
    static bool StrictlyAscending(const std::vector<Entry>& v) {
        for (size_t i = 1; i < v.size(); ++i) {
            if (!(v[i - 1].key < v[i].key)) {
                return false;
            }
        }
        return true;
    }

    // Sorted input: keep the first entry of each run of equal keys.
    static void DropRepeats(std::vector<Entry>& v) {
        size_t out = 0;
        for (size_t i = 0; i < v.size(); ++i) {
            if (out == 0 || v[out - 1].key != v[i].key) {
                v[out] = v[i];
                ++out;
            }
        }
        v.resize(out);
    }

    std::vector<Entry> entries_;
    std::vector<Entry> staged_;
    std::vector<Entry> merged_;
};

}  // namespace WorkCand
}  // namespace Ext
