// ============================================================================
// src/engine/io/filesystem_setupdirs.cpp — Wave 9 lane FS
// IO::FileSystem::SetupDirectories — the cutover wall lane ENG named
// ============================================================================
//
//   | Export                                                    | Retail      | Ord  | Instrs |
//   |-----------------------------------------------------------|-------------|------|--------|
//   | ?SetupDirectories@FileSystem@IO@@QEAAXXZ                  | 0x1800abd00 | 4029 |  722   |
//   | ?GetRootDirectory@FileSystem@IO@@QEAA?AVFilename@System@@XZ| 0x1800ad650 | 2878 |   62   |
//
// Both names checked against Runtime-x64.dll's export directory with pefile
// before a body was written.
//
// This file was `_win` (`filesystem_setupdirs_win.cpp`, until Phase D.4 THREADS
// E.6) because both bodies call Win32 DIRECTLY: GetRootDirectory calls
// GetModuleFileNameW, and SetupDirectories calls GetFileAttributesW inside the
// FileSystem::Exists that MSVC inlined into it. They are
// Platform::ExecutablePath / PathAttributes now (platform_file.h). Both live in
// KERNEL32, which every link of this project already pulls in — **this lane
// adds NO new import library**; SA_LIBS is untouched.
//
// ---------------------------------------------------------------------------
// WHAT SetupDirectories ACTUALLY IS
// ---------------------------------------------------------------------------
// It computes the three path fields of IO::FileSystem and nothing else:
//
//     +0x00 sourceDirectory  = "<root><sub>..\resource\"
//     +0x08 dataDirectory    = "<root><sub><platform><build>\"
//     +0x10 packDirectory    = dataDirectory
//
// where `<root>` is the directory the running module sits in (with its
// trailing '\'), `<sub>` is "" unless the command line carries `-bin`, and
// `<platform>`/`<build>` are "Win"/"Data" for the shipped enums. Each of the
// three can then be overridden wholesale by a `-pathres` / `-pathdat` /
// `-pathpkg` switch. Before all that it reads `<root>cmd.txt`, if it exists,
// and feeds the file's contents to Configuration::SetCommandLine — a response
// file.
//
// The literals were read with `read_memory`, not guessed:
//   0x180155698 "cmd.txt"   0x1801551f0 ""          0x1801556a0 "bin"
//   0x1801556a8 "..\resource"            0x1801556b8 "%s%s%s\"
//   0x1801556c0 "%s%s%s%s\"  0x1801556d0 "pathres"  0x1801556d8 "pathdat"
//   0x1801556e0 "pathpkg"
// and the reconstruction is confirmed on disk: "%s%s%s%s\" with
// ("<install>\", "", "Win", "Data") is "<install>\WinData\", which is exactly
// the directory the shipped game has.
//
// ---------------------------------------------------------------------------
// THE THREE ARMS THAT ARE WRITTEN OUT BUT ARE **NOT REACHED** ON THIS BOOT
// ---------------------------------------------------------------------------
// The triage rule says a port only CLOSES a row if every callee it needs is
// satisfied. Nine of SetupDirectories' thirteen callees were unported when
// this lane started. Five are ported by this lane; the other four are NOT, and
// each is behind a branch that is provably not taken here. Stating that
// precisely is the whole reason this row can close at all:
//
//  1. `?ExistsInPackfile@FileSystem@IO@@QEBA_N...` (0x1800ab180, ord 2322).
//     Guarded by `this->existsInPackfileGate` (+0x18). The FileSystem
//     constructor (??0FileSystem@IO@@QEAA@XZ, 0x1800ac7c0) writes
//     `MOV word ptr [RCX+0x18],0x100` — i.e. **+0x18 = 0** and +0x19 = 1 —
//     and NOTHING sets +0x18 before SetupDirectories runs: its only writer on
//     this path is FileSystem::Startup, which sets it AFTER the call
//     (src/engine/io/filesystem_startup.cpp). So the packfile arm is dead
//     here and the platform arm (GetFileAttributesW) is the live one.
//
//  2/3/4. `?OpenFile@...` (0x1800ac880), `?ReadFile@...` (0x1800ace00),
//     `?CloseFile@...` (0x1800accc0), and the unexported memset at
//     0x1800881b0. All four are inside `if (Exists("<root>cmd.txt"))`. There
//     is no cmd.txt in the install. (B3R RT-IO: OpenFile / ReadFile /
//     CloseFile / SetCommandLine are direct member calls now.)
//
//  5. `?GetCommandLineString@Configuration@System@@QEBA?AVString@2@AEBV32@@Z`
//     (0x1800c5300, ord 2495) — four call sites, every one behind a
//     `Map_Find(Configuration::_map, <key>)`. Lane ENG established that
//     `SetCommandLine` is handed "" by src/app/main.cpp and returns before
//     its parse loop, so the map is EMPTY and all four finds miss. **Pass a
//     command line and this becomes the next unresolved symbol** — the same
//     condition lane ENG attached to the SetCommandLine row, inherited here.
//
// ---------------------------------------------------------------------------
// THE ONE PLACE THIS DIFFERS FROM RETAIL, AND WHY
// ---------------------------------------------------------------------------
// Retail loads `Configuration::_singleton` with an unguarded
// `MOV R13,[0x1801fc6f8]` and later does `LEA RCX,[R13+0x10]`. With the DLL
// absent that variable does not exist unless the mint provides it, and
// `?_singleton@Configuration@System@@0PEAV12@EA` (ord 4803) was NOT in
// standalone/rt_data_mint_tables.inc when this lane started — lane ENG's
// attempt-08 prediction Q5 named it. This lane regenerates the mint (the
// generator picks the symbol up from the source scan now that two files spell
// it as a literal), which is the real fix; the `if (cfg)` guards below are the
// belt to that braces, and they exist ONLY because our standalone can be
// missing a variable retail always has. Every DEREFERENCE retail makes of a
// pointer it believes non-null stays unguarded.
//
// It costs nothing on the boot path even if the mint row were still missing:
// `GetPlatformName` and `GetBuildName` never read `this` at all (checked in
// the disassembly — RCX is not dereferenced in either body), and the four
// Map_Find calls would simply miss, which is the answer they give anyway on an
// empty command line.
//
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
//   the old DLL-era switch   removes THIS ENTIRE LANE — SetupDirectories,
//                          GetRootDirectory, Filename::GetPathDirectory,
//                          Configuration::GetPlatformName/GetBuildName and all
//                          four System::WideString rows — in one variable, so
//                          the integrator can A/B the wall in one run instead
//                          of bisecting five files.
// ============================================================================

#include "platform_file.h"   // Platform::ExecutablePath / PathAttributes

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <new>        // placement new into the retail hidden return slot
#include <utility>    // std::move -- the pointer steal out of a POD out-param

#include "io/FileSystem.h"
#include "system/String.h"
#include "system/Filename.h"
#include "system/Configuration.h"   // ENDGAME B.3.9 SS1: Configuration::_singleton
#include "system/string_internal.h"
#include "system/widestring_internal.h"
#include "system/setupdir_helpers.h"

// ENDGAME B.3.1 step 2: the two Wave-2 lane-A entry points this file used to
// forward-declare here (`RtSystem_String_Format` 0x1800c9730 and
// `RtSystem_Filename_AdjustDelimiters` 0x1800c6970) are gone from this
// translation unit. Format is now `System::String::Format`, and every
// AdjustDelimiters call here was the tail of a mutating Filename entry point,
// which the class spells as its own ctor / operator= (Filename.h: "every
// mutating Filename entry point ends in a call to the private
// AdjustDelimiters").

namespace RtIO {

using RtSystem::StringRefCount;
using RtSystem::RtMemAlloc;
using RtSystem::RtMemFree;
using RtSystem::RtSystem_Configuration_GetPlatformName;
using RtSystem::RtSystem_Configuration_GetBuildName;

// IO::FileSystem::FileInfo now lives in src/engine/io/FileSystem.h.
// It moved there when Wave 9 lane PKG2 rung 3 needed it in a second
// translation unit -- two namespace-scope definitions with different
// members would have been a silent ODR violation, not a compile error.
//
// ONE FIELD WAS CORRECTED IN THE MOVE, and this file's own reader is why
// nothing broke: +0x08 was modelled here as a `uint64_t size`, and it is
// two dwords -- `size` at +0x08 and a READ CURSOR at +0x0C that ReadFile
// advances. The only use below is
// `static_cast<int>(static_cast<uint32_t>(fi.size))`, which takes the low
// dword and so happened to be right. The header carries the three
// independent instructions that settle it.

namespace {

// `?_singleton@Configuration@System@@0PEAV12@EA`, ord 4803, retail 0x1801fc6f8.
// ENDGAME B.3.9 SS1 (B39_RULES D7): the class static `::System::Configuration::_singleton`
// IS this cell now (name door, address door and this member are the same eight
// bytes) -- read fresh, no lazy cache, exactly as filesystem_startup.cpp reads it.
::System::Configuration* ConfigurationSingleton() {
    return ::System::Configuration::_singleton;
}

// ENDGAME B.3.1 step 2: the file-local `AssignInline` is gone. Retail's INLINED
// String::operator= appears seven times in SetupDirectories (0x1800abfe2,
// 0x1800ac0b2, 0x1800ac149, 0x1800ac1b4, 0x1800ac294, 0x1800ac3f3, 0x1800ac53f)
// and is now spelled as itself: `System::String::operator=` (0x1800c9810) is
// the same three steps in the same order, self-assignment test on the BUFFER
// rather than on the object included.
//
// Six of the seven are immediately followed by AdjustDelimiters on the same
// object, and that PAIR is `??4Filename@System@@QEAAAEAV01@AEBVString@1@@Z`
// (0x180026d40): assign, then normalise. Those sites assign to the
// FileSystem's `System::Filename` members.

bool ConfigHasKey(::System::Configuration* cfg, const char* key) {
    // ENDGAME B.3.9 SS1: the `cfg == nullptr` guard this used to carry was the
    // "belt" for the storage row not existing at all (see the header note);
    // that reason is gone now that `?_singleton@Configuration@System@@` is the
    // class static, so retail's genuinely-unguarded dereference is restored.
    System::String k(key);
    // Configuration + 0x10. Unexported (no name in either build): 0x18002ace0
    // is the settings Set's own `Find` (B7 LX).
    return cfg->_map.Find(k._data) != nullptr;
}

// The `-pathres` / `-pathdat` / `-pathpkg` override, which retail emits three
// times verbatim: look the key up, and if it is there rebuild the target as
// `Format("%s%s%s\\", root, sub, <the switch's value>)`.
void ApplyPathOverride(::System::Configuration* cfg, const char* key, System::Filename* target,
                       const char* rootData, const char* subData) {
    if (!ConfigHasKey(cfg, key)) return;

    System::String k(key);
    // The by-value return constructs `value` in place (retail's hidden
    // return slot), as the door that stood here did into a null String.
    System::String value(
        cfg->GetCommandLineString(k));

    // assign-then-AdjustDelimiters == Filename::operator=(const String&),
    // retail 0x180026d40. The three manual releases retail's compiler emitted
    // under this are the three destructors now.
    *target = System::String::Format("%s%s%s\\", rootData, subData, value._data);
}

}  // namespace

// ============================================================================
// ?GetRootDirectory@FileSystem@IO@@QEAA?AVFilename@System@@XZ  retail 0x1800ad650
// ============================================================================
// "The directory the running module lives in, with its trailing backslash."
//
// Retail's `this` (RCX) is never dereferenced — the body only ever touches
// RDX, the hidden return slot. The parameter is kept because that is what the
// signature is, the same argument Wave 4 made for GetSaveDirectory.
//
// The 0x1000-wchar_t buffer is retail's (`MOV EAX,0x2040 ; CALL __chkstk`,
// then GetModuleFileNameW with a count of 0x1000). It is NOT pre-zeroed by
// retail and is not pre-zeroed here: GetModuleFileNameW NUL-terminates on
// success, and on failure retail reads whatever is there — faithfully
// reproduced rather than papered over, because a failure of
// GetModuleFileNameW on our own process is not a condition this port should
// be inventing behaviour for.
//
// The body is the member `IO::FileSystem::GetRootDirectory()`, at the bottom of
// this file, as is `IO::FileSystem::SetupDirectories()`.

}  // namespace RtIO

// ============================================================================
// `IO::FileSystem`'s member bodies for this file
// ============================================================================
// Defined outside `namespace RtIO` because a member of `IO::X` must appear in a
// namespace ENCLOSING the class (B34_BRIEFS section A.2); these bodies reach
// this translation unit's `RtIO`-scoped names — including the
// anonymous-namespace helpers — as `RtIO::X`. Every POD is spelled `RtIO::` or
// `RtSystem::` and every real class `System::` (B34_RULES section 7.2).
//
// NOTHING IN EITHER BODY CHANGED but the spelling of the receiver: same
// callees, same order, same branches, same destructor order in the epilogue,
// the same `if (cfg)` belt the file header explains and every dereference
// retail makes unguarded still unguarded.
namespace IO {

System::Filename FileSystem::GetRootDirectory() {

    Platform::PathChar buf[0x1000];
    Platform::ExecutablePath(buf, 0x1000);   // GetModuleFileNameW(NULL, buf, 0x1000)

    // B.4 Z MIRROR2 (`behaviour:`, known_port_issues row 580): the three
    // locals in declaration order, so scope exit releases them as retail's
    // epilogue does: f (0x1800ad6fa), utf8 (0x1800ad72f), ws LAST
    // (0x1800ad75c). ws is WideString(const wchar_t*) 0x18000a6e0 inlined
    // (null store 0x1800ad692, Set 0x1800caf40); utf8 is AsUTF8 0x1800cacd0's
    // return slot ([RSP+0x20]).
    const System::WideString ws(buf);
    System::String utf8 = ws.AsUTF8();

    // `Filename f = utf8;` — retail's inlined Filename(const String&) at
    // 0x18000c780: adopt the buffer (AddRef, no copy), then normalise.
    System::Filename f(utf8);

    // The by-value result. GetPathDirectory (0x1800c6880) returns a prvalue,
    // which C++17 materialises directly in the caller's slot — the DOOR's
    // placement-new above is retail's hidden-return-slot construction, and this
    // is still exactly one AddRef.
    System::Filename out(f.GetPathDirectory());

    // ~f, ~utf8, ~ws: the destructors, reverse declaration order.

    // Safe to replay: reads no FileSystem state and allocates a fresh buffer
    // every call, so ours and theirs never share a pointer and only the TEXT
    // can be compared — the same shape as GetSaveDirectory's oracle.
    return out;
}

// ============================================================================
// ?SetupDirectories@FileSystem@IO@@QEAAXXZ                 retail 0x1800abd00
// ============================================================================
void FileSystem::SetupDirectories() {

    // --- 0x1800abd28 -------------------------------------------------------
    // GetRootDirectory is retail's own member and is now ours, so this is the
    // direct member call retail's `CALL 0x1800ad650` is. The POD round trip
    // the free-function form needed (land in a `Filename`, steal into the
    // class) is gone; C++17 constructs the result straight into `root`, which
    // is the same single AddRef.
    System::Filename root(GetRootDirectory());

    // --- 0x1800abd2e: R13 = Configuration::_singleton ----------------------
    ::System::Configuration* const cfg = RtIO::ConfigurationSingleton();

    // --- 0x1800abd44: String cmdPath = root + "cmd.txt" --------------------
    // ??HSystem@@YA?AVString@0@AEBV10@PEBD@Z, 0x1800c92d0. Retail passes the
    // Filename straight in as a String, which is what AsString() spells.
    System::String cmdPath = root.AsString() + "cmd.txt";

    // --- 0x1800abd4a-0x1800abe02: `if (Exists(Filename(cmdPath)))`, with
    //     FileSystem::Exists inlined by MSVC. See the header note: the +0x18
    //     arm is dead because the constructor leaves that byte at 0.
    bool exists;
    {
        // adopt-then-AdjustDelimiters == Filename(const String&), 0x18000c780.
        System::Filename f(cmdPath);

        if (_existsInPackfileGate != 0 &&
            ExistsInPackfile(f)) {
            exists = true;
        } else if (_platformExistsGate == 0) {
            exists = false;
        } else {
            // ??0WideString(const String&) (0x1800cab50) over the Filename read
            // AS a String; ~WideString (0x18000a730) at the end of this arm is
            // the inlined release retail has right after the call.
            const System::WideString ws(f.AsString());
            // `CMP EAX,-1 ; SETNZ` — INVALID_FILE_ATTRIBUTES, not a HRESULT.
            exists = (Platform::PathAttributes(ws._data) != Platform::kInvalidPathAttributes);  // GetFileAttributesW
        }
        // ~f is retail's release at 0x1800abe02.
    }

    // --- 0x1800abe08-0x1800abf50: the cmd.txt response file ----------------
    // NOT REACHED on any boot of this project (there is no cmd.txt).
    if (exists) {
        // OpenFile into `fi` as its hidden return slot. The argument is
        // Filename(const String&) again, 0x18000c780, a temporary retail
        // releases immediately after the OpenFile call (0x1800abe6a): the end
        // of this full-expression.
        RtIO::FileInfo fi = OpenFile(System::Filename(cmdPath));

        if (fi.handle != RtIO::FileInfo::kHandleNone) {
            const int size = static_cast<int>(static_cast<uint32_t>(fi.size));
            const int n    = size + 1;
            char* buf = static_cast<char*>(  // LEFT: an alloc result; the tier-I shape does not match the RtMemAlloc spelling
                RtIO::RtMemAlloc(static_cast<uint64_t>(static_cast<int64_t>(n)), 4));
            std::memset(buf, 0, static_cast<size_t>(n));   // 0x1800881b0
            ReadFile(fi, size, buf);
            CloseFile(fi);

            System::String text(buf);
            cfg->SetCommandLine(text);
            RtIO::RtMemFree(buf);
            // ~text is retail's release at 0x1800abf4b.
        }
        // ~fi (FileInfo's dtor, reverse order: 0x1800abef2 then 0x1800abf23)
        // at the end of this block.
    }

    // --- 0x1800abf54-0x1800ac06a: `sub`, the "-bin" sub-directory ----------
    // Retail seeds it from the EMPTY literal at 0x1801551f0, not from the null
    // String: `String::Set(&sub, "")` allocates a zero-length buffer, so
    // `sub.data` is non-null and the three Format calls below can pass it raw.
    System::String sub("");
    if (RtIO::ConfigHasKey(cfg, "bin")) {
        System::String k("bin");
        System::String value(
            cfg->GetCommandLineString(k));
        sub = value;   // retail's inlined operator= at 0x1800ac0b2
    }

    // --- 0x1800ac06a-0x1800ac08b: platform and build names -----------------
    // Retail reads both enums as DWORDs out of the Configuration object
    // (`MOV R8D,dword [R13+0x20]` / `[R13+0x28]`). Both are zero on every boot
    // of this project — the constructor zeroes them and nothing writes them
    // before Engine::Startup step 3 — giving "Win" and "Data".
    const int platformId = cfg ? cfg->_currentPlatform : 0;                // +0x20
    const int buildId    = cfg ? cfg->_currentBuild : 0;                   // +0x28
    // Both return a String by value: each local IS the hidden return slot.
    System::String platform = RtIO::RtSystem_Configuration_GetPlatformName(cfg, platformId);
    System::String build    = RtIO::RtSystem_Configuration_GetBuildName(cfg, buildId);

    // --- 0x1800ac08c-0x1800ac0ef: sourceDirectory --------------------------
    // The four `%s` arguments are passed RAW, exactly as retail passes the
    // qwords. A null here would mean GetRootDirectory failed, which is an
    // upstream fault to find and not a thing to guard
    // (CLAUDE.md, "Don't 'fix' a faithful crash with a null check").
    // The three path fields are `System::Filename` members (B4 X-R3); the
    // String result is assigned through Filename::operator=(const String&).
    _sourceDirectory =
        System::String::Format("%s%s%s\\", root._data, sub._data, "..\\resource");

    // --- 0x1800ac119-0x1800ac186: dataDirectory ----------------------------
    _dataDirectory =
        System::String::Format("%s%s%s%s\\", root._data, sub._data,
                               platform._data, build._data);

    // --- 0x1800ac1b0-0x1800ac1f1: packDirectory = dataDirectory ------------
    // The String overload (0x180026d40), as before the members were typed.
    _packDirectory = _dataDirectory.AsString();

    // --- 0x1800ac1f1-0x1800ac5f0: the three command-line overrides ---------
    // `sub._data` is captured once by retail (RBX) and reused by all three.
    RtIO::ApplyPathOverride(cfg, "pathres", &_sourceDirectory,
                            root._data, sub._data);
    RtIO::ApplyPathOverride(cfg, "pathdat", &_dataDirectory,
                            root._data, sub._data);
    RtIO::ApplyPathOverride(cfg, "pathpkg", &_packDirectory,
                            root._data, sub._data);

    // --- 0x1800ac5f0-0x1800ac6b4: the epilogue, in retail's order ----------
    // Retail releases build, platform, sub, cmdPath, root here. Those five are
    // the five destructors now, and C++ runs them in exactly that order:
    // reverse of declaration, with the two moved-from PODs already null.

    // ---------------------------------------------------------------------
    // ORACLE
    // ---------------------------------------------------------------------
    // The shipped body is replayed onto a SCRATCH FileSystem carrying the same
    // two gate bytes, never onto `self`: it would otherwise overwrite the three
    // fields we just built and, on the cmd.txt arm, call SetCommandLine a
    // second time. Retail reads only +0x18/+0x19 out of `this` and writes only
    // +0x00/+0x08/+0x10, so a scratch object is a complete stand-in.
    //
    // THE PART THAT IS EASY TO GET WRONG, AND WAS. Retail's body dereferences
    // `[0x1801fc6f8]` — the DLL's OWN copy of Configuration::_singleton —
    // directly. That symbol IS minted (rt_data_mint_tables.inc:1561), so our
    // ported constructor writes OUR storage and the DLL's copy stays NULL
    // forever. A replay guarded on "is our slot populated" would therefore
    // always say yes and always fault INSIDE THE ORACLE, reading as a port
    // defect; a replay guarded on "is the DLL's copy populated" would be safe
    // but would NEVER FIRE, which is the same as having no oracle while
    // looking like having one (reference_recheck_must_match_its_own_proof.md).
    //
    // So the guard reads the DLL's slot RAW, and then LENDS it our pointer for
    // the duration of the replay and puts it back. That is a write into the
    // DLL's .data and it is deliberate and bounded: retail's SetupDirectories
    // only ever READS that pointer (a map find and two enum dwords), this
    // function runs exactly once per process on the single-threaded boot path
    // at Engine::Startup step 3, and the slot is restored on every exit from
    // the block. It is the difference between an oracle and a decoration.
}

}  // namespace IO
