// ============================================================================
// src/ext/tweak/ext_tweaks.cpp -- ext_tweaks.h (debug flavour only: CMake compiles this
// file only with BANISHED_TWEAK_OVERLAY on)
// ============================================================================
#include "ext/tweak/ext_tweaks.h"

#include <mutex>

namespace Ext {
namespace Tweak {

namespace {
std::mutex g_mu;
Ao         g_ao;
Faa        g_faa;
Bbs        g_bbs;
Water      g_water;
}  // namespace

Ao AoValues() {
    std::lock_guard<std::mutex> lock(g_mu);
    return g_ao;
}

void SetAo(const Ao& v) {
    std::lock_guard<std::mutex> lock(g_mu);
    g_ao = v;
}

Faa FaaValues() {
    std::lock_guard<std::mutex> lock(g_mu);
    return g_faa;
}

void SetFaa(const Faa& v) {
    std::lock_guard<std::mutex> lock(g_mu);
    g_faa = v;
}

Bbs BbsValues() {
    std::lock_guard<std::mutex> lock(g_mu);
    return g_bbs;
}

void SetBbs(const Bbs& v) {
    std::lock_guard<std::mutex> lock(g_mu);
    g_bbs = v;
}

Water WaterValues() {
    std::lock_guard<std::mutex> lock(g_mu);
    return g_water;
}

void SetWater(const Water& v) {
    std::lock_guard<std::mutex> lock(g_mu);
    g_water = v;
}

}  // namespace Tweak
}  // namespace Ext
