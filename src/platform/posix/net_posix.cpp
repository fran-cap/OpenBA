// ============================================================================
// src/platform/posix/net_posix.cpp -- Platform::Net::HttpsGet over the system's libcurl
// ============================================================================
// NOT RETAIL. The rules are in platform_net.h. The twin of src/platform/win/net_win.cpp.
//
// The system's own libcurl is opened at run time (dlopen) and its easy interface resolved by name,
// so the build needs no curl headers, the exe gains no link-time dependency and no package ships a
// TLS stack: libcurl brings the system's TLS library and trust store. Without the library
// CanHttpsGet() is false. The names tried, in order:
//   /usr/lib/libcurl.4.dylib   macOS (part of the OS; an absolute path, so the working directory is
//                              never searched for it; absent elsewhere, where the open just fails)
//   libcurl.so.4               Linux, the OpenSSL build (Debian / Ubuntu libcurl4, Fedora, Arch...)
//   libcurl-gnutls.so.4        Linux, Debian / Ubuntu's GnuTLS build (libcurl3-gnutls)
// Android has none of them: the open fails and the answer is "not supported" until its own twin.
//
// The few prototypes and numbers below are libcurl's stable ABI (curl/curl.h: CURLOPTTYPE_LONG 0,
// _OBJECTPOINT 10000, _FUNCTIONPOINT 20000; CURLINFO_LONG 0x200000), declared here instead of
// included.
//
// Lifetime: the library is opened once and never closed, curl_global_init is called once and
// curl_global_cleanup never, and nothing here has a destructor, because the caller is a detached
// thread the process may leave running at exit.
// ============================================================================

#include "platform_net.h"

#include <cstdio>
#include <dlfcn.h>
#include <string>

namespace Platform {
namespace Net {

namespace {

// curl/curl.h, by value.
constexpr int kCurlGlobalDefault = 3;   // CURL_GLOBAL_SSL | CURL_GLOBAL_WIN32
constexpr int kOptWriteData = 10001;
constexpr int kOptUrl = 10002;
constexpr int kOptUserAgent = 10018;
constexpr int kOptWriteFunction = 20011;
constexpr int kOptNetrc = 51;
constexpr int kOptFollowLocation = 52;
constexpr int kOptNoSignal = 99;
constexpr int kOptTimeoutMs = 155;
constexpr int kOptConnectTimeoutMs = 156;
constexpr int kOptProtocols = 181;          // deprecated in 7.85 for _STR; still honoured
constexpr int kOptProtocolsStr = 10318;     // 7.85+
constexpr int kInfoResponseCode = 0x200002;
constexpr long kProtoHttps = 1 << 1;        // CURLPROTO_HTTPS
constexpr long kNetrcIgnored = 0;           // CURL_NETRC_IGNORED

constexpr int kCurlOk = 0;
constexpr int kCurlCouldntResolveProxy = 5;
constexpr int kCurlCouldntResolveHost = 6;
constexpr int kCurlCouldntConnect = 7;
constexpr int kCurlWriteError = 23;
constexpr int kCurlTimedOut = 28;
constexpr int kCurlSslConnectError = 35;
constexpr int kCurlPeerFailedVerification = 60;
constexpr int kCurlSslCaCertBadFile = 77;

using WriteFn = size_t (*)(char* data, size_t size, size_t count, void* user);

struct Curl {
    int (*globalInit)(long flags);
    void* (*easyInit)();
    int (*easySetopt)(void* handle, int option, ...);
    int (*easyPerform)(void* handle);
    int (*easyGetinfo)(void* handle, int info, ...);
    void (*easyCleanup)(void* handle);
    const char* (*easyStrerror)(int code);
};

// Opened once, by whichever thread asks first (a function-local static: the C++ runtime makes its
// initialisation thread-safe, which also serialises curl_global_init, not thread-safe before 7.84).
// Trivially destructible, so no exit handler touches it.
const Curl* Library() {
    static Curl curl;
    static const Curl* const loaded = []() -> const Curl* {
        static const char* const kNames[] = {
            "/usr/lib/libcurl.4.dylib",
            "libcurl.so.4",
            "libcurl-gnutls.so.4",
        };
        void* lib = nullptr;
        for (const char* name : kNames) {
            lib = dlopen(name, RTLD_NOW | RTLD_LOCAL);
            if (lib != nullptr) break;
        }
        if (lib == nullptr) return nullptr;
        curl.globalInit = reinterpret_cast<int (*)(long)>(dlsym(lib, "curl_global_init"));
        curl.easyInit = reinterpret_cast<void* (*)()>(dlsym(lib, "curl_easy_init"));
        curl.easySetopt = reinterpret_cast<int (*)(void*, int, ...)>(dlsym(lib, "curl_easy_setopt"));
        curl.easyPerform = reinterpret_cast<int (*)(void*)>(dlsym(lib, "curl_easy_perform"));
        curl.easyGetinfo = reinterpret_cast<int (*)(void*, int, ...)>(dlsym(lib, "curl_easy_getinfo"));
        curl.easyCleanup = reinterpret_cast<void (*)(void*)>(dlsym(lib, "curl_easy_cleanup"));
        curl.easyStrerror = reinterpret_cast<const char* (*)(int)>(dlsym(lib, "curl_easy_strerror"));
        if (curl.globalInit == nullptr || curl.easyInit == nullptr || curl.easySetopt == nullptr ||
            curl.easyPerform == nullptr || curl.easyGetinfo == nullptr || curl.easyCleanup == nullptr ||
            curl.easyStrerror == nullptr) {
            std::fprintf(stderr, "[Net] libcurl: a curl_easy_* symbol is missing\n");
            return nullptr;
        }
        if (curl.globalInit(kCurlGlobalDefault) != kCurlOk) {
            std::fprintf(stderr, "[Net] libcurl: curl_global_init failed\n");
            return nullptr;
        }
        return &curl;
    }();
    return loaded;
}

struct Sink {
    std::string* body;
    size_t maxBytes;
    bool full;   // the cap was reached and the transfer stopped on purpose
};

// Keeps the first maxBytes bytes. Once they are in, answering short stops the transfer
// (CURLE_WRITE_ERROR), which HttpsGet then counts as a read response.
size_t Write(char* data, size_t size, size_t count, void* user) {
    Sink* sink = static_cast<Sink*>(user);
    const size_t bytes = size * count;
    const size_t room = sink->maxBytes - sink->body->size();
    if (bytes <= room) {
        sink->body->append(data, bytes);
        if (bytes < room) return bytes;
        sink->full = true;
        return bytes;
    }
    sink->body->append(data, room);
    sink->full = true;
    return 0;
}

const char* FailedStep(int code) {
    switch (code) {
        case kCurlCouldntResolveProxy:
        case kCurlCouldntResolveHost:
            return "resolve";
        case kCurlCouldntConnect:
            return "connect";
        case kCurlTimedOut:
            return "timeout";
        case kCurlSslConnectError:
        case kCurlPeerFailedVerification:
        case kCurlSslCaCertBadFile:
            return "tls";
        default:
            return "transfer";
    }
}

}  // namespace

bool CanHttpsGet() {
    return Library() != nullptr;
}

int HttpsGet(const char* host, const char* path, const char* userAgent, int timeoutMs, size_t maxBytes,
             std::string& body) {
    body.clear();
    const Curl* curl = Library();
    if (curl == nullptr) return 0;
    void* handle = curl->easyInit();
    if (handle == nullptr) {
        std::fprintf(stderr, "[Net] https get: curl_easy_init failed\n");
        return 0;
    }

    std::string url = "https://";
    url += host;
    url += path;
    // 0 would mean "no limit" to libcurl.
    const long timeout = timeoutMs > 0 ? static_cast<long>(timeoutMs) : 1L;
    Sink sink{&body, maxBytes, false};
    WriteFn write = &Write;

    // Certificate and host-name verification stay at libcurl's defaults (on). No cookie engine is
    // started, redirects are not followed, ~/.netrc is not read.
    int setup = curl->easySetopt(handle, kOptUrl, url.c_str());
    if (setup == kCurlOk && userAgent != nullptr) setup = curl->easySetopt(handle, kOptUserAgent, userAgent);
    if (setup == kCurlOk) setup = curl->easySetopt(handle, kOptNoSignal, 1L);
    if (setup == kCurlOk) setup = curl->easySetopt(handle, kOptTimeoutMs, timeout);
    if (setup == kCurlOk) setup = curl->easySetopt(handle, kOptConnectTimeoutMs, timeout);
    if (setup == kCurlOk) setup = curl->easySetopt(handle, kOptFollowLocation, 0L);
    if (setup == kCurlOk) setup = curl->easySetopt(handle, kOptNetrc, kNetrcIgnored);
    if (setup == kCurlOk && curl->easySetopt(handle, kOptProtocolsStr, "https") != kCurlOk) {
        setup = curl->easySetopt(handle, kOptProtocols, kProtoHttps);
    }
    if (setup == kCurlOk) setup = curl->easySetopt(handle, kOptWriteFunction, write);
    if (setup == kCurlOk) setup = curl->easySetopt(handle, kOptWriteData, static_cast<void*>(&sink));
    if (setup != kCurlOk) {
        std::fprintf(stderr, "[Net] https get: setup failed (curl %d: %s)\n", setup, curl->easyStrerror(setup));
        curl->easyCleanup(handle);
        return 0;
    }

    const int result = curl->easyPerform(handle);
    long status = 0;
    if (result == kCurlOk || (result == kCurlWriteError && sink.full)) {
        if (curl->easyGetinfo(handle, kInfoResponseCode, &status) != kCurlOk) status = 0;
    } else {
        std::fprintf(stderr, "[Net] https get: %s failed (curl %d: %s)\n", FailedStep(result), result,
                     curl->easyStrerror(result));
    }
    curl->easyCleanup(handle);
    if (status <= 0) {
        body.clear();
        return 0;
    }
    return static_cast<int>(status);
}

}  // namespace Net
}  // namespace Platform
