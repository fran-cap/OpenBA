// ============================================================================
// tests/unit/ext_savetail_test.cpp -- the class B / C save machinery
// ============================================================================
// src/ext/savetail.{h,cpp} with src/ext/registry.cpp (block EXTSAVE,
// docs/extensions/handins/EXTSAVE.md): the block-1 tail manifest's encode /
// decode round trip, ZERO bytes when no B / C extension is active, a cut or
// damaged tail detected, an unknown id / a newer format version rejected,
// the lock taken from a manifest overriding the requested state, and the
// load notice's verdict on an `ext:` pseudo-mod. The `unit_*` extensions
// exist only under BANISHED_UNIT_TEST (unit_b class B v1, unit_c class C v3).
// ============================================================================
#include "unit.h"
#include "ext/registry.h"
#include "ext/savetail.h"
#include "ext/toolbar/window_state.h"   // the first host record (EXTHOSTS)

#include <cstring>
#include <string>
#include <vector>

namespace {

using Ext::SaveTail::Record;
using Ext::SaveTail::Status;

const Ext::Entry& E(const char* id) {
    const Ext::Entry* e = Ext::FindEntry(id);
    if (e == nullptr) { std::printf("  no entry %s\n", id); std::abort(); }
    return *e;
}

// A requested state with the given extensions on (everything else default).
Ext::State With(std::initializer_list<const char*> on) {
    Ext::State s = Ext::Resolve(nullptr, nullptr, nullptr, nullptr);
    // Every B / C row off first: some ship ON (DESIGN.md section 2, 1.0.3), and these tests count records.
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

}  // namespace

UNIT_TEST(ext_savetail_crc32_reference) {
    const uint8_t text[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    CHECK_EQ(Ext::SaveTail::Crc32(text, sizeof text), 0xCBF43926u);
}

UNIT_TEST(ext_savetail_round_trip) {
    std::vector<Record> in = {Rec("unit_b", 1), Rec("unit_c", 3)};
    in[1].payload = {1, 2, 3, 0xff};
    const std::vector<uint8_t> bytes = Ext::SaveTail::Encode(in);
    // magic, version, count; the trailer at the end.
    CHECK(bytes.size() > 12 + Ext::SaveTail::kTrailerBytes);
    CHECK_EQ(bytes[0], 'E');
    CHECK_EQ(bytes[1], 'X');
    CHECK_EQ(bytes[2], 'T');
    CHECK_EQ(bytes[3], 'S');

    std::vector<Record> out;
    CHECK(Ext::SaveTail::Decode(bytes.data(), bytes.size(), out) == Status::Ok);
    CHECK_EQ(static_cast<int>(out.size()), 2);
    CHECK_STREQ(out[0].id.c_str(), "unit_b");
    CHECK_EQ(out[0].formatVersion, 1);
    CHECK(out[0].payload.empty());
    CHECK_STREQ(out[1].id.c_str(), "unit_c");
    CHECK_EQ(out[1].formatVersion, 3);
    CHECK(out[1].payload == in[1].payload);
    CHECK(Ext::SaveTail::Validate(out, nullptr) == Status::Ok);
}

UNIT_TEST(ext_savetail_zero_bytes_when_empty) {
    // Encode of nothing is nothing: no header.
    CHECK(Ext::SaveTail::Encode({}).empty());

    // No B / C active (class A on, fixes on): the save tail is zero bytes.
    Ext::Install(With({"unit_a"}), Ext::Profile::PerEntry, false);
    CHECK(Ext::SaveTail::ActiveRecords().empty());
    CHECK(Ext::SaveTail::EncodeActive().empty());

    // The faithful profile, whatever the per-entry keys say: zero bytes, and
    // no pseudo-mod provider at all.
    Ext::State all = With({"unit_a", "unit_b", "unit_c"});
    Ext::State faithful = Ext::Resolve(nullptr, nullptr, "faithful", nullptr);
    Ext::Install(faithful, Ext::Profile::Faithful, true);
    CHECK(Ext::SaveTail::EncodeActive().empty());
    CHECK(Ext::SaveModsProvider() == nullptr);

    // B and C on: both recorded, in table order; class A never.
    Ext::Install(all, Ext::Profile::PerEntry, false);
    const std::vector<Record> active = Ext::SaveTail::ActiveRecords();
    CHECK_EQ(static_cast<int>(active.size()), 2);
    CHECK_STREQ(active[0].id.c_str(), "unit_b");
    CHECK_EQ(active[0].formatVersion, 1);
    CHECK_STREQ(active[1].id.c_str(), "unit_c");
    CHECK_EQ(active[1].formatVersion, 3);
    std::vector<Record> back;
    const std::vector<uint8_t> bytes = Ext::SaveTail::EncodeActive();
    CHECK(Ext::SaveTail::Decode(bytes.data(), bytes.size(), back) == Status::Ok);
    CHECK_EQ(static_cast<int>(back.size()), 3);   // the two extension records, then the toolbar host's
    CHECK_EQ(back[2].kind, Ext::SaveTail::kKindHost);
    CHECK_STREQ(back[2].id.c_str(), "toolbar");

    Ext::Install(With({}), Ext::Profile::PerEntry, false);
}

UNIT_TEST(ext_savetail_truncated_and_garbage) {
    const std::vector<uint8_t> good = Ext::SaveTail::Encode({Rec("unit_b", 1), Rec("unit_c", 3)});
    std::vector<Record> out;

    // Every cut of a real manifest is detected, never read as a shorter one.
    for (size_t n = 1; n < good.size(); ++n) {
        const Status st = Ext::SaveTail::Decode(good.data(), n, out);
        CHECK(st == Status::Truncated || st == Status::BadChecksum);
        CHECK(out.empty());
    }

    // A flipped byte in the body: the CRC.
    std::vector<uint8_t> bad = good;
    bad[14] ^= 0x20;
    CHECK(Ext::SaveTail::Decode(bad.data(), bad.size(), out) == Status::BadChecksum);
    CHECK(out.empty());

    // Extra bytes after the trailer: the trailer no longer matches.
    bad = good;
    bad.push_back(0);
    CHECK(Ext::SaveTail::Decode(bad.data(), bad.size(), out) == Status::Truncated);

    // Not ours (no magic): ignored, not an error.
    const uint8_t garbage[] = {0xAA, 0xAA, 0xAA, 0xAA, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    CHECK(Ext::SaveTail::Decode(garbage, sizeof garbage, out) == Status::NoManifest);
    CHECK(Ext::SaveTail::Decode(nullptr, 0, out) == Status::NoManifest);
    CHECK(Ext::SaveTail::Decode(garbage, 0, out) == Status::NoManifest);

    // A newer chunk version, CRC fixed up: refused.
    bad = good;
    bad[4] = 3;
    const uint32_t crc = Ext::SaveTail::Crc32(bad.data(), bad.size() - 8);
    for (int i = 0; i < 4; ++i) bad[bad.size() - 4 + i] = static_cast<uint8_t>(crc >> (8 * i));
    CHECK(Ext::SaveTail::Decode(bad.data(), bad.size(), out) == Status::NewerChunkVersion);

    // A bad id inside a well-formed chunk (CRC right): malformed.
    std::vector<uint8_t> dotted = Ext::SaveTail::Encode({Rec("unit_b", 1)});
    dotted[18] = '.';   // 12-byte header, kind and flags, 4-byte id length, then the id
    const uint32_t crc2 = Ext::SaveTail::Crc32(dotted.data(), dotted.size() - 8);
    for (int i = 0; i < 4; ++i) dotted[dotted.size() - 4 + i] = static_cast<uint8_t>(crc2 >> (8 * i));
    CHECK(Ext::SaveTail::Decode(dotted.data(), dotted.size(), out) == Status::Malformed);
}

UNIT_TEST(ext_savetail_validate_rejects) {
    std::string who;
    CHECK(Ext::SaveTail::Validate({Rec("no_such_ext", 1)}, &who) == Status::UnknownId);
    CHECK_STREQ(who.c_str(), "no_such_ext");
    CHECK(Ext::SaveTail::Validate({Rec("unit_b", 2)}, &who) == Status::NewerFormatVersion);
    CHECK_STREQ(who.c_str(), "unit_b");
    CHECK(Ext::SaveTail::Validate({Rec("unit_c", 3)}, nullptr) == Status::Ok);   // equal: fine
    CHECK(Ext::SaveTail::Validate({Rec("unit_c", 2)}, nullptr) == Status::Ok);   // older: fine
    CHECK(Ext::SaveTail::Validate({Rec("unit_a", 1)}, nullptr) == Status::NotLockedClass);
    CHECK(Ext::SaveTail::Validate({Rec("icon_slide", 1)}, nullptr) == Status::NotLockedClass);
    CHECK(Ext::SaveTail::Validate({Rec("unit_b", 1), Rec("unit_b", 1)}, nullptr) ==
          Status::DuplicateId);
    CHECK(Ext::SaveTail::Validate({}, nullptr) == Status::Ok);
}

UNIT_TEST(ext_savetail_lock_from_manifest) {
    // The player requests unit_b only.
    Ext::Install(With({"unit_b"}), Ext::Profile::PerEntry, false);
    int game = 0;
    Ext::LockForGame(&game);                    // GameUpdateLoop::Load's entry lock
    CHECK(Ext::Ext().unit_b && !Ext::Ext().unit_c);

    // The save says unit_c only: the game runs exactly the save's set.
    CHECK(Ext::LockForGameWithSet(&game, {"unit_c"}));
    CHECK(!Ext::Ext().unit_b);
    CHECK(Ext::Ext().unit_c);
    CHECK(Ext::Locked());
    CHECK(Ext::EntryLocked(E("unit_b")));
    CHECK(Ext::EntryLocked(E("unit_c")));
    CHECK(!Ext::EntryLocked(E("unit_a")));       // class A stays live
    // The request is untouched, and a toggle in game changes only the request.
    CHECK(Ext::Requested(E("unit_b")));
    CHECK(!Ext::Requested(E("unit_c")));
    CHECK(!Ext::SetRequested(E("unit_c"), false));
    CHECK(Ext::Ext().unit_c);
    // The manifest is what the next save writes.
    const std::vector<Record> active = Ext::SaveTail::ActiveRecords();
    CHECK_EQ(static_cast<int>(active.size()), 1);
    CHECK_STREQ(active[0].id.c_str(), "unit_c");

    // An id that is not a B / C extension: refused, nothing changed.
    CHECK(!Ext::LockForGameWithSet(&game, {"unit_b", "no_such_ext"}));
    CHECK(!Ext::LockForGameWithSet(&game, {"unit_a"}));
    CHECK(!Ext::Ext().unit_b && Ext::Ext().unit_c);

    // Unlock: back at the menu the active state is the request again.
    Ext::UnlockForGame(&game);
    CHECK(!Ext::Locked());
    CHECK(Ext::Ext().unit_b);
    CHECK(!Ext::Ext().unit_c);

    // An empty manifest set locks every B / C off.
    Ext::LockForGameWithSet(&game, {});
    CHECK(!Ext::Ext().unit_b && !Ext::Ext().unit_c);
    Ext::UnlockForGame(&game);
    Ext::Install(With({}), Ext::Profile::PerEntry, false);
}

UNIT_TEST(ext_savetail_load_notice_status) {
    Ext::Install(With({"unit_b"}), Ext::Profile::PerEntry, false);
    CHECK(Ext::IsPseudoModPath("ext:unit_b"));
    CHECK(!Ext::IsPseudoModPath("Mods/foo.pkm"));
    CHECK(!Ext::IsPseudoModPath("ex"));
    CHECK(!Ext::IsPseudoModPath(nullptr));
    CHECK_EQ(Ext::PseudoModLoadStatus("ext:unit_b", 1), 0);   // present, requested
    CHECK_EQ(Ext::PseudoModLoadStatus("ext:unit_b", 2), 1);   // saved by a newer format
    CHECK_EQ(Ext::PseudoModLoadStatus("ext:unit_c", 1), 1);   // present, not requested
    CHECK_EQ(Ext::PseudoModLoadStatus("ext:nope", 1), 1);     // missing
    CHECK_EQ(Ext::PseudoModLoadStatus("ext:unit_a", 1), 1);   // class A is never in a save
    CHECK_EQ(Ext::PseudoModLoadStatus("Mods/x.pkm", 1), 1);
    Ext::Install(With({}), Ext::Profile::PerEntry, false);
}

// ---- host records (block EXTHOSTS, docs/extensions/handins/EXTHOSTS.md) ----------------------------------

namespace {

void FixCrc(std::vector<uint8_t>& b) {
    const uint32_t crc = Ext::SaveTail::Crc32(b.data(), b.size() - 8);
    for (int i = 0; i < 4; ++i) {
        b[b.size() - 4 + i] = static_cast<uint8_t>(crc >> (8 * i));
    }
}

void Put32(std::vector<uint8_t>& b, uint32_t v) {
    for (int i = 0; i < 4; ++i) {
        b.push_back(static_cast<uint8_t>(v >> (8 * i)));
    }
}

Record Host(const char* id, int32_t v, std::vector<uint8_t> payload) {
    Record r = Rec(id, v);
    r.kind = Ext::SaveTail::kKindHost;
    r.flags = 0;
    r.payload = std::move(payload);
    return r;
}

}  // namespace

UNIT_TEST(ext_savetail_reads_a_version_1_chunk) {
    // A tail written before the record kinds: every record an extension's, must-understand.
    std::vector<uint8_t> v1;
    Put32(v1, Ext::SaveTail::kMagic);
    Put32(v1, 1);
    Put32(v1, 1);
    Put32(v1, 6);
    for (char c : std::string("unit_c")) {
        v1.push_back(static_cast<uint8_t>(c));
    }
    Put32(v1, 3);
    Put32(v1, 2);
    v1.push_back(7);
    v1.push_back(9);
    const uint32_t body = static_cast<uint32_t>(v1.size());
    Put32(v1, body);
    Put32(v1, 0);
    FixCrc(v1);
    std::vector<Record> out;
    CHECK(Ext::SaveTail::Decode(v1.data(), v1.size(), out) == Status::Ok);
    CHECK_EQ(static_cast<int>(out.size()), 1);
    CHECK_EQ(out[0].kind, Ext::SaveTail::kKindExtension);
    CHECK_EQ(out[0].flags, Ext::SaveTail::kFlagMustUnderstand);
    CHECK_STREQ(out[0].id.c_str(), "unit_c");
    CHECK_EQ(out[0].formatVersion, 3);
    CHECK_EQ(static_cast<int>(out[0].payload.size()), 2);
}

UNIT_TEST(ext_savetail_split_kinds) {
    std::vector<Record> all = {Rec("unit_b", 1), Host("toolbar", 1, {}), Rec("unit_c", 3)};
    Record later = Rec("later_thing", 1);
    later.kind = 9;
    later.flags = 0;
    all.push_back(later);

    std::vector<Record> ext;
    std::vector<Record> hosts;
    std::vector<std::string> dropped;
    std::string offending;
    CHECK(Ext::SaveTail::SplitKinds(all, ext, hosts, &dropped, &offending) == Status::Ok);
    CHECK_EQ(static_cast<int>(ext.size()), 2);
    CHECK_EQ(static_cast<int>(hosts.size()), 1);
    CHECK_EQ(static_cast<int>(dropped.size()), 1);
    CHECK_STREQ(dropped[0].c_str(), "later_thing");
    // Only extension records reach Validate: a host id is never an "unknown extension".
    CHECK(Ext::SaveTail::Validate(ext, nullptr) == Status::Ok);

    // A later kind marked must-understand: this build cannot honour the save.
    all.back().flags = Ext::SaveTail::kFlagMustUnderstand;
    CHECK(Ext::SaveTail::SplitKinds(all, ext, hosts, &dropped, &offending) == Status::UnknownKind);
    CHECK_STREQ(offending.c_str(), "later_thing");
    CHECK(ext.empty());
    CHECK(hosts.empty());

    // The kinds survive the wire.
    all.pop_back();
    const std::vector<uint8_t> bytes = Ext::SaveTail::Encode(all);
    std::vector<Record> back;
    CHECK(Ext::SaveTail::Decode(bytes.data(), bytes.size(), back) == Status::Ok);
    CHECK_EQ(static_cast<int>(back.size()), 3);
    CHECK_EQ(back[1].kind, Ext::SaveTail::kKindHost);
    CHECK_EQ(back[1].flags, 0);
}

UNIT_TEST(ext_savetail_toolbar_window_round_trip) {
    Ext::Install(With({"unit_b"}), Ext::Profile::PerEntry, false);
    Ext::SaveTail::ResetAllState();
    Ext::Toolbar::WindowState& w = Ext::Toolbar::SavedWindow();
    w.open = true;
    w.x = 640;
    w.y = 77;
    w.page = "restricted_foods";
    const std::vector<uint8_t> bytes = Ext::SaveTail::EncodeActive();

    // A new town forgets it...
    Ext::SaveTail::ResetAllState();
    CHECK(!w.open);
    CHECK_EQ(w.x, 24);
    CHECK(w.page.empty());

    // ...and the save brings it back.
    std::vector<Record> all;
    std::vector<Record> ext;
    std::vector<Record> hosts;
    CHECK(Ext::SaveTail::Decode(bytes.data(), bytes.size(), all) == Status::Ok);
    CHECK(Ext::SaveTail::SplitKinds(all, ext, hosts, nullptr, nullptr) == Status::Ok);
    std::vector<std::string> notes;
    Ext::SaveTail::ApplyHostState(hosts, &notes);
    CHECK(notes.empty());
    CHECK(w.open);
    CHECK_EQ(w.x, 640);
    CHECK_EQ(w.y, 77);
    CHECK_STREQ(w.page.c_str(), "restricted_foods");

    // Stripping the host records changes nothing on the lock side.
    CHECK_EQ(static_cast<int>(ext.size()), 1);
    CHECK_STREQ(ext[0].id.c_str(), "unit_b");
    CHECK(Ext::SaveTail::Validate(ext, nullptr) == Status::Ok);

    Ext::SaveTail::ResetAllState();
    Ext::Install(With({}), Ext::Profile::PerEntry, false);
}

UNIT_TEST(ext_savetail_no_host_record_without_an_extension_record) {
    // Only class A on (a class A page may still open the window): the tail stays zero bytes.
    Ext::Install(With({"unit_a"}), Ext::Profile::PerEntry, false);
    Ext::Toolbar::SavedWindow().open = true;
    CHECK(Ext::SaveTail::EncodeActive().empty());
    Ext::SaveTail::ResetAllState();
    Ext::Install(With({}), Ext::Profile::PerEntry, false);
}

UNIT_TEST(ext_savetail_host_records_are_never_fatal) {
    Ext::SaveTail::ResetAllState();
    Ext::Toolbar::SavedWindow().x = 500;

    // Unknown host, newer host version, a payload the codec refuses: each noted, none fatal, and the
    // refused host is back at its defaults.
    std::vector<uint8_t> refused = {2};   // open byte 2, and nothing after it
    std::vector<Record> hosts = {Host("not_a_host", 1, {}), Host("toolbar", 99, {}), Host("toolbar", 1, refused)};
    std::vector<std::string> notes;
    Ext::SaveTail::ApplyHostState(hosts, &notes);
    CHECK_EQ(static_cast<int>(notes.size()), 3);
    CHECK_EQ(Ext::Toolbar::SavedWindow().x, 24);
}

UNIT_TEST(ext_savetail_toolbar_window_codec_refuses) {
    const Ext::SaveTail::HostCodec& c = Ext::Toolbar::WindowCodec();
    std::vector<uint8_t> ok;
    Ext::Toolbar::SavedWindow().page = "restricted_foods";
    c.encode(ok);
    CHECK(c.decode(ok.data(), ok.size(), 1));
    CHECK(!c.decode(ok.data(), ok.size(), 2));            // an unknown version
    std::vector<uint8_t> extra = ok;
    extra.push_back(0);
    CHECK(!c.decode(extra.data(), extra.size(), 1));      // bytes left over
    CHECK(!c.decode(ok.data(), ok.size() - 1, 1));        // cut short
    std::vector<uint8_t> dotted = ok;
    dotted.back() = '.';                                  // not a page id
    CHECK(!c.decode(dotted.data(), dotted.size(), 1));
    Ext::SaveTail::ResetAllState();
}
