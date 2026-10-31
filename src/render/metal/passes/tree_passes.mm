// NOT RETAIL: reuse the unchanged, platform-independent DXBC transformations.
// Retail inputs and transformed DXBC exist only in memory / the local airconv cache.
#include "tree_passes.h"
#include "tree_variant_key.h"
#include "better_trees/cutout_rewrite.h"
#include "better_trees/receiver_rewrite.h"
#include "better_trees/edge_dither.h"
#include "better_trees/natural_growth.h"
#include <algorithm>
#include <cstdio>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace VideoMetal::TreePasses {
namespace {

// What the extension asked for (main thread writes, the render thread reads).
struct Request {
    int mode = 0;
    float fillK = 0.0f;
    float fillFloor = 0.0f;
    BbsRewrite::Params dither = BbsRewrite::kDefaultParams;
    bool billboardShadow = false;
    bool naturalGrowth = false;   // NATURAL_GROWTH.md (pixel: with the foliage request; vertex: with the row)
    bool naturalGrowthVertex = false;
};

std::mutex requestMutex;
Request requested;

// The render thread's copy for the current frame.
Request frame;
bool msaa = false;

std::unordered_set<const Shader*> coverage;
std::unordered_set<const Shader*> grown;   // NATURAL_GROWTH.md: the natural-growth variants
id<MTLDevice> cachedDevice = nil;

// A transformed shader. `retail` is a copy of the shader it replaces (ARC keeps its function
// alive), so the blend guard never points back into a program's shader vector.
struct Variant {
    Shader shader;
    Shader retail;
    bool changed = false;
    bool coverage = false;
    bool grown = false;
};

std::unordered_map<std::string, Variant> cache;
std::unordered_map<const Shader*, Shader*> originals;

}  // namespace

void SetFoliage(int mode, float fillK, float fillFloor, const BbsRewrite::Params* dither, bool naturalGrowth) {
    std::lock_guard<std::mutex> guard(requestMutex);
    requested.mode = mode;
    requested.fillK = fillK;
    requested.fillFloor = fillFloor;
    requested.dither = dither ? *dither : BbsRewrite::kDefaultParams;
    requested.naturalGrowth = naturalGrowth;
}

void SetBillboardShadow(bool on, bool naturalGrowth) {
    std::lock_guard<std::mutex> guard(requestMutex);
    requested.billboardShadow = on;
    requested.naturalGrowthVertex = naturalGrowth;
}

bool IsCoverage(const Shader* pixel) {
    return pixel && coverage.count(pixel) != 0;
}

bool IsNaturalGrowth(const Shader* shader) {
    return shader && grown.count(shader) != 0;
}

BlendGuard::BlendGuard(Shader*& pixel, bool blending, const Shader* vertex)
    : slot(pixel), saved(pixel) {
    if (!blending && !(IsNaturalGrowth(pixel) && !IsNaturalGrowth(vertex))) {
        return;
    }
    auto found = originals.find(pixel);
    if (found != originals.end()) {
        slot = found->second;
    }
}

void BeginFrame() {
    Request next;
    {
        std::lock_guard<std::mutex> guard(requestMutex);
        next = requested;
    }
    const bool nextMsaa = CurrentSamples() > 1;
    if (next.mode != frame.mode || nextMsaa != msaa || next.naturalGrowth != frame.naturalGrowth) {
        std::fprintf(stderr, "[MetalPasses] better_trees mode %d (%s), natural growth %d\n", next.mode,
                     nextMsaa ? "multisample: alpha-to-coverage" : "single sample: cutout retained",
                     next.naturalGrowth ? 1 : 0);
    }
    frame = next;
    msaa = nextMsaa;
}

namespace {

// NATURAL_GROWTH.md: natural growth on `bytes` when it is wanted and matches (into `grownOut`).
bool Grow(const std::vector<uint8_t>& bytes, bool wanted, std::vector<uint8_t>& grownOut) {
    if (!wanted) {
        return false;
    }
    NgRewrite::Match match;
    NgRewrite::Analyse(bytes.data(), bytes.size(), &match);
    return NgRewrite::Transform(match, &grownOut);
}

// The pixel half: natural growth, then the cutout rewrite, then the billboard edge dither on colour programs.
bool MakePixelVariant(const std::vector<uint8_t>& retailBytes, std::vector<uint8_t>& output, bool& coverageOut,
                      bool& grownOut) {
    std::vector<uint8_t> grownBytes;
    grownOut = Grow(retailBytes, frame.naturalGrowth, grownBytes);
    const auto& bytes = grownOut ? grownBytes : retailBytes;
    FaaRewrite::Match match;
    FaaRewrite::Analyse(bytes.data(), bytes.size(), &match);
    auto options = FaaRewrite::OptionsFor(frame.mode, frame.fillK, msaa, frame.fillFloor);
    bool changed = FaaRewrite::Transform(match, options, &output);
    coverageOut = changed && options.coverage;
    if (!changed && grownOut) {
        output = grownBytes;
    }
    if (!match.colour) {
        return changed || grownOut;
    }
    const std::vector<uint8_t> input = (changed || grownOut) ? output : bytes;
    BbsDither::Match dither;
    BbsDither::Analyse(input.data(), input.size(), &dither);
    std::vector<uint8_t> dithered;
    if (BbsDither::Transform(dither, &dithered, frame.dither)) {
        output = std::move(dithered);
        changed = true;
    }
    return changed || grownOut;
}

// The vertex half: natural growth, then the billboard receiver's shadow lookup.
bool MakeVertexVariant(const std::vector<uint8_t>& retailBytes, std::vector<uint8_t>& output, bool& grownOut) {
    std::vector<uint8_t> grownBytes;
    grownOut = Grow(retailBytes, frame.naturalGrowthVertex, grownBytes);
    const auto& bytes = grownOut ? grownBytes : retailBytes;
    BbsRewrite::Match match;
    BbsRewrite::Analyse(bytes.data(), bytes.size(), &match);
    if (BbsRewrite::TransformWithParams(match, &output, frame.dither)) {
        return true;
    }
    if (grownOut) {
        output = std::move(grownBytes);
        return true;
    }
    return false;
}

}  // namespace

Shader* Select(Shader* retail, const std::vector<uint8_t>& bytes,
               id<MTLDevice> device, bool vertex) {
    const bool wanted = vertex ? frame.billboardShadow : frame.mode != 0;
    if (!wanted || !retail || retail->identity.empty()) {
        return retail;
    }
    if (cachedDevice != device) {
        originals.clear();
        cache.clear();
        coverage.clear();
        grown.clear();
        cachedDevice = device;
    }
    std::string key;
    if (vertex) {
        VertexSettings settings;
        settings.params = frame.dither;
        settings.billboardShadow = frame.billboardShadow;
        settings.naturalGrowth = frame.naturalGrowthVertex;
        key = VertexVariantKey(retail->identity, settings);
    } else {
        PixelSettings settings;
        settings.mode = frame.mode;
        settings.msaa = msaa;
        settings.fillK = frame.fillK;
        settings.fillFloor = frame.fillFloor;
        settings.dither = frame.dither;
        settings.naturalGrowth = frame.naturalGrowth;
        key = PixelVariantKey(retail->identity, settings);
    }
    auto found = cache.find(key);
    if (found == cache.end()) {
        Variant variant;
        std::vector<uint8_t> output;
        bool changed = false;
        if (vertex) {
            changed = MakeVertexVariant(bytes, output, variant.grown);
        } else {
            changed = MakePixelVariant(bytes, output, variant.coverage, variant.grown);
        }
        if (changed) {
            variant.shader = Translate(output, device);
            variant.changed = variant.shader.function != nil;
            if (!variant.changed) {
                variant.coverage = false;
                variant.grown = false;
            }
            variant.retail = *retail;
            std::fprintf(stderr, "[MetalPasses] %s rewrite%s %zu -> %zu: %s\n",
                         vertex ? "billboard receiver" : "cutout/dither", variant.grown ? " + natural growth" : "",
                         bytes.size(), output.size(), variant.changed ? "loaded" : "FAILED; using retail");
        }
        found = cache.emplace(std::move(key), std::move(variant)).first;
        if (found->second.changed && found->second.grown) {
            grown.insert(&found->second.shader);
        }
        if (found->second.changed && !vertex) {
            originals[&found->second.shader] = &found->second.retail;
            if (found->second.coverage) {
                coverage.insert(&found->second.shader);
            }
        }
    }
    if (!found->second.changed) {
        return retail;
    }
    return &found->second.shader;
}

}  // namespace VideoMetal::TreePasses
