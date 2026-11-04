// ============================================================================
// src/ext/background_save/background_save.cpp -- ext.background_save (class A, default OFF)
// ============================================================================
// NOT RETAIL. background_save.h has the design; docs/extensions/handins/HITCH_SAVESNAP.md the
// measurements it rests on:
//   * the serialise has no output-visible side effect: a replay at the save point writes the
//     same bytes as the save (three replays, the stress town), so the image is a function of
//     the world state alone;
//   * a Draw changes no serialised byte (the image at the next Update's start equals the one at
//     the save Update's end), and Draw makes no ExternalFactory call (0 in 3000 Draw windows,
//     ~7000 per Update), so the worker's reference mode and its reference-id lookups meet no
//     resource load on the main thread;
//   * what the camera input writes is either serialised in the head (PlayerController, on the
//     main thread before the worker starts) or kept on a shadow (the tool manager's +0x3c /
//     +0x40 / +0x44, which the body serialises) and copied back after the join.
// ============================================================================
#include "ext/background_save/background_save.h"

#include "Game/GameUpdateLoop.h"
#include "Game/serialize_scope.h"
#include "Game/PlayerController.h"
#include "Game/PlayerToolManager.h"
#include "Entity/ComponentEntity.h"
#include "Entity/EntityList.h"
#include "save/SaveLoad.h"
#include "io/Stream.h"
#include "ui/UserInterface.h"
#include "system/Value.h"
#include "math/Math.h"
#include "ext/registry.h"
#include "ext/eye_camera/eye_camera.h"
#include "ext/async_save_compress/async_save_compress.h"
#include "err_line.h"
#include "load_time.h"
#include "stress_harness.h"
#include "device_lost_drive.h"
#include "options.h"
#include "engine/MainLoop.h"
#include "io/Persistence.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <new>
#include <thread>
#include <vector>

namespace Ext {
namespace BackgroundSave {
namespace {

// The verify image's stream: a plain writing MemoryStream (its destructor is protected; this
// subclass is what ends it).
struct FlatStream : IO::MemoryStream {};

struct Job {
    GameUpdateLoop*   loop  = nullptr;
    Save::SavedState* state = nullptr;
    alignas(16) uint8_t cmsStorage[sizeof(IO::CompressedMemoryStream)];
    IO::CompressedMemoryStream* cms = nullptr;
    GameUpdateLoop::SerializeScope scope;
    std::thread       worker;
    std::atomic<bool> done{false};
    bool              async = false;
    double            t0 = 0, mainMs = 0, workerMs = 0;
    int               frozenUpdates = 0;
    bool              verify = false;    // COMPPROF level 4: the synchronous image to compare with
    bool              toggled = false;   // the verify level's mid-save toggle exercise ran
    float             start3C = 0, start40 = 0, start44 = 0;   // the real camera floats at Start
    // The tool manager's camera floats (+0x3c zoom distance, +0x40 pitch target, +0x44 manual
    // pitch): the frozen camera input writes these copies; the real ones are the body's.
    // A constructed manager (review F-236): a byte copy into raw storage created no object (the
    // class has a non-trivial destructor, so it is not an implicit-lifetime type), and calling
    // UpdateCamera on it was undefined. Start copies in the fields UpdateCamera reads.
    PlayerToolManager shadowManager;
    PlayerToolManager* shadow = nullptr;
    std::vector<uint8_t> verifyImage;   // g_verifyOn only
};

Job* g_job = nullptr;
bool g_installed = false;
int  g_saves = 0;   // background saves started in this process (the log's save number)

bool VerifyDecompress(Save::SavedState* s, std::vector<uint8_t>& out) {
    alignas(16) uint8_t st[sizeof(IO::CompressedMemoryStream)];
    IO::CompressedMemoryStream* cms = ::new (static_cast<void*>(st)) IO::CompressedMemoryStream();
    s->Retrieve(cms->GetMemoryStream());
    bool ok = cms->Open();
    if (ok) {
        const uint32_t size = cms->IO::CompressedStream::GetSize();
        out.resize(size);
        ok = size == 0 || cms->IO::CompressedStream::Read(static_cast<int>(size), out.data());
        cms->IO::CompressedMemoryStream::Close();
    }
    cms->IO::CompressedMemoryStream::~CompressedMemoryStream();
    return ok;
}

// The whole save, synchronously, into a flat image: retail's SerializeAll at the save point
// (a replay of the write path writes the save's bytes, HITCH_SAVESNAP section 1).
void VerifyCapture(GameUpdateLoop* loop, std::vector<uint8_t>& out) {
    constexpr int kCap = 96 * 1024 * 1024;
    alignas(16) uint8_t st[sizeof(FlatStream)];
    FlatStream* fs = ::new (static_cast<void*>(st)) FlatStream();
    fs->Create(kCap);
    loop->SerializeAll(*fs);
    const uint8_t* data = static_cast<const uint8_t*>(fs->_data);
    out.assign(data, data + fs->GetCurrentPos());
    fs->~FlatStream();
}

void WorkerMain(Job* job) {
    const double t = LoadTime::NowMs();
    GameUpdateLoop_SaveRestoreBody(job->loop, job->cms, job->state, job->scope);
    job->workerMs = LoadTime::NowMs() - t;
    job->done.store(true, std::memory_order_release);
}

// The end of a save: the worker joined, the stream ended, the camera floats home, the lines.
void Finish(Job* job) {
    if (job->worker.joinable()) job->worker.join();   // WaitWorker may have joined it already
    job->cms->IO::CompressedMemoryStream::~CompressedMemoryStream();
    PlayerToolManager* tm = job->loop->m_toolManager;
    // The real floats were the worker's to read: nothing on the main thread wrote them while it
    // ran (the line shows them unchanged from Start). The shadow's are where the player moved the
    // camera to; they become the real ones now.
    ErrLine("[BackgroundSave] save %d done: frozenUpdates=%d camera real(+3c,+40,+44)=%.3f,%.3f,%.3f "
            "(at start %.3f,%.3f,%.3f) -> shadow %.3f,%.3f,%.3f\n",
            g_saves, job->frozenUpdates, tm->field_0x3C, tm->field_0x40, tm->field_0x44,
            job->start3C, job->start40, job->start44,
            job->shadow->field_0x3C, job->shadow->field_0x40, job->shadow->field_0x44);
    tm->field_0x3C = job->shadow->field_0x3C;
    tm->field_0x40 = job->shadow->field_0x40;
    tm->field_0x44 = job->shadow->field_0x44;

    if (job->verify) {
        std::vector<uint8_t> image;
        const bool ok = VerifyDecompress(job->state, image);
        const bool same = ok && image == job->verifyImage;
        long long firstDiff = -1;
        if (ok && !same) {
            const size_t n = image.size() < job->verifyImage.size() ? image.size() : job->verifyImage.size();
            for (size_t i = 0; i < n; ++i)
                if (image[i] != job->verifyImage[i]) { firstDiff = (long long)i; break; }
            if (firstDiff < 0) firstDiff = (long long)n;
        }
        StressHarness_Verify("BackgroundSave", same, firstDiff);
        // The hash is the save-test harness's rawHash (LoadTime::HashBytes): a reload of the file
        // compares with it even when a quit kept the harness from printing its own line.
        ErrLine("[BackgroundSave] verify %s: image %zu bytes, synchronous %zu bytes, firstDiff=%lld rawHash=%016llx\n",
                same ? "IDENTICAL" : "DIFF", image.size(), job->verifyImage.size(), firstDiff,
                static_cast<unsigned long long>(LoadTime::HashBytes(image.data(), image.size())));
    }
    if (g_pfProfOn) {
        const ::Ext::AsyncSaveCompress::Stats as = ::Ext::AsyncSaveCompress::LastStats();
        ErrLine("[Stress::save] background main=%.2f ms worker=%.2f ms frozenUpdates=%d total=%.2f ms "
                "stored=%u async=%d jobs=%d endWait=%.2f\n",
                job->mainMs, job->workerMs, job->frozenUpdates, LoadTime::NowMs() - job->t0,
                job->state->_size, job->async ? 1 : 0, job->async ? as.jobs : 0,
                job->async ? as.endWaitMs : 0.0);
    }
    delete job;
}

// The row was switched off while a save ran (Sync left the poll and the barrier installed so
// that save could finish on the path it started): clear them now that it has.
void ClearIfSwitchedOff() {
    if (g_installed) return;
    Save::SaveLoad::_extSaveStart = nullptr;
    Save::SaveLoad::_extSavePoll  = nullptr;
    ::Engine::MainLoop::_extVideoErrorBarrier = nullptr;
}

// The verify level's mid-save toggle exercise (COMPPROF 4, every second background save): the
// row is switched off through the Extensions dialog's own write-back on the first frozen Update
// and back on after the join. The dialog cannot be clicked during the frozen frames (the UI's
// Update does not run), so this is the only way the toggle can meet a running worker; the save
// must still finish on the worker, byte-identical to the synchronous image.
void ToggleExercise(bool on) {
    const Ext::Entry* e = Ext::FindEntry("background_save");
    if (e == nullptr) return;
    ErrLine("[BackgroundSave] toggle exercise: row %s %s\n", on ? "on" : "off",
            on ? "after the join" : "while the worker runs");
    Ext::SetAndRecord(*e, on);
}

// Engine::MainLoop::_extVideoErrorBarrier: the device-lost route destroys every video External
// (and copies their paths' Strings), releases the terrain's height image and the minimap's
// images, and reloads resources, all of which the worker reads (the reference ids, the world
// geometry, the minimap) with the ExternalFactory still in reference mode. So the route waits
// for the worker first. The job stays: the next Update's poll finishes it as usual.
void WaitWorker() {
    Job* job = g_job;
    if (job == nullptr || !job->worker.joinable()) return;
    const double t = LoadTime::NowMs();
    job->worker.join();
    ErrLine("[BackgroundSave] save %d: the device-lost route waited %.2f ms for the worker\n",
            g_saves, LoadTime::NowMs() - t);
}

// Save::SaveLoad::_extSaveStart: UpdateSave state 1, in place of SaveRestore(true, state).
bool Start(Save::SavedState* state) {
    GameUpdateLoop* loop = GameUpdateLoop::_singleton;
    if (!Ext::Ext().background_save || loop == nullptr || g_job != nullptr) return false;

    Job* job = new Job();
    job->loop = loop;
    job->state = state;
    job->t0 = LoadTime::NowMs();
    // The verify level: COMPPROF 4, in the stress town (g_verifyOn) or, for the save scenarios that
    // load a town or start a new game, BANISHED_STRESS_COMPPROF=4 alone (options.h).
    job->verify = g_verifyOn || Opt().stressCompProf == 4;
    if (job->verify) VerifyCapture(loop, job->verifyImage);   // before anything moves

    const double tMain = LoadTime::NowMs();
    // GameUpdateLoop::SaveRestore's save arm, up to SerializeAll, then the head.
    job->cms = ::new (static_cast<void*>(job->cmsStorage)) IO::CompressedMemoryStream();
    job->cms->Create();
    job->async = Ext::Ext().async_save_compress;
    if (job->async) ::Ext::AsyncSaveCompress::Begin(*job->cms);
    loop->SerializeAllHead(*job->cms, job->scope);
    // EntityList::Serialize's opening flush and removals: they mutate the entity list and
    // delete entities, so they run here, before the worker starts. Serialize's own call then
    // finds the lists empty.
    loop->m_entityList->FlushForSerialize(*job->cms);

    job->start3C = loop->m_toolManager->field_0x3C;
    job->start40 = loop->m_toolManager->field_0x40;
    job->start44 = loop->m_toolManager->field_0x44;
    // The fields PlayerToolManager::UpdateCamera (camera_input.cpp) reads or writes.
    const PlayerToolManager* const real = loop->m_toolManager;
    job->shadow = &job->shadowManager;
    job->shadow->field_0x38 = real->field_0x38;
    job->shadow->field_0x3C = real->field_0x3C;
    job->shadow->field_0x40 = real->field_0x40;
    job->shadow->field_0x44 = real->field_0x44;
    job->shadow->field_0x48 = real->field_0x48;
    job->shadow->field_0xA4 = real->field_0xA4;
    job->shadow->field_0xA8 = real->field_0xA8;
    job->shadow->field_0xAC = real->field_0xAC;
    job->mainMs = LoadTime::NowMs() - tMain;

    g_job = job;
    ++g_saves;
    job->worker = std::thread(WorkerMain, job);
    {
        const PlayerController* pc = loop->m_playerController;
        ErrLine("[BackgroundSave] save %d started: main=%.2f ms async=%d verify=%d camera yaw=%.3f "
                "distance=%.3f target=(%.2f,%.2f)\n", g_saves, job->mainMs, job->async ? 1 : 0,
                job->verify ? 1 : 0, pc->yaw, pc->distance, pc->target.x, pc->target.y);
    }
    // BANISHED_FORCE_DEVICE_LOST=bgsave (options.h): the device-lost drive fires on the next
    // Present, i.e. while this worker runs (src/diag/device_lost_drive.h).
    if (DeviceLostDrive::Armed()) DeviceLostDrive::Request();
    return true;
}

// Save::SaveLoad::_extSavePoll: true once the worker is done (and then finished here).
bool Poll() {
    Job* job = g_job;
    if (job == nullptr) return true;
    if (!job->done.load(std::memory_order_acquire)) return false;
    g_job = nullptr;
    const bool toggled = job->toggled;
    Finish(job);
    ClearIfSwitchedOff();
    if (toggled) ToggleExercise(true);   // the row back on: the next save takes the worker again
    return true;
}

}  // namespace

void Sync() {
    g_installed = Ext::Ext().background_save;
    if (g_installed) {
        Save::SaveLoad::_extSaveStart = &Start;
        Save::SaveLoad::_extSavePoll  = &Poll;
        ::Engine::MainLoop::_extVideoErrorBarrier = &WaitWorker;
    } else {
        // No new save takes the worker. A running one keeps its poll and the device-lost barrier
        // until it has finished (Poll / Join clear them): it ends on the path it started.
        Save::SaveLoad::_extSaveStart = nullptr;
        if (g_job == nullptr) {
            Save::SaveLoad::_extSavePoll = nullptr;
            ::Engine::MainLoop::_extVideoErrorBarrier = nullptr;
        }
    }
}

bool Busy() { return g_job != nullptr; }

void FrozenUpdate(GameUpdateLoop& loop, const System::Time& time) {
    Job* job = g_job;
    ++job->frozenUpdates;
    if (job->frozenUpdates == 1 && job->verify && (g_saves % 2) == 0) {
        job->toggled = true;
        ToggleExercise(false);
    }
    PlayerController* pc = loop.m_playerController;
    if (!(Ext::Ext().eye_camera && Ext::EyeCamera::Active())) {
        // PlayerToolManager::Update's camera half (UpdateCamera, 0x1400feec0) on the shadow: the
        // controller's yaw / target / distance (serialised in the head) and the shadow's floats.
        // Hotkeys, tools and the UI do not run: they act on the world the worker is reading.
        job->shadow->UpdateCamera(loop.m_userInterface->_mouseCaptured == 0, time._ticks);
        // GameUpdateLoop::Update step 6, the camera transform.
        Math::Transform transform = pc->CalcTransform();
        Math::Transform3 t3 = transform.AsTransform3();
        pc->camera.SetCameraToWorld(t3);
    }
    loop.m_saveLoad->Update(time);   // the poll: joins the worker and finishes state 1
}

void Join(bool finishSave) {
    Job* job = g_job;
    if (job == nullptr) return;
    g_job = nullptr;
    GameUpdateLoop* const loop = job->loop;
    const double t = LoadTime::NowMs();
    const bool toggled = job->toggled;
    Finish(job);
    ErrLine("[BackgroundSave] save %d: the loop is ending (quit while the worker ran): waited %.2f ms\n",
            g_saves, LoadTime::NowMs() - t);
    // Retail's save is one Update, so a quit always comes after its state 1: the file is queued
    // for writing before the world goes. Finish state 1 the same way here (the name, the blocks,
    // the enqueue), then wait for the persistence worker to write it, because the teardown about
    // to run frees the SaveLoad and with it the embedded metadata block the write reads.
    Save::SaveLoad* const sl = finishSave ? loop->m_saveLoad : nullptr;
    if (sl != nullptr && sl->_progress == Save::SaveLoad::kExtProgressSerializing) {
        sl->UpdateSaveTail();
        const IO::Persistence* const p = IO::Persistence::_singleton;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        // Acquire loads (review F-232 / F-238, the F-147 pattern): SaveJob publishes the status
        // with a release store, and a volatile read of a word the worker writes is still a data
        // race; the acquire also orders the job's last reads of the blocks before the teardown.
        int32_t status = p != nullptr ? __atomic_load_n(&p->_worker._status, __ATOMIC_ACQUIRE) : -1;
        while (status == 0 && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            status = __atomic_load_n(&p->_worker._status, __ATOMIC_ACQUIRE);
        }
        ErrLine("[BackgroundSave] save %d: state 1 finished at the quit, write status=%d after %.2f ms\n",
                g_saves, status, LoadTime::NowMs() - t);
    }
    ClearIfSwitchedOff();
    if (toggled) ToggleExercise(true);
}

}  // namespace BackgroundSave
}  // namespace Ext
