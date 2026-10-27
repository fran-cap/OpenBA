// ============================================================================
// src/platform/common/cpu/roster.cpp -- the thread-count rules and the `sys.cpu_limit` mask
// ============================================================================
// NOT RETAIL. roster.h has the contract and the table.
// ============================================================================
#include "platform/common/cpu/roster.h"

namespace Cpu {

namespace {

int Clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

bool IsSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

int HexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

}  // namespace

uint64_t BigCoreMask(const int* capacity, uint64_t mask) {
    int largest = 0;
    for (int i = 0; i < 64; ++i) {
        if (((mask >> i) & 1u) == 0) continue;
        if (capacity[i] <= 0) return mask;   // unknown: every core counts
        if (capacity[i] > largest) largest = capacity[i];
    }
    if (largest == 0) return mask;
    uint64_t big = 0;
    for (int i = 0; i < 64; ++i) {
        if (((mask >> i) & 1u) == 0) continue;
        if (capacity[i] * 2 > largest) big |= (uint64_t{1} << i);
    }
    return big != 0 ? big : mask;
}

bool RenderThread(int physicalCores) { return physicalCores >= 3; }

int PathWorkers(int physicalCores, bool renderPipe) {
    return Clamp(physicalCores - 2 - (renderPipe ? 1 : 0), 1, kMaxPathWorkers);
}

int CullHelpers(int physicalCores, bool renderPipe) {
    return Clamp(physicalCores - (renderPipe ? 5 : 4), 0, kMaxCullHelpers);
}

int ParallelWorkers(int physicalCores) {
    return Clamp(physicalCores - 1, 0, kMaxParallelWorkers);
}

bool ParseMask(const std::string& text, uint64_t* mask) {
    size_t b = 0;
    size_t e = text.size();
    while (b < e && IsSpace(text[b])) ++b;
    while (e > b && IsSpace(text[e - 1])) --e;
    if (b == e) return false;
    uint64_t v = 0;
    if (e - b > 2 && text[b] == '0' && (text[b + 1] == 'x' || text[b + 1] == 'X')) {
        if (e - b - 2 > 16) return false;
        for (size_t i = b + 2; i < e; ++i) {
            const int d = HexDigit(text[i]);
            if (d < 0) return false;
            v = (v << 4) | static_cast<uint64_t>(d);
        }
    } else {
        for (size_t i = b; i < e; ++i) {
            const char c = text[i];
            if (c < '0' || c > '9') return false;
            const uint64_t d = static_cast<uint64_t>(c - '0');
            if (v > (UINT64_MAX - d) / 10u) return false;   // overflow
            v = v * 10u + d;
        }
    }
    *mask = v;
    return true;
}

int MaskPopcount(uint64_t mask) {
    int n = 0;
    for (; mask != 0; mask &= mask - 1) ++n;
    return n;
}

const char* LimitName(Limit l) {
    switch (l) {
    case Limit::Applied: return "applied";
    case Limit::Clamped: return "clamped";
    case Limit::None:    break;
    }
    return "none";
}

LimitDecision DecideLimit(bool present, const std::string& value, uint64_t systemMask) {
    LimitDecision d;
    if (!present) return d;
    uint64_t m = 0;
    if (!ParseMask(value, &m)) {
        d.junk = true;
        return d;
    }
    d.requested = m;
    if (m == 0) return d;
    const uint64_t inside = m & systemMask;
    if (inside == 0) {
        d.empty = true;
        return d;
    }
    d.apply = inside;
    d.limit = inside == m ? Limit::Applied : Limit::Clamped;
    return d;
}

}  // namespace Cpu
