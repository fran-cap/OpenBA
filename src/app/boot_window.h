// src/app/boot_window.h
// Hold the engine window hidden until there is a real frame to put in it.
//
// The problem, measured (see [BootTime] lines in standalone_debug.log):
//
//   t=  70ms  Engine::Startup begins
//   t= 535ms  ... Runtime platform/filesystem work (package mount, disk-bound)
//   t= 625ms  VideoDX11 CreateDevice step 6: ShowWindow  <-- window on screen
//   t= 750ms  D3D11CreateDevice + swap chain + color/depth buffers
//   t= 940ms  rest of Engine::Startup (audio, input)
//   t=1070ms  our post-Startup boot (sprite sheet, splash .rsc preloads)
//   t=1100ms  first splash frame is finally presented
//
// So the window is up and empty for ~450ms of every boot. Nothing in that
// window is wasted work — it is all load the game genuinely has to do — but
// there is no reason for the user to watch an empty frame while it happens.
// Retail hides this behind the same ShowWindow simply because it reaches its
// first Present sooner.
//
// The fix uses the engine's OWN mechanism rather than a new one. Video::
// Configuration+0x42 ("skipShow") is a non-persisted flag that the shipped
// CreateDevice already honours:
//
//     if (!Configuration+0x42) ShowWindow(Device+0x90, SW_SHOWDEFAULT)
//
// We set that flag before CreateDevice reads it, so the shipped branch skips
// its own ShowWindow with no change to the ported code path, and we take
// responsibility for showing the window once the main loop has presented its
// first frame (MarkFrame, called by Engine::MainLoop::Run after every Frame).
// That is the first frame of WHICHEVER loop runs first: at boot the outer
// GameLoaderLoop, whose frames are retail's black LoaderLoop::Draw clear while
// the loader thread reads Game/GlobalResources.rsc (issue #13: with a large mod
// set that load is about a minute, and the window has to be up and pumping
// for it). What the deferral still buys: the window never appears unpainted
// during Engine::Startup and the post-Startup boot; its first content is a
// presented frame. Since the sync loader wait went (issue #13) that frame is
// black rather than the Shining Rock splash, as in retail.
//
// Boot wall-clock is unchanged — this trades a visible empty window for a
// slightly later, already-presented one.
//
// Kill-switch: BANISHED_DEFER_WINDOW_SHOW=0 restores the shipped behaviour
// (window appears as soon as CreateDevice shows it).

#pragma once

namespace Video { class Configuration; }   // src/engine/video/Device.h

namespace BootWindow {

// Called from CreateDevice (DX11 and Vulkan) just before step 6. Sets the engine's
// skipShow flag and records the window (the HWND on Win32, an opaque handle:
// platform_window.h) so ShowNow can reveal it later.
// Returns true if the deferral was armed; false means the shipped ShowWindow
// runs as normal (kill-switch set, or no config/hwnd available).
bool ArmDeferredShow(::Video::Configuration* videoConfig, void* hwnd);

// Reveal the window. Idempotent, and a no-op when nothing was armed.
// `reason` is logged so the boot trace shows which path revealed it.
void ShowNow(const char* reason);

// True while the window is armed but still hidden.
bool IsPending();

// Once per main-loop frame, after the frame's Present (MainLoop::Draw ->
// ContextData::Finish runs inside Frame; ext.render_pipe only pipelines
// GameUpdateLoop frames, never a loader loop's). Reveals the armed window on
// the first call. Reached from the engine through BootWindow_MarkFrame.
void MarkFrame();

} // namespace BootWindow

extern "C" void BootWindow_MarkFrame();
