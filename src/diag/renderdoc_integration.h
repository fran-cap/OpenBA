// src/diag/renderdoc_integration.h
//
// In-app RenderDoc API integration for the standalone exe.
//
// When the exe is launched via RenderDoc's UI, RenderDoc injects
// renderdoc.dll into the process. This module probes for that DLL at
// startup, fetches the in-app API entry point, and exposes a small
// surface to trigger frame captures programmatically.
//
// If RenderDoc is NOT attached (the exe ran normally), all entry points
// are safe no-ops.
//
// Usage:
//   1. Install RenderDoc (https://renderdoc.org/).
//   2. In RenderDoc's UI, point the launcher at
//      C:/GOG Games/Banished/OpenBA.exe and Launch.
//   3. The exe will detect renderdoc.dll, log the API version, and
//      auto-capture the tenth menu draw (MenuDialogLoopT::Draw) to RenderDoc's
//      capture folder. Press F12 in RenderDoc to capture another frame.
//   4. Open the .rdc file in RenderDoc to inspect GPU state, shaders,
//      bound render targets, etc.

#pragma once

namespace RenderDocIntegration {

// Probe for renderdoc.dll, fetch the API. Call once at startup
// (e.g., from WinMain after the stdout/stderr log redirect).
// Returns true if RenderDoc is attached and the API resolved.
bool Init();

// True if Init() succeeded and RenderDoc is attached.
bool IsAttached();

// Request a one-frame capture. If RenderDoc isn't attached, no-op.
// Captures begin on the next D3D Present and end on the one after.
void TriggerCapture();

// Request a one-frame capture N frames from now. Decremented each OnFrame();
// when it reaches zero TriggerCapture() fires. Used to capture a steady-state
// frame a few frames AFTER a video-settings rebuild (the rebuild frame itself
// is skipped by BeginFrame), so the capture reflects the new render targets.
// No-op if not attached.
void RequestCaptureInFrames(int n);

// Periodic call from the main loop to manage capture state.
// Captures the very first frame after a TriggerCapture() request.
// No-op if not attached.
void OnFrame();

// Accessor for outside code that wants to check whether to capture
// (e.g., MenuDLT::Update can call TriggerCapture() on frame N).
unsigned int FrameCount();

}  // namespace RenderDocIntegration
