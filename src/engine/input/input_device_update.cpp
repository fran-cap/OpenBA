// ============================================================================
// src/engine/input/input_device_update.cpp — Wave 9 lane ENGIO
//   Input::Device::Update and the three internal bodies it calls.
// ============================================================================
//
//   ?Update@Device@Input@@QEAAXAEBVTime@System@@@Z   retail 0x1800E1490  ord 4498
//   Device::UpdateState                              retail 0x1800E0FC0  ord 4528
//   Device::PlatformGetKeyboardState                 retail 0x1800E1C60  ord 3381
//   Device::PlatformGetMouseState                    retail 0x1800E1E10  ord 3382
//   Device::PlatformUpdate                           retail 0x1800E2130  — ALREADY
//       PORTED by Wave 9 lane IOA (input_platform_update.cpp). Called directly; no door.
//
// The three names are Ghidra's, recovered from the profile DLL's debug
// strings. WAVE9_LANE_INPUT.md calls the same three addresses DispatchEvents,
// PollKeyboard and PollMouse; the Ghidra spellings are used here so a reader
// can find them in the project database.
//
// CORRECTION (batch 7, 2026-09-12): this file used to claim all three had "no
// mangled export in either the retail or the profile DLL" and registered none
// of them. That was WRONG in the `reference_aeaa_is_private_not_unexported.md`
// direction: checked with pefile against the retail export directory (not
// assumed), all three ARE exported --
//   0x1800e0fc0  ord 4528  ?UpdateState@Device@Input@@AEAAXPEAUState@12@0HAEBVTime@System@@@Z
//   0x1800e1c60  ord 3381  ?PlatformGetKeyboardState@Device@Input@@AEAA_NPEAUState@12@@Z
//   0x1800e1e10  ord 3382  ?PlatformGetMouseState@Device@Input@@AEAA_NPEAUState@12@@Z
// `AEAA` is PRIVATE, not unexported. So a by-name `ResolveExport` of any of the
// three used to fall through to the DLL silently -- exactly the
// half-registered-port trap other files in this directory warn about. All
// three are now registered by NAME and by ADDRESS below, in
// `RtInputDeviceUpdateRegister`.
//
// This is the whole of the row WAVE9.md called the hardest single item in the
// wave, which two earlier lanes sized and declined. Both of their measurements
// held up under transcription:
//   * lane SYS2: 626 instructions across five bodies, one of them already ours,
//     and Update itself is a trivial 61-instruction shell.
//   * lane INPUT: the COM surface is EXACTLY TWO IDirectInputDevice8 vtable
//     slots, +0x38 Acquire and +0x48 GetDeviceState, the same two in both
//     pollers, with everything else being USER32 resolved by name.
//
// ---------------------------------------------------------------------------
// THE BLOCKER THAT ACTUALLY STOOD, AND HOW IT IS ANSWERED
// ---------------------------------------------------------------------------
// Not the size, and NOT "needs an attended run with a real keyboard and mouse"
// — lane INPUT closed that claim with 1,914 comparisons and no keyboard, and
// nothing below reads a human. The blocker that stood is that **Update DRAINS
// the DirectInput queue**: an ours-then-theirs replay at the live call site
// hands the second body a queue our own pass already emptied, so the two sides
// see different worlds BY CONSTRUCTION and every comparison is meaningless —
// green or red.
//
// Lane INPUT specified the fix as a GetDeviceState RECORDER: one detour on a
// COM slot, the ProbeAudioScene shape. Building it, the recorder turned out to
// be unnecessary, and the reason is the rule that dissolved three of six Wave 8
// audio holdbacks for free — **ask what the guard that reaches the dependency
// actually READS.** These two functions read this->[0x11E0] / this->[0x11E8],
// then two slots of that object's own vtable. Nothing validates the pointer,
// nothing QueryInterfaces it, nothing else in either body touches it. So a
// **synthetic COM device we own outright** — an object whose vtable is an array
// of ours — IS a DirectInput device as far as these functions can tell, and it
// beats a detour in the one way that matters here: a scripted GetDeviceState
// hands BOTH sides the same bytes, so the drain stops existing rather than
// being recorded around (reference_synthetic_subject_beats_a_fake.md).
//
// That probe is input_device_update_probe.cpp (RT_INPUTUPD_PROBE=1).
// The LIVE registration below carries the diff oracle + the diff oracle and NOT a
// replay, for the drain reason above — said here rather than left to be
// inferred from an absent macro.
//
// ---------------------------------------------------------------------------
// THE DOOR CLOSURE, COUNTED BEFORE A LINE WAS WRITTEN
// ---------------------------------------------------------------------------
// disassemble_function(filter_mnemonics="CALL") on each body. Every callee is
// USER32 (linked directly by MinGW — no door), a minted DATA slot we already
// own, or a COM slot on an object the CALLER supplies:
//
//   Device::Update             -> the three below, then PlatformUpdate (ours)
//   Device::UpdateState        -> **ZERO CALLS. A pure leaf.**
//   PlatformGetKeyboardState   -> GetForegroundWindow, GetAsyncKeyState,
//                                 [vtbl+0x48], [vtbl+0x38]
//   PlatformGetMouseState      -> GetForegroundWindow, GetCursorPos,
//                                 ScreenToClient, GetAsyncKeyState,
//                                 GetSystemMetrics, [vtbl+0x48], [vtbl+0x38]
//
// Runtime DATA read by these bodies — all three MINTED since lane C2, so none
// is a new cutover obligation:
//   0x1801FC678  ?_singleton@Application@1@0PEAV11@EA        (+0x00 is the HWND)
//   0x1801FE768  ?_singleton@Configuration@Input@@0PEAV12@EA (+0x09 is legacy)
//   0x1801FE728  ?_secondsPerTick@Timer@System@@0NA
//
// **So these four bodies open NO by-address door and add NO unsatisfied
// export.** The two byte tables at 0x1801F9480 / 0x1801F94F0 are LIFTED as
// literals below rather than read through a door, and that is what makes the
// sentence above true rather than nearly true.
//
// ---------------------------------------------------------------------------
// ZERO CALLERS TODAY — MEASURED, NOT ASSUMED, AND IT IS THE POINT OF THE PORT
// ---------------------------------------------------------------------------
// [B.3.7 close: HISTORICAL. `Engine::MainLoop::Run` is ported
// (src/engine/engine/mainloop_run.cpp) and calls `Input::Device::Update` as a
// member every frame; `fn_InputDevice_Update` is deleted. The account below is
// kept as written.]
// `engine/funcs/engine_funcs.cpp:1554` resolves
// `?Update@Device@Input@@...` into `fn_InputDevice_Update` at init and
// **NOTHING IN THE TREE EVER CALLS IT** — grepped across code/, engine/,
// runtime/, standalone/ and src/render/dx11/: the only hits are the declaration, the
// definition and the resolve. The per-frame call really being made is a NEAR
// CALL inside the DLL's own `Engine::MainLoop::Run`, which is held back, and
// a near call inside the DLL cannot be intercepted by a registration.
//
// So this port's live `calls=` is 0 and will stay 0 until `MainLoop::Run` is
// ported. That is `reference_uncalled_port_is_not_verified.md` sitting
// directly on the deliverable, and it is why the probe below is not a
// nice-to-have: **it is the ONLY verification this function can have**, and
// it must be driven from something that actually executes rather than from
// the port itself. The first version of this lane self-scheduled it from the
// top of `Update` and measured exactly nothing.
//
// What the port IS worth meanwhile: at cutover `MainLoop::Run` becomes ours
// too, and on that day this is the only `Input::Device::Update` in the
// process. Porting it now, while a shipped body still exists to compare
// against, is precisely what WAVE9.md 2 means by spending the verification
// budget before the oracle dies.
//
// ---------------------------------------------------------------------------
// ONE CUTOVER GAP THIS PORT DOES NOT CLOSE, AND MUST NOT BE READ AS CLOSING
// ---------------------------------------------------------------------------
// The two COM pointers at +0x11E0 / +0x11E8 are created by
// Input::Device::PlatformConstruct (0x1800E1820) and ??0Device@Input@@,
// **neither of which is ported**. Today the DLL's ctor fills them. With the DLL
// absent they hold whatever the minted storage holds, and the non-legacy path
// in both pollers dereferences them UNGUARDED — faithfully, because retail
// does. Do NOT add a null check here (CLAUDE.md): the upstream fact to fix is
// that our Input::Device was never constructed, and a guard would skip the poll
// rather than restore it. Carried in WAVE9_LANE_ENGIO.md's HONEST LIMITS.
//
// ---------------------------------------------------------------------------
// CONSTANTS — LIFTED AS RAW BITS (PLAN rule 8), never retyped as decimals
// ---------------------------------------------------------------------------
//   0x180161738  float  0x46FFFE00           == 32767.0f, the value divisor.
//   0x1801616C0  double 0x3FD3333340000000   the repeat DELAY threshold. Look
//                at the low half: this is (double)0.3f, NOT 0.3. Writing 0.3
//                here compiles to 0x3FD3333333333333 and is a DIFFERENT
//                constant — a silently different repeat cadence, and exactly
//                the defect
//                reference_crt_coefficients_must_be_lifted_not_retyped.md is
//                about.
//   0x1801616B8  double 0x3FD0000000000000   == 0.25, the value holdTicks is
//                RESET to after a repeat fires. NOT zero — so the second and
//                later repeats need only 0.05s more. That is retail's cadence
//                and it reads like a bug if you assume a reset to 0.
// ============================================================================

// Phase D.4 SDL I1: no <windows.h> / <dinput.h>. The DirectInput reads, the re-acquire
// and the legacy path's user32 calls are Platform::Input primitives
// (src/platform/common/platform_input.h; Win32 bodies in
// src/platform/win/input_dinput_win.cpp until SDL-C; SDL3 bodies in
// src/platform/sdl/input_sdl.cpp since I2-I4), with the arguments retail passed.
#include <cstdint>
#include <cstddef>
#include <cstring>

#include "application/Application.h"   // ENDGAME B.3.9 ENG: the class static
#include "input/Input.h"
#include "system/Value.h"   // System::Time
#include "system/Timer.h"   // ENDGAME B.3.9 SS1: Timer::_secondsPerTick
#include "input/input_key_tables.h"
#include "platform_input.h"

namespace {

using State = Input::Device::State;

// ---- the three minted DATA slots -------------------------------------------
// RT_DATA_SLOT is the never-cache-a-null form (rt_port.h). Lane E3 measured 22
// sites in the tree using the FROZEN form, where the first caller fixes a null
// for the whole process; these three use the correct one deliberately.
inline const double* TimerSecondsPerTick() {
    // ENDGAME B.3.9 SS1 (B39_RULES D7): the class static now, no lazy cache.
    return &::System::Timer::_secondsPerTick;
}

// ---- the three constants, from their bit patterns ---------------------------
inline float ValueDivisor() {                       // 0x180161738
    const uint32_t bits = 0x46FFFE00u;              // 32767.0f
    float f; std::memcpy(&f, &bits, sizeof(f)); return f;
}
inline double RepeatDelaySeconds() {                // 0x1801616C0
    const uint64_t bits = 0x3FD3333340000000ull;    // (double)0.3f
    double d; std::memcpy(&d, &bits, sizeof(d)); return d;
}
inline double RepeatResetSeconds() {                // 0x1801616B8
    const uint64_t bits = 0x3FD0000000000000ull;    // 0.25
    double d; std::memcpy(&d, &bits, sizeof(d)); return d;
}

// The two lookup tables, lifted VERBATIM from the DLL's .data (0x1801F9480
// index -> virtual key, read ONLY by the legacy path; 0x1801F94F0 DirectInput scan
// code -> index, with retail's index-104 alias quirk), live in
// input/input_key_tables.h since Phase D.4 SDL I2: the SDL scan-code table's generator
// and its unit test read the same bytes.
using ::Input::KeyTables::kIndexToVirtualKey;
using ::Input::KeyTables::kScanCodeToIndex;
static_assert(sizeof(kIndexToVirtualKey) == Input::Device::kKeyboardInputCount,
              "one virtual key per keyboard input");

// The one value the "is this key down" idiom produces. Retail spells it
//     AND AX,0x8000 / NEG AX / SBB AX,AX / AND AX,0x7FFF
// which is a branchless (down ? 0x7FFF : 0). 0x7FFF and not 0x8000: it is the
// POSITIVE full scale of the int16 axis range, which is what makes normalized
// come out as exactly 1.0f from UpdateState's divide by 32767.
constexpr int16_t kFullScale = 0x7FFF;

// The two IDirectInputDevice8 methods retail calls, by BYTE OFFSET into the
// vtable, as the disassembly spells them: +0x38 Acquire (COM index 7), +0x48
// GetDeviceState (COM index 9). Phase D.4 SDL I1: Platform::Input::Acquire /
// ReadKeyboard / ReadMouse made those calls (input_dinput_win.cpp, deleted by SDL-5).

// DIERR_INPUTLOST. The re-acquire loop spins ONLY on this value; any other
// result — success included — leaves the loop.
constexpr uint32_t kDiErrInputLost = static_cast<uint32_t>(Platform::Input::kInputLost);

// ---------------------------------------------------------------------------
// Device::UpdateState — retail 0x1800E0FC0. 234 instructions, ZERO calls.
//
// `this` (RCX) is passed and NEVER READ. Retail loads it and does nothing with
// it. The parameter is kept so the signature matches all four call sites and
// the address registration; this comment is why it is unused rather than a
// transcription slip.
//
// Retail unrolls the loop 4x with a scalar tail. The unrolled body and the tail
// were read SEPARATELY and agree instruction for instruction on every offset,
// so this is written once as a plain loop — the unroll is the compiler's, not
// the algorithm's.
//
// Per element, from the scalar tail at 0x1800E11DD (the clearest copy):
//
//   1800e11dd  MOVSX EAX,word [RCX]          ; raw  = src[i].value  (SIGNED load)
//   1800e11e0  MOVZX EDX,word [RCX+R10]      ; prev = dst[i].value
//   1800e11ec  DIVSS XMM0,XMM2               ; f = (float)raw / 32767.0f
//   1800e11f0  MOVSS [RCX+R10-0x4],XMM0      ; dst[i].normalized = f
//   1800e11f7..1208   dst[i].pressed  = (prev == 0 && raw != 0)
//   1800e120d..1221   dst[i].released = (prev != 0 && raw == 0)
//   1800e1229  MOV   word [RCX+R10],AX       ; dst[i].value = raw
//   1800e121e  UCOMISS XMM0,XMM4 ; JP ; JNZ  ; the f == 0 test
//
// TWO THINGS THE OBVIOUS TRANSCRIPTION GETS WRONG, both read off the flags
// rather than off Ghidra's C (reference_ghidra_c_output_vs_disassembly.md):
//
//  * raw is loaded with MOVSX (signed) for the DIVIDE, but the two zero tests
//    are 16-bit CMP .,SI / TEST DX,DX — equality with zero, where signedness
//    cannot matter. Carrying the MOVSX into the tests, or the zero-extension
//    into the divide, yields a body that is wrong ONLY for negative axis
//    values. No keyboard and no mouse button can produce one, so such a defect
//    would pass every test that did not deliberately feed a negative axis. The
//    probe feeds them.
//
//  * The f == 0 branch is JP (unordered) and THEN JNZ, and BOTH jump to the
//    HOLD path. So a NaN takes the hold path, not the reset path. C++'s
//    if (f == 0.0f) is false for NaN and therefore matches exactly; writing it
//    the other way round (if (f != 0.0f) hold;) sends NaN to the reset path and
//    is wrong. It is written in the matching order below and this comment is
//    why it must stay that way.
//
// The hold path, 0x1800E1365:
//
//   dst[i].holdTicks += *time
//   if (dst[i].holdTicks < (int64)(0.3f  / secondsPerTick))  dst[i].repeat = 0
//   else { dst[i].repeat = 1; dst[i].holdTicks = (int64)(0.25 / secondsPerTick) }
//
// CVTTSD2SI truncates toward zero, which is what a C++ double->int64 cast does.
// Retail recomputes both divides INSIDE the loop; so does this. The only thing
// hoisting them could change is where _secondsPerTick is read, and leaving the
// read where retail has it costs nothing and argues nothing.
// ---------------------------------------------------------------------------
}  // namespace

namespace Input {

void Device::UpdateState(State* dst, State* src, int32_t count, const System::Time& time) {

    const float  divisor = ValueDivisor();
    const double spt     = *TimerSecondsPerTick();
    int32_t      edges   = 0;

    for (int32_t i = 0; i < count; ++i) {
        const int16_t  raw  = src[i].value;
        const uint16_t prev = static_cast<uint16_t>(dst[i].value);

        const float f = static_cast<float>(static_cast<int32_t>(raw)) / divisor;
        dst[i].normalized = f;

        dst[i].pressed  = (prev == 0) && (raw != 0);
        dst[i].released = (prev != 0) && (raw == 0);
        if (dst[i].pressed || dst[i].released) ++edges;

        dst[i].value = raw;

        if (f == 0.0f) {            // ordered AND equal; NaN falls to the else
            dst[i].holdTicks = 0;
            dst[i].repeat    = false;
        } else {
            dst[i].holdTicks += time._ticks;
            if (dst[i].holdTicks < static_cast<int64_t>(RepeatDelaySeconds() / spt)) {
                dst[i].repeat = false;
            } else {
                dst[i].repeat    = true;
                dst[i].holdTicks = static_cast<int64_t>(RepeatResetSeconds() / spt);
            }
        }
    }

    // DID-WORK = this call produced at least one press/release EDGE. calls= here
    // is two per frame by construction and each call sweeps 105 or 13 elements,
    // so the site would read as enormous coverage while almost every element
    // takes the all-zero path — the trap lane M's DID-WORK counter exists to
    // close, one level up from calls=0-reads-as-clean. A run whose work column
    // is 0 has seen no edge at all and has verified only the quiescent case,
    // whatever calls= says.

    // No replay. dst IS the live Device state and this is a read-modify-write of
    // it (pressed/released depend on the PREVIOUS value), so running theirs
    // afterwards compares the second application of an update that is only
    // accidentally idempotent. The byte comparison lives in the probe, on a
    // synthetic Device. See the banner.
    (void)edges;
}

// ---------------------------------------------------------------------------
// Device::PlatformGetKeyboardState — retail 0x1800E1C60. Returns true on EVERY
// exit, including both failure exits (MOV AL,1 at 0x1800E1DF3 is the single
// epilogue).
//
//   1800e1c72  MOV RAX,[0x1801fe768]        ; Input::Configuration::_singleton
//   1800e1c7f  CMP byte [RAX+0x9],0         ; ->legacy
//   1800e1c83  JZ -> the DirectInput path
//
// LEGACY (GetAsyncKeyState) PATH
//   1800e1c89  MOV RAX,[0x1801fc678] / MOV RBX,[RAX]  ; Application::_singleton->hwnd
//   1800e1c93  CALL [GetForegroundWindow]
//   1800e1c9c  JNZ -> zero all 0x69 values, return
//   loop i in 0..0x68: GetAsyncKeyState(kIndexToVirtualKey[i]) -> out[i].value
//
// DIRECTINPUT PATH
//   1800e1d15  MOV RCX,[RCX+0x11e0]                   ; the keyboard device
//   1800e1d29  CALL [vtbl+0x48] (dev, 0x100, keys)    ; GetDeviceState
//   1800e1d2e  JZ -> unpack                           ; 0 == DI_OK
//   otherwise: Acquire in a loop while it returns DIERR_INPUTLOST, then RETURN
//              WITHOUT TOUCHING out AT ALL.
//
// THAT LAST SENTENCE IS LOAD-BEARING AND A SUMMARY WOULD LOSE IT. On a failed
// read the buffer Update just zeroed is left zeroed, so UpdateState still runs
// and still sees all-zero raw values — which RELEASES every held key rather
// than freezing it. A port that "helpfully" skipped the dispatch on failure
// would leave keys stuck down across an alt-tab, and would look more correct
// while being less so. Retail's exit here is MOV AL,1, the same as success, and
// that is transcribed literally.
//
// The 256-byte scan-code buffer is NOT zeroed by retail before the call, and is
// not zeroed here. It is read only on the success path, where DirectInput has
// filled all 256 bytes.
// ---------------------------------------------------------------------------
bool Device::PlatformGetKeyboardState(State* out) {

    const Configuration* config = ::Input::Configuration::_singleton;

    if (config->_legacy) {
        auto* app  = ::Application::Application::_singleton;
        void* hwnd = app->_windowHandle;                      // Application +0x00

        if (Platform::Input::IsForeground(hwnd)) {
            for (int i = 0; i < kKeyboardInputCount; ++i) {
                const int16_t s = Platform::Input::AsyncKeyState(kIndexToVirtualKey[i]);
                out[i].value = (s & 0x8000) ? kFullScale : 0;
            }
        } else {
            for (int i = 0; i < kKeyboardInputCount; ++i) out[i].value = 0;
        }
        return true;
    }

    void* keyboard = _keyboardDevice;
    uint8_t keys[256];   // deliberately uninitialised — see the banner

    const uint32_t hr = static_cast<uint32_t>(Platform::Input::ReadKeyboard(keyboard, keys));   // +0x48

    if (hr != 0) {
        // The pointer is RE-READ on every spin (retail 0x1800E1D30 reloads
        // [RBX+0x11e0] inside the loop rather than reusing RCX). Kept, because a
        // re-acquire is exactly the moment a device pointer could be replaced,
        // and hoisting it out is a change with no upside.
        for (;;) {
            void* dev = _keyboardDevice;
            const uint32_t a = static_cast<uint32_t>(Platform::Input::Acquire(dev));   // +0x38
            if (a != kDiErrInputLost) break;
        }
        return true;
    }

    for (int i = 0; i < 256; ++i)
        out[kScanCodeToIndex[i]].value = (keys[i] & 0x80) ? kFullScale : 0;

    return true;
}

// ---------------------------------------------------------------------------
// Device::PlatformGetMouseState — retail 0x1800E1E10. Returns true on every
// exit.
//
// The 13 mouse inputs, by index, pinned by the store offsets (index*0x18 + 4):
//   0  +0x004  absolute client X       7  +0x0ac  button 3 (DirectInput only)
//   1  +0x01c  absolute client Y       8  +0x0c4  button 4 (DirectInput only)
//   2  +0x034  delta X                 9  +0x0dc  button 5 (DirectInput only)
//   3  +0x04c  delta Y  (NEGATED)     10  +0x0f4  button 6 (DirectInput only)
//   4  +0x064  button 0               11  +0x10c  button 7 (DirectInput only)
//   5  +0x07c  button 1               12  +0x124  wheel
//   6  +0x094  button 2
//
// THREE THINGS THE DISASSEMBLY SAYS THAT A PARAPHRASE LOSES:
//
//  * **The Y delta is negated and the X delta is not**, on BOTH paths, by two
//    DIFFERENT spellings. Legacy: MOVZX ECX,[self+0x11fc] ; SUB CX,DX, i.e.
//    lastY - y — the operands the other way round from X's x - lastX.
//    DirectInput: MOVZX EAX,word[lY] ; NEG AX. Two independent encodings of one
//    sign convention is the confirmation; taking it from only one of them and
//    assuming symmetry inverts vertical mouse-look on the other path.
//
//  * **Both deltas are computed in 16 BITS and WRAP.** SUB CX,. on the low
//    halves — not a 32-bit subtract truncated afterwards. The two agree for any
//    sane cursor position and differ once a coordinate crosses 32768, which is
//    reachable on a multi-monitor desktop.
//
//  * **GetSystemMetrics(SM_SWAPBUTTON) swaps buttons 0 and 1 on BOTH paths**,
//    after they are stored. Retail sets ECX = 0x17 several instructions ahead
//    of the call on both paths (0x1800E1EF4 and 0x1800E1FCD) — easy to lose
//    while reading, and losing it makes left-handed mice silently wrong.
//
// The legacy path also OWNS three device fields the DirectInput path never
// touches: wheelAccum (+0x11F4, filled by the window procedure, consumed AND
// CLEARED here), lastCursorX (+0x11F8) and lastCursorY (+0x11FC). The asymmetry
// is retail's, not a simplification.
// ---------------------------------------------------------------------------
bool Device::PlatformGetMouseState(State* out) {

    const Configuration* config = ::Input::Configuration::_singleton;

    auto swapIfLeftHanded = [&]() {
        if (Platform::Input::ButtonsSwapped()) {                  // GetSystemMetrics(SM_SWAPBUTTON)
            const int16_t b0 = out[4].value;
            out[4].value = out[5].value;
            out[5].value = b0;
        }
    };

    if (config->_legacy) {
        auto* app  = ::Application::Application::_singleton;
        void* hwnd = app->_windowHandle;

        if (!Platform::Input::IsForeground(hwnd)) {
            for (int i = 0; i < kMouseInputCount; ++i) out[i].value = 0;
            return true;
        }

        Platform::Point pt;
        Platform::Input::GetCursorPos(&pt);
        Platform::Input::ScreenToClient(hwnd, &pt);
        const int32_t x = pt.x, y = pt.y;

        int32_t* lastX = &_lastCursorX;
        int32_t* lastY = &_lastCursorY;

        // 16-bit wrapping subtracts, in retail's operand order (see the banner).
        out[2].value = static_cast<int16_t>(static_cast<uint16_t>(x) -
                                            static_cast<uint16_t>(*lastX));
        out[3].value = static_cast<int16_t>(static_cast<uint16_t>(*lastY) -
                                            static_cast<uint16_t>(y));
        *lastX = x;
        *lastY = y;
        out[0].value = static_cast<int16_t>(x);
        out[1].value = static_cast<int16_t>(y);

        out[4].value = (Platform::Input::AsyncMouseButtonState(0x01) & 0x8000) ? kFullScale : 0;  // VK_LBUTTON
        out[5].value = (Platform::Input::AsyncMouseButtonState(0x02) & 0x8000) ? kFullScale : 0;  // VK_RBUTTON
        out[6].value = (Platform::Input::AsyncMouseButtonState(0x04) & 0x8000) ? kFullScale : 0;  // VK_MBUTTON
        swapIfLeftHanded();

        int32_t* wheel = &_wheelAccum;
        const int32_t w = *wheel;
        out[12].value = (w == 0) ? static_cast<int16_t>(0)
                                 : (w > 0 ? kFullScale
                                          : static_cast<int16_t>(0x8001));
        *wheel = 0;

        return true;
    }

    void* mouse = _mouseDevice;

    // DIMOUSESTATE2: int32 lX, lY, lZ; BYTE rgbButtons[8] (Platform::Input::MouseState).
    // Retail zeroes all 20 bytes before the call (three stores at
    // 0x1800E1F8E/F92/F96) even though a successful GetDeviceState fills them; kept.
    Platform::Input::MouseState st = {};

    const uint32_t hr = static_cast<uint32_t>(Platform::Input::ReadMouse(mouse, &st));   // +0x48

    if (hr != 0) {
        for (;;) {
            void* dev = _mouseDevice;
            const uint32_t a = static_cast<uint32_t>(Platform::Input::Acquire(dev));   // +0x38
            if (a != kDiErrInputLost) break;
        }
        return true;
    }

    for (int b = 0; b < 8; ++b)
        out[4 + b].value = (st.buttons[b] & 0x80) ? kFullScale : 0;
    swapIfLeftHanded();

    out[12].value = (st.z == 0) ? static_cast<int16_t>(0)
                                : (st.z > 0 ? kFullScale
                                            : static_cast<int16_t>(0x8001));

    out[2].value = static_cast<int16_t>(st.x);
    out[3].value = static_cast<int16_t>(-static_cast<int16_t>(st.y));

    // The absolute position still comes from the CURSOR even on the DirectInput
    // path — a DI mouse reports deltas only. Retail re-reads
    // Application::_singleton here (0x1800E20D0) rather than reusing the value
    // it loaded earlier on this path; kept.
    Platform::Point pt;
    Platform::Input::GetCursorPos(&pt);
    Platform::Input::ScreenToClient(::Application::Application::_singleton->_windowHandle, &pt);
    out[0].value = static_cast<int16_t>(pt.x);
    out[1].value = static_cast<int16_t>(pt.y);

    return true;
}

}  // namespace Input

namespace {

// The zeroing loop Update runs over each scratch buffer. It is NOT a memset,
// and writing one here would be wrong in a way nothing could observe — which is
// precisely why it is transcribed rather than argued about:
//
//   1800e14c3  MOV qword [RAX-0x6],RSI     ; bytes +0x00..+0x07
//   1800e14c7  MOV qword [RAX+0x0a],RSI    ; bytes +0x10..+0x17
//   1800e14cb  LEA RAX,[RAX+0x18]
//
// **Bytes +0x08..+0x0F of every record are left untouched** — the repeat flag
// and the six unknown bytes after it. It is harmless because UpdateState reads
// ONLY src[i].value out of the scratch and never src[i].repeat, so a
// memset(buf, 0, count * 24) produces byte-identical downstream behaviour and
// is STILL not what retail does. Same argument engine.cpp's MainLoop ctor gives
// for its own partial zero at +0x18: a difference no oracle in this project can
// see is exactly the kind that has to be preserved by transcription
// (reference_oracle_must_not_compare_unwritten_bytes.md,
// reference_never_written_needs_by_whom.md).
void ZeroScratch(State* buf, int32_t count) {
    for (int32_t i = 0; i < count; ++i) {
        // B.4 (R-ENG): the two qwords by field -- +0x00..+0x07 is
        // normalized / value / pressed / released, +0x10 is holdTicks.
        buf[i].normalized = 0.0f;
        buf[i].value      = 0;
        buf[i].pressed    = false;
        buf[i].released   = false;
        buf[i].holdTicks  = 0;
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// ?Update@Device@Input@@QEAAXAEBVTime@System@@@Z — retail 0x1800E1490, ord 4498.
//
//   zero a 0x69-record scratch      1800e14c0
//   if (PlatformGetKeyboardState(this, kb))
//       UpdateState(this, this+0x6c0,  kb, 0x69, time)
//   zero a 0x0D-record scratch      1800e1510
//   if (PlatformGetMouseState(this, ms))
//       UpdateState(this, this+0x1098, ms, 0x0D, time)
//   <a DEAD 4 x 0x11 loop>          1800e1560
//   JMP PlatformUpdate              1800e1594
//
// THE DEAD LOOP AT 0x1800E1560 IS OMITTED — evidence, not assertion. It is
// three instructions, DEC ECX ; LEA RAX,[RAX+0x18] ; JNS, repeated 4 x 18 times
// with **no memory access of any kind**. Lane SYS2 found it and drew the right
// distinction: this is NOT lane FIND's dirAlias, where a local that looked dead
// turned out to be a side-effect carrier through a shared buffer. There is no
// helper here to mutate through and no shared buffer — and the two SIBLING
// zeroing loops immediately above it DO carry MOV qword ptr [RAX-0x6],RSI
// stores while this one carries none. It walks a pointer over the JOYSTICK
// block (4 devices x 18 inputs) and discards it, almost certainly a joystick
// poll compiled out. Omitting it changes no byte of the process, and the
// joystick block is left exactly as ResetState left it — which is what retail
// does too.
//
// Retail TAIL-JUMPS to PlatformUpdate; ours calls it and returns. Identical
// observable behaviour, and PlatformUpdate is already ours (lane IOA).
// ---------------------------------------------------------------------------
namespace Input {

void Device::Update(const System::Time& time) {

    // NOTE FOR ANYONE LOOKING FOR THE PROBE HOOK: it is NOT here, and the first
    // version of this lane put it here and measured nothing. See the
    // ZERO CALLERS section of the banner. The probe is scheduled from
    // Input::KeyMap::GetValue instead (input.cpp), which really does run.

    // Phase D.4 SDL I2: not retail. SDL's keyboard / mouse state is as fresh as its last
    // event pump, and the frame pumps AFTER this poll; DirectInput / GetAsyncKeyState
    // were live here. Pumping first keeps every key on the tick retail saw it
    // (src/platform/sdl/input_sdl.cpp). The events are still dispatched at the pump.
    Platform::Input::Poll();

    State keyboardScratch[kKeyboardInputCount];
    ZeroScratch(keyboardScratch, kKeyboardInputCount);
    const bool gotKeyboard = PlatformGetKeyboardState(keyboardScratch);
    if (gotKeyboard) {
        UpdateState(_keyboard, keyboardScratch, kKeyboardInputCount, time);
    }

    State mouseScratch[kMouseInputCount];
    ZeroScratch(mouseScratch, kMouseInputCount);
    const bool gotMouse = PlatformGetMouseState(mouseScratch);
    if (gotMouse) {
        UpdateState(_mouse, mouseScratch, kMouseInputCount, time);
    }

    // Both pollers return true unconditionally today, so both ifs are always
    // taken. This RECORDS that rather than assuming it: if the work column ever
    // reads below the call count, one of them started returning false and the
    // corresponding dispatch stopped running — which would be invisible
    // otherwise, because the resulting state is "last frame's, unchanged".

    PlatformUpdate();   // retail: JMP 0x1800E2130

    // EXT SEAM (NOT RETAIL, docs/android/P11_TOUCH_POINTER.md): ext.touch_pointer writes the
    // finger's pointer states and one-step action edges after the poll. Null: retail's path.
    if (_extAfterUpdate != nullptr) _extAfterUpdate(*this);
}

void (*Device::_extAfterUpdate)(Device& device) = nullptr;

}  // namespace Input
