// ============================================================================
// src/render/common/single/render_backend_single.cpp -- the backend set of a one-backend build
// ============================================================================
// NOT RETAIL. Every build but the dual Windows exe links exactly one backend, which defines the entry
// points of render_backend.h itself; this file answers the backend-set questions for it (one backend,
// always active). CMake compiles it only into a one-backend build, with BANISHED_RENDER_BACKEND_ID set
// to the backend's directory name; the dual exe compiles src/render/common/multi/ instead.
// ============================================================================
#include "render_backend.h"

#include <cstring>

#ifndef BANISHED_RENDER_BACKEND_ID
#error "BANISHED_RENDER_BACKEND_ID is set by CMakeLists.txt on this file"
#endif

namespace Render {

int BackendCount() {
    return 1;
}

const char* BackendId(int index) {
    return index == 0 ? BANISHED_RENDER_BACKEND_ID : nullptr;
}

const char* BackendName(int index) {
    return index == 0 ? DeviceName() : nullptr;
}

int FindBackend(const char* id) {
    if (id != nullptr && std::strcmp(id, BANISHED_RENDER_BACKEND_ID) == 0) return 0;
    return -1;
}

int ActiveBackend() {
    return 0;
}

bool BackendAvailable(int index) {
    return index == 0;
}

bool SelectBackend(int index) {
    return index == 0;
}

bool WindowPresentedByOtherBackend() {
    return false;
}

bool WindowTakenOverByOtherBackend() {
    return false;
}

}  // namespace Render
