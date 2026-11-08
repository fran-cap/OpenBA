#pragma once
// ============================================================================
// src/render/common/render_impl.h -- one backend's names in a build with several
// ============================================================================
// NOT RETAIL. The backend-neutral interface headers (render_backend.h, ao/ao_backend.h, ...) declare
// the functions a backend defines. A build with one backend (every target but the dual Windows exe)
// links exactly one definition of each, and these two macros are empty.
//
// A build with several (BANISHED_RENDER_BACKEND=dx11+vulkan, src/render/common/README.md "Several
// backends in one exe") compiles each backend's sources with BANISHED_RENDER_IMPL_NS=impl_<name>
// (CMakeLists.txt). Inside those sources the interface functions are declared in an inline
// namespace of that name: a backend's own code still calls `AoBackend::Supported()` and reaches its
// own body, and the two backends' definitions no longer collide at link time. Everywhere else
// (src/ext, src/engine, src/app, src/render/common) the macros stay empty and the same names are the
// dispatchers in src/render/common/multi/render_backend_dispatch.cpp, which forward to the backend
// the player picked (render_backend_table.h).
//
// Only a declaration a backend DEFINES goes between the two macros. Types, inline helpers and
// functions that src/render/common defines once for every backend stay outside.
// ============================================================================

#if defined(BANISHED_RENDER_IMPL_NS)
#define BANISHED_RENDER_IMPL_BEGIN inline namespace BANISHED_RENDER_IMPL_NS {
#define BANISHED_RENDER_IMPL_END }
#else
#define BANISHED_RENDER_IMPL_BEGIN
#define BANISHED_RENDER_IMPL_END
#endif
