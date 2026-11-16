#pragma once
// ============================================================================
// src/engine/ui/Cursor.h — UI::Cursor, 0x30 bytes, 9 slots
// ============================================================================
//
// ENDGAME B.3.6 block D. No header existed for this class before (RULE B6-8):
// the layout is MOVED here from the offsets src/engine/ui/cursor.cpp's ported
// bodies read, and NAMED by the three exported getters, read as retail
// disassembly this pass:
//
//   ?GetHotSpotX@Cursor@UI@@QEBAHXZ     0x18003b260  MOV EAX,[RCX+0x20] ; RET
//   ?GetHotSpotY@Cursor@UI@@QEBAHXZ     0x18003b270  MOV EAX,[RCX+0x24] ; RET
//   ?GetSpriteSheet@Cursor@UI@@QEBA?AV?$ExternalPtr@VSpriteSheet@UI@@@Object@@XZ
//                                       0x18003b250  MOV RAX,[RCX+0x28] ;
//                                                    MOV [RDX],RAX ; MOV RAX,RDX
//                                                    — a hidden RETSLOT
//
//   sizeof 0x30   `MOV EDX,0x30` in its own slot-0 vector deleting destructor
//                 (0x18003b3a0) — B36_SLOTS.md section 1.
//   parent        "Object::External" (`?_ParentTypeName@Cursor@UI@@` 0x18003af50).
//
// +0x20 / +0x24 are NOT written by the constructor (the seven inlined stores in
// Construct/Reconstruct end with `[RCX+0x28] = 0`); Serialize fills them. And
// ~Cursor does NOT release +0x28 (cursor.cpp's banner). Both are retail.
//
// `??_7Cursor@UI@@6B@` @ 0x180156570, NINE slots (B36_SLOTS.md):
//   [0] +0x00 0x18003b3a0  (unexported) vector deleting dtor — the class's own (B.3.9)
//   [1] +0x08 0x18003af80  ?TypeName@Cursor@UI@@UEBAPEBDXZ
//   [2] +0x10 0x18003af60  ?Type@Cursor@UI@@UEBAIXZ
//   [3] +0x18 0x1800cd1b0  ?GetFileVersion@Cursor@UI@@UEBAHXZ  (MOV EAX,1)
//   [4] +0x20 0x1800066e0  ?Compile@External@Object@@  INHERITED — NOT declared
//   [5] +0x28 0x1800cd110  ?Serialize@Cursor@UI@@UEAA_NAEAVStream@IO@@@Z
//   [6] +0x30 0x18003b180  ?Reconstruct@Cursor@UI@@UEAAPEAVExternal@Object@@XZ
//   [7] +0x38 0x18003b240  ?Destruct@Cursor@UI@@UEAAXXZ
//   [8] +0x40 0x18003b1e0  ?Construct@Cursor@UI@@UEAAPEAVExternal@Object@@XZ — B6-1.1
//
// ENDGAME B.3 step 9 (block UI-D, B39_RULES D1/D2): CONCRETE, the
// CharacterSet.h shape: the real public ctor is the inlined one (seven stores
// in Construct 0x18003b1e0, the last `[RCX+0x28] = 0`), slot 0 the class's own
// vector deleting dtor, ~Cursor virtual and EMPTY (retail's is a
// restamp and a tail jump to ~External — the ExternalPtr at +0x28 is NOT
// released). The factory pair is the class's own (cursor.cpp), replacing the
// FACTUI1 row; the minted `??_7Cursor@UI@@6B@` and its RT_DELDTOR row are
// retired.
// ============================================================================

#include <cstdint>
#include <cstddef>

#include "../object/External.h"
#include "../object/Internal.h"   // Object::detail::InternalMemFree

namespace UI {

class SpriteSheet;

class Cursor : public Object::External {
public:
    // The REAL constructor: External() (+0x08..+0x18 zero), then +0x28 = 0 —
    // the ExternalPtr's own default ctor. +0x20 / +0x24 are NOT written
    // (retail; Serialize fills them).
    Cursor() : Object::External() {}

    // [0] 0x18003b3a0 — MSVC's VECTOR deleting destructor, both arms exactly as
    // the retired RT_DELDTOR row ran them (`MOV EDX,0x30`, the element size).
    ~Cursor() override;  // 0x1800cd1c0: empty; the compiler chains ~External

    // ENDGAME B.3.9 TI-UI: ONE reflection block (B39 D6); `_typeInfo` is the
    // class's own record (DEFINE_TYPE in the class's TU), no longer minted.
    // retail: TypeName 0x18003af80, Type 0x18003af60, CreateNewObject 0x18003b070, CreateInPlaceObject 0x18003b120
    DECLARE_TYPE(Cursor)
public:
    int32_t     GetFileVersion() const override;        // [3] 0x1800cd1b0
    // [4] 0x1800066e0 is Object::External::Compile, inherited unchanged.
    bool        Serialize(IO::Stream& stream) override;       // [5] 0x1800cd110
    Object::External* Reconstruct() override;           // [6] 0x18003b180
    void        Destruct() override;                    // [7] 0x18003b240
    virtual Object::External* Construct();              // [8] 0x18003b1e0 — B6-1.1

    // ---- the non-slot exported surface (read this pass, see the banner) ----
    int32_t GetHotSpotX() const { return _hotSpotX; }   // 0x18003b260
    int32_t GetHotSpotY() const { return _hotSpotY; }   // 0x18003b270
    // 0x18003b250 — RETSLOT in retail; a plain by-value return here (not a
    // vtable slot, so both ends of every C++ call are this GCC).
    Object::ExternalPtr<SpriteSheet> GetSpriteSheet() const { return _spriteSheet; }

    // ---- exported, NOT PORTED, no caller: declared, deliberately undefined --
    Cursor(const Cursor& src);                          // 0x18003b280
    Cursor& operator=(const Cursor& src);               // 0x18003b310
    // 0x18003af70 — TypeId (DECLARE_TYPE).
    // Placement is spelt `::new (p) Cursor()` (B9-5): these stay undefined.
    static void* operator new(std::size_t bytes);              // 0x18003af90
    static void* operator new(std::size_t bytes, void* place); // 0x18003b000
    static void  operator delete(void* p) { ::Object::detail::InternalMemFree(p); }                     // 0x18003b010
    // Retail's factory pair (B39_RULES D2), defined and registered in cursor.cpp.

public:
    // ---- the 0x10 bytes Cursor adds (public) ------------------------------
    int32_t _hotSpotX;                              // +0x20  GetHotSpotX; ctor leaves it
    int32_t _hotSpotY;                              // +0x24  GetHotSpotY; ctor leaves it
    Object::ExternalPtr<SpriteSheet> _spriteSheet;  // +0x28  GetSpriteSheet; ctor zeroes
};

static_assert(sizeof(Cursor) == 0x30,
              "UI::Cursor is 0x30 bytes (MOV EDX,0x30 in slot 0, 0x18003b3a0)");
static_assert(alignof(Cursor) == 8, "UI::Cursor aligns to 8 (the vptr)");
static_assert(offsetof(Cursor, _hotSpotX)    == 0x20, "GetHotSpotX 0x18003b260 reads +0x20");
static_assert(offsetof(Cursor, _hotSpotY)    == 0x24, "GetHotSpotY 0x18003b270 reads +0x24");
static_assert(offsetof(Cursor, _spriteSheet) == 0x28, "GetSpriteSheet 0x18003b250 reads +0x28");
static_assert(offsetof(Cursor, _hotSpotX) == sizeof(Object::External),
              "Cursor's own bytes begin right after Object::External");

}  // namespace UI
