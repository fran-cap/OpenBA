// ============================================================================
// tests/fuzz/save_meta_fuzz.cpp -- Save::SaveLoad::LoadMetaData over arbitrary bytes
// ============================================================================
// P0_MAP input surface rank 3a (save files: the metadata block). The async loader drops a save's
// compressed metadata image into `SaveLoad::_embeddedState`; LoadMetaData (retail 0x1800c1b30,
// src/engine/save/saveload_loadmeta.cpp) copies it into a CompressedMemoryStream, opens it with
// the result DISCARDED, runs one IO::Serializer (BinaryReader) over it and fills the six
// out-parameters: description, thumbnail (w*h*4 bytes), width, height, version, mod list.
//
// The input is the embedded image exactly as the save carries it: the CompressedStream container
// (u32 blockCount, blockCount x {u32 offset, u16 csize, u16 usize}, block payloads) whose
// decompressed bytes are a BinaryWriter record (0xaaaaaaaa, length, version, body, 0xbbbbbbbb).
//
// The SaveLoad object is a zeroed raw buffer: LoadMetaData reads and writes `_embeddedState`
// only. The image is allocated with the engine allocator because LoadMetaData MemFree's it.
//
// Build (WSL): ~/fz.sh is not used for the link because the archive's host stubs
// (tests/unit/io_stream_abi_stubs.cpp) define the stream bodies this target needs for real; see
// the scratchpad build script save_meta_build.sh.
//
// MASKS (build flags in the scratchpad build.sh, not in this file, so the body under test is unchanged):
//   F-002  compression.cpp is rebuilt with -fno-sanitize=pointer-overflow: the known match-copy
//          pointer overflow fires on almost every compressed seed.
//   SM-1   MASK_SM1=1 rebuilds saveload_loadmeta.cpp with -fno-sanitize=signed-integer-overflow once
//          the w*h*4 overflow (:317) was recorded, so the wrapped size reaches MemAlloc and the read.
//   SM-2/5 MASK_DEEP=1 (one-off repro build only) drops the signed-overflow / nonnull checks in
//          compressed_stream_open.cpp and memory_stream.cpp to see what blockCount*8 and Read(-8, null)
//          lead to under ASan.
// The container defects this target finds (SM-3/4 block sizes over 0x8000, SM-7 the Read spin on a
// short last block) are masked in save_meta2_fuzz.cpp, which fuzzes the record behind our container.
//
// SEED GENERATOR: built with -DFUZZ_SEEDGEN, main() writes seeds made with OUR compressor
// (System::Compression::Compress) and a hand-laid BinaryWriter record (no game content).
// ============================================================================
#include "save/SaveLoad.h"
#include "io/Stream.h"
#include "io/Serializer.h"
#include "collection/Collection.h"
#include "../../src/engine/collection/array_internal.h"
#include "system/MemoryAllocator.h"
#include "system/Compression.h"
#include "system/Configuration.h"   // System::kGameVersion
#include "system/String.h"          // System::String::kSerializedUtf8Flag
#include "system/string_internal.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>
#include <string>
#include <vector>

#include "object/External.h"
#include "io/PackageFile.h"
#include "io/FileSystem.h"
#include "platform_window.h"

namespace rtcoll { extern const ArraySpec kSpec_MetaEntry20; }

// ---- link stubs: symbols the real stream sources drag in, never reached on this path ----------
// (the unit suite defines the same ones inside individual test files, which are not in the archive)
namespace Object {
Base* ExternalList::CreateNewObject() { return nullptr; }
Base* ExternalList::CreateInPlaceObject(void*) { return nullptr; }
}  // namespace Object
namespace IO {
Object::Base* PackageFile::CreateNewObject() { return nullptr; }
Object::Base* PackageFile::CreateInPlaceObject(void*) { return nullptr; }
FileSystem* FileSystem::_singleton = nullptr;
void FileSystem::CloseFile(RtIO::FileInfo&) { __builtin_trap(); }
}  // namespace IO
namespace Platform { namespace Window {
void ShowErrorMessage(const uint16_t*, const uint16_t*) {}
} }  // namespace Platform::Window

namespace {

using ModArray = Collection::Array<Save::SaveLoad::ModInfo>;

void RunOne(const uint8_t* data, size_t size) {
    alignas(16) static unsigned char slStorage[sizeof(Save::SaveLoad)];
    std::memset(slStorage, 0, sizeof(slStorage));
    Save::SaveLoad* sl = reinterpret_cast<Save::SaveLoad*>(slStorage);

    void* image = System::MemoryAllocator::MemAlloc(size ? size : 1, 4);
    if (size)
        std::memcpy(image, data, size);
    sl->_embeddedState._size = static_cast<uint32_t>(size);
    sl->_embeddedState._data = image;

    System::String description;
    void* pixels = nullptr;
    int width = 0;
    int height = 0;
    int version = 0;
    ModArray mods;
    mods._data = nullptr;
    mods._size = 0;
    mods._capacity = 0;

    sl->LoadMetaData(description, &pixels, width, height, version, mods);
#ifdef FUZZ_SEEDGEN
    std::printf("  decoded: w=%d h=%d ver=%d mods=%d desc=%s mod0=%s\n", width, height, version, mods._size,
                description._data ? description._data : "(null)",
                mods._size > 0 && mods._data[0].name._data ? mods._data[0].name._data : "-");
#endif

    if (pixels != nullptr)
        System::MemoryAllocator::MemFree(pixels);
    // The hand-derived row's SetSize(0) releases the two Strings of every entry.
    rtcoll::AsArray<&rtcoll::kSpec_MetaEntry20>(&mods)->SetSize(0);
    if (mods._data != nullptr)
        System::MemoryAllocator::MemFree(mods._data);
    mods._data = nullptr;
    mods._capacity = 0;
}

}  // namespace

#if !defined(FUZZ_SEEDGEN) && !defined(SAVE_META_EMBEDDED)
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size > 0x40000)
        return 0;
    RunOne(data, size);
    return 0;
}
#endif

// ---- record / container builders (seed generator, and save_meta2's container) ----------------
namespace {
void Put32(std::vector<uint8_t>& v, uint32_t x) {
    for (int i = 0; i < 4; ++i)
        v.push_back(static_cast<uint8_t>(x >> (8 * i)));
}
void PutStr(std::vector<uint8_t>& v, const std::string& s) {
    Put32(v, static_cast<uint32_t>(s.size() + 1) | System::String::kSerializedUtf8Flag);
    v.insert(v.end(), s.begin(), s.end());
    v.push_back(0);
}
void PutWStr(std::vector<uint8_t>& v, const std::string& s) {   // legacy UTF-16 arm
    Put32(v, static_cast<uint32_t>(s.size() + 1));
    for (char c : s) {
        v.push_back(static_cast<uint8_t>(c));
        v.push_back(0);
    }
    v.push_back(0);
    v.push_back(0);
}
std::vector<uint8_t> Record(int ver, const std::string& desc, int w, int h, int nmods, bool wide) {
    std::vector<uint8_t> body;
    if (wide)
        PutWStr(body, desc);
    else
        PutStr(body, desc);
    Put32(body, static_cast<uint32_t>(w));
    Put32(body, static_cast<uint32_t>(h));
    for (int i = 0; i < w * h * 4; ++i)
        body.push_back(static_cast<uint8_t>(i * 7));
    if (ver > 2) {
        Put32(body, 1);   // nameMode
        Put32(body, static_cast<uint32_t>(nmods));
        for (int m = 0; m < nmods; ++m) {
            PutStr(body, "mod" + std::to_string(m));
            PutStr(body, "ext:mod" + std::to_string(m));
            Put32(body, 0x1234u + m);
            Put32(body, System::kGameVersion);
            Put32(body, 1);
        }
    }
    std::vector<uint8_t> rec;
    Put32(rec, IO::kRecordMagic);
    Put32(rec, static_cast<uint32_t>(body.size() + 4));
    Put32(rec, static_cast<uint32_t>(ver));
    rec.insert(rec.end(), body.begin(), body.end());
    Put32(rec, IO::kRecordTrailer);
    return rec;
}
// The CompressedStream container, 32 KB blocks, compressed by OUR encoder or STORED.
std::vector<uint8_t> Container(const std::vector<uint8_t>& plain, bool store) {
    std::vector<std::vector<uint8_t>> blocks;
    std::vector<std::pair<uint16_t, uint16_t>> sizes;
    for (size_t off = 0; off < plain.size(); off += 0x8000) {
        const size_t n = std::min<size_t>(0x8000, plain.size() - off);
        std::vector<uint8_t> enc(n + n / 16 + 64);
        System::Compression c;
        const int r = store ? 0 : c.Compress(plain.data() + off, static_cast<uint32_t>(n), enc.data(),
                                            static_cast<uint32_t>(enc.size()));
        RtSystem::RtMemFree(c._table);
        if (r > 0 && static_cast<size_t>(r) < n) {
            enc.resize(static_cast<size_t>(r));
            sizes.push_back({static_cast<uint16_t>(r), static_cast<uint16_t>(n)});
        } else {
            enc.assign(plain.begin() + off, plain.begin() + off + n);
            sizes.push_back({0, static_cast<uint16_t>(n)});
        }
        blocks.push_back(enc);
    }
    std::vector<uint8_t> out;
    Put32(out, static_cast<uint32_t>(blocks.size()));
    uint32_t pos = 4 + 8 * static_cast<uint32_t>(blocks.size());
    for (size_t i = 0; i < blocks.size(); ++i) {
        Put32(out, pos);
        out.push_back(static_cast<uint8_t>(sizes[i].first));
        out.push_back(static_cast<uint8_t>(sizes[i].first >> 8));
        out.push_back(static_cast<uint8_t>(sizes[i].second));
        out.push_back(static_cast<uint8_t>(sizes[i].second >> 8));
        pos += static_cast<uint32_t>(blocks[i].size());
    }
    for (auto& b : blocks)
        out.insert(out.end(), b.begin(), b.end());
    return out;
}
}  // namespace

#ifdef FUZZ_SEEDGEN
// seedgen <container seed dir> [<plain record seed dir, for save_meta2>]
int main(int argc, char** argv) {
    const char* dir = argc > 1 ? argv[1] : ".";
    const char* plainDir = argc > 2 ? argv[2] : nullptr;
    struct S { int ver; const char* desc; int w, h, n; bool wide, store; };
    const S seeds[] = {
        {3, "My town", 4, 4, 2, false, false}, {3, "Autosave", 16, 9, 0, false, false},
        {2, "Old save", 2, 2, 0, false, true},  {3, "Wide", 3, 3, 1, true, false},
        {3, "", 0, 0, 0, false, true},          {3, "Big", 128, 96, 3, false, false},
        {1, "v1", 1, 1, 0, false, false},       {3, "Mods", 1, 1, 8, false, true},
    };
    int k = 0;
    for (const S& s : seeds) {
        const std::vector<uint8_t> rec = Record(s.ver, s.desc, s.w, s.h, s.n, s.wide);
        const std::vector<uint8_t> img = Container(rec, s.store);
        if (plainDir != nullptr) {
            const std::string pp = std::string(plainDir) + "/rec" + std::to_string(k);
            if (FILE* pf = std::fopen(pp.c_str(), "wb")) {
                std::fwrite(rec.data(), 1, rec.size(), pf);
                std::fclose(pf);
            }
        }
        const std::string path = std::string(dir) + "/seed" + std::to_string(k++);
        FILE* f = std::fopen(path.c_str(), "wb");
        if (!f)
            return 1;
        std::fwrite(img.data(), 1, img.size(), f);
        std::fclose(f);
        RunOne(img.data(), img.size());   // the seed must decode cleanly through the real body
    }
    std::printf("wrote %d seeds\n", k);
    return 0;
}
#endif
