// Sampling profiler — see sampler.h.
#include "sampler.h"
#include <windows.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <vector>

#include "diag_threads.h"   // the registered game threads (block LOADSTAMP)
#include "hw_watch.h"
#include "load_time.h"      // LoadTime::CounterToMs: per-sample ms since process start
#include "options.h"
#include "thread_stack.h"   // ThreadStack::Capture: the lock-free suspended walk

static HANDLE g_mainThread = nullptr;   // from HwWatch_Init (SUSPEND_RESUME + GET_CONTEXT)

volatile int g_samplerTag = static_cast<int>(SamplerTag::OutsideUpdate);

namespace {

constexpr int kMaxFrames = 20;

// g_samplerTag is the main thread's phase only
constexpr uint8_t kTagNotMain = static_cast<uint8_t>(SamplerTag::NotMainThread);

struct Sample {
    uint8_t tag; uint8_t n; char role; uint32_t tid; int64_t qpc;
    uintptr_t rip[kMaxFrames];
};

// One thread the sampler suspends: the main thread (HwWatch's handle) and every
// thread registered in DiagThreads (opened here, on the sampler thread).
struct Tracked {
    uint32_t tid; char role; HANDLE h; bool ownHandle;
    int slot;                        // DiagThreads slot (its busy bit), or -1
    ULONG64 lastCycles;
    bool dead;
};
std::vector<Tracked> g_tracked;      // sampler thread only
int g_seenRegistered = 0;            // DiagThreads entries already looked at

struct ModInfo {
    HMODULE base; uintptr_t lo, hi; char prefix;
    // exports (rva,name), sorted by rva — for nearest-symbol attribution
    std::vector<std::pair<uint32_t, const char*>> exports;
};

CRITICAL_SECTION g_lock;
std::vector<Sample> g_samples;       // pending (guarded by g_lock)
std::vector<Sample> g_flushBuf;
HANDLE g_thread = nullptr;
volatile LONG g_stop = 0;
int g_hz = 1000;
long long g_total = 0, g_failed = 0;
ModInfo g_mods[3];                   // exe, Runtime, VideoDX11
int g_nMods = 0;
FILE* g_out = nullptr;

void LoadExports(ModInfo& m) {
    auto* base = reinterpret_cast<const uint8_t*>(m.base);  // CAST-OK: pe_walk
    auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);  // CAST-OK: pe_walk
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return;
    auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);  // CAST-OK: pe_walk
    if (nt->Signature != IMAGE_NT_SIGNATURE) return;
    m.hi = m.lo + nt->OptionalHeader.SizeOfImage;
    const IMAGE_DATA_DIRECTORY& dd = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (!dd.VirtualAddress || !dd.Size) return;
    auto* ex = reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(base + dd.VirtualAddress);  // CAST-OK: pe_walk
    auto* funcs = reinterpret_cast<const uint32_t*>(base + ex->AddressOfFunctions);  // CAST-OK: pe_walk
    auto* names = reinterpret_cast<const uint32_t*>(base + ex->AddressOfNames);  // CAST-OK: pe_walk
    auto* ords  = reinterpret_cast<const uint16_t*>(base + ex->AddressOfNameOrdinals);  // CAST-OK: pe_walk
    m.exports.reserve(ex->NumberOfNames);
    for (uint32_t i = 0; i < ex->NumberOfNames; ++i) {
        const uint16_t o = ords[i];
        if (o >= ex->NumberOfFunctions) continue;
        const uint32_t rva = funcs[o];
        if (rva >= dd.VirtualAddress && rva < dd.VirtualAddress + dd.Size) continue;  // forwarder
        m.exports.push_back({ rva, reinterpret_cast<const char*>(base + names[i]) });  // CAST-OK: pe_walk
    }
    std::sort(m.exports.begin(), m.exports.end());
}

void AddMod(const char* name, char prefix, bool withExports) {
    HMODULE h = GetModuleHandleA(name);
    if (!h || g_nMods >= 3) return;
    ModInfo& m = g_mods[g_nMods++];
    m.base = h; m.lo = reinterpret_cast<uintptr_t>(h); m.hi = m.lo + 0x10000000; m.prefix = prefix;  // CAST-OK: pe_walk
    if (withExports) LoadExports(m); else {
        auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(h);  // CAST-OK: pe_walk
        auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(reinterpret_cast<const uint8_t*>(h) + dos->e_lfanew);  // CAST-OK: pe_walk
        m.hi = m.lo + nt->OptionalHeader.SizeOfImage;
    }
}

// One frame -> "E:0x140xxxxxx" (exe, rebased to the link-time image base so addr2line
// works), "R:<export>+0xoff" (Runtime nearest export), "V:0xoff", or "?:<module>+off".
void PrintFrame(FILE* o, uintptr_t rip) {
    for (int i = 0; i < g_nMods; ++i) {
        const ModInfo& m = g_mods[i];
        if (rip < m.lo || rip >= m.hi) continue;
        const uintptr_t off = rip - m.lo;
        if (m.prefix == 'E') { std::fprintf(o, " E:0x%llx", 0x140000000ULL + off); return; }
        if (!m.exports.empty()) {
            auto it = std::upper_bound(m.exports.begin(), m.exports.end(),
                                       std::make_pair((uint32_t)off, (const char*)nullptr),
                                       [](const std::pair<uint32_t, const char*>& a,
                                          const std::pair<uint32_t, const char*>& b) { return a.first < b.first; });
            if (it != m.exports.begin()) {
                --it;
                std::fprintf(o, " %c:%s+0x%llx", m.prefix, it->second, (unsigned long long)(off - it->first));
                return;
            }
        }
        std::fprintf(o, " %c:0x%llx", m.prefix, (unsigned long long)off);
        return;
    }
    HMODULE mod = nullptr;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)rip, &mod) && mod) {  // CAST-OK: crash_walk
        char path[MAX_PATH] = {0};
        GetModuleFileNameA(mod, path, MAX_PATH);
        const char* nm = std::strrchr(path, '\\');
        std::fprintf(o, " ?:%s+0x%llx", nm ? nm + 1 : path, (unsigned long long)(rip - (uintptr_t)mod));  // CAST-OK: crash_walk
    } else {
        std::fprintf(o, " ?:0x%llx", (unsigned long long)rip);
    }
}

void WriteSamples(const std::vector<Sample>& v) {
    if (!g_out) g_out = std::fopen("standalone_samples.txt", "w");
    if (!g_out) return;
    for (const Sample& s : v) {
        // "@<role><tid> <ms since process start> <tag> <n> <frames...>" (block LOADSTAMP;
        // tools/sample_report.py still reads the old "<tag> <n> <frames...>" lines).
        std::fprintf(g_out, "@%c%u %.1f %d %d", s.role, (unsigned)s.tid,
                     LoadTime::CounterToMs(s.qpc), (int)s.tag, (int)s.n);
        for (int i = 0; i < s.n; ++i) PrintFrame(g_out, s.rip[i]);
        std::fputc('\n', g_out);
    }
    std::fflush(g_out);
}

// Re-reads the main thread's phase tag while it is stopped (it may have moved since).
void ReReadTag(void* p) {
    static_cast<Sample*>(p)->tag = (uint8_t)g_samplerTag;
}

// No RtlLookupFunctionEntry while the thread is suspended: it waits on the loader's locks,
// which a thread stopped inside LoadLibrary / FreeLibrary holds (the 4000 Hz boot hang).
// ThreadStack::Capture walks with calls that take no lock.
bool TakeSample(Sample& s, const Tracked& t) {
    const bool isMain = (t.role == DiagThreads::kMain);
    s.tag = isMain ? (uint8_t)g_samplerTag : kTagNotMain;
    s.role = t.role;
    s.tid = t.tid;
    s.n = 0;
    LARGE_INTEGER now; QueryPerformanceCounter(&now);
    s.qpc = now.QuadPart;
    CONTEXT ctx; std::memset(&ctx, 0, sizeof(ctx));
    ctx.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
    ThreadStack::Frame frames[kMaxFrames];
    const ThreadStack::Result r = ThreadStack::Capture(t.h, ctx, frames, kMaxFrames,
                                                       isMain ? &ReReadTag : nullptr, &s);
    if (r.status != ThreadStack::Status::kOk) return false;
    for (int f = 0; f < r.frames; ++f) {
        s.rip[s.n++] = frames[f].rip;
    }
    return s.n > 0;
}

// Open the threads registered since the last tick (DiagThreads, block LOADSTAMP).
void RefreshTracked() {
    const int n = DiagThreads::Count();
    while (g_seenRegistered < n) {
        const uint32_t tid = DiagThreads::TidAt(g_seenRegistered);
        if (tid == 0) return;   // still being written: look again next tick
        const char role = DiagThreads::RoleAt(g_seenRegistered);
        const int slot = g_seenRegistered;
        ++g_seenRegistered;
        if (role == DiagThreads::kMain) continue;   // HwWatch's handle is already tracked
        // A new slot for a tid already tracked means the OS reused the id: the old
        // thread has exited (ids are unique among live threads), so its handle is
        // closed and the new owner gets its own (review F-293).
        for (Tracked& t : g_tracked) {
            if (t.tid == tid && !t.dead && t.role != DiagThreads::kMain) {
                t.dead = true;
                if (t.ownHandle) CloseHandle(t.h);
            }
        }
        HANDLE h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION | SYNCHRONIZE,
                              FALSE, tid);
        if (!h) continue;
        g_tracked.push_back(Tracked{ tid, role, h, true, slot, 0, false });
    }
}

// Whether to sample `t` this tick: the main thread always (as before); a registered
// thread while it is mid-job (its busy bit) or when it ran since the previous tick
// (its cycle count moved), so a parked worker writes nothing.
bool WantSample(Tracked& t) {
    if (t.dead) return false;
    if (t.role == DiagThreads::kMain) return true;
    if (WaitForSingleObject(t.h, 0) != WAIT_TIMEOUT) {   // exited: drop it
        t.dead = true;
        if (t.ownHandle) CloseHandle(t.h);
        return false;
    }
    ULONG64 cycles = 0;
    QueryThreadCycleTime(t.h, &cycles);
    const bool ran = cycles != t.lastCycles;
    t.lastCycles = cycles;
    return ran || (t.slot >= 0 && DiagThreads::BusyAt(t.slot));
}

DWORD WINAPI SamplerThread(void*) {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    // HITCH (2026-10-18): a plain waitable timer with a 1 ms period fires at the default
    // 15.6 ms clock when nothing has raised the timer resolution (the frame limiter is off
    // in a stress run), so a "1000 Hz" sampler took ~64 samples/s. Use the high-resolution
    // waitable timer (Windows 10 1803+; frame_limiter.cpp does the same), re-armed one-shot
    // per sample with a 100 ns due time, which also allows rates above 1000 Hz.
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif
    HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (!timer) timer = CreateWaitableTimerA(nullptr, FALSE, nullptr);
    LARGE_INTEGER due; due.QuadPart = -(LONGLONG)(10000000LL / (g_hz > 0 ? g_hz : 1000));   // 100 ns units
    const LONG periodMs = g_hz >= 1000 ? 1 : (1000 / g_hz);
    DWORD lastFlush = GetTickCount();
    Sample s;
    while (!g_stop) {
        if (timer) { SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE); WaitForSingleObject(timer, 50); }
        else Sleep(periodMs);
        if (g_stop) break;
        RefreshTracked();
        for (Tracked& t : g_tracked) {
            if (!WantSample(t)) continue;
            if (TakeSample(s, t)) {
                EnterCriticalSection(&g_lock);
                g_samples.push_back(s);
                LeaveCriticalSection(&g_lock);
                ++g_total;
            } else {
                ++g_failed;
            }
        }
        if (GetTickCount() - lastFlush > 10000) { Sampler_Flush(); lastFlush = GetTickCount(); }
    }
    if (timer) CloseHandle(timer);
    return 0;
}

void AtExitFlush() {
    InterlockedExchange(&g_stop, 1);
    // No timeout: the sampler thread may be inside its own periodic Sampler_Flush, which
    // swaps g_flushBuf and writes g_out outside g_lock. Flushing and closing g_out here
    // while it runs, or letting the statics be destroyed after this handler under a live
    // thread, crashes. The wait is bounded: the loop takes no sample once g_stop is set,
    // its timer wait is at most 50 ms, and a flush in progress always ends.
    if (g_thread) {
        WaitForSingleObject(g_thread, INFINITE);
    }
    Sampler_Flush();
    if (g_out) { std::fclose(g_out); g_out = nullptr; }
    std::fprintf(stderr, "[Sampler] done: %lld samples (%lld failed) -> standalone_samples.txt\n",
                 g_total, g_failed);
}

} // namespace

void Sampler_Flush() {
    EnterCriticalSection(&g_lock);
    g_flushBuf.swap(g_samples);
    LeaveCriticalSection(&g_lock);
    if (!g_flushBuf.empty()) { WriteSamples(g_flushBuf); g_flushBuf.clear(); }
}

void Sampler_Init() {
    const int hz = Opt().sample;
    if (hz == 0) return;
    g_mainThread = HwWatch_MainThreadHandle();
    if (!g_mainThread) { std::fprintf(stderr, "[Sampler] no main-thread handle (HwWatch_Init first)\n"); return; }
    if (hz > 1) g_hz = hz;
    InitializeCriticalSection(&g_lock);
    g_samples.reserve(1 << 16);
    g_tracked.reserve(64);
    g_tracked.push_back(Tracked{ GetThreadId(g_mainThread), DiagThreads::kMain, g_mainThread,
                                 false, -1, 0, false });
    AddMod(nullptr, 'E', false);   // engine DLLs are added at the first flush (loaded later)
    std::fprintf(stderr, "[Sampler] on: %d Hz\n", g_hz);
    g_thread = CreateThread(nullptr, 0, SamplerThread, nullptr, 0, nullptr);
    std::atexit(AtExitFlush);
}
