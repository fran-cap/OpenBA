# src/render/common/ — the backend-neutral side of the renderer

NOT RETAIL. Retail ships one renderer plugin per graphics API (VideoDX11-x64.dll, VideoDX9-x64.dll,
a GL slot); OpenBA links one backend into the exe, picked at configure time, or two in the dual
Windows exe ("Several backends in one exe" below). This directory
holds what every backend shares and what the code outside a backend may include. Nothing here
includes a graphics API header (no `d3d11.h`, `dxgi.h`, Metal, Vulkan): it is on every target's
include path, the unit tests' included.

| File | What |
|---|---|
| `render_backend.h` | The entry points `src/app/video_interface.cpp` writes into `Video::Device::dllSlots` (`Render::DeviceName`, `Render::CreateDeviceData`, and the cutover-only placeholder pair for the second slot), the window flag the Darwin twin asks for, and the two boot diagnostics `src/app/main.cpp` prints (`Render::DumpSwapChainState`, `Render::BootedExclusiveFullscreen`). |
| `render_impl.h` | `BANISHED_RENDER_IMPL_BEGIN` / `_END`: empty in a one-backend build; in the dual exe, the inline namespace `impl_<name>` that each backend's definitions of the interface headers live in. |
| `render_backend_table.h`, `render_backend_table_fill.h` | The dual exe only: one backend's entry points as a table, filled in the backend's own `render_backend_<name>.cpp`. |
| `multi/render_backend_dispatch.cpp` | The dual exe only: the public names of every interface function, forwarding to the picked backend; the requests recorded and made again of a newly picked backend. |
| `single/render_backend_single.cpp` | Every other build: the backend-set questions of `render_backend.h` for its one backend. |
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

## Several backends in one exe

`BANISHED_RENDER_BACKEND=dx11+vulkan` (a `+` list; `tools/build.sh` keys build/cmake-<flavour>-dx11+vulkan)
builds one Windows exe with both backends. Retail's own model is the reason it can switch at run time:
the Options dialog's Renderer change runs the full video restart (OptionsDialog::ApplyVideoSettings, exe
0x140071e10: MainLoop::OnVideoError, Device::Shutdown, Device::Startup, MainLoop::OnVideoRestart), which
destructs and reloads every video object; `ext.render_backend` (src/ext/render_backend/) adds the backend
choice to that combo and the fallback to DirectX 11. Both backends run as retail's interface 1 (the
DX11 slot): `Device::activeInterface` picks the shader and mesh blob that is read, and the Vulkan
backend reads the DX11 one.

How the two coexist, keeping the rules above:

1. Each backend's sources are compiled with `BANISHED_RENDER_IMPL_NS=impl_<name>` (CMakeLists.txt).
   The interface headers wrap the declarations a backend defines in `BANISHED_RENDER_IMPL_BEGIN` /
   `_END` (render_impl.h), and so do the backend's definition blocks: inside a backend, the names are
   its own (an inline namespace, so its code still calls `AoBackend::Supported()`), and the two sets
   do not collide at link time. Types and functions src/render/common defines once stay outside.
2. Each backend fills a `Render::BackendTable` in its `render_backend_<name>.cpp`
   (`render_backend_table_fill.h`, whose names resolve to that backend's own definitions there) and
   returns it from `RenderBackendTable_<name>()`; CMake writes the list of them (render_backends.inc).
3. `multi/render_backend_dispatch.cpp` defines the public names as forwards to the active table.
   `Render::SelectBackend` changes it only while no device exists, and makes the extensions'
   recorded requests (Request / SetFixFlag / SetDrawBloom) of the new backend.

Nothing outside a backend names its namespace (the table holds plain addresses), and a one-backend
build compiles none of this: the macros are empty and `single/` answers for its one backend. A
backend added to a dual build wraps its definition blocks the same way and fills its table.

The backends: `dx11/` (Windows only, its default), `metal/` (macOS), `vulkan/` (the default on Linux
since Vulkan P5, Android, and Windows as the dev bed; `docs/vulkan/PLAN.md`). `tools/build.sh` picks a
non-default one from `BANISHED_RENDER_BACKEND` into its own build directory.
