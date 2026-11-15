// ============================================================================
// src/engine/application/window_proc.cpp -- Application::Application::WindowProc
// ============================================================================
// Phase D.4 SDL W2: the callback walk and the message switch, moved out of
// src/platform/win/platform_startup_win.cpp so the portable engine owns them. What
// stays Win32 is behind Platform::Window: the DefWindowProcW tail
// (Platform::Window::DefaultWindowProc) and WM_DESTROY's PostQuitMessage. From SDL W3
// nothing calls this as a WNDPROC: SDL owns the window procedure and Platform::Pump
// (src/platform/sdl/pump_sdl.cpp) hands it the message tuples translated from SDL
// events (src/platform/common/window_messages.h).
// The message ids are named in src/platform/common/platform_window.h (kMsg*), with
// their WM_* values; the transcription below is otherwise unchanged.
// ============================================================================

#include <cstdint>

#include "application/Application.h"
#include "collection/set_avl.h"
#include "platform_window.h"

using App = Application::Application;
using rtsetavl::Node;

namespace {

// The 16-byte element (Application.h). Only the key at +0x00 and the callback
// at +0x08 are ever read here — the four padding bytes at +0x04 carry a dword
// of the REGISTERING caller's stack frame (message_callbacks.cpp documents
// why), so nothing may compare them.
using Element = App::MessageCallbackEntry;
using MessageCallbackFn = App::MessageCallback;

}  // namespace

// ============================================================================
// Application::Application::WindowProc(HWND, UINT, WPARAM, LPARAM)
// ?WindowProc@Application@1@CA_JPEAUHWND__@@I_K_J@Z    retail 0x18009f930
//                                                      ord 4546  (CA = static)
// ============================================================================
// Retail's `PlatformStartup` stores 0x18009f930 into `WNDCLASSW::lpfnWndProc`;
// here Platform::Pump hands it each translated message (see the banner above).
//
// Retail's shape, transcribed:
//
//   1. read `Application::_singleton`; if it is null, or its callback Set is
//      empty, skip straight to the switch;
//   2. otherwise descend the AVL tree by UNSIGNED comparison of the message id
//      against the key at node+0x00, following +0x18 (less) and +0x20
//      (greater) — the same descent RegisterMessageCallback uses;
//   3. on a hit, CALL the stored callback with the four arguments UNCHANGED and
//      return 0 if it returns non-zero (the callback claimed the message);
//   4. the switch: WM_DESTROY posts quit; WM_SETFOCUS/WM_KILLFOCUS write the
//      one byte at +0x11 and then STILL fall through to DefWindowProcW;
//      WM_ERASEBKGND is swallowed; WM_SYSKEYDOWN is swallowed only for VK_F10;
//      everything else is DefWindowProcW.
//
// TWO THINGS READ FROM THE DISASSEMBLY, NOT FROM THE DECOMPILER
// -------------------------------------------------------------
//   * The equal-key branch at 0x18009f98e RE-TESTS `RAX` for null even though
//     the loop just tested it. Kept — it is retail's, it is free, and removing
//     it is the kind of tidying that makes a transcription stop being one.
//   * `CMP RSI,0x79` at 0x18009f9b9 is a SIXTY-FOUR-BIT compare of the whole
//     WPARAM against VK_F10, not a compare of its low dword.
//
// ============================================================================
int64_t Application::Application::WindowProc(void* hwnd, uint32_t msg,
                                             uint64_t wParam, int64_t lParam) {
    App*  app  = ::Application::Application::_singleton;   // ?_singleton@Application@1@0PEAV11@EA

    if (app != nullptr) {                                   // 0x18009f95c
        Node* n = app->_messageCallbacks.root;
        if (n != nullptr) {                                 // 0x18009f965
            Node* found = nullptr;
            while (n != nullptr) {
                const unsigned key = rtsetavl::ElementOf<Element>(n).message;
                if (msg < key) {                            // 0x18009f976 JC
                    n = n->left;
                } else if (key < msg) {                     // 0x18009f97b JNC
                    n = n->right;
                } else {
                    found = n;
                    break;
                }
            }
            if (found != nullptr) {                         // 0x18009f98e, retail re-tests
                MessageCallbackFn cb =
                    rtsetavl::ElementOf<Element>(found).callback;
                if (cb(hwnd, msg, wParam, lParam) != 0) {
                    return 0;                               // 0x18009f99b -> 0x18009f9f0
                }
            }
        }
    }

    switch (msg) {
        case Platform::Window::kMsgDestroy:                 // 2, 0x18009f9e8
            Platform::Window::PostQuitMessage(0);
            return 0;
        case Platform::Window::kMsgSetFocus:                // 7, 0x18009f9cc
            if (app) app->_running = 1;
            break;                                          // falls to DefWindowProcW
        case Platform::Window::kMsgKillFocus:               // 8, 0x18009f9c1
            if (app) app->_running = 0;
            break;                                          // falls to DefWindowProcW
        case Platform::Window::kMsgEraseBackground:         // 0x14, 0x18009f9b0
            return 0;
        case Platform::Window::kMsgSysKeyDown:              // 0x104, 0x18009f9b9
            if (wParam == Platform::Window::kKeyF10) return 0;   // VK_F10, 64-bit compare
            break;
        default:
            break;
    }
    return Platform::Window::DefaultWindowProc(hwnd, msg, wParam, lParam);   // 0x18009f9e0
}
