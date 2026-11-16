#pragma once
// ============================================================================
// src/engine/ui/Sprite.h — UI::Sprite, a 0x10-byte VALUE TYPE
// ============================================================================
//
// ENDGAME B.3.6 block D (RULE B6-10): a REAL class under retail's name,
// `UI::Sprite`, with real constructors and a trivial copy. It is held BY VALUE
// in `ImageElement`, `LabelElement`, `ProgressElement`, `SliderElement`,
// `Dialog`, `Save::SaveLoad` and the save-slot record
// (src/engine/save/SaveLoad.h), returned BY VALUE through hidden retslots
// (`ImageElement::GetSprite`, `LabelElement::GetSprite`,
// `Dialog::GetBackgroundSprite`) and passed by `const Sprite&` to the four
// `SpriteContext::AddSprite`s — so it must stay trivially copyable, and it is.
//
// The two constructors that take a sheet are defined in src/engine/ui/sprite.cpp.
//
// Retail exports (B36_SLOTS.md section 6, all twelve declared below):
//   ??0Sprite@UI@@QEAA@XZ                                          0x18011ba10
//   ??0Sprite@UI@@QEAA@V?$ExternalPtr@VSpriteSheet@UI@@@Object@@AEBVString@System@@@Z
//                                                                  0x18011b9c0
//   ??0Sprite@UI@@QEAA@V?$ExternalPtr@VSpriteSheet@UI@@@Object@@H@Z 0x18011ba30
//   ??4Sprite@UI@@QEAAAEAV01@AEBV01@@Z   0x180030cb0  two qword copies (all 16
//                                        bytes, pad0C included) = the implicit
//                                        trivial copy assignment
//   ??8 / ??9                            0x180030c60 / 0x180030c80  qword +0x00
//                                        then dword +0x08, short-circuit
//   ?GetSpriteSheet@…                    0x180030ca0  RETSLOT (MOV [RDX],[RCX])
//   ?Height / ?Index / ?IsValid / ?Width 0x180030c10 / c30 / c40 / bf0
//   ?Serialize@Sprite@UI@@QEAAXAEAVSerializer@IO@@@Z  0x18011b980 — NOT PORTED
//     (ExternalPtr<SpriteSheet>::Serialize 0x180004590 on +0x00, then the
//     reader/writer's +0xa0 int slot on +0x08; nothing in the tree calls it).
//
// pad0C IS NEVER WRITTEN by any of the three ctors (only the qword at +0x00
// and the dword at +0x08 are stored). A diff oracle that memcmp's a freshly
// poisoned scratch object against a live one must stop at 0xC bytes. `operator=` DOES copy
// it (0x180030cb6), which the trivial copy reproduces.
//
// THE FIELD NAMES ARE THE ONES THE TREE ALREADY SPELLS (`spriteSheet`,
// `spriteIndex`, `pad0C`) and they stay PUBLIC: unconverted bodies in other
// blocks' files read them directly, and `spriteSheet` is a raw `SpriteSheet*` —
// an `Object::ExternalPtr<T>` is exactly one raw pointer with no refcount
// (External.h), so the bytes are identical. `GetSpriteSheet()` hands out the
// typed handle.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <type_traits>

namespace Object { template <class T> class ExternalPtr; }
namespace System { class String; }
namespace IO     { class Serializer; }
namespace UI     { class SpriteSheet; }

namespace UI {

class Sprite {
public:
    // 0x18011ba10 — `*(qword)+0 = 0; *(dword)+8 = -1`. pad0C untouched.
    Sprite() : spriteSheet(nullptr), spriteIndex(-1) {}

    // 0x18011b9c0 — store the sheet; index = sheet ? GetSpriteIndexFromName : -1.
    Sprite(Object::ExternalPtr<SpriteSheet> sheet, const System::String& name);

    // 0x18011ba30 — two plain stores, no branch.
    Sprite(Object::ExternalPtr<SpriteSheet> sheet, int32_t index);

    Sprite(const Sprite&) = default;
    // ??4 0x180030cb0 — two qword copies, i.e. the implicit trivial one.
    Sprite& operator=(const Sprite&) = default;

    // 0x180030c60 — the sheet qword, then the index dword; pad0C not compared.
    bool operator==(const Sprite& o) const {
        return spriteSheet == o.spriteSheet && spriteIndex == o.spriteIndex;
    }
    // 0x180030c80 — retail's own body, the same two compares with the result
    // inverted (not a call to ==).
    bool operator!=(const Sprite& o) const {
        return !(spriteSheet == o.spriteSheet && spriteIndex == o.spriteIndex);
    }

    // 0x180030ca0 — RETSLOT in retail; a plain by-value return here (not a
    // vtable slot, so both ends of every C++ call are this GCC). Defined in
    // src/engine/ui/sprite.cpp, which sees the complete ExternalPtr<T>.
    Object::ExternalPtr<SpriteSheet> GetSpriteSheet() const;

    // 0x180030bf0 / 0x180030c10 — `entry+0x1C` / `entry+0x20` of the sheet's
    // SpriteInfo, with NO upper-bound check (retail has none).
    int32_t Width() const;
    int32_t Height() const;

    // 0x180030c30 — `MOV EAX,[RCX+0x8] ; RET`.
    int32_t Index() const { return spriteIndex; }

    // 0x180030c40 — index first, then sheet (retail's short-circuit order).
    bool IsValid() const { return (spriteIndex >= 0) && (spriteSheet != nullptr); }

    // 0x18011b980 — declared, NOT PORTED (no caller in the tree; see banner).
    void Serialize(IO::Serializer& serializer);

    // ---- the 0x10 bytes -----------------------------------------------------
    SpriteSheet* spriteSheet; // +0x00  ExternalPtr<UI::SpriteSheet> (one raw pointer; B4 P2 H4: typed)
    int32_t spriteIndex;    // +0x08  index into the sheet (-1 = unset)
    int32_t pad0C;          // +0x0C  never written by a ctor; copied by operator=
};

static_assert(sizeof(Sprite) == 0x10, "UI::Sprite is 0x10 bytes");
static_assert(alignof(Sprite) == 8, "UI::Sprite aligns to 8 (the sheet pointer)");
static_assert(offsetof(Sprite, spriteSheet) == 0x00, "spriteSheet at +0x00");
static_assert(offsetof(Sprite, spriteIndex) == 0x08, "spriteIndex at +0x08");
static_assert(offsetof(Sprite, pad0C)       == 0x0C, "pad0C at +0x0C");
static_assert(std::is_trivially_copyable<Sprite>::value,
              "RULE B6-10: UI::Sprite is held by value and returned through "
              "hidden retslots; its copy must stay trivial");

}  // namespace UI

// Bytes a diff oracle may compare between two Sprite objects. pad0C (the last
// 4) is never written by any ctor -- see the note above.
inline constexpr size_t kSpriteWrittenBytes = 0x0C;

// RULE B6-4.2 transition alias: the bare spelling every src/engine/ui TU and
// ProgressElement.h / SliderElement.h still use.
using Sprite = UI::Sprite;
