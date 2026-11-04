// ============================================================================
// tests/unit/review_graphics_anim_test.cpp -- review 2026-11-03, group
// graphics-anim (F-098, F-121)
// ============================================================================
// F-098: a MeshInstance's AnimationState layers come back from a save with their
// packed instance handle `(instance << 8) | state` read straight off the stream
// (Array<Layer>::Serialize 0x18005cb40). Nothing compared it with the
// AnimationManager's instance table, which the save restored first, so the next
// ResetAnimationState wrote `--_refCount` 0xc bytes into a record past the end of
// `_instances` (heap overflow) and every pose read indexed past it.
//
// F-121: Animation::GetPoseAtTime (0x1800faca0) and GetRootPoseAtTime
// (0x1800faaa0) take `whole % (frameCount - loopStart)`. A looping clip whose
// loop frame is its end frame has loopStart == frameCount: IDIV by zero (#DE,
// the process dies) as soon as the clip passes its last frame.
//
// The save is a hand-built reader over inline bytes: AnimationState::Serialize
// is driven through a fake IO::ReaderWriter on a visit-kind stream, so the
// ExternalPtr<AnimationGroup> read leaves the (null) group untouched without an
// ExternalFactory.
// ============================================================================
#include "unit.h"
#include "graphics/Animation.h"
#include "graphics/AnimationManager.h"
#include "graphics/AnimationState.h"
#include "io/Serializer.h"
#include "io/Stream.h"
#include "system/Timer.h"
#include "system/MemoryAllocator.h"
#include "math/Math.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
// A reading stream with the visitor encoding (GetKind() == 3): the
// ExternalPtr serializer then returns at once for a null pointer, without the
// ExternalFactory. Nothing reads through the stream itself.
// ---------------------------------------------------------------------------
struct FakeStream final : IO::Stream {
    IO::VisitorEncoding encoding;

    FakeStream() {
        _direction = 0;   // reading
        _encoding  = &encoding;
    }
    ~FakeStream() override { _encoding = nullptr; }   // not heap-owned here

    bool Write(int, const void*) override { return false; }
    bool Read(int, void*) override { return false; }
    bool SetCurrentPos(int) override { return false; }
    uint32_t GetCurrentPos() const override { return 0; }
    uint32_t GetSize() const override { return 0; }
};

// The IO::Serializer layout double (stream, readerWriter), as math_test.cpp.
struct FakeSerializer {
    IO::Stream*       stream;
    IO::ReaderWriter* readerWriter;
};

// Reads little-endian scalars off an inline byte string, in call order.
class ByteReader final : public IO::ReaderWriter {
public:
    ByteReader(IO::Serializer& ser, const std::vector<uint8_t>& bytes)
        : IO::ReaderWriter(ser), _bytes(bytes) {}
    ~ByteReader() override {}

    size_t Consumed() const { return _pos; }
    bool   Overran() const { return _overran; }

    int  GetVersion() const override { return 0; }
    void Skip() override {}

    void SerializeCharArray(int n, char* a, const char*) override { Take(a, n); }
    void SerializeBoolArray(int n, bool* a, const char*) override { Take(a, n); }
    void SerializeFloatArray(int n, float* a, const char*) override { Take(a, 4 * n); }
    void SerializeUInt64Array(int n, uint64_t* a, const char*) override { Take(a, 8 * n); }
    void SerializeInt64Array(int n, int64_t* a, const char*) override { Take(a, 8 * n); }
    void SerializeUIntArray(int n, uint32_t* a, const char*) override { Take(a, 4 * n); }
    void SerializeIntArray(int n, int32_t* a, const char*) override { Take(a, 4 * n); }
    void SerializeUShortArray(int n, uint16_t* a, const char*) override { Take(a, 2 * n); }
    void SerializeShortArray(int n, int16_t* a, const char*) override { Take(a, 2 * n); }
    void SerializeUCharArray(int n, unsigned char* a, const char*) override { Take(a, n); }
    void SerializeSCharArray(int n, signed char* a, const char*) override { Take(a, n); }

    void SerializeChar(char& v, const char*) override { Take(&v, 1); }
    void SerializeBool(bool& v, const char*) override {
        uint8_t b = 0;
        Take(&b, 1);
        v = b != 0;
    }
    void SerializeFloat(float& v, const char*) override { Take(&v, 4); }
    void SerializeUInt64(uint64_t& v, const char*) override { Take(&v, 8); }
    void SerializeInt64(int64_t& v, const char*) override { Take(&v, 8); }
    void SerializeUInt(uint32_t& v, const char*) override { Take(&v, 4); }
    void SerializeInt(int32_t& v, const char*) override { Take(&v, 4); }
    void SerializeUShort(uint16_t& v, const char*) override { Take(&v, 2); }
    void SerializeShort(int16_t& v, const char*) override { Take(&v, 2); }
    void SerializeUChar(unsigned char& v, const char*) override { Take(&v, 1); }
    void SerializeSChar(signed char& v, const char*) override { Take(&v, 1); }

    void SerializeBytes(int n, void* buf, const char*) override { Take(buf, n); }

private:
    void Take(void* dst, int n) {
        if (n <= 0)
            return;
        if (_pos + static_cast<size_t>(n) > _bytes.size()) {
            _overran = true;
            std::memset(dst, 0, static_cast<size_t>(n));
            return;
        }
        std::memcpy(dst, _bytes.data() + _pos, static_cast<size_t>(n));
        _pos += static_cast<size_t>(n);
    }

    std::vector<uint8_t> _bytes;
    size_t _pos = 0;
    bool   _overran = false;
};

// Runs AnimationState::Serialize (reading) over `bytes`.
void ReadState(Graphics::AnimationState& st, const std::vector<uint8_t>& bytes, bool& overran,
               size_t& consumed) {
    FakeStream stream;
    FakeSerializer fake{&stream, nullptr};
    IO::Serializer& ser = reinterpret_cast<IO::Serializer&>(fake);   // LEFT: layout double
    ByteReader reader(ser, bytes);
    fake.readerWriter = &reader;
    st.Serialize(ser);
    overran  = reader.Overran();
    consumed = reader.Consumed();
}

// A one-layer AnimationState record as Array<Layer>::Serialize writes it
// (0x18005cb40): int count; per layer: the ExternalPtr (nothing in visit mode
// for a null pointer), int clip, float speed, int64 time, int64 fade length,
// int64 fade remaining, int handle, bool restarted; then the state's bool.
// The handle is the last int before the two bools.
std::vector<uint8_t> OneLayerSave(uint8_t handleLo, uint8_t handleHi) {
    return {
        0x01, 0x00, 0x00, 0x00,                           // layer count 1
        0x00, 0x00, 0x00, 0x00,                           // clip index 0
        0x00, 0x00, 0x80, 0x3f,                           // speed 1.0f
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   // time 0
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   // fade length 0
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   // fade remaining 0
        handleLo, handleHi, 0x00, 0x00,                   // handle
        0x00,                                             // restarted false
        0x00,                                             // instanced false
    };
}

// The host has no seated MemoryAllocator: the arrays allocate and free through
// the instrument seat, armed with the CRT heap (ext_package_cache_test.cpp does
// the same).
void* SeatAlloc(uint64_t size, int) {
    return std::malloc(static_cast<size_t>(size));
}
void SeatFree(void* p) {
    std::free(p);
}
struct HeapSeat {
    HeapSeat() {
        System::g_memoryAllocatorInstrument.memAlloc = &SeatAlloc;
        System::g_memoryAllocatorInstrument.memFree = &SeatFree;
    }
    ~HeapSeat() {
        System::g_memoryAllocatorInstrument.memAlloc = nullptr;
        System::g_memoryAllocatorInstrument.memFree = nullptr;
    }
};

// An AnimationManager with one live instance holding one cached state, built
// the way GetInstanceId leaves one (refCount 1, one state). Declared first in
// each test, so its heap seat outlives the AnimationState the test reads.
struct OneInstanceManager {
    HeapSeat seat;
    alignas(8) unsigned char mem[sizeof(Graphics::AnimationManager)];
    Graphics::AnimationManager* mgr = nullptr;
    Graphics::AnimationManager* saved = nullptr;

    OneInstanceManager() {
        std::memset(mem, 0, sizeof(mem));
        mgr = reinterpret_cast<Graphics::AnimationManager*>(mem);   // LEFT: zeroed 0x9d8 record
        rtcoll::Rt_SetSize_18005c3d0(&mgr->_instances, 1);
        Graphics::AnimationManager::Instance& inst = mgr->_instances._data[0];
        inst._refCount = 1;
        rtcoll::Rt_SetSize_18005b1e0(&inst._states, 1);
        saved = Graphics::AnimationManager::_singleton;
        Graphics::AnimationManager::_singleton = mgr;
    }
    ~OneInstanceManager() {
        Graphics::AnimationManager::_singleton = saved;
        // The record is left to the process: its arrays' rows free through the
        // same allocator, and the test does not depend on the release.
    }
};

}  // namespace

// F-098: instance 5 in a table of one. Before the fix the handle survived the
// read and ResetAnimationState decremented a refCount 0xc bytes into the sixth
// 0x30-byte record of a one-record buffer.
UNIT_TEST(ReviewAnim_SaveHandlePastInstanceTableIsDropped) {
    OneInstanceManager m;
    Graphics::AnimationState st;
    bool overran = false;
    size_t consumed = 0;
    ReadState(st, OneLayerSave(0x00, 0x05), overran, consumed);
    CHECK(!overran);
    CHECK_EQ(consumed, static_cast<size_t>(42));
    CHECK_EQ(st._layers._size, 1);
    CHECK_EQ(st._layers._data[0]._handle, -1);
    st.ResetAnimationState();
    CHECK_EQ(m.mgr->_instances._data[0]._refCount, 1);
}

// F-098: instance 0 exists, but its state 3 does not (one cached state).
UNIT_TEST(ReviewAnim_SaveHandlePastStateTableIsDropped) {
    OneInstanceManager m;
    Graphics::AnimationState st;
    bool overran = false;
    size_t consumed = 0;
    ReadState(st, OneLayerSave(0x03, 0x00), overran, consumed);
    CHECK(!overran);
    CHECK_EQ(st._layers._data[0]._handle, -1);
    st.ResetAnimationState();
    CHECK_EQ(m.mgr->_instances._data[0]._refCount, 1);
}

// F-098: a valid handle (instance 0, state 0) is kept byte for byte, and the
// reset releases it exactly as retail does.
UNIT_TEST(ReviewAnim_SaveHandleInRangeIsKept) {
    OneInstanceManager m;
    Graphics::AnimationState st;
    bool overran = false;
    size_t consumed = 0;
    ReadState(st, OneLayerSave(0x00, 0x00), overran, consumed);
    CHECK(!overran);
    CHECK_EQ(st._layers._data[0]._handle, 0);
    st.ResetAnimationState();
    CHECK_EQ(m.mgr->_instances._data[0]._refCount, 0);
    CHECK_EQ(m.mgr->_instances._data[0]._animIndex, -1);
    CHECK_EQ(m.mgr->_instances._data[0]._states._size, 0);
}

namespace {

// A clip of `frames` frames of `bones` bones; key (f, b) has translation.x =
// 100 * f + b so the sampled frame can be read back.
struct Clip {
    alignas(8) unsigned char mem[sizeof(Graphics::Animation)];
    Graphics::Animation* a = nullptr;
    std::vector<Math::Transform> keys;

    Clip(int start, int end, int loop, bool looping, int frames, int bones) {
        std::memset(mem, 0, sizeof(mem));
        a = reinterpret_cast<Graphics::Animation*>(mem);   // LEFT: zeroed 0x70 record, no names
        a->_startFrame = start;
        a->_endFrame   = end;
        a->_loopFrame  = loop;
        a->_looping    = looping;
        a->_frameRate  = 30.0f;
        a->_boneCount  = bones;
        a->_frameCount = frames;
        keys.resize(static_cast<size_t>(frames > 0 ? frames * bones : 0));
        for (int f = 0; f < frames; ++f) {
            for (int b = 0; b < bones; ++b) {
                Math::Transform& k = keys[static_cast<size_t>(f * bones + b)];
                k.rotation.x = 0.0f;
                k.rotation.y = 0.0f;
                k.rotation.z = 0.0f;
                k.rotation.w = 1.0f;
                k.translation.x = 100.0f * static_cast<float>(f) + static_cast<float>(b);
                k.translation.y = 0.0f;
                k.translation.z = 0.0f;
            }
        }
        a->_keys._data     = keys.empty() ? nullptr : keys.data();
        a->_keys._size     = static_cast<int32_t>(keys.size());
        a->_keys._capacity = static_cast<int32_t>(keys.size());
    }
    ~Clip() {
        a->_keys._data = nullptr;   // the vector owns the keys
        a->_keys._size = 0;
        a->_keys._capacity = 0;
    }
};

struct TickScale {
    double saved;
    TickScale() : saved(System::Timer::_secondsPerTick) { System::Timer::_secondsPerTick = 1e-7; }
    ~TickScale() { System::Timer::_secondsPerTick = saved; }
};

// `seconds` of clip time at 30 frames a second, and a zero delta. Bone 0 is the
// root: GetPoseAtTime resets its rotation and scales its translation by the
// delta, so the sampled frame is read off bone 1.
float PoseX(const Graphics::Animation& a, double seconds, int bone) {
    const System::Time t = {static_cast<int64_t>(seconds * 1e7)};
    const System::Time d = {0};
    Math::Transform out[4];
    for (Math::Transform& o : out) {
        o.translation.x = -1.0f;
        o.translation.y = 0.0f;
        o.translation.z = 0.0f;
    }
    a.GetPoseAtTime(t, d, out, 4);
    return out[bone].translation.x;
}

}  // namespace

// F-121: loop frame == end frame. 1 s at 30 fps is frame 30 of a 4-frame clip;
// loopStart = (10 - 0) / (10 - 0) * 4 = 4 and `30 % (4 - 4)` was IDIV by zero.
// The clip now holds its last frame, as a non-looping clip does.
UNIT_TEST(ReviewAnim_PoseLoopFrameAtEndHoldsLastFrame) {
    TickScale ts;
    Clip c(0, 10, 10, true, 4, 2);
    CHECK_EQ(PoseX(*c.a, 1.0, 1), 301.0f);
    const System::Time t = {10000000};
    const System::Time d = {10000000};   // 1 s: the root offset is scaled by 30
    const Math::Transform root = c.a->GetRootPoseAtTime(t, d);
    CHECK_EQ(root.translation.x, 9000.0f);   // key (3, 0) = 300, times 30
}

// F-121: GetRootPoseAtTime keeps retail's wrap on a valid loop (loopKey 2,
// key 30 -> 2 + 30 % 2 = 2) and samples key 0 for a negative time.
UNIT_TEST(ReviewAnim_RootPoseValidLoopAndNegativeTime) {
    TickScale ts;
    Clip c(0, 10, 5, true, 4, 2);
    const System::Time d = {10000000};
    const System::Time t = {10000000};
    CHECK_EQ(c.a->GetRootPoseAtTime(t, d).translation.x, 6000.0f);
    const System::Time back = {-50000000};
    CHECK_EQ(c.a->GetRootPoseAtTime(back, d).translation.x, 0.0f);
}

// F-121: a clip whose start and end frames coincide (0/0 -> NaN loop start, a
// float->int conversion out of range) and a negative layer time (a key index
// below the buffer) no longer sample outside the keys.
UNIT_TEST(ReviewAnim_PoseDegenerateClipStaysInKeys) {
    TickScale ts;
    Clip c(5, 5, 7, true, 4, 2);
    const float x = PoseX(*c.a, 1.0, 1);
    CHECK(x == 1.0f || x == 101.0f || x == 201.0f || x == 301.0f);

    Clip n(0, 10, 0, false, 4, 2);
    CHECK_EQ(PoseX(*n.a, -5.0, 1), 1.0f);
}

// F-121: a valid looping clip samples exactly as retail: loop frame 5 of
// 0..10 over 4 frames is loopStart 2; frame 30 wraps to 2 + 30 % 2 = 2.
UNIT_TEST(ReviewAnim_PoseValidLoopUnchanged) {
    TickScale ts;
    Clip c(0, 10, 5, true, 4, 2);
    CHECK_EQ(PoseX(*c.a, 1.0, 1), 201.0f);
    CHECK_EQ(PoseX(*c.a, 0.0, 1), 1.0f);
    CHECK_EQ(PoseX(*c.a, 3.0, 1), 201.0f);   // frame 90: 2 + 90 % 2
}
