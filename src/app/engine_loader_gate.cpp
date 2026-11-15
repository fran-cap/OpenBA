// src/app/engine_loader_gate.cpp — see engine_loader_gate.h for the rule
// this enforces and for the Ghidra evidence that it is retail's rule.

#include "engine_loader_gate.h"
#include "engine/UpdateLoop.h"   // src/engine/engine/UpdateLoop.h: Engine::LoaderLoop::_loader

#include <atomic>
#include <cstdio>
#include "platform_sync.h"

namespace EngineLoader {

namespace {

// Armed by the one `Loader::Load` kick, cleared the first time the loader is
// observed finished. Atomic because the observation is a main-thread read of a
// flag set on the main thread but describing another thread's progress; the
// authoritative signal is the event, not this bit.
std::atomic<bool> g_kicked{false};

}  // namespace

void NoteKicked() {
    g_kicked.store(true, std::memory_order_release);
}

bool IsBusy() {
    if (!g_kicked.load(std::memory_order_acquire)) return false;

    // ENDGAME B.3.9 ENG W2: the class static's hFinished (Thread +0x28).
    Platform::EventHandle finished = Engine::LoaderLoop::_loader._hFinished;
    if (!finished) {
        // No loader thread exists in this build. Nothing to be exclusive with,
        // and blocking forever on an object that will never signal would be a
        // hang with no cause to read out of the dump.
        static bool warned = false;
        if (!warned) {
            warned = true;
            printf("[EngineLoader] WARN: _loader._thread.hFinished is null — "
                   "cannot gate on the loader thread\n");
        }
        return false;
    }

    if (Platform::EventWait(finished, 0) == 0) {   // WAIT_OBJECT_0: polled, not consumed
        g_kicked.store(false, std::memory_order_release);
        return false;
    }
    return true;
}

}  // namespace EngineLoader
