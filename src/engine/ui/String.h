#pragma once
// ============================================================================
// src/engine/ui/String.h — UI::String, a 0x10-byte VALUE TYPE
// ============================================================================
//
// ENDGAME B.3.6 block D (RULE B6-10). Retail's name is `UI::String`; the tree
// spelled it `UIString` to dodge `System::String`, and `::UIString` stays as a
// transition alias (RULE B6-4.2) — `Element.h` spells `::UIString _toolTip`.
//
// A handle + index into a `UI::StringTable`: an `ExternalPtr<StringTable>`
// (one raw pointer, no refcount) and an int32 text index, plus 4 bytes nobody
// writes. Byte-for-byte the shape of `UI::Sprite`. NOT refcounted: SetToolTip
// copies exactly two qwords (Element.h banner). Held BY VALUE by
// `Element::_toolTip` (+0x48), TextElement, DocumentTextElement; the exe
// constructs it into caller storage through the ctor doors 63 times, and those
// doors (src/engine/ui/sprite.cpp) delegate here by placement-new.
//
// Retail exports (B36_SLOTS.md section 6, all eight declared below):
//   ??0String@UI@@QEAA@XZ                                       0x18011da40
//   ??0String@UI@@QEAA@V?$ExternalPtr@VStringTable@UI@@@Object@@AEBV0System@@@Z
//                                                               0x18011da60
//   ??0String@UI@@QEAA@V?$ExternalPtr@VStringTable@UI@@@Object@@H@Z 0x18011da20
//   ??4String@UI@@QEAAAEAV01@AEBV01@@Z  0x180035090  two qword copies = trivial
//   ??BString@UI@@QEBA?AV0System@@XZ    0x180035020  RETSLOT: table->GetText(index)
//   ?GetStringTable@…                   0x180035080  RETSLOT (MOV [RDX],[RCX])
//   ?Index@String@UI@@QEBAHXZ           0x180035010  MOV EAX,[RCX+8]
//   ?IsValid@String@UI@@QEBA_NXZ        0x180035060  table first, then index
//
// operator System::String reads `table` UNGUARDED and forwards to
// StringTable::GetText(int), whose own guard order (index < 0 returns empty
// WITHOUT touching `this`) is what makes a never-assigned `{null, -1}` harmless
// (sprite.cpp, the WAVE 10 LANE UIORACLE note). No guard is added here. The port
// calls the body as the static StringTable::GetTextOf(table, index), so a null
// table is not a member call on null (review F-009, undefined behaviour in C++).
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <type_traits>

namespace Object { template <class T> class ExternalPtr; }
namespace System { class String; }
namespace UI     { class StringTable; }

namespace UI {

class String {
public:
    // 0x18011da40 — table = null, index = -1; pad0C untouched.
    String() : table(nullptr), index(-1) {}

    // 0x18011da60 — table = t; index = t ? t->GetIndexFromName(name) : -1.
    String(Object::ExternalPtr<StringTable> t, const System::String& name);

    // 0x18011da20 — two plain stores.
    String(Object::ExternalPtr<StringTable> t, int32_t i);

    String(const String&) = default;
    // ??4 0x180035090 — two qword copies, i.e. the implicit trivial one.
    String& operator=(const String&) = default;

    // 0x180035020 — RETSLOT in retail; by value here (not a vtable slot).
    operator System::String() const;

    // 0x180035080 — RETSLOT in retail; by value here.
    Object::ExternalPtr<StringTable> GetStringTable() const;

    // 0x180035010
    int32_t Index() const { return index; }

    // 0x180035060 — TABLE first, then index (the opposite order from
    // Sprite::IsValid; both kept as retail wrote them).
    bool IsValid() const { return (table != nullptr) && (index >= 0); }

    // ---- the 0x10 bytes (public: other blocks' bodies read them) -----------
    StringTable* table; // +0x00  ExternalPtr<UI::StringTable> (null = no table; B4 P2 H4: typed)
    int32_t index;   // +0x08  index into the table (-1 = unset)
    int32_t pad0C;   // +0x0C  never written by any of the three ctors
};

static_assert(sizeof(String) == 0x10, "UI::String is 0x10 bytes");
static_assert(alignof(String) == 8, "UI::String aligns to 8");
static_assert(offsetof(String, table) == 0x00, "table at +0x00");
static_assert(offsetof(String, index) == 0x08, "index at +0x08");
static_assert(offsetof(String, pad0C) == 0x0C, "pad0C at +0x0C");
static_assert(std::is_trivially_copyable<String>::value,
              "RULE B6-10: UI::String is held by value (Element::_toolTip) and "
              "copied as two qwords; its copy must stay trivial");

}  // namespace UI

// RULE B6-4.2 transition alias: `Element.h` and three src/engine/ui TUs spell
// the bare name.
using UIString = UI::String;

// Bytes a diff oracle may compare between two UI::String objects -- pad0C is
// never written, same trap as Sprite.h's kSpriteWrittenBytes.
inline constexpr size_t kUIStringWrittenBytes = 0x0C;
