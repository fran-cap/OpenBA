// src/diag/diag_threads.cpp -- see diag_threads.h (NOT RETAIL; block LOADSTAMP).
#include "diag_threads.h"

#include <atomic>
#include <cstring>

#include "platform_thread.h"   // Platform::CurrentThreadId

namespace DiagThreads {

namespace {

struct Slot {
    std::atomic<uint32_t> tid;    // published last; 0 = not yet written
    std::atomic<char>     role;
    std::atomic<uint32_t> busy;   // bit 0 while the owner is mid-job (SetBusy)
};

Slot g_slots[kMaxThreads];           // zero-initialised (constant init)
std::atomic<int> g_count{0};
thread_local bool t_registered = false;
thread_local int  t_slot       = -1;

// The newest slot for `tid`: a reused id has its current owner last.
int FindSlot(uint32_t tid) {
    const int n = g_count.load(std::memory_order_acquire);
    const int lim = n < kMaxThreads ? n : kMaxThreads;
    for (int i = lim - 1; i >= 0; --i)
        if (g_slots[i].tid.load(std::memory_order_acquire) == tid) return i;
    return -1;
}

}  // namespace

namespace Detail {

int RegisterAs(uint32_t tid, char role) {
    // Always a new slot, even for an id the OS reused: the sampler's cursor has
    // passed the old one, and the old owner's pair stays whole.
    const int i = g_count.fetch_add(1, std::memory_order_acq_rel);
    if (i >= kMaxThreads) return -1;
    g_slots[i].role.store(role, std::memory_order_relaxed);
    g_slots[i].busy.store(0, std::memory_order_relaxed);
    g_slots[i].tid.store(tid, std::memory_order_release);
    return i;
}

int CurrentSlot() { return t_slot; }

}  // namespace Detail

void Register(char role) {
    if (t_registered) return;
    t_registered = true;
    t_slot = Detail::RegisterAs(Platform::CurrentThreadId(), role);
}

void SetBusy(bool busy) {
    if (t_slot < 0) return;
    g_slots[t_slot].busy.store(busy ? 1u : 0u, std::memory_order_relaxed);
}

void RegisterNamed(const char* typeName) {
    if (t_registered) return;
    const char* n = typeName ? typeName : "";
    char role = kOther;
    if      (std::strstr(n, "PathfinderThread"))  role = kPathfinder;
    else if (std::strstr(n, "PoolThread"))        role = kPathWorker;
    else if (std::strstr(n, "RenderThread"))      role = kRender;
    else if (std::strstr(n, "PersistenceWorker")) role = kPersist;
    else if (std::strstr(n, "Stream"))            role = kAudio;    // Audio::Stream
    else if (std::strstr(n, "Loader"))            role = kLoader;   // Engine::Loader
    Register(role);
}

char RoleOf(uint32_t tid) {
    const int i = FindSlot(tid);
    return i >= 0 ? g_slots[i].role.load(std::memory_order_acquire) : 0;
}

const char* RoleName(char role) {
    switch (role) {
    case kMain:       return "main";
    case kLoader:     return "loader";
    case kPersist:    return "persist";
    case kAudio:      return "audio";
    case kPathfinder: return "pathfinder";
    case kPathWorker: return "path";
    case kRender:     return "render";
    case kCull:       return "cull";
    case kParallel:   return "parallel";
    case kOther:      return "thread";
    default:          return "?";
    }
}

int Count() {
    const int n = g_count.load(std::memory_order_acquire);
    return n < kMaxThreads ? n : kMaxThreads;
}

uint32_t TidAt(int i) { return g_slots[i].tid.load(std::memory_order_acquire); }
char RoleAt(int i)    { return g_slots[i].role.load(std::memory_order_acquire); }
bool BusyAt(int i)    { return (g_slots[i].busy.load(std::memory_order_relaxed) & 1u) != 0; }

}  // namespace DiagThreads
