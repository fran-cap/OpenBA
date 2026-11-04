// ============================================================================
// tests/fuzz/derived_cache_fuzz.cpp -- Ext::DerivedCache::Store::Open / LoadIndex / Find over
//   arbitrary derived.dat, derived.idx and derived.used files
// ============================================================================
// The derived-data cache lives in the user's Documents folder (a local cache: a damaged or planted
// file reaches this parser; its XXH64 checks are not a signature). P0_MAP §8. Linux only (flock,
// a private /tmp directory per process, so fork mode's workers do not share files).
//
// Input: [flags u8][cap u8][idx length u16][used length u16] idx bytes, used bytes, the rest = dat.
//   flags bit 0  every 80-byte record of the idx gets kRecordMagic and its own RecordCheck (the
//                fuzzer cannot forge an XXH64; an attacker can, so this is the realistic input)
//         bit 1  both headers get the right magic / format / record size (generations kept)
//         bit 2  derived.idx's generation := derived.dat's (else the mismatched-pair path)
//         bit 3  RequestCompact + Flush before Close (the writer's rewrite)
//         bit 4  every record whose bytes lie in derived.dat gets their real payloadHash
//         bit 5  the lock is held by "another instance" (this target): OpenReadOnly
//   cap 0 = 1 TB, else 32 + cap * 64 bytes.
// After Open: Find on every record's key (hits, misses, bad payloads), then Close.
// ============================================================================
#include "ext/derived_cache/derived_cache.h"
#include "ext/derived_cache/hash64.h"

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace Ext::DerivedCache;

namespace {

constexpr uint32_t kRecordMagic = 0x31454344u;   // "DCE1" (derived_cache.cpp)
constexpr size_t kHeader = 32;

std::string g_dir;

void WriteFile(const char* name, const uint8_t* p, size_t n, bool present) {
    const std::string path = g_dir + "/" + name;
    if (!present) {
        ::unlink(path.c_str());
        return;
    }
    FILE* f = std::fopen(path.c_str(), "wb");
    if (f == nullptr)
        std::abort();
    if (n != 0)
        std::fwrite(p, 1, n, f);
    std::fclose(f);
}

void SetHeader(std::vector<uint8_t>& f, const char* magic, uint32_t recordSize) {
    if (f.size() < kHeader)
        f.resize(kHeader, 0);
    std::memcpy(f.data(), magic, 8);
    const uint32_t format = kFormatVersion;
    std::memcpy(f.data() + 8, &format, 4);
    std::memcpy(f.data() + 12, &recordSize, 4);
}

Key RecordKey(const IndexRecord& r) {
    Key k;
    k.producer = r.producer;
    k.version = r.version;
    std::memcpy(k.shape, r.shape, sizeof(k.shape));
    k.sourceSize = r.sourceSize;
    k.sourceHash = r.sourceHash;
    return k;
}

}  // namespace

extern "C" int LLVMFuzzerInitialize(int*, char***) {
    char tmpl[] = "/tmp/fz_dcache_XXXXXX";
    if (::mkdtemp(tmpl) == nullptr)
        std::abort();
    g_dir = tmpl;
    SetQuietSpellForTest(1 << 30);   // no timed rewrite: only bit 3 asks for one
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size < 6 || size > 0x10000)
        return 0;
    const uint8_t flags = data[0];
    const uint64_t cap = data[1] == 0 ? (uint64_t{1} << 40) : 32 + uint64_t{data[1]} * 64;
    const size_t idxLen = data[2] | (data[3] << 8);
    const size_t usedLen = data[4] | (data[5] << 8);
    if (6 + idxLen + usedLen > size)
        return 0;
    std::vector<uint8_t> idx(data + 6, data + 6 + idxLen);
    std::vector<uint8_t> used(data + 6 + idxLen, data + 6 + idxLen + usedLen);
    std::vector<uint8_t> dat(data + 6 + idxLen + usedLen, data + size);

    if (flags & 2) {
        SetHeader(dat, "OBADCDAT", 0);
        SetHeader(idx, "OBADCIDX", sizeof(IndexRecord));
    }
    if ((flags & 4) && idx.size() >= kHeader && dat.size() >= kHeader)
        std::memcpy(idx.data() + 16, dat.data() + 16, 8);
    std::vector<IndexRecord> records;
    for (size_t at = kHeader; at + sizeof(IndexRecord) <= idx.size(); at += sizeof(IndexRecord)) {
        IndexRecord r;
        std::memcpy(&r, idx.data() + at, sizeof(r));
        if (flags & 1)
            r.magic = kRecordMagic;
        // MASK (finding: Hash64(nullptr, 0) forms null + 8, UBSan "applying non-zero offset 8 to
        // null pointer" at hash64.cpp:75; first hit by this very call on an empty derived.dat):
        // the harness hashes only a non-empty range.
        if ((flags & 16) && !dat.empty() && r.size != 0 && r.offset <= dat.size() &&
            r.size <= dat.size() - r.offset)
            r.payloadHash = Hash64(dat.data() + r.offset, static_cast<size_t>(r.size));
        if (flags & 1)
            r.check = Hash64(&r, offsetof(IndexRecord, check));
        std::memcpy(idx.data() + at, &r, sizeof(r));
        records.push_back(r);
    }
    WriteFile("derived.idx", idx.data(), idx.size(), true);
    WriteFile("derived.dat", dat.data(), dat.size(), true);
    WriteFile("derived.used", used.data(), used.size(), usedLen != 0);
    WriteFile("derived.clear", nullptr, 0, false);
    WriteFile("derived.idx.new", nullptr, 0, false);

    int lockFd = -1;
    if (flags & 32) {
        lockFd = ::open((g_dir + "/derived.lock").c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
        if (lockFd < 0 || ::flock(lockFd, LOCK_EX | LOCK_NB) != 0)
            std::abort();
    }
    {
        Store store;
        store.Open(g_dir, cap);
        std::vector<uint8_t> out;
        for (const IndexRecord& r : records)
            store.Find(RecordKey(r), out);
        if (flags & 8) {
            store.RequestCompact();
            store.Flush();
            for (const IndexRecord& r : records)
                store.Find(RecordKey(r), out);
        }
        store.Close();
    }
    if (lockFd >= 0)
        ::close(lockFd);
    return 0;
}
