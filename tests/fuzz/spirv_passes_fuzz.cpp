// ============================================================================
// tests/fuzz/spirv_passes_fuzz.cpp -- the Vulkan backend's word-level SPIR-V passes and the SPIR-V
// cache entry decode over arbitrary bytes
// ============================================================================
// P0_MAP input surface rank 5 (with rank 8, the derived-data cache): DecodeEntry
// (src/render/vulkan/shader_convert.cpp) reads a payload from the user's Documents\Banished\Cache, and
// the module it hands back goes to DowngradeSpirv (device.cpp CreateShaderModule, the Vulkan 1.1 path)
// and WidenInputs (pipelines.cpp, linked pixel programs on the 1.1 path) with no further check than
// CheckSpirvStructure. LowerBitFieldInsert and CombineImageSamplers run on dxbc-spirv's output only
// (ConvertDxbc), so for them this target is a robustness check of the word walkers, not a reachable
// surface from a file.
//
// Input: byte 0 is a mode byte, byte 1 picks the WidenInputs widths, the rest is the data.
//   bit 0  the rest is a cache payload: DecodeEntry, then the passes on the module it returns
//   else   the rest is SPIR-V words (little-endian, a trailing partial word dropped), in a heap block of
//          exactly that many words so ASan sees a read past the module's end
//   bit 1  words mode: only modules CheckSpirvStructure accepts (the gate DecodeEntry applies)
//   bit 2  CombineImageSamplers on set 1 instead of set 0
// Built by the scratchpad helper fzx.sh (dxbc-spirv instrumented + the three converter sources).
// Seeds: the FUZZ_SEEDGEN main converts our own programs (the dxbc_spirv target's seeds) and writes the
// modules (before and after the combined-sampler pass) and their cache entries.
// ============================================================================
#include "render/vulkan/shader_convert.h"
#include "render/vulkan/spirv_combine.h"
#include "render/vulkan/spirv_legalize.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace VideoVulkan::Shaders;

namespace {

void RunPasses(const std::vector<uint32_t>& module, uint8_t mode, uint8_t widthByte) {
    std::string why;
    {
        std::vector<uint32_t> m = module;
        (void)DowngradeSpirv(&m, &why);
    }
    {
        uint32_t widths[32];
        for (uint32_t i = 0; i < 32; ++i)
            widths[i] = ((widthByte >> ((i % 4) * 2)) & 3u) + 1u + (i & 1u);
        std::vector<uint32_t> m = module;
        uint32_t changed = 0;
        (void)WidenInputs(&m, widths, &changed, &why);
    }
    {
        std::vector<uint32_t> m = module;
        uint32_t changed = 0;
        (void)LowerBitFieldInsert(&m, &changed, &why);
    }
    // MASK (findings: CombineImageSamplers reads w[1] of an OpTypeSampler (spirv_combine.cpp:118),
    // w[2] of an OpLoad (:363) and words 0..2 of an OpEntryPoint (:343) without checking the
    // instruction is that long, past the module's end when it is the last one). Recorded once; such
    // modules skip the pass so the campaign reaches the rest of it.
    for (size_t at = 5; at < module.size();) {
        const uint32_t words = module[at] >> 16;
        const uint32_t op = module[at] & 0xFFFFu;
        if (words == 0) break;
        if ((op == 26 && words < 2) || (op == 61 && words < 4) || (op == 15 && words < 4)) return;
        at += words;
    }
    {
        std::vector<uint32_t> m = module;
        ShaderReflection refl;
        (void)CombineImageSamplers(&m, (mode & 4) ? 1u : 0u, &refl, &why);
    }
}

}  // namespace

#ifndef FUZZ_SEEDGEN
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size < 2 || size > 0x40000)
        return 0;
    const uint8_t mode = data[0];
    const uint8_t widthByte = data[1];
    data += 2;
    size -= 2;
    if (mode & 1) {
        const std::vector<uint8_t> payload(data, data + size);
        std::vector<uint32_t> spirv;
        ShaderReflection refl;
        if (!DecodeEntry(payload, &spirv, &refl)) return 0;
        RunPasses(spirv, mode, widthByte);
        return 0;
    }
    std::vector<uint32_t> words(size / 4);
    if (!words.empty()) std::memcpy(words.data(), data, words.size() * 4);
    if ((mode & 2) && !CheckSpirvStructure(words, nullptr)) return 0;
    RunPasses(words, mode, widthByte);
    return 0;
}
#else
// ---- FUZZ_SEEDGEN: seedgen <out dir> <dxbc_spirv seed files...> -------------------------------------
namespace {
int g_seed = 0;
void Write(const char* dir, uint8_t mode, uint8_t widths, const uint8_t* data, size_t size) {
    char path[512];
    std::snprintf(path, sizeof(path), "%s/seed_%03d_%02x", dir, g_seed++, mode);
    FILE* f = std::fopen(path, "wb");
    if (!f) return;
    std::fputc(mode, f);
    std::fputc(widths, f);
    std::fwrite(data, 1, size, f);
    std::fclose(f);
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) return 1;
    const char* dir = argv[1];
    for (int a = 2; a < argc; ++a) {
        FILE* f = std::fopen(argv[a], "rb");
        if (!f) continue;
        std::vector<uint8_t> file;
        int c;
        while ((c = std::fgetc(f)) != EOF)
            file.push_back(static_cast<uint8_t>(c));
        std::fclose(f);
        if (file.size() < 2 || (file[0] & 4)) continue;   // raw containers only (the dxbc_spirv flag byte)
        const uint8_t* blob = file.data() + 1;
        const size_t size = file.size() - 1;
        Stage stage;
        std::string why;
        if (!StageOfDxbc(blob, size, &stage, &why)) continue;
        for (int combine = 0; combine < 2; ++combine) {
            ConvertOptions options;
            options.combineSamplers = combine != 0;
            std::vector<uint32_t> spirv;
            ShaderReflection refl;
            if (!ConvertDxbc(blob, size, stage, &spirv, &refl, &why, options)) continue;
            const uint8_t set = stage == Stage::Pixel ? 4 : 0;
            Write(dir, static_cast<uint8_t>(0x02 | set), 0xFF, reinterpret_cast<const uint8_t*>(spirv.data()),
                  spirv.size() * 4);
            const std::vector<uint8_t> entry = EncodeEntry(spirv, refl);
            Write(dir, static_cast<uint8_t>(0x01 | set), 0xFF, entry.data(), entry.size());
        }
    }
    std::printf("%d seeds in %s\n", g_seed, dir);
    return 0;
}
#endif
