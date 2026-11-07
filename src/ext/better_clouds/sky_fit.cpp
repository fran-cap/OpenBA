// src/ext/better_clouds/sky_fit.cpp -- sky_fit.h (BETTERCLOUDS)
//
// NOT RETAIL. Pure byte work over the DXBC container reader of src/render/common/better_trees/dxbc.h.

#include "ext/better_clouds/sky_fit.h"

#include "better_trees/dxbc.h"   // src/render/common: Dxbc::Parse, Dxbc::FourCC

#include <cstring>

namespace Ext {
namespace BetterClouds {

namespace {

uint32_t ReadU32(const std::vector<uint8_t>& d, size_t at) {
    uint32_t v = 0;
    std::memcpy(&v, d.data() + at, sizeof(v));
    return v;
}

}  // namespace

bool ReadSignature(const uint8_t* blob, size_t size, uint32_t fourcc, std::vector<SigElement>* out) {
    out->clear();
    if (blob == nullptr || size == 0) return false;
    Dxbc::Container c;
    std::string err;
    if (!Dxbc::Parse(blob, size, &c, &err)) return false;
    const Dxbc::Chunk* chunk = c.Find(fourcc);
    if (chunk == nullptr) return false;
    const std::vector<uint8_t>& d = chunk->data;
    if (d.size() < 8) return false;
    const uint32_t count = ReadU32(d, 0);
    const uint32_t first = ReadU32(d, 4);
    constexpr size_t kElementBytes = 24;
    if (first > d.size() || count > (d.size() - first) / kElementBytes) return false;
    for (uint32_t i = 0; i < count; ++i) {
        const size_t at = first + i * kElementBytes;
        SigElement e;
        const uint32_t nameAt = ReadU32(d, at);
        e.semanticIndex = ReadU32(d, at + 4);
        e.systemValue = ReadU32(d, at + 8);
        e.componentType = ReadU32(d, at + 12);
        e.reg = ReadU32(d, at + 16);
        e.mask = d[at + 20];
        e.rwMask = d[at + 21];
        if (nameAt >= d.size()) return false;
        const void* nul = std::memchr(d.data() + nameAt, 0, d.size() - nameAt);
        if (nul == nullptr) return false;
        e.name.assign(reinterpret_cast<const char*>(d.data() + nameAt),
                      static_cast<const uint8_t*>(nul) - (d.data() + nameAt));
        out->push_back(e);
    }
    return true;
}

bool PixelFitsVertex(const uint8_t* ourPs, size_t ourPsSize, const uint8_t* installedVs, size_t installedVsSize,
                     std::string* why) {
    std::vector<SigElement> in;
    std::vector<SigElement> out;
    if (!ReadSignature(ourPs, ourPsSize, Dxbc::FourCC('I', 'S', 'G', 'N'), &in)) {
        *why = "our pixel program has no readable input signature";
        return false;
    }
    if (!ReadSignature(installedVs, installedVsSize, Dxbc::FourCC('O', 'S', 'G', 'N'), &out)) {
        *why = "the installed vertex program has no readable output signature";
        return false;
    }
    for (const SigElement& e : in) {
        const SigElement* match = nullptr;
        for (const SigElement& o : out) {
            if (o.name == e.name && o.semanticIndex == e.semanticIndex) match = &o;
        }
        const std::string label = e.name + std::to_string(e.semanticIndex);
        if (match == nullptr) {
            *why = "the installed vertex program does not write " + label;
            return false;
        }
        if (match->reg != e.reg || (e.mask & ~match->mask) != 0) {
            *why = "the installed vertex program writes " + label + " in another register or with fewer components";
            return false;
        }
    }
    why->clear();
    return true;
}

}  // namespace BetterClouds
}  // namespace Ext
