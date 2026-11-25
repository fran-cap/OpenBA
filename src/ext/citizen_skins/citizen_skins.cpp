// ============================================================================
// src/ext/citizen_skins/citizen_skins.cpp -- ext.citizen_skins (docs/extensions/handins/CITIZENSKINS.md)
// ============================================================================
// NOT RETAIL. citizen_skins.h has the design, skin_math.h the byte work.
//
// ORDER. Everything that can be checked is checked before anything is changed: the packages read, the strips
// decoded, the chains built, the program's blobs patched on COPIES. Then the two textures are made, then each
// program is written and made again. Those last steps can still fail on the device; when one does, the
// programs already written get their own bytes back and both textures are made again from the first package's
// strips alone (vanilla's), so the game is left in one consistent state, never half changed. A game without
// the row, or one that could not join the strips, is exactly retail.
// ============================================================================

#include "ext/citizen_skins/citizen_skins.h"
#include "ext/citizen_skins/skin_math.h"

#include "ext/registry.h"

// src/render/common: the package mappings and the block directory's decoder
#include "create_on_demand/payload.h"
#include "graphics/Material.h"
#include "io/FileSystem.h"
#include "io/PackageFile.h"
#include "object/External.h"
#include "system/Filename.h"
#include "video/Device.h"
#include "video/ImageBuffer.h"
#include "video/ShaderProgram.h"
#include "video/VideoProgram.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace Ext {
namespace CitizenSkins {

namespace {

const char kMaleTexture[] = "Models\\Citizen\\MaterialInstance\\CitizenTexture.rsc:resource";
const char kFemaleTexture[] = "Models\\Citizen\\MaterialInstance\\CitizenTextureF.rsc:resource";
const char kMaleMaterial[] = "Models\\Citizen\\MaterialInstance\\citizen.rsc:resource";
const char kFemaleMaterial[] = "Models\\Citizen\\MaterialInstance\\citizenf.rsc:resource";

// What a package can make this extension read: a compiled strip is a few hundred KB at most (vanilla's is 88
// KB), and a package's pairs are bounded again by the candidates kept in all.
constexpr int32_t kMaxEntryBytes = 16 * 1024 * 1024;
constexpr size_t kMaxCandidates = 64;

std::atomic<int> g_sets{1};   // the strips in the installed texture; 1 = nothing installed
// Each installed set's identity: written before g_sets is stored, read after it is loaded (the two are never
// read while they are written: a reader that sees 1 does not read them).
std::atomic<uint64_t> g_ids[kMaxSets];
// OnGameStart / OnVideoRestored (loader and main thread, never together in practice)
std::mutex g_mutex;
bool g_done = false;          // the pass has run for this process (a failure is not tried again every load)

// What a device restart needs to make it all again.
struct Kept {
    Chain male;
    Chain female;
    Chain male1;      // the first package's strip alone: what the textures go back to when an install fails
    Chain female1;
    int sets = 1;
    std::vector<uint64_t> ids;   // one identity per set (SetIdentity)
};
std::unique_ptr<Kept> g_kept;

// ---- reading the packages ---------------------------------------------------------------------------------

uint32_t Rd32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

// The decompressed bytes of one package entry (a compiled resource: the block directory, then LZ blocks).
bool ReadEntry(IO::PackageFile* pack, int index, std::vector<uint8_t>* out) {
    if (pack == nullptr || index < 0 || index >= pack->GetFileCount()) return false;
    const uint64_t offset = pack->GetFileOffset(index);
    const uint32_t size = pack->GetFileSize(index);
    if (size < 12) return false;
    CreateOnDemand::PackageMap* map = CreateOnDemand::Maps().Acquire(pack->_file.winHandle);
    if (map == nullptr) return false;
    bool ok = false;
    if (offset + size <= map->map.size) {
        const uint8_t* entry = map->map.data + offset;
        const uint32_t blocks = Rd32(entry);
        if (blocks >= 1 && blocks < 4096 && 4 + static_cast<uint64_t>(blocks) * 8 <= size) {
            // block b = { uint32 sourceOffset, uint16 compressedSize, uint16 uncompressedSize }; all but the
            // last hold 32 KB
            uint16_t last;
            std::memcpy(&last, entry + 4 + 8 * static_cast<size_t>(blocks - 1) + 6, 2);
            const int32_t total = static_cast<int32_t>((blocks - 1) * CreateOnDemand::kBlockBytes + last);
            if (total <= kMaxEntryBytes) {
                out->assign(static_cast<size_t>(total), 0);
                ok = CreateOnDemand::RedecodeRange(entry, size, 0, total, out->data());
            }
        }
    }
    CreateOnDemand::Maps().Release(map);
    return ok;
}

// One strip of a package. `expectW` x `expectH` (above 0) is the first package's size: any other is refused
// before it is decoded.
bool LoadStrip(IO::PackageFile* pack, int index, const char* entry, Strip* out, uint64_t* hash, int expectW,
               int expectH) {
    std::vector<uint8_t> bytes;
    std::string why;
    if (!ReadEntry(pack, index, &bytes) ||
        !ReadTextureLevel0(bytes.data(), bytes.size(), out, &why, expectW, expectH)) {
        std::fprintf(stderr, "[CitizenSkins] package %u: %s is not a strip this reads (%s)\n",
                     static_cast<unsigned>(pack->GetCRC()), entry, why.empty() ? "unreadable" : why.c_str());
        return false;
    }
    *hash = StripHash(*out);
    return true;
}

// The size of the first strips read (vanilla's): every later strip must be this size.
struct RefSize {
    int maleW = 0;
    int maleH = 0;
    int femaleW = 0;
    int femaleH = 0;
};

// Every mounted package that carries a citizen texture, in mount order (vanilla first). A package gives its
// own pair (CitizenTexture, CitizenTextureF) first, then its numbered pairs (CitizenTexture2 +
// CitizenTextureF2 ...) in number order: each pair is one candidate set. A numbered pair with only one of
// the two wears the package's own for the other. A package's own pair is always read; at most kMaxSets
// numbered pairs are read from one package and kMaxCandidates pairs in all.
std::vector<PackSkin> GatherPackages() {
    std::vector<PackSkin> found;
    IO::FileSystem* fs = IO::FileSystem::_singleton;
    if (fs == nullptr) return found;
    RefSize ref;
    size_t baseCount = 0;   // how many candidates the first package that has a citizen texture gave (vanilla's)
    bool haveBase = false;
    for (int p = 0; p < fs->_packfiles.GetSize() && found.size() < kMaxCandidates; ++p) {
        IO::PackageFile* pack = fs->_packfiles.GetData()[p];
        if (pack == nullptr) continue;
        std::map<int, PackSkin> byNumber;   // 0 = the package's own pair, 2..99 = the numbered ones
        for (int i = 0; i < pack->GetFileCount(); ++i) {
            const System::Filename name = pack->GetFileName(i);
            const char* text = name.AsString()._data;
            bool female = false;
            const int number = ParseStripName(text, &female);
            if (number < 0) continue;
            const bool isNew = byNumber.find(number) == byNumber.end();
            if (isNew && number != 0 && static_cast<int>(byNumber.size()) >= kMaxSets) continue;
            PackSkin& skin = byNumber[number];
            if (!female && !skin.hasMale) {
                skin.hasMale = LoadStrip(pack, i, text, &skin.male, &skin.hashMale, ref.maleW, ref.maleH);
                if (skin.hasMale && ref.maleW == 0) {
                    ref.maleW = skin.male.width;
                    ref.maleH = skin.male.height;
                }
            } else if (female && !skin.hasFemale) {
                skin.hasFemale = LoadStrip(pack, i, text, &skin.female, &skin.hashFemale, ref.femaleW,
                                           ref.femaleH);
                if (skin.hasFemale && ref.femaleW == 0) {
                    ref.femaleW = skin.female.width;
                    ref.femaleH = skin.female.height;
                }
            }
        }
        // First the halves a numbered pair lacks (copied from the package's own pair, which must still be
        // whole: nothing is moved out yet), then the pairs go to the list in number order.
        const auto own = byNumber.find(0);
        if (own != byNumber.end()) {
            for (auto& entry : byNumber) {
                if (entry.first == 0) continue;
                PackSkin& skin = entry.second;
                if (!skin.hasMale && own->second.hasMale) {
                    skin.male = own->second.male;
                    skin.hashMale = own->second.hashMale;
                    skin.hasMale = true;
                }
                if (!skin.hasFemale && own->second.hasFemale) {
                    skin.female = own->second.female;
                    skin.hashFemale = own->second.hashFemale;
                    skin.hasFemale = true;
                }
            }
        }
        for (auto& entry : byNumber) {
            if (found.size() >= kMaxCandidates) break;
            if (entry.second.hasMale || entry.second.hasFemale) found.push_back(std::move(entry.second));
        }
        if (!haveBase && !found.empty()) {
            haveBase = true;
            baseCount = found.size();
        }
    }
    // Vanilla's own look is only the fallback: once a mod brings citizen textures, the village wears the mods'
    // sets alone (a player who turns Cyberpunk on wants every citizen in Cyberpunk, not one in five in vanilla's
    // clothes). The first strips are kept only to size-check the rest, which is done by now.
    // Vanilla's strips still lend the half a mod lacks (a mod with only the men's texture wears vanilla's women).
    if (haveBase && found.size() > baseCount) {
        const PackSkin base = found.front();
        found.erase(found.begin(), found.begin() + static_cast<std::ptrdiff_t>(baseCount));
        for (PackSkin& skin : found) {
            if (!skin.hasMale && base.hasMale) {
                skin.male = base.male;
                skin.hashMale = base.hashMale;
                skin.hasMale = true;
            }
            if (!skin.hasFemale && base.hasFemale) {
                skin.female = base.female;
                skin.hashFemale = base.hashFemale;
                skin.hasFemale = true;
            }
        }
    }
    return found;
}

// ---- installing -------------------------------------------------------------------------------------------

bool MakeTexture(const char* path, const Chain& chain) {
    Object::ExternalFactory* const factory = Object::ExternalFactory::_singleton;
    if (factory == nullptr) return false;
    Video::ImageBuffer* tex = factory->FindOrLoadAs<Video::ImageBuffer>(System::Filename(path));
    if (!Object::IsDerivedFromNullSafe(tex, Video::ImageBuffer::TypeId())) {
        std::fprintf(stderr, "[CitizenSkins] %s did not load\n", path);
        return false;
    }
    Video::ImageBuffer::LevelArray levels;
    levels.SetSize(static_cast<int>(chain.levels.size()));
    for (size_t i = 0; i < chain.levels.size(); ++i) {
        Collection::Array<uint8_t>& l = levels[static_cast<int>(i)];
        l.SetSize(static_cast<int>(chain.levels[i].size()));
        std::memcpy(l.GetData(), chain.levels[i].data(), chain.levels[i].size());
    }
    const uint32_t flags = tex->_flags | Video::ImageBuffer::kMipmaps;
    const bool ok = tex->Create(0, tex->_kind, 5 /* BGRA8 */, flags, chain.width, chain.height, tex->_filter,
                                tex->_addressU, tex->_addressV, tex->_addressW, &levels);
    if (!ok) {
        std::fprintf(stderr, "[CitizenSkins] %s: ImageBuffer::Create(%d x %d) failed\n", path, chain.width,
                     chain.height);
    }
    return ok;
}

// Both textures back to the first package's strip alone (the divide in the program is 8 again by then).
void RestoreTextures(const Kept& kept) {
    const bool male = MakeTexture(kMaleTexture, kept.male1);
    const bool female = MakeTexture(kFemaleTexture, kept.female1);
    if (!male || !female) {
        std::fprintf(stderr, "[CitizenSkins] the first package's strips could not be put back\n");
    }
}

// The vertex programs of a citizen material's passes: where `l(0.125)` is, once each.
void CollectPrograms(const char* materialPath, std::vector<Video::VertexProgram*>* out) {
    Object::ExternalFactory* const factory = Object::ExternalFactory::_singleton;
    if (factory == nullptr) return;
    Graphics::MaterialInstance* mi =
        factory->FindOrLoadAs<Graphics::MaterialInstance>(System::Filename(materialPath));
    if (!Object::IsDerivedFromNullSafe(mi, Graphics::MaterialInstance::TypeId())) return;
    Graphics::Material* m = mi->_material.Get();
    if (m == nullptr) return;
    for (int i = 0; i < m->_passes.GetSize(); ++i) {
        Video::VideoProgram* pass = m->_passes.GetData()[i].Get();
        Video::VertexProgram* vp = pass != nullptr ? pass->_vertexProgram.Get() : nullptr;
        if (vp == nullptr) continue;
        bool seen = false;
        for (Video::VertexProgram* q : *out) seen = seen || q == vp;
        if (!seen) out->push_back(vp);
    }
}

// Every variant's bytes as they are now.
std::vector<std::vector<uint8_t>> CurrentBlobs(const Video::VertexProgram* vp) {
    std::vector<std::vector<uint8_t>> out;
    for (int v = 0; v < vp->_byteCode.GetSize(); ++v) {
        const Collection::Array<uint8_t>& b = vp->_byteCode.GetData()[v];
        out.emplace_back(b.GetData(), b.GetData() + b.GetSize());
    }
    return out;
}

// The patched copies of every variant of `vp`. All or nothing: false when a variant is not exactly the
// program this patches (no `l(0.125)`, more than one, or a checksum that is not its own), and then nothing is
// patched.
bool PatchedCopies(const Video::VertexProgram* vp, int sets, std::vector<std::vector<uint8_t>>* out) {
    *out = CurrentBlobs(vp);
    if (out->empty()) return false;
    for (std::vector<uint8_t>& blob : *out) {
        if (PatchVertexBlob(&blob, sets) != 1) return false;
    }
    return true;
}

// The bytes of `vp` replaced and the program made again; false when the device refuses it.
bool Write(Video::VertexProgram* vp, const std::vector<std::vector<uint8_t>>& blobs) {
    for (size_t v = 0; v < blobs.size(); ++v) {
        Collection::Array<uint8_t>& b = vp->_byteCode[static_cast<int>(v)];
        std::memcpy(b.GetData(), blobs[v].data(), blobs[v].size());
    }
    vp->PlatformDestruct();
    vp->PlatformConstruct();
    if (vp->_data == nullptr || !vp->PlatformCreate()) {
        std::fprintf(stderr, "[CitizenSkins] the device refused a character program\n");
        return false;
    }
    return true;
}

// Texture and program for `sets` strips; the combined chains are those of `kept`. True with everything in
// place; false with the game as vanilla's strips and the original program (or, when a restore itself fails,
// as far as the log says).
bool Install(const Kept& kept) {
    std::vector<Video::VertexProgram*> programs;
    CollectPrograms(kMaleMaterial, &programs);
    CollectPrograms(kFemaleMaterial, &programs);
    struct Job {
        Video::VertexProgram* vp;
        std::vector<std::vector<uint8_t>> blobs;      // patched
        std::vector<std::vector<uint8_t>> original;   // as it is now
    };
    std::vector<Job> jobs;
    for (Video::VertexProgram* vp : programs) {
        Job j{vp, {}, {}};
        if (PatchedCopies(vp, kept.sets, &j.blobs)) {
            j.original = CurrentBlobs(vp);
            jobs.push_back(std::move(j));
        }
    }
    if (jobs.empty()) {
        std::fprintf(stderr, "[CitizenSkins] no character program with the texture divide: left as it is\n");
        return false;
    }
    if (!MakeTexture(kMaleTexture, kept.male) || !MakeTexture(kFemaleTexture, kept.female)) {
        std::fprintf(stderr, "[CitizenSkins] a texture was not made: the first package's strips again\n");
        RestoreTextures(kept);
        return false;
    }
    size_t written = 0;
    bool ok = true;
    while (ok && written < jobs.size()) {
        ok = Write(jobs[written].vp, jobs[written].blobs);
        ++written;
    }
    if (!ok) {
        // the programs touched so far (the one that failed included) get their own bytes back
        for (size_t i = 0; i < written; ++i) Write(jobs[i].vp, jobs[i].original);
        RestoreTextures(kept);
        return false;
    }
    for (int i = 0; i < kept.sets && i < kMaxSets && i < static_cast<int>(kept.ids.size()); ++i) {
        g_ids[i].store(kept.ids[static_cast<size_t>(i)], std::memory_order_relaxed);
    }
    g_sets.store(kept.sets, std::memory_order_release);
    std::fprintf(stderr, "[CitizenSkins] %d strips (%d x %d): %zu program(s) patched\n", kept.sets,
                 kept.male.width, kept.male.height, jobs.size());
    return true;
}

}  // namespace

bool Enabled() {
    const Entry* row = FindEntry("citizen_skins");
    return row != nullptr && Requested(*row);
}

void OnGameStart() {
    if (!Enabled()) return;
    const std::lock_guard<std::mutex> lock(g_mutex);
    if (g_done) return;
    if (Video::Device::_singleton == nullptr) return;   // not tried: the next game start does
    g_done = true;
    const std::vector<PackSkin> packs = GatherPackages();
    std::vector<std::string> notes;
    const Sets sets = ChooseSets(packs, &notes);
    for (const std::string& note : notes) std::fprintf(stderr, "[CitizenSkins] %s\n", note.c_str());
    const int n = static_cast<int>(sets.male.size());
    std::fprintf(stderr, "[CitizenSkins] %zu citizen texture pair(s) found, %d different set(s)\n",
                 packs.size(), n);
    if (n < 2) return;
    // The first package's strips alone: what the textures go back to when an install fails.
    Chain male1;
    Chain female1;
    std::string why;
    if (!BuildChain({sets.male[0]}, &male1, &why) || !BuildChain({sets.female[0]}, &female1, &why)) {
        std::fprintf(stderr, "[CitizenSkins] the first strips do not make a chain: %s\n", why.c_str());
        return;
    }
    // The device may refuse a very wide image: try all the sets, then the first 8, so the worst case is fewer
    // sets, never a broken citizen. (A shorter list drops the sets that come last in the mount order.)
    const int attempts[2] = {n, std::min(n, 8)};
    for (int a = 0; a < 2; ++a) {
        const int count = attempts[a];
        if (a == 1 && count == attempts[0]) break;   // nothing smaller to try
        const std::vector<const Strip*> male(sets.male.begin(), sets.male.begin() + count);
        const std::vector<const Strip*> female(sets.female.begin(), sets.female.begin() + count);
        auto kept = std::make_unique<Kept>();
        kept->sets = count;
        kept->ids.assign(sets.ids.begin(), sets.ids.begin() + count);
        kept->male1 = male1;
        kept->female1 = female1;
        if (!BuildChain(male, &kept->male, &why) || !BuildChain(female, &kept->female, &why)) {
            std::fprintf(stderr, "[CitizenSkins] the strips do not join: %s\n", why.c_str());
            return;
        }
        if (Install(*kept)) {
            g_kept = std::move(kept);
            return;
        }
        if (a == 0 && attempts[1] != attempts[0]) {
            std::fprintf(stderr, "[CitizenSkins] %d strips did not install: trying %d\n", count, attempts[1]);
        }
    }
}

int SlotFor(const char* citizenName, int variant) {
    const int n = g_sets.load(std::memory_order_acquire);
    if (n <= 1 || n > kMaxSets) return variant;
    uint64_t ids[kMaxSets];
    for (int i = 0; i < n; ++i) ids[i] = g_ids[i].load(std::memory_order_relaxed);
    return Ext::CitizenSkins::SlotFor(SetForName(citizenName, ids, n), variant);
}

void OnVideoLost() {
    // the reload that follows gives the vanilla texture and program back; nothing of ours lives on the device
    g_sets.store(1, std::memory_order_release);
}

void OnVideoRestored() {
    const std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_kept) return;
    if (!Enabled()) return;   // switched off since: the reload's own (retail) texture and program stay
    if (!Install(*g_kept)) {
        std::fprintf(stderr, "[CitizenSkins] could not install the strips again after the device restart\n");
    }
}

}  // namespace CitizenSkins
}  // namespace Ext
