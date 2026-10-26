#pragma once
// ============================================================================
// src/game/Game/GameLoadDialogLoopT.h — ENDGAME B.3.8 block LOOP (B38_RULES D8)
//
// The exe's `Engine::DialogLoaderLoopT` subclass: the loading/splash screen
// (Dialog\ShiningRock.rsc, Dialog\Loading.rsc, "Dialog/Loading.rsc:gameLoad%i")
// shown while the Engine::Loader thread loads the loop it wraps. Unreflected.
//
// RETAIL (Application-x64.exe, B38_SLOTS.md sec. 6, B37_RULES sec. 3.1):
//   table 0x140158800, 13 slots, sizeof 0xb60 (MemAlloc(0xb60, 4) at every site)
//   [0]  0x140001010  own scalar deleting dtor (~DialogLoaderLoopT + MemFree)
//   [1]  0x140098fa0  own Startup (tail-calls DialogLoaderLoopT::Startup)
//   [2]  0x140098f90  own Shutdown: a bare `RET 0`
//   [3]  0x140098da0  own Update (random loading tip + the LoaderLoop advance)
//   [4]  IMPORT ?Draw@DialogLoaderLoopT@      [5]  IMPORT ?DrawFailed@UpdateLoop@
//   [6]  IMPORT ?Load@DialogLoaderLoopT@      [7]  IMPORT ?SaveRestore@UpdateLoop@
//   [8]  IMPORT ?OnVideoError@LoaderLoop@     [9]  IMPORT ?OnVideoRestart@UpdateLoop@
//   [10] IMPORT ?GetResourceFilename@UpdateLoop@   [11] IMPORT ?AllowsModChanges@UpdateLoop@
//   [12] 0x140099070  own CreateDialog: the LoaderDialog over _dialogExternal
//   The ctor is inlined at each of the 7 construction sites:
//     ??0DialogLoaderLoopT@Engine@@(filename, next, delay); stamp 0x140158800;
//     +0x188 = +0x18c = 0xff000000; +0x190 = 0; ??0Random@System@@ on +0x194;
//     +0xb58 = 0; +0x40 (_autoAdvance) = 0.
//
// Slots 5-11 are INHERITED from the Engine:: bases, as retail's table holds
// their imports; slot 4 is overridden only as the divergence noted there.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "engine/UpdateLoop.h"   // -Iruntime/types: Engine::DialogLoaderLoopT
#include "system/Value.h"        // -Iruntime/types: System::Random (by value at +0x194)

class GameLoadDialogLoopT final : public Engine::DialogLoaderLoopT {
public:
    // The inlined retail ctor (header banner).
    GameLoadDialogLoopT(const System::String& filename, Engine::UpdateLoop* nextLoop,
                        const System::Time& delay);

    ~GameLoadDialogLoopT() override;                             // [0] 0x140001010
    void Startup() override;                                     // [1] 0x140098fa0
    void Shutdown() override;                                    // [2] 0x140098f90 `RET 0`
    bool Update(const System::Time& time) override;              // [3] 0x140098da0
    // [4] retail: DialogLoaderLoopT::Draw. Overridden only to wrap that base body
    // with the boot-window reveal — STANDALONE DIVERGENCE (splash_loop_stubs.cpp).
    void Draw(float interpolation) override;
    Object::InternalPtr<UI::DialogInstance>* CreateLoopDialog(   // [12] 0x140099070
        Object::InternalPtr<UI::DialogInstance>* ret) override;

public:
    uint32_t       _fadeColor1;   // +0x188  ctor 0xff000000
    uint32_t       _fadeColor2;   // +0x18c  ctor 0xff000000
    uint8_t        _fadeDone;     // +0x190  ctor 0
    uint8_t        _pad191[3];    // +0x191  never written
    System::Random _random;       // +0x194  ??0Random@System@@ (the loading-tip pick)
    int64_t        _tipTimer;     // +0xb58  ticks since the last tip (2.0 s period)
};

static_assert(sizeof(GameLoadDialogLoopT) == 0xb60, "GameLoadDialogLoopT is 0xb60 (MemAlloc(0xb60, 4) at every site)");
static_assert(offsetof(GameLoadDialogLoopT, _pad191) == 0x191, "GameLoadDialogLoopT::_pad191");
static_assert(alignof(GameLoadDialogLoopT) == 8, "qword-aligned (the vptr)");
static_assert(offsetof(GameLoadDialogLoopT, _fadeColor1) == 0x188, "+0x188");
static_assert(offsetof(GameLoadDialogLoopT, _fadeColor2) == 0x18c, "+0x18c");
static_assert(offsetof(GameLoadDialogLoopT, _fadeDone)   == 0x190, "+0x190");
static_assert(offsetof(GameLoadDialogLoopT, _random)     == 0x194, "+0x194 System::Random");
static_assert(offsetof(GameLoadDialogLoopT, _tipTimer)   == 0xb58, "+0xb58 tip timer");
