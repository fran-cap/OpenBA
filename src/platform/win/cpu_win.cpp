// ============================================================================
// src/platform/win/cpu_win.cpp -- Platform:: CPU affinity / topology over Win32
// ============================================================================
// The Windows half of src/platform/common/platform_cpu.h (block CPULIMIT). NOT RETAIL.
// ============================================================================

#include "platform_cpu.h"

#include <windows.h>

#include <vector>

namespace Platform {

bool CpuAffinity(uint64_t* processMask, uint64_t* systemMask) {
    DWORD_PTR process = 0;
    DWORD_PTR system = 0;
    if (!GetProcessAffinityMask(GetCurrentProcess(), &process, &system)) return false;
    *processMask = static_cast<uint64_t>(process);
    *systemMask = static_cast<uint64_t>(system);
    return true;
}

bool CpuSetProcessAffinity(uint64_t mask) {
    return SetProcessAffinityMask(GetCurrentProcess(), static_cast<DWORD_PTR>(mask)) != FALSE;
}

bool CpuPhysicalCores(uint64_t mask, int* cores, const char** reason) {
    DWORD len = 0;
    GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &len);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || len == 0) {
        *reason = "GetLogicalProcessorInformationEx-size";
        return false;
    }
    std::vector<unsigned char> buf(len);
    auto* first = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buf.data());  // CAST-OK: win32
    if (!GetLogicalProcessorInformationEx(RelationProcessorCore, first, &len)) {
        *reason = "GetLogicalProcessorInformationEx";
        return false;
    }
    int n = 0;
    for (DWORD off = 0; off < len;) {
        const auto* e = reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(  // CAST-OK: win32
            buf.data() + off);
        if (e->Size == 0) break;
        if (e->Relationship == RelationProcessorCore) {
            for (WORD g = 0; g < e->Processor.GroupCount; ++g) {
                const GROUP_AFFINITY& ga = e->Processor.GroupMask[g];
                if (ga.Group == 0 && (static_cast<uint64_t>(ga.Mask) & mask) != 0) {
                    ++n;
                    break;
                }
            }
        }
        off += e->Size;
    }
    if (n == 0) {
        *reason = "no-core-in-mask";
        return false;
    }
    *cores = n;
    return true;
}

// No per-core capacity is read on Windows: the roster treats every core as the same size
// (platform_cpu.h).
bool CpuCoreCapacity(int, int*) {
    return false;
}

}  // namespace Platform
