# src/render/dx11/backend.cmake -- the DX11 renderer backend (BANISHED_RENDER_BACKEND=dx11).
#
# Included by the top-level CMakeLists.txt from src/render/${BANISHED_RENDER_BACKEND}/backend.cmake,
# before the targets exist. The contract every backend.cmake fills (src/render/common/README.md):
#   RENDER_SRCS              the backend's own sources (the top level appends src/render/common/*.cpp)
#   RENDER_BACKEND_INCLUDES  the backend's include directories (its own sources only)
#   RENDER_BACKEND_READY     ON when OpenBA can be configured with this backend on this host
# plus any per-source properties of its own files (directory scope, so they reach OpenBA).

file(GLOB_RECURSE RENDER_SRCS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/render/dx11/*.cpp)
set(RENDER_BACKEND_INCLUDES ${CMAKE_SOURCE_DIR}/src/render/dx11)
set(RENDER_BACKEND_READY ON)

if(NOT WIN32)
  # Windows only since Vulkan P5: off Windows the renderer is src/render/vulkan (the default there),
  # and the D3D11 headers this backend compiles against are MinGW-w64's.
  set(RENDER_SRCS)
  set(RENDER_BACKEND_INCLUDES)
  set(RENDER_BACKEND_READY OFF)
  message(STATUS "OpenBA not configured: BANISHED_RENDER_BACKEND=dx11 is Windows-only; off Windows the renderer is vulkan (the default)")
else()
  # The verbatim upstream ASSAO copy (GameTechDev/ASSAO, MIT; src/render/dx11/ao/assao/README.md) is not
  # ours to tidy: its standing -Wsign-compare / unused warnings are silenced per file so the warning
  # gate measures only our code (gate FAA, 2026-10-16: five perf-build warnings, all in this file).
  set_source_files_properties(${CMAKE_SOURCE_DIR}/src/render/dx11/ao/assao/ASSAODX11.cpp PROPERTIES
                              COMPILE_OPTIONS "-Wno-sign-compare;-Wno-unused-function;-Wno-unused-variable")
  set_source_files_properties(${CMAKE_SOURCE_DIR}/src/render/dx11/ao/assao/ASSAODX11.cpp PROPERTIES
                              COMPILE_DEFINITIONS BANISHED_THIRD_PARTY_SOURCE)
endif()
