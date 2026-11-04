// src/diag/win/thread_stack.cpp -- see thread_stack.h.
#include "thread_stack.h"

#include <psapi.h>      // K32GetMappedFileNameW (exported by kernel32)
#include <tlhelp32.h>

#include <cstdio>

namespace ThreadStack {

namespace {

// The image mapped at `address` (its base), from the address space itself: VirtualQuery is
// a system call and takes no loader lock. Not an image (freed, heap, JIT code): false.
bool ImageAt(uint64_t address, uint64_t* base) {
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery(reinterpret_cast<const void*>(address), &mbi, sizeof(mbi)) != sizeof(mbi)) {  // CAST-OK: crash_walk
        return false;
    }
    if (mbi.State != MEM_COMMIT || mbi.Type != MEM_IMAGE || !mbi.AllocationBase) return false;
    *base = reinterpret_cast<uint64_t>(mbi.AllocationBase);  // CAST-OK: crash_walk
    return true;
}

// The RUNTIME_FUNCTION covering `rip` in the image at `base`: a binary search of the
// image's own exception directory, which RtlLookupFunctionEntry does under the loader's
// function-table lock. False: no entry (a leaf function) or no exception directory.
bool FindFunction(uint64_t base, uint64_t rip, RUNTIME_FUNCTION* out) {
    const uint8_t* image = reinterpret_cast<const uint8_t*>(base);  // CAST-OK: pe_walk
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image);  // CAST-OK: pe_walk
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 0x1000) {
        return false;
    }
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(image + dos->e_lfanew);  // CAST-OK: pe_walk
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) return false;
    if (nt->OptionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_EXCEPTION) return false;
    const uint64_t imageSize = nt->OptionalHeader.SizeOfImage;
    const IMAGE_DATA_DIRECTORY& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
    if (dir.VirtualAddress == 0 || (uint64_t)dir.VirtualAddress + dir.Size > imageSize) return false;
    const uint64_t rva = rip - base;
    if (rva >= imageSize) return false;
    const auto* table = reinterpret_cast<const RUNTIME_FUNCTION*>(image + dir.VirtualAddress);  // CAST-OK: pe_walk
    size_t lo = 0;
    size_t hi = dir.Size / sizeof(RUNTIME_FUNCTION);
    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;
        if (rva < table[mid].BeginAddress) {
            hi = mid;
        } else if (rva >= table[mid].EndAddress) {
            lo = mid + 1;
        } else {
            RUNTIME_FUNCTION fn = table[mid];
            // An indirect entry (low bit of UnwindData) names the primary RUNTIME_FUNCTION.
            if (fn.UnwindData & 1u) {
                const uint64_t at = fn.UnwindData & ~1u;
                if (at + sizeof(RUNTIME_FUNCTION) > imageSize) return false;
                fn = *reinterpret_cast<const RUNTIME_FUNCTION*>(image + at);  // CAST-OK: pe_walk
            }
            if (fn.UnwindData == 0 || fn.UnwindData >= imageSize) return false;
            *out = fn;
            return true;
        }
    }
    return false;
}

}  // namespace

Result Capture(HANDLE th, CONTEXT& ctx, Frame* frames, int maxFrames,
               void (*whileSuspended)(void*), void* user) {
    Result r{ Status::kOk, 0, 0 };
    if (SuspendThread(th) == (DWORD)-1) {
        r.status = Status::kSuspendFailed;
        r.error = GetLastError();
        return r;
    }
    if (!GetThreadContext(th, &ctx)) {
        r.status = Status::kContextFailed;
        r.error = GetLastError();
        ResumeThread(th);
        return r;
    }
    if (whileSuspended) whileSuspended(user);

    // The committed stack above the stopped RSP: every frame's RSP must stay inside it.
    uint64_t stackTop = 0;
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery(reinterpret_cast<const void*>(ctx.Rsp), &mbi, sizeof(mbi)) == sizeof(mbi) &&  // CAST-OK: crash_walk
        mbi.State == MEM_COMMIT) {
        stackTop = reinterpret_cast<uint64_t>(mbi.BaseAddress) + mbi.RegionSize;  // CAST-OK: crash_walk
    }
    CONTEXT u = ctx;
    while (r.frames < maxFrames && u.Rip) {
        frames[r.frames++] = Frame{ u.Rip, u.Rsp };
        if (u.Rsp < ctx.Rsp || u.Rsp + 8 > stackTop) break;
        uint64_t base = 0;
        if (!ImageAt(u.Rip, &base)) break;   // unmapped or not an image: nothing to unwind with
        const uint64_t prevRsp = u.Rsp;
        RUNTIME_FUNCTION fn;
        if (!FindFunction(base, u.Rip, &fn)) {
            // Leaf (no unwind data): the return address is at [RSP].
            u.Rip = *reinterpret_cast<const uint64_t*>(u.Rsp);  // CAST-OK: crash_walk
            u.Rsp += 8;
            continue;
        }
        PVOID handlerData = nullptr;
        ULONG64 establisherFrame = 0;
        RtlVirtualUnwind(UNW_FLAG_NHANDLER, base, u.Rip, &fn, &u, &handlerData, &establisherFrame, nullptr);
        if (u.Rsp <= prevRsp) break;   // no progress: a corrupt or foreign frame
    }
    ResumeThread(th);
    return r;
}

bool ModuleAt(uint64_t address, uint64_t* base, char* name, size_t nameLen) {
    if (!ImageAt(address, base)) return false;
    if (nameLen == 0) return true;
    name[0] = '\0';
    // The file the image is mapped from ("\Device\HarddiskVolumeN\...\x.dll"): a system call
    // (NtQueryVirtualMemory) on the mapping, not a walk of the loader's module list.
    wchar_t path[MAX_PATH];
    const DWORD n = K32GetMappedFileNameW(GetCurrentProcess(), reinterpret_cast<void*>(*base), path,  // CAST-OK: crash_walk
                                          MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        std::snprintf(name, nameLen, "?");
        return true;
    }
    DWORD start = 0;
    for (DWORD i = 0; i < n; ++i) {
        if (path[i] == L'\\') start = i + 1;
    }
    // The base name, narrowed by hand (no code-page conversion): a non-ASCII character is '?'.
    size_t out = 0;
    for (DWORD i = start; i < n && out + 1 < nameLen; ++i) {
        name[out++] = path[i] < 0x80 ? static_cast<char>(path[i]) : '?';
    }
    name[out] = '\0';
    return true;
}

void FormatFrame(char* line, size_t lineLen, int frame, uint64_t rip, uint64_t rsp) {
    uint64_t base = 0;
    char name[MAX_PATH];
    if (ModuleAt(rip, &base, name, sizeof(name))) {
        std::snprintf(line, lineLen, "  #%-2d %-22s + 0x%-8llX  (RIP=%016llX RSP=%016llX)\n", frame, name,
                      (unsigned long long)(rip - base), (unsigned long long)rip, (unsigned long long)rsp);
    } else {
        std::snprintf(line, lineLen, "  #%-2d ???                    @ %p          (RSP=%016llX)\n", frame,
                      reinterpret_cast<void*>(rip), (unsigned long long)rsp);  // CAST-OK: crash_walk
    }
}

void DumpOtherThreads(DWORD mainTid, LineSink sink, void* user) {
    const DWORD selfTid = GetCurrentThreadId();
    const DWORD selfPid = GetCurrentProcessId();
    constexpr int kFrames = 32;
    Frame frames[kFrames];
    char line[256];

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        sink(user, "(CreateToolhelp32Snapshot failed -- no thread list)\n");
        return;
    }
    THREADENTRY32 te{};
    te.dwSize = sizeof(te);
    if (Thread32First(snap, &te)) {
        do {
            if (te.dwSize < FIELD_OFFSET(THREADENTRY32, th32ThreadID) + sizeof(DWORD)) continue;
            if (te.th32OwnerProcessID != selfPid) continue;

            const bool isMain = (te.th32ThreadID == mainTid);
            const bool isSelf = (te.th32ThreadID == selfTid);
            std::snprintf(line, sizeof(line), "\n--- thread %u%s%s ---\n", static_cast<uint32_t>(te.th32ThreadID),
                          isMain ? "  [MAIN THREAD]" : "", isSelf ? "  [watchdog -- this thread]" : "");
            sink(user, line);
            if (isSelf) {
                sink(user, "  (the watchdog itself; not interesting)\n");
                continue;
            }

            HANDLE th = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE,
                                   te.th32ThreadID);
            if (!th) {
                std::snprintf(line, sizeof(line), "  (OpenThread failed, GetLastError=%lu)\n", GetLastError());
                sink(user, line);
                continue;
            }
            CONTEXT ctx{};
            ctx.ContextFlags = CONTEXT_FULL;
            const Result r = Capture(th, ctx, frames, kFrames);
            if (r.status == Status::kSuspendFailed) {
                std::snprintf(line, sizeof(line), "  (SuspendThread failed, GetLastError=%lu)\n", r.error);
                sink(user, line);
            } else if (r.status == Status::kContextFailed) {
                std::snprintf(line, sizeof(line), "  (GetThreadContext failed, GetLastError=%lu)\n", r.error);
                sink(user, line);
            } else {
                std::snprintf(line, sizeof(line), "  RIP=%016llX RSP=%016llX RBP=%016llX\n",
                              (unsigned long long)ctx.Rip, (unsigned long long)ctx.Rsp, (unsigned long long)ctx.Rbp);
                sink(user, line);
                for (int i = 0; i < r.frames; ++i) {
                    FormatFrame(line, sizeof(line), i, frames[i].rip, frames[i].rsp);
                    sink(user, line);
                }
            }
            CloseHandle(th);
        } while (Thread32Next(snap, &te));
    }
    CloseHandle(snap);
}

}  // namespace ThreadStack
