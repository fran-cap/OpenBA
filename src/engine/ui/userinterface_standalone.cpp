// ============================================================================
// src/engine/ui/userinterface_standalone.cpp — Wave 10C lane UIF6
// ============================================================================
//
// The two PRIVATE STATIC members UI::UserInterface::ShowDialog installs as
// MainLoop callbacks:
//
//   0x180120310  ?UpdateStandAloneUI@UserInterface@UI@@CAXAEBVTime@System@@PEAX@Z
//   0x1801203f0  ?DrawStandAloneUI@UserInterface@UI@@CAXAEAVContext@Video@@PEAX@Z
//
// They mangle `CAXA...` -- `C` is PRIVATE STATIC.
//
// WHAT THEY ARE. `ShowDialog` builds a NESTED UI::UserInterface for a modal
// dialog and appends these two callbacks to Engine::MainLoop's callback arrays:
// {DrawStandAloneUI, nestedUI} onto MainLoop+0x80 and {UpdateStandAloneUI,
// nestedUI} onto MainLoop+0x90. MainLoop::Frame then drives the nested UI
// without knowing it is nested. src/engine/ui/userinterface_showdialog.cpp
// documents that machinery in full and is the required reading for this file --
// including the retail quirk lane SHOW found, that `Frame` takes the callback
// *fn* from +0x90 but its *context* from +0x80 at the same index.
//
// The consumers: userinterface_showdialog.cpp takes the address of both to
// install them, and src/diag/modal_driver.cpp compares each MainLoop+0x90
// entry's fn against &UpdateStandAloneUI to RECOGNISE the installed entry.
//
// ---------------------------------------------------------------------------
// THE CALLEES
// ---------------------------------------------------------------------------
// Every callee of both bodies, by its exported name:
//   0x1801213c0  ?Update@UserInterface@UI@@QEAAHAEBVTime@System@@@Z   (PORTED, lane UI1)
//   0x180121890  ?Draw@UserInterface@UI@@QEAAXAEAVContext@Video@@@Z   (PORTED, lane UIDRAW)
//   0x1800c7f20  ??0Random@System@@QEAA@XZ
//   0x1800df6c0  ?Update@Scene@Audio@@QEAA_NVTime@System@@0AEAVRandom@4@
//                    AEBVVector3@Math@@M@Z
//   0x1801fe728  ?_secondsPerTick@Timer@System@@0NA                   (DATA)
// Each is a plain C++ call (the DATA row a read of the class static).
// ============================================================================

#include <cstdint>
#include <cstdio>

#include "system/Value.h"
#include "ui/UserInterface.h"
#include "audio/Audio.h"   // ENDGAME B.3.7 callers (K4): Audio::Scene
#include "system/Timer.h"  // ENDGAME B.3.9 SS1: Timer::_secondsPerTick
#include "element_common.h"


namespace {

// ---------------------------------------------------------------------------
// Callee signatures.
//
// `System::Time` is 8 bytes and is passed BY VALUE IN A REGISTER, not by hidden
// reference: 0x180120394 loads `MOV RDX,[RDI]` -- the VALUE behind the incoming
// `const Time&` -- and hands that straight to Audio::Scene::Update, whose two
// `VTime@System@@` parameters are by-value `System::Time` (B7-5).
// ---------------------------------------------------------------------------

// --- entry counters, printed at exit unless log-stripped (see userinterface_hover.cpp) ---
uint64_t g_updEntries   = 0;
uint64_t g_updWithScene = 0;   // took the audio arm
uint64_t g_drawEntries  = 0;

struct StandAloneSummary {
    ~StandAloneSummary() {
        printf("[RtPort] UI::UserInterface::UpdateStandAloneUI  entries=%llu withAudioScene=%llu\n",
                    (unsigned long long)g_updEntries, (unsigned long long)g_updWithScene);
        printf("[RtPort] UI::UserInterface::DrawStandAloneUI  entries=%llu\n",
                    (unsigned long long)g_drawEntries);
        fflush(stdout);
    }
};
StandAloneSummary g_standAloneSummary;

}  // namespace

// ============================================================================
// ?UpdateStandAloneUI@UserInterface@UI@@CAXAEBVTime@System@@PEAX@Z
//                                                          retail 0x180120310
// ============================================================================
// Ticks the nested UI, then ticks its audio scene with a FRESHLY SEEDED RNG.
//
// THE 0xa10-BYTE STACK FRAME IS THE TELL. Retail INLINES the whole of
// `System::Random`'s default constructor here -- `index = 0`, `mt[0] =
// (uint)_time64(NULL)`, then 623 rounds of the MT19937 init recurrence
// `mt[i] = 0x6c078965 * (mt[i-1] ^ (mt[i-1] >> 30)) + i`. That is 2500 bytes of
// PRNG state built on the stack every single tick, purely to hand
// Audio::Scene::Update something to draw from.
//
// THIS PORT CALLS THE EXPORTED CTOR INSTEAD OF RE-INLINING IT, and the
// equivalence was PROVEN, not assumed. ??0Random@System@@QEAA@XZ (0x1800c7f20)
// disassembles to the same three steps in the same order, including the same
// callee for the seed -- both reach 0x18007fb9c (`(uint)_time64(NULL)`) -- and
// the same 623-iteration count (the export counts down R8D=0x26f; the inline
// counts up to `R8D < 0x270` from 1, which is the same 623 writes to
// mt[1..623]). Re-inlining a recurrence we can call by name is how transcription
// defects get minted; src/engine/system/Value.h already pins the layout.
//
// The `0.0 / _secondsPerTick` is retail's, kept as a division rather than folded
// to the 0 it always evaluates to, for the same reason as in EndHover: it tracks
// the constant Engine::Startup installs. It is the `elapsed` Time argument.
//
// The fifth argument is a zeroed 12-byte Math::Vector3 (the listener position),
// and the sixth a 0.0f volume -- 0x1801203a7 and 0x1801203b8 write exactly
// those, and the export's own signature (`AEBVVector3@Math@@M`) names both.
// ============================================================================
void UI::UserInterface::UpdateStandAloneUI(const System::Time& timeRef, void* ui) {
    ++g_updEntries;

    // 0x180120329 -- note the argument SWAP: retail is a static taking
    // (time, ui) and Update is a member taking (this=ui, time). `ui` is the
    // callback's `void*` user data (retail's `PEAX`).
    UserInterface* const self = static_cast<UserInterface*>(ui);   // callback context: retail's message-callback user data is void* (PEAX, 0x180120310), viewed as the UserInterface that registered it  // CAST-OK: user_data
    self->Update(timeRef);

    // 0x180120336..0x180120386 -- the inlined System::Random default ctor
    // (0x1800c7f20); no dtor.
    System::Random rng;

    Audio::Scene* const scene = self->_audioScene;                             // 0x180120388, +0x90
    if (scene) {                                                       // 0x18012038f
        ++g_updWithScene;
        const Vector3 listener(0.0f, 0.0f, 0.0f);                      // 0x1801203a7
        // ENDGAME B.3.9 SS1 (B39_RULES D7): the class static now, read fresh.
        const int64_t elapsed = static_cast<int64_t>(0.0 / ::System::Timer::_secondsPerTick);      // 0x1801203b0
        System::Time now;     now._ticks     = timeRef._ticks;
        System::Time elapsedT; elapsedT._ticks = elapsed;
        scene->Update(
            now, elapsedT, rng, listener, 0.0f);                       // 0x1801203d0
    }
}


// ============================================================================
// ?DrawStandAloneUI@UserInterface@UI@@CAXAEAVContext@Video@@PEAX@Z
//                                                          retail 0x1801203f0
// ============================================================================
// FOURTEEN BYTES. The entire function is an argument swap and a tail jump:
//
//   1801203f0  48 8b c2   MOV RAX,RDX      ; RAX = ui        (param 2)
//   1801203f3  48 8b d1   MOV RDX,RCX      ; RDX = context   (param 1)
//   1801203f6  48 8b c8   MOV RCX,RAX      ; RCX = ui        (the `this`)
//   1801203f9  e9 92140000 JMP 0x180121890 ; UI::UserInterface::Draw
//
// It exists only to adapt MainLoop's (context, userData) callback shape to
// Draw's (this, context) member shape.
//
// NOTE FOR ANYONE RE-DERIVING THIS: Ghidra has NO FUNCTION at 0x1801203f0 --
// `decompile_function` and `disassemble_function` both refuse it, and
// `read_memory` fails on the range even though `disassemble_bytes` reports
// success. These bytes were read straight out of the shipped PE and
// disassembled with objdump instead. A lane that trusted the MCP error and
// recorded "no such function" would have been wrong.
// ============================================================================
void UI::UserInterface::DrawStandAloneUI(Video::Context& context, void* ui) {
    ++g_drawEntries;
    static_cast<UserInterface*>(ui)->Draw(context);   // the tail JMP  // callback context: retail's draw-callback user data is void* (PEAX, 0x1801203f0), viewed as the UserInterface that registered it  // CAST-OK: user_data
}
