# src/render/common/ — the backend-neutral side of the renderer

NOT RETAIL. Retail ships one renderer plugin per graphics API (VideoDX11-x64.dll, VideoDX9-x64.dll,
a GL slot); OpenBA links exactly one backend into the exe, picked at configure time. This directory
holds what every backend shares and what the code outside a backend may include. Nothing here
includes a graphics API header (no `d3d11.h`, `dxgi.h`, Metal, Vulkan): it is on every target's
include path, the unit tests' included.

| File | What |
|---|---|
| `render_backend.h` | The entry points `src/app/video_interface.cpp` writes into `Video::Device::dllSlots` (`Render::DeviceName`, `Render::CreateDeviceData`, and the cutover-only placeholder pair for the second slot), the window flag the Darwin twin asks for, and the two boot diagnostics `src/app/main.cpp` prints (`Render::DumpSwapChainState`, `Render::BootedExclusiveFullscreen`). |
| `ao/shaders/` | Backend-neutral generated ASSAO shader byte arrays. |
| `ao/ao_backend.h` | `AoBackend::`: what ext.ambient_occlusion asks of the backend (readable depth, feature support). |
| `better_trees/foliage_aa_backend.h` | `FaaBackend::`: ext.foliage_aa's request and its tweak stats. |
| `better_trees/billboard_shadow_backend.h` | `BbsBackend::`: ext.billboard_shadow's request. |
| `better_trees/dxbc.{h,cpp}` | A minimal DXBC container + SM4 token reader / writer (pure byte work, unit-tested). |
| `better_trees/cutout_rewrite.{h,cpp}`, `receiver_rewrite.{h,cpp}`, `edge_dither.{h,cpp}` | The ext.better_trees shader-bytecode rewrites over `dxbc.h` (pure, unit-tested: `tests/unit/ext_better_trees_test.cpp`). |

Other backend-neutral seams that a backend implements live with their callers, not here:
`Ext::RenderPipe::FinishPresent` / `FinishRecover` (`src/ext/render_pipe/finish_split.h`) and
`Graphics::ExtAoAfterOpaque` (`src/engine/graphics/renderer_ext_ao.h`).

## How a backend plugs in

A backend is a directory `src/render/<name>/` with:

1. **`backend.cmake`** — included by the top-level `CMakeLists.txt` when
   `BANISHED_RENDER_BACKEND=<name>` (a cache string; `dx11` by default). It sets `RENDER_SRCS`
   (its own sources; the top level appends `src/render/common/*.cpp`), `RENDER_BACKEND_INCLUDES`
   (its include directories, given to its own sources only: nothing outside the backend includes
   its headers) and `RENDER_BACKEND_READY` (`OFF`, with a message, when the host lacks
   what it needs), plus per-source properties of its own files. `src/render/dx11/backend.cmake` is
   the reference.
2. **`render_backend_<name>.cpp`** — the definitions of `render_backend.h` (DX11's are forwarders to
   its plugin exports: `src/render/dx11/render_backend_dx11.cpp`).
3. The bodies of the interface headers above (`AoBackend::`, `FaaBackend::`, `BbsBackend::`) and of
   the two seams named above; a backend that does not support a feature gives the "not supported"
   answer (`AoBackend::Supported()` false) and ignores the request.

Nothing outside `src/render/<name>/` names the backend's own namespace. (The former exception, the
two DXGI swap-chain probes in `src/app/main.cpp`, went behind `Render::DumpSwapChainState` /
`Render::BootedExclusiveFullscreen` when the Vulkan backend arrived; their DX11 bodies are
`src/render/dx11/swap_probe_dx11.cpp`.)

The backends: `dx11/` (Windows only, its default), `metal/` (macOS), `vulkan/` (the default on Linux
since Vulkan P5, Android, and Windows as the dev bed; `docs/vulkan/PLAN.md`). `tools/build.sh` picks a
non-default one from `BANISHED_RENDER_BACKEND` into its own build directory.
