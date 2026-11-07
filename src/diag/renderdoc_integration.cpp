// src/diag/renderdoc_integration.cpp
//
// Implementation of in-app RenderDoc API hookup for graphics debugging.
//
// API reference: https://renderdoc.org/docs/in_application_api.html
// Header equivalent: renderdoc_app.h (struct RENDERDOC_API_1_4_0)
//
// We define just enough of the API surface (start/end frame capture,
// version query, capture-file template) inline so we don't need an
// external header. Layout matches RenderDoc's renderdoc_app.h exactly.

#include "renderdoc_integration.h"

#if defined(_WIN32)
#include <windows.h>
#define RENDERDOC_CC __cdecl
#else
// Phase D.4 LINUX-1: RenderDoc's Linux library is librenderdoc.so, with the same
// RENDERDOC_GetAPI export; renderdoc_app.h's RENDERDOC_CC is __cdecl on Windows and empty
// elsewhere.
#include <dlfcn.h>
#define RENDERDOC_CC
#endif
#include <cstdint>
#include <cstdio>

namespace {

// ---------------------------------------------------------------
// Inline subset of renderdoc_app.h
// ---------------------------------------------------------------

// API version enum we request. 1.4.0 is widely supported; we use
// the small subset of pointers below, all of which exist in 1.0.0+.
constexpr int RENDERDOC_API_VERSION_1_4_0 = 10400;

// Function pointer typedefs — match renderdoc_app.h verbatim.
using pRENDERDOC_GetAPIVersion =
    void(RENDERDOC_CC*)(int* major, int* minor, int* patch);
using pRENDERDOC_StartFrameCapture =
    void(RENDERDOC_CC*)(void* device, void* wndHandle);
using pRENDERDOC_IsFrameCapturing =
    unsigned int(RENDERDOC_CC*)(void);
using pRENDERDOC_EndFrameCapture =
    unsigned int(RENDERDOC_CC*)(void* device, void* wndHandle);
using pRENDERDOC_TriggerCapture =
    void(RENDERDOC_CC*)(void);
using pRENDERDOC_SetCaptureFilePathTemplate =
    void(RENDERDOC_CC*)(const char* pathtemplate);
using RdocCString = const char*;   // named so the declarator below reads as one
using pRENDERDOC_GetCaptureFilePathTemplate =
    RdocCString(RENDERDOC_CC*)(void);
using pRENDERDOC_GetNumCaptures =
    unsigned int(RENDERDOC_CC*)(void);

// Layout of the struct RenderDoc returns from RENDERDOC_GetAPI for
// RENDERDOC_API_VERSION_1_4_0. We only declare the fields we use; the
// rest are reserved void* slots in the same order they appear in the
// real header so layout matches.
struct RENDERDOC_API_1_4_0 {
    pRENDERDOC_GetAPIVersion           GetAPIVersion;

    // 4 reserved slots (SetCaptureOptionU32/F32, GetCaptureOptionU32/F32).
    void* slot_unused1;
    void* slot_unused2;
    void* slot_unused3;
    void* slot_unused4;

    // 1 slot for SetFocusToggleKeys.
    void* slot_unused5;
    // 1 slot for SetCaptureKeys.
    void* slot_unused6;

    // 1 slot for GetOverlayBits.
    void* slot_unused7;
    // 1 slot for MaskOverlayBits.
    void* slot_unused8;

    // 2 slots for RemoveHooks/UnloadCrashHandler (renamed from Shutdown
    // in newer versions).
    void* slot_unused9;
    void* slot_unused10;

    pRENDERDOC_SetCaptureFilePathTemplate  SetCaptureFilePathTemplate;
    pRENDERDOC_GetCaptureFilePathTemplate  GetCaptureFilePathTemplate;

    pRENDERDOC_GetNumCaptures              GetNumCaptures;

    // 1 slot for GetCapture.
    void* slot_unused11;

    pRENDERDOC_TriggerCapture              TriggerCapture;

    // 3 slots for IsTargetControlConnected/LaunchReplayUI/SetActiveWindow.
    // (TriggerMultiFrameCapture sits AFTER EndFrameCapture, not here — easy
    //  to get wrong and end up calling IsFrameCapturing where we expected
    //  StartFrameCapture, silently producing zero captures.)
    void* slot_unused12;
    void* slot_unused13;
    void* slot_unused14;

    pRENDERDOC_StartFrameCapture           StartFrameCapture;
    pRENDERDOC_IsFrameCapturing            IsFrameCapturing;
    pRENDERDOC_EndFrameCapture             EndFrameCapture;

    // ... (remaining 1.4 fields not used by us)
};

using pRENDERDOC_GetAPI =
    int(RENDERDOC_CC*)(int version, void** outAPIPointers);

// ---------------------------------------------------------------
// State
// ---------------------------------------------------------------

RENDERDOC_API_1_4_0* g_api = nullptr;
unsigned int g_frameCount = 0;
// One capture at a time: TriggerCapture queues it (Pending), the next OnFrame starts it
// (InFlight), the OnFrame after that ends it (Idle). Plain, not atomic, as the two bools were.
enum class CaptureState : uint8_t {
    Idle,      // no capture queued or running
    Pending,   // requested; StartFrameCapture on the next OnFrame
    InFlight,  // started; EndFrameCapture on the next OnFrame
};
CaptureState g_captureState = CaptureState::Idle;
int g_captureCountdown = -1;  // <0 = inactive; counts down each OnFrame to 0

}  // anonymous namespace

namespace RenderDocIntegration {

bool Init() {
    if (g_api) return true;

#if defined(_WIN32)
    HMODULE rdoc = GetModuleHandleA("renderdoc.dll");
#else
    // Only a library the RenderDoc launcher already injected: RTLD_NOLOAD never loads it.
    void* rdoc = dlopen("librenderdoc.so", RTLD_NOW | RTLD_NOLOAD);
#endif
    if (!rdoc) {
        printf("[RenderDoc] renderdoc.dll not loaded — running without "
               "graphics debugger. Launch OpenBA.exe via RenderDoc's "
               "UI to enable in-app capture.\n");
        fflush(stdout);
        return false;
    }

#if defined(_WIN32)
    auto getAPI = reinterpret_cast<pRENDERDOC_GetAPI>(  // CAST-OK: win32
        GetProcAddress(rdoc, "RENDERDOC_GetAPI"));
#else
    auto getAPI = reinterpret_cast<pRENDERDOC_GetAPI>(  // CAST-OK: posix
        dlsym(rdoc, "RENDERDOC_GetAPI"));
#endif
    if (!getAPI) {
        printf("[RenderDoc] renderdoc.dll loaded but RENDERDOC_GetAPI "
               "not exported — version mismatch?\n");
        fflush(stdout);
        return false;
    }

    void* api = nullptr;
    int ok = getAPI(RENDERDOC_API_VERSION_1_4_0, &api);
    if (!ok || !api) {
        printf("[RenderDoc] RENDERDOC_GetAPI(1.4.0) failed (ret=%d, "
               "api=%p) — older RenderDoc build?\n", ok, api);
        fflush(stdout);
        return false;
    }

    g_api = static_cast<RENDERDOC_API_1_4_0*>(api);  // CAST-OK: win32

    int major = 0, minor = 0, patch = 0;
    if (g_api->GetAPIVersion) {
        g_api->GetAPIVersion(&major, &minor, &patch);
    }

    // Default capture path: alongside the standalone exe, named
    // "reimpl_capture_<timestamp>.rdc".
    if (g_api->SetCaptureFilePathTemplate) {
        g_api->SetCaptureFilePathTemplate("reimpl_capture");
    }

    const char* pathTpl = "(unset)";
    if (g_api->GetCaptureFilePathTemplate) {
        pathTpl = g_api->GetCaptureFilePathTemplate();
    }

    printf("[RenderDoc] attached — API %d.%d.%d, capture path template "
           "= '%s'\n", major, minor, patch, pathTpl);
    fflush(stdout);
    return true;
}

bool IsAttached() {
    return g_api != nullptr;
}

void TriggerCapture() {
    if (!g_api) return;
    if (g_captureState != CaptureState::Idle) {
        // Already queued or actively capturing — don't double-trigger.
        return;
    }
    g_captureState = CaptureState::Pending;
    printf("[RenderDoc] capture requested (frame=%u)\n", g_frameCount);
    fflush(stdout);
}

void RequestCaptureInFrames(int n) {
    if (!g_api) return;
    if (n < 0) n = 0;
    // Only (re)arm if not already armed/capturing, so repeated Apply clicks
    // don't keep pushing the capture out.
    if (g_captureCountdown < 0 && g_captureState == CaptureState::Idle) {
        g_captureCountdown = n;
        printf("[RenderDoc] capture armed for %d frames from now (frame=%u)\n",
               n, g_frameCount);
        fflush(stdout);
    }
}

void OnFrame() {
    if (!g_api) {
        ++g_frameCount;
        return;
    }

    // Frame-delayed capture arm: fire TriggerCapture when the countdown hits 0.
    if (g_captureCountdown >= 0) {
        if (g_captureCountdown == 0) {
            g_captureCountdown = -1;
            TriggerCapture();
        } else {
            --g_captureCountdown;
        }
    }

    if (g_captureState == CaptureState::InFlight) {
        // Frame N+1: end the capture.
        // device=NULL + wndHandle=NULL means "all devices, all windows".
        unsigned int nCaptures = 0;
        if (g_api->EndFrameCapture) {
            nCaptures = g_api->EndFrameCapture(nullptr, nullptr);
        }
        unsigned int total = g_api->GetNumCaptures
                                 ? g_api->GetNumCaptures() : 0;
        printf("[RenderDoc] capture ended (success=%u, total saved=%u). "
               "Open in RenderDoc UI to inspect.\n", nCaptures, total);
        fflush(stdout);
        g_captureState = CaptureState::Idle;
    }

    if (g_captureState == CaptureState::Pending) {
        // Frame N: begin the capture. Will record everything until
        // EndFrameCapture next OnFrame.
        if (g_api->StartFrameCapture) {
            g_api->StartFrameCapture(nullptr, nullptr);
        }
        printf("[RenderDoc] capture started (frame=%u)\n", g_frameCount);
        fflush(stdout);
        g_captureState = CaptureState::InFlight;
    }

    ++g_frameCount;
}

unsigned int FrameCount() {
    return g_frameCount;
}

}  // namespace RenderDocIntegration
