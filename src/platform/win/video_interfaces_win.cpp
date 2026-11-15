// ============================================================================
// src/platform/win/video_interfaces_win.cpp — Wave 9 lane EVM
//   The plugin-loading half of the EnumerateVideoModes chain.
// ============================================================================
//
//   | Export                                                     | Retail      | Ord  |
//   |------------------------------------------------------------|-------------|------|
//   | ?GetDLLForInterface@Device@Video@@QEBA?AVString@System@@H@Z | 0x1800f7b20 | 2542 |
//   | ?InitializeInterfaces@Device@Video@@QEAAXXZ                 | 0x1800f79f0 | 3073 |
//
// Both names were read out of the shipped export directory with pefile BEFORE
// a line of body was written, and both were grepped (name AND address) across
// code/, engine/, runtime/, standalone/ and src/render/dx11/ first — every hit was a
// comment, so neither was already ported under another name.
//
// WHY THESE TWO, AND WHY IN THIS LANE
// -----------------------------------
// `?EnumerateVideoModes@Device@Video@@QEAA_NXZ` (attempt 22's wall) opens with
// `CALL 0x1800f79f0` — its FIRST instruction after the prologue IS
// `InitializeInterfaces`. And `InitializeInterfaces`'s only non-KERNEL32 callee
// besides the String primitives is `GetDLLForInterface`. The three are one
// chain; porting the wall without them leaves two nulls one call deep.
//
// A SEPARATE _win.cpp because SetErrorMode, the one Win32 call left in the chain,
// needs <windows.h>; src/platform/posix/video_interfaces_posix.cpp is the twin
// without it. Retail reached SetErrorMode / LoadLibraryW / GetProcAddress /
// FreeLibrary through Runtime-x64.dll's IAT.
// The slots retail used were mapped with pefile rather than guessed:
//   0x18014b1b0 SetErrorMode   0x18014b1b8 LoadLibraryW
//   0x18014b1c0 GetProcAddress 0x18014b118 FreeLibrary
//
// THE POINT OF THE WHOLE THING: THE DX11 SLOT
// -------------------------------------------
// `src/app/video_interface.cpp`'s VideoInterfaceFillDx11Slot puts our renderer in
// slot 1, and nothing fills the DX11 interface slot unless the ported
// `Video::Device::InitializeInterfaces` calls it. This body calls it, first thing
// (as does its twin, src/platform/posix/video_interfaces_posix.cpp), so the loop
// skips slot 1.
// ============================================================================

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>

#include "video/Device.h"
#include "system/String.h"
#include "system/string_internal.h"
#include "system/widestring_internal.h"
#include "video_interface.h"   // src/app/video_interface.h; this header is windows.h-free

using System::String;
using Video::Device;
using InterfaceSlot = Video::Device::InterfaceSlot;

// src/app/video_interface.cpp's two fills (VideoInterfaceFillDx11Slot,
// VideoInterfaceFillDx9Placeholder) are declared at FILE scope by
// video_interface.h above (one used to be a block-scope `extern` inside the
// body; inside a `Video::Device` member that would name `Video::…` and fail at
// link). B5 DEV-A: both take the typed `Video::Device*` (`this`), and the
// duplicate declaration that stood here is gone.

namespace {

// The three plugin base names, read out of the shipped .rdata rather than
// retyped: 0x1801588c0 / 0x1801588d0 / 0x1801588e0, then the two suffixes at
// 0x1801588e8 and 0x1801588f0. So slot 1 is "VideoDX11-x64.dll".
const char kNameDX9[]  = "VideoDX9";
const char kNameDX11[] = "VideoDX11";
const char kNameGL[]   = "VideoGL";
const char kSuffixArch[] = "-x64";
const char kSuffixExt[]  = ".dll";

// The two GetProcAddress names, 0x1801588f8 and 0x180158910.
const char kProcCreateDeviceData[] = "CreateDeviceData";
const char kProcGetDeviceName[]    = "GetDeviceName";

// Device::dllSlots (+0xB0). B.4 (R-VIDEO): the unused by-offset `SlotAt` is gone.
constexpr int kSlotCount  = 3;

// ENDGAME B.3.1 step 2: retail's inlined `String::operator=(const String&)` at
// 0x1800f7b7a (and the two identical copies at 0x1800f7bc9 / 0x1800f7c16) IS
// `System::String::operator=`, self-assignment guard first and all — compare
// src/engine/system/string.cpp. The hand-written copy that used to live here
// is gone; the call sites below use the operator.

}  // namespace

// ============================================================================
// ?GetDLLForInterface@Device@Video@@QEBA?AVString@System@@H@Z   0x1800f7b20
//                                                              ord 2542
// ============================================================================
//   String GetDLLForInterface(int index) const;
//
// RETURNS A `System::String` BY VALUE, so the Microsoft x64 ABI gives it a
// HIDDEN RETURN SLOT in RDX and pushes `index` out to R8. Getting that wrong is
// a wild write that still returns a plausible answer
// (reference_hidden_retslot_wild_write.md), so it is spelled out here:
//
//     RCX = this   (SPILLED AT ENTRY AND NEVER READ AGAIN — see below)
//     RDX = String* ret        (the hidden slot)
//     R8D = int index
//
// **`this` IS NEVER READ.** `MOV qword ptr [RSP+0x10],RDX` at 0x1800f7b20
// spills the RET SLOT, not RCX; RCX is dead from the first instruction. The
// function is a pure function of `index`, which is what makes it the one body
// in this lane with a perfect oracle. The parameter is kept for ABI fidelity
// and named so.
//
// THE DEFAULT CASE IS NOT AN ERROR PATH. `TEST R8D,R8D / DEC / DEC / JNZ` at
// 0x1800f7b4e..0x1800f7b5f sends any index outside 0..2 straight to the two
// appends at 0x1800f7c6c, SKIPPING both the assignment and the temporary's
// release. So `GetDLLForInterface(7)` returns **"-x64.dll"** — a name, not an
// empty string and not a failure. Reproduced exactly.
// ============================================================================
// ENDGAME B.3.5 block D2, RULE B5-12. The MEMBER returns the `System::String` by
// value, which is what `?AVString@System@@` says and what GCC's Microsoft-ABI
// lowering implements with the same hidden slot in RDX.
System::String Video::Device::GetDLLForInterface(int index) const {
    // `this` IS NEVER READ — `MOV [RSP+0x10],RDX` at 0x1800f7b20 spills the RET
    // SLOT, not RCX, and RCX is dead from the first instruction. The member keeps
    // `this` for ABI fidelity and reads nothing through it.
    String ret;                                   // 0x1800f7b44 nulls the slot

    const char* base = nullptr;
    if (index == 0)      base = kNameDX9;         // 0x1800f7c01
    else if (index == 1) base = kNameDX11;        // 0x1800f7bb4
    else if (index == 2) base = kNameGL;          // 0x1800f7b65

    if (base != nullptr) {
        String tmp;                               // the [RBP+0x38] local
        tmp.Set(base);                            // 0x1800c95d0
        ret = tmp;                                // the inlined operator=
    }                                             // 0x1800f7c4c — tmp's release

    // 0x1800f7c6c and 0x1800f7ca2 — two `operator+=(const char*)` calls whose
    // RETURNED TEMPORARIES ARE DISCARDED. Retail materialises each into the
    // same [RBP+0x38] slot and releases it immediately; discarding the by-value
    // result of `operator+=` is exactly that, temporary and release included.
    ret += kSuffixArch;
    ret += kSuffixExt;

    return ret;                                   // 0x1800f7cd8 MOV RAX,RDI
}

// ============================================================================
// ?InitializeInterfaces@Device@Video@@QEAAXXZ                   0x1800f79f0
//                                                               ord 3073
// ============================================================================
//   for (int i = 0; i < 3; ++i) {
//       InterfaceSlot* s = this + 0xb0 + i*0x18;
//       if (s->module != nullptr) continue;                  // 0x1800f7a14
//       SetErrorMode(SEM_FAILCRITICALERRORS);                // 0x1800f7a23
//       String  name = GetDLLForInterface(i);                // 0x1800f7a34
//       WideString wide(name);                               // 0x1800f7a44
//       s->module = LoadLibraryW(wide.data);                 // 0x1800f7a4d
//       <release wide>
//       if (s->module) {
//           s->createDeviceData = GetProcAddress(s->module, "CreateDeviceData");
//           s->getDeviceName    = GetProcAddress(s->module, "GetDeviceName");
//           if (!s->createDeviceData || !s->getDeviceName) {
//               FreeLibrary(s->module);
//               s->module = s->getDeviceName = s->createDeviceData = nullptr;
//           }
//       }
//       <release name>
//   }
//
// FOUR THINGS THAT ARE RETAIL'S AND LOOK LIKE MISTAKES
// ----------------------------------------------------
//  1. **`SetErrorMode` is called INSIDE the loop and never restored.** Retail
//     sets SEM_FAILCRITICALERRORS (1) before each LoadLibraryW and leaves it
//     set for the life of the process — it does not save the previous mode and
//     does not put it back. That suppresses the OS "cannot find DLL" box for a
//     missing VideoGL-x64.dll. Transcribed, including the leak of the setting.
//  2. **The two GetProcAddress results are stored in the OPPOSITE order to the
//     field order**: `CreateDeviceData` into +0x10 first (0x1800f7a98), then
//     `GetDeviceName` into +0x08 (0x1800f7aac). Irrelevant to behaviour,
//     recorded because the struct in Device.h lists them the other way and the
//     next reader will otherwise think one of the two is wrong.
//  3. **The failure test reads the STORED field, not the register**:
//     `CMP qword [RBX+0x10],0` then `TEST RAX,RAX` where RAX still holds the
//     second call's result. Same answer, transcribed as written.
//  4. **A slot with a module but a null proc is fully reset**, including the
//     module word, after a FreeLibrary. So a half-usable plugin leaves no
//     trace, which is what lets the pre-fill's sentinel module be safe: a
//     sentinel that failed lookup would be FreeLibrary'd, which is why
//     video_interface.cpp takes a real extra reference on our own module
//     rather than fabricating a handle.
//
// ============================================================================
void Video::Device::InitializeInterfaces() {
    // *** THE LINE THE TREE HAS BEEN ASKING FOR. ***
    // src/app/video_interface.cpp (VideoInterfaceFillDx11Slot's banner) names
    // this call as the thing that must exist once this function is ported.
    // It fills slot 1 with our own CreateDeviceData/GetDeviceName so the loop
    // below SKIPS it.
    VideoInterfaceFillDx11Slot(this);

    int skipped = 0, loaded = 0, rejected = 0;

    // ---- THE ONE PLACE THIS BODY DIFFERS FROM RETAIL (ENDGAME.md A.1) ------
    // Retail LoadLibraryW's each plugin here and GetProcAddress's its two
    // exports. We must not: EVERY shipped video plugin (VideoDX9-x64.dll and
    // VideoDX11-x64.dll both) statically imports Runtime-x64.dll, so loading
    // one maps the DLL the exe is required never to map -- and the retail
    // Runtime that arrives that way has none of the engine state our ports and
    // our mint own, so its very first heap call faults. Measured 2026-09-13:
    // with the DLL still present on disk this loop loaded VideoDX9-x64.dll,
    // whose CreateDeviceData called Runtime+0xC7A8A and died in ntdll's heap.
    // The old cutover gate never saw it because it moved the DLL aside, which
    // made the LoadLibraryW fail for the wrong reason.
    //
    // So the loop resolves the name (GetDLLForInterface) and then loads NOTHING.
    // Slot 1 is already ours from the pre-fill above and is skipped; slot 0 gets
    // the placeholder below; slot 2 (VideoGL) has no shipped DLL at all.
    for (int i = 0; i < kSlotCount; ++i) {            // 0x1800f7afd / 0x1800f7b03
        InterfaceSlot* const slot = &dllSlots[i];
        if (slot->module != nullptr) {                // 0x1800f7a14
            ++skipped;
            continue;
        }

        SetErrorMode(SEM_FAILCRITICALERRORS);         // 0x1800f7a23, ECX = 1

        // ENDGAME B.3.5: a direct qualified member call where the body used to go
        // through the hidden-retslot door by hand (0x1800f7a34).
        String name = Video::Device::GetDLLForInterface(i);

        // 0x1800f7a44..0x1800f7ac9 was the WideString conversion, the
        // LoadLibraryW, the two GetProcAddress calls and the FreeLibrary
        // rejection path. All deleted: see the banner above. `loaded` and
        // `rejected` therefore stay 0, which is the truth about what happened.
        // 0x1800f7ad0..0x1800f7afd — `name`'s destructor, at the end of the
        // loop body, which is where retail's inlined release is.
    }

    // ---- NOT RETAIL --------------------------------------------------------
    // Slot 0 is now always empty (nothing loads VideoDX9-x64.dll), which leaves
    // the GraphicsInterface array one entry short of the slot count that the
    // configuration's interface index and the Options dialog's
    // GetInterface(cfgIndex) assume. Fill slot 0 with the placeholder
    // (src/render/dx11/dx9_placeholder.cpp explains the invariant).
    if (dllSlots[0].module == nullptr) {
        // Declared at file scope (video_interface.h), not block scope: inside a
        // `Video::Device` member a block-scope `extern` would name `Video::…`
        // and link-fail.
        ::VideoInterfaceFillDx9Placeholder(this);
    }
}
