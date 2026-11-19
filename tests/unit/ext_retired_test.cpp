// ============================================================================
// tests/unit/ext_retired_test.cpp -- retired rows (registry.h RETIRED ROWS)
// ============================================================================
// docs/extensions/DESIGN.md section 3 "Retiring a row". A row that left the
// table keeps its id reserved in Ext::RetiredAt, so the saves and settings it
// left keep loading with no notice: its `ext:` pseudo-mod is status 0 / no
// notice, its block-1 manifest record passes SaveTail::Validate and is dropped
// before the lock, its settings key is never read, and no new save names it.
// Only up to the last formatVersion it was written at; anything else stays
// what an unknown id is. The first real entry is work_cand (WORKCAND.md 7.1);
// the test-only ones are unit_retired_b (an old class B row, v2) and
// unit_retired_fix (an old fix), both under BANISHED_UNIT_TEST.
// ============================================================================
#include "unit.h"
#include "ext/load_notice.h"
#include "ext/registry.h"
#include "ext/savetail.h"
#include "io/settings_file.h"

#include <cstring>
#include <string>
#include <vector>

namespace {

using Ext::SaveTail::Record;
using Ext::SaveTail::Status;

const Ext::Entry& E(const char* id) {
    const Ext::Entry* e = Ext::FindEntry(id);
    if (e == nullptr) {
        std::printf("  no entry %s\n", id);
        std::abort();
    }
    return *e;
}

// A requested state with every B / C row off but the given ones (some ship ON).
Ext::State With(std::initializer_list<const char*> on) {
    Ext::State s = Ext::Resolve(nullptr, nullptr, nullptr, nullptr);
    for (int i = 0; i < Ext::EntryCount(); ++i)
        if (Ext::IsLockedClass(Ext::EntryAt(i).cls)) Ext::EntryAt(i).slot(s) = false;
    for (const char* id : on) E(id).slot(s) = true;
    return s;
}

Record Rec(const char* id, int32_t v) {
    Record r;
    r.id = id;
    r.formatVersion = v;
    return r;
}

bool EntriesLookup(void* ctx, const char* key, std::string& value) {
    const IO::SettingsFile::Entries* entries = static_cast<const IO::SettingsFile::Entries*>(ctx);
    for (const IO::SettingsFile::Entry& e : *entries) {
        if (e.key == key) {
            value = e.value;
            return true;
        }
    }
    return false;
}

}  // namespace

// The table: valid, unique ids that are never live rows again; a fix never has a saved version.
UNIT_TEST(ext_retired_table_shape) {
    CHECK(Ext::RetiredCount() >= 1);
    for (int i = 0; i < Ext::RetiredCount(); ++i) {
        const Ext::RetiredEntry& r = Ext::RetiredAt(i);
        CHECK(Ext::ValidId(r.id));
        CHECK(Ext::FindRetired(r.id) == &r);          // unique among the retired
        CHECK(Ext::FindEntry(r.id) == nullptr);       // never registered again as a live row
        CHECK(Ext::FindValueKey(r.id) == nullptr);    // nor as a valued key (one settings namespace)
        CHECK(r.fate != nullptr && r.fate[0] != '\0');
        CHECK(r.lastFormatVersion >= 0);
        if (r.tier == Ext::Tier::Fix) CHECK_EQ(r.lastFormatVersion, 0);
    }
    const Ext::RetiredEntry* wc = Ext::FindRetired("work_cand");
    CHECK(wc != nullptr);
    if (wc != nullptr) {
        CHECK(wc->tier == Ext::Tier::Extension);
        CHECK_EQ(wc->lastFormatVersion, 1);
    }
    CHECK(Ext::FindRetired("no_such_id") == nullptr);
    CHECK(Ext::FindRetired(nullptr) == nullptr);
    CHECK(Ext::FindRetired("unit_b") == nullptr);     // a live row is not retired
}

// An old save's `ext:` pseudo-mod: no notice up to the last version written; past it, or for a fix,
// what an unknown id gets. The notice and the status agree everywhere.
UNIT_TEST(ext_retired_pseudo_mod_loads_without_notice) {
    Ext::Install(With({}), Ext::Profile::PerEntry, false);
    using N = Ext::PseudoModNotice;
    CHECK_EQ(Ext::PseudoModLoadStatus("ext:work_cand", 1), 0);
    CHECK(Ext::PseudoModNoticeFor("ext:work_cand", 1) == N::None);
    CHECK_EQ(Ext::PseudoModLoadStatus("ext:work_cand", 2), 1);              // no build ever wrote v2
    CHECK(Ext::PseudoModNoticeFor("ext:work_cand", 2) == N::NotInBuild);
    CHECK_EQ(Ext::PseudoModLoadStatus("ext:work_cand", 0), 1);              // not a version a save carries
    CHECK_EQ(Ext::PseudoModLoadStatus("ext:unit_retired_b", 1), 0);
    CHECK_EQ(Ext::PseudoModLoadStatus("ext:unit_retired_b", 2), 0);
    CHECK_EQ(Ext::PseudoModLoadStatus("ext:unit_retired_b", 3), 1);
    CHECK_EQ(Ext::PseudoModLoadStatus("ext:unit_retired_fix", 1), 1);       // a fix is never in a save
    CHECK_EQ(Ext::PseudoModLoadStatus("ext:no_such_ext", 1), 1);            // unknown: unchanged
    CHECK(Ext::PseudoModNoticeFor("ext:no_such_ext", 1) == N::NotInBuild);
    CHECK_EQ(Ext::PseudoModLoadStatus("Mods/work_cand.pkm", 1), 1);         // only an `ext:` path
    for (const char* p : {"ext:work_cand", "ext:unit_retired_b", "ext:unit_retired_fix"}) {
        for (int32_t v = -1; v <= 4; ++v) {
            const bool none = Ext::PseudoModNoticeFor(p, v) == N::None;
            CHECK_EQ(none, Ext::PseudoModLoadStatus(p, v) == 0);
        }
    }
}

// An old save's block-1 manifest: the retired record passes Validate, DropRetired takes it out, and the lock
// takes the save's other rows. Past the last version, or an id never registered, it stays fatal.
UNIT_TEST(ext_retired_manifest_validates_then_drops) {
    std::string offending;
    std::vector<Record> recs = {Rec("unit_b", 1), Rec("work_cand", 1), Rec("unit_c", 3)};
    CHECK(Ext::SaveTail::Validate(recs, &offending) == Status::Ok);
    std::vector<std::string> dropped;
    Ext::SaveTail::DropRetired(recs, &dropped);
    CHECK_EQ(dropped.size(), static_cast<size_t>(1));
    if (!dropped.empty()) CHECK_STREQ(dropped[0].c_str(), "work_cand");
    CHECK_EQ(recs.size(), static_cast<size_t>(2));
    if (recs.size() == 2) {
        CHECK_STREQ(recs[0].id.c_str(), "unit_b");   // the others keep their order
        CHECK_STREQ(recs[1].id.c_str(), "unit_c");
    }

    offending.clear();
    CHECK(Ext::SaveTail::Validate({Rec("work_cand", 2)}, &offending) == Status::UnknownId);
    CHECK_STREQ(offending.c_str(), "work_cand");
    CHECK(Ext::SaveTail::Validate({Rec("unit_retired_fix", 1)}, &offending) == Status::UnknownId);
    CHECK(Ext::SaveTail::Validate({Rec("no_such_ext", 1)}, &offending) == Status::UnknownId);
    CHECK(Ext::SaveTail::Validate({Rec("work_cand", 1), Rec("work_cand", 1)}, &offending) ==
          Status::DuplicateId);
    CHECK(Ext::SaveTail::Validate({Rec("unit_retired_b", 2)}, &offending) == Status::Ok);

    // A null `dropped` is fine; a live row's record is never dropped.
    std::vector<Record> live = {Rec("unit_b", 1)};
    Ext::SaveTail::DropRetired(live, nullptr);
    CHECK_EQ(live.size(), static_cast<size_t>(1));
}

// The whole load arm on a save whose only B / C record is the retired one (a default-profile save from
// before the retirement, every other default B / C row turned off): decode, validate, drop, lock. The
// game's B / C set is then empty, which is what the save was made with among the live rows.
UNIT_TEST(ext_retired_only_record_locks_an_empty_set) {
    Ext::Install(With({"unit_b"}), Ext::Profile::PerEntry, false);
    int game = 0;
    Ext::LockForGame(&game);
    CHECK(Ext::Active(E("unit_b")));
    const std::vector<uint8_t> bytes = Ext::SaveTail::Encode({Rec("work_cand", 1)});
    std::vector<Record> recs;
    CHECK(Ext::SaveTail::Decode(bytes.data(), bytes.size(), recs) == Status::Ok);
    std::string offending;
    CHECK(Ext::SaveTail::Validate(recs, &offending) == Status::Ok);
    Ext::SaveTail::DropRetired(recs, nullptr);
    CHECK(recs.empty());
    std::vector<std::string> ids;
    for (const Record& r : recs) ids.push_back(r.id);
    CHECK(Ext::LockForGameWithSet(&game, ids));
    CHECK(!Ext::Active(E("unit_b")));
    CHECK(!Ext::LockForGameWithSet(&game, {"work_cand"}));   // the lock itself only takes live rows
    CHECK(Ext::SaveTail::ApplyState(recs, &offending));
    Ext::UnlockForGame(&game);
    Ext::Install(With({}), Ext::Profile::PerEntry, false);
}

// New saves never name a retired id: everything on, and neither the pseudo-mods nor the manifest has one.
UNIT_TEST(ext_retired_never_written) {
    Ext::State all = Ext::Resolve(nullptr, nullptr, "all", nullptr);
    Ext::Install(all, Ext::Profile::All, false);
    std::vector<Ext::PseudoMod> mods(static_cast<size_t>(Ext::EntryCount()));
    const int n = Ext::PseudoModsForSave(mods.data(), Ext::EntryCount());
    CHECK(n >= 1);
    for (int i = 0; i < n && i < Ext::EntryCount(); ++i) {
        const std::string id = mods[static_cast<size_t>(i)].path.substr(std::strlen(Ext::kPseudoModPrefix));
        CHECK(Ext::FindRetired(id.c_str()) == nullptr);
    }
    for (const Record& r : Ext::SaveTail::ActiveRecords()) CHECK(Ext::FindRetired(r.id.c_str()) == nullptr);
    Ext::Install(With({}), Ext::Profile::PerEntry, false);
}

// An old settings.cfg line of a retired row is never asked for: even a malformed value is not counted.
UNIT_TEST(ext_retired_settings_key_never_read) {
    IO::SettingsFile::Entries entries =
        IO::SettingsFile::Parse(std::string("ext.work_cand=garbage\nfix.unit_retired_fix=x\next.unit_b=1\n"));
    Ext::LoadReport report;
    Ext::State s = Ext::Resolve(&EntriesLookup, &entries, nullptr, &report);
    CHECK_EQ(report.malformed, 0);
    CHECK_EQ(report.malformedValues, 0);
    CHECK(E("unit_b").slot(s));   // the live key beside it is read
}
