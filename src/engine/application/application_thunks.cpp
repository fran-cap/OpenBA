// ============================================================================
// src/engine/application/application_thunks.cpp — Wave 12 lane A
//   Application::Application::Update / Shutdown / Startup
// ============================================================================
// Three one-instruction tail-call thunks — each is a single `jmp` to its
// `Platform*` sibling, verified against `<SCRATCH>/app_disasm_main.txt`:
//
//   ?Update@Application@1@QEAA_NXZ    retail 0x18009f4f0   jmp 0x18009fdf0 (PlatformUpdate)
//   ?Shutdown@Application@1@QEAAXXZ   retail 0x18009f500   jmp 0x18009fd70 (PlatformShutdown)
//   ?Startup@Application@1@QEAA_NXZ   retail 0x18009f510   jmp 0x18009fc10 (PlatformStartup)
//
// A `jmp` (not `call`) is a true tail call: RCX (`this`) is untouched and the
// return value is whatever the callee returns, so each port here just forwards
// the call and its result.
//
// PlatformUpdate and PlatformStartup (application_platform.cpp) are ported in
// this tree — this file forwards to them, so calling Update()/Startup() runs our own
// body exactly once.
//
// PlatformShutdown (0x18009fd70) is in application_platform.cpp too; Shutdown()
// calls it exactly as Update/Startup call their siblings.
// ============================================================================

#include "application/Application.h"

// ENDGAME B.3.7: `Application::Application` is the real class; a global
// `using RtApplication::Application;` is ill-formed next to the namespace.
using App = Application::Application;

// ============================================================================
// Application::Application::Update()    retail 0x18009f4f0   jmp -> PlatformUpdate
// ?Update@Application@1@QEAA_NXZ
// ----------------------------------------------------------------------------
// The tail call's own callee (PlatformUpdate, application_platform.cpp) does the
// work; this wrapper forwards.
// ============================================================================
bool Application::Application::Update() {
    return PlatformUpdate();
}

// ============================================================================
// Application::Application::Shutdown()   retail 0x18009f500   jmp -> PlatformShutdown
// ?Shutdown@Application@1@QEAAXXZ
// ----------------------------------------------------------------------------
// Same shape as Update: PlatformShutdown (application_platform.cpp) does the
// work; this wrapper forwards.
// ============================================================================
void Application::Application::Shutdown() {
    PlatformShutdown();
}

// ============================================================================
// Application::Application::Startup()    retail 0x18009f510   jmp -> PlatformStartup
// ?Startup@Application@1@QEAA_NXZ
// ----------------------------------------------------------------------------
// Same shape as Update: PlatformStartup (application_platform.cpp) does the
// work; this wrapper forwards.
// ============================================================================
bool Application::Application::Startup() {
    return PlatformStartup();
}
