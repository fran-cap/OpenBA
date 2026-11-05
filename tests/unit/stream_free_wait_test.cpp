// ============================================================================
// tests/unit/stream_free_wait_test.cpp — review S-027, fix.stream_free_wait
// ============================================================================
// The real Audio::Device::OnSoundBufferDeleted (src/engine/audio/
// audio_device_buffer_deleted.cpp, retail 0x1800de0e0) against a stand-in
// stream worker on a second thread, over the real Platform events.
//
// S-027: ~SoundBuffer frees `_wmaData` right after OnSoundBufferDeleted returns,
//   while Stream::Run may still read that seek table. With the row on, the call
//   stops the stream's pass and waits for `_hFinished` before the voice is freed.
// Stale wake: `_wakeEvent` is AUTO-reset. A pass that leaves through its
//   `_stopping` check never consumes the wake the drain sets, and the next
//   track's Run then bails at its first queue-full wait. The drain must leave
//   the wake unsignalled.
// Row off: the faithful body, no wait, no stop, no wake.
//
// The Device and the SoundBuffer are raw zeroed storage with only the fields the
// function reads set: no Stream thread starts and no audio backend is touched.
// FreeChannelPlatform is the stand-in below (audio_device.cpp does not link into
// the unit host); it records whether the worker's pass had finished when the
// voice was freed.
// ============================================================================
#include "unit.h"
#include "audio/Device.h"
#include "audio/SoundBuffer.h"
#include "platform_sync.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <thread>

namespace {

alignas(Audio::Device) unsigned char g_deviceBytes[sizeof(Audio::Device)];
alignas(Audio::SoundBuffer) unsigned char g_bufferBytes[sizeof(Audio::SoundBuffer)];

std::atomic<bool> g_passDone{false};       // the stand-in worker has left its pass
std::atomic<int>  g_freeCalls{0};
std::atomic<bool> g_freeSawPassDone{false};

constexpr int kStreamChannel = 5;
constexpr uint32_t kWorkerTimeoutMs = 5000;

struct Fixture {
    Audio::Device* device;
    Audio::SoundBuffer* buffer;
    bool row;

    Fixture(bool rowOn, bool streamed) : row(rowOn) {
        std::memset(g_deviceBytes, 0, sizeof(g_deviceBytes));
        std::memset(g_bufferBytes, 0, sizeof(g_bufferBytes));
        device = reinterpret_cast<Audio::Device*>(g_deviceBytes);   // CAST-OK: test storage
        buffer = reinterpret_cast<Audio::SoundBuffer*>(g_bufferBytes);   // CAST-OK: test storage
        buffer->_streamed = streamed;

        Audio::Stream& s = device->_stream;
        s._wakeEvent = Platform::EventCreate(false, false);   // auto-reset, as Stream()
        s._hFinished = Platform::EventCreate(true, false);    // manual-reset, a pass in flight
        s._channel = kStreamChannel;
        s._stopping = 0;

        Audio::Device::AudioChannel& ch = device->_channels[kStreamChannel];
        ch.source = buffer;
        ch.flags = 1u;   // live

        g_passDone.store(false);
        g_freeCalls.store(0);
        g_freeSawPassDone.store(false);
        Audio::Device::_extFixStreamFreeWait = &row;
    }
    ~Fixture() {
        Audio::Device::_extFixStreamFreeWait = nullptr;
        Platform::EventClose(device->_stream._wakeEvent);
        Platform::EventClose(device->_stream._hFinished);
    }
    bool WakeSignalled() const {
        return Platform::EventWait(device->_stream._wakeEvent, 0) != Platform::kWaitTimeout;
    }
    bool Stopping() const {
        return __atomic_load_n(&device->_stream._stopping, __ATOMIC_RELAXED) != 0;
    }
    // The pass leaves Run: ThreadFunc's SetEvent(hFinished).
    void FinishPass() {
        g_passDone.store(true);
        Platform::EventSet(device->_stream._hFinished);
    }
};

}  // namespace

// The stand-in for retail 0x1800de6f0 (audio_device.cpp is not in the unit host).
void Audio::Device::FreeChannelPlatform(int32_t /*index*/) {
    g_freeSawPassDone.store(g_passDone.load());
    g_freeCalls.fetch_add(1);
}

// A pass parked in Run (between ReadFile and its lock): the drain wakes it, and
// the voice is freed only once the pass has left Run.
UNIT_TEST(stream_free_wait_waits_for_the_pass) {
    Fixture f(true, true);
    std::thread worker([&f] {
        // Run's queue-full wait: EventWaitAny over the buffer event and the wake.
        const uint32_t r = Platform::EventWait(f.device->_stream._wakeEvent, kWorkerTimeoutMs);
        CHECK(r != Platform::kWaitTimeout);
        CHECK(f.Stopping());
        std::this_thread::sleep_for(std::chrono::milliseconds(50));   // the rest of the pass
        f.FinishPass();
    });
    f.device->OnSoundBufferDeleted(f.buffer);
    worker.join();

    CHECK_EQ(g_freeCalls.load(), 1);
    CHECK(g_freeSawPassDone.load());   // S-027: freed before the pass left Run
    CHECK(f.device->_channels[kStreamChannel].source == nullptr);
    CHECK_EQ(f.device->_channels[kStreamChannel].flags, 0u);
    CHECK(!f.WakeSignalled());
}

// A pass that leaves through its `_stopping` check without reaching a wait (not
// started yet, or already past its last wait): the wake the drain set must not
// survive into the next track's pass.
UNIT_TEST(stream_free_wait_leaves_no_stale_wake) {
    Fixture f(true, true);
    std::thread worker([&f] {
        const auto start = std::chrono::steady_clock::now();
        while (!f.Stopping()) {
            if (std::chrono::steady_clock::now() - start > std::chrono::milliseconds(kWorkerTimeoutMs))
                break;
            std::this_thread::yield();
        }
        CHECK(f.Stopping());
        f.FinishPass();   // kExitStop: the wake is never consumed
    });
    f.device->OnSoundBufferDeleted(f.buffer);
    worker.join();

    CHECK_EQ(g_freeCalls.load(), 1);
    CHECK(g_freeSawPassDone.load());
    CHECK(!f.WakeSignalled());   // a signalled wake bails the next Run (kExitBail)
}

// Row off: retail's body. No stop, no wake, no wait (`_hFinished` is never set
// here, so a wait would hang).
UNIT_TEST(stream_free_wait_row_off_is_retail) {
    Fixture f(false, true);
    f.device->OnSoundBufferDeleted(f.buffer);

    CHECK_EQ(g_freeCalls.load(), 1);
    CHECK(!g_freeSawPassDone.load());
    CHECK(!f.Stopping());
    CHECK(!f.WakeSignalled());
    CHECK(f.device->_channels[kStreamChannel].source == nullptr);
}

// Row on, a buffer that is not streamed, or a streamed buffer on a channel the
// stream is not playing: the stream is left alone.
UNIT_TEST(stream_free_wait_leaves_other_channels_alone) {
    {
        Fixture f(true, false);
        f.device->OnSoundBufferDeleted(f.buffer);
        CHECK_EQ(g_freeCalls.load(), 1);
        CHECK(!f.Stopping());
        CHECK(!f.WakeSignalled());
    }
    {
        Fixture f(true, true);
        f.device->_stream._channel = kStreamChannel + 1;
        f.device->OnSoundBufferDeleted(f.buffer);
        CHECK_EQ(g_freeCalls.load(), 1);
        CHECK(!f.Stopping());
        CHECK(!f.WakeSignalled());
    }
}
