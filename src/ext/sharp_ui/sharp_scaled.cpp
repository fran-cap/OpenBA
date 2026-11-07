// ============================================================================
// src/ext/sharp_ui/sharp_scaled.cpp -- the magnified sprite sheets' engine half
// (NOT RETAIL; sharp_scaled.h / sharp_sheet.h / sharp_encode.h have the contract)
// ============================================================================
#include "ext/sharp_ui/sharp_scaled.h"

#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>

#include "ext/derived_cache/derived_cache.h"
#include "ext/derived_cache/hash64.h"
#include "ext/resources/sheet_builder.h"
#include "ext/sharp_ui/sharp_encode.h"
#include "ext/sharp_ui/sharp_math.h"
#include "ext/sharp_ui/sharp_sheet.h"
#include "ext/sharp_ui/sharp_sprites_data_2x.h"
#include "ext/sharp_ui/sharp_sprites_data_4x.h"
#include "graphics/Material.h"
#include "object/External.h"
#include "system/Filename.h"
#include "system/String.h"
#include "ui/SpriteSheet.h"
#include "video/Device.h"
#include "video/ImageBuffer.h"

namespace Ext {
namespace SharpUI {

namespace {

constexpr const char* kDialogSheet     = "Dialog/SpriteSheet.rsc";
constexpr const char* kExtensionsSheet = "Dialog/ExtensionsSprites.rsc";
constexpr int32_t  kFormatBgra8   = 5;    // ImageBuffer::_format 5 = DXGI B8G8R8A8_UNORM
constexpr int32_t  kFormatBc3     = 2;    // ImageBuffer::_format 2 = DXGI BC3_UNORM (1 is BC3 too)

// A copy at or above this factor is made as BC3 with its mips; below, one BGRA8
// level as the sheet itself. A 2x copy keeps no mips: the UI sampler takes the
// NEAREST level (MIN_MAG_LINEAR_MIP_POINT), and at slider 75 (1.33 px per
// unit) that would be a 1x-sized level magnified, softer than the 2x one
// minified.
constexpr int kCompressFrom = 4;

// The derived cache's key for a compressed chain (derived_cache.h): bump the
// version whenever the encode's output changes (sharp_encode.cpp, its mips).
constexpr uint32_t kCacheProducer = DerivedCache::MakeProducer('S', 'H', 'S', 'C');
constexpr uint32_t kCacheVersion  = 1;

// The baked art, per factor and sheet (tools/sharp_sprites/pack.py).
struct ArtSet {
    int              factor;
    const ArtSprite* dialog;
    int              dialogCount;
    const ArtSprite* extensions;
    int              extensionsCount;
};
constexpr ArtSet kArtSets[] = {
    {SpritesData2x::kScale, SpritesData2x::kSprites, SpritesData2x::kSpriteCount, SpritesData2x::kExtSprites,
     SpritesData2x::kExtSpriteCount},
    {SpritesData4x::kScale, SpritesData4x::kSprites, SpritesData4x::kSpriteCount, SpritesData4x::kExtSprites,
     SpritesData4x::kExtSpriteCount},
};
constexpr int kFactors[] = {SpritesData2x::kScale, SpritesData4x::kScale};
constexpr int kFactorCount = static_cast<int>(sizeof kFactors / sizeof kFactors[0]);
static_assert(SpritesData2x::kScale < SpritesData4x::kScale, "PickSheetFactor wants the factors ascending");

const ArtSet* ArtFor(int factor) {
    for (const ArtSet& a : kArtSets)
        if (a.factor == factor) return &a;
    return nullptr;
}

const char* KindName(SheetKind kind) {
    return kind == SheetKind::Extensions ? "extensions" : "dialog";
}

// One build: what it was made FROM (the sheet's content, not its address: the
// factory's Clean frees a sheet and the next load makes a new one, often at the
// same address, and our Extensions sheet is rebuilt at every Clean) and what it
// made. Kept for the process: a texture a frame in flight may still sample is
// never freed; a reloaded sheet with the same content finds its build again, so
// a session holds one copy per sheet content and factor.
struct Built {
    uint64_t                    content  = 0;   // ContentOf
    SheetKind                   kind     = SheetKind::None;
    int                         factor   = 0;
    Video::ImageBuffer*         texture  = nullptr;
    Graphics::MaterialInstance* material = nullptr;
};
std::vector<Built> g_built;

// A build in flight. The CPU half (the upscale, our art, the BC3 chain or the
// cache read) runs on a worker thread from copies of everything it needs, so
// no frame waits for it; the main thread makes the texture and the material
// when a selection next asks for it (ScaledSheetMaterial), with the sheet as it
// is then. The worker never touches the engine.
struct Job {
    // Inputs, written before the worker starts.
    uint64_t                  content = 0;
    SheetKind                 kind    = SheetKind::None;
    int                       factor  = 0;
    int                       width   = 0;   // the base sheet
    int                       height  = 0;
    int                       pad     = 0;
    std::vector<uint8_t>      pixels;        // the base sheet, BGRA8
    std::vector<ScaledSprite> sprites;
    std::chrono::steady_clock::time_point started;
    // Outputs, read once `done` is set.
    std::atomic<bool>    done{false};
    bool                 compress = false;
    bool                 cached   = false;
    int                  drawn    = 0;
    std::vector<uint8_t> atlas;   // the one BGRA8 level (a copy below kCompressFrom)
    Bc3Chain             chain;   // the BC3 levels (from kCompressFrom)
    double               workMs = 0.0;
    // Main thread only: the frame poll saw it done (ScaledSheetJobFinished).
    bool seen = false;
};
std::vector<std::shared_ptr<Job>> g_jobs;

// The (base, factor) pairs a build failed for before its content was read (a
// texture that is not BGRA8, a failed read back): logged once, not retried.
struct Failed {
    const Video::ImageBuffer* base;
    int                       factor;
};
std::vector<Failed> g_failed;

bool HasFailed(const Video::ImageBuffer* base, int factor) {
    for (const Failed& f : g_failed)
        if (f.base == base && f.factor == factor) return true;
    return false;
}

bool SamePathNoCase(const char* a, const char* b) {
    for (; *a != '\0' && *b != '\0'; ++a, ++b) {
        const char ca = (*a == '\\') ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(*a)));
        const char cb = (*b == '\\') ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(*b)));
        if (ca != cb) return false;
    }
    return *a == '\0' && *b == '\0';
}

Video::ImageBuffer* DiffuseOf(const UI::SpriteSheet& sheet) {
    const Graphics::MaterialInstance* const m = sheet.m_field30;
    if (m == nullptr) return nullptr;
    const int32_t i = m->FindTexture(System::String("diffuse"));
    if (i < 0 || i >= m->_textures._size) return nullptr;
    return m->_textures._data[i].image;
}

template <class T>
T* NewObject() {
    return static_cast<T*>(T::CreateNewObject());
}

// The compressed chain of `atlas` (w x h BGRA8): found in the derived cache, or
// encoded and queued for storing. `cached` says which. Any thread: the store's
// Find / Put are (derived_cache.h, THREADS).
Bc3Chain CompressedChain(const std::vector<uint8_t>& atlas, int w, int h, bool* cached) {
    Bc3Chain chain;
    chain.width = w;
    chain.height = h;
    const int levels = MipLevelCount(w, h);
    size_t total = 0;
    for (int i = 0; i < levels; ++i) {
        chain.offsets.push_back(total);
        total += Bc3LevelSize(MipSize(w, i), MipSize(h, i));
    }
    DerivedCache::Key key;
    key.producer   = kCacheProducer;
    key.version    = kCacheVersion;
    key.shape[0]   = static_cast<uint32_t>(w);
    key.shape[1]   = static_cast<uint32_t>(h);
    key.shape[2]   = static_cast<uint32_t>(levels);
    key.shape[3]   = static_cast<uint32_t>(kFormatBc3);
    key.sourceSize = atlas.size();
    key.sourceHash = DerivedCache::Hash64(atlas.data(), atlas.size());
    DerivedCache::Store* const store = DerivedCache::Shared();
    *cached = store != nullptr && store->Find(key, chain.data) && chain.data.size() == total;
    if (*cached) return chain;
    const MipChain mips = BuildMipChain(atlas.data(), w, h, w * 4);
    chain = EncodeBc3Chain(mips);
    if (store != nullptr && chain.data.size() == total) store->Put(key, chain.data);
    return chain;
}

// What a build is made from: the sheet's pixels and its sprites' rectangles.
uint64_t ContentOf(const UI::SpriteSheet& sheet, const std::vector<uint8_t>& pixels) {
    uint64_t h = DerivedCache::Hash64(pixels.data(), pixels.size());
    for (int i = 0; i < sheet.m_sprites._size; ++i) {
        const UI::SpriteInfo& s = sheet.m_sprites._data[i];
        const float uv[4] = {s.u0, s.v0, s.u1, s.v1};
        h = DerivedCache::Hash64(uv, sizeof uv, h);
    }
    return h;
}

// The worker: everything but the engine objects.
void RunJob(const std::shared_ptr<Job> job) {
    const auto t0 = std::chrono::steady_clock::now();
    job->drawn = BuildScaledSheet(job->pixels.data(), job->width, job->height, job->factor, job->pad,
                                  job->sprites.data(), static_cast<int>(job->sprites.size()), job->atlas);
    job->compress = job->factor >= kCompressFrom;
    if (job->compress) {
        job->chain = CompressedChain(job->atlas, job->width * job->factor, job->height * job->factor,
                                     &job->cached);
        job->atlas.clear();
        job->atlas.shrink_to_fit();
    }
    job->pixels.clear();
    job->pixels.shrink_to_fit();
    job->workMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    job->done.store(true, std::memory_order_release);
}

std::shared_ptr<Job> StartJob(UI::SpriteSheet& sheet, SheetKind kind, int factor, uint64_t content,
                              std::vector<uint8_t>&& pixels, int w, int h,
                              std::chrono::steady_clock::time_point t0) {
    const ArtSet* const set = ArtFor(factor);
    const bool ext = kind == SheetKind::Extensions;
    const ArtSprite* const table = set == nullptr ? nullptr : ext ? set->extensions : set->dialog;
    const int tableCount = set == nullptr ? 0 : ext ? set->extensionsCount : set->dialogCount;

    auto job = std::make_shared<Job>();
    job->content = content;
    job->kind    = kind;
    job->factor  = factor;
    job->width   = w;
    job->height  = h;
    job->pad     = sheet.m_field50 ? 1 : 0;
    job->pixels  = std::move(pixels);
    job->started = t0;
    const int count = sheet.m_sprites._size;
    job->sprites.resize(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        const UI::SpriteInfo& s = sheet.m_sprites._data[i];
        // The atlas position is the UVs': a compiled record's x / y are the
        // kit's pad offset (1, 1), not where the sprite sits (sheet_builder.h).
        job->sprites[static_cast<size_t>(i)] =
            SpriteFromRecord(table, tableCount, s.name._data, s.width, s.height, s.u0, s.v0, s.u1, s.v1, w, h,
                             factor);
    }
    if (job->factor >= kCompressFrom && DerivedCache::Shared() == nullptr) DerivedCache::OpenShared();
    std::thread(RunJob, job).detach();
    return job;
}

// The main thread's half of a finished job: the texture and the material, on
// the sheet's base material and sampler as they are now.
Built Finish(UI::SpriteSheet& sheet, const Video::ImageBuffer& base, Job& job) {
    Built b;
    const bool ext = job.kind == SheetKind::Extensions;
    Resources::SheetLayout layout;
    layout.width  = job.width * job.factor;
    layout.height = job.height * job.factor;
    layout.padded = job.pad != 0;

    Video::ImageBuffer::LevelArray levels;
    if (job.compress) {
        const Bc3Chain& chain = job.chain;
        const int n = static_cast<int>(chain.offsets.size());
        levels.SetSize(n);
        for (int i = 0; i < n; ++i) {
            const size_t at = chain.offsets[static_cast<size_t>(i)];
            const size_t end = i + 1 < n ? chain.offsets[static_cast<size_t>(i) + 1] : chain.data.size();
            levels[i].SetSize(static_cast<int>(end - at));
            std::memcpy(levels[i].GetData(), chain.data.data() + at, end - at);
        }
    } else {
        levels.SetSize(1);
        levels[0].SetSize(static_cast<int>(job.atlas.size()));
        std::memcpy(levels[0].GetData(), job.atlas.data(), job.atlas.size());
    }

    Resources::SheetSpec spec;
    spec.sheetWidth   = layout.width;
    spec.sheetHeight  = layout.height;
    spec.imageName    = ext ? "Build/SharpExtensionsSheet.png" : "Build/SharpSpriteSheet.png";
    spec.imageAddress = base._addressU;
    spec.imageFilter  = base._filter;
    spec.imageFormat  = job.compress ? kFormatBc3 : kFormatBgra8;
    spec.imageFlags   = job.compress ? Video::ImageBuffer::kMipmaps : 0u;

    Graphics::Material* const baseMaterial = sheet.m_field30->_material.Get();
    b.texture  = NewObject<Video::ImageBuffer>();
    b.material = NewObject<Graphics::MaterialInstance>();
    if (b.texture == nullptr || b.material == nullptr || baseMaterial == nullptr) {
        std::fprintf(stderr, "[SharpUI] %dx %s sheet: could not make its texture / material\n", job.factor,
                     KindName(job.kind));
        delete b.texture;
        delete b.material;
        return Built();
    }
    Resources::FillImageBuffer(*b.texture, spec, layout);
    // As Resources::CreateTexture, with this copy's format, flags and levels.
    if (!b.texture->Create(0, spec.imageKind, spec.imageFormat, spec.imageFlags, layout.width, layout.height,
                           spec.imageFilter, spec.imageAddress, spec.imageAddress, spec.imageAddress, &levels)) {
        std::fprintf(stderr, "[SharpUI] %dx %s sheet: creating the %d x %d texture failed\n", job.factor,
                     KindName(job.kind), layout.width, layout.height);
        delete b.texture;
        delete b.material;
        return Built();
    }
    Resources::FillMaterialInstance(*b.material, baseMaterial, "diffuse", b.texture);
    b.content = job.content;
    b.kind = job.kind;
    b.factor = job.factor;
    const double ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - job.started).count();
    const char* const how = !job.compress ? "BGRA8" : job.cached ? "BC3 + mips, cached" : "BC3 + mips, encoded";
    std::fprintf(stderr, "[SharpUI] %dx %s sheet built: %d x %d %s, %d of %d sprites from our art, "
                 "%.1f ms on a worker, drawn %.1f ms after it was asked for\n",
                 job.factor, KindName(job.kind), layout.width, layout.height, how, job.drawn,
                 static_cast<int>(job.sprites.size()), job.workMs, ms);
    return b;
}

}  // namespace

SheetKind KindOfSheet(const char* path) {
    if (path == nullptr) return SheetKind::None;
    char buf[256];
    std::snprintf(buf, sizeof buf, "%s", path);
    char* colon = std::strchr(buf, ':');
    if (colon != nullptr) *colon = '\0';   // "Dialog/SpriteSheet.rsc:resource"
    if (SamePathNoCase(buf, kDialogSheet)) return SheetKind::Dialog;
    if (SamePathNoCase(buf, kExtensionsSheet)) return SheetKind::Extensions;
    return SheetKind::None;
}

int PickSheetScale(float pxPerUnitX, float pxPerUnitY) {
    return PickSheetFactor(pxPerUnitX, pxPerUnitY, kFactors, kFactorCount);
}

Graphics::MaterialInstance* ScaledSheetMaterial(UI::SpriteSheet* sheet, SheetKind kind, int factor) {
    if (sheet == nullptr || kind == SheetKind::None || ArtFor(factor) == nullptr) return nullptr;
    Video::ImageBuffer* const base = DiffuseOf(*sheet);
    if (base == nullptr) return nullptr;
    if (HasFailed(base, factor)) return nullptr;
    if (base->_format != kFormatBgra8 || base->_width <= 0 || base->_height <= 0 ||
        Video::Device::_singleton == nullptr) {
        std::fprintf(stderr, "[SharpUI] %dx %s sheet: the sheet texture is not a plain BGRA8 image "
                     "(format %d, %d x %d); retail's sheet is drawn\n", factor, KindName(kind), base->_format,
                     base->_width, base->_height);
        g_failed.push_back(Failed{base, factor});
        return nullptr;
    }
    // Finish narrows each level's byte size to an int (Array::SetSize): a copy
    // past the texture limit is never built (review F-228).
    if (!ScaledSheetFits(base->_width, base->_height, factor)) {
        std::fprintf(stderr, "[SharpUI] %dx %s sheet: %d x %d is too large to copy at %dx; retail's sheet "
                     "is drawn\n", factor, KindName(kind), base->_width, base->_height, factor);
        g_failed.push_back(Failed{base, factor});
        return nullptr;
    }
    // Read back once per selection (a scale change, a load, a finished job;
    // never per frame): the content decides whether an earlier build serves.
    const auto t0 = std::chrono::steady_clock::now();
    std::vector<uint8_t> pixels(static_cast<size_t>(base->_width) * base->_height * 4);
    if (!base->CopyFromTexture(pixels.data(), base->_width, base->_height)) {
        std::fprintf(stderr, "[SharpUI] %dx %s sheet: reading back the %d x %d sheet failed\n", factor,
                     KindName(kind), base->_width, base->_height);
        g_failed.push_back(Failed{base, factor});
        return nullptr;
    }
    const uint64_t content = ContentOf(*sheet, pixels);
    for (const Built& b : g_built)
        if (b.content == content && b.kind == kind && b.factor == factor) return b.material;
    for (size_t i = 0; i < g_jobs.size(); ++i) {
        const std::shared_ptr<Job> job = g_jobs[i];
        if (job->content != content || job->kind != kind || job->factor != factor) continue;
        if (!job->done.load(std::memory_order_acquire)) return nullptr;   // retail's sheet until it is
        g_jobs.erase(g_jobs.begin() + static_cast<std::ptrdiff_t>(i));
        const Built b = Finish(*sheet, *base, *job);
        if (b.material == nullptr) {
            g_failed.push_back(Failed{base, factor});
            return nullptr;
        }
        g_built.push_back(b);
        return b.material;
    }
    g_jobs.push_back(StartJob(*sheet, kind, factor, content, std::move(pixels), base->_width, base->_height, t0));
    return nullptr;
}

void PrewarmScaledSheets(int factor) {
    // The (sheet, factor) pairs the prewarm has asked for: each is read back once, not every frame.
    struct Asked {
        const UI::SpriteSheet* sheet;
        int                    factor;
    };
    static std::vector<Asked> asked;
    Object::ExternalFactory* const f = Object::ExternalFactory::_singleton;
    if (f == nullptr || ArtFor(factor) == nullptr) return;
    for (UI::SpriteSheet* s = f->FindNextAs<UI::SpriteSheet>(UI::SpriteSheet::TypeId(), nullptr);
         s != nullptr; s = f->FindNextAs<UI::SpriteSheet>(UI::SpriteSheet::TypeId(), s)) {
        bool seen = false;
        for (const Asked& a : asked)
            seen = seen || (a.sheet == s && a.factor == factor);
        if (seen) continue;
        const System::Filename path = s->GetPath();
        const SheetKind kind = KindOfSheet(path._data);
        if (kind == SheetKind::None || DiffuseOf(*s) == nullptr) continue;
        asked.push_back(Asked{s, factor});
        ScaledSheetMaterial(s, kind, factor);
    }
}

void WaitForScaledSheets(int capMs) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(capMs);
    for (;;) {
        bool pending = false;
        for (const std::shared_ptr<Job>& job : g_jobs)
            pending = pending || !job->done.load(std::memory_order_acquire);
        if (!pending || std::chrono::steady_clock::now() >= until) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

bool ScaledSheetJobFinished() {
    bool any = false;
    for (const std::shared_ptr<Job>& job : g_jobs) {
        if (job->seen || !job->done.load(std::memory_order_acquire)) continue;
        job->seen = true;
        any = true;
    }
    return any;
}

void DropScaledSheets() {
    if (!g_built.empty()) {
        std::fprintf(stderr, "[SharpUI] video device going away: %d sheet cop%s dropped, rebuilt on the new device\n",
                     static_cast<int>(g_built.size()), g_built.size() == 1 ? "y" : "ies");
    }
    for (const Built& b : g_built) {
        delete b.material;   // the material holds the texture: it goes first
        delete b.texture;
    }
    g_built.clear();
    g_failed.clear();
}

void DropSeenJobs() {
    for (size_t i = 0; i < g_jobs.size();) {
        if (g_jobs[i]->seen)
            g_jobs.erase(g_jobs.begin() + static_cast<std::ptrdiff_t>(i));
        else
            ++i;
    }
}

}  // namespace SharpUI
}  // namespace Ext
