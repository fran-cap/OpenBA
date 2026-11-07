// ============================================================================
// src/ext/large_packages/large_packages.cpp -- fix.large_packages: the glue
// ============================================================================
// NOT RETAIL. large_packages.h has the contract; docs/extensions/handins/LARGEPKM.md
// the format facts and the proof.
// ============================================================================
#include "ext/large_packages/large_packages.h"
#include "ext/large_packages/large_packages_unwrap.h"
#include "ext/registry.h"
#include "io/FileSystem.h"
#include "io/PackageFile.h"
#include "platform_file.h"

#include <cstdio>
#include <vector>

namespace Ext {
namespace LargePackages {

namespace {

// The tables the seam points at, rebuilt at every mount. `g_offsets[pi]` is
// empty for a package that keeps retail's read.
std::vector<std::vector<int64_t>> g_offsets;
std::vector<IO::FileSystem::ExtWidePackage> g_packages;

const char* NameOf(const IO::PackageFile* pkg) {
    const char* n = pkg->_name._data;
    return (n != nullptr && n[0] != '\0') ? n : "(unnamed)";
}

// The index blob's extent, from the package's own header word: [begin, end).
// The OS file pointer moves; every packed read seeks first.
bool ReadIndexExtent(Platform::FileHandle h, int64_t& begin, int64_t& end) {
    uint32_t hdr = 0;
    uint32_t got = 0;
    if (Platform::FileSeek64(h, 0) == 0) return false;
    if (Platform::FileRead(h, &hdr, 4, &got) == 0 || got != 4) return false;
    // ReadPackfile 0x1800b47ca: BTR 31
    begin = static_cast<int64_t>(hdr & IO::PackageFile::kHeaderOffsetMask);
    int32_t blobLen = 0;
    if (Platform::FileSeek64(h, begin) == 0) return false;
    if (Platform::FileRead(h, &blobLen, 4, &got) == 0 || got != 4) return false;
    if (blobLen < 0) return false;
    end = begin + 4 + blobLen;
    return true;
}

}  // namespace

void OnMounted(IO::FileSystem& fs) {
    const bool on = Fix().large_packages;
    const int32_t count = fs._packfiles._size;

    g_offsets.assign(static_cast<size_t>(count > 0 ? count : 0), std::vector<int64_t>());
    g_packages.assign(g_offsets.size(), IO::FileSystem::ExtWidePackage{ nullptr, 0 });
    bool any = false;

    for (int32_t pi = 0; pi < count; ++pi) {
        const IO::PackageFile* pkg = fs._packfiles[pi];
        // A loose package with an OS handle (a package inside a package is read
        // through its parent and keeps retail's read).
        if (RtIO::FileInfo::IsPackedHandle(pkg->_file.handle)) continue;
        const Platform::FileHandle h = pkg->_file.winHandle;
        if (h == nullptr || h == Platform::kInvalidFileHandle) continue;
        const int64_t size = Platform::FileSize64(h);
        if (size <= kRetailLimit) continue;

        const double gib = static_cast<double>(size) / (1024.0 * 1024.0 * 1024.0);
        if (!on) {
            std::fprintf(stderr,
                         "[LargePackages] %s is %.2f GiB: retail reads packages only up to 2 GiB and "
                         "fix.large_packages is off, so its entries past 2 GiB read wrong bytes\n",
                         NameOf(pkg), gib);
            continue;
        }

        int64_t indexBegin = 0;
        int64_t indexEnd = 0;
        if (!ReadIndexExtent(h, indexBegin, indexEnd)) {
            std::fprintf(stderr, "[LargePackages] %s (%.2f GiB): header unreadable, retail's 32-bit reads kept\n",
                         NameOf(pkg), gib);
            continue;
        }

        const int32_t n = pkg->_entries._size;
        std::vector<uint32_t> offs(static_cast<size_t>(n > 0 ? n : 0));
        std::vector<uint32_t> sizes(offs.size());
        for (int32_t i = 0; i < n; ++i) {
            offs[static_cast<size_t>(i)] = pkg->_entries._data[i].offset;
            sizes[static_cast<size_t>(i)] = pkg->_entries._data[i].size;
        }

        std::vector<int64_t>& wide = g_offsets[static_cast<size_t>(pi)];
        const Result r = Unwrap(offs.data(), sizes.data(), n, size, indexBegin, indexEnd, wide);
        if (r.verdict != Verdict::Ok) {
            std::fprintf(stderr,
                         "[LargePackages] %s (%.2f GiB): index does not unwrap (%s, entry %d), "
                         "retail's 32-bit reads kept\n",
                         NameOf(pkg), gib, VerdictText(r.verdict), r.badEntry);
            continue;
        }
        g_packages[static_cast<size_t>(pi)] = IO::FileSystem::ExtWidePackage{ wide.data(), n };
        any = true;
        std::fprintf(stderr,
                     "[LargePackages] %s (%.2f GiB, pack %d): %d entries read with 64-bit offsets "
                     "(%d wraps, %lld hole bytes)\n",
                     NameOf(pkg), gib, pi, n, r.wraps, static_cast<long long>(r.holes));
    }

    IO::FileSystem::_extWideCount = any ? count : 0;
    IO::FileSystem::_extWidePackages = any ? g_packages.data() : nullptr;
}

void Sync() {
    // Installed once at boot: the profile is fixed for the run. Under the
    // faithful profile the seam stays null (no hook, no log line); otherwise
    // the hook reads the fix's live bool at each mount.
    IO::FileSystem::_extPackagesMounted = CurrentProfile() == Profile::Faithful ? nullptr : &OnMounted;
}

}  // namespace LargePackages
}  // namespace Ext
