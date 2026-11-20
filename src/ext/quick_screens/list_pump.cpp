// ============================================================================
// src/ext/quick_screens/list_pump.cpp -- see list_pump.h (NOT RETAIL)
// ============================================================================
#include "ext/quick_screens/list_pump.h"

#include "ext/quick_screens/quick_screens.h"
#include "io/Persistence.h"
#include "platform_sync.h"     // Platform::EventWait
#include "platform_thread.h"   // Platform::YieldThread
#include "platform_time.h"     // Platform::PerfCounter / PerfFrequency
#include "save/SaveLoad.h"
#include "system/Value.h"      // System::Time

#include <cstdint>

namespace Ext {
namespace QuickScreens {

namespace {

double NowMs() {
    static int64_t freq = 0;
    if (freq == 0) Platform::PerfFrequency(&freq);
    int64_t ticks = 0;
    Platform::PerfCounter(&ticks);
    return static_cast<double>(ticks) * 1000.0 / static_cast<double>(freq);
}

}  // namespace

void PumpSaveList(Save::SaveLoad* saveLoad) {
    ::IO::Persistence* const persistence = ::IO::Persistence::_singleton;
    if (persistence == nullptr) return;
    ::IO::PersistenceWorker& worker = persistence->_worker;

    // The worker's finished event is manual-reset: every enqueue resets it and
    // the thread sets it when the job ends, so the wait returns at once when no
    // job is queued or running (save/saveload.cpp, RtSave_SaveLoad_Dtor).
    const auto waitIdle = [&worker](double leftMs) {
        uint32_t timeoutMs = static_cast<uint32_t>(leftMs);
        if (timeoutMs == 0) timeoutMs = 1;
        if (Platform::EventWait(worker._hFinished, timeoutMs) != 0) return false;
        // The thread sets the event and then clears its running bit
        // (system/thread.cpp); Save::SaveLoad::Update tests the bit.
        while (worker.IsWorking()) Platform::YieldThread();
        return true;
    };

    // A zero delta: the frame's own Save::SaveLoad::Update already added the
    // frame time to the operation's elapsed time.
    const System::Time zeroTime{};
    const auto step = [saveLoad, &zeroTime]() {
        const Save::SaveLoad::Progress progressBefore = saveLoad->_progress;
        const uint32_t cursorBefore = saveLoad->_metadataCursor;
        saveLoad->Update(zeroTime);
        if (saveLoad->GetOperation() != Save::SaveLoad::kOpList) return false;
        return saveLoad->_progress != progressBefore || saveLoad->_metadataCursor != cursorBefore;
    };

    PumpListSteps(kListPumpBudgetMs, &NowMs, waitIdle, step);
}

}  // namespace QuickScreens
}  // namespace Ext
