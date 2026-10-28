#pragma once
// ============================================================================
// src/engine/engine/MainLoop.h — Engine::MainLoop, binary-compatible layout
// ============================================================================
//
// Wave 8 lane B. `Engine::MainLoop` is the frame driver: it owns the video
// context, the current/next `UpdateLoop`, the fixed-timestep clock, and the
// video-resource bookkeeping that survives a device loss.
//
// Recovered source file name (profile build, Runtime-x64-profile.dll, the
// assert at 0x1800d54dc): `Engine\MainLoop.cpp`.
//
// ---------------------------------------------------------------------------
// THIS SUPERSEDES engine/types/MainLoop.h (the hook-era header)
// ---------------------------------------------------------------------------
// That header got the SIZE and every OFFSET right — 0xa0 bytes, and all 21
// offsets below match it. It got two things wrong, both of which matter to a
// port and neither of which matters to a reader who only wants to peek:
//
//   1. It calls +0x0c, +0x8c and +0x9c padding. They are not padding: each of
//      +0x00, +0x80 and +0x90 is a `Collection::Array<T>` — `{T* _data;
//      int32 _size; int32 _capacity}` — and those three "pad" dwords are the
//      CAPACITY. Proof: `~MainLoop` (0x1800dd640) calls a distinct Array
//      destructor on each of the three (0x180033a40 for +0x90, 0x180033960
//      for +0x80, and tail-jumps to 0x180033880 for +0x00), and 0x180033880
//      ends with `_data = 0` followed by a single qword zero covering BOTH
//      +0x08 and +0x0c. Code that grows either callback array through
//      `AddPostUpdate`/`AddPostDraw` reads the capacity; a port that treats it
//      as padding would reallocate on every append.
//   2. Its `IsRunning = 0x1800314b0` address constant was commented as a no-arg
//      query. The shipped export is
//      `?IsRunning@MainLoop@Engine@@QEBA_NPEBVUpdateLoop@2@@Z` (ordinal 3158)
//      — it takes a `const UpdateLoop*`. The no-arg `IsRunning` in the export
//      table is `UpdateLoop`'s (0x1800dd7b0, ordinal 3160), a different class.
//
// The hook-era header is left in place for the exe tree that includes it; the
// two corrections are applied there as well (PLAN §6 step 8). New runtime-port
// code includes THIS header.
//
// ---------------------------------------------------------------------------
// THERE IS NO VTABLE
// ---------------------------------------------------------------------------
// `??_7MainLoop@Engine@@6B@` is not exported, and per WAVE8.md §2 that is not
// proof on its own. The proof is the ctor (0x1800dd6b0): its first store into
// `this` is
//        1800dd6c6  XOR EAX,EAX
//        1800dd6c8  MOV qword ptr [RCX],RAX
// a ZERO — the `_videoResources._data` pointer — not a vtable address. The
// profile build's ctor (0x1800d5420) opens with the identical zero store. No
// `MainLoop` method is virtual and nothing in the class is reached through a
// vtable, so PLAN §4.3 risk 3 does not apply here either.
//
// ---------------------------------------------------------------------------
// EVIDENCE FOR EVERY OFFSET
// ---------------------------------------------------------------------------
//   off    field                  proven by                             retail
//   -----  ---------------------  ------------------------------------  -----------
//   0x00   _videoResources        ~MainLoop's third Array dtor;         0x1800dd640
//                                 OnVideoError appends 0x18-byte
//                                 entries to it, OnVideoRestart walks   0x1800dc5e0
//                                 it with `ADD RSI,0x18`                0x1800dc3b0
//   0x10   _context               CreateVideoContext stores the         0x1800dd500
//                                 0x2168-byte Video::Context here;
//                                 ?GetContext@ returns *(this+0x10)     0x1800314d0
//   0x18   _contextDirty          ctor zeroes +0x18 and +0x19 as one
//                                 word; Frame/Draw read it              0x1800dd100
//   0x19   _quit                  Run's loop condition,
//                                 `CMP byte ptr [RSI+0x19],0`           0x1800dd44a
//   0x20   _currentLoop           Run sets it from its argument;
//                                 ?GetCurrentLoop@ returns it           0x1800314e0
//   0x28   _nextLoop              the ENTIRE body of ?SetNextLoop@:
//                                 `MOV [RCX+0x28],RDX; RET`             0x1800dd7a0
//   0x30   _startTicks            Run's first QPC conversion            0x1800dd404
//   0x38   _accumulator           Frame                                 0x1800dd100
//   0x40   _totalTime             Frame                                 0x1800dd100
//   0x48   _deltaTime             ctor: SecondsToTicks(1.0f/60.0f);
//                                 ?GetDeltaTime@ returns it             0x1800314f0
//   0x50   _maxAccumulator        ctor: SecondsToTicks(1.0f/15.0f)      0x1800dd751
//   0x58   _frameTime             Run: _frameEndTicks - _frameStartTicks;
//                                 ?GetFrameTime@                        0x180031500
//   0x60   _frameStartTicks       Run, per iteration;
//                                 ?GetFrameStartTime@                   0x180031510
//   0x68   _frameUpdateTime       ?GetFrameUpdateTime@ /
//                                 ?MarkFrameUpdateTime@                 0x180031520
//   0x70   _frameEndTicks         Run, per iteration                    0x1800dd4ae
//   0x78   _engine                ctor arg (RDX); Run reads the
//                                 subsystems through it                 0x1800dd40e
//   0x80   _postDraw              ?AddPostDraw@ does `ADD RCX,0x80`
//                                 then Array::SetSize                   0x1800dd040
//   0x90   _postUpdate            ?AddPostUpdate@ does `ADD RCX,0x90`   0x1800dcf60
//
// ---------------------------------------------------------------------------
// THE TWO CLOCK CONSTANTS ARE FLOATS WIDENED TO DOUBLE — DO NOT RETYPE THEM
// ---------------------------------------------------------------------------
// Retail's ctor computes the two timing fields as
//        MOVSD XMM0, [0x1801616a8]      ; 0x3f91111120000000
//        DIVSD XMM0, [0x1801fe728]      ; seconds per QPC tick
//        CVTTSD2SI RAX, XMM0
// and the profile build shows what that inlined FROM:
//        MOVSS XMM0, [0x1801b04e4]      ; a 32-bit float
//        CALL  System::Timer::SecondsToTicks(float)
// 0x3f91111120000000 is exactly `(double)(1.0f/60.0f)` and 0x3fb1111120000000
// is exactly `(double)(1.0f/15.0f)` — i.e. the source wrote a float, and the
// compiler widened it. Writing `1.0/60.0` in a port would produce a different
// double and a different tick count. Use the bit patterns.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <type_traits>
// ENDGAME B.3.7: NOT "../common.h" -- nothing here uses it, and its GLOBAL
// `struct ImageBuffer;` forward declarations clash with the video tree's global
// `using RtVideo::ImageBuffer;` (src/engine/video/ImageBuffer.h) in
// video/context_ctor.cpp, which now defines MainLoop::CreateVideoContext.
#include "../collection/Collection.h"
#include "../system/Value.h"      // System::Time (_deltaTime)
#include "../system/Filename.h"   // VideoResourceEntry::_path

namespace Object { class External; }

// Everything below lives in `RtEngine`, matching src/engine/engine/UpdateLoop.h
// (lane A) and the RtVideo / RtObject convention the earlier waves set. This is
// NOT cosmetic: engine/types/MainLoop.h defines a GLOBAL `struct MainLoop`,
// engine/types/UpdateLoop.h a GLOBAL `struct UpdateLoop`, and `engine/types` is
// on the include path AHEAD of `runtime/types` — at global scope these two
// headers would be a hard redefinition in any TU that included both trees, and
// `_currentLoop`/`_nextLoop` would point at a DIFFERENT, incomplete UpdateLoop
// from the one lane A defines.
namespace RtEngine {

struct UpdateLoop;   // defined by lane A in src/engine/engine/UpdateLoop.h
// (`RtEngine::Engine` is an ALIAS of the real class since B.3.7: Engine.h.)

// B3R (registry teardown, W1 RT-Core): the retail ADDRESS constants that
// stood here (Ctor 0x1800dd6b0 .. OneOverFifteen 0x1801616b0) had no code
// reader and are gone. What is left are two byte OFFSETS the ctor's inlined
// Video::Device::SetVideoErrorFuncs writes (engine.cpp reads them).
namespace MainLoopAddr {
    // Video::Device::SetVideoErrorFuncs(err, restart) — the name is the
    // profile build's (0x1800fd860); retail inlines it to two stores at
    // Device +0x50 / +0x58 of `Video::Device::_singleton` (the class static).
    constexpr uintptr_t DeviceErrorFuncSlot   = 0x50;
    constexpr uintptr_t DeviceRestartFuncSlot = 0x58;
}

// One entry of MainLoop::_videoResources. Built by OnVideoError from the live
// object graph and consumed by OnVideoRestart:
//   OnVideoError  0x1800dca4d: entry._typeId = obj->vtable[0x10]()   // GetTypeId
//                 0x1800dca50: entry._object = obj
//                 0x1800dcac6: entry._name   = <the object's resource name>
//   OnVideoRestart 0x1800dc416: reads _typeId, 0x1800dc440: _object,
//                 0x1800dc4b3: _path, and strides by 0x18.
// B.4 (R-ENG): `_object` is the Object::External* it holds and `_path` the
// System::Filename (OnVideoError builds it from External::GetPath; OnVideoRestart
// hands it to ExternalFactory::GetCompiledPath). The array's element copy and
// destroy stay the hand-derived `kSpec_VideoResource` (mainloop_video.cpp) and
// ~MainLoop is declared-undefined, so the Filename's dtor adds no release.
struct VideoResourceEntry {
    int32_t              _typeId;   // +0x00
    int32_t              _pad04;    // +0x04  never written by either function
    ::Object::External*  _object;   // +0x08
    ::System::Filename   _path;     // +0x10  refcounted
};
static_assert(sizeof(VideoResourceEntry) == 0x18, "VideoResourceEntry is 0x18 bytes");
static_assert(offsetof(VideoResourceEntry, _pad04) == 0x4, "VideoResourceEntry::_pad04");
static_assert(offsetof(VideoResourceEntry, _typeId) == 0x00, "VideoResourceEntry::_typeId");
static_assert(offsetof(VideoResourceEntry, _object) == 0x08, "VideoResourceEntry::_object");
static_assert(offsetof(VideoResourceEntry, _path)   == 0x10, "VideoResourceEntry::_path");

// One entry of _postUpdate / _postDraw. Both AddPostUpdate (0x1800dcf60) and
// AddPostDraw (0x1800dd040) build the element from RDX and R8 in a 16-byte
// stack temp and hand it to Array::SetSize, so the element is exactly this
// pair. The two arrays use DIFFERENT Array instantiations (0x18004f8e0 vs
// 0x18004f790) because the callback signatures differ — Update takes a
// `const Time&`, Draw takes a `float` — but the element WIDTH is the same.
// B.4 wave X: one element template, instantiated once per array with that
// array's function type (`MainLoop::PostDrawFunc` / `PostUpdateFunc`), so
// neither array's `_func` is a `void*` its reader reinterprets.
template <class Func>
struct MainLoopCallback {
    Func  _func;      // +0x00
    void* _context;   // +0x08  passed back as the callback's userData argument
};
static_assert(sizeof(MainLoopCallback<void (*)()>) == 0x10, "MainLoopCallback is 0x10 bytes");
static_assert(offsetof(MainLoopCallback<void (*)()>, _context) == 0x08, "MainLoopCallback::_context");

}  // namespace RtEngine

// ############################################################################
// Engine::MainLoop -- THE REAL CLASS (ENDGAME B.3 step 7, block B1)
// ############################################################################
//
// NON-POLYMORPHIC and CONCRETE (B37_RULES sec. 1 item 5: the ctor's first store
// is a zero, `??1MainLoop@Engine@@QEAA@XZ` is `QEAA`). The Wave 8 POD that used
// to be spelled here was field-identical and nothing embeds it, so the POD
// BECOMES the class (B7-6) and `RtEngine::MainLoop` is an alias of it.
//
// HEAP-ONLY, BUILT THROUGH THE DOOR (B6-10): the exe's GameMain allocates
// 0xa0 bytes (`MemAlloc(0xa0,8)` 0x140041f77) and calls the imported
// `??0MainLoop@Engine@@QEAA@PEAV11@@Z`; that ctor and `??1` stay the registered
// DOORS in src/engine/engine/engine.cpp, and are declared here only so no
// caller batch has to edit this header.
//
// `?_singleton@MainLoop@Engine@@1PEAV12@EA` (0x1801fe750) STAYS a data slot
// until the end of B.3 (carry-over 5). engine/funcs/engine_funcs reads it as
// `g_pMainLoopSingleton` (the misnamed second global `g_pEngineSingleton`,
// B37_RULES sec. 6.3 defect 3, was folded into it at the B.3.7 close).
//
// Every by-value `System::Time` RETURN below is a retslot export in retail;
// none of the four has a caller, so they are declared and left undefined.
// ############################################################################

namespace System { class Time; }
namespace Video  { class Context; }

namespace Engine {

class UpdateLoop;   // src/engine/engine/UpdateLoop.h (this block)
class Engine;       // src/engine/engine/Engine.h     (this block)

class MainLoop {
public:
    using PostUpdateFunc = void (*)(const System::Time& time, void* userData);
    using PostDrawFunc   = void (*)(Video::Context& context, void* userData);

    // ---- lifetime: DOORS (engine.cpp), declared, deliberately undefined ----
    explicit MainLoop(Engine* engine);                             // 0x1800dd6b0  door RtEngine_MainLoop_Ctor
    ~MainLoop();                                                   // 0x1800dd640  door RtEngine_MainLoop_Dtor

    // ---- PORTED --------------------------------------------------------------
    void Run(UpdateLoop* loop);                                    // 0x1800dd3c0  mainloop_run.cpp
    bool Update(const System::Time& time);                         // 0x1800dd070  mainloop_run.cpp
    bool Frame(bool skipUpdate);                                   // 0x1800dd100  mainloop_frame.cpp
    void Draw(float interpolation);                                // 0x1800dcd40  loops.cpp
    void SetNextLoop(UpdateLoop* loop);                            // 0x1800dd7a0  engine.cpp
    void OnVideoError();                                           // 0x1800dc5e0  mainloop_video.cpp
    void OnVideoRestart();                                         // 0x1800dc3b0  mainloop_video.cpp
    // The two static thunks the ctor installs into Video::Device +0x50/+0x58:
    // `MOV RCX,[_singleton]; JMP <member>`. engine.cpp.
    static void OnVideoError_();                                   // 0x1800dcd20
    static void OnVideoRestart_();                                 // 0x1800dc5c0
    // AEAA (private) in retail; public here because the registered door is a
    // free function that has to call it (access control is not ABI).
    void CreateVideoContext();                                     // 0x1800dd500  video/context_ctor.cpp

    // ---- exported one-line accessors: faithful inline bodies, NOT REGISTERED
    Video::Context& GetContext() { return *_context; }             // 0x1800314d0  MOV RAX,[RCX+0x10]
    UpdateLoop* GetCurrentLoop() { return _currentLoop; }          // 0x1800314e0  MOV RAX,[RCX+0x20]

    // ---- exported, NOT PORTED, no caller: declared, deliberately undefined --
    static MainLoop* Get();                                        // 0x180031570
    // ?_singleton@MainLoop@Engine@@1PEAV12@EA (0x1801fe750) — the class static (ENDGAME B.3.9 ENG, B39_RULES D7).
    // Its minted storage row is RETIRED (ENDGAME B.3.9 close): no name or
    // address door is left. PUBLIC: foreign readers name it directly.
    static MainLoop* _singleton;

    // EXT SEAM (NOT RETAIL, docs/extensions/handins/RENDERPIPE0.md): ext.render_pipe's frame hook.
    // Null by default (DESIGN.md section 3, hook rule 5); src/ext/render_pipe sets it
    // while the class A extension is installed. Frame calls it at two points:
    //   kExtBeforeSteps  once, before the first fixed step of a Frame that steps
    //                    (the drain: no render work is pending while the sim runs);
    //   kExtFrameTail    in place of the tail's `_time` store and `Draw(alpha)`
    //                    (`alpha` and `time` are the tail's two computed floats).
    // and Update at a third (docs/android/P13_OVERLAP.md):
    //   kExtBeforeTransition  before the loop transition's Shutdown and delete (the
    //                    render thread is joined: nothing it reads is freed under it).
    // Null: Frame is retail's body plus the two null tests, Update plus one.
    static constexpr int32_t kExtBeforeSteps      = 0;
    static constexpr int32_t kExtFrameTail        = 1;
    static constexpr int32_t kExtBeforeTransition = 2;
    using ExtFrameFn = void (*)(MainLoop& loop, int32_t point, float alpha, float time);
    static ExtFrameFn _extFrame;   // defined in mainloop_frame.cpp
    // EXT SEAM (NOT RETAIL, docs/extensions/handins/HITCH_SAVESNAP.md, HITCH_BGSAVE.md): ext.background_save's video-error barrier.
    // Null by default; while the class A extension is installed, OnVideoError calls it first:
    // it returns once no save worker is reading the world (the route below destroys the video
    // Externals and the loop releases the terrain and minimap images the worker serialises).
    using ExtVideoErrorBarrierFn = void (*)();
    static ExtVideoErrorBarrierFn _extVideoErrorBarrier;   // defined in mainloop_video.cpp
    // FIX SEAM (NOT RETAIL, docs/retail_bugs.md section 10): fix.boot_device_lost. Null by default
    // (src/ext/registry_boot.cpp points it at the live bool). While it reads true, OnVideoError and
    // OnVideoRestart skip the Save::SaveLoad walks when no SaveLoad exists yet (a device loss before
    // MenuDialogLoopT::Startup's end constructs the first one); retail dereferences the null.
    // The row's other seam is the backend's (src/render/common/boot_device_lost/).
    static const bool* _extFixBootDeviceLost;   // defined in mainloop_video.cpp
    bool IsRunning(const UpdateLoop* loop) const;                  // 0x1800314b0
    System::Time GetDeltaTime() const;                             // 0x1800314f0  retslot
    System::Time GetFrameTime() const;                             // 0x180031500  retslot
    System::Time GetFrameStartTime() const;                        // 0x180031510  retslot
    System::Time GetFrameUpdateTime() const;                       // 0x180031520  retslot
    void MarkFrameUpdateTime();                                    // 0x180031530
    void AddPostUpdate(PostUpdateFunc func, void* userData);       // 0x1800dcf60
    void RemovePostUpdate(PostUpdateFunc func, void* userData);    // 0x1800dceb0
    void AddPostDraw(PostDrawFunc func, void* userData);           // 0x1800dd040
    void RemovePostDraw(PostDrawFunc func, void* userData);        // 0x1800dcf90
    void ReleaseVideoContext();                                    // 0x1800dd5e0  AEAA
    MainLoop(const MainLoop& other);                               // 0x180031580
    MainLoop& operator=(const MainLoop& other);                    // 0x180031680
    static void* operator new(size_t bytes);                       // 0x1800313d0
    static void* operator new(size_t bytes, void* at);             // 0x180031440
    static void  operator delete(void* p);                         // 0x180031450

    // ---- the 0xa0 bytes, evidence per field in the table at the top ---------
    // PUBLIC: the doors and the unconverted readers reach them.
    // Video resources captured on device loss and restored on device restart.
    Collection::Array<RtEngine::VideoResourceEntry> _videoResources;  // +0x00

    Video::Context* _context;     // +0x10  owned; 0x2168 bytes (B.4 R-VIDEO: H4 typing)
    uint8_t  _contextDirty;       // +0x18  loop switch asks for a fresh context
    uint8_t  _quit;               // +0x19  Run's exit flag
    uint8_t  _pad1a[6];           // +0x1a  ctor zeroes +0x18/+0x19 as one word;
                                  //        +0x1a..+0x1f are never written

    UpdateLoop* _currentLoop;     // +0x20
    UpdateLoop* _nextLoop;        // +0x28  SetNextLoop's only store

    // All times are QPC ticks converted through System::Timer's
    // (qpc - base) * mul / div, NOT raw QPC counts.
    //
    // `_startTicks` means "the tick at which the LAST FRAME was sampled": Frame
    // rewrites it every call (0x1800dd146) and uses the previous value as the
    // delta base; Run's single pre-loop store (0x1800dd404) only seeds it.
    int64_t  _startTicks;         // +0x30
    int64_t  _accumulator;        // +0x38  fixed-step carry
    int64_t  _totalTime;          // +0x40
    // A System::Time: Frame / Run hand `&_deltaTime` to Update(const Time&)
    // and to Input::Device::Update (`LEA RDX,[RDI+0x48]`); GetDeltaTime
    // (0x1800314f0) returns it by value.
    System::Time _deltaTime;      // +0x48  SecondsToTicks((float)1/60)
    int64_t  _maxAccumulator;     // +0x50  SecondsToTicks((float)1/15) -- 4 steps
    int64_t  _frameTime;          // +0x58  _frameEndTicks - _frameStartTicks
    int64_t  _frameStartTicks;    // +0x60
    int64_t  _frameUpdateTime;    // +0x68
    int64_t  _frameEndTicks;      // +0x70

    Engine*  _engine;             // +0x78  ctor arg; NOT owned

    using PostDrawCallback   = RtEngine::MainLoopCallback<PostDrawFunc>;
    using PostUpdateCallback = RtEngine::MainLoopCallback<PostUpdateFunc>;
    Collection::Array<PostDrawCallback>   _postDraw;    // +0x80
    Collection::Array<PostUpdateCallback> _postUpdate;  // +0x90
};

static_assert(sizeof(MainLoop) == 0xa0, "MainLoop is 0xa0 bytes (exe MemAlloc(0xa0,8) 0x140041f77)");
static_assert(offsetof(MainLoop, _pad1a) == 0x1A, "MainLoop::_pad1a");
static_assert(alignof(MainLoop) == 8, "qword-aligned");
static_assert(!std::is_polymorphic<MainLoop>::value,
              "Engine::MainLoop has no vtable: the ctor's first store is a zero");
static_assert(offsetof(MainLoop, _videoResources)  == 0x00, "MainLoop::_videoResources");
static_assert(offsetof(MainLoop, _context)         == 0x10, "MainLoop::_context");
static_assert(offsetof(MainLoop, _contextDirty)    == 0x18, "MainLoop::_contextDirty");
static_assert(offsetof(MainLoop, _quit)            == 0x19, "MainLoop::_quit");
static_assert(offsetof(MainLoop, _currentLoop)     == 0x20, "MainLoop::_currentLoop");
static_assert(offsetof(MainLoop, _nextLoop)        == 0x28, "MainLoop::_nextLoop");
static_assert(offsetof(MainLoop, _startTicks)      == 0x30, "MainLoop::_startTicks");
static_assert(offsetof(MainLoop, _accumulator)     == 0x38, "MainLoop::_accumulator");
static_assert(offsetof(MainLoop, _totalTime)       == 0x40, "MainLoop::_totalTime");
static_assert(offsetof(MainLoop, _deltaTime)       == 0x48, "MainLoop::_deltaTime");
static_assert(offsetof(MainLoop, _maxAccumulator)  == 0x50, "MainLoop::_maxAccumulator");
static_assert(offsetof(MainLoop, _frameTime)       == 0x58, "MainLoop::_frameTime");
static_assert(offsetof(MainLoop, _frameStartTicks) == 0x60, "MainLoop::_frameStartTicks");
static_assert(offsetof(MainLoop, _frameUpdateTime) == 0x68, "MainLoop::_frameUpdateTime");
static_assert(offsetof(MainLoop, _frameEndTicks)   == 0x70, "MainLoop::_frameEndTicks");
static_assert(offsetof(MainLoop, _engine)          == 0x78, "MainLoop::_engine");
static_assert(offsetof(MainLoop, _postDraw)        == 0x80, "MainLoop::_postDraw");
static_assert(offsetof(MainLoop, _postUpdate)      == 0x90, "MainLoop::_postUpdate");

}  // namespace Engine

// B7-6: one definition per layout. Every `RtEngine::MainLoop` spelling in the
// runtime tree now names the class.
namespace RtEngine { using MainLoop = ::Engine::MainLoop; }

// ==== B3R G-Game (R5): Runtime bodies code/Game calls by name ====
extern "C" ::Engine::MainLoop* RtEngine_MainLoop_Ctor(::Engine::MainLoop* self, ::Engine::Engine* engine);  // 0x1800dd6b0
extern "C" void RtEngine_MainLoop_Dtor(::Engine::MainLoop* self);  // 0x1800dd640
// ==== end B3R G-Game ====
