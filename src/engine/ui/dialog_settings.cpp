// ============================================================================
// src/engine/ui/dialog_settings.cpp — the DialogInstance settings round-trip's
// write half.  Wave 9 lane ENC-2, bodies 5 and 6.
// ============================================================================
//
//   0x180114240  ?SaveSettings@DialogInstance@UI@@QEAAXXZ       (EXPORTED)
//   0x180115330  ?OnClosed@DialogInstance@UI@@UEAAXXZ           (EXPORTED)
//
// This closes the row three lanes declined (Z, UIC, UID). The chain that had to
// land first was: IO::Encoding's two factories and the two codec constructors
// (lane ENC), then BinaryWriter's two Serialize slots, its back-patching
// destructor and its deleting destructor (lane ENC-2, binary_writer.cpp). Only
// then does SaveSettings reach real bodies rather than minted traps.
//
// ----------------------------------------------------------------------------
// WHAT +0xa8 AND +0xb0 ACTUALLY ARE — a correction to DialogInstance.h
// ----------------------------------------------------------------------------
// src/engine/ui/DialogInstance.h names them `_userData` (void*) and
// `_userFlags` (int), from the constructor's trailing `PEAX` and `H`
// parameters. The offsets are right and the provenance is right, but the ROLE
// is not what those names suggest: SaveSettings uses the pair as a
// **settings buffer and its byte length** —
//
//     if (_userData == nullptr) return;          // nothing to save into
//     ms.data = _userData;  ms.size = _userFlags;
//
// and `?LoadSettings@DialogInstance@UI@@` (0x180114450) is the exact mirror:
// same null guard on +0xa8, same MemoryStream, same two fields, but
// `direction = 0` and it READS the 0xDEADBEEF magic back out. So the pair is
// one caller-supplied blob plus its size, round-tripped by Save/Load.
//
// The header's field NAMES are left alone here — renaming them would ripple
// through every DialogInstance consumer for a documentation gain — but the role
// is recorded in both places. `_userFlags` being a BYTE LENGTH is the fact a
// future reader most needs; treating it as a bitfield would corrupt the buffer.
//
// This also independently re-confirms lane ENC's direction mapping (0 = read ->
// CreateReader, non-zero = write -> CreateWriter) from a second, unrelated call
// site.
//
// ----------------------------------------------------------------------------
// SaveSettings — transcribed from 0x180114240..0x18011443a
// ----------------------------------------------------------------------------
// The shape, with everything retail inlines named:
//
//   1. bail out if there is no buffer
//   2. MemoryStream ctor (a real CALL to 0x1800ae710), then the stream is
//      pointed AT THE CALLER'S BUFFER by hand: _data/_size/_cursor/_direction
//      are stored directly, NOT through Open() — nothing is allocated and the
//      buffer is not owned, which is why the destructor does not free it
//   3. if the buffer is at least 4 bytes, 0xDEADBEEF is stored into its first
//      dword **directly, not through the stream**, and the cursor is stepped
//      past it. LoadSettings reads that dword back, so it is the format's
//      head magic
//   4. an IO::Serializer is constructed on the stack (retail INLINES
//      ??0Serializer@IO@@QEAA@AEAVStream@1@H@Z; this calls the ported one,
//      which is the same code and keeps the factory-slot choice in one place)
//   5. the dialog's own virtual +0x48 (OnSerializeSettings) gets the STREAM
//   6. the controller count is serialized as an int
//   7. per controller: its type id as a uint, then A SECOND Serializer is built
//      and immediately destroyed around the controller's own +0x50
//   8. both ReaderWriters are released through vtable slot 0 with flags = 1
//   9. the MemoryStream is destroyed (retail inlines ~MemoryStream; this calls
//      the ported one, which is the same code)
//
// FOUR DETAILS THE DECOMPILED C WOULD HAVE FLATTENED:
//
//  * **The loop bound is SNAPSHOT, the array base is NOT.** `_controllers._size`
//    is copied to a stack slot once and every iteration compares against that
//    copy, so a controller added during serialization is not visited. But
//    `_controllers._data` is RE-READ from the object twice per iteration, so a
//    reallocation mid-loop is tolerated. Those are different decisions about
//    the same container and both are reproduced.
//  * **The second Serializer is created and destroyed around a call that never
//    receives it.** The controller's +0x50 is handed the STREAM, not the
//    ReaderWriter. Its only observable effect is the 12-byte header its
//    constructor emits and the trailer plus back-patch its destructor emits —
//    i.e. each controller's settings are WRAPPED IN THEIR OWN FRAMED RECORD.
//    That is the whole reason the framing exists, and it is invisible unless
//    the constructor and destructor are read together.
//  * **The branch selecting CreateReader vs CreateWriter is dead here**, and
//    deliberately kept. `direction` is set to 1 four instructions earlier and
//    the register the branch tests is a copy of it, so the writer arm always
//    wins. Retail still emits the test; collapsing it would be a divergence for
//    no gain, and the same inlined ctor appears in LoadSettings where the other
//    arm is the live one.
//  * **`_userFlags` is read as an int and used as an unsigned size.** A negative
//    value would make `size >= 4` false and leave `_size` huge; retail does not
//    guard it and neither does this. Callers supply a real length.
//
// ----------------------------------------------------------------------------
// OnClosed — transcribed from 0x180115330..0x18011538b
// ----------------------------------------------------------------------------
//     SaveSettings(this);
//     for (i = 0; i < _controllers._size; ++i)
//         _controllers._data[i]->vtable[+0x30]();
//
// Same container idiom as SaveSettings but NOT the same decisions: here the
// bound is re-read from the object every iteration (`CMP EBX,[RSI+0x80]`), so
// this loop DOES see a controller added during dispatch. Two loops over one
// container, three instructions apart in the binary, disagreeing about that —
// which is exactly the kind of thing a "cleaned up" port silently unifies.
//
// ----------------------------------------------------------------------------
// TWO CONTROLLER VTABLE SLOTS, NOW EXPORT-CONFIRMED
// ----------------------------------------------------------------------------
// src/engine/ui/dialog.cpp recorded slot +0x30 as "a fifth DialogController
// vtable slot not yet in the enum ... a slot pinned by ONE unported caller is
// not yet evidence of a name." That caution is now dischargeable: the table at
// ??_7DialogController@UI@@6B@ (0x1801565f0) was read byte-wise and both slots
// resolve to exported names, so they are named by the EXPORT TABLE rather than
// by a caller:
//
//     +0x30  0x1800cd290  ?OnClosed@DialogController@UI@@UEAAXXZ
//     +0x50  0x1800cd250  ?OnSerializeSettings@DialogController@UI@@UEAAXAEAVStream@IO@@@Z
//
// (The same read also shows +0x10 is `?Type@DialogController@UI@@UEBAIXZ`; the
// enum's existing `kControllerVtblGetTypeId` describes it correctly, and the
// name is left as is.)
//
// ----------------------------------------------------------------------------
// NOT NEW CUTOVER SURFACE — the arithmetic that makes this a six-body lane
// ----------------------------------------------------------------------------
// SaveSettings dispatches `self->vtable[+0x48]` and the controllers' +0x10 and
// +0x50; OnClosed dispatches +0x30. **None of those is surface this port adds.**
// Those vtables are minted whether or not anything here is ported, so the
// bodies behind them are already whatever they already are. What porting
// SaveSettings would have added, and what binary_writer.cpp removed first, is
// the BinaryWriter tail — which is reached only because SaveSettings builds a
// writer. Distinguishing the two is what stopped this row from being either a
// two-body underestimate or an open-ended refusal.
//
// ----------------------------------------------------------------------------
// ORACLE: dialog_settings_probe.cpp. Read its HONEST LIMITS.
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "ui/DialogInstance.h"
#include "io/Stream.h"
#include "io/Serializer.h"

namespace RtUi {


namespace {


// (B3X W0: the slot offsets that lived here -- DialogInstance +0x48, the
// DialogController's +0x10/+0x28/+0x30/+0x50 and the ReaderWriter's
// +0x00/+0x10/+0x98/+0xa0 -- are gone; every one is a by-name member call.
// DialogController +0x28 is ?OnOpened@DialogController@UI@@UEAAXXZ
// (0x1800cd2a0), read off ??_7DialogController@UI@@6B@ against the export
// table.)

}  // namespace

// ---------------------------------------------------------------------------
// ?SaveSettings@DialogInstance@UI@@QEAAXXZ   0x180114240
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// ?OnClosed@DialogInstance@UI@@UEAAXXZ   0x180115330  (the member, below; its
// slot door is retired with the minted table, ENDGAME B.3.9 UI-D)
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// ?OnOpened@DialogInstance@UI@@UEAAXXZ   0x180114fb0     WAVE 10B LANE DLGV
// ---------------------------------------------------------------------------
// The READ half of the round-trip whose WRITE half is SaveSettings/OnClosed
// above, and the second of the two abort traps that kept
// `??_7DialogInstance@UI@@6B@` off the promoted list. It lives here rather than
// in dialog.cpp because it is the same round-trip, reusing every constant this
// file already pinned.
//
// It is NOT `LoadSettings(); for each controller OnOpened()` — the symmetry
// OnClosed has. `?LoadSettings@DialogInstance@UI@@` (0x180114450) exists at its
// own address and OnOpened does not call it: retail INLINES a second, subtly
// different copy of the load, and the controller loop runs FIRST rather than
// last. Both facts are read off the body, not inferred from the write half.
//
// FOUR THINGS THE DECOMPILED C WOULD HAVE FLATTENED
//
//  * **~MemoryStream and MemoryStream::MemoryStream are INLINED here**, unlike
//    in SaveSettings where the ctor is a real CALL. That is a compilation
//    difference, not a behavioural one, and it was verified rather than assumed:
//    `??0MemoryStream@IO@@QEAA@XZ` (0x1800ae710) is instruction-for-instruction
//    the prologue at 0x180115019..0x180115094 — the same two vtable stamps
//    (0x1801549a0 then 0x1801555b0), the same field zeroing, the same
//    lock / MemAlloc(size=8, align=4) / unlock, the same `0x1801554a8` stamped
//    into the 8 bytes it gets (the IO::Encoding vtable), and the same release of
//    any previous +0x10. `??1MemoryStream@IO@@UEAA@XZ` (0x1800ae890) is likewise
//    exactly the teardown at both of this body's two exits. So this port CALLS
//    the two exported bodies, as SaveSettings does, and the inlining is not
//    reproduced. The two free forms retail uses — an out-of-line
//    `CALL 0x180006a10` on the early-exit path and an inlined lock/MemFree on
//    the main one — were each disassembled and are the SAME function twice
//    (0x180006a10 and 0x1800c7ad0 are byte-identical COMDAT copies of
//    `if (p) { lock; MemFree(alloc, p); unlock }`).
//
//  * **The controller loop's bound is RE-READ every iteration** (`CMP EDI,
//    [RSI+0x80]`), like OnClosed's and unlike SaveSettings's snapshot. The inner
//    search loop over controllers is re-read too. Neither is unified here.
//
//  * **When `_userFlags < 4` retail reads UNINITIALIZED STACK for the magic.**
//    `MOV EAX,dword ptr [RBP + 0x40]` at 0x180115102 — and RBP+0x40 is the
//    CALLER'S first home slot, which nothing in this function has written at
//    that point. The comparison against 0xDEADBEEF then decides the exit. This
//    is the one place the port is deliberately NOT byte-faithful: reading an
//    uninitialized local is undefined behaviour in C++ and the compiler is
//    entitled to do anything with it, so the local is initialized to a value
//    that is not the magic and the bail-out is taken. The difference is
//    observable only if a caller happened to leave exactly 0xDEADBEEF in its own
//    shadow space, in which case retail would go on to build a reader over a
//    buffer shorter than its own 12-byte header. Stated here rather than
//    silently "cleaned up", because a reader who diffs this against the
//    disassembly will otherwise think a branch was dropped.
//
//  * **The per-controller Serializer's factory branch is LIVE here**, where in
//    SaveSettings it is dead. Retail emits `CMP [RBP-0x30],0 / JNZ` around
//    CreateReader (+0x18) vs CreateWriter (+0x20) and `direction` really is 0 on
//    this path, so the READER arm is the one that wins — the mirror image of the
//    write half, from the same inlined ctor. This port goes through the exported
//    `??0Serializer@IO@@QEAA@AEAVStream@1@H@Z` for both serializers, which keeps
//    the factory-slot choice in one place exactly as SaveSettings does.
//
// AND ONE THING WORTH NAMING: the `if (found < 0)` arm calls ReaderWriter slot
// +0x10, which src/engine/io/Serializer.h already pins as `kSkip`
// (`?Skip@Serializer@IO@@`). So a settings record whose controller type id is no
// longer present in this dialog is SKIPPED, not misapplied — which is what makes
// the per-controller framing SaveSettings emits load-bearing rather than
// decorative. The write half's header explains why the framing exists; this is
// the reader that consumes it.
//
// NOT NEW CUTOVER SURFACE, with one exception that is. Everything this body
// dispatches — the dialog's own +0x48, the controllers' +0x10 / +0x50, the
// ReaderWriter's +0x00 / +0x10 / +0x98 / +0xa0 — is already dispatched by the
// write half three functions above, so porting this adds no vtable surface. It
// DOES add two by-name callees the write half does not use: the MemoryStream
// constructor and destructor exports, which retail inlined here. Both are
// resolved BY NAME through RT_EXPORT_ONCE, so neither is an address door.
// ---------------------------------------------------------------------------
// ATTENDANCE COUNTER for OnOpened — the EFFECTIVENESS CONTROL for the
// ??_7DialogInstance@UI@@6B@ promotion, and NOT a verification of anything.
// Without it, "the candidate boot reached the same place as the control" cannot
// distinguish a safe promotion from one that was never exercised. Counts
// ENTRIES, on every build.
// The three sub-counters split the body's three exits, so an entry that always
// bailed on the magic cannot read as full coverage.
namespace {
struct OpenCounts {
    unsigned long long entries = 0, noBuffer = 0, badMagic = 0, records = 0;
    ~OpenCounts() {
        if (entries == 0) {
            printf("[DlgSettings] ATTENDANCE: OnOpened was not entered in this run.\n");
            return;
        }
        printf("[DlgSettings] ATTENDANCE (entries, not comparisons): "
               "OnOpened entries=%llu noBuffer=%llu badMagic=%llu records=%llu\n",
               entries, noBuffer, badMagic, records);
    }
};
OpenCounts g_openCounts;
}  // namespace

// (ENDGAME B.3.9 UI-D: the slot door `RtUi_DialogInstance_OnOpened` is retired
// with the minted table; its entry count moved into the member.)

}  // namespace RtUi

// ############################################################################
// ENDGAME B.3.6 block B — the three bodies are UI::DialogInstance members now.
// Unchanged except `self->x` became `x` and the hand-inlined vtable calls onto
// THIS block's classes became member calls:
//   own slot +0x48                  -> OnSerializeSettings(&ms)   (VIRTUAL)
//   controller +0x10/+0x28/+0x30/+0x50 -> c->Type() / OnOpened() / OnClosed() /
//                                      OnSerializeSettings(&ms)  (VIRTUAL)
//   RtUi_DialogInstance_SaveSettings -> SaveSettings()
// The ReaderWriter / MemoryStream / Serializer calls are IO's (other classes'
// doors) and stay as they were, reached through the file-scope helpers below
// so that no member keeps a static of its own (RULE B6-5).
// ############################################################################
namespace {

// ENDGAME B.3.9 IO (W2, foreign site): the real classes. The MemoryStream is a
// stack IO::MemoryStream (??0MemoryStream 0x1800ae710 / ??1 0x1800ae890, the
// latter at scope end, which is retail's position: last). Each scope is a
// stack IO::Serializer (??0Serializer 0x1800b9960, version 0; ??1Serializer
// 0x1800b99b0 = `if (rw) delete rw`, at the position the explicit delete had).

}  // namespace

namespace UI {

// ?SaveSettings@DialogInstance@UI@@QEAAXXZ   0x180114240 — see the door.
void DialogInstance::SaveSettings() {
    using namespace RtUi;
    if (_userData == nullptr) return;

    IO::MemoryStream ms;

    const int32_t size = _userFlags;            // a BYTE LENGTH — see the header
    void* const   buf  = _userData;
    ms._data          = buf;
    ms._size          = static_cast<uint32_t>(size);
    ms._cursor        = 0;
    ms._direction     = 1;                      // WRITING

    if (size >= 4) {
        // Stored DIRECTLY into the buffer, then the cursor is stepped past it.
        // LEFT: _userData is the raw settings byte buffer, not a field.
        *reinterpret_cast<uint32_t*>(buf) = kSettingsMagic;   // CAST-OK: byte_io
        ms._cursor += 4;
    }

    IO::Serializer ser(ms, 0);
    IO::ReaderWriter* rw = ser._readerWriter;

    OnSerializeSettings(ms);                   // own slot 9 — VIRTUAL

    int32_t count = _controllers._size;
    rw->SerializeInt(count, nullptr);           // +0xa0

    for (int32_t i = 0; i < count; ++i) {
        DialogController* c = _controllers._data[i];
        uint32_t id = c->Type();                // controller slot 2
        rw->SerializeUInt(id, nullptr);         // +0x98

        IO::Serializer ser2(ms, 0);

        _controllers._data[i]->OnSerializeSettings(ms);   // controller slot 10 (re-read)
        // ~Serializer: `if (rw2) delete rw2`.
    }
    // ~Serializer (`if (rw) delete rw`), then ~MemoryStream.
}

// ?OnClosed@DialogInstance@UI@@UEAAXXZ   0x180115330 — see the door.
void DialogInstance::OnClosed() {                              // [5]
    SaveSettings();
    for (int32_t i = 0; i < _controllers._size; ++i)
        _controllers._data[i]->OnClosed();                     // controller slot 6
}

// ?OnOpened@DialogInstance@UI@@UEAAXXZ   0x180114fb0 — see the door.
void DialogInstance::OnOpened() {                              // [4]
    using namespace RtUi;
    ++g_openCounts.entries;
    for (int32_t i = 0; i < _controllers._size; ++i)
        _controllers._data[i]->OnOpened();                     // controller slot 5

    if (_userData == nullptr) { ++g_openCounts.noBuffer; return; }

    IO::MemoryStream ms;

    const int32_t size = _userFlags;            // a BYTE LENGTH — see the header
    void* const   buf  = _userData;
    ms._data          = buf;
    ms._size          = static_cast<uint32_t>(size);
    ms._cursor        = 0;
    ms._direction     = 0;                      // READING

    uint32_t magic = 0;
    if (size >= 4) {
        // LEFT: _userData is the raw settings byte buffer, not a field.
        magic = *reinterpret_cast<const uint32_t*>(buf);   // CAST-OK: byte_io
        ms._cursor += 4;
    }
    if (magic != kSettingsMagic) {
        ++g_openCounts.badMagic;
        return;                                 // ~MemoryStream
    }

    IO::Serializer ser(ms, 0);
    IO::ReaderWriter* rw = ser._readerWriter;

    OnSerializeSettings(ms);                   // own slot 9 — VIRTUAL

    int32_t count = 0;
    rw->SerializeInt(count, nullptr);           // +0xa0

    for (int32_t k = 0; k < count; ++k) {
        ++g_openCounts.records;
        uint32_t id = 0;
        rw->SerializeUInt(id, nullptr);         // +0x98

        int64_t found = -1;
        for (int32_t j = 0; j < _controllers._size; ++j) {
            if (_controllers._data[j]->Type() == id) {     // controller slot 2
                found = j;
                break;
            }
        }

        IO::Serializer ser2(ms, 0);
        IO::ReaderWriter* rw2 = ser2._readerWriter;

        if (found < 0) {
            rw2->Skip();                        // +0x10
        } else {
            _controllers._data[found]->OnSerializeSettings(ms);   // controller slot 10
        }
        // ~Serializer: `if (rw2) delete rw2`.
    }
    // ~Serializer (`if (rw) delete rw`), then ~MemoryStream.
}

}  // namespace UI
