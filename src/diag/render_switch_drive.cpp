// src/diag/render_switch_drive.cpp -- BANISHED_RENDER_SWITCH (render_switch_drive.h has the grammar)
#include "render_switch_drive.h"

#include "ext/render_backend/render_backend.h"
#include "options.h"
#include "render_backend.h"

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace RenderSwitchDrive {

namespace {

struct Switch {
    int frame = 0;
    std::string backend;
    bool done = false;
};

struct Plan {
    std::vector<Switch> switches;
    std::string failCreate;   // empty: none
    bool failTaken = false;
    std::vector<std::string> failSwitch;   // failswitch=: backends whose next in-game CreateDevice fails, once each
    bool ticked = false;      // the game loop has run: the boot is over
};

Plan& Parsed() {
    static Plan plan = [] {
        Plan p;
        const char* v = Opt().renderSwitch;
        if (v == nullptr || *v == '\0') return p;
        std::string all(v);
        size_t start = 0;
        while (start <= all.size()) {
            size_t end = all.find(',', start);
            if (end == std::string::npos) end = all.size();
            const std::string tok = all.substr(start, end - start);
            start = end + 1;
            if (tok.empty()) continue;
            if (tok.compare(0, 11, "failcreate=") == 0) {
                p.failCreate = tok.substr(11);
                continue;
            }
            if (tok.compare(0, 11, "failswitch=") == 0) {
                const std::string list = tok.substr(11);
                size_t from = 0;
                while (from <= list.size()) {
                    size_t plus = list.find('+', from);
                    if (plus == std::string::npos) plus = list.size();
                    if (plus > from) p.failSwitch.push_back(list.substr(from, plus - from));
                    from = plus + 1;
                }
                continue;
            }
            const size_t colon = tok.find(':');
            if (colon == std::string::npos) {
                std::fprintf(stderr, "[RenderSwitch] token '%s' ignored (want <frame>:<backend>)\n", tok.c_str());
                continue;
            }
            Switch s;
            s.frame = std::atoi(tok.substr(0, colon).c_str());
            s.backend = tok.substr(colon + 1);
            p.switches.push_back(s);
        }
        std::fprintf(stderr, "[RenderSwitch] %zu switch(es)%s%s\n", p.switches.size(),
                     p.failCreate.empty() ? "" : ", failcreate=", p.failCreate.c_str());
        return p;
    }();
    return plan;
}

}  // namespace

void Tick(int frameNum) {
    if (Opt().renderSwitch == nullptr) return;
    Parsed().ticked = true;
    Ext::RenderBackend::RunPendingSwitch();   // a switch asked for while the window was minimised
    for (Switch& s : Parsed().switches) {
        if (s.done || frameNum < s.frame) continue;
        s.done = true;
        const int index = Render::FindBackend(s.backend.c_str());
        std::fprintf(stderr, "[RenderSwitch] frame %d: switch to %s (index %d, active %s)\n", frameNum,
                     s.backend.c_str(), index, Render::BackendId(Render::ActiveBackend()));
        if (index < 0) continue;
        const bool ok = Ext::RenderBackend::SwitchNow(index);
        std::fprintf(stderr, "[RenderSwitch] frame %d: %s, active %s\n", frameNum, ok ? "switched" : "NOT switched",
                     Render::BackendId(Render::ActiveBackend()));
        return;   // one restart per frame
    }
}

bool TakeFailCreate(const char* backendId, bool deviceStartup) {
    if (Opt().renderSwitch == nullptr || backendId == nullptr || !deviceStartup) return false;
    Plan& p = Parsed();
    if (p.ticked) {
        if (Ext::RenderBackend::ListingAdapters()) return false;   // the listing's throwaway DeviceData
        for (size_t i = 0; i < p.failSwitch.size(); ++i) {
            if (p.failSwitch[i] != backendId) continue;
            p.failSwitch.erase(p.failSwitch.begin() + static_cast<std::ptrdiff_t>(i));
            std::fprintf(stderr, "[RenderSwitch] failswitch: %s's CreateDevice fails once (injected)\n", backendId);
            return true;
        }
        return false;
    }
    if (p.failTaken || p.failCreate.empty() || p.failCreate != backendId) return false;
    p.failTaken = true;
    std::fprintf(stderr, "[RenderSwitch] failcreate: %s's CreateDevice fails once (injected)\n", backendId);
    return true;
}

}  // namespace RenderSwitchDrive
