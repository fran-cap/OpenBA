// ============================================================================
// src/engine/system/configuration.cpp — Wave 9 lane ENG, iteration 2
// System::Configuration::SetCommandLine + the string-keyed BST find it shares
// ============================================================================
//
// Attempt 08 got past the Engine wall and stopped exactly where lane ENG
// predicted:
//
//   [RtUnresolved] ?SetCommandLine@Configuration@System@@QEAAXAEBVString@2@@Z
//   Fault: EXEC of 0000000000000000
//     #1  RtEngine_Engine_Startup +0x17a
//
// This file closes that row and the one behind it.
//
//   | Export                                        | Retail      | Ord  |
//   |-----------------------------------------------|-------------|------|
//   | ?SetCommandLine@Configuration@System@@QEAAX…   | 0x1800c47a0 | 3849 |
//   | (unexported) the config map's BST find         | 0x18002ace0 |  --  |
//
// Both names checked against Runtime-x64.dll's export directory with pefile
// before a line was written. No new Win32 API and no new import library — this
// file makes no OS call at all.
//
// ---------------------------------------------------------------------------
// 1. WHAT `SetCommandLine` IS
// ---------------------------------------------------------------------------
// `System::Configuration` (0x30 bytes, ctor at 0x1800c51b0, defined in
// system/Configuration.h) holds a string→string map at +0x10. Retail:
//
//     int pos = 0;                                   // [RBP+0x48]
//     if (cmd.data == nullptr) return;               // 0x1800c47c7
//     int len = StringLength(cmd.data);              // MOV R14D,[RAX-0x8]
//     if (len <= 0) return;                          // 0x1800c47d7  JLE
//     do {
//         String key = ParseCommand(pos, cmd);       // 0x1800c4a00, ord 3310
//         if (!key.data || StringLength(key.data) == 0) { ~key; return; }
//         String val = ParseValue(pos, cmd);         // 0x1800c4930, ord 3311
//         Insert(&this->_map, &node, key, val);      // 0x18002a090 (unexported)
//         ~val; ~key;
//     } while (pos < len);
//
// Note `[RAX-0x8]` is the LENGTH word and `[RAX-0x4]` is the refcount
// (src/engine/system/String.h). Both appear in this function and mixing them
// up would silently turn the loop's exit test into a refcount test.
//
// ---------------------------------------------------------------------------
// 2. THE THREE CALLEES, AND WHY THE LOOP BODY NEVER RUNS ON AN EMPTY LINE
// ---------------------------------------------------------------------------
// `ParseCommand` (0x1800c4a00, ord 3310) and `ParseValue` (0x1800c4930, ord
// 3311) are ported below, and the insert at 0x18002a090 is
// `rtavl::StringMapInsert` (src/engine/collection/avl_trees.cpp). On an empty
// command line the loop body never runs:
//
//   src/app/main.cpp:1107   Game::RunGameMain(hInstance, lpCmdLine ? lpCmdLine : "")
//
// `String::Set(&tmp, "")` allocates a 9-byte block with `length = 0` at
// `data-8` and `refcount = 1` at `data-4` (src/engine/system/string.cpp), so
// `cmd.data` is NON-null and `len == 0`, and retail's own `JLE` at 0x1800c47d7
// returns before `ParseCommand` is reached.
//
// ---------------------------------------------------------------------------
// 3. THE BST FIND (0x18002ace0) IS THE SET'S `Find`
// ---------------------------------------------------------------------------
// B7 LX (D7a): `_map` is `Collection::Set<Pair<String,String>>`
// (RtIo::SettingsSet, io/Persistence.h) and 0x18002ace0 -- 50 instructions,
// two inlined SIGNED strcmps (`MOVSX` at 0x18002ad31 / 0x18002ad7f), the
// asymmetric null arms (a null node key descends right, a null search key
// returns the node), left +0x18 / right +0x20 -- is that template's `Find`
// over its `char*` key, arm for arm. The free port `RtSystem_ConfigMap_Find`
// and its `TreeStrCmp` (the same body as Collection's `AvlStrCmpSigned`) are
// deleted; every caller calls `_map.Find(key._data)`.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <cstdio>

#include "system/String.h"
#include "system/Configuration.h"   // ENDGAME B.3.4b SYSTEM (4c)
#include "io/Persistence.h"         // RtIo::SettingsSet, the map at +0x10
#include "system/string_internal.h"
#include <new>
#include "../collection/avl_trees.h"            // B3R: rtavl::StringMapInsert
#include "collection/Collection.h"  // Collection::Array / Set (the two members)


// ?_singleton@Configuration@System@@0PEAV12@EA  0x1801fc6f8 — the class static
// (ENDGAME B.3.9 SYS, B39_RULES D7).
System::Configuration* System::Configuration::_singleton = nullptr;

// ============================================================================
// ENDGAME B.3.4b SYSTEM (4c) — the `System::Configuration` member BODIES.
// ============================================================================
// Defined at GLOBAL scope with qualified names rather than inside a
// `namespace System { ... }` block — rule B4b-5. Every body is the one that
// used to sit in the
// door of the same name, moved VERBATIM; the edits are `self + 0xNN` becoming a
// named field, the hidden return slots becoming ordinary by-value returns, and
// the POD bridges that the class parameters make unnecessary.
// ============================================================================

void System::Configuration::SetCommandLine(const System::String& cmd) {

    RtIo::SettingsSet* map = &_map;

    // ENDGAME B.3.4b: the B.3.1 step-2 bridge is gone from this body — `cmd` IS
    // the class now, because the parameter is.
    const System::String& src = cmd;

    int pos = 0;
    char* d = src._data;
    if (d != nullptr) {
        const int len = src.GetLength();
        if (len > 0) {
            for (;;) {
                // The inner scope ends exactly where retail's two ~String calls
                // are (0x1800c4838 / 0x1800c4841), so the destructors fire
                // there — val then key, retail's own order — and BEFORE the
                // `pos >= len` test, not after it.
                {
                    // System::Configuration::ParseCommand (0x1800c4a00), by
                    // name (B3R); its by-value result constructs `key`.
                    System::String key(System::Configuration::ParseCommand(pos, cmd));

                    // GetLength (0x1800063c0) returns 0 for a null buffer, so
                    // it IS retail's two-part null-or-empty test. The break
                    // runs ~String on the way out — retail's 0x1800c47f9.
                    if (key.GetLength() == 0)
                        break;

                    // ParseValue (0x1800c4930), the same way.
                    System::String val(System::Configuration::ParseValue(pos, cmd));

                    // 0x1800c4829. The insert is UNEXPORTED in retail. See §2:
                    // unreachable on an empty command line.
                    //
                    // 0x18002a090 is `rtavl::StringMapInsert`
                    // (src/engine/collection/avl_trees.cpp), the
                    // `Collection::Set<Pair<String,String>>` Map_Insert body,
                    // typed on the Set (`self + 0x10`, RtIo::SettingsSet).
                    RtIo::SettingsNode* node = nullptr;
                    rtavl::StringMapInsert(
                        map, &node,
                        &key._data, &val._data);
                }

                if (pos >= len) break;
            }
        }
    }

}

// ============================================================================
// 0x1800c4a00 / 0x1800c4930 — ParseCommand and ParseValue.  Wave 9 lane ADDR.
// ============================================================================
// The banner's §2 listed these two as DELIBERATELY ABSENT and named them as the
// next fault at cutover for anyone who launches with an argument. They are
// closed here.
//
//   ?ParseCommand@Configuration@System@@CA?AVString@2@AEAHAEBV32@@Z  ord 3310
//   ?ParseValue@Configuration@System@@CA?AVString@2@AEAHAEBV32@@Z    ord 3311
//
// Both names re-verified against Runtime-x64.dll's export directory with
// pefile before a line was written, and both addresses confirmed to be the
// export directory's own entries for those ordinals rather than a nearby label.
//
// ---------------------------------------------------------------------------
// THE SIGNATURE: `CA` STATICS RETURNING `String` BY VALUE
// ---------------------------------------------------------------------------
// `CA` is a private STATIC member, so there is no `this`. The return type is
// `?AVString@2@` — a class BY VALUE — so MSVC passes a hidden return slot as
// the first argument and returns that same pointer in RAX:
//
//     RCX = String* ret        (caller-allocated, uninitialised)
//     RDX = int*    pos        (the `AEAH`, an int&)
//     R8  = const String* src  (the `AEBV32@`)
//     RAX = ret                (0x1800c49df / 0x1800c4b36: MOV RAX,R14)
//
// The members below return `System::String` BY VALUE, so the compiler
// passes that hidden return slot and returns it in RAX itself.
//
// ---------------------------------------------------------------------------
// THREE DETAILS THAT ARE FAITHFUL AND MUST NOT BE "FIXED"
// ---------------------------------------------------------------------------
// 1. `GetLength` IS RE-CALLED ON EVERY ITERATION. Retail reloads it inside each
//    loop (0x1800c4989, 0x1800c49b9, ...) rather than hoisting it. Reproduced
//    literally. It cannot change under us — nothing here writes `src` — so this
//    is about shape, not behaviour, and hoisting it would be the first step of
//    a rewrite rather than a port.
//
// 2. THE `src[i]` READS AT 0x1800c4a92 AND 0x1800c4b08 ARE UNGUARDED, and that
//    is CORRECT, not a missing bounds check. Both can run with `i == length`.
//    A System::String's buffer is `length + 1` bytes with a NUL written by
//    every producer (src/engine/system/String.h), so `src[length]` is a legal read
//    of '\0' — which is neither '/' nor ' ' nor ':', so both tests fall the
//    right way. A guard here would not "make it safe", it would change the
//    result at end-of-string. CLAUDE.md's rule applies directly: retail's
//    unguarded deref is load-bearing.
//
// 3. THE EMPTY RETURN WRITES A NULL `data` DIRECTLY. At 0x1800c4a98 retail
//    stores RBP (which is 0) into [R14] and jumps to the epilogue — it does NOT
//    call a String constructor. Reproduced as `return System::String()`, whose
//    default constructor stores a null `_data` and allocates nothing
//    (src/engine/system/String.h).
//
// ---------------------------------------------------------------------------
// DEPENDENCIES: BOTH ARE ALREADY PORTED
// ---------------------------------------------------------------------------
//   ?GetLength@String@System@@QEBAHXZ        0x1800063c0 ord 2750 — String.h
//   ?SubString@String@System@@QEBA?AV12@HH@Z 0x1800c9230 ord 4124 — string.cpp
// They are called here as members of `System::String`.
//
// `SubString`'s end index is INCLUSIVE, which is why both bodies pass
// `pos - 1` / `end - 1`, not `pos`.
// ============================================================================

namespace {

// Retail's blank test, inlined at four sites across the two functions.
inline bool IsBlank(char c) { return c == ' ' || c == '\t'; }

// (B.4 Z LAST IO: the unused oracle helper `SameString` is gone.)

}  // namespace

// ?ParseValue@Configuration@System@@CA?AVString@2@AEAHAEBV32@@Z  0x1800c4930
//
// Skip blanks, then take the run up to the next blank.
System::String System::Configuration::ParseValue(int& pos, const System::String& src) {
    const int posIn = pos;

    // ENDGAME B.3.4b: the B.3.1 step-2 bridges are gone — `src` IS the class
    // and the return is an ordinary by-value return. `GetLength` is
    // re-evaluated per iteration, as retail's 0x1800063c0 call in the loop head
    // is.
    const System::String& s = src;

    while (pos < s.GetLength() && IsBlank(s._data[pos]))
        ++pos;

    const int start = pos;                                   // 0x1800c4992

    while (pos < s.GetLength() && !IsBlank(s._data[pos]))
        ++pos;

    return s.SubString(start, pos - 1);                      // inclusive
}

// ?ParseCommand@Configuration@System@@CA?AVString@2@AEAHAEBV32@@Z  0x1800c4a00
//
// Skip blanks, scan to '/', and if there is one take the run after it up to a
// blank or ':'. If there is no '/', return an empty String.
System::String System::Configuration::ParseCommand(int& pos, const System::String& src) {
    const int posIn = pos;

    // ENDGAME B.3.4b, exactly as ParseValue above.
    const System::String& s = src;

    while (pos < s.GetLength() && IsBlank(s._data[pos]))
        ++pos;

    while (pos < s.GetLength() && s._data[pos] != '/')
        ++pos;

    // 0x1800c4a92. Unguarded by design — detail 2 above. When the scan ran off
    // the end this reads the NUL, which is not '/', so the empty return is
    // taken, which is precisely what retail does.
    if (s._data[pos] != '/') {
        // detail 3: retail's `*ret = 0` IS the default ctor (0x180006760). It
        // is an empty prvalue now.
        return System::String();
    }
    ++pos;                                                   // step over the '/'

    while (pos < s.GetLength() && IsBlank(s._data[pos]))
        ++pos;

    const int start = pos;                                   // 0x1800c4ad2

    while (pos < s.GetLength() &&
           s._data[pos] != ' ' && s._data[pos] != ':')
        ++pos;

    // 0x1800c4b02. `end` is where the run stopped. The DELIMITER is CONSUMED
    // (pos = end + 1) but is NOT part of the returned string. Reading
    // src->data[end] is the second unguarded read of detail 2.
    const int end = pos;
    if (s._data[end] == ' ' || s._data[end] == ':')
        pos = end + 1;

    return s.SubString(start, end - 1);                      // inclusive
}

System::String System::Configuration::GetCommandLineString(
        const System::String& key) const {

    // The find takes the MAP, not `this` — `configuration + 0x10`, whose first
    // qword is the root — which is why it is not a member of this class.
    const RtIo::SettingsNode* node = _map.Find(key._data);           // 0x18002ace0

    // 0x1800c533b: the found node's VALUE, and the temporary's reference.
    char* temp = (node != nullptr) ? node->element.value._data : nullptr;
    if (temp != nullptr) ++RtSystem::StringRefCount(temp);

    // 0x1800c537f: the slot is zeroed FIRST and unconditionally, then adopted.
    // ENDGAME B.3.4b: the hidden slot is the compiler's business now, so the
    // zero is an ordinary default-constructed local. `Adopt` is String's own
    // private AddRef half, reached through the transitional accessor.
    System::String out;
    System::StringPrivateAccess::Adopt(out, temp);

    // 0x1800c53a9 / 0x1800c53d5: the temporary dies. Through the class's own
    // destructor, so the freeing branch's dangling-pointer asymmetry (String.h)
    // is reproduced rather than re-typed. `_data` is written directly and NOT
    // adopted, because retail's `++temp[-4]` above already took this reference.
    {
        System::String tmpObj;
        tmpObj._data = temp;
    }


    return out;
}

// ============================================================================
// ??1Configuration@System@@QEAA@XZ                       retail 0x1800c5170
// ============================================================================
// Wave 22 lane ERRCFG. Thirteen instructions, two of which are the whole body:
//
//   1800c5186  ADD  RCX,0x10
//   1800c518f  CALL 0x18000f7d0        ; Set<Pair<String,String>>::Clear(this+0x10)
//   1800c5195  MOV  RCX,RBX
//   1800c519d  JMP  0x180010ab0        ; ~Array(+elem)(this)      -- TAIL CALL
//
// The name was checked against the export directory with pefile before a line
// was written: `??1` is the destructor and `QEAA` is the NON-VIRTUAL spelling,
// and it is ordinal 556 at 0x1800c5170. `??1...UEAA@XZ` does not exist in this
// DLL and would have resolved NULL in silence
// (reference_phantom_dtor_mangling_qeaa_vs_ueaa.md).
//
// NOT PRESCRIBED, READ. `reference_a_written_recipe_can_be_wrong.md` records a
// wave that ported two dtors exactly as prescribed and swapped a body that
// destroys EVERY element for one that destroys only the FIRST, so both callees
// were opened rather than assumed:
//
//   0x18000f7d0  loads [this]; on non-null it calls 0x18000f890 TWICE, once
//                with [node+0x18] and once with [node+0x20] -- i.e. it recurses
//                down BOTH subtrees -- then frees the node and zeroes
//                {ptr, int}. It is the whole container, not one element.
//   0x180010ab0  reads the COUNT from [this+0x8] and the base from [this] and
//                hands both to 0x180011010 (the per-element loop), then frees
//                the block under the allocator lock and zeroes {ptr, ptr}.
//                Again the whole array.
//
// THE THIRD SUB-OBJECT IS NOT DESTROYED, AND THAT IS RETAIL'S CHOICE.
// ??0Configuration (system/Configuration.h) initialises THREE {pointer, int}
// pairs -- at +0x00, +0x10 and +0x20. This dtor touches +0x10 and +0x00 and
// leaves +0x20 alone. That is a retail leak of the same family as the two
// already flagged inside Engine::Shutdown (Input::Device and Audio::Device),
// and it is reproduced rather than tidied: "fixing" it would free a block
// retail still considers live, which is strictly worse than the leak.
System::Configuration::~Configuration() {
    // B7 LX (D7a): the body is empty. The two calls retail makes are the two
    // members' destructors, which the compiler runs after this body in reverse
    // declaration order -- `_map` (+0x10): ~Set == Clear, 0x1800c518f CALL
    // 0x18000f7d0; then `_entries` (+0x00): ~Array<String>, 0x1800c519d JMP
    // 0x180010ab0.
}
