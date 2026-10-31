// src/ext/family_tree/family_summary.cpp -- NOT RETAIL. ext.family_tree's town summary: the numbers and the
// rows. Standard C++ only. See family_summary.h and docs/extensions/handins/FAMILYTREE.md section 4d.
#include "ext/family_tree/family_summary.h"

#include <algorithm>
#include <cstdio>

namespace Ext {
namespace FamilyTree {

namespace {

// The generation of every person: one more than the later of their known parents, 1 for a person with none.
// Done by a walk up the parents with a memo, not by id order, so a parent that was recorded after the child
// is still found; a loop in the data (which the decoder does not make but a hand-built table could) ends at
// the depth limit.
class Generations {
public:
    explicit Generations(const Tree& tree) : tree_(tree), memo_(tree.Size(), 0) {}

    int Of(uint32_t id, int depth = 0) {
        const Person* p = tree_.Find(id);
        if (p == nullptr) return 0;
        const size_t at = static_cast<size_t>(p - tree_.People().data());
        if (memo_[at] > 0) return memo_[at];
        if (depth > kDepth) return 1;
        const int m = p->mother != 0 ? Of(p->mother, depth + 1) : 0;
        const int f = p->father != 0 ? Of(p->father, depth + 1) : 0;
        const int g = std::max(m, f) + 1;
        if (depth <= kDepth) memo_[at] = g;
        return g;
    }

private:
    static constexpr int kDepth = 400;
    const Tree& tree_;
    std::vector<int> memo_;
};

std::string Count(size_t n) { return std::to_string(n); }

std::string Years(double sum, size_t n) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.1f", n > 0 ? sum / static_cast<double>(n) : 0.0);
    std::string s = buf;
    if (s.size() > 2 && s.compare(s.size() - 2, 2, ".0") == 0) s.resize(s.size() - 2);
    return s + " y";
}

size_t Percent(size_t part, size_t whole) {
    return whole == 0 ? 0 : (part * 100 + whole / 2) / whole;
}

}  // namespace

const char* CauseTitle(uint8_t cause) {
    switch (cause) {
        case kHunger: return "Hunger";
        case kCold: return "Cold";
        case kOldAge: return "Old age";
        case kDisease: return "Disease";
        case kOrphaned: return "Lost their parents";
        case kChildbirth: return "Childbirth";
        case kOther: return "Other";
        default: return "";
    }
}

Summary Summarize(const Tree& tree, int32_t now) {
    Summary s;
    s.people = tree.Size();
    Generations gens(tree);
    int32_t firstYear = INT32_MAX;
    int32_t lastYear = now >= 0 ? now / 12 : -1;
    for (const Person& p : tree.People()) {
        if (p.Alive()) ++s.living; else ++s.dead;
        if (p.bornHere) ++s.bornHere; else ++s.arrived;
        const int g = gens.Of(p.id);
        if (g > 0) {
            if (static_cast<size_t>(g) > s.perGeneration.size()) {
                s.perGeneration.resize(static_cast<size_t>(g), 0);
                s.livingPerGeneration.resize(static_cast<size_t>(g), 0);
            }
            ++s.perGeneration[static_cast<size_t>(g - 1)];
            if (p.Alive()) ++s.livingPerGeneration[static_cast<size_t>(g - 1)];
        }
        if (!p.Alive()) {
            if (p.cause > kAlive && p.cause < kCauseCount) ++s.deathsByCause[p.cause];
            if (!(p.cause == kOther && p.ageAtDeath == 0)) {
                ++s.aged;
                s.ageSum += p.ageAtDeath;
                if (p.bornHere) {
                    ++s.agedHere;
                    s.ageSumHere += p.ageAtDeath;
                }
            }
        }
        if (p.joined >= 0) {
            firstYear = std::min(firstYear, p.joined / 12);
            lastYear = std::max(lastYear, p.joined / 12);
        }
        if (!p.Alive() && p.died >= 0) lastYear = std::max(lastYear, p.died / 12);
    }
    s.generations = static_cast<int>(s.perGeneration.size());

    if (firstYear == INT32_MAX || lastYear < firstYear) return s;
    const int32_t start = std::max<int32_t>(firstYear, lastYear - static_cast<int32_t>(kMaxSummaryYears) + 1);
    const size_t n = static_cast<size_t>(lastYear - start + 1);
    s.years.assign(n, YearRow());
    std::vector<long long> delta(n, 0);
    for (size_t i = 0; i < n; ++i) s.years[i].year = start + static_cast<int32_t>(i);
    for (const Person& p : tree.People()) {
        if (p.joined < 0) continue;
        const int32_t jy = p.joined / 12;
        const size_t ji = static_cast<size_t>(std::max<int32_t>(jy, start) - start);
        if (jy >= start) {
            if (p.bornHere) ++s.years[ji].born; else ++s.years[ji].arrived;
        }
        ++delta[ji];
        if (!p.Alive() && p.died >= 0) {
            const int32_t dy = p.died / 12;
            const size_t di = static_cast<size_t>(std::max<int32_t>(dy, start) - start);
            if (dy >= start) ++s.years[di].died;
            --delta[di];
        }
    }
    long long running = 0;
    for (size_t i = 0; i < n; ++i) {
        running += delta[i];
        s.years[i].living = running > 0 ? static_cast<size_t>(running) : 0;
    }
    return s;
}

std::vector<SummaryRow> SummaryRows(const Summary& s, int32_t now, const std::string& townName,
                                    const std::string& date) {
    std::vector<SummaryRow> rows;
    auto add = [&rows](const std::string& label, const std::string& info) {
        if (rows.size() < kMaxSummaryRows) rows.push_back(SummaryRow{label, info});
    };
    const bool headed = !townName.empty() || !date.empty();
    if (headed) {
        add("Town", "");
        if (!townName.empty()) add("Name", townName);
        if (!date.empty()) add("Date", date);
    }
    if (s.people == 0) {
        add("Nobody has been recorded yet.", "");
        return rows;
    }
    if (!headed) add("Town", "");
    add("People", Count(s.people) + " recorded: " + Count(s.living) + " living, " + Count(s.dead) + " died");
    add("Where from", Count(s.bornHere) + " born here, " + Count(s.arrived) + " arrived");
    if (s.generations > 0) add("Generations", std::to_string(s.generations));
    if (s.aged > 0) {
        std::string info = Years(s.ageSum, s.aged) + " on average (" + Count(s.aged) + " recorded)";
        if (s.agedHere > 0) info += "; born here: " + Years(s.ageSumHere,
                s.agedHere) + " (" + Count(s.agedHere) + ")";
        add("Age at death", info);
    }
    if (s.dead > 0) {
        add("Deaths by cause", "");
        std::vector<uint8_t> causes;
        for (uint8_t c = kAlive + 1; c < kCauseCount; ++c)
            if (s.deathsByCause[c] > 0) causes.push_back(c);
        std::stable_sort(causes.begin(), causes.end(),
                         [&s](uint8_t a, uint8_t b) { return s.deathsByCause[a] > s.deathsByCause[b]; });
        for (const uint8_t c : causes)
            add(CauseTitle(c), Count(s.deathsByCause[c]) + " (" + Count(Percent(s.deathsByCause[c],
                    s.dead)) + "%)");
    }
    if (!s.perGeneration.empty()) {
        add("By generation", "");
        for (size_t g = 0; g < s.perGeneration.size(); ++g)
            add("Generation " + std::to_string(g + 1),
                Count(s.perGeneration[g]) + (s.perGeneration[g] == 1 ? " person, " : " people, ") +
                    Count(s.livingPerGeneration[g]) + " living");
    }
    if (!s.years.empty()) {
        add("Year by year", "");
        const int32_t current = now >= 0 ? now / 12 : -1;
        for (size_t i = s.years.size(); i-- > 0;) {
            const YearRow& y = s.years[i];
            add("Year " + std::to_string(y.year) + (y.year == current ? " (now)" : ""),
                Count(y.born) + " born, " + Count(y.arrived) + " arrived, " + Count(y.died) + " died, " +
                    Count(y.living) + (y.year == current ? " living" : " living at the end"));
        }
    }
    return rows;
}

}  // namespace FamilyTree
}  // namespace Ext
