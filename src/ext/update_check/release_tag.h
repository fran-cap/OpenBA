// ============================================================================
// src/ext/update_check/release_tag.h -- ext.update_check: the pure rules
// ============================================================================
// NOT RETAIL. docs/extensions/handins/UPDATECHECK.md. Standard C++ only, so
// tests/unit/ext_update_check_test.cpp links it without the world.
// ============================================================================
#pragma once

#include <cstdint>
#include <string>

namespace Ext {
namespace UpdateCheck {

// A release version as one comparable number: major * 1000000 + minor * 1000 + patch, each part
// 0..999. It is also the form kept in settings.cfg (ext.update_check_seen), whose values are ints.
constexpr int32_t kNoVersion = 0;

// "1.0.4" or "v1.0.4" (a release tag) packed as above. Exactly three decimal parts of 1..3 digits
// each, nothing after them: anything else (a pre-release suffix, two parts, a blank) is kNoVersion,
// and so is 0.0.0.
inline int32_t PackVersion(const char* text) {
    if (text == nullptr) return kNoVersion;
    const char* p = text;
    if (*p == 'v') ++p;
    int32_t parts[3] = { 0, 0, 0 };
    for (int i = 0; i < 3; ++i) {
        int digits = 0;
        while (*p >= '0' && *p <= '9') {
            if (++digits > 3) return kNoVersion;
            parts[i] = parts[i] * 10 + (*p - '0');
            ++p;
        }
        if (digits == 0) return kNoVersion;
        if (i < 2) {
            if (*p != '.') return kNoVersion;
            ++p;
        }
    }
    if (*p != '\0') return kNoVersion;
    return parts[0] * 1000000 + parts[1] * 1000 + parts[2];
}

// "1.0.4" for a packed version; "" for kNoVersion or a number PackVersion never makes.
inline std::string VersionText(int32_t packed) {
    if (packed <= 0 || packed > 999999999) return std::string();
    return std::to_string(packed / 1000000) + "." + std::to_string(packed / 1000 % 1000) + "." +
           std::to_string(packed % 1000);
}

// The value of the first "tag_name" member in a releases/latest answer (a JSON object), packed;
// kNoVersion when there is none or its value is not a release version. Not a JSON parser: it finds
// the quoted key that is not inside a string's escaped quote (`\"tag_name\"` in the release notes
// is skipped), then the string after its colon. The key precedes the notes in the answer.
inline int32_t LatestFromAnswer(const std::string& body) {
    static const char kKey[] = "\"tag_name\"";
    const size_t keyLen = sizeof(kKey) - 1;
    size_t at = 0;
    for (;;) {
        at = body.find(kKey, at);
        if (at == std::string::npos) return kNoVersion;
        if (at == 0 || body[at - 1] != '\\') break;
        at += keyLen;
    }
    size_t p = at + keyLen;
    while (p < body.size() && (body[p] == ' ' || body[p] == '\t' || body[p] == '\r' || body[p] == '\n')) ++p;
    if (p >= body.size() || body[p] != ':') return kNoVersion;
    ++p;
    while (p < body.size() && (body[p] == ' ' || body[p] == '\t' || body[p] == '\r' || body[p] == '\n')) ++p;
    if (p >= body.size() || body[p] != '"') return kNoVersion;
    ++p;
    const size_t end = body.find('"', p);
    if (end == std::string::npos || end - p > 16) return kNoVersion;
    return PackVersion(body.substr(p, end - p).c_str());
}

// Whether a launch asks the server: once per calendar day (UTC days since 1970). A clock set back
// (today < lastDay) asks again, so a wrong date cannot silence the check for good.
inline bool DueToday(int32_t lastDay, int32_t today) {
    return lastDay != today;
}

// The text the menu's build line gains: " - v1.0.5 available" when `latest` is newer than
// `current`, "" otherwise (the same version, an older one, or either unknown).
inline std::string NoticeSuffix(int32_t current, int32_t latest) {
    if (current == kNoVersion || latest == kNoVersion || latest <= current) return std::string();
    return " - v" + VersionText(latest) + " available";
}

}  // namespace UpdateCheck
}  // namespace Ext
