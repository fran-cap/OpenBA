// src/diag/win/thread_stack.cpp -- see thread_stack.h.
#include "thread_stack.h"

#include <cstdio>
#include <cstring>

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

void FormatFrame(char* line, size_t lineLen, int frame, uint64_t rip, uint64_t rsp) {
    HMODULE mod = nullptr;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(rip), &mod) && mod) {  // CAST-OK: crash_walk
        char path[MAX_PATH] = { 0 };
        GetModuleFileNameA(mod, path, MAX_PATH);
        const char* nm = std::strrchr(path, '\\');
        const uint64_t off = rip - reinterpret_cast<uint64_t>(mod);  // CAST-OK: crash_walk
        std::snprintf(line, lineLen, "  #%-2d %-22s + 0x%-8llX  (RIP=%016llX RSP=%016llX)\n", frame,
                      nm ? nm + 1 : path, (unsigned long long)off, (unsigned long long)rip,
                      (unsigned long long)rsp);
    } else {
        std::snprintf(line, lineLen, "  #%-2d ???                    @ %p          (RSP=%016llX)\n", frame,
                      reinterpret_cast<void*>(rip), (unsigned long long)rsp);  // CAST-OK: crash_walk
    }
}

}  // namespace ThreadStack
