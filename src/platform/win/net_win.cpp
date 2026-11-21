// ============================================================================
// src/platform/win/net_win.cpp -- Platform::Net::HttpsGet over WinHTTP
// ============================================================================
// NOT RETAIL. The rules are in platform_net.h. The twin of src/platform/posix/net_posix.cpp.
//
// winhttp.dll is part of Windows: the system's TLS stack (Schannel) and trust store, the certificate
// chain and name verified (no SECURITY_FLAG_IGNORE_* is ever set). The session is ASYNC so that the
// calling thread can wait on one event against the call's deadline and, when it passes, close the
// request: WinHttpSetTimeouts alone bounds each phase, not their sum (resolve + connect + send +
// receive could add up to four times the timeout). The per-phase timeouts are still set, to the whole
// budget, as a backstop.
//
// Lifetime: the request's state is one heap block with two references, the waiting thread's and the
// status callback's. The callback drops its reference at HANDLE_CLOSING, the request handle's last
// notification, so a callback still running after a timeout never touches freed memory. Nothing here
// is static: the caller's thread is detached, and the process may exit while a call is in flight (the
// block is then simply never freed).
//
// Proxy: the system's automatic proxy setting (WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, Windows 8.1 and
// later: the user's proxy settings, WPAD and PAC), else the default-proxy access type (netsh winhttp)
// where that open is refused. Cookies and authentication are switched off: a 407 or 401 is returned
// as its status, nothing is answered and nothing prompts.
// ============================================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>

#include <atomic>
#include <cstdio>
#include <string>

#include "platform_net.h"

namespace Platform {
namespace Net {

namespace {

constexpr DWORD kReadChunk = 16 * 1024;

std::wstring Widen(const char* utf8) {
    if (utf8 == nullptr || utf8[0] == '\0') {
        return std::wstring();
    }
    const int n = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
    if (n <= 1) {
        return std::wstring();
    }
    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, &out[0], n);
    out.resize(static_cast<size_t>(n - 1));   // the terminator MultiByteToWideChar counted
    return out;
}

const char* ErrorName(DWORD error) {
    switch (error) {
    case ERROR_WINHTTP_TIMEOUT:
        return "timeout";
    case ERROR_WINHTTP_NAME_NOT_RESOLVED:
        return "name not resolved";
    case ERROR_WINHTTP_CANNOT_CONNECT:
        return "cannot connect";
    case ERROR_WINHTTP_CONNECTION_ERROR:
        return "connection error";
    case ERROR_WINHTTP_SECURE_FAILURE:
        return "TLS / certificate failure";
    case ERROR_WINHTTP_OPERATION_CANCELLED:
        return "cancelled";
    case ERROR_WINHTTP_INVALID_SERVER_RESPONSE:
        return "invalid server response";
    case ERROR_WINHTTP_REDIRECT_FAILED:
        return "redirect failed";
    case ERROR_INVALID_PARAMETER:
        return "invalid parameter";
    default:
        return "error";
    }
}

// Where the request is, for the timeout's log line. Written by the callback, read by the waiter.
enum Phase : int {
    kPhaseSend = 0,      // WinHttpSendRequest: proxy detection, resolve, connect, TLS, the request
    kPhaseReceive = 1,   // WinHttpReceiveResponse: waiting for the response headers
    kPhaseRead = 2,      // WinHttpReadData: the body
};

const char* PhaseName(int phase) {
    switch (phase) {
    case kPhaseReceive:
        return "the response headers";
    case kPhaseRead:
        return "the body";
    default:
        // WinHTTP may hold SENDREQUEST_COMPLETE until the server starts answering (seen with a
        // server that delays its headers), so this phase covers a slow answer too.
        return "resolve / connect / TLS / send / the first response bytes";
    }
}

struct Request {
    std::atomic<int> refs{2};          // the waiting thread's and the status callback's
    std::atomic<bool> finished{false}; // Finish ran (or the waiter gave up first)
    std::atomic<int> phase{kPhaseSend};
    HANDLE done = nullptr;             // set by Finish
    HINTERNET handle = nullptr;        // the request handle
    size_t maxBytes = 0;
    // Written by the callback before `done` is set; read by the waiter only after it.
    int status = 0;
    bool ok = false;
    const char* failedStep = nullptr;
    DWORD error = 0;
    DWORD secureFlags = 0;             // WINHTTP_CALLBACK_STATUS_FLAG_* of a SECURE_FAILURE
    std::string body;
    char buffer[kReadChunk];
};

void Release(Request* r) {
    if (r->refs.fetch_sub(1) == 1) {
        if (r->done != nullptr) {
            CloseHandle(r->done);
        }
        delete r;
    }
}

// The callback's chain ends here, once: success or the failed step.
void Finish(Request* r, bool ok, const char* failedStep, DWORD error) {
    if (r->finished.exchange(true)) {
        return;
    }
    r->ok = ok;
    r->failedStep = failedStep;
    r->error = error;
    SetEvent(r->done);
}

// One read into the buffer, never past maxBytes. With the cap reached the call is complete: the rest
// of the body is dropped when the handle closes.
void ReadNext(Request* r) {
    const size_t room = r->maxBytes - r->body.size();
    if (room == 0) {
        Finish(r, true, nullptr, 0);
        return;
    }
    const DWORD want = room < kReadChunk ? static_cast<DWORD>(room) : kReadChunk;
    // The completion may arrive inside this call (READ_COMPLETE, on this thread): nothing may follow.
    if (!WinHttpReadData(r->handle, r->buffer, want, nullptr)) {
        Finish(r, false, "WinHttpReadData", GetLastError());
    }
}

void CALLBACK OnStatus(HINTERNET /*handle*/, DWORD_PTR context, DWORD code, LPVOID info, DWORD infoLength) {
    Request* r = reinterpret_cast<Request*>(context);
    if (r == nullptr) {
        return;
    }
    switch (code) {
    case WINHTTP_CALLBACK_STATUS_SECURE_FAILURE:
        if (info != nullptr && infoLength >= sizeof(DWORD)) {
            r->secureFlags = *static_cast<const DWORD*>(info);
        }
        break;
    case WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE:
        r->phase.store(kPhaseReceive);
        if (!WinHttpReceiveResponse(r->handle, nullptr)) {
            Finish(r, false, "WinHttpReceiveResponse", GetLastError());
        }
        break;
    case WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE: {
        DWORD status = 0;
        DWORD size = sizeof(status);
        if (!WinHttpQueryHeaders(r->handle, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                 WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX)) {
            Finish(r, false, "WinHttpQueryHeaders", GetLastError());
            break;
        }
        r->status = static_cast<int>(status);
        r->phase.store(kPhaseRead);
        ReadNext(r);
        break;
    }
    case WINHTTP_CALLBACK_STATUS_READ_COMPLETE:
        if (infoLength == 0) {
            Finish(r, true, nullptr, 0);   // the end of the body
            break;
        }
        r->body.append(r->buffer, infoLength);
        ReadNext(r);
        break;
    case WINHTTP_CALLBACK_STATUS_REQUEST_ERROR: {
        const WINHTTP_ASYNC_RESULT* result = static_cast<const WINHTTP_ASYNC_RESULT*>(info);
        const char* step = "the request";
        DWORD error = 0;
        if (result != nullptr) {
            error = result->dwError;
            switch (result->dwResult) {
            case API_SEND_REQUEST:
                step = "WinHttpSendRequest";
                break;
            case API_RECEIVE_RESPONSE:
                step = "WinHttpReceiveResponse";
                break;
            case API_READ_DATA:
                step = "WinHttpReadData";
                break;
            default:
                break;
            }
        }
        Finish(r, false, step, error);
        break;
    }
    case WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING:
        // The request handle's last notification: the callback's reference goes.
        Release(r);
        break;
    default:
        break;
    }
}

void LogFailure(const char* host, const char* step, DWORD error, DWORD secureFlags) {
    if (secureFlags != 0) {
        std::fprintf(stderr, "[Net] HttpsGet %s: %s failed, error %lu (%s, certificate flags 0x%08lx)\n", host,
                     step, static_cast<unsigned long>(error), ErrorName(error),
                     static_cast<unsigned long>(secureFlags));
        return;
    }
    std::fprintf(stderr, "[Net] HttpsGet %s: %s failed, error %lu (%s)\n", host, step,
                 static_cast<unsigned long>(error), ErrorName(error));
}

HINTERNET OpenSession(const std::wstring& agent) {
    HINTERNET session = WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC);
    if (session == nullptr) {
        // Before Windows 8.1 the automatic-proxy access type is refused (ERROR_INVALID_PARAMETER).
        session = WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                              WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC);
    }
    return session;
}

}  // namespace

bool CanHttpsGet() {
    return WinHttpCheckPlatform() != FALSE;
}

int HttpsGet(const char* host, const char* path, const char* userAgent, int timeoutMs, size_t maxBytes,
             std::string& body) {
    body.clear();
    const char* hostName = host != nullptr ? host : "";
    const int budget = timeoutMs > 0 ? timeoutMs : 1;
    const ULONGLONG deadline = GetTickCount64() + static_cast<ULONGLONG>(budget);

    const std::wstring agentW = Widen(userAgent);
    const std::wstring hostW = Widen(host);
    const std::wstring pathW = Widen(path);

    HINTERNET session = OpenSession(agentW);
    if (session == nullptr) {
        LogFailure(hostName, "WinHttpOpen", GetLastError(), 0);
        return 0;
    }
    WinHttpSetTimeouts(session, budget, budget, budget, budget);

    HINTERNET connect = WinHttpConnect(session, hostW.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (connect == nullptr) {
        LogFailure(hostName, "WinHttpConnect", GetLastError(), 0);
        WinHttpCloseHandle(session);
        return 0;
    }
    HINTERNET handle = WinHttpOpenRequest(connect, L"GET", pathW.c_str(), nullptr, WINHTTP_NO_REFERER,
                                          WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (handle == nullptr) {
        LogFailure(hostName, "WinHttpOpenRequest", GetLastError(), 0);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return 0;
    }
    // Nothing is sent but the request and the User-Agent: no cookies, no answer to an auth challenge.
    DWORD disable = WINHTTP_DISABLE_COOKIES | WINHTTP_DISABLE_AUTHENTICATION;
    WinHttpSetOption(handle, WINHTTP_OPTION_DISABLE_FEATURE, &disable, sizeof(disable));
    DWORD autologon = WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH;
    WinHttpSetOption(handle, WINHTTP_OPTION_AUTOLOGON_POLICY, &autologon, sizeof(autologon));

    Request* r = new Request();
    r->handle = handle;
    r->maxBytes = maxBytes;
    r->done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    DWORD_PTR context = reinterpret_cast<DWORD_PTR>(r);
    const DWORD notifications = WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS | WINHTTP_CALLBACK_FLAG_HANDLES
                                | WINHTTP_CALLBACK_FLAG_SECURE_FAILURE;
    bool callbackSet = false;
    bool started = false;
    if (r->done == nullptr) {
        LogFailure(hostName, "CreateEvent", GetLastError(), 0);
    } else if (!WinHttpSetOption(handle, WINHTTP_OPTION_CONTEXT_VALUE, &context, sizeof(context))) {
        LogFailure(hostName, "WinHttpSetOption(context)", GetLastError(), 0);
    } else if (WinHttpSetStatusCallback(handle, OnStatus, notifications, 0)
               == WINHTTP_INVALID_STATUS_CALLBACK) {
        LogFailure(hostName, "WinHttpSetStatusCallback", GetLastError(), 0);
    } else {
        callbackSet = true;
        if (!WinHttpSendRequest(handle, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0,
                                context)) {
            LogFailure(hostName, "WinHttpSendRequest", GetLastError(), 0);
        } else {
            started = true;
        }
    }

    int status = 0;
    if (started) {
        const ULONGLONG now = GetTickCount64();
        const DWORD wait = now < deadline ? static_cast<DWORD>(deadline - now) : 0;
        bool timedOut = WaitForSingleObject(r->done, wait) != WAIT_OBJECT_0;
        if (timedOut && r->finished.exchange(true)) {
            // Finish claimed the result as the wait ran out: it sets the event right after.
            WaitForSingleObject(r->done, INFINITE);
            timedOut = false;
        }
        if (timedOut) {
            std::fprintf(stderr, "[Net] HttpsGet %s: timed out after %d ms waiting for %s\n", hostName, budget,
                         PhaseName(r->phase.load()));
        } else if (!r->ok) {
            LogFailure(hostName, r->failedStep, r->error, r->secureFlags);
        } else {
            status = r->status;
            body.swap(r->body);
        }
    }

    // Closing the request ends any operation still in flight; its HANDLE_CLOSING drops the
    // callback's reference, possibly on another thread and after this function returned.
    WinHttpCloseHandle(handle);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    if (!callbackSet) {
        Release(r);   // no callback was registered, so no HANDLE_CLOSING will drop its reference
    }
    Release(r);
    return status;
}

}  // namespace Net
}  // namespace Platform
