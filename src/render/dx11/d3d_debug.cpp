// ============================================================================
// src/render/dx11/d3d_debug.cpp -- see d3d_debug.h
// ============================================================================
#include "d3d_debug.h"

#include <windows.h>
#include <d3d11.h>
#include <d3d11sdklayers.h>

#include <cstdio>
#include <cstdlib>
#include <map>
#include <vector>

namespace {
ID3D11InfoQueue* g_queue = nullptr;
int g_lines = 0;
std::map<int, long long> g_perId;           // message ID -> times seen
constexpr int kPerId = 3;                    // lines printed per message ID
long long g_counts[5] = {};                  // D3D11_MESSAGE_SEVERITY: corruption, error, warning, info, message
constexpr int kMaxLines = 400;
const char* const kSeverity[5] = {"CORRUPTION", "ERROR", "WARNING", "INFO", "MESSAGE"};

void PrintCounts() {
    std::fprintf(stderr, "[D3D11] debug layer totals: corruption=%lld error=%lld warning=%lld info=%lld\n",
                 g_counts[0], g_counts[1], g_counts[2], g_counts[3]);
    for (const auto& [id, count] : g_perId)
        std::fprintf(stderr, "[D3D11]   id=%d x%lld\n", id, count);
    std::fflush(stderr);
}
}  // namespace

void D3dDebug::Attach(ID3D11Device* device) {
    // A device re-create (device lost, OnVideoRestart) attaches again. The previous queue holds a
    // reference to the previous device: release it, and register the exit totals only once.
    if (g_queue != nullptr) {
        g_queue->Release();
        g_queue = nullptr;
    }
    if (device == nullptr) {
        std::fprintf(stderr, "[D3D11] BANISHED_D3D_DEBUG: the debug-layer device create failed "
                             "(SDK layers missing?); running without it\n");
        return;
    }
    if (FAILED(device->QueryInterface(__uuidof(ID3D11InfoQueue), reinterpret_cast<void**>(&g_queue)))) {  // CAST-OK: com_out
        g_queue = nullptr;
        std::fprintf(stderr, "[D3D11] BANISHED_D3D_DEBUG: no ID3D11InfoQueue\n");
        return;
    }
    g_queue->SetMessageCountLimit(4096);
    std::fprintf(stderr, "[D3D11] BANISHED_D3D_DEBUG: debug layer on, info queue attached\n");
    static bool s_countsAtExit = false;
    if (!s_countsAtExit) {
        s_countsAtExit = true;
        std::atexit(PrintCounts);
    }
}

void D3dDebug::Drain() {
    if (g_queue == nullptr)
        return;
    const UINT64 n = g_queue->GetNumStoredMessages();
    std::vector<char> buf;
    for (UINT64 i = 0; i < n; ++i) {
        SIZE_T len = 0;
        if (FAILED(g_queue->GetMessage(i, nullptr, &len)) || len == 0)
            continue;
        buf.resize(len);
        D3D11_MESSAGE* m = reinterpret_cast<D3D11_MESSAGE*>(buf.data());  // CAST-OK: com_out
        if (FAILED(g_queue->GetMessage(i, m, &len)))
            continue;
        const int sev = static_cast<int>(m->Severity);
        if (sev >= 0 && sev < 5)
            ++g_counts[sev];
        const long long seen = ++g_perId[static_cast<int>(m->ID)];
        if (sev <= D3D11_MESSAGE_SEVERITY_WARNING && seen <= kPerId && g_lines < kMaxLines) {
            ++g_lines;
            std::fprintf(stderr, "[D3D11] %s id=%d %.*s\n", kSeverity[sev], static_cast<int>(m->ID),
                         static_cast<int>(m->DescriptionByteLength), m->pDescription);
        }
    }
    g_queue->ClearStoredMessages();
}
