// ============================================================================
// tests/unit/audio_abi_test.cpp — ENDGAME B.3 step 7, block B4
//
//   Audio::SoundBuffer   : Object::CompiledExternal  0x80  9 slots, Compile INHERITED
//   Audio::SoundEffect   : Object::External          0x40  9 slots, Compile OVERRIDDEN
//   Audio::Stream        : System::Thread            0x88  2 slots (Run, OnCompleted)
//   Audio::Device        : non-polymorphic           0x1298, holds the Stream by value
//   Audio::Scene         : non-polymorphic           0x38, held by value (exe)
//   Audio::Configuration : non-polymorphic           0x10
//
// Built to the contract of pathfinding_picking_abi_test.cpp (block B6).
//
// PART A — LAYOUT: sizeof / alignof / offsets are the headers' static_asserts
//   (only the three Audio::Stream offsets Stream.h does not pin are here; Device.h
//   pins them through Device::_stream); the parent edge, concrete, the destructor shape.
// PART B — SLOT ORDER: a probe subclass of the REAL class; each slot is
//   raw-called by byte offset and the marker that runs names the member.
//   SoundBuffer's slot 4 IS Object::External's Compile (B7-1.7); SoundEffect's
//   is its own. Stream's slot 0 is Run and slot 1 is Thread's OnCompleted
//   (B7-1.4: no deleting-dtor slot).
// PART D — ARGUMENT PASSING (B7-5): `ExternalPtr<T>` by value and
//   `System::Time` by value are both ONE REGISTER (file-scope static_asserts).
// PART E — the one-line accessors read retail's offsets (bytes read from the
//   retail DLL, cited in each header).
//
// The classes' out-of-line members are defined HERE as markers
// (the src/engine/audio files that define them do not link into a host test).
// ============================================================================
#include "unit.h"
#include "slot_check.h"
#include "dtor_check.h"

#include "audio/Audio.h"
#include "audio/Device.h"
#include "audio/SoundBuffer.h"
#include "audio/SoundEffect.h"
#include "audio/Stream.h"
#include "system/Value.h"

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <type_traits>
#include <new>
#include "system_abi_stubs.h"   // the recording Thread InitFields / ReleaseFields

namespace {

const void* g_lastSelf = nullptr;
int         g_lastMark = -1;
void Note(const void* self, int mark) { g_lastSelf = self; g_lastMark = mark; }
void Reset() { g_lastSelf = nullptr; g_lastMark = -1; }
// ENDGAME B.3.9 ENG: the destructor markers RECORD each call, so the real
// destructors can be seen running per element, downward (B3X W1: `delete[]`).
int         g_dtorCount = 0;
const void* g_dtorOrder[8] = {};
void NoteDtor(const void* self) {
    if (g_dtorCount < 8) g_dtorOrder[g_dtorCount] = self;
    ++g_dtorCount;
}

// `byteOffset` is RETAIL's; T (the object's class) decides the compiler's.
template <typename Fn, class T>
Fn SlotAt(void* obj, int byteOffset) {
    const uint8_t* vt = *reinterpret_cast<const uint8_t* const*>(obj);
    Fn f = nullptr;
    const void* raw = *reinterpret_cast<const void* const*>(vt + RetailByteOffset<T>(byteOffset));
    std::memcpy(&f, &raw, sizeof f);
    return f;
}
const void* const* VTableOf(void* obj) {
    return *reinterpret_cast<const void* const* const*>(obj);
}

constexpr int kDtorSlot   = 0x00;
constexpr int kTypeName       = 0x08;
constexpr int kType           = 0x10;
constexpr int kGetFileVersion = 0x18;
constexpr int kCompile        = 0x20;
constexpr int kSerialize      = 0x28;
constexpr int kReconstruct    = 0x30;
constexpr int kDestruct       = 0x38;
constexpr int kConstruct      = 0x40;

// A bare Object::External leaf: whatever it carries at +0x20 IS
// `?Compile@External@Object@@` (0x1800066e0).
struct BareExternalProbe final : Object::External {
    int32_t GetFileVersion() const override { return 0; }
    bool Serialize(IO::Stream&) override { return true; }
    Object::External* Reconstruct() override { return this; }
    void Destruct() override {}
};

}  // namespace

// ===========================================================================
// The out-of-line members, AS MARKERS.
// ===========================================================================
namespace Audio {
int32_t SoundBuffer::GetFileVersion() const { Note(this, 3); return 0xa; }
bool SoundBuffer::Serialize(IO::Stream&) { Note(this, 5); return true; }
Object::External* SoundBuffer::Reconstruct() { Note(this, 6); return this; }
void SoundBuffer::Destruct() { Note(this, 7); }
Object::External* SoundBuffer::Construct() { Note(this, 8); return this; }
SoundBuffer::~SoundBuffer() { NoteDtor(this); }

int32_t SoundEffect::GetFileVersion() const { Note(this, 3); return 3; }
Object::External::CompileResult SoundEffect::Compile(const System::Filename&) { Note(this, 4); return 1; }
bool SoundEffect::Serialize(IO::Stream&) { Note(this, 5); return true; }
Object::External* SoundEffect::Reconstruct() { Note(this, 6); return this; }
void SoundEffect::Destruct() { Note(this, 7); }
Object::External* SoundEffect::Construct() { Note(this, 8); return this; }
SoundEffect::~SoundEffect() { NoteDtor(this); }

void Stream::Run() { Note(this, 50); }
// ENDGAME B.3.9 ENG W2: the real ctor / dtor (audio_stream.cpp, not in the
// unit binary) as markers; the Thread base runs system_abi_stubs.cpp's bodies.
Stream::Stream() : System::Thread(0x10000u) { Note(this, 51); }
Stream::~Stream() { Note(this, 52); }

Configuration::~Configuration() { Note(this, 90); }
}  // namespace Audio

namespace {

// B3X W1: retail slot 0 (the deleting destructor) is the destructor pair;
// the probe records in its destructor and frees through a recording
// class-scope operator delete (retail flags 1's MemFree).
struct SoundBufferProbe final : Audio::SoundBuffer {
    ~SoundBufferProbe() override { Note(this, 0); }
    DTOR_CHECK_RECORDING_DELETE
};
struct SoundEffectProbe final : Audio::SoundEffect {
    ~SoundEffectProbe() override { Note(this, 0); }
    DTOR_CHECK_RECORDING_DELETE
};
struct StreamProbe final : Audio::Stream {};

using NameFn    = const char* (*)(const void*);
using TypeFn    = uint32_t (*)(const void*);
using VerFn     = int32_t (*)(const void*);
using CompileFn = uint32_t (*)(void*, const void*);
using SerFn     = bool (*)(void*, void*);
using PtrFn     = void* (*)(void*);
using VoidFn    = void (*)(void*);

// Raw-call the eight slots every Audio resource owns (4 checked separately).
// Slots 1 and 2 are DECLARE_TYPE's inline bodies over the class's own record
// (ENDGAME B.3.9 TI-REST), checked against that record.
template <class T>
void CheckOwnSlotsExceptCompile(void* raw, const ::Object::BaseTypeInfo& record) {
    CHECK((SlotAt<NameFn, T>(raw, kTypeName)(raw)) == record.name);
    CHECK_EQ((SlotAt<TypeFn, T>(raw, kType)(raw)), record.typeId);
    Reset(); (SlotAt<VerFn, T>(raw, kGetFileVersion)(raw));     CHECK_EQ(g_lastMark, kGetFileVersion / 8);
    Reset(); (SlotAt<SerFn, T>(raw, kSerialize)(raw, nullptr)); CHECK_EQ(g_lastMark, kSerialize / 8);
    Reset(); (SlotAt<PtrFn, T>(raw, kReconstruct)(raw));        CHECK_EQ(g_lastMark, kReconstruct / 8);
    Reset(); (SlotAt<VoidFn, T>(raw, kDestruct)(raw));          CHECK_EQ(g_lastMark, kDestruct / 8);
    Reset(); (SlotAt<PtrFn, T>(raw, kConstruct)(raw));          CHECK_EQ(g_lastMark, kConstruct / 8);
    CHECK(g_lastSelf == raw);
}

// Retail slot 0 (kDtorSlot) is now the destructor pair, read raw: compiler
// [0] D1 runs ~P once and frees nothing (retail flags 0); [1] D0 runs it and
// hands the block to the class-scope operator delete (flags 1). D1 is called on
// a placement-new'd probe that is never destroyed again (a stack probe would be
// destroyed twice).
template <class P>
void CheckDtorPair() {
    static_assert(kDtorSlot == 0, "retail slot 0");
    alignas(P) unsigned char buf[sizeof(P)];
    P* p = ::new (static_cast<void*>(buf)) P();
    CHECK(VTableOf(p)[0] != VTableOf(p)[1]);
    Reset(); dtor_check::ResetFrees();
    dtor_check::CallD1(p);
    CHECK_EQ(g_lastMark, kDtorSlot / 8); CHECK(g_lastSelf == p);
    CHECK_EQ(dtor_check::g_frees, 0);
    P* h = dtor_check::NewOnMalloc<P>();
    Reset(); dtor_check::ResetFrees();
    dtor_check::CallD0(h);
    CHECK_EQ(g_lastMark, kDtorSlot / 8); CHECK(g_lastSelf == h);
    CHECK_EQ(dtor_check::g_frees, 1); CHECK(dtor_check::g_lastFree == h);
}

}  // namespace

// ---------------------------------------------------------------------------
// PART A — layouts
// ---------------------------------------------------------------------------
UNIT_TEST(audio_soundbuffer_layout) {
    // sizeof / alignof / the field offsets are SoundBuffer.h's static_asserts.
    CHECK((std::is_base_of<Object::CompiledExternal, Audio::SoundBuffer>::value));  // B7-2
    // ENDGAME B.3.9 ENG: concrete. B3X W1: public virtual destructor (retail
    // slot 0 = D1 + D0).
    CHECK(!std::is_abstract<Audio::SoundBuffer>::value);
    CHECK(std::is_destructible<Audio::SoundBuffer>::value);
    CHECK(std::has_virtual_destructor<Audio::SoundBuffer>::value);
}

UNIT_TEST(audio_soundeffect_layout) {
    // sizeof / alignof / the field offsets / Sample / Variant are SoundEffect.h's static_asserts.
    CHECK((std::is_base_of<Object::External, Audio::SoundEffect>::value));
    CHECK(!(std::is_base_of<Object::CompiledExternal, Audio::SoundEffect>::value));
    CHECK(!std::is_abstract<Audio::SoundEffect>::value);
    CHECK(std::is_destructible<Audio::SoundEffect>::value);
    CHECK(std::has_virtual_destructor<Audio::SoundEffect>::value);
}

UNIT_TEST(audio_stream_layout_and_shape) {
    // sizeof / alignof / _buffers / _bufferEvent / _bytePosition / _channel are Stream.h's
    // static_asserts; the three offsets Stream.h does not pin (Device.h pins them) are below.
    CHECK((std::is_base_of<System::Thread, Audio::Stream>::value));
    CHECK(std::is_polymorphic<Audio::Stream>::value);
    CHECK(!std::is_abstract<Audio::Stream>::value);
    CHECK(std::is_default_constructible<Audio::Stream>::value);    // real public ctor (B.3.9 ENG W2)
    CHECK(!std::has_virtual_destructor<Audio::Stream>::value);
    CHECK_EQ((int)offsetof(Audio::Stream, _wakeEvent),    0x40);
    CHECK_EQ((int)offsetof(Audio::Stream, _lock),         0x50);
    CHECK_EQ((int)offsetof(Audio::Stream, _stopping),     0x80);
}

UNIT_TEST(audio_device_layout_holds_the_stream_pod) {
    // sizeof / the channel structs / the four offsets are Device.h's static_asserts.
    CHECK(!std::is_polymorphic<Audio::Device>::value);
    // B7-12: the Device holds the polymorphic Audio::Stream class by value.
    CHECK((std::is_same<decltype(Audio::Device::_stream), Audio::Stream>::value));
}

UNIT_TEST(audio_scene_and_configuration_layout) {
    // Scene's and Configuration's sizes and offsets are Audio.h's static_asserts.
    CHECK(!std::is_polymorphic<Audio::Scene>::value);
    CHECK(!std::is_abstract<Audio::Scene>::value);
    CHECK(std::is_default_constructible<Audio::Scene>::value);    // real ctor 0x1800e0160 (wave Y)
    CHECK(std::is_destructible<Audio::Scene>::value);             // implicit = retail's
    CHECK(!std::is_trivially_copyable<Audio::Scene>::value);      // B7-8
    CHECK((std::is_same<RtAudio::EffectSlot, Audio::Scene::AttenutedEffect>::value));
    CHECK((std::is_same<RtAudio::Scene, Audio::Scene>::value));

    CHECK(!std::is_polymorphic<Audio::Configuration>::value);
}

// ---------------------------------------------------------------------------
// PART B — slot order
// ---------------------------------------------------------------------------
UNIT_TEST(audio_soundbuffer_slot_order_and_inherited_compile) {
    SoundBufferProbe probe;
    void* raw = static_cast<void*>(&probe);
    CheckOwnSlotsExceptCompile<SoundBufferProbe>(raw, Audio::SoundBuffer::GetTypeInfo());
    CheckDtorPair<SoundBufferProbe>();
    // Slot 4 is NOT SoundBuffer's: it is Object::External's Compile, the same
    // pointer a bare External leaf carries, and it answers retail's 1.
    BareExternalProbe bare;
    CHECK(RetailEntry<SoundBufferProbe>(raw, kCompile / 8) ==
          RetailEntry<BareExternalProbe>(&bare, kCompile / 8));
    CHECK_EQ((SlotAt<CompileFn, SoundBufferProbe>(raw, kCompile)(raw, nullptr)), 1u);
}

UNIT_TEST(audio_soundeffect_slot_order_and_own_compile) {
    SoundEffectProbe probe;
    void* raw = static_cast<void*>(&probe);
    CheckOwnSlotsExceptCompile<SoundEffectProbe>(raw, Audio::SoundEffect::GetTypeInfo());
    CheckDtorPair<SoundEffectProbe>();
    Reset();
    CHECK_EQ((SlotAt<CompileFn, SoundEffectProbe>(raw, kCompile)(raw, nullptr)), 1u);
    CHECK_EQ(g_lastMark, kCompile / 8);
    BareExternalProbe bare;
    CHECK(RetailEntry<SoundEffectProbe>(raw, kCompile / 8) !=
          RetailEntry<BareExternalProbe>(&bare, kCompile / 8));
    // Nine retail slots, ten compiler slots (D1 and D0 for retail slot 0): all
    // distinct, D1 != D0 included.
    const void* const* vt = VTableOf(raw);
    for (int i = 0; i < 10; ++i)
        for (int j = i + 1; j < 10; ++j)
            CHECK(vt[i] != vt[j]);
}

UNIT_TEST(audio_stream_slot0_is_run_and_slot1_is_threads_oncompleted) {
    StreamProbe probe;
    void* raw = static_cast<void*>(&probe);
    Reset();
    SlotAt<VoidFn, StreamProbe>(raw, 0x00)(raw);
    CHECK_EQ(g_lastMark, 50);
    CHECK(g_lastSelf == raw);
    // Slot 1 is System::Thread's own OnCompleted: the same pointer the
    // Thread-derived Engine::Loader-shaped probe carries at +0x08.
    struct OtherThread final : System::Thread {
        OtherThread() : System::Thread(0x40000u) {}
        void Run() override {}
    };
    OtherThread other;
    CHECK(VTableOf(raw)[1] == VTableOf(static_cast<void*>(&other))[1]);
    CHECK(VTableOf(raw)[0] != VTableOf(static_cast<void*>(&other))[0]);
}

// ---------------------------------------------------------------------------
// PART D — B7-5: ExternalPtr and System::Time by value are single registers
// (a trivially copyable 8-byte class travels in one register under ms_abi).
// ---------------------------------------------------------------------------
static_assert(std::is_trivially_copyable<Object::ExternalPtr<Audio::SoundEffect>>::value &&
              sizeof(Object::ExternalPtr<Audio::SoundEffect>) == 8,
              "B7-5: ExternalPtr<SoundEffect> by value is a BARE register (Scene::PlayEffect)");
static_assert(std::is_trivially_copyable<System::Time>::value,
              "B7-5: System::Time by value is a register (Scene::Update's two Times; sizeof 8 is Value.h's)");

// ---------------------------------------------------------------------------
// PART E — the inline accessors read retail's offsets
// ---------------------------------------------------------------------------
UNIT_TEST(audio_accessors_read_retails_offsets) {
    SoundBufferProbe sb;
    const Audio::SoundBuffer& b = sb;
    const uint8_t* base = reinterpret_cast<const uint8_t*>(&b);
    CHECK(reinterpret_cast<const uint8_t*>(&b.GetStreamPath()) == base + 0x30);   // 0x18000bca0
    CHECK(static_cast<const uint8_t*>(b.Format()) == base + 0x54);                // 0x18000bcb0
    sb._loopForever = 1; sb._streamed = 0;
    CHECK(b.IsLooping()); CHECK(!b.IsStreamed());

    SoundEffectProbe se;
    se._radius = 3.5f; se._innerRadius = 1.25f; se._maxLength = 0;
    CHECK(se.GetRadius() == 3.5f);
    CHECK(se.GetInnerRadius() == 1.25f);
    CHECK(se.IsLooping());                      // 0x1800506b0: `[RCX+0x28] == 0`
    se._maxLength = 9;
    CHECK(!se.IsLooping());

    alignas(8) static uint8_t devStorage[sizeof(Audio::Device)] = {};
    Audio::Device& dev = *reinterpret_cast<Audio::Device*>(devStorage);
    CHECK(reinterpret_cast<uint8_t*>(dev.GetChannel(3)) == devStorage + 3 * 0x28);            // 0x18000c710
    CHECK(reinterpret_cast<uint8_t*>(dev.GetPlatformChannel(2)) == devStorage + 0xa98 + 0x40); // 0x18000c720
    CHECK(reinterpret_cast<uint8_t*>(&dev._stream) == devStorage + 0xa00);

    alignas(8) uint8_t sceneStorage[sizeof(Audio::Scene)] = {};
    sceneStorage[0x30 + 4] = 1;
    const Audio::Scene& scene = *reinterpret_cast<const Audio::Scene*>(sceneStorage);
    CHECK(scene.IsPaused(4)); CHECK(!scene.IsPaused(3));                          // 0x180050990

    alignas(4) uint8_t cfgStorage[sizeof(Audio::Configuration)] = {};
    Audio::Configuration& cfg = *reinterpret_cast<Audio::Configuration*>(cfgStorage);
    cfg.SetMusicVolume(50);
    CHECK_EQ(*reinterpret_cast<int32_t*>(cfgStorage + 4), 50);                    // 0x18000d4f0
    CHECK_EQ(cfg.GetMusicVolume(), 50);
}

// ---------------------------------------------------------------------------
// ENDGAME B.3.9 ENG (B39_RULES D1/D2, rule B9-16) -- the concrete classes.
// ---------------------------------------------------------------------------
namespace {
// B3X W1: retail's flags-1 arm is `delete p`: T's own deleting destructor runs
// the chain once and frees through T's class-scope operator delete (MemFree;
// InternalMemFree == free here, matching InternalMemAlloc). The free itself is
// proven on a Recording<T> stand-in whose class operator delete records.
// Retail's flags-3 vector arm has no Itanium counterpart; `delete[]` of a
// `new T[3]` block replaces it (three destructors, downward).
template <class T>
void CheckBothArms() {
    constexpr int kSize = (int)sizeof(T);   // the row's element size is T's header static_assert
    static_assert(dtor_check::HasClassDelete<T>::value, "D0 frees through a class-scope operator delete");
    void* m = Object::detail::InternalMemAlloc(kSize, 8);
    CHECK(m != nullptr);
    T* p = ::new (m) T();
    g_dtorCount = 0;
    delete p;
    CHECK_EQ(g_dtorCount, 1);
    T* r = dtor_check::NewOnMalloc<dtor_check::Recording<T>>();
    g_dtorCount = 0; dtor_check::ResetFrees();
    delete r;
    CHECK_EQ(g_dtorCount, 1);
    CHECK_EQ(dtor_check::g_frees, 1);
    CHECK(dtor_check::g_lastFree == r);
    CHECK_ARRAY_DELETE(T, g_dtorCount, g_dtorOrder);
}
}  // namespace

UNIT_TEST(b39_eng_soundbuffer_slot_order_by_pmf) {
    using B = Audio::SoundBuffer;
    CHECK_DTOR_SLOT(B);   // 0x18000bef0
    CHECK_SLOT(B, TypeName,       0x08/8);   // 0x18000b8b0
    CHECK_SLOT(B, Type,           0x10/8);   // 0x18000b890
    CHECK_SLOT(B, GetFileVersion, 0x18/8);   // 0x1800e0470
    CHECK_SLOT(B, Compile,        0x20/8);   // External's 0x1800066e0
    CHECK_SLOT(B, Serialize,      0x28/8);   // 0x1800e0480
    CHECK_SLOT(B, Reconstruct,    0x30/8);   // 0x18000bb30
    CHECK_SLOT(B, Destruct,       0x38/8);   // 0x18000bc70
    CHECK_SLOT(B, Construct,      0x40/8);   // 0x18000bbd0
}
UNIT_TEST(b39_eng_soundeffect_slot_order_by_pmf) {
    using E = Audio::SoundEffect;
    CHECK_DTOR_SLOT(E);   // 0x180050820
    CHECK_SLOT(E, TypeName,       0x08/8);   // 0x180050340
    CHECK_SLOT(E, Type,           0x10/8);   // 0x180050320
    CHECK_SLOT(E, GetFileVersion, 0x18/8);   // 0x1800e0990
    CHECK_SLOT(E, Compile,        0x20/8);   // 0x1800e09a0
    CHECK_SLOT(E, Serialize,      0x28/8);   // 0x1800e0aa0
    CHECK_SLOT(E, Reconstruct,    0x30/8);   // 0x180050560
    CHECK_SLOT(E, Destruct,       0x38/8);   // 0x180050640
    CHECK_SLOT(E, Construct,      0x40/8);   // 0x1800505d0
}

// SoundBuffer's factory stores (0x18000ba03..a6a): word +0x20, dword +0x24,
// qwords +0x28/+0x30, 0x28 bytes from +0x54, qword +0x38, dword +0x40, qword
// +0x48, dword +0x50. +0x22, +0x44 and +0x7c..+0x7f are NOT written.
UNIT_TEST(b39_eng_soundbuffer_ctor_makes_retails_stores) {
    alignas(8) uint8_t buf[0x80];
    std::memset(buf, 0xCD, sizeof buf);
    Audio::SoundBuffer* b = ::new (buf) Audio::SoundBuffer();
    CHECK(static_cast<void*>(b) == static_cast<void*>(buf));
    auto zero = [&](int lo, int hi) { for (int i = lo; i < hi; ++i) CHECK_EQ((int)buf[i], 0); };
    auto kept = [&](int lo, int hi) { for (int i = lo; i < hi; ++i) CHECK_EQ((int)buf[i], 0xCD); };
    zero(0x08, 0x22); kept(0x22, 0x24); zero(0x24, 0x44); kept(0x44, 0x48);
    zero(0x48, 0x54); zero(0x54, 0x7c); kept(0x7c, 0x80);
    alignas(8) uint8_t buf2[0x80];
    Audio::SoundBuffer* b2 = ::new (buf2) Audio::SoundBuffer();
    SoundBufferProbe probe;
    CHECK(VTableOf(b) == VTableOf(b2));
    CHECK(VTableOf(b) != VTableOf(&probe));
    g_dtorCount = 0;
    dtor_check::ResetFrees();
    b->~SoundBuffer();                      // retail slot 0, flags 0: no free
    b2->~SoundBuffer();
    CHECK_EQ(g_dtorCount, 2);
    CHECK_EQ(dtor_check::g_frees, 0);
}

// SoundEffect's factory stores (0x180050493..4d0): one qword 0x3f800000 over
// +0x20/+0x24, zero +0x28, the Array header +0x30/+0x38.
UNIT_TEST(b39_eng_soundeffect_ctor_makes_retails_stores) {
    alignas(8) uint8_t buf[0x40];
    std::memset(buf, 0xCD, sizeof buf);
    Audio::SoundEffect* e = ::new (buf) Audio::SoundEffect();
    uint64_t q = 0;
    std::memcpy(&q, buf + 0x20, sizeof q);
    CHECK_EQ(q, (uint64_t)0x3f800000ull);
    for (int off = 0x28; off < 0x40; off += 8) {
        std::memcpy(&q, buf + off, sizeof q);
        CHECK_EQ(q, (uint64_t)0);
    }
    for (int off = 0x08; off < 0x20; off += 8) {
        std::memcpy(&q, buf + off, sizeof q);
        CHECK_EQ(q, (uint64_t)0);
    }
    g_dtorCount = 0;
    e->~SoundEffect();                      // retail slot 0, flags 0
    CHECK_EQ(g_dtorCount, 1);
}

// Retail slot-0 bodies whose vector arm is retired: SoundBuffer 0x18000bef0,
// SoundEffect 0x180050820.
UNIT_TEST(b39_eng_soundbuffer_deletingdtor_frees_both_arms) {

    CheckBothArms<Audio::SoundBuffer>();
}
UNIT_TEST(b39_eng_soundeffect_deletingdtor_frees_both_arms) {
    CheckBothArms<Audio::SoundEffect>();
}

// ---------------------------------------------------------------------------
// ENDGAME B.3.9 ENG -- Audio::Device::PlatformChannel is a real polymorphic
// class (the retired unnamed mint row 0x180157c60): seven virtuals in the
// IXAudio2VoiceCallback order, no destructor slot, and the ctor's loop-2
// stores (vptr, +0x08, +0x18; +0x10 is loop 3's and stays untouched).
// ---------------------------------------------------------------------------
namespace Audio {
void Device::PlatformChannel::OnVoiceProcessingPassStart(uint32_t) { Note(this, 60); }
void Device::PlatformChannel::OnVoiceProcessingPassEnd() { Note(this, 61); }
void Device::PlatformChannel::OnStreamEnd() { Note(this, 62); }
void Device::PlatformChannel::OnBufferStart(void*) { Note(this, 63); }
void Device::PlatformChannel::OnBufferEnd(void*) { Note(this, 64); }
void Device::PlatformChannel::OnLoopEnd(void*) { Note(this, 65); }
void Device::PlatformChannel::OnVoiceError(void*, int32_t) { Note(this, 66); }
}  // namespace Audio

UNIT_TEST(b39_eng_platformchannel_slot_order_by_pmf) {
    using PC = Audio::Device::PlatformChannel;
    CHECK(std::is_polymorphic<PC>::value);
    CHECK(!std::has_virtual_destructor<PC>::value);
    CHECK_SLOT(PC, OnVoiceProcessingPassStart, 0x00/8);   // 0x180050200
    CHECK_SLOT(PC, OnVoiceProcessingPassEnd,   0x08/8);   // 0x1800501f0
    CHECK_SLOT(PC, OnStreamEnd,                0x10/8);   // 0x1800501c0
    CHECK_SLOT(PC, OnBufferStart,              0x18/8);   // 0x180050210
    CHECK_SLOT(PC, OnBufferEnd,                0x20/8);   // 0x1800501d0
    CHECK_SLOT(PC, OnLoopEnd,                  0x28/8);   // 0x180050220
    CHECK_SLOT(PC, OnVoiceError,               0x30/8);   // 0x180050230
}

UNIT_TEST(b39_eng_platformchannel_ctor_makes_loop2_stores) {
    alignas(8) uint8_t buf[2 * 0x20];
    std::memset(buf, 0xCD, sizeof buf);
    auto* a = ::new (static_cast<void*>(buf)) Audio::Device::PlatformChannel();
    auto* b = ::new (static_cast<void*>(buf + 0x20)) Audio::Device::PlatformChannel();
    CHECK(a->voice == nullptr);
    CHECK(a->bufferEvent == nullptr);
    for (int i = 0x10; i < 0x18; ++i) CHECK_EQ((int)buf[i], 0xCD);   // loop 3's field
    CHECK(VTableOf(a) != nullptr);
    CHECK(VTableOf(a) == VTableOf(b));
    // A raw COM-shaped call through slot 2 lands in the member with `this`.
    using VoidFn = void (*)(void*);
    Reset(); (SlotAt<VoidFn, Audio::Device::PlatformChannel>(a, 0x10)(a)); CHECK_EQ(g_lastMark, 62); CHECK(g_lastSelf == a);
}

// ENDGAME B.3.9 ENG W2: Audio::Stream is built by its REAL ctor on
// `System::Thread(0x10000)` (retail 0x1800a0e55, EDX = 0x10000): the base body
// runs once, with that stack size, while the object wears THREAD's table; the
// Stream's own body then runs under the Stream table. ~Stream runs its body,
// then the compiler's ~Thread chain (retail inlines it).
UNIT_TEST(b39_eng_audio_stream_real_ctor_on_thread_0x10000) {
    g_systemStub_InitFields    = {};
    g_systemStub_ReleaseFields = {};
    alignas(8) unsigned char buf[sizeof(StreamProbe)];
    Reset();
    StreamProbe* s = ::new (static_cast<void*>(buf)) StreamProbe();
    CHECK_EQ(g_systemStub_InitFields.calls, 1);
    CHECK(g_systemStub_InitFields.self == static_cast<const void*>(s));
    CHECK_EQ(g_systemStub_InitFields.stackSize, 0x10000u);
    CHECK(g_systemStub_InitFields.vptrAtCall != *reinterpret_cast<void* const*>(s));
    CHECK_EQ(g_lastMark, 51);            // the Stream body, after the base
    s->~StreamProbe();
    CHECK_EQ(g_lastMark, 52);
    CHECK_EQ(g_systemStub_ReleaseFields.calls, 1);
    CHECK(g_systemStub_ReleaseFields.vptrAtCall == g_systemStub_InitFields.vptrAtCall);
}
