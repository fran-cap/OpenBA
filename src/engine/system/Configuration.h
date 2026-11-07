#pragma once
// ============================================================================
// src/engine/system/Configuration.h — the real `System::Configuration`
// ============================================================================
//
// ENDGAME B.3.4b SYSTEM (4c). The derivations stay where they were written:
// `src/engine/system/configuration.cpp`'s banner is the command-line parser
// and the string-keyed BST find, instruction by instruction, and
// `src/engine/engine/engine_lifecycle.cpp:305-332` is the constructor. This
// header carries the SHAPE, the evidence for it, and — at the bottom — the
// four things that deliberately did NOT convert.
//
// ============================================================================
// "IS IT A CLASS?" — YES, WITH INSTANCE STATE
// ============================================================================
// The test (B34B_RULES.md §1.2): MSVC emits a class-scoped mangling only for a
// member of a class of that name. Read out of `standalone/rt_export_addrs.inc`
// before a line was moved — the PORTED surface is split across two files, which
// is this class's whole trap:
//
//  registered in src/engine/system/configuration.cpp
//   ?SetCommandLine@Configuration@System@@QEAAXAEBVString@2@@Z      0x1800c47a0  QEAA
//   ?GetCommandLineString@…@QEBA?AVString@2@AEBV32@@Z               0x1800c5300  QEBA
//   ??1Configuration@System@@QEAA@XZ                                0x1800c5170  QEAA
//   ?ParseCommand@…@CA?AVString@2@AEAHAEBV32@@Z                     0x1800c4a00  CA
//   ?ParseValue@…@CA?AVString@2@AEAHAEBV32@@Z                       0x1800c4930  CA
//  registered in src/engine/system/value_types.cpp
//   ?CurrentBuildId@…@SA?AVString@2@XZ                              0x1800c4c60  SA
//   ?CurrentVersion@…@SA?AVString@2@XZ                              0x1800c4f40  SA
//   ?GetVersionString@…@SA?AVString@2@I_N@Z                         0x1800c4f90  SA
//  registered in src/engine/engine/engine_lifecycle.cpp
//   ??0Configuration@System@@QEAA@XZ                                0x1800c51b0  QEAA
//
// Three `QEAA`/`QEBA` instance members over `this`, two `CA` private statics,
// three `SA` public statics and a `QEAA` constructor. Nine class-scoped
// manglings; not the `IO::TextCodec` shape.
//
// ============================================================================
// THE LAYOUT — 0x30 BYTES, THREE SUB-OBJECTS, AND THE CTOR PINS ALL OF IT
// ============================================================================
// `??0Configuration@System@@QEAA@XZ` (0x1800c51b0) is its own layout document,
// and the block MSVC inlined at 0x1800db938-0x1800db954 is the same bytes:
//
//   XOR EDX,EDX
//   MOV [RCX],RDX            MOV [RCX+0x08],RDX
//   LEA RAX,[RCX+0x10]
//   MOV [RAX],RDX            MOV dword [RAX+0x08],EDX    ; a {ptr,int} pair
//   MOV [RCX+0x20],RDX       MOV dword [RCX+0x28],EDX    ; a second one
//   MOV [0x1801fc6f8],RCX    ; ?_singleton@Configuration@System@@
//   MOV RAX,RCX
//
// so: an `Array`-shaped sub-object at +0x00, the string→string map at +0x10 and
// a THIRD {ptr,int} at +0x20. Total 0x30.
//
// **THE FOUR BYTES AT +0x1c AND +0x2c ARE NEVER WRITTEN.** The two `MOV dword`
// stores are 4 bytes each; retail leaves whatever the allocator left in the
// upper half of each pair. `_pad1c` / `_pad2c` are named so that a future
// `memset(this, 0, 0x30)` reads as the behaviour change it would be — and no
// oracle in this project could see that change
// (`reference_oracle_must_not_compare_unwritten_bytes.md`).
//
// **THE SUB-OBJECT AT +0x20 IS NEVER DESTROYED, AND THAT IS RETAIL'S CHOICE.**
// `~Configuration` (0x1800c5170) clears the map at +0x10 and tail-calls
// `~Array(+elem)` on `this`, and touches +0x20 not at all. It is a retail leak
// of the same family as the two already flagged inside `Engine::Shutdown`
// (`Input::Device`, `Audio::Device`) and it is REPRODUCED, not tidied: freeing
// a block retail still considers live is strictly worse than the leak.
//
// **THE FIELD TYPES AT +0x20 / +0x28 ARE POSITIONAL, NOT STRUCTURAL.** The
// four accessors below name them; +0x00 and +0x10 are the real classes (next
// paragraph), and the `offsetof` asserts below are what stopped that
// replacement from moving anything.
//
// **B7 LX (D7a): +0x00 AND +0x10 ARE THE REAL CLASSES.** +0x00 is
// `Collection::Array<System::String>` (0x180010ab0 is its J2 String-element
// destructor, array_destroy.cpp `kD_Str_08`) and +0x10 the settings
// `Collection::Set<Pair<String,String>>` (RtIo::SettingsSet, io/Persistence.h).
// Map_Find (0x18002ace0) IS the Set's `Find`: `IO::FileSystem::Startup`,
// `FileSystem::SetupDirectories`, the engine's command-line test and
// GetCommandLineString call `_map.Find(key._data)`. Declared in that order, the
// two members' destructors run Set then Array after `~Configuration`'s empty
// body -- 0x1800c518f CALL 0x18000f7d0, then 0x1800c519d JMP 0x180010ab0.
//
// ⚠ THE GREP HAZARD (B34_RULES §7.2) is armed for this name: inside a
// `namespace System { ... }` block a bare `Configuration` binds this class.
// There is no POD of the same name, so the risk is only that the word used to
// appear in comments and now names a type.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <type_traits>

// `GetCommandLineString` and the three statics return `System::String` BY
// VALUE, so the definition is needed, not a declaration. Global scope, above
// every namespace block, never inside an `#if` (B31 hygiene / B34_BRIEFS §A.8).
#include "String.h"

#include "../io/Persistence.h"   // RtIo::SettingsSet: the map at +0x10 (and Collection::Array)

namespace System {

// A game version packs major << 24 | minor << 16 | patch << 8 | stage
// (GetVersionString 0x1800c4f90). The stage byte appends a suffix; a shipped
// build is Release.
enum class VersionStage : uint32_t {
    Prototype = 0xfb,   // "R" / " Prototype"
    PreAlpha  = 0xfc,   // "P" / " Pre Alpha"
    Alpha     = 0xfd,   // "A" / " Alpha"
    Beta      = 0xfe,   // "B" / " Beta"
    Release   = 0xff,   // no suffix
};
constexpr uint32_t kGameVersion       = 0x010007ffu;   // 1.0.7 release (CurrentVersion 0x1800c4f40)
constexpr uint32_t kModMinGameVersion = 0x01000400u;   // 1.0.4: a mod built for an older game is out of date

class Configuration {
public:
    // ---- instance members --------------------------------------------------
    // ?SetCommandLine@Configuration@System@@QEAAXAEBVString@2@@Z  0x1800c47a0
    // Parses `/key value` pairs out of the command line into the map at +0x10.
    // On an EMPTY command line — which is every automated run this project has,
    // because every knob here is an environment variable — retail's own `JLE`
    // at 0x1800c47d7 returns before the loop body is reached. See
    // configuration.cpp §2 before concluding the loop is dead code: it is not,
    // it is unreached, and launching with any argument reaches it.
    void SetCommandLine(const String& cmd);

    // ?GetCommandLineString@…@QEBA?AVString@2@AEBV32@@Z           0x1800c5300
    //
    // ⚠ RETURNS `System::String` BY VALUE, WHICH IS A HIDDEN RETURN SLOT AT THE
    // ABI. Retail's is `String* f(const Configuration* this, String* ret, const
    // String& key)` — RCX = this, RDX = ret, R8 = key — and a typedef that
    // omitted the slot would put `key` in RDX, hand the find a `String**` where
    // it wants a `String*`, and write the answer through whatever the caller
    // left in R8: a wild write that STILL returns the right answer in RAX
    // (`reference_hidden_retslot_wild_write.md`). The registered DOOR keeps the
    // three-argument shape and placement-news this member's prvalue into the
    // caller's slot; the MEMBER returns by value, which is what retail's source
    // said. Never register the member directly.
    String GetCommandLineString(const String& key) const;

    // ??0Configuration@System@@QEAA@XZ                            0x1800c51b0
    // B7 LX (D7a): the real constructor (the `RtSystem_Configuration_Ctor`
    // door is deleted; Engine::Startup placement-constructs the 0x30-byte
    // block). The members' ctors are retail's stores in address order:
    //   1800c51c3 / c51c6  qword +0x00, qword +0x08   `_entries` (Array: data,
    //                                                  size + capacity)
    //   1800c51d3 / c51d6  qword +0x10, dword +0x18   `_map` (Set: root, count;
    //                                                  +0x1c NOT written)
    //   1800c51d9 / c51dd  qword +0x20, dword +0x28   platform + language, build
    //   1800c51e0          `_singleton = this`
    // Header-defined: Engine::Startup INLINES it (0x1800db938..0x1800db954).
    Configuration()
        : _entries(), _map(),
          _currentPlatform(0), _currentLanguage(0),   // 1800c51d9: one qword zero (H7)
          _currentBuild(0) {                          // 1800c51dd
        _singleton = this;                            // 1800c51e0 MOV [0x1801fc6f8],RCX
    }

    // ??1Configuration@System@@QEAA@XZ                            0x1800c5170
    // NON-VIRTUAL (`QEAA`, not `UEAA` — the `UEAA` spelling does not exist in
    // this DLL and would have resolved NULL in silence,
    // `reference_phantom_dtor_mangling_qeaa_vs_ueaa.md`). Clears the map at
    // +0x10 and tail-calls `~Array(+elem)` on `this`: the empty body, then the
    // two members' destructors in reverse declaration order. PUBLIC and
    // non-virtual: nothing derives from this class and nothing deletes one
    // through a base.
    ~Configuration();

    // ---- private statics, `CA` -------------------------------------------
    // ?ParseCommand@…@CA?AVString@2@AEAHAEBV32@@Z                 0x1800c4a00
    // ?ParseValue@…@CA?AVString@2@AEAHAEBV32@@Z                   0x1800c4930
    // `CA` is the PRIVATE static spelling; both are real exports (ord 3310 and
    // 3311) and both are registered, so they are PUBLIC here for the same
    // reason `Random::NextGroup` is — their doors call them and access control
    // is not ABI. Both return `String` BY VALUE, so both doors are
    // hidden-retslot doors. `pos` is an in/out `int&`.
    static String ParseCommand(int& pos, const String& src);
    static String ParseValue(int& pos, const String& src);

    // ---- public statics, `SA` --------------------------------------------
    // ?GetVersionString@…@SA?AVString@2@I_N@Z                     0x1800c4f90
    // `version` is packed major<<24 | minor<<16 | patch<<8 | suffix.
    static String GetVersionString(uint32_t version, bool detailed);
    // ?CurrentVersion@…@SA?AVString@2@XZ                          0x1800c4f40
    // Literally `GetVersionString(0x010007ff, false)`.
    static String CurrentVersion();

    // ?_singleton@Configuration@System@@0PEAV12@EA  0x1801fc6f8 — ENDGAME B.3.9
    // SYS (B39_RULES D7): the class static, defined in configuration.cpp;
    // RtSystemPortInit redirects the minted row onto it. Written by the ctor
    // door (engine_lifecycle.cpp, retail `MOV [0x1801fc6f8],RCX`). PUBLIC
    // while foreign readers (IO, the loader thread) still reach the door.
    static Configuration* _singleton;

    // ---- the 0x30 bytes ----------------------------------------------------
    // PUBLIC for the same reason `System::Thread`'s handles are: the
    // unconverted tree reaches these bytes through a cast, and `offsetof` on
    // them has to be well formed.
    // +0x00 -- `Collection::Array<System::String>`: data +0x00, size +0x08
    // (~Array 0x180010ab0 reads the count there), capacity +0x0c.
    Collection::Array<System::String> _entries;   // +0x00
    // +0x10 -- the string->string map, `Collection::Set<Pair<String,String>>`
    // (RtIo::SettingsSet: Map_Find 0x18002ace0 and Map_Insert 0x18002a090
    // serve this map and IO::Persistence's alike): root +0x10, count +0x18,
    // and +0x1c  ⚠ NEVER WRITTEN by the constructor (the Set ctor's two stores).
    RtIo::SettingsSet _map;  // +0x10
    // B.4 R-SYS: +0x20..+0x2b are THREE DWORDS, not a {ptr,int} sub-object.
    // Named by the exported one-line accessors (Runtime-x64.dll):
    //   ?SetCurrentPlatform@ 0x1800c4c20  MOV dword [RCX+0x20],EDX
    //   ?GetCurrentPlatform@ 0x1800c5270  MOV EAX,dword [RCX+0x20]
    //   ?GetCurrentLanguage@ 0x1800c4c30  MOV EAX,dword [RCX+0x24]
    //   ?GetCurrentBuild@    0x1800c52f0  MOV EAX,dword [RCX+0x28]
    // and FileSystem::SetupDirectories reads +0x20 / +0x28 as dwords
    // (0x1800ac06a..8b). The ctor's qword zero at +0x20 is the platform AND
    // the language (two named stores, H7). Nothing here is an allocation, so
    // the dtor leaving +0x20 alone is not a leak: the "sub-object" reading in
    // the banner above is corrected by these four accessors.
    int32_t  _currentPlatform;  // +0x20
    int32_t  _currentLanguage;  // +0x24
    int32_t  _currentBuild;     // +0x28
    uint32_t _pad2c;         // +0x2c  ⚠ NEVER WRITTEN by the constructor
};

static_assert(sizeof(Configuration) == 0x30, "System::Configuration is 0x30 bytes");
static_assert(offsetof(Configuration, _entries)     == 0x00, "Array data @ +0x00");
static_assert(offsetof(Configuration, _entries) + offsetof(Collection::Array<System::String>, _size) == 0x08, "0x180010ab0 reads [this+0x8]");
static_assert(offsetof(Configuration, _map)         == 0x10, "LEA RAX,[RCX+0x10]");
static_assert(offsetof(Configuration, _map) + offsetof(RtIo::SettingsSet, _count) == 0x18, "MOV dword [RAX+0x08],EDX");
static_assert(offsetof(Configuration, _map) + offsetof(RtIo::SettingsSet, _pad0c) == 0x1c, "never written -- keep it named");
static_assert(offsetof(Configuration, _currentPlatform) == 0x20, "GetCurrentPlatform 0x1800c5270 MOV EAX,[RCX+0x20]");
static_assert(offsetof(Configuration, _currentLanguage) == 0x24, "GetCurrentLanguage 0x1800c4c30 MOV EAX,[RCX+0x24]");
static_assert(offsetof(Configuration, _currentBuild)    == 0x28, "GetCurrentBuild 0x1800c52f0 MOV EAX,[RCX+0x28]");
static_assert(offsetof(Configuration, _pad2c)       == 0x2c, "never written -- keep it named");
static_assert(!std::is_polymorphic<Configuration>::value,
              "System::Configuration must never acquire a vtable: retail has "
              "none, and a vptr would push every offset above by eight under a "
              "constructor that writes them as literals");
static_assert(std::is_standard_layout<Configuration>::value,
              "the doors reach it through a reinterpret_cast from void*");

// ============================================================================
// WHAT DID NOT CONVERT — FOUR ROWS, EACH FOR ITS OWN STATED REASON
// ============================================================================
//
// **1. THE CONSTRUCTOR, `??0Configuration@System@@QEAA@XZ` @ 0x1800c51b0.**
// CONVERTED (B7 LX, D7a): the class declares and defines it (inline, above);
// the `RtSystem_Configuration_Ctor` door that wrote the six slots by hand
// is deleted with its one caller (Engine::Startup, engine_lifecycle.cpp).
static_assert(std::is_default_constructible<Configuration>::value,
              "??0Configuration (0x1800c51b0) is the class's constructor");
static_assert(!std::is_trivially_destructible<Configuration>::value,
              "the CLASS owns ??1Configuration@System@@ (0x1800c5170) -- if "
              "this ever reads trivial, the destructor has been deleted and the "
              "??1 door clears nothing");
//
// **2. `?CurrentBuildId@Configuration@System@@SA?AVString@2@XZ` @ 0x1800c4c60.**
// The one ported `SA` static that is NOT declared here, and the reason is a
// live question about the ABI rather than a gap. Retail calls `String::Set` on
// the hidden return slot **WITHOUT zeroing it first**, so `Set`'s own release
// runs on whatever the caller left there; every other String-returning body in
// this class and in `System::DateTime` placement-constructs the slot first. A
// by-value member plus a placement-newing door would zero the slot instead —
// which is either the faithful reading (MSVC inlined `Set`, proved `data` null
// and folded the store away) or a behaviour change on a slot the caller did not
// zero. `value_types.cpp` already recorded the second reading and decided
// "left exactly as retail spells it"; that decision is not overridden on
// inference. The body stays the registered free door it is, and the conversion
// is OWED once someone reads 0x1800c4c60's prologue and settles which it is.
//
// **3. `?_singleton@Configuration@System@@0PEAV12@EA` @ 0x1801fc6f8.** A
// `CUTOVER_REQUIRED` row (`cutover_manifest.inc:77`) answered by a minted
// storage cell, read today through `RT_DATA_SLOT` at
// `engine_lifecycle.cpp:283`. Making it a real `static` member REDIRECTS that
// cell at `standalone/rt_data_mint.cpp`'s `MintStorageCell()` — the B.3.3
// `?_firstTypeInfo@BaseTypeInfo@Object@@` precedent — and `standalone/` is
// another agent's file set this step. **RULE B4b-4**: the FUNCTIONS convert and
// the STATIC stays an `RT_DATA_SLOT` read. The debt is tabled per row in
// B34B_RULES §4.1 and is owed at the class-static conversion, end of B.3.
//
// **4. THE 22 UNPORTED EXPORTS.** `?Startup@` 0x1800c4c50, `?Shutdown@`
// 0x1800c4c40, `?IsCommandSet@` 0x1800c5410, `?AddCommandLine@` 0x1800c5200,
// `?Get@Configuration@System@@SAPEAV12@XZ` 0x18000da70, the Language / Platform
// / Build accessors and the three ResourceParameter members. None has a body in
// this tree and none is registered; an uncalled port is an unverified port.
// Addresses are in B34B_RULES §1.1 for whoever ports them.
//
// The config map's find at **0x18002ace0** is unexported and takes the MAP
// (`configuration + 0x10`), not `this`: it is the Set's own `Find` (B7 LX:
// `RtSystem_ConfigMap_Find` is deleted, every caller calls `_map.Find`).
// ============================================================================

}  // namespace System

// ==== B3R W0-P0c: Runtime bodies reached by retail address ====
// ENDGAME B.3R (B3R_RULES R5): each body below is registered at the retail
// address in its trailing comment and read through the registry today. It is
// declared here, copied from its definition (linkage, namespace, types), so the
// reader can call it by name. Nothing calls it through this declaration yet.
namespace System { class String; }       // system/String.h

// ---- system/value_types.cpp
System::String* RtSystem_Configuration_CurrentBuildId(System::String* ret);  // 0x1800c4c60
// ==== end B3R W0-P0c ====
