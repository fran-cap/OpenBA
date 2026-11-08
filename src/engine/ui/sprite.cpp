// ============================================================================
// src/engine/ui/sprite.cpp — Wave 7 lane F — Sprite + SpriteSheet +
//                              SpriteContext + StringTable + UI::String
// ============================================================================
//
// Ports 20 of the 23 exports this lane owns (PLAN.md §4.2 wave 7, §6). Every
// mangled name below was checked against the shipped export directory of
// C:\GOG Games\Banished\Runtime-x64.dll with a pefile-style reader BEFORE any
// body was written (reference_resolveexport_phantom_names.md); all 23 exist.
// The full write-up — the oracle argument per function, the SpriteInfo layout
// correction, and everything held back — is
// docs/history/runtime_port/WAVE7_LANE_F.md.
//
//   | Export (short form)                    | Retail      | Ported | Oracle  |
//   |-----------------------------------------|-------------|--------|---------|
//   | Sprite::Sprite()                         | 0x18011ba10 | yes    | replay  |
//   | Sprite::Sprite(sheet,name)                | 0x18011b9c0 | yes    | replay  |
//   | Sprite::Sprite(sheet,index)                | 0x18011ba30 | yes    | replay  |
//   | Sprite::IsValid()                        | 0x180030c40 | yes    | replay  |
//   | Sprite::Width()                          | 0x180030bf0 | yes    | replay  |
//   | Sprite::Height()                         | 0x180030c10 | yes    | replay  |
//   | SpriteSheet::SpriteSheet()                | 0x18011d600 | yes    | replay  |
//   | SpriteSheet::GetSpriteCount()             | 0x180003190 | yes    | replay  |
//   | SpriteSheet::GetSpriteIndexFromName()      | 0x1800031f0 | yes    | replay  |
//   | SpriteSheet::GetUVs()                     | 0x180003330 | yes    | replay  |
//   | SpriteSheet::SetSpriteInfo()               | 0x180003460 | yes    | scratch |
//   | SpriteSheet::AddSpriteInfo()                | 0x180003390 | yes    | scratch |
//   | SpriteContext::AddLine()                   | 0x18011cbe0 | NO     | —       |
//   | SpriteContext::SetScissor(Viewport const&)  | 0x18011d050 | NO     | —       |
//   | SpriteContext::SetScissor(int,int,int,int)  | 0x18011d010 | NO     | —       |
//   | StringTable::GetCount()                    | 0x180002610 | yes    | replay  |
//   | StringTable::GetText(int)                  | 0x180002550 | yes    | replay* |
//   | StringTable::GetText(String const&)         | 0x1800025b0 | yes    | replay* |
//   | UI::String::String()                       | 0x18011da40 | yes    | replay  |
//   | UI::String::String(table,name)              | 0x18011da60 | yes    | replay  |
//   | UI::String::String(table,index)             | 0x18011da20 | yes    | replay  |
//   | UI::String::IsValid()                      | 0x180035060 | yes    | replay  |
//   | UI::String::operator System::String()       | 0x180035020 | yes    | replay* |
//
// ---------------------------------------------------------------------------
// A CORRECTION THIS LANE MADE TO ITS OWN INHERITED HEADER
// ---------------------------------------------------------------------------
// src/engine/ui/SpriteSheet.h's SpriteInfo layout was wrong before this
// lane touched it: width/height were guessed at +0x18/+0x1C and +0x20..+0x2C
// were guessed to be UV floats. The real layout (proven by AddSpriteInfo,
// SetSpriteInfo, GetUVs and Sprite::Width/Height all agreeing) is width
// +0x1C, height +0x20, and the four UVs at +0x24/+0x28/+0x2C/+0x30, with a
// previously-unnoticed always-zero int32 at +0x18. See the header for the
// full evidence; nothing in the tree read the old (wrong) fields, so this
// was a latent defect, not a live one.
//
// ---------------------------------------------------------------------------
// WHAT STAYS ON THE DLL, AND WHY (docs/history/runtime_port/WAVE7_LANE_F.md §2)
// ---------------------------------------------------------------------------
// AddLine and both SetScissor overloads all reach unexported, unreversed
// internal helpers (`Flush`, `FlushDeferredFonts`, `AddSpriteDraw`,
// `SpriteCapture::AddScissor`) that submit into the LIVE Video::Context /
// GPU vertex buffer this SpriteContext wraps — state that lives outside the
// SpriteContext object, so a scratch copy of the object does not isolate a
// replay the way it does for SetSpriteInfo/AddSpriteInfo. Running the DLL's
// body a second time for comparison would submit a second, real, visible
// draw or scissor change. See src/engine/ui/SpriteContext.h for the
// partial layout this lane read while making that argument.
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>


#include "object/External.h"
#include "ui/Sprite.h"
#include "ui/SpriteSheet.h"
#include "ui/StringTable.h"
#include "ui/String.h"
#include "system/String.h"
#include "system/MemoryAllocator.h"
#include "system/string_internal.h"   // RtString_StrCompare, for GetIndexFromName
#include "../collection/array_internal.h" // ENDGAME B.3.2 step 2 — kSpec_Sprite38

// ----------------------------------------------------------------------------
// Module / ResolveAddr / ResolveExport, forward-declared rather than pulled in
// from standalone/resolve.h — same reasoning rt_port.h gives for declaring
// RegisterRuntimeExport locally: resolve.h drags in <windows.h>, which PLAN.md
// §4.1 keeps out of src/engine/. These declarations must stay byte-identical
// to resolve.h's; the linker checks them against it. (Precedent:
// src/engine/achievement/achievement_manager.cpp:69-71.)
// ----------------------------------------------------------------------------

namespace {

// ----------------------------------------------------------------------------
// SpriteSheet::GetSpriteIndexFromName's algorithm (retail 0x1800031f0),
// shared by the Sprite-by-name ctor. Retail's inner loop is a hand-rolled
// "compare lengths, then compare bytes up to the first mismatch and subtract"
// that is behaviourally strcmp for two null-terminated buffers of EQUAL
// length (which is the only case it ever reaches the byte loop in) — a plain
// strcmp() reproduces the same sign and the same zero/nonzero verdict, which
// is all GetSpriteIndexFromName's caller (an equality test against -1 or the
// returned index) can observe. Simplification noted rather than transcribing
// retail's manual byte walk instruction for instruction.
// ----------------------------------------------------------------------------
int SpriteIndexFromName(const SpriteInfo* data, int32_t count, const char* key) {
    const int32_t keyLen = key ? RtSystem::StringLength(key) : 0;
    for (int32_t i = 0; i < count; ++i) {
        const char* name = data[i].name._data;
        const int32_t nameLen = name ? RtSystem::StringLength(name) : 0;
        if (nameLen != keyLen) continue;
        if (name == nullptr || nameLen == 0) return i;   // covers keyLen==0 too (lengths already match)
        if (key != nullptr && std::strcmp(name, key) == 0) return i;
        // key == nullptr falls through to "no match, keep scanning": retail's
        // iVar6=1 path. Unreachable in practice (keyLen==0 forces nameLen==0,
        // handled above), kept for exact parity with retail's control flow.
    }
    return -1;
}

}  // namespace

// ============================================================================
// UI::Sprite — ENDGAME B.3.6 block D (RULE B6-10)
// ============================================================================
//
// The bodies are the class's real constructors and members now
// (src/engine/ui/Sprite.h). Each registered DOOR keeps its name and
// placement-news / delegates onto them: the exe and the unconverted bodies
// construct Sprites INTO CALLER STORAGE through the ctor doors, so the doors
// stay until the caller batches convert those sites to C++ construction.

namespace UI {

// Sprite::Sprite(ExternalPtr<SpriteSheet>, String const& name) — retail
// 0x18011b9c0. `sheet` arrives as one raw pointer (ExternalPtr<T>, External.h);
// the name is read through its data pointer, the String's one field.
Sprite::Sprite(Object::ExternalPtr<SpriteSheet> sheet, const System::String& name)
    : spriteSheet(sheet.Get()) {
    if (spriteSheet != nullptr) {
        spriteIndex = spriteSheet->GetSpriteIndexFromName(name);
    } else {
        spriteIndex = -1;
    }
}

// Sprite::Sprite(ExternalPtr<SpriteSheet>, int index) — retail 0x18011ba30.
// Two plain stores, no branch.
Sprite::Sprite(Object::ExternalPtr<SpriteSheet> sheet, int32_t index)
    : spriteSheet(sheet.Get()), spriteIndex(index) {}

// ?GetSpriteSheet@Sprite@UI@@ — retail 0x180030ca0 (`MOV RAX,[RCX]; MOV [RDX],RAX`).
Object::ExternalPtr<SpriteSheet> Sprite::GetSpriteSheet() const {
    return Object::ExternalPtr<SpriteSheet>(spriteSheet);
}

// Sprite::Width() const — retail 0x180030bf0. NO upper-bound check on
// spriteIndex, exactly as retail has none (an out-of-range positive index
// reads whatever follows the array in memory) — CLAUDE.md: do not add a
// guard retail lacks.
int32_t Sprite::Width() const {
    if (spriteIndex < 0) return 0;
    const auto* sheet = spriteSheet;
    return sheet->m_sprites._data[spriteIndex].width;
}

// Sprite::Height() const — retail 0x180030c10. Same no-bound-check note.
int32_t Sprite::Height() const {
    if (spriteIndex < 0) return 0;
    const auto* sheet = spriteSheet;
    return sheet->m_sprites._data[spriteIndex].height;
}

}  // namespace UI

// ============================================================================
// UI::SpriteSheet — the non-virtual members (ENDGAME B.3.6 block D). The class
// is src/engine/ui/SpriteSheet.h; its slots are in spritesheet_vtable.cpp.
// ============================================================================

// SpriteSheet::SpriteSheet() — retail 0x18011d600: the REAL constructor
// (SpriteSheet.h). ENDGAME B.3.9 UI-D retired its door `RtUi_SpriteSheet_Ctor`;
// every construction spells `::new (p) UI::SpriteSheet()`.

namespace UI {

// SpriteSheet::GetSpriteCount() const — retail 0x180003190.
int32_t SpriteSheet::GetSpriteCount() const { return m_sprites._size; }

// SpriteSheet::GetSpriteIndexFromName(String const&) const — retail 0x1800031f0.
int32_t SpriteSheet::GetSpriteIndexFromName(const System::String& name) const {
    return SpriteIndexFromName(m_sprites._data, m_sprites._size, name._data);
}

// SpriteSheet::GetSpriteNameFromIndex(int) const — retail 0x1800032d0.
//
// WAVE 10 LANE UI4. Reached by SpriteCapture::AddSprite. Eighteen instructions,
// and NO bounds check of any kind: the index is scaled straight into
// `m_sprites` by `IMUL RAX,RAX,0x38`. Same faithfulness call as GetUVs below,
// and CLAUDE.md's rule — do not add a guard retail lacks.
//
// RETSLOT in retail: the door keeps (this, hidden-return-slot, index) and
// placement-news this member's prvalue into the slot.
System::String SpriteSheet::GetSpriteNameFromIndex(int32_t index) const {
    System::String out;                        // 0x1800032f4, null before the load
    char* name = m_sprites._data[index].name._data;
    if (name) {
        RtSystem::StringRefCount(name) += 1;   // 0x180003302
        out._data = name;
    }
    return out;
}

// SpriteSheet::GetUVs(int, float&, float&, float&, float&) const — retail
// 0x180003330. NO upper-bound check (only `index < 0` is guarded), matching
// retail exactly — CLAUDE.md: do not add a guard retail lacks.
void SpriteSheet::GetUVs(int32_t index, float& u0, float& v0, float& u1, float& v1) const {
    float ou0 = 0.0f, ov0 = 0.0f, ou1 = 0.0f, ov1 = 0.0f;
    if (index >= 0) {
        const SpriteInfo& e = m_sprites._data[index];
        ou0 = e.u0; ov0 = e.v0; ou1 = e.u1; ov1 = e.v1;
    }
    u0 = ou0; v0 = ov0; u1 = ou1; v1 = ov1;
}

// SpriteSheet::SetSpriteInfo(int,int,int,float,float,float,float) — retail
// 0x180003460. Mutates ONE existing SpriteInfo entry in place; no allocation,
// no refcounted field touched. No bound check on `index`, matching retail.
void SpriteSheet::SetSpriteInfo(int32_t index, int32_t width, int32_t height,
                                float u0, float v0, float u1, float v1) {
    SpriteInfo& e = m_sprites._data[index];
    e.width = width; e.height = height;
    e.u0 = u0; e.v0 = v0; e.u1 = u1; e.v1 = v1;
}

// SpriteSheet::AddSpriteInfo(String const&,String const&,int,int,int,int) —
// retail 0x180003390. B4 Y RT-UI: retail's local is a real SpriteInfo
// (disassembly): the 0x34 bytes zeroed (0x1800033a8..0x1800033c0), x / y /
// width / height stored, then each name copied in with its refcount bump
// (`if (src) { slot = src; INC [src-4] }`, 0x1800033d9 name, 0x1800033e8
// group), the append through 0x180004e30 (which bumps again for the stored
// element), and the local's two releases at the end, group (+0x08) first,
// then name (0x18000340a, 0x18000342b). That is this local's construction,
// two String assignments onto null names, and its implicit destructor at the
// closing brace. (The tree used to skip the local's bump / release pair; the
// net count was the same, the sequence was not retail's.)
void SpriteSheet::AddSpriteInfo(const System::String& name, const System::String& group,
                                int32_t x, int32_t y, int32_t width, int32_t height) {
    const int32_t oldCount = m_sprites._size;

    SpriteInfo newEntry{};
    newEntry.name        = name;          // 0x1800033d9  null slot: adopt + bump
    newEntry.group       = group;         // 0x1800033e8
    newEntry.x           = x;
    newEntry.y           = y;
    newEntry._reserved18 = 0;
    newEntry.width       = width;
    newEntry.height      = height;
    newEntry.u0 = newEntry.v0 = newEntry.u1 = newEntry.v1 = 0.0f;
    newEntry._pad34      = 0;

    // The array mechanics (grow-if-needed, copy-construct with refcount bump,
    // free the old buffer) are Collection::Array<SpriteInfo>'s append-by-one
    // body. `m_sprites` + `m_sprites._size` + `m_spriteCapacity` (+0x20/+0x28/
    // +0x2C) are the {data,size,capacity} triple `Collection::Array<SpriteInfo>`
    // would use, so the call bridges at the use site. `kSpec_Sprite38`
    // (0x180004e30) is NON-TRIVIAL — a String refcount bump in its element
    // assign — so it stays the hand-derived `ExplicitSpec`.
    m_sprites.SetSizeFill(oldCount + 1, &newEntry);
}

}  // namespace UI

// ============================================================================
// UI::StringTable — the non-virtual members (ENDGAME B.3.6 block D). The class
// is src/engine/ui/StringTable.h; its slots are in stringtable_factory.cpp.
// ============================================================================

namespace UI {

// StringTable::GetCount() const — retail 0x180002610: `return *(int*)(this+0x28)`.
int32_t StringTable::GetCount() const { return _entries._size; }

// StringTable::GetText(int) const — retail 0x180002550. Bounds-checked both
// ways (unlike Sprite/SpriteSheet's UV accessors); returns an empty String on
// out-of-range, a NEW OWNED reference on a hit.
// *** WAVE 10 LANE UIORACLE — A REAL PORT DEFECT, FIXED HERE. THE LOADS WERE
// HOISTED ABOVE THE GUARDS. ***
//
// The first draft read `data` (this+0x20) and `count` (this+0x28) at the top and
// then tested `index >= 0 && index < count`. Retail does NOT: its very first
// instruction pair after the prologue is
//
//     180002569  MOV dword ptr [RSP],R9D
//     18000256c  TEST R8D,R8D
//     18000256e  JNS  0x180002573          ; index >= 0 ?
//     180002570  MOV  qword ptr [RDX],R9   ; else *ret = 0
//     180002573  ...                       ; RET -- `this` NEVER DEREFERENCED
//
// so on a NEGATIVE index retail returns an empty String **without touching
// `this` at all**, and only then does it read this+0x28 and this+0x20.
//
// That is not a hypothetical difference. A `UI::String` that names no text is
// `{table = nullptr, id = -1}`, and `UI::String::operator System::String`
// (0x180035020) forwards it here verbatim (`MOV R8D,[RCX+8]; MOV RCX,[RCX]`).
// Retail's guard order makes that harmless; the hoisted loads made it an
// ACCESS_VIOLATION reading 0x20. (As a member this means `this` may be null
// on that path; the body never compares or dereferences it there.)
//
// AND THIS IS NOT THE "don't fix a faithful crash with a null check" case
// (CLAUDE.md). No guard is being ADDED: retail's guards are in retail's ORDER.
//
// NOT RETAIL (review F-009, undefined behaviour): in C++ a member call through a
// null pointer is undefined even when the body never reads `this` (only
// -fno-delete-null-pointer-checks held it up under Clang). The body is the static
// GetTextOf, which takes the table as a plain pointer, so `{null, -1}` reaches it
// defined; retail's guard order and results are unchanged, and no check is added.
System::String StringTable::GetTextOf(const StringTable* table, int32_t index) {
    System::String out;                                          // data == null
    if (index >= 0) {                                            // 0x18000256c
        if (index < table->_entries._size) {                     // 0x180002573
            char* text = table->_entries._data[index].text._data;      // 0x180002585
            if (text) {
                RtSystem::StringRefCount(text) += 1;
                out._data = text;
            }
        }
    }
    return out;
}

System::String StringTable::GetText(int32_t index) const {
    return GetTextOf(this, index);
}

// ?GetIndexFromName@StringTable@UI@@QEBAHAEBVString@System@@@Z  retail 0x180002470
//
// A linear scan of the table's TextEntry array (stride 0x10, data at +0x20,
// count at +0x28 — StringTable.h), comparing `entry.name` against `key`. Two
// details from the listing that a paraphrase loses:
//
//  * retail pre-checks the STORED LENGTHS (`name[-8]` vs `key[-8]`, with a
//    null string counting as length 0) and skips the strcmp entirely when they
//    differ — 0x18000249f..0x1800024b6. Same shortcut String::operator== uses.
//  * once the lengths agree, a table entry whose name is null OR whose length
//    is zero MATCHES IMMEDIATELY (0x1800024b8..0x1800024c2), without looking at
//    the key at all. Since the lengths already agree, the key is empty too, so
//    the answer is right — but it means an empty key finds the first empty
//    slot rather than failing, and that is retail's behaviour, not a shortcut
//    introduced here.
//
// Returns -1 when nothing matches.
int32_t StringTable::GetIndexFromName(const System::String& keyString) const {
    const char* key = keyString._data;
    const int keyLen = (key != nullptr) ? RtSystem::StringLength(key) : 0;

    int result = -1;
    for (int i = 0; i < _entries._size; ++i) {
        const char* name = _entries._data[i].name._data;
        const int nameLen = (name != nullptr) ? RtSystem::StringLength(name) : 0;
        if (nameLen != keyLen) continue;
        if (name == nullptr || nameLen == 0) { result = i; break; }
        if (key == nullptr) continue;                 // retail: ECX = key+1, non-zero
        if (RtSystem::RtString_StrCompare(name, key) == 0) { result = i; break; }
    }
    return result;
}

// StringTable::GetText(String const& key) const — retail 0x1800025b0. A
// two-line forwarder in retail: `GetText(this, retval, GetIndexFromName(key))`.
System::String StringTable::GetText(const System::String& name) const {
    return GetText(GetIndexFromName(name));
}

}  // namespace UI

// ============================================================================
// UI::String — ENDGAME B.3.6 block D (RULE B6-10); retail's `UI::String`,
// the tree's `UIString`. Same shape as UI::Sprite above.
// ============================================================================

namespace UI {

// UI::String::String(ExternalPtr<StringTable>, String const& name) — retail
// 0x18011da60. StringTable::GetIndexFromName is a member (above).
String::String(Object::ExternalPtr<StringTable> t, const System::String& name)
    : table(t.Get()) {
    if (table != nullptr) {
        index = table->GetIndexFromName(name);
    } else {
        index = -1;
    }
}

// UI::String::String(ExternalPtr<StringTable>, int index) — retail
// 0x18011da20. Two plain stores.
String::String(Object::ExternalPtr<StringTable> t, int32_t i)
    : table(t.Get()), index(i) {}

// ?GetStringTable@String@UI@@ — retail 0x180035080 (`MOV RAX,[RCX]; MOV [RDX],RAX`).
Object::ExternalPtr<StringTable> String::GetStringTable() const {
    return Object::ExternalPtr<StringTable>(table);
}

// UI::String::operator System::String() const — retail 0x180035020. A
// ONE-LINE forwarder: `return table->GetText(index)`, with `table` read
// UNGUARDED — StringTable::GetText(int) tests `index < 0` BEFORE it touches
// `this`, which is what makes a never-assigned `{null, -1}` harmless (the
// WAVE 10 LANE UIORACLE note on RtUi_StringTable_GetText_Index). No null check
// retail lacks (CLAUDE.md). NOT RETAIL (review F-009): the call goes through the
// static StringTable::GetTextOf, not a member call on the possibly-null `table`.
String::operator System::String() const {
    return StringTable::GetTextOf(table, index);
}

}  // namespace UI

// ---- the doors --------------------------------------------------------------

// WAVE 9 LANE UIF — src/engine/ui/spritecontext.cpp.

// ============================================================================
// ============================================================================
// 0x1800034F0  UI::SpriteSheet::Set(int, int, String const&, String const&, bool)
//              ord 3819.  WAVE 23 LANE TAIL
// ============================================================================
//
// Five parameters, and the mangling `XHHAEBVString@System@@0_N` is what pins
// their order: int, int, String const&, String const& (the `0` is MSVC's
// back-reference meaning "same type as the previous argument"), bool.
//
// THE ARGUMENT MAPPING WAS READ OFF THE PROLOGUE, not assumed from the
// mangling, because two of the five arrive on the stack. Retail saves four
// registers into the shadow space, pushes R14 and subtracts 0x20, so the frame
// has moved 0x28 by the time the stack arguments are read:
//
//   RCX          -> this
//   EDX  -> R14D -> p1 (int)     stored to +0x48 at 0x1800035B1
//   R8D  -> EBP  -> p2 (int)     stored to +0x4C at 0x1800035A6
//   R9   -> RDI  -> p3 (String&) assigned to +0x38
//   [RSP+0x50]   -> p4 (String&) assigned to +0x40   (caller slot +0x28 = arg5)
//   [RSP+0x58]   -> p5 (bool)    stored to +0x50 at 0x1800035AE (caller +0x30)
//
// NOTE THE ORDER IS NOT SOURCE ORDER: the body assigns +0x40 from p4 FIRST
// (0x1800034FF..0x180003558) and +0x38 from p3 SECOND. Reproduced.
//
// THIS ALSO NAMES THREE FIELDS src/engine/ui/SpriteSheet.h left as
// `m_field48` / `m_field4C` / `m_field50` "unknown". They are precisely this
// function's two ints and its bool. The header is NOT renamed here - a name
// needs more evidence than "the one setter that writes it", and this lane has
// no reader to corroborate against. Recorded in the lane report instead.
//
// ---------------------------------------------------------------------------
// THE STRING ASSIGN IDIOM, AND THE QUIRK IN IT
// ---------------------------------------------------------------------------
// `System::String` is a bare `char*` into a refcounted block: the refcount is
// the dword at [data-4] and the allocation base is [data-8], which is the same
// layout spritecapture.cpp's `StringRc` / `ReleaseString` helpers already use.
// Retail's member-assign, per string (0x180003519..0x180003558):
//
//   old = this->field;
//   if (old != *src) {                       ; pointer identity short-circuit
//       if (old) {
//           if (--*(int32_t*)(old - 4) == 0) {
//               this->field -= 8;            ; *** the field itself is adjusted
//               MemFree(this->field);        ; *** and THEN passed to MemFree
//           } else {
//               this->field = nullptr;       ; *** see below
//           }
//       }
//       this->field = *src;
//       if (this->field) ++*(int32_t*)(this->field - 4);
//   }
//
// TWO THINGS HERE ARE DELIBERATE AND WOULD LOOK LIKE TRANSCRIPTION ERRORS:
//
//  * THE `else this->field = nullptr;` BRANCH (0x180003542 / 0x180003580).
//    When the refcount is still non-zero retail NULLS the member before
//    overwriting it two instructions later. The store is dead in every
//    single-threaded reading of this function. It is reproduced because it is
//    what the bytes do, and because a reader diffing this against the
//    disassembly must not find a "simplification" they then have to re-derive.
//  * THE FREE GOES THROUGH THE MEMBER, NOT A LOCAL. Retail writes
//    `ADD qword ptr [this+0x40],-8` and then loads that same member back as
//    MemFree's argument. Same value as `MemFree(old - 8)` would be, but it
//    leaves the member transiently pointing at the allocation base, and this
//    body reproduces the sequence rather than the shortcut.
//
// The only callee is `?MemFree@MemoryAllocator@System@@SAXPEAX@Z`
// (0x1800C7AD0, confirmed by pefile against the export directory), which this
// file already reaches through its existing MemFree door. No other call.
// ---------------------------------------------------------------------------
// B4 X7-RT: both behaviours above are exactly `System::String::operator=`
// (string.cpp: the buffer guard, Release() storing `data - 8` into the member
// before MemFree or nulling it, the adopt and AddRef), so with the two members
// typed the two call sites below are plain assignments.

namespace UI {

void SpriteSheet::Set(int32_t p1, int32_t p2, const System::String& p3,
                      const System::String& p4, bool p5) {
    // p4 -> +0x40 FIRST, then p3 -> +0x38. Retail's order, not source order.
    // Both are `String::operator=` (0x1800c9810) inlined: the guard on the
    // buffer, the release through the member, the adopt and AddRef.
    m_groupName   = p4;
    m_textureName = p3;

    // The three scalars, in retail's store order: +0x4C, then +0x50, then +0x48.
    m_imageHeight = p2;
    m_padForFiltering = p5;
    m_imageWidth = p1;
}

}  // namespace UI
