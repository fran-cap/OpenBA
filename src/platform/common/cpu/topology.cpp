// ============================================================================
// src/platform/common/cpu/topology.cpp -- the process's CPUs, and the `sys.cpu_limit` boot step
// ============================================================================
// NOT RETAIL. topology.h has the contract; the OS calls are Platform::Cpu*
// (src/platform/common/platform_cpu.h).
// ============================================================================
#include "platform/common/cpu/topology.h"

#include "io/settings_store.h"   // the one-key pre-read of settings.cfg (allocator-free, pre-Startup)
#include "platform_cpu.h"

#include <cstdio>
#include <string>
#include <thread>   // std::thread::hardware_concurrency only (the affinity query failed): no thread is created

namespace Cpu {

namespace {

constexpr const char* kKey = "sys.cpu_limit";

Topology g_topology;
bool g_booted = false;

// settings.cfg's `sys.cpu_limit`, read from the file alone (no legacy registry
// import: the retail key never held it). The last occurrence wins, as the
// engine's parse does.
bool ReadLimitKey(std::string& value) {
    std::string path;
    if (!IO::SettingsStore::DefaultPath(path)) return false;
    IO::SettingsFile::Entries entries;
    if (IO::SettingsStore::Load(path, entries, nullptr) != IO::SettingsStore::Source::File)
        return false;
    bool found = false;
    for (const IO::SettingsFile::Entry& e : entries) {
        if (e.key == kKey) {
            value = e.value;
            found = true;
        }
    }
    return found;
}

}  // namespace

Topology QueryTopology() {
    Topology t;
    uint64_t process = 0;
    uint64_t system = 0;
    if (!Platform::CpuAffinity(&process, &system) || process == 0) {
        const unsigned hc = std::thread::hardware_concurrency();
        t.mask = 0;
        t.logical = hc > 0 ? static_cast<int>(hc) : 1;
        t.physical = t.logical;
        t.topology = false;
        t.reason = "affinity-query";
        return t;
    }
    t.mask = process;
    t.logical = MaskPopcount(process);
    // The big cores (roster.h): the kernel's capacity per CPU of the mask, 0 where unknown.
    int capacity[64] = {0};
    for (int i = 0; i < 64; ++i) {
        if (((process >> i) & 1u) == 0) continue;
        int c = 0;
        if (Platform::CpuCoreCapacity(i, &c)) capacity[i] = c;
    }
    t.bigMask = BigCoreMask(capacity, process);
    t.heterogeneous = t.bigMask != process;
    int cores = 0;
    const char* reason = "";
    if (Platform::CpuPhysicalCores(t.bigMask, &cores, &reason) && cores >= 1 && cores <= t.logical) {
        t.physical = cores;
        t.topology = true;
        t.reason = "";
    } else {
        t.physical = t.logical;
        t.topology = false;
        t.reason = (reason != nullptr && reason[0] != '\0') ? reason : "core-count-out-of-range";
    }
    return t;
}

void Boot() {
    if (g_booted) return;
    g_booted = true;

    std::string value;
    const bool present = ReadLimitKey(value);
    uint64_t process = 0;
    uint64_t system = 0;
    const bool haveMasks = Platform::CpuAffinity(&process, &system);
    LimitDecision d = DecideLimit(present, value, haveMasks ? system : 0);
    if (d.junk) {
        std::fprintf(stderr, "[Cpu] %s=\"%s\" is not a mask (0x55 or 85): ignored, unrestricted\n",
                     kKey, value.c_str());
    } else if (d.empty) {
        std::fprintf(stderr, "[Cpu] %s=0x%llx has no CPU inside the system mask 0x%llx: ignored, "
                     "unrestricted\n", kKey, static_cast<unsigned long long>(d.requested),
                     static_cast<unsigned long long>(system));
    }
    if (d.limit != Limit::None && !Platform::CpuSetProcessAffinity(d.apply)) {
        std::fprintf(stderr, "[Cpu] %s: setting the affinity mask 0x%llx FAILED: unrestricted\n",
                     kKey, static_cast<unsigned long long>(d.apply));
        d.limit = Limit::None;
    }

    g_topology = QueryTopology();
    g_topology.limit = d.limit;
    g_topology.requested = d.requested;

    char line[192];
    Describe(g_topology, line, sizeof(line));
    std::fprintf(stderr, "[Cpu] %s\n", line);
    std::fflush(stderr);
}

const Topology& Get() { return g_topology; }

void Describe(const Topology& t, char* buf, int cap) {
    if (buf == nullptr || cap <= 0) return;
    char source[64];
    if (t.topology) std::snprintf(source, sizeof(source), "topology");
    else            std::snprintf(source, sizeof(source), "fallback:%s", t.reason);
    char requested[40] = "";
    if (t.requested != 0)
        std::snprintf(requested, sizeof(requested), " requested=0x%llx",
                      static_cast<unsigned long long>(t.requested));
    char big[40] = "";
    if (t.heterogeneous)
        std::snprintf(big, sizeof(big), " big=0x%llx", static_cast<unsigned long long>(t.bigMask));
    std::snprintf(buf, static_cast<size_t>(cap), "mask=0x%llx logical=%d physical=%d limit=%s source=%s%s%s",
                  static_cast<unsigned long long>(t.mask), t.logical, t.physical,
                  LimitName(t.limit), source, requested, big);
}

}  // namespace Cpu
