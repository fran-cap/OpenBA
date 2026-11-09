// ============================================================================
// src/diag/tweak_overlay.h -- the debug tweak overlay (Dear ImGui, Alt+F10)
// ============================================================================
// NOT RETAIL. DEBUG FLAVOUR ONLY: CMake option BANISHED_TWEAK_OVERLAY (ON for the debug
// flavour on Windows, OFF for og / perf / release / Linux). Without it this header is empty,
// tweak_overlay.cpp and third_party/imgui are not compiled, and every call site below sits
// inside `#if BANISHED_TWEAK_OVERLAY`. Block TWEAKUI; docs/reference/tweak_overlay.md.
//
// Live tuning of the graphics extensions (ext.ambient_occlusion, ext.better_trees,
// ext.max_shadow_res, ext.render_pipe, ext.eye_camera) without the Options dialog round
// trip. Every change goes through the extension's own live path (Ext::SetAndRecord,
// Ext::SetValueAndRecord, Ext::BetterTrees::Set / Sync, src/ext/tweak/ext_tweaks.h), never
// a poke at render state. Under /profile:faithful it shows "faithful profile: nothing to
// tweak" and every control is disabled.
//
// THREADS
//   main (the SDL pump)   OnSdlEvent: the toggle key; while open, copies the mouse /
//                         keyboard events into a queue and swallows them (the game sees
//                         none). MainThreadTick: applies the queued row / value changes
//                         and snapshots the registry for the UI. Creates the ImGui
//                         context on the first open.
//   presenting thread     BeforePresent: the ImGui frame (events drained from the queue,
//                         NewFrame, the panels, Render) and the renderer's draw into the
//                         frame's last image, right before Present. The DX11 backend calls it
//                         from ContextData::Finish (main thread) or Ext::RenderPipe::FinishPresent
//                         (the render thread); the Vulkan backend from DrawEndFrame. Only this
//                         thread touches the ImGui context once it exists.
//   main, render idle     OnRendererShutdown (the backend's device shutdown): the renderer lets
//                         go of its GPU objects; the next open frame initialises it again
//                         (device lost, a settings rebuild, a switch of ext.render_backend).
//
// RENDERERS
//   The overlay names no graphics API. Each backend implements Renderer in its own directory
//   (src/render/dx11/tweak_overlay_dx11.cpp: ImGui's DX11 backend; src/render/vulkan/
//   tweak_overlay_vulkan.cpp: our own, on the Vulkan backend's resources) and passes it in. One
//   renderer is registered with ImGui at a time: a frame from another renderer shuts the
//   previous one down first (its textures go back to WantCreate, so the font atlas is made
//   again on the new one).
// Closed, each hook costs one atomic load (and the pump one key-event compare).
// ============================================================================
#pragma once

#if BANISHED_TWEAK_OVERLAY

#include <atomic>

#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_scancode.h>

union SDL_Event;
struct ImDrawData;

// The toggle: a compile-time key combo (no settings key, no environment flag).
#define TWEAK_OVERLAY_KEY     SDL_SCANCODE_F10
#define TWEAK_OVERLAY_KEY_MOD SDL_KMOD_ALT     // either Alt

namespace Diag {
namespace TweakOverlay {

namespace detail {
extern std::atomic<bool> g_open;
}

// The overlay is open: it owns the mouse and the keyboard (input_sdl.cpp reads nothing).
inline bool IsOpen() { return detail::g_open.load(std::memory_order_acquire); }

// Pump thread, every SDL event. True = the overlay took it (the toggle, or a mouse /
// keyboard / text event while open): the pump does not translate it for the game.
bool OnSdlEvent(const SDL_Event& ev);

// Pump thread, once per Pump: the queued changes and the registry snapshot.
void MainThreadTick();

// A backend's ImGui renderer. Every call is made on the presenting thread with the ImGui context
// current, under the overlay's draw lock, except Shutdown, which OnRendererShutdown also makes from
// the backend's device shutdown.
class Renderer {
public:
    virtual ~Renderer() = default;
    virtual const char* Name() const = 0;
    // Registers with ImGui (io.BackendRendererName / BackendFlags, RendererHasTextures). False: the
    // frame is skipped and Init is asked again on the next one.
    virtual bool Init() = 0;
    virtual void NewFrame() = 0;
    // Honours draw_data->Textures (create / update / destroy) and draws the lists.
    virtual void Render(ImDrawData* drawData) = 0;
    // Releases every texture (SetStatus(Destroyed): back to WantCreate) and its own objects, and
    // unregisters from ImGui.
    virtual void Shutdown() = 0;
};

// The presenting thread, the device locked, right before Present: the frame's last image is
// `width` x `height` and the renderer draws into it. Nothing while the overlay is closed.
void BeforePresent(Renderer& renderer, unsigned width, unsigned height);

// The backend's device shutdown (main thread; the presenting thread idle): Shutdown on `renderer`
// when it is the registered one.
void OnRendererShutdown(Renderer& renderer);

}  // namespace TweakOverlay
}  // namespace Diag

#endif  // BANISHED_TWEAK_OVERLAY
