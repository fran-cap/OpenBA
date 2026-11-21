// ============================================================================
// src/ext/update_check/update_check.cpp -- ext.update_check: the daily request and the menu's notice
// ============================================================================
// NOT RETAIL. The rules are in update_check.h; the pure ones in release_tag.h.
// ============================================================================

#include "ext/update_check/update_check.h"
#include "ext/update_check/release_tag.h"
#include "ext/registry.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <string>
#include <thread>

#include "io/Persistence.h"
#include "platform_net.h"

namespace Ext {
namespace UpdateCheck {

namespace {

constexpr const char* kHost = "api.github.com";
constexpr const char* kPath = "/repos/fran-cap/OpenBA/releases/latest";
constexpr const char* kUserAgent = "OpenBA/" OPENBA_VERSION;
constexpr int kTimeoutMs = 5000;
constexpr size_t kMaxAnswerBytes = 1024 * 1024;

constexpr const char* kKeyDay = "update_check_day";
constexpr const char* kKeySeen = "update_check_seen";

// The worker's answer. kPending until it stores one; kNoAnswer when no response was read (asked
// again at the next start); otherwise the packed version, kNoVersion for a response without one.
constexpr int32_t kPending = -1;
constexpr int32_t kNoAnswer = -2;
std::atomic<int32_t> g_answer{kPending};

// Main thread only.
bool g_asked = false;       // the worker was started this run
bool g_collected = false;   // its answer was recorded (or was kNoAnswer)
int32_t g_seen = kNoVersion;

int32_t Today() {
    return static_cast<int32_t>(std::time(nullptr) / 86400);
}

void Worker() {
    std::string body;
    const int status = Platform::Net::HttpsGet(kHost, kPath, kUserAgent, kTimeoutMs, kMaxAnswerBytes, body);
    if (status == 0) {
        g_answer.store(kNoAnswer);
        return;
    }
    g_answer.store(status == 200 ? LatestFromAnswer(body) : kNoVersion);
}

// A finished answer into the settings map and the file, once.
void Collect() {
    if (!g_asked || g_collected) return;
    const int32_t answer = g_answer.load();
    if (answer == kPending) return;
    g_collected = true;
    if (answer == kNoAnswer) {
        std::fprintf(stderr, "[UpdateCheck] no answer; asking again at the next start\n");
        return;
    }
    ::IO::Persistence* p = ::IO::Persistence::_singleton;
    if (p == nullptr) return;
    p->Set(KeyPrefix(Tier::Extension), kKeyDay, static_cast<int>(Today()));
    if (answer != kNoVersion) {
        g_seen = answer;
        p->Set(KeyPrefix(Tier::Extension), kKeySeen, static_cast<int>(answer));
    }
    PersistSettings();
    std::fprintf(stderr, "[UpdateCheck] newest release %s, this exe " OPENBA_VERSION "\n",
                 answer != kNoVersion ? VersionText(answer).c_str() : "unknown");
}

}  // namespace

void Boot() {
    if (!Ext().update_check) return;
    int day = 0;
    int seen = 0;
    if (const ::IO::Persistence* p = ::IO::Persistence::_singleton) {
        p->Get(KeyPrefix(Tier::Extension), kKeyDay, day);
        p->Get(KeyPrefix(Tier::Extension), kKeySeen, seen);
    }
    g_seen = static_cast<int32_t>(seen);
    if (!DueToday(static_cast<int32_t>(day), Today())) return;
    if (!Platform::Net::CanHttpsGet()) {
        std::fprintf(stderr, "[UpdateCheck] not supported on this platform\n");
        return;
    }
    g_asked = true;
    std::thread(Worker).detach();
}

std::string MenuNotice() {
    if (!Ext().update_check) return std::string();
    Collect();
    return NoticeSuffix(PackVersion(OPENBA_VERSION), g_seen);
}

}  // namespace UpdateCheck
}  // namespace Ext
