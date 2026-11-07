// src/game/Game/terrain_sidecar.cpp -- see terrain_sidecar.h.
#include "terrain_sidecar.h"
#include "Game/Map.h"          // MapCell
#include <cstdio>
#include <cstdlib>

namespace TerrainSidecar {

uint8_t*       g_mirror      = nullptr;
const MapCell* g_mirrorBase  = nullptr;
int            g_mirrorCount = 0;

namespace {
uint8_t* s_buf    = nullptr;   // owned allocation, reused across rebuilds
int      s_bufCap = 0;
int32_t  s_rebuilds = 0, s_writes = 0, s_verified = 0, s_mismatch = 0;

void Rebuild(const MapCell* cells, int count) {
    if (count > s_bufCap) {
        std::free(s_buf);
        s_buf = static_cast<uint8_t*>(std::malloc((size_t)count));
        s_bufCap = s_buf ? count : 0;
    }
    if (!s_buf) { g_mirror = nullptr; g_mirrorBase = nullptr; g_mirrorCount = 0; return; }
    for (int i = 0; i < count; ++i) s_buf[i] = static_cast<uint8_t>(cells[(size_t)i].terrainType);
    g_mirror = s_buf; g_mirrorBase = cells; g_mirrorCount = count;
    ++s_rebuilds;
}
}  // namespace

uint8_t GetSlow(const MapCell* cells, int count, int idx) {
    const uint8_t raw = static_cast<uint8_t>(cells[(size_t)idx].terrainType);
    if (!cells || count <= 0 || (unsigned)idx >= (unsigned)count) return raw;
    if (cells != g_mirrorBase || count != g_mirrorCount) Rebuild(cells, count);
    if (!g_mirror) return raw;
    if (g_verifyOn) {
        // COMPPROF=4: the real byte is what the faithful read returns; the mirror must agree.
        ++s_verified;
        const bool ok = g_mirror[idx] == raw;
        StressHarness_Verify("TerrainSidecar", ok, ok ? 0 : idx);
        if (!ok) {
            ++s_mismatch;
            g_mirror[idx] = raw;   // self-heal so one stale byte is counted once
        }
        return raw;
    }
    return g_mirror[idx];
}

void NoteWrite(const MapCell* cellPtr) {
    if (!g_mirror || !cellPtr) return;
    const MapCell* base = g_mirrorBase;
    if (cellPtr < base) return;
    const size_t idx = static_cast<size_t>(cellPtr - base);
    if (idx >= (size_t)g_mirrorCount) return;
    g_mirror[idx] = static_cast<uint8_t>(cellPtr->terrainType);
    ++s_writes;
}

void Invalidate() {
    g_mirror = nullptr; g_mirrorBase = nullptr; g_mirrorCount = 0;
}

void Report() {
    if (!s_verified) return;
    std::fprintf(stderr, "[TerrainSidecar] rebuilds=%d writes=%d verified=%d mismatches=%d\n",
                 s_rebuilds, s_writes, s_verified, s_mismatch);
}

}  // namespace TerrainSidecar
