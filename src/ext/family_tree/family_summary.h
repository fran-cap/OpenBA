#pragma once
// ============================================================================
// src/ext/family_tree/family_summary.h -- ext.family_tree's town summary (class C, with family_tree.h)
// ============================================================================
// NOT RETAIL. docs/extensions/handins/FAMILYTREE.md section 4d. What the family record says about the town as
// a whole: how many people there are and where they came from, how they died, how many generations there have
// been, and, year by year, who was born, who arrived, who died and how many were living at the end. Nothing
// is stored for it: it is worked out from the table (births, arrivals and deaths all carry a month stamp), so
// it costs nothing in the save.
//
// Standard C++ only (tests/unit/ext_family_tree_test.cpp). The window that shows it is summary_window.cpp.
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "ext/family_tree/family_tree.h"

namespace Ext {
namespace FamilyTree {

constexpr size_t kMaxSummaryYears = 1000;    // the years kept: the most recent
constexpr size_t kMaxSummaryRows = 1200;

struct YearRow {
    int32_t year = 0;        // the game year (month stamp / 12)
    size_t born = 0;         // born in the town that year
    size_t arrived = 0;      // arrived that year (founders, immigrants)
    size_t died = 0;
    size_t living = 0;       // living at the end of the year (for the current year: now)
};

struct Summary {
    size_t people = 0;
    size_t living = 0;
    size_t dead = 0;
    size_t bornHere = 0;
    size_t arrived = 0;
    int generations = 0;                       // a person with no known parent is generation 1
    std::vector<size_t> perGeneration;         // people in each generation (index 0 is generation 1)
    std::vector<size_t> livingPerGeneration;
    size_t deathsByCause[kCauseCount] = {};    // indexed by Cause; the kAlive slot stays 0
    size_t aged = 0;                           // the dead whose age at death is known
    double ageSum = 0.0;
    size_t agedHere = 0;                       // ... of them, born in the town
    double ageSumHere = 0.0;
    std::vector<YearRow> years;                // oldest first
};

// The summary of the whole table. `now` is the month stamp of now (-1: none; the last year is then the last
// one with an event). A death that was only presumed (a citizen the record lost track of, cause "other" with
// an age of 0) is counted as a death but not in the ages.
Summary Summarize(const Tree& tree, int32_t now);

// The words for each cause, capitalized ("Old age").
const char* CauseTitle(uint8_t cause);

// The summary as rows of the window: a heading row (empty info), then its lines. The years come most recent
// first.
struct SummaryRow {
    std::string label;
    std::string info;
};
// `townName` and `date` ("Year 12, Early Spring") are the window's: when either is not empty the first lines
// are a "Town" heading with a "Name" and a "Date" row (the date row is what the window refreshes in place).
std::vector<SummaryRow> SummaryRows(const Summary& summary, int32_t now,
                                    const std::string& townName = std::string(),
                                    const std::string& date = std::string());

}  // namespace FamilyTree
}  // namespace Ext
