// ============================================================================
// tests/unit/ext_tool_names_test.cpp -- ext.tool_names (class C)
// ============================================================================
// src/ext/tool_names/tool_names.{h,cpp} without the game: the registry row (class C, OFF by default, held by
// the game's lock, the profile keys), the note table the two seams use (a note is shown only for the citizen and
// the use count it was made for), and the save-tail codec: a round trip, the claim of a record by the citizen it
// belongs to, and every refusal. The seams themselves are CitizenAI_PerformWorkCtor5_bd070
// (citizenai_actionctors.cpp) and CitizenUI::OnUpdateLayout (citizenui.cpp), and the game side is
// tool_names_town.cpp: they need the running game.
// ============================================================================
#include "unit.h"
#include "ext/registry.h"
#include "ext/savetail.h"
#include "ext/tool_names/tool_names.h"
#include "io/settings_file.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

using IO::SettingsFile::Entries;
using namespace Ext::ToolNames;

bool EntriesLookup(void* ctx, const char* key, std::string& value) {
    const Entries* entries = static_cast<const Entries*>(ctx);
    for (const IO::SettingsFile::Entry& e : *entries) {
        if (e.key == key) {
            value = e.value;
            return true;
        }
    }
    return false;
}

Ext::State ResolveText(const char* text) {
    Entries entries = IO::SettingsFile::Parse(std::string(text));
    return Ext::Resolve(&EntriesLookup, &entries, nullptr, nullptr);
}

Citizen Who(int32_t entity, const char* name, uint8_t gender = 0) {
    Citizen c;
    c.entity = entity;
    c.name = name;
    c.gender = gender;
    return c;
}

const Ext::SaveTail::StateCodec* ToolCodec() { return Ext::SaveTail::FindCodec("tool_names"); }

std::vector<uint8_t> Encoded() {
    std::vector<uint8_t> out;
    ToolCodec()->encode(out);
    return out;
}

}  // namespace

UNIT_TEST(ext_tool_names_registry_row) {
    const Ext::Entry* e = Ext::FindEntry("tool_names");
    CHECK(e != nullptr);
    if (e == nullptr) {
        return;
    }
    CHECK(e->tier == Ext::Tier::Extension);
    CHECK(e->cls == Ext::Class::C);              // which tool each citizen carries is the town's own state
    CHECK(!e->defaultOn);                        // class C ships OFF, like the other class C rows
    CHECK(e->formatVersion == 1);
    CHECK(Ext::SettingsKey(*e) == "ext.tool_names");
    CHECK(!ResolveText("").ext.tool_names);                      // absent key = the default (off)
    CHECK(!ResolveText("ext.tool_names=0\n").ext.tool_names);
    CHECK(ResolveText("ext.tool_names=1\n").ext.tool_names);
    CHECK(!ResolveText("profile=faithful\n").ext.tool_names);    // the faithful profile turns it off
    const Ext::SaveTail::StateCodec* codec = ToolCodec();
    CHECK(codec != nullptr);
    if (codec) {
        CHECK(codec->encode != nullptr && codec->decode != nullptr && codec->reset != nullptr);
        CHECK(std::strcmp(codec->id, "tool_names") == 0);
    }
}

UNIT_TEST(ext_tool_names_row_is_held_by_the_game_lock) {
    const Ext::Entry* e = Ext::FindEntry("tool_names");
    if (e == nullptr) {
        CHECK(false);
        return;
    }
    int game = 0;
    Ext::Install(ResolveText("ext.tool_names=1\n"), Ext::Profile::PerEntry, false);
    Ext::LockForGame(&game);
    CHECK(Ext::Active(*e));
    CHECK(Ext::EntryLocked(*e));                 // a class C row is fixed for the game once it starts
    Ext::UnlockForGame(&game);
    Ext::Install(ResolveText(""), Ext::Profile::PerEntry, false);
}

UNIT_TEST(ext_tool_names_notes) {
    Clear();
    int citizenA = 0, citizenB = 0, stone = 0, iron = 0;
    CHECK(Lookup(&citizenA, 100) == nullptr);            // nothing noted
    Remember(&citizenA, &stone, 100, Who(1, "Anna"), "Item/Stone.rsc");
    Remember(&citizenB, &iron, 100, Who(2, "Bram"), "Item/Iron.rsc");   // another citizen, the same use count
    CHECK(Lookup(&citizenA, 100) == &stone);
    CHECK(Lookup(&citizenB, 100) == &iron);
    CHECK(Lookup(&citizenA, 250) == nullptr);            // the citizen's use count is another tool's: not trusted
    Remember(&citizenA, &iron, 250, Who(1, "Anna"), "Item/Iron.rsc");   // the next tool replaces the note
    CHECK(Lookup(&citizenA, 250) == &iron);
    CHECK(Lookup(&citizenA, 100) == nullptr);
    Remember(nullptr, &iron, 100, Who(3, "Cy"), "Item/Iron.rsc");       // junk is ignored
    Remember(&citizenA, nullptr, 100, Who(1, "Anna"), "Item/Iron.rsc");
    CHECK(Lookup(&citizenA, 250) == &iron);
    CHECK_EQ(LiveCount(), 2u);
    Forget(&citizenA);
    CHECK(Lookup(&citizenA, 250) == nullptr);
    CHECK(Lookup(&citizenB, 100) == &iron);
    Clear();
    CHECK(Lookup(&citizenB, 100) == nullptr);
}

UNIT_TEST(ext_tool_names_prune_drops_citizens_who_are_gone) {
    Clear();
    int a = 0, b = 0, item = 0;
    Remember(&a, &item, 50, Who(1, "Anna"), "Item/Wood.rsc");
    Remember(&b, &item, 50, Who(2, "Bram"), "Item/Wood.rsc");
    Prune(std::unordered_set<const void*>{&a});
    CHECK(Lookup(&a, 50) == &item);
    CHECK(Lookup(&b, 50) == nullptr);
    CHECK_EQ(LiveCount(), 1u);
    Clear();
}

UNIT_TEST(ext_tool_names_table_is_capped) {
    Clear();
    static int citizens[kMaxNotes + 8];
    int item = 0;
    for (std::size_t i = 0; i < kMaxNotes + 8; ++i) {
        Remember(&citizens[i], &item, 100, Who(static_cast<int32_t>(i), "N"), "Item/Wood.rsc");
    }
    CHECK(LiveCount() <= kMaxNotes);                     // the table never grows without bound
    CHECK(Lookup(&citizens[kMaxNotes + 7], 100) == &item);   // the newest note survives the reset
    Clear();
}

UNIT_TEST(ext_tool_names_codec_round_trip_and_claim) {
    Clear();
    int a = 0, b = 0, item = 0;
    Remember(&a, &item, 50, Who(7, "Anna", 0), "Mod/Tools/Wood.rsc");
    Remember(&b, &item, 100, Who(9, "Bram", 1), "Mod/Tools/Iron.rsc");
    const std::vector<uint8_t> bytes = Encoded();
    CHECK(!bytes.empty());
    // a new game: everything is empty, then the save is loaded
    Clear();
    CHECK(ToolCodec()->decode(bytes.data(), bytes.size(), 1));
    CHECK_EQ(LiveCount(), 0u);                           // the citizens of the old game are gone
    CHECK_EQ(SavedCount(), 2u);
    int newA = 0;                                        // the loaded game's citizen: another address
    std::string path;
    CHECK(!Claim(Who(7, "Anna", 1), 50, path));          // the same entity but another gender: not her
    CHECK(!Claim(Who(7, "Anne", 0), 50, path));          // another name
    CHECK(!Claim(Who(7, "Anna", 0), 51, path));          // another tool now
    CHECK(Claim(Who(7, "Anna", 0), 50, path));
    CHECK(path == "Mod/Tools/Wood.rsc");
    CHECK(!Claim(Who(7, "Anna", 0), 50, path));          // claimed once
    CHECK_EQ(SavedCount(), 1u);
    Remember(&newA, &item, 50, Who(7, "Anna", 0), path); // the game side then notes it live
    CHECK(Lookup(&newA, 50) == &item);
    // a second save before Bram's panel was ever opened keeps his waiting record and Anna's live note
    const std::vector<uint8_t> again = Encoded();
    Clear();
    CHECK(ToolCodec()->decode(again.data(), again.size(), 1));
    CHECK_EQ(SavedCount(), 2u);
    Clear();
}

UNIT_TEST(ext_tool_names_a_new_pickup_replaces_the_saved_record) {
    Clear();
    int a = 0, item = 0;
    Remember(&a, &item, 50, Who(7, "Anna"), "Mod/Tools/Wood.rsc");
    const std::vector<uint8_t> bytes = Encoded();
    Clear();
    CHECK(ToolCodec()->decode(bytes.data(), bytes.size(), 1));
    int b = 0;
    Remember(&b, &item, 100, Who(7, "Anna"), "Mod/Tools/Iron.rsc");   // the citizen took a new tool before the panel
    CHECK_EQ(SavedCount(), 0u);
    std::string path;
    CHECK(!Claim(Who(7, "Anna"), 50, path));
    Clear();
}

UNIT_TEST(ext_tool_names_codec_refusals) {
    Clear();
    int a = 0, item = 0;
    Remember(&a, &item, 50, Who(7, "Anna", 0), "Mod/Tools/Wood.rsc");
    const std::vector<uint8_t> good = Encoded();
    Clear();
    const Ext::SaveTail::StateCodec* codec = ToolCodec();
    CHECK(codec->decode(good.data(), good.size(), 1));
    CHECK_EQ(SavedCount(), 1u);

    // every refusal leaves what was there (one waiting record) alone
    std::vector<uint8_t> bad = good;
    bad.push_back(0);                                    // trailing bytes
    CHECK(!codec->decode(bad.data(), bad.size(), 1));
    for (size_t cut = 0; cut < good.size(); ++cut) {     // cut short at every length
        CHECK(!codec->decode(good.data(), cut, 1));
    }
    CHECK(!codec->decode(good.data(), good.size(), 2));  // a version this build does not know
    CHECK(!codec->decode(good.data(), good.size(), 0));
    bad = good;
    bad[0] = 5;                                          // a count the payload cannot hold
    CHECK(!codec->decode(bad.data(), bad.size(), 1));
    bad = good;
    bad[12] = 2;                                         // gender 2 (count 4 + entity 4 + uses 4, then gender)
    CHECK(!codec->decode(bad.data(), bad.size(), 1));
    bad = good;
    bad[8] = 0;                                          // uses 0 (the low byte of 50)
    CHECK(!codec->decode(bad.data(), bad.size(), 1));
    CHECK_EQ(SavedCount(), 1u);

    // a path with a control character, an empty path, an entity twice
    std::vector<uint8_t> twice;
    {
        Ext::SaveTail::Writer w(twice);
        w.U32(2);
        for (int i = 0; i < 2; ++i) {
            w.I32(7);
            w.I32(50);
            const uint8_t g = 0;
            w.Bytes(&g, 1);
            w.String("Anna");
            w.String("Mod/Tools/Wood.rsc");
        }
    }
    CHECK(!codec->decode(twice.data(), twice.size(), 1));
    std::vector<uint8_t> control;
    {
        Ext::SaveTail::Writer w(control);
        w.U32(1);
        w.I32(7);
        w.I32(50);
        const uint8_t g = 0;
        w.Bytes(&g, 1);
        w.String("Anna");
        w.String(std::string("Mod/\x01Tool.rsc"));
    }
    CHECK(!codec->decode(control.data(), control.size(), 1));
    std::vector<uint8_t> empty;
    {
        Ext::SaveTail::Writer w(empty);
        w.U32(1);
        w.I32(7);
        w.I32(50);
        const uint8_t g = 0;
        w.Bytes(&g, 1);
        w.String("Anna");
        w.String("");
    }
    CHECK(!codec->decode(empty.data(), empty.size(), 1));
    CHECK_EQ(SavedCount(), 1u);

    // an empty town is a valid payload, and Reset empties everything
    std::vector<uint8_t> none;
    Ext::SaveTail::Writer(none).U32(0);
    CHECK(codec->decode(none.data(), none.size(), 1));
    CHECK_EQ(SavedCount(), 0u);
    Remember(&a, &item, 50, Who(7, "Anna"), "Mod/Tools/Wood.rsc");
    codec->reset();
    CHECK_EQ(LiveCount(), 0u);
    CHECK_EQ(SavedCount(), 0u);
}
