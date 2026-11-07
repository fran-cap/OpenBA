// ===========================================================================
// src/diag/mouse_inject.cpp — Wave 10D lane MOUSE
// ===========================================================================
// THE ROUTE: an unattended run has no mouse, so three ported UI bodies
// (EndDragDrop, EndHover, BeginToolTip) had never executed. This file gives
// the harness a mouse.
//
// WHERE IT INJECTS, AND WHY THERE
// -------------------------------
// `UI::UserInterface::Update` does not take the mouse as an argument. It reads
// it out of the `Input::Device` singleton, at the offsets
// src/engine/ui/userinterface_frame.cpp:442-461 decode:
//
//     in + kMouseOffset + 0*0x18 + 4   int16   MouseX raw
//     in + kMouseOffset + 1*0x18 + 4   int16   MouseY raw
//     in + kMouseOffset + 4*0x18 + 4   int16   left button hold count
//                              + 6     bool    left pressed  (down edge)
//                              + 7     bool    left released (up edge)
//     ... inputs 5 and 6 are the other two buttons, input 12 the wheel.
//
// So we write THE INPUT DEVICE, not the UI. That is the same shape as
// autostart.cpp injecting through the dialog vtable rather than simulating OS
// input, one layer lower: we do not fake a hover, we move the pointer and let
// the shipped `DialogInstance::Update` / `Element::Update` decide whether that
// is a hover. Three consequences, all of them the point:
//
//   * It drives the DLL's `UserInterface::Update` and our port identically —
//     both read the same bytes. Nothing here depends on what is registered
//     (cf. WAVE10C_LANE_UIF6.md §3, where a driver that resolved a callback
//     through the port registry silently stopped matching the DLL's).
//   * Everything downstream is retail: the clip test, the strict rect test,
//     `_hovering`, the tooltip flag bits, the event queue.
//   * It cannot manufacture a pass. If the game disagrees that the cursor is
//     on the element, the hover bit stays clear and the counters say so.
//
// The write has to land after the frame's input poll and before the UI reads
// it, so the seam is the two lines around the `fn_UI_Update` call in
// `GameUpdateLoop::Update` (src/game/Game/gameupdateloop.cpp) and around the
// `fn_DialogLoopT_Update` tail call in `MenuDLT::Stub_Update`
// (src/game/Game/menudialogloopt_stubs.cpp). PostUpdate reads the element flags
// back once retail has had its say.
//
// THE SCALE ROUND TRIP — the one piece of arithmetic that can be wrong
// -------------------------------------------------------------------
// The UI coordinate is not the raw value: `userinterface_frame.cpp:446` is
//
//     mx = (int32)((float)rawX * ui->_uiScale)
//
// so placing the cursor at UI x requires solving for rawX. We do not invert it
// analytically and hope; we search the three candidates around x/scale and
// keep the one whose forward evaluation — the exact expression above — equals
// the request. When none does (a scale that skips the requested coordinate) we
// keep the nearest and REPORT THE MISS on its own screen, rather than
// silently aiming somewhere else.
//
// SWITCHES (all off / inert unless set; nothing is printed when unset)
// -------------------------------------------------------------------
//   BANISHED_MOUSE_INJECT=<step>[,<step>...]   arms the injector
//        probe        scan and dump the element table; inject nothing
//        hover        hold the cursor on the target
//        off          park the cursor far outside every element
//        press        left button down edge, at the current position
//        release      left button up edge, at the current position
//        drag         UI::BeginDragDrop on the target (a real drag starts)
//        drop         hold on the target and raise the release edge, which is
//                     what makes Element::Update take its drop arm
//        w            one hold period of nothing
//   BANISHED_MOUSE_TARGET=auto|tip|any|#<name>|@<x>,<y>    default `tip`
//   BANISHED_MOUSE_HOLD=<frames>     frames per step, default 20
//   BANISHED_MOUSE_START=<frame>     first frame of step 0, default 150
//   BANISHED_MOUSE_CONTROL=<name>    negative control, see below. default none
//   BANISHED_MOUSE_TRACE=1           per-frame trace
//
// THE SCREENS, AND WHY THEY ARE SCORED SEPARATELY
// ----------------------------------------------
// This is a route, not a differential, so its "controls caught" question is
// "can each screen be made to read false by a wrong input?"
// (reference_probe_needs_an_effectiveness_control.md). Five screens:
//
//   S1 injected     frames on which we wrote the input bytes
//   S2 coordMatch   frames on which the forward evaluation of the bytes we
//                   wrote equals the UI coordinate we asked for
//   S3 hoverEcho    frames on which the TARGET element came back from retail
//                   with hover bit 17 set  <- the effectiveness counter
//   S4 dlgHover     frames on which the target's dialog had _hovering (+0x98)
//   S5 tipShown     frames on which the target had bit 20, "tooltip is up"
//
// The screens NEST (S5 needs S3 and S4, S3 needs S2, S2 needs S1), and that is
// stated rather than hidden, because a control caught on the union of a nested
// set proves nothing about the inner members
// (reference_union_control_hides_a_blind_screen.md). Each control below is
// therefore chosen to be the FIRST wrong input that a given screen — and not
// the screen above it — must catch, and the report prints all five for every
// control so a blind screen is visible as a screen that never moves.
//
//   BANISHED_MOUSE_CONTROL=nowrite    run the whole state machine, skip the
//                                     store.               must kill S1 (and all)
//   BANISHED_MOUSE_CONTROL=badscale   write x as the raw value, i.e. skip the
//                                     scale solve.  S1 lives, must kill S2
//   BANISHED_MOUSE_CONTROL=offtarget  solve exactly, for a point 8px outside
//                                     the target rect.
//                                     S1+S2 live, must kill S3
//   BANISHED_MOUSE_CONTROL=notip      aim at a hit-testable element that is
//                                     NOT tooltip-capable.
//                                     S1+S2+S3 live, must kill S5
//
// `offtarget` is the load-bearing one: it is the only control that separates
// "we can place the cursor precisely" from "retail agreed the cursor is on the
// element", and without it S2 passing would be read as S3 passing.
// ===========================================================================

#include "mouse_inject.h"
#include "input/Device.h"   // ENDGAME B.3.9 SS2: Input::Device::_singleton

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cmath>

#include "options.h"
#include "clamped_atoi.h"     // review F-087: checked parse of BANISHED_* text
// FULL PATHS ON PURPOSE. `engine/types/` also has a `ui/DialogInstance.h`,
// and `-Iengine/types` precedes `-Iruntime/types` on the standalone command
// line, so the short spelling silently picks up the WRONG header -- which
// compiles far enough to be confusing (it has no RtUI namespace) and would
// have been a wrong field layout if it had.
#include "system/String.h"
#include "ui/UserInterface.h"
#include "ui/Element.h"
#include "ui/DialogInstance.h"
#include "input/Input.h"

namespace {

// ---------------------------------------------------------------------------
// Element::_flags bits this file reads. Named in src/engine/ui/Element.h
// from the exported one-line accessor that reads each one; the two without an
// accessor are named from the body that writes them.
// ---------------------------------------------------------------------------
constexpr uint32_t kFlagHidden       = UI::kElementFlagHidden;       // bit 0
constexpr uint32_t kFlagNoHitTest    = UI::kElementFlagNoHitTest;    // bit 1
constexpr uint32_t kFlagRaisesEvents = UI::kElementFlagRaisesEvents; // bit 16
constexpr uint32_t kFlagHovering     = UI::kElementFlagHovered;      // bit 17,
    // written by Element::Update itself (element_vtable.cpp:786/791, BTS/BTR
    // 0x11). There is no accessor for it; the writer is the evidence.
constexpr uint32_t kFlagToolTipOk    = UI::kElementFlagToolTipValid; // bit 19
constexpr uint32_t kFlagTipShown     = UI::kElementFlagToolTipShown; // bit 20,
    // set beside the BeginToolTip call and cleared beside EndHover
    // (element_vtable.cpp:833/862, BTS/BTR 0x14).
constexpr uint32_t kFlagDisabled     = UI::kElementFlagDisabled;     // bit 23

// Mouse input indices inside the Input::Device state array.
constexpr int kMouseX      = static_cast<int>(Input::Device::MouseInput::X);
constexpr int kMouseY      = static_cast<int>(Input::Device::MouseInput::Y);
constexpr int kMouseButton = static_cast<int>(Input::Device::MouseInput::Left);   // the one
    // Element::Update's drop arm reads as `b04` (in[0x10ff] == input 4 released)

// The linked-list node UserInterface::_dialogs is built from. Spelled raw, as
// userinterface_frame.cpp:281 spells it, rather than instantiating the
// template over an InternalPtr we would only strip again.
struct ListNode { void* value; ListNode* prev; ListNode* next; };

// ---------------------------------------------------------------------------
// Configuration, read once.
// ---------------------------------------------------------------------------
// `DropNow` exists because a drag CANNOT be held across frames here, and that
// was measured. With `drag` on one step and `drop` on the next, the dwell trace
// shows `dragDlg=0` already on the frame the drag started: the toolbar's
// `DialogInstance::Update` captures the mouse (`uiMouseCaptured=1`) and writes
// UpdateState+0x07 back as 0, so `UserInterface::Update`'s own end-drag block
// (`!leftDown && _dragDropDialog`) tears the drag down INLINE -- and that block
// does the teardown itself and NEVER calls EndDragDrop
// (src/engine/ui/userinterface_frame.cpp:527-539).
//
// `DropNow` therefore builds the drop frame's state in the order retail has it
// AT THAT FRAME: `_dragDropDialog` set (through the real exported
// BeginDragDrop, not by writing +0x60), the cursor over a hovering element, and
// the release edge raised -- all before the single `UserInterface::Update` that
// must observe them. Nothing is faked; the wall-clock gap between press and
// release is what is compressed, and no part of retail reads that gap.
enum class Step { Probe, Hover, Off, Press, Release, Drag, Drop, DropNow, Wait };
enum class Control { None, NoWrite, BadScale, OffTarget, NoTip };

struct Config {
    bool        armed        = false;
    Step        steps[16]    = {};
    int         stepCount    = 0;
    int         hold         = 20;
    int         startFrame   = 150;
    Control     control      = Control::None;
    bool        trace        = false;
    // target selector
    bool        byName       = false;
    char        name[64]     = {};
    bool        byCoord      = false;
    int32_t     coordX       = 0;
    int32_t     coordY       = 0;
    bool        wantTipCapable = true;   // `tip`/`auto` vs `any`
    // WAVE 10E lane VERIFY -- BANISHED_MOUSE_TIPSTR. Empty == unset == silent.
    char        tipStr[128]  = {};
    bool        haveTipStr   = false;
};

Config g_cfg;
bool   g_cfgRead = false;

bool ParseStep(const char* tok, Step* out) {
    if (!std::strcmp(tok, "probe"))   { *out = Step::Probe;   return true; }
    if (!std::strcmp(tok, "hover"))   { *out = Step::Hover;   return true; }
    if (!std::strcmp(tok, "off"))     { *out = Step::Off;     return true; }
    if (!std::strcmp(tok, "press"))   { *out = Step::Press;   return true; }
    if (!std::strcmp(tok, "release")) { *out = Step::Release; return true; }
    if (!std::strcmp(tok, "drag"))    { *out = Step::Drag;    return true; }
    if (!std::strcmp(tok, "drop"))    { *out = Step::Drop;    return true; }
    if (!std::strcmp(tok, "dropnow")) { *out = Step::DropNow; return true; }
    if (!std::strcmp(tok, "w"))       { *out = Step::Wait;    return true; }
    return false;
}

void ReadConfig() {
    if (g_cfgRead) return;
    g_cfgRead = true;

    const char* script = Opt().mouseInject;
    if (!script || !*script || !std::strcmp(script, "0")) return;

    // steps
    char buf[512];
    std::snprintf(buf, sizeof(buf), "%s", script);
    char* save = nullptr;
    for (char* tok = strtok_r(buf, ",", &save); tok; tok = strtok_r(nullptr, ",", &save)) {
        Step s;
        if (!ParseStep(tok, &s)) {
            fprintf(stderr, "[MouseInject] unknown step '%s' -- injector NOT armed\n", tok);
            return;
        }
        if (g_cfg.stepCount < 16) g_cfg.steps[g_cfg.stepCount++] = s;
    }
    if (g_cfg.stepCount == 0) return;

    if (const char* h = Opt().mouseHold)
        if (int v = HarnessAtoi(h)) g_cfg.hold = v;
    if (const char* f = Opt().mouseStart)
        if (int v = HarnessAtoi(f)) g_cfg.startFrame = v;
    g_cfg.trace = Opt().mouseTrace != nullptr;

    if (const char* ts = Opt().mouseTipStr) {
        if (*ts && std::strcmp(ts, "0") != 0) {
            std::snprintf(g_cfg.tipStr, sizeof(g_cfg.tipStr), "%s", ts);
            g_cfg.haveTipStr = true;
        }
    }

    if (const char* c = Opt().mouseControl) {
        if (!std::strcmp(c, "nowrite"))        g_cfg.control = Control::NoWrite;
        else if (!std::strcmp(c, "badscale"))  g_cfg.control = Control::BadScale;
        else if (!std::strcmp(c, "offtarget")) g_cfg.control = Control::OffTarget;
        else if (!std::strcmp(c, "notip"))     g_cfg.control = Control::NoTip;
        else if (std::strcmp(c, "none") != 0) {
            fprintf(stderr, "[MouseInject] unknown control '%s' -- injector NOT armed\n", c);
            return;
        }
    }

    const char* t = Opt().mouseTarget;
    if (t && *t) {
        if (t[0] == '#') {
            g_cfg.byName = true;
            std::snprintf(g_cfg.name, sizeof(g_cfg.name), "%s", t + 1);
        } else if (t[0] == '@') {
            int xy[2] = {0, 0};
            if (ScanClampedInts(t + 1, xy, 2, -kHarnessIntLimit, kHarnessIntLimit) == 2) {
                g_cfg.byCoord = true;
                g_cfg.coordX = xy[0];
                g_cfg.coordY = xy[1];
            }
        } else if (!std::strcmp(t, "any")) {
            g_cfg.wantTipCapable = false;
        }
        // "auto" / "tip" keep the default
    }
    // The `notip` control deliberately aims somewhere the tooltip guards refuse.
    if (g_cfg.control == Control::NoTip) g_cfg.wantTipCapable = false;

    g_cfg.armed = true;
}

// ---------------------------------------------------------------------------
// Screens. Counted per frame; printed at exit and at every step boundary.
// ---------------------------------------------------------------------------
struct Screens {
    uint64_t injected   = 0;   // S1
    uint64_t coordMatch = 0;   // S2
    uint64_t hoverEcho  = 0;   // S3  <- the effectiveness counter
    uint64_t dlgHover   = 0;   // S4
    uint64_t tipShown   = 0;   // S5
    uint64_t framesTicked = 0; // context: frames the injector ran at all
    uint64_t targetFound  = 0; // context: frames a target was resolvable
    uint64_t dragStarted  = 0; // context: BeginDragDrop calls we made
};
Screens g_s;

const char* ControlName() {
    switch (g_cfg.control) {
        case Control::NoWrite:   return "nowrite";
        case Control::BadScale:  return "badscale";
        case Control::OffTarget: return "offtarget";
        case Control::NoTip:     return "notip";
        default:                 return "none";
    }
}

struct Report {
    ~Report() {
        if (!g_cfg.armed) return;   // silent when unset — the whole point
        printf("[MouseInject] control=%s ticked=%llu targetFound=%llu dragStarted=%llu\n",
               ControlName(),
               (unsigned long long)g_s.framesTicked,
               (unsigned long long)g_s.targetFound,
               (unsigned long long)g_s.dragStarted);
        printf("[MouseInject] SCREENS  S1 injected=%llu  S2 coordMatch=%llu  "
               "S3 hoverEcho=%llu  S4 dlgHover=%llu  S5 tipShown=%llu\n",
               (unsigned long long)g_s.injected,
               (unsigned long long)g_s.coordMatch,
               (unsigned long long)g_s.hoverEcho,
               (unsigned long long)g_s.dlgHover,
               (unsigned long long)g_s.tipShown);
        // S3 is the effectiveness control: a run whose bytes were written but
        // which retail never agreed was a hover has injected NOTHING that the
        // UI can act on, and must not be read as coverage.
        if (g_cfg.control == Control::None && g_s.injected > 0 && g_s.hoverEcho == 0)
            printf("[MouseInject] INEFFECTIVE -- bytes were written but no frame "
                   "came back hovering; do not read the run as coverage\n");
        fflush(stdout);
    }
};
Report g_report;

// ---------------------------------------------------------------------------
// Input::Device singleton.
// ---------------------------------------------------------------------------
// RENAMED (ENDGAME B.3.3 step 6B): was `InputDevice()`. This file now reaches
// `engine/types/common.h` (through DialogInstance.h -> object/Internal.h ->
// system/Value.h, which stopped duplicating the global `struct Time`), and
// common.h:57 forward-declares `struct InputDevice` at global scope — which
// made the unqualified call at MouseInject::PreUpdate ambiguous. The helper is
// file-local; only its two call sites move.
::Input::Device* InputDeviceSingleton() {
    return ::Input::Device::_singleton;
}

RtInput::State* MouseState(::Input::Device* dev, int index) {
    return &dev->_mouse[index];   // +0x1098 (RtInput::kMouseOffset), stride sizeof(State)
}

// ---------------------------------------------------------------------------
// The scale round trip. Solve raw for `(int32)((float)raw * scale) == want`.
// ---------------------------------------------------------------------------
int32_t Forward(int16_t raw, float scale) {
    return static_cast<int32_t>(static_cast<float>(raw) * scale);
}

// Returns the raw value; *exact is whether the forward evaluation hit `want`.
int16_t SolveRaw(int32_t want, float scale, bool* exact) {
    if (!(scale > 0.0f)) { *exact = (want == 0); return 0; }
    // Clamped just outside int16: any raw value out there is rejected below anyway,
    // and the clamp keeps the cast and `base + d` defined for a huge
    // BANISHED_MOUSE_TARGET coordinate or a tiny UI scale (review F-282).
    const double guess = std::clamp(static_cast<double>(want) / static_cast<double>(scale),
                                    -32770.0, 32770.0);
    int32_t base = static_cast<int32_t>(std::floor(guess));
    int16_t best = 0;
    int32_t bestErr = INT32_MAX;
    for (int32_t d = -2; d <= 2; ++d) {
        int32_t cand = base + d;
        if (cand < -32768 || cand > 32767) continue;
        const int32_t got = Forward(static_cast<int16_t>(cand), scale);
        const int32_t err = (got > want) ? (got - want) : (want - got);
        if (err < bestErr) { bestErr = err; best = static_cast<int16_t>(cand); }
        if (err == 0) break;
    }
    *exact = (bestErr == 0);
    return best;
}

// ---------------------------------------------------------------------------
// Target discovery. Walk the live dialog list exactly as UserInterface::Update
// walks it, then the element tree exactly as Element::Update recurses it.
// ---------------------------------------------------------------------------
struct Target {
    UI::Element*        elem = nullptr;
    UI::DialogInstance* dlg = nullptr;
    int32_t             cx = 0, cy = 0;    // the point we will aim at
    bool                tipCapable = false;
};

// The four guards BeginToolTip itself applies (userinterface_hover.cpp:386-390)
// plus the two flag bits Element::Update tests before it ever calls
// BeginToolTip. Asking the same questions the subject asks is what makes a
// found target actually reach the body rather than merely look plausible.
bool TipCapable(const UI::Element* e) {
    if ((e->_flags & kFlagRaisesEvents) == 0) return false;   // Element::Update
    if ((e->_flags & kFlagToolTipOk) == 0)    return false;   // Element::Update
    if (e->_toolTipDialog == nullptr)         return false;   // BeginToolTip +0x40
    if (e->_toolTip.table == nullptr)         return false;   // BeginToolTip +0x48
    if (e->_toolTip.index < 0)                return false;   // BeginToolTip +0x50
    if ((e->_flags & kFlagDisabled) != 0)     return false;   // BeginToolTip +0x34
    return true;
}

bool HasRect(const UI::Element* e) {
    return e->_layoutRight > e->_layoutLeft + 2 &&
           e->_layoutBottom > e->_layoutTop + 2;
}

int64_t Area(const UI::Element* e) {
    return (int64_t)(e->_layoutRight - e->_layoutLeft) *
           (int64_t)(e->_layoutBottom - e->_layoutTop);
}

struct Scan {
    UI::Element* best = nullptr;
    int64_t      bestArea = 0;
    int          visited = 0;
    int          tipCount = 0;
    bool         dump = false;
};

void Walk(UI::Element* e, UI::DialogInstance* dlg, Scan& sc, int depth) {
    if (!e || depth > 24) return;
    ++sc.visited;
    if ((e->_flags & kFlagHidden) == 0) {
        const bool tip = TipCapable(e);
        if (tip) ++sc.tipCount;
        if (sc.dump && (tip || HasRect(e))) {
            printf("[MouseInject/probe] %*sname=%-24s id=%-6d flags=%08x rect=(%d,%d)-(%d,%d) "
                   "tipDlg=%p tipTbl=%p tipIdx=%d %s\n",
                   depth * 2, "",
                   e->_name ? e->_name : "(null)",
                   (int)e->_elementId, (unsigned)e->_flags,
                   (int)e->_layoutLeft, (int)e->_layoutTop,
                   (int)e->_layoutRight, (int)e->_layoutBottom,
                   e->_toolTipDialog, e->_toolTip.table, (int)e->_toolTip.index,
                   tip ? "<TIP-CAPABLE>" : "");
            // WAVE 10E lane VERIFY -- the one-line fix lane MOUSE named in its
            // §9. `tipTbl`/`tipIdx` above IS the +0x48 pair, and +0x48 is the
            // data the EMPTY arm uses; the arm that calls GetToolTipString
            // TWICE is selected by +0x58 `_toolTipString` and by nothing else
            // (?GetToolTipString@Element@UI@@ 0x1800356a0 reads [rcx+0x58] and
            // touches no other field). Printing +0x48 alone therefore steers a
            // reader toward the arm we already have, so +0x58 is printed here
            // NEXT TO it, with its length, so "no element has an override" is a
            // MEASUREMENT rather than an inference from the static call graph.
            printf("[MouseInject/probe] %*s    tipStr(+0x58)=%p [%s]\n",
                   depth * 2, "", e->_toolTipString,
                   e->_toolTipString
                       ? e->_toolTipString
                       : "null -> EMPTY arm");
        }

        bool candidate = false;
        if (g_cfg.byName) {
            candidate = e->_name &&
                        !std::strcmp(e->_name, g_cfg.name) &&
                        HasRect(e);
        } else if (g_cfg.wantTipCapable) {
            candidate = tip && HasRect(e);
        } else {
            // `any` / the notip control: hit-testable, has a rect, and
            // deliberately NOT tooltip-capable so S5 has a real reason to stay
            // at zero rather than an accidental one.
            candidate = !tip && HasRect(e) && (e->_flags & kFlagNoHitTest) == 0;
        }
        if (candidate) {
            const int64_t a = Area(e);
            // Smallest matching element wins: a leaf is unambiguous, whereas a
            // full-screen container would be "hovered" by any coordinate at all
            // and would make S3 pass for the wrong reason.
            if (!sc.best || a < sc.bestArea) { sc.best = e; sc.bestArea = a; }
        }

        for (int32_t i = 0; i < e->_children._size; ++i)
            Walk(e->_children._data[i], dlg, sc, depth + 1);
    }
}

bool FindTarget(UI::UserInterface* ui, Target* out, bool dump) {
    using ListNode = Collection::LinkedList<UI::DialogInstancePtr>::Node;
    ListNode* head = ui->_dialogs._head;   // +0x00

    if (dump) {
        // THE FRAME THE HOVER TEST ACTUALLY RUNS IN. Element::Update compares
        // the raw layout ints against the raw mouse, and gates the whole thing
        // on _clipRegion, so a degenerate clip makes every hover impossible no
        // matter where the cursor is put -- print it rather than infer it from
        // element rects.
        ::Input::Device* dev = InputDeviceSingleton();
        printf("[MouseInject/probe] ui=%p clip=(%d,%d)-(%d,%d) uiScale=%.4f "
               "rawMouse=(%d,%d) dev=%p" "\n",
               (void*)ui, (int)ui->_clipRegion.left, (int)ui->_clipRegion.top,
               (int)ui->_clipRegion.right, (int)ui->_clipRegion.bottom,
               (double)ui->_uiScale,
               dev ? (int)MouseState(dev, kMouseX)->value : -12345,
               dev ? (int)MouseState(dev, kMouseY)->value : -12345,
               (void*)dev);
        int nd = 0;
        for (ListNode* n = head; n; n = n->next) ++nd;
        printf("[MouseInject/probe] dialogs in list = %d" "\n", nd);
    }

    for (ListNode* n = head; n; n = n->next) {
        UI::DialogInstance* dlg = n->element;   // DialogInstancePtr (UserInterface.h)
        if (!dlg) continue;
        UI::Element* root = dlg->_root.Get();   // +0x50
        if (!root) continue;

        Scan sc;
        sc.dump = dump;
        if (dump)
            printf("[MouseInject/probe] dialog=%p root=%p hovering=%d\n",
                   (void*)dlg, (void*)root, (int)dlg->_hovering);
        Walk(root, dlg, sc, 0);
        if (dump)
            printf("[MouseInject/probe]   visited=%d tipCapable=%d best=%p\n",
                   sc.visited, sc.tipCount, (void*)sc.best);

        if (sc.best && !dump) {   // in probe mode keep walking: dump EVERY dialog
            out->elem = sc.best;
            out->dlg  = dlg;
            out->tipCapable = TipCapable(sc.best);
            out->cx = (sc.best->_layoutLeft + sc.best->_layoutRight) / 2;
            out->cy = (sc.best->_layoutTop  + sc.best->_layoutBottom) / 2;
            if (g_cfg.control == Control::OffTarget) {
                // Exactly solvable, definitely outside. S2 must still pass.
                out->cx = sc.best->_layoutLeft - 8;
                out->cy = sc.best->_layoutTop  - 8;
                if (out->cx < 0) out->cx = sc.best->_layoutRight + 8;
                if (out->cy < 0) out->cy = sc.best->_layoutBottom + 8;
            }
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// WAVE 10E lane VERIFY — BANISHED_MOUSE_TIPSTR: the ONLY way to reach
// BeginToolTip's non-empty arm, and why a harness has to be the one to do it.
// ---------------------------------------------------------------------------
// `?BeginToolTip@UserInterface@UI@@` branches on whether the element's +0x58
// `_toolTipString` is a non-empty System::String. The EMPTY arm asks
// `GetToolTipString` ONCE (to test emptiness) and then falls back to the
// `.rsc` `tipTbl`/`tipIdx` pair at +0x48; the NON-EMPTY arm asks a SECOND time
// and feeds that string to `SetText`. Lane MOUSE's cell covered only the first.
//
// +0x58 IS NEVER WRITTEN BY THE SHIPPED GAME. The only writer is
// `?SetToolTipString@Element@UI@@` (0x1800356e0), and a two-part screen says
// nothing calls it:
//
//   * inside Runtime-x64.dll — Ghidra reports THREE xrefs to 0x1800356e0 and
//     all three are DATA (two export-directory entries plus the entry-point
//     reference). Zero CALL xrefs.
//   * from Application-x64.exe — the import directory (read with pefile) has
//     exactly ONE `*ToolTip*` entry, `?SetToolTip@Element@UI@@` at
//     0x140151ce8, which is the +0x48 setter. `SetToolTipString` is absent.
//
// THAT SCREEN HAS A POSITIVE CONTROL, so "DATA-only" is not being read for
// more than it can carry. `?SetToolTip@` (0x180035740) is ALSO DATA-only in
// the DLL and yet is demonstrably live, because it is in the exe's import
// table — so the DLL half alone is blind to exe callers and the two halves are
// both needed. And `?GetToolTipString@` (0x1800356a0), on the same class in
// the same file, reports two UNCONDITIONAL_CALL xrefs (0x180120948 and
// 0x18012099f, both inside BeginToolTip) — so the xref instrument does report
// calls when calls exist. SetToolTipString has neither kind.
//
// So the non-empty arm is DEAD CODE in the shipped game, and there is no
// element to "find". This switch does not fake the arm: it calls the real
// exported `SetToolTipString` with a real refcounted `System::String`, on the
// live element the injector is about to hover, and then lets retail's own
// BeginToolTip decide which arm to take from the field it actually reads. It
// is a harness-created route and is labelled as one in the report; what it
// verifies is the ported bodies on that arm, not that gameplay reaches it.
//
// Off by default: unset (or `0`, or empty) writes nothing and prints nothing.

// Applied ONCE per (element) so a 20-frame hold does not re-release and
// re-acquire the same string 20 times — SetToolTipString's release arm is real
// and a repeat would be exercising the harness, not the subject.
void ApplyToolTipString(UI::Element* elem) {
    if (!g_cfg.haveTipStr || !elem) return;

    static UI::Element* applied = nullptr;
    if (applied == elem) return;

    // ENDGAME B.3.1 step 2: the String ctor and dtor are System::String's own;
    // B.3.6: UI::Element::SetToolTipString is a member call on the real class.

    void* const before = elem->_toolTipString;
    {
        const System::String str(g_cfg.tipStr);
        if (!str._data) {
            fprintf(stderr, "[MouseInject/tipstr] String ctor produced null -- "
                            "+0x58 left alone\n");
            return;
        }
        elem->SetToolTipString(str);   // B.3.6: `elem` IS a UI::Element*
        // Our local reference is released here; the element now holds the only
        // one — `str`'s destructor, at the end of this scope.
    }
    applied = elem;

    printf("[MouseInject/tipstr] elem=%p name=%s  +0x58 %p -> %p [%s]  "
           "(BeginToolTip must now take the NON-EMPTY arm)\n",
           (void*)elem,
           elem->_name ? elem->_name : "(null)",
           before, elem->_toolTipString,
           elem->_toolTipString ? elem->_toolTipString
                                : "?");
    fflush(stdout);
}

// ---------------------------------------------------------------------------
// BeginDragDrop — the only way to make Element::Update's drop arm reachable.
// ---------------------------------------------------------------------------
// `_dragDropDialog` (+0x60) is what `UserInterface::Update` turns into
// UpdateState::dragActive, and `Element::Update` needs it non-null AND the
// release edge before it raises event 0xc and calls EndDragDrop. Nothing in
// the tree had ever called BeginDragDrop; it is an ordinary export and this is
// the public API a dragging element uses, so the drag we start is a REAL one —
// we are not writing +0x60 by hand.
void StartDrag(UI::UserInterface* ui, const Target& t) {
    // Both InternalPtr<> parameters go by hidden reference, the same way
    // element_vtable.cpp:747 and userinterface_hover.cpp pass them.
    //
    // THE ELEMENT ARGUMENT IS READ-ONLY, AND THAT WAS MEASURED, NOT ASSUMED.
    // BeginDragDrop (0x18011f5f0) is fifteen instructions and it is entirely
    // legible:
    //     +0x60 = *dlgPtr        +0x70 = id        +0x78 = data
    //     +0x80 = x - elem->_layoutLeft (+0x68)     <- a GRAB OFFSET
    //     +0x84 = y - elem->_layoutTop  (+0x6c)
    //     then  EDX=2, tail-JMP 0x1801223d0 == ?Play@UserInterface@UI@@ (the
    //     drag-start sound)
    // It NEVER writes `_dragDropElement` (+0x68 of the UI). So no ownership is
    // taken, EndDragDrop's "destroy the drag element" arm is not armed by this
    // call, and handing it the live hovered element destroys nothing.
    //
    // The first attempt passed nullptr here, to be safe about that destroy arm.
    // It crashed inside retail at 0x18011f60e -- `SUB R9D,[RAX+0x68]` with RAX
    // null -- because the deref is unguarded, which is retail's contract and
    // not a bug for the harness to paper over (CLAUDE.md). The crash is what
    // established the read-only reading above.
    Object::InternalPtr<UI::DialogInstance> dlgLocal(t.dlg);   // a bare pointer copy: InternalPtr's ctor / dtor are trivial
    Object::InternalPtr<UI::Element>        elemLocal(t.elem);

    int32_t dragX = t.cx, dragY = t.cy;
    ui->BeginDragDrop(&dlgLocal,
                      &elemLocal,
                      dragX, dragY, 0x4D4F5553u /*'MOUS'*/, nullptr);
    ++g_s.dragStarted;
    printf("[MouseInject] BeginDragDrop(dlg=%p, x=%d, y=%d) -> _dragDropDialog=%p\n",
           (void*)t.dlg, (int)t.cx, (int)t.cy, ui->_dragDropDialog);

    // ---- Wave 10E lane DRAG: the FIELD DUMP that makes this an oracle -------
    // Added by lane DRAG, after lane MOUSE published. It reads back the five
    // fields BeginDragDrop writes, so a script run says what the function
    // actually did rather than asking the function for its own verdict.
    //
    // The two POINTERS are printed as a match against the argument rather than
    // raw, because the heap address differs between processes and an absolute
    // pointer could not be compared across the two cells. `id` and the two grab
    // offsets ARE absolute and MUST be identical across the switch: same seed,
    // same target, same injected coordinates.
    //
    // The grab offsets are the reason this dump exists. +0x80 and +0x84 are two
    // int32s written from two nearly identical instructions, and TRANSPOSING
    // them is a defect that every other screen this lane has is blind to --
    // the downstream rows only ever read +0x60.
    {
        const uint32_t fId  = ui->_dragDropId;      // +0x70
        const void*    fDat = ui->_dragDropData;    // +0x78
        const int32_t  fGx  = ui->_dragDropGrabX;   // +0x80
        const int32_t  fGy  = ui->_dragDropGrabY;   // +0x84
        // In the RELEASE build src/app/log_strip.h #defines printf away, so
        // these four become unused. Silenced rather than left to warn.
        (void)fId; (void)fDat; (void)fGx; (void)fGy;
        printf("[MouseInject/DRAGFIELDS] dlgMatchesArg=%d id=0x%08X dataIsNull=%d "
               "grabX=%d grabY=%d  (argX=%d argY=%d layoutLeft=%d layoutTop=%d)\n",
               (int)(static_cast<const void*>(ui->_dragDropDialog) == t.dlg), (unsigned)fId, (int)(fDat == nullptr),
               (int)fGx, (int)fGy, (int)dragX, (int)dragY,
               (int)(dragX - fGx), (int)(dragY - fGy));
    }
    fflush(stdout);
}

// ---------------------------------------------------------------------------
// Per-frame state
// ---------------------------------------------------------------------------
int  g_frame      = 0;
int  g_stepIndex  = -1;
int  g_stepFrame  = 0;
bool g_done       = false;
UI::Element* g_lastTarget = nullptr;   // handed from PreUpdate to PostUpdate
UI::DialogInstance* g_lastDlg = nullptr;

// True when `elem` sits in the element tree of `dlg` and `dlg` is still in the
// open-dialog list. Compares pointers on the way down and reads only live nodes.
bool ElementInTree(UI::Element* node, const UI::Element* elem) {
    if (!node) return false;
    if (node == elem) return true;
    for (int32_t i = 0; i < node->_children._size; ++i) {
        if (ElementInTree(node->_children._data[i], elem)) return true;
    }
    return false;
}

bool TargetStillLive(UI::UserInterface* ui, UI::DialogInstance* dlg, const UI::Element* elem) {
    using ListNode = Collection::LinkedList<UI::DialogInstancePtr>::Node;
    for (ListNode* n = ui->_dialogs._head; n; n = n->next) {
        if (n->element != dlg) continue;
        return ElementInTree(dlg->_root.Get(), elem);
    }
    return false;
}

void PrintStepBoundary(const char* what) {
    printf("[MouseInject] frame=%d step=%d/%d %s | S1=%llu S2=%llu S3=%llu S4=%llu S5=%llu\n",
           g_frame, g_stepIndex, g_cfg.stepCount, what,
           (unsigned long long)g_s.injected, (unsigned long long)g_s.coordMatch,
           (unsigned long long)g_s.hoverEcho, (unsigned long long)g_s.dlgHover,
           (unsigned long long)g_s.tipShown);
    fflush(stdout);
}

const char* StepName(Step s) {
    switch (s) {
        case Step::Probe:   return "probe";
        case Step::Hover:   return "hover";
        case Step::Off:     return "off";
        case Step::Press:   return "press";
        case Step::Release: return "release";
        case Step::Drag:    return "drag";
        case Step::Drop:    return "drop";
        case Step::DropNow: return "dropnow";
        default:            return "wait";
    }
}

}  // namespace

// ===========================================================================
namespace MouseInject {

bool Enabled() {
    ReadConfig();
    return g_cfg.armed;
}

void PreUpdate(UI::UserInterface* ui) {
    ReadConfig();
    if (!g_cfg.armed || g_done || !ui) return;

    ++g_frame;
    g_lastTarget = nullptr;
    g_lastDlg    = nullptr;

    if (g_frame < g_cfg.startFrame) return;

    // ---- advance the script ----------------------------------------------
    if (g_stepIndex < 0) {
        g_stepIndex = 0; g_stepFrame = 0;
        PrintStepBoundary(StepName(g_cfg.steps[0]));
    } else if (++g_stepFrame >= g_cfg.hold) {
        g_stepFrame = 0;
        if (++g_stepIndex >= g_cfg.stepCount) {
            g_done = true;
            PrintStepBoundary("DONE");
            return;
        }
        PrintStepBoundary(StepName(g_cfg.steps[g_stepIndex]));
    }

    ++g_s.framesTicked;
    const Step step = g_cfg.steps[g_stepIndex];

    if (step == Step::Probe) {
        if (g_stepFrame == 0) { Target t; FindTarget(ui, &t, /*dump=*/true); }
        return;                                  // probe injects NOTHING
    }
    if (step == Step::Wait) return;

    // ---- resolve where to point ------------------------------------------
    int32_t wantX, wantY;
    Target t;
    if (g_cfg.byCoord) {
        wantX = g_cfg.coordX; wantY = g_cfg.coordY;
    } else {
        if (!FindTarget(ui, &t, /*dump=*/false)) return;
        ++g_s.targetFound;
        g_lastTarget = t.elem;
        g_lastDlg    = t.dlg;
        wantX = t.cx; wantY = t.cy;
        // WAVE 10E lane VERIFY: publish the +0x58 override BEFORE the hover
        // that will open the tooltip. No-op unless BANISHED_MOUSE_TIPSTR is
        // set, and applied once per element.
        ApplyToolTipString(t.elem);
    }

    if (step == Step::Off) {
        // Far outside every rect but still a legal int16 -- this is what ends a
        // hover, and it is how EndHover becomes reachable.
        wantX = 30000; wantY = 30000;
    }

    if ((step == Step::Drag || step == Step::DropNow) &&
        g_stepFrame == 0 && g_lastDlg)
        StartDrag(ui, t);

    // ---- solve, then write ------------------------------------------------
    ::Input::Device* dev = InputDeviceSingleton();
    if (!dev) return;


    const float scale = ui->_uiScale;
    bool exactX = false, exactY = false;
    int16_t rawX, rawY;
    if (g_cfg.control == Control::BadScale) {
        // The defect: treat the UI coordinate as the raw value. Identical when
        // the scale happens to be 1.0, which is why the report prints the
        // scale -- a control that cannot fire must not be scored as passing.
        rawX = static_cast<int16_t>(wantX);
        rawY = static_cast<int16_t>(wantY);
        exactX = (Forward(rawX, scale) == wantX);
        exactY = (Forward(rawY, scale) == wantY);
    } else {
        rawX = SolveRaw(wantX, scale, &exactX);
        rawY = SolveRaw(wantY, scale, &exactY);
    }

    if (g_cfg.control != Control::NoWrite) {
        MouseState(dev, kMouseX)->value = rawX;
        MouseState(dev, kMouseY)->value = rawY;

        RtInput::State* btn = MouseState(dev, kMouseButton);
        if (step == Step::Press) {
            btn->value = 1; btn->pressed = true; btn->released = false;
        } else if (step == Step::Release || step == Step::Drop ||
                   (step == Step::DropNow && g_stepFrame == 0)) {
            // The drop arm is `dragActive && b04`, and b04 IS input 4's
            // `released` byte. `value` stays 0 so UserInterface::Update's own
            // end-drag block does not fire before Element::Update sees it.
            btn->value = 0; btn->pressed = false; btn->released = true;
        } else if (step == Step::Drag) {
            btn->value = 1; btn->pressed = (g_stepFrame == 0); btn->released = false;
        }
        ++g_s.injected;
        if (exactX && exactY) ++g_s.coordMatch;
    }

    if (g_cfg.trace)
        printf("[MouseInject] f=%d %s want=(%d,%d) raw=(%d,%d) scale=%.4f exact=%d%d "
               "elem=%p\n", g_frame, StepName(step), (int)wantX, (int)wantY,
               (int)rawX, (int)rawY, (double)scale, (int)exactX, (int)exactY,
               (void*)g_lastTarget);
}

void PostUpdate(UI::UserInterface* ui) {
    ReadConfig();
    if (!g_cfg.armed || !ui) return;
    if (!g_lastTarget) return;
    // UserInterface::Update ran between PreUpdate and here, and may have closed
    // and freed the dialog (CleanRemovedDialogs) or dropped the element from its
    // tree. Read through the two pointers only when both are still reachable
    // (review F-270 / F-296).
    if (!TargetStillLive(ui, g_lastDlg, g_lastTarget)) {
        g_lastTarget = nullptr;
        g_lastDlg    = nullptr;
        return;
    }

    // Retail has now run. These three reads are the whole difference between
    // "we wrote some bytes" and "the game hovered the thing we aimed at".
    const uint32_t f = g_lastTarget->_flags;
    if ((f & kFlagHovering) != 0) ++g_s.hoverEcho;
    if ((f & kFlagTipShown) != 0) ++g_s.tipShown;
    if (g_lastDlg && g_lastDlg->_hovering != 0) ++g_s.dlgHover;

    // ---- the dwell diagnostic ------------------------------------------
    // `DialogInstance::Update` (0x180113980) only sets `_hovering` after the
    // pointer has sat on the SAME PIXEL for 0.25 s (the double at 0x1801616b8,
    // divided by _secondsPerTick). It keeps the last position in the pair at
    // +0x90/+0x94 -- which src/engine/ui/DialogInstance.h once called
    // `_timerHandle`, a single field (now `_hoverMouseX/Y`); it is NOT: 0x180113a1b and
    // 0x180113a26 compare them against UpdateState mouseX and mouseY
    // separately, and 0x180113a60/0x180113a69 store the new pair.
    //
    // The accumulator at +0xa0 is the one EndHover resets. Printing all three
    // says WHICH guard is refusing rather than leaving S4=0 to be guessed at.
    if (g_cfg.trace && g_lastDlg) {
        const UI::DialogInstance* d = g_lastDlg;
        printf("[MouseInject/dwell] f=%d dlg=%p hovering=%d lastPos=(%d,%d) "
               "accum=%lld uiMouseCaptured=%d elemFlags=%08x dragDlg=%p dragElem=%p\n",
               g_frame, (void*)g_lastDlg, (int)g_lastDlg->_hovering,
               (int)d->_hoverMouseX,
               (int)d->_hoverMouseY,
               (long long)d->_hoverTime,
               (int)ui->_mouseCaptured,
               (unsigned)f,
               // +0x60 is what UserInterface::Update turns into
               // UpdateState::dragActive, and +0x68 is the element EndDragDrop
               // would destroy. A drop that never fires is almost always +0x60
               // already cleared by Update's OWN end-drag block, which does the
               // teardown INLINE and never calls EndDragDrop.
               ui->_dragDropDialog,
               (void*)ui->_dragDropElement);
    }
}

}  // namespace MouseInject
