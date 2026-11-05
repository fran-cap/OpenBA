// ============================================================================
// tests/unit/ext_render_pipe_sent_message_test.cpp -- ext.render_pipe: the main thread's
// hand-off waits run the window messages the render thread SENDS (review F-224)
// ============================================================================
// Windows only (empty elsewhere: no other platform has cross-thread sent messages). The
// window-owning thread (this test's thread, as the game's main thread owns the HWND)
// parks in Handoff::WaitIdle / WaitReleased / ClaimSteps while the "render thread" job
// sends to that window: the stand-in for DXGI's SetWindowPos / SetWindowLong inside
// IDXGISwapChain::Present in exclusive fullscreen (Microsoft's "DXGI Overview,
// Multithread Considerations"), or for a BeginCommands that resizes the window. A
// cross-thread send completes only when the owner thread dispatches it, which a plain
// std::condition_variable wait never does: the frame deadlocked (exit 4 in the game).
//   * WaitIdle + SetWindowPos: a watchdog frees the main thread after 2 s with Done()
//     and records the hang when the send has not returned.
//   * WaitReleased + SendMessageTimeoutW: the send must not time out.
//   * ClaimSteps + SetWindowPos before NoteBegun: as WaitIdle.
// The window is never shown.
// ============================================================================
#if defined(_WIN32)

#include "unit.h"
#include "ext/render_pipe/handoff.h"

#include <windows.h>

#include <atomic>
#include <chrono>
#include <thread>

namespace {

using Ext::RenderPipe::Handoff;

std::atomic<int> g_appMsgs{0};

LRESULT CALLBACK ProbeProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_APP + 7) {
        g_appMsgs.fetch_add(1);
        return 42;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

HWND MakeHiddenWindow() {
    static bool registered = false;
    HINSTANCE inst = GetModuleHandleW(nullptr);
    if (!registered) {
        WNDCLASSW wc = {};
        wc.lpfnWndProc   = ProbeProc;
        wc.hInstance     = inst;
        wc.lpszClassName = L"OpenBAUnitSentMsgProbe";
        RegisterClassW(&wc);
        registered = true;
    }
    // WS_POPUP, never shown: no visible window, no focus change.
    return CreateWindowExW(0, L"OpenBAUnitSentMsgProbe", L"probe", WS_POPUP, 0, 0, 64, 64, nullptr, nullptr,
                           inst, nullptr);
}

// Frees a main thread the job's send left parked (the stand-in for the exit-4 watchdog).
struct Watchdog {
    std::atomic<bool> inSend{false};
    std::atomic<bool> sendReturned{false};
    std::atomic<bool> hung{false};
    std::atomic<bool> finished{false};
    std::thread       thread;

    void Start(Handoff& h, bool alsoBegun) {
        thread = std::thread([this, &h, alsoBegun] {
            const auto t0 = std::chrono::steady_clock::now();
            while (!finished.load()) {
                if (std::chrono::steady_clock::now() - t0 > std::chrono::seconds(2)) {
                    if (inSend.load() && !sendReturned.load()) {
                        hung.store(true);
                        if (alsoBegun) h.NoteBegun();
                        h.Done();
                    }
                    return;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        });
    }
    // The main thread is back: run the send it left pending, if any, and let the job end.
    void Finish() {
        finished.store(true);
        thread.join();
        while (inSend.load() && !sendReturned.load()) {
            MSG m;
            PeekMessageW(&m, nullptr, 0, 0, PM_NOREMOVE | PM_QS_SENDMESSAGE);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
};

}  // namespace

// WaitIdle (pipe_driver.h Join / SettleTail / BeforeTransition).
UNIT_TEST(ext_render_pipe_waitidle_runs_sent_messages) {
    HWND hwnd = MakeHiddenWindow();
    CHECK(hwnd != nullptr);
    if (!hwnd) return;
    Handoff h;
    Watchdog dog;
    std::thread render([&] {
        while (h.WaitJob()) {
            dog.inSend.store(true);
            SetWindowPos(hwnd, nullptr, 0, 0, 80, 80, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOMOVE);
            dog.sendReturned.store(true);
            h.Done();
        }
    });
    dog.Start(h, false);
    h.Publish();
    h.WaitIdle();
    dog.Finish();
    h.WaitIdle();
    CHECK(!dog.hung.load());
    h.Stop();
    render.join();
    DestroyWindow(hwnd);
}

// WaitReleased (THE FENCE, FenceReleased).
UNIT_TEST(ext_render_pipe_waitreleased_runs_sent_messages) {
    HWND hwnd = MakeHiddenWindow();
    CHECK(hwnd != nullptr);
    if (!hwnd) return;
    Handoff h;
    DWORD_PTR result = 0;
    LRESULT ok = 0;
    std::thread render([&] {
        while (h.WaitJob()) {
            ok = SendMessageTimeoutW(hwnd, WM_APP + 7, 0, 0, SMTO_NORMAL, 2000, &result);
            h.Release();
            h.Done();
        }
    });
    h.Publish();
    h.WaitReleased();
    h.WaitIdle();
    CHECK(ok != 0);
    CHECK(result == 42);
    h.Stop();
    render.join();
    MSG m;
    PeekMessageW(&m, nullptr, 0, 0, PM_NOREMOVE | PM_QS_SENDMESSAGE);
    DestroyWindow(hwnd);
}

// ClaimSteps (P13_OVERLAP): it waits for the job's BeginCommands, which may resize the window.
UNIT_TEST(ext_render_pipe_claimsteps_runs_sent_messages) {
    HWND hwnd = MakeHiddenWindow();
    CHECK(hwnd != nullptr);
    if (!hwnd) return;
    Handoff h;
    Watchdog dog;
    std::thread render([&] {
        while (h.WaitJob()) {
            dog.inSend.store(true);
            SetWindowPos(hwnd, nullptr, 0, 0, 88, 88, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOMOVE);
            dog.sendReturned.store(true);
            h.NoteBegun();
            if (!h.PassGate()) h.WaitGate();
            h.Done();
        }
    });
    dog.Start(h, true);
    h.Publish();
    h.ClaimSteps();
    dog.Finish();
    h.WaitIdle();
    CHECK(!dog.hung.load());
    h.Stop();
    render.join();
    DestroyWindow(hwnd);
}

#endif  // _WIN32
