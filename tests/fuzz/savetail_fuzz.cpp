// ============================================================================
// tests/fuzz/savetail_fuzz.cpp -- the save tail manifest and its class C / host codecs over arbitrary bytes
// ============================================================================
// P0_MAP input surface rank 3 (SaveTail). A save is shared between players, so every byte after the last
// faithful Serialize of block 1 is attacker-chosen. One target, a selector byte picks the decoder:
//
//   input[0] % 7
//     0  Ext::SaveTail::Decode (src/ext/savetail.cpp) on input[1..], then the loader's pipeline on an Ok
//        result (SplitKinds -> Validate -> ApplyState / ApplyHostState, as gameupdateloop_serialize.cpp:830-877
//        runs it), and the round-trip properties:
//          * a version-2 chunk that decodes Ok re-encodes to exactly the input bytes;
//          * Decode(Encode(records)) is Ok and gives the same records.
//     1  restricted_foods  StateCodec::decode   (src/ext/restricted_foods/save_state.cpp)
//     2  item_priority     StateCodec::decode   (src/ext/item_priority/save_state.cpp)
//     3  tool_names        StateCodec::decode   (src/ext/tool_names/tool_names.cpp)
//     4  family_tree       StateCodec::decode   (src/ext/family_tree/family_tree.cpp)
//     5  toolbar           HostCodec::decode    (src/ext/toolbar/window_state.cpp)
//     6  as 0, but input[1..] is the manifest BODY only: the target appends the trailer (bodyLength + CRC-32)
//        itself, so a mutated record list is not stopped at BadChecksum and the record parser and the
//        codecs behind ApplyState are reached (a coverage aid; selector 0 still fuzzes the trailer checks).
//   for 1..5: input[1] is the saved format version (as int8: 0 and negatives too), input[2..] the payload.
//   A payload a codec accepts must be stable under re-encoding: e1 = encode(); decode(e1) is true;
//   encode() again == e1. A violation is __builtin_trap() (a logic finding, not a sanitizer one).
//
// Every payload is copied into a heap buffer of exactly its size so ASan sees a read past its end.
//
// FUZZ_SEEDGEN: built with -DFUZZ_SEEDGEN, LLVMFuzzerInitialize writes seeds made by OUR encoders (the
// codecs' own `encode` after filling state through the extensions' public API, and SaveTail::Encode) to the
// directory named by argv[1], then exits. No retail content is used (AGENTS.md section 1).
// ============================================================================
#include "ext/savetail.h"

#include "ext/family_tree/family_tree.h"
#include "ext/item_priority/item_priority.h"
#include "ext/restricted_foods/restricted_foods.h"
#include "ext/tool_names/tool_names.h"
#include "ext/toolbar/window_state.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

using Ext::SaveTail::HostCodec;
using Ext::SaveTail::Record;
using Ext::SaveTail::StateCodec;
using Ext::SaveTail::Status;

const char* const kCodecIds[] = { "restricted_foods", "item_priority", "tool_names", "family_tree" };

bool SameRecords(const std::vector<Record>& a, const std::vector<Record>& b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].kind != b[i].kind || a[i].flags != b[i].flags || a[i].id != b[i].id ||
            a[i].formatVersion != b[i].formatVersion || a[i].payload != b[i].payload) {
            return false;
        }
    }
    return true;
}

void FuzzManifest(const uint8_t* data, size_t size) {
    std::vector<Record> records;
    const Status st = Ext::SaveTail::Decode(data, size, records);
    if (st != Status::Ok) {
        return;
    }
    // Round trip 1: version 2 (the only version Encode writes) re-encodes byte for byte.
    const std::vector<uint8_t> again = Ext::SaveTail::Encode(records);
    uint32_t version = 0;
    std::memcpy(&version, data + 4, 4);
    if (version == Ext::SaveTail::kVersion && !records.empty()) {
        if (again.size() != size || std::memcmp(again.data(), data, size) != 0) {
            std::fprintf(stderr, "savetail: Encode(Decode(x)) != x\n");
            __builtin_trap();
        }
    }
    // Round trip 2: Decode(Encode(records)) == records.
    if (!records.empty()) {
        std::vector<uint8_t> copy(again);
        std::vector<Record> back;
        if (Ext::SaveTail::Decode(copy.data(), copy.size(), back) != Status::Ok || !SameRecords(back, records)) {
            std::fprintf(stderr, "savetail: Decode(Encode(r)) != r\n");
            __builtin_trap();
        }
    }
    // The loader's pipeline (gameupdateloop_serialize.cpp:830-877).
    std::vector<Record> exts;
    std::vector<Record> hosts;
    std::vector<std::string> dropped;
    std::string offending;
    if (Ext::SaveTail::SplitKinds(records, exts, hosts, &dropped, &offending) != Status::Ok) {
        return;
    }
    (void)Ext::SaveTail::StatusName(Ext::SaveTail::Validate(exts, &offending));
    if (Ext::SaveTail::Validate(exts, &offending) == Status::Ok) {
        (void)Ext::SaveTail::ApplyState(exts, &offending);
    }
    std::vector<std::string> notes;
    Ext::SaveTail::ApplyHostState(hosts, &notes);
    Ext::SaveTail::ResetAllState();
}

template <typename Codec>
void FuzzCodec(const Codec& c, int32_t version, const uint8_t* payload, size_t size) {
    c.reset();
    if (!c.decode(payload, size, version)) {
        c.reset();
        return;
    }
    std::vector<uint8_t> e1;
    c.encode(e1);
    std::vector<uint8_t> copy(e1);
    c.reset();
    // re-encoding always writes the newest format: decode it as such
    if (!c.decode(copy.data(), copy.size(), version > 1 ? version : 1) &&
        !c.decode(copy.data(), copy.size(), 2)) {
        std::fprintf(stderr, "savetail codec %s: decode(encode(decode(x))) refused\n", c.id);
        __builtin_trap();
    }
    std::vector<uint8_t> e2;
    c.encode(e2);
    if (e1 != e2) {
        std::fprintf(stderr, "savetail codec %s: encode not stable after a round trip\n", c.id);
        __builtin_trap();
    }
    c.reset();
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size < 1 || size > 0x40000) {
        return 0;
    }
    const int sel = data[0] % 7;
    if (sel == 0) {
        std::vector<uint8_t> buf(data + 1, data + size);
        FuzzManifest(buf.empty() ? nullptr : buf.data(), buf.size());
        return 0;
    }
    if (sel == 6) {
        std::vector<uint8_t> buf(data + 1, data + size);
        const uint32_t bodyLength = static_cast<uint32_t>(buf.size());
        const uint32_t crc = Ext::SaveTail::Crc32(buf.data(), buf.size());
        for (int i = 0; i < 4; ++i) {
            buf.push_back(static_cast<uint8_t>(bodyLength >> (8 * i)));
        }
        for (int i = 0; i < 4; ++i) {
            buf.push_back(static_cast<uint8_t>(crc >> (8 * i)));
        }
        FuzzManifest(buf.data(), buf.size());
        return 0;
    }
    if (size < 2) {
        return 0;
    }
    const int32_t version = static_cast<int8_t>(data[1]);
    // an exact-size heap copy (a zero-length payload gets a 1-byte buffer and size 0)
    uint8_t* payload = static_cast<uint8_t*>(std::malloc(size - 2 == 0 ? 1 : size - 2));
    std::memcpy(payload, data + 2, size - 2);
    if (sel <= 4) {
        const StateCodec* c = Ext::SaveTail::FindCodec(kCodecIds[sel - 1]);
        if (c != nullptr) {
            FuzzCodec(*c, version, payload, size - 2);
        }
    } else {
        const HostCodec* h = Ext::SaveTail::FindHostCodec("toolbar");
        if (h != nullptr) {
            FuzzCodec(*h, version, payload, size - 2);
        }
    }
    std::free(payload);
    return 0;
}

#ifdef FUZZ_SEEDGEN
namespace {

std::string g_dir;
int g_seq = 0;

void Put(uint8_t sel, int8_t version, const std::vector<uint8_t>& body) {
    std::vector<uint8_t> out;
    out.push_back(sel);
    if (sel != 0 && sel != 6) {
        out.push_back(static_cast<uint8_t>(version));
    }
    out.insert(out.end(), body.begin(), body.end());
    char name[512];
    std::snprintf(name, sizeof(name), "%s/seed_%u_%03d", g_dir.c_str(), sel, g_seq++);
    FILE* f = std::fopen(name, "wb");
    if (f == nullptr) {
        std::perror(name);
        std::exit(1);
    }
    std::fwrite(out.data(), 1, out.size(), f);
    std::fclose(f);
}

std::vector<uint8_t> Enc(const StateCodec* c) {
    std::vector<uint8_t> p;
    c->encode(p);
    return p;
}

}  // namespace

extern "C" int LLVMFuzzerInitialize(int* argc, char*** argv) {
    if (*argc < 2) {
        std::fprintf(stderr, "seedgen: usage seedgen <dir>\n");
        std::exit(1);
    }
    g_dir = (*argv)[1];
    using namespace Ext;
    std::vector<Record> manifest;
    auto addRecord = [&](const char* id, int32_t fv, const std::vector<uint8_t>& p) {
        Record r;
        r.id = id;
        r.formatVersion = fv;
        r.payload = p;
        manifest.push_back(r);
    };

    // restricted_foods
    RestrictedFoods::ResetState();
    Put(1, 1, Enc(SaveTail::FindCodec("restricted_foods")));
    RestrictedFoods::ReplaceBlockedPaths({ "Template/Materials/Food/Apple.rsc", "template/fish", "Bread" });
    Put(1, 1, Enc(SaveTail::FindCodec("restricted_foods")));
    addRecord("restricted_foods", 1, Enc(SaveTail::FindCodec("restricted_foods")));

    // item_priority
    ItemPriority::ResetState();
    Put(2, 2, Enc(SaveTail::FindCodec("item_priority")));
    {
        ItemPriority::Lists l;
        ItemPriority::Rank(l, 1, "Template/Materials/Firewood");
        ItemPriority::Rank(l, 1, "Template/Materials/Coal");
        ItemPriority::SetNever(l, 2, "Template/Materials/IronTool");
        ItemPriority::Rank(l, 2, "Template/Materials/SteelTool");
        ItemPriority::Rank(l, 3, "Template/Materials/Herbs");
        ItemPriority::SetNever(l, 4, "Template/Materials/Leather");
        ItemPriority::Rank(l, 4, "Template/Materials/Wool");
        ItemPriority::Apply(l);
    }
    const std::vector<uint8_t> ip = Enc(SaveTail::FindCodec("item_priority"));
    Put(2, 2, ip);
    Put(2, 1, ip);
    addRecord("item_priority", 2, ip);

    // tool_names
    ToolNames::Clear();
    Put(3, 1, Enc(SaveTail::FindCodec("tool_names")));
    {
        static int a, b, c, x, y;
        ToolNames::Citizen w1{ 17, "Anna", 0 };
        ToolNames::Citizen w2{ 42, "Bram", 1 };
        ToolNames::Citizen w3{ 99, "", 1 };
        ToolNames::Remember(&a, &x, 3, w1, "Template/Materials/IronTool");
        ToolNames::Remember(&b, &y, 1, w2, "Template/Materials/SteelTool");
        ToolNames::Remember(&c, &y, 7, w3, "Template/Materials/IronTool");
    }
    const std::vector<uint8_t> tn = Enc(SaveTail::FindCodec("tool_names"));
    Put(3, 1, tn);
    addRecord("tool_names", 1, tn);

    // family_tree
    FamilyTree::Town().Clear();
    Put(4, 2, Enc(SaveTail::FindCodec("family_tree")));
    {
        FamilyTree::Tree& t = FamilyTree::Town();
        const uint32_t m = t.Add("Anna", 0, false, 0, 20, 0, 0);
        const uint32_t f = t.Add("Bram", 1, false, 0, 22, 0, 0);
        t.Marry(m, f, 3);
        const uint32_t k1 = t.Add("Cora", 0, true, 14, 0, m, f);
        const uint32_t k2 = t.Add("Dirk", 1, true, 30, 0, m, f);
        const uint32_t s = t.Add("Eva", 0, false, 40, 18, 0, 0);
        t.Marry(k2, s, 60);
        t.Add("Finn", 1, true, 75, 0, s, k2);
        t.Died(f, 90, 29, 2);
        const uint8_t farmer = t.JobIndex("Farmer");
        const uint8_t wood = t.JobIndex("Woodcutter");
        t.AddJobTime(m, farmer, 36.0f);
        t.AddJobTime(m, wood, 12.0f);
        t.AddJobTime(k1, farmer, 24.0f);
        t.AddJobTime(f, wood, 50.0f);
    }
    const std::vector<uint8_t> ft = Enc(SaveTail::FindCodec("family_tree"));
    Put(4, 2, ft);
    {
        std::vector<uint8_t> v1;
        FamilyTree::Town().Encode(v1, nullptr, 1);
        Put(4, 1, v1);
    }
    addRecord("family_tree", 2, ft);

    // toolbar host
    {
        const HostCodec* h = SaveTail::FindHostCodec("toolbar");
        Toolbar::SavedWindow().open = true;
        Toolbar::SavedWindow().x = 120;
        Toolbar::SavedWindow().y = -4;
        Toolbar::SavedWindow().page = "family_tree";
        std::vector<uint8_t> p;
        h->encode(p);
        Put(5, 1, p);
        Record r;
        r.kind = SaveTail::kKindHost;
        r.flags = 0;
        r.id = "toolbar";
        r.formatVersion = 1;
        r.payload = p;
        manifest.push_back(r);
    }

    // manifests: everything; one per record; a class B-looking empty row; an unknown-kind droppable row
    Put(0, 0, SaveTail::Encode(manifest));
    {
        std::vector<uint8_t> whole = SaveTail::Encode(manifest);
        whole.resize(whole.size() - SaveTail::kTrailerBytes);
        Put(6, 0, whole);
    }
    for (const Record& r : manifest) {
        Put(0, 0, SaveTail::Encode(std::vector<Record>{ r }));
    }
    {
        Record b;
        b.id = "town_screenshot";
        b.formatVersion = 1;
        Record u;
        u.kind = 7;
        u.flags = 0;
        u.id = "future_thing";
        u.formatVersion = 3;
        u.payload = { 1, 2, 3 };
        Put(0, 0, SaveTail::Encode(std::vector<Record>{ b, u }));
    }
    std::printf("seedgen: %d seeds in %s\n", g_seq, g_dir.c_str());
    std::exit(0);
}
#endif
