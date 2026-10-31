// tests/unit/ext_family_tree_test.cpp -- ext.family_tree (class C, FAMILYTREE.md): the registry row and its
// codec, the table and its relations (parents, children, siblings, marriages, deaths), and the wire format
// (round trip, the live hint, and every refusal).
#include "unit.h"
#include "ext/family_tree/family_picture.h"
#include "ext/family_tree/family_summary.h"
#include "ext/family_tree/family_tree.h"
#include "ext/registry.h"
#include "ext/savetail.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace Ext::FamilyTree;

UNIT_TEST(ext_family_tree_registry_row) {
    const Ext::Entry* found = Ext::FindEntry("family_tree");
    CHECK(found != nullptr);
    if (!found) return;
    const Ext::Entry& e = *found;
    CHECK(e.tier == Ext::Tier::Extension);
    CHECK(e.cls == Ext::Class::C);           // the record is the town's own state, kept in the save tail
    CHECK(!e.defaultOn);
    CHECK(e.formatVersion == 2);   // 2: the jobs; version 1 saves still load
    CHECK(Ext::SettingsKey(e) == "ext.family_tree");
    const Ext::SaveTail::StateCodec* codec = Ext::SaveTail::FindCodec("family_tree");
    CHECK(codec != nullptr);
    if (codec) {
        CHECK(codec->encode != nullptr && codec->decode != nullptr && codec->reset != nullptr);
        CHECK(std::strcmp(codec->id, "family_tree") == 0);
    }
}

namespace {
// Anna (f) and Bram (m) arrive, marry, have Cara and Dirk; Cara marries Emil (arrived); Bram dies; Anna
// remarries Finn; Anna and Finn have Gil (half brother of Cara and Dirk).
struct Family {
    Tree t;
    uint32_t anna, bram, cara, dirk, emil, finn, gil;
    Family() {
        anna = t.Add("Anna", 0, false, 100, 20, 0, 0);
        bram = t.Add("Bram", 1, false, 100, 22, 0, 0);
        t.Marry(anna, bram, 101);
        cara = t.Add("Cara", 0, true, 110, 0, anna, bram);
        dirk = t.Add("Dirk", 1, true, 122, 0, anna, bram);
        emil = t.Add("Emil", 1, false, 150, 19, 0, 0);
        t.Marry(cara, emil, 190);
        t.Died(bram, 200, 41, kHunger);
        finn = t.Add("Finn", 1, false, 210, 30, 0, 0);
        t.Marry(anna, finn, 212);
        gil = t.Add("Gil", 1, true, 220, 0, anna, finn);
    }
};
}  // namespace

UNIT_TEST(ext_family_tree_relations) {
    Family f;
    CHECK_EQ(f.t.Size(), 7u);
    CHECK(f.anna == 1 && f.gil == 7);
    CHECK(f.t.Children(f.anna) == (std::vector<uint32_t>{f.cara, f.dirk, f.gil}));
    CHECK(f.t.Children(f.bram) == (std::vector<uint32_t>{f.cara, f.dirk}));
    CHECK(f.t.Children(f.finn) == (std::vector<uint32_t>{f.gil}));
    CHECK(f.t.Children(f.cara).empty());
    CHECK(f.t.Siblings(f.cara) == (std::vector<uint32_t>{f.dirk, f.gil}));   // Gil through the mother only
    CHECK(f.t.Siblings(f.gil) == (std::vector<uint32_t>{f.cara, f.dirk}));
    CHECK(f.t.FullSiblings(f.cara, f.dirk));
    CHECK(!f.t.FullSiblings(f.cara, f.gil));
    CHECK(!f.t.FullSiblings(f.cara, f.cara));
    CHECK(f.t.Siblings(f.anna).empty());      // no parents known: no siblings
    CHECK(f.t.Spouses(f.anna) == (std::vector<uint32_t>{f.bram, f.finn}));   // in marriage order
    CHECK(f.t.Spouses(f.bram) == (std::vector<uint32_t>{f.anna}));
    CHECK(f.t.Spouses(f.dirk).empty());
    CHECK(f.t.Spouses(999).empty() && f.t.Children(0).empty() && f.t.Siblings(999).empty());
    // the living
    CHECK(f.t.LivingIds() == (std::vector<uint32_t>{f.anna, f.cara, f.dirk, f.emil, f.finn, f.gil}));
    const Person* b = f.t.Find(f.bram);
    CHECK(b && !b->Alive() && b->died == 200 && b->ageAtDeath == 41 && b->cause == kHunger);
    CHECK(f.t.Find(0) == nullptr && f.t.Find(99) == nullptr);
}

UNIT_TEST(ext_family_tree_rules_of_the_table) {
    Tree t;
    const uint32_t a = t.Add("Anna", 0, false, 1, 20, 0, 0);
    const uint32_t b = t.Add("Bram", 1, false, 1, 20, 0, 0);
    CHECK(!t.Marry(a, a, 5));                // not to themselves
    CHECK(!t.Marry(a, 99, 5));               // not to nobody
    CHECK(t.Marry(a, b, 5));
    CHECK(!t.Marry(a, b, 6));                // already their latest marriage
    CHECK(!t.Marry(b, a, 6));
    CHECK_EQ(t.Find(a)->marriages.size(), 1u);
    // a parent id that is not in the tree is stored as unknown
    const uint32_t c = t.Add("Cara", 0, true, 9, 0, 77, a);
    CHECK(t.Find(c)->mother == 0 && t.Find(c)->father == a);
    // a negative age is stored as 0; an over-long name is cut
    const uint32_t d = t.Add(std::string(400, 'x'), 1, false, 1, -3, 0, 0);
    CHECK(t.Find(d)->joinedAge == 0 && t.Find(d)->name.size() == kMaxNameBytes);
    // a death happens once; an unknown cause is "other"
    CHECK(t.Died(a, 30, 25, 200));
    CHECK(t.Find(a)->cause == kOther);
    CHECK(!t.Died(a, 31, 26, kCold));
    CHECK(t.Find(a)->died == 30 && t.Find(a)->ageAtDeath == 25);
    CHECK(!t.Died(77, 30, 1, kCold));
    // a marriage can still be recorded for the dead (the page shows who they married)
    CHECK_EQ(t.SameNameOrdinal(a), 1);
    const uint32_t a2 = t.Add("Anna", 0, true, 40, 0, 0, 0);
    CHECK_EQ(t.SameNameOrdinal(a2), 2);
    CHECK_EQ(t.SameNameOrdinal(99), 0);
    t.Clear();
    CHECK_EQ(t.Size(), 0u);
    CHECK_EQ(t.NextId(), 1u);
}

UNIT_TEST(ext_family_tree_codec_round_trip) {
    Family f;
    f.t.FindMutable(f.cara)->live = 41;
    std::vector<uint8_t> bytes;
    f.t.Encode(bytes);
    Tree back;
    CHECK(back.Decode(bytes.data(), bytes.size()));
    CHECK_EQ(back.Size(), f.t.Size());
    CHECK_EQ(back.NextId(), f.t.NextId());
    for (const Person& p : f.t.People()) {
        const Person* q = back.Find(p.id);
        CHECK(q != nullptr);
        if (!q) continue;
        CHECK(q->name == p.name && q->gender == p.gender && q->bornHere == p.bornHere);
        CHECK(q->joined == p.joined && q->joinedAge == p.joinedAge && q->died == p.died);
        CHECK(q->ageAtDeath == p.ageAtDeath && q->cause == p.cause);
        CHECK(q->mother == p.mother && q->father == p.father && q->live == p.live);
        CHECK_EQ(q->marriages.size(), p.marriages.size());
    }
    CHECK(back.Spouses(f.anna) == (std::vector<uint32_t>{f.bram, f.finn}));
    // the next person added after a load does not reuse an id
    CHECK_EQ(back.Add("Hana", 0, true, 230, 0, f.anna, f.finn), 8u);
    // the live hint is asked for the living only, and the dead are written with none
    std::vector<uint8_t> hinted;
    f.t.Encode(hinted, [](const Person& p) { return static_cast<int32_t>(100 + p.id); });
    Tree h;
    CHECK(h.Decode(hinted.data(), hinted.size()));
    CHECK_EQ(h.Find(f.anna)->live, 101);
    CHECK_EQ(h.Find(f.bram)->live, -1);          // dead
    // an empty tree round-trips
    Tree empty;
    std::vector<uint8_t> e;
    empty.Encode(e);
    CHECK(h.Decode(e.data(), e.size()) && h.Size() == 0u);
    // the codec on the registry
    Town().Clear();
    Town().Add("Anna", 0, false, 1, 20, 0, 0);
    const Ext::SaveTail::StateCodec& codec = Codec();
    std::vector<uint8_t> payload;
    codec.encode(payload);
    codec.reset();
    CHECK_EQ(Town().Size(), 0u);
    CHECK(codec.decode(payload.data(), payload.size(), 2));
    CHECK_EQ(Town().Size(), 1u);
    CHECK(!codec.decode(payload.data(), payload.size(), 3));   // a newer layout is refused
    // a version 1 reader finds the jobs' bytes left over
    CHECK(!codec.decode(payload.data(), payload.size(), 1));
    codec.reset();
}

UNIT_TEST(ext_family_tree_codec_refuses_what_it_cannot_read) {
    Family f;
    std::vector<uint8_t> good;
    f.t.Encode(good);
    Tree t;
    // every cut of the payload is refused
    for (size_t n = 0; n < good.size(); n += 7) CHECK(!t.Decode(good.data(), n));
    CHECK_EQ(t.Size(), 0u);
    // trailing bytes
    std::vector<uint8_t> extra = good;
    extra.push_back(0);
    CHECK(!t.Decode(extra.data(), extra.size()));
    // a count far past what the payload holds
    std::vector<uint8_t> huge = good;
    huge[4] = huge[5] = huge[6] = 0xff;
    CHECK(!t.Decode(huge.data(), huge.size()));
    // an id of zero, and ids that do not ascend
    std::vector<uint8_t> zero = good;
    zero[8] = zero[9] = zero[10] = zero[11] = 0;
    CHECK(!t.Decode(zero.data(), zero.size()));
    // dangling references are dropped, not refused
    Tree d;
    d.Add("Anna", 0, false, 1, 20, 0, 0);
    d.Add("Bram", 1, true, 2, 0, 1, 0);
    std::vector<uint8_t> bytes;
    d.Encode(bytes);
    // Bram's mother field: patch it to a person that does not exist (id 9)
    // layout of person 2: after the 8-byte header and person 1; find by search for the name
    size_t at = 0;
    for (size_t i = 0; i + 4 < bytes.size(); ++i)
        if (std::memcmp(&bytes[i], "Bram", 4) == 0) { at = i; break; }
    CHECK(at != 0);
    // name(4) gender(1) flags(1) joined(4) joinedAge(4) died(4) ageAtDeath(4) cause(1) then mother(4)
    const size_t motherAt = at + 4 + 1 + 1 + 4 * 4 + 1;
    bytes[motherAt] = 9;
    Tree back;
    CHECK(back.Decode(bytes.data(), bytes.size()));
    CHECK_EQ(back.Find(2)->mother, 0u);
    // a living person with a cause, and a dead one with none, are refused
    Family g;
    std::vector<uint8_t> bad;
    g.t.Encode(bad);
    Tree x;
    Tree dead;
    dead.Add("Zed", 1, false, 1, 20, 0, 0);
    dead.Died(1, 5, 21, kCold);
    std::vector<uint8_t> db;
    dead.Encode(db);
    size_t zed = 0;
    for (size_t i = 0; i + 3 < db.size(); ++i)
        if (std::memcmp(&db[i], "Zed", 3) == 0) { zed = i; break; }
    const size_t causeAt = zed + 3 + 1 + 1 + 4 * 4;
    CHECK(db[causeAt] == kCold);
    db[causeAt] = kAlive;
    CHECK(!x.Decode(db.data(), db.size()));
}

UNIT_TEST(ext_family_tree_descendant_ids_and_the_outline) {
    Family f;
    const uint32_t hana = f.t.Add("Hana", 0, true, 230, 0, f.cara, f.emil);
    const uint32_t ivo = f.t.Add("Ivo", 1, true, 250, 0, hana, 0);
    // everyone below Anna, each once, nearest first: Cara, Dirk, Gil; then Hana; then Ivo
    CHECK(f.t.DescendantIds(f.anna) == (std::vector<uint32_t>{f.cara, f.dirk, f.gil, hana, ivo}));
    CHECK(f.t.DescendantIds(f.emil) == (std::vector<uint32_t>{hana, ivo}));   // reached through one parent
    CHECK(f.t.DescendantIds(ivo).empty() && f.t.DescendantIds(99).empty());
    const Tree::Descendants d = f.t.DescendantsOf(f.anna);
    CHECK(d.children == 3u && d.total == 5u && d.living == 5u);
    // the picture outlines the person and the descendants
    PictureOptions opt;
    CHECK(PictureHighlighted(f.t, opt).empty());              // none picked
    opt.highlight = f.cara;
    opt.highlightName = "Cara";
    CHECK(PictureHighlighted(f.t, opt) == (std::vector<uint32_t>{f.cara, hana, ivo}));
    opt.highlight = 99;
    CHECK(PictureHighlighted(f.t, opt).empty());              // not in the tree
    opt.highlight = f.cara;
    const PictureLayout lay = BuildPictureLayout(f.t, false);
    const std::string svg = RenderPictureSvg(f.t, lay, opt);
    size_t n = 0;
    for (size_t at = svg.find(PictureHighlightColor()); at != std::string::npos;
         at = svg.find(PictureHighlightColor(), at + 1))
        ++n;
    // three boxes outlined, and the color is used for nothing else
    CHECK_EQ(n, 3u);
    CHECK(svg.find("Cara and the descendants are outlined in orange") != std::string::npos);
    CHECK(RenderPictureSvg(f.t, lay, PictureOptions()).find(PictureHighlightColor()) == std::string::npos);
}

UNIT_TEST(ext_family_tree_summary_numbers) {
    // Anna, Bram arrive at 100 (year 8); Cara born 110, Dirk 122 (year 10); Emil arrives 150 (year 12)
    Family f;
    f.t.Died(f.cara, 300, 15, kCold);
    const Summary s = Summarize(f.t, 400);
    CHECK_EQ(s.people, 7u);
    CHECK_EQ(s.dead, 2u);            // Bram (hunger, age 41) and Cara
    CHECK_EQ(s.living, 5u);
    CHECK_EQ(s.bornHere, 3u);        // Cara, Dirk, Gil
    CHECK_EQ(s.arrived, 4u);
    CHECK_EQ(s.deathsByCause[kHunger], 1u);
    CHECK_EQ(s.deathsByCause[kCold], 1u);
    CHECK_EQ(s.deathsByCause[kOldAge], 0u);
    CHECK_EQ(s.aged, 2u);
    CHECK(s.ageSum == 56.0);         // 41 + 15
    CHECK_EQ(s.agedHere, 1u);        // Cara
    CHECK(s.ageSumHere == 15.0);
    // generations: Anna, Bram, Emil, Finn have no known parents (1); Cara, Dirk, Gil are 2
    CHECK_EQ(s.generations, 2);
    CHECK_EQ(s.perGeneration[0], 4u);
    CHECK_EQ(s.perGeneration[1], 3u);
    CHECK_EQ(s.livingPerGeneration[0], 3u);   // Bram died
    CHECK_EQ(s.livingPerGeneration[1], 2u);   // Cara died
    // the years: 100/12 = 8 up to 400/12 = 33
    CHECK(!s.years.empty());
    CHECK_EQ(s.years.front().year, 8);
    CHECK_EQ(s.years.back().year, 33);
    CHECK_EQ(s.years.size(), 26u);
    const YearRow& y8 = s.years[0];
    CHECK(y8.arrived == 2u && y8.born == 0u && y8.died == 0u && y8.living == 2u);   // Anna and Bram
    CHECK(s.years.back().living == 5u);
    // an empty table has nothing
    Tree none;
    const Summary e = Summarize(none, 10);
    CHECK(e.people == 0u && e.years.empty() && e.generations == 0);
    const std::vector<SummaryRow> rows = SummaryRows(e, 10);
    CHECK(rows.size() == 1u && rows[0].label == "Nobody has been recorded yet.");
}

UNIT_TEST(ext_family_tree_summary_rows_read_well) {
    Family f;
    f.t.Died(f.cara, 300, 15, kCold);
    const std::vector<SummaryRow> rows = SummaryRows(Summarize(f.t, 400), 400);
    CHECK(!rows.empty());
    auto find = [&rows](const std::string& label) -> const SummaryRow* {
        for (const SummaryRow& r : rows)
            if (r.label == label) return &r;
        return nullptr;
    };
    CHECK(find("People") && find("People")->info == "7 recorded: 5 living, 2 died");
    CHECK(find("Where from") && find("Where from")->info == "3 born here, 4 arrived");
    CHECK(find("Generations") && find("Generations")->info == "2");
    CHECK(find("Age at death")
          && find("Age at death")->info == "28 y on average (2 recorded); born here: 15 y (1)");
    CHECK(find("Hunger") && find("Hunger")->info == "1 (50%)");
    CHECK(find("Cold") && find("Cold")->info == "1 (50%)");
    CHECK(find("Old age") == nullptr);                 // no deaths of that kind: no row
    CHECK(find("Generation 1") && find("Generation 1")->info == "4 people, 3 living");
    CHECK(find("Generation 2") && find("Generation 2")->info == "3 people, 2 living");
    CHECK(find("Year 33 (now)") != nullptr);
    CHECK(find("Year 8") != nullptr);
    // the headings come in order, and the years most recent first
    size_t town = 0, deaths = 0, gens = 0, years = 0;
    for (size_t i = 0; i < rows.size(); ++i) {
        if (rows[i].label == "Town") town = i;
        if (rows[i].label == "Deaths by cause") deaths = i;
        if (rows[i].label == "By generation") gens = i;
        if (rows[i].label == "Year by year") years = i;
    }
    CHECK(town == 0 && town < deaths && deaths < gens && gens < years);
    CHECK(rows[years + 1].label == "Year 33 (now)");
    CHECK(rows.back().label == "Year 8");
}

UNIT_TEST(ext_family_tree_summary_rows_name_the_town_and_the_date) {
    Family f;
    const std::vector<SummaryRow> rows = SummaryRows(Summarize(f.t, 400), 400, "Oakmere",
                                                     "Year 33, Early Spring");
    CHECK(rows.size() > 3u);
    CHECK(rows[0].label == "Town" && rows[0].info.empty());
    CHECK(rows[1].label == "Name" && rows[1].info == "Oakmere");
    CHECK(rows[2].label == "Date" && rows[2].info == "Year 33, Early Spring");
    CHECK(rows[3].label == "People");
    size_t headings = 0;
    for (const SummaryRow& r : rows)
        if (r.label == "Town") ++headings;
    CHECK(headings == 1u);   // the heading is not repeated above the people lines
    // an empty record still names the town and the date, then says nobody is recorded
    Tree none;
    const std::vector<SummaryRow> empty = SummaryRows(Summarize(none, 10), 10, "Oakmere",
                                                      "Year 0, Mid Winter");
    CHECK(empty.size() == 4u && empty[1].info == "Oakmere" && empty[2].info == "Year 0, Mid Winter" &&
          empty[3].label == "Nobody has been recorded yet.");
    // with neither (the engine-free default) the rows are the ones above: no Name or Date line
    const std::vector<SummaryRow> bare = SummaryRows(Summarize(f.t, 400), 400);
    CHECK(bare[0].label == "Town" && bare[1].label == "People");
}

UNIT_TEST(ext_family_tree_summary_years_count_the_living) {
    Tree t;
    // joined at month 0 (year 0); a death in year 2; a birth in year 3; a person who died before the shown
    // years
    const uint32_t a = t.Add("A", 0, false, 0, 20, 0, 0);
    const uint32_t b = t.Add("B", 1, false, 5, 20, 0, 0);
    t.Died(a, 12 * 2 + 3, 22, kOldAge);
    const uint32_t c = t.Add("C", 0, true, 12 * 3, 0, a, b);
    (void)c;
    const Summary s = Summarize(t, 12 * 4);
    CHECK_EQ(s.years.size(), 5u);                       // years 0..4
    CHECK_EQ(s.years[0].arrived, 2u);
    CHECK_EQ(s.years[0].living, 2u);
    CHECK_EQ(s.years[1].living, 2u);
    CHECK(s.years[2].died == 1u && s.years[2].living == 1u);
    CHECK(s.years[3].born == 1u && s.years[3].living == 2u);
    CHECK_EQ(s.years[4].living, 2u);
    // too many years keeps the latest ones, the earlier people counted as living at the start
    Tree long_;
    long_.Add("Old", 0, false, 0, 20, 0, 0);
    const Summary l = Summarize(long_, 12 * 5000);
    CHECK_EQ(l.years.size(), kMaxSummaryYears);
    CHECK_EQ(l.years.front().living, 1u);
    CHECK_EQ(l.years.back().year, 5000);
    CHECK_EQ(l.years.front().born + l.years.front().arrived, 0u);   // their arrival is before the years shown
}

UNIT_TEST(ext_family_tree_name_search) {
    CHECK(NameMatches("Anna (2)", ""));
    CHECK(NameMatches("Anna (2)", "   "));
    CHECK(NameMatches("Anna (2)", "ann"));
    CHECK(NameMatches("Anna (2)", "ANNA"));                 // case does not matter
    CHECK(NameMatches("Anna (2)", "nna"));                  // a part of a word
    CHECK(NameMatches("Anna (2)", "anna 2"));               // every word, in any order
    CHECK(NameMatches("Anna (2)", "(2) anna"));
    CHECK(!NameMatches("Anna (2)", "anna 3"));
    CHECK(!NameMatches("Anna", "bram"));
    CHECK(NameMatches("Anna", "  anna  "));                 // spaces around are ignored
    CHECK(!NameMatches("", "a"));
    CHECK(NameMatches("", ""));
}

UNIT_TEST(ext_family_tree_descendants_are_counted_once) {
    Family f;   // Anna: Cara, Dirk (with Bram), Gil (with Finn); Cara and Emil; Bram dead
    const Tree::Descendants anna = f.t.DescendantsOf(f.anna);
    CHECK_EQ(anna.children, 3u);
    CHECK_EQ(anna.total, 3u);
    CHECK_EQ(anna.living, 3u);
    const Tree::Descendants bram = f.t.DescendantsOf(f.bram);
    CHECK(bram.children == 2u && bram.total == 2u && bram.living == 2u);   // the dead have descendants too
    const Tree::Descendants dirk = f.t.DescendantsOf(f.dirk);
    CHECK(dirk.children == 0u && dirk.total == 0u && dirk.living == 0u);
    CHECK_EQ(f.t.DescendantsOf(99).total, 0u);
    // grandchildren and a great-grandchild: counted once, the dead counted in the total but not as living
    const uint32_t hana = f.t.Add("Hana", 0, true, 230, 0, f.cara, f.emil);
    const uint32_t ivo = f.t.Add("Ivo", 1, true, 250, 0, hana, 0);
    const uint32_t jan = f.t.Add("Jan", 1, true, 252, 0, f.cara, f.emil);
    f.t.Died(jan, 260, 8, kDisease);
    const Tree::Descendants a2 = f.t.DescendantsOf(f.anna);
    CHECK_EQ(a2.children, 3u);
    CHECK_EQ(a2.total, 6u);          // Cara, Dirk, Gil, Hana, Jan, Ivo
    CHECK_EQ(a2.living, 5u);         // Jan died
    const Tree::Descendants c2 = f.t.DescendantsOf(f.cara);
    CHECK(c2.children == 2u && c2.total == 3u);
    // a child of two people in the same family is one descendant, not two: Hana's parents are both Anna's
    // descendants
    CHECK_EQ(f.t.DescendantsOf(f.emil).total, 3u);
    (void)ivo;
}

UNIT_TEST(ext_family_tree_jobs_are_credited_in_units) {
    Tree t;
    const uint32_t a = t.Add("Anna", 0, false, 1, 20, 0, 0);
    const uint8_t farmer = t.JobIndex("Farmer");
    const uint8_t miner = t.JobIndex("Miner");
    CHECK(farmer == 0 && miner == 1 && t.JobIndex("Farmer") == farmer);
    CHECK(t.JobIndex("") == kNoJob);
    CHECK(t.JobName(miner) == "Miner" && t.JobName(200).empty());
    CHECK(!t.AddJobTime(99, farmer, 1.0f) && !t.AddJobTime(a, 9, 1.0f));      // unknown person, unknown job
    CHECK(!t.AddJobTime(a, farmer, 0.0f) && !t.AddJobTime(a, farmer, -1.0f));
    // a little at a time adds up: ten sweeps of 0.034 month are three units
    for (int i = 0; i < 10; ++i) CHECK(t.AddJobTime(a, farmer, 0.034f));
    CHECK_EQ(t.Find(a)->jobs.size(), 1u);
    CHECK_EQ(static_cast<int>(t.Find(a)->jobs[0].units), 3);
    // a change of job rounds what was waiting for the old one to the nearest unit (0.04 of a unit's 0.1 is 0)
    CHECK(t.AddJobTime(a, miner, 1.05f));
    CHECK_EQ(static_cast<int>(t.Find(a)->jobs[0].units), 3);
    CHECK_EQ(static_cast<int>(t.JobsOf(a)[0].units), 10);                      // the miner now leads
    CHECK(t.JobsOf(a)[0].job == miner && t.JobsOf(a)[1].job == farmer);
    // a year is 120 units; the counter stops at its largest
    CHECK(t.AddJobTime(a, farmer, 100000.0f));
    CHECK_EQ(static_cast<int>(t.JobsOf(a)[0].units), 65535);
    CHECK(t.JobsOf(99).empty());
}

UNIT_TEST(ext_family_tree_jobs_keep_at_most_a_dozen) {
    Tree t;
    const uint32_t a = t.Add("Anna", 0, false, 1, 20, 0, 0);
    for (int i = 0; i < 12; ++i) {
        const uint8_t j = t.JobIndex("Job" + std::to_string(i));
        CHECK(t.AddJobTime(a, j, 0.1f * static_cast<float>(i + 1)));
    }
    CHECK_EQ(t.Find(a)->jobs.size(), kMaxJobsPerPerson);
    // a thirteenth takes the place of the one with the least time, and keeps that time
    const uint8_t extra = t.JobIndex("Extra");
    CHECK(t.AddJobTime(a, extra, 0.5f));
    CHECK_EQ(t.Find(a)->jobs.size(), kMaxJobsPerPerson);
    bool gone = true, there = false;
    for (const JobTime& j : t.Find(a)->jobs) {
        if (t.JobName(j.job) == "Job0") gone = false;
        if (j.job == extra) there = true;
    }
    CHECK(gone && there);
    // the table of names stops at kMaxJobNames
    Tree n;
    for (size_t i = 0; i < kMaxJobNames; ++i) CHECK(n.JobIndex("N" + std::to_string(i)) != kNoJob);
    CHECK(n.JobIndex("one too many") == kNoJob);
    CHECK_EQ(n.JobNameCount(), kMaxJobNames);
}

UNIT_TEST(ext_family_tree_jobs_in_words) {
    CHECK(Tree::JobSpanText(0) == "under 1 mo");
    CHECK(Tree::JobSpanText(4) == "under 1 mo");     // 0.4 month
    CHECK(Tree::JobSpanText(5) == "1 mo");
    CHECK(Tree::JobSpanText(50) == "5 mo");
    CHECK(Tree::JobSpanText(119) == "12 mo" || Tree::JobSpanText(119) == "1 y");
    CHECK(Tree::JobSpanText(120) == "1 y");
    CHECK(Tree::JobSpanText(1440) == "12 y");
    Tree t;
    const uint32_t a = t.Add("Anna", 0, false, 1, 20, 0, 0);
    CHECK(t.JobsText(a, 3).empty());
    const char* names[] = {"Child", "Farmer", "Miner", "Hunter", "Baker"};
    for (int i = 0; i < 5; ++i) t.AddJobTime(a, t.JobIndex(names[i]), 12.0f * static_cast<float>(i + 1));
    CHECK(t.JobsText(a, 3) == "Baker 5 y, Hunter 4 y, Miner 3 y and 2 more");
    CHECK(t.JobsText(a, 5) == "Baker 5 y, Hunter 4 y, Miner 3 y, Farmer 2 y, Child 1 y");
    CHECK(t.JobsText(a, 1, [](const std::string& k) { return k + "!"; }) == "Baker! 5 y and 4 more");
    CHECK(t.JobsText(99, 3).empty());
}

UNIT_TEST(ext_family_tree_jobs_round_trip_and_old_saves) {
    Family f;
    const uint8_t farmer = f.t.JobIndex("Farmer");
    const uint8_t child = f.t.JobIndex("Child");
    f.t.AddJobTime(f.cara, child, 24.0f);
    f.t.AddJobTime(f.cara, farmer, 36.0f);
    f.t.AddJobTime(f.anna, farmer, 12.0f);
    std::vector<uint8_t> bytes;
    f.t.Encode(bytes);
    Tree back;
    CHECK(back.Decode(bytes.data(), bytes.size()));
    CHECK_EQ(back.JobNameCount(), 2u);
    CHECK(back.JobsText(f.cara, 4) == "Farmer 3 y, Child 2 y");
    CHECK(back.JobsText(f.anna, 4) == "Farmer 1 y");
    CHECK(back.JobsText(f.bram, 4).empty());
    CHECK_EQ(back.Find(f.cara)->jobs[0].units, f.t.Find(f.cara)->jobs[0].units);
    // the sub-unit part waiting is not saved
    CHECK(back.Find(f.cara)->carryJob == kNoJob && back.Find(f.cara)->carryMonths == 0.0f);
    // a version 1 payload loads with no jobs, and is written back as version 2
    std::vector<uint8_t> old;
    f.t.Encode(old, nullptr, 1);
    CHECK(old.size() < bytes.size());
    Tree legacy;
    CHECK(legacy.Decode(old.data(), old.size(), 1));
    CHECK_EQ(legacy.Size(), f.t.Size());
    CHECK(legacy.JobsText(f.cara, 4).empty() && legacy.JobNameCount() == 0u);
    CHECK(!legacy.Decode(old.data(), old.size(), 2));       // the jobs' bytes are missing
    CHECK(!legacy.Decode(bytes.data(), bytes.size(), 1));   // and left over
    CHECK(!legacy.Decode(bytes.data(), bytes.size(), 3));   // a layout we do not know
    // a job that points past the names is refused; so is one with no time, and a repeat of a job
    Tree one;
    const uint32_t p = one.Add("Zed", 1, false, 1, 20, 0, 0);
    one.AddJobTime(p, one.JobIndex("Farmer"), 3.0f);
    std::vector<uint8_t> z;
    one.Encode(z);
    size_t at = 0;
    for (size_t i = 0; i + 3 < z.size(); ++i)
        if (std::memcmp(&z[i], "Zed", 3) == 0) { at = i; break; }
    CHECK(at != 0);
    // name(3) gender(1) flags(1) joined joinedAge died ageAtDeath(16) cause(1) mother father live(12)
    // marriages(1) jobs(1)
    const size_t jobAt = at + 3 + 1 + 1 + 16 + 1 + 12 + 1 + 1;
    CHECK(z[jobAt - 1] == 1 && z[jobAt] == 0);
    std::vector<uint8_t> bad = z;
    bad[jobAt] = 5;
    Tree x;
    CHECK(!x.Decode(bad.data(), bad.size()));
    bad = z;
    bad[jobAt + 1] = bad[jobAt + 2] = 0;
    CHECK(!x.Decode(bad.data(), bad.size()));
    CHECK(x.Decode(z.data(), z.size()));
    CHECK_EQ(x.Find(p)->jobs.size(), 1u);
}

#include "ext/family_tree/tree_layout.h"

namespace {
const TreeNode* NodeOf(const TreeLayout& l, uint32_t id) {
    for (const TreeNode& n : l.nodes)
        if (n.id == id) return &n;
    return nullptr;
}
}  // namespace

UNIT_TEST(ext_family_tree_layout_parent_with_children) {
    Family f;
    // arrived, so no parents; two husbands, three children
    const TreeLayout l = BuildTreeLayout(f.t, f.anna);
    CHECK_EQ(l.nodes.size(), 6u);
    const TreeNode* anna = NodeOf(l, f.anna);
    const TreeNode* bram = NodeOf(l, f.bram);
    const TreeNode* finn = NodeOf(l, f.finn);
    CHECK(anna && bram && finn);
    if (!anna || !bram || !finn) return;
    CHECK(anna->row == 2 && bram->row == 2 && finn->row == 2);
    CHECK_EQ(bram->x - anna->x, 4);
    CHECK_EQ(finn->x - bram->x, 4);
    // children centered under the gap between Anna and her first husband, 4 tiles apart
    const TreeNode* cara = NodeOf(l, f.cara);
    const TreeNode* dirk = NodeOf(l, f.dirk);
    const TreeNode* gil = NodeOf(l, f.gil);
    CHECK(cara && dirk && gil);
    if (!cara || !dirk || !gil) return;
    CHECK(cara->row == 3 && dirk->row == 3 && gil->row == 3);
    CHECK_EQ(dirk->x - cara->x, 4);
    CHECK_EQ(gil->x - dirk->x, 4);
    CHECK_EQ(dirk->x, anna->x + 2);
    CHECK(l.rowUsed[2] && l.rowUsed[3] && !l.rowUsed[1] && !l.rowUsed[0]);
    // the line row under the couple: the stem comes down at the gap, the bar runs from Cara to Gil
    const std::vector<uint8_t>& row = l.link[2];
    CHECK_EQ(row[static_cast<size_t>(cara->x)], static_cast<uint8_t>(kArmRight | kArmDown));
    CHECK_EQ(row[static_cast<size_t>(dirk->x)],
             static_cast<uint8_t>(kArmUp | kArmRight | kArmDown | kArmLeft));
    CHECK_EQ(row[static_cast<size_t>(dirk->x + 1)], static_cast<uint8_t>(kArmRight | kArmLeft));
    CHECK_EQ(row[static_cast<size_t>(gil->x)], static_cast<uint8_t>(kArmLeft | kArmDown));
    bool stem = false;
    for (const TreeGap& g : l.gaps)
        if (g.row == 2 && g.x == anna->x + 2) stem = g.stem;
    CHECK(stem);
    for (const TreeNode& n : l.nodes) CHECK(n.x >= 1 && n.x + 1 < l.width);
}

UNIT_TEST(ext_family_tree_layout_siblings_and_spouse) {
    Family f;
    // parents Anna and Bram, siblings Dirk and Gil, husband Emil
    const TreeLayout l = BuildTreeLayout(f.t, f.cara);
    const TreeNode* cara = NodeOf(l, f.cara);
    const TreeNode* dirk = NodeOf(l, f.dirk);
    const TreeNode* gil = NodeOf(l, f.gil);
    const TreeNode* emil = NodeOf(l, f.emil);
    const TreeNode* anna = NodeOf(l, f.anna);
    const TreeNode* bram = NodeOf(l, f.bram);
    CHECK(cara && dirk && gil && emil && anna && bram);
    if (!(cara && dirk && gil && emil && anna && bram)) return;
    CHECK(dirk->row == 2 && gil->row == 2 && emil->row == 2);
    // siblings, then the person, then the spouse
    CHECK(dirk->x < gil->x && gil->x < cara->x && cara->x < emil->x);
    CHECK_EQ(cara->x - gil->x, 4);
    CHECK(anna->row == 1 && bram->row == 1);
    CHECK_EQ(l.rowUsed[0], false);                                     // neither parent's parents are known
    CHECK_EQ(l.rowUsed[3], false);                                     // no children
    // the parents are a couple around the middle of the siblings and the person; one gap tile with the stem
    const int mid = (dirk->x + cara->x) / 2;
    CHECK_EQ(bram->x, mid - 2);
    CHECK_EQ(anna->x, mid + 2);
    CHECK_EQ(l.link[1][static_cast<size_t>(mid)] & kArmUp, static_cast<int>(kArmUp));
    CHECK_EQ(l.link[1][static_cast<size_t>(dirk->x)], static_cast<uint8_t>(kArmRight | kArmDown));
    CHECK_EQ(l.link[1][static_cast<size_t>(cara->x)], static_cast<uint8_t>(kArmLeft | kArmDown));
}

UNIT_TEST(ext_family_tree_layout_grandparents_and_unknown) {
    Tree t;
    const uint32_t a = t.Add("Ada", 0, false, 1, 50, 0, 0);
    const uint32_t b = t.Add("Bo", 1, false, 1, 50, 0, 0);
    const uint32_t c = t.Add("Cy", 1, false, 1, 50, 0, 0);
    t.Marry(a, b, 2);
    const uint32_t d = t.Add("Di", 0, true, 5, 0, a, b);     // Ada and Bo's daughter
    t.Marry(d, c, 8);
    const uint32_t e = t.Add("Ed", 1, true, 20, 0, d, c);    // the grandson
    const TreeLayout l = BuildTreeLayout(t, e);
    const TreeNode* nd = NodeOf(l, d);
    const TreeNode* nc = NodeOf(l, c);
    const TreeNode* na = NodeOf(l, a);
    const TreeNode* nb = NodeOf(l, b);
    CHECK(nd && nc && na && nb);
    if (!(nd && nc && na && nb)) return;
    CHECK(na->row == 0 && nb->row == 0 && nd->row == 1 && nc->row == 1);
    // Di has known parents and Cy has none: the couple is wide (8 apart) so Di's parents have room
    CHECK_EQ(nd->x - nc->x, 8);
    CHECK_EQ(nb->x, nd->x - 2);   // Di's father Bo on the left, her mother Ada on the right
    CHECK_EQ(na->x, nd->x + 2);
    CHECK_EQ(l.link[0][static_cast<size_t>(nd->x)], static_cast<uint8_t>(kArmUp | kArmDown));
    CHECK(BuildTreeLayout(t, 9999).nodes.empty());
    CHECK_EQ(BuildTreeLayout(t, 9999).width, 0);
}

#include "ext/family_tree/family_picture.h"

namespace {
const PictureBox* BoxFor(const PictureLayout& l, uint32_t id) {
    for (const PictureBox& b : l.boxes)
        if (b.id == id) return &b;
    return nullptr;
}

// Founders Adam and Eva, their children Ann (marries the immigrant Ivo), Ben and Cy (dies childless), and Ann
// and Ivo's child Dot. Adam dies.
struct Town3 {
    Tree t;
    uint32_t adam, eva, ann, ben, cy, ivo, dot;
    Town3() {
        adam = t.Add("Adam", 1, false, 0, 20, 0, 0);
        eva = t.Add("Eva & Co", 0, false, 0, 19, 0, 0);
        t.Marry(adam, eva, 1);
        ann = t.Add("Ann", 0, true, 24, 0, eva, adam);
        ben = t.Add("Ben", 1, true, 30, 0, eva, adam);
        cy = t.Add("Cy", 1, true, 36, 0, eva, adam);
        ivo = t.Add("Ivo", 1, false, 200, 22, 0, 0);
        t.Marry(ann, ivo, 210);
        dot = t.Add("Dot", 0, true, 220, 0, ann, ivo);
        t.Died(adam, 300, 40, kOldAge);
        t.Died(cy, 100, 5, kDisease);
    }
};
}  // namespace

UNIT_TEST(ext_family_tree_picture_generations_settle_with_a_contradicting_marriage) {
    // A widow marries her stepson: by his father she is a generation above him, by the marriage level with
    // him. The rows must not run away (a town of 80 once drew "1605 generations").
    Tree t;
    const uint32_t hal = t.Add("Hal", 1, false, 0, 30, 0, 0);
    const uint32_t wen = t.Add("Wen", 0, false, 0, 28, 0, 0);
    t.Marry(hal, wen, 1);
    const uint32_t sam = t.Add("Sam", 1, true, 20, 0, 0, hal);
    t.Marry(wen, sam, 200);
    t.Add("Tia", 0, true, 220, 0, wen, sam);
    const PictureLayout l = BuildPictureLayout(t, false);
    CHECK_EQ(l.people, 4);
    CHECK(l.gens >= 2 && l.gens <= 4);
    for (size_t i = 0; i < l.boxes.size(); ++i)
        for (size_t j = i + 1; j < l.boxes.size(); ++j)
            if (l.boxes[i].gen == l.boxes[j].gen)
                CHECK(l.boxes[i].x + l.boxes[i].w <= l.boxes[j].x
                      || l.boxes[j].x + l.boxes[j].w <= l.boxes[i].x);
}

UNIT_TEST(ext_family_tree_picture_loop_keeps_other_children_below_their_parents) {
    // A grandson marries his grandmother (a loop between marriage clusters); the unrelated family beside it
    // is not touched.
    Tree t;
    const uint32_t gus = t.Add("Gus", 1, false, 0, 30, 0, 0);
    const uint32_t gia = t.Add("Gia", 0, false, 0, 28, 0, 0);
    t.Marry(gus, gia, 1);
    const uint32_t pat = t.Add("Pat", 1, true, 20, 0, gia, gus);
    const uint32_t cal = t.Add("Cal", 1, true, 200, 0, 0, pat);
    t.Marry(gia, cal, 400);
    const uint32_t ann = t.Add("Ann", 0, false, 0, 30, 0, 0);
    const uint32_t bob = t.Add("Bob", 1, false, 0, 30, 0, 0);
    t.Marry(ann, bob, 1);
    const uint32_t kid = t.Add("Kid", 0, true, 20, 0, ann, bob);
    const PictureLayout l = BuildPictureLayout(t, false);
    int rowOf[16] = {};
    for (const PictureBox& b : l.boxes) rowOf[b.id] = b.gen;
    CHECK(rowOf[kid] > rowOf[ann] && rowOf[kid] > rowOf[bob]);
    CHECK(l.gens <= 5);
    CHECK_EQ(l.boxes.size(), 7u);
}

UNIT_TEST(ext_family_tree_picture_main_job_line) {
    Town3 w;
    const uint8_t farm = w.t.JobIndex("Farmer");
    const uint8_t mine = w.t.JobIndex("Miner");
    w.t.AddJobTime(w.ann, mine, 10.0f);
    w.t.AddJobTime(w.ann, farm, 30.0f);   // the longest
    PictureJobs pj;
    pj.nameOf = [](const std::string& key) { return "<" + key + ">"; };
    const PictureLayout plain = BuildPictureLayout(w.t, false);
    const PictureLayout withJobs = BuildPictureLayout(w.t, false, &pj);
    CHECK(!plain.jobs && plain.boxHeight == kPictureBoxHeight);
    CHECK(withJobs.jobs && withJobs.boxHeight == kPictureBoxHeightWithJob);
    CHECK_EQ(withJobs.boxes.size(), plain.boxes.size());
    for (const PictureBox& b : withJobs.boxes) {
        if (b.id == w.ann) CHECK(b.job.compare(0, 8, "<Farmer>") == 0);
        else CHECK(b.job.empty());   // no job time recorded for anyone else
    }
    CHECK(CanvasFor(withJobs).rowStep > CanvasFor(plain).rowStep);
    PictureOptions opt;
    const std::string svg = RenderPictureSvg(w.t, withJobs, opt);
    CHECK(svg.find("&lt;Farmer&gt;") != std::string::npos);
    CHECK(RenderPictureSvg(w.t, plain, opt).find("Farmer") == std::string::npos);
}

UNIT_TEST(ext_family_tree_picture_layout_everyone) {
    Town3 w;
    const PictureLayout l = BuildPictureLayout(w.t, false);
    CHECK_EQ(l.people, 7);
    CHECK_EQ(l.boxes.size(), 7u);
    CHECK_EQ(l.gens, 3);
    // no two boxes of a row overlap
    for (size_t i = 0; i < l.boxes.size(); ++i)
        for (size_t j = i + 1; j < l.boxes.size(); ++j)
            if (l.boxes[i].gen == l.boxes[j].gen) {
                const PictureBox& a = l.boxes[i];
                const PictureBox& b = l.boxes[j];
                CHECK(a.x + a.w <= b.x || b.x + b.w <= a.x);
            }
    // a marriage is one row, side by side, the spouse gap apart; a child is one row below its parents
    const PictureBox* adam = BoxFor(l, w.adam);
    const PictureBox* eva = BoxFor(l, w.eva);
    const PictureBox* ann = BoxFor(l, w.ann);
    const PictureBox* ivo = BoxFor(l, w.ivo);
    const PictureBox* dot = BoxFor(l, w.dot);
    CHECK(adam && eva && ann && ivo && dot);
    if (!adam || !eva || !ann || !ivo || !dot) return;
    CHECK_EQ(adam->gen, 0);
    CHECK_EQ(eva->gen, 0);
    const PictureBox* left = adam->x < eva->x ? adam : eva;
    const PictureBox* right = left == adam ? eva : adam;
    CHECK_EQ(right->x - (left->x + left->w), kPictureSpouseGap);
    CHECK_EQ(ann->gen, 1);
    CHECK_EQ(ivo->gen, 1);   // the immigrant stands in the row of the person they married
    CHECK_EQ(dot->gen, 2);
    CHECK_EQ(l.couples.size(), 2u);
    CHECK_EQ(l.families.size(), 2u);
}

UNIT_TEST(ext_family_tree_picture_living_only) {
    Town3 w;
    const PictureLayout l = BuildPictureLayout(w.t, true);
    CHECK(BoxFor(l, w.adam) != nullptr);   // dead, but the husband of a living woman and an ancestor
    CHECK(BoxFor(l, w.dot) != nullptr);
    CHECK(BoxFor(l, w.cy) == nullptr);     // dead, no children
    CHECK_EQ(l.people, 6);
    CHECK(BuildPictureLayout(Tree(), false).boxes.empty());
}

UNIT_TEST(ext_family_tree_picture_svg) {
    Town3 w;
    PictureOptions opt;
    opt.now = 12 * 27;
    const std::string svg = RenderPictureSvg(w.t, BuildPictureLayout(w.t, false), opt);
    CHECK(svg.compare(0, 5, "<?xml") == 0);
    CHECK(svg.find("<svg ") != std::string::npos && svg.find("</svg>") != std::string::npos);
    CHECK(svg.find("Village family tree") != std::string::npos);
    CHECK(svg.find("Eva &amp; Co") != std::string::npos);   // text is escaped
    CHECK(svg.find("Eva & Co") == std::string::npos);
    CHECK(svg.find("died Year 25 (age 40)") != std::string::npos);
    CHECK(svg.find("Year 27") != std::string::npos);
    // two families, so two colored lines
    size_t paths = 0;
    for (size_t at = svg.find("<path stroke=\"#"); at != std::string::npos; at = svg.find("<path stroke=\"#",
            at + 1)) ++paths;
    CHECK_EQ(paths, 2u);
}

UNIT_TEST(ext_family_tree_picture_svg_big_family_is_whole) {
    Tree t;
    const uint32_t a = t.Add("Adam", 1, false, 0, 20, 0, 0);
    const uint32_t e = t.Add("Eva", 0, false, 0, 19, 0, 0);
    t.Marry(a, e, 1);
    for (int i = 0; i < 100; ++i) t.Add("Kid" + std::to_string(i), i % 2, true, 24 + i, 0, e, a);
    const std::string svg = RenderPictureSvg(t, BuildPictureLayout(t, false), PictureOptions());
    size_t circles = 0;
    for (size_t at = svg.find("<circle"); at != std::string::npos; at = svg.find("<circle",
            at + 1)) ++circles;
    CHECK_EQ(circles, 100u);
    // the family's one path holds a drop for every child and is closed properly
    const size_t p = svg.find("<path stroke=\"#");
    CHECK(p != std::string::npos);
    const size_t end = svg.find("\"/>\n", p);
    CHECK(end != std::string::npos && end - p > 1024);
    CHECK(svg.compare(svg.size() - 7, 7, "</svg>\n") == 0);
}

UNIT_TEST(ext_family_tree_picture_svg_village_name) {
    Town3 w;
    PictureOptions opt;
    opt.village = "Pine <Hollow> & Co";
    const std::string svg = RenderPictureSvg(w.t, BuildPictureLayout(w.t, false), opt);
    CHECK(svg.find(">Pine &lt;Hollow&gt; &amp; Co</text>") != std::string::npos);   // the title, escaped
    // and the kind of picture under it
    CHECK(svg.find(">Village family tree</text>") != std::string::npos);
    opt.village.clear();
    const std::string plain = RenderPictureSvg(w.t, BuildPictureLayout(w.t, false), opt);
    CHECK(plain.find(">Village family tree</text>") != std::string::npos);
}

// ---- the PNG: the same layout as pixels -------------------------------------------------------------------

#include "ext/family_tree/family_raster.h"

namespace {
// Every character a 5 x 8 block, whatever the size: enough to see where text lands.
class BlockGlyphs final : public GlyphSource {
public:
    int asked = 0;
    bool Glyphs(int cellPx, const std::vector<int32_t>& cps, Ext::Resources::RasterFont& out,
                std::string*) override {
        ++asked;
        out = Ext::Resources::RasterFont();
        out.height = cellPx;
        for (const int32_t c : cps) {
            Ext::Resources::RasterGlyph g;
            g.codepoint = c;
            g.width = 5;
            g.height = 8;
            g.postAdvance = 5;
            g.postKern = 1;
            g.alpha.assign(40, 255);
            out.glyphs.push_back(g);
        }
        return true;
    }
};

class NoGlyphs final : public GlyphSource {
public:
    bool Glyphs(int, const std::vector<int32_t>&, Ext::Resources::RasterFont&, std::string* why) override {
        if (why) *why = "no face";
        return false;
    }
};

void PixelAt(const PictureRaster& r, int x, int y, int rgb[3]) {
    const size_t at = (static_cast<size_t>(y) * static_cast<size_t>(r.width) + static_cast<size_t>(x)) * 4u;
    for (int i = 0; i < 3; ++i) rgb[i] = r.rgba[at + static_cast<size_t>(i)];
}
}  // namespace

UNIT_TEST(ext_family_tree_picture_raster_draws_the_layout) {
    Town3 w;
    const PictureLayout l = BuildPictureLayout(w.t, false);
    const PictureCanvas canvas = CanvasFor(l);
    BlockGlyphs fonts;
    PictureRaster r;
    PictureOptions opt;
    opt.village = "Pine Hollow";
    CHECK(RenderPictureRaster(w.t, l, opt, fonts, r));
    CHECK_EQ(r.width, canvas.width);
    CHECK_EQ(r.height, canvas.height);
    CHECK_EQ(r.rgba.size(), static_cast<size_t>(r.width) * static_cast<size_t>(r.height) * 4u);
    CHECK(r.scale == 1.0);
    CHECK(r.textDrawn);
    int px[3];
    PixelAt(r, 0, 0, px);   // the corner is the white ground
    CHECK(px[0] == 255 && px[1] == 255 && px[2] == 255);
    // Ann (alive, a woman) is a pink box, Adam (dead, a man) a grey one: a pixel just inside each box's top
    // left
    const PictureBox* ann = BoxFor(l, w.ann);
    const PictureBox* adam = BoxFor(l, w.adam);
    CHECK(ann && adam);
    if (!ann || !adam) return;
    PixelAt(r, ann->x + 8, canvas.RowY(ann->gen) + 4, px);
    CHECK(px[0] == 0xfd && px[1] == 0xe8 && px[2] == 0xef);
    PixelAt(r, adam->x + 8, canvas.RowY(adam->gen) + 4, px);
    CHECK(px[0] == 0xef && px[1] == 0xef && px[2] == 0xef);
    // the family's line is drawn in its palette color: the middle of the first segment of each colored stroke
    for (const PictureStroke& s : BuildPictureStrokes(l)) {
        if (s.color < 0) continue;
        const PictureSegment& g = s.segments.back();   // a drop to a child, clear of the boxes' text
        const int mx = (g.x0 + g.x1) / 2, my = (g.y0 + g.y1) / 2;
        PixelAt(r, mx, my, px);
        const unsigned long long want = std::strtoull(PictureColor(s.color) + 1, nullptr, 16);
        CHECK(px[0] == static_cast<int>((want >> 16) & 0xFF)
              && px[1] == static_cast<int>((want >> 8) & 0xFF) &&
              px[2] == static_cast<int>(want & 0xFF));
    }
}

UNIT_TEST(ext_family_tree_picture_strokes_are_in_the_svg) {
    Town3 w;
    const PictureLayout l = BuildPictureLayout(w.t, false);
    const std::string svg = RenderPictureSvg(w.t, l, PictureOptions());
    size_t families = 0;
    for (const PictureStroke& s : BuildPictureStrokes(l)) {
        if (s.color >= 0) ++families;
        for (const PictureSegment& g : s.segments) {
            char buf[96];
            if (g.x0 == g.x1) std::snprintf(buf, sizeof buf, "M%d %d V%d ", g.x0, g.y0, g.y1);
            else std::snprintf(buf, sizeof buf, "M%d %d H%d ", g.x0, g.y0, g.x1);
            CHECK(svg.find(buf) != std::string::npos);
        }
    }
    CHECK_EQ(families, 2u);
}

UNIT_TEST(ext_family_tree_picture_raster_scales_a_big_town_down) {
    Tree t;
    const uint32_t a = t.Add("Adam", 1, false, 0, 20, 0, 0);
    const uint32_t e = t.Add("Eva", 0, false, 0, 19, 0, 0);
    t.Marry(a, e, 1);
    for (int i = 0; i < 1500; ++i) t.Add("Kid" + std::to_string(i), i % 2, true, 24 + i, 0, e, a);
    const PictureLayout l = BuildPictureLayout(t, false);
    CHECK(CanvasFor(l).width > kRasterMaxSide);
    BlockGlyphs fonts;
    PictureRaster r;
    CHECK(RenderPictureRaster(t, l, PictureOptions(), fonts, r));
    CHECK(r.scale < 1.0);
    CHECK(r.width <= kRasterMaxSide && r.height <= kRasterMaxSide);
    CHECK(static_cast<int64_t>(r.width) * r.height <= kRasterMaxPixels);
    CHECK_EQ(r.rgba.size(), static_cast<size_t>(r.width) * static_cast<size_t>(r.height) * 4u);
}

UNIT_TEST(ext_family_tree_picture_raster_needs_a_font_or_says_so) {
    Town3 w;
    NoGlyphs none;
    PictureRaster r;
    CHECK(!RenderPictureRaster(w.t, BuildPictureLayout(w.t, false), PictureOptions(), none, r));
    CHECK(!r.why.empty());
    CHECK(r.rgba.empty());
    BlockGlyphs fonts;
    // nobody to draw
    CHECK(!RenderPictureRaster(Tree(), BuildPictureLayout(Tree(), false), PictureOptions(), fonts, r));
}
