// ============================================================================
// src/engine/engine/singleton_ctors.cpp — Wave 9 lane X
//   The fourteen constructors that PUBLISH a `?_singleton@...` DATA export.
// ============================================================================
//
// WHY THESE FOURTEEN ARE ONE FILE AND NOT FOURTEEN LANES
// -----------------------------------------------------
// They share one defect, not one namespace. Lane C2 (WAVE9_LANE_C2.md section 4)
// enumerated the exported DATA symbols the mint hands out as ZEROED storage at
// cutover and asked, per symbol, "who writes it?". Twenty-three rows answered
// "a function nobody has ported", and fourteen of those are read on the boot
// path by a body of ours that runs before or during the first frame. For every
// one of the fourteen the writer is that class's own constructor.
//
// So the disposition, fixed by lane C2 and not re-litigated here, is: PORT THE
// WRITER, WITH THE SINGLETON WRITE AS A NAMED ACCEPTANCE CRITERION.
//
// *** A COMPLETE, CORRECT, BYTE-FAITHFUL PORT OF ONE OF THESE CONSTRUCTORS IS
// *** STILL WRONG IF IT OMITS THE SINGLETON STORE.
//
// That is not hypothetical. Lane E3 hit it: `MemoryAllocator::Construct` ended
// in `if (slot) *slot = self`, the resolve returned null, the store was skipped
// WITHOUT A WORD, `MemoryAllocator::Get()` returned null for the rest of the
// process, and the first `MemAlloc` faulted on `null + 0x640` several frames
// away from anything naming `_singleton`. Every singleton is now its class's
// own static, and each body below stores it by name.
//
// EVIDENCE
// --------
// Every field store below is transcribed from `objdump -d -M intel` of the
// SHIPPED Runtime-x64.dll at the address named in each section banner.
//
// B.4 (R-ENG): the stores are made through the classes' own fields (they were
// byte-offset `W8/W16/W32/W64/WP` memcpy helpers). Two things the helpers
// preserved are kept by hand:
//
//   1. WIDTHS. Several constructors use one qword store across two named
//      fields (`MOV qword ptr [RCX+0x4],1` in Video::Configuration writes
//      `interfaceIndex = 1` AND `adapterIndex = 0`). Each is written here as
//      its two fields, same values, same order; the bytes are identical.
//   2. GAPS. Input::Device's three loops write bytes +0x00..+0x07 and
//      +0x10..+0x17 of each 0x18-byte record and NEVER TOUCH +0x08..+0x0F.
//      Video::Device never writes +0x1c..+0x1f or +0x98 (which is why
//      Device.h's `maxAnisotropy` comment says the ctor does not write it).
//      Only the fields retail writes are assigned, so the gaps stay unwritten.
//
// The callers are Engine::Engine and Engine::Startup (engine_lifecycle.cpp).
// B.4 (R-ENG): the scratch-buffer probe that stood at the bottom of this
// file had no caller left (its macro was never expanded) and is deleted.
// ============================================================================

#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cstddef>
#include <new>

#include "input/Device.h"   // ENDGAME B.3.9 ENG: the class static
#include "application/Application.h"   // ENDGAME B.3.9 ENG: the class static
#include "video/Device.h"             // ENDGAME B.3.9 VID: the class statics

#include "system/MemoryAllocator.h"
#include "io/Modification.h"
#include "io/FileSystem.h"
#include "io/Persistence.h"
#include "io/PersistenceWorker.h"   // ENDGAME B.3.9 IO (W2)
#include "object/External.h"
#include "object/Internal.h"
#include "video/Device.h"
#include "input/Input.h"
#include "audio/Audio.h"
#include "audio/Device.h"
#include "engine/Engine.h"     // B3R: rtengine::Ctor_* declarations
#include "system/Value.h"      // System::Random (ctor 0x1800c7f20)

// The platform half (singleton_ctors_lock.cpp).
void RtLaneXPlatformLockInit(Platform::LockStorage* cs);
// Installs allocator.cpp's `g_plat` function-pointer seam if it is not already
// installed, and returns whether it took. See the long comment above the
// MemoryAllocator subject in the probe: `rtalloc::Construct` opens with
// `g_plat.lockInit(...)`, and on a normal run that table is all-null because the
// only two callers of RtSystemAllocatorInstallPlatform() are opt-in.
bool RtLaneXEnsureAllocatorPlatform();

// System::MemoryAllocator's constructor body already exists: lane E ported it
// as `rtalloc::Construct` / `rtalloc::Destruct` in src/engine/system/
// allocator.cpp, singleton store included. It was never REGISTERED, which is
// why lane C2's writer audit still ranks that row (c) "writer exists but is NOT
// ported". This lane registers it; the body stays where its owner put it, and
// this file does not duplicate it.
namespace rtalloc {
void Construct(rt::system::MemoryAllocator* self);
void Destruct(rt::system::MemoryAllocator* self);
}  // namespace rtalloc

namespace {

// Offsets asserted against the headers that already pin them, so a header edit
// cannot silently move a field out from under a transcribed byte-offset store.
static_assert(offsetof(IO::Modification, _mods) == 0x08,
              "Modification ctor 0x1800b04b0: MOV qword [RCX+0x8],RAX");
static_assert(offsetof(IO::FileSystem, _existsInPackfileGate) == 0x18,
              "FileSystem ctor 0x1800ac7c0: MOV word [RCX+0x18],0x100 — low byte");
static_assert(offsetof(IO::FileSystem, _platformExistsGate) == 0x19,
              "FileSystem ctor 0x1800ac7c0: ... high byte, so platformExistsGate starts at 1");
static_assert(offsetof(RtObject::ExternalFactory, _referenceModeEnabled) == 0x40,
              "ExternalFactory ctor 0x1800be7e0: MOV word [RCX+0x40],DX");
static_assert(offsetof(RtObject::ExternalFactory, _simpleLoadEnabled) == 0x42,
              "ExternalFactory ctor 0x1800be7e0: MOV byte [RCX+0x42],DL");
static_assert(offsetof(RtVideo::Device, lock) == 0x60,
              "Video::Device ctor 0x1800f7160: ADD RCX,0x60 before InitializeCriticalSection");
static_assert(offsetof(RtVideo::Device, activeInterface) == 0xF8,
              "Video::Device ctor 0x1800f7160: MOV dword [RSI+0xf8],0xffffffff");
static_assert(offsetof(RtVideo::Configuration, refreshNumerator) == 0x38,
              "Video::Configuration ctor 0x1800f6f10: MOV dword [RCX+0x38],0x3c");
static_assert(offsetof(RtInput::Configuration, _mouseDisplay) == 0x0A,
              "Input::Configuration ctor 0x1800e0de0: MOV byte [RCX+0xa],0");
static_assert(RtInput::kKeyboardOffset == 0x6C0, "Input::Device ctor loop 2 base");
static_assert(RtInput::kMouseOffset == 0x1098, "Input::Device ctor loop 3 base");
static_assert(RtInput::kMouseClippingOffset == 0x11D0,
              "Input::Device ctor 0x1800e13b0: MOV byte [RCX+0x11d0],R8B");
static_assert(offsetof(RtAudio::Configuration, effectVolume) == 0x0C,
              "Audio::Configuration ctor 0x1800ddeb0: MOV dword [RCX+0xc],0x64");

}  // namespace

// B3R RT-Core: the fifteen constructor bodies below have external linkage
// (declared in src/engine/engine/Engine.h, namespace rtengine) because
// engine_lifecycle.cpp's Engine::Engine / Startup call them by name; they were
// reached through the ??0 / ??1 name rows before.
namespace rtengine {

// ===========================================================================
// System::MemoryAllocator   ??0MemoryAllocator@System@@QEAA@XZ  0x1800c79a0
//                           ??1MemoryAllocator@System@@QEAA@XZ  0x1800c7960
// ---------------------------------------------------------------------------
// Body: lane E's `rtalloc::Construct` / `rtalloc::Destruct`. Only the MSVC
// return-`this` convention is added here (the retail ctor ends `MOV RAX,RDI`).
// Registering is the whole change, and it is what moves lane C2's row from (c)
// "writer exists but is NOT ported" to (b).
// ===========================================================================
rt::system::MemoryAllocator* Ctor_MemoryAllocator(rt::system::MemoryAllocator* self) {
    rtalloc::Construct(self);
    return self;
}
void Dtor_MemoryAllocator(rt::system::MemoryAllocator* self) {
    // The dtor is a writer too: it stores nullptr into the same slot. Lane C2
    // lists it beside the ctor for that reason.
    rtalloc::Destruct(self);
}

// ===========================================================================
// IO::Modification          ??0Modification@IO@@QEAA@XZ         0x1800b04b0
// ---------------------------------------------------------------------------
//   MOV word ptr [RCX],0x101          ; +0x00 = 1, +0x01 = 1  (two bools)
//   XOR EAX,EAX
//   MOV qword ptr [RCX+0x8],RAX       ; the Mod array pointer
//   MOV qword ptr [RCX+0x10],RAX      ; count (+0x10) and +0x14
//   MOV qword ptr [RCX+0x18],RAX
//   MOV qword ptr [RCX+0x20],RAX
//   MOV qword ptr [0x1801fc6b0],RCX   ; _singleton
//   MOV RAX,RCX
// Lane M found this row independently; loops.cpp:265 reads the slot from
// `RtEngine_Loader_Load`, which is registered at 0x1800dc050 and runs on the
// boot path.
// ===========================================================================
IO::Modification* Ctor_Modification(IO::Modification* self) {
    self->_dirty          = 1;
    self->_savingSettings = 1;
    self->_mods._data          = nullptr;
    self->_mods._size          = 0;
    self->_mods._capacity      = 0;
    self->_savedMods._data     = nullptr;
    self->_savedMods._size     = 0;
    self->_savedMods._capacity = 0;
    IO::Modification::_singleton = self;
    return self;
}

// ===========================================================================
// Object::Factory           ??0Factory@Object@@QEAA@XZ          0x1800c09d0
// ---------------------------------------------------------------------------
//   XOR EAX,EAX
//   MOV qword ptr [RCX],RAX           ; +0x00
//   MOV dword ptr [RCX+0x8],EAX       ; +0x08   (+0x0c NOT written)
//   MOV qword ptr [0x1801fc6e8],RCX   ; _singleton
//   MOV RAX,RCX
//
// Object::Factory IS its type Set's {root, count} header (TypeInfo.h).
// ===========================================================================
::Object::Factory* Ctor_Factory(::Object::Factory* self) {
    self->_types._root  = nullptr;
    self->_types._count = 0;
    ::Object::Factory::_singleton = self;
    return self;
}

// ===========================================================================
// Object::ExternalFactory   ??0ExternalFactory@Object@@QEAA@XZ  0x1800be7e0
// ---------------------------------------------------------------------------
//   XOR EDX,EDX
//   MOV qword [RCX],RDX      / MOV dword [RCX+0x8],EDX     ; +0x0c NOT written
//   LEA RAX,[RCX+0x10] ; MOV qword [RAX],RDX / MOV dword [RAX+0x8],EDX
//   LEA RAX,[RCX+0x20] ; MOV qword [RAX],RDX / MOV dword [RAX+0x8],EDX
//   LEA RAX,[RCX+0x30] ; MOV qword [RAX],RDX / MOV qword [RAX+0x8],RDX
//   MOV word ptr [RCX+0x40],DX        ; _referenceModeEnabled, _loadEnabled
//   MOV byte ptr [RCX+0x42],DL        ; _simpleLoadEnabled
//   MOV qword ptr [0x1801fc6c8],RCX   ; _singleton
//
// The three `LEA` + store pairs are Collection::Array<T> default ctors inlined
// (`_data = 0; _size = 0;` with `_capacity` at +0x0c of each left alone) —
// which is why +0x0c, +0x1c and +0x2c stay unwritten. loops.cpp:257 reads this
// slot for `_loadEnabled`, the flag `Loader::Run` holds across a load.
// ===========================================================================
::Object::ExternalFactory* Ctor_ExternalFactory(::Object::ExternalFactory* self) {
    self->_cleanBitIndex        = 0;
    self->_cleanBitMask         = 0;
    self->_generation           = 0;
    self->_resources._root      = nullptr;
    self->_resources._count     = 0;
    self->_references._root     = nullptr;
    self->_references._count    = 0;
    self->_pathStack._data      = nullptr;
    self->_pathStack._size      = 0;
    self->_pathStack._capacity  = 0;
    self->_referenceModeEnabled = false;
    self->_loadEnabled          = false;
    self->_simpleLoadEnabled    = false;
    ::Object::ExternalFactory::_singleton = self;
    return self;
}

// ===========================================================================
// Object::InternalFactory   ??0InternalFactory@Object@@QEAA@XZ  0x1800c0030
// ---------------------------------------------------------------------------
//   XOR EAX,EAX
//   MOV qword [RCX],RAX ; [RCX+8],RAX ; [RCX+0x10],RAX ; [RCX+0x18],RAX
//   MOV dword [RCX+0x20],EAX                  ; Random::index = 0
//   LEA RCX,[RSP+0x48] ; CALL 0x18007fb9c     ; (uint)time(NULL)
//   LEA RDX,[RBX+0x24] ; MOV dword [RDX],EAX  ; Random::mt[0] = seed
//   ... 623 iterations of mt[i] = 0x6c078965*(mt[i-1]^(mt[i-1]>>30)) + i
//   MOV qword ptr [0x1801fc6d8],RBX           ; _singleton
//
// That tail IS `System::Random::Random()` (0x1800c7f20) inlined — index zeroed
// and the standard init_genrand recurrence over 624 words at +0x24, exactly
// ::Random's layout (Internal.h pins `random` at +0x20). Wave 2 already ported
// that constructor, so this body CALLS it through the port-aware door rather
// than transcribing the recurrence a second time; two copies of a PRNG seed
// that must agree is how a silent desync gets written.
//
// TIME-SEEDED, SO NOT BYTE-DIFFED. Two calls made microseconds apart can
// legitimately land in different seconds, and 0x6c078965 amplifies a
// one-count seed difference across all 624 words. The probe therefore compares
// only [0x00, 0x24) — the deterministic prefix and the Random index — and
// verifies the 623-word recurrence against our OWN mt[0]. Same rationale
// value_types.cpp already records for ??0Random@System@@QEAA@XZ itself.
// ===========================================================================
::Object::InternalFactory* Ctor_InternalFactory(::Object::InternalFactory* self) {
    self->_objects._data        = nullptr;
    self->_objects._size        = 0;
    self->_objects._capacity    = 0;
    self->_persistIds._data     = nullptr;
    self->_persistIds._size     = 0;
    self->_persistIds._capacity = 0;
    ::new (&self->_random) System::Random();   // 0x1800c7f20 (inlined in retail); raw MemAlloc storage
    ::Object::InternalFactory::_singleton = self;
    return self;
}

// ===========================================================================
// IO::FileSystem            ??0FileSystem@IO@@QEAA@XZ           0x1800ac7c0
// ---------------------------------------------------------------------------
//   XOR EDX,EDX
//   MOV qword [RCX],RDX                       ; sourceDirectory
//   LEA RAX,[RCX+0x8]  ; MOV qword [RAX],RDX  ; dataDirectory
//   LEA RAX,[RCX+0x10] ; MOV qword [RAX],RDX
//   MOV word ptr [RCX+0x18],0x100             ; existsInPackfileGate = 0,
//                                             ; platformExistsGate   = 1
//   MOV qword [RCX+0x20],RDX ; [RCX+0x28],RDX ; [RCX+0x30],RDX ; [RCX+0x38],RDX
//   LEA RAX,[RCX+0x40] ; MOV qword [RAX],RDX ; MOV dword [RAX+0x8],EDX
//   MOV qword ptr [0x1801fc6a8],RCX           ; _singleton
//
// The 0x100 word is the one non-zero initial value in the object and it is the
// reason a "memset the struct" port would be wrong: `platformExistsGate` starts
// ENABLED, and FileSystem::Exists reads it every lookup.
// ===========================================================================
IO::FileSystem* Ctor_FileSystem(IO::FileSystem* self) {
    self->_sourceDirectory._data   = nullptr;   // Filename() inlined, 0x1800ac7d8
    self->_dataDirectory._data     = nullptr;   // 0x1800ac7e4
    self->_packDirectory._data     = nullptr;   // 0x1800ac7f0
    self->_existsInPackfileGate    = 0;
    self->_platformExistsGate      = 1;
    self->_packfiles._data         = nullptr;
    self->_packfiles._size         = 0;
    self->_packfiles._capacity     = 0;
    self->_userPackFilenames.data     = nullptr;
    self->_userPackFilenames.size     = 0;
    self->_userPackFilenames.capacity = 0;
    self->_packfileIndex.root       = nullptr;
    self->_packfileIndex.count      = 0;
    IO::FileSystem::_singleton = self;
    return self;
}

// ===========================================================================
// IO::Persistence           ??0Persistence@IO@@QEAA@XZ          0x1800b8020
// ---------------------------------------------------------------------------
// B.4 wave X (X7-RT): the class's own constructor now (its members are typed:
// src/engine/io/Persistence.h quotes the retail sequence), defined in
// src/engine/io/persistence.cpp; Engine's startup placement-constructs it.
// ===========================================================================

// ===========================================================================
// Video::Device   ??0Device@Video@@QEAA@W4DeviceType@01@@Z      0x1800f7160
// ---------------------------------------------------------------------------
//   MOV dword [RCX],EDX                       ; type = the ctor argument
//   XOR EBP,EBP
//   MOV qword [RCX+0x4],RBP                   ; UNALIGNED: +0x04..+0x0b
//   MOV qword [RCX+0xc],RBP                   ; UNALIGNED: +0x0c..+0x13
//   MOV qword [RCX+0x14],RBP                  ; UNALIGNED: +0x14..+0x1b
//                                             ; +0x1c..+0x1f NEVER WRITTEN
//   MOV qword [RCX+0x20 .. +0x58],RBP         ; eight aligned qwords
//   ADD RCX,0x60 ; CALL [InitializeCriticalSection]
//   MOV EDX,0x1388 ; LEA RCX,[RSI+0x60] ; CALL [SetCriticalSectionSpinCount]
//   MOV qword [RSI+0xb0 .. +0xf0],RBP         ; the three plugin slots
//   MOV qword [RSI+0x90],RBP                  ; windowHandle
//   MOV word  [RSI+0x9c],BP                   ; settingsChanged, field9D
//   MOV dword [RSI+0xf8],0xffffffff           ; activeInterface = -1
//   MOV qword [RSI+0x100],RBP                 ; deviceData
//   MOV qword [RSI+0xa0],RBP ; [RSI+0xa8],RBP
//   MOV RAX,[0x1801fe7a8] ; TEST EDI,EDI ; CMOVE RAX,RSI ; MOV [0x1801fe7a8],RAX
//
// *** THE SINGLETON STORE IS CONDITIONAL AND THE CONDITION IS THE ARGUMENT. ***
// The CMOVE keeps the OLD value unless `type == 0`, so only a DeviceType-0
// device becomes `?_singleton@Device@Video@@`. Retail still executes an
// unconditional STORE of the cmov result, which is why this port writes the
// slot back rather than skipping the write — those differ if another thread
// wrote the slot in between, and faithful means faithful.
//
// +0x98 (`maxAnisotropy`) is NOT written by the constructor — Device.h already
// says so, and adapter enumeration fills it. +0x1c..+0x1f likewise.
// ===========================================================================
Video::Device* Ctor_VideoDevice(Video::Device* self, int32_t type) {
    self->type               = type;
    self->backBufferWidth    = 0;
    self->backBufferHeight   = 0;
    self->displayModeIndex   = 0;
    self->refreshRateIndex   = 0;
    self->dxgiAdapterIndex   = 0;
    self->dxgiOutputIndex    = 0;
    self->modeData           = nullptr;
    self->modeCount          = 0;
    self->modeCapacity       = 0;
    self->multisampleData    = nullptr;
    self->multisampleCount   = 0;
    self->multisampleCap     = 0;
    self->interfaceData      = nullptr;
    self->interfaceCount     = 0;
    self->interfaceCapacity  = 0;
    self->onVideoError       = nullptr;   // +0x50 } two qword stores in retail
    self->onVideoRestart     = nullptr;   // +0x58 }
    RtLaneXPlatformLockInit(&self->lock);
    for (int i = 0; i < 3; ++i) {
        self->dllSlots[i].module           = nullptr;
        self->dllSlots[i].getDeviceName    = nullptr;
        self->dllSlots[i].createDeviceData = nullptr;
    }
    self->windowHandle       = nullptr;
    self->settingsChanged    = 0;
    self->field9D            = 0;
    self->activeInterface    = -1;
    self->deviceData         = nullptr;
    self->monitor            = nullptr;
    std::memset(self->gapA8, 0, sizeof(self->gapA8));   // one qword store in retail

    Video::Device* const keep = Video::Device::_singleton;
    Video::Device::_singleton = (type == 0) ? self : keep;   // the CMOVE, then the store
    return self;
}

// ===========================================================================
// Video::Configuration      ??0Configuration@Video@@QEAA@XZ     0x1800f6f10
// ---------------------------------------------------------------------------
//   MOV byte  [RCX],1                 ; settingsValid       (+0x01..+0x03 gap)
//   MOV qword [RCX+0x4],1             ; interfaceIndex=1, adapterIndex=0
//   MOV qword [RCX+0xc],-1            ; width=-1, height=-1
//   MOV qword [RCX+0x14],-1           ; windowX=-1, windowY=-1
//   MOV qword [RCX+0x1c],-1           ; windowWidth=-1, windowHeight=-1
//   MOV qword [RCX+0x24],1            ; multisample=1, textureFilter=0
//   MOV dword [RCX+0x2c],1            ; shadowScalar
//   MOV dword [RCX+0x30],2            ; shaderDetail
//   MOV dword [RCX+0x34],1            ; reflectionDetail
//   MOV dword [RCX+0x38],0x3c         ; refreshNumerator = 60
//   MOV dword [RCX+0x3c],1            ; refreshDenominator
//   MOV word  [RCX+0x40],0x101        ; fullscreen = 1, vsync = 1
//   MOV byte  [RCX+0x42],0            ; field42
//   MOV qword ptr [0x1801fe7a0],RCX   ; _singleton
//
// Six of the thirteen stores are UNALIGNED qwords spanning two named fields.
// Transcribing them as pairs of dword writes would produce the same bytes here,
// but it would lose the fact — and this is the class where a "cleanup" to
// member assignment is most tempting, because every field has a name.
// ===========================================================================
Video::Configuration* Ctor_VideoConfiguration(Video::Configuration* self) {
    self->settingsValid      = 1;
    self->interfaceIndex     = 1;
    self->adapterIndex       = 0;
    self->width              = -1;
    self->height             = -1;
    self->windowX            = -1;
    self->windowY            = -1;
    self->windowWidth        = -1;
    self->windowHeight       = -1;
    self->multisample        = 1;
    self->textureFilter      = 0;
    self->shadowScalar       = 1;
    self->shaderDetail       = 2;
    self->reflectionDetail   = 1;
    self->refreshNumerator   = 0x3c;
    self->refreshDenominator = 1;
    self->fullscreen         = 1;
    self->vsync              = 1;
    self->field42            = 0;
    Video::Configuration::_singleton = self;
    return self;
}

// ===========================================================================
// Application::Application  ??0Application@0@QEAA@PEAX@Z        0x18009f590
// ---------------------------------------------------------------------------
//   LEA RAX,[RCX+0x18] ; XOR R8D,R8D
//   MOV qword [RAX],R8                ; +0x18
//   MOV dword [RAX+0x8],R8D           ; +0x20
//   MOV byte  [RCX+0x28],R8B          ; _quitRequested
//   MOV qword ptr [0x1801fc678],RCX   ; _singleton  <-- BEFORE the next three
//   MOV qword [RCX],R8                ; +0x00
//   MOV qword [RCX+0x8],RDX           ; +0x08 = the ctor's argument
//   MOV word  [RCX+0x10],0x100        ; _cursorEnabled = 0, _running = 1
//
// THE SINGLETON STORE IS NOT LAST. It sits between the +0x28 store and the
// +0x00 store, and the order is preserved here rather than tidied to the end,
// because a reader who assumes "publish last" would draw the wrong conclusion
// about what a concurrent reader can observe.
// ===========================================================================
::Application::Application* Ctor_Application(::Application::Application* self, void* hinstance) {
    self->_messageCallbacks.root  = nullptr;
    self->_messageCallbacks.count = 0;
    self->_quitRequested        = 0;
    ::Application::Application::_singleton = self;
    self->_windowHandle         = nullptr;
    self->_platformHandle       = hinstance;
    self->_cursorEnabled        = 0;
    self->_running              = 1;
    return self;
}

// ===========================================================================
// Input::Device             ??0Device@Input@@QEAA@XZ            0x1800e13b0
// ---------------------------------------------------------------------------
// Three identical loops, one per input block, each writing TWO qwords of every
// 0x18-byte `State` record and skipping the middle one:
//
//   MOV EDX,0x47 ; LEA RAX,[RCX+0x6] ; XOR R8D,R8D
//   loop: DEC EDX ; MOV [RAX-0x6],R8 ; MOV [RAX+0xa],R8 ; LEA RAX,[RAX+0x18]
//         JNS loop
//
// `MOV EDX,0x47` with `DEC`/`JNS` runs 72 times (0x47 down through 0, then -1
// ends it) — 72 * 0x18 == 0x6C0, exactly Input.h's kKeyboardOffset, which is
// the independent check that 72 and not 71 is right. The other two loops are
// 0x68 -> 105 records from 0x6C0 and 0x0c -> 13 records from 0x1098, and both
// land exactly on the next block boundary.
//
// *** BYTES +0x08..+0x0F OF EVERY RECORD ARE NEVER WRITTEN. *** The ctor writes
// [rec+0x00..0x07] and [rec+0x10..0x17] and nothing else, across all 190
// records. That is not an optimisation this port may "complete": ResetState
// (0x1800e0ef0) zeroes the full record, the constructor does not, and a port
// that zeroed them would differ from retail on 1520 bytes.
//
//   MOV qword ptr [0x1801fe770],RCX   ; _singleton
//   MOV byte  [RCX+0x11d0],R8B        ; _mouseClipping   (+0x11d1..+0x11d7 gap)
//   MOV qword [RCX+0x11d8],R8 ; [RCX+0x11e0],R8 ; [RCX+0x11e8],R8
//   MOV byte  [RCX+0x11f0],1
//   MOV qword [RCX+0x11f4],R8         ; UNALIGNED
//   MOV dword [RCX+0x11fc],R8D
// ===========================================================================
::Input::Device* Ctor_InputDevice(::Input::Device* self) {
    // Each record: the qword at +0x00 (normalized, value, pressed, released)
    // and the qword at +0x10 (holdTicks); +0x08..+0x0f untouched.
    auto clearRecord = [](::Input::Device::State& s) {
        s.normalized = 0.0f;
        s.value      = 0;
        s.pressed    = false;
        s.released   = false;
        s.holdTicks  = 0;
    };
    for (auto& block : self->_joystick)                  // 4 joystick blocks, 72 records
        for (auto& s : block) clearRecord(s);
    for (auto& s : self->_keyboard) clearRecord(s);      // keyboard, 105
    for (auto& s : self->_mouse)    clearRecord(s);      // mouse, 13
    ::Input::Device::_singleton = self;
    self->_mouseClipping  = 0;
    self->_directInput    = nullptr;
    self->_keyboardDevice = nullptr;
    self->_mouseDevice    = nullptr;
    self->_hasFocus       = 1;
    self->_wheelAccum     = 0;
    self->_lastCursorX    = 0;
    self->_lastCursorY    = 0;
    return self;
}

// ===========================================================================
// Input::Configuration      ??0Configuration@Input@@QEAA@XZ     0x1800e0de0
// ---------------------------------------------------------------------------
//   MOV qword [RCX],0                 ; keyMap
//   MOV word  [RCX+0x8],0             ; mouseClipping, legacy
//   MOV byte  [RCX+0xa],0             ; mouseDisplay
//   MOV qword ptr [0x1801fe768],RCX   ; _singleton
// ===========================================================================
::Input::Configuration* Ctor_InputConfiguration(::Input::Configuration* self) {
    self->_keyMap        = nullptr;
    self->_mouseClipping = false;
    self->_legacy        = false;
    self->_mouseDisplay  = false;
    ::Input::Configuration::_singleton = self;
    return self;
}

// ===========================================================================
// Audio::Configuration      ??0Configuration@Audio@@QEAA@XZ     0x1800ddeb0
// ---------------------------------------------------------------------------
//   MOV dword [RCX],0x64              ; uiVolume      = 100
//   MOV dword [RCX+0x4],0x32          ; musicVolume   = 50
//   MOV dword [RCX+0x8],0x64          ; ambientVolume = 100
//   MOV dword [RCX+0xc],0x64          ; effectVolume  = 100
//   MOV qword ptr [0x1801fe758],RCX   ; _singleton
//
// The four literals ARE the shipped defaults — music at half, everything else
// full. A zeroed row at cutover would be a silent MUTE, not a crash, which is
// the kind of tier-1 row that would have survived a first cutover boot.
// ===========================================================================
Audio::Configuration* Ctor_AudioConfiguration(Audio::Configuration* self) {
    self->uiVolume      = 0x64;
    self->musicVolume   = 0x32;
    self->ambientVolume = 0x64;
    self->effectVolume  = 0x64;
    Audio::Configuration::_singleton = self;
    return self;
}

// ===========================================================================
// Audio::Device             ??0Device@Audio@@QEAA@XZ            0x1800de3a0
// ---------------------------------------------------------------------------
//   MOV EDI,0x3f ; MOV ECX,EDI ; LEA RAX,[RBX+0x10] ; XOR ESI,ESI
//   loop1: MOV [RAX-0x10],ESI ; MOV [RAX-0x8],RSI ; MOV [RAX],RSI
//          MOV [RAX+0x8],ESI  ; MOV [RAX+0x10],RSI
//          LEA RAX,[RAX+0x28] ; DEC ECX ; JNS loop1
//        -> 64 records of stride 0x28 covering +0x000..+0x9ff; each record
//           writes +0x00 (dword), +0x08, +0x10, +0x18 (dword), +0x20 and
//           leaves +0x04..+0x07 and +0x1c..+0x1f alone.
//   LEA RCX,[RBX+0xa00] ; CALL 0x1800a0e30      ; ??0Stream@Audio@@QEAA@XZ
//   LEA RAX,[RBX+0xa98] ; LEA RCX,[0x180157c60]
//   loop2: MOV [RAX],RCX ; MOV [RAX+0x8],RSI ; MOV [RAX+0x18],RSI
//          LEA RAX,[RAX+0x20] ; DEC EDI ; JNS loop2
//        -> 64 records of stride 0x20 from +0xa98 to +0x1298, each stamped with
//           the same vtable and leaving +0x10..+0x17 for loop3.
//   MOV qword ptr [0x1801fe760],RBX             ; _singleton
//   MOV [RBX+0xa88],RSI ; MOV [RBX+0xa90],RSI
//   LEA RAX,[RBX+0xaa8] ; MOV RCX,RBX ; MOV EDX,0x40
//   loop3: MOV [RAX],RCX ; ADD RCX,0x28 ; LEA RAX,[RAX+0x20] ; DEC RDX ; JNE
//        -> each 0x20 record's +0x10 field points at the matching 0x28 record
//           from loop1, i.e. the two arrays are cross-linked 1:1.
//
// *** ORACLE DEFERRED — SAME BLOCKER AS PERSISTENCE. *** 0x1800a0e30 is
// `??0Stream@Audio@@QEAA@XZ`, a System::Thread SUBCLASS: it calls the same
// `??0Thread@System@@QEAA@I@Z` base that `_beginthreadex`s a suspended worker
// and creates three events, then stamps `??_7Stream@Audio@@6B@` (0x180154950)
// and initialises a second CRITICAL_SECTION. Running the shipped body twice
// against scratch buffers would start two threads on fake objects and the
// handle values would differ by construction. Registered anyway for the same
// reason as Persistence: no caller today, and it is the only writer of
// `?_singleton@Device@Audio@@`, which audio.cpp:880 reads.
//
// Audio::Stream is reached BY NAME, not by address: it is exported, and asking
// by name is what made this call site follow the port. It IS ported now
// (src/engine/audio/audio_stream.cpp registers `??0Stream@Audio@@QEAA@XZ`;
// Audio::Stream is a real System::Thread subclass since ENDGAME B.3.7 block B4),
// and the ctor stays a door (B4b-2: no real ctor on a polymorphic class).
//
// *** THIS RESIDUAL CUTOVER DEPENDENCY IS DISCHARGED. *** The paragraph that
// stood here said: "The per-record vtable at 0x180157c60 is not an export and is
// not in lane C2's mint table, so it is reached by address and will be null at
// cutover." The second clause is NO LONGER TRUE and has not been for some time.
// Re-derived by Wave 24 lane SMALLBODY, and independently by lane CUTOVER:
//
//     grep -n '180157c60' standalone/rt_data_mint_tables.inc
//     608:  // (unexported, carried by address)  DLL 0x180157c60  7 slots
//     1542: { nullptr, 0x180157c60ull, 7, kVSlots2, kVTable2, kVTraps2, ... }
//
// It IS in the mint table, carried as an unexported address-keyed row with 7
// slots, which is exactly the shape an unexported per-record vtable needs. The
// `ResolveAddr(Module::Runtime, 0x180157c60)` door above therefore resolves to
// the minted row at cutover rather than to null.
//
// This correction is itself the Wave 23 §6 defect class — a discharge does not
// propagate, and nothing in this project detects a stale blocker. The comment
// was true when written and has been read as current ever since.
//
// NOT discharged, and deliberately still named: Persistence's 0x180155860,
// which this paragraph used to bracket with 0x180157c60. It was NOT re-checked
// by this lane and nothing here should be read as a verdict on it.
//
// The per-record vtable 0x180157c60 is `Audio::Device::PlatformChannel`'s (the
// IXAudio2VoiceCallback record, `GetPlatformChannel` 0x18000c720 =
// this+0xa98+i*0x20 -- named by block B4; B37_RULES 10).
//
// Audio::Device HAS a type header now (src/engine/audio/Device.h, ENDGAME
// B.3.7 block B4): the two arrays are `Device::AudioChannel _channels[64]` at
// +0x000 and `Device::PlatformChannel _platformChannels[64]` at +0xa98, with
// the Stream by value at +0xa00. This constructor is still the DOOR (B7-12: no
// real Device ctor this step), so it keeps writing the offsets it transcribed.
// ===========================================================================
// ENDGAME B.3.9 ENG: loop 2's vptr + two zero stores are
// `Audio::Device::PlatformChannel`'s real constructor now -- `::new` into each
// record -- so every record wears the class's (COM-shaped) table, and the
// by-address read of the minted unnamed row (a function-local lazy cache, B9-10)
// is gone with that row.
Audio::Device* Ctor_AudioDevice(Audio::Device* self) {
    for (int i = 0; i < Audio::Device::kChannelCount; ++i) {
        Audio::Device::AudioChannel& c = self->_channels[i];
        c.flags      = 0;
        c.source     = nullptr;
        c.userData   = nullptr;
        c.userValue  = 0;
        c.onComplete = nullptr;
    }
    // ENDGAME B.3.9 ENG W2: `??0Stream` is the class's real ctor now (it was
    // the door, by name); built in place at +0xa00 as retail's LEA/CALL does.
    ::new (&self->_stream) Audio::Stream();
    for (int i = 0; i < Audio::Device::kChannelCount; ++i)
        ::new (&self->_platformChannels[i]) Audio::Device::PlatformChannel();
    Audio::Device::_singleton = self;
    self->_xaudio2        = nullptr;
    self->_masteringVoice = nullptr;
    for (int i = 0; i < Audio::Device::kChannelCount; ++i)
        self->_platformChannels[i].channel = &self->_channels[i];
    return self;
}

}  // namespace rtengine

