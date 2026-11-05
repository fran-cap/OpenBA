// ============================================================================
// tests/unit/save_metadata_hardening_test.cpp — Save::SaveLoad::LoadMetaData
//   (retail 0x1800c1b30, src/engine/save/saveload_loadmeta.cpp) on malformed
//   save metadata blocks (review 2026-11-03, findings F-061 and F-035)
// ============================================================================
//
// Each test hands LoadMetaData the compressed byte image the async loader would
// have dropped into `_embeddedState`: one stored (uncompressed) window wrapping a
// BinaryReader payload — magic, word, format version, then the record itself.
// The payloads are written out INLINE as hex bytes.
//
//   F-061  width * height * 4 is a 32-bit signed product of two values read from
//          the file (UB on overflow; retail 0x1800c1c3a `IMUL ESI,[RBX]`). A
//          thumbnail whose size does not fit is rejected: width and height come
//          back 0 and no pixels are read.
//   F-035  the mod count is read from the file and handed to SetSize unbounded
//          (retail 0x1800c1c9a / 0x180034010). The fuzz record below asks for
//          0x70000000 entries (56 GB): the failed allocation leaves the array null
//          and the init loop writes near null. A count out of range is read as 0.
//
// A well-formed version 3 record (1x1 thumbnail, one mod) pins the valid path.
//
// The allocator seat (System::g_memoryAllocatorInstrument) routes MemAlloc /
// MemFree to malloc / free: the unit build has no MemoryAllocator singleton. A
// request above 1 GB is answered null, as a real allocator out of memory would.
// The mod array is never destroyed (ModInfo's out-of-line dtor is a link stub
// here); its buffer is freed by hand, its Strings are empty.
// ============================================================================
#include "unit.h"

#include "save/SaveLoad.h"
#include "system/String.h"
#include "system/MemoryAllocator.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>
#include <vector>

namespace {

using Save::SaveLoad;
using ModInfo = Save::SaveLoad::ModInfo;

uint64_t g_maxRequest = 0;

void* SeatAlloc(uint64_t size, int) {
    if (size > g_maxRequest)
        g_maxRequest = size;
    if (size > (uint64_t(1) << 30))
        return nullptr;
    return std::malloc(static_cast<size_t>(size == 0 ? 1 : size));
}

void SeatFree(void* p) {
    std::free(p);
}

struct SeatScope {
    SeatScope() {
        g_maxRequest = 0;
        System::g_memoryAllocatorInstrument.memAlloc = &SeatAlloc;
        System::g_memoryAllocatorInstrument.memFree  = &SeatFree;
    }
    ~SeatScope() {
        System::g_memoryAllocatorInstrument.memAlloc = nullptr;
        System::g_memoryAllocatorInstrument.memFree  = nullptr;
    }
};

struct MetaResult {
    int      width   = -1;
    int      height  = -1;
    int      version = -1;
    int      modCount = -1;
    uint32_t mod0[3] = {0, 0, 0};
};

// Wraps `payload` in one stored window (window count 1, header 12, compressed
// size 0, uncompressed size n) and runs LoadMetaData on it.
MetaResult RunLoadMetaData(const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> blob = {0x01, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x00, 0x00, 0x00};
    blob.push_back(static_cast<uint8_t>(payload.size()));
    blob.push_back(static_cast<uint8_t>(payload.size() >> 8));
    blob.insert(blob.end(), payload.begin(), payload.end());
    blob.resize(blob.size() + 64, 0);   // the reader's slack, as the fuzz harness gave it

    MetaResult out;
    SeatScope seat;
    // LoadMetaData frees `_embeddedState._data` when it is done: the image is the
    // allocator's.
    void* image = System::MemoryAllocator::MemAlloc(blob.size(), 4);
    std::memcpy(image, blob.data(), blob.size());

    alignas(16) static unsigned char slMem[sizeof(SaveLoad)];
    std::memset(slMem, 0, sizeof slMem);
    SaveLoad* sl = reinterpret_cast<SaveLoad*>(slMem);
    sl->_embeddedState._data = static_cast<uint8_t*>(image);
    sl->_embeddedState._size = static_cast<uint32_t>(blob.size());

    alignas(16) unsigned char modsMem[sizeof(Collection::Array<ModInfo>)];
    std::memset(modsMem, 0, sizeof modsMem);
    auto* mods = reinterpret_cast<Collection::Array<ModInfo>*>(modsMem);
    {
        System::String desc;
        void* pixels = nullptr;
        sl->LoadMetaData(desc, &pixels, out.width, out.height, out.version, *mods);
        System::MemoryAllocator::MemFree(pixels);
    }
    out.modCount = mods->_size;
    if (mods->_size > 0) {
        out.mod0[0] = mods->_data[0].versionMajor;
        out.mod0[1] = mods->_data[0].gameVersion;
        out.mod0[2] = mods->_data[0].versionMinor;
    }
    System::MemoryAllocator::MemFree(mods->_data);
    return out;
}

}  // namespace

// A valid version 3 record: empty description, 1x1 thumbnail, one mod with empty
// name and path and versions 7 / 0x01000400 / 9. Unchanged by the hardening.
UNIT_TEST(save_meta_valid_record_reads_thumbnail_and_mods) {
    const std::vector<uint8_t> payload = {
        0xaa, 0xaa, 0xaa, 0xaa, 0x19, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,   // magic, word, version 3
        0x00, 0x00, 0x00, 0x80,                                                   // description ""
        0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,                           // width 1, height 1
        0x11, 0x22, 0x33, 0x44,                                                   // the 4 thumbnail bytes
        0x03, 0x00, 0x00, 0x00,                                                   // version again
        0x01, 0x00, 0x00, 0x00,                                                   // mod count 1
        0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x80,                           // name "", path ""
        0x07, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x01, 0x09, 0x00, 0x00, 0x00,   // versionMajor, game, minor
        0xbb, 0xbb, 0xbb, 0xbb,
    };
    const MetaResult r = RunLoadMetaData(payload);
    CHECK_EQ(r.version, 3);
    CHECK_EQ(r.width, 1);
    CHECK_EQ(r.height, 1);
    CHECK_EQ(r.modCount, 1);
    CHECK_EQ(r.mod0[0], 7u);
    CHECK_EQ(r.mod0[1], 0x01000400u);
    CHECK_EQ(r.mod0[2], 9u);
}

// F-061, fuzz input sm1_wh_mul_overflow shape: width = height = 0x8000, so
// width * height * 4 = 2^32 overflows int32 (UB; at -O0 it wraps to 0 and the
// caller is handed a 0x8000 x 0x8000 image backed by a 0-byte buffer).
UNIT_TEST(save_meta_thumbnail_size_overflow_is_rejected) {
    const std::vector<uint8_t> payload = {
        0xaa, 0xaa, 0xaa, 0xaa, 0x19, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,   // version 1
        0x00, 0x00, 0x00, 0x80,                                                   // description ""
        0x00, 0x80, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00,                           // width, height 0x8000
        0xbb, 0xbb, 0xbb, 0xbb,
    };
    const MetaResult r = RunLoadMetaData(payload);
    CHECK_EQ(r.version, 1);
    CHECK_EQ(r.width, 0);
    CHECK_EQ(r.height, 0);
}

// F-061, a negative dimension: -1 x 4 x 4 = -16, sign-extended into an 18 EB
// allocation request.
UNIT_TEST(save_meta_thumbnail_negative_size_is_rejected) {
    const std::vector<uint8_t> payload = {
        0xaa, 0xaa, 0xaa, 0xaa, 0x19, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,   // version 1
        0x00, 0x00, 0x00, 0x80,                                                   // description ""
        0xff, 0xff, 0xff, 0xff, 0x04, 0x00, 0x00, 0x00,                           // width -1, height 4
        0xbb, 0xbb, 0xbb, 0xbb,
    };
    const MetaResult r = RunLoadMetaData(payload);
    CHECK_EQ(r.width, 0);
    CHECK_EQ(r.height, 0);
    CHECK(g_maxRequest < (uint64_t(1) << 30));
}

// F-035, fuzz input modcount_0x70000000_record (37 B), verbatim:
// aaaaaaaa1900000003000000010000800000000000000000000300000000000070bbbbbbbb
// Read as: version 3, description with a 1-byte length field set, ... mod count
// 0x70000000. Before the fix SetSize asks for 0x70000000 * 0x20 bytes, gets null,
// and the init loop writes near null.
UNIT_TEST(save_meta_fuzz_mod_count_is_bounded) {
    const std::vector<uint8_t> payload = {
        0xaa, 0xaa, 0xaa, 0xaa, 0x19, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
        0x01, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x70, 0xbb, 0xbb, 0xbb,
        0xbb,
    };
    const MetaResult r = RunLoadMetaData(payload);
    CHECK_EQ(r.version, 3);
    CHECK_EQ(r.modCount, 0);
    CHECK(g_maxRequest < (uint64_t(1) << 30));
}

// F-035, a negative mod count (0xffffffff) is read as no mods.
UNIT_TEST(save_meta_negative_mod_count_is_empty) {
    const std::vector<uint8_t> payload = {
        0xaa, 0xaa, 0xaa, 0xaa, 0x19, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,   // version 3
        0x00, 0x00, 0x00, 0x80,                                                   // description ""
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,                           // width 0, height 0
        0x03, 0x00, 0x00, 0x00,                                                   // version again
        0xff, 0xff, 0xff, 0xff,                                                   // mod count -1
        0xbb, 0xbb, 0xbb, 0xbb,
    };
    const MetaResult r = RunLoadMetaData(payload);
    CHECK_EQ(r.version, 3);
    CHECK_EQ(r.modCount, 0);
}

// review S-032: UpdateList state 4 can call LoadMetaData with no load issued, while
// `_embeddedState` is still {nullptr, 0} from the previous call's closing stores. The copy
// into the stream is then skipped (memmove with a null source is undefined even for 0 bytes);
// the empty stream fails every read and the record comes back empty, and the state stays
// {nullptr, 0}. GCC declares memmove nonnull_if_nonzero, so no sanitizer flags the old call:
// this test pins the path, it cannot show the undefined copy.
UNIT_TEST(save_meta_empty_embedded_state_review_s032) {
    SeatScope seat;
    alignas(16) static unsigned char slMem[sizeof(SaveLoad)];
    std::memset(slMem, 0, sizeof slMem);
    SaveLoad* sl = reinterpret_cast<SaveLoad*>(slMem);
    CHECK(sl->_embeddedState._data == nullptr);
    CHECK_EQ(sl->_embeddedState._size, 0u);

    alignas(16) unsigned char modsMem[sizeof(Collection::Array<ModInfo>)];
    std::memset(modsMem, 0, sizeof modsMem);
    auto* mods = reinterpret_cast<Collection::Array<ModInfo>*>(modsMem);
    int width = -1;
    int height = -1;
    int version = -1;
    {
        System::String desc;
        void* pixels = nullptr;
        sl->LoadMetaData(desc, &pixels, width, height, version, *mods);
        System::MemoryAllocator::MemFree(pixels);
    }
    CHECK(sl->_embeddedState._data == nullptr);
    CHECK_EQ(sl->_embeddedState._size, 0u);
    CHECK_EQ(mods->_size, 0);
    System::MemoryAllocator::MemFree(mods->_data);
}
