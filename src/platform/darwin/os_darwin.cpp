// ============================================================================
// src/platform/darwin/os_darwin.cpp -- Platform:: process-level services over Mach
// ============================================================================
// The Darwin twin of src/platform/posix/os_posix.cpp (src/platform/darwin/README.md).
//
// IsReadablePage (a diagnostic probe, not retail): the page must be MAPPED and
// readable. A mapped page may still be PROT_NONE (MemoryAllocator's reserved but
// uncommitted ranges, allocator_darwin.cpp), so the probe asks mach_vm_region for the
// region's protection rather than asking whether it is mapped.
// ============================================================================

#include "platform_os.h"

#include <dlfcn.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <mach-o/dyld.h>
#include <unistd.h>
#include <cstdio>

namespace Platform {

bool IsReadablePage(const void* p) {
    MemoryRegion region{};
    return QueryMemoryRegion(p, region) && region.readable;
}

// Not async-signal-safe (dladdr takes dyld's lock): its callers are IsReadablePage, the diag
// validators (entity_comp_validator.cpp, scene_watch.cpp) and ForceWork's probes
// (force_work.cpp), on normal threads. The Darwin crash handler
// (src/diag/darwin/crash_handler_darwin.cpp) never calls it; if it ever must, replace the
// dladdr with an image-range table built at startup.
bool QueryMemoryRegion(const void* p, MemoryRegion& out) {
    mach_vm_address_t address = reinterpret_cast<uintptr_t>(p);
    const auto requested = address;
    mach_vm_size_t size = 0;
    vm_region_basic_info_data_64_t info{};
    mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
    mach_port_t object = MACH_PORT_NULL;
    const auto rc = mach_vm_region(mach_task_self(), &address, &size,
                                   VM_REGION_BASIC_INFO_64, reinterpret_cast<vm_region_info_t>(&info), &count, &object);
    if (object != MACH_PORT_NULL) mach_port_deallocate(mach_task_self(), object);
    if (rc != KERN_SUCCESS || requested < address) return false;
    out.base = address;
    out.size = size;
    out.readable = (info.protection & VM_PROT_READ) != 0;
    out.executable = (info.protection & VM_PROT_EXECUTE) != 0;
    out.committed = out.accessible = info.protection != VM_PROT_NONE;
    // A file-backed image mapping (MEM_IMAGE): dladdr answers for any address inside a
    // loaded dyld image (code, data or linkedit); Mach's `shared` flag is copy-on-write
    // sharing, which says nothing about being an image.
    Dl_info owner{};
    out.image = ::dladdr(p, &owner) != 0 && owner.dli_fbase != nullptr;
    return true;
}

uintptr_t ExecutableImageBase() {
    return reinterpret_cast<uintptr_t>(_dyld_get_image_header(0));
}

// ExitProcess: platform_os.h says why error.cpp's Windows arm still declares the
// kernel32 import itself.
void ExitProcess(uint32_t exitCode) {
    std::fflush(nullptr);
    _exit(static_cast<int>(exitCode));
}

}  // namespace Platform
