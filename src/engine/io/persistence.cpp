// ============================================================================
// src/engine/io/persistence.cpp — Wave 4 lane C: IO::Persistence
// ============================================================================
// The two `Get` overloads, the two `Set` overloads and `GetSavePath` of
// `IO::Persistence`. `ReadSettings`/`WriteSettings` are in
// persistence_settings.cpp.
//
// All five retail bodies were decompiled at their real addresses (verified
// non-phantom against the shipped DLL's export table, see
// docs/history/runtime_port/WAVE4_LANE_C.md) and are transcribed line-for-line
// below, including one documented deviation (see RetailValueIsTrue).
// ============================================================================

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <utility>
#include "io/Persistence.h"
#include "system/string_internal.h"
#include "../collection/avl_trees.h"          // rtavl::StringMapInsert

using RtIo::SettingsNode;

namespace {

// ---- Map_Find / Map_Insert -------------------------------------------------
// B7 LX (D7a): `_settingsMap` is the real `Collection::Set<Pair<String,
// String>>` (Persistence.h). Map_Find (0x18002ace0) IS its `Find` -- the
// members below call it on the key's data pointer, which is what retail's
// `MOV R10,[RBX]` re-reads every iteration. Map_Insert (0x18002a090) is
// rtavl::StringMapInsert (avl_trees.cpp), typed on the Set.
SettingsNode* CallMapInsert(RtIo::SettingsSet* map, const System::String& key,
                            const System::String& value) {
    SettingsNode* node = nullptr;
    rtavl::StringMapInsert(                                           // Map_Insert, retail 0x18002a090
        map, &node, &key._data, &value._data);
    return node;
}

// Retail's key encoding: bare concatenation, no separator. "Game" + "StatusIconEnable"
// -> "GameStatusIconEnable", exactly the live HKCU value name this project's
// own memory note (project_debugmenu_combos_and_statusicon_setting.md) found
// on disk. Confirmed at all four Get/Set retail sites (0x1800b81e0 + 0x22,
// 0x1800b8290 + 0x22, 0x1800b80c0 + 0x30, 0x1800b8380 + 0x38): every one calls
// `String::Format((String*)&local, "%s%s", section, key)` --
// ?Format@String@System@@SA?AV12@PEBDZZ, retail 0x1800c9730, which is
// System::String::Format (B3R RT-IO: called by name; the by-address resolve
// that stood here also answered with it, never "the DLL's own export").
System::String MakeSettingsKey(const char* section, const char* key) {
    return System::String::Format("%s%s", section, key);
}

// Retail's Get(bool&) (0x1800b8290) is an inlined strcmp against the literal
// "true": walk both strings until a mismatch or a NUL, then diff the bytes at
// the stopping position. That is exactly `strcmp(value, "true") == 0`.
//
// ONE DEVIATION, documented rather than silently "fixed": if the stored
// value's String has `data == nullptr` (only possible if something OTHER
// than Set(bool) — which always Formats a real, if zero-length, buffer —
// stored a null-data String as a bool setting's value), retail computes
// index -1 into the null pointer and reads `pcVar1[-1]`, an out-of-bounds
// read certain to fault. CLAUDE.md's "don't fix a faithful crash" rule is
// about retail RELYING on a value always being present; this is the reverse
// — an out-of-bounds read retail does not rely on, reachable by no known
// write path. Reproducing the exact byte read here would be equally
// undefined behaviour in our own binary, bought for a state nothing can
// currently create. Treated as false instead of replicated.
bool RetailValueIsTrue(const char* value) {
    if (value == nullptr) return false;
    return std::strcmp(value, "true") == 0;
}

}  // namespace

// ============================================================================
// bool IO::Persistence::Get(const char* section, const char* key, int& out)
// retail 0x1800b81e0 — read-only: Map_Find + a transient
// String::Format/MemFree pair, no persistent mutation.
// ============================================================================
// ENDGAME B.3.4 PACKAGE+FS commit 6: the five bodies in this file are members
// of the real `IO::Persistence`, below. No qualified `IO::` call is
// needed: `IO::Persistence` has no vtable of its own -- the vptr at +0x10 is
// the EMBEDDED worker's (src/engine/io/Persistence.h) -- so B34_BRIEFS
// section A.1's re-entry hazard cannot arise.
// B4 wave X round 6: the four `RtIo_Persistence_{Get,Set}{Int,Bool}(void*
// self, ...)` one-line forwarders that stood here are deleted; their callers
// (input.cpp Configuration::Save, keymap_save.cpp EmitSetting) call the
// members on a typed `IO::Persistence*`.

// ============================================================================
// `IO::Persistence`'s member bodies for this file
// ============================================================================
// Defined in `namespace IO` because a member of `IO::X` must appear in a
// namespace ENCLOSING the class (B34_BRIEFS section A.2). The file-local
// helpers live in this translation unit's GLOBAL anonymous namespace, so they
// are reached as `::MakeSettingsKey` / `::CallMapInsert` /
// `::RetailValueIsTrue` -- spelled with the `::` on purpose,
// so a future reader cannot mistake them for members.
//
// NOTHING ELSE CHANGED: same callees in the same order, the same asymmetric
// release-then-adopt on an existing node, the same documented RetailValueIsTrue
// deviation, the same "only write through the out reference when found"
// contract. Every POD is spelled `RtIo::` or `RtSystem::` and every real class
// `System::` (B34_RULES section 7.2) -- note this file's POD namespace is
// `RtIo`, LOWERCASE o.
namespace IO {

// ?_singleton@Persistence@IO@@0PEAV12@EA (0x1801fc6b8) -- the class static
// (ENDGAME B.3.9 IO, D7).
Persistence* Persistence::_singleton = nullptr;

// ??0Persistence@IO@@QEAA@XZ  retail 0x1800b8020 (B.4 wave X X7-RT). The three
// members' constructors are retail's stores in retail's order (Persistence.h
// quotes the sequence): the map's qword root + dword count, the embedded
// worker (??0Thread(0x10000), its vptr, its member stores), `_savePath`'s
// null data; then the singleton store, the body's one statement.
Persistence::Persistence() : _settingsMap(), _worker(), _savePath() {
    _singleton = this;                                           // 0x1800b809c
}

bool Persistence::Get(const char* section, const char* key, int& outRef) const {
    int* const out = &outRef;

    System::String k = ::MakeSettingsKey(section, key);
    const SettingsNode* n = _settingsMap.Find(k._data);              // Map_Find, retail 0x18002ace0
    bool ours = (n != nullptr);
    int oursVal = *out;   // retail only ever writes *param_3 when found; preserve the caller's value otherwise
    if (ours) {
        // FUN_18007f034 in retail is a bare tail-jump to CRT strtol(s, 0, 10)
        // — no need to call the DLL for it, this is not engine-specific.
        oursVal = static_cast<int>(std::strtol(n->element.value._data ? n->element.value._data : "", nullptr, 10));
    }
    // ~k is retail's release.

    if (ours) *out = oursVal;
    return ours;
}

// ============================================================================
// bool IO::Persistence::Get(const char* section, const char* key, bool& out)
// retail 0x1800b8290 — same shape as Get(int&).
// ============================================================================
bool Persistence::Get(const char* section, const char* key, bool& outRef) const {
    bool* const out = &outRef;

    System::String k = ::MakeSettingsKey(section, key);
    const SettingsNode* n = _settingsMap.Find(k._data);              // Map_Find, retail 0x18002ace0
    bool ours = (n != nullptr);
    bool oursVal = *out;
    if (ours) {
        oursVal = ::RetailValueIsTrue(n->element.value._data);
    }
    // ~k is retail's release.

    if (ours) *out = oursVal;
    return ours;
}

// ============================================================================
// void IO::Persistence::Set(const char* section, const char* key, int value)
// retail 0x1800b80c0 — MUTATES the shared live map.
// ============================================================================
void Persistence::Set(const char* section, const char* key, int value) {

    System::String valStr = System::String::Format("%i", value);
    System::String keyStr = ::MakeSettingsKey(section, key);

    SettingsNode* const node = ::CallMapInsert(&_settingsMap, keyStr, valStr);

    // Retail's asymmetric release-then-adopt, transcribed exactly
    // (0x1800b80c0 + 0x50..0x90): Map_Insert only installs the value on a
    // BRAND NEW node (its own internal refcounting already left node->value
    // == valStr in that case, so this branch is skipped); on an EXISTING
    // node Map_Insert leaves the old value untouched and Persistence::Set
    // itself must swap it. This IS `String::operator=` (0x1800c9810): the
    // same guard-on-buffer, release, adopt sequence.
    if (node->element.value._data != valStr._data) {
        node->element.value = valStr;
    }
    // ~keyStr, ~valStr are retail's two releases.
}

// ============================================================================
// void IO::Persistence::Set(const char* section, const char* key, bool value)
// retail 0x1800b8380 — same shape as Set(int), value formatted as "true"/"false".
// ============================================================================
void Persistence::Set(const char* section, const char* key, bool value) {

    System::String valStr = System::String::Format("%s", value ? "true" : "false");
    System::String keyStr = ::MakeSettingsKey(section, key);

    SettingsNode* const node = ::CallMapInsert(&_settingsMap, keyStr, valStr);

    // Same shape as Set(int) above -- `String::operator=`.
    if (node->element.value._data != valStr._data) {
        node->element.value = valStr;
    }
    // ~keyStr, ~valStr are retail's two releases.
}

// ============================================================================
// System::String IO::Persistence::GetSavePath() const
// retail 0x180007ad0 — a pure read (a ref-counted copy of a field) with no
// mutation.
// ============================================================================
System::String Persistence::GetSavePath() const {

    // `String(const String&)` (retail 0x180006cb0) is exactly the "adopt
    // savePath's buffer, AddRef if non-null" sequence the C body wrote out by
    // hand; returning it by value is retail's hidden-return-slot
    // construction, so this is still exactly one AddRef.
    return System::String(_savePath);
}

}  // namespace IO

