// ============================================================================
// tools/vulkan/shader_check.cpp -- vulkan-shader-check: convert and validate every retail program
// ============================================================================
// NOT RETAIL. The metal-shader-check twin (tools/metal/shader_check.mm) for the Vulkan backend,
// docs/vulkan/PLAN.md §4.2 / §5 P1 ("every retail shader converts and passes spirv-val").
//
//   vulkan-shader-check <game dir> [--spirv-val <exe>] [--vulkan1.1]
//   (cmake --build build/cmake-debug-vulkan -t vulkan-shader-check)
//
// Reads the user's own install, never a copy: every <game dir>/WinData/*.pkg (the top level only,
// so parked mods in WinData/_mods_off stay out), every entry of each package's directory, the
// entry's CompressedStream windows (FastLZ level 1, 32 KiB; the reader of
// tools/audio_tail/pkg_sounds.py), then every DXBC container inside the entry. A program resource
// (Material\...\X_vs.crs, X_ps.crs, ...) carries one container per permutation, in permutation
// order, so an entry is a program and its containers are its permutations. Each one goes through
// the backend's own converter (src/render/vulkan/shader_convert.cpp, the same call the program
// companions make), its cache payload is encoded and decoded back (the entry round trip), and the
// module is validated with `spirv-val --target-env vulkan1.3` when one is found (--spirv-val, else
// `spirv-val` on PATH), else with the converter's structural check. Nothing is written but one
// temporary .spv file in the system temp folder per validation, deleted at once. With --vulkan1.1 each
// module is first rewritten to SPIR-V 1.3 as the backend's 1.1 path does (spirv_legalize.h
// DowngradeSpirv; src/render/vulkan/vulkan11.h) and validated with `--target-env vulkan1.1`.
//
// Prints a summary table and the first error of every failure; exits 1 on any failure, 2 when the
// game directory holds no package.
// ============================================================================
#include "shader_convert.h"
#include "spirv_legalize.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

#if !defined(_WIN32)
#include <sys/wait.h>   // WIFEXITED / WEXITSTATUS (pclose)
#endif

namespace fs = std::filesystem;
using namespace VideoVulkan::Shaders;

namespace {

uint32_t ReadU32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

uint16_t ReadU16(const uint8_t* p) {
    uint16_t v;
    std::memcpy(&v, p, 2);
    return v;
}

// FastLZ level 1 (the package windows): a literal run of (c + 1) bytes for c < 0x20, else a match of
// (c >> 5) + 2 bytes (7 = extended by the next byte) at distance ((c & 0x1F) << 8) + next + 1.
bool FastLz1(const uint8_t* src, size_t n, std::vector<uint8_t>* out, size_t cap) {
    out->clear();
    size_t sp = 0;
    while (sp < n && out->size() < cap) {
        const uint8_t c = src[sp++];
        if (c < 0x20) {
            const size_t count = static_cast<size_t>(c) + 1;
            if (sp + count > n) return false;
            out->insert(out->end(), src + sp, src + sp + count);
            sp += count;
            continue;
        }
        size_t length = c >> 5;
        size_t distance = (static_cast<size_t>(c & 0x1F) << 8) + 1;
        if (length == 7) {
            if (sp >= n) return false;
            length += src[sp++];
        }
        if (sp >= n) return false;
        distance += src[sp++];
        if (distance > out->size()) return false;
        const size_t start = out->size() - distance;
        for (size_t i = 0; i < length + 2; ++i)
            out->push_back((*out)[start + i]);
    }
    return true;
}

// A CompressedStream blob: int32 window count, then per window {uint32 offset, uint16 compressed
// size (0 = stored), uint16 size}, offsets relative to the blob. False when the bytes do not look
// like one (the caller then scans them raw).
bool Decompress(const uint8_t* blob, size_t size, std::vector<uint8_t>* out) {
    if (size < 4) return false;
    const uint32_t count = ReadU32(blob);
    if (count == 0 || count > 1000000 || 4 + 8ull * count > size) return false;
    out->clear();
    std::vector<uint8_t> window;
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t* h = blob + 4 + 8 * i;
        const uint32_t offset = ReadU32(h);
        const uint16_t packed = ReadU16(h + 4);
        const uint16_t plain = ReadU16(h + 6);
        if (packed != 0) {
            if (offset + static_cast<size_t>(packed) > size) return false;
            if (!FastLz1(blob + offset, packed, &window, 0x8000)) return false;
            if (window.size() < plain) return false;
            out->insert(out->end(), window.begin(), window.begin() + plain);
        } else {
            if (offset + static_cast<size_t>(plain) > size) return false;
            out->insert(out->end(), blob + offset, blob + offset + plain);
        }
    }
    return true;
}

struct Entry {
    std::string name;
    uint32_t offset = 0;
    uint32_t size = 0;
};

struct Reader {
    const std::vector<uint8_t>& d;
    size_t p = 0;
    bool ok = true;
    int32_t I32() {
        if (!ok || d.size() - p < 4) {
            ok = false;
            return 0;
        }
        int32_t v;
        std::memcpy(&v, d.data() + p, 4);
        p += 4;
        return v;
    }
    void Skip(size_t n) {
        if (!ok || d.size() - p < n) {
            ok = false;
            return;
        }
        p += n;
    }
    std::string Bytes(size_t n) {
        if (!ok || d.size() - p < n) {
            ok = false;
            return std::string();
        }
        std::string s(reinterpret_cast<const char*>(d.data() + p), n);
        p += n;
        return s;
    }
};

// The package directory (tools/audio_tail/pkg_sounds.py read_pkg): the header word's top bit says a
// mod header precedes the file list (format version, versions, priority, platform, overridable, crc,
// icon, an optional blob, three strings), then per file {name, offset, size}; names are UTF-16 up to
// format 2 and UTF-8 from 3.
bool ReadPackage(const std::vector<uint8_t>& data, std::vector<Entry>* entries, std::string* why) {
    if (data.size() < 8) {
        *why = "too short";
        return false;
    }
    const uint32_t header = ReadU32(data.data());
    const bool modHeader = (header & 0x80000000u) != 0;
    const uint32_t dirOffset = header & 0x7fffffffu;
    if (dirOffset + 4ull > data.size()) {
        *why = "directory offset past the end";
        return false;
    }
    const uint32_t dirSize = ReadU32(data.data() + dirOffset);
    if (dirOffset + 4ull + dirSize > data.size()) {
        *why = "directory past the end";
        return false;
    }
    std::vector<uint8_t> dir;
    if (!Decompress(data.data() + dirOffset + 4, dirSize, &dir)) {
        *why = "directory does not decompress";
        return false;
    }
    Reader r{dir};
    const int32_t files = r.I32();
    int32_t format = 0;
    if (modHeader) {
        format = r.I32();
        r.Skip(16);   // version, user version, priority, platform
        r.Skip(1);    // overridable
        r.Skip(4);    // crc
        const int32_t w = r.I32();
        const int32_t h = r.I32();
        if (w < 0 || h < 0 || w > 4096 || h > 4096) {
            *why = "bad icon size";
            return false;
        }
        r.Skip(4ull * w * h);
        if (format >= 2) r.Skip(static_cast<size_t>(std::max(0, r.I32())));
        for (int i = 0; i < 3; ++i) {
            const int32_t length = r.I32();
            r.Skip(static_cast<size_t>(std::max(0, length)) * (format <= 2 ? 2 : 1));
        }
    }
    if (!r.ok || files < 0) {
        *why = "bad header";
        return false;
    }
    for (int32_t i = 0; i < files && r.ok; ++i) {
        const int32_t length = r.I32();
        if (length < 0 || length > 4096) {
            *why = "bad name length";
            return false;
        }
        Entry e;
        if (format > 2) {
            e.name = r.Bytes(static_cast<size_t>(length));
        } else {
            const std::string wide = r.Bytes(2 * static_cast<size_t>(length));
            for (size_t k = 0; k + 1 < wide.size(); k += 2)
                e.name.push_back(wide[k + 1] == 0 ? wide[k] : '?');
        }
        while (!e.name.empty() && e.name.back() == '\0')
            e.name.pop_back();
        e.offset = static_cast<uint32_t>(r.I32());
        e.size = static_cast<uint32_t>(r.I32());
        entries->push_back(e);
    }
    if (!r.ok) {
        *why = "directory truncated";
        return false;
    }
    return true;
}

// Every well-formed DXBC container in `bytes`, in order: the magic, version 1 at +20, a total size
// at +24 that fits, a chunk count and chunk table that fit.
std::vector<std::pair<size_t, size_t>> FindContainers(const std::vector<uint8_t>& bytes) {
    std::vector<std::pair<size_t, size_t>> found;
    size_t i = 0;
    while (i + 32 <= bytes.size()) {
        if (bytes[i] != 'D' || bytes[i + 1] != 'X' || bytes[i + 2] != 'B' || bytes[i + 3] != 'C') {
            ++i;
            continue;
        }
        const uint32_t version = ReadU32(bytes.data() + i + 20);
        const uint32_t total = ReadU32(bytes.data() + i + 24);
        const uint32_t chunks = ReadU32(bytes.data() + i + 28);
        if (version == 1 && total >= 32 && i + total <= bytes.size() && chunks > 0 && chunks < 64 &&
            32 + 4 * chunks <= total) {
            found.emplace_back(i, total);
            i += total;
        } else {
            ++i;
        }
    }
    return found;
}

std::string Quote(const std::string& s) {
    return "\"" + s + "\"";
}

// Runs `command`, returns its exit status and its output (stdout and stderr).
int Run(const std::string& command, std::string* output) {
#if defined(_WIN32)
    // cmd.exe strips the outer quotes of a command line that starts with one.
    const std::string line = "\"" + command + " 2>&1\"";
#else
    const std::string line = command + " 2>&1";
#endif
    FILE* pipe = popen(line.c_str(), "r");
    if (pipe == nullptr) return -1;
    char buffer[512];
    output->clear();
    while (std::fgets(buffer, sizeof(buffer), pipe) != nullptr)
        *output += buffer;
    const int status = pclose(pipe);
#if defined(_WIN32)
    return status;
#else
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
}

std::string FirstLine(const std::string& text) {
    std::string line = text.substr(0, text.find('\n'));
    while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
        line.pop_back();
    return line;
}

struct Totals {
    unsigned programs = 0;
    unsigned permutations = 0;
    unsigned converted = 0;
    unsigned validated = 0;
    unsigned failed = 0;
};

int HighestBit(uint32_t mask) {
    int high = -1;
    for (int b = 0; b < 32; ++b) {
        if (mask & (1u << b)) high = b;
    }
    return high;
}

}  // namespace

int main(int argc, char** argv) {
    std::string gameDir;
    std::string validator;
    bool vulkan11 = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--spirv-val" && i + 1 < argc) {
            validator = argv[++i];
        } else if (arg == "--vulkan1.1") {
            vulkan11 = true;
        } else if (gameDir.empty() && arg.rfind("--", 0) != 0) {
            gameDir = arg;
        } else {
            std::fprintf(stderr, "usage: vulkan-shader-check <game dir> [--spirv-val <exe>] [--vulkan1.1]\n");
            return 2;
        }
    }
    if (gameDir.empty()) {
        std::fprintf(stderr, "usage: vulkan-shader-check <game dir> [--spirv-val <exe>] [--vulkan1.1]\n");
        return 2;
    }

    // The validator: the one named, else spirv-val on PATH, else the structural check.
    const std::string targetEnv = vulkan11 ? " --target-env vulkan1.1 " : " --target-env vulkan1.3 ";
    std::string probe;
    if (validator.empty()) validator = "spirv-val";
    const bool haveValidator = Run(Quote(validator) + " --version", &probe) == 0;
    std::printf("vulkan-shader-check: dxbc-spirv %s, mapping v%u, game dir %s\n", CompilerCommit(),
                kMappingVersion, gameDir.c_str());
    if (haveValidator) {
        std::printf("validator: %s%s(%s)%s\n", validator.c_str(), targetEnv.c_str(), FirstLine(probe).c_str(),
                    vulkan11 ? ", modules downgraded to SPIR-V 1.3 first" : "");
    } else {
        std::printf("validator: no spirv-val found (PATH or --spirv-val): structural check only\n");
    }

    std::error_code ec;
    const fs::path temp = fs::temp_directory_path(ec) / "vulkan_shader_check.spv";
    // The validator's self-check: a module with a header and nothing else (no capability, no memory
    // model) must be refused, or a validator that answers 0 for everything would pass the run.
    if (haveValidator) {
        const uint32_t empty[5] = {0x07230203u, 0x00010600u, 0, 1, 0};
        {
            std::ofstream out(temp, std::ios::binary | std::ios::trunc);
            out.write(reinterpret_cast<const char*>(empty), sizeof(empty));
        }
        std::string output;
        if (Run(Quote(validator) + targetEnv + Quote(temp.string()), &output) == 0) {
            std::fprintf(stderr, "validator self-check failed: %s accepted an empty module\n", validator.c_str());
            fs::remove(temp, ec);
            return 2;
        }
        std::printf("validator self-check: an empty module is refused (%s)\n", FirstLine(output).c_str());
    }

    std::vector<fs::path> packages;
    for (const auto& item : fs::directory_iterator(fs::path(gameDir) / "WinData", ec)) {
        if (item.is_regular_file() && item.path().extension() == ".pkg") packages.push_back(item.path());
    }
    std::sort(packages.begin(), packages.end());
    if (packages.empty()) {
        std::fprintf(stderr, "no WinData/*.pkg under %s\n", gameDir.c_str());
        return 2;
    }

    Totals stage[2];
    std::vector<std::string> failures;
    uint32_t cbMask = 0;
    uint32_t texMask = 0;
    uint32_t smpMask = 0;
    // P8: the (texture, sampler) pairs the programs sample with, after the combined-sampler pass.
    std::map<std::string, unsigned> pairCensus;
    unsigned permutationsWithExtraPairs = 0;
    unsigned unsampledTextures = 0;
    unsigned entriesSeen = 0;
    unsigned roundTrips = 0;

    for (const fs::path& package : packages) {
        std::ifstream in(package, std::ios::binary);
        std::vector<uint8_t> data{std::istreambuf_iterator<char>(in), {}};
        std::vector<Entry> entries;
        std::string why;
        if (!ReadPackage(data, &entries, &why)) {
            std::printf("FAIL package %s: %s\n", package.filename().string().c_str(), why.c_str());
            stage[0].failed++;
            continue;
        }
        entriesSeen += static_cast<unsigned>(entries.size());
        std::vector<uint8_t> plain;
        for (const Entry& e : entries) {
            if (static_cast<size_t>(e.offset) + e.size > data.size()) continue;
            const uint8_t* bytes = data.data() + e.offset;
            if (!Decompress(bytes, e.size, &plain)) plain.assign(bytes, bytes + e.size);
            const auto containers = FindContainers(plain);
            if (containers.empty()) continue;
            int permutation = 0;
            bool counted[2] = {false, false};
            for (const auto& c : containers) {
                const uint8_t* blob = plain.data() + c.first;
                const std::string where = e.name + " #" + std::to_string(permutation++);
                Stage s = Stage::Vertex;
                if (!StageOfDxbc(blob, c.second, &s, &why)) {
                    failures.push_back(where + ": " + why);
                    stage[0].failed++;
                    continue;
                }
                Totals& t = stage[static_cast<int>(s)];
                if (!counted[static_cast<int>(s)]) {
                    counted[static_cast<int>(s)] = true;
                    t.programs++;
                }
                t.permutations++;
                std::vector<uint32_t> spirv;
                ShaderReflection refl;
                ConvertOptions options;
                options.combineSamplers = true;   // P8: the backend's game programs are combined
                if (!ConvertDxbc(blob, c.second, s, &spirv, &refl, &why, options)) {
                    failures.push_back(where + " (" + StageName(s) + "): " + why);
                    t.failed++;
                    continue;
                }
                t.converted++;
                cbMask |= refl.constantBufferMask;
                texMask |= refl.textureMask;
                smpMask |= refl.samplerMask;
                for (uint32_t r = 0; r < kMaxTextures; ++r) {
                    if (!((refl.textureMask >> r) & 1)) continue;
                    if (refl.textureSampler[r] == kNoSampler) {
                        unsampledTextures++;
                        continue;
                    }
                    pairCensus["t" + std::to_string(r) + "+s" + std::to_string(refl.textureSampler[r])]++;
                }
                for (const CombinedPair& p : refl.extraPairs)
                    pairCensus["t" + std::to_string(p.texture) + "+s" + std::to_string(p.sampler) + " (further)"]++;
                if (!refl.extraPairs.empty()) permutationsWithExtraPairs++;

                std::vector<uint32_t> back;
                ShaderReflection backRefl;
                if (DecodeEntry(EncodeEntry(spirv, refl), &back, &backRefl) && back == spirv &&
                    backRefl.textureMask == refl.textureMask && backRefl.textureSampler == refl.textureSampler &&
                    backRefl.extraPairs.size() == refl.extraPairs.size() && backRefl.inputs.size() == refl.inputs.size() &&
                    backRefl.outputs.size() == refl.outputs.size()) {
                    roundTrips++;
                } else {
                    failures.push_back(where + ": the cache entry does not round-trip");
                    t.failed++;
                    continue;
                }

                if (vulkan11 && !DowngradeSpirv(&spirv, &why)) {
                    failures.push_back(where + " (" + StageName(s) + "): " + why);
                    t.failed++;
                    continue;
                }
                if (haveValidator) {
                    {
                        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
                        out.write(reinterpret_cast<const char*>(spirv.data()),
                                  static_cast<std::streamsize>(spirv.size() * 4));
                    }
                    std::string output;
                    const int status =
                        Run(Quote(validator) + targetEnv + Quote(temp.string()), &output);
                    if (status != 0) {
                        failures.push_back(where + " (" + StageName(s) + "): spirv-val: " + FirstLine(output));
                        t.failed++;
                        continue;
                    }
                } else if (!CheckSpirvStructure(spirv, &why)) {
                    failures.push_back(where + ": " + why);
                    t.failed++;
                    continue;
                }
                t.validated++;
            }
        }
    }
    fs::remove(temp, ec);

    std::printf("packages: %zu, entries: %u\n", packages.size(), entriesSeen);
    std::printf("%-8s %9s %13s %10s %10s %7s\n", "stage", "programs", "permutations", "converted", "validated",
                "failed");
    Totals sum;
    for (int i = 0; i < 2; ++i) {
        const Totals& t = stage[i];
        std::printf("%-8s %9u %13u %10u %10u %7u\n", StageName(static_cast<Stage>(i)), t.programs, t.permutations,
                    t.converted, t.validated, t.failed);
        sum.programs += t.programs;
        sum.permutations += t.permutations;
        sum.converted += t.converted;
        sum.validated += t.validated;
        sum.failed += t.failed;
    }
    std::printf("%-8s %9u %13u %10u %10u %7u\n", "total", sum.programs, sum.permutations, sum.converted,
                sum.validated, sum.failed);
    std::printf("highest register used: b%d t%d s%d (layout limits b%u t%u s%u)\n", HighestBit(cbMask),
                HighestBit(texMask), HighestBit(smpMask), kMaxConstantBuffers - 1, kMaxTextures - 1,
                kMaxSamplers - 1);
    std::printf("cache entry round trip: %u / %u\n", roundTrips, sum.converted);
    std::printf("combined (texture+sampler) pairs over the permutations:");
    for (const auto& p : pairCensus)
        std::printf(" %s x%u", p.first.c_str(), p.second);
    std::printf("\ntextures read without a sampler: %u; permutations with a further pair: %u\n", unsampledTextures,
                permutationsWithExtraPairs);
    for (const std::string& f : failures)
        std::printf("FAIL %s\n", f.c_str());
    return sum.failed != 0 ? 1 : 0;
}
